"""Execute selected original TZ local-vTable routines with explicit host boundaries."""
import argparse
import hashlib
import hmac
import io
import json
import os
import stat
from pathlib import Path
import struct
import sys

import unicorn
from unicorn import arm64_const as R
from elftools.elf.elffile import ELFFile

IMAGE_SHA = "f6317e33e3daf280d39a7912bb103e8a8840e0130e6ff1a4b158920e5e7e3fa2"
TLS = 0x40001000
CTX = 0x40010000
CFG = 0x40020000
TA = 0x40050000
NAME = 0x40060000
OUT = 0x40031000
STOP = 0x40000000
HEAP = 0x40090000
TEST_ID = b"qcom-vp-local-state-test-identity".ljust(48, b"\0")


class DiskStore:
    """Map firmware names to exclusive durable files in one store directory."""
    def __init__(self, root):
        self.root = Path(root)
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        self.root_fd = os.open(self.root, flags)
        if not stat.S_ISDIR(os.fstat(self.root_fd).st_mode):
            os.close(self.root_fd)
            raise RuntimeError("store path is not a directory")

    def _name(self, firmware_name):
        return hashlib.sha256(firmware_name.encode("utf-8")).hexdigest()

    def _entry(self, name):
        try:
            info = os.stat(name, dir_fd=self.root_fd, follow_symlinks=False)
        except FileNotFoundError:
            return None
        if not stat.S_ISREG(info.st_mode):
            raise RuntimeError("store entries must be regular files")
        return info

    @staticmethod
    def _same_file(left, right):
        return left is not None and right is not None and (left.st_dev, left.st_ino) == (right.st_dev, right.st_ino)

    def _read_entry(self, name, expected):
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=self.root_fd)
        try:
            opened = os.fstat(fd)
            if not stat.S_ISREG(opened.st_mode) or not self._same_file(expected, opened):
                raise RuntimeError("store entry changed during read")
            with os.fdopen(fd, "rb") as stream:
                fd = -1
                return stream.read()
        finally:
            if fd >= 0:
                os.close(fd)

    def get(self, firmware_name, default=None):
        name = self._name(firmware_name)
        entry = self._entry(name)
        return default if entry is None else self._read_entry(name, entry)

    def __getitem__(self, firmware_name):
        name = self._name(firmware_name)
        entry = self._entry(name)
        if entry is None:
            raise KeyError(firmware_name)
        return self._read_entry(name, entry)

    def __contains__(self, firmware_name):
        return self._entry(self._name(firmware_name)) is not None

    def __len__(self):
        names = os.listdir(self.root_fd)
        for name in names:
            if len(name) != 64 or any(char not in "0123456789abcdef" for char in name):
                raise RuntimeError("store contains an unexpected entry")
            self._entry(name)
        return len(names)

    def _sync_dir(self):
        os.fsync(self.root_fd)

    def __setitem__(self, firmware_name, content):
        name = self._name(firmware_name)
        temporary = name + ".pending"
        original = self._entry(name)
        if self._entry(temporary) is not None:
            raise RuntimeError("store has an unfinished temporary file")
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
        fd = os.open(temporary, flags, 0o600, dir_fd=self.root_fd)
        owned = os.fstat(fd)
        try:
            stream = os.fdopen(fd, "wb")
            fd = -1
            with stream:
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
            temporary_entry = self._entry(temporary)
            if not self._same_file(owned, temporary_entry):
                raise RuntimeError("temporary store file changed before replace")
            current = self._entry(name)
            if (original is None) != (current is None) or (original and not self._same_file(original, current)):
                raise RuntimeError("store target changed before replace")
            os.replace(temporary, name, src_dir_fd=self.root_fd, dst_dir_fd=self.root_fd)
            self._sync_dir()
        except Exception:
            if fd >= 0:
                os.close(fd)
            try:
                temporary_entry = self._entry(temporary)
                if self._same_file(owned, temporary_entry):
                    os.unlink(temporary, dir_fd=self.root_fd)
                    self._sync_dir()
            except OSError:
                pass
            raise

    def pop(self, firmware_name, default=None):
        name = self._name(firmware_name)
        entry = self._entry(name)
        if entry is None:
            return default
        content = self._read_entry(name, entry)
        current = self._entry(name)
        if not self._same_file(entry, current):
            raise RuntimeError("store entry changed before removal")
        os.unlink(name, dir_fd=self.root_fd)
        self._sync_dir()
        return content

    def rename(self, source_name, destination_name):
        source, destination = self._name(source_name), self._name(destination_name)
        source_entry, destination_entry = self._entry(source), self._entry(destination)
        if source_entry is None:
            return False
        if not self._same_file(source_entry, self._entry(source)):
            raise RuntimeError("rename source changed")
        current_destination = self._entry(destination)
        if (destination_entry is None) != (current_destination is None) or (destination_entry and not self._same_file(destination_entry, current_destination)):
            raise RuntimeError("rename destination changed")
        os.replace(source, destination, src_dir_fd=self.root_fd, dst_dir_fd=self.root_fd)
        self._sync_dir()
        return True
class Machine:
    def __init__(self, image, test_key, store):
        self.uc = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
        self.uc.ctl_set_cpu_model(R.UC_CPU_ARM64_MAX)
        segments = [dict(seg.header) for seg in ELFFile(io.BytesIO(image)).iter_segments()
                    if seg["p_type"] == "PT_LOAD"]
        ranges = []
        for seg in segments:
            if seg["p_memsz"]:
                low = seg["p_vaddr"] & ~4095
                high = (seg["p_vaddr"] + seg["p_memsz"] + 4095) & ~4095
                ranges.append((low, high))
        merged = []
        for low, high in sorted(ranges):
            if merged and low <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(merged[-1][1], high))
            else:
                merged.append((low, high))
        for low, high in merged:
            self.uc.mem_map(low, high - low)
        for seg in segments:
            if seg["p_filesz"]:
                self.uc.mem_write(seg["p_vaddr"], image[seg["p_offset"]:seg["p_offset"] + seg["p_filesz"]])
        self.uc.mem_map(0x40000000, 0x200000)
        self.uc.reg_write(R.UC_ARM64_REG_SP, 0x401ff000)
        self.uc.reg_write(R.UC_ARM64_REG_TPIDRRO_EL0, TLS)
        self.uc.reg_write(R.UC_ARM64_REG_CPACR_EL1, 3 << 20)
        self.put64(TLS + 0x18, 0x40000100)
        self.put64(TLS + 0x28, TLS + 0x28)
        self.put64(TLS + 0x50, CFG)
        self.put64(TLS + 0x58, 0x40000100)
        self.put64(TLS + 0x78, CTX)
        self.put64(0xd86b5018, 0x40002000)
        self.put64(0x40002000, 0x12345678)
        self.put64(CTX + 0x13f0, 0x40040000)
        self.uc.mem_write(CTX, b"\x01\0")
        self.uc.mem_write(CTX + 0x42, b"\x01")
        self.uc.mem_write(CTX + 0x18a9, b"\x01")
        self.uc.mem_write(CTX + 0x18aa, b"\0")
        self.uc.mem_write(CTX + 0x18ab, b"\0")
        self.uc.mem_write(CTX + 0x18ac, b"\x01")
        table = bytearray(512)
        table[0] = 0
        table[1] = 9
        self.uc.mem_write(CTX + 0x1459, bytes(table))
        identity = bytearray(1588)
        identity[32:80] = TEST_ID
        self.uc.mem_write(0x40040000, bytes(identity))
        self.uc.mem_write(TA + 0xef, test_key)
        self.uc.mem_write(TA + 0x10f, b"\x01")
        self.uc.mem_write(TA + 0x4c, b"research\0")
        self.uc.mem_write(TA + 0xad, b"\0")
        self.uc.mem_write(TA + 0x7d, TEST_ID)
        self.uc.mem_write(NAME, b"index\0")
        self.store = store
        self.events = []
        self.allocations = {}
        self.heap = HEAP
        self.boundary = None
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self.hook)

    def arg(self, number):
        return self.uc.reg_read(getattr(R, f"UC_ARM64_REG_X{number}"))

    def put64(self, address, value):
        self.uc.mem_write(address, struct.pack("<Q", value))

    def u64(self, address):
        return struct.unpack("<Q", self.uc.mem_read(address, 8))[0]

    def string(self, address):
        return bytes(self.uc.mem_read(address, 128)).split(b"\0")[0].decode("ascii")

    def ret(self, value):
        self.uc.reg_write(R.UC_ARM64_REG_X0, value & 0xffffffffffffffff)
        self.uc.reg_write(R.UC_ARM64_REG_PC, self.uc.reg_read(R.UC_ARM64_REG_LR))

    def hook(self, uc, address, size, _):
        if address == 0x40000100:
            if self.arg(3) == 0x1001:
                self.events.append({"op": "host_counter_object_absent"})
                self.ret(10)
                return
            if self.arg(1) == 4:
                args = self.arg(2)
                assert struct.unpack("<I", uc.mem_read(self.u64(args), 4))[0] == 0x8b
                self.events.append({"op": "host_counter_object_absent"})
                self.ret(0xffffffff)
                return
            raise RuntimeError("Unsupported host TEE request")
        if address == 0xd864d2a4:
            assert self.string(self.arg(0)) == "cmnlib_gppo_rpmb_enablement"
            self.events.append({"op": "host_config_select_local_file_authority"})
            self.ret(0)
        elif address == 0xd8638688:
            name = self.string(self.arg(1))
            blob = self.store.get(name)
            if blob is None:
                self.events.append({"op": "read_missing", "backup": name.endswith(".bak")})
                self.ret(0xffff0008)
                return
            offset, length = self.arg(4), self.arg(3)
            data = blob[offset:offset + length]
            uc.mem_write(self.arg(2), data)
            uc.mem_write(self.arg(5), struct.pack("<I", len(data)))
            self.events.append({"op": "read", "bytes": len(data), "backup": name.endswith(".bak")})
            self.ret(0)
        elif address == 0xd86387c0:
            name = self.string(self.arg(1))
            offset, length = self.arg(4), self.arg(3)
            if offset != 0:
                raise RuntimeError("Unsupported nonzero file offset")
            backup = bool(uc.mem_read(TA + 0xcc, 1)[0])
            if backup and name in self.store:
                self.store[name + ".bak"] = self.store[name]
            self.store[name] = bytes(uc.mem_read(self.arg(2), length))
            uc.mem_write(self.arg(5), struct.pack("<I", length))
            self.events.append({"op": "write", "bytes": length, "backup_requested": backup})
            self.ret(0)
        elif address == 0xd8638900:
            name = self.string(self.arg(1))
            present = self.store.pop(name, None) is not None
            self.events.append({"op": "remove", "present": present, "backup": name.endswith(".bak")})
            self.ret(0 if present else 0xffff0008)
        elif address == 0xd86389fc:
            source, destination = self.string(self.arg(1)), self.string(self.arg(2))
            success = self.store.rename(source, destination)
            self.events.append({"op": "rename", "success": success})
            self.ret(0 if success else 0xffff0008)
        elif address == 0xd8650b7c:
            count = self.arg(0)
            pointer = self.heap
            self.heap += (count + 15) & ~15
            if self.heap >= 0x400a0000:
                raise RuntimeError("Test heap exhausted")
            self.allocations[pointer] = count
            self.ret(pointer)
        elif address == 0xd8650b8c:
            pointer = self.arg(0)
            if pointer not in self.allocations:
                raise RuntimeError("Invalid test allocation")
            uc.mem_write(pointer, b"\0" * self.allocations.pop(pointer))
            self.ret(0)
        elif address == 0xd865b864:
            assert self.arg(1) == 32 and self.arg(3) == 1 and self.arg(5) == 3
            source, length = self.u64(self.arg(2)), self.u64(self.arg(2) + 8)
            descriptor = self.arg(4)
            segments = self.u64(descriptor)
            assert self.u64(descriptor + 8) == 1 and self.u64(segments + 8) == 32
            digest = hmac.digest(bytes(uc.mem_read(self.arg(0), 32)), bytes(uc.mem_read(source, length)), "sha256")
            uc.mem_write(self.u64(segments), digest)
            self.events.append({"op": "host_hmac_sha256", "bytes": length})
            self.ret(0)
        elif address == 0xd86489f4:
            self.boundary = "before_rpmb_persistence"
            self.events.append({"op": self.boundary})
            uc.emu_stop()
        elif address == 0xd8651238:
            self.ret(0)
        elif address == 0xd861b4c0:
            raise RuntimeError("Firmware stack guard failed")

    def run(self, address, *args):
        for index, value in enumerate(args):
            self.uc.reg_write(getattr(R, f"UC_ARM64_REG_X{index}"), value)
        self.uc.reg_write(R.UC_ARM64_REG_LR, STOP)
        self.uc.emu_start(address, STOP, count=100000)
        if self.boundary:
            raise RuntimeError("Execution reached the RPMB stop")
        if self.uc.reg_read(R.UC_ARM64_REG_PC) != STOP:
            raise RuntimeError("Firmware instruction limit reached")
        return {"return": hex(self.uc.reg_read(R.UC_ARM64_REG_W0)), "events": self.events,
                "files_remaining": len(self.store), "allocations_remaining": len(self.allocations)}


def parse_key(text):
    try:
        key = bytes.fromhex(text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("test key must be hexadecimal") from error
    if len(key) != 32:
        raise argparse.ArgumentTypeError("test key must contain exactly 32 bytes")
    return key


def execute(operation, image_path, expected_sha, store_path, test_key, version):
    image = image_path.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    if digest != expected_sha or digest != IMAGE_SHA:
        raise SystemExit(f"firmware SHA-256 mismatch: {digest}")
    if not store_path.is_dir():
        raise SystemExit("store directory must exist before the operation")
    store = DiskStore(store_path)
    machine = Machine(image, test_key, store)
    lookup = machine.run(0xd864d970, 0x0b)
    lookup_context = machine.arg(1)
    if lookup["return"] != "0x0" or lookup_context != 0:
        raise RuntimeError(f"constructed UID 0x0b lookup did not return empty: {lookup}")
    primary = "research/vTable"
    if operation == "create":
        if len(store) != 0:
            raise SystemExit("store directory must be empty for create")
        result = machine.run(0xd864328c, TA)
        if result["return"] != "0x0" or primary not in store:
            raise RuntimeError(f"vTable creation failed: {result}")
    elif operation == "set-version":
        if primary not in store:
            raise SystemExit("create the local vTable first")
        machine.uc.mem_write(NAME, TEST_ID)
        result = machine.run(0xd86427e0, TA, NAME, 48, version)
        if result["return"] != "0x0":
            raise RuntimeError(f"version update failed: {result}")
        result["requested_version"] = version
    elif operation == "get-version":
        if primary not in store:
            raise SystemExit("local vTable does not exist")
        machine.uc.mem_write(NAME, TEST_ID)
        result = machine.run(0xd86419dc, TA, NAME, 48, OUT)
        observed = struct.unpack("<I", machine.uc.mem_read(OUT, 4))[0]
        if result["return"] != "0x0":
            raise RuntimeError(f"version lookup failed: {result}")
        result["observed_version"] = observed
    elif operation == "tamper":
        blob = store.get(primary)
        if blob is None or len(blob) != 0x251:
            raise SystemExit("primary vTable must be 0x251 bytes")
        changed = bytearray(blob)
        changed[100] ^= 1
        store[primary] = bytes(changed)
        result = {"tampered_file_bytes": len(changed), "files_remaining": len(store)}
    elif operation == "verify-tamper":
        result = machine.run(0xd8643498, TA, 0)
        if result["return"] != "0xf0100001" or primary in store:
            raise RuntimeError(f"firmware did not reject tampered vTable: {result}")
        result["backup_retained"] = "research/vTable.bak" in store
    else:
        raise SystemExit("unknown operation")
    result["uid0b_registry_lookup"] = {
        "handler_pointer": lookup["return"],
        "context_pointer": hex(lookup_context),
        "scope": "constructed empty registry only"
    }
    result["operation"] = operation
    result["image_sha256"] = digest
    result["host_boundaries"] = ["explicit test key", "local-authority config", "host HMAC", "durable host files", "counter object absent"]
    result["rpmb_used"] = False
    print(json.dumps(result, sort_keys=True))


def main():
    parser = argparse.ArgumentParser(description="Run bounded original TZ local-vTable routines.")
    parser.add_argument("operation", choices=["create", "set-version", "get-version", "tamper", "verify-tamper"])
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--store", type=Path, required=True)
    parser.add_argument("--test-key-hex", type=parse_key, required=True)
    parser.add_argument("--version", type=int, default=7)
    args = parser.parse_args()
    if not 0 <= args.version <= 0xffffffff:
        parser.error("--version must fit in an unsigned 32-bit value")
    execute(args.operation, args.image, args.sha256.lower(), args.store, args.test_key_hex, args.version)

if __name__ == "__main__":
    main()

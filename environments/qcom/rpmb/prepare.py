#!/usr/bin/env python3
"""Prepare declared inputs for the qcom-vp RPMB persistence experiment."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / "tools/qcom/qcom-vp/tests/fixtures/ufs-rpmb"
ENTRY = FIXTURE.parent / "ufs/producer.S"
HELPERS = (
    ("platforms/qcom/apss-gic.lua", ROOT / "platforms/qcom/apss-gic.lua"),
    ("platforms/qcom/sm88xx/ufs.lua", ROOT / "platforms/qcom/sm88xx/ufs.lua"),
)
LUN_BYTES = (4096, 4096, 4096, 8192, 12288, 16384)


def run(command):
    subprocess.run(command, check=True)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def toml_string(value):
    return json.dumps(value, ensure_ascii=True)


def prepare(output, rpmb_seed=None, rpmb_size_kib=256):
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    inputs = output / "inputs"
    build = output / ".build"
    inputs.mkdir()
    build.mkdir()

    for required in (ENTRY, FIXTURE / "driver.c", FIXTURE / "platform.lua", *(path for _, path in HELPERS)):
        if not required.is_file():
            raise FileNotFoundError(f"required RPMB input is missing: {required}")

    elf = build / "producer.elf"
    binary = inputs / "producer.bin"
    run([
        "clang", "--target=aarch64-none-elf", "-fuse-ld=lld", "-nostdlib",
        "-Wl,-Ttext=0x300000", "-Wl,-e,_start", "-ffreestanding", "-fno-builtin",
        "-mgeneral-regs-only", "-O2", str(ENTRY), str(FIXTURE / "driver.c"), "-o", str(elf),
    ])
    run(["llvm-objcopy", "-O", "binary", str(elf), str(binary)])
    if not binary.stat().st_size or binary.stat().st_size > 0x20000:
        raise RuntimeError("The guest image must fit the declared 128 KiB ROM.")

    shutil.copyfile(FIXTURE / "platform.lua", inputs / "platform.lua")
    (inputs / "profile.lua").write_text(
        f"return {{rpmb_size_kib = {rpmb_size_kib}}}\n", encoding="utf-8"
    )
    files = ["platform.lua", "profile.lua", "producer.bin"]
    for name, source in HELPERS:
        destination = inputs / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        files.append(name)

    for index, size in enumerate(LUN_BYTES):
        name = f"initial-lun{index}.img"
        (inputs / name).write_bytes(bytes(size))
        files.append(name)

    if rpmb_seed is not None:
        seed = inputs / "rpmb-seed.bin"
        shutil.copyfile(rpmb_seed, seed)
        files.append("rpmb-seed.bin")

    lines = [
        "schema = 1",
        'name = "ufs-rpmb-persistent-state"',
        'description = "Does the UFS RPMB request path retain authenticated key, counter, and media state across a new experiment process?"',
        'entry = "A synthetic AArch64 guest starts at 0x300000 after virtual UFS link startup. A fresh state provisions key bytes 1 through 32 and writes two RPMB blocks. A resumed state checks the authenticated counter and media, then checks replay, wrong-key, and tampered-frame rejection."',
        'security_policy = "The guest uses a synthetic test key. The experiment does not use an OEM key or claim physical authentication."',
        "platform = \"platform.lua\"",
        "limitations = [",
        '  "The guest and reset state are constructed. No OEM firmware or physical device executes.",',
        '  "The RPMB model is a virtual substitute. This experiment does not qualify JESD220 behavior against hardware.",',
        '  "The guest exits through an intentional unmapped access at 0x600d0000 after its checks.",',
        '  "A process result records execution, not independent proof of every guest assertion.",',
        '  "The Lua platform is trusted host code and is not sandboxed.",',
        "]",
    ]
    for name in files:
        path = inputs / name
        lines.extend([
            "",
            "[[files]]",
            f"name = {toml_string(name)}",
            f"path = {toml_string('inputs/' + name)}",
            f"sha256 = {toml_string(sha256(path))}",
        ])
    lines.extend([
        "",
        "[[state]]",
        'name = "lun0.img"',
        'seed = "initial-lun0.img"',
        "",
        "[[state]]",
        'name = "lun1.img"',
        'seed = "initial-lun1.img"',
        "",
        "[[state]]",
        'name = "lun2.img"',
        'seed = "initial-lun2.img"',
        "",
        "[[state]]",
        'name = "lun3.img"',
        'seed = "initial-lun3.img"',
        "",
        "[[state]]",
        'name = "lun4.img"',
        'seed = "initial-lun4.img"',
        "",
        "[[state]]",
        'name = "lun5.img"',
        'seed = "initial-lun5.img"',
        "",
        "[[state]]",
        'name = "rpmb.bin"',
    ])
    if rpmb_seed is not None:
        lines.append('seed = "rpmb-seed.bin"')
    lines.extend([
        "",
        "[[state]]",
        'name = "rpmb.bin.lock"',
        "",
        "[[state]]",
        'name = "rpmb.bin.tmp"',
        "",
    ])
    (output / "environment.toml").write_text("\n".join(lines), encoding="utf-8")
    shutil.rmtree(build)
    print(f"Prepared environment: {output / 'environment.toml'}")
    print("The manifest declares six seeded mutable LUNs and the RPMB lock and temp paths.")
    print("rpmb.bin starts from an immutable seed." if rpmb_seed is not None else
          "rpmb.bin starts absent and will be created by the device.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True,
                        help="new directory for the manifest and immutable input snapshot")
    parser.add_argument("--rpmb-seed", type=Path,
                        help="optional immutable seed for malformed-state testing")
    parser.add_argument("--rpmb-size-kib", type=int, default=256,
                        help="RPMB geometry in 128 KiB units (default: 256)")
    args = parser.parse_args()
    if args.rpmb_size_kib < 128 or args.rpmb_size_kib > 255 * 128 or args.rpmb_size_kib % 128:
        parser.error("--rpmb-size-kib must be a multiple of 128 from 128 through 32640")
    prepare(args.out.resolve(), args.rpmb_seed.resolve() if args.rpmb_seed else None,
            args.rpmb_size_kib)

if __name__ == "__main__":
    main()

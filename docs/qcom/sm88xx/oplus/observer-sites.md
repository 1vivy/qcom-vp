# Hash-bound observer sites

The observer supports the two OEM identities below. It does not select a profile from a platform name.
Each profile requires the exact kernel member, kernel length, kernel hash, UEFI hash, and ABL hash.
The decoder also checks the hashes of both decompressed EFI volumes.
Unknown identities fail before guest execution. A mixed pair of firmware images also fails.

These coordinates come from offline ELF and PE parsing, LZMA decoding, and Capstone AArch64 disassembly.
They are not evidence of a completed virtual boot.
No proprietary image or extracted code file belongs in this repository.

## Source identities

| Profile | OEM package |
| --- | --- |
| SM8850 | `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip` |
| SM8845 | `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip` |

All hashes below use SHA-256.

| Object | SM8850 | SM8845 |
| --- | --- | --- |
| `IMAGES/uefi.elf` | `da8ad1b0ffc2eff91d7d34d8574a682b7007e0b052439fbbcde989d5393071f5` | `13722b7ed849359e7afc731dfdc1eb0c1fc1815907d708f4b94cadb7e03be4f4` |
| `IMAGES/abl.elf` | `b3cd257aee11de8720fbf5557d52326d38bdf1e9176794d85c791efa4a714278` | `83767a6f1b2ce098c6e4ebc60c546833652a923548f3b1d1cd737533f576b0bf` |
| Decoded DXE volume | `0329b1be2fc320149437cc29d2de83a886fe40d0d4a348371cb56175fc92a4d6` | `ae119f8b8b083df44e131a7591b6a7bee7c04a88905c78214157095cba38e65e` |
| Decoded ABL volume | `3898da4264df336e0ba9f8d251baa397337f3455b3f35d73fd77059505371d80` | `f73a11a42e6427b2517ee19c11e854c00161070e9a864825c8f550f70a2bf025` |
| Kernel Image | `2609ff5297e2716fd061daf3f51661b4ea068ed96cd17a320bef32d7d3d71393` | `e47ec8fecfcaca4fe80ccc020af242d956225e09441db632db2190a89c522aa5` |

Both kernels use `IMAGES/boot.img`, with the Image at file offset `0x1000`.
The boot header gives lengths of 39,889,408 bytes and 40,024,576 bytes, respectively.
Offline hashing of both prepared `kernel-reference.bin` files gave the corresponding hashes above.
The observer reads the entire executing kernel from the guest and checks its hash again.
It does not load this reference into guest memory.

## Container coordinates

| Coordinate | SM8850 | SM8845 |
| --- | --- | --- |
| UEFI ELF entry | `0xc6800000` | `0xa7000000` |
| UEFI segment file offset | `0x1000` | `0x1000` |
| DXE LZMA stream in UEFI ELF | `0x55268` | `0x54268` |
| DXE PE in decoded volume | `0x73c` | `0x6fc` |
| Decoded DXE volume length | 4,685,832 | 4,472,840 |
| DXE PE length / entry RVA | `0x30000` / `0x1000` | `0x30000` / `0x1000` |
| ABL LZMA stream in ELF | `0x1078` | `0x1078` |
| ABL PE in decoded volume | `0xb8` | `0xb8` |
| Decoded ABL volume length | 778,440 | 778,440 |
| ABL PE length / entry RVA | `0xbe000` / `0x189c` | `0xbe000` / `0x189c` |

Both PEs use preferred base zero. Their sections have equal raw offsets and virtual addresses.
For SM8845 DXE, the section RVAs are `.text=0x1000`, `.data=0x2a000`, and `.reloc=0x2f000`.
For SM8845 ABL, they are `.text=0x1000`, `.data=0x7b000`, and `.reloc=0xbd000`.
Offline relocation parsing found no relocations across the entry signatures or observed branch sites.
The SM8845 directories contain 327 nonzero DXE relocations and 1,393 nonzero ABL relocations.

## Execution coordinates

An RVA is relative to the loaded PE base. It is not a fixed guest address.
The observer obtains the DXE entry from SEC register `x20` and subtracts `0x1000` to obtain the DXE base.
It obtains the ABL entry from the actual StartImage dispatch register `x8`.
It compares 32 entry bytes with the selected ABL, then subtracts `0x189c` to obtain the ABL base.

| Site | SM8850 | SM8845 | Offline instruction or role |
| --- | --- | --- | --- |
| SEC discovery PC | `0xc6817e60` | `0xa7017f0c` | SM8850: `mov x3, x20`. SM8845: `blr x20` |
| SEC final DXE dispatch PC | `0xc6817f8c` | `0xa7017f0c` | `blr x20`, opcode `0xd63f0280` |
| DXE StartImage dispatch RVA | `0x6f44` | `0x6f44` | `blr x8`, opcode `0xd63f0100` |
| DXE ExitBootServices RVA | `0x24d8` | `0x24d8` | Function entry |
| ABL ExitBootServices call RVA | `0x157f4` | `0x156e4` | `blr x8`, opcode `0xd63f0100` |
| ABL call return RVA | `0x157f8` | `0x156e8` | `tbz x0, #63`, opcode `0xb6f800c0` |
| ABL final kernel branch RVA | `0x15a24` | `0x15914` | `blr x19`, opcode `0xd63f0260` |
| ABL argument helper RVA | `0x56bd8` | `0x56b84` | Clears `x1`, `x2`, and `x3`, then returns |

Opcode values are 32-bit instruction words, not byte-order strings.
The SM8845 UEFI entry instruction is `b 0xa700ee40`, opcode `0x14003b90`.
Its ELF entry and loadable segment both identify `0xa7000000`.

SM8845 SEC loads the relocated entry into `x20` at PC `0xa7017da8` from the loader result at `sp+8`.
Its final dispatch at ELF file offset `0x18f0c` executes `blr x20`.
The DXE dispatch loads `x8` from the image record at `x20+0x20` at RVA `0x6f38`.
It passes the image handle in `x0` and the system table in `x1` before the branch at `0x6f44`.
That branch is at decoded DXE volume offset `0x7640`.

SM8845 DXE stores the `BOOT_SERV` table at RVA `0x2a578`.
Its ExitBootServices slot, at table offset `0xe8`, contains RVA `0x24d8` before relocation.
SM8845 ABL loads that slot into `x8` at RVA `0x156e0`.
The call and return instructions are at decoded ABL volume offsets `0x1579c` and `0x157a0`.
The success branch targets RVA `0x15700`.
The error path returns to GetMemoryMap at RVA `0x15654` through the branch at `0x156fc`.
The observer requires a return value of zero. It observes firmware retries without creating host retries.
A DXE driver can replace the ExitBootServices slot with a wrapper.
The SM8850 wrapper prints `Shutting Down UEFI Boot Services` before it enters the core service.
When the call target differs from DXE RVA `0x24d8`, the observer requires the core entry before the ABL return site.
A wrapper that returns without the core entry fails the run.

SM8845 ABL loads `x19` from `sp+0x570` at RVA `0x158ec`.
The AArch64 path loads the FDT argument into `x0` at RVA `0x1590c`.
It calls the argument helper at RVA `0x15910`, then executes `blr x19` at `0x15914`.
The final branch is at decoded ABL volume offset `0x159cc`.
The observer obtains the kernel address from this runtime `x19`, not from an assumed load address.

The first SM8845 kernel instruction is `ccmp x18, #0, #13, pl`, opcode `0xfa405a4d`.
The next instruction branches to Image offset `0x1f8f068`.
The observer requires the first instruction to complete at entry PC plus four without an execution-state change.
A breakpoint at the kernel entry alone does not satisfy this requirement.
The existing full kernel hash, FDT, exception-level, and security-state checks remain required.

## Bounded kernel capture

The observer reads memory in 32 KiB requests and buffers socket input in 64 KiB blocks.
It checks cancellation and the deadline before each socket read.
It still checks every packet checksum and the complete loaded kernel hash.
The observer does not retry a failed translation through a different physical address space.

Run920 reports an APSS watchdog bite before QEMU reports a failed debug translation.
That sequence does not establish a missing stage-two mapping in the original kernel state.
The earlier physical fallback did not solve the observer delay and is absent from the final implementation.

Run921 captures all 39,889,408 kernel bytes in 0.426 seconds through translated reads.
The observer then verifies entry and detaches before Linux emits its UART log.
The observer changes no watchdog setting or guest register.
Run922 repeats entry and console proof after removal of the temporary diagnostics.

## Simulation time during inspection

The automatic observer holds SystemC time while it inspects a stopped APSS.
The private `ObservationClock` socket uses the existing QBox suspension API.
The observer resumes time for each guest continue or step request.
It also resumes time after the debugger acknowledges detach.
Socket closure releases the session's hold after an observer error.

Each completed memory read checks the timestamp against the pause acknowledgement.
A changed timestamp fails observation.
The `result.json` field `observation_time_policy` records `suspend-during-inspection`.
Manual debugger sessions leave this policy unchanged.

This diagnostic policy does not change watchdog registers, deadlines, authentication, or guest memory.
It does not claim hardware debug-freeze semantics or an atomic machine snapshot.
The reference platforms use QBox's `multithread-quantum` policy.
Other policies must satisfy the same timestamp check.

The 2026-10-29 SM8850 run reproduced kernel entry, debugger detach, and 4,573 kernel UART bytes.
All 238 pause/resume pairs retained the same timestamp.
The next unsupported access still reads `0x17810008`.
The [current environment record](../../../environment-inventory.md#current-requalification-2026-10-29) identifies the command, inputs, and retained evidence.

## Offline reproduction

This example disassembles the SM8845 sites without executing firmware.
It requires Python with Capstone and access to the private OEM ZIP.

```python
import lzma
import struct
import sys
import zipfile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

archive = zipfile.ZipFile(sys.argv[1])
uefi = archive.read("IMAGES/uefi.elf")
abl_elf = archive.read("IMAGES/abl.elf")
core_volume = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(uefi[0x54268:])
abl_volume = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(abl_elf[0x1078:])
core = core_volume[0x6fc:0x306fc]
abl = abl_volume[0xb8:0xbe0b8]
boot = archive.read("IMAGES/boot.img")
assert struct.unpack_from("<Q", uefi, 24)[0] == 0xa7000000
disassembler = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
for data, offset, address in [
    (uefi, 0x1000, 0xa7000000),
    (uefi, 0x18f0c, 0xa7017f0c),
    (core, 0x6f44, 0x6f44),
    (abl, 0x156e4, 0x156e4),
    (abl, 0x156e8, 0x156e8),
    (abl, 0x15914, 0x15914),
    (boot, 0x1000, 0),
]:
    for instruction in disassembler.disasm(data[offset:offset + 4], address):
        opcode = struct.unpack("<I", instruction.bytes)[0]
        print(hex(address), hex(opcode), instruction.mnemonic, instruction.op_str)
```

## Host checks

`profiles_require_exact_kernel_identities` covers both profiles and rejects changed kernel lengths, hashes, and member names.
The normal run command checks the selected private package before it starts the platform.
Host checks do not prove virtual boot. Only real guest execution can produce the handoff evidence.

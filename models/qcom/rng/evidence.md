# APSS random generator

## Model

`qrng.h` implements `qcom,qrng-v1`.
The board maps its relative registers at `0x010c0000`.
The model uses the host's `getrandom` interface through `host_entropy.h`.
The TME sequencer uses the same helper for command `0x13`.

The host supplies real random bytes, not a fixed response or a successful authentication result.
Host acquisition errors stop execution.
The model does not log random values.
It does not claim physical noise measurements or hardware health-test qualification.

## ROM evidence

The source is the unmodified `pbl8850.bin`, mapped at `0x300000` and decoded as AArch64.
Its SHA-256 is `91bd8f81efa7a5940d4b6d4c220388b559ceef39b6b47dd7b4b0727a1beb1d1c`.
The PBL sequence, the XBL channel-three accesses at `0x010c3004`, and the QHEE channel-twelve accesses at `0x010cc004` place this aperture at `0x010c0000`.
The source-only base name and the vendor register names remain descriptive labels for these observed addresses.

| Offset | Behavior | ROM evidence |
| --- | --- | --- |
| `0x14` | Bit 1 starts generation. Bit 0 holds reset. | `0x3094a4` sets bit 1. `0x309430` pulses bit 0 after a health error. |
| `0x100` | Health status | `0x3093ec` checks bits 3:0. |
| `0x120` | Health error status | `0x3093ec` requires zero. |
| `0x124` | Acquisition status | `0x3094a4` waits for bit 0 to clear. |
| `0x5000` | Random data | `0x3094ec` consumes words and copies a final partial word when necessary. |
| `0x5004` | Data availability | `0x309430` waits for bit 0. |
| `0x3000` | Channel-three random data | XBL selects this channel through its RNG register map. |
| `0x3004` | Channel-three availability | XBL `0x14855f90` checks bit zero. |

Function `0x3094a4` delays 100 microseconds after it starts the generator.
Function `0x309430` uses a bounded availability poll.
Function `0x3094ec` rejects a null destination and a zero byte count.
These operations establish an entropy service, not a status-only device.

## Functional policy

The virtual source acquires 64 bytes when the guest starts generation or releases reset with generation enabled.
Each data read consumes four bytes.
The source replenishes the buffer after its last word.
Availability reflects an active generator with buffered data.
A disabled generator cannot return data successfully.
Reset and disable invalidate the buffered output.

Channels three and five consume distinct words from the same host entropy buffer.
They share the observed generator control and reset state.

Acquisition is synchronous, so the virtual source has no pending acquisition after a completed transaction.
The health registers report no virtual source error while acquisition succeeds.
A host error is fatal rather than a fabricated hardware success.
This policy does not implement an analog noise source or its physical failure modes.

Only aligned, little-endian, 32-bit accesses are supported.
Unknown registers and unsupported control bits fail through the platform's access monitor.
The model grants no direct-memory access.

## Execution evidence

The real APSS ROM passes its initial RNG control access after this model is installed.
With timer interrupts connected, the APSS/TME diagnostic next reaches clock register `0x174074` at PC `0x313448`.
This observation proves progress beyond the generator initialization path.
It does not establish a complete secure boot or a kernel boot.

## XBL channel three

The OEM XBL reads channel-three status at `0x010c3004`.
The existing model previously exposed only channel five.
The model now supplies channel-three data through the same real host entropy source.
It preserves activation, reset, and acquisition failure behavior.

`qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/rng.py` independently records these two channel-three addresses for the matched stock RngDxe.
That instrument uses deterministic data, which this model does not copy.
The real XBL run passed this channel and reached its ChipInfo query at `0x1485eba8`.
The complete integration suite passed all 50 tests with one test thread.

## Selected SM8845 QHEE channel twelve

The selected `IMAGES/hypvm.mbn` SHA-256 is `7b36e0ca1b365b2294856058b77d3e6e571f66963282de77ed5c4656452c14da`.
The source is the package in `prepared-sm8845-common-runtime11/inputs.json`, not an unrelated analysis project.
Its entry is AArch64 at `0x80000000`.

1. At `0x80000020`, QHEE reads `0x01fc8004` and retains its low three mode bits.
2. For mode zero, `0x80000044` reads channel-twelve availability at `0x010cc004`.
3. If bit zero is clear, QHEE parks at `0x800000d0` with `WFE`.
4. Otherwise, `0x80000050..0x80000078` consumes eight words from `0x010cc000`.
5. The code combines the words into four 64-bit values and stores them at `0x80209f10`.

The model now exposes offsets `0xc000` and `0xc004`.
They use the same activation predicate and real host entropy cursor as channels three and five.
The aliases do not enable generation, manufacture availability, or catch acquisition failures.
The reset platform routes the existing RNG aperture at `0x010c0000` with size `0xc008`.
Its composition checks for competing APSS targets before continuing.
No separate channel model or seeded guest buffer is required.

The selected SM8850 QHEE instead executes `RNDRRS` at `0x80000034`.
It then executes `RNDR` at `0x8000003c`, `0x80000044`, and `0x8000004c`.
These instructions require architectural FEAT_RNG support.
The MMIO aliases do not implement these instructions.
Both entry sequences are static evidence only.
No native QHEE execution or authentication qualification is claimed here.

## QSEE channel one

The selected SM8850 `RADIO/tz.img` has SHA-256 `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932`.
Its bootstrap at `0x14694084` calls `0x156b7e48` with destination `0x157dfd90` and length eight.
Configuration `0x157e7348` supplies base `0x010c0000` at offset `0x18` and channel one at offset `0x139`.
Its initialized flag at offset `0x138` is already one.
Availability helper `0x156d2034` reads `0x010c1004`.
Data helper `0x156d1f40` reads `0x010c1000`.

The model exposes these two offsets through the existing entropy owner and cursor.
It preserves the existing activation, reset, and entropy failure behavior.
The aliases do not seed a guest buffer or independently assert availability.
SM8845 uses configuration `0x157dca40`, with the same base and channel fields.
Its request wrapper is `0x156b3830`. Its status and data reads occur at `0x156c6fec` and `0x156c6ef8`.
Run133 executes the eight-byte request from `0x14694084`.
Its native trace passes the returned-length comparison at `0x14694088` and continues at `0x14694090`.
This establishes native entropy consumption, not complete coverage of the firmware's error paths.

## CPUCP cluster generators

The selected `IMAGES/cpucp.elf` images define one QRNG instance per cluster.
The SM8850 image has SHA-256 `3475ceab499e02fac22e249e7249ea9ee92c0eb21a260eb9b6f18d185ab70e24`.
The SM8845 image has SHA-256 `5667791c9c681da9fe05c5a91985fa30b9d9d67a8bebfa149cc1193d75c336a4`.
Both descriptor templates at `0x1d82d980` contain base `0x198a0000` at offset `0xb8`.
Constructors `0x1d8034ee` and `0x1d803552` add `lane << 24` to this base.
The second instance therefore starts at `0x1a8a0000`.

| Operation | SM8850 PC | SM8845 PC |
| --- | --- | --- |
| Read control at `+0x14` | `0x1d80946a` | `0x1d8097d0` |
| Write generation enable in bit one | `0x1d809470` | `0x1d8097d6` |
| Read health words at `+0x100`, `+0x120`, and `+0x208` | `0x1d80925c..0x1d80926e` | `0x1d80960e..0x1d809620` |
| Pulse reset bit zero after a health error | `0x1d8092aa..0x1d8092c0` | `0x1d80965c..0x1d809672` |

The `enable_qrng` property controls generation.
The firmware checks health only when that property enables generation.
The diagnostic strings identify this block as QRNG and name `DRBG_DEBUG_REG1` for failure inspection.
The existing host-entropy model supplies independent control and entropy buffers for both instances.
The added health word at `+0x208` reports no source error under the existing synchronous acquisition policy.
This policy does not claim physical health-test qualification.

SM8850 run302 passes the previous control access and reaches outer power register `0x19880338`.
SM8845 run233 reaches the same register at PC `0x1d80a1c2`.
A temporary native probe exercises generation, buffer refill, reset, disable, and rejected writes to the health word.
The probe passes without deterministic entropy or guest data injection.


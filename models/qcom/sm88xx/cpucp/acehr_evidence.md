# CPUCP ACEHR request controller

## Scope and inputs

This group covers the two CPUCP ACEHR request controllers that the selected
SM8850 and SM8845 images configure at `0x18180000` and `0x18380000`.
The inputs are `IMAGES/cpucp.elf` from these packages:

- `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`, SHA-256
  `3475ceab499e02fac22e249e7249ea9ee92c0eb21a260eb9b6f18d185ab70e24`
  (SM8850).
- `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`, SHA-256
  `5667791c9c681da9fe05c5a91985fa30b9d9d67a8bebfa149cc1193d75c336a4`
  (SM8845).

PC values below are static ELF virtual addresses in the `0x1d800000` image.
The two images relocate data at runtime.
This recovery uses static disassembly and the SM8850 run-272 capture at the first ACEHR queue publication.
This investigation ran no build, test, formatter, native run, or OEM run.

The local `refs/qemu` tree and public searches did not identify a
register-compatible ACEHR, ACD, or CDPM model.
The implementation reuses this repository's `TmeSequencer` secure DMA
transaction pattern, not an unrelated hardware controller.

## Configured instances

Each image registers two ACEHR devices.
The table entry supplies the initializer and the device object.
The device object holds a name pointer and a configuration pointer.
The configuration supplies two bases and the interrupt callback at `+0x648`.

| SoC | Instance | Device table entry | Object | Configuration | Control base | Descriptor base |
| --- | --- | --- | --- | --- | --- | --- |
| SM8850 | 0 | `0x1d8232c8` | `0x1d823560` | `0x1d825088` | `0x18180000` | `0x18184000` |
| SM8850 | 1 | `0x1d8232d8` | `0x1d823538` | `0x1d824a38` | `0x18380000` | `0x18384000` |
| SM8845 | 0 | `0x1d822dc8` | `0x1d823070` | `0x1d824c78` | `0x18180000` | `0x18184000` |
| SM8845 | 1 | `0x1d822dd8` | `0x1d823048` | `0x1d824628` | `0x18380000` | `0x18384000` |

The object names are `acehr0@18180000` and `acehr1@18380000` in both images.
The initializer is `0x1d805e62` on SM8850 and `0x1d80611a` on SM8845.
Both instances use the same initializer.
The callback addresses at configuration `+0x648` are:

| SoC | Instance | Callback |
| --- | --- | --- |
| SM8850 | 0 | `0x1d805dd2` |
| SM8850 | 1 | `0x1d805e1a` |
| SM8845 | 0 | `0x1d80608a` |
| SM8845 | 1 | `0x1d8060d2` |

Configuration `+0x8` is the descriptor base, not a second register block.
The submit path loads it at `0x1d806236` (SM8850) and `0x1d8064ee` (SM8845)
and writes a 16-byte descriptor at `descriptor base + 16 * head`.
The head index is configuration `+0x3c`.
Descriptor slots are written at `0x1d806248/0x1d80624e` (SM8850) and
`0x1d806500/0x1d806504` (SM8845).

## Initializer contract

The initializer performs these steps in order:

1. Clear configuration counts at `+0x38`, `+0x3c`, `+0x10`, and `+0x18`.
2. Clear 64 channel records at `+0x48..+0x648`, stride `0x18`.
   Each record receives zero at `+0x0`, `+0x1`, `+0x4`, `+0x8`, and `+0xc`.
   Its `+0x10` word receives the reference-count helper `0x1d805ce4`.
3. Load control base from configuration `+0x0`.
4. Write `0xf` to control base `+0x21c`.
5. Call the configuration callback at `+0x648`.

The channel records live in CPUCP RAM inside the configuration object.
They are guest structures, not controller registers.
The only controller write in the initializer is the 32-bit `0xf` at `+0x21c`:

| SoC | Base load | Configuration store | Callback load and call |
| --- | --- | --- | --- |
| SM8850 | `0x1d805eac` | `0x1d805eb6` | `0x1d805eb8/0x1d805ebc` |
| SM8845 | `0x1d806164` | `0x1d80616e` | `0x1d806170/0x1d806174` |

The callback does not touch either ACEHR base.
It enables CPUCP interrupt sources and sets the matching machine interrupt
enable bits:

| SoC | Instance | Completion ID/source | Error ID/source | Helpers |
| --- | --- | --- | --- | --- |
| SM8850 | 0 | `0x4d10` / 76 | `0x4910` / 72 | `0x1d81c60c`, `0x1d81c5d8` |
| SM8850 | 1 | `0x4e10` / 77 | `0x4a10` / 73 | Same |
| SM8845 | 0 | `0x4d10` / 76 | `0x4910` / 72 | Same |
| SM8845 | 1 | `0x4e10` / 77 | `0x4a10` / 73 | Same |

The interrupt helper maps each encoded ID to source `(ID >> 8) - 1`
at `0x1d801f48..0x1d801f52`.
SM8850 handler table base `0x1d834430` has 16-byte entries.
Sources 72/73 dispatch error handler `0x1d805f06`.
Sources 76/77 dispatch completion handler `0x1d805cf4`.
SM8845 table base `0x1d8343d0` has the same layout.
Its error handler is `0x1d8061be` and completion handler is `0x1d805fac`.
This interrupt programming is the next external boundary after the `+0x21c`
store. It belongs to the CPUCP interrupt controller.

## Register contract

Both images use the same offsets.
The table gives the access PC in SM8850 and SM8845 order.

| Offset | Width | Access | Meaning | SM8850 PC | SM8845 PC |
| --- | --- | --- | --- | --- | --- |
| `+0x004+4*n` | 32 | RW | Per-channel arm/ack word, `n` in `0..63` | `0x1d805da6` | `0x1d80605e` |
| `+0x204` | 32 | RW | Software request bits, channels `0..31` | `0x1d8062e4` | `0x1d80659c` |
| `+0x208` | 32 | RW | Software request bits, channels `32..63` | `0x1d8063c8` | `0x1d806680` |
| `+0x20c` | 32 | RW | Lower-half interrupt enable bits | `0x1d8063a0/0x1d8063ce` | `0x1d806658/0x1d806686` |
| `+0x210` | 32 | RW | Upper-half interrupt enable bits | `0x1d8062c6/0x1d8063b2` | `0x1d80657e/0x1d80666a` |
| `+0x21c` | 32 | RW | Controller configuration | `0x1d805eb6` | `0x1d80616e` |
| `+0x560` | 64 | RO | Completion bitmap polled per channel | `0x1d8064c8` | `0x1d806780` |
| `+0x570` | 64 | RO | Service-pending bitmap read by the handler | `0x1d805d40` | `0x1d805ff0` |
| `+0x588` | 64 | W | Trigger release value | `0x1d805d64` | `0x1d806014` |
| `+0x590` | 32 | RO | Wake/validity bitmap read by the error path | `0x1d806074` | `0x1d80632c` |
| `+0x594` | 32 | RO | Captured write-response address low | `0x1d805f3a` | `0x1d8061f2` |
| `+0x598` | 32 | RO | Captured write-response address high | `0x1d805f46` | `0x1d8061fe` |
| `+0x59c` | 32 | RO | Captured read-response address low | `0x1d805f52` | `0x1d80620a` |
| `+0x5a0` | 32 | RO | Captured read-response address high | `0x1d805f5e` | `0x1d806216` |
| `+0x5a4` | 32 | RO | Captured burst/translation address | `0x1d805f6a` | `0x1d806222` |
| `+0x5a8` | 32 | RO | Captured unaligned address | `0x1d805f74` | `0x1d80622c` |
| `+0x5ac` | 32 | RW | Service word written by the error path | `0x1d8060d0` | `0x1d806388` |
| `+0x3e00+4*n` | 32 | RW | Passive queue slot publication, not execution | `0x1d80627e` | `0x1d806536` |

The service path rewrites the per-channel trigger word with mask `0x6`
(`0x1d805daa..0x1d805dba` and `0x1d806062..0x1d806072`).
That clears bit 0 and preserves bits 1 and 2.
The receiver sets bit 0 and preserves bits 1 and 2 (`0x1d806430..0x1d806438`).
The model treats a write that clears bit 0 as an acknowledgement for that
channel. It clears only that channel's completion and IRQ state.
The completion poll reads `+0x560` as one 64-bit word and tests
`1 << channel`.
The error path loads `+0x594..+0x5a8` into RAM and logs them as
`WRESP_ADDR`, `RRESP_ADDR`, `NRWBURST_TSB_ADDR`, and `UNALIGN_TSB_ADDR`.
The matching strings are `0x1d824878..0x1d824928` (SM8850) and
`0x1d824468..0x1d824518` (SM8845).

## Request submission and execution

The submit entry is `0x1d806144` (SM8850) and `0x1d8063fc` (SM8845).
It accepts a request record and a 16-byte descriptor pointer:

- Record byte `0` selects ACEHR instance `0` or `1`.
- Record byte `1` selects channel `0..63`.
- Record byte `2` selects software trigger (`0`) or persistent external trigger (`1`).
- Record `+0x8` and `+0x10` hold source and destination addresses.
- Record `+0x18` holds the requested transfer length.
- The builder supplies two encoded 64-bit descriptor words.

The first real producer is the initializer `0x1d807154` on SM8850.
It builds the request at `0x1d8071f2..0x1d80725e`.
It calls descriptor builder `0x1d8066d2` at `0x1d807262`.
The builder calls submit at `0x1d80680c`.
The initializer checks a negative return, then advances to the next table entry.
It does not poll for completion at that point.

The first entry at `0x812fab90` has source `0x17090c00`, destination
`0x1d880000`, length `0x10`, and flag bytes `02 00 03`.
It is enabled by word `1` at entry `+0x18`.
The SM8845 entry at `0x8126e4b8` has the same values.
The SM8850 runtime capture on run 272 recorded descriptor words
`0x0000b84860050003` and `0x0000000000ec4000`.
The store to `0x18183e00` wrote zero.
The actual builder produces those exact words from the first entry.

The source is APSS GIC redistributor slot zero, base `0x17080000`,
at SGI/PPI configuration offset `+0x10c00`.
The reverse entry at `0x812f8510` copies `0x1d880000` to `0x17090c00`.
SM8845 has the reverse entry at `0x8126be38`.
The forward and reverse entries both request 16 bytes.
They establish a GIC configuration snapshot and restore pair.

The descriptor fields for the selected 32-bit physical addresses are:

| Field | Decode |
| --- | --- |
| Source | `(word0 >> 19) & 0xffffffff` |
| Destination | `((word0 >> 59) & 31) | ((word1 & 0x7ffffff) << 5)` |
| Hardware mode | `(word0 >> 12) & 7` |
| Beat width | `1 << ((word0 >> 15) & 3)` bytes |
| Beat count | `(word0 & 0xfff) + 1` |
| Address-range flags | `(word0 >> 17) & 3` |

The captured command has hardware mode zero, width four, and four beats.
Its real effect is four ordered 32-bit reads from GICR followed by four
32-bit writes to the ACEHR data SRAM.
The model maps the address-range flags through one virtual physical bus.
It does not claim silicon translation behavior.

Configuration `+0x8` supplies descriptor SRAM.
The submit path writes two 64-bit words at `base + 16 * head`.
It then publishes a packed channel range at `control + 0x3e00 + 4*channel`.
The low 16 bits come from channel field `+0x50`.
The high 16 bits come from channel field `+0x54`.
The model treats those fields as inclusive first and last slot indices.
It rejects a reversed or out-of-ring range.
The selected first request publishes `0..0`.

For record byte `2 == 1`, submit branches at `0x1d806280` to
`0x1d8062fe` (SM8850).
It skips software trigger writes to `+0x20c/+0x204` or `+0x210/+0x208`.
The queue publication is passive. No transfer or completion follows that
store without a separate external event.
The model exposes `trigger_external(channel, delay)` for a real event owner.
Production does not synthesize such an event.

For record byte `2 == 0`, submit writes interrupt policy at `+0x20c/+0x210` and request state at `+0x204/+0x208`.
Record byte six selects interrupt or polling completion. Polling clears the interrupt mask without suppressing DMA.
The per-channel start bit executes the complete published slot range.
Completion requires every bus read and write to succeed.
The wait routine polls the channel bit at `+0x560`.

The receive callback at `0x1d8063e4` checks a different delivered-message
header. It is not the decoder for the memory-transfer descriptor above.
No guest queue or response is produced by the model.

## Implemented model

`models/qcom/sm88xx/cpucp/acehr.h` implements `qcom_vp::QcomCpucpAcehr`.
It has one `0x8000`-byte target socket, an optional DMA initiator, and an
optional IRQ output.

| Region | Behavior |
| --- | --- |
| `+0x0000..+0x3fff` | Bounded control and trigger registers |
| `+0x4000..+0x7fff` | 1024 retained 16-byte descriptor slots |
| `+0x3e00..+0x3efc` | Passive packed first/last queue indices for 64 channels |
| Unlisted control offset | `TLM_ADDRESS_ERROR_RESPONSE` |
| Address at or above `0x8000` | `TLM_ADDRESS_ERROR_RESPONSE` |

The controller retains guest descriptor stores.
It marks a slot complete only after all 16 descriptor bytes are written.
A queue publication accepts an inclusive range with `first <= last < 1024`.
It makes no memory transfer and no completion.
The first captured publication has range `0..0`.

A rising per-channel start bit processes each descriptor in the published range.
`trigger_external(channel, delay)` provides the same operation to a real
external event owner.
Production supplies no synthetic external events.
Only hardware mode zero is implemented: copy `count` beats of `width` bytes
from source to destination in order.
The model uses one stack buffer and one reused TLM payload.
It publishes completion only after all reads and writes succeed.
An unbound DMA socket, bad queue, unsupported descriptor, or bus failure
returns an error and publishes no completion for that channel.

The DMA extension explicitly selects secure privileged accesses with
`unspecified=false`, `user=false`, and requester ID zero.
This is a virtual bus policy, not an authentication or privilege grant.
The model writes only to the destination address from a validated descriptor.
It never writes a CPUCP guest queue, SCMI response, FREE word, or host reply.

The completion bit at `+0x560` and service bit at `+0x570` are per-channel.
A write to channel word `+0x004+4*n` with bit zero clear acknowledges that
channel without clearing unrelated bits.
A write at `+0x588` clears selected service bits.
The optional IRQ asserts only for completed enabled channels.
Unsupported operations and failed DMA transfers do not assert it.

Access rules:

- Aligned 32-bit and 64-bit controller transfers only.
- Byte enables, other widths, and unaligned addresses fail before decode.
- A 32-bit register rejects a 64-bit transfer and the reverse.
- The descriptor store accepts aligned 32-bit and 64-bit transfers.

## Explicit virtual policy

The following choices are substitutes, not measured silicon values:

- The descriptor store length is `0x4000` bytes, 1024 slots.
  The base is firmware-derived. The length matches the `0x3ff` submission
  bound checked at `0x1d80619a` (SM8850) and `0x1d806452` (SM8845).
- Cold state is zero for every register and descriptor slot.
- An enabled request processes a complete inclusive slot range.
  A reversed range, partial descriptor, or slot beyond 1023 fails.
- Interrupt masks do not gate transfers. Request words do not execute them.
  Repeated writes with the channel start bit already set do not repeat DMA.
- Mode zero performs a physical copy with width `1`, `2`, `4`, or `8` bytes.
  A descriptor may transfer at most `0x8000` bytes.
  The model accepts 32-bit physical addresses only.
- Address-range flag bits use the unified virtual physical bus.
  No separate silicon address translation or coherence rule is claimed.
- The external-trigger API needs a real caller. No event is generated by
  queue publication or by firmware configuration.
- The error capture words stay zero because a failed bus transaction is
  returned as a TLM error, not fabricated as a hardware fault interrupt.

Both selected DDR images contain 1596 candidate 32-byte transfer records
with a `0x1d88xxxx` endpoint and length at most `0x8000`.
This scan is not a proven selected runtime inventory.
The highest candidate endpoint ends at `0x1d887178` in both images.
The virtual data SRAM should therefore cover `0x1d880000..0x1d887fff`
(`0x8000` bytes), not only the first 16-byte destination.

## RAM and event dependencies

The ACEHR descriptor store at `0x18184000/0x18384000` belongs to each
controller instance. It is not the CPUCP data SRAM at `0x1d880000`.
The first GIC snapshot needs three real owners in one DMA-visible route:

1. ACEHR descriptor storage and the passive queue index.
2. The existing APSS GIC redistributor at `0x17090c00`.
3. The shared ACEHR data SRAM at `0x1d880000..0x1d887fff`.

The data SRAM must be reachable by the ACEHR DMA initiator.
It must also be shared with another processor only when that processor has
a proven access to the same physical range.
CPUCP local workspace at `0x18b30000` and its `0x81280000` backup are
separate regions. Correcting that workspace alias does not change the
ACEHR descriptor store or data SRAM contract.

The first mode-one queue publication needs the descriptor store but does
not read GICR or write data SRAM.
A real external event must call `trigger_external` before that snapshot
has a data effect.
Software mode-zero requests use the per-channel start word and require the DMA route.
Only completed transfers can assert completion IRQ sources 76 or 77.
Error sources 72 and 73 are separate handlers and are not asserted on success.

The image strings name memory-operation, PDP, CPU-event, and thermal users.
Those names alone do not prove their event sources or data SRAM endpoints.
No broad route, fake event, or completion is installed for those users.

### Synchronous channel start

SM8850 `0x1d8063e4` validates an allocated mode-zero channel.
At `0x1d806438`, it writes `(old & 6) | 1` to `base + 4 + 4 * channel`.
The helper at `0x1d80644c` checks that channel's completion with a one-millisecond deadline.
Failure returns `-10` and reaches the original fatal ECALL at `0x1d807080`.
Run274 reaches this fatal path, but the panic alone does not identify the failed deadline condition.
The later wait at `0x1d80344a` belongs to the fatal handler, not a normal two-hart boot barrier.

A bit-zero set now executes the published descriptor range through the existing DMA path.
A bit-zero clear acknowledges that channel's completion and interrupt state.
The focused fixture verifies the copied bytes before it accepts completion.
SM8850 run276 and SM8845 run220 pass the former memset panic without a CPUCP exception.
They reach the next startup wait at `0x1d81e592` and `0x1d81dd46`, respectively.
The full integration run passes 47 host tests and 73 platform tests. Clippy passes with warnings denied.

Run595 establishes the deadline failure before the completion-bit test.
The helper adds `0x0f4240` nanoseconds to its current timestamp.
It reads the completion bitmap, reads time again, and checks the deadline before it tests the bitmap.
Branch `0x1d8064e2` takes the timeout path on that first check.
The later rendezvous waits for a second hart that still waits for its startup publication.
Consequently, the PDP loader never executes in this run.

The previous global synchronization quantum was one millisecond.
That resolution could consume the complete firmware budget at a synchronization boundary.
The platform now uses a 100-microsecond quantum.
This change preserves firmware deadlines and DMA completion rules.
It does not change watchdogs or manufacture a completion.

Run597 executes the same memory check at `0x1d8055aa`.
It reaches the success path at `0x1d807084` and the PDP loader at `0x1d80b79e`.
All 88 platform tests pass with the smaller quantum.
The OEM run later stops at an unsupported per-core DVFS vote.
Kernel entry remains unverified.

Run612 contradicts the conclusion that the smaller quantum removes this failure.
It reaches the same `-10` return at `0x1d806528` and the fatal ECALL at `0x1d807080`.
The firmware prints `acehr_memset_prepare: acehr configuration failed -10`.
Run613 uses the same build and passes this check.
The helper reads time from MTIME at `0x18b8bff8` through `0x1d81f8c2`.
The QBox `riscv_aclint_mtimer` returns the native QEMU virtual clock.
With multi-threaded TCG and no instruction counting, that clock follows host time.
[INFERENCE] A host stall of more than one millisecond between the two time reads causes the timeout.
A stall during the blocking `+0x560` read is one possible cause.
The platform therefore does not make this deadline deterministic.
A deterministic deadline needs a different time source for the CPUCP instance, not an ACEHR model change.

Run616 tried single-threaded TCG with instruction counting for the CPUCP instance only.
Each instruction took one nanosecond of virtual time.
The run did not reach the previous frontier before the 300-second limit.
PDP0 executed few instructions, PDP1 did not execute, and the UART output was shorter.
CPUCP took a different path and read the APSS identity word at `0x17651000`.
The platform therefore keeps the native clock until a time source preserves the order between processors.

### PDP image load start (run324)

The SM8850 run324 CPUCP store at `0x1d806438` faults on address `0x181800ac`.
The offset is ACEHR0 channel 42 start (`0x004 + 4 * 42`) and the width is four bytes.
`build/private/run-sm8850-common-runtime324/system.log` records this line:

```
UNMODELED_ACCESS core=platform.cpucp_access pc=0x1d806438 address=0x181800ac width=4 operation=write status=TLM_COMMAND_ERROR_RESPONSE
RUN_FAILED reason=unmodeled-access
```

`build/private/run-sm8850-common-runtime324/console.log` records two warnings at
simulation time `7.025405801 s`. The first warning is the CPUCP router rejection
at offset `0x19930000`. The second warning is the model message "ACEHR DMA read
or write failed" followed by "completion was not published" at
`models/qcom/sm88xx/cpucp/acehr.h:197`.
`qemu-cpucp.log` gives hart 0, cause 7, epc `0x1d806438`, tval `0x181800ac`,
and `desc=fault_store`.

The queue publication and the descriptor decode both succeeded.
The model reached `execute()` and the DMA write failed.
The staged source is readable at `0x1d900000`. The destination has no owner.

The producer is the PDP image loader at SM8850 `0x1d80b79e`:

- Field `+0xf8` of the descriptor at `0x1d82d980` holds `0x19930000`.
  The instance stride is `0x268`.
- The loader copies `[0x81300000,0x81320000)` (`0x20000` bytes) through
  `0x1d900000`.
- Mode 1 sets source `0x1d900000 + offset` and destination
  `0x19930000 + offset`.
- The error string at `0x1d8269b0` reads "Unable to allocate ACEHR SW trigger
  for PDP%d".
- The start store at `0x1d80b97e` calls `0x1d8063e4`. The callback pointer at
  `0x1d80b888..0x1d80b88c` is `0x1d80b3b0`. The wait at `0x1d80b982` polls the
  completion marker.

The copy length and the source range agree with the first load segment of
`IMAGES/pdp.elf`. That segment has file offset `0x1000`, `p_paddr 0x81300000`,
and `0x108b8` bytes. The remaining `0xf748` bytes extend to `0x81320000`.
The copy destination interval is `[0x19930000,0x19950000)`.
The firmware strings `PDP_RAM_CE_Error_IERR_0x0` and
`PDP_RAM_UC_Error_IERR_0x3` at `0x1d827eb8` and `0x1d827f10` name PDP RAM.
The name of the block at `0x19930000` is an inference from those strings.

A reconstruction of the builder at `0x1d8066d2` gives four `0x8000`-byte chunks
of eight-byte beats. The first chunk has these descriptor words:

| Word | Value |
| --- | --- |
| `0` | `0x0000ec8000038fff` |
| `1` | `0x0000000000cc9800` |

The model decoder returns source `0x1d900000`, destination `0x19930000`,
mode 0, width 8, and 4096 beats. The source-range flag is bit 17.
The later chunks add `0x8000` to both addresses and to word 1.

The PDP vector table at file offset `0x1000` gives initial SP `0x2001f500` and
reset handler `0x200018a5`. The PDP image uses local address `0x20000000`.
The CPUCP copy window is `0x20000` bytes, and the PDP SP is inside that size.
These facts support an alias from CPUCP `0x19930000` to PDP local `0x20000000`.
They do not prove that alias.
The CPUCP PDP0 mailbox base is `0x19960000`, which is `0x30000` bytes above the
copy base. This evidence establishes no APSS view of either block.
At run324, the second RAM base lacked independent evidence.
The lane initializer and run561 now establish it. See the next subsection.

No ACEHR register change follows from this failure.
The model returns a failed bus transaction as a TLM error and publishes no
completion (`acehr.h:196-198`, `acehr.h:342-345`).
The evidence identifies a missing memory owner, not a different register
contract.

The platform maps a writable `0x20000`-byte RAM owner at `0x19930000` on the CPUCP router.
The owner retains the staged bytes.
Its initial zero state is virtual policy, not a measured reset value.
This evidence establishes no APSS view or PDP execution alias.

SM8850 run325 passes this DMA destination and reaches a power-control write at `0x19880258`.
The ACEHR guest fixture checks that an unmapped destination cannot publish completion or an interrupt.
The focused test passes.
Neither result establishes UEFI entry or kernel entry.

### PDP1 image load destination (run561)

Run561 completed with a failed eight-byte DMA write at `0x1a930000`.
Its `system.log.zst:system.log` records:

```text
ACEHR_DMA_ACCESS_FAILED model=platform.cpucp_acehr1 address=0x1a930000 width=8 operation=write status=-2
UNMODELED_ACCESS core=platform.cpucp_access pc=0x1d806438 address=0x183800f4 width=4 operation=write status=TLM_COMMAND_ERROR_RESPONSE
```

The start offset `0xf4` selects channel 60: `4 + 4 * 60`.
This failure identifies a missing destination owner, not a rejected descriptor.

Bounded reads of the selected SM8850 `IMAGES/cpucp.elf` establish the second base:

- The template word at `0x1d82da78`, descriptor base `0x1d82d980` plus `0xf8`,
  contains `0x19930000`.
- The initializer at `0x1d8034f4..0x1d803508` selects
  `0x1d82d980 + lane * 0x268` and computes `lane << 24`.
- At `0x1d80358e..0x1d803594`, it adds that value to the template word
  and writes the selected descriptor's `+0xf8`.
- Thus, lane 1 receives `0x19930000 + 0x01000000 = 0x1a930000`.
  The second descriptor is zero in the ELF before this initialization.

The loader at `0x1d80b7ec..0x1d80b7fc` computes source interval
`[0x81300000,0x81320000)` and length `0x20000`.
It limits each request to `0x8000` bytes at `0x1d80b80a..0x1d80b812`.
For load mode 1, `0x1d80b926..0x1d80b946` selects source
`0x1d900000 + offset` and destination `descriptor[+0xf8] + offset`.
The loop at `0x1d80b988..0x1d80b98c` advances by the chunk length.
The resulting lane-1 destination interval is `[0x1a930000,0x1a950000)`.
This proves a `0x20000`-byte copy span without a broad surrounding map.

Arithmetic gives the first descriptor words `0x0000ec8000038fff` and
`0x0000000000d49800`. No run captured a descriptor dump for this transfer.
The existing decoder gives source `0x1d900000`, destination `0x1a930000`,
mode zero, width eight, and 4096 beats.
The existing geometry check accepts this descriptor: mode is zero, the
reserved bits are zero, both addresses keep natural alignment, and the span is `0x8000` bytes.
Thus the model reached the bus write, and only the destination owner was absent.
The four destination chunks start at `0x1a930000`, `0x1a938000`,
`0x1a940000`, and `0x1a948000`.

The registered device inventory gives an independent cross-check.
It records the PDP1 transmitter at `0x1a960000` and the PDP0 transmitter at
`0x19960000`. See `models/qcom/sm88xx/cpucp/subsystem_evidence.md`.
The derived lane-1 RAM base is `0x30000` bytes below `0x1a960000`.
That gap matches the proven PDP0 RAM-to-mailbox gap of `0x30000` bytes.
The two independent constants agree on the lane-1 block position.

The existing fixture at `tools/qcom/qcom-vp/tests/fixtures/cpucp-acehr/consumer.cpp`
lines 320–330 covers the same failure class with an unmapped PDP0 destination.
It requires a command-error response, restored channel state, zero completion
and service bitmaps, and no completion interrupt.
The model returns before completion publication when either DMA access fails.
No ACEHR semantic change follows from run561.
This investigation ran no builds, tests, or boots.

### Asynchronous service publication

Run292 enters the ACEHR interrupt immediately after the start store at `0x1d806438`.
The guest publishes its callback pointer at `0x1d80643c`, after that store.
Immediate service publication can therefore invoke the old callback and leave the snapshot wait unresolved.
Run294 uses a one-microsecond service interval after the successful copy.
This interval is virtual scheduling policy, not measured hardware latency.
Acknowledgement and reset cancel pending service state. No service event survives a failed DMA transfer.
Run294 passes the snapshot wait and reaches runtime clock configuration at `0x17c02020`.
Run295 passes that configuration and reaches the next DMA register source at `0x181c0560`.
SM8845 run229 still takes the default callback because the old model executes at submission before channel start.
Its interrupt occurs at `0x1d8066f2`, before callback publication at `0x1d8066f4`.
The corrected lifecycle removes DMA execution from request and interrupt-mask writes.
This correction prevents an earlier completion from remaining pending when the guest arms its callback.

SM8845 run236 still takes the interrupt at `0x1d8066f4`, before its callback store retires.
Its completion marker at `0x18b302b8` remains `0xfffffffa`.
The guest then waits at `0x1d80bdb8` until the APSS watchdog expires.
Debugger-assisted run237 takes the interrupt after the store and reaches the next register group.
The callback at `0x1d80b806` clears the marker, which confirms the missing producer.

The current service interval accounts for every selected read and write beat.
The common platform selects a 19.2 MHz virtual DMA bus.
Service follows the accumulated bus interval, target delays, and one additional microsecond.
The bus frequency is virtual policy, not a measured ACEHR clock.
Data effects and polled completion remain synchronous with successful DMA.
Acknowledgement and reset still cancel pending service state.

The regression selects 20 MHz and transfers 16 KiB through actual bus transactions.
It requires no service at 100 microseconds and successful service with matching bytes at 300 microseconds.
The focused fixture and all 74 platform integration tests pass.
SM8845 run238 passes the former wait without a debugger.
SM8850 run310 also passes the snapshot and auxiliary PLL initialization.
Both runs next reject the unmapped read at `0x17c30020`.
Neither run establishes kernel entry.




## Fixture

`tools/qcom/qcom-vp/tests/fixtures/cpucp-acehr/consumer.cpp` instantiates both
instances, binds their IRQs, and supplies fixture-only GIC and SRAM targets.
It checks:

- Cold state, instance isolation, queue geometry, and descriptor retention.
- The initializer write `0xf` at `+0x21c` on both instances.
- A passive queue publication with no transfer, completion, or IRQ.
- The captured GICR-to-SRAM descriptor and the selected reverse restore.
- Four real 32-bit DMA reads and four writes per selected descriptor.
- Lower- and upper-half software triggers, including masked pending retention.
- An inclusive two-slot chain, invalid bounds, and partial descriptors.
- Completion and IRQ only after successful DMA, with W1C and per-channel ack.
  An unrelated completion stays pending when another channel is acknowledged.
- Unsupported modes and failed source or destination transfers without completion.
- Address, alignment, width, byte-enable, and cold-reset behavior.

The expected marker is `CPUCP_ACEHR_REQUEST_BOUNDARY_PASS`.

## Integration

Register the model in `src/platform.cc`:

```cpp
#include "qcom/sm88xx/cpucp/acehr.h"
static gs::ModuleFactory::ModuleRegistrationWrapper cpucp_acehr_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,cpucp-acehr-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomCpucpAcehr>(
            "qcom,cpucp-acehr-v1"),
        "CPUCP ACEHR request-controller constructor", cci::CCI_ABSOLUTE_NAME);
});
```

Map both controller apertures and bind their DMA initiators to the CPUCP
router's target socket:

```lua
p.cpucp_acehr_0={moduletype='qcom,cpucp-acehr-v1',
    target_socket={address=0x18180000,size=0x8000,bind=bus},
    dma_socket={bind='&'..o.router..'.target_socket'}}
p.cpucp_acehr_1={moduletype='qcom,cpucp-acehr-v1',
    target_socket={address=0x18380000,size=0x8000,bind=bus},
    dma_socket={bind='&'..o.router..'.target_socket'}}
p.cpucp_acehr_data={moduletype='gs_memory',init_mem=true,
    target_socket={address=0x1d880000,size=0x8000,bind=bus}}
```

Route the CPUCP DMA view of `0x17090c00` to the existing APSS GIC owner.
That owner already maps GICD at `0x17000000` and eight redistributors at
`0x17080000+slot*0x40000`, each of size `0x20000`.
Do not create a second GIC implementation.
Share the `0x1d880000` SRAM owner with any APSS view that needs it.
Bind instance zero `irq` to `cpucp_subsystem.source_76`.
Bind instance one `irq` to `cpucp_subsystem.source_77`.
The error handlers own sources 72 and 73.
The model does not assert an error source for a successful DMA transfer.

Add the fixture to `tools/qcom/qcom-vp/tests/platform.rs`:

```rust
#[test]
#[ignore = "Requires make build and a C++17 compiler"]
fn cpucp_acehr_request_engine_boundary_is_explicit() {
    run_native_consumer("cpucp-acehr", "CPUCP_ACEHR_REQUEST_BOUNDARY_PASS");
}
```

Run it after `make build`:

```text
cargo test --test platform cpucp_acehr -- --ignored --nocapture
```

## Next external boundary

After the `+0x21c` store, the first boundary is CPUCP interrupt-source
programming at `0x4d10/0x4910` and `0x4e10/0x4a10`.
The next ACEHR boundary is passive queue publication at `+0x3e00`.
Run 272 captured the first publication and its complete descriptor.
That publication alone does not perform DMA or report completion.

The active boundary is a software request at `+0x204/+0x208` or a real
external event for a persistent channel.
The model performs mode-zero copies through its bound DMA initiator.
An absent GIC source, SRAM destination, unsupported mode, or failed bus
transfer fails explicitly without publishing completion.
The selected strings name later users: `acehr_memset_prepare`, PDP image load,
CPU event reads, and thermal reads.
The PDP image load now has a recovered mode-zero destination for both lanes.
See "PDP image load start (run324)" and
"PDP1 image load destination (run561)".
The other users' event sources and nonzero hardware modes remain outside the
recovered memory-copy contract.

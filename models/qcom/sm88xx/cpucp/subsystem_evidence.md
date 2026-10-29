# CPUCP subsystem initialization

## Scope and runtime observation

These OEM failures follow the legacy RNMI change:

| Image | Run | Access | CPUCP PC |
| --- | --- | --- | --- |
| SM8850 | `run258` | Read 32 bits at `0x18876104` | `0x812d41e6` |
| SM8845 | `run208` | Read 32 bits at `0x18876104` | `0x81244192` |
| SM8850 | `run259` | Read 32 bits at `0x18860000` | `0x1d803d30` |
| SM8845 | `run209` | Read 32 bits at `0x18860000` | `0x1d803e54` |

These observations establish entry into the selected DDR-resident initialization routine.
They do not establish completion of the CPUCP service or the mailbox transport.
This child performed static recovery only. It did not build, test, run OEM firmware, or commit.

The inputs are `IMAGES/cpucp.elf` from these selected archives:

- `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`, SHA-256 `3475ceab499e02fac22e249e7249ea9ee92c0eb21a260eb9b6f18d185ab70e24`.
- `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`, SHA-256 `5667791c9c681da9fe05c5a91985fa30b9d9d67a8bebfa149cc1193d75c336a4`.

Disassembly starts at identified instruction boundaries.
An unrestricted disassembly of DDR sections can lose alignment in preceding data.
Candidate address constants are not execution evidence.

## Subsystem control and local workspace

SM8850 `0x812d41de..0x812d423c` and SM8845 `0x8124418a..0x812441e8` execute the same sequence:

1. Read `0x18876104`, clear bit zero, and write the result.
2. Read the word, set bit one, and write the result.
3. Poll bit one until it reads as one.
4. Read the word, set bit 31, and write the result.
5. Update a guest initialization bitmap.
6. Clear `[0x18b50000,0x18b70000)` through the guest memset routine.

The final branch targets memset `0x1d81ee94/0x1d81e648`.
The start and end arguments derive from PC-relative address producers in the routine.
The entire 128 KiB interval is required. A smaller page or the former hart-zero marker allocation is insufficient.

The startup callers are `0x1d802f9a/0x1d802ffa` for hart zero.
They pass initialization index one, then enter controller setup and per-hart initialization.
The alternate callers `0x1d802ffe/0x1d80305e` pass index zero.
This is not evidence that a hardware write clears the RAM.

The model makes these explicit virtual choices:

- Control reset value is one. Bit zero inhibits the modeled interrupt path.
- Bit one is a retained enable request. It reads as set only after the inhibit is clear.
- Bit 31 commits the enabled state. The interrupt controller drives outputs only after this commit.
- These functional gate meanings are board policy, not recovered physical register names or measured timing.
- Unsupported control bits fail the transaction. The model does not manufacture additional acknowledgments.
- Control writes do not change RAM. Guest stores perform the complete memory clear.
- One persistent `cpucp_ram` owner covers `[0x18b30000,0x18b70000)`, including this workspace.
- Cold zero is virtual board policy. Architectural CPU reset preserves the owner and its contents.
- DDR snapshot buffers remain separate. No APSS alias is inferred.

Both images also read `0x18876118` bit 18 in later power-path code.
The readers are `0x1d805630/0x1d80587a` and `0x1d80c1be/0x1d80c5f4`.
The current virtual board starts awake and has no CPUCP power-collapse transition.
Thus this read reports zero. It is not a captured silicon reset value or a completed sleep acknowledgment.
Other registers in the aperture remain unsupported.

## Compact PLICv2 geometry

The firmware names the controller `plicv2` in its initialized strings.
Initialization begins at `0x1d801b82/0x1d801be2`.
It reads two words at `0x18875000`:

| Register | Decoded value |
| --- | --- |
| `+0` low 16 bits | Source count `N` |
| `+0` high 16 bits | Hart-context count `H` |
| `+4` low 16 bits | Priority count `P` |

The firmware requires at least 16 priorities for its configured thresholds.
Its configured hart limit is two at `0x1d834200/0x1d8341a0`.
The software callback initialization reserves 256 source entries.
None of these facts measures the silicon ID words.

Let `W=ceil(N/32)`.
The priority-field width is the smallest positive multiple of four that can represent `P-1`.
The initializer derives these contiguous ranges:

1. Trigger configuration starts at `+8`, with `W` 32-bit words.
2. Packed priority fields follow trigger configuration.
3. Per-hart enable banks follow priorities, with `W` words for each hart.
4. Per-hart threshold words follow enable banks.
5. Per-hart claim/completion words follow thresholds.

The code also clears fixed words `+0xf8+4*hart` and `+0x200`.
The model retains these observed configuration words without invented status effects.

The declared virtual geometry is `N=256`, `H=2`, and `P=16`:

| Offset | Modeled range or value |
| --- | --- |
| `0x000` | ID word `0x00020100` |
| `0x004` | Priority count 16 |
| `0x008..0x027` | Eight trigger words, one bit per source |
| `0x028..0x0a7` | 32 packed priority words, eight four-bit fields per word |
| `0x0a8..0x0e7` | Two enable banks, eight words each |
| `0x0e8..0x0ef` | Two threshold words |
| `0x0f0..0x0f7` | Two claim/completion words |
| `0x0f8..0x0ff` | Fixed per-hart initialization words |
| `0x100..0x107` | Two additional per-hart initialization words |
| `0x200` | Fixed configuration word |

This geometry respects the software capacity and avoids overlap with the fixed initialization words.
It is an explicit virtual topology, not a claim that these are the physical ID values.
The current composition instantiates both CPUCP harts.
Each hart uses its own enable, threshold, and claim context.

The functional gateway uses standard PLIC concepts: pending state, per-target masks, priorities, claims, and completion.
The public comparison is QEMU commit `d73149283205645625bfa9fe029cfb68e4c4a9ae`, `hw/intc/sifive_plic.c`.
That GPL-2.0-or-later implementation is a semantic reference. No source code is copied into the BSD model.
The compact register decoder is not compatible with QEMU's standard SiFive PLIC device, so that device cannot replace this boundary directly.

The model selects the highest enabled priority above threshold. Equal priorities select the lowest source index.
Source index zero is valid here. Claim IDs encode `source+1`, and zero means no eligible source.
A claim removes pending and holds the source in service.
The selected firmware completes by writing zero to its hart's claim register, not by writing the source ID.
See claim helper `0x1d802034/0x1d802094` and completion helper `0x1d80201c/0x1d80207c`.
The model permits nested claims and completes them in reverse order.

A level-triggered input that remains asserted becomes pending again after completion.
An edge-triggered input requires a new rising transition after completion.
The gateway does not latch another request while a source is in service.
The signal-socket callback also executes for unchanged writes.
An unchanged asserted input is not a new edge.
Trigger configuration writes do not create or clear pending events.

### Trigger configuration, not software pending

SM8850 `0x812d6b1a` registers 21 PIC sources per cluster.
It reads the enabled-source mask from descriptor `+0x28` and the trigger mask from `+0x2c`.
It registers each handler, configures its trigger, and then enables it.
At `0x812d6b96..0x812d6b9a`, it calls `0x1d801d28` with an encoded interrupt and one trigger bit.
That helper changes the corresponding bit in the first bank.

Run559 captures source 131 at the helper's bit-set branch, `0x1d801d68`.
The caller is `0x812d6b9e`, and the target is `0x18875018`.
The helper writes mask eight after the registration step.
The same initialization selects pulse sources 131, 136, 142, 163, 168, and 174.
The case for source offset 14 names `SMMU correctable error (Pulse)` at `0x1d828ca0`.
The model therefore uses one for edge mode and zero for level mode.

The earlier model treated this bank as writable pending state.
Run558 shows claims for these six sources with every input level zero.
The false source 174 enters `0x1d812752`, which reads `0xa18` through an absent second-cluster block.
Run556 confirms the zero base in descriptor `0x1d82dbe8` at `+0x40`.
The correction preserves the register value without creating an interrupt.
It does not map the invalid address or suppress a genuine hardware event.

The native fixture checks level reassertion and edge rearming through real BEU inputs.
It checks that trigger writes alone produce no claim.
The secure notification fixture observes claims and guest-produced responses, not trigger words as pending state.
The build succeeds, and all 81 platform tests pass.
Run560 passes the false-error path and reaches an ACEHR request at `0x183800f4`.
The kernel entry remains unverified.

## Adjacent per-hart initialization

The next routine is SM8850 `0x812d417a..0x812d41ac` or SM8845 `0x81244126..0x81244158`.
Its computed addresses establish three more writes for hart `h`:

- Clear `0x18875100+4*h`.
- Publish a handler pointer at `0x17c40020+4*h`.
- Publish the same pointer at `0x17c40040+4*h`.

The pointer is `0x1d801298` on SM8850 and `0x1d80129c` on SM8845.
These are the identified RNMI handler entries.
The boot controller retains both vector banks separately.
Publication alone does not reset a CPU, redirect its current PC, or produce an NMI.
The correspondence between these two banks and QEMU's interrupt and exception vector properties remains unproven.
The model does not select that correspondence by guess.

Per-hart initialization then calls `0x1d801b16/0x1d801b76`.
Both images use the configuration table at `0x1d82c000`, with 24-byte entries.
The entries select BEU bases `0x18b70000` and `0x18b71000`.
Initialization writes zero at `+0x20`, then `0xee` at `+0x10` and `+0x28`.
The consumers read cause at `0x1d8018a2/0x1d801902`, then read address halves and accrued state through this table.
Their logging strings identify `BUS-ERROR` and `BEU`.

Rocket Chip commit `ece7b9ad544b07df39cd13b6fd7236562a26355d`, `src/main/scala/tile/BusErrorUnit.scala`, provides the matching public register contract:

| Offset | Rocket register | Modeled behavior |
| --- | --- | --- |
| `0x00` | Cause | Three-bit retained cause, initially zero |
| `0x08` | Value | Retained error address, explicitly zero before any event on this virtual board |
| `0x10` | Enable | Event enable mask, supported positions `0xee` |
| `0x18` | Platform interrupt | Mask for the platform output |
| `0x20` | Accrued | Retained event bits, software writable as in Rocket |
| `0x28` | Local interrupt | Mask for the local output |

The outputs derive from accrued bits and their respective masks.
Unsupported positions remain zero. Both aligned 32-bit halves and 64-bit accesses share each retained word.
No Rocket implementation source is copied.
The virtual board has no cache ECC or TileLink error producer.
Unmodeled bus faults still fail through the existing access monitor rather than become a fabricated recoverable error.
Production BEU outputs remain unconnected because their physical destinations and RNMI vector-bank assignment are not established.
No NMI event is injected.
The native fixture uses software writes to accrued state and explicit fixture-only output bindings to check both mask effects.

## Architectural interrupt input

The final CPU input is now recoverable from the selected instruction and data sequences:

- Controller initialization calls enable helper `0x1d81c5d8/0x1d81bd8c` with unencoded argument `0x10`.
- For an unencoded argument, that helper sets `mie` bit `a0`. Thus it enables architectural interrupt 16.
- The common interrupt entry at `0x1d8010bc` removes the top interrupt bit from `mcause`.
- It indexes a 16-byte callback table at `0x1d834230/0x1d8341d0` by the remaining cause.
- Entry 16 contains handler `0x1d80204c/0x1d8020ac`.
- That handler reads the PLIC claim, calls the registered source callback, and completes through the per-hart register.

The board therefore connects PLIC hart-zero output to `cpucp.irq_in_16`.
This is not a connection from encoded external source `0x1d10` directly to CPU input 28.
The source indices remain nonsecure mailbox 28, secure mailbox 24, and PDP mailbox 40.
The existing pinned QEMU supports nonstandard local interrupt inputs and their `mie` bits.
This slice does not add a CPU patch or change the CPU base.

## CPUCP hardware mailbox boundary

The earlier [mailbox recovery](evidence.md#cpucp-hardware-register-consumers) identifies the register operations.
This slice supplies one retained owner for each reachable CPUCP RX bank:

| Bank | CPUCP TX | CPUCP RX | PLIC source |
| --- | --- | --- | --- |
| Nonsecure AP | `0x17640000` | `0x18820000` | 28 |
| Secure AP | `0x17648000` | `0x18828000` | 24 |
| PDP0 | `0x19960000` | `0x18808000` | 40 |
| PDP1 | `0x1a960000` | Same physical RX owner | Shared through PDP0 setup |

All four devices appear in the selected registered device inventory:

| Name | SM8850 device/configuration | SM8845 device/configuration |
| --- | --- | --- |
| `cpucp_pdp1@1a960000` | `0x1d823588/0x1d82de50` | `0x1d823098/0x1d82de10` |
| `cpucp_pdp0@19960000` | `0x1d8235b0/0x1d82de80` | `0x1d8230c0/0x1d82de40` |
| `cpucp_ap_ns@17640000` | `0x1d8235d8/0x1d82deb0` | `0x1d8230e8/0x1d82de70` |
| `cpucp_ap_s@17648000` | `0x1d823600/0x1d82dee0` | `0x1d823110/0x1d82dea0` |

The PDP1 configuration has a null interrupt setup callback, but it is still a registered device.
The model maps its transmitter through `auxiliary_tx_socket`, with separate retained commands.
Both PDP devices use RX base `0x18808000`, so they share pending, MAP, enable, and receive-command state.
No second RX owner or duplicate interrupt output is created.

Each bank declares virtual IDR `0x181`: bit zero is one, channel count is 64, and group count is one.
The bit-zero physical meaning and silicon geometry remain unmeasured.
The selected configurations use group zero, and channel descriptors include channels zero and two.
This geometry admits those accesses without returning a zero count that silently disables registration.

The bank supports aligned 32-bit and 64-bit accesses:

- TX and RX commands occupy `+0x100+8*channel`.
- TX low writes retain data. TX high writes, or a complete 64-bit TX write, ring the hardware peer.
- MAP at `+0x4000` selects channels for the single interrupt group.
- Status at `+0x4400` exposes pending channels selected by MAP.
- W1C at `+0x4800` clears only the written channel bits.
- Enable at `+0x4c00` masks interrupt assertion without discarding pending data.
- RX command reads do not clear pending. This matches the CPUCP read-then-clear sequence.

[Linux's CPUCP mailbox driver](https://raw.githubusercontent.com/torvalds/linux/master/drivers/mailbox/qcom-cpucp-mbox.c) confirms command-high send and MAP, status, W1C, and enable offsets.
It also keeps command consumption separate from interrupt clearing.
The selected firmware supplies the additional 64-bit access and geometry requirements.
The implementation uses the existing project's retained-register and signal-socket patterns.
It does not copy Linux driver source.

`peer_path` declares an optional same-channel hardware peer for a virtual topology.
`auxiliary_peer_path` applies the same rule to the separate PDP1 transmitter.
A peer receives the complete retained command and a pending bit, not a synthesized service result.
No queue, SCMI header, FREE word, grant, callback list, or response bytes are created by the model.
A production TX doorbell without a declared peer emits a diagnostic and fails the transaction.
It cannot silently report delivery or completion.

Production peer paths remain empty because APSS cross-aperture and channel translations remain unresolved.
The synthetic fixture declares its two peer paths explicitly.
That fixture route is not installed as a physical OEM route.

## Hardware mutex inventory

The `0x18860000` failure is not another mailbox geometry read.
SM8850 `0x1d803d0e..0x1d803dba` and SM8845 `0x1d803e32..0x1d803ede` implement fallback compare-and-exchange with a hardware mutex:

1. Read the owner word.
2. Reject recursive acquisition when the current hart already owns it.
3. Write token `1 << mhartid`.
4. Read the owner again to determine whether arbitration succeeded.
5. Perform the conditional update in guest memory only after acquisition.
6. Write zero to release ownership.

Allocation initialization at `0x1d803ea6/0x1d803fca` prepares two software pools.
Both pools declare ten hardware slots and the same base `0x18860000`.
They do not describe two physical hardware banks.
Slot zero protects allocation, while the remaining slots are selected through the guest allocation tables.
The complete hardware interval is `[0x18860000,0x18860028)`, with four-byte words.
The pools and their allocated handles remain guest-produced objects.

The existing `QcomSmemMutex` implements the required contract: a nonzero write claims only a free word, reads return the owner, and zero releases it.
The composition reuses ten instances rather than introducing another mutex implementation.
The [Linux Qualcomm hardware-spinlock driver](https://raw.githubusercontent.com/torvalds/linux/master/drivers/hwspinlock/qcom_hwspinlock.c) provides the same write-token, read-owner, release-zero sequence.
Its physical strides and lock count are not substituted for the selected firmware's ten contiguous words.

Physical lock one at `0x18860004` already belongs to `qsee_ticket_guard` in the APSS view.
The CPUCP view aliases that same atomic owner.
Other words have one owner each and direct APSS aliases when the production helper receives its APSS router.
CPUCP and QSEE therefore cannot acquire independent copies of the same physical lock.
The synthetic fixture exercises contention, release, independent slot nine, and the shared lock-one owner.

## Native machine timer

After the mutex change, these real OEM stops occur:

| Firmware | Run | Access | PC |
|---|---|---|---|
| SM8850 | `run260` | R64 `0x18b8bff8` | `0x1d81f8c6` |
| SM8845 | `run210` | R64 `0x18b8bff8` | `0x1d81f07a` |

Both selected ELFs use a CLINT-compatible machine timer.
The following addresses come from their instructions, not from a vendor-name match.

| Operation | SM8850 PC | SM8845 PC | Contract |
|---|---|---|---|
| Read time | `0x1d81f8c6` | `0x1d81f07a` | R64 MTIME at `0x18b8bff8` |
| Initialize time | `0x1d8032ca` | `0x1d80332e` | W64 MTIME from the shared counter value |
| Initialize deadline | `0x1d81f712` | `0x1d81eec6` | W64 MTIMECMP for the current hart |
| Enable timer interrupt | `0x1d81f714` | `0x1d81eec8` | Call the interrupt-enable helper with argument 7 |
| Read deadline for suspend | `0x1d80bec4` | `0x1d80c2fa` | Read the current comparator value |
| Disable deadline for suspend | `0x1d80becc` | `0x1d80c302` | Write all ones to MTIMECMP |
| Restore time on hart zero | `0x1d80bfc4` | `0x1d80c3fa` | Write the retained MTIME value |

The comparator address producer computes `0x18b84000 + 8 * mhartid`.
Its constant is `((0x03171000 - 0x800) * 8) & 0xffffffff`.
The timer initialization routines start at `0x1d81f6c0` and `0x1d81ee74`.
They divide MTIME by `0x4b00`, retain the rounded tick base, and program the next period.
`0x4b00` is 19200 ticks.

Both images have the same initialized timebase descriptor at `0x1d82d830`.
It contains the shared counter address `0x1781d000`.
Its frequency field at `0x1d82d840` is `0x0124f800`, or 19200000 Hz.
Thus, the 19200-tick period is one millisecond.
The time-initialization routine reads this shared counter and writes its value to MTIME.
Do not replace that guest copy with a host-seeded counter or an alias between the two counters.

The machine interrupt table also identifies the timer handler at index 7.
The handler pointers are `0x1d81f720` at `0x1d8342a8` and `0x1d81eed4` at `0x1d834248`.
The timer uses architectural MTIP, interrupt 7, not a compact-PLIC source.

The board reuses the installed QBox `riscv_aclint_mtimer` component.
Its native libqemu implementation is `hw/intc/riscv_aclint.c`.
The board sets `timebase_freq=19200000`, `timecmp_base=0`, and `time_base=0x7ff8`.
Its MMIO window is `[0x18b84000, 0x18b8c000)`.
This window does not include the separate MSWI aperture at `0x18b80000`.
The board leaves `provide_rdtime=false` and adds no new CSR-time behavior.

The native counter scales QEMU virtual time and retains a writable time offset.
It does not increment because a guest read occurs.
Comparator writes update pending state and schedule a native timer event.
Writes to MTIME recompute the comparisons.
The native device supports 64-bit accesses and low/high 32-bit accesses.
No host event substitutes for a guest comparator write.
No new CPU-reset hook or preloaded deadline is added.

The current composition instantiates only CPUCP hart zero.
The timer therefore contains one comparator for that architectural hart.
The address formula does not establish the full physical hart inventory.
The CLINT-compatible layout also does not identify the physical CPU vendor or core.
The existing CPU base and opt-in legacy RNMI selection remain unchanged.

## Runtime source identity and policy views

The system identity reads are SM8850 `0x812d401a` and SM8845 `0x81244012`.
They read `0x01fc8000` through the existing TCSR identity owner.
The identity structure bases are `0x18b30570` and `0x18b30588`.
The model does not initialize those structures.

The next group reads corrected `PTE_ROW4_MSB` at `0x221c20ec`.
It retains bit 8 at `0x18b3057b` or `0x18b30593`, not `0x18b3017b`.
The paired read at `0x221c8780` is the existing consolidated debug vector zero.
It is not `QC_CONFIG0`.
Its bit 11 goes to the next byte in each identity structure.
The [fuse evidence](../fuse_controller/evidence.md#cpucp-manufacturing-and-debug-identity-group) records the complete read group and all five identified PTE-flag consumers.
The shared fuse owner exposes one required, read-only manufacturing word.
The board declares virtual word zero without changing debug or authentication policy.
Bit names differ across SoCs, so this policy does not establish a physical die-coordinate meaning.

The next reads are CPU presence at `0x1765100c` and disabled LLC groups at `0x17651020`.
They use the existing shared APSS identity owner, not the fuse owner.
Both selected cluster tables contain CPU masks `0x3f/0x03` and LLC masks `0x0fff/0x0fff`.
The board declares CPU presence `0x033f` and no disabled LLC groups.
The [topology evidence](../apss_boot/evidence.md#cpucp-runtime-topology-words) records the bit mapping, the SM8845 guest cap, and native fixture examples.
The existing profile and variant words remain unchanged.


## Verification

With the integrated model built, run:

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform cpucp_subsystem_routes_real_mailbox_interrupts_and_preserves_workspace -- --ignored --exact
```

The two native guests check geometry-derived offsets, source 28 to architectural interrupt 16, priority threshold masking, claim/completion, and subsystem commit gating.
They check ordered command-low/high publication, 64-bit RX commands, high channel bits, masked pending retention, unrelated W1C preservation, and a real guest-produced return command.
They access both ends of the full local workspace and clear the entire interval through guest stores.
They also check per-hart initialization words, retained recovery-vector publication, and BEU local/platform mask effects.
All four mailbox transmitters and all three RX owners expose geometry in the fixture.
PDP0 and PDP1 low-word readback checks keep their transmit state independent.
The hardware-lock checks cover busy-owner preservation and acquisition after release across the two CPU views.
A second variant requires an explicit failure and diagnostic when the sender has no peer.
The timer extension checks writable 64-bit time, low/high 32-bit time and comparator accesses, and 64-bit comparator readback.
It checks that a past comparison sets MTIP while global MIE is clear and that a later deadline clears it.
It then waits for a real future timer event with local MTIE enabled and global MIE clear.
After wakeup, it enables global MIE and requires exactly one architectural interrupt-7 entry.
The handler disables the comparator and checks MTIP withdrawal before return.
The deadline check allows the native backend's one-tick integer-time conversion boundary.

The first subsystem revision built successfully.
Its focused fixture then timed out. A retained diagnostic trace showed two architectural interrupt-16 entries instead of one.
The first source callback incorrectly treated every unchanged asserted write as another request, including writes during service.
RX command reads repeat the mailbox's asserted IRQ output, which caused the duplicate.
The corrected gateway accepts rising transitions only while it is not in service and samples the level again at completion.
The fixture still requires exactly one interrupt and now fails immediately on a duplicate.
The corrected subsystem fixture passes, including the native timer extension.
SM8850 run261 passes the timer initialization and reaches the system identity read at `0x01fc8000`.
The platform now routes that CPUCP address to the existing TCSR identity owner.
SM8850 run262 and SM8845 run211 pass that shared view and stop at fuse address `0x221c20ec`.
Their failing PCs are `0x812d41b2` and `0x8124415e`, respectively.

OEM acceptance must record the real stores at control `0x18876104` and the subsequent guest memory clear.
Then record PLIC ID reads, the source callback registration, and the first mailbox command-high writer.
A mailbox service claim requires an established peer route and actual guest request consumption and response publication.
This change alone does not establish those events.

## Native software interrupts

The CPUCP scheduler writes one at `0x18b80000 + 4*hart` after it publishes work.
SM8850 run279 reaches this store at `0x1d81e3ea`. SM8845 run221 reaches it at `0x1d81db9e`.
The platform maps the separate MSWI aperture through QBox's existing `riscv_aclint_swi` wrapper.
Pinned QEMU connects each output directly to the corresponding CPU's machine software interrupt.
Writes of zero clear MSIP. Reads return the actual pending state.

The secondary fixture requires an architectural interrupt with cause `0x8000000000000003`.
Its handler clears MSIP and checks the readback before it publishes completion.
The focused native fixture passes.
SM8850 run281 and SM8845 run222 pass the scheduler store and reach the next access at `0x188787a0`.
Their next failing PCs are `0x1d80c384` and `0x1d80c7ba`.

## Sequencer instruction and configuration storage

Both CPUCP ELFs publish ten upload descriptors with a 32-byte stride.
The SM8850 table starts at `0x812fc0f0`. The SM8845 table starts at `0x812714b0`.
Each descriptor supplies a source pointer, length, type, destination, lane stride, and lane mask.
SM8850 `0x812d5efe` and SM8845 `0x81247774` iterate over these descriptors.

| Type | Destination | Length | Encoding |
| --- | --- | --- | --- |
| 0 | `0x188c8000`, `0x188d8000` | `0x30a` bytes | Word stores cover `0x30c` bytes. |
| 1 | `0x188c8470`, `0x188d8470` | `0x32` words | Index/value pairs select sparse branch words. |
| 2 | `0x18877000` | `0x4c` bytes | Each source byte occupies one destination word. |
| 3 | `0x18877604` | Two words | One pair selects the existing setup word at `+0xc`. |
| 4 | `0x188b8000` | `0x200` bytes | The guest copies aligned words. |
| 5 | `0x188b8470` | `0x20` words | Index/value pairs select sparse branch words. |
| 7 | `0x188a0000` | `0x26` bytes | Word stores cover `0x28` bytes. |
| 8 | `0x188a0028` | `0x34` words | The guest copies words and publishes lane selectors. |
| 10 | `0x180c0198`, `0x182c0198` | `0x1b` words | Word stores use eight-byte spacing. |
| 11 | `0x180c01b4`, `0x182c01b4` | `0x2f` words | Word stores use eight-byte spacing. |

SM8850 `0x812d5e24` implements rounded word copies. `0x812d5c1c` implements index/value writes.
Routine `0x812d5cdc` implements the expanded byte and strided word forms.
The platform maps only the decoded storage extents and sparse words.
It does not load instruction bytes from the host or manufacture completion.

The shared `qcom,pdp-cprh-v1` owner now retains the type-10 and type-11 slots.
Its APSS and CPUCP faces share one state for each lane.
The PDP can therefore update the same words without a separate alias backing.
This cutover preserves the sparse upload geometry and does not execute the uploaded instructions.

Two post-upload descriptors start at SM8850 `0x812fc0a0` and SM8845 `0x81271460`.
They select configuration records at `0x188c0000`, `0x188d0000`, and `0x188b0000`.
The two lane banks contain indices zero through 94. The shared bank contains 36 explicit indices through 80.
Each hardware record has a 32-byte stride and six implemented words.
SM8850 `0x812d5956` packs the guest records and writes these words.
It then updates lane policy words at `+0x8808` and `+0x8810`.
The `qcom,cpucp-sequence-configuration-v1` model retains the records and rejects undeclared indices and padding words.

This profile retains instruction and configuration storage without autonomous sequence execution.
It creates no power transition, interrupt, reset, or guest acknowledgement from these writes.
An execution request requires separate evidence and implementation.

SM8850 run288 and SM8845 run225 pass the upload and record programming routines.
They next read `0x18871000`, at PCs `0x1d81a616` and `0x1d819dcc`.
The full integration run passes 47 host tests and 74 native tests. Clippy passes with warnings denied.

## Low-power telemetry

SM8850 `0x1d81a612` and SM8845 `0x1d819dc8` enable three low-power counters at `0x18871000`.
They also configure the duration bank at `0x18871100`.
The read function selects event counts at offsets `8`, `0x10`, and `0x18`.
It reads three 64-bit duration values from `0x108..0x11c`, then scales the result by `2/19200`.
Its diagnostic identifies invalid selections as `SS_LPM` counters.

The virtual profile supplies no low-power transition source for this bank.
The model retains enable controls and reports zero events and residence time.
Enable writes cannot create activity. Counter writes and unknown offsets remain errors.
The native subsystem fixture checks this no-event boundary after both enables are written.
SM8850 run289 passes initialization and next reads `0x181d2604` at PC `0x1d80845c`.

### Core hang detectors

The CPUCP heartbeat code addresses this module at `0x18870000`.
It defines two detectors and five shared words.
The private layout supplies the module and field names in this table.

| Offset | Register | Fields |
| --- | --- | --- |
| `0x00 + 4*r` | `CORE_r_HANG_COUNTER_CFG` | `EN` bit 0 |
| `0x10 + 4*r` | `CORE_r_HANG_COUNTER_THRESHOLD` | `TH` bits 31:0 |
| `0x20 + 4*r` | `CORE_r_HANG_COUNTER_IRQ_CLR` | `CLR` bit 0 |
| `0x30 + 4*r` | `CORE_r_HANG_COUNTER_IRQ_STS` | `STS` bit 0, read-only |
| `0x40` | `FIRST_CORE_HANG_STATUS` | `CORE_NO` bits 1:0, read-only |
| `0x48`, `0x4c`, `0x50` | Security and clock-gate overrides | Bit 0 |
| `0x54`, `0x58` | Edge-detect configuration | Bits 2:0 and bit 0 |

The low-power banks of the same module are at `+0x1000` and `+0x1200`, so the SM8850 module base is `0x18870000`.
CPUCP `0x1d803026..0x1d80304c` writes threshold `0x2ef2c` at `0x18870010 + 4*hart`, then writes one at `0x18870000 + 4*hart`.
The same thread then logs statistics every 60 seconds. No recovered CPUCP path writes the detector again.

A detector counts the time that its hart makes no progress.
An emulated hart retires instructions while it executes and stops in WFI. No stall source exists, so no counter reaches its threshold.
The platform retains the writable fields. The status words read zero, and nonzero writes to them fail.
Run618 stops at the threshold write of hart one, `0x18870014` from PC `0x1d80303e`.

## Level request controllers

The four descriptors at SM8850 `0x1d82e730` and SM8845 `0x1d82e6f0` select two controllers per lane.
Their bases are `0x181c8000`, `0x181d0000`, `0x183c8000`, and `0x183d0000`.
The initializer at SM8850 `0x1d80843c` and SM8845 `0x1d8087ee` configures offsets `0x2604`, `0x260c`, `0x2620`, `0x262c`, `0x26b0`, and `0x26b4`.

SM8850 `0x1d80817a` controls enable bits zero and four at `+0x2604`.
Routine `0x1d8082da` writes `0x30000 | (enable << 7)` to `+0x2600`.
Routine `0x1d8081f6` writes `0x100000 | level` to the same command word.
Both poll `+0x26e0` until bits 17:16 clear. A level request also requires the low 16 bits to equal the target.
The model completes these operations synchronously only after the controller enable fields permit them.
It retains the accepted level when disabled. Unsupported commands and writes to status remain errors.
This functional transition replaces analog settling, not firmware execution or protocol responses.

The PDP target descriptors select the same four bases at `0x8132bb54`.
The level request owner covers only offsets `0x2600..0x26e3` within each target.
Its socket accepts the CPUCP binding and the APSS alias.
Both masters share the accepted level and configuration.
The separate CPRH target owner cannot shadow this window.
See [the target evidence](../pdp/evidence.md#cprh-target-configuration).

PDP `0x813233de` reads `+0x2608` before an ACD calibration.
It returns true when the CPRH voltage in `CPRH_STATUS_0[17:6]` is less than or equal to bits 15:0 of that word.
Otherwise caller `0x813224cc` reads target status `+0x824` and requires bits zero and two.
No firmware image writes `+0x2608`, and no register definition names it.
The model therefore reads the word as virtual zero and rejects writes.
The PDP then depends on the target status that the closed-loop measurement sets after loop enable.
`docs/qcom/sm88xx/stubs.md` records this substitute.

SM8850 run291 passes the controller and reaches an ACEHR copy from `0x1d900000`.
The upload routine uses a separate `0x20000`-byte local snapshot store at that address.
Its source interval is `[0x81300000,0x81320000)`, calculated at `0x1d80b7ec..0x1d80b7fc`.
The local store has independent zero-initialized backing. The DMA engine performs all subsequent guest-requested copies.

## CPU thermal snapshot sources

The `cpuss_thermal` module uses ACEHR1 channel 61 to copy ten descriptors, with 72 bytes in total.
SM8850 source rows start at `0x1d830278`. SM8845 rows start at `0x1d8302f0`.
Their lane IDs, first sample indices, aggregate indices, and register bases match.
Both package DTBs set `cfg_cpu_present=0`, so the constructors retain the board's six-plus-two core topology.

| Source interval | Meaning |
| --- | --- |
| `[0x181c04f4,0x181c0524)` | Six lane-zero sample pairs |
| `[0x181c0560,0x181c0564)` | Lane-zero aggregate |
| `[0x183c04fc,0x183c050c)` | Two lane-one sample pairs |
| `[0x183c0560,0x183c0564)` | Lane-one aggregate |

SM8850 producers `0x1d815b4a..0x1d815b98` and `0x1d815c10..0x1d815c6a` construct the aggregate and pair copies.
SM8845 equivalents are `0x8124a57e..0x8124a5cc` and `0x8124a658..0x8124a6ba`.
The consumers require validity bit 16 and divide the low 16-bit temperature by ten.
The pair consumer computes minimum and average temperatures from independently valid words.

The virtual model supplies a declared 25-degree environment with valid samples.
The model `qcom,cpuss-llm-v1` owns the same block as the PDP configuration.
It accepts the recovered sample words and the recovered configuration words.
A write to a sample word fails, and every unlisted offset keeps its address error.
It is not an alias to TSENS, whose encoding and sensor wiring differ.
The actual DMA engine copies the sample words into the existing CPUCP image owner.
SM8850 run296 passes the failed source access and executes the thermal consumer through `0x1d815f44`.
That run times out later and does not establish kernel entry.

## LLM limits block and PDP configuration

The PDP limit driver programs the same block that holds the thermal samples.
One aperture starts at `0x181c0000` and the other starts at `0x183c0000`.
The level request banks at `0x181c8000` and `0x183c8000` bound each aperture to `0x8000` bytes.
The platform binds the owner to the CPUCP router and adds an APSS alias at the same address.
A PDP system access uses local address `0x181c0000`, and the APSS router carries it to that alias.
The alias and the CPUCP bind reach one owner, so both requesters see the same words.

The PDP reads its base from the record pointer at `0x2001db2c`.
`0x81302660` returns that pointer address, and `0x8130b214` returns `*(record + 0)`.
The static table at `0x8130d1b4` holds `{0x2000d1bc, 2}`.
The two rows at `0x8130d1bc` are `{0x181c0000, 0x00030000, 0x00020001}` and `{0x183c0000, 0x00030001, 0x00020001}`.
No code reference to that table exists in the image. A data word at `0x8130cbc4` points to it.
The image `IMAGES/pdp.elf` has SHA-256 `74fb165a..7e8d` and comes from the OEM archive `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`.
PDP segment zero loads at system `0x81300000`, and its local address is `system - 0x61300000`.

Run575 fails at `pc=0x813263e6 address=0x181c0380`.
That store is the first word of the mode-zero DDR table, so PDP0 programs lane zero.
No run programs lane one, and both lanes share one table, so the model owns both apertures.

### Initialization tables

`0x8130aee0` reads the version word `[0x8130202e() + 0x14]` and compares it with `0xa7`.
It also takes the mode from the `K_CONFIG valid_Aging_Volt_0x17` value.
Four tables exist, and the guest writes every word before a later read:

| Entry | Revision | Mode |
| --- | --- | --- |
| `0x813263d4` | not `0xa7` | `0` |
| `0x81325ecc` | not `0xa7` | `1` |
| `0x81300a4c` | `0xa7` | `0` |
| `0x81300f60` | `0xa7` | `1` |

`0x813263d4` holds 143 dword stores and no load of the block.
The first store is at `0x813263e6`, and the last store is at `0x813268aa`.
The branch at `0x81326742` skips a literal pool and continues at `0x81326778`.
The mode-one tables add `+0x390`.
The two DDR tables add `+0x3bc..+0x3d0` and `+0x2810`.
The values differ, and the model keeps only the offset set.

| Offset group | Content |
| --- | --- |
| `0x380`, `0x390`, `0x394..0x3d0`, `0x410`, `0x430`, `0x450`, `0x800` | Trip-point map and monitor setup. |
| `0x880`, `0x890..0x89c`, `0x900`, `0x910`, `0x914`, `0x980`, `0x990`, `0x994` | Limit words, resource identifiers, and thresholds. |
| `0xa80`, `0xa90..0xaa4`, `0xb10..0xb1c` | Current and power limits. |
| `0x1000`, `0x1018`, `0x101c`, `0x1024`, `0x1098`, `0x109c` | Trip-point enable bitmap and control words. |
| `0x1110..0x114c` | Sixteen trip-point temperature words. |
| `0x142c`, `0x1680`, `0x1698..0x16b0` | Bank controls and sample windows. |
| `0x1a80`, `0x1a90..0x1aac`, `0x1b10..0x1b1c` | Limit tables. |
| `0x1b90..0x1bcc`, `0x1bd4..0x1bdc`, `0x1be0..0x1c08` | Sixteen trip-point records. |
| `0x1c0c`, `0x1c14..0x1c4c`, `0x1c54..0x1c8c` | Sixteen response records. |
| `0x2390..0x23cc` | Sixteen trip-point level words. |
| `0x2410`, `0x2414`, `0x2490`, `0x2494` | Settling words for sides one, two, and three. |
| `0x2514`, `0x2794`, `0x2810`, `0x2814`, `0x2980`, `0x2a60` | Unit fields and accumulators. |
| `0x0c4`, `0x2984`, `0xb90`, `0x2890` | Limit enable bits and status words. |

Gaps inside a written group stay unwritten by every table.
`0x1c10`, `0x1bd0`, `0x1c50`, and `0x1020` are examples.
The model keeps each gap as an address error.

The guest writes `0x2c` to `0x1000`, which enables trip points 2, 3, and 5.
The same table writes `0x1118`, `0x111c`, and `0x1124` with `0x41a`, `0x442`, and `0x226`.
Each `0x1b90` entry calls `FUN_20000998` with the entry index.
The `0x890` and `0x894` words keep mask `0xff003113` and take one identifier byte at `[23:16]`.
The `0x880` word keeps bits `[31:2]` and takes the mode at `[1:0]`.

Run576 passes the initialization stores and reaches interrupt registration.
Its first unsupported read is `0x181c2a40` at local PC `0x20002c2a`.
`llm_register_interrupt_handler` at `0x20002d58` selects three callback pairs from the table at local SRAM `0x2001d098`.
The table resides at ELF file offset `0x12098`, not in the DDR code segment.

| Enable helper | Offset | Operation |
| --- | --- | --- |
| `0x20002c24` | `0x2a40` | Keep mask `0x00331311`, then set bit 5. |
| `0x20002c70` | `0x2a40` | Keep mask `0x00331131`, then set bit 9. |
| `0x2000b1bc` | `0x2a10` | Set bit 8. |

The model retains these enable controls from declared zero.
They do not supply pending bits or cause interrupts by themselves.
Separate handlers read pending bits at `0x2ba0` and `0x2b70`.
The model does not yet implement those pending registers.

### Limit paths

The recovered limit paths do not poll the block.
Their reads do not wait for a status or acknowledgment bit.
These instruction addresses are system addresses.

| Offset | Operation | Value or mask | Instruction |
| --- | --- | --- | --- |
| `0x890` | read and write | keep `0xff003113`, set `[23:16]` | `0x813026f8`, `0x81302706` |
| `0x894` | read and write | keep `0xff003113`, set `[23:16]` | `0x8130270a`, `0x81302712` |
| `0x880` | read and write | keep `[31:2]`, set `[1:0]` | `0x81302716`, `0x81302726` |
| `0xc4` | read and write | set bit 0 | `0x81302748`, `0x81302750` |
| `0x2984` | read and write | set bit 0 | `0x8130b22a`, `0x8130b230` |
| `0x2984` | read and write | clear bit 0 | `0x8130b23e`, `0x8130b244` |
| `0x1000` | read and write | one bit at the `+6` index | `0x813027b0`, `0x813027ba` |
| `0x1000` | read | one bit at the `+6` index | `0x813027d0` |
| `0x1000` | read and write | one bit at the `+10` index | `0x81302860`, `0x8130286a` |
| `0x1110 + 4 * i` | read and write | keep bit 16, set the low half | `0x813027f4`, `0x813027fe` |
| `0x2390 + 4 * i` | read and write | clear `0xc000` and `0x98`, set the value at bit 4 | `0x81302816`, `0x81302826` |
| `0x2390 + 4 * i` | read | whole word | `0x8130283e` |
| `0x1110 + 4 * i` | read and write | keep bit 16, set the low half | `0x81302886`, `0x81302896` |
| `0x2a60` | read and write | low half plus a signed delta, high half plus the carry | `0x8130289e`, `0x813028ac`, `0x813028b0`, `0x813028be` |
| `0x1110 + 4 * i` | read | sign-extended low half | `0x813028d6` |
| `0x1a80` | read and write | set or clear bit 0 | `0x813028ee`, `0x813028f6`, `0x813028fe` |
| `0x2890` | read | low half and bit 16 | `0x8130290e` |
| `0xb90` | read | bit 16, then `low * 0x17 / 0x78` | `0x8130292a` |
| `0x2410 + 4 * i` | read and write | side one keeps `[31:16]`, side two keeps `[15:0]` | `0x81302b94`, `0x81302b9c`, `0x81302ba6` |
| `0x2490 + 4 * i` | read and write | write the low half | `0x81302bb6`, `0x81302bb8` |

`0x81302b6c` is the only user of the `0x2410` and `0x2490` banks, and its only caller `0x81302bdc` passes index zero.
`0x81302920` reads `0xb90`, and `0x81302904` reads `0x2890`.
Each read tests bit 16 as a validity flag.
A clear flag makes the caller keep no measurement.
The model starts both words at zero, so the flag stays clear.

### CPUCP access

The CPUCP image holds the two block bases at `0x1d830280` and `0x1d830290`.
The rows around them hold `0x500`, `0x701`, and the code pointer `0x1d813332`.
The image holds no other complete address inside either aperture.
The offsets `0x4e0`, `0x4f4`, `0x4fc`, and `0x560` never appear as constants.
The image computes them from a base and an index, so the CPUCP reads the sample words and the aggregate.

### Open items

No run yet programs lane one.
The record indices at `+6`, `+8`, and `+10` come from a runtime record that the image does not contain.
The trip banks therefore accept sixteen entries, and a reader must confirm the used indices.
The CPUCP image may write another offset through a computed address. No such write appears in the recovered rows.
DDR-half routines that receive the base as an argument were not enumerated. No literal reference to the record exists in the DDR half.

## CPUCP post-ROM control configuration

Run577 stops at `0x18860200`, from CPUCP PC `0x1d808e1e`.
The `.700` CPUCP helper at `0x1d808e16..0x1d808ef6` contains only configuration accesses and returns without a poll.
Its caller at `0x1d808f04` can log `CPUSS Boot Cluster: %d booted by CPBL`.
The call at `0x1d808fc4` enters this helper.

| Address | Guest operation | Access PCs |
| --- | --- | --- |
| `0x18860200` | Clear bit 0 | `0x1d808e1e`, `0x1d808e26` |
| `0x18860204` | Clear bits 2:0 | `0x1d808e28`, `0x1d808e2c` |
| `0x1887d128` | Clear bit 0 | `0x1d808e36`, `0x1d808e3e` |
| `0x1887d124` | Replace bits 2:0 with 2, then clear mask `0x10110` | `0x1d808e40..0x1d808e54` |
| `0x18875208` | Set bit 0 | `0x1d808e5e`, `0x1d808e64` |
| `0x1887e034` | Clear bit 0 | `0x1d808e6a`, `0x1d808e6e` |
| `0x18870050` | Clear bit 0 | `0x1d808e74`, `0x1d808e78` |
| `0x18877708` | Clear bit 0 | `0x1d808e82`, `0x1d808e8a` |
| `0x1887770c` | Clear bits 2:0 | `0x1d808e8c`, `0x1d808e90` |
| `0x1887c104` | Clear bit 0 | `0x1d808e9a`, `0x1d808e9e` |
| `0x12004320` | Clear bit 0 | `0x1d808ea8`, `0x1d808eac` |
| `0x18878444` | Clear bit 0 | `0x1d808eb6`, `0x1d808eba` |
| `0x188782f0` | Clear bits 2:0 | `0x1d808ec0`, `0x1d808ec4` |
| `0x188780c0` | Write zero | `0x1d808eca` |
| `0x18878424` | Write zero through the existing domain owner | `0x1d808ed2` |
| `0x188a1028`, `0x188a2028` | Write zero | `0x1d808eda`, `0x1d808ee2` |
| `0x188a503c`, `0x188a6028` | Write zero | `0x1d808eea`, `0x1d808ef2` |
| `0x1887e030` | Write `0xff` in the caller | `0x1d808fdc` |

The composition reuses `qcom,retained-boot-control-v1` for nineteen previously unowned words.
Each word starts at declared virtual zero and retains writes within its recovered mask.
These controls do not create completion, clock, reset, or interrupt effects.
Their hardware identities and side effects remain unresolved.
The existing domain owner continues to handle `0x18878424`.

### Native verification

The build and all 84 platform tests pass.
Run576 executes the LLM table and exposes its missing interrupt enable control.
Run578 passes that control and reaches PDP PC `0x813242aa`.
Its next unsupported write is the separate CPRH address `0x180c0198`.
Run578 records 14624 UART bytes, but `kernel_entry` remains false.
The UART bytes do not establish a Linux boot.

## Clock-gating overrides

### NSI banks

Run605 stops at a 64-bit read of `0x18000b70`, from CPUCP PC `0x1d808d44`.
The caller is the post-ROM sequence at `0x1d808fc8`, directly after the control configuration above.
Function `0x1d808d08` walks two lane descriptors at `0x1d82d980`, with stride `0x268`.
Each descriptor names six NSI bases, and the function clears one override field and one enable bit in each.
It does not poll a status word.

| Lane-zero base | Module | Words and operation |
| --- | --- | --- |
| `0x18000000` | NSI control plane | `0xb70` becomes zero, `0xb90` keeps bits 63:40, `0xb78` clears bit 0 |
| `0x18040000` | NSI debug plane | `0x478` keeps bits 63:36, `0x480` clears bit 0 |
| `0x18060000` | NSI security gasket | `0x90` keeps bits 63:28, `0x98` clears bit 0 |
| `0x18070000` | NSI LMH gasket | `0xb8` keeps bits 63:24, `0xc0` clears bit 0 |
| `0x18080000` | NSI ARCE | `0xb0` keeps bits 63:24, `0xb8` clears bit 0 |
| `0x180c0000` | NSI CPRH gasket | `0x5c8` keeps bits 63:34, `0x5d8` keeps bits 63:15, `0x5d0` clears bit 0 |

The private layout names these words `*_CGC`, `*_RIF_CGC`, `NSI_CONTROL_PLANE_CGC2`, and `NSI_CPRH_GASKET_CPRH_CGC`.
Each is a clock-gating override.
The SM8850 file holds zero bases for lane one, so the firmware fills that descriptor at run time.
Lane one uses the existing lane stride of `0x200000`.

`qcom,clock-gating-overrides-v1` retains each declared word with a declared initial value and writable field.
It accepts aligned 32-bit halves and 64-bit words.
It creates no clock, power, or completion effect, because clock gating is not visible to the firmware here.
A write outside the writable field fails, and undeclared offsets keep their address error.
The CPRH gasket words already belong to the shared CPRH lane rows.
The initial values come from a private layout. No permitted source establishes the silicon reset values.

The native fixture checks the reset values, the CPUCP clear sequence, 32-bit merges, the RIF field, holes, and cold reset.
Run606 passes the clearing loop and stops at a 32-bit read of `0x17c06040`, from CPUCP PC `0x1d80ad70`.

### CPUCP clock-controller words

The post-ROM sequence then calls `0x1d80adce` from `0x1d808fcc` and `0x1d80ad6c` from `0x1d808fd0`.
The first sets bit 0 of `0x17e84020` and clears bit 0 of `0x17c02134`, which existing owners retain.
The second changes only fields of these 32-bit words, and it does not poll:

| Address | Operation |
| --- | --- |
| `0x17c06040` | Clear bits 30:29 |
| `0x17c06044` | Keep only the bits of `0x0086fe03` |
| `0x17906024` | Clear bits 8, 5, and 4 |
| `0x17c02144`, `0x17c02148` | Clear bit 0 |
| `0x17912000..0x1791200c` | Clear bit 0 of each word |

The Linux Kaanapali DTS does not name these addresses.
The CPUCP image establishes the class of the words.
Its strings at file offsets `0x27f88` and `0x27fd0` report L9 clock-gating failures.
The function that uses them, near `0x1d80df64`, toggles bit 0 of each clock-gating id in the same register families: `0x17c050xx`, `0x179050xx`, `0x179020xx`, and `0x1886d0xx`.
[INFERENCE] The post-ROM words are therefore clock-gating overrides, not clock enables with a consumer.

The same owner retains these words as 32-bit words.
Their reset value is declared virtual zero, and all 32 bits are writable, because no source defines the fields.
A 64-bit or neighboring access fails.
`platforms/qcom/sm88xx/cpucp-clock-gating.lua` maps each word from the APSS and CPUCP routers.

The native fixture also checks a 32-bit word and its width boundary.

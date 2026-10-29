# CPUCP outer core-start sequence

## Evidence and scope

This extension uses the same private ROM and RV64 execution base as `cpu_domain_power_evidence.md`.
Capstone decoding establishes the instructions, descriptor offsets, and static programming tables below.
An isolated CPUCP run reached `0x334bd0` and read `0x198f0028` after the initial domain routine completed.
The extension addresses that real boundary.

`cpu_domain_core.h` contains relative-address state for one lane.
`QcomCpuDomainPower` owns two such states and all target sockets.
It also owns the single reset driver for each attached architectural CPU.
No model contains a SoC address or a SoC test.
Platform parameters provide the implemented masks, attached masks, and boot-core location.

## Recovered outer sequence

`0x334ba4` performs these operations:

1. Program three 32-bit routing words at descriptor offset `0x08`, word offsets `0x28`, `0x38`, and `0x40`.
2. Call `0x334f00(lane,4)`.
3. Change 64-bit control `0x260` at descriptor offset `0x28`.
4. Clear control bits 7, 5, and 6. Assert reset bits `[23:16]`. Clear bits 10 and 9.
5. Set control bit 0 and clear bit 15.
6. Set request `0x258` bit 1. Poll status `0x270` bit 0 until it becomes one.
7. Set control `0x260` bit 8.
8. For applicable boot modes, program descriptor offset `0x30`, word `0x30`.
9. Program descriptor offset `0x38`, word 8, bits 0 and 8.
10. Call `0x334ff6`, then `0x334f00(lane,0)`.
11. Clear fields in control `0x290` and call `0x335044`.
12. Write `0x300000` to control `0x328`. Call `0x3350a4`.
13. Set descriptor offset `0x58`, word `0xf8`, bit 52.
14. Set control `0x310` bit 0. Set control `0x10` low twelve bits to the lane ID.
15. Set request `0x258` bit 4. Call `0x335140`.
16. Read the selected core's word at `0x18879100 + lane*0x80 + core*0x10`.
17. Perform an optional warm-core preparation if that word's bit 0 is set.
18. Call `0x334f00(lane,1)` and `0x334f00(lane,2)`.
19. Call `0x335194(lane,core)`.

After success, the outer caller writes one to `0x1887c000 + lane*4`.
The existing controller accepts that publication word.
CPUCP runtime `0x1d809860` reads bit 0, and `0x1d8099ba` starts the lane only when the bit is clear.
That start sequence pulses the lane reset at `0x1887c020 + lane*4` through `0x1d808978`.
The word therefore reports cluster state. It reads one while the publication, the initialized cluster, and the NCC clock remain.
A lane reset clears it until the cluster initializes again. A held core does not clear it.

The earlier model also required a powered core out of reset.
No APSS core of lane one runs during UEFI, so lane one read zero.
In run625, CPUCP then started lane one again and reset PDP1 while PDP1 executed. PDP1 had not saved a context.
Its warm restore then loaded zero registers and locked up.
It does not interpret that publication as a firmware or kernel handoff.

The static tables selected by `0x334f00` contain zero register pointers for both lanes:
`0x3364b0`, `0x336550`, `0x3366d0`, `0x336770`, and `0x336810`.
The routine skips zero pointers. Those operations need no extra MMIO model for this ROM.

## Memory and timing configuration

`0x334ff6` copies three 64-bit words from ROM `0x3368f0` to the boot memory-access registers at `0xc28`, `0xc30`, and `0xc38`.
It then sets memory-access control `0xc10` bits 10 and 0.
The DVFS bank comes from descriptor offset `0x38`.

`0x335044` reads twelve offset/value records at ROM `0x336908`.
Seven records contain nonzero offsets: `0x2c0`, `0x3f8`, `0x500`, `0x588`, `0x590`, `0x598`, and `0x608`.
It writes those words in the power-control bank.

`0x3350a4` copies the table at ROM `0x3369c8`.
It writes nine pairs of 64-bit words:

- First words: `0x348..0x388`, stride eight.
- Second words: `0x390..0x3d0`, stride eight.
- Three later records contain sentinel offsets `0xdeadc0de` and do not cause MMIO accesses.

These registers retain explicit sequence/timing configuration.
Unknown offsets do not become register RAM.
The functional initialization operation requires the observed configuration banks to be programmed.
It does not emulate proprietary sequencer instructions or claim analog characterization.

## Cluster initialization and per-core requests

`0x335140` sets power request `0x278` bit 8.
It then reads status `0x2b0`, extracts bits `[60:56]`, and waits for a value in `[5,10)`.
ROM words `0x336480` and `0x336484` provide those limits.
The ROM does not identify that field as a counter or provide names for its internal states.

The model uses an explicit functional state machine:

| Encoding | Virtual phase |
|---|---|
| 0 | No qualified supply |
| 1 | Supply qualified |
| 2 | Core reset held while sequence configuration is incomplete |
| 3 | Configuration complete, initialization not requested |
| 4 | Initialization request accepted |
| 5 | Functional cluster initialization complete |

Phase 5 is the lowest completion code accepted by the ROM.
It is not an unconditional register reset value or a fabricated analog measurement.
The initialization operation completes only after these modeled effects:

- The initial domain and downstream operation complete with a live clock and all required branches.
- Supply control bit 0 and supply request bit 1 are set. The observed inhibit bits 5, 6, 7, and 15 are clear.
- Control bit 8 releases the outer isolation condition.
- All three routing words and eighteen range words are programmed.
- The sequence data and enable controls are programmed.
- The NCC control word contains the required cold configuration if software accesses it.
- Request `0x258` bit 4 is set.
- Control `0x10` contains the correct lane ID.
- Request `0x278` bit 8 starts the operation.

[INFERENCE] The supply and isolation names describe functional effects inferred from ordering and polling.
The high status field may represent an analog qualification or an internal hardware phase.
The explicit substitute completes functional qualification after those effects. It does not simulate voltage or analog calibration.
There is no authentication operation in this sequence and no authentication substitute in the model.

## Runtime cluster bring-up

The selected CPUCP runtime also starts a cluster.
SM8850 preparation starts at `0x1d809870`.
For the boot cluster it writes timing words `0x2c0` and `0x3f8` at `0x1d8098aa` and `0x1d8098ba`.
Routine `0x1d809180` then writes the eighteen range words at `0x1d809186..0x1d809222`.
Routine `0x1d80902a` writes the three routing words at `0x1d80903a..0x1d809052`.
Routine `0x1d809056` changes control `0x260` with mask `0xffffffffffff791f` and value `0xff0001`.
Routine `0x1d809072` sets request `0x258` bit 1 and polls status `0x270` bit 0.
Routine `0x1d8090d6` sets control `0x260` bit 8.
Routine `0x1d8090e6` writes bridge word `0x30` with bits 0, 1, and 25, the observed value `0x2000003`.
Routine `0x1d8090fe` sets DVFS configuration bits 0 and 8 at offset 8.
The fields are `DISDVFS` and `DISDVFSWAKE`.
Routine `0x1d8094d0` writes the lane tag at `0x10`.
Routine `0x1d8094ec` sets request `0x258` bit 4.
Routine `0x1d8094f2` sets request `0x278` bit 8 and polls `0x2b0[60:56]` at `0x1d809508`.

The runtime writes no timing word for the second cluster before that poll.
It also does not rewrite memory-access control `0xc10` or the boot configuration at `0xc28..0xc38`.
The ROM retains this configuration, so lane one retains a qualified boot policy.
The model keeps the memory-access condition.
Timing words remain configuration without a completion effect.

An earlier model required all seven timing words and reported phase 2 forever for the second cluster.
SM8850 runs 540, 545, and 547 execute this runtime sequence.
Run 547 writes `+0x258` values `2` and `0x12`, then `+0x278` value `0x100`.
It programs eighteen range words, three routing words, DVFS configuration `0x101`, and bridge `0x2000003`.

It writes lane tag one at `+0x10`.
It writes no timing word to lane one.
Lane one retains memory-access control `0x401` and the three boot configuration words at the start request.
Run 547 writes all seven timing words and the three boot memory-access words to lane zero.
Lane zero reaches phase 5, and its status read returns `0x0500000000000100` after the core request.
Lane one returned phase 2 before this correction.

Run 550 reports both lanes online after the correction.
The runtime then stores the PDP load-completion flag, `+0x258` bit 6, at `0x1d80b73e`.
Run550 next reads `0x17c2c008` from PC `0x1d804328`.
The cluster-watchdog composition did not map that word, so the run stopped with `UNMODELED_ACCESS`.
The physical meaning of that word remains open.
The SCMI server at `0x1d80e1e8` does not run in run 550.

The narrowed correction preserves the sequencer checks and removes only the timing-write condition.
The build succeeds, and all 81 platform tests pass.
Run553 also maps the second-cluster watchdog configuration words.
It passes the earlier boundary and stops at CPUCP write `0x1a850098`.
The kernel entry remains unverified.

`0x335140` sets power request `0x278` bit 8 and polls `0x2b0[60:56]` at `0x33516c`.

`0x335194` clears request bit 8 and sets the selected core bit in `0x278[7:0]`.
It polls the corresponding status bit in `0x2b0[15:8]`.
The model derives those acknowledgement bits from the requested, implemented, functionally powered cores.
A powered core can remain held in architectural reset.

Word `0x2b0` is `NCC_PWR_STS1`.
Bits `[15:8]` are `CORSPRINITDONE`, and bits `[7:0]` are `CORDOMISON`.
Bits `[60:56]` are `CAGPWRSTATE`. Bit 63 and the per-CPU state fields stay zero in the model.
CPUCP `0x1d80b226` reads `CORDOMISON` to decide whether a core is already on.
If the bit is clear, CPU-on function `0x1d809b0a` powers the core and calls `0x1d8096c8` first.
That helper caches the boot lane's `NCC_RVBAR` (`+0x328`) once and copies the cache to other lanes.
The model earlier reported `CORDOMISON` as zero.
CPUCP then ran CPU-on for the running boot core during initialization and cached the ROM value `0x300000`.
QSEE `0x157bc884` later writes the warm vector `0x1468e000` to the boot lane, but the cache kept the ROM value.
In run640, lane one core zero therefore left reset at `0x300000` and executed the cold PBL path, which reinitialized UFS.
A functionally powered core now reports `CORDOMISON` with `CORSPRINITDONE`.

The routine then reads fuse selector `0x88` through the existing service at `0x335608`.
It writes the resulting boolean to bit 56 of descriptor offset `0x70`, core stride `0x100000`, word `0x150`.
Finally, it clears the selected reset bit in `0x260[23:16]` and leaves the other reset bits asserted.
This is the observed per-core reset-release boundary.

The controller combines that core-reset condition with the existing domain-reset condition.
One `SC_THREAD` drives one reset input for each attached CPU.
The domain initialization latch is separate from the combined CPU reset level.
Each lane acknowledges reset only after all attached cores receive its captured request.
Thus an outer core-reset hold cannot create a circular dependency that revokes the upstream domain supply acknowledgement.
Clock or domain loss still revokes supply, cluster completion, and all core power acknowledgements.
The existing events that change state or reach deadlines drive those effects without periodic polling.

## Core C-state report

The boot status word at `+0x270` starts at `0xff00000000000000` in the model.
That value marks the eight cores that the lane masks `0x3f` and `0x3` declare as valid.
No permitted source establishes the silicon reset word.

| Bits | Field | Model value |
| --- | --- | --- |
| `[63:56]` | `YLDCOREVLD` | The reset value `0xff` |
| `[43:40]` | `NCCPWRSTATE` | Zero |
| `[39:36]` | `NCCCSTATE` | Zero |
| `[4n+7:4n+4]` | `CPUnCSTATE` | Zero, two, three, or four, as the next list describes |
| `[0]` | `NCCRESETDONE` | The supply acknowledgement |

The model sets each `CPUnCSTATE` field from the state of core `n`:

1. A core without power or in reset reports four.
2. A powered core that executes reports zero.
3. A powered core in WFI with its `OK2ENTERC4` bit set reports four.
4. A powered core in WFI with only its `OK2ENTERC3` bit set reports three.
5. Another powered core in WFI reports two.

The firmware establishes these boundaries:

- PDP `0x8132113c` treats a core as on when its `YLDCOREVLD` bit is set and its C-state is two or less.
- PDP `0x813210bc` requires C-state two exactly.
- CPUCP `0x1d80c8a2` returns four for a core when its cluster is not on. It returns the field otherwise.
- CPUCP callers `0x1d802d18`, `0x1d802ebc`, and `0x1d811888` treat a value greater than two as inactive.
- CPUCP `0x1d80c9ee` returns five for a cluster that is not on. Its caller `0x1d811efe` treats a value greater than three as inactive.

`NCC_PWR_CFG1` (`+0x290`) has per-core `OK2ENTERC4` bits `[15:8]` and `OK2ENTERC3` bits `[7:0]`.
TZ `0x1573e734..0x1573e848` prepares a requested state before WFI:

- A state below two sets no enable.
- State three sets `OK2ENTERC3` for the core.
- State four sets both bits for the core.

TZ `0x1573e8c4..0x1573e994` clears the same bits after wake.
The model therefore reports the deepest enabled state for a core in WFI, and C2 when no bit is set.
[INFERENCE] Zero for an executing core and four for a core without power follow from these comparisons. No document states the encoding.
The model does not save or lose core state in C3. Interrupt wake from C3 or C4 continues after the WFI.
`CORDOMISON` continues to report the requested core power, also in C4.

PDP ACD preparation `0x81320a64` uses these fields.
It counts the cores of its cluster that are on and fails if more than one is on.
It then polls the C2 predicate for that core 1000 times with a delay of one microsecond.
UEFI `CalibrationDxe` sends SCMI request `0x12` with type two, then waits on an event.
The wait puts the requesting core in WFI.
PDP notification `0x13` with type three returns the status and signals the event.

Run644 printed `PDP returned ACD calibration failure`.
PDP0 executed the timeout path at `0x81320b26`. The model reported zero in `YLDCOREVLD`, so no core was valid.

Run645 reported C2 for every core in WFI.
PDP0 then executed the `ACD Prep: CPU ON Count Error` path at `0x81320ac2..0x81320acc`.
Lane zero core one waited in the TZ power-down WFI, so the PDP counted two cores that were on.

Run647 reported C4 for that core. PDP0 counted one core, saw C2 on the requesting core, and continued at `0x81320b44`.

### Start request for a core in C4

UEFI `CalibrationDxe` unplugs its auxiliary core before a calibration and plugs it in again.
TZ prepares state four for the unplugged core and executes WFI at `0x1567c044` or `0x1567c05c`.
On hardware, the core then loses power, and the TZ start request at `0x19050000` powers it again from the warm vector.

The model ignored a start request for a core that was not in reset.
An unplugged core stayed in WFI, and CalibrationDxe printed `Timed out waiting for cores to turn on` with mask `0x41`.
Runs 628, 633, 646, and 651 stopped there. The result depended on the order of the WFI and the start request.

A start request for a core in C4 now holds that core in reset for one reset-loop pass.
The next pass applies the request and releases the core from the warm vector.
The model logs `APSS_CORE_POWER_CYCLE` for this transition.
A start request for an executing core still has no effect.

## Cold and warm boundaries

Core-status words at control offsets `0x1100 + lane*0x80 + core*0x10` report actual modeled core power.
They start clear because no outer core request is active.
The observed cold sequence therefore does not enter the optional warm-core path.
Its accesses to descriptor offset `0x48` remain unsupported and fail through AccessMonitor.
The optional write to descriptor offset `0x68`, word `0x560`, remains off because ROM `0x336488` is zero.

Power control, bridge, sequencer, fabric, and per-core policy sockets accept aligned 64-bit accesses only.
Routing and existing control sockets accept aligned 32-bit accesses only.
All sockets reject byte enables, unknown offsets, invalid commands, and unsupported control fields.
No socket grants DMI.
Virtual cold outer control `0x260=0xff86e0` holds all eight core reset bits and the observed inhibits.
That value is explicit model policy, not a measured silicon POR value.

Word `+0x260` is `NCC_BOOT_CFG1`. Bit 10 is `OK2ENTERCL5`, and bit 9 is `OK2ENTERCL5LITE`.
The model earlier treated both bits as supply inhibits.
TZ `0x1573eb64..0x1573eb80` sets them before a cluster low-power entry, and `0x1573ec64..0x1573ec84` clears them after wake.
In run655, TZ set them after the lane-one calibration. The model then removed the lane supply and reset PDP1.
UEFI then printed `calibration complete notification to PDP1 failed`.
The two bits now only permit a collapse. The model does not collapse a cluster, so the lane stays powered.

## Platform wiring

The board uses the shared `cpu_domains` instance in `platforms/qcom/sm88xx/reset.lua`.
The lane masks are `0x3f` and `3`.
These masks come from the descriptor words, not C++ SoC conditions.
Both lanes attach all their implemented cores.
Separate reset and vector outputs connect each core to its APSS wrapper.


The broader shared socket spans still reject gaps.
Processor forwarding remains restricted to the known words.
No mapping for `0x198d0000`, `0x19aa0000`, or their lane-1 counterparts is required by this cold path.

The board and regression fixture use `qcom,gcc-taycan-eko-t-pll-v1`.
See `gcc_pll_evidence.md` for the source identity and divided output.


The selected CPUCP runtime also writes core-policy bits 50, 55, and 57 at `+0x150`.
The model retains those bits alongside the ROM's bit 56.
These fields do not create authorization, power acknowledgement, or reset release.
The runtime writes warm address `0x1468e000` to the lane's `+0x328` word.
The selected producer PCs are SM8850 `0x1d80917a` and SM8845 `0x1d80952c`.
The controller publishes that actual word to held cores before it releases them.
Running cores defer vector sampling until reset.
The secure software retains each requested entry and context separately.

## Regression and verification boundary

The `cpu-domain` actual guest fixture includes the initial domain checks and the outer sequence.
Its producer programs the recovered configuration through real TLM accesses.
Its consumer is a real AArch64 CPU with a retained boot counter and heartbeat in shared RAM.
The test checks reset hold/release by observing that CPU, not a mock reset sink.
The outer checks distinguish core-power acknowledgement from reset release.
They also check QRNG policy changes, per-core reset reassertion, and supply-loss revocation.
The lane-one checks program the second-cluster write set.
That set contains the eighteen range words, three routing words, the DVFS configuration word, and the NCC clock word.
Lane one also retains the ROM memory-access control and boot configuration.
They leave every timing word at zero.
They then require phase 5 and the per-core reset release.
This coverage protects the runtime path from a return to the ROM timing-word condition.

The actual guest regression passed during `make test`.
The integrated run `run-sm8850-functional-controls7` passed the outer initialization and core power acknowledgement.
CPUCP then reached the `FEATURE_CONFIG11` read at `0x33572c`.
That read occurs before the final per-core reset release.
The run does not prove firmware or kernel handoff.

## NCC clock transition

SM8850 XBL identifies the former bridge bank as NCC clock configuration.
XBL `0x1484ed64..0x1484edc0` selects AUX, programs lock timeouts, enables a PLL, and selects its even output.
XBL establishes these fields in the 64-bit word at bank offset `0x30`:

| Bit | Effect |
| --- | --- |
| 25 | Software controls the PLL configuration. |
| 2 | Software controls the AUX/PLL mux. |
| 3 | The mux selects the PLL instead of AUX. |
| 5 | The PLL path selects the even output instead of the main output. |

Bank offset `0x10` retains two lock timeouts at bits 15:0 and 43:32.
XBL writes 960 to each field.
The model does not interpret a mux override as a core-reset request.
The earlier model incorrectly required bit 2 to remain clear.
An actual XBL run exposed that error through an unexpected APSS reset.

The hardware path uses the live domain reference until its first DVFS request.
After that request, the hardware-owned PLL supplies the applied frequency.
The explicit AUX path uses that reference too.
An explicit PLL selection requires the configured PLL output to be available.
A missing, disabled, or unlocked selected output holds architectural reset without removing core power.
PLL events release reset when the selected output becomes available.

### Clock ownership transition

PDP device configuration `0x2000d1ec` names the NCC clock bank at local address `0x409a2000`.
The lane-zero window maps that address to `0x199a2000`.
Routine `0x2000b5e6` clears bits 3, 2, 0, and 25 in its 64-bit word at `+0x30`.
The stores occur at `0x2000b5f6`, `0x2000b606`, `0x2000b616`, and `0x2000b634`.
It then writes `0x10000` to DPM offset `0x80` at `0x2000b63c`.

Run582 exposed an incorrect dependency on the cold value `0x2000003`.
The model cleared cluster initialization when the PDP removed the software overrides.
The PDP log recorded another reset-vector load, and the run timed out.
These writes return clock control to hardware, rather than request a cluster reset.

The cold override check now applies only before cluster initialization.
Supply loss and explicit reset controls still affect the cluster.
An explicit PLL selection still requires the selected live output.
The native regression first checks that incomplete overrides cannot qualify cold initialization.
It then clears the four runtime bits while an architectural CPU executes.
The CPU must retain its boot count and continue its heartbeat.

Before the correction, that regression failed at stage 503, address `0xbad007dc`.
The stage identifies the removal of bit zero.
After the correction, all 87 platform tests pass.
Run583 passes the PDP clock transition and reaches the fail-safe configuration routine.
Its next unsupported read is local `0x408b00d8`, from PDP PC `0x200046e0`.
Run583 records 14619 UART bytes and `kernel_entry=false`.
These bytes are not kernel boot evidence.

## DVFS fail-safe configuration

Run583 stops at PDP PC `0x200046e0`, which reads local `0x408b00d8`.
The lane-zero window maps this address to `0x198b00d8`.
Function `0x20004684` programs two fail-safe selections and their parameters.
Its helper at `0x200036e0` programs two additional parameter words.
Both functions use 64-bit reads and writes.

| Offset | PDP store PCs |
| --- | --- |
| `0xd8` | `0x20004728`, `0x20003758` |
| `0xe0` | `0x200047a6`, `0x200037ba` |
| `0x168` | `0x20004760` |
| `0x170` | `0x200047d8` |
| `0xfa0` | `0x20003734` |
| `0xfa8` | `0x2000379c` |

The shared performance-state table retains these six words from virtual zero.
Configuration writes do not request a transition or qualify cold initialization.
The APSS and CPUCP aliases reach the existing lane owner.
The functional DVFS owner also accepts the corresponding normal-state rows.
Unknown offsets remain errors.

The native regression programs each word while a CPU executes.
It checks the other lane, the CPU boot count, and the heartbeat.
All 87 platform tests and both affected Lua syntax checks pass.
Run584 passes fail-safe programming and reaches local `0x408b0c40`, at PDP PC `0x2000b2d2`.
The PDP requires a nonzero doubleword there before it enables DVFS.
That run leaves the register contract unresolved.

Run584 records 14623 UART bytes and `kernel_entry=false`.
These bytes are not kernel boot evidence.

## Boot memory-access handshake

The NCC0 DVFS module defines this common block:

| Offset | Register | Relevant field |
| --- | --- | --- |
| `0xc10` | `NCC_DVFS_MEM_ACC_CFG` | Bit 10 is `BOOTMEMACCEN`. Bit 0 is `DVFSLVHVRAEN`. |
| `0xc28..0xc38` | `NCC_BOOT_MEM_ACC_CFG1/2/3` | The words contain memory read-assist and timing fields. |
| `0xc40` | `NCC_BOOT_MEM_ACC_STS` | Read-only bit 0 is `BOOTMEMACCPUSHDONE`. The reset value is zero. |

The three reset values exactly match the values that SM8850 ROM `0x334ff6` programs.
PDP `0x2000b2cc` checks the resulting status before it clears `BOOTMEMACCEN`.
This correlation supports reuse of the common handshake.
The completion-bit encoding remains a sibling-IP inference, not a direct SM8850 hardware observation.
The model does not import a full register map or its reset defaults.

SM8850 also writes upper fields at `+0xc10`.
PDP `0x2000b884` supplies the upper mask `0x003e1f00`.
PDP `0x2000b436` programs bits 9:1 and retains the low enable bit.
The model accepts these observed fields without applying the older reserved mask.
PDP `0x2000b35a` supplies the upper DVFS configuration mask `0x3e1f3e1f`.

Virtual SRAM applies the complete read-assist policy synchronously when the cluster has power and `BOOTMEMACCEN` is set.
The completion bit stays clear without power, a complete three-word policy, or the request.
Policy writes invalidate completion until the domain processes them.
Supply loss and request removal clear completion.
The model bypasses analog SRAM timing and voltage effects.
It does not create a DVFS transition or interrupt from this memory-access operation.

The cold DVFS disable fields no longer act as runtime reset gates.
The native regression checks incomplete policy, missing request, lane isolation, and supply-loss revocation.
It also enables DVFS while the CPU retains its boot count and heartbeat.
All 87 platform tests and the Lua syntax check pass.
Run585 passes the memory-access check and reaches a 32-bit read at local `0x408b0000`, from PDP PC `0x20003ce8`.
Run585 records 14618 UART bytes and `kernel_entry=false`.

## Functional DVFS requests

PDP `0x20003cbc..0x20003d08` reads and writes `NCC_DVFS_CTL` at local address `0x408b0000`.
Its first request selects state 30.
The common request and status block holds these fields.
The model does not import a complete address map or reset profile.
Its initial control word selects target 31 as explicit virtual policy.

| Register | Functional fields |
| --- | --- |
| Control `+0x0` | Bits 20:16 select the state. Bits 0..3 request all effects, frequency, voltage, or memory access. Bit 24 locks changes. |
| Status `+0x50` | Bit 32 reports busy. Bit 33 reports the lock. Bit 34 reports a voltage error. Bit 24 reports a locked request. |
| Applied state `+0x50` | Bits 4:0 report the state. Bits 39:35, 44:40, and 49:45 report frequency, voltage, and memory-access states. |

The owner accepts aligned 32-bit and 64-bit transfers.
A partial write preserves the other half.
The NCC window spans `0x2300` bytes, but unknown registers within that window still fail.
State configuration does not change an effective output until a request applies it.

Each request applies only its selected effects.
The handler checks the frequency before it changes either rail.
The power owner checks both target profiles before it changes either target.
The handler then updates the PLL output and the virtual SRAM access state.
Only after these effects occur does it publish the requested state and clear busy.
Virtual SRAM retains its bytes across supported access states.

An invalid profile returns a TLM command error and preserves the previous applied state.
A voltage-profile failure also sets the modeled voltage-response error.
A locked request sets the lock-error field without changing outputs.
The model does not generate a DVFS interrupt.

PDP `0x2000472c..0x20004760` programs the frequency word for state 30.
Bits 8:0 contain the multiplier, bits 24:9 contain the fraction, and bits 26:25 contain the output selector.
CPUCP `0x1d80d0ac` treats a nonzero selector as a divide-by-two output.
The captured state-30 word `0x63` specifies 1900800000 Hz at the 19.2 MHz reference.

Hardware ownership supplies the effective PLL output even when software stops its oscillator.
Software register writes remain retained during that ownership.
A return to software control exposes its actual lock state, not an unconditional ready value.
The guest regression selects AUX while the software PLL acquires lock before it restores the PLL path.

All 88 platform tests pass.
The CPU-domain fixture executes an AArch64 consumer during hardware DVFS and the safe return to software control.
Run588 passes the native state-30 request.
Its next unsupported write is local `0x408b0728`, from PDP PC `0x200040a0`.
Run588 records 14619 UART bytes and `kernel_entry=false`.
These bytes are not kernel boot evidence.

### Per-core software votes

PDP table `0x2000d220` declares six slots at offsets `0x1d10..0x1d38`, with an eight-byte stride.
The table starts at file offset `0xe220` in SM8850 and `0xe5a8` in SM8845 `.305`.
Function `0x200039ec` replaces bits 5:1 with a state index and sets bit zero.
Function `0x20003a54` applies that operation to each present core.
The runtime writers at `0x200049ee` and `0x20004a7e` use the same operation.

The initializer at `0x20003b80` selects either one software slot or all present slots.
It calls `0x20003a24` to set `+0xc18[7]`.
Helper `0x20003af0` changes bit eight, sets bit ten, and requests all effects when it clears bit eight.
Helper `0x2000b5c0` changes bit sixteen, not bit zero.
The model retains these fields and uses bit seven to select the software vote bank.

The virtual arbitration policy selects the enabled request with the highest effective frequency.
It compares the programmed multiplier, fraction, and divider rather than the numeric state index.
Equal-rate requests retain priority by slot order.
The configured core mask excludes absent cores.
Silicon arbitration between conflicting slots remains unmeasured.
No autonomous hardware voter supplies requests in this model.

Votes written before mode enable remain configuration.
Mode enable alone does not change the applied state.
A control request starts the initial transition.
Later writes to enabled slots request all effects through the existing clock, power, and memory owners.
Completion follows those effects, and a locked request preserves them.

A cleared slot no longer contributes to arbitration.
If no request remains, the last applied state remains until another request or a separate power action.
An explicit control request with no enabled slot returns a command error.
Undeclared vote fields fail instead of silently changing another policy.

The real AArch64 fixture covers staging, mode enable, mixed rates, withdrawal, partial accesses, locking, and continued CPU execution.
Its state 23 has a lower index but a higher effective rate than state 30.
All 88 platform tests pass.
Run601 passes per-core initialization and reaches the QMX DPM configuration write at `0x19aa0b50`.
The run records 15172 UART bytes and does not reach kernel entry.

### QMX DPM configuration

The PDP DPM device `dpm@408c0000` has configuration `0x2000d250`.
Its table at `0x2000d260` lists the lane bases `0x19aa0000` and `0x1aaa0000`.
These are the same lane banks that the CPUCP ROM programs at `+0x10`, `+0x130..0x23f`, `+0x900..0xa2f`, and `+0xc18..0xc97`.
The selected firmware writes these lane words as configuration, and no recovered consumer reads them back as a table.

Source file `ncc_qmx_dpm_init.c` contains two writers:

| Function | 64-bit offsets |
| --- | --- |
| `0x20004ba0` | `0xb50`, `0x480` |
| `0x20004c34` | `0x488..0x4c8` and `0x5b8..0x5e0`, with an eight-byte stride |

The writers get their values from the key-value configuration `K_DPM_CFG` and `K_DPM_CLUSTER_WEIGHTS`.
Their trace helper `0x2000c858` jumps to `0x81326c3c`, which is only `bx lr`.
No SRAM or DDR code reads these offsets back or polls them.
The model therefore retains the 17 words per lane and creates no measurement, limit, or completion.
Other offsets in the window keep their address error.

The AArch64 fixture writes all three ranges and a 32-bit upper half while a CPU executes.
It checks that the other lane stays zero.
All 88 platform tests pass.
Run603 passes the QMX writers and stops at the per-core DPM write to local `0x408c0018`, from PDP PC `0x81326f10`.
It records 15175 UART bytes and does not reach kernel entry.

### Initialization configuration

PDP `0x20004044` programs eight memory-access regions.
Each region has three 64-bit configuration words.
The model retains them without simulating electrical read-assist behavior.
Configuration alone does not change SRAM bytes or report request completion.

| Region | First word | Second word | Third word |
| --- | --- | --- | --- |
| `r = 0..3` | `0x728 + 8*r` | `0x748 + 8*r` | `0x768 + 8*r` |
| 4 | `0xc80` | `0xc88` | `0xc90` |
| `r = 5..7` | `0xd00 + 0x18*(r-5)` | `0xd08 + 0x18*(r-5)` | `0xd10 + 0x18*(r-5)` |

The range selectors at `+0x10` and `+0xc98` retain the programmed state bounds.
Stores `0x200040a0..0x200041b4` establish the region addresses.
`+0x728` is `NCC_DVFS_EMA1_RGN0_CFG`.

PDP `0x200041c8` programs the performance-state columns below.
States 0 through 15 use an eight-byte stride within each normal column.
The model maps states 30 and 31 to the next two words in each normal column.
States 16 through 23 use a `0xf8`-byte stride from the extended bases.
The extended records end before the first core vote at `+0x1d10`.

| Normal base | Extended base | Functional use |
| --- | --- | --- |
| `0x58` | `0x1550` | Retained state configuration |
| `0xe8` | `0x1558` | Primary frequency |
| `0xf20` | `0x15f0` | Retained secondary frequency |
| `0x9d0` | `0x15d0` | Retained policy |
| `0xa60` | `0x15d8` | Retained policy |
| `0xaf0` | `0x15e0` | Retained policy |
| `0xb80` | `0x15e8` | Retained policy |
| `0x4d8` | `0x1590` | Retained target parameters |
| `0x568` | `0x1598` | Retained target parameters |
| `0x1430` | `0x1638` | Retained target fields |
| `0x14c0` | `0x1640` | Retained target fields |
| `0x8b0` | `0x15c0` | Retained state configuration |
| `0x940` | `0x15c8` | Retained state configuration |
| `0x790` | `0x15b0` | Retained state configuration |
| `0x820` | `0x15b8` | Retained state configuration |
| `0xfb0` | `0x15f8` | Retained state configuration |
| `0x1040` | `0x1600` | Retained state configuration |

PDP `0x8130b7be..0x8130b81a` and `0x8130b51c` write the last four columns after the ACD calibration.
PDP `0x8130b568` writes states 30 and 31 of the `0xfb0` and `0x1040` columns.
Run650 stopped at a write to `+0x1c78`, and run652 stopped at a write to `+0x1cc0`. Both belong to state 23.

Helper `0x200037d4` establishes the secondary frequency column and its extended base at `0x15f0`.
Helpers `0x20003898` and `0x20003944` also program two auxiliary columns.
Their normal ranges are `0x130..0x1a8` and `0x1c0..0x238`.
Their extended ranges are `0xc18..0xc50` and `0xc58..0xc90`.
The base table at `0x2000d218` selects `0x19aa0000` and `0x1aaa0000`.

PDP `0x2000b642..0x2000b692` updates `+0xf18` and the DVFS disable fields.
The model retains `+0xf18` from explicit virtual zero.
This configuration word does not produce a completion event.

PDP `0x8130b866` sets `NCC_ACD_MISC_CFG` (`+0xc20`) bit zero, `ACDEN`, after the ACD calibration.
Helper `0x8130b5aa` then sets bit three.
Its reset value is zero.
The model retains the word from virtual zero. Clock-droop response has no modeled effect.
Run653 stopped at the first read of this word.

PDP `0x81321704` then reads and writes `NCC_DVMR_REGION_CFG` (`+0xc78`) with 64-bit accesses.
All bits are writable. The reset value is zero.
The PDP fields reach bit 57.
The model retains the word from virtual zero without a droop-mitigation effect.
Run654 stopped at the first read of this word.

PDP `0x2000b312` then sets `NCC_BOOT_CTL1` bit 5, `CSRINITLATEDONE`.
Only firmware writes this flag.
The model preserves the existing power and reset gates without manufacturing late initialization.

Run590 passes memory and performance-state configuration.
It stops at the firmware's late-initialization store, local `0x40880258`, at PC `0x2000b31e`.
The accepted mask now includes that firmware-owned flag.
The guest regression also checks a frequency-only request at state 23 while voltage and memory-access state remain unchanged.

All 88 platform tests and the affected Lua syntax checks pass.
Run591 passes the late-initialization flag.
It then stops at a 32-bit write to the PDP reset-vector register, local `0x408802c8`, at PC `0x20006968`.
Run591 records 14623 UART bytes and `kernel_entry=false`.


## NCC Zonda PLL

XBL source descriptors `0x148f1780` and `0x148f17b8` identify banks at `0x19a30000` and `0x1aa30000`.
Their operation table starts at `0x148f3820`.
The model key is `qcom,ncc-zonda-pll-v1`.
The model contains no SoC selector.

| XBL routine | Register contract |
| --- | --- |
| `0x14861b14` | Set MODE bits 1 and 2, write RUN at `0x30`, poll lock bit 31, then enable output. |
| `0x14861ba4` | Enable the main, even, or vote interface in USER_CTL at `0x0c`. |
| `0x14861c34` | Write the calibration configuration at `0x14..0x2c` and the two user controls. |
| `0x14861ca4` | Program L at `0x04`, alpha at `0x08`, and divider fields in USER_CTL_U at `0x10`. |

The Zonda ELU and OLE headers describe this register layout.
These sources establish the contract, not an independent silicon-variant identification.
The model retains the calibration configuration without simulating analog calibration.
It requires reset release, bypass release, a nonzero multiplier, and RUN before acquisition.
Lock follows a virtual five-microsecond delay.
Main and even gates remain independent.
The even divider uses encodings 0, 1, 2, and 3 for divisors 1, 2, 8, and 4.
The model rejects dynamic latch operations, FREEZE, and M/N mode.

The guest regression checks that mux changes preserve both heartbeat progress and the boot count.
It also checks zero-multiplier lock rejection and event-driven reset release after selected-PLL recovery.
The fixture waits for heartbeat progress and rejects a changed boot count.
It polls only shared RAM, not clock status.
Constant instruction delays do not synchronize independent QEMU instances.

An actual reset run completed the XBL NCC transition without the erroneous reset.
These observations do not establish kernel handoff.

## Selected runtime cluster configuration

The selected packages use `IMAGES/cpucp.elf`.
Their hashes are recorded in `evidence.md`.
This group adds configuration retention to the existing domain owner.
It does not add a power acknowledgement, firmware acknowledgement, interrupt, or reset transition.

### Initializer inventory

| Item | SM8850 virtual address | SM8845 virtual address |
| --- | --- | --- |
| Early initializer table entry | `0x1d823228` | `0x1d822d28` |
| Early initializer | `0x812d4000` | `0x81244000` |
| QMX policy routine | `0x812d446a` | `0x81244416` |
| QMX doubleword read | `0x812d44a6` | `0x81244452` |
| Next initializer table entry | `0x1d823238` | `0x1d822d38` |
| Next initializer | `0x812d4674` | `0x81244620` |
| Descriptor template | `0x1d82d980` | `0x1d82d980` |
| Descriptor constructor | `0x1d8034ee` | `0x1d803552` |
| Later cluster-table call | `0x1d809a9c` | `0x1d809e0c` |
| Later cluster-table routine | `0x812da062` | `0x8124d8ce` |
| Table-set descriptors | `0x1d833190` | `0x1d833238` |

The early initializer reads identity and fuse policy.
It initializes both cluster descriptors, computes their core masks, and reads QMX policy.
The QMX routine reads descriptor `+0x110`, then bank `+0x5d0`, then extracts bit 54.
It can combine that bit with the `qmx_disable` property.
The existing fuse evidence documents the property-selection flag.
There is no write or completion poll in this QMX routine.

The descriptor strides are `0x268` and `0x248`, respectively.
The constructors add `lane << 24` to the relevant bank pointers.
Thus both packages select these banks:

| Descriptor field | Lane zero | Lane one |
| --- | --- | --- |
| `+0x100` | `0x199e0000` | `0x1a9e0000` |
| `+0x108` | `0x199e1000` | `0x1a9e1000` |
| `+0x110` | `0x19898000` | `0x1a898000` |
| `+0x120` | `0x19aa0000` | `0x1aaa0000` |

### Complete bounded table programming

Before the later table call, both callers clear bit zero at cluster bank `+0x650`.
The access PCs are SM8850 `0x1d809a92..0x1d809a98` and SM8845 `0x1d809e02..0x1d809e08`.
The called routines perform these operations:

1. Set bit zero and clear bits 6:1 at cluster bank `+0x660`.
2. Read software descriptors and filter primary records against the implemented core mask.
3. Write packed primary records to descriptor `+0x100`, starting at bank `+0x400`.
4. Set primary selector bits in cluster words `+0x630`, `+0x638`, `+0x640`, and `+0x648`.
5. Write the primary record count to cluster word `+0x658`.
6. Write packed secondary records to descriptor `+0x108`, starting at bank `+0x400`.
7. Set secondary selector bits at cluster word `+0x678`.
8. Write the secondary record count to cluster word `+0x680`.

SM8850 returns after those two sets.
SM8845 also programs a third set through descriptor `+0x120`:

1. Write packed records starting at bank `+0x900`.
2. Set selector bits at `+0xa10`, `+0xa18`, or `+0xa20`, according to the record class.
3. Write the count at `+0xa28`.

The SM8845 third-set branch uses software policy at `0x18b30300`.
When that byte is nonzero, the routine skips the third set.
These routines contain no hardware completion poll.
Their loops traverse software records, not asynchronous hardware status.

The descriptor capacities are 64 primary slots, three secondary slots, and four third-set slots.
SM8850 declares 50 and 18 primary input records for lanes zero and one.
SM8845 declares 26 and 10 primary input records.
Both packages declare two secondary input records per lane.
SM8845 declares one third-set input record per lane.
The model bounds each table by its declared capacity, not by an unrestricted register window.

The model retains the explicit selector words, counts, and packed records.
No downstream sequencer in the current model consumes these runtime tables.
This is bounded virtual configuration state, not an implementation of an undocumented hardware scheduler.
QMX word `+0x5d0` is read-only and resets to zero.
In particular, bit 54 is clear as explicit permissive virtual policy.
This value is not a measured silicon reset value.
Unknown offsets, byte enables, invalid widths, and invalid commands still fail through AccessMonitor.
Aligned 32-bit halves and aligned 64-bit transactions share each retained word.
A 32-bit transaction preserves the other 32-bit half.
The existing owner still controls clocks, supply, core power, and architectural reset.

### Telemetry counters

The same table banks hold the counters for these records.
The primary counters are 64-bit words at `+0x000..+0x1f8`. The secondary counters are at `+0x1000..+0x1010`.
CPUCP `Telemetry_read` at `0x1d81a4d6` finds the counter index for a record and reads the record and the counter.
For mode one and mode two, it scales the counter by `2/19200`. It returns other modes unchanged.
The 500-millisecond service at `0x1d81adca` and `0x1d81ae28` publishes the results in the `TEMT` export buffer.
No recovered CPUCP path branches on a counter value.

The primary event identifiers form pairs: `0x00/0x01`, `0x02/0x03`, `0x34/0x35`, and `0x36/0x37`.
The even identifier uses mode zero. The odd identifier uses mode two.
The export field names `ss1ent_0`, `ss1tnr_0`, `ss2ent_0`, `ss2tnr_0`, `ss3ent_0`, and `ss3tnr_0` start at `0x1d82a358`.
[INFERENCE] Each pair is the entry count and the residence time of one low-power state.
The secondary records `0x0000` and `0x10001` have the same count and scaled-time form.
No recovered string names the fourth primary pair or the secondary pair.

These counters measure low-power entries and residence time.
The virtual profile is continuously awake and has no low-power transition source.
Counter reads therefore return zero, and counter writes fail.
See the CPUCP low-power telemetry entry in `docs/qcom/sm88xx/stubs.md`.
Run615 stops at the first secondary counter read, `0x199e1000` from PC `0x1d81a5f0`.

### Source file offsets

The early table entries have ELF file offsets `0x24228` and `0x23d28`.
Both descriptor templates have ELF file offset `0x2e980`.
The QMX reads have ELF file offsets `0x3c4a6` and `0x3c452`.
The next initializers start at ELF file offsets `0x3c674` and `0x3c620`.
The later table routines start at ELF file offsets `0x42062` and `0x458ce`.
The set descriptors start at ELF file offsets `0x34190` and `0x34238`.
Each pair lists SM8850 first and SM8845 second.

### Next initializer dependencies

The next initializer is outside this configuration group.
Both packages perform these accesses:

- Write unnamed BFSM event configuration at `0x18877800` and `0x18877804`.
- Set bit 14 at `0x17c02158`, `0x17903004`, and `0x1886800c`.
- For implemented lanes, set bit 14 at descriptor `+0x88`, bank offset `0x2010`.
- For implemented lanes, replace bits 1:0 with two at descriptor `+0x78`, bank offset `0x3018`.
- Program `0x17953000`, `0x17953004`, `0x17953008`, `0x1795300c`, and `0x17953010`.
- Set bit one at `0x17953030`.
- Call SM8850 `0x812d4638` or SM8845 `0x812445e4`.

Those last helpers write `0x1000` to `0x18877618` and `0x18877614`.
They poll bits 18 and 19 at `0x18877810`.
The lane-dependent addresses are `0x181b2010`/`0x183b2010` and `0x18003018`/`0x18203018`.
They belong to other controller interfaces.
This cluster extension does not return fabricated completion for those polls.

### Verification handoff

The existing `cpu-domain` guest fixture now exercises both lanes and all bounded configuration slots.
It checks native doubleword retention and independent 32-bit half writes.
It also checks QMX policy and the recovered enable-field modification.
Heartbeat progress and retained boot counts check that configuration does not reset the running core.
Held-core counters check that configuration does not release other cores.
The existing power and reset tests remain in the same fixture.
No new Rust test registration is required.

Build the integrated model before this command:

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform cpu_domain_acknowledgements_require_live_hardware_and_deliver_reset -- --ignored --exact
```

The focused domain fixture passes with the other five CPUCP tests.
SM8850 run265 and SM8845 run214 pass QMX discovery and reach the next initializer at `0x18877800`.
The complete integration run passes 47 host tests and 72 platform tests. Clippy passes with warnings denied.

## Runtime configuration-apply transition

The next initializer from the preceding inventory now has a bounded functional implementation.
`cpucp_runtime_controls.h` stores its configuration inside the existing CPUCP boot owner.
`platforms/qcom/sm88xx/cpucp-runtime-controls.lua` maps the recovered banks for both processor views.
The platform supplies `cpucp_clock_root_3`, the existing domain clock, as the live source.
No second clock-readiness flag or replacement clock source exists in the implementation.

### Source identity and limits

The CPUCP LPM BFSM layout places `CPUCP_LPM_SEQ_WAIT_EVT_CTRL_MASK` at block offset `0x161c`, with mask `0xffffff`.
The selected XBL accesses that exact field at `0x1887761c`.
This places the relocated block at `0x18876000`.

The CPUCP LPM BFSM layout does not name offsets `0x1614`, `0x1618`, `0x1800`, `0x1804`, or `0x1810`.
The selected firmware establishes their accesses and polling, not their silicon field names.
The model therefore does not call `0x18877800` a PLIC register.
It also does not assign undocumented clock names from matching offsets in another controller.
GCC UBWCP offsets `0x53000..0x53020` do not establish the identity of base `0x17900000`.

### Explicit virtual contract

The following behavior is virtual configuration-apply policy, not a claim of complete BFSM or electrical emulation:

1. Retain the two event configuration words.
2. Retain bit 14 in the three global policy words and the implemented lane policy words.
3. Retain both complete 64-bit lane words, including independent 32-bit half writes.
4. Retain all six words at the recovered `0x17953000` bank.
5. Apply the retained group immediately when its recovered enable fields and live source permit operation.
6. Qualify the two setup requests independently against that applied group.

The group requires both event words and all six clock-bank words to receive writes.
This distinguishes the two explicit zero writes from absent configuration.
The three global bit-14 fields must be set.
Each implemented lane needs its bit-14 field set and its 64-bit policy field `[1:0]` equal to two.
Clock-bank offsets `0`, `0x0c`, and `0x10` need bit zero set.
Offsets `4` and `8` require explicit writes, but their values do not qualify readiness.
Offset `0x30` needs bit one set.
The existing selected source must actually report a running clock.

Under those conditions, setup `+0x14` bit 12 qualifies status bit 18.
Setup `+0x18` bit 12 independently qualifies status bit 19.
The observed helper writes `+0x18` first, then `+0x14`, then polls both status bits.
The association between each individual request and status bit is explicit virtual policy.
The selected helper does not distinguish that association because it writes both requests before polling.
The first status mask comes from instruction bytes `37 07 04 00`, which encode `LUI a4,0x40`.
The resulting value is `0x40000`, not `0x400000`.
The second instruction is `37 07 08 00`, which produces `0x80000`.
Both packages contain these exact words.
Their PCs are SM8850 `0x812d464e/0x812d4662` and SM8845 `0x812445fa/0x8124460e`.
The corresponding ELF offsets are `0x3c64e/0x3c662` and `0x3c5fa/0x3c60e`.

Clearing a request clears its qualification.
Repeated requests are idempotent.
Source loss or removal of a required enable clears both qualifications.
Source restoration restores qualification only while the configuration and requests remain valid.
Status reads do not execute a transition, consume a request, or change retained state.
No arbitrary delay substitutes for absent timing evidence.
The configuration takes effect through normal MMIO writes.

The boot owner still accepts bit 8 only for the existing V2 reset contract.
Runtime bit 12 is available only when the platform enables this optional interface with an actual clock source.
Neither bit 12 nor the runtime status requests architectural reset.
Neither status bit represents firmware command completion, an authenticated result, or peer delivery.
No interrupt is generated by these configuration writes.
The source register names and electrical event mapping remain unproven.

Ordinary event, policy, and clock configuration words retain all 32 bits.
Qualification tests only the stated enable fields and write presence.
Observed writes do not establish reserved-bit masks.
Unknown offsets, unsupported request bits, invalid commands, byte enables, and invalid widths still fail.
The normalized interfaces in the helper have separate event, policy, and clock-bank apertures.
They do not expose unrestricted register RAM or SoC conditions in C++.

### Guest coverage and verification handoff

The existing `cpucp-reset` producer now checks this group after the actual reset consumer reaches boot count two.
Both V1 and V2 use the same checks.
The tests cover:

- Requests before configuration.
- An absent final configuration word.
- Complete configuration with a stopped actual clock branch.
- Qualification after that branch starts.
- Independent request clearing and repeated requests.
- Policy loss, source loss, and source restoration.
- Clock enable removal and restoration.
- Independent halves of a lane policy word.
- Unrelated configuration bits and nonzero values at clock offsets `4` and `8`.
- An unchanged real consumer boot count throughout the transition.

The fixture uses an existing clock-branch model with an explicit always-on reference.
The production board instead uses its existing domain source.
The fixture does not substitute a boolean readiness parameter.
The legacy RNMI fixture inherits these reset checks too.
No new Rust test registration is required.

With the integrated model built, run:

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform cpucp_cease_requires_reset_and_uses_the_published_vector -- --ignored --exact
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform cpucp_legacy_rnmi_preserves_reset_traps_interrupts_and_disabled_boundary -- --ignored --exact
```

Both focused reset tests pass after the status-mask correction.
SM8850 run269 and SM8845 run215 pass both qualification polls.
They next request an eight-byte counter read at `0x1781d000`, which the existing timer rejects.
The failing PCs are `0x1d8023f6` and `0x1d802456`, respectively.
The complete integration run passes 47 host tests and 72 platform tests. Clippy passes with warnings denied.

### Secure CPU_ON runtime preparation

SM8850 preparation starts at `0x1d809870`. SM8845 preparation starts at `0x1d809be0`.
Both routines issue the same relative outer-power accesses in the same order:

1. Update `+0x2c0`, `+0x3f8`, and `+0x4e8`.
2. Update per-core fields in `+0x288`, then `+0x3f8`.
3. Update the second per-core field in `+0x288`.
4. Update `+0x4e8`, set bit 53 at `+0x4f8`, and update `+0x3f0` twice.
5. Set bit 28 at `+0x260`.

The `+0x288` writes place per-core bytes in bits `[48:41]` and `[57:49]`.
The `+0x3f0` writes place the cluster index in bits `[32:0]` and core index in `[57:33]`.
The `+0x4e8` write sets bit 1. The `+0x4f8` write sets bit 53.
These are retained configuration writes. They do not report completion or release reset.
The existing `+0x278`, `+0x2b0`, per-core policy, and `+0x260[23:16]` sequence owns power acknowledgement and reset release.

The hooks `0x812d50c4` and `0x81245060` load descriptor offset `+0x140`, then read `+0xd00`.
The address formula is `0x19030d00 + cluster*0x1000000 + core*0x100000`.
The hook accepts cluster indices zero and one, with core indices zero through five.
The second cluster has two configured cores. The read value flows into registered guest predicates.
This value mirrors pinned QEMU max-TCG MIDR `0x000f0510`.
The pinned source constructs that value from implementer zero, architecture `0xf`, and part number `'Q'`.
The CPUCP predicate mask `0xff0ffff0` and comparison value `0x510f0020` do not match this identity.
The predicate mismatch is normal guest behavior. The model does not fabricate a Qualcomm MIDR to select a workaround.

The feature aperture rows route one 64-bit word per configured CPU.
Each alias maps the physical `+0xd00` word to its cluster/core-relative offset:

| Cluster | Guest word | Size | Feature-socket offset |
| --- | --- | --- | --- |
| 0, core `k` from 0 through 5 | `0x19030d00 + k*0x100000` | 8 | `k*0x100000 + 0xd00` |
| 1, core `k` from 0 through 1 | `0x1a030d00 + k*0x100000` | 8 | `k*0x100000 + 0xd00` |

The aliases do not overlap the existing per-core policy ranges.
Both ranges route to read-only feature transport. Each core word returns `0x000f0510`.
Unsupported clusters, cores, writes, widths, and alignments return errors.
The power socket size already covers each new register.
Add these missing sparse rows to the address router:

| Lane | Guest address | Size |
| --- | --- | --- |
| 0 | `0x19880288`, `0x198803f0`, `0x198804e8`, `0x198804f8` | 8 each |
| 1 | `0x1a880288`, `0x1a8803f0`, `0x1a8804e8`, `0x1a8804f8` | 8 each |

The `cpu-domain` fixture retains both lanes' preparation words,
checks the QEMU MIDR identity, and shows that held cores remain held.
It writes control bit 28 only after both domain lanes are initialized.
Any write to `+0x260` transfers reset ownership to CPUCP.
A cold domain then holds its cores.
After integration, run:

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform cpu_domain_acknowledgements_require_live_hardware_and_deliver_reset -- --ignored --exact
```

No build, test, formatter, OEM run, or commit was performed during this recovery.

## Runtime interrupt configuration

SM8850 `0x1d80c37c..0x1d80c396` sets bit zero at `0x188787a0` and bit four at `0x18878424`.
SM8845 `0x1d80c7b2..0x1d80c7cc` performs the same operations.
These operations follow the interrupt setup calls in the initialization function.
The existing domain controller retains both words. The sparse platform routes expose only these observed addresses.
The virtual words start at zero. They do not create interrupts, release cores, or fabricate acknowledgements.
The physical effects remain unmeasured.
SM8850 run284 passes these accesses and stops at `0x188c8000`, from PC `0x812d5eb8`.
SM8845 run223 reaches the same next address from PC `0x8124772e`.

## Native doubleword tag access

SM8845 `0x1d808b5c..0x1d808b72` uses native 64-bit reads and writes for cluster tags at `+0x630` and `+0x418`.
The routine preserves the existing upper tag fields and replaces the low channel index.
The existing model now accepts both 32-bit and 64-bit accesses to the same tag state.
High bits outside the implemented 20-bit tag remain invalid.
The platform aliases cover the complete eight-byte access. They do not introduce a second register owner.
The native domain fixture checks doubleword readback and word access to the same retained tag.

## QRNG policy does not control architectural reset

The ROM sets fabric `+0xf8` bit 52 and power `+0x310` bit zero before cluster initialization.
The original model incorrectly treated both values as permanent reset prerequisites.
The selected CPUCP runtime identifies their purpose through the `enable_qrng` property.
SM8850 `0x1d80943a..0x1d809470` writes the inverse property value to both fields.
It writes the property value to QRNG generation bit one.
SM8845 `0x1d80979e..0x1d8097d6` performs the same operations.
These are QRNG policy controls, not proof of lost power or isolation.

The domain model retains both fields without using them to assert architectural reset.
Supply, clock, sequencer, and explicit reset conditions remain effective.
The native domain fixture changes both fields while a real APSS consumer updates its heartbeat.
The old model stops that consumer at fixture stage 201, address `0xbad00324`.
The corrected model passes the fixture, including its existing source-loss and supply-loss checks.

## Runtime QCCE policy cohort

The selected CPUCP images apply QCCE workarounds through 56-byte records and a shared range matcher.
The main tables start at SM8850 `0x812fb448` and SM8845 `0x8126eda8`.
Each record supplies a name, an identifier, a range table, a matcher, and an action callback.
The matchers at `0x1d809d7e` and `0x1d80a0ee` separate identity bits with mask `0xff0ffff0`.
They compare the remaining revision bits against inclusive ranges in 12-byte records.
The query describes the CPU identity, not an MMIO offset.

The callbacks select the lane descriptor and change these retained words.
Power, fabric, sequence, and auxiliary bases are `0x19880000`, `0x19830000`, `0x198b0000`, and `0x19aa0000`.
Each lane adds `lane << 24` to those bases.

| Policy | SM8850 callback | SM8845 callback | Register effect |
| --- | --- | --- | --- |
| QCCE-265 | `0x1d809de6` | `0x1d80a156` | Power `+0x4f0`: replace mask `0x7000000` with `0x5000000` |
| QCCE-278 | `0x1d809e1a` | `0x1d80a18a` | Power `+0x410`: replace bits 35:34 with one |
| QCCE-279 | `0x1d809e42` | `0x1d80a1b2` | Power `+0x338`: clear bit 36 |
| QCCE-316 | `0x1d809e64` | `0x1d80a1d4` | Fabric `+0x108`: set bit four |
| QCCE-334 | `0x1d809e82` | `0x1d80a1f2` | Auxiliary `+0x10`: set bit 32 |
| QCCE-335 | `0x1d809ea6` | `0x1d80a216` | Power `+0x4f0`: set bit eight |
| QCCE-422 | `0x1d809ec4` | `0x1d80a234` | Fabric `+0x88`: clear bit 31 |
| QCCE-459 | `0x1d809ee2` | `0x1d80a252` | Fabric `+0x100`: set bit 21 |
| QCCE-463 | `0x1d809f02` | `0x1d80a272` | Fabric `+0x90`: write per-lane boolean to bit 63 |
| QCCE-466 | `0x1d809f36` | `0x1d80a2a6` | Fabric `+0x80`: replace mask `0x1f80000` with `0x80000` |
| QCCE-506 | `0x1d809f5a` | `0x1d80a2ca` | Fabric `+0x118`: clear bit ten |
| QCCE-513 | `0x1d809f78` | `0x1d80a2e8` | Auxiliary `+0x10`: set bit 34 |
| QCCE-515 | Not selected | `0x1d80a30c` | Fabric `+0x80`: clear bits 61:60 |
| QCCE-555 | `0x1d809f9c` | `0x1d80a32a` | Sequence `+8`: set bit 17 |
| QCCE-580 | `0x1d809fb8` | `0x1d80a346` | Existing power `+0x3f8`: replace timing fields |
| QCCE-711 | `0x1d809fe2` | Not selected | Fabric `+0x168`: replace mask `0x3e` with eight |
| PGS-44779 | Not selected | `0x1d80a3ac` | Fabric `+0x118`: clear bits one and zero |

The existing owners retain these explicit configuration words from virtual zero.
The model does not reproduce undocumented microarchitecture errata or their electrical effects.
No policy write creates completion, changes architectural reset, or supplies a firmware response.
The native fixture checks lane isolation and continued APSS execution across policy writes.
SM8850 run304 and SM8845 run234 pass QCCE-279 and reach the auxiliary QCCE-513 access.
With the auxiliary word connected, SM8850 run305 and SM8845 run235 reach the next initializer at `0x19860100`.
The native domain fixture and complete integration suite pass.

## Post-QCCE configuration

SM8850 `0x1d80a004..0x1d80a092` initializes two policy banks and the fabric fields after the workaround callbacks.
SM8845 uses `0x1d80a41a..0x1d80a4a8`.
The descriptor supplies policy bases `0x19860000` and `0x19870000`, plus the usual lane offset.
The initializer clears word `+0x100`, clears mask `0x30000` at `+0x400`, and writes one at `+0x3000`.
These are bounded configuration writes without a completion poll.
These words belong to the NCC MPAM frames, so partition zero receives them. See `mpam_evidence.md`.

The fabric initializer preserves the low half at `+0xa0` and writes its upper configuration fields.
It writes `0x3210` to bits 15:0 at `+0xa8` and one to bits 43:40 at `+0xf8`.
The later SM8850 store at `0x1d80a17e` writes `pipDisVicWays` to bits 15:0 at fabric `+0xc8`.
The existing fabric owner retains these fields without creating a power or reset event.

SM8850 run307 next reaches fabric `+0x320` from `0x1d808ff4`.
Initializer `0x1d808ff0..0x1d809028` writes `0x3ab0` to bits 15:0, sets bit 48, and clears bit 49.
SM8845 performs the same operations at `0x1d8093a2..0x1d8093da`.
The shared fabric word retains both CPUCP writes and secure APSS accesses.
See the deferred QSEE consumer in `docs/qcom/sm88xx/boot-dependencies.md`.

The Infiniti EL1 probe of `0x19830320` failed and returned no value.
See `docs/qcom/sm88xx/oplus/infiniti/hardware-evidence.md` for the journal and crashdump association.
That probe does not establish this word's reset value or its CPUCP access policy.
The virtual word starts at zero as an explicit configuration substitute.

SM8850 run308 passes this initialization and reaches the auxiliary PLL at `0x19aa1000`.
SM8845 run236 reports no unmodeled access before the APSS watchdog expires.
Neither result establishes a kernel handoff.


## Runtime core configuration

The SM8850 CPUCP runtime function at `0x812d658c` initializes six ordinary records in either lane.
The SM8845 image includes the same function and the same base descriptors.
Lane zero starts at `0x19850000`, and lane one starts at `0x1a850000`.
Each ordinary record uses a `0x40`-byte stride.

The function accesses only 64-bit words at offsets `0x08`, `0x10`, `0x18`, and `0x20`.
It writes zero to three words and preserves the value at offset `0x10`.
The main loop supplies six ordinary records, independent of the attached APSS core count.
The descriptor also supplies a special record at offset `0x168`.
That record uses offsets `0x00`, `0x10`, `0x18`, and `0x20`.
The function does not poll these words or use them as hardware-produced status.

The CPU domain owner retains only these exact words.
The composition maps these records in both lanes without changing the APSS core masks.

Run553 stopped at the third lane-one record, address `0x1a850098`, from PC `0x812d65ce`.
Run554 passes that write after this correction.
It stops later at CPUCP PC `0x1d812752`, which reads address `0xa18`.
The build succeeds, and all 81 platform tests pass with both register banks in the fixture.

Every gap remains an error.
Runs314 and315 identify the first normal and special lane-zero accesses.

SM8850 `0x1d812744..0x1d812764` later updates offset `0xa10`.
It copies bits from read-only offset `0xa18` into that retained control.
The virtual status starts at zero because no modeled producer drives it.
The code does not poll this status.

SM8850 `0x1d80def8..0x1d80df00` later sets bit 32 in the NCC bridge control.
The existing bridge owner retains this firmware request with its other selected fields.
This bit creates no separate reset or completion effect.

SM8850 `0x1d809a40..0x1d809a7c` updates power words `0x570` and `0x420`.
It clears and sets fields without waiting for hardware completion.
The existing power owner retains these two runtime configuration words.

PDP `0x20000974` and `0x20001cf4` read power word `0x420` with a 32-bit access, clear bit 24, and write the half back.
Run604 stopped at the first 32-bit read, local `0x40880420`, because the owner accepted only 64-bit transfers.
The owner now accepts an aligned 32-bit half of each declared word.
A write merges the half into the doubleword before the doubleword checks and effects apply.
The two status words stay read-only, and undeclared offsets still fail.

SM8850 `0x1d80adea..0x1d80ae2a` changes fabric word `0x150` and power word `0x578`.
The function selects retained fields from its Boolean argument.
Function `0x1d80ae2c..0x1d80ae54` similarly changes bits 3:1 at power word `0x600`.
Neither function polls these words or waits for a completion event.
The existing owners retain these exact configuration words independently for each lane.

The CPU-domain guest fixture checks these words and the runtime configuration records.
The focused guest test passes after the model changes.
SM8850 run323 reaches its execution timeout without verified kernel entry.

After the PDP image copy, SM8850 `0x1d80b736..0x1d80b73e` sets request `0x258` bit 6.
The function then schedules a software timer and logs `PDP:%d image loading done`.
The request owner retains this bit without changing APSS reset ownership or manufacturing PDP readiness.
The source does not establish a hardware completion effect for this bit.
The guest fixture checks that this request preserves the running APSS core and keeps a held core stopped.
The focused test passes.
SM8850 run326 passes the request and reaches its execution timeout without an unmodeled-access report.
Kernel entry remains unverified.

## Runtime sequence status

SM8850 `0x1d80cea0..0x1d80cefe` reads sequence offset `0x50`.
Bit 32 means that a transaction is busy, not that the interface is available.
A clear bit skips directly to the performance-state check at `0x1d80cef8`.
When bit 32 is set and bits 23:16 are zero, the function logs `Wait for current transaction to complete`.
It then returns `-6`.
PDP `0x2000b2c2` checks the same bit, and `0x2000b272` waits for it to clear.

The earlier model held bit 32 at one, which made both consumers wait without an active transaction.
The cold state now reports zero because no transaction exists.
This state does not establish completion of a future request or replace its hardware effects.
Guest writes to this status fail.

A native SystemC smoke read the actual owner and applied both recovered consumer predicates.
Before the correction, it reported `cpucp_return=-6 pdp_wait=1`.
After the correction, it reported `cpucp_return=0 pdp_wait=0`.

The selected path then reads `0x3a04` and extracts an implementation value.
No selected source identifies a nonzero virtual value or a writable field.
The profile returns zero and rejects nonzero writes.

The focused CPU-domain test passes after these changes.
SM8850 run340 passes both reads and reaches `0x18874004`.
Kernel entry remains unverified.

## Per-core runtime configuration

The shared runtime function at SM8850 `0x812d6622` initializes three records for each selected core.
Each record has a `0x40`-byte stride and uses offsets `0x08`, `0x10`, `0x18`, and `0x20`.
The first lane-zero record starts at `0x19040000`.
The existing `0x100000` core stride and `0x1000000` lane stride select the other records.
SM8845 uses the same runtime function body at a different image address.

The existing core owner retains only these 12 words per core.
The words start at virtual zero and create no reset, power, or completion effect.
The guest fixture checks every selected core, record, and word through real TLM accesses.
Its focused test and the Lua syntax checks pass.

SM8850 run336 passes this group and reaches `0x17951064`.
Kernel entry remains unverified.

## Per-core runtime policy records

The SM8850 runtime writer at `0x812d5508` programs paired 64-bit fields from software records.
SM8845 uses the matching writer at `0x812454a4`.
The first pair starts at core bank offset `0xb8`, with a `0x10`-byte row stride.
The companion selector helper writes paired fields at offsets `0xf8` and `0x100`.
Its four-bit selector permits four rows.
The recovered words occupy offsets `0xb8` through `0x130`, with an eight-byte stride.

The existing core owner retains these fields separately for each selected core and lane.
The words start at virtual zero and do not change power acknowledgement or architectural reset.
The guest fixture checks full-width retention and lane separation through the existing core sockets.
Its focused test and the Lua syntax checks pass.

SM8850 run335 passes these records and reaches `0x19040018`.
Kernel entry remains unverified.

## Per-core debug configuration

SM8850 `0x1d8034a0..0x1d8034b2` derives each debug base from descriptor field `+0x138`.
The first base is `0x1d001000`.
Each core adds `0x1000`, and each lane adds `0x100000`.
The selected profile has six lane-zero cores and two lane-one cores.

SM8850 `0x1d80975a..0x1d809774` sets bit zero at base offset zero and bit one at offset `0x150`.
These offsets match Arm [CTICONTROL](https://dflund.se/~getz/ARM/SysReg/ext-cticontrol.html) and [CTIDEVCTL](https://dflund.se/~getz/ARM/SysReg/ext-ctidevctl.html).
The architecture defines a global enable bit and two debug catch enable bits.
This match supports a CTI identity but does not establish the complete physical trigger wiring.

The profile uses the existing retained-control model for these two exact words.
Their writable masks are one and three, and both start at zero.
No external debugger or trigger producer connects to these words.
The substitute creates no debugger stop, reset, or authentication grant.

Lua syntax and the native configuration sequence pass.
SM8850 run334 next writes the core policy word at `0x190320b8`.
Kernel entry remains unverified.

## Startup clear and enable sequence

SM8850 `0x1d80cc66..0x1d80cd5c` clears two words and enables three controls for each lane.
SM8845 uses the matching sequence from `0x1d80d0ba`.
Each lane adds four bytes to these physical addresses.

The sequence reads `0x1887c180` and `0x18878080`, then writes zero to each word.
The virtual profile supplies a quiescent zero value and accepts only zero writes.
No modeled producer raises a bit in either word.
This policy does not identify the physical event sources.

The sequence writes one at `0x18878040` and `0x18871200`.
It changes bits 1:0 at `0x18871300`.
The boot lane also changes bits 2:0 at `0x18878400`.
The existing domain owner retains these configuration fields without creating a reset or interrupt.

The guest fixture checks both lanes and preserves the APSS execution invariants.
Its focused test passes.
SM8850 run333 passes this sequence and reaches `0x1d001000`.
Kernel entry remains unverified.

## Lane configuration windows

SM8850 function `0x1d808a52` programs the bank from descriptor field `+0x70`.
SM8845 uses the matching function at `0x1d808e04`.
The first descriptor selects `0x18080000`.
The virtual profile maps the second lane at `0x18280000`, with the existing `0x200000` lane stride.

The SM8850 table at `0x1d82e770` contains eight lane-zero entries and four lane-one entries.
SM8845 uses the same counts at `0x1d82e730`.
The loop reads and writes 64-bit words at offsets `0x08` through `count * 8`.
It preserves the low 32 bits and combines fields from two software arrays.
It performs no completion poll.

The existing CPU-domain owner retains only those exact words independently for each lane.
Virtual cold zero does not claim a measured hardware reset value.
The model does not infer memory protection or translation effects from these unnamed fields.
The guest fixture checks retained values, lane separation, and unchanged APSS reset behavior.

SM8850 run332 passes the table and reaches a read at `0x1887c180`.
The focused CPU-domain test passes.
Kernel entry remains unverified.

## Runtime route policy

The SM8850 CPUCP runtime image function at `0x812d686e` selects the lane descriptor.
It programs 64-bit route words at offsets `0x48` through `0x80`, with an eight-byte stride.
The lane descriptors point to `0x198f0000` and `0x1a8f0000`.
The function applies masks and constant fields to these eight words.
It does not poll a word or use it as hardware-produced status.

The existing route owner retains each word independently for each lane.
These words do not satisfy the three route fields required for core release.
Runs312 and313 identify the first two accesses at `0x812d68aa` and `0x812d6aba`.

SM8850 `0x1d810d96..0x1d810daa` later samples route offsets `0x88` and `0x90`.
It copies both values into a software report and does not poll them.
The selected source does not identify their fields or a producer.
The virtual profile reports zero as an explicit no-event state.
Guest writes to these two status words fail.

## Auxiliary Zonda PLLs

The SM8850 descriptor words at `0x1d82ee88` and `0x1d82eed8` select `0x19aa1000` and `0x1aaa1000`.
SM8845 uses descriptor words `0x1d82eee8` and `0x1d82ef40` for the same addresses.
SM8850 driver `0x1d80dd3e` reads MODE, programs the divider and output controls, and waits for lock.
Its register sequence matches the existing Zonda PLL contract.

The common platform therefore instantiates that existing model once per lane.
Each exact aperture spans `0x34` bytes.
The model retains its 19.2 MHz virtual reference and five-microsecond acquisition policy.
The platform does not duplicate the PLL implementation or select behavior through a SoC check.

SM8850 runs309 and310 pass this driver and next read `0x17c30020`.
The failed Infiniti EL1 read at `0x19aa1000` supplies no physical reset value or lock timing.
See `docs/qcom/sm88xx/oplus/infiniti/hardware-evidence.md` for that access limitation.

## Per-core release pair

The SM8850 input is `tz-8850-afdfaf27.img`.
Its SHA-256 is `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932`.
The SM8845 input is `RADIO/tz.img` from the CPH2771 package.
Its SHA-256 is `b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09`.
SM8850 TZ `0x1573e964..0x1573e984` stores one aligned 64-bit pair for a single core.
SM8845 TZ `0x157317e4..0x15731804` contains the same sequence.
The sequence loads `0x19050000` at `0x1573e96c`.
The address is `0x19050000 + core*0x100000 + lane*0x1000000`.
The core byte is at descriptor `+0x27`.
The lane byte is at `+0x27` of the object at descriptor `+0xa8`.
The same lane byte indexes the per-lane block at `0x15814980 + lane*0x80`.
The SM8850 cohort path at `0x1573dc74..0x1573dc90` uses the same formula.
That path reads the core list from `+0xb0` and the count from `+0xb8`.
The object that holds this list supplies the 16 MB lane term.
The per-core descriptor supplies the 1 MB term.
That path reads its payload from the per-CPU array at `0x1564db88`.
The function at `0x1573eda4` fills the eight array entries with `-1`.
The selected source has no other direct reference to that array.

The single-core path writes `-1` to `+0x8` and zero to `+0x0`.
The cohort path writes the saved payload to `+0x8` and one to `+0x0`.
Both paths write `+0x8` first.

The pair write follows a GIC redistributor sequence for the same CPU.
The code tests bit 29 at `0x17090100 + cpu*0x40000`.
That word is GICR_ISENABLER0 in the architectural SGI frame.
The code writes `0x20000000` to `0x17090180` and `0x17090280`.
Those words are GICR_ICENABLER0 and GICR_ICPENDR0.
The code then polls bit 3 at `0x17080000 + cpu*0x40000`.
That word is GICR_CTLR.
Bit 29 enables the secure physical timer PPI.
The handler dispatches on a 16-bit power state with values two through six.
The strings near the handler name PSCI.
The call at `0x1573eb84` passes the per-CPU array into the cohort path.
The TZ string at `0x157d0ae2` names a PSCI cluster bring-up failure.

No selected source names the block at `0x19050000`.
ARM PMU and RAS use system registers, not this aperture.
The debug trigger block in this profile sits at `0x1d001000`.
The evidence places the pair in the APSS per-core register window.
No available document names its block or its fields.
Its physical register name and its read semantics remain open.
The model applies the functional start request described in the next sections.

## Retained release pair

The CPU-domain owner keeps two 64-bit words for each selected core and lane.
`release0_socket` and `release1_socket` accept only offsets zero and eight.
The model requires an aligned eight-byte access.
It rejects byte enables, other widths, other offsets, and unselected cores.
A write replaces one retained word.
A read returns the retained word.

`platforms/qcom/sm88xx/reset.lua` routes each pair through an `addrtr` at
`0x19050000 + lane*0x1000000 + core*0x100000`.
The translation supplies `core*0x100000` as the local base.
Only the APSS router reaches these pairs.
CPUCP access remains unobserved.
SM8845 uses the same platform builder and the same TZ sequence.

The first observed access is `0x19050008` from SM8850 run405.
The APSS monitor reports `pc=0x17633e980`, `address=0x19050008`, width eight, and a write.
The TZ link PC is `0x1573e980` and the payload is `-1`.
Run405 ended at that store with an unmapped-access report.

## Core start request

Run406 models the pair. The TZ then reaches the poll at `0x1573e678`.

TZ `0x1573e608` is a CPU power method.
It loads the driver table from CPU descriptor `+0x10`.
It calls the method at driver `+0x28` with the descriptor, the state argument, and a table pointer.
Run406 executes `0x1573e870..0x1573e998` between that call and the poll.
That range is the executed wake body.
The trace does not capture the table value at driver `+0x28`.
This note does not claim `0x1573e870` as that value.

`0x1573e870` tests bit 29 of `0x17090100 + cpu*0x40000`.
That word is GICR_ISENABLER0.
It clears bit 29 in GICR_ICENABLER0 and GICR_ICPENDR0.
It polls bit 3 of GICR_CTLR at `0x17080000 + cpu*0x40000`.
It writes the release pair only when the PPI was enabled.

The pair carries a 64-bit payload at `+0x8` and a command at `+0x0`.
The single-core path writes `-1` and then command zero.
The cohort path at `0x1573dc74..0x1573dc90` writes a per-CPU 16-byte entry and then command one.
Both paths use the same address formula.
The model accepts command values zero and one.

The model applies this functional contract:
- Command zero or one requests a start of the addressed core.
- Payload `-1` selects the published warm vector at descriptor `+0x328`.
- Any other payload replaces that vector before reset release.
- The request sets the core power bit in `+0x278` and clears the core reset bit in `+0x260[23:16]`.
- The request is ignored until the lane reports the initialized cluster.
- A core that is already out of reset keeps its execution.

The physical identity of the `0x19050000` block remains open.
This contract is an inference from the two TZ producers and the following poll.
It does not claim a silicon register name.

## APSS CPU boot status block

TZ `0x1573e644..0x1573e69c` polls `0x81f5a000 + lane*0x40000`.
It reads the word at `+0x30` and compares it with `0xabcd9876`.
It then reads the halfword at `+0x02`.
It tests bit `1 << core` of that halfword with an eight-bit mask.
Bits 16..23 of the first word therefore carry the per-core online state.

The producer is the PDP firmware.
`IMAGES/pdp.elf` maps its first load segment to `0x81300000`.
File offset `0x14b80` maps to ELF address `0x81321b80`.
Thumb `0x81321b80` clears `0x100` bytes at `0x81f5a000`.
It writes `0xabcd9876` at `+0x30` and halfword two at `+0x34`.
It copies `0x80` bytes from `0x8132b8ad` to `+0x80` and initializes `+0x38`.
The block pointer lives at PDP data `0x813d0a94`.
Thumb `0x81321c78` sets or clears one bitmap bit.
Thumb `0x81321b4c` tests one bitmap bit.
These sites are from the SM8850 package.
This note does not inspect the SM8845 PDP image.
The SM8845 TZ contains the same poll sequence, so the block values are the same.

The model does not execute PDP firmware.
The CPU domain publishes the same header through the optional `boot_status_socket`.
`platforms/qcom/sm88xx/pdp-scmi.lua` adds two `addrtr` aliases at `0x81f5a000` and `0x81f9a000`.
Each alias forwards to the existing DDR owner, so no RAM overlay exists.
The native runtimes pass `cpu_domain='cpu_domains'` to that helper.
A PDP-only fixture omits the option and skips the publication.
The domain writes the header after a lane initializes and on each release command.
The bitmap reports cores that are attached, powered, and out of reset.
The model writes bits 16..23 only. The other PDP bitmap bits stay zero.
This is a functional completion record. It is not an authentication claim.

## Cluster bring-up publication words

Run407 passes the wake poll.
The next access is a read at `0x1887004c` from TZ `0x1573eddc`.

TZ `0x1573ed44` is the cluster bring-up.
It calls the per-lane LPM lock initializer at driver `+0x8`.
It fills the eight-entry per-CPU table at `0x1564db88` with `-1`.
It sets bit zero at `0x1887004c`.
It resolves the `apss` device node into `0x15814900`.
It writes one to `0x18878040` and `0x18878044`.
It writes zero to `0x18878080` and `0x18878084`.
It clears bit two at `0x17952000`.

CPUCP `0x1d808e70..0x1d808e78` clears bit zero at `0x18870050`.
That address is four bytes above the TZ word.
The two words are separate. This note does not claim a set/clear pair.
No selected firmware reads `0x1887004c` after the TZ write.
The selected TZ and CPUCP images do not access `0x17952000` otherwise.

The model retains `0x1887004c` with writable mask one and reset zero.
It retains `0x17952000` with writable mask four and reset zero.
Both words have no completion, power, or reset effect.
`platforms/qcom/sm88xx/reset.lua` binds both to the APSS router only.

## Run406 shutdown exception

Run406 ends with a data abort on CPU zero.
The ESR is `0x96000210`, EC `0x25`, and DFSC `0x10`.
The FAR is `0x17815000` and the ELR is `0x1566edd0`.
The vector is `0x14695200`.
The delay helper `0x1566eda4` reads the Qtimer frame-two counter at `0x17815000`.
Its second read at `0x1566edd0` faults after many successful reads.

QBox marks the CPU initiator finished during `QemuCpu::end_of_simulation`.
The bridge then returns `MemTxError` for a later guest access.
QEMU converts that result into a synchronous external abort.
`docs/qcom/sm88xx/boot-dependencies.md` records this path.
The timer model is not the cause.

The IMEM snapshot contains TZ code at `0x14694af0`.
It contains no ESR, FAR, or ELR record table there.
The recorder at `0x146942c4..0x14694abc` is the architectural vector body.
It takes no separate fault record that this run exposes.

## Next prediction

Run407 passes the wake poll and reaches the cluster bring-up.
The two retained words should let `0x1573ed44` finish and return zero.
QSEE should then continue the PSCI state machine from its caller.
A later target core should leave reset at the published warm vector `0x1468e000`.
The APSS trace should then show TZ warm-entry translation blocks.

The next candidate stop is the per-lane LPM method at driver `+0x8` if it returns an error.
That error path logs the LPM synchronize lock failure at `0x157d0ae2`.
The secure SCMI cluster path remains unmodeled and can stop later work.



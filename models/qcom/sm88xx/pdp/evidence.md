# PDP execution contract

## Inputs and scope

The SM8850 runtime executes the package PDP image instead of the virtual SCMI service.
The image is `IMAGES/pdp.elf` from the CPH2749 `.700` package.
Its SHA-256 is `74fb165a6619f2451b56360eddd2628827095497593474089316056de8437e8d`.
The file contains 323448 bytes.
No firmware bytes belong in this repository.

The ELF uses machine zero and entry zero.
It contains no section table, so `.ARM.attributes` cannot establish the CPU identity.
The model uses QEMU Cortex-M7 as an architectural substitute, not a measured PDP CPU identity.
The selected instructions use BASEPRI, PSP, CONTROL, VFP, and M7-compatible cache operations.

## Image views

| File offset | System load address | File and memory size | Execution view |
|---|---|---|---|
| `0x1000` | `0x81300000` | `0x108b8` | Local SRAM `0x20000000` |
| `0x12000` | `0x8131d000` | `0x588` | Local SRAM `0x2001d000` |
| `0x13000` | `0x81320000` | `0x11fc8` | DDR identity |
| `0x25000` | `0x8136e000` | `4` | DDR identity |
| `0x26000` | `0x8136f000` | `0x80` | DDR identity |
| `0x27000` | `0x81370000` | `0x2270c` | DDR identity |
| `0x4a000` | `0x813d0000` | `0x798` | Programmable DDR view |
| `0x4b000` | `0x813e0000` | `0x798` | Lane-one DDR data |

The ELF does not zero-fill the first segment to `0x20000`.
The CPUCP loader separately copies that entire interval into private SRAM.
Its descriptor constructor adds `lane << 24` to template destination `0x19930000`.
The two destinations are `0x19930000` and `0x1a930000`, each with size `0x20000`.
See `models/qcom/sm88xx/cpucp/acehr_evidence.md` for the exact producer instructions.
The PDP local alias refers to the same SRAM owner, not a second image copy.

## Reset and clocks

The vector table at file offset `0x1000` supplies SP `0x2001f500` and reset handler `0x200018a5`.
Reset `0x200018a4` sets BASEPRI, PSP, and CONTROL before it calls `0x20001ab4`.
That function configures VTOR, CPACR, and FPCCR, then writes FPSCR.
It initializes address windows before it clears local and DDR BSS.
The DDR BSS clear at `0x2000ae14` covers `0x813d0798 + 0xfb4` bytes.

Function `0x20000750` uses ACTLR bit 11 and CCR cache bits 16 and 17.
It uses ICIALLU, CSSELR, CCSIDR, and DCISW through the architectural SCB.
QEMU owns these registers and their cache-maintenance behavior.
The platform does not create private cache completion words.

Function `0x20001fa4` reads `MPU_TYPE.DREGION` at `0xe000ed90`.
It compares that count with the configured region count 13 at `0x2000d0b8`.
With eight regions, run563 reaches the Zephyr assertion in `arm_mpu.c`, line 415.
The platform selects the sixteen-region Cortex-M7 MPU configuration.
This choice satisfies the firmware's stated requirement and does not bypass the check.

Function `0x20002278` sets SysTick reload `0x77f` and enables its interrupt without setting CLKSOURCE.
The SysTick handler at `0x200022b0` accounts for `0x780` cycles per tick.
The microsecond delay at `0x20008454` uses frequency `19200000` and polls the SysTick cycle count.
The platform therefore supplies a 19.2 MHz reference clock.
The separate CPU clock uses the same explicit virtual frequency, not a silicon performance claim.

The NCC timer is separate from SysTick.
Function `0x2000686c` reads its 64-bit counter at `0x408d0010`.
Initializer `0x81327540` reads its frequency at `0x408d0038` and derives the timestamp conversion factor.
The model supplies a time-based counter with an explicit 19.2 MHz virtual frequency.
It does not increment the counter because a read occurs.

## Functional reset release

CPUCP copies the authenticated image through ACEHR before it sets cluster request `+0x258` bit 6.
The store is at `0x1d80b73e`.
The following CPUCP task polls the PDP-produced status and version fields.
No recovered CPUCP instruction writes a separate PDP reset vector or reset-release register.

[INFERENCE] The functional NCC sequencer uses that load-completion flag to release the real PDP CPU.
It also requires the initialized cluster, its available clock, and released domain reset.
The exact silicon reset circuit remains unknown.
The controller prepares architectural reset, checks the gates again, and then releases without suspension.
The CPU fetches both SP and PC from the guest-loaded vector table.
No host code sets its current PC or supplies firmware return values.

## PDP reset-vector publication

Function `0x20006924` logs `init_cl5 entry`.
Its store at `0x20006968` publishes `0x2000c781` to local register `0x408802c8`.
The lane-zero system view is `0x198802c8`.
The private layout names this word `NCC_PDP_RVBAR`.
Bits 31:7 select the vector base, and the low seven bits are reserved.

The masked base is `0x2000c780`.
That table contains SP `0x2001f500` and reset handler `0x2000c701`.
This is normal vector-table publication, not a panic path.
Run591 stops at this register write after the firmware publishes late initialization.

The register accepts a low 32-bit access or a native 64-bit access.
Its reserved upper word reads zero.
Publication does not restart a running CPU.
A later architectural reset fetches SP and PC from the selected table.
The controller publishes the vector before it prepares reset.
The CPU updates the native `init-nsvtor` property before the architectural reset.
The existing FIFO barrier waits for the native vector fetch before release.

The CPU-domain fixture executes an M7 guest with two vector tables.
It verifies continued execution after publication and new SP and PC values after reset.
All 88 platform tests pass with this fixture.
Run592 passes the OEM publication and reaches the DVFS status read at local `0x408b0788`.
Kernel entry remains unverified.

## Shared cluster bank and SMEM pointer

PDP local `0x40898000` addresses its cluster register bank.
The system view is `0x19898000 + (lane << 24)`, which the CPU-domain model already owns.
CPUCP `0x812d44a6` reads bank offset `0x5d0` and extracts bit 54.
PDP `0x20000730` reads the same offset and extracts bits 15:8 as its lane.
The lane-one data segment at `0x813e0000` and the lane stride require index one for the second PDP.
The model therefore reports the cluster index in bits 15:8 and keeps bit 54 clear.

PDP `0x2000509c..0x200050a2` writes doubleword one to offset `0x268` during mailbox setup.
The CPUCP ACEHR inventory at `0x812f6b40` snapshots `0x19898268` as an eight-byte register.
The model retains the written value without a completion effect.
Run565 stopped at this write before the bank received this offset.

PDP `0x813281e0` reads the SMEM target-info pointer at `0x01f00000 + 0xd4000`.
It requires magic `0x49494953` at the referenced record.
The PDP view uses the existing APSS boot-scratch owner at `0x01fd4000`.
Run564 stopped at this read before the view existed.

## Local address space

PDP local `0x40800000..0x40afffff` is the NCC register space of its lane.
The system view is `0x19800000 + (lane << 24)` plus the local offset from `0x40800000`.
Three sources give the same rule:

1. The cluster bank at local `0x40898000` is system `0x19898000` for lane zero.
2. The window registers at local `0x40990100` are system `0x19990100` in the CPUCP inventory.
3. The NCC bank table at `0x2000d1ec` starts with DVFS base `0x408b0000`.

The composition maps this space as one priority-one window to the APSS router.
PDP-private blocks inside the space, for example the NCC timer at `0x408d0000`, have priority zero.
Only existing exact-offset owners answer through the window. Other offsets fail.

The PDP also addresses system space directly below `0x20000000`.
For example, the AUX PLL is at `0x19aa0000`.
A second priority-one window gives identity access to the APSS router for this range.
A third window gives identity access to `0x22000000..0x22ffffff` for the fuse page and the corrected rows.

## IDDQ configuration

Function `0x81324d64` programs IDDQ configuration words for both rails.
It writes `0x17913004..0x1791300c`, `0x17913104..0x1791310c`, and `0x17915000`.
No PDP, CPUCP, or APSS consumer reads these words back.
The composition retains each word from zero without a hardware effect.

The same path writes one doubleword to `0x180c05e0 + (lane << 21)` for each lane.
The shared CPRH owner retains these words.
Both PDP and CPUCP views reach the same state.
PDP `0x81323172..0x813231da` later reads and changes the doubleword at lane offset `0x5e0`.

Function `0x81328f6a` tests bit 8 of corrected word `0x221c24c8`.
The composition declares this word zero, so the VPP bit stays clear.
The meaning of the bit is open.

## Limits manager block

Function `0x81302668` initializes the limits manager blocks after the VPP step.
The block bases come from the table at `0x2000d1bc`, and the record pointer at `0x2001db2c` selects one base.
The same blocks hold the CPUCP thermal sample words.
Run575 fails at `pc=0x813263e6 address=0x181c0380`, so PDP0 programs the block at `0x181c0000`.
The caller reads `+0x880`, `+0x890`, `+0x894`, and `+0xc4` back, then it sets limit enable bit 0 at `+0x2984`.
The platform binds one `qcom,cpuss-llm-v1` owner to each lane and adds an APSS alias for the PDP.
The PDP store at local `0x181c0380` therefore reaches the CPUCP owner.
The device record at `0x2000cbb0..0x2000cbd0` lists the table pointer.
The register contract, the instruction addresses, and the open items are in
[the CPUCP evidence](../cpucp/subsystem_evidence.md#llm-limits-block-and-pdp-configuration).

## DDR execution

The PDP executes code from protected DDR at `0x81320000`.
Run573 stopped with a libqemu assertion in `probe_access` at PC `0x8132a3fe`.
The PDP fetched that 32-bit Thumb instruction through the transaction path.
The instruction crosses a 1 KiB M-profile page boundary at `0x8132a400`.

Both PDP CPUs now join `ddr_low` and `ddr_low_backing` as execution participants.
Each PDP instance receives native RAM views, so its fetches use direct memory.
Writes from other initiators invalidate the translated PDP code.
The M-profile QEMU device is an `armv7m` container.
`PdpCpu` therefore supplies its architectural CPU for code invalidation and transaction hints.
Run575 passes the run573 instruction and the VPP fuse check.

## Watchdog

Device `pdp_wdog@409c0000` uses driver `wdt_bsa`.
Its refresh frame is at `0x409c0000`, and its control frame is at `0x409c1000`.
Function `0x8132ac54` writes WOR at `+0x8` and `+0xc`, writes one at `+0x100`, and then sets WCS bit zero.
The initialized timeout at `0x813d0778` is `0x89544000` counter ticks.
Function `0x8132ad58` reads WCV at `+0x10` and `+0x14`.
The sw_isr_table entry for IRQ 6 holds bark handler `0x8132acd5`.
That handler reads WCS, prints `WDOG BARK!!!`, and raises a kernel panic.

The model implements the BSA WRR, WCS, WOR, and WCV contract.
A WCS write, a WOR write, or a refresh restarts the timeout.
WS0 drives PDP IRQ 6, and a second expiry stops the simulation.
No source names control offset `0x100`, so the model retains it and logs the first write.

## Fuse identity

Function `0x20005744` reads corrected words `0x221c20d0` and `0x221c20d4`.
It combines `0x221c20d0[31:28]` with `0x221c20d4[0]` to form the speed bin.
Function `0x20000848` reads `0x221c20ec[8]` as the MCN field.
It builds configuration prefixes with `ncc%d.v%d.mcn%d.sb%d`.
Function `0x81327a90` registers each prefix in increasing specificity.
The SM8850 composition declares the speed-bin row zero.
The PTE identity word uses the existing fuse-policy owner.

## Receiver command writes

Function `0x2000637c` clears local RX command pairs three and four at `0x40970118..0x40970124`.
It then writes zero to peer channel `lane + 4` and `0x1f` to peer channel `lane + 6`.
The receiver writes change stored command data only.
They do not create or clear pending state.

## Programmable address windows

Function `0x20000730` reads the lane index from `0x408985d0[15:8]`.
Function `0x20005370` reads the window count from `0x40990000[3:0]`.
The CPUCP register inventory declares eight windows at system `0x19990100 + 0x400*n`.
The inventory starts at `0x812f7b90` and repeats at `0x812fa208`.
Each record declares twelve register bytes.
This supports the model's explicit eight-window geometry, not an unmeasured identification value.

The PDP programs local registers at `0x40990100 + 0x400*n` from this table at `0x2001d000`:

| Base | Size | Target | Lane stride |
|---|---|---|---|
| `0x813d0000` | `0x10000` | `0x813d0000` | Enabled |
| `0x81f00000` | `0x40000` | `0x81f00000` | Disabled |
| `0x81f40000` | `0x40000` | `0x81f40000` | Enabled |

The initializer adds `size * lane` to the target when the descriptor enables the lane stride.
It writes the base and mask at offset zero.
It writes target bits 47:16 at offset four.
It writes enable bit 31 and target bits 51:48 at offset eight.
It clears every unused window.

The model translates the guest-programmed windows and preserves the protected DDR owner.
Unmatched addresses retain their identity view.
Register changes invalidate previous direct-memory views.
The model rejects malformed geometry and transfers across translation boundaries.
It does not grant APSS privilege to PDP transactions.

## Mailbox devices

The device records start at `0x2000cb70`, `0x2000cb84`, and `0x2000cb98`.
Their configuration records are at `0x2001d3dc`, `0x2001d3f0`, and `0x2001d404`.
The common ISR table starts at `0x2000ccb0`.
The table contains an argument and a handler for each external IRQ.

| Role | Local RX | Peer TX | RX mask | IRQ |
|---|---|---|---|---|
| Unresolved auxiliary peer | `0x40978000` | `0x17638000` | `3` | 2 |
| APNS SCMI | `0x40970000` | `0x17630000` | `0xff` | 3 |
| CPUCP SCMI | `0x40950000` | `0x18818000` | `0xf` | 1 |

APNS initializer `0x20006004` constructs slot `0x81f08100 + (lane << 7)`.
It registers callback `0x20005e09` on the second device.
CPUCP initializer `0x20005f9c` constructs slot `0x1d83c000 + (lane << 8)`.
It registers callback `0x20005dc9` on the third device.
The auxiliary device has no recovered peer, so an attempted send fails explicitly.

Function `0x2000b928` sends through configuration word one, which names the peer TX aperture.
The RX command pair is at `+0x100 + 8*channel`.
MAP, status, and W1C use `+0x4000`, `+0x4400`, and `+0x4800`, with eight bytes per group.
The global enable word is at `+0x4c00`.
IDR bits 14:8 give the group count, not the channel count.
The existing CPUCP mailbox model supplies this register contract.

The APNS response path at `0x20005e76..0x20005e86` uses the lane as its outgoing channel.
The two PDP devices therefore share the APSS RX owner at `0x17610000` on SPI 34.
The APSS TX apertures are `0x19980000` and `0x1a980000`.
The CPUCP PDP transport retains its existing shared RX owner and two independent TX apertures.
The hardware banks transfer guest-written command pairs only.
They do not construct SCMI headers, payloads, FREE words, or CPU online records.

## CPRH program and configuration storage

Run578 reaches the first PDP store to `0x180c0198`, at PC `0x813242aa`.
Function `0x20004b28` selects lane base `0x180c0000` or `0x182c0000`.
It calls `0x81324298`, `0x81323130`, and `0x81323116`.
The last helper enters `0x81323060`.

The `qcom,pdp-cprh-v1` owner shares each lane between the APSS and CPUCP routers.
It replaces the sparse CPUCP program owners and the separate IDDQ doubleword owners.
Each lane starts with declared zero storage.
Guest uploads supply every program word.

| Lane offset | Access evidence | Storage contract |
| --- | --- | --- |
| `0x198 + 8*i`, `i < 27` | CPUCP type-10 upload descriptors | Retained dwords |
| `0x1b4 + 8*i`, `i < 47` | CPUCP type-11 upload descriptors | Retained dwords |
| `0x198 + 8*i`, `i < 7` | PDP `0x813242a6..0x813242b0` | Same shared dwords |
| `0x1d0 + 8*i`, `i < 12` | PDP `0x813242ba..0x813242c4` | Same shared dwords |
| `0x328`, `0x32c` | PDP `0x813242c8`, `0x813242cc` | Retained dwords |
| `0x508`, `0x50c` | PDP `0x813242d2`, `0x813242da` | Retain `0x1c` and `0x80000000` writes without inferred execution |
| `0x480..0x4c8`, stride 8 | PDP local `0x20004bf4`, `0x20004c72..0x20004d5a` | Retained doublewords |
| `0x5b8..0x5e0`, stride 8 | PDP local `0x20004c96..0x20004d8a` | Retained doublewords |
| `0x5f0..0x6d8`, stride 8 | PDP `0x8132306e..0x81323114` | Thirty table rows |
| `0x6e0`, `0x6e8` | PDP `0x813230ba..0x813230f8` | Two auxiliary rows |
| `0x7c0..0x8b8`, stride 8 | PDP `0x813231de..0x813231fa` | Thirty-two retained rows |
| `0x950..0xa48`, stride 8 | PDP `0x813231f0`, `0x813231f4` | Thirty-two retained rows |
| `0xb50`, `0xb58` | PDP local `0x20004bde` | Retained doublewords |

Run579 reaches the last row at `0x180c0a48`.
The instruction at `0x813231e6` increments the cursor before the second bank access.
Thus, the second bank starts at `0x7c0 + 8 + 0x188 = 0x950`, not `0x948`.
The model preserves this boundary and rejects the preceding and following doublewords.

The CPUCP upload descriptors appear in [the subsystem evidence](../cpucp/subsystem_evidence.md#sequencer-instruction-and-configuration-storage).
The owner rejects holes such as `0x19c`, `0x1a4`, and `0x1ac`.
It checks both halves before an eight-byte transfer changes program storage.
Doubleword configuration accepts either four-byte half without changing the other half.

The dump function reads `0x330`, `0x338`, and `0x340` as busy and program-counter diagnostics.
The model retains uploaded programs without interpreting their internal instructions.
NCC power requests apply programmed profiles through the separate CPRH target owners.
Busy and program-counter diagnostics report idle because functional requests complete synchronously.
Configuration writes alone do not create a voltage transition, interrupt, or completion.
Each power request checks both targets before it changes either effective corner.

## DPM and limiter configuration

The DPM configuration at local `0x2000d250` selects `0x408c0000`.
Its system address is `0x198c0000 + (lane << 24)`.
The model retains these doublewords:

| Offset | Producer | Source |
| --- | --- | --- |
| `0x00` | `0x81326eba` | The second pair from `K_DPM_CFG` |
| `0x30` | `0x81326eb0` | The first pair from `K_DPM_CFG` |
| `0x80` | Local `0x2000b63c` | Guest write `0x10000` |
| `0x18`, `0x20`, `0x28`, `0xa8`, `0xb0`, `0xb8` | `0x81326f10..0x81326f38` | The `K_DPM_CLUSTER_WEIGHTS` record |

Run603 stopped at the first cluster-weight write, `0x18`.

The same configuration lists six per-core bases at `0x2000d268`, from local `0x400b0000` with a stride of `0x100000`.
The private layout names the block `NCC_CORE_DPMPPT` and places it at APSS offset `0x020b0000`.
Its system address is `0x190b0000 + (core << 20) + (lane << 24)`.
A second PDP window maps local `0x40000000..0x407fffff` to system `0x19000000 + (lane << 24)`, with the same rule as the NCC window.
Function `0x81326c40` copies `K_DPM_CORE_WEIGHTS` into 33 doublewords of each present core.
The offsets are `0x10..0x98` and `0x100..0x170`, at stride eight.
The private layout names the `0x10..0x98` weight fields `NCC_DPM_CFG1` and `NCC_DPM_CFG2`.
Lane zero has six core banks and lane one has two.

The recovered producers do not poll or read back these blocks.
The model starts these words at declared zero and applies no physical power-control effect.
Undeclared offsets and widths fail.

The fixture checks the cluster words, lane isolation, the core-bank boundaries, widths, and cold reset.
Run604 passes the QMX, cluster, and core DPM writers.
It stops at a 32-bit read of power word `0x420`, which the CPU-domain evidence describes.

The limiter configuration at link `0x8132d410` selects local `0x40a00000`.
Its system address is `0x19a00000 + (lane << 24)`.
The table at `0x813d0518` declares fourteen windows.
Their offsets are `0x00..0x160` at stride `0x20`, followed by `0x1a0` and `0x1c0`.
Each window holds doublewords at `+0`, `+8`, and `+0x10`.
The gap at `0x180` and each window's `+0x18` remain unsupported.

PDP `0x813270b6`, `0x813270ce`, and `0x813270e6` write the three words.
Readers `0x81326fc4` and `0x81326fd4` inspect the limit words.
The stop path clears bits 2:0 at `0x8132721a`.
Both start and stop paths then set control bit 3 at `0x8132722e`.
The model retains these requests without physical clock limiting or a fabricated completion.

The four groups written by `0x20003da8` are DDR data, not limiter registers.
The pointer at local `0x2001d3d8` contains `0x81f04000`.
The guest therefore publishes those descriptors through the existing DDR owner.

## CPRH target configuration

Run580 stops at PDP PC `0x81323614`, which reads `0x181c8004`.
The device record at `0x2000cbd4` names `CPRH` and points to configuration `0x8132b9ac`.
That configuration selects four 12-byte descriptors at `0x8132bb54`.
Their bases are `0x181c8000`, `0x181d0000`, `0x183c8000`, and `0x183d0000`.
Each descriptor selects one target thread.

The initializer at `0x81323600` selects a descriptor and clears loop-enable bit zero at offset four.
It programs the timer, selection words, thresholds, interrupt enables, and voltage configuration.
The later function at `0x81324244` returns zero without a hardware operation.
The separate helper at `0x81324250` can change loop-enable bit zero.

Function `0x81322958` selects the minimum quotient for each ring oscillator.
It uses target offset `0x840 + thread*0x440 + oscillator*4`.
It writes the oscillator gate count at `0xa0 + oscillator*4`.
Function `0x813226ec` programs voltage rows at `0x3000 + thread*0xa0 + level*4`.
It writes the corresponding period at `0x2008 + level*4`.
The model declares forty rows from the `0xa0` thread stride and sixteen oscillator entries.

The public [CPR driver](https://android.googlesource.com/kernel/msm/+/6ef255005ceaae118d35fba8585de7ba1d4d5efb/drivers/regulator/cpr3-regulator.c)
defines the `0x440` target stride and the voltage-row fields.
Its corner rows start at `0x3a00`, while this PDP uses `0x3000`.
Its control word is at `0x3aa0`, while this PDP uses `0x3a00`.
These differences prevent reuse of an older address map without a separate version.

`qcom,cpuss-cprh-target-v1` retains the declared configuration from virtual zero.
The control socket covers `[0,0x2600)`, and the table socket covers `[0x3000,0x4000)`.
The existing level request owner retains its separate window at `+0x2600`.
APSS aliases and CPUCP bindings reach the same owners.
The level request socket accepts both masters without a second copy of its state.

Loop control can start a functional sample when its inputs are available.
The chain-check word retains configuration without starting a measurement.
Unknown status reads fail.
Configuration writes do not change an applied voltage.
The native consumer checks shared state, controller isolation, row boundaries, measurement requests, and rejected status writes.

Run581 exposed a single-master socket in the level request owner.
After the socket correction, all 87 platform tests and the Lua syntax check pass.
Run582 passes target initialization and reaches the DVFS preparation routine at `0x2000b5e6`.
The PDP log then records another reset-vector load after the routine clears NCC clock overrides.
The run times out with 14623 UART bytes and `kernel_entry=false`.
These bytes are not kernel boot evidence.

### Functional power profiles

Each NCC request selects one of the 32 rows in its shared CPRH lane.
The first CPR field, bits 9:5, selects target zero's corner.
The second CPR field, bits 19:15, selects target one's corner.
The model keeps generic target labels instead of asserting physical rail identities.
NSI fields and array overrides retain their programmed values without replacing these corner indices.

Run587 exposed an incorrect interpretation of the override bits.
The model selected NSI state 30 as a corner, but the firmware programmed only corners 0 through 27.
A native debugger captured state row `0x3779de`, which selects CPR corner 14 for both targets.
The private record is `run-sm8850-common-runtime587/ncc-request-snapshot.json`.

The target requires a programmed corner and an enabled voltage control word.
Its effective step count is `max(initial, floor)`.
Its virtual voltage is `(base + effective_steps) * multiplier` millivolts.
The [Linux CPR driver](https://android.googlesource.com/kernel/msm/+/6ef255005ceaae118d35fba8585de7ba1d4d5efb/drivers/regulator/cpr3-regulator.c)
defines the base, step, and multiplier relation.
The PDP target uses its own corner and control offsets, which the section above records.

Both targets must accept the profile before either target changes.
The model applies the profile synchronously without reconstructing analog regulation or internal sequence execution.
A later table write does not change the applied voltage until another request applies the new profile.
This policy preserves the functional request boundary without generating sensor measurements or interrupts.

### Initialization diagnostics

Function `0x81322c60` logs `Aging Start` and calls the register dump at `0x81320ea0`.
This is an initialization path, not a panic handler.
Runs592 through 594 stopped at successive reads in that dump.
These common register fields are:
The private layout supplies the register names in this table.

| Register | Offset | Functional policy |
| --- | --- | --- |
| `NCC_DVFS_STS2` | `0x788` | No external limit or tracking request is active. Completed software requests leave no active arbitration request. |
| `CPRH_SEQ_BUSY` and rail PCs | `0x330`, `0x338`, `0x340` | Synchronous effects leave no busy sequencer or current instruction. |
| `CPR_IRQ_STATUS_0` | `0x824` | Explicit samples publish DONE and MID after their results. The ideal regulator creates no acknowledgement timeout. |
| `CPRH_STATUS_0` | `0x3a04` | The word reports the applied corner, previous corner, voltage, and loop enable. |
| `CPRH_OSM_TIMER_EXP_IDLE_OVERRIDE` | `0x3c94` | Bits 31:26 retain the override. Timeout flags remain clear. |
| `ACDCR` and `ACDSSCR` | `0x04`, `0x28` | Adaptive clock controls remain disabled. Nonzero writes fail. |

The target status uses corner bits 5:0, voltage bits 17:6, previous-corner bits 23:18, and loop bit 29.
The model encodes its virtual millivolts in the voltage field.
This voltage encoding is a functional policy, not a measured silicon encoding.
Function `0x81324250` changes loop enable through target offset four, bit zero.
A table write or failed request preserves the applied status.

PDP reads the adaptive controls at local `0x409d0004` and `0x409d0028`.
Their system base is `0x199d0000 + (lane << 24)`.
The existing NCC sources supply the effective clock without adaptive distribution.
The platform does not claim an enabled adaptive clock path.

All 88 platform tests and the Lua syntax check pass.
Run594 passes the CPRH diagnostics and reaches the adaptive clock read.
Run595 takes another path and prints `SBL1, End` with 15610 UART bytes.
Its PDP cores remain in reset, so this run does not exercise the adaptive clock reads.
The watchdog ends the run before QBox shutdown produces the timer abort at `0x17815000`.
The timer access does not fail through the model or AccessMonitor.
The CPUCP trace identifies an earlier DMA deadline failure.
See [the deadline evidence](../cpucp/acehr_evidence.md#synchronous-channel-start).
Kernel entry remains unverified.

### Functional aging measurements

Function `0x8132347c` restores target configuration before the aging samples.
Store `0x81323494` writes `0x80000600` to target offset `0x20`.
The private layout names this word `CPR_BIST_CHAIN_CHECK0`.
The firmware sets the high bit, and the write does not start a measurement.
The earlier model incorrectly rejected this configuration.

The recovered request and result cohort is:

| Operation | Firmware | Registers |
| --- | --- | --- |
| Prepare the ordinary sample | `0x81323250` | Gates `0xa0/0xa4`, selection masks `0xe0/0x120`, ownership words `0x200+4*n`, interrupt mask `0x81c`, loop control `0x04` |
| Request the aging sample | `0x813235f0` | Count mode `0x04[7:6] = 3`, then continuation word `0x800` |
| Wait for the result | `0x81322ea0` and `0x81323548` | BUSY at `0x08[0]` |
| Check and consume the result | `0x81323550` and `0x81323456` | PAGE_IS_AGE at `0x08[1]`, quotient pair and selectors at `0x814` |
| Clear and disable | `0x81323208` | Loop control `0x04`, interrupt mask `0x81c`, and write-one-to-clear status at `0x820` |

The four ordinary ownership counts are 10, 6, 10, and 10.
Descriptor byte nine supplies each count.
SM8850 stores the descriptors at PDP file offset `0x1eb54`, linked at `0x8132bb54`.
SM8845 `.305` stores the same counts at PDP file offset `0x1ebd4`.
Thread zero uses the default reset route.

Run599 selects aging sensor 15 with mask `0xffff7fff`.
Its ordinary ownership writes cover only sensors zero through nine.
Requiring an ownership write for sensor 15 incorrectly prevented completion.
The corrected model retains the default thread-zero route without adding an ownership word for that sensor.

The platform declares a 19.2 MHz reference and a 1.92 GHz nominal virtual oscillator.
Manual samples use `(gate + 1) * oscillator_clock / reference_clock`.
The Linux CPR driver uses `gate + 1` when it scales aging quotients.
Zero gates exclude an oscillator from this functional profile.
The oscillator does not drift between ordinary and aging samples.
This policy does not claim to match physical oscillator calibration.

A regulated ideal sensor instead matches its programmed target at the applied voltage.
Both modes require an applied supply, available clocks, selected sensors, and valid oscillator inputs.
Missing inputs cannot create a completed sample.
The model publishes the result tuple before it clears BUSY and publishes DONE and MID.
Count mode selects PAGE_IS_AGE for the completed sample.

Interrupt masks do not clear raw status.
Write-one-to-clear accesses clear only the selected status flags.
MID enable stops automatic continuation until software requests another sample.
Otherwise, the configured reference-clock interval schedules the next sample.
Disabling the loop or its thread cancels that schedule.
This recovered cohort polls status and does not connect an external interrupt route.

All 88 platform tests and the Lua syntax check pass.
The fixture checks clock-derived samples, aging selection without an ownership write, missing inputs, clear semantics, and cancellation.
Run600 consumes samples at `0x81323000` and reaches the aging completion path at `0x81322e62`.
It does not enter the maximum-margin fallback at `0x81322faa`.
The run stops at the per-core DVFS vote bank, local `0x408b1d10`, with 15170 UART bytes.
Kernel entry remains unverified.


## Verification boundary

An isolated strict monitor rejected the M-profile reset reads before libqemu patch 0023.
The reset helper supplied unspecified transaction attributes.
After the patch, the same strict monitor accepts the real vector reads.
The native log reports SP `0x2001f500` and PC `0x200018a5`.
That smoke leaves reset asserted and does not prove firmware execution.
The integrated boot must separately establish guest execution, its peripherals, and its replies.

The shared CPRH and power-control changes pass all 86 platform tests.
Run580 passes the lane program and its two 32-row banks.
Its next unsupported access is the separate target controller at `0x181c8004`, from PDP PC `0x81323614`.
The DPM and limiter fixtures prove their declared register boundaries, not completed native initialization.
Run580 records 14625 UART bytes and `kernel_entry=false`.
These bytes are not kernel boot evidence.

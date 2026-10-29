# Boot clock roots

## Identity and sources

The clock headers define these relative-address IP models:

| Class | Compatible key | Register span |
| --- | --- | --- |
| `qcom_vp::QcomClockRoot` | `qcom,rcg-v1` | `0x14` |
| `qcom_vp::QcomClockDivider` | `qcom,clock-integer-divider-v1` | `4` |
| `qcom_vp::QcomClockBranchEnables` | `qcom,clock-branch-enables-v1` | `0x14` |
| `qcom_vp::QcomClockBranch` | `qcom,clock-branch-v1` | `4` |
| `qcom_vp::QcomSymbolClockMux` | `qcom,symbol-clock-mux-v1` | `4` |
| `qcom_vp::VirtualAlwaysOnClock` | `qcom,virtual-always-on-clock-v1` | No registers |

Each RCG instance models one root. No model contains physical addresses or root-index conditions.
Platform data supplies physical addresses, source selectors, and ROOT_EN policy.
`clock_branch.h` supplies the source-dependent branch model.

The primary evidence is `edl-re:pbl8850.bin sha256=91bd8f81efa7a5940d4b6d4c220388b559ceef39b6b47dd7b4b0727a1beb1d1c`.
ROM addresses equal raw file offsets plus `0x300000`.
Disassembly used Capstone RV64 with compressed instructions enabled.
No proprietary bytes were copied into the repository.

UPDATE is bit 0, ROOT_EN is bit 1, and ROOT_OFF is bit 31.
Source selection uses bits 10:8, HID uses bits 4:0, and mode uses bits 13:12.
The public RCG2 driver defines the same command, selector, and mode bits.
Its five-bit HID width matches the SM8850 GCC roots.
Zero HID bypasses division. Other values divide by `(HID + 1) / 2`, as the public `calc_rate` helper does.
The four-bit integer divider decodes as its register value plus one, as the [public divider helper](https://github.com/torvalds/linux/blob/v6.12/drivers/clk/clk-divider.c) does.
The ROM address table establishes the register identities.

## ROM sequence

At `0x33608a-0x3360ac`, the ROM traverses 32-byte descriptors at `0x336d38`.
Each descriptor includes a register base, configuration table, and function table.
Its configurations have a 28-byte stride.
The first word is an advertised frequency, not a hardware status value.
Bytes 4-7 contain source selector, twice-divider, M, and N.
Bytes 24-25 select the upstream PLL descriptor and PLL configuration for the source setup routine at `0x335ca2`.

| Index | Descriptor | Command address | Configurations | PLL selectors |
| --- | --- | --- | --- | --- |
| 0 | `0x336d38` | `0x17c01004` | `0x3370f8`, `0x337114` | 1 → PLL0 |
| 1 | `0x336d58` | `0x17c03004` | `0x337130` | 1 → PLL0 |
| 2 | `0x336d78` | `0x17c02004` | `0x33714c`, `0x337168` | 2 → PLL4 |
| 3 | `0x336d98` | `0x17c00004` | `0x337184`, `0x3371a0` | 1 → PLL0 |
| 4 | `0x336db8` | `0x1742d4` | `0x3371bc`, `0x3371d8` | 1 → PLL0 |
| 5 | `0x336dd8` | `0x174400` | `0x3371f4`, `0x337210` | 1 → PLL0 |
| 6 | `0x336df8` | `0x174658` | `0x33722c`, `0x337248` | 5 → PLL4 |
| 7 | `0x336e18` | `0x17407c` | `0x337264`, `0x337280` | 5 → PLL4 |
| 8 | `0x336e38` | `0x1484f0` | `0x33729c` | 1 → PLL0 |
| 9 | `0x336e58` | `0x148030` | `0x3372b8`, `0x3372d4` | 5 → PLL4 |
| 10 | `0x336e78` | `0x14402c` | `0x3372f0`, `0x33730c` | 5 → PLL4 |

Selector 0 selects the always-on crystal, with no HID division when HID=0.
Selectors without explicit platform wiring remain unavailable. They do not receive an update acknowledgment.
PLL0 means `QcomGccPll::main_output_hz(0)` at `0x100000`.
PLL4 means `main_output_hz(4)` at `0x104000`.
The index matches the bank's physical offset divided by `0x1000`.
The primary and secondary bank indices default to zero and four.
Platform data can select other indices without changing the root model.
The main, even, calibration, secondary, and crystal selectors must be unique when enabled.
Selector eight disables an input.
The `PllSource` interface supplies rates and change events from either a Taycan bank or a Pongo source.
An unsupported output remains unavailable.

The low-profile row at `0x33714c` advertises 200 MHz with selector 2 and twice-divider 6.
Its upstream descriptor selects PLL4, whose primary configuration advertises 806 MHz.
These advertised rates are inconsistent with a divide-by-three output.
The model computes the configured PLL frequency instead of substituting either advertised rate.
No frequency table value creates a clock source.

The first four descriptors call `0x3359c8` through `0x336c48`.
That wrapper sets ROOT_EN and jumps from `0x3359dc` to the common routine at `0x335b10`.
It is not a separate mux format.

At `0x335b2a-0x335b7a`, the common routine preserves unrelated CFG fields and replaces source, HID, and mode.
It encodes a nonzero twice-divider as value minus one.
For `0 < M < N`, it writes M at CMD+8, `~(N-M)` at CMD+12, and `~N` at CMD+16.
It then selects dual-edge mode 2.
All listed initial configurations use bypass mode with M=N=0.
At `0x335b7e-0x335ba4`, it sets UPDATE and polls for its removal, with a 1000-iteration timeout and microsecond delays.

The descriptor at `0x336e98` targets `0x147110` through `0x337388` and function `0x335bb8`.
That routine writes configuration byte 4 directly. The row at `0x337328` writes 1.
Its 300-MHz advertised rate and PLL0 source identify the integer divide-by-two path.
The divider instance's `output_hz()` uses the configured upstream PLL main output divided by `(value + 1)`.

## Branch controls and boundary

The later ROM dispatcher at `0x3360e0` selects the table at `0x336ef8`.
Calls at `0x334970`, `0x3349c0`, `0x334a08`, and `0x334a3a` select operations 2, 0, 3, and 1.
Operation 2 calls `0x3359e4`.
It sets bit 0 at `0x17c0402c`, then at `0x17c04038` and `0x17c04030` for lane 0.
Lane 1 instead uses `0x17c0403c` and `0x17c04034`.
The model retains these five enable controls and exposes `branch_enabled(index)` in ascending address order.
An enabled control does not assert downstream consumer readiness.

Operations 0, 3, and 1 use `0x335a2c`, `0x335aac`, and `0x335a66`.
They toggle request bits at `0x17c020b4` and poll status bits at `0x17c020a4`.
Those registers are outside this RCG and branch contract.
The separate model in `cpu_domain_power.h` implements them.
See `cpu_domain_power_evidence.md` for the domain prerequisites and reset policy.

The initial generic branch lists at `0x336d10` and `0x336d28` contain null register bases.
The routines at `0x335d44` and `0x335dd0` skip those bases.
No generic GCC branch status bank is necessary for the root initialization loop.

## Implemented state and timing

Each root has retained shadow CFG/M/N/D words and separate requested and active configurations.
An UPDATE snapshots the shadow words.
A valid source must remain available across a virtual one-microsecond acquisition interval before the requested configuration becomes active.
This interval is model policy, not measured silicon timing.
The model starts acquisition on the update write when the source is already available.
For a previously unavailable source, PLL state and lock-deadline events start acquisition when the source becomes available.
A changed or unavailable source restarts acquisition.
The root's event-driven worker observes source changes without a periodic polling loop.
Local writes, acquisition completion, PLL writes, votes, and PLL lock deadlines propagate through `state_changed`.
The branch model also emits `state_changed` after writes and cold reset.

A missing PLL, an unlocked PLL, a disabled PLL output, an unsupported selector, or an invalid M/N configuration cannot clear UPDATE.
The model supports the ROM's eight-bit M/N values and full-word complement encoding.
Other fractional modes remain unavailable.
The model computes the selected PLL main or even output instead of using an advertised rate.

Each RCG instance's `output_hz()` returns zero when the root is disabled or the active source disappears.
A pending update keeps the active configuration until the root acquires the requested source.
`require_root_enable=true` also requires ROOT_EN.
`require_root_enable=false` continuously asserts virtual downstream demand.
This explicit policy replaces unmodeled consumer vote wiring. It does not complete any firmware command.
ROOT_OFF derives from this output state and is read-only.
Cold construction and `cold_reset()` clear all configuration, commands, divider, and branch enables.
A CPU-only reset must not reset these shared clock instances.

The transport accepts aligned four-byte little-endian reads and writes only.
It rejects byte enables, invalid widths, unknown addresses, unsupported command controls, and unsupported divider values.
It does not grant DMI.
Shadow writes do not change the active configuration before UPDATE completion.

## APSS roots and branches

The AArch64 helper at `0x312f84` returns the boot descriptor structure at `0x31d8d0`.
Its root table starts at `0x31d4d0` and contains 26 descriptors with a 32-byte stride.
Function `0x312f90` implements the same RCG update sequence as the CPUCP helper.
The board shares common roots between APSS and CPUCP.

| Additional command address | Descriptor | Configuration table | PLL selector |
| --- | --- | --- | --- |
| `0x18206c` | `0x31d4d0` | `0x31dab0` | 1 → PLL0 |
| `0x1741a8` | `0x31d510` | `0x31db20` | 1 → PLL0 |
| `0x17452c` | `0x31d530` | `0x31db58` | 1 → PLL0 |
| `0x176030` | `0x31d5f0` | `0x31dc8c` | 1 → PLL0 |
| `0x176160` | `0x31d610` | `0x31dcc4` | 5 → PLL4 |

These configurations also select the crystal through selector zero.
The integer-divider descriptor at `0x31d5d0` names the existing register at `0x147110`.
Descriptors with null register addresses do not create devices.

Four further APSS roots use GPLL0 even through selector 6:

| Command address | Descriptor | Rate table | Enabled PLL selectors |
| --- | --- | --- | --- |
| `0x14b00c` | `0x31d630` | `0x31dcfc` | 1 → PLL0 main, 6 → PLL0 even |
| `0x177034` | `0x31d650` | `0x31dda4` | 1 → PLL0 main, 5 → PLL4 main, 6 → PLL0 even |
| `0x1770a4` | `0x31d670` | `0x31de4c` | 1 → PLL0 main, 5 → PLL4 main, 6 → PLL0 even |
| `0x139034` | `0x31d690` | `0x31ded8` | 1 → PLL0 main, 6 → PLL0 even |

Each rate row has a 28-byte stride, with u32 Hz followed by u8 mux, twice-divider, M, and N.
The SDCC rows select twice-dividers 8, 5, and 4 for 75, 120, and 150 MHz from GPLL0 even.
The UFS rows select twice-dividers 24, 8, and 6 for 25, 75, and 100 MHz.
The USB row selects twice-divider 9 and advertises 66666667 Hz.
With the programmed 600-MHz PLL and even divide-by-two, the integer model computes 66666666 Hz.
It does not replace this result with the rounded table label.
The pinned [SM8850 GCC driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c)
independently identifies GPLL0 even as a postdivider at USER_CTL bits 13:10, with encoding 1 for divide-by-two.
See `gcc_pll_evidence.md` for the independent output gates and ROM divider programming.


The branch descriptors at `0x31d478` and `0x31d490` name `0x174074` and `0x174070`.
Function `0x313420` sets bit zero to enable a branch.
Function `0x3134ac` clears that bit to disable it.
Function `0x3131a0` requires status bits 31:28 to clear before it reports an active clock.

The published [branch definitions](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/clk-branch.h) identify enable bit zero and CLK_OFF bit 31.
The model preserves enable requests and derives CLK_OFF from actual upstream frequency.
Writes cannot override CLK_OFF.
The board connects both boot branches to the neighboring root at `0x17407c` as explicit virtual wiring.
This connection does not claim a recovered physical clock topology.
The model rejects unsupported controls instead of returning an unconditional active status.

## Platform binding

`platforms/qcom/sm88xx/reset.lua` contains the physical mappings and source selectors.
Each shared router converts physical addresses to the target's relative offsets.
The processor forwarding ranges expose only the modeled registers.
No model contains a root-index condition or a broad fallback register bank.

`pll_path` resolves an absolute SystemC path or a path relative to the model's parent.
Elaboration fails if the named object has the wrong type or does not exist.
Selectors must be unique values in 0..7. Value 8 disables a source.
The branch requires an existing `ClockSource` through `source_path`.
The CPU domain's separate `root_path` binding does not change.
`ClockSource` contains only virtual availability and event methods. It contains no state.
RCGs, branches, symbol muxes, and the explicit virtual reference implement this contract.
Clock-change events propagate source availability without periodic polling.

## UFS topology and virtual policy

The ROM list at `0x31e0b8` contains nine 24-byte branch descriptors.
The entry at `0x31e190` has a null register base and terminates the list.
The register addresses are ROM evidence. The names and parent topology below use the pinned Linux driver.
No proprietary configuration table or firmware bytes are included here.

| Branch | ROM register | Selected upstream source |
| --- | --- | --- |
| Aggregate AXI | `0x1770f0` | AXI RCG `0x177034` |
| AHB | `0x177028` | Explicit virtual always-on reference |
| AXI | `0x177018` | AXI RCG `0x177034` |
| ICE | `0x17707c` | ICE RCG `0x17708c` |
| PHY auxiliary | `0x1770bc` | Auxiliary RCG `0x1770c0` |
| RX symbol 0 | `0x177030` | Mux `0x177068` |
| RX symbol 1 | `0x1770d8` | Mux `0x1770ec` |
| TX symbol 0 | `0x17702c` | Mux `0x177058` |
| UniPro | `0x17706c` | UniPro RCG `0x1770a4` |

The three symbol descriptors set flags to 2 at descriptor offset 20.
The ROM skips halt polling for those branches. That behavior does not prove PHY readiness.
The power list at `0x31e1a8` belongs to the separate power model.

UEFI ClockDxe `Clock_ConfigClock` sets and clears CBCR bits 12 through 14 at image offsets `0x10b9c..0x10bfc`.
These are `FORCE_MEM_PERIPH_OFF`, `FORCE_MEM_PERIPH_ON`, and `FORCE_MEM_CORE_ON`.
The UFS driver writes them for the ICE branch `0x17707c`. Run663 stopped at that write.
Each UFS branch now retains bits 12 through 14. They control SRAM retention only and have no modeled effect.

The pinned [gcc-canoe.c](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c)
defines ICE with parent map 3: XO=0, PLL0 main=1, PLL4 main=5, and PLL0 even=6.
It defines auxiliary with XO-only parent map 4.
The board appends ICE and auxiliary as roots 21 and 22. It retains all twenty existing root indices.
The AXI and UniPro roots remain indices 18 and 19.

The same driver defines symbol muxes with shift 0 and width 2.
Parent maps 9, 10, and 11 encode the respective PHY source as 0 and XO as 2.
The model retains those controls and rejects selectors 1 and 3, high bits, and unknown addresses.
The board binds all three PHY inputs to the real `ufs` object's `ClockSource`.
It does not substitute an always-on source for the PHY.

The board explicitly chooses selector 0 at symbol-mux reset.
For RCG reset, zero CFG selects XO and continuous virtual downstream demand supplies the enable policy.
Neither choice claims a recovered silicon power-on value.
The AHB clock has no declared Linux parent.
Its separately named virtual always-on reference is board policy, not an inferred silicon connection.
The mux crystal reference also uses an explicitly declared virtual always-on source.

The driver declares hardware control bit 1 for aggregate AXI, AHB, AXI, ICE, auxiliary, and UniPro.
Those board branches support retained enable bit 0 and hardware-control bit 1.
Symbol branches and existing boot branches reject bit 1.
The `continuous_demand` policy asserts virtual consumer demand while hardware control is set.
Either a software enable or demanded hardware control still requires a running upstream source.
The default demand policy is false. Writes cannot override CLK_OFF bit 31.
Source changes propagate through events, even without another register access.
Polling a stopped branch cannot enable its source or change its controls.

### Source identity limits

The local reference DT is `qcom-rehost:inputs/reference/linux-sm8850/arch/arm64/boot/dts/qcom/kaanapali.dtsi sha256=61bc5f222bc638753d0a91f7fd41d322cc901915a197e43c51f28402b0d8049e`.
It identifies `qcom,kaanapali-gcc` at `0x100000` and supplies three UFS PHY clock inputs.
It lists the aggregate, AXI, AHB, UniPro, and symbol clock roles.
The local reference clock directory contains no Kaanapali GCC implementation.
The DT does not establish register selectors, PLL type, or the internal AHB parent.
Therefore Canoe supplies an explicit topology analogue, not proof that Kaanapali uses an identical GCC implementation.
ROM register addresses and observed fields remain authoritative where available.
The Canoe probe also writes ICE bit 14 (`FORCE_MEM_CORE_ON`). This model does not implement that separate control.

### Guest fixture coverage

The existing PLL fixture now connects its live PLL-backed root to a symbol mux and branch.
Its guest checks software enable, hardware-control demand, source loss, retained controls, crystal selection, and disable.
It repeats reads after source loss to ensure that polling cannot restore readiness.
The fixture uses the root as a controllable source. It does not simulate UFS PHY readiness.
The extended fixture passed during `make test` with the other eight actual guest regressions.

## Execution evidence

The integrated CPUCP ROM passes all eleven original roots and its domain initialization.
It performs real APSS reset assertion and release.
The integrated APSS ROM also passes early roots, branch controls, UFS clock setup, and PHY initialization.
It reaches UFS link negotiation.
These observations establish firmware progress, not a kernel boot.

## OEM XBL boot branch operation

The private helper `build/private/xbl-boot-clocks.lua` extends an existing reset platform.
It returns a function that accepts the platform table.
It preserves existing module names, source bindings, and UFS dependencies.
It rejects an overlapping target or processor forwarding range.
New registers use the existing shared clock router and both processor forwarding paths.

The input image is `xbl_sc.elf` from package `CPH2749export_11_16.0.5.700EX01_2026022701130186`.
Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
Its ELF entry is `0x14827fa8`.
The private JSON files contain decoded register facts, not firmware bytes.

Disassembly establishes these operations:

| Instruction range | Table pointer | Count address | Register operation |
| --- | --- | --- | --- |
| `0x148605d8..0x148605f8` | `0x148f2f80` → `0x148f2d28` | `0x148f2fae` | Read, clear bit 0, write seven words |
| `0x14860614..0x14860634` | `0x148f2f88` → `0x148f2d44` | `0x148f2faf` | Read, set bit 0, write 121 words |
| `0x14860654..0x1486067c` | `0x148f2f90` → `0x148f2f28` | `0x148f2fb0` | Read, OR the mask, write four vote words |

The operation has no completion poll.
The helper also covers all 59 addresses in the HW_CTL list at `0x148f12d8`.
The loop at `0x1484f330` sets bit 1.
These branches support software enable and explicit virtual consumer demand under HW_CTL.
The helper enables this capability on existing branches without replacing their source bindings.

The scalar operation facts also establish policy fields at 18 branch addresses.
`retained_mask` permits only those declared fields.
Thirteen branches retain bit 20, and six retain bit 21.
The branch at `0x147018` retains both fields.
These fields do not change the enable request, source state, or CLK_OFF calculation.
Cold reset clears them with the local branch controls.
The scalar store PCs remain in `build/private/xbl_scalar_clock_operations.json`.

### Identity limits and virtual source policy

The seven clear operations do not prove BCR deassertion.
The clear operations do not establish the current register identities.
The helper therefore uses `QcomRetainedBootControl` for the seven unmodeled controls.
Its compatible key is `qcom,retained-boot-control-v1`.
This profile declares writable mask one and reset value one for each of those controls.
The model requires an explicit writable mask. The reset value must fit that mask.
It supplies no power status, reset completion, or peripheral implementation.
Other fields and offsets fail.
Replace this control with a real reset or power model when current evidence establishes a modeled consumer.

The pinned Canoe Linux driver establishes branch identities at `0x110068`, `0x110050`, and `0x171010`.
It declares no parent for those three branches.
The recovered target ClockDxe facts in `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/clocks.py` provide additional source relations.
That file pins ClockDxe SHA-256 `68c0e48234dd390aea34abb4685d11b10e9ef0800bd2745a5029105f9a0dc360`.
Its `ROOT_BRANCHES` table connects:

| Branch | Root | Existing platform object |
| --- | --- | --- |
| `0x110050` | `0x174400` | `cpucp_clock_root_6` |
| `0x171004` | `0x174400` | `cpucp_clock_root_6` |
| `0x171010` | `0x14402c` | `cpucp_clock_root_11` |

These branches require the existing live RCG source.
The same recovered table connects `0x110068` to the unmodeled root at `0x1823f0`.
All other new branches lack an established, modeled upstream source.
The helper binds these branches to the separately named `xbl_boot_clock_reference`.
This always-on reference is an explicit functional substitute, not a claim about physical topology.
It does not assert peripheral readiness or firmware completion.
Existing ROM and UFS branches retain their live sources.

### Independent votes

`clock_votes.h` defines `QcomClockVoteBank`, with compatible key `qcom,clock-vote-bank-v1`.
It exposes four retained words at relative offsets 0, 8, 16, and 24.
The platform maps these words at `0x151000`, `0x151008`, `0x151010`, and `0x151018`.
The supported masks are `0x00bc317b`, `0xe03c0200`, `0x01f8280f`, and `0x03c00f70`.
The masks come from the four real XBL OR operations.
Unsupported bits and the three intervening words fail.
Cold reset clears all four words.

The Linux driver uses processor vote words at `0x152000` and later offsets.
The helper declares this virtual RPM wiring explicitly:

| Mapped branch | Vote word | Mask | Evidence |
| --- | --- | --- | --- |
| `0x110068` | `0x151000` | `0x1000` | Linux branch bit 12 |
| `0x110050` | `0x151000` | `0x100000` | Linux branch bit 20 |
| `0x144024` | `0x151010` | `0x400000` | Unverified field name and address, virtual wiring policy |
| `0x182020` | `0x151008` | `0x200` | Unverified field name and address, virtual wiring policy |

The last two connections remain conditional topology substitutes.
No permitted source establishes which clock owns bit 22 of `0x151010` or bit nine of `0x151008`.
Other supported vote bits retain requests for unmodeled consumers.
They do not expose detached completion or success status.
There is no inferred connection from these votes to UFS.

A branch runs when software enable, an independent vote, or demanded HW_CTL requests it.
Every request still requires a running upstream source.
CBCR reads retain local controls and derive CLK_OFF from that result.
A vote does not rewrite local controls.
Removing software enable cannot stop a branch that still has an independent vote.
Source loss stops the branch even when a vote remains.
The branch model has four fixed vote slots from `vote_path_0` through `vote_mask_3`.
Any configured slot can keep the branch running.
Vote changes from each distinct bank propagate through the branch event worker without polling.
Elaboration rejects an absent vote bank, an unsupported field, or a field without a bank.
It also rejects a slot gap and a duplicate owner.
A duplicate owner uses the same bank and offset with an overlapping mask.

### Integration and verification

`src/platform.cc` registers these class and compatible-key pairs:

- `qcom_vp::QcomClockVoteBank` → `qcom,clock-vote-bank-v1`
- `qcom_vp::QcomRetainedBootControl` → `qcom,retained-boot-control-v1`

The branch class keeps its existing compatible key.
After the private AOSS root additions, call:

```lua
assert(loadfile('build/private/xbl-boot-clocks.lua'))()(platform)
```

The helper also retains 56 DCD controls and 36 separate scalar policy words.
The DCD table starts at XBL `0x148f11f4`.
Scalar stores occupy `0x1484ef88..0x1484f3a0`.
These controls do not simulate physical memory collapse, clamp timing, or dynamic division.
The existing UFS reference gate at `0x01fd5054` retains its live consumer connection.

The interconnect interpreter at `0x1484de38` executes 118 masked configuration writes from table `0x14920bc8`.
The count at `0x149216d8` is 118. Every entry selects a direct 32-bit write without a status wait.
The private helper retains the declared fields. It does not simulate physical interconnect timing or arbitration.
The actual trace reaches both later HW_CTL and DCD loops, then returns from clock initialization at `0x1484f3b4`.

The real XBL executed all three boot lists and all REFGEN readiness loops.
The guest regression checks independent vote words, vote removal, and source loss with an active vote.
`cargo test --locked -- --include-ignored --test-threads=1` passed all 50 tests after integration.
These observations do not establish kernel execution.

## OEM XBL QUP peripheral group

`build/private/xbl-peripheral-clocks.lua` returns `function(platform)`.
It adds 23 RCGs, their 23 branches, and three independent vote words.
It does not activate itself or replace an existing mapping.
It rejects overlapping ranges in `cpucp_clock_router`, `apss_router`, and `cpucp_router`.
APSS and CPUCP routes forward to the shared clock router.
The existing `gcc_plls` object supplies every physical PLL output.

### Firmware coordinates

The selected image is `CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES/xbl_sc.elf`.
Its SHA256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
The ELF load segment maps file `0xc2000` to virtual `0x148eb000`.
The following coordinates come from that segment, not an older SoC register map.

Descriptors `0x148f1ee8..0x148f21a8` have a 32-byte stride.
They share the six-row rate table at `0x148f1e40` (file `0xc8e40`).
Each descriptor contains its table pointer at offset 0 and its physical command address at offset `0x10`.
The row count is the byte at offset `0x19`.
The faulting descriptor is `0x148f1fa8` (file `0xc8fa8`).
Its address field is `0x148f1fb8` (file `0xc8fb8`) and contains `0x118790`.

The rate API calls `0x14860510` from `0x14860d50` or `0x14860dc4`.
It selects a rate through `0x14860104`, then transfers through `0x14860550` to `0x14860244`.
Instruction `0x148602b0` calls `0x148603cc`.
That function loads the descriptor address at `0x148603ec`, then reads the command word at `0x148603f4`.
The same function programs CFG, M, N, D, and UPDATE at `0x14860400..0x1486047c`.
It polls UPDATE at `0x14860480` and `0x14860504`.

The branch descriptors occupy `0x148f26a8..0x148f2968`, with a 32-byte stride.
Each row contains a name pointer, root pointer, flags, CBCR address, vote address, and vote mask.
The UART name pointer at `0x148f2768` selects string `0x148dc640`, `gcc_qupv3_wrap1_s7_clk`.
Its root pointer is `0x148f1fa8`.
Its CBCR is `0x118780`, with vote `0x152010` bit 16.

### Complete bounded topology

The descriptor and branch rows follow the same order in this table.
All command windows have size `0x14`. All CBCR windows have size 4.

| Root descriptor | Command address | CBCR | Vote address | Vote mask |
| --- | --- | --- | --- | --- |
| `0x148f1ee8` | `0x118014` | `0x118004` | `0x152008` | `0x00400000` |
| `0x148f1f08` | `0x118150` | `0x118140` | `0x152008` | `0x00800000` |
| `0x148f1f28` | `0x1182a0` | `0x118290` | `0x152008` | `0x02000000` |
| `0x148f1f48` | `0x1183dc` | `0x1183cc` | `0x152008` | `0x04000000` |
| `0x148f1f68` | `0x118518` | `0x118508` | `0x152008` | `0x08000000` |
| `0x148f1f88` | `0x118654` | `0x118644` | `0x152008` | `0x10000000` |
| `0x148f1fa8` | `0x118790` | `0x118780` | `0x152010` | `0x00010000` |
| `0x148f1fc8` | `0x11e014` | `0x11e004` | `0x152010` | `0x00000010` |
| `0x148f1fe8` | `0x11e150` | `0x11e140` | `0x152010` | `0x00000020` |
| `0x148f2008` | `0x11e28c` | `0x11e27c` | `0x152010` | `0x00000040` |
| `0x148f2028` | `0x11e3c8` | `0x11e3b8` | `0x152010` | `0x00000080` |
| `0x148f2048` | `0x11e504` | `0x11e4f4` | `0x152010` | `0x00000100` |
| `0x148f2068` | `0x1a8014` | `0x1a8004` | `0x152018` | `0x00001000` |
| `0x148f2088` | `0x1a8150` | `0x1a8140` | `0x152018` | `0x00002000` |
| `0x148f20a8` | `0x1a828c` | `0x1a827c` | `0x152018` | `0x00004000` |
| `0x148f20c8` | `0x1a83c8` | `0x1a83b8` | `0x152018` | `0x00008000` |
| `0x148f20e8` | `0x1a8504` | `0x1a84f4` | `0x152018` | `0x00010000` |
| `0x148f2108` | `0x1a8640` | `0x1a8630` | `0x152018` | `0x00020000` |
| `0x148f2128` | `0x1a9014` | `0x1a9004` | `0x152018` | `0x04000000` |
| `0x148f2148` | `0x1a9150` | `0x1a9140` | `0x152018` | `0x08000000` |
| `0x148f2168` | `0x1a928c` | `0x1a927c` | `0x152018` | `0x10000000` |
| `0x148f2188` | `0x1a93c8` | `0x1a93b8` | `0x152018` | `0x20000000` |
| `0x148f21a8` | `0x1a9504` | `0x1a94f4` | `0x152018` | `0x40000000` |

The vote bank starts at `0x152008`, with relative word offsets 0, 8, and 16.
Its supported masks are `0x1ec00000`, `0x000101f0`, and `0x7c03f000`.
The mapping ends before the existing PLL vote word at `0x152020`.
Branches use the existing source-dependent enable and vote logic.
A vote cannot keep a branch running after its RCG loses the selected source.
These rows do not require HW_CTL or retained CBCR policy fields.

### Sources and arithmetic

The pinned [SM8850 GCC driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c)
defines `gcc_parent_map_0` at lines 210–214.
It assigns selector 0 to XO, selector 1 to GPLL0 main, and selector 6 to GPLL0 even.
The serial roots at lines 821–1520 use this map, `mnd_width=16`, and `hid_width=5`.
Specifically, `gcc_qupv3_wrap1_s7_clk_src` starts at line 1011 and sets `cmd_rcgr=0x18790`.
Add the GCC physical base `0x100000` to obtain `0x118790`.
The first wrapper uses engines 0, 1, 3, 4, 5, 6, and 7.
The other groups use wrapper 2 engines 0–4, wrapper 3 engines 0–5, and wrapper 4 engines 0–4.
The neighboring QSPI descriptor uses a different rate table. This helper does not claim to model that descriptor.

Each XBL rate row has a 28-byte stride.
Its u32 rate starts at offset 0.
Offset 8 contains the selector byte, twice-divider byte, u16 M, and u16 N.
The helper uses the existing GPLL0 bank index 0 for both main and even outputs.
The even output remains dependent on the existing PLL gate and postdivider state.

| Rate row | Declared Hz | Selector | Twice-divider | M | N |
| --- | --- | --- | --- | --- | --- |
| `0x148f1e40` | 7372800 | 6 | 2 | 384 | 15625 |
| `0x148f1e5c` | 14745600 | 6 | 2 | 768 | 15625 |
| `0x148f1e78` | 19200000 | 0 | 2 | 0 | 0 |
| `0x148f1e94` | 48000000 | 6 | 2 | 4 | 25 |
| `0x148f1eb0` | 66666667 | 1 | 18 | 0 | 0 |
| `0x148f1ecc` | 100000000 | 1 | 12 | 0 | 0 |

The first two rows exceed the old eight-bit M/N limit.
`QcomClockRoot::mnd_width` now accepts 8 or 16 and defaults to 8 for existing roots.
The helper explicitly selects 16 for this group.
Arithmetic masks M, inverted N, and inverted D to that width.
It reconstructs the denominator as `(~N_register & mask) + M`.
It rejects zero M, a denominator not greater than M, overflow beyond the width, or a mismatched D.

Instructions `0x14860434..0x14860470` write full-word M, `~(N-M)`, and a clamped inverted duty value.
All fractional rows in this table have `2*(N-M) >= N`, so that duty value is N.
The existing `~D == N` check therefore remains valid for this bounded group.
Other duty encodings remain unsupported.
The 600-MHz main output and 300-MHz even output produce the declared fractional rates exactly.
The integer 66.666667-MHz row computes 66666666 Hz, as with the existing ROM integer-divider convention.

### State and integration contract

The model retains shadow, requested, and active configurations.
UPDATE captures the shadow words and makes the output unavailable during acquisition.
It clears only after the selected physical source supplies a valid rate.
Source loss during acquisition cancels acquisition until that source returns.
Source loss after acquisition makes the output unavailable and propagates to the branches.
No readiness word bypasses those dependencies.

The helper uses `require_root_enable=false`, as the reset platform does for other peripheral roots.
This explicit virtual downstream demand replaces absent consumer-to-root vote wiring.
It does not enable a PLL, forge UPDATE completion, or assert a branch vote.
CBCRs still require software enable or their independent vote.
Unsupported selectors, modes, duty encodings, vote bits, and register offsets retain the existing fail-closed behavior.
The helper does not model serial engines, QSPI, pads, or DDR.

After the reset platform and the existing boot-clock helper, activate this helper once:

```lua
assert(loadfile('build/private/xbl-peripheral-clocks.lua'))()(platform)
```

No new compatible registration is required.
The integrated private profile retains the existing shared mappings.
`make build` completed with the 16-bit model.
The actual OEM run programmed the UART root and transmitted its XBL version, boot interface, and secure-boot policy.
The existing model regression executable passed all 20 tests.
These observations do not establish Linux execution.

## Additional main sources

The selected CPH2771 root at `0x14402c` uses six independent GPLL banks.
Its nine rate records require more than the primary and secondary selectors.
The [SM8845 composition evidence](../sm8845/tme_runtime_abi/evidence.md) records the complete source table.

`main_sources` adds semicolon-separated `selector index` records to an RCG.
Each selector must be unique across the additional records and the enabled scalar selectors.
Each index must address an existing PLL bank.
The model resolves the table once during elaboration and retains eight fixed slots.
Each selected source uses its actual main output and lock event.
No additional record overrides a scalar selector or changes the rate formula.

The PLL guest regression selects banks eleven and four through separate additional selectors.
It checks pending updates, source-specific availability, and output loss after vote removal.
The updated guest fails against the previous executable because that executable does not implement these mappings.
The corrected executable passed all 32 host tests and 21 native guest tests.

## SM8850 training rates at root 0x14402c

XBL uses nine 28-byte rate rows at `0x148f1be8`.
Each row contains the frequency at offset zero, selector at offset eight, and source ID at offset 16.
The source table at `0x148f2a98` resolves each source ID to its PLL descriptor.

| Frequency in Hz | Selector | Encoded divider | Source |
| ---: | ---: | ---: | --- |
| 19200000 | 0 | 2 | XO |
| 282000000 | 1 | 2 | GPLL10 |
| 350000000 | 2 | 2 | GPLL6 |
| 533000000 | 4 | 4 | GPLL1 |
| 605600000 | 7 | 4 | GPLL2 |
| 806000000 | 5 | 2 | GPLL4 |
| 933000000 | 3 | 2 | GPLL5 |
| 1066000000 | 4 | 2 | GPLL1 |
| 1211200000 | 7 | 2 | GPLL2 |

Run98 stopped at update poll `0x148604ec` with command address `0x14402c` and selected row `0x148f1cc8`.
The existing topology lacked selector seven and four other sources.
The platform now connects all six main outputs through the existing root model.
The guest still programs each PLL and divider.
Run99 passes the update and executes five calibration rounds across all four MC banks.

## Post-DDR power domains

### Native inventories

The SM8850 image is CPH2749 `16.0.5.700EX01`, member `IMAGES/xbl_sc.elf`.
Its SHA256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
The SM8845 image is CPH2771 `16.0.1.305EX01`, with the same member name.
Its SHA256 is `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
Analysis used pyelftools PT_LOAD translation and Capstone AArch64 disassembly.

Each native descriptor has a 32-byte stride.
Offset zero holds the name pointer.
Offset eight holds the 32-bit GDSCR address.
Offset twelve holds an optional auxiliary address.
Offset `0x18` holds the software reference count.
The remaining initial fields are zero.

SM8850 stores the table pointer at `0x148f2f68` and the count, seven, at `0x148f2fb2`.
SM8845 stores the table pointer at `0x148f0988` and the count, nine, at `0x148f09d2`.
The faulting descriptors are `0x148f2b70` and `0x148f0578`.
Their ELF file offsets are `0xc9b70` and `0xc9578`.
Both select `0x17b068`.

| Native name | SM8850 descriptor | SM8845 descriptor | GDSCR | Auxiliary word |
| --- | --- | --- | --- | --- |
| `cam_cc_titan_top_gdsc` | Absent | `0x148f0538` | `0x0958e334` | None |
| `gcc_gpu_smmu_vote_gds` | `0x148f2b30` | `0x148f0558` | `0x17b03c` | None |
| `gcc_mm_rscc_gdsc` | `0x148f2b50` | Absent | `0x180004` | None |
| `gcc_mmu_tcu_vote_gds` | `0x148f2b70` | `0x148f0578` | `0x17b068` | None |
| `gcc_ufs_mem_phy_gdsc` | `0x148f2b90` | `0x148f0598` | `0x19e000` | None |
| `gcc_ufs_phy_gdsc` | `0x148f2bb0` | `0x148f05b8` | `0x177004` | None |
| `gcc_usb30_prim_gdsc` | `0x148f2bd0` | `0x148f05d8` | `0x139004` | None |
| `gpu_cc_cx_gdsc` | Absent | `0x148f05f8` | `0x03d99080` | `0x03d99094` |
| `gpu_cc_gpu_smmu_vote_gds` | `0x148f2bf0` | `0x148f0618` | `0x03d95004` | None |
| `gx_clkctl_gx_gdsc` | Absent | `0x148f0638` | `0x03d68024` | None |

### Transition evidence and model policy

SM8850 enables a domain at `0x14860898`.
SM8845 uses the identical instruction sequence at `0x1485ee10`.
Both increment the reference count and change hardware only on its zero-to-one transition.
They load the GDSCR address from descriptor offset eight.
They clear collapse bit zero, then poll power-on bit 31 with one-microsecond delays.
The bounded loop permits 150 delayed reads.
The native routine returns zero even if the poll expires.
This return value does not prove that hardware powered on.

The corresponding disable routines are `0x1486091c` and `0x1485ee94`.
They decrement a nonzero reference count and set collapse bit zero when the count reaches zero.
These paths do not access the auxiliary word.
The descriptor tables do not identify clock sources or parent power domains.
No physical dependency follows from adjacency or the domain names alone.

The SMMU caller supplies two additional branch requests, separate from the GDS requests.
SM8850 stores its power map at `0x14926150` and its clock map at `0x14926120`.
SM8845 stores those maps at `0x14920a20` and `0x149209f0`.
Each map contains two 16-byte rows and an all-ones base terminator.
Base `0x15000000` selects `gcc_mmu_tcu_vote_gds` and `gcc_mmu_tcu_vote_clk`.
Base `0x03da0000` selects `gcc_gpu_smmu_vote_gds` and `gcc_gpu_smmu_vote_clk`.
SM8850 routines `0x14862998` and `0x14862a54` consume the power and clock maps respectively.

| Branch name | SM8850 descriptor | SM8845 descriptor | CBCR | RCG pointer |
| --- | --- | --- | --- | --- |
| `gcc_gpu_smmu_vote_clk` | `0x148f2568` | `0x148f0078` | `0x17b000` | Zero |
| `gcc_mmu_tcu_vote_clk` | `0x148f2588` | `0x148f0098` | `0x17b02c` | Zero |

Both branch descriptors contain flags `0x20` and zero independent vote fields.
The helper maps both CBCRs through the existing branch model.
Their upstream source is the existing explicit `xbl_boot_clock_reference`.
This is a functional reference policy, not recovered physical topology.
The firmware independently requests each branch after the power request.
No evidence establishes that either branch is a prerequisite for the GDSCR power-on bit.

`qcom,gdsc-v1` is an IP key, not a UFS-specific key.
Its existing collapse request and power-on status match these native operations.
The model now retains collapse control separately from its powered state.
Bit 31 remains read-only when firmware rewrites the value that it read.
Unsupported fields still fail.
The existing CFG_GDSCR behavior remains available to existing eight-byte consumers.
New native mappings expose only their evidenced four-byte GDSCR.

The new `virtual_supply` option declares an available functional supply for an isolated domain latch.
It defaults to false and cannot coexist with a bound peripheral consumer.
A supported enable request changes the isolated latch to powered.
A disable request removes that power state.
This option does not assert that an absent GPU, camera, MMU, or clock is ready.
Unbound domains without this explicit supply still reject power-on requests.
UFS and USB retain their existing consumer connections and their supply behavior.

The platform helper adds the three common missing GDSCRs and the three SM8845-only GDSCRs.
It preserves shared APSS and CPUCP aliases through `cpucp_clock_router`.
SM8850 retains the existing MM RSCC object name and its aliases.
That object now uses GDSC behavior instead of retained-memory behavior.
The early boot list still clears its collapse bit.

The auxiliary control at `0x03d99094` remains unresolved and unmapped.
The recovered enable and disable paths do not establish its fields or transitions.
Physical parent supplies, clock dependencies, and independent hardware voters also remain unresolved.
The isolated functional policy does not substitute ready values for these missing interfaces.

### Integration

Both runtime compositions now invoke the helper after the existing USB owner is present.
For a manually composed platform, use exactly one applicable command:

```lua
dofile('platforms/qcom/sm88xx/xbl-power-domains.lua')(platform)
-- SM8845 instead:
dofile('platforms/qcom/sm88xx/xbl-power-domains.lua')(platform, {0x0958e334,0x03d99080,0x03d68024})
```

The existing UFS, USB, clock routers, and boot-clock composition must already exist.
Do not invoke these commands again after a runtime composition.

### Verification

`make build` succeeds.
`cargo test --locked -- --include-ignored` passes all 91 tests.
`cargo clippy --locked --all-targets -- -D warnings` succeeds.
The clock regression enables an isolated domain, then collapses it while the write value still contains the power-on status.
It checks that the written status bit cannot preserve power or change completion state.
Existing UFS regressions still exercise actual consumer transitions and their memory effects.

SM8850 run113 and SM8845 run99 execute the domain and branch requests.
Both then read GPU SMMU ID0 at `0x03da0020`, which has no model yet.
Their APSS PCs are `0x1486253c` and `0x14860b4c`.
Their retained UART logs still end after `boot_smem_debug_init`.
These runs do not establish completed SMMU initialization or kernel execution.

## QUP serial-engine DFS revision

The post-QUPFW callback requires `qcom,rcg-dfs-v1`.
`QcomDfsClockRoot` extends the existing root with eight performance levels.
The ordinary `qcom,rcg-v1` register span and behavior remain unchanged.
The model has no SoC condition.
Both platforms retain their selected address lists, branch votes, and PLL connections.
The peripheral helper maps the DFS root span through offset `0xf8`, inclusive.
It rejects the unused holes rather than supplying retained words for them.

### Original image evidence

The source images are `IMAGES/xbl_sc.elf` in these OEM packages:

- SM8850: `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`.
- SM8845: `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`.

Addresses below are ELF virtual addresses.
The SM8850 instruction and descriptor bytes also match the selected original package.
No firmware image or binary table is part of this change.

| Operation | SM8850 | SM8845 |
| --- | --- | --- |
| DFS initialization dispatcher | `0x14860fd8` | `0x1485f550` |
| Performance configuration writer | `0x148614c0` | `0x1485fa38` |
| DFS enable writer | `0x14861564` | `0x1485fadc` |
| Clock enable dispatcher | `0x14860a98` | `0x1485f010` |

The failing SM8850 instruction at `0x148614e8` reads root base plus `0x1c`, plus four times the descriptor level.
For the selected `wrap1_s5` root, the base is `0x118518`.
Level zero therefore reads `0x118534`.
The descriptor is `0x148f1f68`.
Its rate table is `0x148f1e40`, with six rows and eight supported levels.
The dispatcher passes each row's mux descriptor at row offset eight.
Mux bytes encode selector, twice-divider, u16 M, u16 N, and level at byte six.
A level of `0xff` excludes that row from DFS programming.

The six SM8850 rows start at `0x148f1e40`, with stride `0x1c`.
Their levels are `0xff`, `0xff`, zero, two, three, and five.
Thus native initialization programs XO at level zero, not the first fractional rate.
The previous section records the exact source and divider values for all six rows.
The selected root descriptors from `0x148f1ee8` through `0x148f21a8` all specify eight levels.
The existing helper's entire SM8850 serial-engine group therefore uses this revision.

SM8845 uses rate table `0x148ef9a0` for its serial roots.
Its `0x11e150` root instead uses `0x148efb48`, through descriptor `0x148efbf0`.
Both selected tables contain the same six rates, mux values, dividers, fractions, and level assignments.
The first eight root descriptors occupy `0x148efa48` through `0x148efb28`.
The remaining descriptors occupy `0x148efbf0` through `0x148efd50`.
Each selected descriptor specifies eight levels.
This establishes the DFS contract for the existing twenty-entry SM8845 group.
It does not import SM8850-only wrapper roots.

### Selected firmware consumers

The firmware callback resolves only each `se_clock` name.
It does not resolve separate wrapper, AHB, core, or AXI clock names.
Its native rate call uses operation eight, which initializes the DFS table.
The callback enables the branch before programming the engine.
Protocols two and `0x0d` omit the rate call.
The callback disables the branch after programming, except for protocol eight.
The read-only firmware investigation establishes these callback facts.

| SoC | Selected clock | Branch descriptor | Root descriptor | CMD | CBCR |
| --- | --- | --- | --- | --- | --- |
| SM8850 | `wrap1_s5` | `0x148f2728` | `0x148f1f68` | `0x118518` | `0x118508` |
| SM8850 | `wrap1_s6` | `0x148f2748` | `0x148f1f88` | `0x118654` | `0x118644` |
| SM8850 | `wrap2_s2` | `0x148f27c8` | `0x148f2008` | `0x11e28c` | `0x11e27c` |
| SM8850 | `wrap1_s3` | `0x148f26e8` | `0x148f1f28` | `0x1182a0` | `0x118290` |
| SM8850 | `wrap3_s0` | `0x148f2828` | `0x148f2068` | `0x1a8014` | `0x1a8004` |
| SM8845 | `wrap1_s3` | `0x148f0158` | `0x148efa88` | `0x1182a0` | `0x118290` |
| SM8845 | `wrap1_s5` | `0x148f0198` | `0x148efac8` | `0x118518` | `0x118508` |
| SM8845 | `wrap1_s6` | `0x148f01b8` | `0x148efae8` | `0x118654` | `0x118644` |
| SM8845 | `wrap3_s4` | `0x148f0378` | `0x148efd50` | `0x1a8504` | `0x1a84f4` |

The names have prefix `gcc_qupv3_` and suffix `_clk`.
The existing source-dependent branch and vote objects remain the consumer clock interfaces.
No additional physical mux, divider, or gate register is established by these callbacks.
The performance CFG contains the source mux and HID divider.
The branch table contains the consumer gate and its vote.

### Register layout and functional policy

| Offset | Meaning |
| --- | --- |
| `0x00..0x10` | Existing CMD and shadow CFG/M/N/D |
| `0x14` | DFS control and current-level status |
| `0x1c + 4*level` | Performance CFG |
| `0x5c + 4*level` | Performance M |
| `0x9c + 4*level` | Performance inverted N |
| `0xdc + 4*level` | Performance inverted D |

The native configuration writer establishes all four table banks.
It writes M, `~(N-M)`, and `~N` for fractional rows.
The native enable writer stores `0x21` at offset `0x14`.
This enables DFS and hardware clock control.
The selected source remains XO at level zero until a consumer requests another level.
No table contains a source rate independently of its connected PLL.

The public [Linux RCG2 driver](https://github.com/torvalds/linux/blob/v6.12/drivers/clk/qcom/clk-rcg2.c)
defines eight performance levels and the same CFG/M/N bank offsets.
Its DFS rate calculation reads current level from bits `4:1`.
It calculates the selected source rate through HID and M/N fields.
Linux does not establish the D bank here. The original firmware writer establishes that bank.
The public [SM8850 GCC driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c)
establishes the serial roots, mux selectors, five-bit HID, and sixteen-bit M/N fields.
These are protocol references, not copied GPL implementations.

The DFS control word at `0x14` keeps the current level at bits `4:1`, and bit zero is the DFS enable.
The public RCG2 driver reads the same two fields.
The selected firmware also writes `0x21`, so it sets bit five and no other bit.
The model treats bits `14:11` as the software performance field.
Bit ten is an override request, bit nine is update status, and bit fifteen is an unimplemented software-control mode.
Those four field meanings come from a private layout, and no firmware write in the selected images sets them.
The model retains the software performance and override fields.
It derives the current level and update status.
Software override selects the software level.
The software-control mode at bit fifteen remains unsupported because its behavior is not established here.
Writes that select that mode receive a command error.
Otherwise, the consumer `request_level()` method selects the hardware level.
The upload callback does not require a nonzero hardware request.

The model uses the existing one-microsecond acquisition policy for each requested configuration.
This timing and the pending indication at bit nine are functional model policies.
They are not measured silicon timing.
An unprogrammed CFG, an unsupported level, an invalid fraction, or an unavailable source cannot complete acquisition.
The model publishes the current level only after acquisition.
It retains the previous current level while another request remains pending.
Source loss stops the selected clock and propagates through the existing `ClockSource` event.
Disabling DFS returns to the conventional shadow configuration through acquisition.
Cold reset clears all performance banks and requests.
The model does not invent DFS FSM states.

### Coverage and native acceptance probe

The PLL fixture now includes a separate DFS root.
Its new transitions cover unprogrammed enable, programming after enable, fractional level seven, source loss, and unsupported level eight.
They also cover current-level publication, full inverted-D retention, and return to conventional root updates.
The existing non-DFS fixture roots remain unchanged.
The integrated suite passed all 92 tests.
Clippy passed with warnings denied.

SM8850 run125 passes the former DFS read at `0x118534` and reaches the original GSI loader.
Its next unsupported access is GSI control `0x00a04000`, read from PC `0x148a0570`.
The UART records the original `QUPFW` load of 52,715 bytes.
This establishes progress through the selected DFS setup, not completion of QUP initialization.
The retained evidence is `build/private/run-sm8850-common-runtime125/`.
SM8845 still fails at its earlier PIMEM protection allocation.

For SM8850, retain the original authenticated boot inputs and UART log.
Observe the native call at `0x1486105c` and the read at `0x148614e8`.
Check the requested root base and mux level before each access.
Require table writes to finish before the `0x21` store at `0x14861578`.
For `wrap1_s5`, require successful reads and writes at `0x118534` and DFS control `0x11852c`.
Then require native branch enable and the firmware consumer's actual programming effects.
Require the original callback to return its own status.
Do not replace that status or suppress a later native error.
For SM8845, use the selected descriptor rows above and DFS writer `0x1485fa38`.
The probe must retain any next unmodeled address and the native PC.
Passing the first read alone does not establish QUP firmware completion or kernel boot.

## QSEE clock and power dependency group

The selected SM8850 initializer is `0x15751b9c`, and its release function is `0x15751f30`.
The corresponding SM8845 functions are `0x157448c0` and `0x15744c28`.
`qsee-clock-dependencies.lua` reuses existing owners and adds only missing controls.
Both runtime profiles apply it after the XBL clocks, power domains, DDR controls, and DCP owner.

Both initializers set bit zero at `0x00185000`, then wait for bit 31 to become one.
The model treats bit zero as the enable request and bit 31 as its status.
Neither the register name nor the field names are established, and the exact hardware identity at this address remains unresolved.
The branch model selects this polarity explicitly, without changing ordinary CBCR `CLK_OFF` behavior.
Its status still depends on the enable request and upstream source.

The GPU CX and GX domains occupy `0x03d99080..0x03d99087` and `0x03d68024..0x03d6802b`.
Each existing GDSC owner also owns its adjacent CFG word.
The [Qualcomm GDSC driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gdsc.c) identifies power-up completion bit 16 and power-down completion bit 15.
The model derives these bits from accepted power transitions.
It retains GDSCR bit eight without granting a power transition.
The explicit virtual supply does not create a GPU or establish physical parent supplies.

The existing DCP owner supplies the same causal completion bits at `0x09bab004`.
Its collapse request remains at `0x09bab000`.
Guest writes cannot forge either completion bit.
Ordinary retained fields remain separate from those status bits.

| Shared direct controls | Functional policy |
| --- | --- |
| `0x00132004` | Retained mask `0x8fd0000f` when no existing owner exists |
| `0x03d990d4`, `0x03d990e8` | Existing or new ordinary clock branches |
| `0x03d95000` | New ordinary clock branch for QSEE `gpucc_gpu_smmu_clk` |
| `0x03d94000`, `0x03d94004`, `0x09bb2000` | Retained mask `0x804` |
| `0x01ff7000`, `0x01fd3300` | Retained bit zero |
| `0x00147174`, `0x00147014` | Existing owner or retained mask `0x8fd0000e` |
| `0x0014d000` | Retained mask `0x8fd00004` |
| `0x0011828c` | Existing divider owner or retained four-bit divider |
| `0x0014b004`, `0x0014b008` | Existing DDR controls promoted to causal branches by the descriptor setup below |

SM8850 also clears `0x06e1f100` and configures `0x00172008/0x0017200c`.
SM8845 instead has an additional direct control at `0x00147178`.
These differences remain platform data, not SoC checks inside models.
The QSEE enable helper is SM8850 `0x157517f4` and SM8845 `0x15744518`.
It enables the `gpucc_gpu_smmu_clk` descriptor at SM8850 `0x157f41c0` and SM8845 `0x157e7808`.
The descriptor has no RCG parent. The branch uses the virtual boot-clock reference, as the XBL SMMU branches do.
The following section records the initializer-called descriptor inventory.

The native fixture passed for both OEM compositions.
It checked clock polarity, shared APSS/CPUCP state, both GDSC transition directions, and DCP status.
The actual OEM path remains the acceptance gate for conditional consumers.

### Run184: complete initializer-called descriptor groups

Run184 stops at APSS PC `0x572f51800`, on a four-byte read of `0x00174044`.
The source instruction is `ldr w10,[x8]` at selected-image VA `0x15751800`.
The QSEE relocation adds `0x55d800000` to this source VA.
The producer is the initializer at `0x15751b9c`, not an APPSBL entry point.
The corresponding SM8845 initializer is `0x157448c0`.
These initializers call descriptor groups 2, 5, 6, 7, 8, and 9.
Conditional calls retain their original firmware conditions.

The source files are `RADIO/tz.img` from the selected CPH2749 and CPH2771 packages.
Their SHA256 values are:

- SM8850: `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932`.
- SM8845: `b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09`.

All offsets below use ELF `PT_LOAD` mapping.
SM8850 executable VA `0x15669000` maps to file offset `0x34000`.
Its descriptor VA `0x157d7000` maps to file offset `0x1a2000`.
SM8845 executable VA `0x15666000` maps to file offset `0x34000`.
Its descriptor VA `0x157ce000` maps to file offset `0x19c000`.
Physical load addresses differ from these link VAs.

| Source operation | SM8850 VA / file offset | SM8845 VA / file offset |
| --- | --- | --- |
| Initializer | `15751b9c / 11cb9c` | `157448c0 / 1128c0` |
| Descriptor executor | `15750eac / 11beac` | `15743bd0 / 111bd0` |
| Branch enable | `157517e0 / 11c7e0` | `15744504 / 112504` |
| Branch release | `1575181c / 11c81c` | `15744540 / 112540` |
| Branch poll | `157518e0 / 11c8e0` | `15744604 / 112604` |
| Domain enable | `157519cc / 11c9cc` | `157446f0 / 1126f0` |
| Domain release | `157519f0 / 11c9f0` | `15744714 / 112714` |
| Domain poll | `15751a10 / 11ca10` | `15744734 / 112734` |

Each group row occupies `0x88` bytes.
The first three pointers select late branches, domains, and prerequisite branches.
Branch entries occupy 24 bytes and domain entries occupy 32 bytes.
A zero first pointer terminates each list.
Branch fields contain the status address, optional vote address, vote mask, and firmware selection fields.

| Group | SM8850 row VA / file offset | SM8845 row VA / file offset |
| --- | --- | --- |
| 2 | `157f7ec8 / 1c2ec8` | `157eb730 / 1b9730` |
| 5 | `157f8060 / 1c3060` | `157eb8c8 / 1b98c8` |
| 6 | `157f80e8 / 1c30e8` | `157eb950 / 1b9950` |
| 7 | `157f8170 / 1c3170` | `157eb9d8 / 1b99d8` |
| 8 | `157f81f8 / 1c31f8` | `157eba60 / 1b9a60` |
| 9 | `157f8280 / 1c3280` | `157ebae8 / 1b9ae8` |

Group 2 starts its prerequisite list at SM8850 VA `0x157f76a0`, file offset `0x1c26a0`.
The corresponding SM8845 list starts at VA `0x157eaf18`, file offset `0x1b8f18`.
Its first entry supplies `0x174044`.
Groups 7 and 8 also reference that same physical branch.
They do not receive separate state.

The common group 2 direct gates are `0x14b004`, `0x14b008`, `0x114014`, `0x147030`, and `0x17707c`.
SM8850 also contains `0x116014` and `0x115008`.
Its domain list contains `0x16b004`, `0x177004`, `0x139004`, and `0x110004`.
Existing UFS and USB domain consumers remain connected.

| Group 2 status address | Vote word / mask |
| --- | --- |
| `14101c`, `141004`, `110050` | `153038 / 8`, `153038 / 20`, `153038 / 100000` |
| `123148`, `123144`, `110058` | `153040 / 80000`, `153040 / 200000`, `153040 / 40` |
| `1232a0`, `123298`, `12329c` | `153048 / 1`, `153048 / 4`, `153048 / 2` |
| `1233f8`, `1233f0`, `1233f4` | `153050 / 400`, `153050 / 100`, `153050 / 200` |
| SM8850 `123550`, `123548`, `12354c` | `153050 / 1000000`, `153050 / 400000`, `153050 / 800000` |
| SM8845 `1aa134`, `1aa15c`, `1aa158` | `153050 / 80`, `153060 / 4`, `153060 / 2` |

Group 5 starts at SM8850 VA `0x157f77f0` and SM8845 VA `0x157eb078`.
It directly enables `0x0788105c`, `0x07881054`, `0x0788107c`, and `0x07881074`.
Its shared vote word is `0x078a0000`.
The status-address/mask pairs are:

- `0788301c/400`, `0789a01c/400000`, `07883030/800`, `0789a030/800000`.
- `07881050/2`, `0788104c/1`, `07881070/2000`, `0788106c/1000`.

Group 6 supplies prerequisite gate `0x171004`.
Its domain pointer selects zero-initialized storage at SM8850 VA `0x1580c428` or SM8845 VA `0x157ff9b0`.
Group 8 similarly selects zero-initialized storage at `0x1580c448` or `0x157ff9d0`.
These selected-image lists have zero first entries.
That file state does not establish runtime absence. Their possible producers remain a separate evidence question.

Group 7 supplies prerequisite gates `0x126004`, `0x12603c`, and `0x174044`.
Its late gates are `0x126014`, `0x126028`, `0x0958e37c`, `0x0958e348`, and `0x0958e36c`.
The remaining gates are `0x0958e2bc`, `0x0958e16c`, `0x0958d020`, `0x0958e19c`, `0x0958e15c`, `0x0958e198`, and `0x0958e188`.
Its domain is `0x0958e334`.

Group 8 supplies prerequisite gates `0x174044` and `0x127004`.
Its late gates are `0x12701c`, `0x09baa0c4`, `0x09bb4040`, and `0x09bae004`.
Group 9 supplies prerequisite gate `0x147000`.
Its late gates are `0x06bd5018`, `0x06bd502c`, `0x06e1100c`, `0x06e11300`, and `0x06e21038`.
The remaining gates are `0x06be7000`, `0x06bd1000`, and `0x07f06000`.
Its domains are `0x06e11090` and `0x06e20090`.

The branch producer reads the vote word when the descriptor supplies one.
It saves whether the request was previously absent, then writes `old | mask`.
Without a vote word, it uses the status/control address and mask one.
Release clears only that request mask and clears the saved ownership flag.
The branch poll skips software waiting when hardware control bit one is set.
Otherwise, it requires bit 31 to clear, with the original 100-iteration timeout.
The model derives this bit from a direct gate or independent vote and a running source.

The domain producer saves collapse bit zero, then writes `old & ~1`.
Its poll requires power bit 31 to become one, with the original 10000-iteration timeout.
Release writes `old | 1`.
The existing GDSC model performs the connected power operation before it changes completion state.
Explicit virtual supplies apply only where no peripheral consumer exists.

The executor retains existing mux and divider owners.
For example, gate `0x110050` retains its existing `cpucp_clock_root_6` source.
The selected direct branch descriptors do not contain mux selections or divider values.
Group 2 also delegates firmware clock IDs `8`, `b7`, `ad`, and `19`.
SM8850 adds ID `209`.
The unchanged firmware resolves these requests through its clock driver.
Its resource votes include `mmcx.lvl` and `mxc.lvl`.
Group 6 uses `gfx.lvl`, and SM8850 also uses `gmxc.lvl`.
These calls remain separate from the direct MMIO contract.
No fabricated mux setting, frequency, resource response, or authentication result replaces them.

The [public Canoe GCC driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c) identifies offset `0x6b004` as the PCIe domain.
The selected firmware establishes its physical access at `0x16b004`.
The public driver does not identify the failing `0x174044` branch.
Other names remain unresolved where no matching source establishes them.
New branches use the existing explicit virtual reference, not an inferred physical parent.

The helper promotes three existing retained branch controls at `0x126014`, `0x126028`, and `0x12701c`.
It retains their `0x200000` policy bit separately from gate bit zero.
It also promotes the existing DDR gate controls and the `0x110004` domain control.
The fault initializer's `0x153038 |= 0x100000` operation now updates the real QSEE vote word.
All existing router aliases continue to reach the same owner.

Branch `0x110050` already has an independent XBL vote bank.
QUP1 S0 at `0x118004` has three real owners: XBL, QSEE word `0x153040`, and the DCP bank.
The four indexed slots preserve each request word without merging their storage.
Any configured bank can request the branch.
A stopped source still stops the branch, and guest writes cannot forge its status.

Integration requires the clock helper after `qsee-fault-monitors.lua`.
`platforms/qcom/sm88xx/clock-branch-votes.lua` appends each owner to the next free slot.
It rejects an overlapping owner and a fifth owner with the branch address.
The existing factory exposes `vote_path_0` through `vote_mask_3` as CCI parameters.
Unknown addresses, unsupported fields, and original firmware failures remain visible.
This change does not alter image authentication or claim APPSBL execution.
The integrated test run passed 47 host cases and 57 of 58 native cases.
The initial QSEE fixture exposed duplicate DCP aperture ownership.
After the helper preserved the DCP owner, the focused fixture passed for both selected SoCs.
It also passed after the GPU policy fields gained independent retention checks.



### GPU policy retention and shared DCP ownership

SM8850 run186 and SM8845 run146 reach the same policy write at `0x03d990d4`.
The source instructions are `0x15751d30` and `0x15744a54`.
Each read-modify-write preserves mask `0x89c05005` and sets bit 13.
The earlier enable writes preserve `0x89c07004` at that branch and `0x89c00004` at `0x03d990e8`.
Both then set bit zero.

The branch owners retain policy masks `0x09c07004` and `0x09c00004`, respectively.
Bit 31 remains derived status. Bit zero remains the actual enable request.
Policy fields alone cannot enable a clock or override an absent source.

The DCP owner already contains `0x09bb4040` and `0x09bae004`.
The helper reuses that owner instead of creating overlapping branch modules.
Its two status bits require the existing DCP power and root state under an explicit virtual-parent policy.
The independent vote regression uses three distinct banks.
Each bank can retain a request after the other two release it.
A remaining vote cannot override upstream source loss.

SM8850 run187 passes both previously rejected GPU writes.
It then waits at source `0x15751db4` for `0x09bab004.bit15` and reaches the unchanged watchdog deadline.
SM8845 run147 also reports a watchdog bite.
These runs do not establish DCP idle completion or kernel entry.

### QSEE GPLL vote

After the DCP display domain collapses, QSEE votes two GCC PLLs through one register.
The vote helper is SM8845 `0x15744578`. It sets the record mask at the control address and then polls the status word.
The default status mask is `0x40000000`, which is PLL_MODE bit 30.

| Record | SM8850 | SM8845 | Status | Control | Mask |
| --- | --- | --- | --- | --- | --- |
| `gcc_gpll0` | `0x157f6458` | `0x157e9860` | `0x00100000` | `0x00153058` | `0x1` |
| `gcc_gpll7` | `0x157f68d0` | `0x157e9d10` | `0x00107000` | `0x00153058` | `0x80` |

Each mask bit selects the PLL bank with the same index, as in the ROM and APSS vote words.
`qsee-clock-dependencies.lua` maps APSS and CPUCP `0x153058` to the shared PLL owner's fourth vote slot.
That slot is independent storage. The owner combines all master votes, so a release by one master keeps the other votes.
The poll uses the existing lock status, so it completes only when the PLL actually runs.

SM8850 run220 and SM8845 run176 pass this vote and the DCP display-domain collapse.
Both then read `0x01dc0020`, which no model owns. These runs do not establish kernel entry.

### QSEE QUP and I2C common clocks

Run396 stops on the first QUP common clock vote.
The APSS PC is `0x7c6f51814`.
The runtime delta is `0x7b1800000`, so the selected-image address is `0x15751814`.
This instruction is `str w9,[x8]` in the branch enable at `0x157517e0`.
It writes GCC vote word `0x00153040`.
The model rejected the write because bank `0x153040` did not declare the requested bit.
The QUP1 array starts with `gcc_qupv3_wrap1_core_2x_clk`.
Its mask is `0x40000`, so the first write requests that field.

The caller chain is `0x15716048..0x15716078`, then `0x1571661c`.
That function enables the clock array for one QUP instance.
The executor is `0x15750b24`.
It enables each parent, votes the branch, starts the optional domain, and polls status.
The SM8845 counterparts are QUP init `0x1570ab78`, called from `0x1570e558`.
Its branch enable is `0x15744504`, and the vote store is `0x15744538`.

The clock arrays come from device `QUPV3_HW_PROP` in the selected `IMAGES/tz_qti_config.mbn`.
The SM8850 image has SHA256 `46f6b44e0474bb0adf87a8085a87e896ab97d065637259b1f683ee519172bfb6`.
The SM8845 image has SHA256 `b0da9952ca94f0dde273baa4112c334dcfa27e9ffb76393ac5129657cbec9a58`.
The SM8850 segment loads at `0xd80eb000`.
Its property directory is at `0xd80eca48`.
Its device table is at `0xd80ee260`.
The SM8845 segment loads at `0xd81eb000`.
Its property layout is identical.
The QUP entries are `qupv3_common_hw` and `qupv3_<n>_clocks_arr`.

The SM8850 property lists four QUP instances: `QUP_1C`, `QUP_2C`, `QUP_3C`, and `QUP_4C`.
The SM8845 property lists three: `QUP_1C`, `QUP_2C`, and `QUP_3C`.
The SM8850 property also declares `has_qup_i2c_master_hub = 1` and `i2chub_clocks_arr`.
The SM8845 property has neither field.
The selected `IMAGES/devcfg.mbn` image supplies the permission properties.
Its `/dev/buses/qupac` OEM node has 21 `qupv3_perms` entries and 2 `i2chub_perms` entries.
The same image also carries a larger core node with 45 `qupv3_perms` entries.
Both nodes declare `qupv3_shared_se_list`.
The firmware scan and the SE list builder therefore receive non-empty permission sets.

The initializer reads the clock arrays in this order:

| Order | SM8850 | SM8845 |
| --- | --- | --- |
| 1 | QUP1 four clocks | QUP1 four clocks |
| 2 | QUP2 four clocks | QUP2 four clocks |
| 3 | QUP3 four clocks | QUP3 four clocks |
| 4 | QUP4 four clocks | none |
| 5 | i2chub two clocks | none |

Each row below is one descriptor record.
The record contains the status address at `+0x0`, the vote word at `+0x8`, and the mask at `+0xc`.
The name string pointer is at `+0x18`.

The common loops run before the firmware scan at `0x82461000`.
The i2chub loop runs after that scan and after the SE phase.
The cleanup releases the same fields after the load attempt.

| Clock | SM8850 record | SM8845 record | Status | Vote | Mask |
| --- | --- | --- | --- | --- | --- |
| `gcc_qupv3_wrap1_core_2x_clk` | `0x157f4ac0` | `0x157e8008` | `0x12315c` | `0x153040` | `0x40000` |
| `gcc_qupv3_wrap1_core_clk` | `0x157f4b00` | `0x157e8048` | `0x123148` | `0x153040` | `0x80000` |
| `gcc_qupv3_wrap_1_m_axi_clk` | `0x157f4200` | `0x157e7848` | `0x123140` | `0x153040` | `0x100000` |
| `gcc_qupv3_wrap_1_s_ahb_clk` | `0x157f4400` | `0x157e7a08` | `0x123144` | `0x153040` | `0x200000` |
| `gcc_qupv3_wrap2_core_2x_clk` | `0x157f4d80` | `0x157e82c8` | `0x1232b4` | `0x153048` | `0x8` |
| `gcc_qupv3_wrap2_core_clk` | `0x157f4dc0` | `0x157e8308` | `0x1232a0` | `0x153048` | `0x1` |
| `gcc_qupv3_wrap_2_m_ahb_clk` | `0x157f4240` | `0x157e7888` | `0x123298` | `0x153048` | `0x4` |
| `gcc_qupv3_wrap_2_s_ahb_clk` | `0x157f4440` | `0x157e7a48` | `0x12329c` | `0x153048` | `0x2` |
| `gcc_qupv3_wrap3_core_2x_clk` | `0x157f4f40` | `0x157e8548` | `0x12340c` | `0x153050` | `0x800` |
| `gcc_qupv3_wrap3_core_clk` | `0x157f4f80` | `0x157e8588` | `0x1233f8` | `0x153050` | `0x400` |
| `gcc_qupv3_wrap_3_m_ahb_clk` | `0x157f4280` | `0x157e78c8` | `0x1233f0` | `0x153050` | `0x100` |
| `gcc_qupv3_wrap_3_s_ahb_clk` | `0x157f4500` | `0x157e7b08` | `0x1233f4` | `0x153050` | `0x200` |
| `gcc_qupv3_wrap4_core_2x_clk` | `0x157f51c0` | absent | `0x123564` | `0x153050` | `0x2000000` |
| `gcc_qupv3_wrap4_core_clk` | `0x157f5200` | absent | `0x123550` | `0x153050` | `0x1000000` |
| `gcc_qupv3_wrap_4_m_ahb_clk` | `0x157f42c0` | absent | `0x123548` | `0x153050` | `0x400000` |
| `gcc_qupv3_wrap_4_s_ahb_clk` | `0x157f4540` | absent | `0x12354c` | `0x153050` | `0x800000` |
| `gcc_qupv3_i2c_core_clk` | `0x157f4940` | not reached | `0x123004` | `0x153040` | `0x100` |
| `gcc_qupv3_i2c_s_ahb_clk` | `0x157f43c0` | not reached | `0x123000` | `0x153040` | `0x80` |

The selected SM8850 image contains the QUP4 and i2chub records.
The SM8845 image has i2c clock records, but its config declares no QUP4 or i2chub array.
This path therefore reaches neither on SM8845.

`qsee-clock-dependencies.lua` declares the common votes and every enabled engine vote on the existing `0x153040`, `0x153048`, and `0x153050` banks.
Run400 first requests `0x153040` mask `0x400000`, the QUP1 S0 clock.
The declared bank words now include that mask and the remaining enabled engine masks.
Each new status address becomes a vote-only branch with the explicit virtual boot-clock reference, as the existing QUP wrap branches do.
The descriptor `+0x28` field points to a clock configuration record, for example SM8850 `0x157f5bb8`.
The executor calls the parent path before the vote.
Run396 shows no unmodeled access in that parent path.
The model therefore keeps the explicit virtual reference and invents no physical parent.
The votes are independent storage, so a release by one master cannot clear another master's request.

Run396 shows the first rejected mask.
The first enabled clock is `gcc_qupv3_wrap1_core_2x_clk`.
Its mask `0x40000` was absent from the bank.
The remaining masks are reached in the array order above.

SM8850 run397 accepts all common votes.
It then reads `0x1ac0004`, the QUP4 common version word.
The QSEE consistency loop at `0x157160bc` compares that word across all common entries.
SM8845 `0x1570b010` performs the same comparison.
Wrappers 1 through 3 already had owners. Wrapper 4 had none.
The selected SM8850 `QUPV3_HW_PROP` entry `QUP_4C` supplies base `0x1ac0000`.
`platforms/qcom/sm88xx/qsee-qup-common.lua` now composes `qup_common_4` at that base with the existing `qcom,geni-common-v1` model.
The model's declared revision is the measured `0x40030000`, so the equality check and the firmware version comparison both pass.
The value comes from the phone kernel record `20261026T003332Z-phone-qup-hw-version`.
It does not claim any other register value.
The SM8845 property has no `QUP_4C` entry.
Its `QUP_2C` entry supplies base `0x8c0000`.
The SM8845 XBL composition owns wrappers 1 and 3 only.
`qsee-qup-common.lua` adds `qup_common_2` at that base with the same model, so the SM8845 comparison also holds.

SM8850 run398 passes the QUP common-version check and the prologue writes for wrappers 1 through 4.
It then reads `0x9c0120`, the i2chub common block at `0x9c0000`.
The prologue at `0x15715364` sets bit zero at `+0x118`, `+0x120`, and `+0x21c` on every common entry.
The selected SM8850 `i2chub_common_hw` entry `I2CHUB_0C` supplies base `0x9c0000`.
`qsee-qup-common.lua` now composes `i2chub_common` at that base with the same `qcom,geni-common-v1` model.
The model owns the three controls and retains their values.
The block receives no SE list from the common prologue itself.
The selected OEM node declares two i2chub permission entries, so the list builder has an i2chub set as well.
SM8845 has no i2chub common block and no equivalent access.

The SE clocks are part of this path.
`FUN_15716eb8` builds the SE assignment list at `DAT_15804300`.
It requires `qupv3_perms`, `qupv3_shared_se_list`, and, for the hub, `i2chub_perms`.
The selected devcfg image declares all three.
Run400 proves that the list is populated: it reaches the SE configuration block and enables the first engine clock.
The OEM node has 21 permission entries and the core node has 45.
The enabled SM8850 set has seventeen engines; the SM8845 set has fourteen.
The difference in counts is why the model uses the enabled entries, not every listed identifier.
The declared enabled set comes from the OEM `/dev/buses/qupac` node.
The same image repeats a larger 45-entry core node.
Run400's first request matches both nodes.
A later run that reaches a core-only engine would test which node QSEE reads; the model currently uses the OEM set.

After the common votes and the version check, the next selected-image read is the QUP firmware scan.
`FUN_15715c24` calls `FUN_15719a40` and `FUN_15719a84`.
`FUN_15719a84` reads the embedded firmware program headers from `0x157cb990`.
That table follows the embedded ELF header at `0x157cb95c`.
`FUN_15719b04` then returns the segment physical address.
The first candidate is segment one at `0x82461000`.
The scan reads that word to compare it with magic `0x20495351` or `0x57464553`.
The platform maps `0x82461000` inside the DDR window, so the read itself is not an unmodeled access.
The native XBL staged the selected `qupv3fw` image there.
The measured SM8850 QUP common revision is `0x40030000`, from the phone kernel log record `20261026T003332Z-phone-qup-hw-version`.
The QSI record at `0x82461000` also declares version `0x40030000` at `+8`.
The comparison at `0x15716408` therefore accepts the record.
Run400 selects the firmware, reaches the SE configuration block, and then fails before any GSI access.

The selected devcfg `/dev/buses/qupac` permission entries enable seventeen SM8850 engines and fourteen SM8845 engines.
Each enabled entry selects a `gcc_qupv3_wrap*_s*_clk` descriptor with a CBCR address, a GCC vote word, and a vote mask.
The SM8850 descriptors are `0x157f4bd8..0x157f5318`; the SM8845 descriptors are `0x157e8120..0x157e8720`.
Run400 first requests mask `0x400000` in bank `0x153040`, the QUP1 S0 clock at CBCR `0x118004`.
The previous bank mask did not contain that bit, so the write failed.
`platforms/qcom/sm88xx/qsee-clock-dependencies.lua` now declares every enabled engine vote for both SoCs.
The two hub permission entries select the `gcc_qupv3_i2c_s0_clk` and `gcc_qupv3_i2c_s1_clk` descriptors at `0x157f4998` and `0x157f49d8`.
They vote at CBCR `0x117004` mask `0x400` and CBCR `0x117020` mask `0x800` in bank `0x153040`.
The `0x123004`/`0x123000` common votes are separate and remain unchanged.
A vote does not assert a root rate or a hardware completion.

Run404 passes all engine votes and reads `0x00a80080`, the QUP1 S0 SE control at `+0x80`.
The selected TZ sequence at `0x15715610..0x157158f4` then reaches `+0x24`, `+0x28`, `+0x2000`, `+0x2004`, and `+0x2008`, copies configuration to `+0x1010`, and sets `+0x200c` bit zero.
`platforms/qcom/sm88xx/qsee-qup-common.lua` now owns every enabled engine:
seventeen QUP engines plus the two hub engines on SM8850, and fourteen QUP engines on SM8845.
It reuses an existing `qcom,geni-se-v1` owner when the base matches and rejects a partial overlap.
`models/qcom/uart/geni_uart.h` now retains `+0x200c`.
The sequence does not poll that word and does not claim a transfer or completion.

The selected TZ loaders also write `+0x60` before configuration and `+0xb008` after the mailbox reports ready.
`models/qcom/gsi/gsi.h` now retains both ordered controls.
It also accepts the TZ direct `0x81` mailbox request while keeping the XBL `0x61` arm step.
The SM8850 table has a fourth QUP4 instance at GSI `0x1a04000` and common `0x1ac0000`; `xbl-qup-firmware.lua` adds it.
The SM8845 table has three instances.
The next expected boundary is the GSI firmware bootstrap at `0xa04060` and the selected payload copy.
Do not add a firmware response or an acknowledged transfer without selected-image evidence.

## CPUCP runtime root tables

SM8850 `0x1d808b8e..0x1d808d06` and SM8845 `0x1d808f40` configure four existing roots through runtime tables.
Each table has four words at root base `+0x20..+0x2c`.
Each word contains the source selector in bits 10:8 and the divider in bits 4:0.
The guest enables runtime control at `+0x18` and selects level two through a separate selector word.
It sets request bit zero, waits for bit 31, then clears the request.

| Root base | Selector | Request/status |
| --- | --- | --- |
| `0x17c02000` | `0x17c02168` | `0x17c0216c` |
| `0x17c01000` | `0x17c0113c` | `0x17c01140` |
| `0x17c00000` | `0x17c00150` | `0x17c00154` |
| `0x17c03000` | `0x17c0225c` | `0x17c02260` |

The runtime revision retains the conventional RCG interface and its PLL source wiring.
An accepted request updates the same active clock configuration.
Status bit 31 requires a programmed level, enabled control, completed acquisition, and a live source.
Unknown offsets and unsupported table fields remain errors.
The native PLL fixture checks a missing PLL source and successful selection of the live crystal.
SM8850 run295 passes the runtime selection and reaches a later ACEHR DMA source.

SM8845 run228 passes the tables and reaches the shared handover selector at `0x18877678`.
SM8850 `0x1d808548..0x1d8085d2` and SM8845 `0x1d8088fa..0x1d808984` select and enable that path.
The existing boot controller retains selector `+0x78` and enable `+0x80`.
Status `+0x94` reports no pending disable operation in the synchronous virtual profile.
The disable path clears enable bit two before it polls status bit zero.
The model does not use that status to release a CPU or publish a firmware response.

### Preserve the active source during runtime updates

Run298's host debugger found `platform.apss` in `QemuCpu::hold_reset` while CPUCP continued thermal processing.
The domain retained complete tags and enabled branches, but its initialized and completion states were clear.
The runtime root had completed a valid `0x200` selection with command two.
The pending update previously reported zero output, which the domain interpreted as physical clock loss.

The runtime revision then retained the active source during a pending table update.
Actual source loss still yields zero frequency and triggers the existing domain reset policy.
The native clock fixture checks that an unavailable replacement does not interrupt the established crystal output.

Run669 showed the same fault in the conventional revision.
UEFI changed the UFS AXI, ICE, UniPro, and auxiliary roots while the UFS link was active.
Each pending update reported zero output for 1 µs. The UFS controller interpreted the gap as source clock loss and stopped the PHY.
UFSDxe then sent `DME_PEER_GET` to a stopped link.
A real RCG continues on its active source until the new source is ready.
All RCG revisions now retain the active configuration during an update.

Run299 completes the QSEE load and reaches the later firmware handoff after this correction.
CPUCP then waits for the fourth root's runtime selection because selector two had no parent in the ROM-only source map.
SM8850 table `0x1d8340b8` and SM8845 table `0x1d834050` select source two for level two.
The runtime composition connects this selector to the existing PLL4 source, as on the adjacent CPUCP root.

## UEFI camera RSC clocks

The SM8850 `IMAGES/uefi.elf` has SHA-256 `da8ad1b0ffc2eff91d7d34d8574a682b7007e0b052439fbbcde989d5393071f5`.
Its ClockDxe image (GUID `4db5dea6-5302-4d1a-8a82-677a683b0d29`) has PE SHA-256 `c27e141dcfd0e26c9ff7cd7c9c20c0a5cedd09fa5e86a5630fea95540cf85b70`.

RscDxe enables five clocks before it uses the camera RSC:
`gcc_camera_ahb_clk`, `cam_cc_titan_top_gdsc`, `cam_cc_drv_ahb_clk`, `cam_cc_drv_xo_clk`, and `cam_cc_core_ahb_clk`.
An enable request first detects the domain configuration from `CMD_RCGR` and `CFG_RCGR`.

HWIODxe places `CAM_CC_CAM_CC_REG` at camcc `0x0956d000` plus `0x20000`.
The ClockDxe records give these registers:

| Record | Name | Register |
| --- | --- | --- |
| `0x270d0` | `cam_cc_drv_ahb_clk` | CBCR `0x0958e398`, domain `0x28c20` |
| `0x27990` | `cam_cc_drv_xo_clk` | CBCR `0x0958e390`, domain `0x28de0` |
| `0x26ff0` | `cam_cc_core_ahb_clk` | CBCR `0x0958e348`, domain `0x28c20` |
| `0x280f0` | `cam_cc_titan_top_gdsc` | GDSCR `0x0958e334` |
| `0x28c20` | AHB domain | `CMD_RCGR` `0x0958d100` |
| `0x28de0` | XO domain | `CMD_RCGR` `0x0958e34c` |

QSEE already owns the core AHB branch and the TITAN_TOP GDSC.
`platforms/qcom/sm88xx/uefi-clock-dependencies.lua` adds the two roots and the two remaining branches.
Both roots select XO at reset. That value is the ClockDxe safe configuration, so no camcc PLL is required.
Run656 stopped at the first read, `CFG_RCGR` `0x0958e350`, from ClockDxe image offset `0x11154`.
Run657 passed these clocks and stopped in the camera CESTA programming.
See `models/qcom/rpmh/evidence.md` for the camera CESTA.

After the CESTA programming, `Clock_InitCESTA` reads each source with the `CESTA_CONTROLLED` flag.
It enables and then disables a source that is off, once, for its one-time settings.
The camcc source records `0x29948`, `0x29a08`, and `0x29ac8` (PLL3 through PLL5) carry flag bit `0x4000000`.
HWIODxe places `CAM_CC_PLL0..7` at camcc `0x0956d000` with a `0x1000` stride.
The platform maps one `qcom,gcc-taycan-eko-t-pll-v1` bank with eight PLLs there.
Run659 stopped at the first read, PLL3 `PLL_MODE` at `0x09570000`.
Run660 passed the PLL sequence and stopped in RpmhDxe.

## UEFI PCIe RSCC clocks

Run737 passes RscDxe for the camera RSCC.
ClockDxe then writes `0x210000` to the vote word `0x152010` at image offset `0x10a54`.
The vote bank at `0x152008` rejects bit 21, because no branch owns that bit.
A diagnostic run that accepts bits 20 and 21 then reads CLK_OFF at `0x111008`, from image offset `0x10ad4`.
That function tests CBCR bits 31:28 through the branch pointer at descriptor offset `+0x8`.

The two CBCR words sit at GCC module offsets `0x1004` and `0x1008`, and ClockDxe reads them at `0x111004` and `0x111008`.
Linux `gcc-canoe.c` lists `gcc_pcie_rscc_cfg_ahb_clk` and `gcc_pcie_rscc_xo_clk` in the same order as the two writes.
The model starts both words with bit 31 set, so both branches report CLK_OFF before the first vote.
The exact reset words are not established, and no consumer depends on the other bits.
The GCC base `0x100000` and module offset `0x10000` give the same two addresses.
Linux `gcc-canoe.c` keeps eleven critical clocks on in its probe function, and both RSCC clocks are in that list.
The vote writes of `0x52010` bits 20 and 21 follow that order.

`platforms/qcom/sm88xx/uefi-clock-dependencies.lua` adds both branches with the virtual XO reference as the source.
It adds bits 20 and 21 to the second word of the existing vote bank.
Each branch reports CLK_OFF from its own vote. No other bit of that word changes.

## UEFI GPU power domains

After DisplayDxe, ClockDxe enables two GPU power domains.

The first is the GPU CC CX GDSC at `0x03d99080`.
ClockDxe image offset `0x10ef8` clears SW_COLLAPSE and polls PWR_ON at the GDSCR.
It then polls bit 31 of the word at `0x03d99094` (run757 stopped at that read).
Linux `gpucc-canoe.c` declares `gpu_cc_cx_gdsc` with `.gdscr = 0x9080` and `.gds_hw_ctrl = 0x9094`.
Linux `gdsc-regulator.c` `poll_gdsc_status()` reads PWR_ON (bit 31) from `GDS_HW_CTRL` when a domain declares it.
The `qcom,gdsc-v1` model therefore has an optional read-only `hw_ctrl_offset` word.
Bit 31 of that word reports the same power state as GDSCR bit 31. A write fails.
Both composition helpers that create the GPU CX GDSC set `hw_ctrl_offset = 0x14`.

The second domain uses a GPU-specific hook at ClockDxe image offset `0x41ac`.
The hook writes the GPU CC FF root at `0x03d99474`.
`FF_CMD_RCGR` uses GPU CC offset `0x5474`, and `FF_CFG_RCGR` uses `0x5478`.
ClockDxe uses those offsets from a base that HWIODxe maps at `0x03d94000`.
It sets SRC_SEL to 5 and the divider field to 5, then sets UPDATE.
Linux `gpucc-canoe.c` parent map 1 gives selector 5 as `GPLL0_OUT_MAIN`.
No firmware access reaches M, N, or D for this root, so the model leaves those offsets undeclared.
`platforms/qcom/sm88xx/uefi-clock-dependencies.lua` adds a `qcom,rcg-v1` root with GPLL0 at selector 5.

The hook then sets bit zero at `0x03d68040` and `0x03d68008`.
Both words are CBCR-style enables in the GX clock control block.
Firmware does not establish their private layout names.
The same block holds `GX_GDSCR` at `+0x24`, which the platform already models.
The hook programs no GX root, so both branches use the virtual XO reference.
Run763 passes both domains and stops at a write of `0x8` to `0x03d9d314`.

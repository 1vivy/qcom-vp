# UFS controller integration

## Model boundary

`UfsController` uses the compatible key `qcom,ufs-hci-v1`.
This key identifies the wrapper contract, not a verified SM8850 hardware revision.
The wrapper uses QBox `ufs` and `ufs_lu` for the UFS protocol and block operations.
It does not load boot artifacts into guest RAM.
It does not alter firmware authentication.

The public `target_socket` accepts aligned, four-byte register transactions.
The wrapper forwards the original transaction, including its attribute extension, to QBox.
An explicit register list prevents unknown vendor registers from returning zero through libqemu.
Unsupported task-management writes also return a TLM address error.
MCQ is disabled and cannot be enabled through this wrapper.

QEMU processes UTRL doorbells, descriptors, UPIUs, and PRDT entries.
Its SCSI backend performs reads and writes against the six mutable raw files.
The upstream logical block size is 4096 bytes.
Writes are not snapshots and do not use a read-only backend.

### Implementation sources and licenses

The controller and media implementation remain the pinned public QEMU UFS stack.
No second controller, media backend, or custom UPIU transport exists.
The backend identifies its target specification as UFS 4.1.
This wrapper exposes only its supported single-queue subset, with one lane and gears one through four.
It does not infer G5 or MCQ support from that specification label.
The declared vendor HW_VERSION is separate from the backend's UFS device specification version.

QEMU `hw/ufs/ufs.c` and `hw/ufs/lu.c` carry GPL-2.0-or-later licensing.
The libqemu patches modify those existing GPL files and retain their license notices.
The local C++ model headers carry BSD-3-Clause-Clear notices.
This slice imports no Linux, EDK II, U-Boot, or qcom-rehost driver implementation into those headers.
Linux sources supply register and protocol evidence.
qcom-rehost observations are secondary evidence only. They do not establish driver quality, compatibility, or implementation authority.
The selected firmware consumers and the existing QEMU state determine each implemented operation.

## Registration and boot selection

`src/platform.cc` registers the controller and `qcom,ufs-lu-v1` logical units.
The platform target uses the installed QBox headers and libraries.
`package-lock.cmake` applies `0003-implement-ufs-boot-lun.patch` through the checked dependency patch mechanism.
The dependency cache key includes every libqemu patch digest.

The selected boot LU uses its existing SCSI device, block backend, DMA buffers, and completion path.
The request retains boot well-known LUN `0xb0` in its UPIU header.
No separate boot disk or fixed LUN-zero alias exists.
Selection zero disables boot access. Values one and two select matching nonzero `bBootLunID` values.

Invalid selections, duplicate nonzero IDs, and missing selected units fail.
Boot attribute requests require index zero and selector zero.
Unit descriptors expose provisioned boot IDs.
The device descriptor exposes boot capability separately.

## Platform interface

`platforms/qcom/sm88xx/ufs.lua` creates a dedicated QEMU instance without a CPU.
The helper requires these inputs:

| Input | Meaning |
| --- | --- |
| `luns[0..5]` | Independent mutable files for this run. |
| `boot_lun_ids[0..5]` | Provisioned `bBootLunID` values. |
| `boot_enable` | The provisioning header's Boolean `bBootEnable` capability. |
| `boot_lun_en` | The board's explicit initial selection. |
| `router` | The CPU MMIO router. |
| `ram_router` | A separate router for permitted DMA memory. |
| `irq` | The GIC input reference. |
| `virtual_phy` | Explicit permission to use the digital PHY substitute. |

The installed board files select boot ID one as virtual reset policy.
The helper checks that this ID exists.
Neither capability nor the Android slot determines the selection.
The helper escapes file-path commas for QEMU's option parser.

The DMA bridge uses `QemuInitiatorSocket::init_global` because sysbus UFS uses QEMU's global address space.
It calls `enable_bulk_access()` before QEMU creates the memory-region operations.
Other initiator sockets keep their eight-byte access limit.
It preserves the upstream unspecified transaction attributes.
Its monitor permits those attributes only at this device ingress.
The bridge denies DMI and routes every transfer through the allowed RAM view.

The current platform permits the shared APSS SRAM range `[0x14800000, 0x14a80000)`.
CPU accesses and DMA accesses use the same backing memory.
ROM, MMIO, and other memory remain outside the DMA view.
This physical view does not implement the apps SMMU.
The package DT's stream ID `0x60` does not establish the ROM's translation or security configuration.

The interrupt output connects to GIC SPI 265, architectural INTID 297.
`apss-gic.lua` uses QBox input `spi_in_265`, not `spi_in_297`.
It connects all four GIC outputs and the CPU's four architectural timer outputs.
GIC security extensions remain enabled. LPIs remain disabled because this platform has no ITS.

The UFS wrapper captures each IRQ level while the UFS instance lock remains held.
It queues levels in order and forwards them through `irq_out` on SystemC.
The capture callback does not wait or release the instance lock.
QBox's GPIO proxy updates its previous level after the callback returns.
Releasing that lock during the callback permits an acknowledgement to suppress the falling edge against a stale previous level.
The private capture path prevents that reentrancy without changing the level-high interrupt or the public platform binding.
The instance lock prevents another UFS callback until the proxy commits its previous level.

A mutex protects the FIFO. The drain releases that mutex before it enters the APSS instance.
The deque stores transitions in blocks, without a fixed capacity or a separate allocation for each transition.
The controller retains the capture GPIO and its proxy throughout simulation.
At simulation end, it acquires the UFS instance lock before disabling the callback.
This waits for any active capture callback before controller teardown.

The GIC exposes one CPU and 864 SPIs, including the separate UART input at SPI 835.
Its first redistributor occupies `0x20000` bytes at `0x17080000`.
The package DT reserves more redistributors with a different stride.
The first frame has the same address. Additional frames and GICv4 extensions remain unsupported.

## GDSC power dependency

`qcom,gdsc-v1` identifies `qcom_vp::QcomGdsc` in `models/qcom/ufs/gdsc.h`.
Each instance exposes an eight-byte relative register window.
Platform data supplies the physical address and consumer binding:

| Domain | Physical address | Linux supply | Output binding |
| --- | --- | --- | --- |
| `gcc_ufs_mem_phy_gdsc` | `0x0019e000` | `vdd_mx` | UFS `mem_power_socket` |
| `gcc_ufs_phy_gdsc` | `0x00177004` | `vdd_cx` | UFS `logic_power_socket` |

Both domains start collapsed as explicit virtual reset policy.
This policy is not a measured silicon reset value.
The two mandatory inputs start off in `UfsController`.
The controller and virtual PHY require both supplies.
An unpowered register access fails. It does not return a fabricated reset value.

GDSCR offset zero implements software collapse at bit 0 and read-only power status at bit 31.
CFG_GDSCR offset four reports power-up completion at bit 16 or power-down completion at bit 15.
Other control bits and CFG_GDSCR writes fail.
Each `power_socket` sends a four-byte Boolean write at relative address zero.
The consumer applies the transition synchronously before the GDSC acknowledges completion.
No status poll causes a power transition.

An unbound GDSC starts collapsed and rejects power-on requests unless the platform explicitly sets `virtual_supply=true`.
This supports XBL's unused domain at `0x147170` without inventing a powered consumer.
XBL `0x14860684` requests collapse and observes the existing inactive state.

The virtual supply controls an isolated domain latch. It does not power an absent peripheral.
It cannot coexist with a bound consumer.
Post-DDR domain inventories and their limits appear in `models/qcom/sm88xx/cpucp/clock_roots_evidence.md`.

Collapse requires an empty QEMU transfer doorbell.
A busy collapse fails without changing the supply, readiness, or completion status.
The model does not cancel active requests or report them as successful.
For an idle collapse, the wrapper uses QEMU MMIO to:

1. Disable interrupts and HCE.
2. Stop the transfer list and clear its base.
3. Clear UIC argument registers and pending interrupt status.
4. Remove PHY supply and discard PHY configuration, reset history, start, and readiness.

Loss of either supply also clears the wrapper's vendor configuration.
Guest software must repeat PHY setup, HCE enable, link startup, and transfer-list setup after power returns.
Doorbells require PHY readiness, HCE, device presence, and a running transfer list.
DMA remains on the real QEMU path and requires both supplies and PHY readiness.
PHY reset assertion, power-down, PLL disable, or stop also requires an idle controller before removing readiness.
These changes disable the controller through the same MMIO path.

This is functional, quiescent collapse, not an analog or retention model.
There is no modeled voltage ramp, retention mode, regulator negotiation, or GDSC transition delay.
`vdd_mx` and `vdd_cx` identify Linux dependencies, not additional emulated regulator controls.
The implementation does not claim that QEMU `system_reset()` resets UFS.
QEMU's HCE-clear implementation explicitly clears HCS and HCE.
The wrapper uses that implementation rather than an unverified device reset callback.
The wrapper reports local UIC readiness while its digital supplies and source clocks remain available.
Local DME reads do not require HCE, PHY readiness, or a connected peer.
Transfer readiness and DMA retain their separate physical prerequisites.
The attached UFS device retains its provisioning, saved device attributes, and completed media writes.
The wrapper does not claim a complete controller reset or a UFS device power cycle.

The pinned [Canoe GCC source][canoe-gcc-gdsc] names both domains and their supplies.
Its offsets are relative to GCC base `0x00100000`.
The pinned [GDSC driver][canoe-gdsc] defines collapse and completion bits.
APSS ROM `0x3135d4` clears bit 0, then `0x313118` polls offset four for bit 16.
ROM `0x313610` sets bit 0 and polls bit 15.
These observations establish the implemented handshake, not retention behavior.

[canoe-gcc-gdsc]: https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c#L3920
[canoe-gdsc]: https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gdsc.c
## Controller reset and reference clock

The controller reset socket maps GCC `0x177000`.
The pinned Canoe driver names this offset `GCC_UFS_PHY_BCR`.
APSS `0x313100` writes the register through its reset descriptor.
Bit 0 holds the functional controller and PHY in reset.
Assertion requires idle transfer state and removes controller configuration and PHY readiness.
Release requires explicit guest initialization.

The separate active-low device reset maps TLMM `0x0f1e9000`.
The rehost model records the matching OEM UFSDxe consumer.
APSS also reaches that register through its GPIO helper at `0x310704`.
The wrapper sends transitions to QEMU's real `device-reset-n` input.
Reset removes device presence and transfer readiness without removing host or PHY configuration.
Each attached SCSI device receives a real reset and reports reset unit attention.
Release does not perform link startup.

The reference gate maps TCSR `0x01fd5054`, bit 0.
APSS `0x307244–0x30725c` enables it before PHY initialization.
The rehost clock table names the same register `tcsr_ufs_clkref_en`.
`qcom,tcsr-clock-reference-v1` provides an initially disabled gate with an explicitly available upstream reference.
Its consumer must apply each transition before the gate retains its new state.
Reference loss requires an idle controller, stops the PHY, and disables HCE.
Reference restoration alone does not restore readiness.

The actual UFS regression passed controller reset and device reset.
It confirmed retained media, retained host state across device reset, and real SCSI unit attention.
The model does not claim a complete UFS power cycle.
Reference-gate coverage uses the same actual guest fixture.



## Evidence

The inspected ROM is `pbl8850.bin`, loaded at `0x00300000`.
Its SHA-256 is `91bd8f81efa7a5940d4b6d4c220388b559ceef39b6b47dd7b4b0727a1beb1d1c`.
The following addresses identify instructions in that ROM, not source analogues:

| ROM address | Observation |
| --- | --- |
| `0x3078dc`, `0x3078e4` | Construct `0x01d84020`, the UFS interrupt-status address. |
| `0x30782c`, `0x307834` | Complete `0x01d84030`, then read HCS. |
| `0x307f48`, `0x307f58` | Construct the same interrupt-status address. |
| `0x307f98` through `0x307fb0` | Write UIC arguments at offsets `0x94`, `0x98`, `0x9c`, then command at `0x90`. |
| `0x3079f0` through `0x307a00` | Read-modify-write bit 0 at PHY address `0x01d80408`. |
| `0x307a1c` through `0x307a24` | Read-modify-write bit 5 at PHY address `0x01d8042c`. |
| `0x307a8c` through `0x307a9c` | Read-modify-write bit 31 at `0x01d8684c`. |

These instructions establish the HCI base `0x01d84000` independently of the older firmware source.
The Lua helper maps `0x3000` bytes, but the wrapper rejects unimplemented offsets inside that window.

### Exact package DT

The source package is `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`.
Its extracted `IMAGES/vendor_boot.img` contains a Canoe DTB at file offset `0x1537df8`.
That DTB identifies `qcom,canoe`, MSM ID `0x294`, and revision `0x10000`.
Private DT extraction used each FDT header's total size, then `dtc` decoded each DTB.
No extracted DTB, DTS, firmware bytes, or calibration byte tables enter this repository.

| DT node or property | Integration requirement |
| --- | --- |
| `/soc/ufshc@1d84000`, `interrupts = <0 0x109 4>` | GIC SPI 265, architectural INTID 297, level-high. |
| Root `interrupt-parent`, phandle 1 | `/soc/interrupt-controller@17000000`, compatible `arm,gic-v3`. |
| GIC `reg` | Distributor `0x17000000`, size `0x10000`. Redistributors `0x17080000`, size `0x200000`, stride `0x40000`. |
| UFS `iommus = <0x69 0x60 0>` | Apps SMMU stream ID `0x60`, mask zero. |
| `/soc/apps-smmu@15000000`, phandle `0x69` | Compatible `qcom,qsmmu-v500`, size `0x100000`, two IOMMU cells. |
| UFS `dma-coherent` | CPU and UFS must observe the same descriptor and buffer backing bytes. |
| UFS `qcom,iommu-dma = "fastmap"` | Linux uses a translated DMA mapping. This does not prove the ROM uses translation. |
| UFS `msi-parent = <0x8d 0x60>` | ITS device ID `0x60`, separate from the legacy HCI SPI. MCQ and ESI remain disabled here. |
| UFS `reg-names` | HCI `0x01d84000/0x3000`, ICE `0x01d88000/0x18000`, MCQ SQD `0x01da5000/0x2000`, MCQ vendor `0x01da4000/0x10`. |
| `/soc/ufsphy_mem@1d80000` | Separate PHY region `0x01d80000/0x2000`. It is not part of the HCI window. |

The base node has status `disabled`.
The package's `dtbo.img` overlay at offset `0x120` enables both the controller and PHY.
Its fragments 10 and 11 target `ufsphy_mem` and `ufshc_mem`.
The PHY compatible is `qcom,ufs-phy-qmp-v4-canoe`.
The overlay also specifies active-low device reset on TLMM GPIO 217 and the GCC reset named `rst`.

These facts establish interrupt wiring and Linux DMA routing.
They do not authorize arbitrary device access to secure RAM.
For pre-SMMU integration, permit only descriptor and data RAM through a dedicated device ingress.
Keep the upstream unspecified attributes unchanged.
Reject MMIO, ROM, unbacked memory, and RAM outside that ingress's allowed ranges.
A translated guest IOVA must not silently become a physical address after the guest enables its SMMU.

### ROM initialization sequence

The ROM's initialization entry is `0x3079d4`.
The branch at `0x3079ec` can skip PHY programming, according to the result from `0x305798`.
When the branch does not skip programming, the sequence is:

1. At `0x3079f8..0x307a00`, assert PHY software reset at `0x01d80408`, bit 0.
2. At `0x307e40`, program 95 table entries. The address table is at `0x31c148`.
   The loop at `0x307e6c..0x307e88` writes byte-sized calibration values as 32-bit stores.
3. At `0x307e8c..0x307efc`, select a second table through a platform query.
   The alternatives start at `0x31c324` and `0x31c3c4`, each with 20 address/value entries.
4. At `0x307a1c..0x307a24`, set PHY PLL control bit 5 at `0x01d8042c`.
5. At `0x307a30..0x307a38`, clear HCI CFG0 bit 5 at `0x01d840d8`.
6. At `0x307a3c..0x307a50`, release software reset, then set PHY start bit 0 at `0x01d80400`.
7. At `0x307a5c..0x307a7c`, poll PCS ready bit 0 at `0x01d805a8`.
8. When the context's word at offset `0x28` equals 1, set DEBUG_SPARE_CFG bit 31 at `0x01d8684c`.
9. At `0x307ab0..0x307b00`, disable HCE, enable HCE, then poll HCE bit 0.
10. At `0x307b04..0x307c48`, issue DME GET/SET commands.
    These include attributes `0x15a9`, `0x9000` or `0x15a4`, `0x1520`, and `0x1540`.
11. At `0x307c84..0x307ca8`, issue DME_LINK_STARTUP, then check HCS device-present bit 0.
12. At `0x307cdc..0x307cec`, write the transfer-list address and enable the transfer list.

The current OEM [Canoe PHY header][canoe-phy-h] confirms the control addresses above.
Its [driver][canoe-phy-c] asserts reset, programs calibration, releases reset, starts SERDES, then polls PCS ready.
It also identifies power-down control at `0x01d80404`, where value 1 releases analog power collapse.
This sequence is evidence for control semantics, not evidence for every reset value or successful analog calibration.
The ROM's calibration table is not identical to the current kernel's table.
Do not replace either table with the other.

The current OEM [host driver][oem-host] clears QUNIPRO_G4_SEL at CFG0 bit 5.
The current upstream [host driver][linux-host] treats DEBUG_SPARE_CFG as software boot information.
Neither inspected source identifies the effect of its bit 31 in this ROM.
CFG1 bit 31 is a different register. Its ICE workaround must not be applied to DEBUG_SPARE_CFG.

[canoe-phy-h]: https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/oneplus/sm8850_b_16.0.0_oneplus_15/drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-canoe.h
[canoe-phy-c]: https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/oneplus/sm8850_b_16.0.0_oneplus_15/drivers/phy/qualcomm/phy-qcom-ufs-qmp-v4-canoe.c
[oem-host]: https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/oneplus/sm8850_b_16.0.0_oneplus_15/drivers/ufs/host/ufs-qcom.c
[linux-host]: https://github.com/torvalds/linux/blob/master/drivers/ufs/host/ufs-qcom.c

The subsystem base is `0x01d80000`.
The HCI sits at subsystem offset `0x4000`, with size `0x3000`.
A 38.4 MHz reference clock serves the subsystem.
These register facts follow:

- SYS1CLK_1US occupies HCI offset `0xc0`, with mask `0x7ff`.
- CFG1 occupies `0xdc`.
  Its PHY reset bit is 1, reference-clock enable bit is 26, and warm DME reset bit is 28.
- HW_VERSION occupies `0xe4`, with major, minor, and step fields.
- HCI offset `0x284c` is DEBUG_SPARE_CFG. Its one SPARE field covers all 32 bits.
- Later software reads the UFS version from DEBUG_SPARE_CFG.

The ROM access at `0x01d8684c` agrees with that DEBUG_SPARE_CFG address.
No evidence establishes a hardware side effect for any DEBUG_SPARE_CFG bit on SM8850.
The explicit virtual option retains all 32 bits as software storage without a completion side effect.
UEFI UFSDxe wrote a value outside bit 31 in run675, and the earlier bit 31 policy rejected it.
The option likewise retains CFG0 bits zero and five, with a virtual reset value of zero.
Other CFG0 bits fail instead of becoming generic register storage.
Without this option, these registers fail.
CFG1 now implements the bounded selected reset sequence described below.
HW_VERSION reports an explicitly virtual revision. It does not report measured silicon identity.

The virtual PHY's reset, start, and PLL control fields start at zero.
PCS ready starts clear.
Calibration registers require a guest write before readback.
The guest's byte values remain mutable state, not committed calibration data.
Reset assertion clears start and readiness.
Power collapse or PLL disable also clears start.
The model has no analog delay, calibration quality measurement, or physical lane negotiation.

The wrapper supports programming SYS1CLK_1US at offset `0xc0`, followed by readback.
The analogue register definition supplies its `0x7ff` clock-count mask.
The current Linux driver programs this count for interrupt aggregation.
Writes with bits outside that mask fail.
An initial read fails because no authoritative SM8850 reset value was found.
This state does not implement an aggregation timer or change upstream protocol timing.
No instruction in the traced ROM initialization above programs this register.

### UniPro configuration evidence

The analogue firmware's `UfsCommonLib/ufs_core.h:247,258` names `PA_LocalVerInfo` and `PA_SaveConfigTime`.
The [Linux attribute definitions](https://github.com/torvalds/linux/blob/master/include/ufs/unipro.h) confirm IDs `0x15a9` and `0x15a4`.
The [Linux v5.4 version enumeration](https://github.com/torvalds/linux/blob/v5.4/drivers/scsi/ufs/unipro.h) defines value three as UniPro 1.6.
The explicit virtual link reports this version. It does not claim the physical controller's revision.

ROM instructions at `0x307b04..0x307b40` read the local version and compare the result with five.
The equal branch uses vendor attribute `0x9000`. This model does not implement that vendor attribute.
The other branch at `0x307b84..0x307bd4` reads and updates `PA_SaveConfigTime`.
The ROM uses a mask from address `0x31c468`. That word is zero in the inspected ROM.
The model retains a written timing value and returns it on local GET.
Its virtual reset value is zero. The virtual link has no analog configuration delay.

ROM instructions at `0x307bfc..0x307c48` write one to both available-lane attributes.
The virtual topology permits exactly that lane count and rejects other counts.
These operations do not assert PHY readiness, device presence, or transfer completion.
The existing reset, power, PLL, start, and readiness conditions still gate every UIC command.

The next real fault occurred at APSS `0x307fb0` during `DME_SET`.
A read-only debugger capture identified attribute `0x156a`, value one, after successful link startup.
Attribute `0x156a` is `PA_HSSeries`.
The same contract also identifies gear, termination, and power-mode user data.
The driver programs those attributes before it requests a power-mode change.

The real QEMU state retains local negotiation values:

| Attributes | Functional state |
| --- | --- |
| `0x1568`, `0x1583` | TX and RX gear, from one through the advertised maximum of four. |
| `0x1569`, `0x1584` | TX and RX termination, zero or one. |
| `0x156a` | HS series, one or two. |
| `0x1571` | Negotiated TX and RX modes. Each nibble accepts FAST, SLOW, FASTAUTO, or SLOWAUTO. |
| `0x15b0–0x15b5` | Six retained 16-bit power-mode words. |

Unknown attributes, lane selectors, and unsupported peer configuration remain errors.
Invalid values preserve the preceding configuration.
A successful power-mode write raises QEMU's real UPMS status.
An invalid mode does not raise that status.
Initial gear and series are one, and initial mode is `0x11`.
The selected XBL timing cohort below defines its additional virtual state.
These initial values define virtual policy, not physical reset measurements.


### Provisioned boot units

The selected SM8850 OEM package contains `IMAGES/provision_samsung.xml`.
Its line 3 declares boot capability with `bBootEnable="1"`.
Its lines 5 through 10 assign boot IDs `[0, 1, 2, 0, 0, 0]` to numbered units zero through five.
The prepared manifest records those IDs in `storage.luns[].boot_lun_id`.
The wrapper consumes these IDs rather than assigning a boot identity to LUN zero.

The analogue firmware's `UfsCommonLib/ufs_memory.c:2877..2880` distinguishes capability from selection.
It resolves boot access by matching `bBootLunID` against `bBootLunEn`.
The same source's `ufs_api.c:881..882` sets the selection through an attribute operation.
The [Linux protocol definitions](https://github.com/torvalds/linux/blob/master/include/ufs/ufs.h) identify boot well-known LUN `0xb0` and selection attribute zero.
These facts determine the model's lookup and query interfaces.

The provisioning XML does not specify the initial `bBootLunEn` attribute.
The installed board files select boot ID one as explicit virtual reset policy.
This selection identifies the declared boot-A unit, not an OEM provisioning default.
The helper rejects a policy that names an absent boot ID.
It does not infer a selection from capability or the Android slot.

## Upstream limits and missing facts

The inspected libqemu checkout reports revision `6ef1d0ee35929a157e8027db8d246412ee0df95c`.
The relevant implementation is `hw/ufs/ufs.c`, `hw/ufs/lu.c`, and `include/block/ufs.h`.
QBox supplies `qemu-components/ufs/include/ufs.h` and `ufs_lu/include/ufs_lu.h`.

- Sysbus DMA uses the global address space and unspecified transaction attributes.
  A future upstream fix needs a DMA MemoryRegion link and an explicit device-attribute policy.
- The upstream UIC handler supplied fixed attributes and accepted unsupported operations.
  The patches retain supported negotiation state inside QEMU.
  The wrapper rejects unknown attributes and commands before dispatch.
  The declared topology has one lane per direction and supports gears one through four.
  Unsupported lane counts and negotiation values fail without changing the existing state.
- Task management and transfer-list clear operations are incomplete upstream.
  The wrapper rejects their unsupported writes instead of silently accepting them.
- QBox's global peripheral helper drops asynchronous DMI invalidation work.
  This wrapper uses its own bridge and denies DMI instead.
- The implementation patch supplies real boot selection through the existing block-backed logical units.
  It does not implement a separate firmware preload or authentication service.
- The ROM uses transfer-list clear at `0x3079a0` on an error path.
  The wrapper rejects this write because upstream does not implement cancellation.
  This blocks that recovery path, not the successful transfer path.
- The virtual PHY accepts the observed ROM and selected XBL register identities and explicit control registers.
  It retains guest calibration bytes but does not compare them with copied firmware values.
  It requires reference clock, reset assertion, reset release, power release, PLL enable, and start before PCS ready.
  Link startup requires that ready state.
  Without the explicit option, link startup and PHY accesses fail.
  See `docs/qcom/sm88xx/stubs.md` for the substitute's fidelity boundary.
- The address and ROM evidence applies to the supplied SM8850 ROM.
  The selected SM8845 analysis below establishes static compatibility for the extended composition.
  Native SM8845 XBL and UEFI storage acceptance remains separate.

The build applies the implementation patch to its private dependency checkout.
The repository retains no modified cache source as an implementation artifact.

## Verification

The actual guest test uses the installed QBox executable and six block-backed logical units.
It checks boot selections one and two, normal LUN reads, unit descriptors, capability, and invalid selection or query fields.
It also checks that selection zero rejects a boot read.
The guest compares complete DMA buffers with distinct data from the selected backing files.
The fixture now explicitly powers both GDSCs before accessing UFS.
It collapses and restores each supply separately after successful media writes.
For each supply, it checks cleared PHY readiness, HCS, HCE, interrupt state, and transfer-list configuration.
It then repeats initialization and reads real media through normal and boot addressing.
The existing host assertions still compare the resulting LUN files.
This extension requires a new test run. No result for the extension is claimed here.


```sh
cargo test --locked --test platform ufs_boot_selection_reads_the_existing_lun_backends -- --ignored
```

Before the GDSC extension, the test passed with real NOP, read, write, and query requests.
It observes assertion and clearing of GIC INTID 297 for each request.
It writes LUN1, reads the changed data through normal and boot addressing, and compares the resulting host files.
LUN2 retains its original data.
Neither synthetic guest execution nor model construction establishes an OEM ROM boot or kernel handoff.

### Bulk DMA evidence

The fixture reads three blocks from LUN4 and compares all 12288 bytes with their pattern.
The data buffer has no direct host pointer, so QEMU maps it through a 4096-byte bounce buffer.
Each unmap writes one transaction of at most 4096 bytes to the model.
The test requires three 4096-byte UFS transactions at the data buffer.
Each required transaction starts at a separate 4 KiB address.
The trace shows the first eight bytes as `0x4444444444444444`.
The host test also requires an eight-byte CPU control at `0x14806000`.
The APSS initiator does not select the bulk path.
The test requires eight-byte CPU transactions only in that trace.
The trace is the `ACCESS_TRACE` diagnostic output of the platform access monitor.
The documented host suite passed 102 unit tests and 95 platform tests, including this test.

## Selected XBL QMP v4 programming

The selected `IMAGES/xbl_sc.elf` has SHA-256 `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
Its package is `CPH2749export_11_16.0.5.700EX01_2026022701130186`.
The same package's `IMAGES/xbl_config.elf` has SHA-256 `c35a368853ba57f00c9486d4b8fadeb0ded3a59ce410818a69ac415e3c44d3ef`.
Private ELF and FDT decoding recovered the register identities below.
No firmware, DTB, or calibration value table enters the repository.

### The reported access is EUD, not UFS

XBL `0x1489acb8` has file offset `0x74cb8`.
It logs `usb_eud_serial ` through the string at `0x148e2535`.
Instructions `0x1489acd8..0x1489acfc` divide the input serial number into four bytes.
They store those bytes as four words at `0x088e1000`, `0x088e1004`, `0x088e1008`, and `0x088e100c`.
The reported store at `0x1489acec` has file offset `0x74cec`.
Its caller is `0x1488ec74`.
These stores do not identify a PHY alias or a new UFS IP version.
The UFS helper does not map this EUD aperture.

### Complete register group

The configuration contains FDTs at file offsets `0x2ac` and `0x10e14`.
Both contain `/soc/ufs0@1D80000/ufs_phy_cfg`.
Their three PHY programming tables are identical.
The node's `reg` property selects base `0x01d80000` and aperture `0x35000`.
This aperture includes the controller and other blocks. It does not authorize arbitrary PHY accesses.
The source reference is 38.4 MHz.
The initial profile selects high speed, gear five, two lanes, and Rate B.

| Property | First FDT file offset | Second FDT file offset | Records, including terminator |
| --- | --- | --- | --- |
| `MphyInitTable` | `0x2948` | `0x18f8c` | 92 |
| `MphyLaneInitTable` | `0x2bf4` | `0x19238` | 55 |
| `MphyInitEndTable` | `0x2b7c` | `0x191c0` | 18 |

Each FDT record contains three big-endian 16-bit fields: condition, register offset, and value.
Condition `0x8000` terminates each table.
The selected tables use condition zero, Rate A condition one, and Rate B condition two.
The end table writes the same rate register for either rate. It does not add another register identity.
`MphyEOMTable` and `MphyLaneEOMTable` contain only terminators.
`xbl_phy_registers.h` records all 158 non-reset identities from the three programming tables.
Software reset at offset `0x408` uses the existing control path.

| XBL consumer | File offset | Operation |
| --- | --- | --- |
| `0x148d1784..0x148d18dc` | `0xab784` | Decode the selected FDT property into six-byte native records. |
| `0x148d1914` | `0xab914` | Obtain the initialization table. |
| `0x148d197c` | `0xab97c` | Obtain the second-lane table. |
| `0x148d19e4` | `0xab9e4` | Obtain the end table. |
| `0x148cb3fc..0x148cb4d8` | `0xa53fc` | Apply conditions and issue 32-bit stores of byte-sized values. |
| `0x148caeb8`, `0x148caefc`, `0x148caf28` | `0xa4eb8`, `0xa4efc`, `0xa4f28` | Select the three tables in order. |
| `0x148d1a64` | `0xaba64` | Start the selected physical or debug PHY path. |
| `0x148d1b04` | `0xabb04` | Start the physical PHY through offset `0x400`, bit zero. |
| `0x148d1b14..0x148d1b34` | `0xabb14` | Poll PCS ready at offset `0x5a8`, bit zero. |

The first table asserts software reset and releases power-down at offset `0x404`.
It selects the initial lane configuration through offset `0x5fc`.
The lane table supplies the second lane and updates that configuration.
The end table enables PLL control at offset `0x42c` and releases software reset.
The start helper reads offset `0x400`, writes start, and delays ten microseconds before the readiness poll.
It permits up to 1000 further one-microsecond polls.
The physical path does not poll a separate PLL-lock register.
The model substitutes ideal physical acquisition. It does not substitute an initial ready value.
The debug branch for platform type `0xf` uses offset `0xc00` and polls offset `0xbc`.
That branch remains unsupported.

### State and parent contract

Call `platforms/qcom/sm88xx/sm8850/ufs-xbl.lua` with the completed platform table.
Call it after the existing UFS and live UFS branches exist, before elaboration.
No new C++ module registration is required.
The helper connects the existing `qcom,ufs-hci-v1` instance to the six live source branches.
Both loaders use one QMP v4 register contract. No legacy incomplete mode or SoC-specific branch remains.
The helper declares virtual host revision 3.0.1 through `virtual_hw_revision`.
This is an explicit functional identity, not a measured chip revision.
It does not raise the backend's existing one-lane, gear-one-through-four negotiation limits.

Both loaders use the same register backing, PHY readiness, QEMU controller, six LUNs, resets, and supplies.
The model accepts the full XBL register set in addition to the original PBL register set.
XBL selects its lane table only when the negotiated lane count is two.
The start contract does not require calibration writes for an unused lane.
Power, reset release, live clocks, PLL enable, and explicit start still determine PCS readiness.
The model retains guest calibration bytes. It does not compare them with vendor bytes.
Calibration writes during an active start are rejected.
Reset assertion clears the programming marks. Supply loss clears retained values as before.
The PBL sequence can still initialize the same block before XBL selects its extended table sequence.

The helper connects six existing live branch sources:

| Parameter | Existing branch address |
| --- | --- |
| `phy_axi_clock` | `0x177018` |
| `phy_ahb_clock` | `0x177028` |
| `phy_ice_clock` | `0x17707c` |
| `phy_unipro_clock` | `0x17706c` |
| `phy_aux_clock` | `0x1770bc` |
| `phy_aggre_clock` | `0x1770f0` |

The configuration names these six clocks, three symbol clocks, the memory GDSC, and `tcsr_ufs_clkref_en`.
The symbol clocks already consume actual PHY readiness. They are not inputs to their own PHY readiness.
The TCSR gate and both supplies retain their existing transport paths.
Source loss removes start and quiesces an idle controller through the existing QEMU MMIO path.
Source loss during active DMA stops simulation. It does not complete or discard a transfer.
Source restoration alone does not restore readiness.
Reset, power loss, reference loss, and explicit stop retain their existing idle checks and readiness revocation.

The earlier XBL group `0x148a082c..0x148a08e4` only retains additional clock requests.
Its existing GDSC `0x177004`, ICE branch `0x17707c`, and AHB branch `0x177028` remain UFS dependencies.
The DDR-controls evidence records the other retained words. Those words do not establish source readiness.

The PBL enables these six branches before it initializes the PHY.
Its group-one descriptor at `0x31d810` selects the branch table at `0x31e0b8`.
The call at `0x307234` reaches `0x313aec`, then the table walker at `0x313544`.
The ten-entry descriptor span contains the six sources above, three symbol branches, and a zero-address entry.
This source requirement does not add a clock that the inspected PBL omits.

### Connected host reset contract

The host initialization also writes HCI CFG1 at offset `0xdc`.
Instructions `0x148cae88..0x148cae90` assert PHY reset with `0x1800052e`.
Instructions `0x148caf98..0x148cafa0` release that bit with `0x1800052c` before PHY start.
Instructions `0x148cafd0..0x148cafe0` write `0x1c00052d`, then `0x148cafe8` enables HCE.
The matching [Linux header][oem-ufs-header] identifies active-high PHY reset at bit one.
Its assert helper reads CFG1 after the write. Its release helper clears that bit.

The reset transition requires idle transfer state before it quiesces the existing QEMU controller.
Assertion holds the same PHY in reset, removes start, and clears programming marks.
PHY table writes remain possible during the hold. Start remains rejected until reset release.
Release does not itself produce PCS readiness.
The later start still requires reference, source clocks, power, and PLL enable.

Bit 28 is `WARM_DME_RESET`.
The selected XBL holds it high across PHY reset, PHY start, and successful HCE enable.
The selected instructions do not toggle it, poll completion, or read a self-clear acknowledgment.
This proves that a persistent active-high reset hold would be incorrect for this path.
It does not establish an independent physical pulse polarity or trigger.
The functional substitute accepts the observed fixed-high warm-reset policy only with its combined PHY-reset sequence.
That first reset transition performs real quiesce. It does not merely retain the word.
Independent warm-policy changes remain rejected.
Readback retains the high policy bit. No fabricated completion or self-clear bit exists.

The mask validator separates fixed policy `0x1800052c` from controls at bits zero, one, and 26.
The fixed policy includes the observed memory/cache settings and the disabled SMMU-prefetch policy.
It does not translate DMA or override the external protection path.
Bit zero retains the selected interface configuration.
Bit 26 gates the device reference after CFG1 programming.
Loss of that reference requires idle state and quiesces HCE through the existing path.
Remote UIC operations, transfer doorbells, and DMA require that device reference.
Local DME reads use the separate digital UIC readiness contract.
The PHY's internal PCS readiness does not substitute for a device reference.
Power loss and controller reset clear CFG1 programming. Readback then requires a new accepted write.

HW_VERSION at HCI offset `0xe4` is read-only and reports declared virtual revision `0x30010000`.
The selected consumer reads it at `0x148cae44`.
This selects the existing host protocol path without claiming a physical revision or adding backend capabilities.
Only this declared virtual revision is implemented.
Writes, unsupported policy fields, and unsupported reset transitions fail.

[oem-ufs-header]: https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/oneplus/sm8850_b_16.0.0_oneplus_15/drivers/ufs/host/ufs-qcom.h

The helper leaves `ufs.dma_socket -> ufs_dma_access -> ufs_dma_protection -> ufs_ram_router` unchanged.
The SMMU helper can insert translation after `ufs_dma_access` by replacing its initiator binding.
Its physical output must use the captured original binding to `ufs_dma_protection.target_socket`.
Page walks must use that same protected physical path.
Existing RAM aliases must retain their original backing. No new medium or duplicate controller is permitted.

SM8850 run52 and SM8845 run47 execute this initialization group and complete `media_init:2`.
The next failure is the device-LUN readiness command, not PHY acquisition.

## Selected XBL capability and timing cohort

The selected local capability queries precede HCE enable and PHY configuration.
SM8850 run49 observes HCS and HCE as zero after BSP reset and before `ufs_utp_get_host_caps`.
The capability getter starts at `0x148c9a88`.
It queries gear limits, lane limits, local version, and the local cache of remote version.
Its final query at `0x148c9be4` submits local DME_GET for `0x15a0`.
The host enables HCE later, at `0x148cafe8`.

These calls require the digital UIC engine, not an already initialized physical link.
The wrapper derives local UIC readiness from its actual supply, reset, and source-clock state.
It leaves transfer readiness and live peer availability separate.
The same sequence completes on both selected OEMs after this correction.

### Recovered command group

The source image and SHA-256 are the selected XBL identified above.
Routine `0x148c9a88`, file offset `0xa3a88`, contains seven local capability reads.
Its immediate query wrapper calls it at `0x148c82dc`, file offset `0xa22dc`.
Cold initialization also calls it at `0x148cae64`.

| Selected consumer | Command and attributes | Contract |
| --- | --- | --- |
| `0x148c9aac..0x148c9be4` | Local GET: `0x1586`, `0x1587`, `0x156a`, `0x1520`, `0x1540`, `0x15a9`, `0x15a0` | PWM/HS gear limits, current HS series, lane limits, local version, and remote version. |
| `0x148caba8` | Peer GET `0x0002` | TX HS gear capability uses the existing declared maximum of four. |
| `0x148c756c` | Peer GET `0x0023` | The connected peer TX gear follows the retained local RX gear. |
| `0x148c9c58`, `0x148c9c80` | Local and peer GET `0x15aa` | Explicit virtual timing granularity is one microsecond. |
| `0x148c9ce0`, `0x148c9d08` | Local and peer GET `0x15a8` | Local and peer activation timing use separate retained state. |
| `0x148c9d64` | Peer SET `0x15a8` | The timing comparison can update peer activation timing. |
| `0x148c9d9c` | Local SET `0x15a7` | Retain the selected hibernation timing. |
| `0x148c9e40..0x148ca180` | Local GET/SET lane, gear, termination, series, power-user-data, and power-mode attributes | Retain the existing bounded negotiation state. |
| `0x148c9fc8` | Peer GET `0x009f`, selector four | Read the declared ideal peer G4 adaptation value for RX lane zero. |
| `0x148ca020` | Local SET `0x15d3` | Retain the peer adaptation initial value. |
| `0x148c9ff4`, `0x148ca04c` | Local SET `0x15d4` | Retain the selected adaptation type. |
| `0x148ca454` | Local GET `0x0041` | Report TX HIBERN8 only from the modeled host/link state. |
| `0x148cb050`, `0x148cb06c` | Local SET `0x155e`, `0x155f` | Retain the two selected TX LCC controls. |
| `0x148ca298..0x148ca2ac` | Local SET `0x0037`, selector zero | Retain the selected TX HS equalizer setting for the single connected lane. |

The [standard attribute definitions](https://github.com/torvalds/linux/blob/master/include/ufs/unipro.h) identify remote version, timing, gear, and adaptation attributes.
The wrapper accepts only this bounded command, attribute, and selector set.
The backend still validates values before it changes state.

### Actual peer and reset state

`PA_RemoteVerInfo` reads the local cache of the peer's UniPro identity.
The cache starts at zero. A successful DME_LINK_STARTUP records the declared UniPro 1.6 identity, value three.
Host reset and device reset retain this identity because the same virtual device remains attached.
The cached identity does not establish live peer availability or set `HCS.DP`.
It does not report the virtual host revision register.
Peer GET operations require actual device presence and a released device reset.
Only the selected peer activation-time SET is supported.
It requires actual peer availability and a nonzero 16-bit value.

The existing DME_LINK_STARTUP path establishes `HCS.DP`.
HCE collapse and device reset clear it through the existing backend paths.
Releasing device reset alone does not restore peer availability.
No wrapper-only peer-present flag or preloaded link-ready value exists.

Local and peer activation timing start at ten virtual units.
The declared granularity makes each unit one microsecond.
Local hibernation timing starts at zero. These values are functional policy, not physical timing measurements.
Activation writes accept nonzero 16-bit values. Hibernation writes accept 16-bit values.
Device reset restores the peer activation-time default through the real backend reset callback.
Host timing follows the same retained-host-state boundary as the existing negotiation attributes.

Adaptation initial state starts at zero. The declared ideal peer G4 initial capability is also zero.
Adaptation type starts at `PA_NO_ADAPT`, value three.
The backend accepts only standard types zero, one, and three.
Initial-value and TX equalizer writes accept one byte. LCC controls accept zero or one.
These settings do not create PCS readiness, device presence, or training completion.
No analog adaptation quality is claimed.

The backend reports TX HIBERN8 only when HCE is enabled and either no link exists or explicit HIBER_ENTER state applies.
Successful link startup clears the explicit hibernation state.
HIBER_ENTER and HIBER_EXIT change that state with the existing backend command paths.
Other TX state samples remain unsupported rather than returning an invented active-state value.
The wrapper's existing PHY and clock conditions still apply before the query reaches the backend.

### Cold-path boundary and rebuild

Cold initialization reads local vendor attributes `0xd002` at `0x148ca368` and `0x9007` at `0x148ca3cc`.
Their file offsets are `0xa4368` and `0xa43cc`.
These attributes retain independent 32-bit configuration words, with an explicit virtual reset value of zero.
`0xd002` is DME_VS_CORE_CLK_CTRL. `0x9007` is PA_VS_CORE_CLK_40NS_CYCLES.
They retain timing requests without creating clocks, device presence, or completion outside the normal UIC path.
Only local GET and SET operations accept these attributes. Unknown vendor attributes remain rejected.

The selected code replaces the one-microsecond field at `0x148ca3a0` and the 40-nanosecond field at `0x148ca408`.
The selected code also clears divider-enable bit eight through a read-modify-write operation.
The public [U-Boot UFS clock implementation](https://lists.u-boot-project.org/pipermail/u-boot/2024-September/564430.html) identifies these attributes and their version-dependent fields.
The model uses register facts, not copied driver code.

The implementation changes `ufs_controller.h` and the existing libqemu negotiation patch `0006`.
`package-lock.cmake` already hashes and applies that patch.
Use `make build` to rebuild the backend and platform through that machinery.
A platform-only rebuild cannot supply the new backend attributes.

### Integration verification

The actual guest regression passed with local capability reads before HCE and PHY setup.
It verifies that reset preserves cached identity while disconnected peer requests still fail.
It also verifies independent local and peer activation timing and retained backing-media writes.
The PHY regression failed before removal of the unused-lane requirement and passed afterward.
The assertions are in `tools/qcom/qcom-vp/tests/fixtures/ufs/driver.c`.

SM8850 run52 and SM8845 run47 complete `media_init:2`.
Both then report SCSI ILLEGAL REQUEST with ASC `0x20` and UFS error `0x28b`.
UFS error `0x28b` maps to TEST UNIT READY.
The read-only debugger observation in SM8850 run53 captures the actual request at `0x148c6c40`.
Its CDB is TEST UNIT READY with zero parameters, and its target is device LUN `0xd0`.
The host reports HCE one and HCS `0x10f`.
The preceding readiness request targets boot LUN `0xb0` and completes.

Patch `0009-implement-ufs-device-readiness.patch` supplies this device-LUN command through the existing QEMU completion path.
It checks host enable, device presence, reset, and initialization state.
Invalid fields retain CHECK CONDITION. Unknown commands retain their existing failures.
The guest regression verifies valid readiness, invalid parameters, and readiness after a real device reset and link startup.

The complete integration run passes 74 tests and Clippy with warnings denied.
SM8850 run55 and SM8845 run49 pass device readiness and read the configuration descriptor.
They then reject WriteBooster flag `0x10` with UFS errors `0x114` and `0x209`.
SM8845 run51 passes that operation and completes `media_init:3`.
It next fails on the DDR status read at `0x0c2f0004`.
SM8850 run56 instead faults at `0x1488df78` before UART initialization.
That separate regression remains under investigation.
These observations do not establish a kernel handoff or a kernel log.

### Configuration and WriteBooster behavior

Patch `0010-expose-ufs-configuration.patch` constructs the configuration descriptor from the actual device and logical units.
The eight-entry layout follows the public [ufs-utils definitions](https://github.com/SanDisk-Open-Source/ufs-utils/blob/dev/ufs_cmds.c).
Its header occupies `0x16` bytes. Each unit occupies `0x1a` bytes.
Absent units remain disabled.
Configurations with units above seven or allocation counts above `UINT32_MAX` return NOT READABLE.
The existing lock rejects configuration writes.

One virtual allocation unit contains one 4096-byte logical block.
This explicit geometry keeps descriptor allocation, SCSI capacity, and backing size consistent.
It does not represent physical flash geometry.
The guest regression compares six different units against READ CAPACITY and checks absent units and rejected configuration changes.

Patch `0011-enable-ufs-hibernation-flush.patch` connects flag `0x10` to QEMU's existing WriteBooster engine.
The active link uses the normal flush flag.
The hibernating link uses the hibernation flush flag.
The existing idle engine reduces occupancy and retains its partial-flush rules.
Its timer uses virtual time rather than host elapsed time.
The actual SCSI backend still performs the media writes.

The guest regression writes real data and checks three states across idle intervals.
An active link does not drain with only the hibernation flag.
A hibernating link does not drain with that flag clear.
A hibernating link drains with that flag set.
The guest then reads the written bytes and preserves them across existing reset tests.
The complete integration run passes 74 tests and Clippy with warnings denied.

## Selected SM8845 extended composition

### Source identities and scope

The source is the selected CPH2771 package from `build/private/sm8845-reconstructed.toml`.
ZIP members and decompressed UEFI sections were read in memory.
No extracted executable or calibration table was written to the repository.

| Member | SHA-256 |
| --- | --- |
| `IMAGES/xbl_sc.elf` | `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4` |
| `IMAGES/xbl_config.elf` | `2effcfac7784d044a34dbff04c0da063c17806083707af285952b75964d8376c` |
| `IMAGES/uefi.elf` | `13722b7ed849359e7afc731dfdc1eb0c1fc1815907d708f4b94cadb7e03be4f4` |
| `IMAGES/vendor_boot.img` | `1736dde58f5d28a4d31cfeefd7e97316fc0cfd48ccfce87b21eab9a5d4ef2d72` |

The UEFI container's firmware volume starts at file offset `0x1000`.
The encapsulating FFS file starts at `0x54238`, with its GUID-defined section at `0x54250`.
Its decoded volume contains UFSDxe, GUID `0d35cd8e-97ea-4f9a-96af-0f0d89f76567`.
The UFSDxe PE SHA-256 is `6b872b8f86cbad939f6ecabaed7577246fd5946f85fcb0bc0b29ee4b09f87aeb`.
UEFI coordinates below are PE RVAs, not offsets in the compressed container.
The PE uses equal raw offsets and RVAs for these sections.
Its entry RVA is `0x1000`. Its actual load address requires native observation.

### Matching configuration, distinct regulator inventory

Both selected configuration FDTs, at file offsets `0x30c` and `0xf52c`, contain `/soc/ufs0@1D80000`.
Their controller-group aperture is `0x01d80000..0x01db5000`, and their reference frequency is 38.4 MHz.
The `reg` properties start at `0x307c` and `0x17df4`.
The aperture does not authorize accesses to unimplemented neighboring blocks.

All 22 clock-name/config properties and all five PHY-table properties match the selected SM8850 data byte-for-byte.
The three substantive PHY tables also match between the two SM8845 FDTs.

| Table | First FDT file offset | Second FDT file offset | Records including terminator |
| --- | --- | --- | --- |
| `MphyInitTable` | `0x3938` | `0x186b0` | 92 |
| `MphyLaneInitTable` | `0x3be4` | `0x1895c` | 55 |
| `MphyInitEndTable` | `0x3b6c` | `0x188e4` | 18 |

Both EOM tables contain only a terminator.
Thus the existing 158 non-reset identities, six-byte record decoder, and QMP v4 control contract apply.
The selected speed request remains high-speed G5, two lanes, Rate B.
That request does not expand the model's one-lane, G1–G4 capabilities.

The PMIC rail names differ from SM8850.
SM8845 names `L17B_E0`, `L3F_E0`, `L2F_E0`, `L1I_E0`, and `L2I_E0` for VCC, VCCQ, VDDA, VDDA_CORE, and QREF.
Their first-FDT properties start at `0x35e0`, `0x35f4`, `0x3608`, `0x361c`, and `0x3630`.
The UFS helper does not model PMIC regulators or replace their selected platform inventory.
Its two GDSC dependencies are not substitutes for those PMIC services.

### Controller, clocks, reset, IRQ, and DMA

Selected `vendor_boot.img` contains nine candidate DTBs. All nine specify the same UFS register windows, SPI 265, and SID `0x60`.
This agreement does not identify which DTB ABL eventually selects.
In the first DTB, at file offset `0x1034000`, the PHY `reg` property starts at `0x10552d0`.
The host `reg`, `interrupts`, and `iommus` properties start at `0x1055a74`, `0x1055ae8`, and `0x1055d08`.

| Resource | Selected contract | Composition boundary |
| --- | --- | --- |
| PHY | `0x01d80000`, size `0x2000` | Existing `ufs.phy_socket`, same retained configuration and link |
| HCI | `0x01d84000`, size `0x3000` | Existing QEMU controller, single queue |
| ICE | `0x01d88000`, size `0x18000` | The wrapper does not implement ICE. The platform retains four QSEE error-branch words; no engine or key slot exists. |
| MCQ SQD / VS | `0x01da5000/0x2000`, `0x01da4000/0x10` | Not implemented, MCQ remains disabled |
| Interrupt | SPI 265, level high, architectural INTID 297 | Existing `&apss_gic.spi_in_265` |
| DMA | SID `0x60`, coherent property | Existing LUNs, RAM backing, and protected physical route |
| Controller BCR | `0x00177000` | Existing idle-checked controller/PHY reset |
| Device reset | `0x0f1e9000`, active low | Existing QEMU device-reset input |
| Reference gate | `0x01fd5054` | Existing TCSR clock gate |
| GDSCs | `0x0019e000`, `0x00177004` | Existing memory and logic power inputs |

SM8845 XBL stores branch addresses at file offsets `0xc9b0c..0xc9c2c`, with a `0x20` stride.
They are aggregate AXI, AHB, AXI, ICE, AUX, RX0, RX1, TX0, UniPro, and reference, in that order.
Its root-address words at `0xc8f08`, `0xc8f28`, and `0xc8f48` select AXI, ICE, and UniPro.
Its GDSC words at `0xc9c50` and `0xc9c54` select memory and logic supplies.
The BCR address word is at `0xc9718`.

The pinned public [Canoe GCC roots][sm8845-ufs-gcc] and [branch definitions][sm8845-ufs-branches] supply names and parent relationships.
Its `gcc_parent_map_3` uses XO=0, GPLL0 main=1, GPLL4 main=5, and GPLL0 even=6.
Its AUX parent map uses only XO=0.

| Input | Branch | Parent root or declared source |
| --- | --- | --- |
| AXI | `0x177018` | RCG `0x177034`, parent map 3 |
| Aggregate AXI | `0x1770f0` | Same AXI root |
| AHB | `0x177028` | Existing explicit virtual always-on reference, not a measured silicon parent |
| ICE | `0x17707c` | RCG `0x17708c`, parent map 3 |
| UniPro | `0x17706c` | RCG `0x1770a4`, parent map 3 |
| AUX | `0x1770bc` | RCG `0x1770c0`, XO-only parent map |
| TX0 / RX0 / RX1 symbols | `0x17702c`, `0x177030`, `0x1770d8` | Existing muxes at `0x177058`, `0x177068`, `0x1770ec` |

The existing shared reset composition already uses these physical parents.
The helper's former AUX/UniPro parameter labels were reversed. This change corrects those labels.
All six sources already participate in one readiness conjunction, so this correction does not change that predicate.
Symbol clocks still depend on actual PHY readiness, not on fabricated ready values.
Continuous root/branch demand, the AHB source, and reset selectors remain explicit virtual policy.

[sm8845-ufs-gcc]: https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c#L1605-L1730
[sm8845-ufs-branches]: https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c#L3382-L3514

The UFS helper preserves the existing controller, LUNs, DMA monitor, and physical protection.
The [SMMU composition](../sm88xx/sm8845/smmu/evidence.md) inserts translation before physical protection.
Its selected IORT and consumer evidence remain separate from the host revision and PHY contract.
No new LUN, controller, DMA buffer, SRAM alias, or media preload is needed.

### Selected driver acceptance points

| Operation | SM8845 XBL VA / ELF file offset | SM8845 UFSDxe RVA |
| --- | --- | --- |
| Read HW_VERSION at group `+0x40e4` | `0x148c908c / 0xa508c` | `0x138cc`, also `0x13664` |
| Assert CFG1 PHY reset with `0x1800052e` | `0x148c90d8 / 0xa50d8` | `0x13918` |
| Obtain initial/lane/end tables | Calls at `0x148c9100`, `0x148c9144`, `0x148c9170` / `0xa5100`, `0xa5144`, `0xa5170` | `0x13940`, `0x139b8`, `0x13a28` |
| Apply conditional table records | `0x148c9644 / 0xa5644` | `0x13f24` |
| Release CFG1 PHY reset with `0x1800052c` | `0x148c91e8 / 0xa51e8` | `0x13ac8` |
| Start PHY through `+0x400` | `0x148cfd44 / 0xabd44` | `0x1ad54` |
| Poll PCS ready through `+0x5a8` | `0x148cfd54 / 0xabd54` | `0x1ad6c` |
| Set device reference/interface with `0x1c00052d` | `0x148c9228 / 0xa5228` | `0x13b08` |
| Enable HCE | `0x148c9230 / 0xa5230` | `0x13b10` |
| Submit DME_LINK_STARTUP | `0x148c92f8 / 0xa52f8` | `0x13bd8` |
| Prepare transfer-list base words | `0x148c93c8..0x148c93dc / 0xa53c8..0xa53dc` | `0x13ca8..0x13cbc` |

Both host-initialization paths compare HW_VERSION with `0x30010000`.
That declared virtual revision selects the existing active-low device-reset sequence.
XBL writes device reset at `0x148cfe30` and `0x148cfe40`, file offsets `0xabe30` and `0xabe40`.
UFSDxe writes it at RVAs `0x1aea4` and `0x1aebc`.
Both consumers select the second-lane table when the version's upper half exceeds `0x3000` and the request uses two lanes.
Their later `>=0x5000` checks do not apply to declared revision `0x3001`.
The revision is functional policy, not a physical SM8845 revision or permission to enable G5, ICE, or MCQ.

The seven capability queries at XBL `0x148c7cd0..0x148c7e2c` match the existing implemented cohort.
The UIC sender is `0x148c7994`, file offset `0xa3994`. UFSDxe uses RVA `0x11b7c`.
Actual completion requires successful commands, valid DMA descriptors, and matching data from the existing block-backed units.
A successful PCS poll or HCE write alone does not prove media continuity.

### Integration call and remaining functional limits

Reuse the existing helper after the UFS instance and live source branches exist, before elaboration:

```lua
dofile(directory..'sm8850-ufs-xbl.lua')(platform)
```

No SM8845 duplicate helper, C++ model version, or new registration is required.
This closes the selected extended composition data, not all optional UEFI storage services.
The following boundaries remain explicit:

- Cold timing initialization reads and modifies vendor attributes `0xd002` and `0x9007`.
  XBL calls occur at `0x148c85b0/0x148c85e8` and `0x148c8614/0x148c8650`.
  Their ELF offsets are `0xa45b0/0xa45e8` and `0xa4614/0xa4650`.
  UFSDxe supplies the same arguments at RVAs `0x1278c/0x127bc` and `0x127f4/0x12824`.
  The model retains these independent attributes. Their zero reset values are explicit virtual policy.
- PHY platform type `0xf`, EOM, analog calibration quality, ICE/HWKM, MCQ, and additional lanes/gears remain unsupported.
  The functional PHY accepts guest programming and acquires readiness only after its existing prerequisites.
- Transfer cancellation, unsupported task management, active-DMA clock loss, and unsupported power/reset transitions retain their existing failure behavior.
- PMIC rail operations, interconnect votes, SMMU translation, and secure storage services remain separate dependencies.
  Matching UFS clock tables does not establish equivalence of those services.
- RPMB uses patch `0026-implement-ufs-rpmb.patch`. UFSDxe calls its open routine with well-known LUN `0xc4` at RVA `0x4828`.
  It reports `Cannot open UFS RPMB!` through the branch at `0x4830..0x4840`.
  The listener-failure string reference is at `0x3c0c`.
  Secure write protection requests remain unsupported. See "Authenticated RPMB" below.

Acceptance must retain actual SM8845 XBL/UEFI storage calls, negotiated limits, transfer completion, and bytes read from the selected LUNs.
It must also retain interrupt assertion/clear and the unchanged DMA backing identity across handoff.
The RPMB protocol follows the JESD220 frame rules in "Authenticated RPMB". No response bypasses the key, counter, or MAC checks.
SM8845 run51 preserves the original image loads through APDP and completes all three media phases.
It initializes the log partition through actual UFS writes. UEFI storage calls remain unobserved.
### UFSDxe automatic-hibernation interface

Offline disassembly of the selected SM8850 UFSDxe PE (SHA-256 `60e978a3e3eb438fed97a797585ababe81d826533e6e67bdb3ea87e1e69eebd7`) identifies register writes at RVA `0x1a6cc` and `0x1a6d0`.
The selected SM8845 UFSDxe PE (SHA-256 `6b872b8f86cbad939f6ecabaed7577246fd5946f85fcb0bc0b29ee4b09f87aeb`) has matching writes at RVA `0x1a70c` and `0x1a710`.
Each helper writes `0x3c` to HCI `+0xfc`, then writes `0x0c00 | (caller_value & 0x3ff)` to HCI `+0x18`.
The setter method is at SM8850 RVA `0xcccc`. The dispatch calls it at `0x2fd0` with a caller-supplied 16-bit value.
The reapply method is at RVA `0xd13c`. Its call at `0x4b64` reaches it only when context byte `+0x4cc` is nonzero.
That method reads the timer from context bytes `+0x4cd..+0x4ce`. Offline inspection found no internal writer for these bytes, fixed nonzero default, or actual selected nonzero timer write.

The pinned libqemu revision stores AHIT in its register structure but has no AHIT write handler or automatic idle timer producer.
Its explicit UIC HIBER_ENTER/EXIT commands update hibernation state. They do not create automatic idle transitions.
The wrapper rejects writes to `+0x18` and `+0xfc`. No automatic timer is implemented without evidence of an active selected timer value.

The decisive future probe is a native UEFI capture of writes to HCI `+0xfc` and `+0x18`, with the guest PC and values.
If the capture shows an active timer, resolve the setter caller and its `w1` value before implementing automatic entry and exit against real transfer and link state.

### UEFI peer rate query

Run676 completed UFS DMA through the QHEE SMMU context.
UFSDxe then read peer `TX_HSRATE_Series` (`0x0022`) with DME_PEER_GET.
The wrapper rejected the command because it permitted only peer `TX_HSGEAR` (`0x0023`).

`UfsCommonLib/ufs_memory.c:1138-1165` reads the peer gear and rate in sequence.
The peer rate follows the retained local `PA_HSSeries` selection.
Patch `0024-report-peer-ufs-hs-rate.patch` returns that selection through the QEMU UIC command path.
The wrapper permits `0x0022` only for DME_PEER_GET after the PHY and reference clock are ready.
The QEMU link must also report device presence.

### UEFI RPMB readiness

Run677 passed the peer rate query. UFSDxe then opened well-known LUN `0xc4`.
`UfsCommonLib/ufs_api.c:258-395` sends TEST UNIT READY before the RPMB descriptor query.
The QEMU well-known LUN handler rejected this command on RPMB with sense `05/20/00`.
The `ufs_open` call then failed, and UEFI reported `Cannot open UFS RPMB!`.

Patch `0025-report-ufs-rpmb-readiness.patch` permits TEST UNIT READY for the RPMB well-known LUN.
The command uses the existing device reset, initialization, HCE, and link presence checks.
The model still rejects malformed CDBs and does not emulate authenticated RPMB frames.
The guest fixture checks a ready RPMB unit and an invalid CDB field.

### Authenticated RPMB

Run713 passes app authentication in QSEE. The app load then fails in the secure-storage listener query.
QSEE `0x15748398` sends listener `0x2000` a version `0x101` request for device two.
Instructions `0x157485a4..0x157485d8` accept a capacity of `0x100..0x8000` 512-byte sectors only.
UFSDxe `UFS.c:2717` reports `qLogicalBlockCount * 256` bytes from the RPMB unit descriptor.
QEMU reported a disabled RPMB unit with a zero count. The capacity check failed, and every app load returned one.

Patch `0026-implement-ufs-rpmb.patch` adds the `rpmb-size-kib` controller property.
A nonzero size enables the RPMB unit descriptor with 256-byte blocks.
The descriptor reports the block count and `bRPMBRegion0Size` in 128 KiB units at offset `0x13`.
The SM8850 board declares 4 MiB. This value is a virtual profile, not a captured device size.

SECURITY PROTOCOL OUT and IN use protocol `0xec` and protocol-specific value `0x0001`.
Each message contains 512-byte frames. The HMAC-SHA256 MAC covers bytes 228..511 of every frame.
The model implements these request types:

| Type | Request | Effect |
| --- | --- | --- |
| `1` | Key programming | Stores the first key. A second key returns general failure. |
| `2` | Write counter read | Returns the nonce, the counter, and a MAC. Without a key it returns NO_KEY. |
| `3` | Authenticated write | Checks the key, the MAC, the counter, and the address range, then writes and increments the counter. |
| `4` | Authenticated read | Returns the requested blocks, the nonce, and one MAC over all frames. |
| `5` | Result read | Returns the result of the last key programming or write. |

A response without a programmed key carries a zero MAC.
A write uses at most `bRPMB_ReadWriteSize` frames, 64 in the geometry descriptor.
Malformed command fields, other request types, and IN lengths without a matching response return CHECK CONDITION.
Device reset discards pending responses. The key, the counter, and the data persist for the run.
Each run starts with an unprogrammed RPMB.
The guest fixture checks the descriptor, NO_KEY results, single key programming, counter reads,
authenticated writes and reads, replay, MAC, and address failures, and length rejection.

# Peripheral provenance review

Status: 2026-10-28. This review covers all 48 table rows in the inventory.
The inventory is `artifacts:2026-10-28-source-only-facts/source-only-peripherals.md sha256=d3f3b2cba7172de6b9606a058a192ff4c89431e9f53f8038e4fb1a74cdbdb25e`.
Entry coordinates below refer to that inventory, not current repository line numbers.
The inventory is a historical citation list and a set of research leads.
Prior evidence pages, accumulated research, and device probes supplied the substantive basis.
This review keeps the unresolved questions concrete and does not treat removal of a citation as proof of wrong behavior.
Companion reviews: [processors and clocks](provenance-processors.md), [TME and boot](provenance-tme.md), [shared composition](provenance-shared.md).

Firmware analysis establishes accesses and data in selected images.
It does not establish physical reset values or register effects that the firmware does not test.
Public driver evidence applies to the identified hardware family, not automatically to every target.
A family catalog pattern can guide an implementation when the record states its basis and its limit.
The review preserves these limits.
It includes no nonredistributable source or binary material.

## Independent public sources

| Source | Scope |
| --- | --- |
| [Linux v6.18 LLCC driver](https://github.com/torvalds/linux/blob/v6.18/drivers/soc/qcom/llcc-qcom.c) | LLCC v6 COMMON offsets |
| [Linux v6.18 PMIC driver](https://github.com/torvalds/linux/blob/v6.18/drivers/mfd/qcom-spmi-pmic.c) | Common PMIC revision, type, and subtype fields |
| [Linux v6.18 TSENS v2 driver](https://github.com/torvalds/linux/blob/v6.18/drivers/thermal/qcom/tsens-v2.c) | Common TSENS v2 registers and reset bit |
| [Linux v6.18 UFS header](https://github.com/torvalds/linux/blob/v6.18/drivers/ufs/host/ufs-qcom.h) | Public UFS vendor offsets and fields |
| [Linux v6.18 UniPro header](https://github.com/torvalds/linux/blob/v6.18/include/ufs/unipro.h) | UniPro attribute identities |
| [OnePlus Canoe DT](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/kernel_platform/qcom/opensource/devicetree/qcom/canoe.dtsi) | UFS HCI aperture and clock roles |
| [OnePlus UFS driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/oneplus/sm8850_b_16.0.0_oneplus_15/drivers/ufs/host/ufs-qcom.c) | Device reference-clock mask and DEBUG_SPARE version use |
| [EDK II dependency evaluator](https://github.com/tianocore/edk2/blob/edk2-stable202605/MdeModulePkg/Core/Dxe/Dispatcher/Dependency.c) | Absent-DEPEX scheduling rule |

The OnePlus driver link selects a branch. Its revision needs a pin before a claim depends on those exact bytes.
The other public links select the stated release tags or commit.

## Claim dispositions

Inventory labels retain the earlier paths under `models/`.
Current model locations follow the manufacturer and family hierarchy.
A selected-firmware claim remains specific to that image and procedure.
An unresolved physical meaning cannot become a model fact merely because a guest accepts the implementation.

| Inventory entry | Disposition and evidence boundary |
| --- | --- |
| `ddr/controls-evidence.md:246` | Keep the uncertainty about reset values and reserved masks. The selected-consumer analysis establishes no broader claim. |
| `ddr/controls-evidence.md:398` | Selected writes at `0x010b9754/0x010b9758` support the operation. The PERIOD_BUS_CFG name remains unresolved. |
| `ddr/evidence.md:374` | SHRM writes support the consumed offsets. Physical clock-gating and status meanings remain unresolved. |
| `ddr/phy-evidence.md:339` | Selected writes support bit use. The AACS name remains unresolved and does not establish branch identity. |
| `ddr/phy-evidence.md:587` | Linux LLCC v6 defines COMMON hardware info at `0x64000`. FEAC, PMGR, and LPI_LB names and complete extents remain unresolved. |
| `ddr/phy-evidence.md:755` | Firmware accesses establish used addresses, not the physical `0x400` extent. The implemented extent stays an assumption. |
| `ddr/phy-evidence.md:761` | Firmware accesses establish used MCCC banks, not the complete `0x800` apertures. Those extents stay assumptions. |
| `eud/evidence.md:13` | Selected XBL accesses support addresses, masks, and bounded operations. The inherited physical register names lack independent support. |
| `eud/evidence.md:261` | SM8850 writes zero to both byte addresses before EN2. That supports the write, not physical reset values or symbolic names. |
| `eud/evidence.md:317` | The selected callback opens the GUID and writes its partition header. This supports the GUID's use, not its source-level token. |
| `spmi/README.md:19` | SM8845 instructions and SM8850 execution support bit-four ownership for the selected uploader. They do not establish a universal permission table. |
| `spmi/README.md:63` | Selected firmware reads seven bytes. Linux independently supports the common type, subtype, and revision offsets. The dummy byte remains firmware-specific. |
| `spmi/README.md:412` | The common revision fields agree with Linux. The driver varies revision interpretation by subtype, so the layout is not universally interchangeable. |
| `spmi/README.md:427` | Retention at `0x39/0x3a` and `0x98/0x99` does not establish ULS or phase semantics. Independent register evidence remains necessary. |
| `spmi/README.md:583` | The hardware record establishes BOB subtype value 7 and the selected consumer. It does not establish the inherited enum name. |
| `spmi/README.md:588` | The hardware record supports numeric values. It does not establish the MISC enum name or prove that subtype `0x6b` cannot exist. |
| `spmi/README.md:593` | The selected call returns 32. Its symbolic error name lacks independent support. |
| `spmi/README.md:594` | The available record does not expose an OCP clear for every BOB slot. The general operation remains unresolved. |
| `spmi/README.md:595` | One failed run does not establish the general empty-type condition. A permitted decode of the selected call path remains necessary. |
| `spmi/README.md:596` | Selected allocation and stride support two slots. The symbolic property name remains unresolved. |
| `spmi/README.md:605` | XBL `0x14882e34` and run532 support the two-byte access and value `0xe468`. They do not establish the model's 32000-microvolt scale. |
| `spmi/README.md:635` | The hardware record supports numeric type 20. The symbolic enum remains unresolved. |
| `spmi/pmic.h:414` | The ULS and phase declarations lack independent semantic support. Their retained writable state is implementation, not an oracle. |
| `spmi/pmic.h:426` | The selected transaction supports two bytes at `0x68/0x69`. The BOB voltage conversion stays an implemented assumption. |
| `rpmh/evidence.md:263` | Selected UEFI reads establish consumed fields and masks. They do not establish physical discovery values. |
| `rpmh/evidence.md:607` | DisplayDxe reads the word and checks bit 12. It does not establish the physical name or zero reset value. |
| `rpmh/rscc.h:18` | Selected UEFI accesses establish offsets and masks. The four identification and configuration responses lack independent support. |
| `rpmh/pcie_rscc.h:17` | Selected UEFI accesses establish offsets and masks. The corresponding PCIe identification and configuration responses remain unresolved. |
| `tsens/README.md:52` | Linux TSENS v2 defines software reset at control bit 1 and implements its assertion and release. |
| `tsens/README.md:120` | The selected firmware sets bit 22. Linux does not establish its auto-adjust meaning. The implemented timing behavior remains unresolved. |
| `tsens/README.md:171` | Linux supports common v2 control, period, threshold, status, and TRDY offsets. It does not establish the TBCB, watchdog, or legacy groups. |
| `tsens/README.md:183` | Selected stores and reads do not establish watchdog tick units or timeout behavior. The model depends on these unresolved assumptions. |
| `tsens/README.md:185` | Selected access does not establish the trigger and disable-selector meanings. The modeled fields need independent definitions. |
| `ufs/README.md:308` | Canoe DT identifies HCI at `0x1d84000`. Selected firmware supports the wrapper-base convention at `0x1d80000`. Keep those coordinates distinct. |
| `ufs/README.md:309` | Canoe DT declares a `0x3000` HCI region. Selected ROM addresses support the `0x4000` offset from the stated wrapper base. |
| `ufs/README.md:310` | Selected FDTs report a 38.4 MHz controller-group reference. Canoe separately identifies a 19.2 MHz device reference pin. These are different clock roles. |
| `ufs/README.md:313` | Linux defines SYS1CLK_1US at `0xc0`. The complete `0x7ff` mask lacks independent support and stays an implementation assumption. |
| `ufs/README.md:314` | Linux defines CFG1 at `0xdc` and PHY reset bit 1. OnePlus uses reference-clock bit 26. Warm DME reset bit 28 remains unresolved. |
| `ufs/README.md:316` | Public headers define HW_VERSION at `0xe4`. They define major bits 31:28, minor bits 27:16, and step bits 15:0. |
| `ufs/README.md:317` | Public headers define DEBUG_SPARE_CFG at `0x284c`. They do not establish one full-width SPARE field. That broader meaning remains unresolved. |
| `ufs/README.md:318` | The OnePlus driver reads version data from DEBUG_SPARE_CFG for major versions at least five. This establishes the selected driver path only. |
| `ufs/README.md:367` | Linux UniPro defines PA_HSSERIES `0x156a`, gear, termination, power mode, and related attributes. Selected firmware provides target-specific use. |
| `ufs/README.md:614` | Selected XBL keeps bit 28 set. It neither toggles the bit nor establishes warm-reset semantics. The implemented behavior remains unresolved. |
| `ufs/README.md:759` | The captured CDB establishes TEST UNIT READY for that request. It does not establish a universal error-to-command mapping. |
| `smmu/evidence.md:548` | Public EDK II calls `CoreAllEfiServicesAvailable()` when DEPEX is absent. Selected-firmware equivalence needs its own comparison. |
| `soccp/evidence.md:518` | Selected XBL tables and consumers support the decoded boot-record fields. They do not establish a general Qualcomm ABI. |
| `llcc/evidence.md:218` | The selected additional-region table establishes region six at relative slot zero. Keep the claim specific to that firmware layout. |
| `llcc/evidence.md:238` | Selected TME writes and reads the lock word. That does not establish irreversible locking, authorization, write restrictions, or reset-domain semantics. |

The inventory contains no UART row.
The reviewed UART evidence cites private research records and selected firmware inputs.
That observation does not certify every UART claim outside this inventory.

## Unresolved evidence and interpretation questions

Working model behavior must not be removed merely to satisfy a text search.
These questions concern physical accuracy and the recorded scope of each assumption.
Each item needs an evidence record, or a separately justified replacement design:

1. SPMI ULS and phase semantics, BOB scaling, and unsupported enum or error claims.
2. TSENS bit-22 timing, watchdog units, and TBCB or legacy field semantics.
3. Camera and PCIe RSCC identification and configuration responses.
4. UFS SYS1CLK mask, bit-28 reset behavior, and the full-width DEBUG_SPARE interpretation.
5. DDR block identities and complete physical extents.
6. LLCC irreversible-lock and authorization behavior beyond the observed transaction.
7. EUD physical reset and symbolic register claims where implementation depends on them.

Calling inherited values or behavior virtual policy does not answer these questions.
Shared documentation and platform declarations inherit the same unresolved status.
The UFS controller reference and the device reference pin remain separate claims.

# TME and boot provenance review

This review covers the entries in `artifacts:2026-10-28-source-only-facts/source-only-tme.md sha256=73c7ce0989025d7355642382c46b550d28cb5b7e23184d6a08f30cab312e159e`.
The pinned hash identifies the reviewed inventory.
It covers `tme_mailbox`, `tme_runtime_abi`, `tme_protection`, `boot_control`, `apss_boot`, `fuse_controller`, `rng`, and `gpce`.
It records one disposition for each inventory entry.
Companion reviews: [processors and clocks](provenance-processors.md), [peripherals](provenance-peripherals.md), [shared composition](provenance-shared.md).

## Method

The inventory lists facts that came from a licensed vendor tree.
The inventory is a list of leads, not evidence.
Prior evidence pages, accumulated research, and device probes supplied the substantive basis.
For each entry this review did four steps.

1. Read the surrounding evidence record.
2. Record what selected firmware, permitted public source, or retained execution record establishes.
3. Keep the fact when a permitted source supports it.
4. Mark the fact as unestablished when no permitted source supports it.

It also records whether the model depends on the unestablished part.
An unestablished name with no consumer is a documentation defect.
An unestablished value that changes model behavior is an open evidence question with a stated limit.

No vendor source was consulted for this review.
`BOOT.MXF` was not opened.
No firmware bytes, certificates, or keys were added to the repository.

## Disposition labels

| Label | Meaning |
| --- | --- |
| Firmware | Selected firmware instructions or data establish the fact. The evidence record cites the image and the offset. |
| Public | A permitted public source establishes the fact. The citation gives revision and path. |
| Declared | The value is an explicit model or board declaration. A member records it as declared, not measured. |
| Label only | The token is a descriptive name. No source establishes the name, and no behavior depends on it. |
| Blocker | Model behavior depends on the fact, and no recorded evidence establishes it. The row states the open interpretation question and the evidence that would settle it. |
| Declared lineage | A value follows a recorded manufacturer or family catalog pattern. The record names the pattern, its basis, and its applicability limit. |

## Public sources used

| Source | Revision | License identifier | Facts used |
| --- | --- | --- | --- |
| [Qualcomm security profiles](https://github.com/qualcomm/security-profiles/blob/a69b6decf24456fb622de05ecb9b74e0bf871bee/nord_security_profile.xml) | `a69b6decf24456fb622de05ecb9b74e0bf871bee` | `LICENSE.txt` in that repository | Image software IDs and authenticators. `OEM_SECURITY_POLICY` bits. `ANTI_ROLLBACK_FEATURE_EN` bits. Dump-policy vector widths. |
| [Qualcomm TF-A](https://github.com/qualcomm-linux/trusted-firmware-a/blob/f7787c3627e200933f02b93b1c25b8eb5bcf797d/drivers/qti/accesscontrol/xpu/xpu3.c) | `f7787c3627e200933f02b93b1c25b8eb5bcf797d` | BSD-3-Clause | XPU region-count field `IDR0[25:16]`. Domain permission mask form. |
| [securemsm `IAppLoader.h`](https://android.googlesource.com/kernel/msm-modules/securemsm/+/8c28e44bf54c65f6190dafeb6fef47a2a3e49092/include/smci/interface/IAppLoader.h) | `8c28e44bf54c65f6190dafeb6fef47a2a3e49092` | GPL-2.0-only | Loader result 12 name. `loadFromBuffer` count `0x1001`. |
| [securemsm `IQSEEComCompatAppLoader.h`](https://android.googlesource.com/kernel/msm-modules/securemsm/+/8c28e44bf54c65f6190dafeb6fef47a2a3e49092/smcinvoke/IQSEEComCompatAppLoader.h) | `8c28e44bf54c65f6190dafeb6fef47a2a3e49092` | GPL-2.0-only | Compatibility loader result 12 name. `loadFromBuffer` count `0x1012`. |
| [Linux `qcom-wdt.c`](https://github.com/torvalds/linux/blob/v6.12/drivers/watchdog/qcom-wdt.c) | `v6.12` | GPL-2.0-only | KPSS watchdog offsets `0x4`, `0x8`, `0xc`, `0x10`, and `0x14`. |

The repository cites these sources by URL and revision.
It copies no source text from them.

## `models/qcom/sm88xx/tme_mailbox`

| Inventory entry | Claim | Disposition | Support or remaining gap |
| --- | --- | --- | --- |
| evidence.md:301 | Software ID `0x37` is a TME sequencer ID | Firmware and label | The signed input selects `0x37` at offset `0x00c`. The name is a label only. |
| evidence.md:647 | Failure `0xf025` is a DBGAR error | Blocker for the name | The observation is that a debug-address command can fail. The error dictionary does not establish the name. |
| evidence.md:729 | Event `0x301e` is a transport-key broadcast | Label only | The name is a label only. The command inference rests on the public API and the GPCE window sequence. |
| evidence.md:754 | Register index `0xf` selects `+0x2c` | Firmware and label | The accessor table at `0x2326f0` maps the index. The field name is a label. |
| evidence.md:787 | Event `0x301a` is a key-wrap event with three fields | Firmware and label | The submit body writes the three values through known indices. The event name is a label. |
| evidence.md:1045 | Page offset `0x24` has mask `0xf` and four override bits | Blocker | QHEE writes `0xf` once. Neither the mask nor the four field names have a permitted source. |
| evidence.md:1121 | The base word selects secure or nonsecure mode | Blocker | The QSEE table writes one. The polarity and the field name have no permitted source. |
| evidence.md:2197 | `0x01fb300c` is a PRNG interrupt enable | Blocker for the name | QSEE tests bit zero and writes `0x06800040`. The name has no permitted source. |
| evidence.md:2228 | Key-generation events carry five named fields | Firmware and label | The public `hwkm` request structure supports the field order. The event names are labels. |
| watchdog.h:122 | The override word is write-only | Blocker | No consumer reads it in the decoded set. That bounds the risk but does not establish the access type. |
| watchdog.h:189 | Bark is at `0x10` and bite at `0x14` | Public | The Linux KPSS layout matches. The device-tree node at `0x17600000` selects that layout. |

## `models/qcom/sm88xx/tme_runtime_abi`

| Inventory entry | Claim | Disposition | Support or remaining gap |
| --- | --- | --- | --- |
| sm8845-evidence.md:134 | A bit-zero request selects a PLL reference | Firmware and label | Instructions `0x1484d3d4..0x1484d3d8` set and retain bit zero. The field name is a label. |
| sm8845-evidence.md:135 | AOP PLL1 feeds AOP PLL0 | Declared | The board declares the reference. No record of the physical netlist exists. |
| sm8845-evidence.md:202 | REFGEN blocks hold three named controls | Firmware and label | The offsets, values, and poll are established. The field names are labels. |
| sm8845-evidence.md:396 | A wrapper writes three named fields | Firmware and label | The instruction sequence is established. The function and field names are labels. |
| sm8845-evidence.md:475 | Mask `0x61` marks a configuration mode | Firmware and label | Bits five and six write the even and odd enables. The bit-zero meaning is unestablished. |
| sm8845-evidence.md:2702 | Dump policy has presence bytes and target words | Blocker | The public profile defines vector widths. It does not define this private context layout. |
| sm8845-evidence.md:3479 | Event `0x301e` name | Label only | The name is a label. The command mapping stays an inference. |
| sm8845-evidence.md:3555 | Event `0x301a` name | Label only | The name is a label. The register writes establish the fields. |

## `models/qcom/sm88xx/tme_protection`

| Inventory entry | Claim | Disposition | Support or remaining gap |
| --- | --- | --- | --- |
| evidence.md:13 | A comparable block defines the identification fields | Public and firmware | Public XPU3 encodes the region count minus one in `IDR0[25:16]`. The TME offsets come from the ROM. |
| evidence.md:64 | The TCSR client aperture is `0x01f00000` with size `0x100000` | Declared and firmware | Selected consumers read `0x01fc8000` and `0x01fc8004`. The base and size are platform declarations. |
| evidence.md:70 | The RPMH MPU sits at AOSS offset `0x01292000` | Blocker for the values | The platform declares the AOSS base, size, and offset. This review found no independent record. |
| evidence.md:210 | Bit zero selects APNSPE and bit one controls clocking | Blocker for the names | The OEM branch reads bit zero before region programming. The names have no permitted source. |
| evidence.md:683 | Reporting controls sit at `0x208` and `0x20c` | Firmware and blocker | The `0x20c` mask `0xc00007ff` is established. The names and the `0x208` pairing are unestablished. |

## `models/qcom/sm88xx/boot_control`

| Inventory entry | Claim | Disposition | Support or remaining gap |
| --- | --- | --- | --- |
| evidence.md:160 | Error `0x16004` is a sleep-init vector failure | Firmware and inference | The OEM writes the exit vector, reads it back, and reports this error. The name is an inference. |
| evidence.md:322 | The TCSR base composes to `0x01f00000` | Blocker | The ROM establishes the TME address. The base decomposition has no permitted source. |
| evidence.md:323 | `TCSR_ACXPU4_DCR` holds `SLE` in bits `3:0` | Blocker | The ApSS alias at `0x01fd0000` and the field encoding are unestablished. |
| evidence.md:445 | `0x0c2f0000` is a debug-enable word | Blocker for the name | QSEE reads the address. The field meanings of bits six, five, and four are unestablished. |
| tme_crashdump_evidence.md:13 | Dump options use bits zero, one, and two | Firmware | The XBL consumers distinguish flags one, two, and four. |
| tme_crashdump_evidence.md:15 | A global vector query names the options | Label only | The name is a label. The consumers establish the query. |
| tme_crashdump_evidence.md:83 | Offset `0x2000` is a boot-stage vector | Label only | The consumer reads bits 2:0. The name is a label and supports no readiness claim. |

## `models/qcom/sm88xx/apss_boot`

| Inventory entry | Claim | Disposition | Support or remaining gap |
| --- | --- | --- | --- |
| boot_info_evidence.md:10 | Boot information composes from the TCSR base | Firmware and label | The accessor at `0x30d828` materializes `0x01ff0000`. The base composition is unestablished. |
| boot_info_evidence.md:11 | The word takes full and masked writes | Declared | The ROM performs reads only. The model declares the wider access. |
| boot_info_evidence.md:93 | Software IDs `0x7b`, `0x35`, and `0x36` | Public | The security profile names BOOT-BLOCK, TME-FW, and XBL-SC with those IDs. |
| evidence.md:235 | The APSS block sits at `APSS_CSR_NSEC + 0x1000` | Firmware and label | CPUCP reads `0x17651000` at offsets `0x0c`, `0x18`, `0x20`, and `0x24`. The base expression is unestablished. |
| evidence.md:333 | Timer interrupt `0x28` is INTID 40 | Public | The device tree gives physical SPI 8. The architectural offset is 40. |
| evidence.md:430 | The timer frame has a fixed register layout | Firmware and declared | The ROM establishes `+0x28` and `+0x2c` and control bits zero, one, and two. The other offsets are declared. |
| evidence.md:438 | A rollover-safe read samples HI, LO, HI | Declared | This is a model property. No hardware requirement is claimed. |
| evidence.md:441 | The analogue block starts at `0x16802000` | Blocker for the value | The platform declares the base. This review found no independent record. |
| evidence.md:550 | TCSR offsets `0x88000` and `0x88004` hold identity | Firmware and label | The ROM reads `0x01fc8004`. The TME view is `0x05fc8000`. The offset decomposition is unestablished. |
| evidence.md:573 | `SOC_HW_VERSION` fields | Firmware | The TME helper masks family `31:28` and device `27:16`. The minor fields follow the XBL decode. |
| evidence.md:616 | The sleep counter runs at 32,768 Hz | Firmware and declared | XBL reads `0x0c221000`. The rate is a declaration. |
| evidence.md:633 | `0x0c2a9010` reports debug power | Firmware and label | XBL checks bit 31. Bit zero and the register name are unestablished. |
| boot_identity.h:14 | The identity block layout | Firmware | The CPUCP consumers read the five offsets. The names are labels. |

## `models/qcom/sm88xx/fuse_controller`

| Inventory entry | Claim | Disposition | Support or remaining gap |
| --- | --- | --- | --- |
| evidence.md:16 | The register names and offsets are layout facts | Firmware and label | The consumers establish the offsets. The names are labels unless a public source names them. |
| evidence.md:21 | Offset `0x10` holds 32 error bits | Firmware and label | QSEE uses bit 28 as the pass indication. The other bits are unestablished. |
| evidence.md:115 | The image vector has a full-width data field | Declared | The model retains the write. The consumer at `0x205b94` gates the update. |
| evidence.md:122 | The range bases are `TMESS_BASE + 0x021c8000` and `+0x1000` | Firmware | The descriptor table selects `0x02309054` and the ROM adds `0x140000`. |
| evidence.md:176 | Offset `0x10` is the BIST error word | Firmware and label | Same as entry 21. |
| evidence.md:260 | Offset `0x604` is the secure-boot word | Firmware | The APSS dispatcher reads the word at `0x30cf18`. |
| evidence.md:265 | The secure-boot field positions | Firmware and blocker | Bits 5, 4, and 3:0 have consumers. The positions of bits 8 and 6 are unestablished and change the model value. |
| evidence.md:284 | `QC_CONFIG0` bit 10 enables CPU bypass | Firmware and label | The dispatcher extracts bit 10 at `0x30d3a8`. The name is a label. |
| evidence.md:289 | `QC_CONFIG5` bits 6:4 hold drive strength | Firmware and label | The dispatcher extracts bits 6:4 at `0x30d080`. |
| evidence.md:302 | Image `0x7b` is BOOT-BLOCK, authenticated by TME-PBL | Public | The security profile gives that ID and authenticator. |
| evidence.md:318 | `FEATURE_CONFIG11` is APSS spare fuses | Firmware and label | CPUCP selector `0x88` reads offset `0x12c`. The spare label is unestablished. |
| evidence.md:334 | Five policy fields select authentication behavior | Firmware and label | The TME consumers read each word. The field names are labels. |
| evidence.md:543 | `OEM_CONFIG5` field split | Public and blocker | The low byte and its bit names are public. The bits 31:9 field and the bit-8 meaning are unestablished. |
| evidence.md:551 | Rollback bits and the recommended `0x0f` | Public | The security profile gives the same names and the same recommendation. |
| evidence.md:601 | Debug vector one fields | Firmware and label | The masks establish bits 18, 5, and 3. The names are labels. |
| evidence.md:643 | Four policy registers with one-bit fields | Public and firmware | The security profile gives bits 1, 4, 5, and 11. The OEM helper reads each word. |
| evidence.md:681 | The profile recommends OEM authorization | Public | The security profile recommends each control and states the activation requirement. |
| evidence.md:900 | Calibration address ranges | Firmware and label | The selected XBL reads the listed addresses. The labels are unestablished. |
| evidence.md:902 | Offset `0x550` is a spare row word | Label only | The name is a label. The selected instructions establish the masks. |
| evidence.md:1219 | The CPR step counter starts at `+0x32d4` | Firmware | UEFI CPRDxe writes the entry, and run768 recorded the range. |
| evidence.md:1278 | The QFPROM aperture is `0x021c2000` and `0xec` holds two candidate names | Firmware and label | The CPUCP read at `0x221c20ec` is established. The bit names conflict in the vendor definitions. |
| evidence.md:1285 | Debug vector zero sits at software offset `0x780` | Firmware and label | CPUCP reads `0x221c8780` bit 11. The name is a label. |
| evidence.md:1355 | Offset `0x2090` is a ROM error word | Firmware and label | QSEE extracts bits 17:16. Bit zero and the name are unestablished. |
| fuse_controller.h:20 | Offset `0x73c` holds lifecycle encodings | Firmware and label | TME `0x20d06e` reads the word. The encodings are unestablished. |
| fuse_controller.h:372 | The self-test control uses mask `0x1` and START at bit zero | Blocker | The OEM writes the word. The mask and the bit meaning have no permitted source. |
| fuse_controller.h:405 | Offset `0x10` is the BIST error word | Firmware and label | Same as entry 21. |

## `models/qcom/rng`

| Inventory entry | Claim | Disposition | Support or remaining gap |
| --- | --- | --- | --- |
| evidence.md:19 | The generator aperture is `0x010c0000` | Firmware | The PBL sequence, XBL `0x010c3004`, and QHEE `0x010cc004` establish the aperture. |

## `models/qcom/gpce`

| Inventory entry | Claim | Disposition | Support or remaining gap |
| --- | --- | --- | --- |
| evidence.md:565 | The key manager sits at `01df0000` in a `0x9800` window | Firmware and blocker | QSEE reads seven words in that aperture. The `+0x30000` composition and the size are unestablished. |
| evidence.md:576 | The identity registers reset to `0x02020000` and `4` | Blocker | QSEE reads and stores both words. No permitted source establishes the values. |
| qce_v6.h:214 | The shared window spans `01df0000..01df0fff` | Firmware and label | The QSEE accesses establish the used words. The group split is unestablished. |
| qce_v6.h:236 | The control word resets to `7` | Blocker for the reset value | QSEE clears the CRC bit before first use. The reset value is unestablished. |
| evidence.md:781 | A reference-device boot-log link | Public | The link is a device boot log. It is not licensed source code. |
| evidence.md:799 | `IAppLoader.h` result 12 is a signature error | Public | Two public headers name result 12. The `0x1012` count matches the compatibility loader. The remote object identity is unestablished. |

## Unresolved evidence and interpretation questions

These items change model behavior, and no recorded evidence establishes them.
Each line names the model symbol that depends on the fact and the evidence that would settle the question.
The list records open research, not a blanket prohibition caused by citation removal.
A recorded family pattern can support a value when the record states the basis and the limit.

| Model | Symbol or parameter | Missing prerequisite |
| --- | --- | --- |
| `tme_mailbox` | `WDOG_CHICKEN_BITS` mask and read rejection in `watchdog.h` | A permitted record of offset `0x24` access type and field positions. |
| `tme_mailbox` | Bank mode word and its polarity in `watchdog.h` | A permitted record of the security meaning of the mode word. |
| `tme_mailbox` | `0xf025` and `0x301e` and `0x301a` dictionary names | A permitted error and event dictionary. The model behavior does not depend on them. |
| `tme_runtime_abi` | Private dump-context presence bytes | A permitted record of the context layout. |
| `tme_protection` | AOSS and RPMH MPU aperture values | A permitted record of the AOSS base, size, and MPU offset. |
| `tme_protection` | `GCR` bit names and reporting-control names | A permitted XPU definition for this block. |
| `boot_control` | TCSR base decomposition and `TCSR_ACXPU4_DCR` field | A permitted TCSR map for this SoC. |
| `boot_control` | Debug-enable word field meanings | A permitted record of bits six through four. |
| `apss_boot` | Analogue timer base `0x16802000` | A permitted record of that block. |
| `fuse_controller` | Secure-boot bits 8 and 6 | A permitted secure-boot fuse definition. |
| `fuse_controller` | Self-test control mask `0x1` | A permitted record of the control word. |
| `fuse_controller` | `OEM_CONFIG5` bits 31:9 and bit 8 | A permitted OEM configuration definition. |
| `gpce` | Key-manager identity values and control reset | A permitted key-manager register definition. |

## Model changes in this review

Only evidence records, header comments, and this document changed.
No model behavior changed.
No register, mask, parameter, default, or access rule changed.
No vendor bytes, keys, or certificates were added.

| File | Change |
| --- | --- |
| `models/qcom/sm88xx/tme_mailbox/evidence.md` | Replaced unestablished dictionary names with bounded statements. |
| `models/qcom/sm88xx/tme_mailbox/watchdog.h` | Replaced two unsupported comments with the retained observation and the public offset citation. |
| `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md` | Bounded the PLL, REFGEN, wrapper, mask, and dump-layout claims. |
| `models/qcom/sm88xx/tme_protection/evidence.md` | Added the public XPU3 citation. Marked declared apertures. Marked control names. |
| `models/qcom/sm88xx/tme_protection/tme_protection.h` | Replaced three unsupported control and debug-address comments. |
| `models/qcom/sm88xx/boot_control/evidence.md` | Bounded the error name, the TCSR decomposition, and the debug word. |
| `models/qcom/sm88xx/boot_control/tme_crashdump_evidence.md` | Bounded the option flags and the boot-stage name. |
| `models/qcom/sm88xx/apss_boot/evidence.md` | Bounded the identity, timer, sleep-counter, and debug-power claims. |
| `models/qcom/sm88xx/apss_boot/boot_info_evidence.md` | Added the public software-ID citation. Marked declared field positions. |
| `models/qcom/sm88xx/apss_boot/boot_identity.h` | Replaced the unsupported base composition in the comment. |
| `models/qcom/sm88xx/fuse_controller/evidence.md` | Added public policy citations. Marked names as labels. Marked two field blockers. |
| `models/qcom/sm88xx/fuse_controller/fuse_controller.h` | Replaced three unsupported comments. |
| `models/qcom/rng/evidence.md` | Replaced the base name claim with the firmware addresses. |
| `models/qcom/gpce/evidence.md` | Added the two public loader headers. Marked the key-manager declarations. |
| `models/qcom/gpce/qce_v6.h` | Replaced two unsupported reset comments. |

## What this review does not establish

This review does not establish that any model matches silicon.
It does not qualify authentication, authorization, or kernel handoff.
It does not close the open questions above.
A retained fact here means that a recorded observation or an inference with a stated basis supports the statement.
It does not mean that the model behavior is complete or verified.

## Verification

Suggested sequence for an integration check:

1. Run the repository text check on the edited documents and obtain zero violations.
2. Build the registered models that include `watchdog.h`, `fuse_controller.h`, `qce_v6.h`, and `boot_identity.h`.
3. Run the existing native fixtures for `tme-warmup`, the fuse controller, the protection controller, and the GPCE wrap path.
4. Check that the run logs still show the same boundaries. No behavior changed, so no new boundary should appear.
5. Read the open-question table before any claim that depends on these entries, and record the evidence for each line.

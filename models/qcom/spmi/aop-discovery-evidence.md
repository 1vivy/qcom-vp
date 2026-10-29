# AOP PMIC revision audit

## Sources and scope

This audit uses the selected CPH2771 SM8845 and CPH2749 SM8850 packages.
The image hashes and loaded discovery spans appear in [RPMh evidence](../rpmh/evidence.md#selected-configuration-sources).
The analysis reads the original ELF segments and evaluates the existing Lua inventories.
It does not execute an OEM run or change firmware bytes.

The guest PPID programming capture at AOP admission:

| SoC | Capture | Rows | SHA-256 of capture text |
| --- | --- | --- | --- |
| SM8845 | Run172 table | 798 | `3398ad723c0409bb66722e6f03908a494d14a6f60b0a510ed0e088d6055764dc` |
| SM8850 | Native table | 818 | `96b9b528c56a8dc0e2fb03abddf3950eeef08b26062ed38a8631fab51a896bf2` |

The capture text files are no longer available.
Their SHA-256 values identify the tables, but no check can repeat them.

Each capture index addresses the service's compact array, not a global APID.
Indices below `0x4d4` belong to bus zero. Later indices belong to bus one.
The global APID for those later indices is `0x800 + index - 0x4d4`.
Bits `12:8` supply SID. Bits `7:0` supply peripheral. Bit 31 does not change the PPID.
The audit excludes PPIDs with bit 12 set, as the native translation does.
Duplicate PPID entries count once in the tables below.

The identity spans select physical SIDs and config slots before the mask intersection.
A secondary SID does not become another primary config slot.
The tables enumerate the complete mask-and-route intersection, including reads after an earlier failed revision.
They do not claim that the failed run executed every later read.

| SoC | Mask-selected slots | Mapped slots | Declared revision | Undeclared | Partial revision gap | Mask-selected but unmapped |
| --- | --- | --- | --- | --- | --- | --- |
| SM8845 | 832 | 621 | 594 | 23 | 4 | 211 |
| SM8850 | 1264 | 712 | 712 | 0 | 0 | 552 |

A declared revision includes explicit absent records. It does not establish a real silicon revision.
Of the mapped declared revisions, SM8845 has 399 explicit absent records and 195 other records.
SM8850 has 427 explicit absent records and 285 other records.
Undeclared peripherals use the existing virtual absence policy only if no exploratory write occurred.
A partial record retains its real model error. The audit does not replace that error with invented revision bytes.

## Complete mapped inventories

All peripheral numbers below are hexadecimal. Ranges include both ends.
`R` means an existing seven-byte revision record, including explicit absence.
`U` means an undeclared peripheral. `G` means declared controls without a complete revision record.
All omitted class cells are empty.

### SM8845

| Bus/SID | R peripherals | U peripherals | G peripherals |
| --- | --- | --- | --- |
| 0/0 | `01,04–09,0b,0d,12–13,15–29,2b–2e,36,50–51,54–5c,5e,60–64,67–bd,e8–e9,fe` | — | — |
| 0/1 | `01,04–06,08–0b,0d–0f,13–15,1c–1d,36,39,3c,3e,40–41,47–4a,51,5e,88–93,98–99,c1–d1,e1–e2,e4–eb,ee–ef,fe–ff` | — | — |
| 0/3 | `01,04–06,08–0b,0e–0f,1c–1d,36,39,3c,51,5e,88–8f,99–b2,c1–c3,e1,fe–ff` | — | — |
| 0/4 | `01,04–06,08–0b,0e,1c–1d,4b–4d,51,5e,88–8b,99,f8–fa,fe–ff` | — | — |
| 0/5 | `01,04–06,08–0b,0e–0f,1c–1d,36,39,3c,51,5e,88–8f,99–b2,c1–c3,e1,fe–ff` | — | — |
| 0/6 | `01,04–06,08–0b,0e–0f,1c–1d,36,39,3c,51,5e,88–8f,99–b2,c1–c3,e1,fe–ff` | — | — |
| 0/7 | `01,04–06,08–0b,0e–0f,14,16–19,1c–1f,21,26–29,2b–2e,30,36,39–3a,3c–3d,47–48,4e–4f,51,5e,68,70–73,88–8f,df,f8–fa,fd–fe` | `0c,13,20,22–25,2a,3e,4a,90–99,f5–f7` | `49,f0–f2` |
| 0/8 | `01,04–06,08–0b,0e–0f,1c–1d,36,39,3c,51,5e,88–8f,99–b2,c1–c3,e1,fe–ff` | — | — |
| 0/10 | `01,04–06,08–0b,0e,1c–1d,51,5e,88–89,99,c1–c7,e1,fe–ff` | — | — |
| 0/12 | `01,04–07,09,24,2c,40–46,51,c0–c1,e1` | — | — |
| 0/13 | `01,04–07,09,24,2c,40–46,51,c0–c1,e1` | — | — |

SIDs 2, 9, and 11 have no populated identity. Bus one has no populated identity.
The model `0x4c` pointer at `0x0b092860 + 0x17*4` selects `0x81c3b13a`.
That mask selects peripherals `01..ff`, unlike the narrower XBL mask at `0x148f1a5c`.
This difference explains all four partial revision gaps.

### SM8850

| Bus/SID | R peripherals |
| --- | --- |
| 0/0 | `01,04–09,0b,0d,12–33,36–37,40–45,48–4b,50–51,54–5c,5e,60–64,67–bf,d0–d7,e8–e9,ef–fc,fe` |
| 0/1 | `01,04–0b,0d–0f,13–15,1c–1d,36,39,3c,3e,40–43,47–4e,51,5e,88–9b,c1–d2,e1–e2,e4–eb,ee–ef,fe–ff` |
| 0/3 | `01,04–0b,0e–0f,1c–1d,36,39,51,5e,88–95,99–b8,c1–c4,e1,fe–ff` |
| 0/5 | `01,04–0b,0e–0f,1c–1d,36,39,51,5e,88–95,99–b8,c1–c4,e1,fe–ff` |
| 0/6 | `01,04–0b,0e–0f,1c–1d,36,39,51,5e,88–95,99–b8,c1–c4,e1,fe–ff` |
| 0/8 | `01,04–0b,0e–0f,1c–1d,36,39,51,5e,88–95,99–b8,c1–c4,e1,fe–ff` |
| 1/7 | `01,04–06,08–0c,0e–0f,14,16–19,1c–1e,22–2d,30,36,39–3a,3c–3e,47–4a,4e–4f,51,5e,88–99,f0–f2,fd–fe` |
| 1/9 | `01,04–0b,0e–0f,1c–1d,51,5e,88–8f,99–a6,fe–ff` |
| 1/10 | `01,04–06,08–0b,0e,1c–1d,51,5e,88–89,99,c1–c7,e1,fe–ff` |
| 1/12 | `01,04–07,09,24,2c,40–46,51,c0–c1,e1` |
| 1/13 | `01,04–06,09,51` |

The intersection contains no revision reads on SIDs 0/2, 0/4, 0/7, 0/9, 1/0, or 1/11.
The identity span still controls slot allocation for those populated secondary SIDs.
The bus-one PMK primary at SID0 also has no selected route in this capture.
No SM8850 partial record requires a new declaration.

## Revision evidence gaps

The SM8845 profile declares configuration controls at these four mapped peripherals.
None has revision bytes `+0..+6` in the selected inventory.
The inspected PSI records establish configuration writes, not revision responses.
The corresponding SM8850 records belong to model `0x56`, not SM8845 model `0x4c`.
Their existing virtual absent records cannot establish PM7550BA silicon revisions.

| SM8845 bus0 SID7 peripheral | SM8845 PSI record span | SM8850 bus1 SID7 comparison span | Missing evidence |
| --- | --- | --- | --- |
| `49` | `0x148f5cf4..0x148f5e20` | `0x148face0..0x148fafec` | All seven revision bytes, including type, subtype, and dummy |
| `f0` | `0x148f63a8..0x148f64f8` | `0x148f5724..0x148fb4a8` | All seven revision bytes, including type, subtype, and dummy |
| `f1` | `0x148f6408..0x148f6438` | `0x148f5a3c..0x148fb3f4` | All seven revision bytes, including type, subtype, and dummy |
| `f2` | `0x148f666c..0x148f6780` | `0x148f5a30..0x148fb718` | All seven revision bytes, including type, subtype, and dummy |

The [public PM7550BA tree](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/pm7550ba.dtsi) identifies GPIO, temperature, and repeater blocks.
It supplies no revision bytes for these four peripherals.
The XBL regulator allocators provide capacities and register strides, not these missing revision responses.
Examples include SM8845 LDO `0x1487e0f8..0x1487e164` and SMPS `0x14882260..0x14882288`.
The corresponding SM8850 allocators are `0x1487fa7c..0x1487fb20` and `0x14882b50..0x14882bcc`.
A regulator table or a PSI configuration address alone cannot establish the missing type or subtype.
No new revision record is justified by these sources.

Both AOP configurations give slot7 zero SMPS, LDO, and BOB counts.
Those entries reside at `0x81c80028`, `0x81c80098`, and `0x81c80108`.
Thus these gaps add no configured rail or alias.
The inventory declarations remain unchanged. SM8850 remains at the observed 104 records.
The service correction below permits discovery to continue after failed revision reads.
Native SM8845 run176 then publishes the database, and its DCP passes the CESTA rail lookups.
These runs do not establish kernel boot.

## Native discovery error handling

Zero config counts do not disable the peripheral scan.
The native scan first selects an identity, communication object, mask, and mapped SID bank.
It reads the mask-selected peripherals before it requests regulator counts for a classified rail.

| Operation | SM8845 PC | SM8850 PC |
| --- | --- | --- |
| Discovery entry | `0x81c1fda6` | `0x81c201e2` |
| Identity scan | `0x0b0845e2` | `0x0b08469e` |
| Model-mask selector | `0x0b084752` | `0x0b08480e` |
| Clear all seven revision bytes | `0x81c1ff48..0x81c1ff50` | `0x81c20384..0x81c2038c` |
| Set seven-byte count and call read | `0x81c1ff66..0x81c1ff70` | `0x81c203a2..0x81c203ac` |
| Test subtype/dummy and type, without testing read status | `0x81c1ff74..0x81c1ff84` | `0x81c203b0..0x81c203c0` |
| Read adapter returns zero or `0x80` | `0x0b083fcc..0x0b084012` | `0x0b084088..0x0b0840ce` |
| Check transport completion errors before copying read data | `0x0b084a9a..0x0b084ab6` | `0x0b084b56..0x0b084b72` |
| Reject classified LDO count zero | `0x81c20018` | `0x81c20454` |
| Reject LDO index at or above count | `0x81c2005e` | `0x81c2049a` |
| Reject SMPS count zero / excessive index | `0x81c205f0` / `0x81c20638` | `0x81c20a2c` / `0x81c20a74` |
| Reject BOB count zero / excessive index | `0x81c20378` / `0x81c203b4` | `0x81c207b4` / `0x81c207f0` |
| Common discovery fatal branch | `0x81c209a2` | `0x81c20dde` |
| Fatal handler, through the low-address alias | `0x0b0836fc` | `0x0b0837bc` |

The read adapter reports failure, but the revision caller ignores that result.
A failed seven-byte transaction leaves the cleared buffer unchanged.
The subtype/dummy equality check then skips that peripheral.
The native code does not convert the transport error into a successful PMIC revision.
A classified regulator with an invalid count still enters the fatal path.

The service now retains `AOP_PMIC_REVISION_READ_FAILED` and continues with the next peripheral.
It does not insert a revision, change PMIC status, skip the entire slot, or suppress a model diagnostic.
It still rejects missing enabled rails, invalid classified counts, and invalid source hashes.

The focused fixture adds a failed revision on a zero-count slot and requires successful publication of the other rails.
It also requires the exact seven-byte command on that slot.
Separate cases reject a successful classified rail on a zero-count slot and an excessive LDO index.
The existing failed-enabled-rail case still requires publication failure.
The integration suite passes these cases in `aop_resources_follow_discovered_rails_and_reject_failed_sources`.

# Processor and clock provenance review

Status: review of the `models/qcom/sm88xx/cpucp`, `models/qcom/sm88xx/pdp`, `models/qcom/aop`, and `models/qcom/cpr` slice.
Lead list: `artifacts:2026-10-28-source-only-facts/source-only-cpucp.md sha256=9c14cd97411a919d7c3cca3f472708b5b12626b775b280410cf1bb1bb7f4f438`.
The lead list holds 94 entries, not 78. Every entry has a row below.

This page records one disposition for each lead entry.
It does not replace the evidence pages, and it does not claim that every model is now qualified.
Companion reviews: [peripherals](provenance-peripherals.md), [TME and boot](provenance-tme.md), [shared composition](provenance-shared.md).

The lead list is a historical citation inventory, not proof of implementation derivation.
Prior evidence pages, accumulated research, and device probes supplied the substantive basis.
The rows retain useful research questions without certifying every physical interpretation.
Manufacturer and family catalog patterns can guide implementation as recorded inferences.
Record their applicability and limits without requiring an exact IP-version identification first.
Do not include nonredistributable source or binary material.
This review records evidence gaps. It does not block work outside the affected claims.

## Dispositions

Each row uses one of these words.

| Word | Meaning |
| --- | --- |
| FW | A selected firmware image establishes the address, access, value, or sequence. |
| PUB | A public licensed source establishes the definition. |
| DERIVED | The fact follows from FW or PUB facts plus repository arithmetic. |
| LABEL | A descriptive or inherited name whose physical interpretation remains unverified. The row states the behavioral basis separately. |
| POLICY | A recorded model initial state or substitute with a stated limit. This label does not establish why an inherited constant was selected. |
| BLOCKER | Historical audit label for an unresolved evidence or interpretation question, not an automatic prohibition for other work. |
| REMOVED | The unsupported prose is gone. The model behavior is unchanged. |

## Evidence references

| Id | Reference |
| --- | --- |
| CPUCP | `edl-re:CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES/cpucp.elf sha256=3475ceab499e02fac22e249e7249ea9ee92c0eb21a260eb9b6f18d185ab70e24` |
| PDP | `edl-re:CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES/pdp.elf sha256=74fb165a6619f2451b56360eddd2628827095497593474089316056de8437e8d` |
| UEFI | `edl-re:CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES/uefi.elf sha256=da8ad1b0ffc2eff91d7d34d8574a682b7007e0b052439fbbcde989d5393071f5` |
| XBL | `edl-re:CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES/xbl_sc.elf sha256=b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a` |
| TZ | `edl-re:CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES/tz.mbn sha256=afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932` |
| ROM | `edl-re:pbl8850.bin sha256=91bd8f81efa7a5940d4b6d4c220388b559ceef39b6b47dd7b4b0727a1beb1d1c` |
| GLINK | GlinkDxe PE `sha256=acbc4086e7d95f0ed938e91a1ec3c8f14e369243ed15cf29a337ecbdfff7725a`, recorded in `models/qcom/soccp/evidence.md` |
| RCG2 | `torvalds/linux@v6.12:drivers/clk/qcom/clk-rcg2.c` |
| DIV | `torvalds/linux@v6.12:drivers/clk/clk-divider.c` |
| GCC8850 | `OnePlusOSS/android_kernel_oneplus_sm8850@fc30e54174d254ff7f33622a9278e4435f6718d2:drivers/clk/qcom/gcc-canoe.c` |
| GPUCC8850 | `OnePlusOSS/android_kernel_oneplus_sm8850@fc30e54174d254ff7f33622a9278e4435f6718d2:drivers/clk/qcom/gpucc-canoe.c` |
| CPR3 | `https://android.googlesource.com/kernel/msm/+/6ef255005ceaae118d35fba8585de7ba1d4d5efb/drivers/regulator/cpr3-regulator.c` |
| MPAM | Arm A-profile Architecture Registers (DDI0601): `MPAMCFG_PART_SEL` 0x0100, `MPAMCFG_CMAX` 0x0108, `MPAMCFG_PRI` 0x0400, `MSMON_CFG_MON_SEL` 0x0800 |

The earlier audit attributed some definitions to a private register layout without recording its origin.
That attribution alone does not establish derivation from the removed references.
The rows retain the unresolved meanings and values for research.

## models/qcom/sm88xx/cpucp

| # | Fact | Disposition | Support or unresolved prerequisite |
| --- | --- | --- | --- |
| 1 | `clock_roots_evidence.md:25` RCG control bits | PUB, FW | RCG2 lines 25, 26, 31, 35, 36, and 37. XBL writes agree. |
| 2 | `:26` HID division factor | PUB, FW | RCG2 line 162 `mult_frac(rate, 2, hid_div + 1)`. XBL HID writes agree. |
| 3 | `:27` four-bit divider decodes as value plus one | PUB | DIV line 102 `return val + 1`. |
| 4 | `:374` vote bit 22 of `0x151010` | LABEL, POLICY | The word and mask are XBL OR operations. The field name is unverified. The page now declares virtual wiring. |
| 5 | `:375` vote bit 9 of `0x151008` | LABEL, POLICY | As row 4. |
| 6 | `:848` DFS control fields | PUB, LABEL | RCG2 lines 53 and 1473 give offset `0x14` and level bits 4:1. Bits 14:11, 10, 9, and 15 are unverified labels. |
| 7 | `:849` software control bit 15 and hardware control bit 5 | LABEL, FW | XBL writes `0x21`, so bit 5 is set. The field name is unverified. |
| 8 | `:905` word at `0x00185000` | FW | TZ initializers `0x15751b9c` and `0x15751f30` set bit 0 and poll bit 31. The register name is not established. |
| 9 | `:1411` PCIe RSCC CBCR offsets | FW, PUB | ClockDxe reads `0x111004` and `0x111008`. GCC8850 lists the two clocks in the same order. |
| 10 | `:1412` PCIe RSCC reset words | POLICY | The model starts bit 31 set. The exact word is not established. |
| 11 | `:1436` GPU CC FF root offsets | FW, DERIVED | The hook writes `0x03d99474` and `0x03d99478` from base `0x03d94000`, so the offsets are `0x5474` and `0x5478`. |
| 12 | `:1440` FF root has no M/N/D | FW | No firmware access reaches `+8`, `+0xc`, or `+0x10`. The offsets stay undeclared. |
| 13 | `:1444` GX branch words | FW, LABEL | The hook sets bit 0 at `0x03d68040` and `0x03d68008`. The names are unverified. |
| 14 | `cpu_domain_core_evidence.md:171` word `0x2b0` fields | FW, LABEL | ROM `0x335140` and `0x335194` poll bits 60:56 and 15:8. CPUCP `0x1d80b226` reads bits 7:0. Names are labels. |
| 15 | `:198` boot status initial word | DERIVED, POLICY | `0xff` in the valid field matches the eight cores that masks `0x3f` and `0x3` declare. The silicon reset word is not established. |
| 16 | `:284` `+0x260` bits 10 and 9 | FW, LABEL | TZ `0x1573eb64` sets both bits and `0x1573ec64` clears them. Names are labels. |
| 17 | `:342` bank offset `0x30` fields | FW, LABEL | XBL `0x1484ed64..0x1484edc0` writes bits 25, 2, 3, and 5. Names are labels. |
| 18 | `:427` DVFS boot memory-access offsets | FW, LABEL | ROM `0x334ff6` writes `0xc28..0xc38`. PDP `0x2000b2cc` reads `0xc40`. |
| 19 | `:431` request and enable bits | FW | PDP `0x2000b436` programs bits 9:1 and keeps bit 0. PDP `0x2000b884` supplies the upper mask. |
| 20 | `:433` completion bit reset zero | POLICY | The word is read-only in the model. Zero is a declared initial state. The page already labels the encoding a sibling-IP inference. |
| 21 | `:441` reserved-bit comparison | REMOVED | PDP `0x2000b884` supplies the accepted mask. The unverified comparison is gone. |
| 22 | `:465` DVFS control field positions | LABEL | PDP reads and writes `0x408b0000` and selects state 30. The bit positions are unverified. |
| 23 | `:552` `NCC_GPR_ROM` comparison | REMOVED | The firmware lane writes and the absent read-back now carry the claim. |
| 24 | `:588` region word name | FW, LABEL | PDP `0x20004044` programs the region words. The name is unverified. |
| 25 | `:631` ACD bit 3 reserved | REMOVED | PDP `0x8130b5aa` sets bit 3, so the model accepts it. |
| 26 | `:637` DVMR region word | FW, POLICY | PDP `0x81321704` reads and writes the word with 64-bit accesses. Zero is a declared initial state. |
| 27 | `:642` `CSRINITLATEDONE` | FW, LABEL | PDP `0x2000b312` sets bit 5. The name is unverified. |
| 28 | `:788` low-power bank comparison | REMOVED | CPUCP `0x1d81a616` reads `0x18871000`. The cross-SoC claim is gone. |
| 29 | `:857` LPM wait mask at `0x161c` | FW, LABEL | XBL accesses `0x1887761c`. The layout name is unverified. |
| 30 | `cpu_domain_power_evidence.md:19` absent SM8750 header | REMOVED | The page now cites the firmware lane descriptors and the ROM sequence. |
| 31 | `evidence.md:624` parser reads `p_paddr` | FW | The program-header table in the same page records different `p_vaddr` and `p_paddr` values for both images. |
| 32 | `gcc_pll_evidence.md:218` Pongo L=2 | FW, DERIVED | XBL `0x14861768` writes L at `+0x10`. The 38.4 and 19.2 MHz outputs follow the modelled divider. |
| 33 | `:250` REFGEN pointers | FW | XBL `0x1484ee54..0x1484ee68` builds the three pointers. |
| 34 | `:259` REFGEN offsets and names | FW, LABEL | XBL displacements give `0xa8`, `0x44`, `0x5c`, and `0x60`. Names are labels. |
| 35 | `:273` `IP_TUNEH` function | LABEL | The analog function is not established. The page now says so. |
| 36 | `:274` `CMN_REFGEN_CTRL1` value 8 | FW, LABEL | XBL writes 8. The mechanism is unverified. |
| 37 | `:275` `CMN_REFGEN_CTRL2` value 3 | FW, LABEL | XBL writes 3. The mechanism is unverified. |
| 38 | `mpam_evidence.md:11` frame offsets | FW | CPUCP names `msc0@19870000` and `msc1@1a870000`, and run613 stops at `0x19871000`. |
| 39 | `:46` identification and initial words | PUB, POLICY | The selector, capacity, priority, and monitor offsets are architectural. The identification and initial words are model-supplied. |
| 40 | `mpam_msc.h:13` layout citation | PUB, POLICY | The header now cites the architectural offsets and states the model-supplied values. |
| 41 | `amu_evidence.md:20` AMU block address | FW | CPUCP holds `0x19010000` three times, and lane table `0x1d82e710` pairs it with core base `0x19000000`. |
| 42 | `:35` identification words read-only | POLICY | No firmware instruction reads them. The model supplies the values and rejects writes. |
| 43 | `:38` enable words start clear | POLICY | The page now declares the cleared state. No firmware writes an enable. |
| 44 | `:53` counters start clear | POLICY, FW | Run609 copies the counters before any enable write, so no decision depends on a reset value. |
| 45 | `amu.h:12` register citation | PUB, POLICY | The header now separates the counter and type offsets from the unverified enable and identification words. |
| 46 | `packed_root_evidence.md:86` CMU `0x20` fields | FW, LABEL | XBL `0x1484d294..0x1484d2f0` writes bits 25, 2, 5, and 3. |
| 47 | `:98` later DVFS divider change | DERIVED | The text now marks this statement an inference. |
| 48 | `power_control_evidence.md:14` AOSS base | DERIVED | Hold word `0x0c264000` minus offset `0x01264000`. |
| 49 | `:15` hold word identity | FW | ROM `0x3352e8` loads `0x0c264000`, `0x3352ee` sets bit 0, and `0x3352f2` writes the word. |
| 50 | `:17` XBL sets the reset mode and clears the bit | BLOCKER | No selected image references the word outside the ROM path. The prose is removed. |
| 51 | `:18` XBL clears the bit for shutdown | BLOCKER | As row 50. |
| 52 | `:68` TLMM base | LABEL | Now attributed to the private layout. The value only supports a rejection. |
| 53 | `:69` TLMM register base | LABEL | As row 52. |
| 54 | `:70` pad drive word name and mask | LABEL | The address arithmetic keeps its rejection role. The name is unverified. |
| 55 | `:73` JTAG drive word name | LABEL | As row 54. |
| 56 | `subsystem_evidence.md:512` hang module | FW, LABEL | CPUCP `0x1d803026` writes `0x18870010 + 4*hart`. Names are labels. |
| 57 | `:792` NSI word names | LABEL | CPUCP `0x1d808d08` walks the lane descriptors and writes the words. Names are labels. |
| 58 | `:797` declared NSI initial values | POLICY | The page now calls them declared initial values. |
| 59 | `:802` reset-value origin | BLOCKER | The earlier page attributes the values to a private layout. Their physical reset interpretation remains unverified, and that attribution does not establish implementation derivation. |

## models/qcom/sm88xx/pdp

| # | Fact | Disposition | Support or unresolved prerequisite |
| --- | --- | --- | --- |
| 60 | `evidence.md:86` reset-vector word identity | FW, DERIVED | Store `0x20006968` publishes `0x2000c781`. Masking bits 31:7 gives base `0x2000c780`, which holds the recorded table. The name is a label. |
| 61 | `:344` per-core DPM base | FW, LABEL | The DPM table at `0x2000d268` lists local bases `0x400b0000` with a `0x100000` stride. |
| 62 | `:349` weight fields at `0x10..0x98` | FW, LABEL | PDP `0x81326c40` copies 33 doublewords. Names are labels. |
| 63 | `:401` status at `+0x720` | REMOVED | The public CPRH layout difference already carries the version argument. |
| 64 | `:453` diagnostic word offsets | FW, PUB, LABEL | Runs592 to 594 read these offsets. `0x81c`, `0x820`, and `0x824` match CPR3 lines 136 to 141. Names are labels. |
| 65 | `:489` BIST chain-check word | FW, LABEL | PDP `0x81323494` writes `0x80000600` to `+0x20`. The name is a label. |
| 66 | `:507` thread-zero default route | FW | Run599 completes the aging sample for sensor 15 without an ownership word for it. |
| 67 | `acd_evidence.md:5` ACD module identity | FW, LABEL | Instance table `0x8131d0c8` gives local `0x409d0000` for lane zero. The page no longer claims a module name. |
| 68 | `:20` absent SM8750 entry | REMOVED | The firmware table and the inference below carry the claim. |
| 69 | `:23` same register set for entry 1 | POLICY | The inference label stays. |
| 70 | `acd.h:12` module name | REMOVED | The comment now cites the instance table. |
| 71 | `acd.h:44` 58 offsets with access and value | BLOCKER | Firmware writes or reads `0x04`, `0x08`, `0x14`, `0x28`, `0x44`, `0x4c..0x54`, `0xcc`, `0xd8`, `0xe0`, and `0x110`. The other offsets and all initial words are unverified. |
| 72 | `power_controls.h:94` per-core DPM name | REMOVED | The comment now cites the DPM table address. |

## models/qcom/aop

| # | Fact | Disposition | Support or unresolved prerequisite |
| --- | --- | --- | --- |
| 73 | `evidence.md:31` transport descriptor RVA | FW | GLINK `.data` at RVA `0xdb18`, and the two channel strings at RVAs `0xda7b` and `0xcfbf`. |
| 74 | `:34` transport field values | FW | The descriptor words at RVAs `0xdb18..0xdb48`. |
| 75 | `:99` `IPCC_C_AOP` at index zero | FW | The client table in the same image. The mailbox route at client zero is exercised by the model. |
| 76 | `:100` `IPCC_C_APSS_NS0` at index six | FW | The same table, and the APSS bank index that `platforms/qcom/sm88xx/sm8850/soccp.lua` uses. |
| 77 | `:118` buffer sizes of 64 and 100 bytes | FW | The client buffers, read as whole-buffer calls at RVAs `0x7ec8` and `0x8584`. |
| 78 | `:132` 200 `Stall(10)` iterations | FW | The three client loops in the same image. |
| 79 | `:134` separate link, open, and transmit loops | FW | As row 78. |
| 80 | `:135` timeout returns `EFI_DEVICE_ERROR` | FW | The transmit path return value. |
| 81 | `qmp.h:29` descriptor constants | FW, PUB | RVAs `0x8124..0x8130` and `0x7a48..0x7a5c`. Linux `drivers/soc/qcom/qcom_aoss.c` defines the same descriptor offsets, as the aop evidence page records. |
| 82 | `qmp.h:264` two clock resources | FW | The UEFI client transmits `qdss` and `qdss_ao` in its clock messages. |

## models/qcom/cpr

| # | Fact | Disposition | Support or unresolved prerequisite |
| --- | --- | --- | --- |
| 83 | `evidence.md:5` CPRF block identity | FW, DERIVED | The four CPRDxe address builders start at `0x0c20100c`, so the block occupies the `0x0c200000` page. |
| 84 | `:7` register names and initial words are private values | BLOCKER | Kept as a disclosure. The values are unverified. |
| 85 | `:30` twelve CPRF rows | FW | Every builder asserts `cprf_id < 12`. |
| 86 | `:34` readback of `CTRL_INTERACTION_EN` | FW, LABEL | CPRDxe reads `0xc230` and discards the value. The field name is a label. |
| 87 | `:68` three initial words | BLOCKER | No permitted source establishes these words. CPRDxe reads `CPRFm_CTRL` before it writes, and no instruction branches on the value. |
| 88 | `:82` declared layout origin | BLOCKER | The heading now says "private layout". The origin is unrecorded. |
| 89 | `:84` `CPRFm_CTRL` bits 16:0 | FW | The read, mask, and write sequence uses `0x1ffff` and keeps bit 16. |
| 90 | `:85` twenty entry model | FW | CPRDxe writes entries up to 19 for the GFX plan. |
| 91 | `controller.h:13` thread stride | PUB, FW | CPR3 defines `0x440`. Selected XBL uses `0x740`. |
| 92 | `controller.h:147` step-counter offset | FW, LABEL | CPRDxe RVA `0xb940` writes from `0x32d4`. The name is a label. |
| 93 | `cprf.h:42` twelve rows | FW | As row 85. |
| 94 | `cprf.h:47` sixteen entries | FW, POLICY | The model keeps 20 entries because firmware reaches 19. |

## Unresolved evidence and interpretation questions

These questions concern physical accuracy and the scope of implemented assumptions.
They are not blanket publication blockers caused by citation removal.
Existing records and catalog patterns can support bounded inferences without a new hardware read for every value.
Do not call an inherited value intentional policy without a recorded design basis.

1. Definitions previously attributed to a private register layout.
   LABEL and BLOCKER rows identify different questions, not a common proven derivation.
   Record the applicable evidence or inference and its limits for each claim.
2. AMU identification, control, and enable words.
   Model: `models/qcom/sm88xx/cpucp/amu.h`, `constant()` and the enable offsets `0xc00..0xc24`.
   Useful evidence: a permitted register description or an authorized observation with verified address mappings.
3. MPAM identification and initial words.
   Model: `models/qcom/sm88xx/cpucp/mpam_msc.h`, `identity()` and the `Partition` and `Monitor` defaults.
   Useful evidence: a permitted register description or an authorized observation of the relevant MPAM frames.
4. ACD register table.
   Model: `models/qcom/sm88xx/pdp/acd.h`, the 58-row `registers` table beyond the firmware offsets in row 71.
   Question: which remaining offsets follow a recorded family pattern, and which access or reset assumptions remain untested?
5. CPRF initial words and field names.
   Model: `models/qcom/cpr/cprf.h`, `ctrl_reset`, `vrm_reset`, `hw_version`.
   Useful evidence: a permitted register description or an authorized observation of the relevant CPRF words.
6. Clock-gating override initial words and writable masks.
   Model: `models/qcom/sm88xx/cpucp/clock_gating_overrides.h`.
   Question: which initial words and writable masks are observed, inferred from a catalog pattern, or untested implementation assumptions?
7. TLMM pad word names.
   Model text: `models/qcom/sm88xx/cpucp/power_control_evidence.md`.
   The names only support a rejection, so no behavior depends on them.

## Follow-up disposition for other files

1. Platform Lua carries the same private-layout values for the vote banks, the clock-gating overrides, the ACD instances, and the AMU and MPAM frames.
   Those constants need the same disposition.
2. `docs/qcom/sm88xx/stubs.md` repeats the ACD and clock-gating reset claims, and it names the `ncc_acd_acd_ncc` bank at row 187.
   `platforms/qcom/sm88xx/pdp-runtime.lua` repeats the `APSS_HM+0x020b0000` module name at row 72.
   Both need the same reclassification.
3. `docs/qcom/sm88xx/decisions.md` may repeat the reset-value claims that this review reclassifies.
   Check it when those claims change.

## Verification note

The model constants did not change, so the existing fixtures keep their expectations.
The checks below cover the platform fixtures and the store references that this page adds.

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform
cargo run --locked -p qcom-vp -- references
```

The platform test runs the `core-amu`, `ncc-mpam-msc`, `pdp-power-controls`, and `cprf` fixtures with the other model fixtures.
The references command checks the store paths and hashes that this page adds.
A controlled-language check of the edited pages reported no violations.

This review ran no build, test, lint, or formatter.
The edited model files changed comments only.

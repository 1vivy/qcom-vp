# Fuse controller evidence

## Register interface

The model key is `qcom,sm8850-fuse-controller`.
The TME view starts at `0x021c8000`.
The APSS and CPUCP views start at `0x221c8000`.
All views use the same state.
The virtual board also supplies a 4 KiB TME view at `0x02308000`.
The ROM uses that base for the security-policy queries described below.
Both TME views share the same configured words.
This mapping defines virtual consistency, not a captured physical alias.
The same controller owns the bounded corrected PTE manufacturing word described below.
That word has one declared value across its TME, APSS, and CPUCP virtual views.

The offsets below are established by the selected firmware accesses that the table cites.
The register names are descriptive labels unless a public source names them.
The public security profile contributes only the policy fuses cited in the sections below.
The supplied SM8850 ROM executes the listed accesses.

| Offset | Register | Virtual value | Policy or observation |
| --- | --- | --- | --- |
| `0x10` | BIST status word | `0x10000000` when `secondary_hw_key_programmed` is true, else `0` | The SM8850 QSEE secure-state check uses bit 28 as the secure-key pass indication. The source-only name and the error meaning of the other bits remain unestablished. |
| `0x40` | `QFPROM_CONTROLLER_WR_PERMISSIONS_LSW` | `0x1800c060` | QSEE `0x15696058` requires these fuse write-protection bits. |
| `0x44` | `QFPROM_CONTROLLER_WR_PERMISSIONS_MSW` | Profile value, zero in the retail profile | The QSEE table masks this word. It is readable but not measured. |
| `0x48` | `QFPROM_CONTROLLER_RD_PERMISSIONS_LSW` | `0x10000000` | QSEE requires bit 28, `SOC_ROM_PATCH_READ_DISABLE`. |
| `0x4c` | `QFPROM_CONTROLLER_RD_PERMISSIONS_MSW` | Profile value, zero in the retail profile | The QSEE table masks this word. It is readable but not measured. |
| `0x50` | `FUSE_REDUNDANCY_ENABLE0` | `0x08000000` | QSEE requires bit 27, `SOC_ROM_PATCH_FEC_ENABLE`. |
| `0x54` | `FUSE_REDUNDANCY_ENABLE1` | Profile value, zero in the retail profile | The QSEE table masks this word. It is readable but not measured. |
| `0x12c` | `FEATURE_CONFIG11` | `0` | CPUCP selector `0x88` reads bits 11:8 and selects each cluster's core policy. |
| `0x148` | `FEATURE_CONFIG18` | `0` | Bit 19 excludes BOOT-BLOCK from this supplied chain. The package does not contain that image. |
| `0x14c` | `FEATURE_CONFIG19` | `0` | TME `0x201854` extracts bits 25:18. The profile declares this field as zero. |
| `0x200` | `QC_CONFIG0` | `0` | CPU bypass remains disabled. APSS `0x30d3a8` reads bit 10. |
| `0x204` | `QC_CONFIG1` | `0x88000000` | Bit 31 selects ROM boot. Bit 27 enables legacy OEM authentication control. Test mode remains clear. |
| `0x210` | `QC_CONFIG4` | `0` | The virtual profile uses the TSMC foundry encoding. CPUCP selector `2` reads bits 11:8. |
| `0x214` | `QC_CONFIG5` | `0` | The peripheral drive-strength field remains zero. APSS `0x30d080` reads bits 6:4. |
| `0x218` | `QC_CONFIG6` | `0` | The TME secure-boot, ROM-access, and ROM-entry bypass bits remain clear. |
| `0x21c` | `QC_CONFIG7` | `0` | TME `0x20187e` reads this policy word. The virtual profile leaves its disable fields clear. |
| `0x300` | `OEM_CONFIG0` | `0x1000` | Bit 12 disables fuse programming. The boot-order field remains zero. |
| `0x308` | `OEM_CONFIG2` | `0x40000000` | Bit 30 requires the certificate's extended key usage. The observed leaf certificates declare code signing. |
| `0x314` | `OEM_CONFIG5` | `0x0f` | The low byte enables rollback features. Donor `0x2192ac` consumes bit 3 for OEM class 2 and bit 0 for other OEM classes. |
| `0x318` | `OEM_CONFIG6` | Profile value, zero in the retail profile | QSEE reads it as the high half of the OEM_CONFIG5 pair. The retail profile does not declare the remaining OEM configuration bits. |
| `0x400` | `ANTIROLLBACK0` | `0` | The virtual QTI TME minimum version is zero. This value does not disable signature verification. |
| `0x444` | `ANTIROLLBACK17` | `0` | The virtual OEM SHRM and CPUCP minimum versions are zero. |
| `0x448` | `ANTIROLLBACK18` | `0` | TME `0x2016aa` reads this rollback word. The virtual minimum versions are zero. |
| `0x458` | `ANTIROLLBACK22` | `0` | TME `0x201062` reads this rollback word. The virtual minimum versions are zero. |
| `0x45c` | `ANTIROLLBACK23` | `0` | The same ROM query reads the next rollback word. The virtual minimum versions are zero. |
| `0x500..0x548` | `PK_HASH0..18` | Configured certificate digest | The model preserves the ROM's alternating 32-bit and 24-bit layout. Missing material makes the read fail. |
| `0x600` | `BOOT_CONFIG` | `0` | The boot-order field remains zero. The watchdog uses GPIO control. APSS `0x30d218` reads this word. |
| `0x604` | `SECURE_BOOT` | `0x130` | Authentication remains enabled with an OEM fuse-backed root hash. Serial binding remains disabled. |
| `0x610` | `CHIP_UNIQUE_ID_0` | `1` | The serial number is explicitly virtual. Serial binding remains disabled. |
| `0x614` | `CHIP_UNIQUE_ID_1` | `0xa01b0200` | The chip word shares the profile's hardware version with TCSR and the sequencer. |
| `0x630` | `MRC_0` | Profile value, zero in the retail profile | QSEE reads the QC and OEM root-certificate activation and revocation lists. The profile does not declare those lists. |
| `0x700` | `OEM_HW_ID` | `0x51` | The selected TME OEM metadata contains this value at file `0x2f1a8`. |
| `0x704` | `OEM_PRODUCT_ID` | `0` | The profile selects product zero. The retained expected HWID also uses that product. |
| `0x73c` | Lifecycle word | `5` | The source-only name for this lifecycle is `EXT_OPERATIONAL`. TME `0x20d06e` reads the word. |
| `0x744` | `JTAG_ID` | Platform data | The XBL ChipInfo table establishes each part number. Company bits remain declared virtual policy. |
| `0x780` | `TME_CONSOLIDATED_DEBUG_VECTOR0` | `0` | TME `0x20180c` reads the low nibble. Secure-boot authentication remains enabled. Debug reenables remain clear. |
| `0x784` | `TME_CONSOLIDATED_DEBUG_VECTOR1` | `0` | OEM `0x43bc80` queries DAP and TITAN debug fields. Debug enables remain clear. |
| `0xcf0` | `OEM_SECURITY_POLICY_LSB` | `0` | OEM lifecycle bits 17:16 select the virtual non-development profile. |
| `0xcf8` | `PATCH_VERSION` | `0` | The TME and APSS ROM patch versions remain zero. APSS `0x30d718` consumes this word. |
| `0x1004` | `CAPT_SEC_BOOT_GPIO` | `0x20` | The virtual reset policy selects ROM boot. The ROM can update the retained 13-bit field. |
| `0x100c` | `TME_TEST_IMAGE_VECTOR_1` | `0` | TME `0x2013ca` writes bit 3. The model retains the guest's full word. |
| `0x1034` | `FEC_ESR` | `0` | The virtual fuse interface reports no correction error. |
| `0x1054` | Self-test control word | `0` | The source-only mask is bit 0. The virtual self-test completes at the write. |
| `0x1058` | Self-test status word | `0` | The source-only name of bit 0 is a sense-done flag. It becomes one after an accepted start. This word accepts reads only. |

`PATCH_VERSION` contains the TME version in bits 31:16 and the APSS version in bits 15:0.
`BOOT_CONFIG` also contains the APSS boot-speed field in bits 8:7.
Zero names the source's 600 MHz option. The fuse value does not implement the clock hardware.

These values define virtual policy. They are not measurements from either retail board.
They do not establish all physical fuse values, remaining security fields, or other anti-rollback values.
The host's `auth=retail` setting does not prove that the ROM completed authentication.

## Access rules

The model accepts aligned 32-bit little-endian reads of the listed registers.
It also accepts aligned 64-bit reads when both constituent 32-bit words are listed.
The low-address word occupies bits 31:0.
The model validates both words before it changes the transaction buffer.
`CAPT_SEC_BOOT_GPIO` and `TME_TEST_IMAGE_VECTOR_1` accept 32-bit writes.
The self-test control word also accepts 32-bit writes.
Only bit zero applies. The source-only mask defines no other field.
The self-test status word accepts reads only.
The secure-key status at `0x10` accepts reads only.
The protection, redundancy, OEM_CONFIG6, and MRC words accept reads only.
Wide writes, byte enables, insufficient streaming widths, and invalid alignments fail.
Unknown constituent words and other unsupported transfers also fail.
The model never supplies zero for an unknown fuse.

### XBL diagnostic reads

The audited image is `IMAGES/xbl_sc.elf` from the CPH2749 `16.0.5.700EX01` package.
Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
The bounded loop at `0x1482987c..0x148298ec` uses five 64-bit table entries.
Instruction `0x148298a0` is `LDR X5,[X8]`.

| Label | Address | Model result |
| --- | --- | --- |
| Serial Number | `0x221c8610` | Reads the listed serial and chip words at offsets `0x610` and `0x614`. |
| OEM Config Row 0 | `0x221c2158` | Uses a separate fuse aperture. |
| OEM Config Row 1 | `0x221c2160` | Uses a separate fuse aperture. |
| Feature Config Row 0 | `0x221c2188` | Uses a separate fuse aperture. |
| Feature Config Row 1 | `0x221c2190` | Uses a separate fuse aperture. |

Only the serial entry exposes an adjacent pair in this controller's existing register map.
The profile already supplies both words.
The other four entries do not justify new words in this model.

The guest regression checks the wide serial/chip pair with distinct word values.
It also checks narrow reads, the two writable controls, and rejection of a pair with an unknown second word.
The integrated suite passed all 53 tests.
The canonical run `run-sm8850-reconstructed-cli1` passed this diagnostic loop and retained 3,315 actual UART bytes.

The image vector is mutable state, not a programmable fuse.
The register has a full-width data field.
TME `0x205b94..0x205c08` checks the authenticated image record before it selects the vector update.
The model retains that write without changing any authentication decision.
Run `run-sm8850-functional-controls47` passed the update and reached handoff flags at `0x00c02018`.

### TME fuse self-test start

The controller aperture sits at `0x021c8000` in the TME view.
The local range base is `0x02308000`, and range five starts at `+0x1000`.
Two independent selections establish this split.
The OEM descriptor table at `RADIO/tme_fw.img` file offset `0x296e4` holds `{1, 0x02309054}` and `{2, 0x0230900c}`.
TME ROM `0x2013c0..0x2013ca` adds `0x140000` to addresses below `0x02300000`.
The difference between `0x02308000` and `0x021c8000` is that alias.
The source-only base macros remain unestablished, and the three values above are sufficient.
`0x1004` is `CAPT_SEC_BOOT_GPIO` and `0x1034` is `FEC_ESR`, both in range five.
Therefore model offset `0x1054` is the self-test control word, not a corrected or raw fuse row.
The corrected `QFPROM_CORR_MEMORY_REDUN_ROW49_MSB` is at `0x021c3054`, which the model does not map.
The source-only mask is `0x1` with the start bit at bit zero.
The self-test status word follows at `+0x58`. Its source-only bit zero is a sense-done flag.
Its access macros define reads only.

The TME local view is a `+0x140000` alias.
TME ROM `0x2013c0..0x2013ca` adds `0x140000` to addresses below `0x02300000` before it writes an offset of `0x900c`.
The resulting local write address is `0x0230900c`.
This bounds the alias arithmetic independently of the platform translator.

Run `run-sm8850-common-runtime419` stopped at TME OEM PC `0x4325d8`.
The faulting store was `sw a2,0(a0)` with effective address `0x02309054` and `TLM_COMMAND_ERROR`.
The OEM image `RADIO/tme_fw.img` file offset `0x296e4` holds the selected descriptor table.
Its entries are `{1, 0x02309054}`, `{2, 0x0230900c}`, and `{3, 0x02309010}`.
OEM function `0x43248a` builds a five-byte descriptor and scans the table for the identifier in its first byte.
The selected entry is identifier one.
The store writes the descriptor value to that entry.
Relative to the local base `0x02308000`, the selected offset is `0x1054`.
The request is therefore one 32-bit `QFPROM_BIST_CTRL` write.
It is not a corrected or raw fuse blow, a write-once word, or an anti-rollback version write.

The retained trace does not contain the descriptor value.
The model applies the documented one-bit mask, so a zero value leaves the self-test idle and a one value starts it.
The contract therefore does not depend on the unrecovered value.
The virtual array supplies no sense error, so the self-test completes at the write.
START reads zero and `FUSE_SENSE_DONE` reads one.
The model does not invent a BIST error bitmap.
The available evidence does not define a mapping from the injected FEC status to `QFPROM_BIST_ERROR0/1`.
No fuse value, policy word, or authentication input changes.

## Execution evidence

### QSEE secure-state fuse status

Run420 passes the BIST store and stops later on an APSS data abort.
The ESR is `0x96000010`, the FAR is `0x221c8010`, and the ELR is `0x75ca95d24`.
The ELR is a translation-block address.
The known TZ vector `0x14695200` appears in the same trace as `0x75ba95200`.
Their difference is `0x747400000`, so the guest PC is `0x15695d24`.
The TZ image segment that contains file offset `0x60d1c` maps to virtual address `0x15695d1c`.
The file bytes at that offset match the trace instruction sequence, including `ldr w3,[x8]`.
The faulting read is `0x221c8010`, which is model offset `0x10`.

QSEE `0x15695c68` calls service `0x14e`, waits for `0x221c9054` to become zero, and then requires bit 28 of `0x221c8010`.
The caller `0x15695ff0` builds the `qsee_get_secure_state` failure mask.
Its mask bit one prints `Bit 1: Sec HW key is not programmed` from `0xd87b0f98`.
The ordered message set and the mask bit positions establish the alignment.
A set bit 28 therefore reports a programmed secure key.

Offset `0x10` is a 32-bit status word.
The source-only name `QFPROM_BIST_ERROR0` and the error meaning of its thirty-two bits remain unestablished.
The SM8850 QSEE consumer instead uses bit 28 as the pass indication.
The model follows the consumer for that bit.
The model exposes the explicit fuse-profile parameter `secondary_hw_key_programmed`.
The parameter defaults to false, so an undeclared profile fails closed.
The retail Oplus fuse profile sets it true.
The word reads `0x10000000` when the parameter is true and zero otherwise.
The model does not recover the other thirty-one status bits and reports them as zero.
The read changes no authentication input or decision.

The parameter declares the factory-provisioned secondary hardware key.
The key itself is not modeled.
The parameter does not derive from the OEM root hash, and the two fuses are independent.
This is a declared substitute, not a measurement of the key fuses.

### Run421 fuse-window audit

Run421 stops at APSS translation-block PC `0x62397a074` on FAR `0x221c8318`.
The TZ vector `0x14695200` appears as `0x622895200`.
The delta is `0x60e200000`, and the guest PC is `0x1577a074`.
That PC lies in the descriptor checker `0x1577a054`.
The checker reads both words of a descriptor address.
The descriptor at `0x157f90bc` names `0x221c8314` and expects low nibble `0xf`.
The failing second word is `OEM_CONFIG6` at offset `0x318`.

The audit scans each immediate address in the QSEE image for the window `0x221c8000..0x221c9fff`.
It also decodes the twenty-byte descriptor tables that the checker uses.
The read set follows.
- `0x10`, `0x40`, `0x44`, `0x48`, `0x4c`, `0x50`, `0x54`.
- `0x14c`, `0x214`, `0x300`, `0x308`, `0x314`, `0x318`, `0x458`.
- `0x500..0x530`, `0x604`, `0x610`, `0x614`, `0x630`.
- `0x700`, `0x704`, `0x73c`, `0x744`, `0x780`, `0x784`.
- `0x1054`.

The existing model already covers the policy, identity, hash, lifecycle, and debug words in that list.
The audit adds the six protection and redundancy words, `OEM_CONFIG6`, and `MRC_0`.
QSEE `0x15696058` checks the three low words against `0x10000000`, `0x1800c060`, and `0x08000000`.
The checker also reads the three high words even when their masks are zero.
The retail profile declares the required low-word bits and zero high words.
`OEM_CONFIG6` and `MRC_0` are declared profile words.
The retail profile sets both to zero because it does not declare the remaining OEM configuration bits or the root-certificate lists.
These values are virtual policy, not fuse measurements.
They change no image-authentication input or decision.

A scan of `abl.elf` finds no direct fuse-window address.
A scan of `uefi.elf` finds no aligned fuse-window literal.
This result does not prove the absence of indirect accesses through a table or the TME service.

### Run420 key-wrap requests

Run420 logs two QSEE wrap records with identifier `0x3600008` and argument `0x15`.
Each record follows a `SEQUENCER_KEY_DERIVED slot=31` event.
Each completes as `SEQUENCER_KEY_WRAPPED source=31 wrapping=0x15 owner=2 bytes=92`.
No failure, denial, or error event occurs between them.
The repeats are two independent derive-and-wrap operations, not retries after an error status.


An isolated APSS execution reached the `BOOT_CONFIG` read after the preceding profile reads and the shared CPUCP scratch read.
The other cores remained paused during that diagnostic.
The complete OEM execution has not reached UEFI, ABL, or kernel entry.

### APSS security-policy reads

The isolated diagnostic uses `build/private/diagnostic-apss-controls3.lua`.
It loads `build/private/run-sm8850-functional-controls3/run.lua`.
It pauses TME and CPUCP and makes all six UFS LUNs read-only.
The command is:

```text
timeout 12s build/install/bin/qcom-vp-system --gs_luafile build/private/diagnostic-apss-controls3.lua
```

The first diagnostic stopped at the unsupported `SECURE_BOOT` read.
After the implementation, APSS passed the security-policy reads and reached `FEATURE_CONFIG18`.
The log `build/private/diagnostic-apss-secure-policy.log` records that execution.
This run does not prove authentication completion or kernel entry.

The raw PBL uses AArch64 instructions at base `0x300000`.
At `0x302e74`, selector `0x1e` requests the authentication policy.
The dispatch table at `0x31d178` maps this selector to `0x30cf0c`.
The load at `0x30cf18` reads `SECURE_BOOT`.
The next instruction extracts bit 5, `AUTH_EN`.
The function stores this bit at `0x148166d0` and returns to `0x302e80`.

The register at offset `0x604` is `SECURE_BOOT`.
The model requires an explicit nine-bit `secure_boot` parameter.
It does not provide a valid default.
The configured virtual OEM policy is `0x130`:

| Bits | Field | Virtual value | Meaning from the register definition |
| --- | --- | --- | --- |
| 8 | `FUSE_SRC` | 1 | OEM source rather than Qualcomm source |
| 6 | `USE_SERIAL_NUM` | 0 | No serial-number binding |
| 5 | `AUTH_EN` | 1 | Authentication enabled |
| 4 | `PK_HASH_IN_FUSE` | 1 | The root certificate SHA-384 hash comes from `PK_HASH` |
| 3:0 | `ROM_PK_HASH_INDEX` | 0 | ROM table index, unused when the fuse hash is selected |

Bit 7 is reserved and remains zero.
The APSS dispatcher reads bit 4 at `0x30d120`.
It reads the ROM index at `0x30d5bc` and serial binding at `0x30d5d0`.
Those consumers establish bits 5, 4, and 3:0.
Bit 8, bit 6, and bit 7 have no selected consumer in this record.
Their source-only names `FUSE_SRC`, `USE_SERIAL_NUM`, and the reserved status of bit 7 remain unestablished.
The configured value depends on the positions of bits 8 and 6, so those two positions remain a publication blocker.
The model exposes the configured word without inventing authentication results.
The configured value is virtual policy, not a retail fuse capture.
The model requires a separately checked OEM root input before it permits root-hash reads.

The next request at `0x302e98` uses selector `0x67`.
It reads `QC_CONFIG0` at offset `0x200` through instruction `0x30d3a8`.
It extracts `CPU_BYPASS_EN`, bit 10.
This access does not read `QC_CONFIG6`.

The later request at `0x302f40` uses selector `0x0d`.
It reads `QC_CONFIG5` at offset `0x214` through instruction `0x30d080`.
It extracts the peripheral drive strength in bits 6:4.

The model also requires explicit `qc_config0` and `qc_config5` parameters.
The board configures each word as zero.
This keeps CPU bypass, authentication-disable, and pause bits clear.
The existing compatible key and socket remain unchanged.
No new registration or address mapping is necessary.
The integrated APSS execution passed these reads.

## BOOT-BLOCK selection

Command `0x6d` reads `FEATURE_CONFIG18` through `0x30d3b4` and `0x30d564`.
The dispatcher extracts bit 19 and caches it at `0x148166f4`.
Image `0x7b` is BOOT-BLOCK.
TME-PBL authenticates that image when the feature enables it.

The ROM tests this flag at `0x303c80`, `0x303fac`, `0x30699c`, and `0x309c4c`.
Zero excludes BOOT-BLOCK. It is not an unused spare.
The selected package supplies no image with signing ID `0x7b`.
Its flashing XML supplies no BOOT-BLOCK file or partition.
The virtual profile therefore leaves this feature disabled.
See `models/qcom/sm88xx/apss_boot/boot_info_evidence.md` for the package identities and source details.

## CPU core policy

The integrated run `run-sm8850-functional-controls7` reached CPUCP `0x33572c` with address `0x221c812c`.
The earlier `QC_CONFIG4` claim did not distinguish two paths to that shared load instruction.
Selector `2` adds `0xe4` at `0x33569a` and reads offset `0x210`.
Selector `0x88` reads offset `0x12c` directly.
That word is `FEATURE_CONFIG11`, marked as APSS spare fuses.

CPUCP `0x335206` requests selector `0x88` after the core power acknowledgement.
Instructions `0x335216–0x335258` select a cluster bit and copy it to bit 56 of the core register at offset `0x150`.
The CPU domain model retains that bit.
The virtual profile supplies zero explicitly.
This policy does not change authentication or replace a hardware acknowledgement.
This evidence does not establish the physical effect of that core bit.

## TME authentication policy

The authenticated sequencer path permits the real TME firmware to consume its first APSS request.
The following accesses establish the second TME base:

| TME PC | Address | Consumed field |
| --- | --- | --- |
| `0x201b00` | `0x02308204` | `QC_CONFIG1` bit 30, device test mode |
| `0x201746` | `0x02308218` | `QC_CONFIG6` bit 12, TME secure-boot bypass |
| `0x2011ac` | `0x02308214` | `QC_CONFIG5` bit 10, QTI authentication disable |
| `0x2018f2` | `0x02308400` | `ANTIROLLBACK0`, low 16 bits |
| `0x201334` | `0x02308308` | `OEM_CONFIG2` bit 30, extended-key-usage enforcement |

The profile keeps test mode and both authentication bypasses clear.
It requires explicit values for both new words.
The zero QTI minimum is virtual policy, not a captured retail fuse value.
The selected TME metadata declares software ID `0x35` and version zero.
The build string `ssg.tmefw.4.0-00746-release` is not the anti-rollback counter.

The secondary view forwards to the existing register model.
It does not return zero for an unimplemented word.
The integrated runs progressed through each supported query and retained the next rejected access.
Run `run-sm8850-functional-controls23` passed the QTI minimum and reached `OEM_CONFIG2`.
No policy read creates an authentication result.

### Image rollback and debug policy

The ROM queries additional policy words after certificate verification.
Runs `run-sm8850-functional-controls38` through `run-sm8850-functional-controls43` identify the accesses.
The model requires an explicit value for each supported word.
An unknown offset still fails.

The profile sets the additional rollback words to zero.
These values define virtual minimum versions, not measured retail fuse values.
The ROM helper at `0x2007b2` counts set bits in each selected field.
The profile does not alter that calculation.

`QC_CONFIG7` and `TME_CONSOLIDATED_DEBUG_VECTOR0` also use explicit zero values.
The latter retains the secure-boot policy because authentication-disable bit 2 remains clear.
Run `run-sm8850-functional-controls43` passed these reads and requested an image hash from SRAM at `0x14827000`.

### OEM lifecycle policy

TME `0x201916` reads `OEM_SECURITY_POLICY_LSB` through the second fuse view.
The following shifts extract `OEM_DEVICE_LCS`, bits 17:16.

The profile explicitly selects zero for this word.
The retained `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/machine.py`, lines 179–183, uses the same constructed policy for the OEM development predicate.
Its evidence identifies development when bits 17:16 equal one.
Neither project claims a physical capture of this word.
The zero word also leaves the additional OEM authorization controls clear.
It does not change `SECURE_BOOT.AUTH_EN` or substitute a signature result.

Run `run-sm8850-functional-controls31` passed this query and reached the TME hardware-version read at `0x05fc8000`.
That progress does not establish complete image authentication.


## Image certificate roots

The selected package contains two complete certificate chains in `IMAGES/signed_firmware_soc_view.elf`.
One chain uses the QTI production root.
The other chain uses the OPLUS root.
The same OPLUS root appears in `IMAGES/multi_image.mbn`.
Direct P-384 verification with SHA-384 accepted each certificate signature, including each root's self-signature.
This cryptographic check does not establish all certificate policy checks or firmware admission.

| Root | Source DER range | SHA-256 |
| --- | --- | --- |
| `OPLUS ROOT CA 1` | `multi_image.mbn` bytes `[0x2743,0x29ba)` | `51a5acc6bfee42dfe62a8ef1ee0c5e04912037005a7c9658dcc91e500ffcdb0e` |
| `SRoT MBNv7 Image Signing Root CA 6` | `signed_firmware_soc_view.elf` bytes `[0x2f99d,0x2fc16)` | `6d8d595fc1ad2f80383f266453f9b48b35f48a097cc81b1ec282161c561d47f6` |

The OPLUS root's complete DER encoding has SHA-384:

```text
31313ec6f0c237b69004d153b2ec2d3ddf1d826d0f60a952d039d5ded862ab0e515e4819d1844fdc91242304c685f202
```

This value exactly matches the retained Sahara `OEM_PK_HASH` in `edl-re:files-partial-edl-load/device_fingerprint.txt sha256=2a7fbc4e70d6a3c8b5571239b06a97b81cc5d69ac544d72fa5d125dee0a5808a`.
It hashes the certificate, not its public key alone.
[Qualcomm's provisioning procedure](https://docs.qualcomm.com/doc/80-70018-11/topic/generate-signed-sec-elf-image.html) specifies the same SHA-384 coverage.
The configured `SECURE_BOOT` word selects this fuse-backed OEM root policy.
The ROM's reconstruction code establishes the raw packing described below.

The QTI production root's SHA-384 is:

```text
467f3020c4cc788d2a27a6e097ffa7bfc24e82c2d56953d3b45b494cdba5c2422a947d2c81b5752b8a2ac9eac8acdf34
```

Those 48 bytes appear at TME ROM offset `0x12e5b`.
The QTI test root's distinct SHA-384 appears in the next table entry at `0x12e8b`.
Both roots therefore have ROM entries.
The test root's name alone does not prove that firmware rejects it.
The QTI selection policy still requires the actual ROM path.
Neither QTI root replaces the OEM fuse hash or the sequencer's separate P-521 issuer.

Every observed leaf declares code-signing purpose OID `1.3.6.1.5.5.7.3.3`.
The `OEM_CONFIG2` policy requires that purpose.
Python's strict extension decoder rejected an existing basic-constraints encoding in the OEM leaf.
The direct signature checks did not require that decoder.
The real firmware must still apply its own certificate rules.

## Raw root-hash layout

The TME dispatcher at `0x200ff8` uses the table at `0x2123b4`.
Selector `0x22` reaches the raw hash reader at `0x201264`.
The reader obtains fourteen words at offsets `0x500..0x534`.
It masks each odd word to 24 bits.
Function `0x20bf94` calls that reader at `0x20bfb4`.
Its loop at `0x20bfec..0x20c00e` copies bytes in address order and omits every eighth raw byte.

The authentication caller at `0x207a22..0x207a2e` supplies a 48-byte buffer.
Digest bytes therefore occupy logical hash bits 383:0, without reversal.
For word index `i`, the first digest byte is `7 * floor(i / 2) + 4 * (i % 2)`.
An even word contains four bytes, and an odd word contains three bytes.
The first byte occupies register bits 7:0.
Only two digest bytes occupy word thirteen.

The model sets the 128 spare logical bits to zero by explicit virtual policy.
The ROM does not establish their physical OEM values.
The reserved word at offset `0x54c` remains unsupported.
Query `0x1c` reads `SECURE_BOOT` at offset `0x604` and selects the fuse hash through bit four.
The alternative path copies 48 bytes directly from `0x212e2b + index * 48`.

The host's `oem_root` input records the certificate path and SHA-256.
Preparation and execution check each private copy.
Execution computes the standard SHA-384 digest and supplies it to `oem_pk_hash_sha384`.
The model does not discover trust from an image that it must authenticate.

A bounded read-only debugger smoke read fourteen actual fuse words through the running platform.
The ROM's byte-reassembly rule reconstructed the exact SHA-384 of the checked OEM certificate.
The smoke printed `OEM_FUSE_HASH_MATCHES_VERIFIED_DER`.
The same smoke read the configured JTAG, chip, serial, OEM, and product words.
A separate CLI negative case rejected a changed prepared root before it created run output.
Neither smoke establishes that the complete firmware authentication path succeeded.

## SoC identity data

The actual XBL binaries contain these ChipInfo entries:

| Package | XBL SHA-256 | Entry offset | ChipInfo ID | Part number |
| --- | --- | --- | --- | --- |
| CPH2749 / SM8850 | `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a` | `0xb7bd8` | `0x294` | `0x2b3` |
| CPH2771 / SM8845 | `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4` | `0xb7bf0` | `0x2ad` | `0x2fd` |

Each entry contains a name pointer, ChipInfo ID, part number, and modem mask.
The source ABI is `ChipInfoLoader.h:117–133`.
The matching source consumer is `ChipInfoLoader.c:181–205`.
The SM8850 family record also maps device `0x1b` to default ChipInfo ID `0x294`.
This record supports the selected hardware version `0xa01b0200`.

The JTAG word contains the part number at bits 27:12 and company bits at 11:0.
Bits 31:28 contain the revision.
The profiles select company value `0x0e1` as virtual policy.
The retained fingerprint lists the primary complete word as expected, but its reported HWID does not match that expectation.
It is not a verified physical JTAG capture.
The actual firmware must still enforce its selected hardware-binding rules.
The model never converts a failed comparison into success.

TME `0x201258` retains the JTAG word's low 28 bits.
Wrapper `0x20c02a` returns that value to `0x20ba8e`.
The caller stores it in the composed hardware identity.
The register set also defines the serial, chip, OEM, and product words.

The SM8845 package uses a distinct OPLUS root and a different public key.
Its root has SHA-256 `b63d555667052fbb74989f7f01dce44d285001acc5c9ea654bf78637a15665ca`.
Its complete DER certificate has SHA-384 `dfaeb50ab002eca931c2b672ac26016d35a1e5d88b0c9c40a9c126288face870b618dbd5ca751e2414099981df937efd`.
The private SM8845 manifest identifies that separate certificate.
The platform reuses the same model without a SoC check.

## XBL identity and feature views

SM8850 XBL reads the TLMM part number at `0x1485eba8` and its revision at `0x1485ebbc`.
The read-only view uses the same configured JTAG word as the raw fuse bank.
It does not maintain another identity.

| Address | Value |
| --- | --- |
| `0x0f1db014` | JTAG bits 27:12 |
| `0x0f1db018` | JTAG bits 31:28 |
| `0x221c8124` | Explicit `feature_config9` value |

XBL descriptors `0x148ddb10` and `0x148ddb28` select bits 23:20 and 19:16 from `0x221c8124`.
Instructions `0x1485f014` and `0x1485f038` read this word.
The virtual profile selects zero for both fields.

The private reconstruction profile declares no fused CPU or peripheral defects.
Its bounded corrected view supplies zero at `0x221c21a0` and `0x221c2418..0x221c2427`.
Fifteen XBL descriptors at `0x148f3600..0x148f3750` establish these addresses and their field masks.
The CPU fields use masks `0x3f` and `0x300` in the first word.
The other descriptors use four contiguous peripheral words.
These values describe virtual policy, not captured physical fuses.

The canonical reconstructed run passed ChipInfo and the wide fuse diagnostics.
Its next unsupported access was the UFS clock request at `0x153028`, from XBL `0x148a0834`.
This observation proves XBL progress, not kernel handoff.


## Runtime image-security initialization

The failing address `0x021c8314` belongs to the existing fuse aperture.
It is not an RNG bank.
The retained trace is `build/private/diagnostic-reconstructed-tme-tme.log:73969–74151`.
It records OEM `0x439e76` entering `0x43a2e0`, then `0x43bb6e`, then donor `0x218f5e`.
Donor `0x218f96` calls the policy function at `0x2192ac`.
The failed load is `0x2192ca`.
This trace predates this change. It does not prove that the new read passed.

### Inputs and register identity

The donor is `8975tme.bin`, executable base `0x200000`.
Its supplied SHA-256 is `94059e1c3ba90f7112fa600a7033d83aa71cee6ff5573219d72b63c69c7b67d7`.
The OEM consumer is the CPH2749 package's `RADIO/tme_fw.img`.
Its ELF load addresses require subtraction of `0x20000000` for the executed TME view.
Instruction addresses below use that executed view.

`OEM_CONFIG5` is at offset `0x314` with these fields:

| Bits | Field | Virtual value |
| --- | --- | --- |
| 31:9 | `OEM_CONFIG_191_169` | `0` |
| 8 | `PERIPH_CTRL` | `0` |
| 7:0 | `ANTI_ROLLBACK_FEATURE_EN` | `0x0f` |

The current [Qualcomm security profile](https://github.com/qualcomm/security-profiles/blob/a69b6decf24456fb622de05ecb9b74e0bf871bee/nord_security_profile.xml#L3180-L3191)
defines `ANTI_ROLLBACK_FEATURE_EN` with the same three low-bit names and recommends `0xf`.
It calls bit 3 reserved for future use and explicitly instructs provisioning that bit.
That public record independently supports the low-byte names and the recommended value.
It does not establish the `PERIPH_CTRL` meaning of bit 8 or the field at bits 31:9.
The donor establishes an actual use for bit 3 in its class-2 OEM rollback policy.
This does not establish a new public name for that bit.
This change does not create a second owner for that separate aperture.

### Exact policy branch

Donor `0x2192ac` receives a nonnull context in `a0`, a class in `a1`, and a source selector in `a2`.
It preserves the class in `s0` and the source selector in `s1`.
The loads at `0x2192c2`, `0x2192c6`, and `0x2192ca` read `SECURE_BOOT`, `QC_CONFIG5`, and `OEM_CONFIG5`.
All three loads occur before the source branch.

| Condition | Return value | Instruction evidence |
| --- | --- | --- |
| Source selector is nonzero | `!(QC_CONFIG5 & 0x400)` | `0x2192ce–0x2192d8` |
| Source selector is zero and `AUTH_EN` is clear | `0` | `0x2192da–0x2192e2` |
| Source selector is zero, `AUTH_EN` is set, and class equals `2` | `OEM_CONFIG5[3]` | `0x2192e4–0x2192ee` |
| Source selector is zero, `AUTH_EN` is set, and class differs from `2` | `OEM_CONFIG5[0]` | The branch at `0x2192e6` skips the bit-3 extraction. `0x2192ee` masks bit 0. |

The profile keeps `SECURE_BOOT=0x130` and `QC_CONFIG5=0`.
Thus authentication stays enabled, with the fuse-backed OEM root hash and QTI authentication-disable bit clear.
`OEM_CONFIG5=0x0f` enables both observed OEM rollback branches.
It does not manufacture a version, authentication result, or entropy byte.
The existing zero minimum versions remain explicit virtual policy.
They are not captured retail counters.

### Bounded following dependencies

The donor API template stores `0x2192ac` at file offset `0x33cdc`, interface offset `0x58`.
The same interface contains the following related functions:

| Function | Observed behavior |
| --- | --- |
| `0x218f5e` | Locates an image record, calls the enable policy, and copies the minimum byte at record offset `0x19` only when enabled. |
| `0x21943a` | Counts programmed bits in record words at offsets `8` and, when present, `0x14`. It returns the resulting version byte. |
| `0x2191a0` | Compares an incoming version with the minimum byte. Its `BLTU` at `0x219206` preserves rejection of an older version. |
| `0x2194f6` | Loads the configured `ARBConfig:arbFuseMapQti` or `ARBConfig:arbFuseMapOem` descriptor array. Each record occupies `0x24` bytes. |
| `0x219606` | Checks a fuse address against lower bound `0x21c0237` before it normalizes the address. Invalid input calls the fatal service. |

OEM `0x43a2e0` builds both source policies.
The calls at `0x439e76` and `0x439e92` pass source selectors one and zero, respectively.
The next helper `0x43bc00` independently rereads QTI bit 10 or OEM `AUTH_EN`.
It checks consistency after a delay and retains the fatal callback on disagreement at `0x43bc64`.
Its required words already exist.

The nearby helper `0x43bc80–0x43bdb2` queries consolidated debug policy.
Its tables at `0x445310` and `0x445368` select masks for vectors at `+0x780` and `+0x784`.
The second table uses mask `0x40000` for image `0x91` and mask `0x28` for image `2`.
The latter register is `TME_CONSOLIDATED_DEBUG_VECTOR_1`.
The masks establish bits 18, 5, and 3 of that word.
The source-only bit names `DAP_DBGEN`, `TITAN_DBGEN`, and the reserved status of bit 3 remain descriptive labels.
The explicit virtual word is zero, so these masks do not enable debug.
Other JTAG-disable and peripheral-disable fields in this word also remain zero.
This word is not a captured retail debug vector.
Zero also leaves the high disable controls unasserted.
These include JTAG instruction disables in bits 31:28, secure-boot-GPIO disable in bit 27, and SP disable in bit 26.
Other disable controls are EUD in bit 24, DCC in bit 23, and DEBUGBUS in bit 22.
Thus zero enables no debug field, but it does not establish complete silicon debug lockdown.
The helper reads each selected word twice and retains its consistency-failure callback at `0x43bd64`.

The model now requires both new profile words to fit 32 bits.
Neither parameter has a valid default.
All existing aliases use the same words through the existing socket.
Writes and unknown offsets still fail under the existing access rules.

### Remaining boundaries

The configured rollback descriptor arrays determine later corrected-fuse addresses.
This bounded cohort does not justify a blanket zero-filled corrected-fuse window or unlisted rollback rows.
Such addresses still require their own descriptor and register evidence.
The donor still performs version comparisons and the OEM still performs its authentication and consistency checks.
No software patch, bypass bit, or ready-value substitute is added.

The entropy callback construction does not establish a new hardware interface at `0x021c8314`.
The existing APSS generator and sequencer entropy command use actual host entropy.
See `models/qcom/rng/evidence.md` for that separate interface.
No TME-specific crypto-engine completion or additional entropy-bank semantics follow from this fuse read.
Any later unsupported engine, key-storage, or entropy access requires its own consumer evidence and functional implementation.
This change does not claim authentication completion or a Linux boot log.
No build, test, formatter, native run, artifact hash, or commit ran for this slice.

## OEM feature authorization group

### Source facts

The selected SM8850 OEM helper is `0x441228..0x4412d4`.
The selected SM8845 counterpart is `0x4414a6..0x441552`.
These addresses use the executed TME view, with `0x20000000` subtracted from the ELF virtual addresses.
Both private `signed_firmware_soc_view.elf` inputs contain the same four-entry policy structure.
The model does not store either vendor image.

The four read-only registers have one-bit fields.
Do not confuse these software-view offsets with the same offsets in the corrected QFPROM aperture.
The current [Qualcomm security profile](https://github.com/qualcomm/security-profiles/blob/a69b6decf24456fb622de05ecb9b74e0bf871bee/nord_security_profile.xml#L2486-L2545)
defines `OEM_SECURITY_POLICY` bits 1, 4, 5, and 11 for these four controls.
It recommends one for each control and states that activation is required for them to take effect.
That public record independently supports the bit positions and the semantics.

| Raw address | `oem_security_policy_lsb` bit | bit-zero name | Feature ID |
| --- | --- | --- | --- |
| `0x021c871c` | 1 | `ENFORCEOEMAUTHZONSOCDEBUG` | `0x186` |
| `0x021c8724` | 4 | `ENFORCEOEMAUTHZONFUSEOVERRIDES` | `0x189` |
| `0x021c8728` | 5 | `ENFORCEOEMAUTHZONROMPATCH` | `0x184` |
| `0x021c872c` | 11 | `ACTIVATESECURITYPOLICY` | Master gate |

SM8850 loads the first three words at `0x441284`, `0x441296`, and `0x4412a4`.
SM8845 loads them at `0x441502`, `0x441514`, and `0x441522`.
Both helpers mask bit zero before they store the Boolean.
The fourth table entry, feature `0x18d`, contains literal one.
It has no independent fuse read.
This analysis does not assign a public feature name to `0x18d`.
Its established role is a recognized feature that requires authorization whenever the master gate is active.

The SM8850 table starts at `0x448c40`.
The SM8845 table starts at `0x448c20`.
Both use initialization flags `0x448264` and `0x448266`.
Both cache the master gate in byte `0x448265`.
The gate loads occur at `0x4412c0` and `0x44153e`, respectively.
For a matched ID, the helper writes `master_gate && selected_value` to `*a1`.
It returns one for a match, independently of that Boolean.
For an unknown ID, it returns zero without writing the output byte.

SM8850 `0x441052` calls the table helper during feature dispatch.
Its counterpart is SM8845 `0x4412d0`.
The following calls, `0x44105c` and `0x4412da`, use authorization predicates `0x4411c6` and `0x441444`.
Those predicates also query the existing service with selector `0x200`.
They combine that result with the inverse selected policy Boolean.
The predicates do not replace signature verification.
SM8850 `0x440f20` and SM8845 `0x44119e` separately query feature `0x186` through those predicates.
The dispatch compares IDs `0x184`, `0x189`, and `0x18d` before selecting its existing callback.
Those comparisons occur at SM8850 `0x44108a..0x4410b0` and SM8845 `0x441308..0x44132e`.
The model does not replace these callbacks or their results.

The security profile cited above recommends OEM authorization for SoC debug, fuse overrides, and ROM updates.
For debug, TME must verify both Qualcomm and OEM signatures.
For fuse overrides and ROM updates, it requires an OEM signature.
All three controls require the activation gate.
These controls map to `OEM_SECURITY_POLICY` bits 1, 4, 5, and 11.

### Virtual policy and implementation

The shared reconstructed OEM profile sets policy bits 1, 4, 5, and 11 in `platforms/qcom/sm88xx/tme-runtime.lua`.
Both selected SoCs therefore declare `oem_security_policy_lsb=0x832`.
The four read-only views each return one.
This virtual policy activates the controls and requires OEM authorization.
It is not a physical retail fuse capture.
It does not enable debug, disable authentication, replace a root hash, lower rollback counters, or change the namespace lock policy.
Feature `0x18d` remains the firmware's literal, not a model-provided completion value.
All existing lifecycle, debug-vector, root, authentication, and rollback parameters remain unchanged.

The existing required `oem_security_policy_lsb` parameter is the single authoritative backing word.
The model derives each bit-zero policy view from that word.
It adds no independent configuration parameter that can disagree with the aggregate view at `+0xcf0`.
The reconstructed profile preserves all other bits when it applies mask `0x832`.
Unrelated board profiles retain their existing policy words.

The existing read path serves these four offsets through every fuse socket alias.
The existing write rejection still applies.
Neighbors `+0x718`, `+0x720`, and `+0x730` remain unsupported.
No zero-filled fuse range is added.

The existing native fuse fixture now checks each new row.
The fixture supplies aggregate word `0x810`, which produces alternating view values `0,1,0,1`.
It reads the aggregate word and compares each view with the corresponding extracted bit.
It also checks the composed 64-bit read at `+0x728`.
This synthetic aggregate word is a fixture input, not the board policy.
The fixture retains the final reserved-neighbor failure at `+0x54c`.
The full integration suite passes 62 tests, including this native fuse fixture.
Both canonical OEM runs consume the new views and proceed to the authorization import.
SM8850 stops at `0x440f28`. SM8845 stops at `0x4411a6`.
Both accesses select missing legacy API `0x1f4`, at local address `0x4169f4`.
These results prove register consumption, not successful APDP authorization or kernel execution.

## OEM report-summary corrected policy row

The selected post-copy report summary invokes the real OEM policy getter.
SM8845 calls `0x420d78`, which calls generic fuse reader `0x436706`.
SM8850 uses corresponding policy getter `0x420cf8` and reader `0x43648a`.
Both supply raw lookup key `0x021c0090` and corrected-view selector one.
The raw address is a descriptor key in this call, not a direct MMIO read.

The selected descriptor resides at SM8850 `0x444a70` and SM8845 `0x444b40`.
Its ten words are:

`5, 1, 0, 0x021c0090, 0x02302090, 0x20, 0x20, 0, 1, 5`.

The generic reader resolves the corrected row and checks its access conditions before copying eight bytes.
SM8845 policy getter then extracts bits 17:16 and retains the result in its existing OEM cache.
The canonical software view already identifies those bits as `OEM_DEVICE_LCS`.
The corrected view therefore uses the same authoritative `oem_security_policy_lsb` parameter.
It introduces no independent policy, lifecycle, or identity value.

The selected read group is:

| Address | Consumer and contract |
| --- | --- |
| `0x02302090,+4` | Eight-byte corrected policy row. Return the existing policy parameter as a little-endian 64-bit value. Its validated upper word is zero. |
| `0x02302098,+4` | Generic corrected/error check. Descriptor selects bitmap word zero and mask `0x20`. |
| `0x023020a0,+4` | Generic access/error check. Descriptor selects bitmap word zero and mask `0x20`. |

SM8845 helper `0x43690e` selects the error-status word at `0x02302098` or `+4`.
Helper `0x43696e` selects the access-status word at `0x023020a0` or `+4`.
Both apply descriptor-owned masks.
Helper `0x4369ca` can read row-enable words at `0x023020a8,+4` for descriptor modes one through three.
The selected policy descriptor uses mode zero and skips those reads.
The model leaves row-enable words unsupported.

Capture `artifacts:2026-10-22-tme-report-captures/report-handler-acceptance.json sha256=9d42f174fca4b2b0b1a5eec514fa5c8de1fd96367144025c3b7bc591859d4ef6` confirms this path on SM8845.
The real report contains aggregate denial `0x4005`.
The report flag getter returns `0x59657321` with flags zero.
The subsequent first fault is at `0x43686e`, reading the corrected policy row from `a0=0x02302090`.
This occurs before output used-length publication.

### Explicit virtual status policy

The optional `corrected_policy_socket` belongs to the existing fuse controller.
The runtime binds only `0x02302090..0x023020a7`.
This replaces the old zero-filled `runtime_fuse_status` memory at `0x02302098`.
The socket accepts aligned four-byte and eight-byte reads within the complete group.
It rejects every write, byte enables, unsupported widths, and unknown addresses.

The two status pairs use an explicit virtual aggregate-error policy.
When `fec_error_status` is zero, they report zero error bitmaps.
When that parameter is nonzero, all four status words report all bits set.
The real OEM descriptor mask then selects its existing error branch.
This is a fail-closed virtual mapping, not a recovered vendor bitmap encoding.
The model does not claim that aggregate FEC bits share either OEM status layout.
The policy row itself always uses the same authoritative policy parameter as the canonical view.

The reconstructed profile keeps `oem_security_policy_lsb=0x832`.
Its OEM authorization requirements remain active.
No authentication-disable bit, debug enable, root hash, rollback minimum, or identity changes.

Both native OEM report handlers now consume this row and return zero.
They preserve their original denial records and publish used length `0xc8`.
See `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md` for the captured instruction addresses.
The native fuse fixture also checks policy identity and both all-one status pairs with a nonzero aggregate error.

## Selected XBL calibration dependency group

This group uses the two XBL hashes in the SoC identity table above.
The selected inputs are `prepared-sm8850-common-runtime12` and `prepared-sm8845-common-runtime11`.
Runs `run-sm8850-common-runtime59` and `run-sm8845-common-runtime54` reached this group after SHRM image loading.
The reported first reads were `0x221c89dc` and `0x221c2550`, respectively.
These observations do not prove that the remaining calibration path succeeds.

### Consumers and field layout

SM8850 decoder `0x1484a1e0` receives a fuse object through `x0`.
The object contains a descriptor pointer at offset zero and a 16-bit descriptor count at offset eight.
Each descriptor contains `{u64 address, u32 shift, u32 mask}`.
Load `0x1484a220` reads the descriptor address.
The decoder concatenates masked fields in descriptor order.
It then applies the packing factor in `w1` and the signed-magnitude flag in `w2`.
SM8845 uses the same decoder structure at `0x14848728`.

The SM8850 descriptor inventory contains 211 entries at `0x14915a60..0x149205c8`.
The SM8845 inventory contains 155 entries at `0x149163f8..0x1491c1b0`.
These are static inventories, not claims that every entry executes on one boot.
The union covers 32 raw words.
SM8845 also reads one raw word directly, outside those descriptor arrays.

SM8845 function `0x1484a0d8` supplies four integers to log function `0x14848dc0`.
Its format string at `0x148da054` is:

```text
CPR- Global: rev %d,vpid %d Local: rev %d,vpid %d
```

| Meaning from log argument order | Address | Shift | Mask | Instruction |
| --- | --- | --- | --- | --- |
| Global revision | `0x221c89dc` | 6 | `0x3c0` | Load `0x1484a104`, extract `0x1484a118` |
| Global voltage-plan ID | `0x221c2550` | 1 | `0x1e` | Load `0x1484a0fc`, extract `0x1484a10c` |
| Local revision | `0x221c8a24` | 2 | `0x3c` | Load `0x1484a108`, extract `0x1484a11c` |
| Local voltage-plan ID | `0x221c2550` | 5 | `0x1e0` | Second load `0x1484a100`, extract `0x1484a114` |

The function returns the global voltage-plan ID, not a revision.
Caller `0x14844464` stores this returned byte at `0x149298e0`.
The zero policy keeps both revisions and both plan IDs consistent.
It does not assert that these fields are physical aliases.

The following table lists every descriptor field in the union.
Each entry uses `shift:mask`, with hexadecimal masks.
Addresses omit the common `0x221c0000` base.
All listed words return explicit virtual zero.

| Offset | Descriptor fields |
| --- | --- |
| `0x8970` | `0:0000003f, 6:00000fc0, 12:0003f000, 18:00fc0000, 24:3f000000, 30:c0000000` |
| `0x8974` | `0:000003ff, 10:003ffc00, 22:ffc00000` |
| `0x8978` | `0:00000003, 2:00003ffc, 14:03ffc000, 26:fc000000` |
| `0x897c` | `0:00000003, 2:000003fc, 10:0000fc00, 16:003f0000, 22:0fc00000, 28:f0000000` |
| `0x8980` | `0:00000003, 2:0000007c, 7:00000f80, 12:0001f000, 17:003e0000, 22:07c00000, 27:f8000000` |
| `0x8984` | `0:000000ff, 8:00003f00, 14:000fc000, 20:03f00000, 26:fc000000` |
| `0x8988` | `0:0000003f, 6:00000fc0, 12:0003f000, 18:00fc0000, 24:ff000000` |
| `0x898c` | `0:0000000f, 4:0000fff0, 16:0fff0000, 28:f0000000` |
| `0x8990` | `0:000000ff, 8:000fff00, 20:fff00000` |
| `0x8994` | `0:00000fff, 12:00fff000, 24:ff000000` |
| `0x8998` | `0:000000ff, 8:00000f00, 12:0003f000, 18:00fc0000, 24:3f000000, 30:c0000000` |
| `0x899c` | `0:0000000f, 4:000003f0, 10:0000fc00, 16:003f0000, 22:0fc00000, 28:f0000000` |
| `0x89a0` | `0:00000001, 1:0000003e, 6:000007c0, 11:0000f800, 16:001f0000, 21:03e00000, 26:7c000000, 31:80000000` |
| `0x89a4` | `0:0000000f, 4:000003f0, 10:0000fc00, 16:003f0000, 22:0fc00000, 28:f0000000` |
| `0x89a8` | `0:00000001, 1:0000003e, 6:000007c0, 11:0000f800, 16:001f0000, 26:7c000000, 31:80000000` |
| `0x89ac` | `0:0000000f, 4:000001f0, 9:00007e00, 15:001f8000, 21:07e00000, 27:f8000000` |
| `0x89b0` | `0:00000001, 1:0000007e, 7:00001f80, 13:0003e000, 18:007c0000, 23:0f800000, 28:f0000000` |
| `0x89b4` | `0:00000001, 1:0000003e, 6:000007c0, 11:0007f800, 19:01f80000, 25:7e000000, 31:80000000` |
| `0x89b8` | `0:0000001f, 5:000007e0, 11:0000f800, 16:001f0000, 21:03e00000, 26:7c000000, 31:80000000` |
| `0x89bc` | `0:0000000f, 4:000001f0, 9:00003e00, 14:0007c000, 19:07f80000, 27:f8000000` |
| `0x89c0` | `0:00000001, 1:0000007e, 7:00001f80, 13:0007e000, 19:00f80000, 24:1f000000, 29:e0000000` |
| `0x89c4` | `0:00000003, 2:0000007c, 7:00000f80, 12:0001f000, 17:003e0000, 22:07c00000, 27:f8000000` |
| `0x89c8` | `0:0000001f, 5:00001fe0, 13:0007e000, 19:01f80000, 25:7e000000, 31:80000000` |
| `0x89cc` | `0:0000001f, 5:000003e0, 10:00007c00, 15:000f8000, 20:01f00000, 25:3e000000, 30:c0000000` |
| `0x89d0` | `0:00000007, 3:000000f8, 8:00001f00, 13:001fe000, 21:07e00000, 27:f8000000` |
| `0x89d4` | `0:00000001, 1:0000007e, 7:00001f80, 13:0003e000, 18:007c0000, 23:0f800000, 28:f0000000` |
| `0x89d8` | `0:00000001, 1:0000003e, 6:000007c0, 11:0000f800, 30:c0000000` |
| `0x89dc` | `0:0000003f, 6:000003c0` |
| `0x8a60` | `31:80000000` |
| `0x8a64` | `0:0000001f, 10:00007c00` |
| `0x8a68` | `7:00001f80, 25:fe000000` |
| `0x8a6c` | `0:0000001f` |

SM8850 revision descriptors occupy `0x149203a8..0x149205c8`.
All use `0x221c89dc[9:6]`, except `0x149203c8` and `0x149203d8`.
Those two use `0x221c8998[11:8]`.
SM8845 revision descriptors occupy `0x1491c0b0..0x1491c1b0`.
Only `0x1491c0c0` uses `0x221c8998[11:8]`.
The others use `0x221c89dc[9:6]`.

These are all direct branch-and-link callers of the generic decoder in each selected code segment:

| SM8850 callsite | SM8845 callsite |
| --- | --- |
| `0x14845f64` | `0x14844498` |
| `0x148463e8` | `0x1484491c` |
| `0x148464d0` | `0x14844a04` |
| `0x148466f8` | `0x14844c2c` |
| `0x14846870` | `0x14844da4` |
| `0x14846ad4` | `0x14845008` |
| `0x14847328` | `0x14845870` |
| `0x148474fc` | `0x14845a44` |
| `0x14847a34` | `0x14845f7c` |
| `0x14847a90` | `0x14845fd8` |
| `0x14899c0c` | `0x14898584` |

For example, SM8850 `0x148463ec..0x148463f0` stores the decoded revision into rail state at offset `0x1c`.
SM8850 `0x148464d8` and `0x148466fc..0x14846700` store decoded signed voltage steps.
The last caller pair decodes into `w23` and stores the result in its existing output record.
The guest remains the producer of all derived calibration state.
The model does not replace the decoder or publish a completed calibration result.

### Layout support and limits

Software-view addresses in this group are selected-consumer evidence.
`0x221c8970..0x221c89dc`, `0x221c8a24`, and `0x221c8a60..0x221c8a6c` carry reads that the selected XBL performs.
The `CALIBRATION4..31` and `CALIBRATION64..67` labels and the corrected offset `0x550` field name come only from the source-only definitions.
Thus the selected instructions, not those labels, establish the two VPID masks.

The older field names do not fully match the selected XBL layout.
For example, `CALIBRATION31` assigns bits 8:0 to `CPR12_SVS2_QUOT_MIN_11_3`.
The selected revision mask overlaps that field.
`CALIBRATION49[6:2]` is `GFX_PEAK_VOLTAGE_DELTA`, not a four-bit local CPR revision.
The `CALIBRATION64..67` fields describe ACD NSP2 DVM values.
Do not infer selected rail names or physical field aliases from those older definitions.
The exact selected field names beyond the observed log remain a gap.
The raw addresses, masks, descriptor order, and decoder behavior are established independently.

The reference voltage plan supplies margin records for revision zero and voltage-plan ID zero.
Its fuse records distinguish voltage, quotient, oscillator selector, quotient offset, and voltage offset.
The CPR library defines the same concatenation and signed-magnitude conversion as the selected decoder.
Zero therefore yields zero decoded correction for every listed descriptor, independent of packing factor.
The CPR rail code stores revisions and voltage steps in guest rail state.
It also applies voltage steps to the reference voltage and retains floor and ceiling calculations.
It retains quotient selection, margins, and voltage-offset adjustment.
Zero calibration data does not mean zero final rail voltage.
These code effects explain the virtual policy. They do not prove identical selected voltage-plan contents.

### Ownership, virtual assumptions, and integration

The shared policy declares revision zero, plan ID zero, and zero calibration corrections.
No physical fuse capture establishes those values in the repository.
SM8850 can instead supply the private calibration page described below.
Without a page, unused bits within the 58 admitted raw words are also explicitly virtual zero.
No unlisted calibration word receives a default value.
The policy excludes PRNG health thresholds at raw offsets `0x968` and `0x96c`.
It also excludes security, root-hash, authorization, rollback, lifecycle, debug, and identity words.
The existing TSENS selector at `0x221c2d18` retains its separate owner and default-calibration policy.

`calibration_fuses.h` supplies `QcomCalibrationFuses::read_raw_word(uint64_t offset, const QcomCalibrationFuses::Page* page, uint32_t& value)`.
The offset uses the existing controller's `0x...8000` base.
The function returns false without changing `value` for every unlisted or unaligned offset.
It returns true for offsets `0x970..0x9dc`, `0x9e0..0xa20`, `0xa24`, `0xa60..0xa6c`, and `0xb0c..0xb28`.
Only aligned words within those ranges are admitted.
A null page returns zero.
A page returns the little-endian raw word at the same offset.
The page does not admit another offset.

The existing `fuse_controller.h` includes that header.
Its `read_word()` calls the policy only after all existing branches and before the unknown-address error.
All current security branches stay authoritative.
The existing controller retains its width, alignment, read-only, and wide-read atomicity rules.
All existing raw aliases then use one policy without new router ownership.

The controller has one new CCI parameter, `calibration_fuses`.
It holds the prepared page path, or an empty string.
At elaboration the controller loads the page and rejects a size other than 4096 bytes.
It then prints one line:
`FUSE_CALIBRATION_SOURCE model=qcom,sm8850-fuse-controller source=private-page sha256=<hash>`.
Without a page it prints `source=declared-zero` and returns zero for the listed words.
The host prints no page value.

`platforms/qcom/sm88xx/xbl-calibration-fuses.lua` adds only four read-only bytes at `0x221c2550`.
It uses the existing initialized `gs_memory` convention and checks APSS target overlap.
Both runtime helpers call this helper.
No new module registration is required.
The word contains the two zero plan IDs described above.
This mapping does not expose neighboring corrected rows.

### Native calibration integration

SM8850 run60 and SM8845 run55 pass their original calibration reads.
SM8845 logs `CPR- Global: rev 0,vpid 0 Local: rev 0,vpid 0`.
The integration run passes 75 tests and Clippy with warnings denied.
These checks do not establish coverage of every calibration word.

SM8845 then asserts at `0x14844850` because its first rail has no matching voltage plan.
Run56 stops before that assertion at `0x14844834`, with a hardware breakpoint.
Read-only debugger observations show rail `0x101`, foundry zero, and chip version `0x20000`.
The sole candidate uses version record `0x14915f30`, with wildcard foundry five and bounds `0x10000..0x100ff`.
The native rail pointer at offset `0x30` remains null after the original selection loop.
Thus the failure concerns the declared chip revision, not the zero calibration values.
The board now selects revision 1.0 through its existing identity inputs.
No code replaces the plan pointer, suppresses the assertion, or changes the firmware table.
The diagnostic remains at `build/private/run-sm8845-common-runtime56/cpr-selection.json`.

SM8850 instead writes `0x19978178` at instruction `0x1484b900`.
The value contains bits 6:4 of the PMIC byte that the original routine reads and may update.
The lower three bits select a record from the guest's `cpr.bin`.
The board retains the published word through the existing retained-control model.
This word supplies no calibration result, voltage, or completion.
Static decoding found one direct `0x1997` address builder in the selected XBL load segments.
This observation does not exclude computed consumers in other images.

SM8850 run61 and SM8845 run57 pass these dependencies and reach controller write `0x62800c`.
SM8845 retains the original CPR log with zero revisions and plan IDs.
Its unsupported-chip-revision assertion no longer occurs on this path.

The policy does not implement a CPR oscillator, closed-loop measurement, regulator, or later hardware completion.
Those consumers require separate effect-backed models if native execution reaches them.

## PDP image calibration reads

The SM8850 PDP image is `IMAGES/pdp.elf`.
Its SHA-256 is `74fb165a6619f2451b56360eddd2628827095497593474089316056de8437e8d`.
The image holds 81 literals of `0x221c8000` and four literals of `0x221c2000`.
A literal belongs to one `LDR` instruction or to one accessor function.
A Thumb decode at each 2-byte offset found every load and its PC.
The scan also follows the two branch targets inside `0x81329ccc` and `0x81329d44`.
No instruction builds either base with `MOVW` and `MOVT`.
No consumer indexes the base with a register.
Therefore the tables below hold every QFPROM word the image reads.

Two mechanisms reach the words.
Direct loads read one constant offset from the base.
Eighty accessor functions in a table at `0x81331d44` read one or two words and return a field.
The accessor table contains `{id, function}` pairs and ends at `0x81331fc4`.
`0x8132a6f4` resolves an id to an accessor and asserts `fuse_func != ((void *)0)` when the id is absent.
`0x8132a6b0` replaces the ids in a VPP record with those accessors.
The record data comes from the configuration blob, so the runtime selects a subset of the table.

### Field layout and consumers

The offsets omit the `0x221c8000` base.
Consumer PCs are the load instructions.
Each row lists the bits that the loads take from that word.
A combining accessor reads both of its words.
`sign N` names the bit that selects a negative value.

| Offset | Fields and masks | Consumer PCs |
| --- | --- | --- |
| `0x980` | `22:0x3c00000`, `27:0x78000000`, sign 26, sign 31 | `81329e4e`, `81329e52`, `8132a036`, `8132a03a` |
| `0x9dc` | `10:0x7c00`, `16:0x1f0000`, `22:0x7c00000`, `28:0xf0000000`, sign 15, sign 21, sign 27 | `81329e2a`, `81329e2e`, `81329ebe`, `81329ed0`, `81329ef2`, `81329ef6`, `81329f3a`, `81329f3e` |
| `0x9e0` | `0:0x3`, `2:0x7c`, `8:0x1f00`, `14:0x3ffc000`, `26:0xfc000000`, sign 1, sign 7, sign 13 | `81329dda`, `81329e72`, `81329e76`, `81329e96`, `81329e9a`, `81329eba`, `81329ecc`, `81329f8e` |
| `0x9e4` | `0:0x3f`, `6:0x3ffc0`, `18:0x3ffc0000`, `30:0xc0000000` | `81329dee`, `81329f5e`, `81329f8a`, `81329fa6` |
| `0x9e8` | `0:0x3ff`, `10:0x3ffc00`, `22:0x3fc00000` | `81329dea`, `81329e06`, `81329e1a` |
| `0x9ec` | `12:0x1f000`, `18:0x7c0000`, `24:0x1f000000`, `30:0xc0000000`, sign 17, sign 23, sign 29 | `8132a012`, `8132a016`, `8132a0a6`, `8132a0b8`, `8132a0da`, `8132a0de`, `8132a0fe`, `8132a102` |
| `0x9f0` | `0:0xf`, `4:0x1f0`, `10:0x7c00`, `28:0xf0000000`, sign 3, sign 9, sign 15 | `81329fba`, `8132a05a`, `8132a05e`, `8132a07e`, `8132a082`, `8132a0a2`, `8132a0b4` |
| `0x9f4` | `0:0xff`, `8:0xfff00`, `20:0xfff00000` | `81329fb6`, `8132a122`, `8132a132` |
| `0x9f8` | `0:0xfff`, `12:0xfff000`, `24:0xff000000` | `81329fd2`, `81329ffa`, `8132a142` |
| `0x9fc` | `0:0xf`, `4:0xff0`, `26:0x7c000000`, sign 31 | `81329fe2`, `81329ff6`, `8132a1d6`, `8132a1da` |
| `0xa00` | `6:0x7c0`, `12:0x1f000`, `18:0x7c0000`, `24:0x1f000000`, `30:0xc0000000`, sign 5, sign 11, sign 17, sign 23, sign 29 | `8132a21e`, `8132a222`, `8132a24a`, `8132a25c`, `8132a27e`, `8132a282`, `8132a2a2`, `8132a2a6`, `8132a2c6`, `8132a2ca`, `8132a2ea`, `8132a2ee` |
| `0xa04` | `0:0xf`, `16:0xfff0000`, `28:0xf0000000`, sign 3 | `8132a162`, `8132a246`, `8132a258`, `8132a346` |
| `0xa08` | `0:0xff`, `8:0xfff00`, `20:0xfff00000` | `8132a1a2`, `8132a332`, `8132a342` |
| `0xa0c` | `0:0xfff`, `12:0xfff000`, `24:0xff000000` | `8132a172`, `8132a18a`, `8132a1c6` |
| `0xa10` | `0:0xf`, `4:0xff0`, `20:0x1f00000`, `26:0x7c000000`, sign 25, sign 31 | `81329cf0`, `81329cf4`, `8132a186`, `8132a1b2`, `8132a472`, `8132a476`, `8132a4e6`, `8132a4ea` |
| `0xa14` | `6:0x7c0`, `12:0x1f000`, `18:0x7c0000`, `24:0x1f000000`, `30:0xc0000000`, sign 5, sign 11, sign 17, sign 23, sign 29 | `8132a3fa`, `8132a5d2`, `8132a5d6`, `8132a5f6`, `8132a5fa`, `8132a61a`, `8132a61e`, `8132a63e`, `8132a642`, `8132a662`, `8132a666` |
| `0xa18` | `0:0x3ff`, `10:0x3ffc00`, `22:0xffc00000` | `8132a3f6`, `8132a412`, `8132a68a` |
| `0xa1c` | `0:0x3`, `2:0x3ffc`, `14:0x3ffc000`, `26:0xfc000000` | `8132a432`, `8132a45a`, `8132a686`, `8132a6a2` |
| `0xa20` | `0:0x3f`, `6:0x3ffc0`, `18:0x3fc0000` | `81329d10`, `81329d20`, `8132a422`, `8132a442`, `8132a456` |
| `0xa24` | `2:0x3c` | `81329d0c`, `81329d1c`, `81329d6e` |
| `0xa64` | `15:0xf8000`, `21:0xffe00000`, sign 20 | `81329f16`, `81329f1a`, `81329f72` |
| `0xa68` | bit 0 | `81329f6e` |
| `0xb0c` | `15:0x1f8000`, `21:0x7e00000`, `3:0x3f8`, `9:0x7e00` | `81324d76`, `81324d7a`, `81324e6e`, `81324e72` |
| `0xb10` | `7:0xf80`, sign 12 | `8132a1fa`, `8132a1fe` |
| `0xb14` | `5:0x1ffe0`, `17:0x3e0000`, `23:0xf800000`, `29:0xe0000000`, sign 22, sign 28 | `8132a152`, `8132a556`, `8132a568`, `8132a58a`, `8132a58e`, `8132a5ae`, `8132a5b2` |
| `0xb18` | `0:0x7`, `3:0xf8`, `9:0x3e00`, `21:0x3e00000`, `27:0xf8000000`, sign 2, sign 8, sign 14, sign 26 | `8132a49a`, `8132a4a4`, `8132a4c2`, `8132a4c6`, `8132a50a`, `8132a50e`, `8132a52e`, `8132a532`, `8132a552`, `8132a564` |
| `0xb1c` | `1:0x1ffe`, `13:0x1ffe000`, `25:0xfe000000`, sign 0 | `8132a3be`, `8132a3d6`, `8132a3e6`, `8132a496`, `8132a49e` |
| `0xb20` | `0:0x1f`, `5:0x1ffe0`, `17:0x1ffe0000` | `8132a39a`, `8132a3aa`, `8132a3ba` |
| `0xb24` | `9:0x1ffe00`, `21:0xffe00000` | `8132a372`, `8132a38a` |
| `0xb28` | bit 0, `4:0xfff0`, `16:0x1f0000`, sign 21 | `81329d7e`, `8132a30e`, `8132a312`, `8132a35e`, `8132a36e` |

The corrected page has five read words.
The offsets use the `0x221c2000` base.

| Offset | Fields and masks | Consumer PCs |
| --- | --- | --- |
| `0xd0` | `28:0xf0000000` | `2000574a` |
| `0xd4` | `0:0x1` speed bin, `17:0x7fe0000` IDDQ, `27:0xf8000000` IDDQ with `0xd8[4:0]` | `20005746`, `81324c9a`, `81324cea` |
| `0xd8` | `0:0x1f` IDDQ with `0xd4[31:27]`, `5:0x3fe0` IDDQ, `15:0x1ffffff` IDDQ | `81324cb8`, `81324cee`, `81324d04` |
| `0xec` | `8:0x100` MCN | `20000856` |
| `0x24c8` | `8:0x100` | `81328f6a` |

Literal `0x81328fd0` holds `0x221c2000` and the load at `0x81328f62` uses it.
The read at `0x81328f6a` takes bit eight and passes it as the fifth argument of `0x81329aec`.
That function returns error `-0x21` only when the argument is above `0x20`.
Zero therefore passes the check.
The field name remains unknown, and the value has no proven silicon meaning.

Literal `0x81329d30` holds the raw base and the load at `0x81329cce` uses it.
Function `0x81329ccc` reads `0xa10` for value zero and `0xa24` and `0xa20` for value two.

### Declared-zero safety and its limit

The zero value is safe for the consumer paths below.
Each of these consumers either accepts zero as an encoded value or falls back to declared data.
This list does not cover the VPP voltage-setting records.
SM8850 run572 rejects declared zero in that path.

- The VPP accessor path adds each result to a reference value at `0x81329934..0x8132994c`.
  A zero result is one addition of zero.
  The ratio path sends a non-positive difference to `0x813299b2`.
  That path sets the result to zero and branches past the `sdiv` at `0x81329980`.
  The divisor at that instruction is therefore never zero.
  These arithmetic facts do not make the voltage-setting records valid.
- The IDDQ path reads `0xb0c` at `0x81324d76` and `0x81324e6e`.
  A zero mask makes the `and` at `0x81324dc6` or `0x81324ed0` produce zero.
  The `orreq` at `0x81324dce` or `0x81324ed8` then writes the default mask `0x26`.
  `cbz` at `0x81324df2` and `0x81324f0c` skips the store, so the low byte stays clear.
- The flex-ID gate at `0x81329d7e` takes `0xb28[3:1]`.
  `cmp r3,#6` at `0x81329d8a` sends only a value above six to the assert at `0x81329d8e`.
  Zero passes that check.
- The corrected IDDQ fields fall back to the `K_LKG_DEFAULT_VALUE` and `K_LKG_DEFAULT_VALUE_QMX` keys in `FUN_20016c44`.
  `FUN_20016c44` takes the fuse value only when it is nonzero.
- The speed bin and the MCN build configuration prefixes.
  `0x81327a90` registers each prefix in increasing specificity, so a zero level selects a broader prefix.
- Corrected `0x24c8[8]` is the fifth argument of `0x81329aec`.
  The callee rejects only a value above `0x20`.

These consumers do not require a nonzero fuse value.
The remaining panics in these paths depend on configuration data:
- A missing `K_LKG_NCC_THRESHOLD` key asserts at `0x81324b8a` with `dpm_soc.c` line `0x4c`.
- A missing VPP plan asserts `Voltage or Fuse Table is NULL` at `0x81329906`.
- A missing fuse-adjust row asserts `Fuse Adjust Settings not found` from the path at `0x8132998e`.
These outcomes do not depend on a QFPROM value.
The VPP voltage-setting check is different.
It requires nonzero record fields, so it needs the real page.

### Run572 VPP assertion

SM8850 run572 executes the genuine PDP image with the declared-zero policy.
The VPP path asserts at `0x8132937e` with `VPP: Voltage Setting Fuses not valid`.
The source is `nccpalfw/drivers/power/vpp/vpp_process_pegasus.c`.
The check reads a voltage-setting record.
The record selects a subset of the accessor table.
A record is valid only when its int16 fields at `+0x1c` and `+0x1e` are nonzero.
Zero calibration words produce zero fields.
The check therefore fails on the first record.
Declared zero is not a safe substitute for the SM8850 VPP volume words.

### Private calibration page

One private read of a selected phone's raw QFPROM page supplies this record.
The record is `20261026T230217Z-phone-qfprom-calibration-read`.
The page covers raw `0x221c8000..0x221c8fff`.
Its SHA-256 is `ac366204888a7f6c4fd32921563b3b641dd896b10fcf32351e1d8832e2d0ae64`.
The page and its values stay out of the repository.
Only this record identifier and hash appear in the repository.

The manifest input `calibration_fuses` names the page path and its hash.
Prepare copies the page to the prepared directory as `calibration-fuses.bin`.
Prepare and run both require exactly 4096 bytes and the declared hash.
A mismatch stops the command.
The run passes the copied path to the platform as the `calibration_fuses` option.
SM8850 uses that option only when the manifest supplies it.
SM8845 rejects the input.
The model returns the little-endian raw word for every listed offset.
Unlisted offsets keep their address error.
The page does not widen the fuse aperture.

### Corrected words beyond the current owners

`0x221c20d8` is a new consumer word.
The current `pdp_pte_speed_bin` window covers only `0x221c20d0..0x221c20d7`.
Extend that window to `0x221c20db` or add one window for `0x221c20d8`.
The word reads zero under the declared policy, and the IDDQ fallback accepts that value.
The existing owners for `0x221c20d0`, `0x221c20d4`, and `0x221c20ec` remain sufficient.
The private page covers only the raw `0x221c8xxx` aperture, so it does not change this word.

`0x221c24c8` is a second new consumer word, and no current window covers it.
Add one read-only window for that word.
The existing `0x221c2418` and `0x221c2188` windows do not reach it.
The word reads zero under the declared policy, and the `0x20` bound accepts that value.

### Unmodelled IDDQ writes

Function `0x81324d64` programs IDDQ registers after it reads `0xb0c`.
The veneer at `0x2000c8f0` reaches it from `0x20004b98`.
The base is `0x221c8000 - 0xa800000 - 0xb5000`, that is `0x17913000`.
The writes are:
- `0x17913004`, `0x17913008`, `0x1791300c` for rail zero.
- `0x17913104`, `0x17913108`, `0x1791310c` for rail one.
- `0x17915000`, where bits 20:16 become `0x1e` and bits 15:0 become `0x12c`.
- One doubleword at `0x180c05e0` for rail zero and at `0x182c05e0` for rail one.

These addresses are not QFPROM fuse words.
The current platform routes `0x17913000` and `0x17915000` through its system window, and no register owner answers there.
The addresses `0x180c05e0` and `0x182c05e0` fall outside the current windows.
Map them or record an explicit substitute before this path runs.

## CPR controller consumers

The calibration inputs feed the original voltage calculations and CPR controller programming.
`models/qcom/cpr/controller.h` implements the shared `qcom,cpr3-controller-v1` device.
The board helpers declare controller geometry from each selected XBL image.

| Selected table | SM8850 | SM8845 |
|---|---|---|
| Settings getter | `0x1484ba20` | `0x14849ea8` |
| Settings object | `0x14915458` | `0x14915eb8` |
| Controller pointers | `0x149145a0`, ten entries | `0x14915560`, nine entries |
| Rail pointers | `0x14914a30`, seventeen entries | `0x149159a8`, sixteen entries |
| Controller initializer | `0x1484bdec..0x1484c0c0` | `0x1484a290..0x1484a5ac` |

Each controller record supplies its base at `+0`, thread count at `+0xc`, and configuration pointer at `+0x18`.
The configuration's first byte gives the sensor count.
Every selected controller has one thread, with ID zero.
Each aperture contains `0x4000` bytes.

| Rail | Base | SM8850 sensors | SM8845 sensors |
|---|---|---|---|
| MSS | `0x2b580000` | 29 | 29 |
| CX | `0x00628000` | 50 | 37 |
| GFX | `0x03d9a000` | 41 | 37 |
| MXG | `0x03d8a000` | 39 | Not selected |
| NSP2 | `0x00634000` | 8 | 4 |
| LPI_CX | `0x06e70000` | 4 | 4 |
| MM | `0x00630000` | 34 | 33 |
| MX | `0x00638000` | 74 | 34 |
| MXC | `0x0062c000` | 39 | 64 |
| NSP | `0x0063c000` | 10 | 9 |

SM8850 `0x1484bdac..0x1484bde8` programs targets at `+0x840 + 0x40*mode + 4*RO`.
Instructions `0x1484c180..0x1484c264` establish the selected thread stride of `0x740`.
The selected stride holds a `0x40` header and 28 target modes.
UEFI CPRDxe RVA `0xb968` programs the GFX targets for modes 16 and higher, for example at `+0xc40`.
The model therefore keeps targets for 28 modes in each thread.
The mode selection field stays four bits wide, because no selected firmware writes a higher mode there.
Instructions `0x1484bf88..0x1484bfac` program byte-map fields at `+0x3640 + 4*i`.
The largest selected initialization offset is `0x3658`.

UEFI CPRDxe also programs `CPR_STEP_COUNTER_SETTINGS_n`.
Entry zero sits at `+0x32d4` with a MAX_STEPS field.
CPRDxe RVA `0xb940` writes `((a - b) / c) & 0xff` to entry `n + 1` for each mode of a rail's voltage plan.
Its caller at RVA `0x2e84` loops over the mode count from the plan data.
A diagnostic run (run768) logged every write above `0x3314`.
Only the GFX controller wrote there, and its last entry is 19 at `+0x3320`.
The model retains `+0x32b0..+0x3320` as configuration.
The ideal sensors produce no error, so these step limits change no voltage.

The public [CPR regulator driver](https://git.cdn.openwrt.org/openwrt/staging/hauke/plain/target/linux/qualcommax/patches-6.1/0901-regulator-add-Qualcomm-CPR-regulators.patch)
corroborates the core register fields.
The implementation copies no proprietary code.

The virtual oscillator matches each active target.
A measurement requires an available sensor, an enabled thread, an unmasked oscillator, a target, and a gate counter.
The result reports the target extrema and oscillator selectors with zero voltage error.
Missing prerequisites leave BUSY set without a new DONE event.
Enable publishes pending masks.
CONT requests another measurement, and interrupt clear removes only the selected status bits.
The timer repeats measurements when the MID interrupt is disabled.

The device retains configuration across disable.
It does not drive voltage, CPRF transactions, external interrupts, or authentication events.
Aging, HTOL, and 64-RO measurements remain unsupported and report a warning.
No fabricated aging result exists.

The AOSS CPRF block is a separate owner. `qcom,rpmh-cprf-v1` occupies `0x0c200000` with size `0x10000`.
UEFI CPRDxe writes the rail voltage limits, the safe voltage, one CTRL field, the VRM resource, and the enable bit there.
That block keeps the words and applies no voltage change, for the same ideal-sensor reason.
[The CPRF evidence](../../cpr/evidence.md) records the decoded accesses.

The AArch64 `cpr` fixture checks missing prerequisites, target-derived results, mask publication, interrupt clear, and disable/re-enable.
The targeted run passes.
The integration run passes 76 tests and Clippy with warnings denied.
SM8850 run62 passes the original controller initialization and next accesses `0x0c260304` at `0x14899e40`.
SM8845 run58 instead times out during an earlier APSS reset, before UART output.
That run does not prove SM8845 controller execution.
Kernel execution remains unobserved.

## CPUCP manufacturing and debug identity group

SM8850 run262 and SM8845 run211 stop at R32 `0x221c20ec`.
Their PCs are `0x812d41b2` and `0x8124415e`.
Both runs passed the native timer and the shared system identity view.
These observations do not establish CPUCP service execution.

Both selected `cpucp.elf` files initialize this complete fuse-read group:

| Read | SM8850 PC | SM8845 PC | Retained value |
|---|---|---|---|
| `0x221c20ec` | `0x812d41b2` | `0x8124415e` | Bit 8 goes to `0x18b3057b` / `0x18b30593` |
| `0x221c8780` | `0x812d41ca` | `0x81244176` | Bit 11 goes to `0x18b3057c` / `0x18b30594` |

The first destination is not `0x18b3017b`.
For SM8850, its address is `(0x812d41bc + sign_extend32(0x9785c000) + 0x3bf) & 0xffffffff`.
The SM8845 address uses PC `0x81244168`, upper immediate `0x978ec000`, and displacement `0x42b`.
The host does not initialize either destination.

### Register names and their limits

The corrected QFPROM aperture is at `0x021c2000` in the TME view.
Offset `0xec` is `PTE_ROW4_MSB`.
The older definitions disagree on its bit names.
One places bit 8 inside `DIE_X`, and another places it inside a generic `PTE_DATA` field.
Do not assign a physical die-coordinate meaning to this selected firmware's policy bit.

The second read is `TME_CONSOLIDATED_DEBUG_VECTOR_0`, not `QC_CONFIG0`.
Its software-view offset is `0x780`, and bit 11 is `TMEROM_DBGEN`.
That name does not prove the physical bit assignment on either selected part.
The existing `tme_debug_vector0` parameter remains its sole virtual owner.
The canonical `QC_CONFIG0` word remains at offset `0x200`, or `0x221c8200`.

### Actual consumers

Five identified paths consume the first retained byte in each image.
When that byte is nonzero:

| SM8850 read PC | SM8845 read PC | Observed consequence |
|---|---|---|
| `0x1d80dffc` | `0x1d80e668` | The guarded path returns `-1`. |
| `0x812d4294` | `0x81244240` | Initialization skips the `cfg_cpu_present` property override. |
| `0x812d4390` | `0x8124433c` | Initialization skips the `llc_way_mask` property override. |
| `0x812d44ae` | `0x8124445a` | Initialization skips the `qmx_disable` property override. |
| `0x812d5f64` | `0x812477da` | The service-command path returns `-3`. |

The LLC path accesses the flag through the identity structure at offset `0xb`.
The structure bases are `0x18b30570` and `0x18b30588`.
The debug-vector byte has a separate consumer at `0x1d8025a2` / `0x1d802602`.
Those instructions copy it into another guest-owned runtime flag.
These branches do not establish an authentication role for PTE bit 8.

The same initialization group also reads `0x1765100c`, `0x17651020`, and `0x17651018`.
Those addresses are not fuse registers.
They do not justify more words in the corrected fuse aperture.

### Explicit virtual policy and ownership

The required `pte_row4_msb` parameter contains the complete read-only 32-bit word.
The production profile declares zero.
This is a virtual manufacturing value, not a physical fuse measurement.
It keeps the observed bit-8 guards clear and does not assign a physical DIE_X value.
Missing or wider-than-32-bit parameter values fail profile validation.

The existing fuse controller supplies only four bytes through `pte_identity_socket`.
The TME view is `0x021c20ec`.
APSS and CPUCP translate `0x221c20ec` to that same owner.
These mappings define virtual consistency, not a newly captured physical alias.
The socket accepts only complete four-byte reads.
Writes fail. Adjacent corrected rows remain unmapped.
The change does not add a blanket zero-filled aperture or a calibration fallback.

The existing debug-vector, lifecycle, OEM identity, authentication, and rollback policies remain unchanged.
The guest still extracts both bits, applies its property rules, and produces its runtime state.

### Verification

The native fuse fixture uses PTE word `0xa5a50100`, debug vector zero `0x800`, and `QC_CONFIG0=0`.
It checks the complete PTE word, its bit 8, and the independence of the two software-view policy words.
Separate variants require failure on a PTE write and on the next word.
The original reserved-word boundary check remains in place.

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform fuse_reads_compose_existing_words_without_expanding_the_register_map -- --ignored --exact
```

The focused fuse fixture passes, including write rejection and the adjacent-address boundary.
SM8850 run263 and SM8845 run212 pass the PTE read and reach the next identity register at `0x1765100c`.
Their failing PCs are `0x812d425e` and `0x8124420a`, respectively.
The complete integration run passes 47 host tests and 71 platform tests. Clippy passes with warnings denied.


## QFPROM ROM error status for UEFI

Run678 passed the UFS RPMB open and reached Oplus storage initialization.
QSEE then read `0x221c2090` at PC `0x1568c3e8`, before the platform had a register owner.
Offset `0x2090` is a 32-bit ROM status word.
The QSEE accessor extracts bits 17:16, which have no established meaning.
The source-only name `QFPROM_ROM_ERROR` and the error meaning of bit zero remain descriptive labels, not established fields.
The model therefore retains an explicit 32-bit virtual error word, without assigning a meaning to bits 17:16.
The SM8850 profile supplies zero because this virtual fuse ROM has no recorded read error.
The APSS and CPUCP aliases share the TME fuse owner, and writes fail.
The owner logs each successful status read as `QFPROM_ROM_ERROR_STATUS`.

The fixture supplies `0x30000`. It checks the complete word, the extracted bits, and write rejection.
The profile value is not a hardware measurement and does not disable image authentication.

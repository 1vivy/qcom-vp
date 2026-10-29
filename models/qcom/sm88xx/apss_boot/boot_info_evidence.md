# TCSR boot information

## Register identity

`QcomTcsrBootInfo` implements `qcom,tcsr-boot-info-v1`.
The selected ROM accessor at `0x30d828` materializes `0x01ff0000` and returns four words.
Those words carry the failed count, the maximum count, the set-B selection, and the recovery enable.
The source-only register name and the TCSR base composition remain unestablished.
The model accepts aligned full 32-bit reads and writes at local offset zero.
The selected ROM performs reads only, so the masked-write acceptance is a model contract, not an observed access.

| Bits | Field | Meaning |
| --- | --- | --- |
| 5:1 | NUMBER_OF_FAILED_BOOT_ATTEMPTS | Failed boot count |
| 10:6 | NUMBER_OF_MAX_BOOT_ATTEMPTS | Maximum boot count |
| 31 | USE_SET_B | Select image set B instead of A |
| 0 | EARLY_FIRMWARE_FUNCTIONAL_FAILURE_RECOVERY_FEATURE_ENABLE | Enable early firmware failure recovery |
| 30:11 | BOOT_INFO_SPARE | Spare storage |

No vendor source or proprietary binary is copied into the repository.

## ROM evidence and downstream behavior

Evidence binary: `edl-re:pbl8850.bin sha256=91bd8f81efa7a5940d4b6d4c220388b559ceef39b6b47dd7b4b0727a1beb1d1c`, APSS base `0x00300000`.
For each address below, the file offset equals the address minus `0x00300000`.
Instructions were decoded with Capstone in AArch64 little-endian mode.

- `0x30d824-0x30d850`: accessor checks the output pointer, reads the register at `0x30d82c`, and returns four words.
  Their order is failed count, maximum count, set B, and recovery enable.
  The function returns ROM status `0xc5ab3000`, not the register value.
- `0x303010-0x303018`: the static initializer passes `0x1481670c` to this accessor.
  The four output words occupy `0x1481670c..0x14816718`.
- Cached accessor `0x301da8`, command `0x26`, selects branch `0x302010`.
  The byte jump table is at `0x319b20`.
  Branch `0x302010-0x30202c` copies all four cached words to its caller.
- `0x3064a8-0x3064b8`: the image-set selection path requests command `0x26` into `sp+8`.
- `0x306510-0x306538`: recovery enable at `sp+0x14` gates the failed-count/maximum comparison.
  If enabled, `failed >= maximum` selects the alternate path at `0x306590`.
- `0x30653c-0x306558`: below that limit, `USE_SET_B` can invert the normal set selection.
- `0x306574-0x306588`: with recovery disabled, `USE_SET_B` still changes the selected set.
  Thus bit 31 is not a completion indication.

The static initializer ends after command `0x85` at `0x303034-0x303044`.
That command uses the existing OEM_CONFIG0 dependency.
The initializer then writes RAM metadata at `0x303048-0x303064` and returns.
Its caller checks the result at `0x302320-0x302324` and calls `0x303088`.
Function `0x303088-0x3030ec` initializes RAM structures and copies ROM data.
This continuation does not require another boot-status MMIO register.
This finding does not claim that later boot stages require no further hardware.

Only the accessor at `0x30d828` materializes `0x01ff0000` in the decoded APSS region.
No peer completion transition is established for this register.
The model therefore stores guest writes instead of synthesizing completion or failure events.
It does not count reads, advance retries automatically, or change TME bank 6/7 state.

## Explicit virtual reset policy

The platform must supply every reset field. Invalid defaults prevent accidental policy selection.
Use this cold virtual profile:

| Parameter | Value | Rationale |
| --- | --- | --- |
| `failed_attempts` | 0 | No retained failed boot history exists at cold construction. |
| `max_attempts` | 0 | Retry recovery is disabled, so its threshold is inactive. |
| `use_set_b` | 0 | Select normal image set A without a retained override. |
| `recovery_enabled` | 0 | Do not claim an implemented cross-reset failure recovery mechanism. |

These fields encode `0x00000000`. This is a declared virtual policy, not a measured silicon reset value.
Spare bits initially contain zero.
The module accepts aligned, four-byte little-endian reads and writes at local offset zero.
Full-word writes retain all bits, including the spare field.
Other offsets, byte enables, transfer sizes, and commands return TLM errors.
No image-authentication behavior changes.

## FEATURE_CONFIG18 bit 19

This bit has direct consumers. It is not an unused spare in this ROM.

- `0x30d3b4` dispatches command `0x6d` to the read of FEATURE_CONFIG18 at `0x30d560`.
  The read at `0x30d564` extracts bit 19.
- `0x302fbc-0x302fcc` caches the result at `0x148166f4`.
- Cached command `0x20` selects `0x301ef8`, which returns that word at `0x301efc`.
- `0x303c0c-0x303c14` reads the cached flag.
  At `0x303c80-0x303c8c`, zero skips processing for image identifier `0x7b`.
- `0x303e5c-0x303e64` reads the same flag into `sp+0x54`.
  At `0x303fac-0x303fb8`, zero skips the image `0x7b` entry.
- `0x306958-0x306960` reads the flag into `sp+0x54`.
  At `0x30699c-0x3069a8`, image `0x7b` with zero flag branches past processing.
- `0x309c28-0x309c34` reads the flag.
  At `0x309c4c-0x309c54`, zero excludes image `0x7b` from the eligible-image count.

The current [Qualcomm security profile](https://github.com/qualcomm/security-profiles/blob/a69b6decf24456fb622de05ecb9b74e0bf871bee/nord_security_profile.xml)
names `BOOT-BLOCK` with software ID `0x7B` and authenticator `TME-PBL`.
It names `TME-FW` as `0x35` and `XBL-SC` as `0x36`, both with `TME-PBL`.
The model's `0x7b` image is therefore BOOT-BLOCK, not the TME firmware image.
The extraction masks in accessor `0x30d824-0x30d850` establish the field positions in the table above.
This record does not list those masks, so the table needs that instruction-level citation.

The selected manifest `build/private/sm8850.toml:17-19` names package
`CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`.
The extracted package has the following signing metadata:

| Supplied file | Hash-segment offset | SW ID word offset | SW ID |
| --- | --- | --- | --- |
| `IMAGES/xbl_sc.elf` | `0x11c000` | `0x11c030` | `0x36` |
| `IMAGES/signed_firmware_soc_view.elf` | `0x2f000` | `0x2f030` | `0x35` |
| `IMAGES/xbl_config.elf` | `0x5d000` | `0x5d030` | `0x25` |
| `IMAGES/multi_image.mbn` | `0x2000` | `0x2030` | `0x22` |
| `IMAGES/multi_image_qti.mbn` | `0x2000` | `0x2030` | `0x23` |
| `IMAGES/tme_config.elf` | `0x8000` | `0x8030` | `0x94` |

The inspection decoded ELF32/ELF64 program headers and selected Qualcomm hash segments with segment-type bits equal to 2.
These segments use hash-header version 7.
The SW ID is the 32-bit word at hash-segment offset `0x30`.
A scan of all top-level ELF files in package `IMAGES/` and `RADIO/` found no SW ID `0x7b`.
This scan did not inspect embedded files inside filesystem images.
`IMAGES/rawprogram1.xml:5-13` maps the supplied early boot images, including XBL-SC, TME-FW, both multi-image files, and TME configuration.
There is no BOOT-BLOCK file or partition entry in the package XML search.
The package therefore supplies an early chain without a standalone signed BOOT-BLOCK image.
This does not establish the physical device's fuse value.

For this supplied chain, retain bit 19 clear as an explicit BOOT-BLOCK-disabled virtual policy.
Do not describe zero as an inert spare or use it to remove another supplied image.
If a BOOT-BLOCK image becomes part of the selected chain, set bit 19 (`0x00080000`) and execute its normal authentication.
Do not substitute XBL-SC or TME-FW for BOOT-BLOCK.
Normal image verification remains required for the supplied XBL-SC, TME-FW, and other boot images.
This boot-information model does not modify the fuse or image-authentication behavior.

## Platform integration

`src/platform.cc` registers `QcomTcsrBootInfo` as `qcom,tcsr-boot-info-v1`.
The shared board maps its four-byte socket at `0x01ff0000` on the APSS router.
Its explicit cold profile sets all four reset fields to zero.
Neighboring TCSR offsets remain outside that mapping.

The integrated OEM execution passed the boot-information read and reached UFS initialization.
See `docs/qcom/sm88xx/oplus/bringup.md` for the current run and its next hardware boundary.
This observation does not establish image-authentication completion or kernel entry.

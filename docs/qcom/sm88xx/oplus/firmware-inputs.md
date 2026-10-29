# Firmware inputs

This page describes the existing OEM preparation and boot workflow.
For external compositions, arbitrary dump layouts, raw storage, or storage-less research, use the [environment runbook](../../../environments.md).

This repository contains no vendor binary.
You supply each firmware input as a local file.
A manifest names each file and its SHA-256.
`qcom-vp prepare` checks every hash before it creates output.
[firmware/qcom/sm88xx/oplus/example.toml](../../../../firmware/qcom/sm88xx/oplus/example.toml) shows the format. Its hash values are placeholders.

## Input summary

| Manifest key | Required | Content | Checks | Consumer | If absent |
| --- | --- | --- | --- | --- | --- |
| `[pbl]` | Yes | Primary boot loader ROM image, and the CPUCP image inside it | Hash of the image, range, `cpucp_offset` bounds | `apss_rom` and `cpucp_rom` windows in `platforms/qcom/sm88xx/reset.lua` | Preparation fails |
| `[tme]` | Yes | TME ROM image | Hash of the image, range | `tme_rom` window in `platforms/qcom/sm88xx/reset.lua` | Preparation fails |
| `[package]` | Yes | OEM flash package (ZIP) | Hash of the file, member checks in `storage.rs` | Six virtual LUN files, the kernel reference, the observer sites | Preparation fails |
| `board` | Yes | Label for the device | Lowercase letters, digits, hyphens | Selects the board file | Preparation fails |
| `[board_file]` | No | Board file | Hash of the file, must define `ufs_boot_lun` | Boot LUN selection in the generated Lua configuration | The installed file for the label |
| `[oem_root]` | No | Complete DER certificate of the OEM root | Hash of the file | SHA-384 of the file enters the fuse profile as `oem_pk_hash_sha384` | Root-hash reads fail |
| `[sequencer_root]` and `sequencer_entitlement_sha256` | No, both or neither | P-521 issuer key (136 bytes) and the digest of the accepted entitlement | Hash, size, pairing | Virtual TME boot status model | Sequencer authentication fails closed |
| `[calibration_fuses]` | No | Raw QFPROM calibration page, 4096 bytes, SM8850 only | Hash, size, platform | `calibration_fuses` option of the fuse controller | The listed calibration words read as declared zero |
| `[tme_runtime_donor]` | No | Private donor image for the pinned TME reconstruction | Hash, the embedded recipe | `prepare_tme_runtime` in `main.rs` | The original TME runs unchanged |
| `patches` | No | Ordered patch recipes for `pbl` or `tme` | Input hash, span hash | `firmware::apply_patch` | No patch |
| `omit_secdata` | No | Omit the `sec.dat` write when the package lacks it | None | `storage::assemble` | Preparation reports the missing member |

`platform` is `sm8850` or `sm8845`.
`rom_compatibility` must be `true` for `sm8845` and `false` for `sm8850`.
`auth` must be `retail`.

## ROM dumps and offsets

A dump can be larger than the ROM image.
The `offset` key gives the file offset of the image. The default is zero.
The hash covers only the image bytes, from `offset` to `offset` plus the image size.
The file must contain the complete image.

| Image | Size | Address in the platform | Default offset key |
| --- | --- | --- | --- |
| PBL | `0x3c000` | `0x300000` (APSS) | `[pbl] offset = 0` |
| CPUCP (inside the PBL image) | `0x8000` | `0x334000` (CPUCP) | `[pbl] cpucp_offset = 0x34000` |
| TME | `0x20000` | `0x200000` (TME) | `[tme] offset = 0` |

`cpucp_offset` counts from the start of the PBL image, not from the start of the file.
The CPUCP image must lie inside the PBL image.
`cpucp_offset` is not valid in `[tme]`.
The sizes and addresses belong to the SM8850 memory map in `platforms/qcom/sm88xx/reset.lua`.
They are not manifest keys.

Preparation copies each image to `pbl.bin` and `tme.bin` in the output directory.
Each copy starts at offset zero.
Patch offsets refer to these copies. `inputs.json` retains original paths, image offsets, and hashes.
`prepared.json` records the original manifest and the prepared file identities.
The CPUCP offset appears in `prepared.json` as `cpucp_offset`.

```toml
[pbl]
path = "/private/full-dump.bin"
sha256 = "<hash of the PBL image bytes only>"
offset = 0x1000
cpucp_offset = 0x34000

[tme]
path = "/private/full-dump.bin"
sha256 = "<hash of the TME image bytes only>"
offset = 0x80000
```

## Patches

A patch recipe is a TOML file with these keys:

- `schema = 1`
- `name` and `reason`
- `target`, either `pbl` or `tme`
- `input_sha256`, the hash of the prepared copy before the patch
- `offset`, counted in the prepared copy
- `expected_span_sha256`, the hash of the bytes that the patch replaces
- `replacement_hex`

Preparation applies patches in manifest order.
A patch with a wrong span hash does not change the file.
The pinned TME reconstruction rejects a patch that changes its required TME image.

## Package

The package is an OEM flash ZIP.
`storage.rs` reads these members:

- `IMAGES/provision_samsung.xml`, `IMAGES/Setting.xml` and `IMAGES/Projectconfig.xml`
- `IMAGES/rawprogram0.xml` to `rawprogram5.xml` and the matching `patch` files
- Each image that a `rawprogram` record names
- `IMAGES/boot.img`, which supplies the kernel reference

A package from another OEM works only if it has the same layout.
Preparation reports the first missing member.
The automatic observer needs `IMAGES/boot.img` and the UEFI and ABL images from the package.
It also needs a hash-bound profile for the kernel image.
For other firmware, run with `--apss-gdb` and use your own debugger.

## Board

`board` is a free label.
Without `[board_file]`, a label `<vendor>-<device>` selects `boards/qcom/sm88xx/<vendor>/<device>.lua` in the installed data.
The installed examples use `oplus-infiniti` and `oplus-cph2771`.
Supply `[board_file]` for another file or label format.
The file is a Lua chunk that returns a table. The table must define `ufs_boot_lun`.
Preparation copies the chosen file to `board.lua`, and the run uses that copy.
The installed board files select boot ID one as virtual reset policy.

## Check before you run

1. Hash each input and compare it to the manifest.
2. Run `qcom-vp prepare` with a new output directory.
3. Read the `inputs.json` file in the output directory. It lists the resolved path and hash of each input.
4. Run `qcom-vp run --prepared <directory> --out <new directory> --stop-at kernel-entry`.

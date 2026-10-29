# Selected TZ protection controllers

## Scope and source

QSEE reads protection controllers through APSS after the crypto BAM setup.
SM8850 run230 reads `0x0747600c` at PC `0x157b9084`.
SM8845 run185 reads `0x09c0000c` at PC `0x157af920`.
Each address is a controller REV register at offset `+0xc`.
The source images are the `RADIO/tz_ac_config.img` files in `models/qcom/vmidmt/tz-platform-evidence.md`.
The AC ELF maps file offset `0x1000` to VA `0xd8019000` on both platforms.

## Controller inventory

AC root index 7 points to the inventory, and root index 8 points to its count.

| Platform | Root 7 location | Inventory file offset | Count |
| --- | --- | --- | ---: |
| SM8850 | `0x7040` | `0x1310` | 84 |
| SM8845 | `0x6c88` | `0x1320` | 78 |

Each record has `0x28` bytes. The controller base is at `+0xc`, and the resource ID is at `+0x10`.
The lookup at SM8850 `0x157bacf8` compares the resource ID at `+0x10`.
The identity reader at SM8850 `0x157b9070` or SM8845 `0x157af90c` reads `+0xc`, `+0x0`, `+0x4` and `+0x8`.
Thus QSEE needs an APSS configuration view for each controller.

The platform files list the complete inventories as `{resource, base}` pairs.
`platforms/qcom/sm88xx/tz-protection-cohort.lua` appends each TZ pair that the XBL cohort does not contain.
A shared address must keep the same resource ID. A conflict stops the composition.
The existing XBL helper then attaches the APSS view to the shared runtime controller.

## Controller type and capacity

QSEE selects the policy-array stride from IDR0 bits `[1:0]`.
The policy validator at SM8850 `0x157b91d8` reads the cached IDR0 at `+0x14`.
A nonzero value selects 16-byte fixed-resource records. Zero selects 32-byte programmable records.
A fixed record keeps the region index at `+0xc`. A programmable record keeps it at `+0x1c`.
Thus the controller type must match the policy-array shape.

QSEE predicate `0x1575da20` selects a record when its selector has no bit outside the feature mask.
Most records use selector 1. Five programmable arrays also use selectors `9`, `0x8001` or `0x10001`.
The required capacity is the highest region index in the array plus one, for every selector.
`tz_protection` in each runtime-data file supplies the type and capacity that the TZ arrays require.
`platforms/qcom/sm88xx/tme-runtime.lua` applies these rows after the TME and XBL rows.
Each row only raises a capacity. Its `source` is the AC file offset of the policy record.
These values are virtual geometry, not IDR measurements.

| Platform | Change | Controllers |
| --- | --- | --- |
| Both | Programmable to fixed | `0x03f20000`, `0x041f8000`, `0x0061a000`, `0x100e0000`, `0x00a00000`, `0x00800000` |
| SM8845 | Programmable to fixed | `0x01900000`, `0x07a00000`, `0x07900000`, `0x01d8a000` |
| SM8845 | Capacity | `0x01e50000` 90, `0x002f0000` 200, `0x34c60000` 71, `0x0f000000` 270 |
| SM8850 | New controller | Ten controllers in the table below |
| Both | Aperture `0x100000000` | `0x07f30000` |

The type changes affect controllers that only appear in the TME client list.
No selected TME or XBL descriptor programs these controllers.

QSEE bounds helper `0x157bada8` masks each bound with the IDR1 width and alignment when no client row applies.
The TZ record for resource `0x96` region 1 is `[0xff800000,0x100000000)`.
Its 32-bit encoded end is zero. The declared aperture gives that zero end its exclusive 4 GiB limit.
Without it, the model rejects the activation write at `0x07f31044` with a command error.

SM8850 has ten TZ controllers without a TME owner. SM8845 has a TME owner for each TZ controller.
Each new SM8850 controller uses 64 regions, 32 address bits and 4 KiB alignment, like the TME clients.

| Resource | Base | Policy record | Records | Type |
| --- | --- | --- | ---: | --- |
| `0x4c` | `0x01900000` | `0x2508` | 36 | Fixed |
| `0xfe` | `0x01ac8000` | `0x2530` | 0 | Programmable |
| `0xa2` | `0x01a00000` | None | 0 | Programmable |
| `0xba` | `0x07470000` | `0x25d0` | 5 | Programmable |
| `0x0b` | `0x01890000` | `0x2620` | 13 | Fixed |
| `0xf9` | `0x07850000` | `0x2648` | 4 | Programmable |
| `0x95` | `0x07840000` | `0x2670` | 4 | Programmable |
| `0xfa` | `0x07a00000` | `0x2698` | 45 | Fixed |
| `0x4a` | `0x07900000` | `0x26c0` | 45 | Fixed |
| `0x10` | `0x01d8a000` | `0x2738` | 3 | Fixed |

No policy array exists for `0xfe` and `0xa2`. Their programmable type is a virtual choice.

## Policy-control words

AC root index `0x1d` points to `0x28`-byte policy-control records. Root index `0x1e` points to the count.
SM8850 has 8 records at file `0x10e0`. SM8845 has 9 records at file `0x10f8`.
Each record selects a resource at `+0x1c`, one masked word at `+0x8`, and a masked array at `+0x10`.
The writer at SM8850 `0x15769cbc` writes `(old & ~mask) | (value & mask)` and compares the readback.
A different readback returns error `0x7d`.

| Resource | Words | Owner |
| --- | --- | --- |
| `0x84` | `0x01de4000` | Existing XBL boot policy control |
| `0x85` | `0x03f24000`, `0x03f24004` | Retained words with label `xpu` |
| `0xf4`, `0xf5`, `0x96` | `0x07f08000` | Existing XBL boot policy control |
| `0xed` | `0x09b2a000` | Retained words with label `xpu` |
| `0xa0` | `0x01d10000` | Retained words with label `xpu` |
| `0x87` | `0x08b1f000` on SM8845 | Retained words with label `xpu` |
| `0xb5` | `0x17d00008` | Existing XBL boot policy control |

Each retained word keeps the guest value with a zero virtual reset. It does not grant access or change a region.

## Ownership readback

SM8850 run232 stops in the QSEE fatal loop at `0x15759b20`.
Validator helper `0x157b9398` first claims the complete capability `0xc0000001`.
Then it writes the capability minus the released class, for example `0x80000000`.
It requires the readback to keep the requested bits, which include bit zero.
The earlier model removed bit zero, so the helper returned `0xf3`.

The model now keeps the shared QAD-zero bit when a retained security qualifier survives the write.
A qualifier-only write still cannot acquire a qualifier that the requester did not own.
The namespace fixture expects owner `0x80000003` after this release, not `0x80000002`.
The permission result does not change.

## Limits

The policy-control words and the new controllers do not connect a data client.
The model does not prove the silicon IDR values or the hardware identity of each controller.

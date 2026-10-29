# Selected TZ VMID mapper instances

## Scope and source

`platforms/qcom/sm88xx/xbl-vmid-mappers.lua` composes the union of the XBL and TZ inventories.
It retains the existing XBL modules for identical port/base pairs.
It does not change the VMID mapper register implementation.
The source packages match those in `platform-evidence.md`.

| Platform | Package member | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| SM8850 | `RADIO/tz_ac_config.img` | 64868 | `1bfb0381a1c35ba6c59f4c154addd08dea6a4d0a3d1decdd91833888d66c499f` |
| SM8845 | `RADIO/tz_ac_config.img` | 60096 | `6163354d7bcbf75e80223462448b03ce7352ecf9dd6a541a50d707dc212fd4b5` |
| SM8850 | `RADIO/tz.img` | — | `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932` |
| SM8845 | `RADIO/tz.img` | — | `b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09` |

The AC ELF maps file offset `0x1000` to VA `0xd8019000` on both platforms.
Thus, `VA = file offset + 0xd8018000` for the data below.
No firmware payload bytes enter the repository.

## Hardware inventory and field interpretation

The AC header at file `0x1000` contains magic `0x072382d3` and version 1.
Its pointer at `+0x10` selects the root array.
The array starts at file `0x7008` on SM8850 and `0x6c50` on SM8845.
Root indices `0x48` and `0x49` supply the inventory pointer and the count pointer.
These are data pointers. The table has a separate count, not a zero sentinel.

| Platform | Inventory pointer location | Inventory file range | Count pointer location | Count location | Count |
| --- | --- | --- | --- | --- | ---: |
| SM8850 | `0x7248` | `[0x4ef8,0x5118)` | `0x7250` | `0x5118` | 17 |
| SM8845 | `0x6e90` | `[0x4c18,0x4df8)` | `0x6e98` | `0x4df8` | 15 |

Inventory SHA-256 hashes:

- SM8850: `e7b1b1d7c3e76bbc9f7c148307f5d56b85a2ceae714769b0f41b700ef829eb34`.
- SM8845: `2245300b387d9235cfe3951b62a5cbbf54651e2f1bd9592581568673e6b5e6e0`.

Each hardware record occupies `0x20` bytes.
The following interpretation separates the raw layout from the HAL descriptor contract.
The embedded-descriptor interpretation is [INFERENCE] from the matching field order and the QSEE getter's accesses.
The public `hal_vmidmt_info` structure defines the embedded descriptor's field order:

<https://github.com/qualcomm-linux/trusted-firmware-a/blob/f7787c3627e200933f02b93b1c25b8eb5bcf797d/drivers/qti/accesscontrol/vmidmt/vmidmt_hal.h#L358-L378>

| Record offset | Width | Interpretation | Selected values |
| --- | ---: | --- | --- |
| `+0x00` | 8 | Port slot. The low byte selects the HAL port. Upper bytes are zero. | See tables |
| `+0x08` | 8 | MMIO base | See tables |
| `+0x10` | 2 | Descriptor entry-count prefill | Column `a` below |
| `+0x12` | 2 | Descriptor VMID-count prefill | 0 |
| `+0x14` | 1 | Descriptor SSD-width override | Column `b` below |
| `+0x15` | 1 | Descriptor SID-width prefill | 0 |
| `+0x16` | 1 | Descriptor IAS prefill | 0 |
| `+0x17` | 1 | Descriptor stream-match prefill | 0 |
| `+0x18` | 8 | Zero extension slot. Its nonzero meaning remains unproved. | 0 |

The raw decoding `{u64 port, u64 base, u32 a, u32 b, u64 zero}` gives the same values.
However, `a` and `b` are not independent requested geometry parameters for the HAL initializer.
The getter overwrites the descriptor count, VMID count, SID width, IAS, and stream-match flag from ID registers.
It retains a nonzero SSD-width override.
The direct import path from these root indices into the DAL descriptors remains unproved.
Thus, the embedded field interpretation does not prove a native compiler structure for the complete record.
No decoded consumer gives the zero extension slot a nonzero meaning.

## Complete hardware inventories

Ordinals below are zero-based positions in the hardware inventory, not initialization order.
Every aperture occupies `[base,base+0x1000)` in the virtual platform.
`Reuse` means the same existing XBL module owns that port/base pair.
`New` means this change adds the mapper.
No instance overlaps a different APSS owner in the composed Lua platforms.

### SM8850

| Ordinal | File offset | Port | Base | a | b | Ownership |
| ---: | --- | --- | --- | --- | --- | --- |
| 0 | `0x4ef8` | `0x02` | `0x01dc0000` | `0x15` | 5 | New |
| 1 | `0x4f18` | `0x4a` | `0x01def000` | `0x40` | 6 | New |
| 2 | `0x4f38` | `0x28` | `0x03f30000` | `0x30` | 7 | New |
| 3 | `0x4f58` | `0x47` | `0x06cb1000` | `0x09` | 4 | New |
| 4 | `0x4f78` | `0x0b` | `0x06c00000` | `0x0e` | 4 | New |
| 5 | `0x4f98` | `0x40` | `0x06cb2000` | `0x11` | 5 | New |
| 6 | `0x4fb8` | `0x4c` | `0x07f0c000` | `0x80` | 7 | New |
| 7 | `0x4fd8` | `0x49` | `0x06cb0000` | `0x06` | 3 | New |
| 8 | `0x4ff8` | `0x41` | `0x06cb3000` | `0x09` | 4 | New |
| 9 | `0x5018` | `0x3f` | `0x088dd000` | `0x01` | 0 | New |
| 10 | `0x5038` | `0x19` | `0x00ac6000` | `0x30` | 6 | Reuse |
| 11 | `0x5058` | `0x3e` | `0x008c6000` | `0x30` | 6 | Reuse |
| 12 | `0x5078` | `0x5c` | `0x019c6000` | `0x30` | 6 | Reuse |
| 13 | `0x5098` | `0x5d` | `0x01ac6000` | `0x30` | 6 | Reuse |
| 14 | `0x50b8` | `0x3b` | `0x070c0000` | `0x01` | 0 | New |
| 15 | `0x50d8` | `0x63` | `0x07ac6000` | `0x30` | 6 | New |
| 16 | `0x50f8` | `0x39` | `0x079c6000` | `0x30` | 6 | New |

### SM8845

| Ordinal | File offset | Port | Base | a | b | Ownership |
| ---: | --- | --- | --- | --- | --- | --- |
| 0 | `0x4c18` | `0x3e` | `0x008c6000` | `0x30` | 6 | Reuse |
| 1 | `0x4c38` | `0x19` | `0x00ac6000` | `0x30` | 6 | Reuse |
| 2 | `0x4c58` | `0x5c` | `0x019c6000` | `0x30` | 6 | Reuse |
| 3 | `0x4c78` | `0x02` | `0x01dc0000` | `0x15` | 5 | New |
| 4 | `0x4c98` | `0x4a` | `0x01def000` | `0x40` | 6 | New |
| 5 | `0x4cb8` | `0x28` | `0x03f30000` | `0x30` | 7 | New |
| 6 | `0x4cd8` | `0x0b` | `0x06c00000` | `0x0e` | 4 | New |
| 7 | `0x4cf8` | `0x47` | `0x06cb1000` | `0x09` | 4 | New |
| 8 | `0x4d18` | `0x40` | `0x06cb2000` | `0x11` | 5 | New |
| 9 | `0x4d38` | `0x41` | `0x06cb3000` | `0x09` | 4 | New |
| 10 | `0x4d58` | `0x3b` | `0x070c0000` | `0x01` | 0 | New |
| 11 | `0x4d78` | `0x39` | `0x079c6000` | `0x30` | 6 | New |
| 12 | `0x4d98` | `0x63` | `0x07ac6000` | `0x30` | 6 | New |
| 13 | `0x4db8` | `0x4c` | `0x07f0c000` | `0x80` | 7 | New |
| 14 | `0x4dd8` | `0x3f` | `0x088dd000` | `0x01` | 0 | New |

## QSEE policy selection and initialization order

The hardware inventory does not select the boot initialization order.
A separate packed policy inventory uses 13-byte records.
Its fields are port, error options, SSD pointer, SSD element count, static selection, and private namespace.
These match the first 13 bytes of the XBL policy records documented in `platform-evidence.md`.
All selected records contain error options `0x1f`, a null SSD pointer, zero SSD elements, and private namespace 0.

Root indices `0x23` and `0x24` supply a pointer to this policy inventory and its count pointer.
SM8850 imports these fields at `0x15760c54..0x15760d90` into globals `0x1580c678` and `0x1580c680`.
SM8845 uses globals `0x157ffc78` and `0x157ffc80`.

| Platform | Policy file range | Count location | Count | Port order with static selection |
| --- | --- | --- | ---: | --- |
| SM8850 | `[0x6204,0x626c)` | `0x626c` | 8 | `3e:2,19:2,5c:2,5d:2,02:1,4a:1,28:1,4c:1` |
| SM8845 | `[0x5e54,0x5eaf)` | `0x5eb0` | 7 | `3e:2,19:2,5c:2,02:1,4a:1,28:1,4c:1` |

The loops are SM8850 `0x15766b40..0x15766b90` and SM8845 `0x15759cac..0x15759cfc`.
Each loop checks byte `+11`, advances by 13 bytes, and stops at the imported count.
Selection 2 sets the global suppression flag before the HAL call.
The HAL still reads capabilities for these shared ports but suppresses its table initialization writes.
Selection 1 clears that flag and performs the normal initialization writes.
This explains the first new access at port `0x02`, rather than the hardware inventory's first shared record on SM8845.

The per-record wrappers are SM8850 `0x15766280` and SM8845 `0x157593ec`.
They supply secure extensions `0xff`, a zero secure configuration, and a real configuration-access mask.
The mask selects all 32 VMIDs and permits none initially.
They configure both error banks after successful HAL initialization.
They do not import an SSD policy when its pointer is null.

The HAL dispatch wrappers are SM8850 `0x157b76fc` and SM8845 `0x157adf98`.
They select the numeric port through `/dev/vmidmt`, then call the instance's HAL operations.
The property resolvers are SM8850 `0x157b7bcc` and SM8845 `0x157ae468`.
The remaining hardware instances remain available for later port-specific calls.
This evidence does not assert that QSEE initializes every hardware instance during this boot group.

## Register requirements and failure paths

| Operation | SM8850 PC or range | SM8845 PC or range |
| --- | --- | --- |
| HAL initialization | `0x157b7f10..0x157b8154` | `0x157ae7ac..0x157ae9f0` |
| Capability getter | `0x157b8158..0x157b81a8` | `0x157ae9f4..0x157aea44` |
| First IDR0 load | `0x157b8160` | `0x157ae9fc` |
| Requested-count comparison | `0x157b7fa0..0x157b7fac` | `0x157ae83c..0x157ae848` |
| SCR1 write/read checks | `0x157b7fc4..0x157b8014` | `0x157ae860..0x157ae8b0` |
| SSD initialization | `0x157b803c..0x157b8094` | `0x157ae8d8..0x157ae930` |
| Match and context initialization | `0x157b8098..0x157b8140` | `0x157ae934..0x157ae9dc` |
| Configuration/readback helper | `0x157b81ac` | `0x157aea48` |

These PCs use the ELF link addresses.
The SM8850 run220 trace reports the first fault at runtime PC `0x40d1b8160`.
The SM8845 run176 trace reports runtime PC `0x4245ae9fc`.
Both traces report FAR `0x01dc0020`, an EL1 data abort, and the expected IDR0 load.

The getter reads these fields:

- IDR0 `+0x20`: entry count in bits 7:0, SID width in bits 12:9, and SMS in bit 27.
- IDR1 `+0x24`: SSD width in bits 11:8, only when the descriptor override is zero.
- IDR2 `+0x28`: IAS in bits 3:0.
- IDR5 `+0x34`: VMID count in bits 7:0.

The HAL initially writes all permission bits at `+0x9c`.
A supplied extension count other than `0xff` must not exceed the reported IDR0 count.
The HAL returns 5 when the requested count exceeds that capability.
The selected wrapper supplies `0xff`, which selects the reported count instead.
The HAL checks SCR1 bit 24 and the partition count after each write.
A mismatch returns 7.
The configuration helper also checks retained permissions, control fields, bypass VMID, and auxiliary fields after writes.
The existing mapper retains these fields and preserves those failure paths.

The default path writes `0x111` to secure and nonsecure control registers.
It writes the reported count shifted by eight to SCR1.
It clears both bypass VMIDs and auxiliary registers.
It enables both clock controls and clears both fault-restore registers.
SSD initialization uses the original descriptor override when nonzero, otherwise the newly read width.
Widths below five use one word at `+0x80`.
Widths five through seven use `2^width / 32` words.
A larger width returns 6 rather than inventing a usable table.
The normal path writes all ones to these words.
SMS enables the match-table clear loop at `+0x800`.
The HAL initializes each reported context at `+0xc00` to `0x10000` and each auxiliary context at `+0xe00` to zero.

## Virtual geometry and ownership checks

All instances use the existing virtual policy:

- `entry_count=128`.
- `stream_id_bits=15`.
- `ssd_index_bits=7`.
- `input_address_bits=48`.
- `vmid_count=32`.
- `stream_match_support=true`.

The largest decoded count prefill is 128, and the largest SSD override is seven.
Neither value requires a larger virtual aperture or a different register implementation.
These remain virtual capabilities, not measured silicon reset values.
A retained descriptor override can restrict QSEE's SSD initialization below the model's advertised maximum.

The helper rejects duplicate ports within each source inventory.
It permits cross-source reuse only for identical port/base pairs.
It checks the complete union for aperture overlap and existing APSS owners before it creates modules.
The existing runtime call sites need no change.
The final composition contains 17 SM8850 modules and 15 SM8845 modules.
A Lua-only composition check counted these modules and checked every mapper against all final APSS targets.
Both platforms passed without an overlap.
The check used unused path strings and constructed tables only.
It did not load firmware, open LUN files, or start QEMU.

## Native result and TCSR words

SM8850 run221 and SM8845 run177 pass every TZ VMIDMT initialization.
QSEE then writes all ones to four TCSR words.
The writer is SM8850 `0x157665d4..0x15766770` or SM8845 `0x15759740..0x157598dc`.
It builds the list `0x01fc5040`, `0x01fc3040`, `0x01fc5050`, and `0x01fc2050`.
For each word, the gate at SM8850 `0x157691c4` checks protection resource `0x14`.
The writer stores `0xffffffff` only when that gate sets its flag.
The selected policy sets the flag for all four words.
The caller is the TZ VMIDMT policy loop at SM8850 `0x15766c54`.

`platforms/qcom/sm88xx/qsee-retained-controls.lua` retains each word in an explicit owner with label `tcsr`.
No decoded consumer reads the words, so they report no policy result.
SM8850 run222 and SM8845 run178 pass these writes.
Both then read GPU address `0x03d95000`. The QSEE clock dependency group now owns that branch.

After the TZ protection policy, QSEE writes a second TCSR list and compares each readback.
AC root index `0x14` points to 8-byte `{address, value}` records. Root index `0x16` points to the count.
The SM8850 list is at AC file offset `0x75e4`, and the SM8845 list is at `0x722c`.
Both lists hold four records.

| Address | Value |
| --- | --- |
| `0x01fc50d0` | `0xffffffff` |
| `0x01fc50d4` | `0xffffffff` |
| `0x01fc50d8` | `0xffffffff` |
| `0x01fc50dc` | `0x0000ffff` |

The SM8850 writer stores each value at `0x157698c4` and compares the readback at `0x157698d0`.
A different readback returns error `0x7d`. The same retained-word group with label `tcsr` keeps these words.

## Limits

The direct hardware-inventory import and the zero extension slot remain research gaps.
The TCSR word names and hardware effects remain unknown.
No data-plane permission enforcement follows from register composition alone.
Authentication, authorization, XPU ownership, and TME protection remain unchanged.

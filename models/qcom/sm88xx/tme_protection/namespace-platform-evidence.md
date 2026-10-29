# Selected XBL namespace platform evidence

## Inputs and coordinates

This analysis reads the two selected OEM packages. No device measurements are available.
All firmware coordinates are ELF physical addresses. No vendor bytes are included here.
The files below identify the inputs without private paths.

| Cohort | Member | SHA-256 |
| --- | --- | --- |
| SM8850 | `IMAGES/xbl_sc.elf` | `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a` |
| SM8850 | `IMAGES/xbl_ac_config.mbn` | `afa0b63443a8d6a0588f4e8992a0efc2780b72f2cd1c4052e4357045bc515e39` |
| SM8845 | `IMAGES/xbl_sc.elf` | `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4` |
| SM8845 | `IMAGES/xbl_ac_config.mbn` | `013c2d7749c85d692b231ee51168f956788e131e41b181ae715571b041cad83b` |

## Selection and programming path

Both AC payloads start at `0x14946000`. Their header selects root `0x14946d10`.
The root selects the 40-byte descriptor array at `0x14946778` through offset `0x18`.
The count pointer at offset `0x20` selects 35 records in each payload.
Its address is `0x1494bca8` for SM8850 and `0x1494c068` for SM8845.
The root's offset `0x38` selects the AC lookup array at `0x14946200`.
Its count pointer is `0x14947470` or `0x14947444`, respectively.
These AC arrays are separate from the built-in XBL arrays below.

Each descriptor contains controller base at `+0`, selector at `+4`, resource ID at `+8`, and mode at `+0xc`.
It contains record count at `+0xe` and record pointer at `+0x10`.
Every namespace descriptor uses selector one and mode one, except SM8845 resource `0x16c`.
That record uses selector `0x400` and mode zero. It is not a namespace-enable request.
The selector-one predicate returns true directly at SM8850 `0x14858708`.
Other selectors use the configured feature mask through `0x14858920`.

The namespace pass starts at SM8850 `0x14858970` or SM8845 `0x14856ea4`.
It selects mode one before it resolves each resource's controller.
The GCR helper starts at `0x14859d00` or `0x14858234`.
Its platform/revision gate remains unchanged.
The helper reads GCR, writes literal one when bit zero is clear, and checks bit zero afterward.
It does not merge dynamic-clock bit one into that write.
The first write is APSS `0x0c290100` at PC `0x14859dcc` or `0x14858300`.
The successful checks are at `0x14859ddc` and `0x14858310`.
The first AC descriptor, resource `0x73`, has no region records. It still enables namespace mode.

Region programming starts at `0x14858d08` or `0x1485723c` after the namespace pass.
The region dispatcher starts at `0x1485a0a8` or `0x148585dc`.
Programmable arrays use 32-byte records: start, end, selector, read mask, write mask, and region index.
Fixed-resource arrays use 16-byte records without bounds.
The controller type determines the firmware's record stride.
Thus selecting the correct virtual type is required to interpret these arrays.
The array stride and the selected indices establish requirements, not physical IDR reset values.

The programmer at `0x1485a854` or `0x14858d88` uses region stride `0x40`.
It writes bounds at `+0x100c/+0x1014`, with high words at `+0x1008/+0x1010` when declared.
It writes permission masks at `+0x1018/+0x101c` and activation one at `+0x1004`.
It writes configuration lock one at `+0x1000` when the caller requests that lock.
The ownership helper at `0x1485b484` or `0x148599b8` writes `+0x1030`.
Readback code checks activation, locks, permissions, and normalized bounds.
No platform alias may replace these checks or create separate ownership state.

The APSS capability getter at `0x1485b644` or `0x14859b78` supplies domain IDs zero and seven.
Its returned capability mask is `0xc0000001`. This mask is not the APSS data identity.
The per-domain ownership arrays are firmware RAM shadows, not additional register banks.
For resource `0x73`, SM8850 XBL descriptor `0x148e3340` points to BSS arrays `0x14a14628/0x14a14634`.
SM8845 descriptor `0x148e1460` points to `0x14937628/0x14937634`.
The AC descriptor at `0x149460e0` points to `0x1494be78/0x1494be84` or `0x1494c238/0x1494c244`.
Each descriptor stores minimum index, entry count, read-shadow pointer, and write-shadow pointer.
The helper at `0x1485b880` or `0x14859db4` bounds-checks indices before it stores complete permission words.
These pointers do not justify new MMIO aliases or permission overrides.

## Complete selected AC arrays

`F` means a 16-byte fixed-resource array. `P` means a 32-byte programmable array.
`N` counts records. `Required` is highest selected region index plus one, not a hardware capacity measurement.
The rows include all 35 descriptors in each payload, including the excluded SM8845 mode-zero record.
Both descriptor arrays have the same origin and stride. Coordinates must not be joined by array index across boards.

| SM8850 ID | Descriptor | Array | N | Type | Required | SM8845 ID | Descriptor | Array | N | Type | Required |
| --- | --- | --- | ---: | --- | ---: | --- | --- | --- | ---: | --- | ---: |
| `0x73` | `0x14946778` | `0x0` | 0 | P | 0 | `0x73` | `0x14946778` | `0x0` | 0 | P | 0 |
| `0x76` | `0x149467a0` | `0x14947478` | 24 | P | 38 | `0x76` | `0x149467a0` | `0x14947448` | 25 | P | 38 |
| `0x1e` | `0x149467c8` | `0x14947778` | 1 | P | 1 | `0x1e` | `0x149467c8` | `0x14947768` | 1 | P | 1 |
| `0x3c` | `0x149467f0` | `0x14947798` | 29 | P | 72 | `0x3c` | `0x149467f0` | `0x14947788` | 29 | P | 72 |
| `0xbb` | `0x14946818` | `0x14947b38` | 15 | P | 73 | `0xbb` | `0x14946818` | `0x14947b28` | 18 | P | 73 |
| `0x39` | `0x14946840` | `0x14947d18` | 2 | P | 28 | `0x39` | `0x14946840` | `0x14947d68` | 2 | P | 28 |
| `0x78` | `0x14946868` | `0x14947d58` | 2 | P | 4 | `0x78` | `0x14946868` | `0x14947da8` | 2 | P | 4 |
| `0x79` | `0x14946890` | `0x14947d98` | 2 | P | 4 | `0x79` | `0x14946890` | `0x14947de8` | 2 | P | 4 |
| `0x23` | `0x149468b8` | `0x14947dd8` | 8 | P | 22 | `0x23` | `0x149468b8` | `0x14947e28` | 8 | P | 22 |
| `0x22` | `0x149468e0` | `0x14947ed8` | 71 | P | 125 | `0x22` | `0x149468e0` | `0x14947f28` | 75 | P | 125 |
| `0x1` | `0x14946908` | `0x149487b8` | 30 | F | 41 | `0x1` | `0x14946908` | `0x14948888` | 30 | F | 41 |
| `0x84` | `0x14946930` | `0x14948998` | 31 | F | 41 | `0x84` | `0x14946930` | `0x14948a68` | 31 | F | 41 |
| `0x63` | `0x14946958` | `0x14948b88` | 8 | F | 8 | `0x63` | `0x14946958` | `0x14948c58` | 8 | F | 8 |
| `0x12` | `0x14946980` | `0x14948c08` | 102 | F | 185 | `0x12` | `0x14946980` | `0x14948cd8` | 101 | F | 185 |
| `0x3` | `0x149469a8` | `0x14949268` | 9 | F | 22 | `0x3` | `0x149469a8` | `0x14949328` | 9 | F | 22 |
| `0x82` | `0x149469d0` | `0x149492f8` | 7 | P | 70 | `0x82` | `0x149469d0` | `0x149493b8` | 7 | P | 70 |
| `0x91` | `0x149469f8` | `0x149493d8` | 5 | P | 6 | `0x91` | `0x149469f8` | `0x14949498` | 5 | P | 6 |
| `0x96` | `0x14946a20` | `0x14949478` | 1 | P | 1 | `0x96` | `0x14946a20` | `0x14949538` | 1 | P | 1 |
| `0x3d` | `0x14946a48` | `0x14949498` | 41 | P | 54 | `0x3d` | `0x14946a48` | `0x14949558` | 43 | P | 55 |
| `0x27` | `0x14946a70` | `0x149499b8` | 1 | P | 10 | `0x27` | `0x14946a70` | `0x14949ab8` | 1 | P | 10 |
| `0xde` | `0x14946a98` | `0x149499d8` | 22 | P | 22 | `0xde` | `0x14946a98` | `0x14949ad8` | 22 | P | 22 |
| `0x28` | `0x14946ac0` | `0x14949c98` | 14 | P | 26 | `0x28` | `0x14946ac0` | `0x14949d98` | 15 | P | 26 |
| `0x29` | `0x14946ae8` | `0x14949e58` | 2 | P | 4 | `0x16c` | `0x14946ae8` | `0x14949f78` | 24 | P | 24 |
| `0xf7` | `0x14946b10` | `0x14949e98` | 33 | P | 34 | `0x29` | `0x14946b10` | `0x1494a278` | 2 | P | 4 |
| `0xf8` | `0x14946b38` | `0x1494a2b8` | 3 | P | 3 | `0xf7` | `0x14946b38` | `0x1494a2b8` | 33 | P | 34 |
| `0x56` | `0x14946b60` | `0x1494a318` | 1 | F | 1 | `0x56` | `0x14946b60` | `0x1494a6d8` | 1 | F | 1 |
| `0x7d` | `0x14946b88` | `0x1494a328` | 2 | F | 16 | `0x7d` | `0x14946b88` | `0x1494a6e8` | 2 | F | 16 |
| `0x77` | `0x14946bb0` | `0x1494a348` | 40 | P | 51 | `0x77` | `0x14946bb0` | `0x1494a708` | 40 | P | 51 |
| `0x4d` | `0x14946bd8` | `0x1494a848` | 16 | P | 12 | `0x4d` | `0x14946bd8` | `0x1494ac08` | 18 | P | 19 |
| `0xbd` | `0x14946c00` | `0x1494aa48` | 4 | P | 8 | `0xbd` | `0x14946c00` | `0x1494ae48` | 3 | P | 8 |
| `0x14` | `0x14946c28` | `0x1494aac8` | 44 | P | 94 | `0x14` | `0x14946c28` | `0x1494aea8` | 43 | P | 94 |
| `0x30` | `0x14946c50` | `0x1494b048` | 38 | F | 257 | `0x30` | `0x14946c50` | `0x1494b408` | 38 | F | 257 |
| `0x67` | `0x14946c78` | `0x1494b2a8` | 8 | F | 8 | `0x67` | `0x14946c78` | `0x1494b668` | 8 | F | 8 |
| `0xb5` | `0x14946ca0` | `0x1494b328` | 45 | P | 45 | `0xb5` | `0x14946ca0` | `0x1494b6e8` | 45 | P | 45 |
| `0x32` | `0x14946cc8` | `0x1494b8c8` | 31 | P | 39 | `0x32` | `0x14946cc8` | `0x1494bc88` | 31 | P | 39 |

## Built-in XBL cohort coordinates

SM8850 roots `0x148e3298/0x148e32b0` select the same 33-record table at `0x14925af0`.
Its count is at `0x148e3460`.
SM8845 roots `0x148e13b8/0x148e13d0` select 31 records at `0x14920428`.
Its count is at `0x148e1580`.
These tables contain controller bases and identities, not physical protected-client apertures.

| ID | APSS base | SM8850 record | SM8845 record |
| --- | --- | --- | --- |
| `0x73` | `0x0c290000` | `0x14925af0` | `0x14920428` |
| `0x76` | `0x0c294000` | `0x14925b18` | `0x14920450` |
| `0x1e` | `0x003fe000` | `0x14925b40` | `0x14920478` |
| `0x3c` | `0x310c0000` | `0x14925b68` | `0x149204a0` |
| `0x39` | `0x01524000` | `0x14925b90` | `0x149204c8` |
| `0x78` | `0x01530000` | `0x14925bb8` | `0x149204f0` |
| `0x79` | `0x01534000` | `0x14925be0` | `0x14920518` |
| `0x23` | `0x01520000` | `0x14925c08` | `0x14920540` |
| `0x22` | `0x01538000` | `0x14925c30` | `0x14920568` |
| `0x1` | `0x01dc1000` | `0x14925c58` | `0x14920590` |
| `0x84` | `0x01de6000` | `0x14925c80` | `0x149205b8` |
| `0x63` | `0x01df8000` | `0x14925ca8` | `0x149205e0` |
| `0xf6` | `0x09c00000` | `0x14925cd0` | — |
| `0x12` | `0x002f0000` | `0x14925cf8` | `0x14920608` |
| `0x3` | `0x03e00000` | `0x14925d20` | `0x14920630` |
| `0x82` | `0x34c60000` | `0x14925d48` | `0x14920658` |
| `0x91` | `0x34cb0000` | `0x14925d70` | `0x14920680` |
| `0x96` | `0x07f30000` | `0x14925d98` | `0x149206a8` |
| `0x3d` | `0x310c4000` | `0x14925dc0` | `0x149206d0` |
| `0x27` | `0x0061c000` | `0x14925de8` | `0x149206f8` |
| `0xde` | `0x17e04000` | `0x14925e10` | `0x14920720` |
| `0x28` | `0x0c41c000` | `0x14925e38` | `0x14920748` |
| `0x29` | `0x010bc000` | `0x14925e60` | `0x14920770` |
| `0xf7` | `0x17e00000` | `0x14925e88` | `0x14920798` |
| `0xf8` | `0x310cc000` | `0x14925eb0` | — |
| `0x56` | `0x088de000` | `0x14925ed8` | `0x149207c0` |
| `0x77` | `0x0c292000` | `0x14925f00` | `0x149207e8` |
| `0xbd` | `0x00d00000` | `0x14925f28` | `0x14920810` |
| `0x14` | `0x01f00000` | `0x14925f50` | `0x14920838` |
| `0x30` | `0x0f000000` | `0x14925f78` | `0x14920860` |
| `0x67` | `0x01d98000` | `0x14925fa0` | `0x14920888` |
| `0xb5` | `0x17e08000` | `0x14925fc8` | `0x149208b0` |
| `0x32` | `0x07f20000` | `0x14925ff0` | `0x149208d8` |

The AC namespace pass adds resources `0xbb`, `0x7d`, and `0x4d` to both built-in alias cohorts.
Their bases are `0x01e50000`, `0x010d0000`, and `0x310c8000`.
All three use existing TME owners. No new region state is required.
SM8850 retains its built-in `0xf6` controller even though the selected AC array does not program it.
SM8845 omits SM8850 built-in resources `0xf6/0xf8`.
The resulting APSS alias inventories contain 36 and 34 controllers, respectively.

## Shared owners and declared virtual geometry

Each APSS configuration alias targets one optional socket on the existing protection owner.
The dedicated `xbl_protection_configuration` router selects those sockets at their TME-view bases.
The primary `target_socket` remains on `tme_router`.
Both sockets use the same region array, invalidation callbacks, locks, and ownership state.
The SPMI split still excludes `[0x0c41c000,0x0c41e000)` from its packed arbiter view.
Neither SPMI transaction offsets nor unrelated board controls change.

The table lists the complete union of enabled AC and built-in XBL aliases.
Geometry is `regions/address_bits/alignment_bits`.
The configuration window starts at the APSS base or TME owner base and has the stated size.
Every owner declares eleven QAD entries. This is virtual policy, not a physical measurement.
The selected permission words require mask `0xc000077f`, including domain ten and excluding domain seven's QAD bit.
No new protected data clients are connected.

| ID | APSS base | TME owner base | Type | SM8850 geometry/window size | SM8845 geometry/window size |
| --- | --- | --- | --- | --- | --- |
| `0x73` | `0x0c290000` | `0x10290000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x76` | `0x0c294000` | `0x10294000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x1e` | `0x003fe000` | `0x043fe000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x3c` | `0x310c0000` | `0x310c0000` | P | `128/32/12`, `0x3000` | `90/32/12`, `0x2680` |
| `0x39` | `0x01524000` | `0x05524000` | P | `64/40/12`, `0x2000` | `64/36/0`, `0x2000` |
| `0x78` | `0x01530000` | `0x05530000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x79` | `0x01534000` | `0x05534000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x23` | `0x01520000` | `0x05520000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x22` | `0x01538000` | `0x05538000` | P | `256/32/12`, `0x5000` | `136/32/12`, `0x3200` |
| `0x1` | `0x01dc1000` | `0x05dc1000` | F | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x84` | `0x01de6000` | `0x05de6000` | F | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x63` | `0x01df8000` | `0x05df8000` | F | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0xf6` | `0x09c00000` | `0x0dc00000` | P | `64/32/12`, `0x2000` | — |
| `0x12` | `0x002f0000` | `0x042f0000` | F | `256/32/12`, `0x5000` | `185/32/12`, `0x3e40` |
| `0x3` | `0x03e00000` | `0x07e00000` | F | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x82` | `0x34c60000` | `0x34c60000` | P | `128/37/12`, `0x3000` | `70/37/12`, `0x2180` |
| `0x91` | `0x34cb0000` | `0x34cb0000` | P | `64/32/0`, `0x2000` | `64/32/0`, `0x2000` |
| `0x96` | `0x07f30000` | `0x0bf30000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x3d` | `0x310c4000` | `0x310c4000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x27` | `0x0061c000` | `0x0461c000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0xde` | `0x17e04000` | `0x1be04000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x28` | `0x0c41c000` | `0x1041c000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x29` | `0x010bc000` | `0x050bc000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0xf7` | `0x17e00000` | `0x1be00000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0xf8` | `0x310cc000` | `0x310cc000` | P | `64/32/12`, `0x2000` | — |
| `0x56` | `0x088de000` | `0x0c8de000` | F | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x77` | `0x0c292000` | `0x10292000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0xbd` | `0x00d00000` | `0x04d00000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x14` | `0x01f00000` | `0x05f00000` | P | `1024/32/12`, `0x11000` | `1024/32/12`, `0x11000` |
| `0x30` | `0x0f000000` | `0x13000000` | F | `1024/32/12`, `0x11000` | `259/32/12`, `0x50c0` |
| `0x67` | `0x01d98000` | `0x05d98000` | F | `1024/40/0`, `0x11000` | `64/32/12`, `0x2000` |
| `0xb5` | `0x17e08000` | `0x1be08000` | P | `1024/40/0`, `0x11000` | `64/36/12`, `0x2000` |
| `0x32` | `0x07f20000` | `0x0bf20000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0xbb` | `0x01e50000` | `0x05e50000` | P | `128/32/12`, `0x3000` | `73/32/12`, `0x2240` |
| `0x7d` | `0x010d0000` | `0x050d0000` | F | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |
| `0x4d` | `0x310c8000` | `0x310c8000` | P | `64/32/12`, `0x2000` | `64/32/12`, `0x2000` |

The geometry changes are bounded by selected policy requirements:

- Both boards use fixed-resource virtual types for IDs `1`, `0x84`, `0x63`, `3`, `0x56`, and `0x67`.
- SM8845 increases IDs `0x3c/0x12/0x82` to 90/185/70 entries, respectively.
- Both boards declare 37 bits for ID `0x82` to preserve the fallback's exclusive 64 GiB endpoint.
- SM8845 declares 36 bits for ID `0xb5` to preserve its exclusive endpoint `0x880000000`.
- Other existing capacities, address widths, alignments, and virtual revisions remain unchanged.

SM8845's policy arrays require indices through 71 at controller `0x310c0000`.
Its selected allocation cohort requires indices 84 through 89.
The declared 90 entries cover both requirements without changing that six-slot allocator.
This capacity is a virtual minimum, not a physical identification value.

The following SM8845 cohorts preserve each original exclusive endpoint.
XBL and AC agree on each shared cohort.
The other cohorts fit the existing capacities.

| Resource | XBL descriptor | AC descriptor | XBL/AC range | TME descriptor | TME range |
| --- | --- | --- | --- | --- | --- |
| `0x73` | `0x148e1460` | `0x149460e0` | `[0,3)` | `0x456e78` | `[0,3)` |
| `0x76` | `0x148e1478` | `0x149460f8` | `[3,4)` | `0x456e88` | `[3,4)` |
| `0x3c` | `0x148e1490` | `0x14946110` | `[84,90)` | `0x456e98` | `[84,90)` |
| `0x39` | `0x148e14a8` | `0x14946128` | `[0,2)` | `0x456ea8` | `[0,2)` |
| `0x23` | `0x148e14c0` | `0x14946140` | `[0,13)` | `0x456eb8` | `[0,13)` |
| `0xbc` | Not selected | Not selected | Not selected | `0x456ec8` | `[0,35)` |
| `0x22` | `0x148e14d8` | `0x14946158` | `[97,101)` | `0x456ed8` | `[97,100)` |
| `0x82` | `0x148e14f0` | `0x14946170` | `[0,60)` | `0x456ee8` | `[0,60)` |
| `0x3d` | `0x148e1508` | `0x14946188` | `[0,3)` | `0x456ef8` | `[0,3)` |
| `0x27` | `0x148e1520` | `0x149461a0` | `[0,8)` | `0x456f08` | `[0,8)` |
| `0x28` | `0x148e1538` | `0x149461b8` | `[0,18)` | Not selected | Not selected |
| `0xf7` | `0x148e1550` | `0x149461d0` | `[46,48)` | `0x456f18` | `[46,48)` |
| `0xbd` | `0x148e1568` | `0x149461e8` | `[3,5)` | `0x456f28` | `[3,5)` |
| `0x80` | Not selected | Not selected | Not selected | `0x456f38` | `[27,29)` |

Run109 observes the selected AC descriptor `0x14946278` and its cohort pointer `0x14946110`.
Run110 records zero at both PIMEM allocation returns, `0x148bdc44` and `0x148bdc64`, and caller `0x148752dc`.
The requested ranges are `[0x31923000,0x31925000)` and `[0x31903000,0x31904000)`.
Evidence: `build/private/run-sm8845-common-runtime110/pimem-geometry-probe.json`.

SM8850's two substitute owners remain explicit virtual policy.
Resource `0x67` now uses the evidenced fixed-resource array layout.
It retains capacity 1024, width 40, byte alignment, eleven QAD entries, and virtual revision `0x40000000`.
Resource `0xb5` retains the same geometry with programmable ranges.
Neither substitute connects a new data client.

Array bounds establish the widths required to preserve supplied values, not physical identification fields.
Resource `0x39` requires 36 bits for `[0x9dcc00000,0x9dcc02000)`.
Its existing widths, 40 and 36, already preserve that range.
Resource `0x91` needs byte granularity for ranges such as `[1,0x18)` and `[0x1e,0x23)`.
Both existing profiles already declare byte alignment there.
Other existing page alignments remain declared virtual normalization policy.
They must not be described as captured hardware alignment values.
For example, both payloads supply ID `0xf7` end `0x1728ffff` and ID `0x32` end `0x07a01c80`.
The firmware masks both bounds with the alignment from IDR1 before it programs or compares them.
The retained page policy normalizes these values. It does not preserve every raw low bit.
No data-client mapping is added to claim exact physical isolation for these otherwise unconnected controllers.

## Finite client apertures

Root offsets `0x88/0x90` select a separate table of 16 client records.
SM8850 uses table `0x1494730c` and count coordinate `0x149473cc`.
SM8845 uses table `0x149472e4` and count coordinate `0x149473a4`.
Each 12-byte record contains the resource ID, address mask, and subtraction base.
These records are not the controller lookup array at `0x14946200`.

| Resource | APSS controller | Mask | SM8850 record | SM8845 record |
| --- | --- | --- | --- | --- |
| `0x27` | `0x0061c000` | `0x3ffff` | `0x1494730c` | `0x149472e4` |
| `0x29` | `0x010bc000` | `0x3fff` | `0x14947318` | `0x149472f0` |

Both finite records use subtraction base zero. The other 14 records use mask `0xffffffff`.
SM8850 helper `0x1485b928` subtracts the selected base and applies its mask.
It then applies the IDR1 address and alignment masks.
Resource `0x29` therefore converts `[0x010bb000,0x010bc000)` to encoded bounds `0x3000/0`.
The source record is at `0x14949e78` or `0x1494a298`.
Both native runs reached activation with exactly those encoded bounds.

The virtual `aperture_size` declares the finite boundary without changing register readback.
An encoded zero end denotes that exclusive boundary when the profile declares an aperture.
The model rejects inverted nonzero bounds and endpoints beyond the aperture.
Without a declared aperture, zero retains the previous explicit-end behavior.
This bounded interpretation is a behavioral substitute, not a claim about physical endpoint comparators.
The configuration registers do not connect an additional protected data client.

SM8845 retains its 31 runtime permission records and 30 debug addresses.
Both profiles retain AC overrides in a separate `xbl_protection` array.
The common helper applies both arrays to the existing shared owners.


## Fallback ranges and next bounded path

The namespace pass also enumerates the root's fallback ranges through offsets `0xb8/0xc0`.
SM8850 uses `0x149471b8`, with 14 records counted at `0x14947308`.
SM8845 uses `0x149471a8`, with 13 records counted at `0x149472e0`.
Each record has start, exclusive end, resource ID, and a reserved word, with stride `0x18`.
This pass adds no resource identities beyond the complete inventory above.

| ID | SM8850 fallback range | SM8845 fallback range |
| --- | --- | --- |
| `0x22` | `[0x00b00000,0x00b20000)` | — |
| `0xbd` | `[0x00d20000,0x00d2c000)` | same |
| `0x73` | `[0x0b080000,0x0b098000)` | same |
| `0x73` | `[0x0b100000,0x0b10c000)` | same |
| `0x76` | `[0x0c210000,0x0c290000)` | same |
| `0x76` | `[0x0ec90000,0x0ed20000)` | `[0x0ec90000,0x0ed50000)` |
| `0x28` | `[0x0c400000,0x0cd00000)` | same |
| `0x27` | `[0x14680000,0x146a7000)` | same |
| `0x23` | `[0x14800000,0x14c00000)` | same |
| `0xf7` | `[0x1d800000,0x1f000000)` | same |
| `0x3d` | `[0x31020000,0x31060000)` | same |
| `0x3c` | `[0x31900000,0x31940000)` | same |
| `0x82` | `[0x80000000,0x100000000)` | same |
| `0x82` | `[0x880000000,0x1000000000)` | same |

After region programming, XBL calls global policy helpers at SM8850 `0x14859b4c`, `0x14859a48`, and `0x14859ad0`.
The SM8845 counterparts are `0x14858080`, `0x14857f7c`, and `0x14858004`.
They gate newer register groups on revision `>=0x40020000`, with additional type/capability checks.
The groups include `+0x404/+0x408` and `+0x600..+0x620`.
The retained virtual revision is `0x40000000`, so these groups are not selected by this profile.
This declaration predates namespace support. It is not changed to skip the failing GCR operation.
Unsupported neighboring register groups must remain unsupported.

The native integration runs retain their results in these directories:

- `build/private/run-sm8850-common-runtime13`
- `build/private/run-sm8845-common-runtime12`

Both runs advance from the XPU activation rejection to authorization import `0x4169f4`.
The respective TME PCs are `0x440f28` and `0x4411a6`.
The UART logs still contain firmware output only.
These observations do not prove programming of every region, APDP authorization, or kernel execution.

The full suite passes 62 tests, including both native namespace and aperture regressions.
The namespace cases exercise foreign ownership, final-owner release, zero authority, and nonsecure rejection.
The aperture cases exercise encoded limits, old direct mappings, neighboring writes, and denied protected writes.
They also reject inverted bounds and endpoints beyond the declared aperture.

## Configuration interface contract

The APSS socket uses `apss_config_owner_mask=0xc0000001`.
The primary TME socket uses `config_owner_mask=4`, from the OEM configuration getter's domain-two input.
These masks are configuration capabilities. They are not data-QAD identities.
Secure transaction attributes must not add capability bits to either mask.
The trusted TME profile explicitly declares `config_secure_unspecified=true` for its unspecified-security transport.
The APSS socket requires actual secure attributes. It does not upgrade unspecified or nonsecure accesses.
A missing capability declaration must not authorize namespace writes.
There is no configuration runtime-QAD phase transition.
The existing data-QAD phase transition remains independent and unchanged.
The primary TME declarations come from pre-existing runtime owners.
The alias helper supplies them only on its two SM8850 substitute owners.

The model implements GCR namespace selection and domain configuration locks on these shared sockets.
The platform does not weaken configuration locks, permission masks, or security attributes.
It retires only an unlocked region after ownership release removes every effective permission.
Firmware shadow arrays do not create a second hardware owner.
It does not bypass authentication, rollback, fuses, root hashes, locks, or region permissions.

### Corrected TME configuration capability

SM8850 owner writer `0x426244` and reader `0x426282` call getter `0x427616`.
Initialization `0x426908` also calls it at `0x426924`.
The getter maps singleton domain two to mask `4` and caches it at `0x4491e8`.
SM8845 getter `0x427552` performs the same operation and caches its result at `0x4491c8`.
Release helper `0x4262c6` uses the same ownership writer and reader.

The earlier mask-two declaration confused configuration ownership with an unrelated permission getter.
In this image, getter `0x4275b8` maps domain zero to `0x80000001`, not mask two.
Its callers classify software permission shadows.
The correction selects fixed mask four, not a union or a configuration phase transition.
The generic native fixture retains its separately declared mask two.

The native trace `namespace-lock-origin-tme.log` records the failing ownership write and check.
Writer `0x426244` supplies mask four for region two at local register `0x055210b0`.
Reader `0x426282` obtains zero under the earlier mask-two profile.
The OEM reports `f057` at source coordinate `0x2d1`, then invokes its fatal diagnostic collector.
The later read at `0x810000` is not the original failure.
No foreign ownership, permission, or lock rule changes to resolve this mismatch.

### Sequencer DBGAR system view

The later DBGAR request supplies system addresses, not TME-local configuration addresses.
Both OEM runs first exposed the missing view at `0x003fe304`.
The existing signed runtime debug lists did not enumerate that controller.
The XBL cohort now supplies one four-byte DBGAR view for each listed controller.
The helper reuses existing exact views and rejects conflicting owners.

For example, system `0x003fe304` translates to the existing primary register at `0x043fe304`.
The view reaches `tme_router`, not the APSS configuration-capability socket.
It preserves the sequencer's secure DMA attributes and the shared DBGAR storage.
It creates no protected data client and grants no broader MMIO aperture.
The virtual debug-access value remains zero.

Runs `run-sm8850-common-runtime12` and `run-sm8845-common-runtime11` pass the previous DMA rejection.
The retained `dbgars-origin-tme.log` shows real completion and return through the OEM caller.
The subsequent ownership failure is explained above.
No status-only completion replaces the register writes or readback.


### SOCCP DEBUG allocation failure

SM8845 run120 stops at original XBL PC `0x148593f4`.
The retained capture is `build/private/run-sm8845-common-runtime120/soccp-namespace-probe.json`.
The descriptor selects resource `0xbd`, controller `0x00d00000`, and cohort pointer `0x149461e8`.
That cohort contains start three and count two.
The allocator reaches index five after it reads activation bit zero as set for slots three and four.
The scan at `0x148593b0..0x148593f0` does not use the controller's total region count.
The existing 64-region declaration cannot increase this two-slot firmware cohort.

The failed request covers `[0x00d24000,0x00d27000)`.
The original fallback covers `[0x00d20000,0x00d2c000)`.
The selected AC array at `0x1494ae48` programs slots one, two, and seven, not slots three and four.
Its three permission pairs are `0x80000005`, `0x80000001`, and `0xc0000005`, respectively.
Thus the selected static AC records do not identify the producers of the exhausted slots.

The original `IMAGES/sdi.mbn` contains 72480 bytes.
Its SHA-256 is `dc8230fb90ed353d46dc01ca376b8afb73c791d579dc4b8fe8785d55131656c9`.
It matches `RADIO/soccp_debug.img` byte for byte.
Its two SRAM load segments start at `0x00d20000` and `0x00d24000`.
Their file sizes are `0x2b40` and `0x2ea2`, respectively.
Both segments declare memory size `0x4000`.
The failure address therefore belongs to the second original SRAM load segment.

XBL helper `0x14858cb4` releases a class through ownership writer `0x148599b8`.
It then removes that class from the firmware permission shadows through `0x14859db4`.
The caller at `0x14859878..0x148598b4` checks those shadows before it writes activation zero.
The selected original TME release helper at `0x426264` checks ownership before and after release.
Its namespace branch at `0x4262a8` omits the legacy activation-clear write at `0x4262ba`.
These paths do not justify removal of a remaining foreign permission.

Run120 did not capture the slot registers because the debugger rejected the MMIO reads.
Run121 captures the missing register history through temporary model instrumentation.
The capture is `build/private/run-sm8845-common-runtime121/system.log.zst`, member `system.log`, lines 23292 through 23443.
The instrumented instance is `runtime_descriptor_protection_26`.
The model no longer contains the capture code.

| Time (ns) | Operation | Result |
| ---: | --- | --- |
| `4265775742` | APSS enables slot three | Bounds `[0x20000,0x23000)`, read/write `0x14`, lock zero, owner zero |
| `4267023472` | TME claims slot three | Owner `4`, retained read/write `0x14` |
| `4267726511` | APSS enables slot four | Bounds `[0x20000,0x24000)`, read/write `0x14`, lock zero, owner zero |
| `4268960817` | TME claims slot four | Owner `4`, retained read/write `0x14` |
| `4268975987` | TME releases slot three | Owner zero, retained permissions zero, base read/write `0x10`, activation one |
| `4273809293` | APSS scans slot three | Activation one, base read/write `0x10`, owner zero |
| `4273813394` | APSS scans slot four | Activation one, base read/write `0x14`, owner `4`, retained read/write `0x14` |
| `4274245120` | TME releases slot four | Owner zero, retained permissions zero, base read/write `0x10`, activation one |
| `4274914002` | APSS disables slot three | Activation zero, base read/write `0x10` |

Slot four extends the first load segment from its rounded file size to its declared memory size.
The original producer uses slot three for `[0x20000,0x23000)` and slot four for `[0x20000,0x24000)`.
The next SRAM segment requires another allocation.
At that point, slot four still has a live TME owner.
Slot three has no owner, but the former model retains a duplicate part of the transferred policy.

APSS directly programs `0x14` without an APSS ownership claim.
TME then acquires the complete policy and later releases that ownership.
The original namespace release omits a separate permission or activation clear.
The former model copies the policy into TME's retained words but leaves the complete base copy.
Release removes mask four from the base copy and clears the retained words.
That leaves `0x10` without a separate ownership acquisition or permission writer.
The residual word therefore does not establish an independent live policy.

The correction records the authority that directly programs each base permission word.
A first owner from another authority accepts an unowned policy as a transfer.
The model retains the transferred word for that owner and removes its duplicate base copy.
The complete transferred policy remains effective while that owner retains ownership.
Its final release can then retire an unlocked empty region through the existing reclamation rule.
The model does not clear the other slot's live owner.

A same-authority first claim still preserves the existing shared-policy behavior.
An acquisition with an existing owner also preserves that behavior.
Thus the observed APSS-owned, TME-co-owned handoff remains distinct from this unowned transfer.
Write-once locks retain the existing rejection of permission changes.
The two-slot firmware cohort and the declared controller capacity remain unchanged.

The existing namespace fixture now checks a transferred policy with an additional data-domain bit.
It checks complete retention, separate configuration views, final release, activation, and the free-region report.
Its shared-owner case programs and claims the initial policy through APSS, as the original OEM sequence does.

The later GPT error does not identify the namespace failure.

The integration checks cover the correction.
`make build` succeeds, all 96 tests in two suites pass, and Clippy passes with `-D warnings`.
SM8845 run122 no longer reaches namespace exhaustion or the fatal GPT cleanup.
It reaches TME PC `0x43cd60`, which calls missing import `0x4169d0`.
This is the same independent policy-import boundary that SM8850 reaches.
The UART still reports the start of SOCCP_DEBUG, not its completion.
This result does not establish a complete debug-image load or kernel execution.
The ownership-transfer rule remains a functional model, not a measured silicon specification.
Authentication qualification remains not-established.

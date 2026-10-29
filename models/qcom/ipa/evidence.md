# IPA secure configuration

## Selected firmware

Both package archives contain the ELF member `RADIO/tz.img`.
All code addresses below are ELF virtual addresses before the runtime slide.

| SoC | Package | SHA-256 of `RADIO/tz.img` | Routine | Record table |
|---|---|---|---|---|
| SM8850 | `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip` | `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932` | `0x157262d8..0x15726580` | `0x157f1b30` |
| SM8845 | `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip` | `b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09` | `0x1571aa44..0x1571acec` | `0x157e50dc` |

Capstone decodes both complete routines as AArch64 instructions.
Both routines have the same control flow, MMIO addresses, and table meanings.
The SM8850 tail entry at `0x15726590` branches to the routine.
The SM8845 tail entry at `0x1571acfc` does the same.
Only the two delay calls leave the routine before its return.
Neither routine polls a device status bit.

## Device-tree and public register evidence

The package `RADIO/vendor_boot.img` files contain DTBs with node `qcom,ipa@3e00000`.
All nine SM8850 variants and all five SM8845 variants give the same IPA properties.
Example DTB file offsets are `0x1537df8` for SM8850 and `0x1034000` for SM8845.
Both declare `ipa-base=0x03e00000+0x84000` and `gsi-base=0x03e04000+0xfc000`.
Both declare `qcom,ipa-cfg-offset=0x140000` and `qcom,ipa-hw-ver=0x18`.

The [OnePlus IPA DT](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/oneplus/sm8850_b_16.0.0_oneplus_15/vendor/qcom/opensource/data-devicetree/ipa.dtsi) names version 24 as IPAv5.5.
The model key `qcom,ipa-v5.5` identifies that version, not the generic DT compatible `qcom,ipa`.
The [IPA5.5 HWIO header](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/oneplus/sm8850_b_16.0.0_oneplus_15/vendor/qcom/opensource/dataipa/drivers/platform/msm/ipa/ipa_v3/dump/ipa5.5/ipa_hwio.h) defines the debug base at wrapper offset `0x148000`.
Thus the modeled window starts at `IPA_DEBUG_REG_BASE=0x03f48000`.
The inventory below uses addresses relative to the DT configuration base where necessary.

| Absolute address or range | Public IPA5.5 name | Public fields |
|---|---|---|
| `0x03f482c4/2c8` | `IPA_RX_HPS_CLIENTS_MIN_DEPTH_0/1` | Client minimum depths. |
| `0x03f482cc/2d0` | `IPA_RX_HPS_CLIENTS_MAX_DEPTH_0/1` | Client maximum depths. |
| `0x03f48584/588` | `IPA_DPS_SEQUENCER_FIRST/LAST` | Read-only address boundaries. |
| `0x03f4858c/590` | `IPA_HPS_SEQUENCER_FIRST/LAST` | Read-only address boundaries. |
| `0x03f48800+4*i` | `IPA_ENDP_GSI_CFG1_n` | `INIT_ENDP[31]`, `ENDP_EN[16]`. |
| `0x03f49000+4*i` | `IPA_ENDP_GSI_CFG_TLV_n` | `FIFO_SIZE[23:16]`, `FIFO_BASE_ADDR[15:0]`. |
| `0x03f49400+4*i` | `IPA_ENDP_GSI_CFG_AOS_n` | The same FIFO fields. |
| `0x03f49800+4*j` | `IPA_COAL_VP_AOS_FIFO_n` | The same FIFO fields. |

The [GSI5.5 HWIO header](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/oneplus/sm8850_b_16.0.0_oneplus_15/vendor/qcom/opensource/dataipa/drivers/platform/msm/ipa/ipa_v3/dump/ipa5.5/gsi_hwio.h) supplies the following names.
Offsets in this table are relative to `GSI_REG_BASE=0x03e04000`.

| GSI offsets | Public names and fields |
|---|---|
| `0x18/1c` | `GSI_PERIPH_BASE_ADDR_LSB/MSB`. |
| `0x25c` | `GSI_SHRAM_PTR_USER_INFO_DATA_BASE_ADDR`, `SHRAM_PTR[15:0]`. This is a pointer, not a size register. |
| `0x400/404/408` | `GSI_IRAM_PTR_CH_CMD/EE_GENERIC_CMD/TLV_CH_NOT_FULL`. |
| `0x414/418/41c/420` | `GSI_IRAM_PTR_MSI_DB/CH_DB/EV_DB/NEW_RE`. |
| `0x424/428/42c` | `GSI_IRAM_PTR_CH_DIS_COMP/CH_EMPTY/EVENT_GEN_COMP`. |
| `0x430/434/438` | `GSI_IRAM_PTR_PERIPH_IF_TLV_IN_0/2/1`. |
| `0x43c/440/444/448` | `GSI_IRAM_PTR_TIMER_EXPIRED/WRITE_ENG_COMP/READ_ENG_COMP/UC_GP_INT`. |
| `0x44c/470` | `GSI_IRAM_PTR_INT_MOD_STOPED/INT_NOTIFY_MCS`. |
| `0x9000+0x400*ee+4*ch` | `GSI_MAP_EE_n_CH_k_VP_TABLE`, `PHY_CH[7:0]`, `VALID[8]`. |
| `0x0/0xb000` | `GSI_CFG` and `GSI_MCS_CFG`, used only by the additional upload callback. |

The public IRAM pointer fields have width 12.
The selected channel-map stores contain only indices zero through 35.
They do not set the public `VALID` bit. The model does not manufacture that bit.
The public shared-RAM array starts at GSI offset `0x2000` and ends at index 2037.
Its final word occupies `0x03e07fd4..0x03e07fd7`.
The header does not give a reset value for the user-info pointer.

The [Linux GSI v5.0 register file](https://github.com/torvalds/linux/blob/master/drivers/net/ipa/reg/gsi_reg-v5.0.c) defines EE registers, not these secure global words.
The model uses QSEE and the matching IPA5.5 headers for the secure layout.
It does not treat upstream EE offsets as global register offsets.

## Complete routine access inventory

Every MMIO transfer is a little-endian, aligned, 32-bit transfer.
The inventory counts executed transfers, not instruction sites.
The selected tables enable all 36 records.

| Region | Distinct words | Reads | Writes | Evidence in SM8850 | Evidence in SM8845 |
|---|---:|---:|---:|---|---|
| GCC | 1 | 2 | 2 | `0x157262fc..0x15726318` | `0x1571aa68..0x1571aa84` |
| IPA configuration | 144 | 280 | 284 | `0x15726354..0x15726560` | `0x1571aac0..0x1571accc` |
| GSI configuration and channel map | 57 | 0 | 57 | `0x15726350`, `0x157264b0..0x15726568` | `0x1571aabc`, `0x1571ac1c..0x1571acd4` |
| GSI parameter | 1 | 1 | 0 | `0x1572656c` | `0x1571acd8` |
| GSI canary RAM | 1 | 0 | 1 | `0x15726574` | `0x1571ace0` |
| Total | 204 | 283 | 344 | 627 transfers | 627 transfers |

### GCC reset

The routine reads `0x00189000`, sets bit zero, and writes the word.
It delays 200 microseconds, reads the word again, clears bit zero, and writes the word.
It then delays 100 microseconds.
The delay targets are `0x1566eda4` for SM8850 and `0x1566bda4` for SM8845.

### IPA words

Let `i` select a record from zero through 35.
Each record has five bytes.
Let `a[i]` mean its fourth byte and `b[i]` mean its fifth byte.
Let `old` mean the value from the immediately preceding register read.
Let `S` start at zero before the first allocation loop.

| Addresses | Transfer sequence and values |
|---|---|
| `0x03f48800 + 4*i` | Read, then write `(old & 0x80000000) | 0x10000` when `a[i]` is nonzero. |
| `0x03f49000 + 4*i` | Read, then write `(old & 0xffff) | (a[i] << 16)`. Read again, then write `(old & 0xff0000) | (S & 0xffff)`. Increase `S` by `a[i]`. |
| `0x03f49400 + 4*i` | Use the same sequence with `b[i]`. Continue `S` from the preceding loop. |
| `0x03f48800 + 4*i` | After each second-loop allocation, read and write `(old & 0x10000) | 0x80000000` when `a[i]` is nonzero. |
| `0x03f49800 + 4*j`, `j=0..31` | Read, then write `0x80000 | (old & 0xffff)`. Read again, then write `(old & 0xff0000) + 8*j`. |
| `0x03f482c4`, `0x03f482cc` | Write `0x30000303`. |
| `0x03f482c8`, `0x03f482d0` | Write zero. |

The zero-size branch clears the allocation word through two read/write pairs.
It first retains only bits 15:0, then retains only bits 23:16.
Neither selected table takes that branch.
The first allocation loop consumes 413 units.
The second allocation loop consumes another 550 units.
These are decoded allocation sizes, not claims about physical memory units.

### Decoded channel and allocation facts

The first two record bytes select a GSI channel word.
The address expression is `0x03e0d000 + ((first << 10) | (second << 2))`.
The third byte has no consumer in this routine.
The routine writes the record index to each channel word when the fourth byte is nonzero.
The table below lists decoded channel addresses and allocation counts.
It does not contain firmware bytes.

| Index | GSI channel word | First allocation | Second allocation |
|---:|---|---:|---:|
| 0 | `0x03e0d03c` | 8 | 16 |
| 1 | `0x03e0d000` | 8 | 16 |
| 2 | `0x03e0d02c` | 25 | 32 |
| 3 | `0x03e0d01c` | 8 | 16 |
| 4 | `0x03e0d024` | 8 | 16 |
| 5 | `0x03e0d420` | 28 | 32 |
| 6 | `0x03e0d040` | 8 | 16 |
| 7 | `0x03e0d044` | 8 | 16 |
| 8 | `0x03e0d048` | 8 | 16 |
| 9 | `0x03e0d04c` | 26 | 32 |
| 10 | `0x03e0d014` | 10 | 16 |
| 11 | `0x03e0d800` | 8 | 16 |
| 12 | `0x03e0d400` | 16 | 28 |
| 13 | `0x03e0d404` | 20 | 24 |
| 14 | `0x03e0d030` | 20 | 24 |
| 15 | `0x03e0d408` | 28 | 32 |
| 16 | `0x03e0d034` | 16 | 23 |
| 17 | `0x03e0d038` | 9 | 9 |
| 18 | `0x03e0d40c` | 9 | 9 |
| 19 | `0x03e0d410` | 9 | 9 |
| 20 | `0x03e0d414` | 5 | 5 |
| 21 | `0x03e0d418` | 5 | 5 |
| 22 | `0x03e0d41c` | 9 | 9 |
| 23 | `0x03e0d010` | 16 | 23 |
| 24 | `0x03e0d004` | 9 | 9 |
| 25 | `0x03e0d050` | 5 | 5 |
| 26 | `0x03e0d008` | 5 | 5 |
| 27 | `0x03e0d054` | 9 | 9 |
| 28 | `0x03e0d00c` | 8 | 14 |
| 29 | `0x03e0d058` | 8 | 14 |
| 30 | `0x03e0d05c` | 9 | 9 |
| 31 | `0x03e0d060` | 9 | 9 |
| 32 | `0x03e0d020` | 9 | 9 |
| 33 | `0x03e0d028` | 9 | 9 |
| 34 | `0x03e0d018` | 9 | 9 |
| 35 | `0x03e0d064` | 9 | 9 |

### GSI global writes

The routine writes the following literal values in this order.

| Address | Value |
|---|---:|
| `0x03e04400` | 1 |
| `0x03e04418` | 2 |
| `0x03e04424` | 3 |
| `0x03e04428` | 4 |
| `0x03e04404` | 5 |
| `0x03e0442c` | 6 |
| `0x03e0444c` | 7 |
| `0x03e04430` | 8 |
| `0x03e04434` | 9 |
| `0x03e04438` | 10 |
| `0x03e04420` | 11 |
| `0x03e04444` | 12 |
| `0x03e0443c` | 13 |
| `0x03e0441c` | 14 |
| `0x03e04448` | 15 |
| `0x03e04440` | 16 |
| `0x03e04408` | 17 |
| `0x03e04414` | 18 |
| `0x03e04470` | 19 |
| `0x03e04018` | 0 |
| `0x03e0401c` | 0 |

The routine then reads `0x03e0425c`.
It writes `0xdeadbeef` to `0x03e05ffc + ((value & 0xffff) << 3)`.

## Other access paths

The scan covers both complete main code segments and the other executable ELF segments.
The main segments contain 352256 and 347136 four-byte instruction slots, respectively.
The scan checks immediate address construction and aligned address literals in every segment.
It also checks direct branch targets and aligned pointers to the additional callback.
Runtime-supplied addresses cannot prove the absence of all indirect MMIO accesses.

Both images contain an additional callback named `ipa_gsi_bring_up` in its error string.
Its code starts at `0x156e5d70` for SM8850 and `0x156dad04` for SM8845.
It iterates image records and copies their contents to discovered destinations.
Its pointer appears in image descriptors at `0x157e9360` and `0x157ddfa0`, respectively.
The descriptor image selector is 15.
Additional pointer references occur at `0x159a41f8` and `0x159a0300`.
No direct branch or call in either main segment targets this callback.

| Access | SM8850 instruction | SM8845 instruction | Meaning from code |
|---|---|---|---|
| Read `0x03f48584` | `0x156e5e20` | `0x156dadb4` | First upload-region offset, relative to `0x03e00000`. |
| Read `0x03f4858c` | `0x156e5e44` | `0x156dadd8` | Second upload-region offset. |
| Read `0x03f48588` or `0x03f48590` | `0x156e5e58` | `0x156dadec` | Selected region end offset. |
| Read either start offset again | `0x156e5e5c` | `0x156dadf0` | Compute capacity as end minus start plus four. |
| Read `0x03e0f000`, then write one | `0x156e5ef0..0x156e5efc` | `0x156dae84..0x156dae90` | Control request after three successful record copies. |
| Read/write `0x03e04000` | `0x156e5f00..0x156e5f10` | `0x156dae94..0x156daea4` | Write `(old & 0xf3b) | 4`. |
| Read/write `0x03e04000` again | `0x156e5f14..0x156e5f20` | `0x156daea8..0x156daeb4` | Write `(old & 0xf3e) | 1`. |

All scalar accesses in this callback have width four.
Its copy destinations depend on the image records and the discovery-register values.
The fixed third upload region starts at `0x03ea8000` and has capacity `0x8108`.
The function copies record data only when each region and size match.
These are upload-service accesses, not additional accesses in the secure configuration routine.
No observed boot trace in this assignment establishes that the boot path invokes this callback.
The model does not invent discovery values or firmware-upload completion.
These undeclared accesses retain the normal unknown-access log and error behavior.

Both images also contain the literal GCC address in a page-descriptor sequence.
The locations are `0x15823c48` and `0x15818c48`.
Those literals describe consecutive pages and do not identify another BCR access instruction.
Other immediate candidates construct log identifiers or the separate GCC word `0x00185000`.

## Functional substitutes

The model retains full configuration words at only the declared offsets.
Firmware read/modify/write masks do not prove hardware write masks.
The model therefore applies no write masks to those words.
All retained words and the canary word start at virtual zero.
Unknown offsets log `IPA_UNMODELED` and return a TLM address error.
Invalid transfers also log their address and fail.
No descriptor engine, packet processing, DMA, or interrupt source exists in this model.
The configuration writes do not create a completed transfer.

The explicit virtual `USER_INFO_DATA_BASE_ADDR` pointer value is `0x3fb`.
No selected public source supplies its silicon reset value.
QSEE uses eight-byte units and places the canary four bytes before this pointer.
The pointer selects the retained canary word at `0x03e07fd4`.
This is the last word in the public shared-RAM array, not a RAM-size register.
The model rejects writes to the parameter and accesses to adjacent undeclared RAM words.

The GSI router window ends at `0x03e0e000`.
The IPA configuration window occupies `0x03f48000..0x03f49fff`.
The shared Lua file rejects overlaps with existing APSS router targets before it adds either window.

SM8845 already retains the XBL clock-policy word at `0x03f404a8` in its existing owner.
XBL `0x1484d7fc` writes `0x10064` there. See `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md`.
The IPA window excludes that word and the XPU controls at `0x03f24000`.

The GCC reset word uses the existing retained BCR pattern from `platforms/qcom/sm88xx/dcp-clocks.lua`.
Only bit zero retains writes.
The BCR does not clear the IPA state because the existing pattern has no downstream reset signal.
The explicit model cold reset clears all retained IPA state.

## Behavioral check

The native `ipa` consumer checks every declared configuration bank and channel word.
It checks all 19 global words, both controls, the parameter, and the canary.
It also checks cold reset, unsupported offsets, parameter writes, and invalid transfer widths.
The check does not run OEM firmware.

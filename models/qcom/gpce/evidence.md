# QSEE CE, GPCE startup, and QCE SHA

## Inputs and method

The source analysis used selected-image instructions and the run145 failure log.
The integration observations below remain separate from that static evidence.
ELF reads used PT_LOAD virtual addresses through pyelftools.
Capstone decoded AArch64 instructions.

| Package | RADIO/tz.img SHA-256 |
| --- | --- |
| CPH2771, SM8845 | b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09 |
| CPH2749, SM8850 | afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932 |

The packages are the selected exports `edl-re:CPH2771export_11_16.0.1.305EX01_2025121000260242.zip sha256=6e15a8792b09e40cb203e5230d3f7d467f0910f102dbab3a7d715853f04ee1d1` and `edl-re:CPH2749export_11_16.0.5.700EX01_2026022701130186.zip sha256=c961d7380aec0d5f15f577182e35f6bb109a550cc91ec591b66acf8c148236f0`.
The existing run145 log ends with an APSS read at PC `e42caf094`, physical address `01dfa104`, width four.
The selected SM8845 instruction at `156af094` reads that exact address.
The runtime relocation delta for this instruction is `e2d600000`.

Public sources define the QCE register layout and the SHA programming contract:

- Linux [`regs-v5.h`](https://github.com/torvalds/linux/blob/master/drivers/crypto/qce/regs-v5.h) defines register offsets and bit fields.
- Linux [`common.c`](https://github.com/torvalds/linux/blob/master/drivers/crypto/qce/common.c) defines the SHA register sequence, the little-endian configuration, and the GO bits.
- Linux [`sha.c`](https://github.com/torvalds/linux/blob/master/drivers/crypto/qce/sha.c) defines the incremental state model and the digest readback.
- Linux [`dma.h`](https://github.com/torvalds/linux/blob/master/drivers/crypto/qce/dma.h) defines the 104-byte result dump and its 128-byte padded buffer size.
- The OEM SPS [`bam.c`](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/soc/qcom/sps/bam.c) defines the BAM pipe layout.

These public files are GPL-2.0-only.
The model copies constants and behavior, not source text.

## Version provenance

The retained Android log `gobbl-lab:records/20261012T090758Z-phone-loop/evidence/63-avc-dmesg.stdout sha256=21b24cd2ff320938126d32c1e7f75efb8777b89fcf0d0e9ce567d58c95109991` line 1704 reports `QTI Crypto 6.0.0` at `1de0000`.
The CPH2747 Android driver decodes the three version fields as `06000000`.
The public `qcryptohw_50.h` offset `1a000` places the register window at `01dfa000`.
This value is a driver-decoded version, not a raw MMIO read from the current boot.
No separate CPH2749 capture of the version word exists.
Both runtime profiles select the functional QCE model.
The SM8845 silicon version remains unobserved.

## Caller and bounded startup group

Addresses use hexadecimal notation.
Correspondence follows decoded instructions and call edges, not an assumed image-wide delta.

| Role | SM8845 | SM8850 |
| --- | --- | --- |
| Boot initialization caller edge | 156da770 | 156e580c |
| Second caller edge | 156da928 | 156e59b4 |
| CE startup entry | 156af07c | 156b3740 |
| CRYPTO_STATUS2 load | 156af094 | 156b3758 |
| Capability and control initializer | 156aeeb8 | 156b357c |
| GPCE root | 156f94d0 | 1570462c |
| GPCE initial controls | 156f8880 | 157039dc |
| TPKEY receive window | 156f88cc | 15703a28 |
| TPKEY ready wait | 156f8904 | 15703a60 |
| Normal destination consumers | 156f9388 | 157044e4 |
| Special destination consumers | 156f9430 | 1570458c |

The first startup entry polls `01dfa104.bit30` until zero.
Each unsuccessful poll calls the delay helper with argument one.
The next read checks bit31.
Its diagnostic string is `CRYPTO0_CRYPTO_STATUS2 BIST_ERROR` in both images.
The initializer then reads capabilities and writes setup controls.
The remaining startup instructions configure software memory metadata, not additional MMIO.

Linux names STATUS2 at crypto offset `104` and ENGINES_AVAIL at `108`:
<https://github.com/torvalds/linux/blob/master/drivers/crypto/qce/regs-v5.h>.
These names support the register-family identification.
Linux does not establish this selected engine's geometry or the BIST bits.
The selected OEM instructions establish those accesses independently.

## Exact setup accesses

| Physical address | Selected-image use | Model behavior |
| --- | --- | --- |
| 01dfa104 | Poll bit30, report bit31 | Completed cold BIST. Bit0 reports an active request. |
| 01dfa108 | Extract bits12:9 | SHA_SELECT bit4, SHA512_SELECT bit5, and ENCR_AES_SEL bit0. No other engine. |
| 01dfa180 | Extract bits29:24 into first count | Named virtual first count, default eight |
| 01dfa184 | Extract bits29:23, subtract first count | Named virtual total count, default sixteen |
| 01dfab00 | Low byte becomes selector base0 | Named virtual value, default zero |
| 01dfab08 | Low byte becomes selector base1 | Named virtual value, default two |
| 01dfab10 | Low byte becomes selector base2 | Named virtual value, default four |
| 01dfab14 | Primary root destination selector | Named virtual value, default eight |
| 01dfab18 | Secondary root destination selector | Named virtual value, default nine |
| 01dfe004+n*10, n<=f | Read slot control, write one | Retain the written control |
| 01dfe704+n*10, n>f | Read slot control, write one | Retain the written control |
| 01dff100..01dff124 | Preserve bits5:1, then set bit0 in ten words | Retain six control bits per word |
| 01df1000 | Clear bit0 | Retain bit0 |
| 01df5000..01df500c | Write four ffffffff masks | Retain four words |
| 01df2008 | Set bit3 | Retain bit3 |
| 01df101c | Clear bit8, set bit8, replace low byte with 3e | Retain nine control bits. Bit8 opens the TPKEY receive window. |
| 01df1020 | Poll bit8 | Bit8 reports a received transport key. |

The slot loop iterates only over the first count.
For indices above fifteen, its independently advanced pointer selects the second address formula.
The initializer stores the second count as total minus first.
The ten access controls start at zero under the cold virtual policy.
The model retains each write for later reads.
These controls do not contain key material and do not create ownership authority.
Their detailed silicon bit names remain unknown.

SM8845 `156af3a8..156af3d8` identifies another consumer of the three selector bases.
It computes one selector as `base2 + 2*object[20]`.
It computes another from base0 or base1, selected by object byte21, plus the same doubled index.
The following code submits software key operations through the existing queue implementation.
Thus these bytes are selector bases, not readiness flags or random data.
The virtual values are explicit model policy, not recovered fuse values.
Future transport must check selector bounds and authorization before it accepts those operations.

### Transport-key receive state

`15703a28` writes `01df101c` three times: clear bit8, set bit8, then set the low byte to `0x3e`.
It changes only those fields and keeps the other control bits.
Wait `15703a60` polls `01df1020` bit8 and stops after `0x270f` iterations.
A successful wait clears bit8 of `01df101c` and keeps the received key.
Public `ice.c` names the same field `QCOM_ICE_REG_HWKM_TZ_TPKEY_RECEIVE_CTL`.
It uses bit8 as `QCOM_ICE_HWKM_TPKEY_EN_BIT` and calls the TME transport-key broadcast while that window is open.
The model therefore treats bit8 of `01df101c` as the receive-window control.
It treats bit8 of `01df1020` as the received-key status.
The model retains the low byte `0x3e` without a decoded meaning.
Closing the window does not clear the retained key.

## QCE register window

The model maps the QCE register block at offset `a000` from `01df0000`.
This matches physical base `01dfa000`.
Offsets below are Linux register offsets inside that block.

| Offset | Register | Model behavior |
| --- | --- | --- |
| 000 | VERSION | CCI value, default `06000000` |
| 100 | STATUS | Latched errors and done. Bit8 reports auth busy. Write zero clears. |
| 104 | STATUS2 | Bit0 reports an active request. Bit31 reports a rejected request. Write zero clears latched error bits. |
| 108 | ENGINES_AVAIL | `SHA_SELECT` bit4, `SHA512_SELECT` bit5, and `ENCR_AES_SEL` bit0. The value is `31`. |
| 10c | FIFO_SIZES | Reads zero. Writes are errors. |
| 110 | SEG_SIZE | Stored. It must equal AUTH_SEG_SIZE for a hash operation. |
| 120 | GOPROC | GO, clear-context, and result-dump bits. Reads zero. |
| 140 | ENGINES_AVAIL_EXT | Second engine bitmap. Reports zero. |
| 2000 | GOPROC_OEM_KEY | Physical `01dfc000`. GO with the OEM hardware key. It accepts the same bits as GOPROC. |
| 200..208 | ENCR_SEG_CFG, size, start | Stored. Any nonzero value rejects a hash operation. |
| 20c..23c | Counter IV and mask registers | Stored as state. They do not affect a hash. |
| 300 | AUTH_SEG_CFG | Stored and checked at GO. |
| 304 | AUTH_SEG_SIZE | Stored. It is the input byte count. |
| 308 | AUTH_SEG_START | Stored. It must be zero. |
| 310..34c | AUTH_IV0..15 | Programmed SHA state. See the byte order section. |
| 350..35c | AUTH_INFO_NONCE0..3 | Retained state. Reads return the programmed words. Plain SHA ignores them. |
| 390..39c | AUTH_BYTECNT0..3 | Numeric byte counter words. Words 2 and 3 must be zero for this model. |
| 3a0..3bc | AUTH_EXP_MAC | Reads zero. Writes are errors. |
| 400 | CONFIG | Pipe set at bits8:5 and little-endian mode at bit9. |

The key bank is separate from the `a000` block.
It starts at offset `d000` from `01df0000`, which is physical `01dfd000`.
The table below uses the QCE key-register offsets that the bank carries.

| Offset | Register | Model behavior |
| --- | --- | --- |
| 3000..301c | ENCR_KEY0..7 | Retained with write-time CONFIG byte lanes. Reads return zero. |
| 3020..303c | ENCR_XTS_KEY0..7 | Same retention. Reads return zero. |
| 3040..307c | AUTH_KEY0..15 | Same retention. Supplied-key CMAC consumes four or eight words. Reads return zero. |

Reads of the key bank return zero.
That value is an explicit model limitation, not a recovered silicon value.
The model never returns retained key material.
Supplied-key CMAC reads AUTH_KEY words. Plain hashes and unsupported keyed modes do not consume them.

The model accepts the private zero writes that QSEE performs at offsets `11c`, `124`, `128`, and `13c`.
It retains only the zero value and rejects any other value.
These words remain undocumented controls.
The model does not derive a crypto result from them.
Offsets `440..4c7` are not implemented.
The selected generic 6.0 SHA path does not read them.
The savepath reads them only for algorithm variants `0x13..0x18`.

### AUTH_CFG fields

The model requires these fields for a hash request:

| Field | Accepted value |
| --- | --- |
| AUTH_ALG (bits2:0) | 1, SHA |
| AUTH_MODE (bits8:6) | 0, hash |
| AUTH_SIZE (bits13:9) | 0 selects SHA-1. 1 selects SHA-256. 2 selects SHA-384. 3 selects SHA-512. |
| USE_HW_KEY_AUTH, USE_PIPE_KEY_AUTH | Both zero |
| COMP_EXP_MAC, F9_DIRECTION | Both zero |
| AUTH_NONCE_NUM_WORDS | Zero |
| AUTH_LAST | Set for a final request |
| AUTH_FIRST | Accepted. The model always uses the programmed IV and count. |

AUTH_POS and AUTH_KEY_SIZE have no effect without a cipher and a key.
The model ignores those fields.
The selected firmware may set AUTH_POS for requests that never enable encryption.
Linux sets AUTH_SIZE to 1 only for SHA-256.
Linux uses size 0 for SHA-1.
The selected QSEE gate calls give the full enum.
They store size 0 for SHA-1, 1 for SHA-256, 2 for SHA-384 at `156c6bdc`, and 3 for SHA-512 at `156c6f7c`.
Digest lengths `14`, `20`, `30`, and `40` at `156c5c98`, `156c6704`, `156c6aac`, and `156c6e4c` agree with this order.
A SHA-224 request uses the SHA-256 compression with a caller-provided initial IV.
The model does not advertise SHA-224.

### Key and nonce programming

The selected QSEE command-element path writes the key words and the nonce words before `AUTH_SEG_CFG`.
The loop at `156d7994..156d7a14` writes QCE offset `3040 + i*4` for sixteen words.
Each word is a caller key byte group or zero when the caller supplies no key.
The same path writes `AUTH_INFO_NONCE0..3` at `350..35c`.
Those words are zero except for request types 6 and 7.

Linux `regs-v5.h` names the whole bank:
`ENCR_KEY0..7` at `3000..301c`, `ENCR_XTS_KEY0..7` at `3020..303c`,
`AUTH_KEY0..15` at `3040..307c`, and nonce words at `350..35c`.
The physical bank is `01dfd000..01dfd07f`.

The model retains 32 key words with write-time CONFIG byte lanes and four raw nonce words.
It accepts these writes before GO.
Plain SHA reads neither the keys nor the nonces.
Key reads return zero as an explicit limitation.
The model makes no claim about the silicon readback of a key register.
The nonce words read back as programmed.
No captured read establishes that value, so it is a model choice that gives the state a consistent view.

CMAC accepts the virtual OEM key or a supplied AES-128 or AES-256 key.
Other keyed modes remain unsupported.
A nonzero `AUTH_NONCE_NUM_WORDS`, an HMAC mode, a cipher configuration, or a pipe key flag still rejects the request.
Retained key words alone never create a MAC or a cipher result.

### AES-256 CMAC with the OEM hardware key

Run717 passes the RPMB capacity check in QSEE.
The RPMB frame validator at `1574c2f8` then derives its key at `1574c3f8`.
The derivation calls the capability gate at `156da994` with algorithm 4.
The gate tests `CRYPTO_ENCR_AES_SEL` bit0 at `156d2448`.
With bit0 clear, it returns 5 and the validator fails at `1574c3fc`.

A diagnostic build set bit0 and rejected every AES request.
Run727 then recorded the actual request.
QSEE wrote GO value 7 to `01dfc000` through its BAM command list.
Linux `regs-v5.h` names QCE offset `2000` `REG_GOPROC_OEM_KEY` and `1000` `REG_GOPROC_QC_KEY`.
The request had these fields:

| Field | Run727 value | Linux `regs-v5.h` meaning |
| --- | --- | --- |
| AUTH_SEG_CFG | `65e52` | |
| AUTH_ALG (bits2:0) | 2 | AES |
| AUTH_KEY_SIZE (bits5:3) | 2 | AES-256 |
| AUTH_MODE (bits8:6) | 1 | CMAC |
| AUTH_SIZE (bits13:9) | 15 | 16-byte MAC |
| AUTH_POS (bits15:14) | 1 | After |
| AUTH_LAST, AUTH_FIRST | 0, 1 | First block, not final |
| USE_HW_KEY_AUTH (bit18) | 1 | Hardware key |
| SEG_SIZE, AUTH_SEG_SIZE | `10`, `10` | One AES block |

QSEE also wrote `AUTH_KEY0..15`, but USE_HW_KEY_AUTH selects the hardware key.

The OEM-key path accepts this request shape.
AUTH_FIRST starts the CMAC chain at zero.
A request without AUTH_FIRST loads the chain from `AUTH_IV0..3`.
A nonfinal request encrypts each block into the chain and publishes the CBC-MAC state.
A final request applies the RFC 4493 subkey to its last block and publishes the MAC.
The model writes the 16 result bytes to `AUTH_IV0..3` in the same byte order as a SHA digest.
The result dump therefore carries the bytes in their natural order.

The OEM hardware key comes from secure fuses on silicon.
No capture provides it, and the model never reads a guest key word for it.
The default virtual OEM key is the SHA-256 digest of the public label
`qcom-vp virtual GPCE OEM hardware key v1`.
Fresh RPMB state has no key. Retained RPMB state must match its configured identity.

The model rejects the plain GOPROC with USE_HW_KEY_AUTH set.
The OEM alias also requires USE_HW_KEY_AUTH.
The supplied-key path uses plain GOPROC with USE_HW_KEY_AUTH clear.
GOPROC_QC_KEY has no selected consumer and stays unmapped.

The `qce-bam` fixture checks a nonfinal block, a final partial block, and a final full block.
Python `cryptography` computes the expected values with the same virtual key.
The fixture also checks both invalid key-source combinations.

### Virtual OEM key input

The model accepts the optional CCI parameter `oem_hw_key_hex`.
The SoC platform wrappers apply it through `platforms/qcom/sm88xx/security-roots.lua`.
Omit the field or set it to an empty string to keep the SHA-256 digest of the public label above.
A nonempty value must contain exactly 64 hexadecimal characters.
Do not add a `0x` prefix or whitespace. The model accepts uppercase and lowercase letters.
The model decodes the characters to the 32-byte AES-256 key before simulation starts.

The configured key applies only to OEM-key AES-256 CMAC requests.
The request must use `USE_HW_KEY_AUTH` through `GOPROC_OEM_KEY`.
Hash operations and the TME transport-key path do not use this value.
The default public-label key and configured key remain in the QCE module across guest reset.
A new simulator process initializes the key again from its profile.

Both values enter the virtual model.
The default value is public test material. An explicit value needs its own provenance.
The profile does not export phone identity by default or prove compatibility with captured sealed objects.

The [native provider trace](../../../docs/environment-inventory.md#native-provider-and-hardware-dependency) connects UID `0x0b` construction to this existing path.
It does not qualify the secure-file operation that uses the resulting cached key.
The separate [active experiment](../../../docs/environment-inventory.md#native-supplied-key-derivation) exercises operation one on the native object.

### CMAC with a supplied key

The model accepts AES-128 and AES-256 keys through `AUTH_KEY0..3` and `AUTH_KEY0..7`, respectively.
AUTH_KEY_SIZE values zero and two select those sizes.
AUTH_SIZE must select a 16-byte MAC, and AUTH_MODE must select CMAC.
Plain GOPROC starts the request with USE_HW_KEY_AUTH clear.
The OEM alias, pipe keys, other sizes, and other AES modes remain rejected for this path.

The key words use the CONFIG byte lanes at each write, not the lanes at GO.
With bit nine clear, each register word holds a big-endian key group.
With bit nine set, the writer supplies the reversed word.
GO captures the decoded key in the AES schedule before the stream supplies data.
An attempted key write during an active request sets CFG_CHNG_ERR without changing that request.
The temporary key buffer uses stack storage, and the model clears it after schedule initialization.

Linux `qce_setup_regs_ahash` establishes the first programming form.
It writes big-endian key groups before it sets CONFIG bit nine for GO.
The source is [Linux v6.18 common.c](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/crypto/qce/common.c).
The [register definitions](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/crypto/qce/regs-v5.h) identify both key sizes, CMAC mode, key registers, and CONFIG bit nine.
These sources establish the register contract, not physical qualification of SM8850.

The `qce-bam` fixture checks the following boundaries:

1. The vectors cover both key sizes and include empty, complete, and partial messages.
2. Big-endian key writes remain valid when CONFIG changes before GO.
3. Little-endian writes produce the expected MAC.
4. A saved CBC-MAC chain and byte count continue across separate segments.
5. A key write during an active request cannot change its result.
6. A later request uses its newly supplied key.
7. Invalid key-source combinations and the unsupported size encoding fail.

RFC 4493 and NIST SP 800-38B supply the standard vectors.
Python `cryptography` independently checks the partial message and the intermediate CBC-MAC chain.
The existing OEM-key profile test also passes without a change to its expected values.

### IV and byte-count byte order

QSEE `156c68e4` programs engine type 2 through `156d2824` with these SHA-256 IV words:

`67e6096a 85ae67bb 72f36e3c 3af54fa5 7f520e51 8c68059b abd9831f 19cde05b`

Each word is the byte reverse of the mathematical SHA-256 state word.
The selected request sets CONFIG little-endian bit9.
`156d7528` loads these words from the context.
`156d753c` writes them unchanged to AUTH_IV.
The init paths use the same reversed form for the SHA-224 initial IV, for example `d89e05c1` for `c1059ed8`.

Linux `qce_setup_regs_ahash` writes the mathematical state words while CONFIG has little-endian bit9 clear.
It then sets little-endian mode and writes GO.
The model therefore treats an AUTH_IV MMIO word as a byte-lane value.
With CONFIG bit9 set, the state word is `bswap32(word)`.
With CONFIG bit9 clear, the state word is the word value.
A read applies the inverse mapping.
An LE store of the read word therefore yields the big-endian digest bytes.
A writer that keeps CONFIG bit9 clear during the IV write uses mathematical state words.
A writer that sets bit9 first uses byte-reversed state words.
QSEE uses the second form.

The AUTH_BYTECNT registers are not byte-lane data.
The selected savepath reads them without a byte reverse at `156d6764..156d67d4`.
The model stores and reads them as numeric words.
Word 0 holds bits63:32 and word 1 holds bits31:0.
This order follows the big-endian 64-bit SHA length field.
No capture distinguishes this order from the opposite pair order.
Words 2 and 3 must be zero.

The result dump uses big-endian byte fields.
The dump path of the savepath byte reverses each count word at `156d6710` and `156d6724`.
Linux `qce_ahash_done` copies the dump auth IV bytes directly into the digest.
Linux converts each dump count word with `cpu_to_be32` before the next request.
This establishes the dump byte order.

## SHA operation behavior

The model loads the programmed IV words and the programmed byte count at GO.
It accepts AUTH_FIRST with a nonzero programmed IV.
It does not replace a caller-provided IV with a fixed standard IV.

A nonfinal request must end on a block boundary.
The block is 64 bytes for SHA-1 and SHA-256 and 128 bytes for SHA-384 and SHA-512.
The programmed byte count must be a multiple of the block size.
The model adds the segment size to the programmed count.
On AUTH_LAST it appends `80`, zero bytes, and the big-endian bit length.
The length field is 64 bits for SHA-1 and SHA-256 and 128 bits for SHA-384 and SHA-512.
It then compresses the final block.
On a nonfinal request it publishes the running state without padding.

The model publishes AUTH_IV and AUTH_BYTECNT only after all AUTH_SEG_SIZE bytes arrive.
An intermediate MMIO read of AUTH_IV returns the state before the request.
This prevents a partial digest from looking complete.

The model uses the OpenSSL low-level SHA context API.
The same adapter pattern exists for SHA-384 in `models/qcom/sm88xx/tme_mailbox/sequencer_hash.h`.
The model injects the programmed state into the `h` words of `SHA_CTX`, `SHA256_CTX`, or `SHA512_CTX`.
A SHA-384 or SHA-512 state word uses two AUTH_IV words, high half first.
QSEE `156c6c84..156c6e1c` programs the SHA-384 initial value in this order.
QSEE `156d7528..156d753c` writes all 16 AUTH_IV words.
It reads 16 words back at `156d6618..156d6684` because the version is above `05040000`.
The model sets `Nl` and `Nh` to the programmed bit count and clears `num`.
`Update` streams the input bytes.
`Final` performs the FIPS 180-4 padding and produces the digest bytes.
A nonfinal request exports the context `h` words.
The hardware byte count stays separate and unpadded.
The model reuses the member contexts without a per-block state copy.
No per-request heap allocation exists.

### GO command

The implemented GO bits follow `regs-v5.h`:

| Bit | Name | Model behavior |
| --- | --- | --- |
| 0 | GO | Required. Starts the request. |
| 1 | CLR_CNTXT | Accepted. The model always loads the programmed context, so this bit has no separate effect. |
| 2 | RESULTS_DUMP | Selects the 128-byte result dump. |

Any other GO bit is an error.
A GO write during an active request does not change the request.
It latches CFG_CHNG_ERR in STATUS.
QSEE writes GO values 3 and 7 at `156d533c..156d5360`.
Linux writes GO bit0 and optional bit2 in `qce_crypto_go`.

## Result dump

The buffer is 128 bytes.
The size comes from `QCE_RESULT_BUF_SZ` in Linux `dma.h`, which aligns the 104-byte structure to 64 bytes.

| Byte offset | Content |
| --- | --- |
| 0..63 | 16 AUTH_IV words as big-endian bytes |
| 64..79 | 4 AUTH_BYTECNT words as big-endian bytes |
| 80..95 | 4 encryption counter IV words, zero for a hash |
| 96..99 | STATUS as a big-endian word |
| 100..103 | STATUS2 as a big-endian word |
| 104..127 | Zero padding |

The dump is available only after the full input segment arrives.
The model produces it only when GO bit2 selects it.
Direct AUTH_IV reads expose the same state as the first digest bytes of the dump.
A read of the output pipe before completion returns zero transferred bytes.
The BAM then writes no memory.

## BAM stream contract

The BAM selects the pipe pair through CONFIG bits8:5.
Pipe `2n` carries RAM to QCE input.
Pipe `2n+1` carries the QCE result to RAM.
This matches the sibling BAM model and the upstream `P_DIRECTION` handling.

The BAM reaches the pipe rings and the RAM buffers through its `dma_socket`.
That socket may terminate at an SMMU stream port, so the ring and buffer addresses are IOVAs.
The BAM reaches the QCE and BAM registers for opcode 4 command elements through `command_socket`.
That socket is a physical register path.
The two paths carry the same captured security attributes, and neither path substitutes for the other.

A stream transaction carries `BamStreamExtension`.
`transferred` is a byte count from zero to `data_length`.
Zero means backpressure, not completion.
The model consumes exactly `transferred` input bytes.
A short read fills exactly `transferred` dump bytes.
The model never writes caller memory for input.
The model never treats a zero-length read as an end marker.

The model raises `stream_ready` only from its own thread.
This avoids a re-entrant BAM callback during a stream transaction.
A true write is a deliberate wake event.
The model writes false when no capacity remains.
A stalled pipe sees a new true write after GO or after a dump becomes available.

A pipe outside the selected pair returns a command error.
A read on the input pipe and a write on the output pipe return a command error.
Before the first CONFIG write the model stalls stream accesses because the caller did not select a pipe set.

## Error boundaries

The model rejects these cases with STATUS SW_ERR, STATUS done, and the top STATUS2 error bit.
It publishes no digest and no result dump.

| Case | Reason |
| --- | --- |
| AUTH_ALG not SHA or AES | KASUMI, SNOW3G, and ZUC are not implemented |
| AUTH_MODE not hash for SHA | HMAC needs a key and is not implemented |
| Unsupported AES mode, key size, or source | Only supplied-key AES-128/256 CMAC and OEM-key AES-256 CMAC have qualified consumers |
| AUTH_SIZE above 3 for SHA | No selected source names another size |
| Pipe key flag, or HW key flag on SHA | Pipe and hardware keys apply only to the OEM-key CMAC |
| Expected-MAC or F9 bit | Authenticated decrypt is not implemented |
| Nonzero nonce word count | CCM nonce use is not implemented |
| Nonzero AUTH_SEG_START | Segment offsets inside a stream are not implemented |
| SEG_SIZE differs from AUTH_SEG_SIZE | Hash-only flow needs one byte count |
| Nonzero encryption configuration | Cipher engines are not implemented |
| Count words 2 or 3 nonzero | A 128-bit counter is not implemented |
| Unaligned programmed count | The engine continues from a block boundary |
| Partial nonfinal segment | The hardware contract requires block alignment |
| Input longer than AUTH_SEG_SIZE | The descriptor does not match the request |
| Unread result dump before a new GO | The model does not overwrite a pending result |
| Wrong stream pipe | The selected pipe pair separates contexts |

A configuration write during an active request latches CFG_CHNG_ERR and leaves the request unchanged.
STATUS and STATUS2 writes are exempt.
A key-bank or nonce write is accepted before GO and retains its word.
A retained key word never produces a MAC or a cipher result.
A program error is visible to the firmware and does not become a false success.

## Implemented limits

- Segment size is a 32-bit register value.
- The accumulated count is 64 bits.
- The maximum message length is `(2^64 - 1) / 8` bytes, because the bit length must fit in 64 bits.
- The engine keeps 16 AUTH_IV words. SHA-1 uses 5. SHA-256 uses 8.
- The block size is 64 bytes for both algorithms.
- The result dump is 128 bytes.
- The model retains 32 key words and four nonce words.
- Key reads return zero. Supplied-key CMAC consumes four or eight retained AUTH_KEY words.
- The OEM-key CMAC uses the virtual OEM key. No MMIO path exposes it.
- The model uses fixed stack and member buffers only.

## Remaining unknowns

- The FIFO_SIZES value is not recovered. The model reports zero and rejects writes.
- The vendor STATUS2 bits other than bit0, bit30, and bit31 are not decoded.
- The private words at `11c`, `124`, `128`, and `13c` have no decoded function.
- The private result block at `440..4c7` has no decoded layout.
- SHA-224 selection depends on a caller-provided IV and caller-side truncation.
- Abort and context-clear semantics beyond `CLR_CNTXT` are not recovered.
- Result dump status byte order outside little-endian mode is not observed.
- The silicon readback of the ENCR, XTS, and AUTH key words is not observed.
  Zero is a model limitation and not a measured value.
- The hardware use of the nonce words for request types 6 and 7 is not decoded.
- The BAM pipe direction for this firmware comes from the sibling BAM model, not from a captured trace.
- The consumer of the key-manager info record and its version checks are outside the decoded path.

## Virtual policy and crypto boundary

The helper declares eight first-bank slots and sixteen total slots.
It declares destination selectors independently from TME identifiers.
Numeric equality between destination eight and TME source eight establishes no identity or authority.
The model retains the written key and nonce words and never exposes key material.
It contains no key export handler. CMAC supports supplied AES-128/256 keys and the virtual OEM key.
The separate authenticated import path stores a key but enables no keyed operation.
The constructor reports the cold virtual policy and the implemented hash engines.
Unimplemented status words remain errors.
It emits `QCE_V6_UNSUPPORTED` and returns a TLM command error.
The existing access monitor then records the failing guest access.
Unknown register addresses, unsupported control bits, invalid widths, and byte enables remain errors.
No generic retained aperture accepts unknown accesses.

The warm-up poll cannot complete merely because firmware writes the start control.
TME sequencer command `1c` is the only producer.
The TME model derives a virtual transport key and sends it over the `tpkey` sideband.
The GPCE keeps that key in hidden state and sets `01df1020` bit8.
Without command `1c` the bit stays clear.
If the broadcast arrives while bit8 of `01df101c` is clear, the key stays pending.
The later window write accepts it and sets the bit.
The native `tme-warmup` fixture covers the cold status, the open window, the deferred delivery, and both refusal paths.

The public `tme_hwkm_master_defs.h` names the transport key, profile, and destination.
The selected GPCE code and public `ice.c` both use bit8 as the receive-window control.
The real key derivation does not appear in either firmware image.
[INFERENCE] The real transport key derives from fused chip-unique material.
`docs/qcom/sm88xx/stubs.md` records the virtual seed substitute.

The model exposes no key read path and no blob export path.
It unwraps only its declared virtual format and it authenticates no image.
The wrap format and its failure modes are in `models/qcom/sm88xx/tme_mailbox/evidence.md`.
The silicon blob cipher and authenticated layout remain unrecovered.

### Authenticated destination import

`15704158` opens the TPKEY window, queues two descriptors, and waits for the status bit.
`15703f08` performs the FIFO access through `01df2000..01df2060`:

| Offset | Use in the selected code |
| --- | --- |
| `2000` | Control. Bit0 starts the request. Bit1 toggles around it. |
| `2004` | Input status at mask `7c000`, output status at mask `3e00` |
| `2008` | Done status. Bit1 reports completion. |
| `2010` | Word written back once before the request |
| `201c` | Input data word |
| `205c` | Output data word |

`157051f8` builds the import record and `15703f08` sends it.
The record holds one command word and 96 blob bytes, in 25 words.
The command word is `c8000004 | (command[8] << 5) | (command[9] << 13)`.
The normal consumer supplies destination `01dfab14` in field eight and auxiliary selector `3e` in field nine.
An earlier note gave a 60-byte payload and 19 words.
That count cannot carry the 92-byte export prefix.
The 25-word count matches the observed blob and the consumer loop.

The model implements the authenticated part of this path.
`import_wrapped_key()` requires a delivered transport key, a destination below the declared total count, and auxiliary selector `3e`.
It checks the blob extent, the magic, the length, the zero padding, and the tag before it copies any material.
A failed check leaves the destination absent.
The hidden destination store has no MMIO read path.

### FIFO import transaction

`15703b74` drives the transaction and `15703f08` selects the engine base.

| Offset | Selected use | Model behavior |
| --- | --- | --- |
| `2000` | Control. Writes 2, clears 2, then sets 1. | Retains bits 0 and 1. Bit0 starts a request and clears the transaction state. |
| `2004` | Bit `7c000`: input space. Bit `3e00`: output data. | Reports `7c000` while the request accepts a word, and `3e00` while output words remain. |
| `2008` | Bit1: done. Bit3: startup control. | Bit1 reports a completed transaction. A write of bit1 clears it. Bit3 keeps its startup value. |
| `2010` | Read, then write the same value. | Retained acknowledgement word. |
| `201c` | Input data word. | Appends to the 25-word request. |
| `205c` | Output data word. | Pops one of the two response words. |

The request is one command word and 96 blob bytes.
The command word is `c8000004 | (destination << 5) | (auxiliary << 13)`.
The model installs the blob only after the authenticated import succeeds.
It then publishes two output words, where the second word is the status.
Status zero means installed. Status one means the command or the authentication failed.
A malformed command word, a tampered blob, a missing transport key, and an unknown destination all report status one and install nothing.
The done bit appears only after the transaction finishes.

The status encoding at `2004` is a virtual nonzero form. The selected code tests the masks only for nonzero values.
The FIFO depth, the exact count fields, and the `2010` meaning remain unrecovered.
The native `tme-wrap` fixture drives this sequence end to end, including the tampered and malformed cases.

## HWKM shared group and key-manager status

QSEE `15703ac8` reads the key-manager words at `01df0000`, `01df0004`, `01df000c`, `01df1000`, `01df1004`, `01df101c`, and `01df1020`.
Those accesses establish the aperture.
The source-only `CRYPTO0_CRYPTO_TOP+0x30000` composition, the `0x9800` size, and the internal group split remain unestablished.
The model keeps only the words this boot path uses.

`15703ac8` is the key-manager info getter.
Argument `0x100` selects the record the current boot path requests.
The getter reads these words:

| Register | Address | Access | Model source |
| --- | --- | --- | --- |
| `KM_IPCAT_VERSION` | `01df0000` | read | Declared identity word, no independent record |
| `KM_KEY_POLICY_VERSION` | `01df0004` | read | Declared version 4, no independent record |
| `KM_KEYTABLE_SIZE` | `01df000c` | read | Declared slot count, 16 |
| `KM_CTL` | `01df1000` | read and write | Retained firmware write |
| `KM_STATUS` | `01df1004` | read | Boot-init and key-table state |
| `TPKEY_RECEIVE_CTL` | `01df101c` | read and write | Receive window |
| `TPKEY_RECEIVE_STATUS` | `01df1020` | read | Key delivery state |

`KM_IPCAT_VERSION` and `KM_KEY_POLICY_VERSION` are model declarations.
The values `0x02020000` and `4` come only from the source-only definitions.
This review found no independent record of those reset values.
The getter reports them to QSEE, so they remain a publication blocker if a consumer checks the identity.

The getter copies the three identity words into the record.
It also reports bit8 and the destination slot of `TPKEY_RECEIVE_STATUS`.
It reports `KM_STATUS` bit6 as `CRYPTO_LIB_BIST_ERROR` and bit8 as `BIST_ERROR`.
No other `KM_STATUS` field reaches this caller.

`KM_SHARED_STATUS.KM_BUSY` follows work that the engine accepted and did not finish.
A command is in flight between its first word and its response.
A delivered transport key waits while the receive window stays closed.
The cold model reports neither condition.

`KM_CTL` retains the firmware write.
The write that clears `CRC_CHECK_EN` sets the boot-init state.
That write is the only self-test step the model runs.
`KM_STATUS.BIST_DONE` and `CRYPTO_LIB_BIST_DONE` follow that state.
`KT_CLEAR_DONE` reports an empty key table.
An installed destination clears it.
The model runs no boot command list, so the boot-list bits and both error bits stay zero.
[INFERENCE] Silicon sets the boot-list bits from the hardware boot sequencer.

`TPKEY_RECEIVE_STATUS` reports the destination slot that the receive window named.
`15703a28` names slot `3e`.

The model keeps no other shared-group word.
Writes to the read-only words and reads of unmapped offsets fail.
`01df0100` is one such read.

The native `tme-warmup` fixture covers this group.
Case four checks the identity values, the empty key table, and the boot-init transition.
Case five checks that `01df0100` fails.

## Composition and integration

`src/platform.cc` registers `qcom,qce-v6` with `qcom_vp::QceV6` from `models/qcom/gpce/qce_v6.h`.
Both runtime compositions invoke `platforms/qcom/sm88xx/qsee-gpce.lua`.
The Lua object is `p.gpce`.
The Lua assertion and overlap guard keep the nine narrow APSS mappings.
The first mapping is the HWKM shared group at `0000` and covers `0x10` bytes.
The key bank at `01dfd000` covers `0x80` bytes and reaches the model at offset `d000`.
QSEE function `156d69d0` writes zero to QCE offset `5000` through a BAM command.
That physical word is `01dff000`. The model retains it and gives it no lock or completion effect.
The bindings are:

- `p.gpce.target_socket` to `gpce_register_router.initiator_socket`
- `p.crypto_bam.stream_socket` to `p.gpce.stream_socket`
- `p.gpce.stream_ready` to `p.crypto_bam.stream_ready`
- `p.crypto_bam.dma_socket` to the translated RAM path
- `p.crypto_bam.command_socket` to the physical register router

The BAM command base is `01dc0000`.
An opcode 4 command element at `3a13c` therefore reaches `01dfa13c` through `command_socket`.
That path is physical and bypasses the SMMU.
Ring, payload and result buffers use `dma_socket`, which reaches RAM after translation.
The platform must bind both sockets. A missing `command_socket` fails a command descriptor and
does not use `dma_socket`.
The existing XPU owner retains its separate aperture at `01df8000`.
The model still rejects undocumented words inside its mapped banks.

SM8850 run185 exposed the initial whole-window collision with that XPU owner.
The overlap guard rejected composition before guest execution.
The narrow mappings preserve that guard and the existing protection controller.
SM8845 run146 passes its previous BIST-status boundary and next rejects a GPU clock policy write at `03d990d4`.

## Verification status

The current `qce-bam` fixture passes with the split transport, the SMMU stream port and the
real BAM, QCE and SMMU models.
The pass covers these behaviors:

- SHA-256 of `abc` gives `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
- SHA-1 of 64 times `a` plus `bbb` gives `ffeceea41e39ec070be9be367748d417dda81bb7`.
- A SHA-1 continuation restores its IV and count after an intervening SHA-256 request.
- A result descriptor before GO receives backpressure and causes no DMA write.
- A command list that resets the BAM does not execute its remaining commands.
- The SHA-256 command list writes `AUTH_KEY0..15` at physical `01dfd040..01dfd07c`
  and `AUTH_INFO_NONCE0..3` at physical `01dfa350..01dfa35c` before GO.
- Key reads return zero for all 16 programmed words.
- Nonce reads return the four programmed words after the operation.
- A nonsecure doorbell cannot use the attributes from an earlier secure submission.
- An NS-owned stream entry that selects secure context 111 faults before physical access.
- Ring, payload and digest traffic crosses IOVA `0x782740000` through genuine page tables
  into a separate physical RAM array.
- Opcode 4 writes reach the physical QCE and BAM registers and never use the translated path.
- An unbound command socket fails a command descriptor without completion.

The fixture does not prove the native DMA path.
The retained run 393 log records a BAM payload at IOVA `0x27c400670` with length 424 that fails
because the BAM bypasses SMMU translation for that buffer.
The QSEE callback `156b38b8` to `15729c00` returns a virtual address and synchronizes the SMMU.
The ring IOVA `0x82740000` coincides with physical RAM, so that value alone does not show a
correct bypass.
Native run 393 did not reach UEFI or the kernel.

## QSEE extended crypto capabilities

### Capability gate at 156d22a0

The selected SM8850 `tz.mbn` maps virtual address `15669000` to file offset `34000`.
Function `156d22a0` holds the capability gate and runs to `156d25f0`.
Six arguments describe one operation. `w1` is an algorithm number from 1 to 24.
`w2` is a mode number from 1 to 17. `w3` is the AUTH size enum.
`w4` is the AUTH key-size enum. `w5` and `w6` are option bits.

The function checks the tag word `57484543` at `ctx+8` and a nonzero base pointer at `ctx+0`.
It returns `00200003` for a bad argument.
It returns `5` when no engine matches the request.
It returns zero after it stores the operation state.

Two reads decide the result. Instruction `156d2510` reads `01dfa108`.
Instruction `156d2534` reads `01dfa140`.
The function ANDs each word with a bit set for the algorithm.
A nonzero result from `01dfa108` passes the gate under mask `fc07ffff`.
Otherwise a nonzero result from `01dfa140` passes the gate under mask `1fffff`.

The jump table at `157c9160` holds 24 byte offsets from `156d2384`.
The offsets select these required bits:

| Algorithm | Word | Required bit | Public name |
| --- | --- | --- | --- |
| 1, 2, 3 | 01dfa108 | 1 | `CRYPTO_DES_SEL` |
| 4 | 01dfa108 | 0 | `CRYPTO_ENCR_AES_SEL` |
| 5 | 01dfa108 | 3 | `CRYPTO_ENCR_KASUMI_SEL` |
| 6 | 01dfa108 | 2 | `CRYPTO_ENCR_SNOW3G_SEL` |
| 7 | 01dfa108 | 26 | `CRYPTO_ENCR_ZUC_SEL` |
| 8 | 01dfa140 | 1 | none |
| 9, 11 | 01dfa108 | 4 | `CRYPTO_SHA_SEL` |
| 10 | none | none | none |
| 12, 13 | 01dfa108 | 5 | `CRYPTO_SHA512_SEL` |
| 14 | 01dfa140 | 0 | none |
| 15 to 24 | 01dfa140 | 5 | none |

The bit names come from the public downstream
[`qcryptohw_50.h`](https://android.googlesource.com/kernel/msm-modules/securemsm/+/16e68ba50769e6fcd8a9ed6be6d3fa45145a6e09/crypto-qti/qcryptohw_50.h).
That header names the `01dfa108` bits for QCE 5.0 hardware.
It does not define `01dfa140`.
Algorithm 10 has no bits, so it always fails the gate.

Version tests change three entries. Algorithm 2 requires version `05050002` or later.
Algorithms 8 and 14 require version `05060003`, or a version above `0506ffff`.
The profile reports `06000000`, so every gate applies as the table shows.

The gate stores the accepted state before it returns zero.
It writes the algorithm and mode at `ctx+40` and `ctx+44`.
It writes the size enum at `ctx+48` and the key-size enum at `ctx+49`.
It also sets option bits at `ctx+0c`.
The submit path at `156d7cf8` shifts `ctx+48` into AUTH_SIZE bits 13:9.
The same path shifts `ctx+49` into AUTH_KEY_SIZE bits 5:3.

### Model policy

The model reports bits 0, 4, and 5 at `01dfa108`.
Bit 0 is `CRYPTO_ENCR_AES_SEL`, bit 4 is `CRYPTO_SHA_SEL`, and bit 5 is `CRYPTO_SHA512_SEL`.
Bits 4 and 5 match the implemented SHA-1, SHA-256, SHA-384, and SHA-512 paths.
Bit zero matches the supplied-key AES-128/256 and OEM-key AES-256 CMAC paths. Other AES requests fail with a logged rejection.
The operation start rejects any SHA AUTH_SIZE above 3.
The model reports zero at `01dfa140`.
The firmware therefore denies algorithms 8, 14, and 15 to 24.
It also denies DES, KASUMI, SNOW3G, and ZUC.

Zero at `01dfa140` is a truthful denial. It is not a hidden completion.
The model has no engine behind any bit in that word.
A set bit would claim an operation that the model cannot perform.
The model keeps the word at zero until a selected source names a bit and an implementation exists.

### Loader result in run 680

Run 680 prints four lines on the UART in this order:

1. `Smc Invoke call failed ret 0xC`
2. `IQSEEComCompatAppLoader_loadFromBuffer failed ret = 12`
3. `QseeResponse->result = 0x7`
4. `Status = 0x7`

The DXE volume explains each line. `IMAGES/uefi.elf` holds an LZMA stream at offset `55268`.
The stream decodes to 4,685,832 bytes.
The module at volume offset `909b4` holds the SmcInvoke and ScmArmV8 code.
Its `.text` starts at RVA `1000`.

RVA `9d3a` holds the loadFromBuffer format string, and RVA `4fec` prints it.
The loadFromBuffer function stores the buffer pointer and length at `sp+10`.
It then calls the client environment method through the pointer at data `c560`.
It passes `1012` in `w3` at that call.
The method returns `12`, and the function returns EFI status `8000000000000007`.
Status code 7 is EFI_DEVICE_ERROR.

RVA `94cc` holds the response format string, and RVA `2970` prints it.
The ScmArmV8 path builds an SMC number as `w20+300` with parameter id 1.
It reads the response result word from `sp+78`.
A nonzero word prints the result and sets status `8000000000000007`.
The printed `7` is therefore a QSEE response value.
The gate function never returns 7. It returns 0, 5, or `00200003`.

RVA `9a6d` holds the failed SMC format string, and RVA `4388` prints it.
The argument is a global status word at data offset `4d0`.
The same line appears on a working reference device during `RemoteObj_invoke`
(<https://www.scribd.com/document/879914379/Boot-Log>).
A value of `0xC` therefore does not stop that device.

The capture does not connect the zero at `01dfa140` to the loader result.
The run reached the gate once, at the end of the simulation.
The load failure carries a different response code.
No selected instruction links the two events.

### Open checks

The model can log reads of `01dfa108` without changing its value.
The log can establish the order of the capability check and the loader failure.
A source that names `01dfa140` bits is required before the model advertises another engine.
A diagnostic build may set a capability bit only to record a request that the model then rejects.
Run727 used that method for the AES request. No committed build sets a bit without an implementation.

### Loader error dictionary limit

The public [IAppLoader.h](https://android.googlesource.com/kernel/msm-modules/securemsm/+/8c28e44bf54c65f6190dafeb6fef47a2a3e49092/include/smci/interface/IAppLoader.h)
names result 12 `ELF_SIGNATURE_ERROR`.
Its `loadFromBuffer` method packs one input buffer and one output object as `0x1001`.
The same revision's [IQSEEComCompatAppLoader.h](https://android.googlesource.com/kernel/msm-modules/securemsm/+/8c28e44bf54c65f6190dafeb6fef47a2a3e49092/smcinvoke/IQSEEComCompatAppLoader.h)
also names result 12 `ELF_SIGNATURE_ERROR`.
Its buffer method packs two input buffers, one output buffer, and one output object as `0x1012`.
Both headers carry `GPL-2.0-only` identifiers. No header code is copied here.
The selected compatibility call uses that count, but a matching count does not establish its remote object's identity.
Result 12 is consistent with this public error dictionary.
The exact selected response path and submitted app remain unestablished.

The caller is the Oplus storage driver, but its secure-app buffer has no identified source.
The prepared disk's `oplus_storagefw_a` partition contains only zero bytes.
The `oplus_sec_a` partition starts with an ELF image.
The partition contents do not prove which buffer the loader submitted.
The model needs the exact SMC request, the TZ response path, and the app identity before a hardware change.

# Selected DDR PHY and MC frontend

## Identity and ownership

This group covers the independent frontend routines called by selected XBL function `0x148a7530`.
It complements `controls-evidence.md`. It does not replace that group's four register lists.

Evidence uses these files from package `CPH2749export_11_16.0.5.700EX01_2026022701130186`:

- `IMAGES/xbl_sc.elf`, SHA-256 `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
- `IMAGES/xbl_config.elf`, SHA-256 `c35a368853ba57f00c9486d4b8fadeb0ded3a59ce410818a69ac415e3c44d3ef`.

Instruction coordinates are AArch64 virtual addresses in the selected XBL ELF.
Configuration coordinates are file offsets in the selected configuration ELF.
Static analysis used pyelftools and Capstone.
No matching public register description establishes physical names for `0x310a9134` or `0x310a9138`.
The names in the platform helper describe functional roles, not an established PLL register ABI.

The backend contract remains `build/private/xbl-ddr-contract.json` and `build/private/xbl-ddr-geometry-facts.json`.
The virtual backend supplies ideal error-free training and 4 GiB geometry.
Firmware owns its DCB, pointers, timing configuration, metadata, and publication functions.
This group changes none of those objects.

## Call order

The normal HAL calls `0x148a74e0`, SHRM startup `0x148adb08`, then `0x148a7530` at `0x148a8464`.
The frontend function contains these calls:

| Call instruction | Target | Operation |
| --- | --- | --- |
| `0x148a7580` | `0x148a656c` | DCB `+0x74` and per-pair `+0x9c` programming |
| `0x148a7584` | `0x148aa4bc` | Two source-control programming sequences |
| `0x148a758c` | `0x148aa520` | Selection and first handshake request |
| `0x148a7590` | `0x148aa548` | First acknowledgment poll |
| `0x148a7594` | `0x148aa568` | Second handshake and idle observation |
| `0x148a7598` | `0x148a6624` | DCB `+0x78` and five explicit control requests |
| `0x148a759c` | `0x148ad968` | Existing DCB controls group, unchanged |
| `0x148a75a0` | `0x148b092c` | MC configuration and guest timing data |
| `0x148a75bc` | `0x148afe98` | Three RMW words per enabled controller pair |
| `0x148a75cc` | `0x148aba3c` → `0x148b5b38` | DCB `+0x94`, frontend configuration, synchronous update polls |
| `0x148a75d4` | `0x148a9d38` with argument one | Set request bits at `0x310a9020` |
| `0x148a75e4` | `0x148ae974` | Guest sequence interpreter, empty for both selected DCB alternatives |
| `0x148a75e8` | `0x148aba40` → `0x148b5ed0` | Guest-selected timing configuration |

The initial stores and final stores in `0x148a7530..0x148a7638` update guest shared state.
They are not PHY MMIO.
The enclosing HAL later calls the original initialization command builder `0x148a8558` at `0x148a84b8`.
This model does not replace that builder or its SHRM transport.

## Additional offset/value programs

The interpreter remains `0x148ad764` → runtime-installed `0x148be4c0`.
Its arguments are base, record count, and guest record pointer.
Each record contains a 32-bit offset and a 32-bit value.
The host helper contains address sets only. It does not contain the vendor record values.

The two selected DCB payloads start at configuration file offsets `0x42bcc` and `0x4fbd0`.

| DCB pair | First section offset | Second section offset | Record count | Interpreter base |
| --- | --- | --- | --- | --- |
| `+0x74` | `0x55b0` | `0x56e8` | 94 in each | Zero, records contain absolute addresses |
| `+0x78` | `0x58a0` | `0x59d8` | 142 in each | Zero, records contain absolute addresses |
| `+0x90` | `0x6188` | `0x62c0` | 2319 / 2401 | `0x34b80000` |
| `+0x94` | `0xaa00` | `0xadc8` | 347 in each | `0x34a00000` |
| `+0x9c` | `0xb880` | `0xbc48` | One per enabled pair | `0x31bbe000` plus pair displacement |

The pair displacements are `0`, `0x1000000`, `0x400000`, and `0x1400000`.
The `+0x9c` record offset is `0x10`.
Address coverage matches between alternatives except for `+0x90`.
The `+0x90` union contains 2491 distinct addresses, within relative offsets `0..0xb528`.
The platform records this union and the explicit helper words as known configuration.
Other words within the bounded MC and LLC regions use the logged retained policy described below.

## Source controls and handshake

`0x148aa4bc..0x148aa51c` performs these RMW operations:

1. At `0x310a0018` and `0x310a1018`, replace bits 22..24 with `1`.
2. At both words, set bit 25.
3. At `0x310a0000` and `0x310a1000`, set bit 2.

No lock poll occurs in this function.
The existing `xbl_lpass_plls` instance owns these two banks at a `0x1000` stride.
Its compatible key is `qcom,gcc-taycan-eko-t-pll-v1`. The PHY helper does not create replacement mappings.
The reference handshake consumes the existing `PllSource` input rate.
The virtual operation requires a nonzero reference input, not a main or even PLL output.
This is explicit ideal analog policy. The physical output selection remains unproven.
Both original source helpers set USER_CTL fields and MODE bit two, without enabling an output gate or polling lock.
The authoritative source retains its programming, enable, vote, lock, and output behavior.

`0x148aa520..0x148aa544` writes the low selection nibble to `0x310a9130`.
It then writes `(old & 0x10) | 1` to `0x310a9134`.
`0x148aa548..0x148aa564` waits for bit 4, then writes `old & 0x10`.
`0x148aa568..0x148aa59c` repeats the request, poll, and request-clear sequence at `0x310a9138`.
The second handshake follows the first.

`0x148aa5a0..0x148aa5ac` reads `0x310a9010` and stores `(~value) & 3` to guest byte `0x3104d430`.
The functional observation word starts at zero and rejects writes.
This is an explicit ideal idle observation, not a measured hardware reset value or a training result.

The reference bank also owns DCB words `0x310a9000`, `0x310a9044`, `0x310a9084`, and `0x310a9120`.
Those words retain guest configuration without status synthesis.
The helper removes these addresses from its retained-word list to prevent duplicate apertures.

## Other explicit programming

`0x148a664c..0x148a66a8` performs five RMW requests.
Their addresses are `0x31320004 + i*0x12c`, for `i=0..4`.
Each write is `(old & 0x80000016) | 1`.
There is no status poll in this sequence.

`0x148afe98..0x148aff3c` accesses offsets `0x6ecc`, `0x6f0c`, and `0x6f24`.
The bases are `0x31b80000`, `0x32b80000`, `0x31f80000`, and `0x32f80000`.
The caller selects pairs through the guest mask at `0x3104ac7c`.
These RMW words retain their guest values. They do not assert training completion.

`0x148a9d38..0x148a9da4`, with argument one, writes `(old & 5) | 0xa` to `0x310a9020`.
The argument-zero path clears bits one and three, sets bits zero and two, and polls `0x310a9024`.
It waits for status bits zero and one to clear before clearing the control word.
SM8845 performs the same sequence at `0x148a83f0..0x148a845c`.
SM8850 run 76 and SM8845 run 73 reach this transition after SHRM initialization.
The ideal control transition retains each request and reports idle status zero.
This policy supplies no physical transition delay or unobserved register reset effects.

## MC configuration and data separation

`0x148b092c..0x148b0980` programs DCB `+0x90` before its additional helpers.
`0x148ba5bc..0x148ba83c` consumes guest runtime data through DCB `+0x38`.
Most operations copy timing fields into guest shared state.
Its MMIO helper `0x148afde4..0x148afe20` reads eleven frequency words at `0x34b8a140 + i*4`.
These words return the guest's earlier configuration writes, not host frequency tables.

The optional `0x148baf4c..0x148baff8` path programs these relative MC words:
`0x6d00`, `0xa30c`, `0xa314`, `0x6a88`, `0x6a80`, `0x6f14`, `0x6b50`, `0x6b48`,
`0x6b60`, `0x6af8`, `0x66c0`, `0x6fa0`, `0x6b58`, and `0x8ddc`.
Its tail calls `0x148ba840..0x148baf48`.
That routine programs clock levels 1..10 through `0x148b02b0` and RMW helper `0x148b0398`.
The latter uses getter `0x148b0320` and the same setter.

The setter computes exact offsets from level `L` and register index `R`:

- Kind 1: `4*(125*L + R)`.
- Kind 2: `0x3000 + 4*(125*L + R)`.
- Kind 4: `0x74fc + 4*(125*L + R)`.

The Lua address set includes both conditional branches at `0x148bab1c` and their helper accesses.
It does not model these configuration tables as memory.
`0x148b07c8..0x148b0928` copies selected software tables to guest shared state. It has no PHY MMIO.

## Synchronous frontend updates

`0x148b5b38..0x148b5ecc` first applies DCB `+0x94`.
Its explicit configuration uses these relative word groups:

- `0x6100`, `0x6114 + i*0x20 + j*4`, with `i=0..7` and `j=0..2`.
- `0x30d0..0x310c`, `0x3120..0x313c`, and `0x2080..0x209c`, with stride four.
- `0x5600..0x5610` and `0x561c..0x568c`, with stride four.
- `0x56c0`, plus reads at `0x6504` and `0x6060`.

At `0x148b5bbc`, XBL writes one to relative control `0x6020`.
After configuration, `0x148b5e5c..0x148b5eb8` writes one and polls bit zero until it clears.
The control offsets are `0x20`, `0x1020`, `0x2020`, `0x3020`, `0x5020`, and `0x6020`.
These are apply/update requests. They are not idle retained words.

`0x148b5ed0..0x148b5f74` then writes guest-selected timing fields at `0x4728`, `0x4730`, and `0x5040`.
It updates `0x3154`, writes one to `0x5020`, and programs words `0x57fc` and `0x57e4` through `0x148abcfc`.
The model preserves all guest configuration.

## Empty selected sequence and guest-owned data

DCB `+0x6c` selects a two-byte sequence at payload offsets `0x5460` and `0x5598`.
The sequence contains a zero terminator in both alternatives.
`0x148ae974` calls timing snapshot helper `0x148ae7a8`, then tests that first halfword at `0x148ae99c`.
It branches directly to `0x148aea2c` and calls timing reconstruction helper `0x148ae804`.
No interpreter operation or physical analog training routine executes through this selected sequence.
Both helpers operate on guest configuration and software timing tables.
The model does not replace these helpers, inject sequence data, or add training headers.

The earlier `0x148a74e0` tail at `0x148a6720..0x148a67a8` also copies configuration into guest storage.
It uses DCB pairs `+0x18`, `+0x38`, and `+0xa0`.
Those are data-copy calls to `0x148ad878`, not offset/value MMIO programs.
They require the existing guest storage mappings, not new PHY apertures.

## Functional substitute limits

`QcomDdrPhyProgram` uses compatible key `qcom,ddr-phy-program-v1`.
Lua supplies every address, accepted word, prerequisite, and dependency.
The class contains no SM8850 addresses.
Configuration values start at zero and retain accepted guest writes.
All transactions must be aligned 32-bit reads or writes without byte enables.
Unknown words outside the declared retained regions fail with an address error.
Invalid commands fail with a command error.

The two reference handshakes require the guest selection write.
The second handshake also requires acceptance of the first.
An accepted request can wait while the authoritative input reference is unavailable.
Bit 4 follows that input and accepted requests, not an unproven PLL output.
Clearing the request preserves its acknowledgment.
Reset does not manufacture an acknowledgment.

The LLC update controls require all 347 DCB `+0x94` words and the completed reference handshakes.
Each accepted write of one completes synchronously, so its request bit reads zero.
An update before accepted programming returns an error.
This behavior is an ideal programming substitute, not proof that arbitrary values train physical DDR.

Cold reset clears configuration acceptance, retained values, and command completion in each new bank.
Reset handling must reset all related banks together.
Dependency paths must form the acyclic graph supplied by this helper.
The programming readiness interface does not claim a physical clock rate or implement an analog clock source.

Existing retained controls own the remaining non-status words, except for these existing clock-control owners:

| DCB `+0x78` addresses | Existing owner | Selected write and acceptance |
| --- | --- | --- |
| `0x31320558`, `0x31320560` | Existing hardware-control branches | `0x08000003`, with bit 27 retained and continuous hardware demand |
| `0x310e4020`, `0x310e4040`, `0x31332028`, `0x31332050` | Existing interconnect retained controls | One, within mask one |
| `0x31333008 + i*8`, `i=0..8` | Existing interconnect retained controls | One, within mask `0x3d` |
| `0x31335090` | Existing interconnect retained control | `0x100`, within mask `0x3ff` |

The helper omits these sixteen addresses from its retained allowlist.
`0x3132055c` remains a separate PHY-owned configuration word.
Existing `QcomClockBranch` ignores written CLK_OFF and derives read status from its actual upstream source.
The branch owners include bit 27 in their retained masks.
The helper requires these existing owners before composition. It does not create detached substitutes.

Existing DCB `+0x80/+0x84/+0x88/+0x8c`, SHRM, virtual DRAM, UFS clocks, and power domains remain unchanged.
The new helper does not overlap those mappings.
Unknown frontend configuration uses explicit retained storage, not a completion generator.
No image load, authentication, firmware callback, or training-header flow is skipped.

## Integration and checks

The platform registers `QcomDdrPhyProgram` as `qcom,ddr-phy-program-v1`.
Both board helpers compose the existing reference source, clock branches, and guest storage mappings.
The native firmware executes its frontend functions before the first SHRM initialization command.
Guest regressions check reference ordering, update prerequisites, retained state, pair isolation, and command effects.

## SM8845 selected dependency group

`platforms/qcom/sm88xx/sm8845/ddr-phy.lua` uses the CPH2771 XBL and configuration ELF named in `controls-evidence.md`.
The bounded frontend is `0x148a5b60..0x148a5c68`.
Its call order matches the earlier platform, but its DCB address sets differ.
The following table records independently decoded SM8845 targets.
The comparison excluded relocated branch targets and PC-relative data bases, not MMIO operands.

| Operation | SM8845 target | SM8850 comparison target |
| --- | --- | --- |
| DCB `+0x74` and per-pair `+0x9c` | `0x148a4b84` | `0x148a656c` |
| PLL control requests | `0x148a8b74` | `0x148aa4bc` |
| Ordered reference requests and polls | `0x148a8bd8` | `0x148aa520` |
| DCB `+0x78` and five explicit RMW requests | `0x148a4c3c` | `0x148a6624` |
| Frontend lists and apply operation | `0x148abfd4` | `0x148ad968` |
| MC program and runtime timing data | `0x148aef88` | `0x148b092c` |
| Three words per enabled controller pair | `0x148ae604` | `0x148afe98` |
| LLC program and update operations | `0x148b3fd8` | `0x148b5b38` |
| Reference request bits | `0x148a83f0` | `0x148a9d38` |
| Selected sequence interpreter | `0x148ad0d4` | `0x148ae974` |
| Final timing configuration | `0x148b4370` | `0x148b5ed0` |

The reference, explicit controller words, frontend apply operations, and LLC update routines retain the same MMIO operands.
The authoritative `xbl_lpass_plls` owns both source banks.
The reference model observes those live banks through its existing PLL interface.
It never replaces PLL lock, programming, or output state.

### Independent configuration recovery

The two DCB payloads start at file offsets `0x3fb14` and `0x4cb18`.
The selected interpreter remains an offset/value word-store loop.

| Pair | First section | Second section | Records, first/second | Base |
| --- | --- | --- | --- | --- |
| `+0x74` | `0x56b8` | `0x56b8` | 81 / 82 | Zero, absolute addresses |
| `+0x78` | `0x5940` | `0x5948` | 142 / 142 | Zero, absolute addresses |
| `+0x90` | `0x6230` | `0x6238` | 2397 / 2397 | `0x34b80000` |
| `+0x94` | `0xad18` | `0xad20` | 347 / 348 | `0x34a00000` |
| `+0x9c` | `0xbb78` | `0xbb88` | 1 / 1 | `0x31bbe000` plus enabled-pair displacement |

Both MC alternatives contain the same 2397 distinct addresses.
The LLC alternatives contain 349 addresses in their union and 346 addresses in their intersection.
The model requires the common 346 words before an update.
It retains the alternative-only words without requiring an absent alternative's configuration.
This minimum prerequisite is explicit virtual policy.
It is not a claim that either physical program succeeds without its remaining words.

The recovered sets also include explicit accesses from the decoded helper routines.
The initial MC set contains 2437 words.
The initial LLC set contains 410 configuration words.
The separate retained set contains 238 words.
These counts exclude the reference bank, two clock branches, and frontend controls.
The bounded retained policy also accepts intervening MC and LLC configuration words with an explicit access log.

The optional timing routine at `0x148b940c` calls `0x148b8ce0`.
That routine differs from its SM8850 counterpart.
Its decoded setter and RMW calls establish the accepted timing words independently.
The setter at `0x148aea1c` and getter at `0x148aea8c` preserve the three offset formulas described above.
The RMW helper is `0x148aeb04`.
Levels remain 1 through 10.
Both conditional branches contribute accepted addresses.
Guest timing values remain guest-owned.

The SM8845 scalar clock table does not contain DCB branches `0x31320558` and `0x31320560`.
The helper reuses an existing clock-branch owner when one exists.
Otherwise, it creates branches against the existing `xbl_boot_clock_reference`.
This is an explicit virtual source, not evidence for a physical mux relationship.
Branch read status follows that source and accepted requests, not the written CLK_OFF bit.
The remaining DCB interconnect words retain guest values without physical timing claims.
They do not require SM8850 interconnect owners.

### Nonempty selected sequence

SM8845 DCB `+0x6c` selects four bytes at payload offset `0x5598` in both alternatives.
Its halfwords are `0x9802`, then zero.
It is not the empty SM8850 sequence.
Interpreter `0x148ad0d4` dispatches class two to `0x148b3204`.
The selected low twelve bits equal `0x802`.
At `0x148b3294`, the handler stores one to byte `0x3104aca8`.
This is shared software state, not a new PHY MMIO handshake.
The interpreter then processes the zero terminator.
Its snapshot and reconstruction routines at `0x148acf08` and `0x148acf64` preserve guest timing data.
No host sequence, training header, or firmware callback replaces these instructions.

### Functional boundary

The generic models provide synchronous frontend updates and ideal reference observations.
They do not simulate analog training margins, physical delays, or measured device geometry.
Physical masks and names for the unidentified DCB words remain unknown.
The exact downstream PLL output choice remains unknown.
The reference handshake uses the existing policy that requires the source's live input reference.
The later SHRM training substitute is separate and retains real shared SRAM and guest outputs.

Static work decoded both selected ELF files with pyelftools and Capstone.

## Runtime configuration and preparation observations

Both selected DCB alternatives write `0x08000003` to branches `0x31320558` and `0x31320560`.
SM8850 records occur at image offsets `0x48894`, `0x488a4`, `0x559d0`, and `0x559e0`.
SM8845 records occur at `0x4587c`, `0x4588c`, `0x52888`, and `0x52898`.
The branch owners retain bit 27 and accept continuous hardware demand.
CBCR bit 27 is `AACS_CLK_EN_CTRL`.
This name does not establish the physical identity of these two branches.

SM8850 instruction `0x148b5e48` reads `0x34a06604` and copies the word to shared address `0x3104bc8c`.
SM8845 performs the same copy at `0x148b42e8`.
The next read uses retained configuration `0x34a06060`.
The guest masks that value with `0x1fff` and stores it at `0x3104bc90`.
Both paths then request six existing frontend updates and wait for their completion.

The virtual frontend returns zero for the read-only observation at `0x34a06604`.
Its physical meaning remains unknown.
This is an explicit ideal hardware input, not a captured device value or an authentication result.
The model retains the adjacent guest configuration and completes updates through the existing request path.

## Per-pair MC configuration

SM8850 run 72 reaches the per-pair timing getter at `0x148b0388`.
It reads `0x31b875fc` after the reference requests and frontend updates complete.
The loop at `0x148b256c` derives four bases: `0x31b80000`, `0x32b80000`, `0x31f80000`, and `0x32f80000`.
Shared word `0x3104ac7c` selects the enabled pairs.

The getter at `0x148b0320` uses three timing regions at offsets zero, `0x3000`, and `0x74fc`.
Each row has a stride of 125 words.
Helper `0x148bbec8` updates field `0x40` in levels zero through ten.
Helper `0x148bbf14` also changes retained word `0x64ac`.
SM8845 getter `0x148aea8c` uses the same address formulas.

The virtual policy retains every word in these eleven timing rows.
The formula establishes the layout, not the physical validity of every field.
The existing selected DCB words remain available outside these rows.
The four pairs have independent storage with deterministic cold zero.
Broadcast writes update all four banks.
Broadcast reads return pair zero, as an explicit virtual selection policy.
The address translators select these banks without creating another register owner.

SM8845 run 70 also reaches the frontend RMW at `0x148b43e4`.
This reads `0x34a03154`, sets bits zero and one, and stores the result.
The word is retained configuration even though the selected SM8845 DCB does not initialize it.
The adjacent writes at `0x34a04728`, `0x34a04730`, `0x34a05040`, and `0x34a05020` retain their existing owners.

## Permissive retained configuration boundary

Each virtual MC and LLC window accepts 16-bit register offsets, `0..0xffff`.
This bounded virtual policy includes runtime words beyond the selected DCB's last address.
Each region has four independent banks and a broadcast view.
Known commands and observations retain their dedicated behavior.
Every access to another undeclared word emits `UNMODELED_DDR_CONFIGURATION_SUBSTITUTE`.

Unknown configuration starts at zero and retains full guest writes.
The model does not clear unknown request bits or manufacture ready bits.
This policy permits ordinary configuration without one-register-at-a-time discovery.
It does not establish physical register semantics.
Both the direct command engine and configuration storage share the same per-pair LLC views.
Separate absent-rank control objects are no longer necessary.

## Top-level controls and geometry publication

Both selected instruction streams reference eight control words relative to `0x31080000`.
Their offsets are `0x8`, `0x58`, `0x9c`, `0x12c`, `0x208`, `0x220`, `0x2c0`, and `0x30c`.
The existing retained owners already supply most of this group.
The PHY helper adds only missing words and preserves those owners.
These controls retain guest values from cold zero.

SM8850 uses the direct references at `0x14871580`, `0x148a6d50`, `0x148a78cc`, `0x148a8dc8`, `0x148a9e08`, `0x148a9e80`, `0x148ae1c4`, and `0x148ae604`.
SM8845 uses `0x1486fbe4`, `0x148a5368`, `0x148a5efc`, `0x148a7480`, `0x148a84c0`, `0x148a8538`, `0x148ac924`, and `0x148acd64`.
The two references to offset `0x9c` also write offset `0x2c0`.

The native geometry publisher writes LLC offset `0xd060` at SM8850 `0x148b6a4c` and SM8845 `0x148b4eec`.
This word exceeds the earlier DCB-only aperture.
The full virtual register window retains these runtime publications without replacing guest geometry calculations.

SM8850 run79 and SM8845 run76 publish the geometry registers and reach the LLC update sequence.
They stop because offset `0x4000` retains the request bit.
SM8850 `0x148abbbc` and SM8845 `0x148aa234` write one and poll bit zero at offsets `0x4000` and `0x4004`.
Both words now use the existing synchronous update owner.
The model retains the preceding configuration before it completes either update.

## PXI training frontend

The next SM8850 consumer starts at `0x148b7524`.
The independently decoded SM8845 equivalent starts at `0x148b59c4`.
Both paths follow the direct descriptor operation `0x8c`.
They do not immediately start PXI traffic.

The first branch tests guest byte `0x3104b1d2`.
If that byte is zero, XBL modifies eight halfwords at `*(uint64_t*)0x3104ac70 + 0x634`.
It replaces each nonzero value other than 500 with 500.
It also clears bits two and three in eight bytes at `0x3104afa3 + i*6`.
These are guest data effects. The host does not replace them.

SM8850 `0x148b75a8` and SM8845 `0x148b5a48` then populate reusable command slots.
The slot/opcode pairs include `9/0xe0`, `10/0`, `11/0xe4`, `27/0`, `0/0x60`, `1/0xa4`,
`2/0xa0`, `3/0x64`, `6/0x71`, `7/0x90`, and `8/0x8c`.
The descriptor owner must decode these words before a later trigger consumes them.
Writing a slot alone does not execute it.

### Producers and observations

All offsets below use the LLC aperture and its existing four pair views.
The broadcast base is `0x34a00000`.
The pair bases are `0x31a00000`, `0x32a00000`, `0x31e00000`, and `0x32e00000`.
The same reusable frontend serves both SoCs.

| Role | SM8850 producer or consumer | SM8845 equivalent | Address or field |
| --- | --- | --- | --- |
| Common PXI program | `0x148b99c4` | `0x148b7e64` | Controls near `+0x8010`, packet words at `+0xe000` |
| Read training program | `0x148b9c34` | `0x148b80d4` | DCB halfwords `+0x20/+0x22`, interpreter base `+0xe000` |
| Write training program | `0x148b9c80` | `0x148b8120` | DCB halfwords `+0x24/+0x26`, interpreter base `+0xe000` |
| Common packet program | `0x148b9c18..0x148b9c30` | `0x148b80b8..0x148b80d0` | DCB halfwords `+0x1c/+0x1e`, interpreter base `+0xe000` |
| Pattern producer | `0x148b891c` | `0x148b6dbc` | 32 words each at `+0xe780`, `+0xeb80`, `+0xed80`, `+0xef80` |
| Request builder | `0x148ababc` | `0x148aa134` | `+0x8020`, low five bits from argument one |
| Request fields | Same builder | Same builder | Bits `8..12` from argument two, bits `16..20` from argument three, plus bit 25 |
| Clock-hold control | Same builder | Same builder | RMW `+0x8044`, bit eight for nonzero argument one, bit ten for nonzero argument three |
| Completion consumer | `0x148b9968` | `0x148b7e08` | Wait until `(+0x802c & 0x1f) == 0x1f` |
| Beat-count reset | `0x148abb0c` | `0x148aa184` | Write one, then zero, at `+0x8074` |
| Beat-count observation | `0x148abb30` | `0x148aa1a8` | Read `+0x8070`, clear count while nonzero |
| Result consumer | `0x148b8970` | `0x148b6e10` | Two words at pair `+0xfce8/+0xfcec` |

The common program retains enable/control fields at `+0x801c` and transfer selection at `+0x8024`.
SM8850 `0x148b98f0` clears the beat count, configures the engine, requests mask `0x1e`, `0x1d`, or `0x1f`, then polls completion.
The lower-level traffic helpers also request mask `0x18`.
These masks identify participating engines. They do not establish a DRAM byte address or transfer length.

The result consumer combines the two pair words into one 64-bit value.
It stores that value at `argument_four + argument_two*8 + pair_index*16`.
Guest mask `0x3104ac7c` selects the pairs.
The native caller owns that destination and its later interpretation.
Neither the frontend nor SHRM may replace this guest pointer with a host-selected output area.

### Implemented state and data boundary

`pxi_frontend=true` enables the grouped PXI frontend in `QcomDdrPhyProgram`.
Each pair retains its own request, idle mask, beat count, and two result words.
Cold reset clears the request, count, and results. All five engines start idle.
Writing `+0x8074` with bit zero set clears only the beat count.
Abort fields make the selected engines idle without changing configuration.
Observation words reject writes.

The start request is the write to `+0x8020`, not the later clock-hold RMW.
The virtual engine executes synchronously after the existing LLC and reference prerequisites.
Engine bits are DQRD, DQWD, DQOP, CAWD, and CAOP, from bit zero through bit four.
The frontend executes the write stream before the read stream when both participate.
CA-only calibration requests have no DQ stream and use ideal electrical behavior.
They do not write a host-selected DRAM pattern.

The DQWD payload table starts at `+0xe600`. Its task table starts at `+0xe680`.
The DQRD payload table starts at `+0xea00`. Its task table starts at `+0xea80`.
Start indices come from the low bytes of `+0x8024` and `+0x8028`.
Each payload uses repeat bits `9..0`, task-count bits `24..20`, and task-pointer bits `29..25`.
The repeat field encodes one less than the repetition count.
Zero marks the end of the selected payload list.
The read payload's bit 30 clears the accumulated comparison result.
Valid, transmitting tasks consume their data pointers and data-selector fields.
The supported selectors are the guest data table and the guest-seeded PRBS stream.
The engine rejects unsupported selectors or SPG mode before it performs data transfers.
It also checks the complete selected stream against the populated rank capacity.

The functional stream transfers four bytes per x8 lane for each transmitting task.
Read comparisons use actual bytes from the same DRAM backing used by CPU and DMA accesses.
The two result words accumulate observed XOR expected bits for the controller's two lanes.
Thus a changed DRAM byte produces a nonzero comparison result.
Each read task adds two logical comparison beats to the count at `+0x8070`.
Completion appears only after all required transfers succeed.
Transport failure leaves the request incomplete.

The virtual PRBS recurrence uses polynomial mask `0x80200003` and the guest's two GPR seed words.
This is a deterministic virtual stream policy, not a recovered silicon PRBS polynomial.
The guest still supplies its pattern words, packet lists, task words, and seeds.
The frontend does not force comparison success when transmit and expected streams differ.

The native helper `0x148b8ac4` writes CA GPR words two and three at `+0xe308/+0xe30c`.
Its values `0x44000000` and `0x84000000` select the two rank streams.
The functional RRBC decode uses row bits `16..0`, column bits `26..17`, bank bits `29..27`, and rank-mask bits `31..30`.
The raw CA word at `+0xe384` supplies rank-mask bits `15..14` when the RRBC mask is zero.
The linear per-lane address is `(row*banks + bank)*2^columns + column`.
An x8 lane stores one byte per column address.
The normalized callback's column 512 therefore starts at per-lane byte offset 512, not 1024.
This is the selected training-address policy, not a general LPDDR5 command-bus emulator.
The static result consumer above does not read those locations through a CPU pointer.

`QcomVirtualDdr::pxi_transfer` uses 64-byte stripes across eight x8 lanes behind four x16 controllers.
The dense offset is `rank_offset + ((byte_offset/64)*8 + lane)*64 + byte_offset%64`.
Each lane holds 512 MiB. Each controller holds 1024 MiB.
The native density table at XBL `0x149267b6` supplies that controller capacity for MR8 `0x10`.
The native geometry table at `0x1492678e` supplies 15 rows and ten columns.
SM8845 uses geometry table `0x14921096`, density table `0x149210be`, and capacity writer `0x148b4b90`.
Its density-four, width-zero entries also give 15/10 geometry and 1024 MiB per controller.
The native writer stores four capacities at `0x148b4e84` and zero source bases at `0x148b4e8c`.
Rank one follows rank zero's total capacity across all eight lanes when a fixture enables it.
The board maps the first 2 GiB at `0x80000000` and the next 2 GiB at `0x880000000`.
Optional parameters `dram_lower_size` and `dram_upper_base` describe this aperture gap.
Zero values select contiguous memory for isolated model consumers.
Transfers split at stripe and aperture boundaries.
The method checks the full request range and address arithmetic before the first transfer.
This virtual interleave policy does not claim recovery of the physical controller's address hash.
No second memory object or private calibration backing exists.

The selected SM8850 DCB alternatives independently establish the matching payload, task, and data tables.
Their common program has 169 offset/value records at DCB pair `+0x1c`.
The read program has 44 records at pair `+0x20`. The write program has 70 records at pair `+0x24`.
The native helper updates these same table fields rather than a host-generated packet description.
The existing 64 KiB retained configuration policy and LLC update commands remain unchanged.
Unknown packet and configuration words still emit the existing configuration diagnostic.

The SHRM `0x0506` training path is a separate backend dependency.
Its completion must preserve the frontend configuration and satisfy its own saved-output effects.
SHRM saved physical state does not prove that PXI traffic executes.
The SHRM backend owns that lifecycle and its saved register image.

The guest regression executes PXI writes and comparisons through the real frontend and shared DRAM backing.
It checks CPU-visible data, channel isolation, a channel stripe boundary, and the split DRAM aperture.
It also changes a DRAM byte through the CPU and requires the corresponding mismatch result.
Clearing the beat count preserves that mismatch.
The integration run passes 77 tests and Clippy.
Native DDR completion and kernel execution remain unobserved.


## Logged clock configuration windows

SM8850 run82 and SM8845 run79 pass the SHRM configuration update at `0x310760a4`.
They next read `0x310a92d4`, at PCs `0x148aa0dc` and `0x148a8794`.
The SM8850 helper updates this word with the same retained fields as the adjacent reference controls.
It does not poll the word for completion.

The reference owner now covers `0x310a9000+0x1000`.
Its request, acknowledgment, idle observations, source checks, and required configuration retain their existing behavior.
The same owner receives the previously separate configuration words.
Unknown configuration accesses retain writes and emit the explicit substitute diagnostic.

Four configuration windows cover `0x310a8000..0x310a9000`, except the three existing masked controls.
Those separate controls remain at `0x310a8178`, `0x310a82bc`, and `0x310a83ec`.
The live PLL owners remain separate.
The common Lua helper assigns each retained word to one owner.

The global configuration owner covers `0x34bc0000+0x1000` with the same logged policy.
SM8850 `0x148aa130..0x148aa1ac` updates offsets `0x3c` and `0x40`, then performs a separate barrier read.
The selected DCB words preserve their full values.
The pair-specific controls at offset `0x134` retain their separate owners.
These windows are virtual configuration policies, not claims about undocumented silicon status.

## LLCC v6 topology and selected cache configuration

The earlier register boundary was LLCC, not an analog PHY.
SM8850 run83 and SM8845 run80 stopped at `0x348640b0`.
The instruction PCs are `0x148acca0` and `0x148ab30c`.
The native routines copy controller geometry into LLCC COMMON words.
They do not measure PHY calibration.

The LLCC COMMON block starts at relative `0x64000`.
Its FEAC, PMGR, and LPI_LB blocks start at `0x70000`, `0x7c000`, and `0xa6000`.
These block names describe address ownership, not every field's meaning.
Linux independently identifies the v6 COMMON offsets:
<https://github.com/torvalds/linux/blob/master/drivers/soc/qcom/llcc-qcom.c>.
The same driver defines the activation trigger, completion, and clear protocol.

Both selected OEM images use four local instances:

| Selector | Local aperture | Native pair index |
| --- | --- | --- |
| 1 | `0x31800000+0x200000` | 0 |
| 2 | `0x32800000+0x200000` | 1 |
| 3 | `0x31c00000+0x200000` | 2 |
| 4 | `0x32c00000+0x200000` | 3 |

The OR broadcast aperture is `0x34800000+0x200000`.
The AND broadcast aperture is `0x34c00000+0x200000`.
Both broadcasts share the local state.
The AND view is not a separate read-only status bank.
SM8850 `0x148befa4..0x148befb8` reads `0x34d3c09c`, sets bit one, and writes the AND view.

SM8850 `0x148ad3f0..0x148ad404` derives the local displacement from pair index `P`:
`((P >> 1) << 22) + ((P & 1) << 24)`.
It adds this displacement to `0x31920074 + slice*0x1000`.
The loop admits pair indices zero through three.
SM8845 contains the same operations at `0x148aba5c..0x148aba70`.
These indexed accesses establish local aliases independently of literal broadcast builders.

`platforms/qcom/sm88xx/llcc.lua` declares this topology for both SoC helpers.
Its owner key is `qcom,llcc-v6`.
Encoded local addresses use `selector*0x200000 + relative_offset`.
The declared aperture does not authorize protection changes.
Relative ranges `0x60000..0x624bf` and `0xb0000..0xb127f` belong to security controllers.
The LLCC model must not treat their accesses as ordinary retained configuration.

APSS, TME, and CPUCP each receive the same six views.
CPUCP `0x1d81a18a..0x1d81a1e4` adds the base word at `0x1d833068` to LLC offsets.
The image initializes that word to `0x03400000`, so the first store is `0x34c91000`.
That address is relative word `0x91000` in the AND-broadcast view.
The routine then accesses relative words `0x73018`, `0x7301c`, and `0x73044`.
These offsets are already declared configuration words.
Run614 stops at the first store because CPUCP had no LLCC view.

### Complete selected DCB address coverage

The native interpreter calls use base `0x34800000`.
SM8850 calls its interpreter at `0x148ac930`.
SM8845 calls its interpreter at `0x148aafac`.
Both obtain the record pointer and byte length from guest DCB halfwords `+0x98` and `+0x9a`.
Each record contains a relative word offset and its guest-owned value.

| Image | DCB file offset | Relative program offset | Byte length | Records |
| --- | --- | --- | --- | --- |
| SM8850 first | `0x42bcc` | `0xb4d8` | `0x3a8` | 117 |
| SM8850 second | `0x4fbd0` | `0xb8a0` | `0x3a8` | 117 |
| SM8845 first | `0x3fb14` | `0xb7f0` | `0x388` | 113 |
| SM8845 second | `0x4cb18` | `0xb800` | `0x388` | 113 |

Both alternatives within each image have identical address coverage.
Offsets range from `0x42090` through `0x104040`.
The common Lua helper records all 113 common offsets.
SM8850 additionally programs `0x7c064`, `0x7c118`, `0x7c11c`, and `0x7c120`.
The helper contains no vendor configuration values.
The native interpreter supplies all values, including different values between alternatives.
Unknown ordinary words retain guest writes with explicit substitute diagnostics.
Status and request words require their own state transitions.

### Geometry preservation and complete update group

SM8850 `0x148acc54..0x148acd04` copies two geometry entries.
SM8845 `0x148ab2c0..0x148ab370` contains the corresponding helper.
The source words are `0x34a0d040`, `0x34a0d050`, `0x34a0d060`, and `0x34a0d070`.
Each source group contains two words.
The destinations are LLCC `0x348640a0/b0/c0/d0`, each with a second word at `+4`.
The `b0` group preserves selected existing bits through an initial read and masked reconstruction.
The LLCC owner must preserve those writes.
The existing controller owner retains all source words.

SM8850 `0x148ac9b8..0x148acc50` and SM8845 `0x148ab034..0x148ab2bc` execute the preceding update group.
The following table lists every polled result in that group:

| Request or prerequisite | Required result |
| --- | --- |
| Initial FEAC observations | `0x34845240` and `0x348452a0` bit one clear |
| PMGR `0x3487c028` bit one | `0x3487c038/3c` match the expanded guest mask |
| SM8850 PMGR completion | `0x3487c114 & 0x3fff` clears |
| LPI `0x348a6014` pulse and `0x348a6018` bit one | `0x348a6024/6058` match the expanded guest mask |
| LPI completion | `0x348a606c & 3` clears |
| FEAC `0x34845214` bit 24 pulse | `0x34845240` bit one clears, AND-view `0x34c45240` bit zero sets |
| FEAC `0x34843908` bit zero | AND-view `0x34c43408` bit 28 sets |

The firmware expands each enabled guest-mask bit `i` into bit `2*i+1`.
SM8850 expands 32 input bits.
SM8845 expands 24 input bits.
The mask comes from guest word `0x3104acac`, not a host topology constant.

SM8850 writes the PMGR mask controls without truncation.
It also programs `0x3487c118/11c/120` and polls `0x3487c114`.
SM8845 masks `0x3487c024` to 31 bits.
It writes `0x7fffffff` to `0x3487c01c`.
Its write to `0x3487c020` preserves 29 mask bits and adds `0x60000000`.
Its PMGR high-result comparison uses only 30 bits.
It does not poll `0x3487c114`.
SM8845 compares only 16 bits of the LPI high-result word.
SM8850 compares the complete high-result word.

The update group reads LPI words `0x348a6000` and `0x348a6008` into guest storage.
The second value uses only 24 bits.
SM8850 helper `0x148aceac` later transforms and writes those stored values.
It also transforms guest PMGR data before writing `0x3487c010` and `0x3487c018`.
Those output words must preserve guest configuration.
No evidence establishes an analog calibration result or a physical frequency from these accesses.

### Slice activation and DARE region requests

SM8850 `0x148ace28..0x148acea8` and SM8845 `0x148ab494..0x148ab514` address slice `S` at `0x34800000 + S*0x1000`.
They check control `+4` bit one, preserve control bit zero, select operation four, and pulse bit zero.
They poll AND-view status `+4` for completion bit four and deactivated bit one.
They then write one to `+8`.
This is a request/completion/clear protocol, not an unconditional ready word.
The activation helper at SM8850 `0x148bf7d4` uses operation two and expects status bits four and zero.

The later native loop covers 32 guest region descriptors.
SM8850 `0x148acfcc..0x148ad5b8` and SM8845 `0x148ab638..0x148abc24` contain this loop.
It programs indexed rows at relative `0x120000 + S*0x1000`.
It also addresses request/status arrays at `0x103010 + S*8`.
The Lua helper declares all observed row fields and both array words.
Local indexed reads use the same bank state as broadcast accesses.

SM8850 `0x148ad164..0x148ad1c0` installs six key words from guest stack RAM.
Each key contains 128 encryption bits and 64 alpha bits.
The source contains four `0x60`-byte groups, each with four `0x18`-byte entries.
The destination formula is `0x34910000 + group*0x1000 + entry*0x100`.
The six destination offsets are `0x20`, `0x24`, `0x28`, `0x2c`, `0`, and `4`, in that order.
These words are not saved cache contents or ordinary configuration.

The SecLib producer at `0x148bf0cc` obtains the key bytes before this installation.
The consumer reads marker `0x34d10080 + group*0x1000 + entry*0x100`.
Zero causes installation. A nonzero marker skips installation.
The validation helper at `0x148ad5bc` requires the exact marker `0xf03`.
The current model does not manufacture this marker.

The row protocol polls AND-view `0x34d20074 + S*0x1000`.
It first waits for bits zero and one to clear.
After a pulse at OR-view `0x34920064 + S*0x1000`, it waits for both bits to set.
The alternate path waits for bit four at `0x34d03014 + S*8`.
Error paths inspect row fields and publish firmware diagnostics.
These requests initialize DARE regions, not cache directories or analog PHY state.
DARE provides DRAM authentication, replay protection, and encryption.
The descriptors contain data ranges, metadata ranges, feature controls, and key selection.
Completion requires the corresponding key, memory, and metadata effects.
The current model rejects these starts instead of substituting cache completion.

The caller at `0x14874788..0x148747a8` skips this consumer when its boot mode equals one.
The retained native checkpoint does not establish that mode value.
The APBL cold-boot log does not establish this separate DDR mode.

SM8850 `0x148bf75c` passes local ranges near `0x31923000` and `0x31903000` through pointer arguments.
The target `0x14878d10` calls range-registration function `0x14856be4`.
These pointers do not establish additional MMIO status effects.
The corresponding SM8845 routine starts at `0x148bdbfc`.

This inventory used pyelftools and Capstone on both selected OEM images.
It did not execute firmware, change signed images, or access the physical device.

## DDR subsystem and MCCC local state

`DDR_REG_DDR_SS_REGS` starts at relative `0x80000`, with extent `0x400`.
The common configuration helper preserves the existing owner at `0x3108012c`.
It retains the remaining words through two bounded windows.
SM8850 `0x148a8110` and SM8845 `0x148a6770` read `0x31080300` after the first LLCC geometry transfer.
Bit zero selects a subsequent configuration path, not an operation-complete response.

Four MCCC slave banks each have extent `0x800`.
Their bases are `0x31bc0000`, `0x32bc0000`, `0x31fc0000`, and `0x32fc0000`.
`platforms/qcom/sm88xx/ddr-mccc.lua` gives these local views independent state behind the existing broadcast owner.
It replaces the separate scalar owners at each bank's `+0x134`.
SM8850 `0x148ba29c..0x148ba35c` applies and removes bit-22 requests through each selected bank's `+0x10`.
SM8845 performs the corresponding first read at `0x148b8768`.

## Accepted rank policy and final publication

SM8850 `0x148ac218..0x148ac2d0` updates the low two rank bits at `0x34a05328`.
SM8845 uses the corresponding helper at `0x148aa890`.
The guest writes one to `0x34a05020` before it polls the resulting state.
The disabling path checks `0x34a05c0c[17:16]` and `0x34a00708[9:8]`.
The command engine now owns both observations.
The frontend applies the written policy only after it accepts the update request.

Descriptor operation `0x50` remains an independent producer of this rank state.
Policy updates preserve unrelated bits, other pairs, and DRAM mode registers.
The native fixture checks those invariants and restores the selected rank state.

SM8850 `0x148a8eb4` and SM8845 `0x148a756c` write one to `0x34a06604`.
The previous read-only classification was incorrect.
The model accepts this publication after configuration and an accepted `0x6020` update.
Its observation changes from zero to one after the write.
This ordering is the virtual contract, not a claim about physical timing.

## GEM NoC configuration after DDR initialization

The selected ICB interpreter uses records of `0x18` bytes.
Type two applies a masked 32-bit write.
The following complete programs contain the new GEM NoC controls:

| Image | Program | Records |
| --- | --- | --- |
| SM8850 | `0x14922a60` | 8 |
| SM8850 | `0x14922b30` | 2 |
| SM8850 | `0x14922b70` | 2 |
| SM8845 | `0x1491d8f8` | 10 |

Their union contains ten words in eight policy blocks.
`platforms/qcom/sm88xx/gem-noc.lua` declares those words and retains configuration within each `0x100`-byte window.
The guest supplies every policy value.
Unknown words emit substitute logs.
The windows do not replace existing protection, clock, or interconnect owners.
The model does not simulate NoC bandwidth or contention.

## Native completion and verification

SM8850 run87 and SM8845 run84 return from the original `DDR_FW_Init`.
Both report `ddr_init = 1 cold boot` and complete `sbl1_ddr_init`.
Both report 4096 MB on rank zero and zero MB on rank one.
Their first new fault was the GEM NoC write at `0x31102008`.

SM8850 run88 and SM8845 run85 pass that configuration group.
Both enter `do_ddr_training`.
Both then stop at an unmapped legacy TME import, address `0x4168f0`.
The TME PCs are `0x42c374` and `0x42c3da`, respectively.
Neither run establishes UEFI, ABL, or kernel execution.

The complete host suite passes 79 tests.
Clippy passes with warnings denied.
The LLCC guest checks bank isolation, OR/AND views, request-dependent state, completion clearing, and preserved memory.
It also checks rejection at the excluded security ranges and unsupported DARE starts.
The DDR guest checks descriptor effects and the later rank-policy transition.

## Analog calibration during fresh training

SM8845 helper `0x148ae6cc` controls one MC bank through its physical pair address.
SM8850 contains the matching helper at `0x148aff60`.
The SM8845 producer executes this sequence:

1. It sets control bit 12 at offset `0x8e7c`.
2. It enables bit one at `0x8e98`, or `0x8ea4` for the alternate selection.
3. It sets control bit zero at `0x8e7c`.
4. It pulses bit 17 at `0x6ecc` through helper `0x148ae8b8`.
5. It waits for the selected completion field.
6. It restores the control and enable fields.

Mode bit 25 at `0x8e74` selects completion bit nine at `0x8f98`.
The alternate mode waits for bit 18 at `0x70b4`.
The later loop checks busy fields at `0x6edc`.
The result consumers read three five-bit fields from `0x8fa0[28:24]`, `0x8fa4[4:0]`, and `0x8fa4[12:8]`.
SM8845 `0x148af1ac..0x148af1f8` publishes these results through the original metadata writer.

The explicit `mc_calibration` option models this request and publication sequence.
Each bank starts without a result.
A publication edge completes an enabled request only when its programming sources are available.
The ideal analog policy supplies midpoint code 16 for each five-bit result.
This value is a virtual calibration result, not a measured silicon value.
The model does not simulate electrical timing or physical convergence.

A new request invalidates the old completion and results.
A repeated high publication value does not create a new edge.
The completed results survive control cleanup.
Broadcast requests and publication affect all four independent banks.
The DDR guest checks these transitions, both status modes, independent banks, and broadcast completion.

SM8850 run96 and SM8845 run91 complete calibration in all four banks.
SM8850 then reports `Error code 84 at ClockDriver.c Line 2034`.
Its RCGR update times out before the fatal handler attempts the rejected write at `0x17960020`.
That write is a consequence, not the cause of the clock failure.
SM8845 instead reaches a four-bank poll at MC offset `0x6538`.
Neither run proves complete DDR training or kernel entry.

## Selected measurement at MC offset 0x6538

SM8845 producer `0x148b99b8` configures `0x6c3c`, enables `0x6cb8[1:0]`, and selects `0x64ac[27:24]`.
It sets request bit two at `0x64b4` after programming its mode fields.
Poll helper `0x148ba174` reads completion bit 16 at `0x6538`.
Consumer `0x148b9aa0` transfers two five-bit results into the guest's timing rows.
Cleanup helper `0x148ba18c` clears the request and selection fields.

| Field | Consumer condition | Result destination |
| --- | --- | --- |
| `0x6538[4:0]` | `0x64b4[0]` is clear | Mode-four timing field `0x40[8:4]` |
| `0x6538[12:8]` | `0x64b4[1]` is set | Mode-four timing field `0x40[20:16]` |
| `0x6538[16]` | The request completes | Four-bank wait at `0x148b0d70` |

The explicit virtual policy completes an enabled request with a nonzero selection and available programming sources.
Each selected result receives midpoint code 16.
A new request invalidates the previous completion before checking its prerequisites.
Cleanup retains the completed results.
The earlier analog operation retains independent state.
The model does not claim a measured delay or recovered electrical algorithm.

SM8850 run99 and SM8845 run92 each complete five rounds across all four banks.
Both then reject a SHRM trigger write at `0x31076064`.
The instruction PCs are `0x148adc74` and `0x148ac2e0`.
Neither run proves complete DDR training or kernel entry.
The guest regression covers missing prerequisites, result selection, restart, cleanup, bank isolation, and broadcast requests.


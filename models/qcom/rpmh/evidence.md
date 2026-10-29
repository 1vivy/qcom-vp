# Virtual RPMh resource manager

## Scope and ownership

`QcomRpmhRsc` implements banked RSC transport and shared virtual resource state.
It does not execute the physical AOP microcontroller.
It does not replace image loading, authentication, boot callbacks, UEFI, or ABL.

The selected XBL callback does not require an AOP release register model.
Do not manufacture such a register from the callback's name.
The authenticated-handoff observer supplies virtual startup admission after genuine guest authentication.
This is not proof that the original AOP instructions execute.

The RSC transport never writes guest memory.
The separate `QcomAopResourceService` owns resource publication after admission.
It reads selected records from guest-loaded memory and never embeds vendor records.
XBL still owns SRAM clock records at `0x0c3e0000+0x58`.

## Selected package evidence

The package directory is `CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES`.
The following SHA-256 values identify the inputs inspected directly:

| File | SHA-256 |
| --- | --- |
| `xbl_sc.elf` | `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a` |
| `aop.mbn` | `adaf8166f8fd78fa52e45a4e771685a3df5fc6018a01ac388e1bcd0a792a8c11` |
| `aop_devcfg.mbn` | `440e74f3c217fbe78248e2e743f21df4be4acfeb82d77b26b44a083f4cf113cc` |
| `xbl_config.elf` | `c35a368853ba57f00c9486d4b8fadeb0ded3a59ce410818a69ac415e3c44d3ef` |
| `uefi.elf` | `da8ad1b0ffc2eff91d7d34d8574a682b7007e0b052439fbbcde989d5393071f5` |

### XBL AOP boot record

The record table starts at `0x148ec328` with stride `0x88`.
The interpreter starts at `0x148291f4`.
Instructions at `0x148292d4` and `0x14829310` register the lists from record offsets `+0x38` and `+0x40`.
These are the preprocessor and postprocessor lists.

Record 12, at `0x148ec988`, has the name `AOP` and image identifier `0x21`.
Its preprocessor pointer is zero.
Its postprocessor pointer is `0x148ed698`.
That list contains one callback and then a null entry:

| Callback | Name pointer | Name |
| --- | --- | --- |
| `0x148391f4` | `0x148d5e6c` | `aop_loaded` |

The callback contains three instructions:

1. Set argument `w1` to `0x20`.
2. Set argument `w2` to one.
3. Branch to `0x1482cc20`.

This routine resolves the boot context and calls `0x1482c86c`.
That setter writes a property value and its valid flag in normal memory.
It uses a `0x18` property stride.
It does not access an AOP reset, release, ready, or status register.

The property getter at `0x1482c9bc` checks the valid flag before returning the value.
The call at `0x1484151c` reads property `0x20` in the error-handling path.
Nearby strings include `Error code %lx at %s Line %lu` and `Call Stack:`.
This is not evidence of a normal-path physical AOP release operation.

The later callback `boot_clock_init_rpm`, at `0x14839320`, resolves interface `0x0c`.
It invokes that interface's `+0x28` method at `0x14839344`.
The PMIC pre-initialization wrapper at `0x14831d44` resolves interface `0x18` and calls its `+0x90` method.
Neither wrapper alone establishes a physical AOP release/status address.
The clock method resolves to `0x14843948 -> 0x1484ec14`.
It allocates object `0x1da` and appends a clock record, not an RSC vote or ready bit.

### Selected ARC resources

The selected post-DDR FDT starts at file offset `0x10e14` in `xbl_config.elf`.
Its size is `0x31158`.
Its ten `qcom,rpmh-arc-regulator` nodes all select `qcom,drv-id = 2`.
Each supplies a `qcom,resource-name`.

The selected AOP has static command-database records at file offset `0x16a68`.
Each level record and its timer record occupy `0x30` bytes together.
The level record stores the resource name at `+0` and the command address at `+0x10`.
The selected post-DDR names resolve as follows:

| FDT node | Resource | Command address |
| --- | --- | --- |
| `/soc/vdd_mxa` | `mx.lvl` | `0x30010` |
| `/soc/vdd_mxc` | `mxc.lvl` | `0x300a0` |
| `/soc/vdd_cx` | `cx.lvl` | `0x30000` |
| `/soc/vdd_mm` | `mmcx.lvl` | `0x30080` |
| `/soc/vdd_gx` | `gfx.lvl` | `0x30050` |
| `/soc/vdd_lpi_mx` | `lmx.lvl` | `0x30040` |
| `/soc/vdd_lpi_cx` | `lcx.lvl` | `0x30030` |
| `/soc/vdd_nsp` | `nsp.lvl` | `0x30090` |
| `/soc/vdd_nsp2` | `nsp2.lvl` | `0x300c0` |
| `/soc/vdd_gmxc` | `gmxc.lvl` | `0x300d0` |

These ten resources describe the original post-DDR consumer, not the complete AOP inventory.
The helpers now include all fifteen selected ARC levels and the selected BCM resources.
They do not enable timer records or arbitrary addresses between those records.

## Exact selected UEFI transport

The selected `uefi.elf` contains an LZMA stream at file offset `0x55268`.
It expands to `4685832` bytes.
The `RpmhDxe` PE payload starts at expanded offset `0x3258bc` and has size `0x10000`.
Its SHA-256 is `98b6b3891fe7eb77405887a4c9d478b8f11d9eb8d46b4205f3f0a023e2c1ccb9`.
The addresses below are PE RVAs, not XBL addresses.

| Evidence | Contract |
| --- | --- |
| `0xe268` | Software DRV2 selects hardware DRV2, with controller base `0x18900000`. |
| `0xe218` | Six TCSs, sixteen commands, and polling configuration with both IRQ identifiers zero. |
| `0xb6e8` | Three active-message TCSs, sleep starts at three, and wake starts at five. |
| `0x6624..0x662c` | Configuration word uses bits 12..17 for the DRV2 TCS count. |
| `0x6684..0x6694` | DRV stride is `0x10000`. Configuration word `+0xc` uses bits 27..31 for command count. |
| `0x6c60..0x6c8c` | TCS stride is `0x2a0`. Control at `+0xd24` uses AMC bit 16 and trigger bit 24. |
| `0x6da8..0x6dc4` | TCS idle status at `+0xd28`, bit zero. |
| `0x7834..0x790c` | Command stride is `0x18`. Message, address, and data start at `+0xd34`, `+0xd38`, and `+0xd3c`. |
| `0x769c..0x76cc` | Command response data is at command-relative `+0x10`. |

The DRV2 bank starts at `0x18920000`.
The controller also owns DRV0 at `0x18900000` and the shared sequencer registers.
Each DRV retains separate command, interrupt, and TCS state.

### Later UEFI evidence is separate

The supporting qcom-rehost tree has another `RpmhDxe.efi`.
Its SHA-256 is `ae1368dba518193c6f987e4f3057917cc099b6ad37ad8d243c556a2b2f49d092`.
Its source inventory identifies the `16.0.10.601` UEFI cohort, not this selected package.
Direct comparison found all differences in PE offsets `0xe950..0xea28`.
The HAL code and configuration ranges cited above match the selected module exactly.
The selected module, rather than the Python substitute, establishes this model's transport contract.
The Python substitute's unrestricted resource writes and unconditional extra status register are not adopted.

## Virtual arbitration and power policy

APSS DRV0, APSS DRV2, and TME use separate requester indices in one resource owner.
Each accepted ARC command replaces only its requester's vote.
The effective ARC state is the maximum of the present votes.
A weaker vote cannot erase a stronger vote from another requester.
A present zero vote differs from an absent vote.
IRQ clearing and TCS reuse preserve votes.

BCM commands carry valid bit 29, commit bit 30, X bits 27:14, and Y bits 13:0.
The model retains pending BCM votes per requester.
A commit publishes that requester's pending group.
The virtual aggregate uses independent maxima for X and Y across valid votes.
A committed invalid vote withdraws that requester's demand.
The model does not claim physical bandwidth, frequency, or cross-DRV silicon arbitration.

Repeated commands execute in slot order.
No implicit voltage floor, thermal clamp, dependency vote, or settling delay exists.
A resource without a published vote has no reported state.

An active-message trigger validates the entire enabled batch first.
Only eight-byte write commands, with an optional completion request, are executable.
A valid batch updates accepted or pending resource state before it publishes completion.
A pending BCM vote does not become effective before its commit.
There is no physical rail effect beyond the virtual resource state.
No physical clock or rail model may treat this acknowledgement as an observed frequency or voltage.
The `resource_state()` C++ API exposes accepted virtual state without inventing a guest hardware register.

The following requests fail with a TLM error and do not publish a new completion:

- An unknown resource or register.
- A read command or a response-data read without an implemented resource backend.
- An empty trigger, a sleep/wake trigger, or a trigger without AMC mode.
- An unsupported message length or register bit.
- An unaligned access, a non-word access, or an access with byte enables.

Unknown accesses retain the normal router/TLM diagnostic path.
The idle bit means that no synchronous transaction remains outstanding.
It is not an AOP ready bit and does not acknowledge an unaccepted command.

## Registration and integration

Register `qcom,rpmh-rsc-v1` with class `qcom_vp::QcomRpmhRsc` from `models/qcom/rpmh/rsc.h`.
The constructor takes `sc_core::sc_module_name` only.
The root instance owns up to 64 explicit resources.
The `resources` parameter lists selected ARC or BCM addresses.
Elaboration rejects duplicate addresses and unsupported vote formats.

The `banks` parameter contains rows of `DRV index`, `TCS count`, and `active count`.
An optional fourth field names the requester index. Otherwise the index is `requester_base` plus the DRV index.
SM8850 APSS declares `0 4 2`, `1 4 2 4`, `2 6 3`, and `3 3 1 5`. SM8845 APSS declares `0 4 2` and `2 6 3`.
The separate TME controller declares `0 6 3`.
Its `resource_owner_path` selects the APSS resource owner, and its `requester_base` is three.
The display controller uses requester one.
Up to eight requesters share one resource owner.
Elaboration rejects duplicate requester indices and indirect owner chains.

### CPUCP DRVs

The CPUCP table at `0x1d82edc0` has `0x24`-byte entries.
Entries one and three select bases `0x18910000` and `0x18930000`.
Their bytes 3 and 4 select the TCS pair that function `0x1d80d612` programs: two and three for DRV1, one and two for DRV3.
Run610 stops at the first CPUCP write, TCS zero of DRV3 at `0x18930d3c`.
[INFERENCE] TCS indices below the selected pair are active TCSs, and the pair is the sleep and wake storage.
The SM8850 composition therefore declares DRV1 with four TCSs, two active, and DRV3 with three TCSs, one active.
Display and TME already hold requesters one and three, so the CPUCP DRVs use requesters four and five.
`cpucp_router` reaches both DRV windows through aliases to the same APSS controller.
SM8845 keeps two DRVs until its CPUCP table is checked.

The `sequencer_words` parameter declares retained instruction capacity in 32-bit words.
APSS declares 64 words. TME declares none.
This storage does not execute an autonomous power sequence.

Enable the board group with:

```lua
p = require('sm8850-rpmh')(p)
```

The helper requires `p.apss_router`.
It creates `p.rpmh_rsc` at `0x18900000..0x1892ffff`.
It rejects overlapping APSS targets and duplicate installation.
DRV1 has no declared TCS partition.
The helper leaves guest-owned SRAM unchanged.

## QSEE transport and sequencer evidence

The selected SM8850 `tz.img` SHA-256 is `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932`.
The selected SM8845 `tz.img` SHA-256 is `b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09`.

| Contract | SM8850 evidence | SM8845 evidence |
| --- | --- | --- |
| RPMh initializer | `0x15740314` | `0x15732f78` |
| DRV0 configuration read | `0x15741c84` | `0x15735494` |
| Software descriptor | `0x157f35b0` | `0x157e6bf8` |
| Partition | Two active TCSs, sleep TCS2, wake TCS3 | Same selected partition |
| Command geometry | Sixteen commands per TCS | Same selected geometry |

Both software descriptors use zero CPU interrupt identifiers.
Submission still enables the TCS interrupt and polls completion.
The model retains those enables and supplies level outputs from enabled pending status.
W1C clears completion without deleting resource votes.

Both helpers connect DRV0 and DRV2 to SPIs 3 and 5.
The [Canoe device tree](https://raw.githubusercontent.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/kernel_platform/qcom/opensource/devicetree/qcom/canoe.dtsi) supplies the SM8850 routing.
The selected SM8845 `vendor_boot.img` supplies the same routing at file offset `0x104d26c`.
Its SHA-256 is `1736dde58f5d28a4d31cfeefd7e97316fc0cfd48ccfce87b21eab9a5d4ef2d72`.
Its first DTB starts at `0x1034000`.
This evidence does not identify ABL's eventual DTB selection.

QSEE initializes controller word `+0xd8` before it waits for the genuine AOP publication cookie.
RSC idle and completion do not create that cookie.
The first concrete QSEE clock vote requests `xo.lvl`, address `0x300e0`, with payload three.
SM8850 instructions `0x157505c4..0x157505fc` submit and wait for this vote.

The selected AOP tables contain fifteen ARC levels at `0x30000..0x300e0`, with stride `0x10`.
SM8850 contains 32 BCM resources at `0x50000..0x5007c`, with stride four.
SM8845 contains the same set except `0x50054` and `0x50064`.
SM8850 ARC and BCM source tables start at `0x0b100a68` and `0x0b100510`.
SM8845 source tables start at `0x0b100660` and `0x0b100ba8`.
The database publisher still copies and checks the genuine guest-loaded records.

SM8850 initializer `0x157439b0` uploads 50 halfword instructions to controller offsets `0x550..0x5b0`.
It writes branch addresses `0x1a`, `0x36`, `0x44`, and `0x60` at `0x4bc..0x4c8`.
It selects timer owner two through the low bits of `+0x1c`.
The model retains these instructions, branches, and owner selection.
It also retains the deferred timestamp handoff at `+0x4510/+0x4514`.
These writes do not create a capture event or execute a power sequence.

Controller `+8` reports DRV count in bits 26:24 and instruction words in bits 23:16.
The remaining fields report timestamp events and delay-counter width, not branch count or instruction width.
The virtual controller declares three DRV positions, 64 instruction words, no timestamp events, and no autonomous solver.
Its `+0xc` word consistently reports four DRV0 TCSs, six DRV2 TCSs, and sixteen commands.
These are explicit virtual dimensions, not measured silicon dimensions.

### Verification

A direct SystemC probe exercised the real model through its TLM socket.
It checked shared ARC maxima, lowering, zero versus absent votes, BCM commit groups, withdrawal, and atomic rejection.
It also checked IRQ masking, pending-state preservation, and W1C deassertion.
The probe reported `shared_arc=4 bcm_withdrawn=0 atomic_rejection=pass irq_masking=pass`.

SM8850 run175 and SM8845 run139 pass DRV0 initialization and next reach AOP publication discovery.
Both find a zero discovery offset and fail on the resulting read at `0x0c300000`.
Neither log contains an image-admission marker.
The native RSC regression passes completion, reuse, bank isolation, W1C, and rejected commands.
SM8850 run179 subsequently admits AOP image `0x21` at sequence 41 through genuine opcode `0x10` completion.
The original TME consumes the resulting command database.
Run180 passes the CX/MX level writes and reaches RSC interrupt control `0x00c00218`.
SM8845 run142 admits AOP at sequence 144 and PDP at sequence 238 through the streaming authentication path.
SM8850 run181 preserves both admissions and passes the RSC control access.
Both next stop at missing TME runtime import `0x968`, not at AOP discovery.
These observations do not establish kernel execution.

## Authenticated virtual AOP resource startup

`QcomAopResourceService` implements `ImageHandoffSink` for image ID `0x21`.
Its `observer_path` must resolve to the shared `ImageHandoffSource`.
Only the observer owns admission. The resource service cannot report authentication success.
The observer requires genuine metadata, segment authentication, successful finalization, and guest consumption of that completion.
The service does not change the original mailbox response when publication fails.

The board must select `startup_policy='synchronous-virtual'`.
This explicitly replaces the recovered resource startup task, not the AOP CPU or physical release wire.
SM8845 startup `0x0b080f24` calls DDR initialization and creates task `0x2007aa`.
That task calls initialization `0x81c1eb3c` at `0x0b0807ca`.
SM8850 startup `0x0b080f52` creates task `0x2007c8`.
Its task calls initialization `0x81c1ed7c` at `0x0b0807e8`.
Cookie callers are `0x81c1f1c0` and `0x81c1f66e`, respectively.
The positive-cookie stores are `0x0b09082e` and `0x0b090954`.
They write `0xa0c00c1e` to AOP-local `0x053f0040`.
RSC idle or vote completion never starts this service.

### Selected SM8845 evidence

| Input | SHA-256 |
| --- | --- |
| `xbl_sc.elf` | `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4` |
| `xbl_config.elf` | `2effcfac7784d044a34dbff04c0da063c17806083707af285952b75964d8376c` |
| `aop.mbn` | `1c56eeb2fff24846dd923e1594689b07ce2c5c8539d56ffb7608073940c6daff` |
| `aop_devcfg.mbn` | `6aa335cac21a9291b3f7aa095450c41847f92066fd16a4cde6b095f025859dd0` |
| `uefi.elf` | `13722b7ed849359e7afc731dfdc1eb0c1fc1815907d708f4b94cadb7e03be4f4` |

SM8845 post-DDR FDT starts at file `0xf52c`, size `0x2dc00`.
It selects exactly nine ARC resources from the table above, excluding `gmxc.lvl` at `0x300d0`.
`sm8845-rpmh.lua` enables those nine addresses. `sm8850-rpmh.lua` retains all ten.
SM8845 RpmhDxe expands from UEFI file `0x54268`, at expanded offset `0x2f87d4`, size `0x10000`.
Its SHA-256 is `ef5b911d2a888042993cfec83bf28ef4fabea983277f03df3d293f7fe2e685dc`.
Cross-SoC differences lie only in PE `0xe950..0xea27`, in build/debug data.
The transport instructions and configuration match. No RSC model change is required.

SM8845 canonical AOP record is `0x148ea948`, with name pointer `0x148d3b48` and ID `0x21` at `+8`.
Its preprocessor and postprocessor pointers at `+0x38/+0x40` are both zero.
SM8850 canonical AOP record remains `0x148ec988`, as documented above.
Record names are at `+0x78`, with stride `0x88`.
The addresses `0x148ec978/0x148eca00` are preceding name fields, not record starts.

### Guest-loaded selected inputs

All numeric row fields in the `tables` CCI string use hexadecimal.
Each row supplies slave, version, count, records address, records hash, auxiliary address, auxiliary size, and auxiliary hash.
The copied groups register slaves `3,3,5`, each with version `0x1000`.
Their counts remain `30,15,30` for SM8845 and `30,15,32` for SM8850.
The fourth row derives slave-four records from configuration sources, as described below.

| SoC | Input | APSS address | Size | SHA-256 |
| --- | --- | --- | --- | --- |
| SM8845 | ARC records | `0x0b100660` | `0x2d0` | `2e1e9729f2dc736165cb557a3ebc0f2d1639212dd0298e9122c5c065110ff30c` |
| SM8845 | ARC auxiliary | `0x0b100408` | `0x258` | `65e4690cf7443f149bd471903d78d0e6e4e5b98c804c6fda848df0ecee5da437` |
| SM8845 | second slave3 records | `0x0b100950` | `0x168` | `851b3d628f3be9ff091b4b22f0cc140a38d9d1b668ae8940f29ccbbc81a07f5b` |
| SM8845 | second slave3 auxiliary | `0x0b100930` | `0x1e` | `6c02f96d361d9f7382aa057e6fd48aba3733d7eeb147d71319049058b135a6b6` |
| SM8845 | slave5 records | `0x0b100ba8` | `0x2d0` | `2af8848d70d3379930b51e3448d63238b8151dece0e39fe7105693a148928b7a` |
| SM8845 | slave5 auxiliary | `0x0b100ab8` | `0xf0` | `f82c5c9dcc988e34128a10feca24f754fc0a3640e025f968e759868bdce4605b` |
| SM8850 | ARC records | `0x0b100a68` | `0x2d0` | `e8e030d741cb112f7bdadf4a520d854aceebc80ac8477aea4180aac8cec486f3` |
| SM8850 | ARC auxiliary | `0x0b100810` | `0x258` | `7625fbe2a6cbbe270b9c56ec3d5d6905133baa0b27c5290807710a62f2086921` |
| SM8850 | second slave3 records | `0x0b100d58` | `0x168` | `851b3d628f3be9ff091b4b22f0cc140a38d9d1b668ae8940f29ccbbc81a07f5b` |
| SM8850 | second slave3 auxiliary | `0x0b100d38` | `0x1e` | `3736b70b9e7dc5c9b58fb7a8dc011772c64e0cc2a0a9d15685f37e9415d70c76` |
| SM8850 | slave5 records | `0x0b100510` | `0x300` | `9564042bbe1bb913a6b9c7b80626c115df8b64dd488d354d13f491e432ec9458` |
| SM8850 | slave5 auxiliary | `0x0b100410` | `0x100` | `adc8f6184012cf6526e1a099cd2f14be5ddffafa1eda460481027ad5f75f73df` |

The publisher reads these six spans through `memory_socket`, then checks SHA-256.
It never opens a host firmware file.
A source mismatch leaves the cold pointer and cookie unpublished.
Invalid selected configuration or SHA-256 context initialization failure stops elaboration before subscription.

### Actual database writer and consumer

Both producers clear `0x81c60000+0x2000` at `0x81c1e39a`.
They store version `1`, magic `0x0c0330db`, and initialize the data cursor to database `+0x90`.
Direct disassembly of SM8845 `0x81c23ce8..0x81c23dca` confirms the registration algorithm.
SM8850 `0x81c2405e..0x81c24140` has the same algorithm.
The scan at SM8845 `0x81c23d50..0x81c23d5a` tests only whether each slave field is zero.
It does not merge or reject an existing matching slave.
Therefore the second slave3 input occupies its own directory slot, after ARC.

Each directory occupies `0x10` bytes, with at most eight entries.
Relative to database `+i*0x10`, fields are u16 slave `+8`, record offset `+0xa`, auxiliary offset `+0xc`, count `+0xe`, version `+0x10`.
Record and auxiliary offsets are relative to database `+0x90`.
Records occupy `count*0x18` bytes. Auxiliary bytes immediately follow that array.
The writer aligns each group's allocation to eight bytes, not sixteen.
All four selected groups fit the `0x2000` output backing.
The length also depends on discovered PMICs and rails.
The focused cases below produce `0x14c0` for SM8845 and `0x16a0` for SM8850.
These values follow from copied and derived extents. They are not fixed service constants.

CmdDbDxe reads the APSS pointer at `0x0c3f000c`, RVA `0x4830..0x4844`.
RVA `0x254c..0x25ac` requires a nonnull pointer, version `1`, and magic `0x0c0330db`.
RVA `0x1f38..0x2140` searches up to eight directory slots and the actual `0x18`-byte records.
The record has name/u64 `+0`, priority words `+8/+0xc`, address/u32 `+0x10`, auxiliary length/u16 `+0x14`, and auxiliary offset/u16 `+0x16`.
SM8845 selected CmdDbDxe SHA-256 is `0c54424eaa6588ef1f0cb22c901f942bdab63d190993f1c4159fd1fb5b422783`.
SM8850 selected CmdDbDxe SHA-256 is `b163aa8b210ab1dcb4009d6630fe8adb6dcd96daa8b7828027bdf93900ba204f`.
Their consumer code matches.

### Publication, backing identity, and failure

The service starts dormant and performs no constructor memory writes.
After admission it validates all inputs before writing any output.
It clears the header and writes the complete database once, including zero gaps and tail.
It publishes only supported resource exports.
It writes header `+0=0xf0040`, computed length at `+0x10`, then database pointer at `+0xc`.
The resource-ready cookie at `+0x40` is the last write.
Unsupported log and other AOP exports remain zero.
Failed output writes trigger best-effort pointer, length, and cookie invalidation.
A permanently broken memory backend cannot guarantee rollback, and the service never reports publication success in that case.
One admission is terminal for one construction. Duplicate events never repeat publication.
New generations do not invent a warm-reset lifecycle.

`aop-configuration.lua` supplies one cold `0x400` backing at APSS `0x0c3f0000`.
Its TME alias at `0x103f0000` and CPUCP alias at `0x0c3f0000` route to that same APSS object.
All views reject overlaps before installation.
CPUCP `0x1d80d4b6` reads the header, polls the cookie without a timeout, and then reads the database pointer at `+0xc`.
Run609 stopped at its first header read because CPUCP had no view. Run610 passes the readiness poll.
Selected export code at SM8845 `0x14685b52..0x14685b96` and SM8850 `0x14685bd2..0x14685c16` adds `0x07000000` to eligible local addresses.
That rule maps AOP-local `0x053f0000` to APSS `0x0c3f0000`.
No AOP-local router exists in this service policy, so no additional local mapping is created.

Both selected ELF program tables specify PT_LOAD destinations `0x0b080000+0x18000` and `0x0b100000+0xc000`.
Their code file spans are SM8845 `0x1000+0x13bfc` and SM8850 `0x1000+0x14034`.
Their data file spans are SM8845 `0x15000+0xbd20` and SM8850 `0x16000+0xbcc0`.
The helper maps only those evidenced non-DDR/non-IMEM memory extents.
It creates coherent UFS, sequencer DMA, and TME aliases to their APSS backing objects.
The TME windows start at `0x0f080000` and `0x0f100000`.
Each new alias maps to `region_address-backing_base`.
Existing aliases must preserve that same backing-relative offset.
SM8850 run67 exposed the missing TME view during a byte copy from `0x0f08d000`.
Run68 completes that copy and reaches the DDR reference handshake.
The installed `addrtr.h` implementation forwards `mapped_base_addr+(address-target_socket.address)`.
The remaining PT_LOAD extents are `0x81c00000+0x16000`, `0x81c16000+0x8000`, `0x81c1e000+0x42000`, `0x14685000+0x5000`, and `0x1468a000+0x1000`.
Existing coherent DDR and IMEM must cover these destinations.
The helper preserves those objects and reuses DDR for the database.
Partial overlap, duplicate mappings, and conflicting DMA backing identities are composition errors.
No selected vendor bytes enter the repository.

### Integration and verification

Both runtime profiles compose the observer and `aop-resource-service.lua` after their shared header and memory backings.
Both profiles compose the shared ARC and BCM inventory described above.

The real observer and publisher ran with an AArch64 guest.
The guest supplied synthetic protocol responses and hash-checked data from each selected private AOP image.
This test does not establish genuine image authentication.
Both profiles published version one, magic `0x0c0330db`, and separate directory entries for both slave-three groups.
The computed lengths were `0xb00` for SM8845 and `0xb40` for SM8850.
The guest resolved `gfx.lvl` to resource address `0x30050`.

Eight invalid protocol cases left the resource pointer, cookie, and database version zero.
These cases covered failed metadata, failed segments, byte-zero finalization, wrong context, wrong sequence, missing release, stale response bytes, and repeated publication.
A changed source byte permitted the synthetic admission event but failed resource publication.
That failure preserved the original successful completion word and left the resource pointer, cookie, and database version zero.

SM8850 run142 and SM8845 run117 complete the original AOP image-loading sequence.
The selected CmdDbDxe consumer remains unobserved.
Unlisted resources, physical resource reads, interrupt delivery, and triggered sleep/wake execution remain unsupported.
Native kernel logs remain the end-to-end boot criterion.

## Original TME command-database consumer

The original TME consumes the published database before UEFI.
Its cold loop requires both the ready predicate and image `0x21` state `5`.
Legacy import `0x3c0` selects donor routine `0x2311ea`, which checks the real published cookie at TME `0x103f0040`.
The service does not seed that cookie before original image admission.

| Operation | SM8850 PC | SM8845 PC |
| --- | --- | --- |
| Ready gate | `0x420410` | `0x420490` |
| Database initialization call | `0x427ae8` | `0x427a24` |
| Database initializer | `0x428102` | `0x428052` |
| Published pointer read | `0x42840c` | `0x42835c` |
| Resource initialization | `0x442242` | `0x4423b0` |
| First RSC idle read | `0x428436` | `0x428386` |
| RSC programming | `0x428650` | `0x4285a0` |

The initializer reads pointer `0x81c60000` through the shared TME header at `0x103f000c`.
It requires database version one and magic `0x0c0330db`.
The original resource initializer resolves four names from the existing slave-five records.

| Resource | Command address |
| --- | --- |
| `MC0` | `0x50000` |
| `SH0` | `0x50004` |
| `SN0` | `0x50010` |
| `CN0` | `0x50030` |

Each initial vote contains `0x60000001`.
Both selected AOP images supply these records.
All twelve configured source hashes match the corresponding original AOP bytes.
No publication profile changes are necessary for this consumer.

The TME uses local RSC base `0x02210000`, TCS1.
Its relative register layout matches the existing `QcomRpmhRsc` model.
`aop-resource-service.lua` installs a separate controller with shared resource ownership.
It preserves the independent TME staging and interrupt state.
The local configuration declares virtual DRV zero, while its shared requester index is three.

| Field | TME address |
| --- | --- |
| TCS control | `0x02210fc4` |
| Idle status | `0x02210fc8` |
| Command enable | `0x02210fcc` |
| Slot-zero message | `0x02210fd4` |
| Slot-zero address | `0x02210fd8` |
| Slot-zero data | `0x02210fdc` |

Commands use stride `0x18`, message `0x10108`, and enable mask `0xf`.
Control bits 16 and 24 request AMC execution.
The native routine polls idle bit zero, then clears the enable mask.
The model applies the complete accepted batch before completion.
Its idle indication describes transport state, not AOP readiness or authentication.
BCM payloads remain retained virtual votes, not claims about physical bandwidth or frequency.

The original TME derives packed voltage-level indices from command-database records `cx.lvl` and `mx.lvl`.
SM8850 stores them at `0x022370a8` and `0x022370ac`, from instructions `0x435e06` and `0x435ed8`.
SM8845 uses instructions `0x435e5e` and `0x435f30`.
The shared composition retains these computed values in two independent control words.
The virtual supply policy does not simulate voltage changes or alter authentication results.

The TME also uses interrupt completion for RSC TCS1.
SM8850 routine `0x428410` enables interrupt bit 1 and suspends its scheduler context while the batch executes.
Initializer `0x4284e0` registers handler `0x428538` on CLIC source 24.
That handler clears bit 1 at `0x02210d08` and resumes the waiting context through the original callbacks.
The composition connects the existing RSC completion output to that source.

SM8850 instructions `0x428524` and `0x4286fc` write `1` to `0x00c00218` and `3` to `0x00c00214`.
The composition retains both words independently.
The virtual platform remains awake and does not simulate a physical power collapse.
The exact hardware meaning of these outer words remains unqualified.
Actual RSC command completion and its interrupt mask control the interrupt, not these retained values.

## AOP hardware lifecycle

`aop-lifecycle.lua` composes the hardware consumed by the original AOP callbacks.
The callbacks remain in the selected firmware.
The models do not replace their admission, error, or completion paths.

| Contract | Selected evidence | Functional behavior |
| --- | --- | --- |
| AOP reset | Donor `0x222ff4`, selector one | The word at TME `0x102f3000` retains the original reset request |
| Transfer controller | Original callback group rooted at `0x422b2e` | Requests at `+0x404` and `+0x414` advance separate transfer phases |
| PLL update | SM8850 `0x4232ec`, SM8845 `0x4232a2` | MODE latch bit 14 controls acknowledgment bit 13 |
| Request pairs | Original resource records at `0x44688c` | A read-only acknowledgment mirrors the retained request |
| Halt channels | The same four resource records | Request bit zero and the upstream clock determine clock availability |

The transfer controller starts with status zero.
The first phase publishes `0x80000000`.
The second phase publishes one only after the first phase.
Software cannot write either status directly.
These phases describe hardware requests, not authenticated image admission.

The halt channels report no unsolicited activity or denied request under the virtual policy.
Their connected branches still depend on actual request state.
The three root generators use an explicit virtual reference for their selected source.
This reference does not establish a physical mux assignment or measured frequency.
APSS aliases reach the same backing as each TME register.

The native fixture checks ordering, separate instances, upstream clock loss, retained writes, and rejected status writes.
It also checks invalid offsets and transaction widths.
SM8850 run141 records one from the original selector gate at `0x437256`.
That gate executes the genuine reset callback before it returns.
The next fault belongs to separate image-policy initialization at `0x04d42000`.
This observation does not prove later AOP resource publication or kernel execution.

## Native display RSC register version

`display_rsc.h` implements the register version consumed by the native DCP firmware.
It uses the existing RPMh vote backend.
It does not replace the APSS register version.

The selected packages contain `RADIO/dcp.img`.
SM8850 uses CPH2749 export `11_16.0.5.700EX01_2026022701130186`.
Its SHA-256 is `5863d9b9826426b507b194f7461c781ab7fe9a41d10063d410db20eee5cdd551`.
SM8845 uses CPH2771 export `11_16.0.1.305EX01_2025121000260242`.
Its SHA-256 is `2556992b799d102bad74235025e675c0620e957719fe18f92f14fce5789eb766`.

Both embedded device trees set the DRV base to DCP `0x103c2000`.
They specify three TCS groups and twelve commands per group.
The TCS map assigns active, sleep, and wake to indices zero, one, and two.
Interrupt source 32 receives the active completion.

| Register | SM8850 instruction | SM8845 instruction | Contract |
| --- | --- | --- | --- |
| `+0x8` | `0xa00eb91a` | `0xa4117ac4` | Bits 23:16 give instruction capacity in words |
| `+0x370+4*i` | `0xa00eb936` | `0xa4117ae0` | The driver copies eleven sequence words |
| `+0x124+32*i` | `0xa00eb8e4` | `0xa4117a8e` | Bit zero enables timestamp channel `i` |
| `+0x12c+32*i` | `0xa00eb948` | `0xa4117af2` | The driver reads the upper timestamp word |
| `+0x220` | `0xa00eb8f6`, `0xa00eb908` | `0xa4117aa0`, `0xa4117ab2` | Separate updates retain the start address and bit 31 |
| `+0x2dc+4*i` | `0xa00eb912` | `0xa4117abc` | Branch addresses use byte indices |
| `+0x48` | `0xa00eb958` | `0xa4117b02` | The driver programs the PDC start address with bit 31 |
| `+0x40`, `+0x38` | `0xa00eb962`, `0xa00eb964` | `0xa4117b0c`, `0xa4117b0e` | The driver programs a two-word match value |
| `+0x110`, `+0x11c` | `0xa00eb762`, `0xa00eb772` | `0xa411790c`, `0xa411791c` | Initialization retains one and `0x9600` |
| `+0x520`, `+0x524`, `+0x528` | `0xa00e8b94`, `0xa00e90a0`, `0xa00e9254` | `0xa4114ca6`, `0xa411525a`, `0xa411540e` | Active interrupt enable, status, and clear |
| `+0x540+0x150*i` | `0xa00e95b8` | `0xa4115772` | Wait mask |
| `+0x544+0x150*i` | `0xa00e95f8` | `0xa41157b2` | The rising trigger submits an active batch |
| `+0x548+0x150*i` | `0xa00e94f6` | `0xa41156b0` | Bit zero reports an idle TCS |
| `+0x54c+0x150*i` | `0xa00e95c0` | `0xa411577a` | Enabled command slots |
| `+0x554+0x150*i+0x18*j` | `0xa00e954c` | `0xa4115706` | Command message |
| `+0x558+0x150*i+0x18*j` | `0xa00e955c` | `0xa4115716` | Resource address |
| `+0x55c+0x150*i+0x18*j` | `0xa00e9578` | `0xa4115732` | Vote value |

The virtual sequence memory has 64 words.
This capacity describes allocated storage, not sequencer readiness or a measured hardware limit.
The model retains the programmed sequence, branch addresses, match values, and override.
An active trigger uses the shared backend to validate and publish the complete batch.
Only successful publication produces command completion and its masked interrupt.

Timestamp status starts at zero.
Sequence programming and override writes do not create timestamps.
The `timestamp_event(channel, ticks)` API records an external event only when the channel is enabled.
No current platform component calls this API.
The native fixture supplies an event directly and checks its consumer-visible timestamp.
It also checks shared votes, interrupt assertion, interrupt clearing, and rejected APSS-only offsets.

UEFI DisplayDxe also reads this RSC at system `0x09bc22d0`.
Its function at RVA `0x321f4` reads `+0x2d0` and returns one when bit 12 is clear.
Offset `+0x2d0` is `RSC_SEQ_PWR_CTRL_STATUS_DRV0`, with a reset value of zero.
Run754 stopped at that read.
The model runs no sequence, so the power-control outputs keep the reset value, and the word reads zero.
A write to this word fails.

The instruction-to-signal map remains unqualified.
The model does not execute the opaque sequence or claim a completed display collapse.
Sleep and wake TCS groups retain their commands but do not execute on configuration writes.
This RSC model has no WFI input.
Do not treat an unqualified WFI as a completed hardware sequence.
The domain transition must complete before CFG_GDSCR reports POWER_DOWN_COMPLETE.


## Native display PDC and CESTA storage

`display_pdc.h` implements the display DRV aperture at system `0x0b2a0000`.
It does not change the APSS PDC owner or its four DRVs.
The native DCP remapper exposes this aperture at `0x4b2a0000`.

The PDC IRQ table contains twenty records in both images.
SM8850 getter `0xa00ebb32` supplies the table at `0xa0217fc8` and count at `0xa012c1bc`.
SM8845 getter `0xa4117cdc` supplies the table at `0xa4242d24` and count at `0xa415c054`.
The fourth record selects type four, IRQ zero, and enable one.
Its original access is `+0x11c` at SM8850 `0xa00ebf14` or SM8845 `0xa41180be`.
The model retains twenty words at `+0x110+4*i`.
Configuration does not create an event, timestamp, or interrupt.

The capability word at `+0x1004` has a different consumer from the capture capability:

| Field | Original consumer | Meaning |
| --- | --- | --- |
| Bits 11:8 | SM8850 `0xa00eb98a`, called by `0xa00e8136` | PDC TCS count |
| Bits 7:5 | SM8850 `0xa00eb9ec` | Command capacity per TCS |
| Bits 15:12 | Existing APSS capture contract | Capture capability |

The diagnostic string at `0xa013598b` names the consumer `rpmh_pdc_init`.
It reads the enabled command mask at DCP `0x4b2a5000+0xc8*i`.
It searches resource addresses at DCP `0x4b5f9810+0xc8*i+0x10*j`.
The adjacent word gives the low thirteen bits of the vote.
These two apertures therefore describe the same PDC TCS contract.
They are not independent anonymous storage banks.

The `command_table` parameter gives explicit virtual topology.
Each row contains the TCS index, command index, resource address, and initial vote.
The capability fields and enable masks derive from these records.
The command aperture is read-only.
An empty table declares zero TCS groups.
No selected evidence yet supplies the boot-programmed PDC resource and vote records.
The platform must not invent them to satisfy a lookup.
The model has no capture producer and advertises no capture capability.
Its diagnostic capture words remain zero.

`cesta.h` (`qcom,cesta-v1`) implements the five CESTA banks with one owner.
Platform data lists the register destinations for each bank. Other offsets fail.
The display instance uses the layout below from the selected device trees:

| Socket | System base | Size |
| --- | --- | --- |
| `crmb_socket` | `0x09bcc000` | `0x400` |
| `crmb_pt_socket` | `0x09bcc400` | `0x400` |
| `crmc_socket` | `0x09bcc800` | `0x2000` |
| `crmv_socket` | `0x09bce800` | `0x600` |
| `common_socket` | `0x09bcee00` | `0x200` |

SM8850 target descriptor `0xa021659c` supplies 169 register records at `0xa012b254`.
SM8845 packed descriptor `0xa424133c` supplies the corresponding records at `0xa415b100`.
The write instructions are `0xa00dd7ae` and `0xa4109a26`.
The records select 130 CRMC writes, ten CRMV writes, thirteen CRMB writes, fifteen CRMB_PT writes, and one common write.
The model accepts only the decoded destinations.
It also retains the later CRMC control offsets `0x28`, `0xdc`, and `0x268`.
Their runtime completion semantics remain unqualified.

The original code copies 98 instruction words to CRMC `+0x56c..+0x6f0`.
It copies 27 branch words to CRMC `+0x4ec..+0x554`.
SM8850 uses write instructions `0xa00dd738` and `0xa00dd75a`.
SM8845 uses `0xa41099aa` and `0xa41099d0`.
These loops use counts, not completion polls.
Writes retain the program but do not execute it.
No register write creates an inferred acknowledgment, vote, or GDSC transition.

### Camera CESTA from UEFI ClockDxe

The SM8850 ClockDxe image has PE SHA-256 `c27e141dcfd0e26c9ff7cd7c9c20c0a5cedd09fa5e86a5630fea95540cf85b70`.
Its CESTA array at image offset `0x24380` lists `cam_cesta`, `disp_cesta`, and `pcie_cesta`.
Only `cam_cesta` (record `0x3b728`, node `/soc/cesta@9549e00`) has the default-enable flag.
`Clock_InitCESTA` therefore programs only the camera instance at boot.

HWIODxe places the five camera banks in the TITAN block at `0x09000000`:

| Socket | System base | Size |
| --- | --- | --- |
| `crmb_socket` | `0x09549e00` | `0x400` |
| `crmb_pt_socket` | `0x0954a200` | `0x400` |
| `crmc_socket` | `0x0954a600` | `0x2000` |
| `crmv_socket` | `0x0954c600` | `0x600` |
| `common_socket` | `0x0954cc00` | `0x200` |

The record writes 93 sequence words to CRMC `+0x1380` and 22 branch words to CRMC `+0x1300`.
Its 212 register records at image offset `0x1d2f0` write 190 CRMC words, 13 CRMB words, 8 CRMV words, and one common word.
Types are raw values, Command DB addresses, and HLVL values from `mmcx.lvl` and `mxc.lvl`.
The camera instance declares exactly these destinations. Run657 stopped at the first sequence write, `0x0954b980`.

### PCIe CESTA from UEFI ClockDxe

The array at image offset `0x24380` also lists `pcie_cesta`.
Its record is `0x3b8c8` and its node is `/soc/cesta@1d03000`.
The record holds `nFlags` value zero at offset `0xc8`, so it has no default-enable bit.
`Clock_InitCESTA` therefore skips it, because RVA `0xcfd0` reads that byte and RVA `0xcfd4` tests bit 0.
Run745 programmed this instance through the clock resource path at RVA `0x9614`.
That path rejects records with bit 0 set at RVA `0x966c` and accepts records without it.
Both paths call the write routine at RVA `0xcc80`. [INFERENCE] Therefore only the second path selects the PCIe record.

The three records carry these `nFlags` words at offset `0xc8`:

| Record | Node | `nFlags` |
| --- | --- | --- |
| `cam_cesta` | `/soc/cesta@9549e00` | `0x00000001` |
| `disp_cesta` | `/soc/cesta@9bcc000` | `0x00400000` |
| `pcie_cesta` | `/soc/cesta@1d03000` | `0x00000000` |

The `reg` and `reg-names` properties of the node give the five bank bases.
The DTB at `xbl_config.elf` file offset `0x10e14` supplies these values:

| Socket | reg-name | System base | Size |
| --- | --- | --- | --- |
| `crmb_socket` | `PCIE_RSCC_CRMB` | `0x01d03000` | `0x400` |
| `crmb_pt_socket` | `PCIE_RSCC_CRMB_PT` | `0x01d03400` | `0x400` |
| `crmc_socket` | `PCIE_RSCC_CRMC` | `0x01d03800` | `0x2000` |
| `crmv_socket` | `PCIE_RSCC_CRMV` | `0x01d05800` | `0x600` |
| `common_socket` | `PCIE_RSCC_CRM_COMMON` | `0x01d05e00` | `0x200` |

The same tree supplies the camera banks above and the display banks at `0x09bcc000`.
RVA `0xd448` selects a base address in two steps.
It finds the index of the bank name in `reg-names`, then reads the `reg` pair at that index.

The record writes 122 sequence words to CRMC `+0x8b8` and 28 branch words to CRMC `+0x838`.
The sequence words start at image offset `0x1f410`.
The branch words start at image offset `0x1f5f8`.
Run745 wrote `0x38c15000` at `0x1d040b8`.
That address is CRMC `0x01d03800` plus the sequence offset `0x8b8`.
The value is the first sequence word.

The 103 register records at image offset `0x1f668` use these banks:

| Bank | Records | Value types |
| --- | --- | --- |
| CRMC | 74 | 62 raw and 12 Command DB |
| CRMB_PT | 18 | 12 raw and 6 HLVL |
| CRMV | 10 | 5 raw and 5 HLVL |
| CRM_COMMON | 1 | 1 raw |
| CRMB | 0 | none |

The records name 73 destinations in CRMC. The destination `+0x6c` appears twice.
The sequence and branch lists add 150 CRMC destinations.
`platforms/qcom/sm88xx/uefi-clock-dependencies.lua` therefore declares 223 CRMC words as `uefi_pcie_cesta`.

The record has no clock dependency list, because the field at `0xb8` is zero.
No PCIe source carries `CLOCK_FLAG_CESTA_CONTROLLED` (`0x04000000`).
The three flagged source records are `cam_cc_pll3`, `cam_cc_pll4`, and `cam_cc_pll5`.
They start at image offsets `0x29948`, `0x29a08`, and `0x29ac8`.
The camera PLL bank therefore covers every controlled source, and this instance needs no PLL bank.
The write routine reads no CESTA word.
This instance therefore needs no virtual value and no completion word.

## Native DCP GDSC hardware handoff

The DCP clock controller maps `DISP_CC_DISP_CC_REG` at DCP `0x103aa000` in both selected trees.
The slot is `0xa014f0ec` on SM8850 and `0xa417f0ec` on SM8845.
The mapping records are `0xa020d070` and `0xa423cb00`.
GDSC `+0x1000` therefore aliases system `0x09bab000`.

The clock initializer explicitly enables hardware control:

| Operation | SM8850 | SM8845 |
| --- | --- | --- |
| Initializer entry | `0xa00d8d90` | `0xa4105e5a` |
| Registered initializer pointer | `0xa0128130` | `0xa4158130` |
| Load mapped base | `0xa00d8daa` | `0xa4105e72` |
| Read GDSCR at mapped base plus `0x1000` | `0xa00d8db2` | `0xa4105e7a` |
| Set bit one | `0xa00d8db6` | `0xa4105e7e` |
| Store GDSCR | `0xa00d8dba` | `0xa4105e82` |

This write follows a successful return from the preceding clock initialization helper.
It supplies direct evidence for GDSCR HW_CONTROL.
It is not a CESTA vote or an inferred effect of WFI.

The generic GDSC descriptors also identify the same register:

| Descriptor field | SM8850 | SM8845 |
| --- | --- | --- |
| Core descriptor | `0xa0212578` | `0xa423db18` |
| GDSCR relative offset | `+0x1000` | `+0x1000` |
| INT2 descriptor | `0xa02125bc` | `0xa423db50` |
| INT2 GDSCR relative offset | `+0x3000` | `+0x3000` |
| HAL method table | `0xa02111a8` | `0xa423c980` |
| Enable method | `0xa00d7df2` | `0xa4104eba` |
| Disable method | `0xa00d7e44` | `0xa4104f0c` |
| Software-enable query | `0xa00d7e6a` | `0xa4104f32` |
| Power-on query | `0xa00d7e86` | `0xa4104f4e` |

The enable method clears SW_COLLAPSE and polls GDSCR bit 31.
If the HAL flags request hardware control, it then sets bit one.
The conditional hardware-control stores are `0xa00d7e2c` and `0xa4104ef4`.
The disable method can clear bit one before it sets SW_COLLAPSE.
Its hardware-control stores are `0xa00d7e60` and `0xa4104f28`.
The separate target initializer proves the actual handoff without relying on that conditional flag.


## BCM revision and VCD status

`bcm_status.h` implements the two decoded consumers of the DCP BCM status aperture.
The DCP mapping row is SM8850 `0xa012bff8` or SM8845 `0xa415be90`.
It maps `+RPMH_BCM_BCM_STATUS` to DCP `0x4bbf0000`, which aliases system `0x0bbf0000`.
The ICB records are `0xa0217144` and `0xa4241f14`.
Both specify mapping size `0x1f00` and an internal offset of `0x200`.

| Register or operation | SM8850 PC | SM8845 PC | Decoded contract |
| --- | --- | --- | --- |
| BCM base `+0x1908` | `0xa00e0730` | `0xa410c7c4` | Cache revision bits 23:8 |
| Compare cached revision shifted by eight with `0x105` | `0xa00e046a` | `0xa410c4fe` | Select the pre-1.5 zero-vote workaround |
| Compare cached revision with `0x20000` | `0xa00e14e2` | `0xa410d576` | Select the revision-dependent withdrawal path |
| Copy auxiliary VCD byte to BCM record `+0x14` | `0xa00e0912`, `0xa00e091c` | `0xa410c9a6`, `0xa410c9b0` | The status index is the command-database VCD |
| BCM base `+0x800+4*VCD` | `0xa00e154a` | `0xa410d5de` | Read the low four status bits |
| Test the low four bits | `0xa00e1550` | `0xa410d5e4` | Nonzero ends the bounded wait |

The first address is base plus `0x200+0x1000+0x708`.
Its `0x200` term does not establish a per-DRV vote view.
The other consumer indexes a VCD, not a BCM resource number or global DRV number.
No decoded consumer writes this aperture.

The model advertises virtual BCM ABI 2.0 with revision word `0x00020000`.
This is not a measured silicon revision or a readiness indication.
It selects normal fourteen-bit X and Y encoding.
The comparison against `0x105` is false, so a zero X vote encodes zero rather than the pre-1.5 sentinel `0xfffc0000`.
The comparison `0x20000 < revision` is also false.
Thus the model does not bypass the revision-dependent withdrawal path with a newer revision claim.

That path checks whether the new vote pair is zero and the old pair is nonzero.
If so, it reads VCD status up to 501 times, with up to 500 ten-microsecond delays.
Nonzero status ends this wait.
The timeout also returns through the normal `Complete` log path.
The decode does not establish a physical busy-state encoding.

The virtual low-bit contract is explicit:

1. A staged vote that the shared owner did not publish reports one.
2. Publication clears that requester's staged state through the existing BCM commit operation.
3. A VCD reports zero only when none of its mapped resources has a staged vote from any requester.

`QcomRpmhRsc::resource_pending()` reads this actual owner state.
The status model does not keep a second copy of votes or completion state.
A commit by one requester cannot clear another requester's staged vote.
Status reads do not publish votes or advance state.
This virtual contract can retain the firmware's bounded delay after an idle withdrawal.
It does not claim a physical clock-plan acknowledgment.

The selected AOP database supplies the VCD mapping.
SM8850 BCM records start at `0x0b100510`, with auxiliary records at `0x0b100410`.
SM8845 records start at `0x0b100ba8`, with auxiliary records at `0x0b100ab8`.
Each BCM record has length `0x18`.
Each auxiliary record has length eight and stores the VCD byte at `+6`.
The existing publisher copies these original guest-loaded records.

| VCD | Resource addresses |
| --- | --- |
| 0 | `0x50000`, `0x5005c`, `0x5006c`, `0x50074`, `0x5007c` |
| 1 | `0x50004`, `0x50008`, `0x5000c`, `0x50070`, `0x50078` |
| 3 | `0x50010..0x50024`, stride four |
| 4 | `0x50028`, `0x5002c` |
| 5 | `0x50030`, `0x50034` |
| 6 | `0x50038` |
| 7 | `0x5003c` |
| 8 | `0x50040`, `0x50044`, `0x50048`, `0x50060`, `0x50064` |
| 9 | `0x5004c` |
| 10 | `0x50050` |
| 11 | `0x50054` |
| 12 | `0x50058` |
| 13 | `0x50068` |

SM8845 omits resources `0x50054` and `0x50064`.
The `resource_vcds` parameter carries this platform data without a SoC check inside the model.
The model rejects unconfigured VCD offsets, other unknown offsets, and register writes.
It checks that every configured resource exists in the shared owner.

The generic LOCAL_CLK and CESTA error branches validate BCM description fields during ICB initialization.
They do not introduce another decoded MMIO status register.
The selected SM8850 ICB list contains sixteen BCM descriptions, with resource details supplied by the command database.
Vote payloads and their publication remain in the existing RPMh backend.
No additional X/Y, per-DRV vote-table, clock-plan, or physical acknowledgment layout is qualified.

The focused native regression stages votes, commits a resource group, and checks VCD status.
It also checks that one requester's commit leaves another requester's staged vote busy.
Its test name is `bcm_status_tracks_staged_votes_until_owner_commit`.
Its execution and the platform build remain pending for this change.


## VRM entries from the loaded AOP configuration

The publisher appends slave four after the existing `3,3,5` directory entries.
The first three record arrays, auxiliary arrays, offsets, and directory fields do not change.
The VRM table version is `0x100`, not `0x1000`.
Its priorities are zero. Each record has four auxiliary bytes and its own auxiliary offset.
Aliases retain separate records, even when their resource addresses match.

### Selected configuration sources

| SoC | Image | SHA-256 |
| --- | --- | --- |
| SM8845 | `RADIO/aop.img` | `1c56eeb2fff24846dd923e1594689b07ce2c5c8539d56ffb7608073940c6daff` |
| SM8850 | `RADIO/aop.img` | `adaf8166f8fd78fa52e45a4e771685a3df5fc6018a01ac388e1bcd0a792a8c11` |
| SM8845 | `RADIO/aop_config.img` | `6aa335cac21a9291b3f7aa095450c41847f92066fd16a4cde6b095f025859dd0` |
| SM8850 | `RADIO/aop_config.img` | `440e74f3c217fbe78248e2e743f21df4be4acfeb82d77b26b44a083f4cf113cc` |

| SoC | Loaded span | SHA-256 |
| --- | --- | --- |
| SM8845 | `0x81c80000+0x3350` | `6d6425951b6331cc7d55dc8b205530abdd9c418cb2e3fbd691045b13e9ba7bd1` |
| SM8845 | `0x81c84000+0x4af` | `4c612853b70e4cfdaf3d9ac7d1a2ede3250c4237493073d44a03d3ad71866e0c` |
| SM8850 | `0x81c80000+0x3720` | `f8a2a5f5a7c88c8e824116e75e35c7c98820d65c4a2af89aa6800479e1265c34` |
| SM8850 | `0x81c84000+0x55e` | `85a4f70a6f371c4e128338fe635e1a59ff826b034f44d388eb733f500544e39d` |

For slave four, the row's count field specifies the first source size.
The second source contains strings, not an existing auxiliary array.
The service checks both hashes before it derives records.
It reads these sources through the memory socket at authenticated image admission.

The `discovery` CCI string supplies three more rows, each with an address, size, and SHA-256.
These rows select the model-mask pointer table, mask bytes, and regulator layout bytes.
The publisher checks all three spans before it reads PMIC registers.

| SoC | Loaded discovery span | SHA-256 |
| --- | --- | --- |
| SM8845 | `0x0b092860+0xbc` | `bb21c3fbfad7a44fdf50af474fdd78f031fa0bf8677130754eddcda91a72b9e7` |
| SM8850 | `0x0b092944+0xbc` | `ea32fc2d34e21abc0c680142092a27b829ab509c0bb1935dfcb3bf439a556feb` |
| SM8845 | `0x81c3ae7a+0x2e0` | `20f320273918eaea4f7da5922f83bc866a8983084a87d613d1e9c93556f0ba79` |
| SM8850 | `0x81c3b8a2+0x2e0` | `20f320273918eaea4f7da5922f83bc866a8983084a87d613d1e9c93556f0ba79` |
| SM8845 | `0x0b100208+0xa2` | `23cf18b1d8bd14baec865afeb321f67ef193dd4f928d95346ca3e7e31d027148` |
| SM8850 | `0x0b100210+0xa2` | `23cf18b1d8bd14baec865afeb321f67ef193dd4f928d95346ca3e7e31d027148` |


### Source layout and allocation

Both configurations contain the property directory at `0x81c801cc`.
Each eight-byte row contains a CRC-32 of the property name and a pointer.
Most properties point to 28 pointers or counts, in two sets of fourteen PMIC slots.
The normal startup path visits each set, then each slot, then SMPS, LDO, and BOB.

The configuration data starts at image file offset `0x1000`.
The strings start at file offset `0x5000` and loaded address `0x81c84000`.

| Source | SM8845 VA | SM8850 VA |
| --- | --- | --- |
| SMPS counts | `0x81c8000c` | `0x81c8000c` |
| LDO counts | `0x81c8007c` | `0x81c8007c` |
| BOB counts | `0x81c800ec` | `0x81c800ec` |
| SMPS rail vector | `0x81c81d9c` | `0x81c8216c` |
| SMPS name vector | `0x81c81fd0` | `0x81c823a0` |
| SMPS dependency vector | `0x81c82040` | `0x81c82410` |
| LDO rail vector | `0x81c81e0c` | `0x81c821dc` |
| LDO name vector | `0x81c820b0` | `0x81c82480` |
| LDO dependency vector | `0x81c82120` | `0x81c824f0` |
| BOB rail vector | `0x81c81e7c` | `0x81c8224c` |
| BOB name vector | `0x81c82190` | `0x81c82560` |
| BOB dependency vector | `0x81c82200` | `0x81c825d0` |
| Target rows | `0x81c81050` | `0x81c81230` |
| Target count | `0x81c802a8` | `0x81c802b8` |


Resource properties contain eight-byte name pairs. Pair zero is unused.
Each pair supplies a primary name and an optional alias.
Dependency rows occupy sixteen bytes. Row zero is unused.
Each row supplies a parent pair pointer, child-list pointer, child count, and reservation count.
Rail rows occupy `0x24` bytes. Bit zero enables the row.
Bits `4:1` select the resource type.
The target table uses `0x28`-byte rows and a separate count property.

The first pass reserves parent IDs.
The first child of each parent reserves one contiguous block with the parent's child count.
Later children consume that block in traversal order.
The parent retains its original reserved ID.
The selected clock rows are disabled. They add no records on the normal PMIC path.
Resources without parents consume the next ID.
Type-one resources use a separate counter that starts at `0x60`.
The resource address is `0x40000 | (id << 8)`.
The target pass follows the PMIC pass and uses the same counters.

| Operation | SM8845 PC | SM8850 PC |
| --- | --- | --- |
| Parent and PMIC passes | `0x81c21d7a` | `0x81c21404` |
| Parent source traversal | `0x81c21f7e` | `0x81c21600` |
| Enabled rail traversal | `0x81c216be` | `0x81c2188e` |
| Target source traversal | `0x81c20dd6` | `0x81c20e9a` |
| ID allocation | `0x81c2330e` | `0x81c23684` |
| Address and auxiliary type | `0x81c232cc` | `0x81c23642` |
| Record preparation | `0x81c2321c` | `0x81c23592` |
| Record append | `0x0b087616` | `0x0b08783c` |
| Final slave-four registration | `0x81c22ffa` | `0x81c23370` |
| SPMI channel translation | `0x0b084c9c` | `0x0b084d58` |

The append routine copies the zero priority words from its cleared preparation record.
It copies the name, address, auxiliary length, and auxiliary offset into the `0x18`-byte result.
It advances both output cursors. Final registration uses the existing database allocator.
That allocator adds a new directory slot. It does not merge matching slave IDs.

### Normal PMIC discovery

The normal path does not use the fallback addresses `0x14`, `0x40`, and `0xa0`.
It does not calculate the physical SID as twice the config slot.
Those rules belong to the absent-PMIC fallback.

The identity pass reads five bytes at SID register `0x0102`.
Register `0x0104` must report type `0x51`.
Register `0x0105` supplies the PMIC model.
Register `0x0106` supplies the slave count. Zero selects two slaves.
The pass advances the physical SID and config slot by that count.
Only the first slot receives a PMIC model. Secondary slots are not independent PMICs.

The model selects a pointer from the 47-entry table for model IDs `0x35..0x63`.
Each SID bank uses 32 mask bytes.
The peripheral pass visits mapped PPIDs in increasing SID and peripheral order.
For each mask bit, it reads seven revision bytes from that peripheral.
Type is byte four. Subtype is byte five.
Zero type or equal subtype and byte six rejects the peripheral.

The regulator driver records the first base and communication object for each regulator kind.
Later peripherals use index `(base-first_base)/stride`.
The row retains the observed peripheral base.
The communication object supplies the physical SID.
SMPS, LDO, and BOB strides are `0x300`, `0x100`, and `0x200` bytes.
These values come from the hash-checked layout span, not fixed peripheral bases.

| Operation | SM8845 PC | SM8850 PC |
| --- | --- | --- |
| Identity and slave-count scan | `0x0b0845e2` | `0x0b08469e` |
| Physical SID mapping | `0x0b084468` | `0x0b084524` |
| Communication objects | `0x81c1fc60` | `0x81c20114` |
| Peripheral discovery | `0x81c1fda6` | `0x81c201e2` |
| Model-mask selection | `0x0b084752` | `0x0b08480e` |
| Seven-byte revision read call | `0x81c1ff70` | `0x81c203ac` |
| First LDO base and layout | `0x81c20002` | `0x81c2043e` |
| LDO index calculation | `0x81c20046` | `0x81c20482` |
| Rail SID and base consumption | `0x81c219a4` | `0x81c21b74` |

The publisher uses the existing APSS observer lane for these read commands.
It checks completion status before it consumes the reply.
No peripheral write or new SPMI owner is required.
An enabled rail without a discovered row fails publication.
Missing PPIDs omit discovery reads. Failed revision transactions leave the native cleared buffer unchanged and skip that peripheral.
They do not abort discovery. Invalid classified rail counts still fail publication.
See the [complete PMIC audit](../spmi/aop-discovery-evidence.md) for both mapped inventories and paired error-handling PCs.

### Auxiliary data and runtime input

The four auxiliary bytes are resource type, VRM index low byte, zero, and zero.
The type byte is one for ordinary rails and two for type-one resources.
SMPS records select the secondary peripheral, one address above the control peripheral.
SM8850 `0x81c21bc2..0x81c21bd8` resolves this secondary field before record preparation.
The AOP constructs its software translation table from the SPMI arbiter.
SM8850 `0x0b0840f2..0x0b0840fa` selects its static software descriptor at loaded address `0x0b100330`.
SM8845 `0x0b084036..0x0b08403e` selects the descriptor at `0x0b100328`.
These descriptors contain pointers to arrays that initially contain zeros.
They are not pointers read from arbiter address `0x0c428000`.

SM8850 `0x0b084dec..0x0b0850e0` constructs the translation table.
The SM8845 function starts at `0x0b084d30`.
Both descriptors select `0x4d4` APIDs on bus zero and `0x200` APIDs on bus one.
The APID registers start at APSS `0x0c403000`.
Bus one starts at APID `0x800`.
Each register supplies the peripheral in bits `7:0` and SID in bits `12:8`.
An absent PPID prevents resource registration.

SM8850 `0x0b08505a..0x0b0850d6` reads the VRM map at APSS `0x0c402300`.
The matching SM8845 block starts at `0x0b084f9e`.
Each of the 256 words supplies peripheral bits `15:8`, SID bits `20:16`, and bus bits `22:21`.
The translation starts with sentinel `0x100`.
Each matching map word replaces that value with its index. The last match wins.
The auxiliary byte contains the low byte, so an unmatched sentinel produces zero.
The service reads both hardware tables once through its memory socket.
The existing SPMI arbiter owns these registers and retains the guest-programmed values.
No RAM alias or second SPMI owner is required.
These mutable registers have no fixed firmware hash.
Failed reads or absent PPIDs prevent publication.

SM8850 `0x81c20e26..0x81c20e58` selects the fallback startup path only when PMIC zero is absent or unknown.
The selected platform provides PMIC zero.
This decoder implements the normal PMIC path, not the absent-PMIC fallback.
It does not claim physical AOP execution or a completed OEM boot.

### Derived names required by DCP

| SoC | Name | Address |
| --- | --- | --- |
| SM8845 | `L1D_E0` | `0x42e00` |
| SM8845 | `L3D_E0` | `0x41c00` |
| SM8850 | `L1D_E0` | `0x43400` |
| SM8850 | `L3D_E0` | `0x43f00` |

The results depend on the discovered inventory, not only the configuration hash.
The current SM8845 board policy produces 89 records and length `0x14c0` in the focused probe.
Its absent SID2, SID9, and SID11 omit `S1C_E0`, its alias, `S1J_E0`, and `S1L_E0`.
The SM8850 admission capture and current revision policy produce 104 records and length `0x16a0`.
Its bus-one SID12 identity spans two SIDs. The secondary slot does not publish the seven configured N rails.
The earlier counts of 93 and 111 assumed that all configured slots were independent PMICs.
The earlier SM8845 `L1D_E0=0x42f00` address made that same incorrect assumption.

The focused regression is `aop_resources_follow_discovered_rails_and_reject_failed_sources`.
Its synthetic configuration supplies LDOs at physical SID3, peripherals `0xc1` and `0xc3`.
It checks both resource addresses, auxiliary mapping, secondary-slot exclusion, and unchanged ARC/BCM bytes.
It also checks duplicate admission, missing PPIDs, failed revision reads, and failed config or discovery hashes.
It also checks a failed revision on a zero-count slot, a classified zero-count rail, and an excessive index.
The integration suite passes this regression.

A private SystemC probe used both hash-identified AOP images and configuration images.
The SM8850 case used the existing 818-row PPID capture and the current virtual revision policy.
Every emitted rail, secondary SMPS peripheral, and target resolved through that capture.
The SM8845 case used synthetic PPID programming for the current board policy.
Both cases checked unchanged ARC/BCM records, auxiliary arrays, and directory fields.
Both configuration-hash failures left the pointer, cookie, and database version zero.
A changed SM8850 discovery-mask hash also prevented publication.
These probes do not establish genuine image authentication or an OEM boot.

Native SM8850 run220 and SM8845 run176 publish the database after genuine AOP image admission.
In both runs, the DCP resolves `L1D_E0` and `L3D_E0` and passes Clock_InitCESTA.
SM8845 logs failed revision reads for SID7 peripherals `0x49`, `0xf0`, `0xf1`, and `0xf2`, and discovery continues.
These runs do not establish kernel entry.


### VRM votes from UEFI

UEFI RpmhDxe uses DRV2. After ClockDxe, it sends the first VRM vote: message `0x10108`, address `0x42a00`, and data `0x9c8`.
The resource owner accepted only the listed ARC and BCM resources, so it rejected the batch (run662).

The AOP resource service now implements `RpmhResourceDirectory`.
After a complete publication, it reports each VRM register of a published record: `0x40000 | id << 8` plus `0x0`, `0x4`, `0x8`, or `0xc`.
The SM8850 resource owner names the service in `directory_path`.
It creates the entry of a published VRM register at its first vote and logs `RPMH_VRM_RESOURCE`.
Addresses that the database does not publish still fail.
The owner aggregates each VRM register as the maximum over requesters, like an ARC level.

Run663 voted voltage, enable, and mode at `0x42000`, `0x42a00`, `0x43400`, `0x43800`, and `0x43f00`.
`0x43400` is `L1D_E0`, and `0x43f00` is `L3D_E0`.
The model does not apply these votes to the PMIC regulator registers. A later regulator status read can therefore disagree with an enable vote.

## UEFI camera RSCC

RscDxe configures one RSC instance from its data section.
The instance table starts at image RVA 0x71c0.
The first record names `cam` and keeps physical base `0x09000000`.
The second record names `pcie` and keeps physical base `0x01d00000`.
The table count is two at RVA 0x7290.
The camera helpers ignore the record base and use the literal base `0x09545000`.
The helper at RVA 0x33e8 compares the instance name with the string `cam` at RVA 0x58c9.
The same helper selects the simple offsets for the other instance names.

The first register access is a read of `0x09545004` at RVA 0x3550.
The value is masked with `0x071f1f1f`, which is `TITAN_RSCC_RSC_PARAM_SOLVER_CONFIG_DRVd`.
The initialization routine at RVA 0x2f08 makes these reads in this order:

| Address | Width | HWIO name | Field and use | RVA |
| --- | --- | --- | --- | --- |
| `0x09545004` | 32 | `RSC_PARAM_SOLVER_CONFIG_DRVd` | complete masked word | 0x3550 |
| `0x0954500c` | 32 | `RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRVd` | complete word, mask `0xf8ffffff` | 0x3698 |
| `0x09545008` | 32 | `RSC_PARAM_RSC_CONFIG_DRVd` | `NUM_DRV` bits 26:24 | 0x3598 |
| `0x09545008` | 32 | `RSC_PARAM_RSC_CONFIG_DRVd` | `NUM_SEQ_CMD_WORDS` bits 23:16 | 0x35d8 |
| `0x09545008` | 32 | `RSC_PARAM_RSC_CONFIG_DRVd` | `NUM_TS_EVENTS` bits 11:8 | 0x3618 |
| `0x09545008` | 32 | `RSC_PARAM_RSC_CONFIG_DRVd` | `DELAY_CNTR_BITWIDTH` bits 4:0 | 0x3658 |
| `0x09545000` | 32 | `RSC_ID_DRVd` | `STEP_VER` bits 7:0 | 0x3498 |
| `0x09545000` | 32 | `RSC_ID_DRVd` | `MINOR_VER` bits 15:8 | 0x34d4 |
| `0x09545000` | 32 | `RSC_ID_DRVd` | `MAJOR_VER` bits 23:16 | 0x3510 |

The parameter reads precede the identification reads, because RVA 0x2f08 calls RVA 0x314c before RVA 0x310c.
RVA 0x314c stores the parameters in the configuration block at handle offset 0x58.
RVA 0x310c stores the three identification bytes in the same block.

The routine at RVA 0x31c4 selects the sequence copy routine from the handle byte at offset 8.
The indirect call at RVA 0x3248 runs RVA 0x37ac for the 32-bit mode.
The camera branch of RVA 0x37ac reads and writes `TITAN_RSCC_SEQ_MEM_CHN_TCS_m_DRV0` at `0x09545000 + 0x4b8 + 4 * m`.
Each store repeats the masked-write read first, at RVA 0x3804 or 0x3854, and then writes at RVA 0x3808 or 0x3858.
The mode record at RVA 0x7150 supplies the sequence at RVA 0x7298 and the count 24.
The words land in slots 0 to 23, which is the complete `SEQ_MEM_CHN_TCS` range.

The routine at RVA 0x2f80 then writes the branch table and the override address.

| Address | Width | HWIO name | Value and use | RVA |
| --- | --- | --- | --- | --- |
| `0x09545420` to `0x09545440` | 32 | `RSC_SEQ_CFG_BR_ADDR_i_CHN_TCS_DRV0` | branch addresses for i 1 to 8 | 0x3788 |
| `0x09545220` | 32 | `RSC_SEQ_OVERRIDE_START_ADDR_DRV0` | `VALID` bit 31 becomes one | 0x36e4 |
| `0x09545220` | 32 | `RSC_SEQ_OVERRIDE_START_ADDR_DRV0` | `ADDR` bits 7:0 become zero | 0x3730 |

The branch values come from RVA 0x72f8: zero, `0x8`, `0x18`, `0x1c`, `0x2e`, `0x36`, `0x46`, `0x4a`, and `0x5c`.
The loop skips index zero, because its value is zero.
Each write is a direct store of the low 16 bits.
Both override writes are masked reads and writes of the same word, so the model sees two reads and two writes at `0x09545220`.
The mode record stores the start address zero at RVA 0x7160.

The image never starts the sequencer in this path.
It has no immediate for `0x09545280` (`RSC_SEQ_OVERRIDE_TRIGGER_DRV0`) or `0x09545284` (`RSC_SEQ_OVERRIDE_TRIGGER_START_ADDRESS_DRV0`).
It never reads `0x09545224` (`RSC_SEQ_BUSY_DRV0`), `0x09545228` (`RSC_SEQ_PROGRAM_COUNTER_DRV0`), or `0x09545230` (`RSC_SEQ_COMP_DRV0`).
It never reads the timestamp units at `0x09545120` to `0x0954513c`.
RscDxe RVA `0x38f0` (`rsc_hal_conf_hidden_tcs_owner`) also serves the camera instance.
It reads `0x0954501c` (`HIDDEN_TCS_CTRL_DRV0`) and writes back bits 1:0, the PDC timer owner.
Run731 stopped at that read, which the first decode missed.
The model retains the owner field. A write of `HIDDEN_TCS_TRIGGER` (bit 16) fails, because no hidden TCS exists.
The hidden-TCS data write at RVA `0x390c` stays guarded by the `pcie` name check at RVA `0x33a4`.

The protocol initialization at RVA 0x15e8 resolves one instance by name and rejects any other count at RVA 0x1614.
Run730 selected the camera instance. [INFERENCE]
Run742 continued into the PCIe instance at RVA 0x7228, so the driver initializes both records.
The camera model therefore covers the camera aperture only.
The next section gives the PCIe decode.

The block ends at `0x09548000`, from `TITAN_RSCC_RSCC_RSC_REG_BASE_SIZE` value `0x3000`.
The camera aperture does not overlap the CESTA banks at `0x09549e00`.

The model returns these virtual words, because no SM8850 source gives them:

| Offset | Value | Register | Consumer |
| --- | --- | --- | --- |
| 0x0 | `0x00030800` | `RSC_ID_DRVd_POR` | RVA 0x310c stores the bytes |
| 0x4 | `0x00010100` | `RSC_PARAM_SOLVER_CONFIG_DRVd_POR` | RVA 0x314c stores the word |
| 0x8 | `0x03100214` | `RSC_PARAM_RSC_CONFIG_DRVd_POR` | RVA 0x314c stores four fields |
| 0xc | `0x60004104` | `RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRVd_POR` | RVA 0x314c stores the word |

The driver stores these words and compares none of them in this path.
The model runs no sequencer, so busy, the program counter, and the trigger fail.

The native fixture `camera-rscc` checks the parameter reads, the sequence retention, the owner field, and the failing offsets.
The regression is `camera_rscc_retains_the_sequence_without_a_start_request`.

## UEFI PCIe RSCC

The instance record at RVA 0x7228 names `pcie` and keeps physical base `0x01d00000`.
The name test at RVA 0x33a4 compares the instance name with the string at RVA 0x58cd.
The HAL helpers return the simple offsets for this name, so the driver adds the record base to each offset.
The mode record at RVA 0x7188 supplies four fields:

- the sequence pointer `0x730c`
- the sequence count five
- the start address zero at RVA 0x7198
- the branch list `0x7420` with one entry

The one branch entry is zero, so the driver writes no branch address.
The five sequence words at RVA 0x730c match `pcie_rsc_rsc_sleep` in `rsc_config.c` lines 94 to 116.

Run742 read `0x01d00004` first.
The read order is the same as for the camera instance:

| Address | Width | HWIO name | Field and use | RVA |
| --- | --- | --- | --- | --- |
| `0x01d00004` | 32 | `RSC_PARAM_SOLVER_CONFIG_DRVd` | complete masked word | 0x3550 |
| `0x01d0000c` | 32 | `RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRVd` | complete word, mask `0xf8ffffff` | 0x3698 |
| `0x01d00008` | 32 | `RSC_PARAM_RSC_CONFIG_DRVd` | `NUM_DRV`, `NUM_SEQ_CMD_WORDS`, `NUM_TS_EVENTS`, delay width | 0x3598, 0x35d8, 0x3618, 0x3658 |
| `0x01d00000` | 32 | `RSC_ID_DRVd` | `STEP_VER`, `MINOR_VER`, `MAJOR_VER` | 0x3498, 0x34d4, 0x3510 |

The copy routine at RVA 0x37ac takes the non-camera branch for this instance.
It reads `NUM_SEQ_CMD_WORDS` again at RVA 0x3828.
It returns zero when the start plus the size exceeds that word count (RVA 0x3830 and 0x383c).
The five words land in slots 0 to 4 at `0x01d00370` to `0x01d00380`.
Each masked write reads the slot first at RVA 0x3804 or 0x3854, then stores at RVA 0x3808 or 0x3858.

RVA 0x2f54 then calls RVA 0x38f0 for this instance.
RVA 0x3938 follows the `pcie` name test at RVA 0x33a4.
These writes follow:

| Address | Width | HWIO name | Value and use | RVA |
| --- | --- | --- | --- | --- |
| `0x01d0001c` | 32 | `HIDDEN_TCS_CTRL_DRV0` | owner bits 1:0, masked read and write | 0x3920, 0x3928 |
| `0x01d00038` | 32 | `HIDDEN_TCS_CMD0_DATA_DRVd` | `0x80ffffff` | 0x3958 |
| `0x01d00040` | 32 | `HIDDEN_TCS_CMD1_DATA_DRVd` | `0xffffffff` | 0x3964 |
| `0x01d00048` | 32 | `HIDDEN_TCS_CMD2_DATA_DRV0` | `0x80000021` | 0x3974 |

The source function `rsc_hal_set_hidden_tcs_init` (rsc_hal.c 711 to 718) gives the same three data words.
The three data stores are direct stores, so no read precedes them.

RVA 0x2f80 sets the override address.

| Address | Width | HWIO name | Value and use | RVA |
| --- | --- | --- | --- | --- |
| `0x01d00220` | 32 | `RSC_SEQ_OVERRIDE_START_ADDR_DRV0` | `VALID` bit 31 becomes one | 0x36e4 |
| `0x01d00220` | 32 | `RSC_SEQ_OVERRIDE_START_ADDR_DRV0` | `ADDR` becomes zero | 0x3730 |

Both writes are masked reads and writes, with reads at RVA 0x36dc and 0x3724.

The image starts no sequence.
It writes no trigger at `0x01d00280` or `0x01d00284`.
It reads no busy bit, program counter, completion word, or timestamp.
The image writes no force-wake request and reads no force-wake status. [INFERENCE]
The three data words configure the timer path. No hidden-TCS trigger occurs and no completion wait occurs. [INFERENCE]
The block ends at `0x01d01000`, from `PCIE_RSCC_RSC_REG_BASE_SIZE` value `0x1000`.

The model returns these virtual words, because no SM8850 source gives them:

| Offset | Value | Register |
| --- | --- | --- |
| 0x0 | `0x00030800` | `RSC_ID_DRVd_POR` |
| 0x4 | `0x00010100` | `RSC_PARAM_SOLVER_CONFIG_DRVd_POR` |
| 0x8 | `0x01100214` | `RSC_PARAM_RSC_CONFIG_DRVd_POR`, `NUM_SEQ_CMD_WORDS` 0x10 |
| 0xc | `0x08000001` | `RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRVd_POR` |

The word count `0x10` bounds the five-word copy at RVA 0x383c.
The model retains the sequence slots, the owner field, the three data words, and the override word.
It has no branch registers, because the driver writes none.
The native fixture `pcie-rscc` checks these words and the failing offsets.
The regression is `pcie_rscc_retains_the_sequence_and_hidden_tcs_data`.

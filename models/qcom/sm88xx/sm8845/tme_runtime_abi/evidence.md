# SM8845 runtime composition evidence

## Scope and inputs

The installed SM8845 factory uses the supplied QRD8850 compatibility ROMs. It does not claim a native SM8845 ROM capture.
The factory selects the runtime composition when `options.tme_runtime_api_map` is present. `options.tme` names the reconstructed ROM.
The helper does not select media, console, debugger, trace, or mutable-LUN policy.
The private diagnostic calls the same installed helper and retains its independent trace files and read-only media policy.

The source package is `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`.
Its SHA-256 is `6e15a8792b09e40cb203e5230d3f7d467f0910f102dbab3a7d715853f04ee1d1`.
No package bytes belong in Git.

| Package member | SHA-256 |
| --- | --- |
| `IMAGES/signed_firmware_soc_view.elf` | `6a781e1eb215fbcd2d157bebb7b25502f6ae74ed9bce7810dc61f98545c10fb4` |
| `IMAGES/tme_config.elf` | `153c60bf2db234b3aceae57a2849cdddf4b07d2ec3337748cfcece668a52a49e` |
| `IMAGES/sequencer_ram.elf` | `d7dc67ddd77332e558d14b7acb82bc42e1be66a07786403c96b655f46d019636` |
| `IMAGES/xbl_sc.elf` | `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4` |

The [execution evidence](../../../../../docs/qcom/sm88xx/oplus/bringup.md#optional-common-runtime-reconstruction) records the current ROM, adapter, map, and recipe identities.

## Independent identities

The guest/OEM identity is `0xa0220100`. The sequencer selector is `0xa01b0200`.
The selected CPR table at XBL `0x14915f30` permits chip revisions 1.0 through 1.255.
The board therefore selects virtual revision 1.0, not the earlier unsupported 2.0 value.
This selection preserves the OEM SoC identity `0xa022` and the independent signed sequencer selector.
The selected OEM metadata permits SoC version `0xa022` at file offset `0x2f130`.
The QTI list at `0x2f050` permits both `0xa022` and `0xa01b`.
The sequencer input carries signed TLV `0x8401 = 0xa01bffff` at file offset `0x10a4`.
Its admission consumer compares the configured selector's upper halfword with that signed value.
The virtual revision `0x0200` is policy. It is not a measured retail revision.
XBL's ChipInfo record at file offset `0xb7bf0` selects JTAG part `0x2fd`.

## Shared runtime and independent tables

`platforms/qcom/sm88xx/tme-runtime.lua` owns the common ROM, bridge, interrupt, timer, protection, and retained-register composition.
It accepts independent Lua data. It has no SoC branch and does not run Python.
`sm8845-runtime-data.lua` records the selected package's tables.
`sm8850-runtime-data.lua` preserves the existing SM8850 virtual geometry and tables.

| SM8845 source | Meaning |
| --- | --- |
| OEM `0x44688c`, four 32-byte records | PLL register banks |
| OEM `0x44685c` through `0x446888` | Nine boot branches |
| OEM `0x423156` and donor `0x222b9c` | Additional class-three branch pairs |
| Configuration `0x4555e4`, 55 32-byte records | Client controllers |
| Configuration `0x45b520`, 31 32-byte records | Permission descriptors |
| Configuration `0x45b900` | Permission descriptor count: 31 |
| Configuration `0x456dfc`, 30 words | DBGAR addresses |
| Configuration `0x456e74` | DBGAR count: 30, not an address |
| Configuration fault-mask list | 27 words at `0x05fc40ec` through `0x05fc4154` |
| OEM `0x444afc` | External-fault words `0x00c0040c`, `0x00c00410`, `0x00c00408` |
| OEM `0x41ef24` | Sequencer completion interrupt 23 |

Each permission record retains its own source address.
The resource arrays determine fixed-resource format, maximum resource index, address width, and alignment.
The controller at `0x310ce000` has a permission record but no corresponding DBGAR slot.
The tables must therefore never share an index-based join.
The virtual address translation adds `0x04000000` below `0x1ef00000`.
It subtracts `0x20000000` within `[0x20000000,0x2230c000)`.
Existing global, local, and RPMh protection controllers are reused by translated address.
External debug access remains zero. Permission configuration still follows the real OEM writes.

## Initial XBL dependency group

The selected CPH2771 ELF entry is `0x14825fa8`.
Its `0x14826094` instruction performs a 64-bit TCSR identity read.
That shared model's access-width support remains open.
The next recorded fault was the read at `0x14826270` from `0x0c2a9010`.
Static decoding of `0x14826268..0x14826290` confirms a bit-31 CDBGPWRUPREQ test.
The composition uses the existing SM8850 external-request policy, `0x80000000`.
This input does not enable or grant debug access.

The rest of the group uses corresponding CPH2771 consumers, not SM8850 offsets:

| CPH2771 address | Recovered dependency |
| --- | --- |
| `0x148f0fb8..0x148f1048` | Corrected feature rows at `0x221c21a0` and `0x221c2418` |
| `0x148d2400` | Raw feature rows at `0x221c2188` and `0x221c2190` |
| `0x148e9f88..0x148e9fa0` | SHRM IRAM `(0x31020000,0x28000)` and DMEM `(0x31048000,0x18000)` |
| `0x148e24e8`, `0x148e2f18`, `0x148e9c00` | IMEM references at `0x14680000` |
| `0x1484d194` | Initial clock configuration routine |
| `0x1485eb04` | Clear, enable, and vote-list consumer |
| `0x148f0748` | Six clear controls |
| `0x148f0760` | 120 enable controls |
| `0x148f0940` | Five address/mask vote records |
| `0x148f09ce..0x148f09d0` | Clear, enable, and vote counts: 6, 120, 5 |
| `0x1484d828`, table `0x148eef40` | 27 HW_CTL words |
| `0x1484d864`, table `0x148eee84` | 46 dynamic-divider controls |
| `0x1484d4b0..0x1484d7e8` | Scalar clock-policy fields retained by explicit masks |
| `0x148eeff0..0x148ef098` | AOSS PLL0, Pongo PLL1, and two LPASS PLL descriptors |
| `0x148ef3c8..0x148ef4c8` | Five AOSS root descriptors and independent selector records |
| `0x1484d2f8..0x1484d37c` | Ten four-bit AOSS divider fields |
| `0x1484d3e0..0x1484d42c` | Reference generators at `0x00ff0000` and `0x00fa0000` |
| `0x148ef9a0` | Peripheral rate table, byte-identical to the SM8850 table at `0x148f1e40` |
| `0x148f0118..0x148f0378` | Twenty peripheral branch/root/vote records |
| `0x14921e38` | UART properties, including pads 62 and 63 |
| `0x1491dfb0` | TLMM base `0x0f100000` and stride `0x1000` |
| `0x1485d864`, called at `0x1484d4ac` | Guest initialization of AOP clock records at `0x0c3e0000` |
| `0x1484d474` | Unused-domain control at `0x147170` |

The boot vote masks are `0x01bc317b`, `0xe03c0200`, `0x01f8500f`, `0xf00`, and `8`.
The last mask belongs to the independent word at `0x151028`.
A separate four-byte instance of the existing vote-bank model represents that sparse word.
The shared boot-clock builder receives explicit SM8845 arrays and masks.
It does not import SM8850 scalar, interconnect, disable, source-wiring, or dynamic-divider tables.
Unmodeled consumer demand uses the existing explicit virtual clock reference, not a claimed physical parent.

The peripheral group differs from SM8850.
SM8845 adds three QUPv3 roots at `0x11e640`, `0x11e77c`, and `0x11e8b8`.
It does not install SM8850's sixth QUPv3 root or fourth QUP wrapper.
The shared peripheral builder receives all twenty SM8845 records and their vote masks.
The common rate records establish the same XO, GPLL0-main, and GPLL0-even selector protocol.

The reset factory already supplies cold-zero IMEM and SHRM memory through the APSS, sequencer-DMA, and UFS routers.
The helper does not inject handoff records or preload UEFI, XBL, DDR structures, or a kernel.
Supporting rehost implementations do not authorize initial memory contents.
SM8850 DDR, DDR-control, PMIC, thermal, RPMh, and SOCCP tables are not applied to SM8845.

### AOSS, LPASS, reference, and REFGEN startup

Native run `bg346` passed the previous GCC PLL STATUS faults.
Its next failure was the APSS read at PC `0x1484d3c0`, address `0x0c2a900c`.
This register belongs to the AOSS startup cohort, not an independent ready input.
The complete cohort now belongs to `platforms/qcom/sm88xx/sm8845/xbl-aoss.lua`.
The clock helper invokes it once.
Its existing module names remain unchanged, and no parallel PLL or root state is introduced.

The selected routine loads `x21=0x0c2a80b8` at `0x1484d1ac..0x1484d1b4`.
Instruction `0x1484d3c0` adds displacement `0xf54`, which gives `0x0c2a900c`.
Instructions `0x1484d3d4..0x1484d3d8` set and retain bit zero.
The selected instructions establish a retained bit-zero request at this address.
The source-only field name and the physical PLL reference relationship remain unestablished.
The board retains that request while keeping the existing explicit 19.2 MHz virtual PLL reference.
It does not claim a recovered physical reference-clock netlist or synthesize lock.

The helper composes all four selected PLL banks:

| Source index | Source descriptor | Bank address | Existing model contract |
| --- | --- | --- | --- |
| 0 | `0x148eeff0` | `0x0c2a0000` | Taycan EKO T |
| 1 | `0x148ef028` | `0x0c2a1000` | Pongo |
| 2 | `0x148ef060` | `0x310a0000` | Taycan EKO T |
| 3 | `0x148ef098` | `0x310a1000` | Taycan EKO T |

The source register address is at descriptor offset `0x28`.
Source indices 0, 2, and 3 use callback table `0x148f1190`.
That table selects the existing normal Taycan configuration and direct-enable protocol.
The direct-enable routine at `0x1485fd8c` writes OPMODE at offset four, releases reset, polls MODE, and enables output.
Pongo uses its separate table at `0x148f1148`.
The two LPASS descriptors use consecutive banks in one authoritative two-bank object.
None of these instances inherits the GCC voted coarse-calibration capability.

The same cohort contains five RCGs:

| Root descriptor | Rate record | Command address | Selected source output | Mux | Advertised rate |
| --- | --- | --- | --- | --- | --- |
| `0x148ef3c8` | `0x148ef3a8` | `0x0c2a8084` | PLL0 main | 4 | 400 MHz |
| `0x148ef408` | `0x148ef3e8` | `0x0c2a80e8` | PLL1 calibration | 2 | 153.6 MHz |
| `0x148ef448` | `0x148ef428` | `0x0c2a80a0` | PLL1 main | 6 | 38.4 MHz |
| `0x148ef488` | `0x148ef468` | `0x0c2a818c` | PLL0 even | 1 | 300 MHz |
| `0x148ef4c8` | `0x148ef4a8` | `0x0c2a81a4` | PLL1 even | 3 | 19.2 MHz |

The Pongo calibration output keeps the explicit virtual reference ratio eight.
The 153.6 MHz record establishes the selected output requirement, not measured silicon calibration timing.
Each RCG still performs real shadow-register programming, source selection, and bounded acquisition.
A stopped or unlocked PLL cannot become an available root through these controls.

All thirteen retained policy words have explicit masks and cold-zero reset policy:

| Address | Mask | Guest store | Meaning established by the selected access |
| --- | --- | --- | --- |
| `0x0c2a901c` | `0x1` | `0x1484d1dc` | PLL0 software-mode selection |
| `0x0c2a9024` | `0x1` | `0x1484d1e8` | PLL1 software-mode selection |
| `0x0c2a80b8` | `0xf` | `0x1484d308` | Four-bit divider field |
| `0x0c2a80c0` | `0xf` | `0x1484d318` | Four-bit divider field |
| `0x0c2a80e4` | `0xf` | `0x1484d328` | Four-bit divider field |
| `0x0c2a80c8` | `0xf` | `0x1484d334` | Four-bit divider field |
| `0x0c2a80d0` | `0xf` | `0x1484d340` | Four-bit divider field |
| `0x0c2a80d8` | `0xf` | `0x1484d34c` | Four-bit divider field |
| `0x0c2a80e0` | `0xf` | `0x1484d358` | Four-bit divider field |
| `0x0c2a81d0` | `0xf` | `0x1484d364` | Four-bit divider field |
| `0x0c2a81dc` | `0xf` | `0x1484d370` | Four-bit divider field |
| `0x0c2a81ec` | `0xf` | `0x1484d37c` | Four-bit divider field |
| `0x0c2a900c` | `0x1` | `0x1484d3d8` | PLL0 reference selection |

The existing retained-control model rejects undeclared fields and offsets.
These controls do not simulate hardware mode selection or cycle-accurate divider timing.
Only the selected instructions supply the table above.

The following REFGEN sequence is also part of this stage:

1. `0x1484d3c4..0x1484d3d0` selects status pointers `0x00ff0044` and `0x00fa0044`.
2. `0x1484d3ec..0x1484d3f0` writes `0xf` at pointer displacement `0x64`, giving block offset `0xa8`.
3. `0x1484d404..0x1484d410` polls status bit 7 at block offset `0x44`.
4. `0x1484d420..0x1484d424` writes `8` at displacement `0x18`, giving block offset `0x5c`.
5. `0x1484d428..0x1484d42c` writes `3` at displacement `0x1c`, giving block offset `0x60`.

The existing REFGEN NXGEN model therefore serves blocks `0x00ff0000` and `0x00fa0000`.
The source-only REFGEN field names remain unestablished.
The selected instructions establish the offsets, values, and poll listed above.
The model does not copy that three-block topology to this board.
Each selected block declares fixed virtual power and a five-microsecond bounded calibration substitute.
It begins unready. The guest's tuning write starts calibration.
No host-written ready word, analog measurement, or early completion is supplied.

### Post-REFGEN boot-branch lists

Native run `bg347` passed the reference and REFGEN stage.
Its next failure was the read at `0x1485ec4c` from `0x1826ac`.
This is the first entry of a sixteen-branch disable cohort, not a request for another ready input.

At `0x1484d430`, the caller loads the first word of table `0x148eefac`.
It invokes the disable routine at `0x1484d434`.
The loop at `0x1484d448..0x1484d468` consumes the remaining fifteen entries.
Its bound is the immediate count `0x10`.
The disable routine `0x1485ec2c..0x1485ec5c` reads each CBCR, clears bit zero, and writes it back.
It performs no halt poll, source lookup, or vote-register transaction.
The exact selected list is:

| Table word | CBCR |
| --- | --- |
| `0x148eefac` | `0x1826ac` |
| `0x148eefb0` | `0x182040` |
| `0x148eefb4` | `0x182054` |
| `0x148eefb8` | `0x1827f4` |
| `0x148eefbc` | `0x1748fc` |
| `0x148eefc0` | `0x174920` |
| `0x148eefc4` | `0x174940` |
| `0x148eefc8` | `0x174010` |
| `0x148eefcc` | `0x17400c` |
| `0x148eefd0` | `0x174018` |
| `0x148eefd4` | `0x174014` |
| `0x148eefd8` | `0x174908` |
| `0x148eefdc` | `0x14d004` |
| `0x148eefe0` | `0x148010` |
| `0x148eefe4` | `0x1827fc` |
| `0x148eefe8` | `0x182808` |

The word at `0x148eefec` is outside the bounded list.
No sentinel or adjacent data word becomes another branch.
`sm8845-xbl-clocks.lua` now supplies the full disable list to the shared boot-clock builder.
The builder reuses any existing branch state by register address and provides the established APSS/CPUCP routed views.
The controls at `0x1827fc` and `0x182808` previously had only scalar policy retention.
They now use real branch objects while retaining their separately observed bit-20 policy field.
No overlapping scalar object remains at either address.

This list is distinct from the 120 branch enables at `0x148f0760`, the 27 HW_CTL entries, and the five vote records.
Their counts and masks remain unchanged.
The twenty peripheral descriptors still provide their exact parent RCGs and vote masks.
The two domain branch descriptors still connect `0x144018 -> 0x14402c` and `0x144024 -> 0x144158`.
The selected disable-list format supplies no corresponding parent or vote fields.
Adjacency of `0x1826ac` to root `0x1826bc` does not establish a physical parent.
The helper therefore retains the declared virtual-reference policy for otherwise unmodeled consumers.
It does not invent an independent vote or claim a recovered silicon connection.

The public reference is [OnePlus SM8845 `gcc-canoe.c`](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8845/blob/2774f857d6e77443de95defbf1199e0afdc14d9a/drivers/clk/qcom/gcc-canoe.c).
The inspected file has SHA-256 `b9e22de60cce6370cd44ae1ee82d5cf72cd3dee2b62e24f54ad64bc383d3c842`.
It does not declare these sixteen internal CBCR offsets.
That source does not authorize replacement of the selected sixteen entries or guessed parent wiring.
Existing clock models still derive halt state from their requests and declared upstream clock.
No detached ready or unconditional running word is installed.



### Complete domain-root cohort

A later native SM8845 fault at PC `0x1485e938` reads command word `0x1826bc`.
This result comes from `build/private/run-sm8845-common-runtime1/system.log`, not a launch for this evidence.
The selected consumer `0x1485e910..0x1485ea50` implements the existing RCG command/configuration protocol.
It writes mux and divider fields, optional M/N/D words, and command UPDATE.
It polls the real UPDATE bit with a bounded retry count.
An isolated ready word would omit this protocol.

The pointer at `0x148f0978` selects table `0x148f03d8`, with 27 domain-root descriptors.
Five descriptors select the already composed AOSS roots.
Two descriptors select the separate packed-root group.
The other twenty descriptors select ordinary GCC RCGs.
`sm8845-xbl-domain-roots.lua` configures that complete GCC cohort.
It reuses nineteen reset objects by command address and adds only the absent root at `0x1826bc`.
The new root has one state machine and three routed views, for APSS, CPUCP, and the shared clock router.

| Descriptor | RCG command | Main selector | GPLL bank |
| --- | --- | --- | --- |
| `0x148ef548` | `0x18206c` | 1 | 0 |
| `0x148ef508` | `0x1826bc` | 5 | 4 |
| `0x148ef588` | `0x141024` | 2 | 7 |
| `0x148ef5c8` | `0x1a200c` | 1 | 0 |
| `0x148ef5e8` | `0x1742d4` | 1 | 0 |
| `0x148ef608` | `0x1741a8` | 1 | 0 |
| `0x148ef648` | `0x17452c` | 1 | 0 |
| `0x148ef6a0` | `0x174400` | 1 | 0 |
| `0x148ef6c0` | `0x174658` | 5 | 4 |
| `0x148ef6e0` | `0x148298` | 1 | 0 |
| `0x148ef720` | `0x1484f0` | 1 | 0 |
| `0x148ef740` | `0x148030` | 5 | 4 |
| `0x148ef760` | `0x148168` | 1 | 0 |
| `0x148ef880` | `0x14402c` | See six-source map below | See below |
| `0x148ef8c0` | `0x11c0c8` | 4 | 1 |
| `0x148ef8e0` | `0x11c1f8` | 4 | 1 |
| `0x148ef900` | `0x183030` | 4 | 1 |
| `0x148efe78` | `0x144158` | 5 | 4 |
| `0x148efe98` | `0x176030` | 1 | 0 |
| `0x148efeb8` | `0x176160` | 5 | 4 |

Each descriptor's first pointer selects its rate array.
Its byte at offset `0x19` supplies the entry count.
The selectors at rate offset `8` and source indices at offset `0x10` establish each connection.
The rate consumer advances by 28 bytes at `0x1485e6b0` and `0x1485e770`.
The source pointer at `0x148f0980` selects the 17-entry source table at `0x148f04b0`.
Each selected source descriptor supplies its PLL address at offset `0x28`.

The failing root's rate at `0x148ef4e8` selects mux 5 and source 8.
Source 8 selects descriptor `0x148ef1b0`, whose register bank starts at `0x104000`.
The existing GCC model therefore supplies bank 4, not an unconditional clock.
That same rate array also belongs to `0x174658`, `0x148030`, and `0x176160`.

The nine rates at `0x148ef780..0x148ef860` give root `0x14402c` six main-output connections:
`1 -> GPLL10`, `2 -> GPLL6`, `3 -> GPLL5`, `4 -> GPLL1`, `5 -> GPLL4`, and `7 -> GPLL2`.
Selector zero remains XO.
The primary connection is selector 1, bank 10. The secondary connection is selector 2, bank 6.
The optional generic `main_sources='3 5;4 1;5 4;7 2'` parameter adds the remaining four main outputs.
These mappings do not replace or duplicate the existing enabled selectors.
That model extension must exist before all nine rates can execute.
No SoC branch or additional registered model is required.
All other GCC roots in this cohort use the established single-main-source parameters.
Unused inherited selector connections are disabled, not silently retained from the compatibility board.

Branch descriptors `0x148f00d8` and `0x148f0398` supply two additional explicit connections.
CBCR `0x144018` uses root `0x14402c`, and CBCR `0x144024` uses root `0x144158`.
These branches now depend on their selected root state.
Other unmodeled consumers retain the declared virtual-reference policy.

### Voted PLL coarse-calibration cohort

The next native run passed root `0x1826bc`.
It then stopped at PC `0x1486002c`, reading PLL7 address `0x10700c`.
The existing GCC router already forwards that address to the shared PLL7 state.
The existing PLL model rejected the register offset, rather than lacking an address mapping.

Source index 11 selects descriptor `0x148ef258`, with PLL base `0x107000`.
Its method table is `0x148f11d8`.
The table differs from the ordinary Taycan table at `0x148f1190` in exactly one callback:
configuration uses wrapper `0x1485fff8` instead of `0x1485fed8`.
All eight other callbacks are identical.
The wrapper first calls ordinary configuration, then enables a real vote and waits for the existing PLL state.
It does not implement a second PLL register layout.

| Source index | Descriptor | Register base | Shared GCC bank |
| --- | --- | --- | --- |
| 5 | `0x148ef108` | `0x101000` | 1 |
| 6 | `0x148ef140` | `0x10a000` | 10 |
| 7 | `0x148ef178` | `0x102000` | 2 |
| 8 | `0x148ef1b0` | `0x104000` | 4 |
| 9 | `0x148ef1e8` | `0x105000` | 5 |
| 10 | `0x148ef220` | `0x106000` | 6 |
| 11 | `0x148ef258` | `0x107000` | 7 |
| 12 | `0x148ef290` | `0x108000` | 8 |
| 13 | `0x148ef2c8` | `0x109000` | 9 |
| 14 | `0x148ef300` | `0x06e00000` | External source, not a GCC-bank alias |

These descriptors share callback table `0x148f11d8`.
The nine shared-bank indices give capability mask `0x7f6`.
The external source is not selected by any of the twenty ordinary domain roots above.
The later source-initialization loop selects it directly, as described in the external-PLL section below.
GPLL0, AOSS PLL0, and LPASS retain the ordinary callback and need no added status aperture.

The selected instructions establish the complete transfer:

1. `0x14860014` calls ordinary configuration at `0x1485fed8`.
2. `0x1486001c` saves the current vote register.
3. `0x14860024` calls vote enable at `0x1485ff04`. This polls MODE with any-bit mask `0x80000004`, not an all-bits lock test.
4. `0x1486002c` reads STATUS at PLL offset `0x0c`.
5. `0x14860038..0x14860040` extracts STATUS bits `[17:13]` and moves them to CONFIG_CTL bits `[16:12]`.
6. `0x1486003c..0x14860050` sets CONFIG_CTL bit 10, then ORs the extracted coarse code.
7. `0x14860054..0x1486005c` clears TEST_CTL bit 8.
8. `0x14860060` restores the saved vote register.

Vote enable constructs mask `0x80000004` at `0x1485ff10` and `0x1485ff18`.
Its call at `0x1485ff2c` enters the bounded poll at `0x1485f980`.
The poll reads MODE at `0x1485f99c`, executes `tst w8,w19` at `0x1485f9a4`, and returns on `b.ne` at `0x1485f9a8`.
RESET_N bit 2 alone can therefore end the poll before LOCK_DET bit 31 asserts.
The earlier assumption that this path waits for full PLL lock was incorrect.

The instrumented native run `bg341` recorded these STATUS-read prerequisites:

| Bank | Locked | TEST_CTL | CONFIG_CTL | MODE | Shared votes |
| --- | --- | --- | --- | --- | --- |
| GPLL7 | 1 | `0x500` | `0x25c400e7` | `0x4` | `0x91` |
| GPLL1 | 0 | `0x500` | `0x25c400e7` | `0x4` | `0x93` |

Both reads followed actual vote-enabled operation and retained TEST_CTL.STATUS_EN.
The GPLL1 result disproves full lock as a prerequisite for this coarse-code transfer.

The selected wrapper uses the instruction sequence above.
Its source-only function and field names remain unestablished.
The selected binary supplies the implemented masks.
The pinned [Linux register table](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/clk-alpha-pll.c)
independently places Taycan STATUS at `0x0c`.

The board declares `gcc_plls.coarse_calibration_mask=0x7f6` on the existing shared bank.
It also declares `gcc_plls.virtual_coarse_calibration_code=0`, within the generic five-bit range `0..31`.
The code is virtual policy, not a measured analog result or a ready bit.
A STATUS read requires a declared bank, actual PLL running state, and TEST_CTL.STATUS_EN.
The coarse code becomes available immediately under that explicit virtual policy.
It does not wait for full PLL lock and does not claim measured analog calibration timing.
Clock outputs and MODE lock bits still require the complete five-microsecond acquisition interval.
The policy neither forces lock nor reduces that interval to zero.
Writes and undeclared-bank STATUS reads remain unsupported.
The generic model extension remains open.
No detached memory word, duplicate PLL, or SoC condition belongs in that extension.

### Directly initialized external PLL

Native run `bg350` passed the sixteen disable controls.
It next read `0x06e00000` at PC `0x1485fe1c`.
That instruction belongs to the Taycan IsEnabled callback, not a detached readiness register.
`sm8845-xbl-aoss.lua` now supplies one `xbl_external_pll` instance of the existing generic PLL model.
It owns only `[0x06e00000,0x06e00038)`.
It does not replace GPLL0 or add a second global vote-register owner.

The selected descriptor is source index 14 in table `0x148f04b0`:

| Descriptor field | Coordinate | Value or reference |
| --- | --- | --- |
| Default configuration array | `0x148ef300` | `0x148da450`, one record |
| Calibration configuration array | `0x148ef308` | `0x148da2a0`, one record |
| Register settings array | `0x148ef310` | `0x148da248`, one record |
| Vote register and mask | `0x148ef318` | `0x152020`, mask `0x1` |
| Callback table | `0x148ef320` | `0x148f11d8` |
| PLL register base | `0x148ef328` | `0x06e00000` |
| Flags and configuration mask | `0x148ef32c` | `nFlags=0`, `nConfigMask=0x61` |

The default record supplies alpha zero, predivider one, even divider two, odd divider five, and L=32.
The calibration record supplies L=42.
The settings record supplies USER_CTL `0x04000008` and TEST_CTL `0x500`.
The host does not preload these register contents.
The actual guest configuration routines write them.

No rate in the twenty ordinary domain-root arrays selects source index 14.
The selected caller is instead `Clock_InitImage`'s complete source-initialization loop:

1. `0x1484d654` reads the source count, 17.
2. `0x1484d66c..0x1484d670` selects each source pointer from table `0x148f04b0`.
3. `0x1484d674..0x1484d678` skips only descriptors with flag bit 10.
4. `0x1484d67c..0x1484d680` calls source initialization `0x1485e294` with no explicit frequency record.
5. The loop advances through all seventeen descriptors at `0x1484d68c..0x1484d694`.

Source 14 has no skip flag and has a default configuration record.
It therefore runs the voted configuration wrapper even without an RCG consumer.
The initial IsEnabled callback at `0x1485fe10..0x1485fe28` tests MODE bits 31 and 2.
It returns false from the cold-zero PLL state.

The vote register's presence alone is not the complete proof of voted operation.
The selected code establishes the following writes:

1. `0x1485e2c0` saves the descriptor's vote address in `w21`.
2. `0x1485e350..0x1485e354` enables the main output.
3. Configuration-mask bits 5 and 6 take `0x1485e3c8` and `0x1485e3dc`, enabling even and odd outputs.
4. With no explicit configuration argument, `0x1485e450..0x1485e4e8` selects the default record.
5. `0x1485e4ec` branches to `0x1485e380` because the saved vote address is nonzero.
6. `0x1485e380..0x1485e388` dispatches ConfigSource operation 1 through `0x1485f8fc`.
7. The callback's operation-1 case at `0x1485fe98..0x1485fec4` sets USER_CTL bit 25.
8. `0x1485e49c..0x1485e4b0` programs the calibration L field and invokes the voted configuration wrapper.

The wrapper at `0x1485fff8` calls normal configuration before EnableVote.
Normal configuration saves USER_CTL at `0x14860084`.
Its bit-25 test at `0x148600f4` therefore sets MODE.RESET_N at `0x14860104`.
This path does not write OPMODE=1 for direct operation.
EnableVote at `0x1485ff04` then writes mask one into the existing `0x152020` register.
The wrapper reads and transfers coarse STATUS, disables STATUS_EN, and restores the original global vote word.
Its any-bit MODE poll and virtual coarse-code policy are the same bounded contract documented above.

Mask `0x61` carries the even and odd output enables in bits five and six.
The selected writes at `0x1485e3c8` and `0x1485e3dc` establish those two uses.
The source-only meaning of bit zero of this mask remains unestablished.

The external instance requests `external_vote_path='gcc_plls'` and `external_vote_mask=1`.
This generic capability must observe the existing owner's live aggregate vote and change event.
It must not duplicate the global register, manufacture a vote, or alias the external PLL's state to GPLL0.
The external instance also declares `coarse_calibration_mask=1` and `virtual_coarse_calibration_code=0`.
Its cold state remains unprogrammed and unlocked.
Guest configuration, actual reset release, the live selected vote, and acquisition still control running state and clock availability.
Odd-output selection is retained by the existing register contract. No unobserved odd-output consumer or frequency is invented.
The generic shared-vote observer implementation remains open.

### Post-source scalar and table controls

The latest native run passed all seventeen source-initialization entries, including the external PLL with its shared-vote observer.
It then failed at APSS PC `0x1484d6d4`, writing `0x183160`, with `TLM_COMMAND_ERROR_RESPONSE`.
This is execution evidence from the prior run, not a launch of the changes below.

The selected instructions establish the failing address without an SM8850 offset assumption:

1. `0x1484d484` and `0x1484d498` construct `x24=0x183188`.
2. `0x1484d6cc` reads `[x24-0x28]`, which is `0x183160`.
3. `0x1484d6d0` ORs bit 20 into the returned word.
4. `0x1484d6d4` stores that word to the same address.

The existing branch object already owns this register because the selected HW_CTL table includes it.
Its missing `retained_mask=0x100000` caused the strict write rejection.
Replacing it with a scalar object would lose its independently selected HW_CTL behavior.
The correction instead adds bit-20 retention to all five related branches:

| Guest store | Register | Added retained mask |
| --- | --- | --- |
| `0x1484d6d4` | `0x183160` | `0x100000` |
| `0x1484d6e0` | `0x183164` | `0x100000` |
| `0x1484d6ec` | `0x183168` | `0x100000` |
| `0x1484d6f8` | `0x183170` | `0x100000` |
| `0x1484d704` | `0x183178` | `0x100000` |

All five occur in the selected 27-word HW_CTL table at `0x148eef40`.
The final three also occur in the selected enable table.
The builder reuses their existing branch instances and keeps their gate, source, and HW_CTL state.
Retained bit 20 has no readiness effect in `QcomClockBranch`.
Neither the selected accesses nor the public driver establish a physical function name for bit 20 at these internal offsets.

The complete following scalar cohort ends at `0x1484d7fc`, before the conditional table consumers.
The table records each scalar store after the source loop:

| Guest store | Register | Operation or retained field |
| --- | --- | --- |
| `0x1484d6c8` | `0x148000` | OR `0x100000`, existing branch retention |
| `0x1484d6d4..0x1484d704` | Five branches listed above | OR `0x100000` |
| `0x1484d710` | `0x110078` | OR `0x100000`, existing branch retention |
| `0x1484d71c` | `0x19d068` | OR `0x100000`, existing branch retention |
| `0x1484d728` | `0x199064` | OR `0x100000`, existing branch retention |
| `0x1484d73c` | `0x173018` | OR `0x33`, new retained policy |
| `0x1484d74c` | `0x183024` | OR `0x200000`, existing policy |
| `0x1484d758` | `0x199044` | OR `0x200000`, existing policy |
| `0x1484d768` | `0x11c080` | OR `0x200000`, existing policy |
| `0x1484d774` | `0x11c0a8` | OR `0x200000`, existing policy |
| `0x1484d780` | `0x19d044` | OR `0x200000`, existing policy |
| `0x1484d794` | `0x110084` | OR `0x200000`, existing policy |
| `0x1484d7a0` | `0x182068` | OR `0x100000`, existing policy |
| `0x1484d7ac` | `0x182044` | OR `0x100000`, existing policy |
| `0x1484d7b8` | `0x18205c` | OR `0x100000`, existing policy |
| `0x1484d7c4` | `0x18204c` | OR `0x100000`, existing policy |
| `0x1484d7d0` | `0x1827fc` | OR `0x100000`, existing disable-branch retention |
| `0x1484d7dc` | `0x182808` | OR `0x100000`, existing disable-branch retention |
| `0x1484d7e8` | `0x11c350` | OR `0x200000`, existing branch retention |
| `0x1484d7f4` | `0x11828c` | Clear bits `[3:0]`, new retained mask `0xf` |
| `0x1484d7f8` | `0x143144` | Store `0x21`, new retained mask `0x21` |
| `0x1484d7fc` | `0x03f404a8` | Store `0x10064`, new retained mask `0x10064` |

The final two addresses follow explicit register construction, not adjacent table words.
`x19=0x144004`, so the subtraction at `0x1484d75c` gives `0x143144`.
`0x1484d734..0x1484d738` constructs `0x03f404a8`.
The masks for these two words admit only the bits in the selected literal writes.
They do not claim complete silicon register masks or accept arbitrary future field values.
All four new scalar words use the existing retained-control model with cold-zero state.
Unknown fields and offsets remain errors. No register becomes a status or completion input.

The pinned [SM8845 GCC driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8845/blob/2774f857d6e77443de95defbf1199e0afdc14d9a/drivers/clk/qcom/gcc-canoe.c#L1807-L1819)
identifies offset `0x1828c` as `gcc_qupv3_wrap1_s2_clk_src`, a four-bit divider at shift zero.
Its parent is `gcc_qupv3_wrap1_qspi_ref_clk_src`, not a direct GPLL output.
With GCC base `0x100000`, this is the selected absolute address `0x11828c`.
The current composition therefore retains the exact divider field without using the generic direct-PLL divider with an incorrect parent.
Divider-output wiring and timing remain unsupported.
The public driver does not declare the three other new scalar offsets or the five internal bit-20 fields.
Their selected guest accesses establish retention only, not power, memory-collapse, or timeout behavior.

The following bounded table consumers remain intact:

- `0x1484d800..0x1484d840` checks byte `0x148eef3c` and the result of `0x1484c7f8` before the HW_CTL loop.
- `0x1484d828..0x1484d840` consumes exactly 27 words from `0x148eef40`, ORing bit 1 into each existing register.
- `0x1484d844..0x1484d874` checks byte `0x148eee80`, then consumes exactly 46 words from `0x148eee84`, storing one to each.
- Both selected configuration bytes are one. The separate HW_CTL predicate remains guest-controlled.
- `0x1484d878..0x1484d898` returns from the routine. No adjacent table entry extends either list.

Static decoding confirms both complete tables match the existing SM8845 Lua arrays.
No source, vote, AOSS root, or domain-root change is required for this cohort.
No generic model or duplicate register owner is added.
The change does not import SM8850 scalar controls or infer undocumented parent clocks from register adjacency.

## Cold EUD initialization group

The retained canonical run `build/private/run-sm8845-common-runtime2/system.log` stops at APSS PC `0x1489962c`, writing `0x088e1000`.
This is the first serial-byte write, not a USB controller start or an authentication decision.
Native `IMAGES/xbl_sc.elf` disassembly supplies the complete selected group.
The image hash is listed in the input table above.

Caller `0x1488d144..0x1488d204` reads the serial at `0x221c8610`, writes four bytes, and checks EUD enable bit zero.
The disconnected path enables `usb30_prim`, configures EUD tuning, checks `DCTL.RUN_STOP`, stores permission two, and disables the group.
The guest controls this path. No branch or source image instruction changes.

| Consumer | Evidence and modeled operation |
| --- | --- |
| `0x148995f8..0x14899648` | Four low-first serial bytes at `0x88e1000 + 4*i`, retained with mask `0xff` |
| `0x14899470` | Enable bit zero at `0x88e2000`, with enumeration read `0x88e0240` only on the active path |
| `0x1489b264..0x1489b308` | Detect `0xc278000`, extra tune `0x88e102c`, 17 paired tune entries, then PBUS `0x88e1024` |
| `0x148994f0(0,1)` | Bit-zero reference votes `0x1fd504c` and `0x1fd505c`, not ready-status registers |
| `0x148994b4` | DCTL `0xa60c704` bit 31, clear under explicit disconnected-session policy |
| `0x1488d1c0..0x1488d1d8` | Permission two at `0x14914ae4`, then group disable |

The PHY tuning arrays start at guest addresses `0x148e0a95` and `0x148e0aa6`.
Their 34 bytes stay in guest storage. The model retains only the guest's MMIO writes.
No request write changes EUD enable or enumeration status.

The clock descriptor at `0x148f0720` names `usb30_prim` through string `0x148da7cf`.
Its nine 32-byte nodes start at `0x148f0c58`.
They select `0x139094`, `0x139030`, `0x139018`, `0x13902c`, `0x139028`, `0x139068`, `0x13906c`, `0x139070`, and `0x1fd504c`.
The second branch differs from SM8850's `0x139090`. The composition does not substitute that address.
Selected node `0x139070` has flag `0x20`, which skips clock-off polling when no PIPE clock exists.

The first three nodes share root descriptor `0x148eff78`, command address `0x139034`.
Its rate entry at `0x148eff58` requests 200 MHz from selector 1 and GPLL0 main, with divider three.
The reset platform's actual owner is `cpucp_clock_root_20`, with APSS/CPUCP aliases and the required GPLL0 connection.
The shared helper discovers that owner by address and uses the existing RCG state.
It does not assume that module name or create another root.

GDSC `0x139004` comes from table `0x148f0d80`. BCR is `0x139000`.
Enable consumer `0x1485f010` powers the GDSC before it enables all nine branch nodes.
GDSC consumer `0x1485ec60` clears collapse bit zero and waits for power-on bit 31.
Branch consumer `0x1485ed10` sets bit zero and polls clock-off bit 31 unless HWCG bit 1 or node flag `0x20` skips it.
Disable consumer `0x1485f174` clears the branch enables, then sets GDSC collapse through `0x1485ecdc`.
This cold sequence does not request BCR reset.

Both runtime factories now use `platforms/qcom/sm88xx/xbl-eud.lua` with explicit selected branch data.
The SM8845 boot policy has no owner for either reference vote.
The helper creates one retained owner per missing vote, with routed APSS/CPUCP aliases.
The SM8850 policy already owns both votes, and those objects remain unchanged.
All new targets retain overlap checks.
The group disable clears its ninth node's vote at `0x1fd504c`. The separate `0x1fd505c` vote remains retained.

Permitted substitutes are cold-zero retained controls, disconnected host/session policy, virtual controller-register power, and virtual references for non-PIPE consumers.
Master branches consume the existing RCG/PLL state. PIPE has no clock while disconnected.
The GDSC acknowledges virtual register-domain power, not physical PHY lock.
Reset, attach, controller start, enumeration writes, and USB service operations remain unsupported.
No authentication, rollback, AVB, or lock policy changes.

See [the complete EUD comparison](../../../eud/evidence.md#sm8845-cph2771-comparison) for all selected addresses and public source limits.
This change has static firmware evidence only. Builds, both native runs, and retained logs remain pending.

## XBL protection aliases and connected SPMI inventory

The reported actual replay reached `XBL_AC_CFG` load start after the EUD group.
Its next first fault was APSS PC `0x148581e8`, reading `0x0152000c`.
This change does not claim another firmware replay.

### Complete protection cohort

The selected CPH2771 XBL contains configuration roots at `0x148e13b8` and `0x148e13d0`.
Their `+0x10` pointers select `0x14920078` and `0x14920250`.
Both structures select resource table `0x14920428` at `+0x38` and count pointer `0x148e1580` at `+0x40`.
The count is 31.
Each record occupies `0x28` bytes.
The system base, configuration base, and identifier occupy `+8`, `+0xc`, and `+0x10`.
Both bases agree in all records.
`platforms/qcom/sm88xx/sm8845/xbl-protection.lua` records all identifiers and bases in source order.
Identifier `0x23` is the record at `0x14920540`, with base `0x01520000`.

Reader `0x148581d8..0x14858230` reads revision `+0xc` and configuration words `+0`, `+4`, and `+8`.
Its masks are `0x03ffffff & 0xffff007f`, `0x3f3f3f3f`, and `0x3f1f`.
Code `0x14858858..0x148588a8` uses stride `0x40`, ownership `0x1030`, and permissions `0x1018/0x101c`.
These contracts match the existing controller.
The fault therefore requires a configuration alias, not another permission array.

The shared helper is `platforms/qcom/sm88xx/xbl-protection.lua`.
Both board wrappers supply independent cohorts.
The SM8845 cohort omits SM8850 identifiers `0xf6` and `0xf8`.
The helper finds each controller by translated address, never by another table's index.
Every SM8845 member already exists in the runtime composition.
This includes `0x67` at `0x01d98000` and `0xb5` at `0x17e08000`.
SM8845 adds no substitute controller or changed geometry.
Aliases preserve the existing region state, fixed-resource type, revision, permissions, locks, client connections, and permission-change events.

The helper splits the packed SPMI aperture around `[0x0c41c000,0x0c41e000)`.
The two remaining views preserve the arbiter's offsets and share its target.
The existing TME-view XPU owns the excluded range.
The helper rejects overlapping APSS mappings.
It does not alter authentication, rollback, AVB, or requester policy.

### CPH2771 SPMI and PMIC evidence

The package's `IMAGES/vendor_boot.img` SHA-256 is `1736dde58f5d28a4d31cfeefd7e97316fc0cfd48ccfce87b21eab9a5d4ef2d72`.
Its Alor DTB starts at file offset `0x1034000` and declares SoC ID `0x2ad`.
`IMAGES/dtbo.img` SHA-256 is `2dd0010eb6bdf8ddcca979251bb26bb14e5e9304c1bdeada2fe3c906afb18b53`.
Its four overlays start at file offsets `0xa0`, `0x124d33`, `0x2497af`, and `0x36bf3a`.
All four contain the same PMIC topology.
The last overlay names the EU-US board.
These offsets identify data in package images, not XBL physical addresses.

XBL transport descriptors `0x148f1520` and `0x148f1578` share base `0x0c400000` and debug base `0x11314000`.
Their packed bus/APID fields are `0x07ff0000` and `0x01ff0001`.
Bus one starts at APID `0x800`, with arbiter offset `0x16000`.
Bus zero uses arbiter offset `0xb000`.
These fields match the existing packed arbiter contract.
The device tree independently declares bus windows at `0x0c426000` and `0x0c437000`.
The composition uses the existing synchronous virtual transport, empty initial routes, disabled initial buses, and executing owner zero.
Only guest initialization establishes routes and enables buses.
No transport command fabricates authentication completion.

Overlay `__fixups__.spmi0_bus` targets fragments zero and fourteen.
Fragment zero declares PMK, PM8550, PM8550VE, PMR735D, and PM8010 nodes.
It disables PM8550VE nodes at SIDs two and nine.
Fragment fourteen adds PMD802x at SID four and PM7550BA at SID seven.
No selected overlay populates the second hardware bus.
The debug GLINK tree is not a second physical arbiter inventory.

| Bus/SID | Selected model | Model ID | Evidence and limit |
| --- | --- | --- | --- |
| 0/0 | PMK8850 | `0x5c` | Alor base debug node `pmk8850@0`. The overlay retains the older `pmk8550` name. |
| 0/1 | PM8550 | `0x49` | Enabled overlay node `pm8550@1`. |
| 0/3, 0/5, 0/6, 0/8 | PM8550VE | `0x46` | Enabled overlay nodes. |
| 0/4 | PMD8028 family substitute | `0x57` | Overlay says `pmd802x`, not an exact suffix. This selection is virtual policy. |
| 0/7 | PM7550BA | `0x4c` | Fragment fourteen and matching debug node. |
| 0/10 | PMR735D | `0x48` | Overlay node `pmr735d@a`. |
| 0/12, 0/13 | PM8010 | `0x41` | Separate overlay nodes at `c` and `d`. |

Model-number authority is public Linux [socinfo.c](https://github.com/torvalds/linux/blob/master/drivers/soc/qcom/socinfo.c).
The [SPMI PMIC header](https://github.com/torvalds/linux/blob/master/include/soc/qcom/qcom-spmi-pmic.h) independently identifies most subtype constants.
The [PM7550BA device tree](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/pm7550ba.dtsi) confirms that device's SID-seven convention.
The CPH2771 package, not SM8850 or qcom-rehost, supplies the selected topology.
The conflicting PMK node names do not establish a measured retail identity.
The composition explicitly selects the newer base-tree name.
Every declared primary uses virtual revision 1.0, variant zero, and one SID.
These values are permitted substitutes, not physical revision or SID-span measurements.
Undeclared SIDs return transport FAILURE.

Selector `0x14884724` indexes pointer table `0x148ddc20` by model minus `0x35`.
The selected first-bank masks are:

| Model | Mask address |
| --- | --- |
| `0x5c` | `0x148f18bc` |
| `0x49` | `0x148f191c` |
| `0x46` | `0x148f199c` |
| `0x41` | `0x148f19fc` |
| `0x57` | `0x148f1a1c` |
| `0x4c` | `0x148f1a5c` |
| `0x48` | `0x148f1adc` |

Each bank mask contains 256 bits.
The shared masks for `0x5c`, `0x41`, and `0x48` describe the same ranges.
The board supplies the other masks explicitly.
A discovery bit does not establish a peripheral type or analog behavior.

### Permitted virtual peripheral behavior

Only PMK bus zero, SID zero receives the selected PON peripheral at `0x0800`.
XBL `0x1483112c` constructs that address.
No PON group is inferred on every populated SID.
The existing PON model retains bounded reset/spare controls without a physical power transition.

CPH2771 `0x14882da0..0x14882df4` returns 72 SDAM banks for models `0x4a` and `0x5c`.
The loop at `0x14882efc..0x14882fac` probes each bank and requires type `0x2e`.
Code `0x14882c68` selects register layout `0x148f1808`, with stride `0x100` and size byte `0x44`.
The composition declares 72 functional PMK SDAM banks at `0x70..0xb7`.
This virtual capacity follows the selected consumer, not a physical reset dump.
The SDAM model provides retained storage, size byte four, and an empty history pointer at `0x7446`.
XBL reads history control `0x726b` at `0x14831274`.
Calls `0x148313dc..0x14831424` select `0x7446`, `0x7445`, `0x7447`, and `0x744b`.
The PM7550BA DT's SDAM banks remain absent in this virtual profile.
The selected XBL SDAM count function does not support model `0x4c`.

The PMK retains mask-three control `0x9a5a` and mask-`0x80` control `0xa245`.
PSI version bytes `0x0150/0x0151` retain guest writes on every populated SID.
CPH2771 callers include `0x14839ce8`, `0x14839ab8`, and `0x1483ac94`.
Raw controls can restrict an existing retained SDAM byte's mask. They cannot widen it.
The profile also declares PBS memory, exact PSI request bytes, interrupt controls, and explicit virtual input policies.
Their complete contracts and source coordinates are in `models/qcom/spmi/README.md`.
Declared GPIO, regulator, clock, and RTC services replace the corresponding discovery absences.
Other mask-selected peripherals expose only absent revision bytes.
No generic MISC revision, ADC mailbox, PBS execution, or temperature result is supplied.
Regulator readiness derives from accepted enable requests.
The explicit exploratory policy retains unknown ordinary configuration and logs each access.
Unsupported owned operations still fail. See `models/qcom/spmi/README.md` for the exact boundary.
No board checks or board inventory enter the C++ models.

## SM8850 opcode-six completion investigation

This section concerns the common adapter and map, not a measured SM8845 execution.
The inspected CPH2749 `signed_firmware_soc_view.elf` has SHA-256 `4258fc24317e3467b39144ab72a46d85e4494c80bddc37b14de16177ff0c4a19`.
Its load segments at `0x2041c000..0x20449fff` supply the OEM code and data.
The addresses below use the TME view, which subtracts `0x20000000`.
Instruction decoding supplied the static evidence. No firmware execution ran for this investigation.

### Receive, dispatch, and result publication

The observed request is sequence one, opcode six, argument `0x2040c000`, and size `0xa4`.
The run observed pending state one and result `0xffffffff` in response slot `0x02000010`.
These fields do not establish an authentication failure.

| OEM address | Established operation |
| --- | --- |
| `0x41c3e6..0x41c3f4` | Query milestone five through API `0xec`. Call receiver `0x42b856` while the milestone remains clear. |
| `0x41e43c..0x41e456` | Query the requested milestone and call the same receiver while it remains clear. |
| `0x42b8dc..0x42b8ec` | Call API `0x9ec` with four output pointers: opcode, argument, size, and identifier. |
| `0x42b91c..0x42b92e` | Call API `0x9cc` to validate the argument range before copying the request. |
| `0x42ba04..0x42ba22` | Pass opcode, identifier, input descriptor, and output descriptor to dispatcher `0x42b7ca`. |
| `0x42b7d6..0x42b834` | Search 21 opcode/handler pairs at `0x444418`. Apply the identifier gate, then call the selected handler. |
| `0x444418..0x44441f` | Opcode six selects handler `0x42b170`. |
| `0x42ba3e..0x42ba4a` | Call API `0x9f8` with the received identifier and the service result. |
| `0x42ba86..0x42ba92` | Poll API `0x9f0` while its result equals fourteen. Require zero after the peer consumes the response. |

The dispatcher stores its gate at byte `0x44815a` and word `0x44815c`.
When the byte is nonzero, an identifier no greater than the stored word does not call the handler.
The dispatcher returns service result two in that case.
That result is distinct from response state two.
The outer receiver still calls the completion publisher with the service result.

Map entries `0x9ec -> 0xa24`, `0x9f8 -> 0xa30`, and `0x9f0 -> 0xa28` select donor functions `0x22d360`, `0x22d43e`, and `0x22d4de`.
The donor export table starts at `0x233c84`.
Receiver `0x22d360` copies record words at offsets four, eight, twelve, and zero to its four outputs.
Helper `0x22d6a0` allocates a response slot with state one and result minus one.
Allocation does not prove that the OEM handler executed.

Publisher `0x22d43e` checks the identifier and requires pending state one.
At `0x22d49c`, it stores the supplied service result at response offset `0xc`.
It sets response offset `0x10` to one and advances the completion counter.
After a fence, instruction `0x22d4ae` stores response state two at offset eight.
An authentication error therefore remains a completed response with a nonzero service result.
Pending state one bounds the fault before successful publication, not to an authentication rejection.
The later trace narrows this boundary to a service call, as described below.

### Opcode-six payload and asynchronous callback

Handler `0x42b170` requires at least `0xa4` bytes and copies exactly that size at `0x42b1b4..0x42b1c4`.
It reads the first payload word as an image selector.
It normalizes addresses at payload offsets four, `0x10`, and `0x18` through `0x42a21a`.
It checks address arithmetic before copying image data.
It checks the first two ranges through API `0x9e0` with flags `0x104`.
It checks the third range through that API with flags `0x500`.
Calls from `0x42b35a` onward require validation sentinel `0x2ec88ff0`, not Boolean true.
No map or adapter change may replace these checks with service success.

The boot path at `0x41c3a4..0x41c3cc` prepares a callback context and calls API `0x3f8` with arguments four, one, and its address.
The context starts with pointer `0x443008`, whose first word selects callback `0x41c498`.
The callback stores its status argument at context offset `0x20`.
It then tail-calls API `0x514`.
Mapped donor target `0x21ca84` examines the synchronization object at context offset `0x24`.
If that operation returns zero and the context status remains zero, it changes the context status to two.
This context status is not the mailbox response state.

Original API `0x3f8` target `0x21bb1e` and mapped donor target `0x21be7c` both construct packet opcode `0x2009`.
Both place the three arguments at packet offsets `0x10`, `0x14`, and `0x18`.
The original transport pointer is `0x416d28`.
The relocated donor transport pointer is `0x489540`.
The inspected packet layout does not establish a correction to map entry `0x3f8 -> 0x40c`.

The later opcode-five handler at `0x42bbda` uses the same callback descriptor.
Its API `0x568` call at `0x42bc26` maps to donor `0x21bbde`, which constructs packet opcode `0x2003`.
OEM `0x42bc28..0x42bc38` requires both transport status and callback status zero before publishing milestone five.
Neither milestone five nor callback status supplies an authorized substitute for opcode-six authentication.

### Trace narrows the service boundary

`build/private/mailbox-state-tme.log` includes CPU register records as well as translated instruction blocks.
CPU records, not instruction-block translation alone, establish the following execution.
Handler entry `0x42b170` appears at line 330819.
The handler passes its initial image checks and reaches `0x42b522`, which compares the last result with `0x2ec88ff0`.
Its last recorded block is `0x42b52c` at line 356856.
That block calls OEM function `0x439e06` at instruction `0x42b52e`.
The block's registers retain service object `s2=0x448930`, `sp=0x408640`, and `gp=0x404c18`.
No later handler return or completion call appears in this bounded trace.

Cold-loop entry `0x427a7e` then appears at line 357188.
Its register record has `sp=0x40d7d0`, `gp=0x404804`, and `ra=0x226e32`.
These registers identify a different scheduled thread.
The cold loop is not a direct call from the opcode-six handler.
The trace does not cover the service body or prove which operation blocks that service thread.

The RAM capture records image selector `0x95`.
The three payload address/size pairs are `(0x149fff18,0x40)`, `(0x149fff58,0xa8)`, and `(0x1480e000,0x3000)`.
Payload word `+0xc` is zero.
The first optional pair at `+0x20` is `(0x14946000,0x1494e000)`, with count one at `+0xa0`.
The live dispatch gate retains identifier one.
The relocated donor operations include request getter `0x23507e`, consistent with the adapter's legacy request ring.

### Completion interrupt boundary

The subsequent CPU trace reaches `0x43a072` and calls the verifier through `0x4398c6`.
The request thread then waits for a real entropy operation.
The sequencer executes command `0x13`, but its completion interrupt does not reach the CPU.

A non-halting snapshot reads only registers with verified read-only behavior and matching access widths.
CLIC source 23 is pending and enabled.
Its priority and the threshold are both zero.
The sequencer reports phase `0x42`, completion one, and interrupt enable one.
The external source word is `0x4`, and its enable word is `0x3c042576`.

The virtual CLIC requires a priority greater than its threshold.
The shared runtime profile now selects reset priority one.
This value is an explicit virtual policy, not a captured silicon value.
Firmware retains control of interrupt enables, priorities, and the threshold.
The policy does not create completion or change authentication results.

Both OEM runs pass this wait with the new priority.
SM8850 then reaches import `0x70c` at `0x41fdf2`.
SM8845 reaches the same import at `0x41fe72`.
Both stop at the unmapped import address `0x416f0c`.
This result does not prove image authentication or kernel execution.

The pending response did not establish a mailbox ABI defect.
The completion analysis below adds two imports to the common map.
The adapter remains unchanged. The reconstruction recipe needs the new map digest.

Debugger reads of the legacy import window failed and stopped a diagnostic run.
Trace guest import loads instead.
Guest-owned RAM and supported MMIO require separate access contracts.

### Completion status and callback imports

Static decoding uses the original ROM, donor ROM, and both selected OEM ELFs.
The donor export table starts at `0x233c84`.
The following addresses refer to unrelocated ROM code, not debug reads of the virtual import window.

Both faulting instructions load the same pointer:

| Consumer | SM8850 | CPH2771 | Legacy import |
| --- | --- | --- | --- |
| Completion-state predicate | `0x41fdf2` | `0x41fe72` | `0x70c` |
| Completion ownership predicate | `0x41fe22` | `0x41fea2` | `0x648` |
| Completion acknowledgment | `0x41fe5e` | `0x41fede` | `0x64c` |
| Operation `0x13` completion accessor | `0x41fc68` | `0x41fce8` | `0x794` |

The first load uses `lui a0,0x417` and `lw a0,-0xf4(a0)`.
Its address is `0x416f0c`, or legacy base `0x416800` plus `0x70c`.
The following indirect call takes no explicit arguments.
Its zero return skips the completion body.

The operation selector is separate from the sequencer command number.
For OEM operation `0x13`, the request table uses index `0x12`.
SM8850 table word `0x4432ac` selects `0x41f916`, which loads existing import `0x790`.
Its completion table word `0x443398` selects `0x41fc64`, which loads missing import `0x794`.
CPH2771 table words `0x4432b0` and `0x44339c` select `0x41f996` and `0x41fce4`.
Thus `0x794` is an independently decoded sibling, not a guessed adjacent slot.

| Legacy import | Donor import | Original function | Donor function | Contract |
| --- | --- | --- | --- | --- |
| `0x63c` | `0x65c` | `0x21ca1c` | `0x21cd2e` | Read `(index, output_word_pointer)`, return Boolean access result |
| `0x640` | `0x660` | `0x21ca4a` | `0x21cd64` | Write `(index, word)`, return Boolean access result |
| `0x644` | `0x664` | `0x21cef2` | `0x21d2e2` | Read completion phase and result field, return their actual predicate |
| `0x648` | `0x668` | `0x21cfc6` | `0x21d3a6` | Check completed phase and selected engine ownership |
| `0x64c` | `0x66c` | `0x21cf66` | `0x21d348` | Read indexed word five, then acknowledge through indexed word six when nonzero |
| `0x6b0` | `0x6d0` | `0x21cee4` | `0x21d2d4` | Snapshot completion fields into the supplied context, without output-word indices |
| `0x6b4` | `0x6d4` | `0x21cdb4` | `0x21d1ac` | Snapshot `(first_output_index, context)`, return Boolean read result |
| `0x70c` | `0x72c` | `0x21cd86` | `0x21d17a` | Read indexed status word zero, return access result AND `((word & 0x70) == 0x40)` |
| `0x714` | `0x744` | `0x21c944` | `0x21cc34` | Read `(output_index, output_word_pointer)` with range and pointer checks |
| `0x794` | `0x7dc` | `0x21db80` | `0x21ddd2` | Complete `(operation_descriptor, result_argument)` through its context at offset `0x18` |

Only `0x70c -> 0x72c` and `0x794 -> 0x7dc` are new map entries.
The other entries already exist and remain unchanged.
Original `0x21cd86` calls shared prologue `0x21d088`, which loads original import `0x63c`.
Donor `0x21d17a` explicitly loads its corresponding import `0x65c`.
Both pass index zero and a real stack output pointer before testing the returned status bits.

Original `0x21cf3c..0x21cf44` checks bit zero of indexed word two.
Donor `0x21d328..0x21d32e` XORs the full word with one, then ANDs the Boolean access result.
The getter masks its result with pointer-validity predicates, which produce canonical zero or one.
Thus the donor also preserves the bit-zero-clear predicate, regardless of higher status bits.

The ownership predicate requires status bits `0x70` to equal `0x40`.
It also checks the low two status bits against the selected engine.
Original calls use import `0x4e4`. Donor calls use corresponding import `0x4f8`.
Neither the map nor the adapter replaces this ownership check.
Acknowledgment remains after the OEM completion consumer, not before its output reads.

The snapshot reads indexed words two, three, four, and `0x1e`.
With an output index other than minus one, it also reads three consecutive output words through import `0x714`.
Original `0x21ceb0..0x21ceb8` copies `0x1c` bytes into context offset four.
Donor `0x21d2a6..0x21d2ae` preserves that copy.
The `0x6b0` wrapper supplies index minus one, so its snapshot does not request those three output words.
This wrapper does not remove the operation's other real output transfers.

Original completion accessor `0x21db80` uses shared prologue `0x21dfba`.
Donor `0x21ddd2` saves the same descriptor and result arguments explicitly.
Both read the context at descriptor offset `0x18`.
When the context exists, both snapshot through `0x6b0` and notify through original import `0x540`, donor import `0x558`.
The accessor returns the original result argument.

Original notifier `0x21d052` and donor notifier `0x21d43c` compute callback reason `(result_argument - 1) & 2`.
This gives reason zero for argument one and reason two for argument zero.
They dispatch through original import `0x53c`, donor import `0x554`.
Original `0x21d03e` and donor `0x21d428` either store the reason at context offset `0x20` or invoke its virtual callback.
The context's optional pointer at offset `0x24` and its method at offset four select the store path.
No constant callback reason or host-written completion replaces this dispatch.

Backend Boolean acceptance and OEM resolver status are different contracts.
SM8850 `0x41f802` initializes resolver status to two.
At `0x41fa34`, backend false retains two, while backend true changes the resolver status to zero.
CPH2771 `0x41fab4..0x41fab8` preserves the same conversion.
The completion resolver at SM8850 `0x41fd90..0x41fd96` also converts backend false to two and true to zero.
Do not describe resolver status zero as backend false or refusal.
Do not apply the notifier's separate reason conversion to the resolver status without tracing its caller.

Before the entropy-output change below, the map contained 266 entries.
Its SHA-256 was `8e7c062ce98a0a50d224ea07139834680cde3e465a4532e01c74b06d37522b5a`.
The current digest and required recipe update appear below.
No adapter bytes, relocation spans, or reconstructed ROM bytes change.
Static decoding does not prove the next native outcome, image authentication, or kernel execution.

### Entropy output consumer and remaining completion cohort

SM8850 fault PC `0x41fd40` loads legacy import `0x6c4` from `0x416ec4`.
CPH2771 `0x41fdc0` loads the same import.
The SM8850 operation table word `0x44341c` selects `0x41fd3c` for operation `0x2002`.
The CPH2771 table word `0x443420` selects `0x41fdbc`.
Both arms pass the operation descriptor at request offset `0x10` and the actual completion predicate.
The common resolver converts the returned Boolean to status zero or two.

The corresponding request table word `0x443330` selects SM8850 `0x41f9d6`.
Its load at `0x41f9da` selects legacy request import `0x6c0`.
The CPH2771 table word `0x443334` selects `0x41fa56`, with the load at `0x41fa5a`.
Existing map entry `0x6c0 -> 0x6e0` selects donor request function `0x220234`.
That function submits command `0x13`.
It writes indexed word seven with `(descriptor[0] + 3) >> 2` and writes zero to indexed words eight and nine.
Each required write and the final submission must succeed.
On failure, the optional context at descriptor offset four receives virtual callback reason two.

Donor import `0x6e4` selects candidate output function `0x22029a`.
Its two arguments are the descriptor and the completion predicate.
The descriptor supplies the byte length at offset zero and the optional context at offset four.
For a non-null context, the function snapshots completion fields through donor import `0x6d0`.
For a nonzero predicate, it then calls donor import `0x748` with these arguments:

- First output-word index: zero.
- Buffer descriptor: the pointer at context offset `0x1b0`.
- Requested byte count: the word at operation-descriptor offset zero.

Donor `0x748` selects output-transfer function `0x21cca6`.
This function checks the buffer capacity at offset eight against the requested byte count.
It resets the buffer count at offset `0x0c` before that check.
It reads `(byte_count + 3) >> 2` real output words through donor import `0x744`.
For each word, it appends `min(remaining_bytes, 4)` bytes through helper `0x217b22`.
An output-word read failure returns zero.
A capacity failure also returns zero.
Zero requested bytes return one without reading an output word.
The function does not replace output bytes with a constant or round the published byte count upward.
The append helper uses the buffer data pointer at offset four and increments the count by the actual copied byte count.
The transfer loop does not test the append helper's return value.
Thus its success result proves successful output-word reads, not an independent check that the buffer data pointer was valid.
Do not introduce a stronger error contract when describing this firmware routine.
Original transfer function `0x21c9aa` preserves the same capacity check, count reset, word-read loop, and unchecked append result.
Existing import `0x718 -> 0x748` already selects this transfer contract.

Candidate `0x22029a` replaces a nonzero completion predicate with this transfer result.
It then calls notifier `0x558` with the context and the resulting predicate.
It returns that predicate.
A null context returns the incoming predicate without a snapshot, transfer, or notification.
A zero incoming predicate skips the transfer but still notifies a non-null context.
The notifier retains reason `(predicate - 1) & 2` and the existing virtual-callback or status-store path.
The OEM interrupt handler acknowledges completion only after its completion consumer returns.

Map entry `0x6c4 -> 0x6e4` is an authorized behavioral substitute, not a claim of exact original-body equivalence.
The existing request mapping establishes the byte-length/context descriptor consumed by this completion path.
SM8850 context constructor `0x41cfb2` stores its supplied output object at context offset `0x1b0`, at instruction `0x41cfd0`.
Its base initializer `0x41cfe4` initializes completion fields, callback status at `0x20`, and synchronization pointer at `0x24`.
This context shape agrees with the donor output reader and notifier.
The original implementation of import `0x6c4` remains unidentified.
The supplied original ROM contains `0x20000` bytes, with addresses `0x200000..0x21ffff`.
It does not contain the original `0x232xxx` selector tables or a recoverable export table.
Original resolver load `0x21c5d6` alone does not prove its target's output contract.
The missing original upper body prevents exact equivalence claims, but does not prevent this explicitly authorized compatibility inference.

Do not align original and donor switch arms by position.
Donor `0x21c6e4` loads import `0x7f4`, but this is not proof of correspondence with original `0x21c5d6`.
Donor table word `0x2325c8` selects that arm for operation `0x2007`.
Its target `0x220456` reads one output word into context offset `0x28`.
It does not perform the byte transfer required by candidate `0x22029a`.
Similarly, original `0x21fe3a` uses a different output object path and does not establish the missing export.

The following table inventories every distinct completion import in the decoded OEM resolver.
The addresses identify import loads, not function entries.
The special `0x6e4` consumer also appears at SM8850 `0x41fbe4` and CPH2771 `0x41fc64`.
There are 35 distinct imports. Four had mappings before this change.
This change adds one behavioral substitute. The other 30 remain unmapped.

| Legacy import | SM8850 load | CPH2771 load | Map status |
| --- | --- | --- | --- |
| `0x638` | `0x41fcf0` | `0x41fd70` | Unmapped |
| `0x654` | `0x41fcd2` | `0x41fd52` | Unmapped |
| `0x660` | `0x41fd0e` | `0x41fd8e` | Unmapped |
| `0x668` | `0x41fd4a` | `0x41fdca` | Unmapped |
| `0x678` | `0x41fce6` | `0x41fd66` | Unmapped |
| `0x684` | `0x41fcc8` | `0x41fd48` | Unmapped |
| `0x68c` | `0x41fc18` | `0x41fc98` | Unmapped |
| `0x694` | `0x41fd7c` | `0x41fdfc` | Unmapped |
| `0x69c` | `0x41fd68` | `0x41fde8` | Unmapped |
| `0x6a4` | `0x41fbfe` | `0x41fc7e` | Existing `0x6ac` |
| `0x6ac` | `0x41fc5e` | `0x41fcde` | Unmapped |
| `0x6c4` | `0x41fd40` | `0x41fdc0` | New compatibility inference `0x6e4` |
| `0x6d4` | `0x41fc4a` | `0x41fcca` | Unmapped |
| `0x6d8` | `0x41fc72` | `0x41fcf2` | Unmapped |
| `0x6e4` | `0x41fc7c` | `0x41fcfc` | Corrected to `0x704`, see hardware SHA-384 completion below |
| `0x6e8` | `0x41fc54` | `0x41fcd4` | Unmapped |
| `0x6f0` | `0x41fd54` | `0x41fdd4` | Unmapped |
| `0x6f8` | `0x41fcfa` | `0x41fd7a` | Unmapped |
| `0x730` | `0x41fd22` | `0x41fda2` | Unmapped |
| `0x73c` | `0x41fd86` | `0x41fe06` | Unmapped |
| `0x744` | `0x41fd72` | `0x41fdf2` | Unmapped |
| `0x750` | `0x41fc36` | `0x41fcb6` | Unmapped |
| `0x758` | `0x41fc40` | `0x41fcc0` | Existing `0x788` |
| `0x760` | `0x41fcbe` | `0x41fd3e` | Unmapped |
| `0x774` | `0x41fbca` | `0x41fc4a` | Unmapped |
| `0x77c` | `0x41fd5e` | `0x41fdde` | Unmapped |
| `0x784` | `0x41fd2c` | `0x41fdac` | Unmapped |
| `0x78c` | `0x41fd04` | `0x41fd84` | Unmapped |
| `0x794` | `0x41fc68` | `0x41fce8` | Existing `0x7dc` |
| `0x79c` | `0x41fc2c` | `0x41fcac` | Unmapped |
| `0x7a4` | `0x41fcb4` | `0x41fd34` | Unmapped |
| `0x7ac` | `0x41fc22` | `0x41fca2` | Unmapped |
| `0x7b4` | `0x41fcdc` | `0x41fd5c` | Unmapped |
| `0x7bc` | `0x41fd36` | `0x41fdb6` | Unmapped |
| `0x7c4` | `0x41fd18` | `0x41fd98` | Unmapped |

Request-import proximity does not prove completion equivalence.
For example, mapped request `0x688 -> 0x6a8` reaches donor `0x2204ac`.
Its candidate completion `0x6ac` at `0x220590` uses descriptor context offset `0x0c`.
It reads output word zero only when the incoming predicate is zero, then stores the word at context offset `0x28`.
This differs from candidate `0x22029a`, which transfers output bytes only for a nonzero predicate.
Do not replace either condition with a generic completion rule.

#### Completion cohort: bounded proposals and rejected selector-only matches

This investigation uses static instruction decoding only.
It does not change the map, recipe, authentication policy, or firmware bytes.
The inventory above records the map before these proposals.
The five new rows below and one correction are behavioral compatibility inferences.
They are not claims of recovered original export identities.

The original ROM contains completion import loads, but no recoverable export table.
An original load address below identifies the imported slot, not its missing function entry.
The donor export word is at `0x233c84 + donor_import`.
All OEM addresses use the TME view.
The first OEM address in each pair is SM8850. The second is CPH2771.

**Bounded integration group.**
Each proposed completion consumes the context offset used by an already mapped submit function.
Actual selector tables select both halves.
This provides more evidence than adjacent ordinals or matching instruction alignment.
Execution must still establish compatibility with the OEM-created descriptor.

| Legacy → donor | Opcode | Original import load | OEM completion loads | Donor function | Descriptor context | Existing submit dependency |
| --- | --- | --- | --- | --- | --- | --- |
| `0x68c → 0x6ac` | `0x2001` | `0x21c5e0` | `0x41fc18 / 0x41fc98` | `0x220590` | `+0x0c` | `0x688 → 0x6a8`, function `0x2204ac` |
| `0x6d4 → 0x6f4` | `3` | `0x21c6a8` | `0x41fc4a / 0x41fcca` | `0x21f376` | `+0x1c` | `0x6cc → 0x6ec`, function `0x21f324` |
| `0x6e8 → 0x708` | `5` | `0x21c69e` | `0x41fc54 / 0x41fcd4` | `0x21f424` | `+0x1c` | `0x6e0 → 0x700`, function `0x21f3d0` |
| `0x6d8 → 0x6f8` | `6` | `0x21c68a` | `0x41fc72 / 0x41fcf2` | `0x21f4c6` | `+0x20` | `0x6d0 → 0x6f0`, function `0x21f474` |
| `0x750 → 0x780` | `7` | `0x21c694` | `0x41fc36 / 0x41fcb6` | `0x21f570` | `+0x20` | `0x74c → 0x77c`, function `0x21f51e` |
| Correct `0x6a4 → 0x6c4` | `0x1001` | `0x21c4fc` | `0x41fbfe / 0x41fc7e` | `0x21e6ac` | `+0x18` | `0x6a0 → 0x6c0`, function `0x21e58e` |

The following table gives the actual table-word selectors.
The donor completion arm loads its import four bytes after the listed arm entry.
These table words contain code pointers, not import numbers.

| Opcode | OEM submit words | OEM completion words | Donor submit word → arm | Donor completion word → arm |
| --- | --- | --- | --- | --- |
| `0x2001` | `0x44332c / 0x443330` | `0x443418 / 0x44341c` | `0x23246c → 0x21c34c` | `0x2325b0 → 0x21c602` |
| `3` | `0x44326c / 0x443270` | `0x443358 / 0x44335c` | `0x2323b8 → 0x21c4ee` | `0x2324fc → 0x21c780` |
| `5` | `0x443274 / 0x443278` | `0x443360 / 0x443364` | `0x2323c0 → 0x21c4f8` | `0x232504 → 0x21c78a` |
| `6` | `0x443278 / 0x44327c` | `0x443364 / 0x443368` | `0x2323c4 → 0x21c4e4` | `0x232508 → 0x21c776` |
| `7` | `0x44327c / 0x443280` | `0x443368 / 0x44336c` | `0x2323c8 → 0x21c502` | `0x23250c → 0x21c794` |
| `0x1001` | `0x4432e8 / 0x4432ec` | `0x4433d4 / 0x4433d8` | `0x232424 → 0x21c332` | `0x232568 → 0x21c5e8` |

Let `d` be the operation descriptor and `p` the incoming completion predicate.
OEM common call site `0x41fd8a / 0x41fe0a` passes request offset `0x10` as `d`.
The OEM resolver converts a false backend return to status two and a true return to status zero.
It does not invert `p` before the call.
All six candidates skip context work when their context pointer is null.

For `0x68c → 0x6ac`, donor `0x220590` snapshots the context through import `0x6d0`.
Only `p == 0` triggers an output-word-zero read through import `0x744`.
A successful read stores that real word at context offset `0x28`.
A failed read leaves that field unchanged.
The read result does not replace `p`.
The function notifies through `0x558` and returns the original `p`.
The paired submit function sends command `0x0f` and reads the descriptor count at `+8`.
This completion does not copy a SHA digest.
Do not replace its zero-predicate branch with the hardware hash consumer's nonzero-predicate transfer.
The high-level opcode name alone does not prove that these two requests use the same output format.

For `0x6d4`, `0x6e8`, and `0x6d8`, each donor completion snapshots through `0x6d0`.
For nonzero `p`, it obtains a byte count from donor import `0x4ec`, function `0x21f63a`.
The arguments differ:

- `0x6f4` passes descriptor words at `+4`, `+0x0c`, and `+0x10`, then zero.
- `0x708` passes descriptor words at `+4`, `+8`, and `+0x0c`, then zero.
- `0x6f8` passes descriptor words at `+4`, `+0x0c`, and `+0x10`, then zero.

Each then transfers that many real bytes through import `0x748`, starting at output word zero.
The destination buffer descriptor is at context offset `0x1b0`.
The transfer Boolean becomes the returned predicate.
These functions call notifier `0x554` directly, not Boolean wrapper `0x558`.
Incoming zero produces reason two.
A transfer result of zero produces reason three, through `(result - 1) & 3`.
A transfer result of one produces reason zero.
Preserve the distinction between refusal and output-transfer failure.
The request functions independently read the same context offsets at `0x21f32c`, `0x21f3d8`, and `0x21f47c`.

For `0x750 → 0x780`, donor `0x21f570` also snapshots and uses byte-count helper `0x4ec`.
It passes descriptor words at `+4`, `+8`, `+0x0c`, and `+0x10`.
Its ordinary output branch uses `0x748`, context `+0x1b0`, and explicit notifier `0x554`.
Its zero-predicate reason is two. Its ordinary transfer-failure reason is three.
However, descriptor word `+8` equal to zero or six selects a count-only branch.
Word `+8` equal to one also selects that branch when word `+0x0c` equals five.
This branch does not read output words.
If capacity is sufficient, it publishes the calculated byte count at buffer offset `0x0c`.
If capacity is insufficient, it leaves that count unchanged.
Both capacity outcomes return one and notify reason zero in the donor instructions.
This unusual behavior is observed code, not permission to synthesize output or a successful authentication result.
Keep this row separate if the integration cannot preserve that exact count-only contract.

For the `0x6a4` correction, donor `0x21e6ac` snapshots context `d[0x18]`.
When `p == 0`, it releases the slot at context `+0x2c` through helper `0x21e48a`.
It then stores minus one in that slot, notifies through `0x558`, and returns `p`.
For nonzero `p`, it does not release or reset the slot.
Mapped submit `0x21e58e` reads context `+0x18` and stores its allocated slot at context `+0x2c`.
The current completion mapping `0x6a4 → 0x6ac` instead reads context `+0x0c`.
It also implements an unrelated output-word read.
Those contracts conflict. This is not a harmless alternate export for opcode `0x1001`.

Dependencies already mapped include snapshot `0x6b0 → 0x6d0`, reason notifier `0x53c → 0x554`, and Boolean notifier `0x540 → 0x558`.
They also include output reader `0x714 → 0x744` and output transfer `0x718 → 0x748`.
Donor `0x554` stores the explicit reason at context `+0x20` when context `+0x24` selects the synchronization path.
Otherwise it dispatches virtual callback slot zero.
Wrapper `0x558` supplies reason `(p - 1) & 2`.
Snapshot `0x6d0` calls `0x6d4` with output index minus one.
The deferred slot-management family below instead uses snapshot `0x6d4` with output index zero.
Do not merge these snapshot operations.

The integration includes all five proposed additions and the `0x6a4` correction.
The curve classifier mapping `0x2ac -> 0x2a4` is a separate addition.
The remaining candidates below stay unmapped.


**Remaining 20 unmapped completion imports.**
These are exact donor candidates selected by decoded operation keys, not approved integration rows.
Each row still lacks an established OEM descriptor bridge through its submit import.
The dependency column gives the required submit candidate.
Do not add these rows solely because their selectors use the same numeric opcode.
The inventory above supplies both OEM load addresses for every row.

| Legacy → candidate | Opcode | Original import load | Donor function | Donor completion selector | Context | Submit dependency |
| --- | --- | --- | --- | --- | --- | --- |
| `0x638 → 0x658` | `0x1008` | `0x21c59a` | `0x21f184` | `0x232584 → 0x21c65e` | `+4` | `0x634 → 0x654` |
| `0x654 → 0x674` | `0x1002` | `0x21c586` | `0x21e788` | `0x23256c → 0x21c686` | `+8` | `0x650 → 0x670` |
| `0x660 → 0x680` | `0x1007` | `0x21c5a4` | `0x21f0dc` | `0x232580 → 0x21c6a4` | `+8` | `0x65c → 0x67c` |
| `0x668 → 0x688` | `0x3003` | `0x21c662` | `0x21ff1e` | `0x232600 → 0x21c758` | `+4` | `0x664 → 0x684` |
| `0x684 → 0x6a4` | `0x21` | `0x21c608` | `0x21e0fa` | `0x232550 → 0x21c730` | `+0x24` | `0x680 → 0x6a0` |
| `0x694 → 0x6b4` | `0x32` | `0x21c55e` | `0x21d8c4` | Explicit arm `0x21c79e` | `+0x18` | `0x690 → 0x6b0` |
| `0x69c → 0x6bc` | `0x31` | `0x21c6b2` | `0x21d696` | Explicit arm `0x21c7bc` | `+0x10` | `0x698 → 0x6b8` |
| `0x6ac → 0x6cc` | `0x17` | `0x21c64e` | `0x21df70` | `0x232528 → 0x21c708` | `+0x14` | `0x6a8 → 0x6c8` |
| `0x6f0 → 0x710` | `0x3007` | `0x21c658` | `0x21ffd8` | `0x232610 → 0x21c762` | `+8` | `0x6ec → 0x70c` |
| `0x6f8 → 0x718` | `0x1003` | `0x21c57c` | `0x21e906` | `0x232570 → 0x21c67c` | `+0x1c` | `0x6f4 → 0x714` |
| `0x73c → 0x76c` | `0x26` | `0x21c63a` | `0x2208bc` | `0x232564 → 0x21c6f4` | `+0x14` | `0x738 → 0x768` |
| `0x744 → 0x774` | `0x3002` | `0x21c544` | `0x21fea6` | `0x2325fc → 0x21c636` | `+4` | `0x740 → 0x770` |
| `0x760 → 0x790` | `0x12` | `0x21c644` | `0x21db60` | `0x232514 → 0x21c712` | `+0x14` | `0x75c → 0x78c` |
| `0x774 → 0x7a4` | `0x3010` | `0x21c676` | `0x220160` | `0x232634 → 0x21c74e` | `+4` | `0x770 → 0x7a0` |
| `0x77c → 0x7ac` | `0x3005` | `0x21c680` | `0x21fe30` | `0x232608 → 0x21c76c` | `+4` | `0x778 → 0x7a8` |
| `0x78c → 0x7bc` | `0x1006` | `0x21c572` | `0x21eff0` | `0x23257c → 0x21c690` | `+0x14` | `0x788 → 0x7b8` |
| `0x79c → 0x7cc` | `0x15` | `0x21c626` | `0x21de42` | `0x232520 → 0x21c73a` | `+0x24` | `0x798 → 0x7c8` |
| `0x7a4 → 0x7dc` | `0x14` | `0x21c630` | `0x21ddd2` | `0x23251c → 0x21c6fe` | `+0x18` | `0x7a0 → 0x7d8` |
| `0x7ac → 0x7e4` | `0x16` | `0x21c61c` | `0x21de6a` | `0x232524 → 0x21c726` | `+0x24` | `0x7a8 → 0x7e0` |
| `0x7b4 → 0x7ec` | `0x1005` | `0x21c568` | `0x21ee14` | `0x232578 → 0x21c668` | `+0x10` | `0x7b0 → 0x7e8` |

Their decoded contracts establish the following rejection boundaries:

- `0x638` and `0x660` snapshot with output index zero, notify through `0x558`, and return unchanged `p`. They do not transfer output buffers.
- `0x654`, `0x6f8`, and `0x78c` also snapshot with index zero. Only zero `p` releases context slot `+0x2c` and resets it to minus one.
- `0x684` uses snapshot index minus one. Its zero-predicate cleanup calls donor import `0x50c` on context `+0x2c`, then resets that slot.
- `0x668` transfers bytes only for nonzero `p`. Its buffer pointer is context `+0x28`, not `+0x1b0`. The byte count is that buffer's capacity.
- `0x694` publishes source counts into buffers at context `+0x1b0` and `+0x1b4` when capacities permit. It does not read output words. It preserves `p`.
- `0x69c` also publishes those source counts. It then transfers 12 bytes from output index four to context buffer `+0x1bc`. On success, it transfers 16 bytes from index zero to buffer `+0x1b8`.
- `0x6ac` transfers output for nonzero `p`. Its length is `4 * (((d[0x0c] + 31) >> 5) << shift)`. The shift is zero for `d[8]` equal to `0x21` or `0x22`. Otherwise it is the byte at `d + 0x10`, XOR one.
- `0x6f0`, `0x744`, `0x77c`, `0x79c`, and `0x7ac` snapshot with index minus one, notify through `0x558`, and return unchanged `p`. Their different context offsets remain significant.
- `0x73c` selects its output length through donor table `0x232794` using the descriptor byte at `+4`. Nonzero `p` transfers through `0x748` into context buffer `+0x1b0`. A generic fixed digest length is not justified.
- `0x760` obtains a length from donor `0x4f4` using descriptor word `+8`. It converts that result to `8 * ((length + 3) >> 2)` bytes. Zero converted length forces a false return.
- `0x774` snapshots, then calls virtual callback slot zero directly with reason `(p - 1) & 2`. It does not use the synchronization-aware notifier.
- `0x7a4` snapshots context `+0x18` and notifies through `0x558`. It transfers no output bytes.
- `0x7b4` snapshots with output index zero. Nonzero `p` transfers 92 bytes when descriptor word `+0x0c` equals two, otherwise 100 bytes. It starts at output index three and uses a temporary buffer descriptor. A failed transfer or zero `p` releases context slot `+0x2c`, resets it, and returns zero.

The constructor investigations below establish five complete request/completion families.
Those imports no longer belong to the deferred cohort.


For the deferred table, the zero-predicate branches and all buffer paths come from donor bodies.
They are not measured OEM behavior.
Original counterparts show why an original-body resemblance cannot establish the missing import identity.
For example, original `0x21f142` uses context `+0x18`, while donor `0x21f376` uses `+0x1c`.
Original `0x21de94` uses context `+0x20`, while donor `0x21e0fa` uses `+0x24`.
Original `0x21d4da` and donor `0x21d8c4` both publish source counts without reading output words.
Original `0x21d29a` and donor `0x21d696` both implement the separate two-buffer output transfer.
These are contract witnesses, not recovered legacy export addresses.

Existing `0x794 → 0x7dc` does not require correction from this evidence.
Operation `0x14` selects donor submit arm `0x21c46c`, which loads `0x7d8`.
Its completion arm `0x21c6fe` loads `0x7dc`.
The earlier claim that this selector used donor `0x7c4` was incorrect.
The separate body at `0x21ddaa` does not establish the missing OEM constructor bridge.
No signature acceptance, authenticated-image admission, or kernel execution is established by this cohort investigation.

For the deferred rows, the following formulas identify both request and completion table words.
Use the opcode shown in the candidate table as `op`.
OEM table bases below are CPH2771 bases. SM8850 bases are four bytes lower.

| Operation range | OEM submit word | OEM completion word | Donor submit word | Donor completion word |
| --- | --- | --- | --- | --- |
| `1..0x21` | `0x443268 + 4*(op-1)` | `0x443354 + 4*(op-1)` | See next range | See next range |
| Donor `1..7` | — | — | `0x2323b0 + 4*(op-1)` | `0x2324f4 + 4*(op-1)` |
| Donor `0x11..0x26` | — | — | `0x2323cc + 4*(op-0x11)` | `0x232510 + 4*(op-0x11)` |
| `0x1001..0x1011` | `0x4432ec + 4*(op-0x1001)` | `0x4433d8 + 4*(op-0x1001)` | `0x232424 + 4*(op-0x1001)` | `0x232568 + 4*(op-0x1001)` |
| `0x2001..0x2009` | `0x443330 + 4*(op-0x2001)` | `0x44341c + 4*(op-0x2001)` | `0x23246c + 4*(op-0x2001)` | `0x2325b0 + 4*(op-0x2001)` |
| Donor `0x3002..0x3010` | Explicit branches | Explicit branches | `0x2324b8 + 4*(op-0x3002)` | `0x2325fc + 4*(op-0x3002)` |

These formulas identify selector words only. They do not approve default or unsupported arms.
For the explicit OEM branches, the following addresses identify arm entries.
Each arm loads its selected import at entry plus four.

| Opcode | OEM submit arms | OEM completion arms |
| --- | --- | --- |
| `0x26` | `0x41fa26 / 0x41faa6` | `0x41fd82 / 0x41fe02` |
| `0x31` | `0x41fa08 / 0x41fa88` | `0x41fd64 / 0x41fde4` |
| `0x32` | `0x41fa1c / 0x41fa9c` | `0x41fd78 / 0x41fdf8` |
| `0x3002` | `0x41fa12 / 0x41fa92` | `0x41fd6e / 0x41fdee` |
| `0x3003` | `0x41f9ea / 0x41fa6a` | `0x41fd46 / 0x41fdc6` |
| `0x3005` | `0x41f9fe / 0x41fa7e` | `0x41fd5a / 0x41fdda` |
| `0x3007` | `0x41f9f4 / 0x41fa74` | `0x41fd50 / 0x41fdd0` |
| `0x3010` | `0x41f86e / 0x41f8ee` | `0x41fbc6 / 0x41fc46` |

Donor explicit submit arm `0x21c536` selects `0x6b8` for opcode `0x31`.
Arm `0x21c518` selects `0x6b0` for opcode `0x32`.
For notifier `0x554`, the status-store path requires a non-null pointer at context `+0x24` and a nonzero word at that pointer plus four.
All other non-null contexts use virtual callback slot zero.
Neither notifier path establishes that cryptographic verification succeeded.

The entropy investigation adds `0x6c4 -> 0x6e4`.
The hardware hash investigation below corrects the separate entry `0x6e4 -> 0x704`.
Those corrections first produced a 267-entry map with SHA-256 `016e236051e29ed040ddb718f3a35f492d412a3e2d4c0cb5415222b7e78cb188`.
The [execution evidence](../../../../../docs/qcom/sm88xx/oplus/bringup.md#optional-common-runtime-reconstruction) records the current integrated identities.
Later native runs passed hash-state transfer and reached runtime signature verification.
These results do not prove complete image authentication or kernel execution.

### Hardware SHA-384 completion

The actual SM8845 hardware opcode-one submission selects OEM table word `0x443268`.
Its arm `0x41f908` loads legacy import `0x6dc`, mapped to donor `0x6fc` at `0x21f282`.
The descriptor fields are control at zero, type at four, argument at eight, phase at twelve, and context at `0x18`.
Donor `0x21f282` constructs one source and submits the actual hash request.

The corresponding OEM completion table word `0x443354` selects `0x41fc60`.
Its load at `0x41fc64` uses legacy import `0x6e4`.
This hardware operation is separate from high-level opcode `0x2001` and its completion consumer.
The old `0x6e4 -> 0x6c4` mapping came from eight matched resolver instructions.
It selected donor `0x21e6ac`, the completion paired with hardware opcode-three submit function `0x21e58e`.
That completion snapshots status and notifies the context without copying the hash state.

The bounded SM8845 probe found a 640-byte phase-three request followed by a 16-byte phase-five request.
Both used control one and one source.
The final cumulative length was 656 bytes.
Both requests wrote only the count and one descriptor.
Thus the final request omitted the saved 64-byte state required by the actual emitter and the model.
The SM8850 probe found the same omission with initial length `0x200` and final length `0x79`.
Earlier boot finalizations supplied all 64 state bytes.

Donor hardware opcode-one completion table word `0x2324f4` selects arm `0x21c650`.
That arm loads import `0x704`, which exports function `0x21f2d4`.
This completion uses the same descriptor context at offset `0x18`.
It snapshots status through `0x6d0`.
For a nonzero predicate, it computes the output size through `0x4ec` from the descriptor's type, argument, and phase.
For SHA-384, this size is 64 before finalization and 48 after finalization.
It copies real output through `0x748` into the output object at context offset `0x1b0`.
The transfer result determines the notification through `0x554` and the returned predicate.
Original body `0x21f150..0x21f1a6` preserves the same descriptor fields, size calculation, byte transfer, and result-derived notification.

The corrected map selects `0x6e4 -> 0x704`.
No hidden host context replaces the guest state.
No missing-state continuation becomes valid.
The hash model, descriptor checks, secure DMA identity, and cryptographic primitives remain unchanged.
The temporary framing diagnostic was removed after both requests were captured.
The next native runs and integration gates remain pending.



### Post-signature streaming phase wrappers

The reported SM8845 fault is the import read at `0x43b8b6` from `0x416bf0`.
The import base is `0x416800`, so the missing legacy index is `0x3f0`.
Static decoding uses both selected OEM packages and the two ROM inputs identified above.
Addresses in this section use the TME address view.
No new firmware execution ran for this investigation.

The coherent wrapper group is:

| Legacy → donor | Donor export body | Original body witness | Contract |
| --- | --- | --- | --- |
| `0x3ec → 0x400` | `0x21a4cc..0x21a4e2` | `0x21a624..0x21a63a` | Final phase: `B(-1, 5, buffer, context, control)` |
| `0x3f0 → 0x404` | `0x21a4a2..0x21a4b2` | `0x21a5f6..0x21a60a` | Initial phase: `B(algorithm, 3, buffer, context, control)` |
| `0x3f4 → 0x408` | `0x21a4b4..0x21a4ca` | `0x21a60c..0x21a622` | Update phase: `B(-1, 4, buffer, context, control)` |

Here, `B` is donor import `0x294`, export body `0x21a8e4..0x21a976`.
The original wrappers all call legacy import `0x29c`.
The initial wrapper accepts four registers: algorithm, buffer, context, and control.
The update and final wrappers accept three registers: buffer, context, and control.
Each wrapper rearranges registers and tail-calls the builder.
Each wrapper preserves the builder's return value without converting it into success.

The original export table is absent.
Thus these rows establish behavioral compatibility, not recovered original export identities.
The original bodies independently establish the same register rearrangement, phase constants, and minus-one algorithm sentinel.
The OEM callers establish which missing imports require those contracts.
The proposals do not depend on numeric index proximity.

#### Both OEM callers and error propagation

SM8845 function `0x43b7f8` constructs a buffer at `sp + 8` and a context at `sp + 0x18`.
It calls constructor `0x41d04a` before the missing import.
At `0x43b8ba..0x43b8c2`, it passes `(2, sp + 8, sp + 0x18, 1)`.
The buffer's four words are initially zero.
Thus this call initializes the stream without input bytes.
The constructor puts the output-buffer pointer at context `+0x1b0`.

At `0x43b8c4..0x43b8c8`, the caller ORs the service return with context status at `+0x20`.
Only zero combined status follows the success branch.
Failure selects OEM error `0x7030` and retains the context diagnostic at `+8`.
The function destroys its context before return.
The corresponding SM8850 import load is `0x43b63a`.
Its call at `0x43b646` uses the same four arguments.
Its status combination and error branch at `0x43b648..0x43b668` are equivalent.

SM8845 update caller `0x43bb16..0x43bb24` loads legacy `0x3f4`.
It passes `(sp + 0x0c, retained_context, 1)`.
That buffer has zero at `+0`, a translated input pointer at `+4`, and the input size at `+8` and `+0x0c`.
The retained context is not a newly fabricated ready object.
The caller checks the return and selects error `0x7031` on failure.
The SM8850 counterpart loads the same import at `0x43b89e` and calls it at `0x43b8a8`.

SM8845 final caller `0x43bbe2..0x43bbf0` loads legacy `0x3ec`.
It passes `(sp + 0x20, sp + 0x34, 1)`.
It ORs the return with context status at `sp + 0x54`, again context `+0x20`.
It then requires output length `0x30` before subsequent image checks.
The SM8850 counterpart loads the same import at `0x43b96a` and calls it at `0x43b974`.
Its status combination at `0x43b976..0x43b97a` is equivalent.
These callers establish a 48-byte output requirement, not an unconditional authenticated-image result.

#### Builder layout and dispatch dependencies

The donor builder clears a `0x78`-byte request before it fills the following fields.
Offsets below are relative to that request, not the builder's stack pointer.

| Offset | Value |
| --- | --- |
| `+4` | Opcode `2` |
| `+0x10` | Unchanged caller control |
| `+0x14` | Algorithm, written only for initial phase `3` |
| `+0x18` | Constant `1` |
| `+0x1c` | Phase `3`, `4`, or `5` |
| `+0x20` | Selected input pointer |
| `+0x24` | Buffer word `+0x0c`, the input byte count |
| `+0x28` | Context pointer |

For a nonempty buffer, the builder selects buffer `+4` when nonzero, otherwise buffer `+0`.
A zero byte count forces the request's input pointer to zero.
Initial phase stores the algorithm at context `+0x28` and clears context `+0x34`.
It calls donor import `0x15c` with the context.
A nonzero result calls cleanup helper `0x21aa9c`, which wipes the buffers described by context fields `+0x170` and `+0x174`.
Update and final phases retain the stored algorithm and accumulated state.
All phases call donor import `0x540`, the existing request dispatcher, with `(request, context)`.
The builder returns that dispatch result unchanged.
Context status and output data remain separate guest-owned fields.

The OEM opcode-two bridge is essential.
SM8845 submit selector word `0x44326c` selects local arm `0x41f982`.
That arm constructs its lower request and loads legacy `0x6dc` at `0x41f990`.
The lower update path loads legacy `0x6e0` at `0x41f9ae`.
Completion selector word `0x443358` selects local arm `0x41fcf8`.
It loads legacy `0x6e4` at `0x41fcfc`.
These imports already map to donor `0x6fc`, `0x700`, and corrected `0x704`.
The SHA-384 state-transfer contract above remains required.

Do not replace the OEM dispatcher with the donor ROM's native opcode-two selector.
Donor submit table word `0x2323b4` selects the common return arm `0x21c548`.
Donor completion table word `0x2324f8` selects `0x21c65a`, which sets a zero result.
Neither supplies the OEM's local bridge.
The three proposed wrappers require the existing runtime dispatch ownership and that bridge.
They do not authorize a ready stub or a success stub.

The superficially plausible command-`0x2001`, `0x2003`, and `0x2006` wrappers are not this group.
Their request layouts and argument positions differ.
In particular, donor `0x90c` interprets its second argument as a byte count, not this caller's context pointer.
No mapping to that service is justified.

The OEM's surrounding range checks, image checks, signature results, and version-policy checks remain authoritative.
No root hash, fuse, rollback decision, or cryptographic result changes.
This static evidence does not establish completed image authentication or kernel execution.
Map integration and the next actual runs remain pending.

### Image-operation context transfer and cleanup

The reported SM8845 fault reads `0x41695c` at `0x41f6b4`.
The import base is `0x416800`, so the missing legacy slot is `0x15c`.
Static decoding compares the original ROM, donor ROM, and packaged CPH2771 `RADIO/tme_fw.img`.
The ROM input hashes match the recipe's original and donor identities.
No new firmware execution ran for this investigation.

The immediate cohort has these contracts:

| Legacy → donor | Original body witness | Donor export body | Contract |
| --- | --- | --- | --- |
| `0x158 → 0x15c`, unchanged | `0x21c7d0..0x21c7d4` | `0x21cad0..0x21cad4` | Return `context + 0x38`. No null check, allocation, status conversion, or cleanup. |
| `0x15c → 0x160`, added | `0x21c7b4..0x21c7c4` | `0x21cab4..0x21cac4` | Non-null source transfers bytes into `context + 0x38`. Null source clears the length word of the descriptor at `context + 0x1a8`. |
| Not added: cleanup export | `0x21c7d6..0x21c7e4` | Donor `0x158`, `0x21cad6..0x21cae4` | Clear context words `+0x28`, `+0x2c`, and `+0x30`, then wipe the embedded state's two buffers. |

The original export table is absent.
The original bodies are contract witnesses, not recovered export-table entries.
The OEM call arguments identify the getter and transfer contracts.
No immediate caller identifies a missing legacy slot for the cleanup export.
Thus the map does not guess a cleanup slot from neighboring donor indices.
In particular, donor `0x15c` is the getter, not the missing setter.
The existing paired mapping `0x158 → 0x15c` is correct.

#### Transfer width, ownership, and return value

The non-null branch adds `0x38` to the destination context.
It then enters original helper `0x21c7c6` or donor helper `0x21cac6`.
Each helper reads descriptor pointers from destination `+0x170` and source `+0x170`.
Thus destination context `+0x1a8` supplies the destination descriptor.
The source argument is an embedded-state pointer, not a buffer descriptor or a scalar length.

Original `0x21c7e8` and donor `0x21cae8` select source descriptor word `+4` when nonzero, otherwise word `+0`.
They read the source byte count from descriptor `+0x0c`.
They tail-call original `0x217afc` or donor `0x217b08` with `(destination_descriptor, source_pointer, source_byte_count)`.
Both routines initially select a zero result.
For nonzero source pointer, byte count, and destination descriptor `+4`, they perform a bounded byte copy.
The bound is the minimum of destination capacity at descriptor `+8` and source byte count.
Original helper `0x217c8a` and donor helper `0x217c72` return the copied byte count.
The destination descriptor receives that count at `+0x0c`.
The wrapper returns the count unchanged.
It does not return a Boolean authentication result or detect truncation as a separate error.
All descriptor pointers, capacities, and byte counts use 32-bit loads and stores.
The copy operates on bytes, not 64-bit scalar fields.

The null-source branch reads the descriptor pointer directly from context `+0x1a8`.
It clears only that descriptor's 32-bit length at `+0x0c`.
It neither wipes nor releases the backing memory.
Its residual `a0` is the descriptor pointer, not a defined success status.
Neither branch checks the context or its destination descriptor for null.
Callers must supply an initialized context.
No branch allocates, frees, adopts a pointer, or transfers buffer ownership.

#### Embedded-state layout and actual cleanup

OEM constructor `0x41d07c` initializes the embedded state at context `+0x38` through `0x41d9ae`.
For embedded-state base `E`, it constructs two 16-byte descriptors:

| Field | First descriptor | Second descriptor |
| --- | --- | --- |
| Descriptor address | `E + 0x150` | `E + 0x160` |
| Word `+0` | Zero | Zero |
| Word `+4` | `E` | `E + 0xa8` |
| Word `+8`, capacity | `0xa8` | `0xa8` |
| Word `+0x0c`, length | Zero | Zero |
| Stored descriptor pointer | `E + 0x170` | `E + 0x174` |

The context therefore stores these descriptor pointers at `+0x1a8` and `+0x1ac`.
Original cleanup `0x21ace2` and donor cleanup `0x21aa9c` accept `E`, not the outer context.
Each processes both descriptors through original `0x217b68` or donor `0x217b48`.
Each descriptor operation zero-fills its owned backing span of capacity `+8` when pointer `+4` is nonzero.
It then clears descriptor length `+0x0c` and returns the capacity.
When backing pointer `+4` is zero, it returns zero without clearing the length.
The zero-fill helpers are original `0x2180ce` and donor `0x217ffc`.
They perform byte stores and have no allocator or import dependency.
These routines wipe buffers. They do not release allocations.
OEM local `0x41ff48` and descriptor wipe `0x42085c` independently preserve this cleanup contract.
OEM local `0x41f708` additionally clears outer context `+0x34` before tail-calling that cleanup with `context + 0x38`.
The donor cleanup export does not clear `+0x34`, so these entry points are not interchangeable.

#### OEM arguments and error propagation

At `0x41f686..0x41f69c`, the OEM copies `s1 + 0xa4` into request `s0 + 0x14` and retained context `+0x28`.
It copies `s1 + 0xb0` into retained context `+0x34`.
At `0x41f6a4..0x41f6ae`, legacy `0x158` receives `s1 + 0x7c`.
Its result is the embedded state at `s1 + 0xb4`.
At `0x41f6b4..0x41f6bc`, legacy `0x15c` receives `(retained_context, embedded_state)`.
The caller ignores the transfer count and calls OEM `0x41f746`.
That routine returns `seqz` of its internal result at `0x41f78e`.
The caller preserves that Boolean decision.
On zero, `0x41f6e0..0x41f6e6` passes the same context and zero source to `0x15c`.
That operation clears the retained input length before local cleanup of `s1 + 0x7c`.
The separate completion path `0x41fd14..0x41fd26` also performs local cleanup followed by the explicit null-source call.
No adapter is required for these proven getter, transfer, or cleanup contracts.

The added transfer calls only donor-local helpers `0x21cac6`, `0x21cae8`, `0x217b08`, and `0x217c72`.
The final byte-copy helper is donor `0x230bb6`.
It creates no new unresolved import dependency.
The next OEM call is local `0x41f746`, which calls `0x41f868` and can call `0x41f5a8`.
Local `0x41f868` validates the queued request through `0x41ec0a` before selecting an opcode-specific import.
Its opcode-two bridge and completion dependencies remain the existing `0x6dc`, `0x6e0`, and `0x6e4` mappings documented above.
Local `0x41f5a8` uses existing legacy import `0xb40 → 0xb8c` at `0x41f5de`.
These static dependencies do not predict the next actual runtime fault.

The transfer addition retains the established getter, transfer, and cleanup contracts.
The constructor-backed families below extend the checked map.
The [execution evidence](../../../../../docs/qcom/sm88xx/oplus/bringup.md#optional-common-runtime-reconstruction) records the current adapter and ROM identities.
These ABI mappings do not bypass authentication, rollback, root hashes, fuses, or locks.

### DBGAR request and completion descriptor bridge

Both OEM runs reached legacy import `0x7b8` at local address `0x416fb8`.
The SM8850 resolver loads it at `0x41f9bc`. SM8845 loads it at `0x41fa3c`.
The operation `0x2007` submit words are `0x443344/0x443348`.
Its completion words are `0x443430/0x443434`.
They select completion import `0x7bc` through loads `0x41fd36/0x41fdb6`.

The existing `0x8c8 -> 0x908` mapping selects donor constructor `0x21bc16`.
OEM callers `0x423f98` and `0x423f4e` supply an address array, count, and completion context.
Each caller limits a batch to eight addresses and copies actual input words into its temporary array.
The respective constructor calls occur at `0x424000` and `0x423fb6`.
Both callers check context status `+0x20` and obtain the failed address from context `+0x28`.

Shared prologue `0x21beea` preserves those three arguments and clears a `0x78`-byte request.
Constructor `0x21bc16` writes opcode `0x2007` at request `+4`.
It writes the address pointer, count, and context at request `+0x10/+0x14/+0x18`.
Shared helper `0x21bf5e` invokes the existing dispatcher with the request and context.
The OEM dispatcher passes request `+0x10` to the selected operation.
This establishes the lower descriptor as pointer, count, and context at offsets zero, four, and eight.

Legacy `0x7b8 -> 0x7f0` selects donor submit function `0x2203b6`.
It builds a real source descriptor with byte count equal to four times the address count.
It submits sequencer command `0x25` only after the precondition and every input write succeed.
Submission failure notifies a non-null context through its callback with reason two and returns zero.

Legacy `0x7bc -> 0x7f4` selects completion `0x220456`.
It obtains the same context at lower-descriptor offset eight.
It snapshots status and attempts output word zero for a non-null context.
A successful read stores the failed-address word at context `+0x28`.
It notifies with the incoming predicate but returns one, including when the output read fails.
The mapping preserves this firmware behavior. It does not replace it with unconditional service success.

These are behavioral compatibility inferences, not recovered original export identities.
The paired imports preserve the real request, output read, and completion notification.
The adapter and reconstructed ROM bytes remain unchanged.

### Additional constructor-backed operation families

These additions use existing donor constructors or a complete retained-constructor witness.
The OEM dispatcher passes request `+0x10` as the lower descriptor `d`.
Each context offset below therefore follows from the constructor's actual stores.
The mappings are behavioral compatibility inferences, not recovered original export identities.

| Operation | Constructor mapping and body | Request mapping and body | Completion mapping and body | Context |
| --- | --- | --- | --- | --- |
| `0x1004` | `0x288 -> 0x278`, `0x21b7f0` | `0x674 -> 0x694`, `0x21e93e` | `0x678 -> 0x698`, `0x21ecba` | `d+0x10` |
| `0x2009` | `0x3f8 -> 0x40c`, `0x21be7c` | `0x72c -> 0x75c`, `0x220680` | `0x730 -> 0x760`, `0x2206e0` | `d+8` |
| `0x2005` | `0x530 -> 0x548`, `0x21bba6` | `0x780 -> 0x7b0`, `0x2201d0` | `0x784 -> 0x7b4`, `0x22020a` | `d+0` |
| `0x2006` | `0x8cc -> 0x90c`, `0x21bcc2` | `0x7c0 -> 0x7f8`, `0x2205e8` | `0x7c4 -> 0x7fc`, `0x220658` | `d+8` |

#### Operation `0x1004`

OEM constructor loads occur at `0x4289dc/0x42892c`.
Calls at `0x4289f2/0x428942` enter the already-mapped donor constructor.
Constructor `0x21b7f0..0x21b82e` stores arguments `a1,a0,a3,a4,a2,a5` at consecutive lower-descriptor words.
The request selects a real slot and stores it at context `+0x2c`.
Its failure paths preserve the callback, slot release, and minus-one reset.
The completion snapshots output index zero and releases that slot only for a zero predicate.

The completion performs no output-buffer transfer and returns the incoming predicate.
Required imports for dispatch, request programming, input transfer, snapshot, and notification already exist.
The mappings do not replace unsupported type rejection or slot allocation.

#### Operations `0x2009` and `0x2005`

Constructor `0x21be7c` stores two scalar arguments and context at request `+0x10/+0x14/+0x18`.
OEM calls at `0x41c3cc` and `0x43734a` supply `(4,1,context)` and `(4,2,context)`.
The request emits command `0x1d` only after its precondition and all indexed writes succeed.
The completion snapshots and notifies with the incoming predicate but returns one unconditionally.
It does not read output data or release a slot.

Constructor `0x21bba6` instead stores only its context at request `+0x10`.
Original constructor `0x21baa2` corroborates that opcode and layout.
The request emits command `0x20` through the existing backend submission path.
The completion snapshots only for a zero predicate, notifies, and returns that predicate unchanged.
This mapping does not implement an otherwise unsupported backend command.

#### Operation `0x2006`

OEM import loads `0x4220e2/0x4220ba` and `0x4393ae/0x43962a` supply `(address,byte_count,context)`.
They do not supply a buffer descriptor.
Original constructor `0x21bbf0` and donor `0x21bcc2` preserve those argument roles.
The constructor stores them at request `+0x10/+0x14/+0x18`.
Each OEM caller checks both the constructor return and context status `+0x20`.

| Cohort | Submit selector, arm, and load | Completion selector, arm, and load |
| --- | --- | --- |
| SM8850 | `0x443340 -> 0x41f9c2 -> 0x41f9c6` | `0x44342c -> 0x41fd14 -> 0x41fd18` |
| SM8845 | `0x443344 -> 0x41fa42 -> 0x41fa46` | `0x443430 -> 0x41fd94 -> 0x41fd98` |

The constructor retains range validation and rejects zero length.
For lengths through `0x207`, it clears the translated bytes directly and invokes the caller's callback.
Larger lengths use an internal stack synchronization context for the first operation.
Only a zero dispatch result and zero internal status permit the second operation with the caller's context.
Neither internal request storage nor its context outlives that synchronized operation.

The request writes indexed words zero through three as `1,address,byte_count,address` for command `0x11`.
The completion only notifies and returns its incoming predicate.
It performs no output read, copy, or status snapshot.
The backend must perform the real memory operation before it publishes completion.

These groups produce a 294-entry map with SHA-256 `049bd3e364d6cb8155a8fe165cc73d058509509a0405d43e98e417791c25ca65`.
Constructor evidence alone does not establish successful backend execution, image admission, or kernel execution.

## APDP authorization ABI and bounded denial

### Scope and evidence classes

The mappings below connect complete constructor, submit, and completion families.
An operation number alone does not establish an ABI mapping.
These are static contracts, not new native execution results.
Reconstructed identities and integrated native outcomes remain pending.
No vendor payload belongs in the adapter, map, or this evidence.

Outer image authentication remains a separate security boundary.
Registered-memory range admission does not establish signature authentication.
Debug authorization and ROM-patch authorization remain denied.
This adapter does not implement successful token parsing or entitlement publication.

The earlier canonical-run observations below predate this dependency group.
A later parent capture established the incorrect command-error publication described here.
Neither observation proves the corrected denial path or Linux execution.

### Feature `0x186`: request and completion objects

Legacy constructor `0x1f4` maps to donor `0x1ec`, the aggregate importer at `0x223d26`.
Its command helper is donor import `0x5dc`, body `0x224974`.
That helper constructs borrowed stack objects and invokes constructor `0x274`, body `0x21bfc6`.
The constructor submits operation `0x3003`, whose sequencer command is `0x29`.
Legacy submit `0x664` maps to donor `0x684`, body `0x21fece`.
Legacy completion `0x668` maps to donor `0x688`, body `0x21ff1e`.
These mappings retain the OEM dispatcher and its live callbacks.

Let `S` denote the stack pointer after donor `0x22497a`.
Let `C=S+0x0c`, `B=S+0x38`, and `I=S+0x48`.
All offsets in the following tables are hexadecimal.

| Object field | Contents and consumer |
| --- | --- |
| `C+00` | Callback-pointer container `0x232b68`, whose first word is `0x21981e` |
| `C+04` | Snapshot of indexed word 2, `CMD_RESPONSE_STATUS` at MMIO `0x02230144` |
| `C+08` | Snapshot of indexed word 3, `CMD_ERROR_STATUS` at MMIO `0x02230148` |
| `C+0c` | Snapshot of indexed word 4, `CMD_SUPP_ERROR_STATUS` at MMIO `0x02230160` |
| `C+10` | Snapshot of indexed word `0x1e`, MMIO `0x02230164`, exact meaning unresolved |
| `C+14..+1c` | Three optional output words, zero because the snapshot wrapper selects output index minus one |
| `C+20` | Completion reason, initialized to one, then updated through the genuine callback |
| `C+24` | Borrowed OEM synchronization object, obtained through live import `0x3b8` |
| `C+28` | Pointer to output descriptor `B` |
| `B+00` | Zero primary pointer |
| `B+04` | Output destination, donor global mask word `0x48cbb0` |
| `B+08` | Four-byte output capacity |
| `B+0c` | Actual output length, initialized to zero |
| `I+00` | Borrowed caller token pointer |
| `I+04` | Zero alternate pointer, so input transfer selects `I+00` |
| `I+08` | Caller input capacity |
| `I+0c` | Caller input used length, also sent through indexed word 13 |

Donor `0x224994..0x2249d8` constructs these objects.
Words `C+4..+1c` initially contain zero.
The helper also clears the global mask before dispatch.
For the captured token, the input pointer was `0x189fd000` and both input lengths were `0x2d0`.
These values describe that input, not fixed ABI constants.

The owning frame reserves `S+00..+08` as padding or scratch.
Its canary is at `S+58`.
Saved `s3,s2,s1,s0,ra` occupy `S+5c..+6c`.
These words are not context fields.
The frame owns `C`, `B`, and `I` until the helper returns.
It neither allocates nor frees the borrowed synchronization object.
Adjacent words after `0x232b68` are parser jump targets, not established virtual methods.

Constructor `0x21bfc6` calls shared prologue `0x21c096`.
That prologue allocates `0x90` bytes and zeros `0x78` bytes for request `R`.
The constructor and helper `0x21c0d0..0x21c0e2` establish this layout:

| Request field | Contents |
| --- | --- |
| `R+00` | Zero |
| `R+04` | Operation `0x3003` |
| `R+08..+0c` | Zero |
| `R+10` | Pointer to `I` |
| `R+14` | Pointer to `C` |
| `R+18..+77` | Zero |
| `R+78..+7b` | No request field established |
| `R+7c` | Canary |
| `R+80..+8c` | Saved state |

The shared helper calls live import `0x540` with `(R,C)`.
OEM resolver `0x41fa2e` supplies lower descriptor `d=R+0x10`.
Thus `d+0=I` and `d+4=C`.
No pointer adjustment changes the context base.
Request storage remains live until the synchronized constructor restores its frame at `0x21c0c6`.

Submit acquires the engine through `0x730`, writes command `0x29` through `0x660`, and transfers input through `0x724`.
It launches through `0x6e8`.
Completion polling retains ownership, readiness, result, and acknowledgment dependencies.
Completion `0x21ff1e` snapshots through `0x6d0 -> 0x6d4`.
Only a nonzero backend predicate permits output transfer through `0x748`.
Notification through `0x558 -> 0x554` converts predicate `p` to reason `(p-1)&2`.
The genuine callback `0x21981e` stores the reason and forwards through `0x52c`.
Forwarding calls `sync.vtable[4]`, not the adjacent word `sync[4]`.
Reset uses `sync.vtable[8]`, and wait uses `sync.vtable[0]`.

### Status separation and aggregate failure

Response bit zero at `0x144` determines the backend Boolean.
The raw command error at `0x148` is a separate word.
Callback reason at `C+0x20` is also separate.
Helper `0x2249ee` reads `C+8`, not `C+0x20`.
At `0x2249f4`, a nonzero command error suppresses the mask copy.
Importer `0x223e60` tests that returned word for zero before parser entry.

The distinction identifies the role of each field, not a vendor denial enumeration.
The bounded denial policy defines `0xffffffff` at `CMD_ERROR_STATUS` as an explicit virtual-policy failure.
This value is not a recovered vendor enum or the callback reason.
The decoded denial consumers test only zero versus nonzero.
Submission failure remains separate and can return `0x1000` without a completed snapshot.

The prior atomic capture used `C=0x4084dc` throughout completion, notification, callback, and helper return.
It recorded response `C+4=1`, command error `C+8=0`, and callback reason `C+20=2`.
The helper returned zero, and importer `0x223e60` saw zero.
This captured failure demonstrates incorrect status publication, not a lost callback.
It does not establish the corrected native outcome.

With completed denial, zero output, and nonzero command error, the real importer takes its per-record error branch.
The importer increments its attempt count through donor `0x1fc/0x22c`.
It stores the nonzero per-record error through donor `0x228`.
No successful record means zero parser successes.
The aggregate selects sentinel `0x59657321`, then converts it to result `0x4005` at `0x223d82..0x223d98`.
The aggregate result is not the per-record command-error value.

The denied path must not call parser `0x180`, ingest `0x1a4`, or publication helper `0x5b8`.
It must not initialize an authorization context or copy a mask.
It must not call mask setter `0x21c`, processed setter `0x234`, or final publication `0x220`.
The mask remains zero, and the processed byte remains false.
The OEM record processor can still return zero after recording the feature result.
That service return does not grant authorization.

### Callback ownership and unsupported positive paths

Only three adapter bindings are required by the bounded importer denial:

| Donor live slot | Retained legacy slot | OEM backing state |
| --- | --- | --- |
| `0x1fc` count getter | `0x204` | Word `0x4480bc` |
| `0x22c` count setter | `0x234` | Word `0x4480bc` |
| `0x228` error setter | `0x230` | Word `0x4480c0` |

Each trampoline loads the retained OEM pointer and tail-jumps through it.
It preserves argument registers, stack pointer, and return address.
A null retained callback reaches the existing invalid path.
The legacy map rows for `0x204`, `0x234`, and `0x230` remain legacy-owned.
They must not map back to the donor wrappers, which would recurse.
The map has one row per legacy slot, including all three rows of each authorization family.

The adapter no longer supplies newly authored bindings for `0x198`, `0x21c`, `0x234`, or `0x5a4`.
Their removed trampolines are unnecessary on the bounded denied path.
Unrelated preexisting legacy-owned rows remain unchanged.
No donor parser binding for `0x1a0` is added.
No authorization context is allocated or extended into its neighboring buffer.
Unsupported positive authorization remains unsupported and must fail closed.

### Feature `0x184`: complete ROM-patch family

The SM8850 payload contains a second record at file offset `0x12d0`, after the feature-`0x186` record.
Its feature is `0x184`, body length is `0x298`, and total length is `0x29c`.
The selected SM8845 payload contains only the `0x186` record.
Both supplied APDP files are ELF images with record data at file offset `0x1000`.

| Role | Original body | Donor body | Legacy-to-donor import |
| --- | --- | --- | --- |
| Constructor | `0x21bee4` | `0x21bfa4` | `0x4a4 -> 0x4b8` |
| Submit | `0x21fd6e` | `0x21fe58` | `0x740 -> 0x770` |
| Completion | `0x21fdc0` | `0x21fea6` | `0x744 -> 0x774` |

SM8850 selects the callback at `0x440fd4..0x440fd8` and `0x44108a..0x4410b0`.
The call passes `a0=B=sp+0x30` and `a1=C=sp+0x40`.
SM8845 has the same call contract at `0x441308..0x44132e`.
The buffer contains `B+0=0`, `B+4=record_header`, and total length at both `B+8` and `B+0xc`.
The transfer consumes the header and body, not just the body.

SM8850 constructs `C` at `0x440e44..0x440e5c`.
It sets `C+0=0x443008`, zeros `C+4..+1c`, sets `C+20=1`, and stores the synchronization object at `C+24`.
Before each record, live reset `0x50c -> 0x524` clears snapshots and resets the reason.
OEM callback `0x41c498` stores reason at `C+20`, then forwards through `0x514 -> 0x52c`.
These genuine synchronization callbacks remain unchanged.

Original helper `0x21c00e..0x21c020` and donor helper `0x21c0d0..0x21c0e2` establish request operation `0x3002`.
They store `B` at `R+0x10` and `C` at `R+0x14`.
The lower descriptor is `d=R+0x10`, with `d+0=B` and `d+4=C`.
Original dispatcher `0x528` maps to donor live dispatcher `0x540`.
This shared request envelope retains the synchronized request lifetime described above.

SM8850 submit arm/load are `0x41fa12/0x41fa16`.
Its completion arm/load are `0x41fd6e/0x41fd72`.
SM8845 equivalents are `0x41fa92/0x41fa96` and `0x41fdee/0x41fdf2`.
Donor selector word `0x2324b8` targets `0x21c380`, which loads submit `0x770`.
Donor selector word `0x2325fc` targets `0x21c636`, which loads completion `0x774`.

Submit `0x21fe58..0x21fea4` acquires through `0x730`.
It writes command `0x18` at indexed word one and input length at indexed word 13.
It transfers `B` through `0x724` and launches through `0x6e8`.
This is ROM-patch authorization command `0x18`, not debug command `0x29`.
Completion `0x21fea6` preserves predicate `p`, snapshots through `0x6d0`, notifies through `0x558`, and returns unchanged `p`.
Its index-minus-one snapshot requires no output-word reads.
With a false predicate, genuine notification records reason two.
SM8850 `0x4410be` reads that reason and reaches the ordinary per-record error branch.

These constructor stores, selector loads, submit inputs, and completion consumers establish the complete family.
They do not establish safe positive ROM-patch authorization or patch installation.
The permitted substitute remains completed denial with zero output and no entitlement publication.

### Denial outcome

Corrected native evidence is retained in `artifacts:2026-10-22-tme-report-captures/apdp-denial-proof.json sha256=b63709335c1fa22a59a2c58cb7bcad87ea31b0f21eee0af8b149e38ce36d76d8`.
The SM8850 capture records `C+8=0xffffffff` and `C+20=2` at `0x2249ee`.
Helper return at `0x224a02` and importer predicate `s1` at `0x223e60` both contain `0xffffffff`.
The attempt count at `0x4480bc` becomes one.
The last error at `0x4480c0` becomes `0xffffffff`.
The mask at `0x4480b4` and processed byte at `0x4480b0` remain zero.
The first `0x29c` bytes at authorization context `0x41275c` remain zero.

The capture records no hits at parser `0x224a7a`, ingest `0x224e74`, or publication sites `0x223eda`, `0x223eea`, and `0x223f06`.
Aggregate capture at `0x223dac` records `a0=s2=0x4005`.
Instruction `0x223daa` assigns `s2` to `a0`.
A breakpoint before that instruction still sees the canary in `a0`, not the return result.
The OEM record-processor return at `0x43747c` is zero.
Execution reaches continuation `0x4374ca` with the denied-state scalars intact.

63 integration tests passed.
Canonical SM8850 run15 denies commands `0x29` and `0x18`, then logs `APDP Image Loaded`.
Canonical SM8845 run14 denies command `0x29`, then logs the same image-load event.
Both runs also reach `boot_dload_dump_security_regions`, `EUD Init`, and blacklist enable.
Their next fault loads unsupported legacy slot `0x1a8`.
These results establish the bounded APDP denial, not successful authorization or Linux execution.

## Unsupported accesses and execution claims

These ABI mappings require no SoC-specific C++ branch.
Unknown MMIO remains unmapped or fails through the existing model's protocol checks.
The selected clock routine also references a packed root at `0x148effb8`.
Its low address word is `0x199a2010`, and its alternate path performs 64-bit operations at offsets zero and `0x20`.
This change does not invent an MMIO protocol for that address.
It also does not import SM8850-only DDR or thermal tables to hide a later fault.

Static ELF decoding and package-table inspection supplied the evidence above.
The complete integration suite passes 62 tests.
Both canonical OEM runs report the completed `XBL_AC_CFG` load and reach APDP metadata authentication.
Both then stop at missing authorization import `0x1f4`.
These results do not establish APDP authorization, complete authentication qualification, or kernel execution.

## Post-APDP report query: retained OEM denial records

This group implements a bounded denial query, not authorization success.
It preserves the actual OEM report bytes.
It does not initialize or parse a positive authorization context.
The original upper-ROM bodies for legacy `0x1a8` and `0x1d8` remain unavailable.
The replacement contract therefore uses the selected OEM consumer, live ownership, and independent donor semantic witnesses.
This correspondence is an inference, not a recovered original implementation.

### Complete operation group and selected XBL consumer

The SM8850 handler starts at `0x432eb6`.
Its ten-entry table resides at `0x444714`.
The SM8845 handler starts at `0x432f0e`, with table `0x4447a8`.
Both dispatch only operation values zero through nine.
The input descriptor supplies a pointer and at least four bytes.
The output descriptor supplies a destination and capacity.
The handler clears output used length before dispatch.

| Operation | SM8850 arm | SM8845 arm |
| --- | --- | --- |
| 0 | `0x432f34` | `0x432f8c` |
| 1 | `0x432f82` | `0x432fda` |
| 2 | `0x433078` | `0x4330d0` |
| 3 | `0x432fc6` | `0x43301e` |
| 4 | `0x43318c` | `0x4331e4` |
| 5 | `0x432fe6` | `0x43303e` |
| 6 | `0x43303e` | `0x433096` |
| 7 | `0x43316e` | `0x4331c6` |
| 8 | `0x43315c` | `0x4331b4` |
| 9 | `0x432fd2` | `0x43302a` |

The table and every arm remain genuine OEM code.
Only the two report imports change.
The other arms retain their original configuration, state, reset, and error paths.
In particular, this adapter does not replace the enclosing handler with a successful stub.

Operation two requires a nonnull destination and capacity at least `0xc8`.
Invalid arguments return status three without publishing output length.
SM8850 getter `0x440dfe` and SM8845 getter `0x44107c` both return `0x415624`.
The caller passes this report pointer to legacy `0x1a8`.
It then passes a word output pointer to legacy `0x1d8`.
Only sentinel `0x59657321` permits the original copy path.
Failure logs event `0x502`, returns status one, and retains output length zero.

The copy path preserves all `0xc0` report bytes.
It prefixes a four-byte flag word and copies the resulting `0xc4` bytes to destination `+4`.
Destination `+0` receives the genuine copy result.
The handler then performs its original local policy calls and publishes used length `0xc8`.
Flag bits zero and one control separate OEM policy branches.
The denied query supplies neither bit.

The selected SM8850 `RADIO/xbl.img` supplies an independent consumer witness.
At `0x14876cf4..0x14876d00`, XBL calls `0x148a1bf0` with operation two, a destination, and capacity `0xc4`.
That wrapper writes the operation to shared address `0x2040c000`.
It sends service ten with a four-byte input through `0x148a1558`.
It checks the returned copy length at shared `+0x800` against the caller's capacity.
It copies data from shared `+0x804` and returns the real service status.
The wrapper rejects nonzero capacity with a null destination and capacity above `0xc4`.
The TME envelope's extra four bytes hold the copy result, not an additional report field.

XBL checks flag bit one at `0x14876d24..0x14876d34`.
Its report loop advances by twelve bytes at `0x14876fb0` and stops at `0xc0`.
Each record contains a feature identifier, record status, and result word.
The loop recognizes feature identifiers including `0x184` and `0x186`.
It distinguishes result `0x59657321` from `0x4005`.
Its strings distinguish “debug policy applied” from “no debug policy applied”.
Thus a successful query can carry genuine denial records without granting authorization.

### Candidate exports and callback ownership

Donor export table `0x233c84` does not establish slot correspondence by itself.
Neighboring static and live exports have different roles:

| Donor slot | Body or owner | Contract |
| --- | --- | --- |
| `0x190` | `0x2251b4` | Tail-call live `0x248`. Not a report constructor. |
| `0x194` | `0x225094` | Obtain the existing context through live `0x198`, then store its pointer. |
| `0x198` | live OEM callback | Existing context getter. Selected OEM returns `0x41275c`. |
| `0x1a4` | `0x224e74` | Parser/ingest path. Not selected for denial query. |
| `0x1a8` | `0x224126` | Ignore incoming report pointer, write control through `0x5b0`, then call `0x238`. |
| `0x1d0` | `0x22414e` | Clear output word, obtain context, derive two flags from presence byte `+4` and word `+8`. |
| `0x1dc` | `0x2241d8` | Lookup policy ID in context entries when presence bit three and count `+0x90` permit it. |
| `0x238` | `0x22484a` | Serialize compact three-byte policy-right records. Not the twelve-byte OEM feature report. |
| `0x240` | `0x22491a` | Apply ten configuration values through indexed writes. Not a scalar getter. |

Donor `0x1d0` returns `0x4002` for null output.
An existing zero context returns sentinel `0x59657321` with flags zero.
Presence byte bit zero gates its reads of word `+8`.
Word bits `0x10` and `0x20` become output bits zero and one.
This provides the flag semantic witness without allocating a context.

The rejected donor report path has the following full dependency closure:

| Donor dependency | Selected OEM correspondence | Evidence and effect |
| --- | --- | --- |
| `0x1d8`, destination | No safe selected destination | Legacy `0x1dc` returns `0x4467e0`, which contains the OEM ID array, not the feature report. Do not bind by proximity. |
| `0x208`, item count | Legacy `0x210`, OEM `0x420a98` | Reads count through legacy `0x1d4`, whose getter returns `0x447c28`. Captured count is ten. |
| `0x20c`, item ID | Legacy `0x214`, OEM `0x420a7e` | Indexes `0x4467e0`, with IDs `2,3,4,10,11,20,30,31,38,37` in hexadecimal. |
| `0x1dc`, item result | Donor `0x2241d8` | Uses the existing context through `0x194/0x198`. Zero context returns `0x4006`, not an authorization grant. |
| `0x240`, configuration completion | Donor `0x22491a` | Calls live `0x210` for ten values, then `0x5bc` for registers four through thirteen and register three. |
| `0x210`, configuration value | Legacy `0x218` candidate | Its OEM installer can select `0x420a64`, but the actual captured slot is zero. It is not safe to invent installation. |
| `0x5bc`, configuration writer | Donor `0x2247ae` | Bounds register index to thirteen and writes `0x07ad2000 + index*4`. |
| `0x5b0`, report control | Donor `0x2247a0` | Writes one to `0x00c00210`. No image-authentication or debug-grant result is produced. |

The compact serializer accepts item-result sentinel `0x59657321`.
It packs the ID byte and selected result bits into three bytes.
Result `0x4005` stops its loop, while other errors skip an item.
This differs from the XBL consumer's sixteen twelve-byte feature records.
Directly mapping legacy `0x1a8` to donor `0x1a8` would therefore leave unsafe destination and configuration dependencies.
The adapter does not select that mapping or install those callbacks.

### Actual guest state and bounded adapter

Capture `artifacts:2026-10-22-tme-report-captures/report-query-contract.json sha256=79b341df3bd981d54187f0e0565154cdf424627f5d1d40cc6e05af9ff8a7b4b3` stopped at SM8850 `0x433094`.
It read the existing report before any replacement report import ran.
The report contained these actual guest words:

`0x186, 0, 0x4005, 0x184, 3, 0xffffffff`, followed by zero words through its `0xc0` extent.

The context at `0x41275c` contained `0x29c` zero bytes.
The live count was one, the retained per-record error was `0xffffffff`, the mask was zero, and the processed byte was zero.
These are parent-observed values, not injected adapter data.
The adapter does not hardcode these feature IDs, their order, or count one.

The adapter retains these OEM scalar owners:

| Legacy slot | OEM body | State |
| --- | --- | --- |
| `0x198` | `0x420aac` | Context pointer `0x41275c` |
| `0x204` | `0x420c8c` | Actual attempted count through pointer getter `0x1b8`, state `0x4480bc` |
| `0x200` | `0x420cc0` | Retained error through pointer getter `0x1b4`, state `0x4480c0` |
| `0x208` | `0x420bda` | Processed byte through pointer getter `0x1bc`, state `0x4480b1` |
| `0x1f8` | `0x420c58` | Mask through pointer getter `0x1ac`, state `0x4480b8` |

Legacy `0x1a8` now validates the exact OEM report owner and reads the context through its real getter.
It accepts only a nonnull, aligned context whose bounded `0x29c` bytes are all zero.
It rejects a processed record or a nonzero mask.
Attempt count, per-token error, and aggregate report results remain separate layers.
Neither count nor error is a precondition for reading an existing report.
The adapter does not compare a raw per-token error with an aggregate report result.
It neither changes report bytes nor constructs substitute feature records.
An unsupported or positive state fails closed.
This denial-only policy does not claim support for every original authorization state.

After validation, it requests virtual report-control retention and sets a one-use latch at `0x464104`.
The latch is the only private query state.
Legacy `0x1d8` clears its output before checking that latch.
It consumes the latch and returns flags zero with the sentinel only for the validated denial.
Null output returns `0x4002`.
Missing validation returns `0x4005`, so the original OEM handler takes its real failure branch.
Every prepare call clears the latch before any validation.
ABI activation also clears it.
No heap storage, positive-context parser, publication callback, or grant path is added.

### Explicit virtual control policy

The runtime sequencer owns a four-byte report-control socket at `0x00c00210`.
The runtime platform binds it separately from command and authorization registers.
It requires the explicit `control_state_substitute` policy.
The socket accepts only aligned full-word accesses at offset zero.
It rejects writes with any bits other than bit zero.
Reads return the retained value.
Value zero clears retention, and value one records the report-control request.
Changed values produce a `virtual report control retained` log.

Retention starts at zero for each model instance.
ABI activation writes zero, and each validated query writes one.
This virtual effect does not alter debug access, fuse policy, image authentication, sequencer completion, or commands `0x18` and `0x29`.
The physical effect of this control remains unknown.
This explicit retention policy replaces no authentication decision.

No build, test, formatter, firmware launch, or recipe-hash update ran for this evidence.
Both OEM smoke runs and integration verification remain pending.
Required observations are the real flag sentinel, flags zero, unchanged report bytes, used length `0xc8`, and continued original-handler execution.
Negative checks must retain zero used length for inadequate capacity and invalid report state.

### Post-copy security-summary closure

SM8850 run16 and SM8845 run15 then ran.
Both logged report-control retention one, then stopped at legacy `0x578`.
The faults occurred at SM8850 `0x4211cc` and SM8845 `0x42124c`.
These accesses still belong to the selected report handler.
They do not establish completed report-query execution.

After copying the output, SM8850 `0x433112` calls summary constructor `0x4211a8`.
The SM8845 counterparts are `0x43316a` and `0x421228`.
The constructor populates OEM summary storage at `0x44853c`.
It records its progress in summary word `+0x70`.
Its selected read dependencies require the following complete mapping group:

| Legacy → donor | Producer and destination | Contract |
| --- | --- | --- |
| `0x578 → 0x590` | `0x2246ea`, summary byte `+6` | Read bit five of the existing fuse snapshot word at donor `0x21c8604`. Check the repeated read through the integrity dependency. Null output returns three. |
| `0x594 → 0x5ac` | `0x224650`, summary word `+8` | Translate lifecycle nibble from snapshot `0x21c873c` through donor table `0x232b24`. Check the repeated nibble. Null or unsupported input returns three. |
| `0x58c`, unchanged | Retained OEM `0x420cf8`, summary word `+0xc` | Existing OEM cached policy query. Preserve its pointer, initialization, transport, and error ownership. |
| `0x584 → 0x59c` | `0x2246c8`, summary word `+0x10` | Read grant mask from `0x00c00224`. Null output returns three. |
| `0x588 → 0x5a0` | `0x2246a6`, summary word `+0x14` | Read second grant mask from `0x00c00228`. Null output returns three. |
| `0x580 → 0x598` | `0x2247e4`, summary words `+0x18,+0x1c` | Indexed fuse-snapshot view. Donor accepts indices zero through four, returns zero for indices below three, and checks repeated snapshot reads for indices three and four. The OEM calls indices zero and one. |
| `0x590 → 0x5a8` | `0x2247c8`, fourteen words at summary `+0x24` | Read actual configuration register `0x07ad2000 + index*4`. Null output or index above thirteen returns three. |

Donor integrity import `0x5ec` is body `0x217e5a`.
It calls donor `0x5fc=0x219862`, then tail-calls donor `0x5f0=0x217cac`.
The former obtains existing synchronization state through `0x3bc`.
The latter retains its existing `0x610` random/delay and integrity machinery.
These donor imports already belong to the reconstructed runtime.
They are not legacy slot `0x5ec`, whose bridge mapping has a different role.
No new callback replaces this integrity chain.

The retained OEM policy query uses its existing cached words at `0x4480c4,+4`.
Its uncached path calls OEM `0x43648a` and extracts two bits from the returned word.
This path was already used by APDP policy evaluation.
The query group does not remap its live ownership or change its policy.

After collecting the summary, constructor `0x4211a8` tests `(summary[0x10] & 6) == 6`.
Only that positive-mask branch calls legacy `0x194` with an output pointer.
The supported no-grant view returns mask zero, so this branch is not selected.
The constructor retains its original magic, version, progress, status, and log writes.
It does not replace report bytes with summary bytes.

The handler next calls the existing registry getter `0x41c5c0`.
That getter retains its initialized registry and existing initialization path.
Flag-controlled mutators remain unselected because the derived report flags are zero.
The captured handler state has `s2=0`.
Therefore, it also skips the separate `s2==2` transition helper at `0x433226`.
The original handler can then publish output used length and return.
These selected-branch facts do not claim implementation of unsupported positive-grant paths.

The model exposes both summary grant words through one read-only socket at `0x00c00224..0x00c0022b`.
The socket requires both explicit authorization-denial options.
Under that policy, neither engine can produce a grant, so both summary views return zero.
This is an explicit virtual no-grant view, not a recovered physical register implementation.
Without both denial options, access fails.
Every write fails, including a zero write.
Unknown offsets, partial accesses, and unaligned accesses fail.
No setter, parser, entitlement publication, or positive authorization effect is added.
The fourteen configuration words still come from their existing retained register bank.

The earlier speculative authorization socket was removed before native denial was established.
This bounded read-only view serves the now-decoded post-copy consumer.
It does not restore a writable mask-publication shortcut.
Commands `0x18` and `0x29`, their denial errors, and their output lengths remain unchanged.


### Retained OEM policy getter: corrected fuse dependency

SM8845 run16 advanced through the summary import group.
Its next fault was `0x43686e`, reading the corrected policy row at `0x02302090`.
Capture `artifacts:2026-10-22-tme-report-captures/report-handler-acceptance.json sha256=9d42f174fca4b2b0b1a5eec514fa5c8de1fd96367144025c3b7bc591859d4ef6` confirms flags zero and the genuine report before this fault.
The original handler had not yet published used length.

The selected chain is report handler `0x43316a`, summary `0x421228`, retained policy getter `0x420d78`, and generic fuse reader `0x436706`.
The reader resolves raw lookup key `0x021c0090` to corrected row `0x02302090`.
Both OEM descriptors select one eight-byte row and error mask `0x20`.
The selected closure includes policy words `0x02302090,+4`, error status `0x02302098,+4`, and access status `0x023020a0,+4`.
Descriptor mode zero excludes the row-enable words at `0x023020a8,+4`.

The existing fuse controller now owns that complete selected group through its optional corrected-policy socket.
The platform removes the former zero-filled status memory.
The row derives from the same `oem_security_policy_lsb` parameter as the canonical policy view.
The profile remains `0x832`, with the same lifecycle and authorization requirements.
Zero aggregate FEC state returns zero status bitmaps.
Nonzero aggregate FEC state returns all-one status bitmaps under an explicit virtual fail-closed policy.
This mapping lets the genuine descriptor mask select failure.
It does not claim a recovered vendor bitmap encoding.
Writes and unknown fields fail, and the unselected row-enable words remain unsupported.

See `models/qcom/sm88xx/fuse_controller/evidence.md` for both descriptor addresses, the complete getter chain, and status-policy boundaries.

### Native report completion

SM8850 `run-sm8850-common-runtime18` and SM8845 `run-sm8845-common-runtime17` ran next.
Both runs passed the corrected policy query and completed the original report handler.
Read-only debugger captures stopped at the original instructions without changing firmware, registers, or policy.

| Result | SM8850 | SM8845 |
| --- | --- | --- |
| Flag result | `0x59657321`, flags zero | `0x59657321`, flags zero |
| Original records | `0x186,0,0x4005` and `0x184,3,0xffffffff` | `0x186,0,0x4005` |
| Used length | `0xc8` at `0x433158` | `0xc8` at `0x4331b0` |
| Handler return | Zero at `0x43316c` | Zero at `0x4331c4` |

Both output buffers contain prefix words `0xc4,0`.
Each output preserves all `0xc0` original report bytes at offset eight.
Neither path creates a grant or equates an aggregate result with a raw command error.
Both native boots next reach the APSS NCC gasket operation.
Kernel entry remains unproved.

### DDR-training request import

SM8850 run88 and SM8845 run85 stop at legacy table address `0x4168f0`.
Both callers enter this path after the original `sbl1_ddr_init` completes.
The missing entry is legacy slot `0xf0`, not an unmapped hardware register.

The original callsite at `0x21b38e` corresponds to donor callsite `0x21b162`.
Both pass an identifier, the retained second argument, and a context at `sp + 0x4c`.
Both exclude identifiers `0xaaaaaaaa` and `0xffffffff` before the call.
The donor callsite selects slot `0xf4`.
Its table at `0x233c84` supplies function `0x21b7b6`.
Thus the bridge maps legacy `0xf0` to donor `0xf4`.

Original wrapper `0x21b660` and donor wrapper `0x21b7b6` preserve the same request contract.
Their outlined prologues retain the three arguments and clear a `0x78`-byte request.
Both write the identifier to context `+0x2c`.
Both select operation `0x1002` at request `+4`.
Request fields `+0x10`, `+0x14`, and `+0x18` contain the second argument, identifier, and context.
Both invoke the existing dispatcher with the request and context, then return its status.

SM8850 `0x42c374` and SM8845 `0x42c3da` supply the identifier, selector two, and context at `sp +4`.
Both retain the returned status before copying the context summary into their output.
Each caller requires zero transport status before it checks the summary and context status.
The mapping preserves those checks and the actual operation.
It does not supply a successful return or modify an authentication decision.

#### Submission and completion imports

SM8850 run89 and SM8845 run86 pass the constructor import and reach the operation dispatcher.
Both then stop at legacy slot `0x650`.
The complete operation requires its submission and completion imports:

| Legacy slot | Donor slot | Original body | Donor body |
| --- | --- | --- | --- |
| `0x650` | `0x670` | `0x21e4ca` | `0x21e6e4` |
| `0x654` | `0x674` | `0x21e56e` | `0x21e788` |

SM8850 operation table `0x4432e8` selects submission target `0x41f972`.
SM8845 table `0x4432ec` selects target `0x41f9f2`.
Their import loads select legacy `0x650`.
Each passes the descriptor at request `+0x10`.
The queue retains a complete copy of the `0x78`-byte request.
The context remains caller-owned.

Both submission bodies retain the identifier in context `+0x2c`.
They write indexed fields `1=0xd`, `7=0`, `8=0`, `9=0`, `0xa=identifier`, and `0xf=selector`.
They then start the sequencer operation and clear the identifier's retained bitmap bit.
A checked preparation failure delivers context status two and returns false.
The mapping does not complete the hardware operation itself.

Completion tables `0x4433d4` and `0x4433d8` select legacy `0x654`.
The completion bodies preserve the incoming status and collect the seven-word result into context `+4..+0x1c`.
They clear the identifier's bitmap bit only when the completion status is zero.
They reset context `+0x2c` to `0xffffffff` and notify the context.
The existing notifier delivers context status zero for completion status zero, otherwise status two.
All indexed accessors, result collectors, and notification imports already have mappings.

The separate inventory covers other admitted `0x100x` operations.
Both OEM admission routines accept only `0x1001..0x1008`, `0x1010`, and `0x1011`.
Their mask is `0x180ff`, applied to the opcode minus `0x1001`.
Operation `0x1012` has ROM constructors but fails the OEM admission check.
The map does not change this policy or add unproved export identities for other families.

SM8850 run90 and SM8845 run87 pass the added submission import.
Both next stop on a sequencer write to `0x02230010`, at donor PC `0x21cd7e`.
This is a hardware metadata boundary, not another missing legacy import.
Native completion of operation `0x1002` remains unobserved.
The complete host suite passes 78 tests, and Clippy passes with warnings denied.

### DDR key allocation and sequencer metadata

The prior key-load failure occurred before the hardware command.
SM8850 diagnostic run92 stops at donor `0x21e3fe`, before the upper-bound comparison.
The destination is slot `0x1e`, but the selected range is `0..0xf`.
The failure path invokes the context callback with status two.
It does not acquire the sequencer or submit SYSTEM_KDF.

Both OEM identifier tables assign hardware slots `0x1c..0x30` to API identifiers `0x12..0x26`.
The SM8850 table starts at `0x4454fc`. The SM8845 table starts at `0x4455cc`.
DDR requests API identifier `0x14`, which maps to hardware slot `0x1e`.
Donor `0x21e216` decodes the virtual geometry register at `0x02237014`.
Its four five-bit counts define consecutive groups from the five-bit base at bits `28:24`.
The fixed groups occupy slots `0x16..0x1b`.

The virtual geometry now uses value `0x1c0294b5`.
The base is `0x1c`, and the counts are 21, 5, 5, and 5.
The groups are `0x1c..0x30`, `0x31..0x35`, `0x36..0x3a`, and `0x3b..0x3f`.
They do not overlap the fixed groups or exceed the allocator's 64-slot limit.
This is a virtual capacity policy, not a recovered silicon value.
The model does not remove the firmware's range checks.

The sequencer retains indexed metadata at offsets `0x10`, `0x14`, `0x18`, `0x30`, `0x40`, `0x44`, `0x48`, `0x4c`, and `0x54`.
Donor accessor `0x21fca2` selects these offsets through table `0x2326f0`.
Writes require an acquired engine, as other command metadata does.
Retention does not create keys, complete operations, or grant authorization.

SM8850 run93 and SM8845 run88 reach command `0x2` with arguments zero, three, and zero.
Both stop at the then-unimplemented SYSTEM_KDF command.
Those runs do not execute the complete derivation, HMAC, and cleanup sequence.
The earlier `0x4005` report matches retained APDP data, not a fresh hardware result from the rejected key-load request.
The exact source of that stale report remains unresolved.

### Native DDR HMAC lifecycle

SM8850 `0x42c7e6` and SM8845 `0x42c85c` read the keyed-HMAC entry at `0x416b94`.
The legacy table starts at `0x416800`, so this entry is slot `0x394`.
The initial attribution to `0x294` was an arithmetic error.
The map contains only the corrected `0x394 -> 0x394` entry.

Donor initializer `0x233c84 + 0x394` supplies facade `0x21a4e4`.
The facade preserves the encoded algorithm, translated key slot, input descriptor, credential, and context.
It constructs internal operation three, which existing submission and completion mappings already cover.
The outer APSS opcode `0x17` is not the internal operation number.
No new argument adapter or successful-return substitute is required.

The selected command-four request computes HMAC-SHA-384 over the actual 32-byte DMA input.
The completion copies 48 output bytes and publishes that length through the original OEM response.
The explicit virtual key profile supplies coherent derivation and cleanup effects.
`models/qcom/sm88xx/tme_mailbox/evidence.md` separates its virtual root and policy binding from recovered hardware facts.

SM8850 run95 and SM8845 run90 execute the complete request sequence.
The APSS traces reach successful HMAC branches `0x148c06cc` and `0x148beb24`.
They then reach successful cleanup paths `0x148c0654` and `0x148beaac`.
Both OEMs enter fresh DDR training and poll PHY offset `0x8f98` until timeout.
The complete suite passes 79 tests. Clippy passes with warnings denied.
Kernel entry remains unproved.

### Native SMEM diagnostic provider

Original SM8850 helper `0x43d390` requests existing item `0x1d5` with a size output.
Legacy slot `0xa28` maps to donor slot `0xa64`, body `0x22d8f6`.
This function locates an existing item. It does not allocate missing storage.
The original consumer requires size `0x1000`.

Original hook `0x436228` passes storage `0x4122bc` through legacy slot `0xa54`.
That slot maps to donor `0xa9c`, body `0x22e1cc`, which discovers the actual partitions.
The installed capacity hook `0x43625c` returns 26, encoded as `0x1a`.
Each record occupies `0x2c` bytes. The complete storage ends at `0x412734`, the next referenced OEM object.
The provider hooks remain original OEM functions, not donor default stubs.

XBL publisher `0x14850450` writes its actual descriptor pointer through the WONCE words at `0x01fd4000` and `0x01fd4004`.
The donor view at `0x07ad4000` aliases those words.
The donor mutex view at `0x07a43000` aliases the existing mutex at `0x01f43000`.
Neither alias creates another descriptor, partition heap, or mutex.
The original descriptor, version, partition, and allocation checks remain active.

SM8850 run122 stops after the lookup at `0x43d39e`.
The returned pointer is `0x81d01030`. The size output is `0x1000`.
The original diagnostic writes its `0x80` bytes at item offset `0xe00`.
The subsequent LLCC validator returns zero at `0x43317a`.
The initial startup class is zero, as observed in `s2` at `0x43316e`.
That value selects cold helper `0x433226` before the diagnostic.

The run then reports a failed DARE operation for row three in all four banks.
Milestone nine, PIMEM completion, UEFI, ABL, and kernel execution remain unproved.
The complete suite passes 92 tests. Clippy passes with warnings denied.

### Image policy getters and OEM context

SM8850 run136 stops at `0x43a5d4` while reading legacy pointer `0x4169f0`.
The bridge rejects this missing import. The address is not an AOP readiness register.
Both selected OEMs use the same adjacent policy interface.

| Legacy slot | Donor slot | Genuine donor body | Contract |
| --- | --- | --- | --- |
| `0x198` | `0x198` | OEM-installed callback | Return the actual retained context pointer. |
| `0x1e4` | `0x1dc` | `0x2241d8` | Query a policy identifier and copy its value. |
| `0x1e8` | `0x1e0` | `0x2243ca` | Query an image record and copy its algorithm, data pointer, and length. |
| `0x1ec` | `0x1e4` | `0x224260` | Return the retained bitmap pointer and byte count. |
| `0x1f0` | `0x1e8` | `0x2242e0` | Query an image identifier and write its enabled byte. |
| `0x1f4` | `0x1ec` | `0x223d26` | Preserve the existing aggregate authorization importer. |

The four getters call donor `0x194`, body `0x225094`.
That function calls the live OEM callback through runtime slot `0x489198`.
SM8850 installer `0x420ede` supplies function `0x420aac`.
SM8845 installer `0x420f5e` supplies function `0x420b2c`.
Both original callbacks return context `0x41275c`.
Routing the installer write preserves one authoritative callback instead of copying its pointer.

The image query checks pointer validity, context presence, bitmap bounds, lifecycle, and the applicable image record.
Missing context presence bit `0x04` returns `0x4006`.
The original wrappers accept the output byte only after return sentinel `0x59657321`.
An error therefore retains the original false result.
The map supplies genuine code, not a fixed policy decision or authorization grant.
These bindings are compatibility inferences from both consumer contracts, not recovered original export identities.

### Shared IMEM for AOP image consumers

SM8845 run113 reads an unmapped byte at `0x18689580`, from copy PC `0x230bbc`.
The active converter at `0x2316a4` uses table `0x2335a0`.
Its system base `0x00100000`, local base `0x04100000`, and size `0x1ef00000` translate APSS `0x14689580` to that address.
The byte belongs to initialized AOP code at file offset `0x51580`.
It is not an invented buffer or a zero-fill request.

Donor bootstrap table `0x213da0` declares IMEM local base `0x18680000`, system base `0x14680000`, and size `0x2c000`.
The new local view aliases the existing `apss_imem` backing.
APSS, UFS DMA, sequencer DMA, CPUCP, and the existing TME system view retain that same storage.
The former independent words at offsets `0x8c4`, `0x8c8`, `0x8d4`, `0x8d8`, `0x8dc`, and `0xc6c` now use those bytes.
Alias creation neither copies data nor changes requester permissions.

Both AOP images also use existing IRAM at `0x0b080000`, DMEM at `0x0b100000`, and three DDR segments.
Their IMEM segments start at `0x14685000` and `0x1468a000`.
The local alias completes the shared views for this image group.
Image metadata completion alone does not establish segment authentication, admission, or release.

### Native AOP prerequisite verification

SM8850 run137 records the OEM installation of `0x420aac` at runtime slot `0x489198`.
Image identifier `0x21` then calls genuine donor function `0x2242e0`.
The actual context getter returns `0x41275c`, whose captured header contains no presence flags.
The policy function returns `0x4006`. The output byte remains zero.
The retained `policy-probe.json` records these observations without guest writes.

SM8845 run114 compares 112 loaded bytes through local `0x18689580` and system `0x14689580`.
Both views match the selected AOP image at file offset `0x51580`.
Their SHA-256 is `40be1694a538eae4da4fd1a97a2b9b8c622645ba467d2a93273448dda16785a5`.
The original read at `0x230bbc` completes and advances to `0x230bc0`.
The retained `imem-coherence-probe.json` records the copy arguments, comparison, and executed read.

Both OEMs then reach the missing legacy import at `0x416934`.
Their PCs are SM8850 `0x43723a` and SM8845 `0x4374b6`.
These runs do not establish AOP admission, resource publication, or kernel execution.
The complete suite passes 94 tests. Clippy passes with warnings denied.

### AOP lifecycle and resource imports

Both OEMs use the same seven remaining lifecycle imports.
The SM8845 lifecycle code is `0x27c` above the corresponding SM8850 code.
The bindings use the genuine selector branches, arguments, hardware effects, and resource layouts.

| Legacy slot | Donor slot | Donor body | Contract |
| --- | --- | --- | --- |
| `0x134` | `0x138` | `0x222fca` | Select the domain admission callback |
| `0x114` | `0x118` | `0x222ff4` | Select transfer preparation or clear domain-one control bit zero |
| `0x130` | `0x134` | `0x22302a` | Select the domain release callback |
| `0xf4` | `0xf8` | `0x223058` | Publish method pointers into the actual resource records |
| `0xf8` | `0xfc` | `0x222974` | Apply the indexed resource operations through their published methods |
| `0x324` | `0x320` | `0x21b71e` | Construct operation `0x1010` with the supplied context |
| `0x6b8` | `0x6d8` | `0x21e4f0` | Submit `0x1010`, collect its ranges, and call its context callback |

The current SM8850 gate loads `0x134` at `0x43723a`, then `0x114` at `0x43724c`.
Both receive `(1,3)`.
The first genuine selector returns one for domain one.
The second clears bit zero at `0x102f3000` before returning one.
Zero results retain the original `0xb012` and `0xb013` errors.
The subsequent object methods remain responsible for their own results.

Domain zero uses the existing live callback bindings.
SM8850 installs admission preparation `0x422b38`, transfer preparation `0x422d0a`, and release sequence `0x422e44`.
SM8845 installs `0x422aee`, `0x422cc0`, and `0x422dfa`.
The corresponding donor slots are `0x574`, `0x2e4`, and `0x4b0`.
These callbacks perform actual hardware operations.
Constant-success replacements would lose those effects.

The shared context at `0x44690c` points to four records at `0x44688c`.
Each record occupies `0x20` bytes.
Record word `+0x18` selects the method family. Word `+0x14` receives its live method table.
The families are `2,3,3,2`.
The explicit index-two and index-one operations therefore use family three.
Donor slot `0x9bc` supplies the OEM getter for that family.
Its custom method `+8` performs the update handshake and polls bit 13.
The wrapper also retains its saved genuine method.

SM8850 publishes resources at `0x436d0a`. SM8845 uses `0x436f86`.
Operation `0x1010` submits through `0x41f986` or `0x41fa06`.
It completes through the supplied context callback during submission.
Its separate completion entries are the original zero-return paths.
No added completion import is required.
The existing range collector returns four counts and four starting identifiers.
The OEM then submits the corresponding `0x1002` operations with selector four.

The adjacent authenticated-read helper has a separate binding, `0x5e4 -> 0x604`.
Original body `0x21a1d2` and donor body `0x21a04a` preserve all five arguments.
They obtain the live context, construct the same callback descriptor, and retain the optional output write.
The OEM callers are `0x436822` and `0x436a9e`.
This binding does not replace authentication or belong to the current domain-one selector gate.

The hardware closure includes AOP control, clock/reset controls, request acknowledgments, and the two existing resource PLL banks.
The bindings alone do not establish hardware completion, AOP release, or resource publication.

### Image `0x89`: digest imports and unresolved policy bitmaps

SM8850 run142 stops at `0x43cabe` while reading legacy import `0x4169c8`.
The selected initializer starts at `0x43c9ac`. SM8845 starts at `0x43cc28`.
Static analysis uses each package's `RADIO/tme_fw.img`, selected by the corresponding prepared manifest.
The SM8850 manifest is `prepared-sm8850-common-runtime21/prepared.json`.
The SM8845 manifest is `prepared-sm8845-common-runtime19/prepared.json`.
The local firmware manifests identify those same packages.
No new native execution establishes completion of this group.

Both initializers retain repeated lifecycle reads at `0x021c873c`.
They also retain grant reads at `0x00c00224` and fuse reads at `0x021c8780/84`.
Their existing integrity dependency remains legacy `0x5cc`.
No mapping changes these reads or their comparison failures.

The following bindings supply genuine three-output getters:

| Legacy slot | Donor slot | Donor body | Outputs through `(a0, a1, a2)` |
| --- | --- | --- | --- |
| `0x1c8` | `0x1c8` | `0x224478` | Algorithm, data pointer, byte length from context `+0xa8/+0xac/+0xb0` |
| `0x1cc` | `0x1c4` | `0x2244f4` | Algorithm, data pointer, byte length from context `+0xb4/+0xb8/+0xbc` |

The first getter requires context presence bit `0x20`. The second requires bit `0x80`.
Both obtain the actual context through donor `0x194`, then the existing OEM callback at `0x198`.
Both reject any null output pointer with `0x4002`.
A failed context query returns `0x4005`. A missing presence bit returns `0x4006`.
Only a present record produces output writes and sentinel `0x59657321`.
Neither getter creates a record, changes a grant, or replaces authentication.

The initializer passes `(sp, sp+8, sp+4)` to legacy `0x1c8`.
It converts only the sentinel into its retained presence byte at `0x448b9d`.
The independent SM8850 consumer at `0x428740` passes three stack output pointers.
It requires algorithm two and a nonzero byte length before publishing the returned buffer.
It converts length `0x60` to `0x30` through its original branch.
SM8845 uses `0x428690` for that same contract.

Legacy `0x1cc` supplies another three-output digest query.
SM8850 reads it at `0x4310f4` and `0x43a1f0`.
SM8845 reads it at `0x43114c` and `0x43a46c`.
The latter consumer accepts only the sentinel before interpreting the algorithm and length.
Algorithms two, three, and five select digest sizes `0x20`, `0x30`, and `0x40`.
These argument and output contracts support the bindings independently of adjacent slot offsets.

Two required providers remain unresolved:

| Legacy slot | SM8850 read | SM8845 read | Required contract |
| --- | --- | --- | --- |
| `0x1d0` | `0x43cae4` | `0x43cd60` | Two outputs: pointer and byte length for two policy words |
| `0x1c4` | `0x43cb72` | `0x43cdee` | Two outputs: pointer and byte length for one policy word |

The original copy helper at `0x43d144` consumes those byte arrays without changing their meaning.
It rejects a null source.
It also rejects a byte length when that length shifted right two exceeds the supplied word capacity.
The initializer processes the resulting words as two-bit policy fields.
It preserves the original failure paths and does not substitute zero policy arrays.

Donor `0x1d0` is not compatible: body `0x22414e` writes scalar flags through one output pointer.
Donor `0x1c4` is also not compatible with legacy `0x1c4`: its contract requires three output pointers.
Donor `0x1e4` returns the image-enable bitmap from context `+0x10/+0x14`.
That byte bitmap does not establish equivalence with either two-bit policy array.
None of these alternatives supplies a supported mapping for the unresolved pair.

Static scans found no direct publication of these four legacy slots in either selected OEM load segment.
This observation does not prove that indirect publication is absent.
No recovered legacy implementation or equivalent donor provider establishes the two remaining contracts.
The map therefore leaves both unsupported slots unmapped.
The complete initializer import group is not closed.

The existing OEM claim function, AOSS publication, lifecycle, grants, and fuses remain separate dependencies.
Actual context presence and record contents also remain guest-owned inputs.
This change does not establish initializer completion, policy admission, completed image loading, or kernel execution.
The bindings were built, and both updated firmware scenarios were prepared.
SM8850 run146 executes the first digest query without changing its original result handling.
It next reads missing import `0x4169d0` at `0x43cae4`.
The second digest getter remains statically supported but unobserved in this native path.

### Policy bitmap follow-up: retained records and publication probe

This static analysis uses `prepared-sm8850-common-runtime22/prepared.json` and `prepared-sm8845-common-runtime20/prepared.json`.
The manifests select each package's `RADIO/tme_fw.img`.
The ELF load addresses use alias `0x2041c000` for TME address `0x41c000`.
Donor addresses below use binary base `0x200000`.
No build, preparation, test, or native execution accompanies this analysis.

Both OEM consumers pass two output addresses to each unresolved import:

| Import | SM8850 call | SM8845 call | Output addresses |
| --- | --- | --- | --- |
| Legacy `0x1d0` | `0x43caec` | `0x43cd68` | Pointer at `sp+0x18`, byte length at `sp+0x14` |
| Legacy `0x1c4` | `0x43cb7a` | `0x43cdf6` | Pointer at `sp+0x10`, byte length at `sp+0x0c` |

The consumers require sentinel `0x59657321` before they copy the returned bytes.
SM8850 supplies capacities two and one to helper `0x43d144`.
Its first bitmap loop at `0x43cb28..0x43cb4e` interprets each word as sixteen two-bit fields.
The loop changes field value three through its original bit operation.
Thus a pointer and byte length alone do not establish equivalent policy contents.

The donor ingest function `0x224e74` obtains its context through live export `0x198`.
It retains the following input records:

| Input presence mask | Input fields | Retained context fields | Ingest instructions |
| --- | --- | --- | --- |
| `0x01` | Flags at `+0xc0`, masked with `0x30` | Flags at `+8` | `0x224eb8..0x224ed8` |
| `0x02` | Scalar word at `+4` | Word at `+0x0c` | `0x224eda..0x224ef2` |
| `0x04` | Pointer and byte length at `+8/+0x0c` | Copied buffer at `+0x10/+0x14` | `0x224ef4..0x224f2e` |
| `0x08` | ID/right records at `+0x10`, count at `+0x88` | Entries at `+0x18`, count at `+0x90` | Live slot `0x244`, donor body `0x225274` |
| `0x10` | ID and digest triple at `+0x8c..+0x98`, count at `+0x9c` | ID and digest triple at `+0x94..+0xa0`, count at `+0xa4` | `0x224f54..0x224fb2` |
| `0x20` | Digest triple at `+0xa0/+0xa4/+0xa8` | Digest triple at `+0xa8/+0xac/+0xb0` | `0x224fb4..0x224fee` |
| `0x80` | Digest triple at `+0xac/+0xb0/+0xb4` | Digest triple at `+0xb4/+0xb8/+0xbc` | `0x224ff6..0x225030` |

The ingest function does not process presence mask `0x40`.
This observation does not establish what that mask means in the legacy format.
The buffer-copy service is donor slot `0x178`, body `0x2250c6`.
It copies the supplied byte count into storage owned by the existing context.
It does not convert policy encodings.

Donor `0x224354..0x22436e` indexes the `+0x10/+0x14` buffer with `image_id >> 3`.
It extracts one bit with `image_id & 7`.
This establishes a one-bit image bitmap, not either required two-bit policy bitmap.
Donor `0x225274..0x22531e` copies or merges ID/right pairs.
For matching IDs, it combines rights with bitwise OR.
Donor `0x2241d8` queries those pairs by ID.
No recovered relation identifies those IDs and rights with either legacy bitmap's field indices and values.
Expanding these pairs into policy words would therefore require an unsupported semantic choice.

The donor export table at `0x233c84` contains zero at slot `0x1cc`.
Its nonzero context-query exports retain the previously documented flag, image, ID/right, and digest contracts.
Neither the zero entry nor adjacent slot offsets identify a missing bitmap provider.
The available original binary ends at `0x220000`.
Its entry at `0x21602e` calls `0x2301da`, outside that input.
Thus this input does not supply the complete original runtime for provider recovery.
These observations do not exclude OEM publication through an indirect store.

The next runtime probe must observe publication without substituting a provider:

1. Capture writes overlapping `0x4169c4..0x4169c7` and `0x4169d0..0x4169d3` from reset through the current fault.
2. Record each writer PC, value, and bridge activation state. Include writes that the bridge rejects before forwarding.
3. At SM8850 `0x43cae4`, capture both backing RAM words without translated bridge reads.
4. If either word is nonzero, capture its target instructions and trace the installer that supplied that pointer.
5. If no provider exists, capture the input record at donor ingest entry `0x224e74` and the context after its return.

For the last step, retain the input presence word, input bytes through `+0xc3`, and context bytes through `+0x29b`.
Capture referenced buffers with their actual lengths before their owners release them.
Use the same probe at SM8845 `0x43cd60` when that path becomes available.
Bridge `prepare()` rejects an unmapped slot before forwarding its transaction.
Therefore a translated address error does not establish the value of the underlying RAM word.
An absent record must retain its genuine failure behavior, not become a successful zero bitmap.

No supported binding or adaptation results from this static follow-up.
The API map and relocation recipe remain unchanged.
The existing digest bindings remain `0x1c8 -> 0x1c8` and `0x1cc -> 0x1c4`.
Their provenance hashes remain the values recorded in both prepared manifests.
Original claim, initialization, authorization, and failure paths remain unchanged.
Authentication qualification remains not-established.

### Native policy backing and empty context

SM8850 run148 executes donor digest getter `0x224478`.
Its trace reaches `0x2244be` because context presence bit `0x20` is clear.
That instruction selects the original record-absence result `0x4006`.
The getter does not return a digest or a success sentinel.
The subsequent cross-CPU debugger memory read crashes QBox, so this run does not prove further boot progress.

Run149 observes API publication and backing memory through temporary bridge instrumentation.
It records the original write at `0x420ede` to legacy slot `0x198`, with value `0x420aac`.
No transaction through the bridge writes either watched bitmap slot before the fault.
The backing words at `0x4169c4..0x4169d3` are all zero.
The live donor table contains `0x225094` at `0x489194` and `0x420aac` at `0x489198`.

The original callback at `0x420aac` returns context address `0x41275c`.
Run150 reads all `0x2c0` bytes from that address through the underlying memory target.
Every word is zero, and every debug transaction returns its complete length with `TLM_OK_RESPONSE`.
The positive control contains the nonzero API pointers, so this is not a generic zero response.
The retained log is `run-sm8850-common-runtime150/system.log.zst`, member `system.log`, lines 24552 through 24599.

These observations establish an empty current context, not a valid zero-valued policy bitmap.
The original non-sentinel branch remains necessary for absent policy records.
They do not establish the original bitmap providers' numeric error codes or their positive-record behavior.
Authentication qualification remains not-established.

### Guarded queries for an absent dump policy

The current [Qualcomm security profile](https://github.com/qualcomm/security-profiles/blob/a69b6decf24456fb622de05ecb9b74e0bf871bee/nord_security_profile.xml#L2201-L2268) defines separate unencrypted and encrypted bits for each dump target.
Its [field definitions](https://github.com/qualcomm/security-profiles/blob/a69b6decf24456fb622de05ecb9b74e0bf871bee/nord_security_profile.xml#L2468-L2478) bound the IP-scan vector to four bytes and the QAD vector to eight bytes.
The public profile defines vector bits and lengths, not the private context's presence-byte layout.
The recorded initializer correspondence lacks exact instruction coordinates for those fields.
That layout claim remains unestablished.
[INFERENCE] Legacy `0x1d0` queries the QAD vector, and legacy `0x1c4` queries the IP-scan vector.
The original loop changes a pair with both capability bits to the encrypted-only pair.

The original ROM capture does not supply these providers.
The adapter therefore implements the existing fully denied context contract, not a positive policy parser.
It publishes the authored `legacy_empty_dump_policy` function in both legacy-owned slots before bridge activation.
It obtains the current context, processed state, and grant mask through the original OEM callbacks.
The report adapter and the optional queries share the same context check.

The query requires all these conditions before it reports absence:

1. Both output pointers are nonzero.
2. The context pointer is nonzero and aligned.
3. All `0x29c` context bytes are zero.
4. The processed state and grant mask are zero.

The absent-record result is `0x4006`.
An unavailable or nonempty context produces `0x4005`, and a null output pointer produces `0x4002`.
These are authored compatibility results from the existing error family, not recovered numeric results from the missing original providers.
The original consumers distinguish the success sentinel from every other result.
They retain their original absent-policy branch.

The query never returns `0x59657321`.
It does not write either output pointer, create a bitmap, parse an unverified policy, or grant authorization.
It does not support positive debug-policy contexts.
The original caller, not the adapter, clears its local policy fields on the failure path.
The authentication checks and original authorization records remain unchanged.

The native fixture checks empty, populated, processed, granted, missing, unaligned, and null-output states.
It also checks a nonzero final context word and unchanged output storage.
All 97 tests in two suites pass, and Clippy passes with `-D warnings`.
Both updated preparation commands reproduce the recorded ROM hash.
Authentication qualification remains not-established.

SM8850 run151 completes SOCCP_DEBUG, SOCCP_DCD, PDP_CDB, PDP, CPUCP_DTB, CPUCPFW, TZ_QTI_Config, QSEE Dev Config, and QSEE loading.
Its next fault is an SMMU read at `0x150ed070`, from APSS PC `0x14862718`.
SM8845 run123 completes SOCCP_DEBUG, SOCCP_DCD, PDP_CDB, PDP, and CPUCP_DTB.
It reports CPUCPFW authentication metadata timing before a TME read fails at `0x21835500`, from PC `0x230bbc`.
Both original firmware paths therefore pass the previously missing policy-query imports.
The retained UART logs establish image-loading progress, not UEFI, ABL, or kernel execution.

### SMEM protection after SBL1 completion

SM8850 run159 and SM8845 run126 pass the XPU reporting controls and report `SBL1, End`.
Their next fault reads legacy slot `0xa38` at `0x417238`.
The caller PCs are `0x432f7a` and `0x432fd2`.
The SM8850 caller invokes this zero-argument service after its milestone-eight request.
It preserves the preceding request's result independently.

The compatibility map selects donor slot `0xa74`, body `0x22e716`.
This service obtains the SMEM range and iterates its existing partition descriptors.
It uses descriptor flags and host identifiers to construct permissions, then applies those permissions through the existing protection interface.
It records completion only after the original loop.
Its missing-range branch retains the original diagnostic and fatal path.

The service uses live donor slots `0xa6c`, `0xa54`, `0xa44`, and `0xa4c`.
These provide capacity, range discovery, permission construction, and permission application.
The existing map connects original capacity slot `0xa30` to donor `0xa6c`.
SM8850 installer `0x436256` supplies original capacity hook `0x43625c`, which returns 26.

Original installer `0x4362b0` supplies host-mapping hook `0x436260` through legacy slot `0xa08`.
That slot already maps to donor `0xa40`, which the permission writer consumes.
The donor's default unmapped-host function does not replace this live OEM hook.
The existing SMEM descriptor and partition-discovery hooks remain unchanged.

The missing original export table prevents an exact original-function identity claim.
This binding is a behavioral compatibility inference from the caller, live providers, partition layout, and donor effects.
It imports executable donor behavior rather than a successful-return substitute.
The native authentication and authorization decisions remain unchanged.

### SMEM range callback ownership

SM8850 run160 and SM8845 run127 execute the imported finalizer.
They reach donor permission writer `0x22e80e` and its callback at `0x22e90a`.
That callback reads donor slot `0x28`.
It passes a 64-bit range, permission records, their count, and flags.

The earlier identity mapping placed original register-reader `0x42514a` in that donor slot.
This original function expects a descriptor pointer, not a range address.
It interpreted the range contents as a pointer and byte count.
Validator `0x425a9c` rejected the count alignment with original error `0xf034`.
The caller then reported `0xf016` and entered the original fatal path.
The later read at `0x810000` belongs to donor diagnostic capture, not the initiating failure.

Original installer `0x425548` assigns range service `0x424b14` to legacy slot `0x24`.
This service preserves all eight arguments and validates the original range and permission list.
Validator `0x425990` consumes twelve-byte records with identifier, permission, and attribute fields at offsets zero, four, and eight.
Donor writer `0x22e80e` constructs the same record layout.

The corrected map connects legacy `0x24` to donor `0x28`.
Legacy `0x28` remains legacy-owned, so original callers retain their register-reader callback.
The bridge also retains the legacy table address for callers that read its self-pointer.
No guessed callback occupies donor slot `0x24`.
The correction preserves range validation, permission validation, protected updates, and original failure results.

SM8850 run161 completes the partition loop, sets the completion byte, and returns to original caller `0x432f80`.
Original boot sequencing then calls the next initialization function at `0x42a4cc`.
It stops at missing legacy import `0x314`, not in SMEM finalization.
SM8845 run128 reaches the same missing import at PC `0x42a566`.
The full integration run passes 98 tests, and Clippy reports no warnings.
Neither run establishes kernel entry or authentication qualification.

### Post-SBL interface-object factory

Original SM8850 initialization `0x42a4cc` requests a factory through legacy import `0x314`.
It calls method zero and rejects a reported size above `0x9f`.
It then calls method `+0x0c` with a storage pointer, context pair, and two `-1` values.
The original code retains the resulting object for subsequent registration calls.

Donor slot `0x310` points to getter `0x2226b0`.
The initialized donor object uses method table `0x232920`.
Method zero at `0x2225f6` returns exactly `0x9f`.
Method `+0x0c` at `0x222682` aligns the supplied storage and constructs the object with those arguments.
The constructor installs method table `0x232880` and consumes the supplied context.

The map therefore connects legacy `0x314` to donor `0x310`.
It does not create an object pointer or substitute a registration result.

SM8850 run162 constructs the interface, then stops while loading the registration import at `0x42a61c`.
That call uses legacy import `0xa4`.
The successful path next calls legacy import `0x9c` with the selected twelve-byte records.
The map connects these imports to donor slots `0xa8` and `0xa0`.

Donor `0x22c9ca` consumes all eight registration arguments.
It validates descriptor fields `+0x64` and `+0x68` and the supplied version string.
It records the supplied context and returns its actual validation result.
Donor `0x22ccca` requires the registered state before it processes the selected records.
It executes its original validation stages and propagates each failure.
These bindings import executable validation services, not successful-return substitutes.

SM8850 run163 returns `-7` from the registration service.
Its `CDI_Seal` request completes transport but retains a nonzero service response at donor `0x22b3f2`.
Original `0x41e3a2..0x41e3ba` logs that result and continues.
The selected-record validation call remains unexecuted on this failure path.
This result does not establish successful registration or authentication qualification.

### Late allocation and message decoding

Original allocator wrapper `0x43539a` constructs a twenty-byte request for SMEM item `0x275`.
The request contains the remote host, item number, size, output pointer, and flags.
It supplies flag `0x40000000`, requires status zero, and consumes the pointer at request offset twelve.
SM8850 run163 stops at its missing legacy import `0xa24`.

The map connects legacy `0xa24` to donor `0xa60`, function `0x22d948`.
That function requires initialized SMEM state and checks the item limit before dispatch.
Its backend reaches allocator `0x22ddb6` through the live `0xa7c` callback.
The allocator checks the host and size, finds the partition, and updates actual allocation metadata.
It publishes the allocated pointer at request offset twelve and preserves allocation failures.

The later message loop receives a four-byte tag and an actual message body through `0x42035a`.
It constructs a pointer/length descriptor for the body after its eight-byte reply header.
Both `0x41e50a` and dispatch routine `0x42b076` require legacy identifier decoder `0x3fc`.
The map connects this import to donor `0x410`, function `0x221714`.
The parser returns the actual type-six identifier in `a0/a1`.
It returns all ones on parsing failure or a different type.

The remaining bounded initialization calls use existing queue, critical-section, and watchdog services.
Queue tag `0xfade0000` at `0x00c04fe4` reports a count, not APSS readiness.
No host-generated queue record or completed response replaces those services.

### Channel records with different runtime layouts

The OEM bulk initializers are SM8850 `0x435218` and SM8845 `0x435270`.
They address three records with stride `0xa8`.
Their array bases are `0x448f1c` and `0x448efc`, respectively.
Donor initializer `0x230188` instead uses stride `0x218`.
The selected OEM images initially supply zero across all three legacy records.

The common prefix contains the lock at `+0x3c` and the platform descriptor at `+0x78`.
It also contains the allocated SMEM pointer at `+0x7c`, control pointers at `+0x80/+0x88`, and state at `+0x94`.
Donor constructor `0x22fee4` initializes the actual SMEM header, control pointers, and channel state.
It writes fields at `+0xa4` and `+0xa8`.
Its logger uses the larger donor record.
Direct substitution with legacy pointers would therefore corrupt the next record.

The bridge migrates the three prefixes to independent records at `0x464200` during its first activation.
It initializes each extension to zero and preserves the supplied prefix bytes.
Later legacy reads and writes alias the corresponding prefix in the expanded record.
Repeated activation does not repeat migration or reset channel state.
The bridge excludes the legacy array from DMI before activation as well as afterward.
The existing scratch words at `0x464100` and `0x464104` remain separate.

The adapter converts channel pointers at three legacy entry points.
It preserves unrelated pointers, already expanded pointers, and arguments `a1..a7`.
It then transfers control to the actual donor function.

| Legacy slot | Donor slot | Donor function | Operation |
| --- | --- | --- | --- |
| `0xb94` | `0xbe0` | `0x22ffc4` | Open channel |
| `0xb9c` | `0xbe8` | `0x22fbec` | Process channel interrupt |
| `0xba8` | `0xc00` | `0x22fee4` | Initialize channel |

The map also connects `0xb8c` to `0xbd8`, `0xb98` to `0xbe4`, and `0xba0` to `0xbf4`.
These bindings preserve the actual runtime services and the OEM callback installation.
The bridge control words at offsets four and eight expose the legacy and expanded array bases to the adapter.
They accept aligned 32-bit reads only.

The native ABI fixture checks migration, bidirectional aliasing, record boundaries, extension isolation, and repeated activation.
Suite `bg936` passes all 98 tests.
Both production prepares independently reconstruct ROM SHA-256 `76c35cabc4705565d5d08f0a393de761978e316d72948b27d233ff12605d0893`.
SM8850 run167 enters `0x22fee4` and returns to OEM PC `0x4352f4`.
It next stops at TME IPCC write `0x05110038`, PC `0x44095e`.
This run proves the first constructor return, not completion of all three native channels.

### Late interrupt priority query and adjacent services

SM8850 run168 stops at PC `0x434fb2`, reading `0x4172e4`.
SM8845 run133 reaches the same import at PC `0x43500a`.
Both runs pass the previous TME IPCC initialization writes.
These observations do not establish kernel entry.

Static decoding uses the two supplied ROM files and the selected SM8850 `RADIO/tme_fw.img`.
The original ROM SHA-256 is `e5485515dde2da7b12ef0c06f1e5af8e1a56b693af2c49d43b68abd78db3deb3`.
The donor ROM SHA-256 is `94059e1c3ba90f7112fa600a7033d83aa71cee6ff5573219d72b63c69c7b67d7`.
The SM8850 ELF SHA-256 is `4258fc24317e3467b39144ab72a46d85e4494c80bddc37b14de16177ff0c4a19`.
ROM addresses use base `0x200000`.
OEM addresses below remove the ELF segment alias `0x20000000`.
The donor export table starts at file offset `0x33c84`.

The failing load uses `lui a0,0x417` and `lw a5,0x2e4(a0)`.
The legacy import base is `0x416800`, so this load selects offset `0xae4`, not offset `0x2e4`.
Original OEM function `0x434f9e` checks incoming `a2 == 7` and incoming `a0 == -1`.
Only that branch calls this import, with the original `a1` as its single argument.
The other branch calls OEM function `0x4403e0` with a retained interface and the original arguments.

The map adds `0xae4 -> 0xb30`.
Donor slot `0xb30` selects `0x223566`.
That wrapper loads the retained controller at `0x41cb68`.
It calls donor slot `0x990`, function `0x2237a0`, with `(controller, interrupt_index)`.
The backend selects a command from the controller's descriptor table.
It writes that command to the byte register at `0x00c00100`.
The previous description incorrectly identified slot `0x970` and a priority read.

The genuine priority accessor remains `0x22382a`.
It does not implement this binding.
Native run383 observes the command store with address `0x00c00100` and value one.
Its return address is `0x22fe56`, immediately after the transport's peer-notify call.

The adjacent donor group contains different contracts.
The following table records each export independently.
An absent legacy binding means that this analysis did not establish its original slot.

| Donor slot | Donor entry | Legacy binding | Calling contract and effects |
| --- | --- | --- | --- |
| `0xb10` | `0x2234ca` | Existing `0xac4` | Initialize the supplied controller and callback storage, then retain its pointer at `0x41cb68`. |
| `0xb14` | `0x223510` | Not added | Register `(interrupt_index, callback, callback_argument)` through backend `0x978`, function `0x2236e8`. Return its actual range-check result. |
| `0xb18` | `0x22352c` | Not added | Read the interrupt enable state through backend `0x97c`, function `0x223734`. A missing controller returns zero. |
| `0xb1c` | `0x223ba8` | Existing `0xad0` | Invoke export `0xb24` with interrupt `0x22`, callback from export `0xb20`, and callback argument zero. |
| `0xb20` | `0x223bc0` | Not added | Emit diagnostic arguments `(2, 2, 0, 0x201d, 0, 0, 0, 0)` through `0x21769e`. |
| `0xb24` | `0x2235ca` | Existing `0xad8` | Register the supplied interrupt callback through `0xb14`, then enable that interrupt through `0xae0`. |
| `0xb28` | `0x2235aa` | Not added | Write the supplied global threshold through backend `0x988`, function `0x22383a`. |
| `0xb2c` | `0x223582` | Not added | Write `(interrupt_index, priority)` through backend `0x98c`, function `0x22381a`. |
| `0xb30` | `0x223566` | Added `0xae4` | Send the descriptor-selected command through backend `0x990`, function `0x2237a0`. |
| `0xb34` | `0x223452` | Existing `0xae8` | Default return-only hook. The original OEM installs its own function in this slot. |
| `0xb38` | `0x223454` | Not added | Range-check an interrupt, acknowledge through `0x958`, enable through `0xae0`, then invoke its registered callback and argument. |
| `0xb3c` | `0x2275de` | Added `0xaf0` | Wait for retained scheduler event state and return the observed event word. The second argument controls the pre-wait service call. |

Backend `0x988` writes one byte at controller base plus layout offset `0x34`.
Backend `0x98c` writes one byte at controller base plus layout offset `0x30` plus interrupt index.
These are separate threshold and priority operations.
Backend `0x964`, function `0x22374a`, updates the software enable bitmap and writes one to the hardware enable byte.
Backend `0x958`, function `0x2237da`, checks the pending byte before its optional acknowledge-register write.
The callback dispatcher subtracts the layout's first interrupt before it selects the eight-byte callback record.

The original OEM initializer at `0x4378ca` consumes legacy `0xac4`.
Its subsequent calls at `0x4378e8` and `0x4378f2` consume `0xab0` and `0xad0`.
The installer at `0x437920..0x43792c` writes OEM function `0x43791c` into legacy `0xae8`.
Thus nearby code includes controller initialization and hook installation, not interchangeable priority accessors.
The map retains that existing OEM hook ownership.
No new return-only function enters the map.

The additional `0xaf0 -> 0xb3c` binding uses an independent OEM consumer.
SM8850 `0x4284a2..0x4284b2` calls legacy `0xaf0` with `(1, 2)` and compares the returned word against one.
Donor `0x2275de` retains its scheduler state, critical-section calls, optional pre-wait call, and `ecall`.
It reads the actual event word at scheduler context offset `0x60`.
For a nonzero event word, it stores zero when the first argument is nonzero.
Otherwise it stores the event word minus one.
It returns the observed word, not the updated count.
This is a behavioral substitute supported by the consumer, not proof of identical absent original instructions.

The selected downstream arithmetic binding adds `0x960 -> 0x998`.
Original ROM wrapper `0x218264` calls original helper `0x2308f4`.
Donor wrapper `0x218192` calls donor helper `0x230dc2`.
Both preserve the argument registers across their wrapper prologues.
The donor helper implements unsigned 64-bit division with numerator `a0/a1` and denominator `a2/a3`.
It returns the quotient in `a0/a1`.
Original ROM consumer `0x217e86` loads legacy `0x960` for a retained two-word numerator and denominator.
OEM consumer `0x42295c..0x422970` supplies its two-word numerator and denominator `(1000, 0)`.
Its subsequent multiplication and subtraction consume the quotient.
The binding executes the division service rather than returning a fabricated delay or timestamp.

The earlier analysis decoded donor `0x9a0`, function `0x227340`, but deferred its legacy binding.
It returns zero when scheduler context fields `+0x4c` or `+0x20` are nonzero.
Otherwise it returns one plus the equality test between `context[+0x38]` and `context[+0x10] - 1`.
The later native runs reach the corresponding idle predicate at legacy `0x968`.
The functional binding below preserves this actual scheduler query rather than requiring unavailable original-body equivalence.

The original ROM file contains only `0x200000..0x21ffff`.
It omits the original export table, the original division helper, and the late controller bodies.
Therefore this analysis cannot prove original-body equivalence for the late IRQ group.
The OEM consumer establishes the new priority-query contract.
The donor backend establishes its real service effects.
Unused adjacent slots remain unbound until an original consumer or provider establishes their contracts.
No constant slot delta supplies the new bindings.

The map SHA-256 is now `7f36e84165ed2406a677abf880c79982101eb76ed0b8b9845cdb52052ab9c14f`.
Only `api_map_sha256` changes in the relocation recipe.
The recorded ROM digest remains `76c35cabc4705565d5d08f0a393de761978e316d72948b27d233ff12605d0893`.
No adapter instruction, ROM span, or relocation changes.
This slice uses static decoding only.
It does not execute the reconstruction helper, builds, tests, formatters, or native firmware.
Production preparation and the next native observations remain pending.


### Channel event registration

SM8850 run169 and SM8845 run134 pass the priority query and stop at legacy import `0x9a4`.
Their PCs are `0x4355e4` and `0x43563c`, both reading effective address `0x4171a4`.
Both callers construct the same twenty-byte descriptor on the stack.
Its first ten bytes contain the channel name.
Offset twelve contains the OEM callback, and offset sixteen contains its original context pointer.
The callbacks are SM8850 `0x4357dc` and SM8845 `0x435834`.

Donor export `0x9dc`, function `0x22f6be`, consumes this descriptor.
It requires nonzero descriptor, callback, and context pointers.
It copies the ten-byte name and both pointers into its actual twenty-byte registration record.
It then increments the retained registration count.
Its original diagnostic path handles invalid descriptors and exhausted capacity.
The map connects legacy `0x9a4` to this service without replacing its checks or manufacturing an event.

This binding changes only the map and its recipe digest.
The map SHA-256 is `6538b0962e7c00d50e2c53046cbc6cfe887d3ba7230f251add3212c39484649a`.
The recipe SHA-256 is `10287a22b4e53ac3f4ea1af648fd76043a72717b8bca651067989f7e8078edeb`.
The reconstructed ROM digest remains unchanged.

Suite `bg945` passes all 99 tests and Clippy with warnings denied.
Production preparation independently reconstructs the recorded ROM and checks the new map identity.
SM8850 run170 passes the registration import and continues its TME scheduler with real machine-timer interrupts.
APSS reaches QSEE EL1, then traps on a separate SIMD access before the host timeout.
No completed registration result replaces the actual donor service.


### Scheduler idle predicate

SM8850 run181 and SM8845 run142 stop at import address `0x417168`.
The legacy base `0x416800` makes this slot `0x968`.
The respective caller PCs are `0x427b4c` and `0x427a88`.
Both callers test only whether the result is zero.

The map connects this import to donor export `0x9a0`, function `0x227340`.
That function reads the live scheduler context at unrelocated donor address `0x41cbb4`.
It preserves all three possible results described above.
The same donor scheduler owns the queried fields.
The binding neither writes scheduler state nor returns an unconditional idle result.

The SM8850 caller first disables interrupts through legacy `0xb14`.
A zero predicate restores the critical section through legacy `0xb18` and returns.
A nonzero predicate permits the original low-power checks and hardware sequence.
SM8845 uses the same zero/nonzero decision.
An instruction scan found one direct load of this import in each selected executable image.

This is a functional compatibility binding, not proof of an identical missing original implementation.
The virtual platform preserves RAM and does not require physical power collapse.
Actual scheduler work, interrupt state, and subsequent original checks remain authoritative.
This binding does not replace an authentication or authorization decision.

### Interrupt disable and restore around retained sleep

SM8850 run183 and SM8845 run144 pass the scheduler query and external wake checks.
They next stop at legacy import `0xa84`, read through `0x417284`.
The caller PCs are `0x429d0e` and `0x429c5e`.
Both power-entry paths supply interrupt ID seven before RSC preparation.
Their exit paths supply the same ID to legacy import `0xa94`.
The corresponding enable loads are `0x429cec` and `0x429c3c`.

The map connects disable import `0xa84` to donor `0xad0`, function `0x223558`.
It connects enable import `0xa94` to donor `0xae0`, function `0x22354a`.
Both wrappers load the actual controller from unrelocated donor address `0x41cb68`.
Their shared tail at `0x22361e` preserves the supplied interrupt ID as the backend's second argument.

The disable wrapper selects donor `0x95c`, function `0x223788`.
That backend clears the actual interrupt-enable byte and invokes source-mask cleanup through `0x980`.
The enable wrapper selects donor `0x964`, function `0x22374a`.
That backend updates the source mask through `0x994`, then writes one to the interrupt-enable byte.
The source-mask helpers preserve machine interrupt state around their updates.
Their first-external-ID checks keep internal interrupt seven separate from the external source bitmap.

These bindings preserve actual controller state rather than return-only substitutes.
The paired entry and exit consumers establish functional compatibility.
The missing original export table still prevents an exact original-body identity claim.

SM8850 run184 and SM8845 run145 pass both new import boundaries and reach their original power restore vectors.
Their next host unmodeled-access reports concern APSS, not an unresolved legacy import.
The map SHA-256 is `fd1f1cd29843c42080e373cb6c486ae4112daf10140247decbe4b1f4b7c99041`.
The recipe SHA-256 is `013f562c669966c57fedb2818417ba4d85c80498b7ab5ef74f6ee6e648cfee4f`.
The prepared manifests retain both values.
Neither run proves kernel entry or establishes authentication qualification.

### QMP receive CRC validation

SM8850 run254 and SM8845 run205 enter the genuine TME QMP receive callback after the APSS notification.
They stop on import read `0x417278`, at respective PCs `0x435852` and `0x4358aa`.
The legacy base `0x416800` makes this slot `0xa78`.
This operation validates packet integrity. It does not dispatch a channel service or acknowledge the QMP descriptor.

The two OEM callers have the same argument contract.
They first decode the packet's little-endian halfword at offsets zero and one.
A zero value bypasses CRC validation.
Otherwise, the caller supplies:

| Argument | Value |
| --- | --- |
| `a0` | Expected little-endian CRC from packet offsets two and three |
| `a1` | Packet pointer plus eight |
| `a2` | Received packet byte count minus eight |

The SM8850 argument sequence occupies `0x435856..0x43586a`.
The SM8845 equivalent occupies `0x4358ae..0x4358c2`.
A zero return selects the original rejection path.
A nonzero return permits the original packet processing.
The model must not replace this result with unconditional success.

The preserved original ROM contains the CRC validator at `0x218116`.
It retains the expected value, forwards the data pointer and byte count to legacy import `0xa64`, and compares the computed result.
It returns exactly one for equality and zero for inequality.
The neighboring original CRC32 validator at `0x218184` has the same structure but calls legacy `0xa68`.
The original CRC16 producer at `0x218146` returns zero for a null pointer or zero byte count.
Otherwise, it starts with `0xffff`, updates through a 16-bit reflected table, complements the result, and returns its low 16 bits.
The original table pointer is `0x231792`, beyond the available original ROM capture.

Donor export `0xac4` selects CRC16 validator `0x218044`.
It preserves all three arguments and calls donor export `0xab0`, CRC16 producer `0x218074`.
That producer has the same null, length, initial-state, update, complement, and return-width behavior as the original.
Its table at `0x231bd6` starts with `0x0000`, `0x1189`, `0x2312`, and `0x329b`.
The existing mapping already connects original CRC16 producer `0xa64` to donor `0xab0`.
It also connects original CRC32 producer `0xa68` to donor `0xab4` and CRC32 validator `0xa7c` to donor `0xac8`.

The map now connects legacy `0xa78` to donor `0xac4`.
This uses the actual guest checksum implementation and preserves its failure result.
No bridge code, adapter instruction, ROM span, packet byte, or descriptor field changes.
The unavailable original table prevents an independent comparison of every original table entry.
The paired producers, paired validators, and both OEM callers establish the functional binding.

Both refreshed preparations validate the API map and reconstruction provenance.
The reconstructed ROM identity remains unchanged.

### Observed CRC validation and QMP buffer lifecycle

SM8850 run255 executes actual donor validator `0x218044` and producer `0x218074`.
The OEM branch at `0x43586c` does not reject the packet.
Execution continues through `0x43586e`, `0x435872`, and `0x43589c`.
It then stops on missing import read `0x4171c0`, at PC `0x4358a0`.
SM8845 run206 executes the same donor functions.
Its branch at `0x4358c4` accepts the packet and continues through `0x4358c6`.
It stops on the same import at PC `0x4358f8`.
Thus the native OEM receive path passes CRC validation. This evidence is not limited to a synthetic checksum input.

An independent native probe executes the unmodified donor validator.
For ASCII `123456789`, expected CRC `0x906e` returns one.
Expected CRC `0x906f` returns zero.
The eight-byte prefix with expected CRC `0x906e` returns zero.
A null pointer and zero length with expected CRC zero return one.
The probe reaches success sentinel `0x600d0000`, at PC `0x30008e`.

The new dependency group covers receive-buffer release and the existing response-transmit helper.
It does not add adjacent APIs from their position alone.

| Legacy import | Donor export | Donor body | Recovered operation |
| --- | --- | --- | --- |
| `0x9c0` | `0x9f8` | `0x22f4ac` | Release the actual received buffer |
| `0x9c4` | `0x9fc` | `0x22f3da` | Submit an actual response buffer |

#### Receive-buffer release

SM8850 callback `0x4357de` preserves the live channel handle and receive-buffer pointer.
It calls legacy `0x9c0` for packets no larger than eight bytes and after accepted packet processing.
The respective call sites are `0x435816..0x435820` and `0x4358a0..0x4358aa`.
SM8845 uses equivalent sites `0x43586e..0x435878` and `0x4358f8..0x435902`.
Both supply the channel handle, the original buffer pointer, and constant one.
The original caller tests the result and retains its diagnostic failure path.

Donor `0x22f4ac` rejects a null handle with minus one.
Otherwise, it locks the channel through its live synchronization object at offset `0x3c`.
It forwards the handle and original buffer pointer to transport export `0xbf8`, body `0x23010c`.
It then unlocks the channel and returns the actual transport result.
The transport ignores the caller's third argument because QMP uses its existing single-buffer ownership state.

The QMP transport requires connected state five.
A disconnected channel returns minus six.
It requires the supplied pointer to equal the actual RX mailbox address plus four.
It also requires a nonzero mailbox length.
A pointer mismatch or empty mailbox returns minus one.
On success, the guest writes zero to that mailbox length.
It performs its descriptor readback through export `0xbf0`, then invokes notification export `0xbfc`.
Only this successful guest path returns zero.
The platform does not clear the shared length or force a successful result.

#### Response transmission

SM8850 helper `0x435692` tests its retained channel condition before calling legacy `0x9c4`.
The actual submission at `0x4356bc..0x4356cc` supplies the live handle, private value zero, payload pointer, and byte count.
SM8845 supplies the same arguments at `0x435714..0x435724`.
The enclosing packet sender computes the real CRC16 before this submission.
The helper preserves the original nonzero-result failure path.

Donor `0x22f3da` validates the handle and locks its live synchronization object.
It constructs the actual twelve-byte transport request from payload pointer, length, and private value.
It calls transport export `0xc04`, body `0x23008e`.
That transport validates connected state, payload capacity, and the actual in-flight flag.
The corresponding failures are minus six, minus three, and minus thirteen.
The successful transport marks the request in flight and invokes its actual buffer-copy and notification routines.
The core preserves a busy result through its existing queue path, rather than fabricate a transmission completion.
It sets the genuine pending-TX flag only after successful direct submission.
Subsequent peer consumption remains necessary for actual TX completion.

Both mappings use the same live donor channel objects that supply the receive callback.
The existing OEM callback installers and notification mappings remain unchanged.
The available original ROM capture does not include these original upper-ROM export bodies.
The explicit OEM argument contracts and complete donor transport paths establish functional compatibility, not original-body identity.
No generic close, event, or unrelated neighboring export is added.

The API map SHA-256 for that change was `0e3241a8005d617c229eea3fe8cdea99a4597b8d25b1ab5b56405dc63c5ba94b`.
Only the API map identity changed in the relocation recipe.
No bridge code, adapter instruction, reconstructed ROM span, or host mailbox response changes.
Both updated preparations validate this map and its reconstruction provenance.
SM8850 run266 executes receive release through donor `0x22f4ac` and transport `0x23010c`.
The transport sets success at `0x230184`. OEM branch `0x4358ac` accepts that result and continues at `0x4358cc`.
Native response transmission remains unverified.
The run later times out while CPUCP polls its configuration status, without a kernel handoff.
The static paths preserve release errors for null handles, wrong pointers, empty buffers, and disconnected channels.
They also preserve transmit capacity, disconnected-state, and busy errors.

### Command 0x166 request-buffer predicate

SM8850 run385 reaches missing import read `0x41701c` at OEM PC `0x4419fe`.
The legacy base `0x416800` makes this import `0x81c`.
The native run supplies the dependency evidence. This change does not claim subsequent command completion or kernel entry.

Static analysis uses the SM8850 package's `RADIO/tme_fw.img` and the original and donor ROM files from prepared runtime35.
The ELF virtual address minus `0x20000000` gives each OEM address below.
The ROM binary base is `0x200000`.

OEM wrapper `0x4419f0` accepts a pointer to an eight-byte buffer descriptor.
At `0x441a02..0x441a08`, it calls import `0x81c` with descriptor word zero in `a0` and word one in `a1`.
These words are the buffer pointer and byte count.
The wrapper converts the actual return value to a Boolean with `snez` at `0x441a0a`.
For example, OEM consumer `0x42d8ea` rejects a nonzero result before checking that the byte count is at least `0x304`.
Consumer `0x433ce4` rejects a nonzero result before reading a four-byte value from the buffer.
Thus this import checks the descriptor. It does not authenticate the command, copy its data, or complete its response.

The available original ROM contains the same descriptor wrapper at `0x216a30`.
Its import load at `0x216a3e` selects legacy `0x81c`.
It forwards the same two descriptor words and applies the same `snez` result conversion.
Donor wrapper `0x2169ec` has the same contract.
Its load at `0x2169fa` selects donor slot `0x858`, through address `0x419858`.
This paired wrapper establishes the binding independently of export-slot arithmetic.

A second original/donor pair has the same binding.
Original byte-scan routine `0x216760` calls legacy `0x81c` before reading the buffer.
Donor byte-scan routine `0x216716` instead calls donor `0x858`.
Both return zero when the predicate rejects the buffer.
Otherwise, both inspect actual bytes and return minus one only when every byte equals the requested value.

The donor export table at file offset `0x33c84` selects function `0x216204` for slot `0x858`.
That function preserves the buffer pointer and byte count.
It calls donor `0x848`, function `0x2161c8`, which returns one when the byte count is zero.
If that result is zero, it calls donor `0x850`, function `0x2161bc`, which returns one when the pointer is null.
The final result is one for either invalid condition and zero otherwise.
The original ROM contains corresponding count and pointer predicates at `0x2161cc` and `0x2161c0`.
The original export table lies outside the available capture, so this evidence establishes functional compatibility rather than export-table identity.

The map adds only `81c 858`.
This binding executes the real conditional guest implementation, not an unconditional success or no-op.
It preserves both rejection conditions and the OEM result branches.
The implementation does not dereference the supplied buffer, write guest data, or access hardware.
It introduces no new MMIO dependency.
Further command processing can reach buffer reads and other services, but static predicate analysis does not establish the next native hardware access.
No bridge, adapter, ROM span, command byte, or response changes.

The API map SHA-256 is `dc5e6a2a3153cb5aabca311dc2a799127833dbf436011269cede053a5f922acf`.
The relocation recipe records that map identity.
The resulting recipe SHA-256 is `d7a4609f6d3f7a99593b39e82f8dfbbc502b573a1d40b1b0542acf797f6c2a9e`.
Preparation runtime36 accepts both identities.
SM8850 run386 passes the predicate and enters the actual command handler at `0x432054`.
It then stops at `0x4320a0` on missing import `0x310`.
The run does not produce a response or reach kernel entry.

### Command 0x166 decoder factory

SM8850 run386 stops at `0x4320a0`, reading legacy import `0x310` through address `0x416b10`.
Handler `0x432054` requests the decoder factory without arguments.
It calls method zero for the required storage size.
It aligns its stack storage to sixteen bytes.
At `0x4320d6`, it calls method `+8` with that storage, the request descriptor, and schema pointer `0x4446cc`.

Donor export `0x30c` selects getter `0x221d40`.
The getter returns the initialized object through unrelocated RAM word `0x419c0c`.
That word points to `0x419c10`, whose method table is `0x232864`.
The reconstruction relocates these RAM addresses without changing their roles.

Method zero, `0x221cb2`, returns `0x73` bytes.
Method `+8`, `0x221d22`, aligns the supplied storage to four bytes and preserves the request and schema arguments.
Constructor `0x2218de` installs decoder table `0x2327b0` and calls initialization routine `0x220fde`.
The OEM caller then uses decoder method `+0x2c`, function `0x221280`, to extract its actual request data.
The existing encoder binding `0x314 -> 0x310` remains separate.

The map adds `0x310 -> 0x30c`.
This binding returns the initialized guest object rather than an invented pointer.
The method contract supports the binding, but the unavailable original export table prevents an original-body identity claim.

Preparation runtime37 accepts map SHA-256 `bf4663808f91d694505448b534f564b72b481244623b9e01f74b12004763ad8d`.
Its recipe SHA-256 is `9b6e749bc6097ba2026419b0e714f2eea9dddd6c5d663c3fe7eb0f86461c39f9`.
SM8850 run387 decodes the request and executes response submission at `0x4356cc`.
The retained QMP buffer contains a 382-byte response for CBOR tag `0x166`, with a 368-byte payload.
An independent CRC16 calculation over its encoded payload gives `0xfa5e`, equal to the packet's CRC.
Both mailbox lengths are zero after peer consumption.

QSEE executes the successful receive branch at `0x15670934` and returns transport success at `0x15670500`.
Its caller continues at `0x15675c00`, rather than entering fatal reason `0x64`.
The next rejection is a GPCE version read at `0x01dfa000`, from QSEE `0x156d2350`.
This run does not reach kernel entry.

### RMB message handler import `0xe8`

SM8850 run411 stops at an unmodeled TME read of `0x4168e8`.
The monitor reports `TLM_ADDRESS_ERROR_RESPONSE` at PC `0x42e32c`, width four.
The guest trace shows `lui a0,0x417` at `0x42e328`, then `lw a2,-0x718(a0)` at `0x42e32c`.
The load address is `0x4168e8`, or legacy base `0x416800` plus import `0xe8`.
Instructions `0x42e330..0x42e334` pass `a1 = sp + 0x24` and `a0 = s1`, then call the loaded pointer with `jalr a2`.
Thus the import supplies a code pointer, not a data word.

The caller is handler seven of the OEM RMB message table.
OEM image `RADIO/tme_fw.img` has SHA-256 `4258fc24317e3467b39144ab72a46d85e4494c80bddc37b14de16177ff0c4a19`.
Its ELF load alias `0x2041c000` corresponds to TME local address `0x41c000`.
Table `0x444520` holds nine `{handler, id}` pairs with ids one through eight and zero.
Word `0x444550` selects handler `0x42e2d6`, and word `0x444554` supplies id seven.
Source string bytes at `0x4444c4..0x4444f4` name `services/tme_messages/src/tme_handle_rmb.cpp`.

Handler seven zeroes a `0x14`-byte record at `sp + 0x10`.
It stores the record descriptor at `sp + 8`.
It calls import `0x3b8` and passes the return to constructor `0x41cfe4`.
That constructor writes type word `0x443084`, sets state `+0x20` to one, and stores the import return at `+0x24`.
It clears `+0x28..+0x34` and initializes the embedded buffer at `+0x38`.
Handler seven then calls import `0xe8` with the incoming message value in `a0` and the object address in `a1`.
A nonzero return is an error.
On success, `0x437934` serializes object words `+0x20`, `+4`, `+8`, `+0x18`, and `+0x1c` into the record.
`0x42e432` sends that record through the existing output interface.

OEM caller `0x4361b0` independently supplies the same contract.
It builds the same object and calls import `0xe8` with `a0` equal to one.
It then requires a zero return, object `+0x20` equal to zero, and object `+8` equal to zero.
Neither caller supplies an opcode. The imported wrapper selects it.

The donor export table at file offset `0x33c84` has two adjacent wrappers with this shape.
Donor slot `0xe8` selects function `0x21c052`.
That wrapper builds a request with word `+4` equal to `0x3009` and arguments at `+0x10` and `+0x14`.
Donor slot `0xec` selects function `0x21ba60`.
It builds the same request with word `+4` equal to `0x1008`.
Both wrappers call donor dispatcher `0x540`, function `0x21c142`.
The existing map binds legacy dispatcher import `0x528` to donor `0x540`.
Both wrappers return the dispatcher result unchanged.

The original ROM contains the same `0x1008` wrapper at `0x21b976..0x21b990`.
Its dispatcher load at `0x21ba02` selects legacy `0x528`.
The donor slot `0xec` wrapper has the same prologue, request layout, dispatcher selection, and return path.
This pairing establishes a behavioral counterpart, not an export-table identity.

Legacy import `0xec` selects a Boolean query.
OEM callers `0x41c3e6`, `0x42184a`, and `0x42b32e` pass one small index and test the return as a Boolean.
Donor function `0x231114` is the matching Boolean selector query, and it occupies donor slot `0xf0`.
The map already contains `ec f0`.
That entry moves the Boolean query by one four-byte slot.
The operation wrapper immediately before the query keeps the same adjacency.
Legacy `0xe8` therefore selects donor `0xec`, the `0x1008` wrapper, not donor `0xe8`.
Opcode `0x1008` also lies inside the OEM-admitted `0x1001..0x1008` operation set.
The `0x3009` request is not in that admitted set.
That admission evidence supports the selection. It does not replace the caller contract.

The map adds only `e8 ec`.
The bridge now translates the legacy read to candidate slot `0x4890ec`.
The candidate slot holds donor wrapper `0x21ba60` after reconstruction relocation.
The wrapper builds its real request and enters the existing dispatcher.
It does not fabricate a success result, bypass a protection check, or change an authentication decision.
No bridge, adapter, reconstructed span, or OEM byte changes.

The API map SHA-256 is `b472cecad2634378d924c68bbd1fa303a1c448aeda9120a7ce468e51ea5aecd8`.
The relocation recipe records that identity.
The resulting recipe SHA-256 is `cf326dcf89db63e47b48b7359d4c4a5c29a4924209cdca29f6012411c647cc11`.
The reconstructed ROM SHA-256 remains `76c35cabc4705565d5d08f0a393de761978e316d72948b27d233ff12605d0893`.
The original export table lies outside the `0x20000`-byte capture.
This binding therefore remains a behavioral correspondence.

The next native run should no longer report an address error at `0x4168e8`.
It should execute the donor dispatcher at `0x21c142` and return the handler's actual status.
The evidence does not show that the peer accepts the RMB response.
It does not prove image authenticity or kernel entry.

### Operation submit import `0x634` and legacy import audit

SM8850 run412 stops at an unmodeled TME read of `0x416e34`.
The monitor reports `TLM_ADDRESS_ERROR_RESPONSE` at PC `0x41f9a8`, width four.
The read address is legacy base `0x416800` plus import `0x634`.
The fault follows run411's resolved import `0xe8`.

The faulting instruction is `lw a1,-0x1cc(a0)` in stub `0x41f9a4`.
The stub jumps to `0x41fa2e`, which calls the import with `a0 = s1 + 0x10` and tests the return.
A zero return selects status two. A nonzero return selects status zero.
The stub lies in the OEM operation submit table. Table index seven selects `0x41f9a4`.
The operation descriptor word `+4` selects the table entry.

Donor slot `0x654` selects function `0x21f0fe` at file offset `0x33c84 + 0x654`.
That body accepts one descriptor pointer.
It performs indexed sequencer writes one, seven, nine, and fifteen.
It starts the operation through donor `0x6e8`.
It returns one on success and zero on failure.
The argument count, request lifetime, and return polarity match the OEM stub.
The prior operation inventory records `0x634 -> 0x654` as the submit dependency for this operation family.
The map adds only `634 654`.
This binding executes the real submit body and its failure paths.
It does not complete the operation, authenticate an image, or fabricate a result.

#### Legacy import audit

A static scan of the OEM image resolves every `lui 0x416` or `lui 0x417` table access.
It finds 861 access sites over 358 legacy import slots.
The map now covers 291 of those slots.
Sixty-seven slots remain unmapped.

The run412 trace executed only one unmapped slot: `0x634`.
The other 71 are static references. They may lie outside the current boot path.
The paired completion `0x638` stayed unmapped at run412.
The later completion evidence maps it to donor `0x658`.

Operation cohort still unmapped:

- 0x65c, 0x660, 0x680, 0x684, 0x690
- 0x694, 0x698, 0x69c, 0x6a8, 0x6ac, 0x6ec
- 0x6f0, 0x6f4, 0x6f8, 0x738, 0x73c, 0x748
- 0x75c, 0x760, 0x770, 0x774, 0x778, 0x77c
- 0x788, 0x78c, 0x798, 0x79c, 0x7a0, 0x7a4
- 0x7a8, 0x7ac, 0x7cc

Other slots still unmapped:

- 0x044, 0x048, 0x084, 0x08c, 0x0b4
- 0x0d0, 0x0d4, 0x13c, 0x178, 0x194, 0x21c
- 0x2b0, 0x2b4, 0x2c0, 0x2c4, 0x2c8, 0x2cc
- 0x2d4, 0x2dc, 0x2e0, 0x2e4, 0x2f8, 0x2fc
- 0x300, 0x31c, 0x320, 0x3b0, 0x404, 0x434
- 0x490, 0x498, 0x57c, 0x990, 0x9e4
- 0xa20

Each operation-cohort row needs a decoded submit bridge and return convention before integration.
The other slots need a decoded caller and a donor counterpart.
The map does not add a slot from index proximity.

The update does not change authentication, protection, or the reconstructed image.

The API map SHA-256 is `ac79dbebcaf21de7cfe1202dae14f8563cf9f9a4fd711ba282c456adcc1ae31c`.
The relocation recipe records that identity.
The resulting recipe SHA-256 is `741546aebf7e3170c495cf7e3457bb755b7973be2b740a1f4cacdc5d709baf3e`.
The reconstructed ROM SHA-256 remains `76c35cabc4705565d5d08f0a393de761978e316d72948b27d233ff12605d0893`.

The next native run should translate the `0x634` read to candidate `0x489654`.
It should return donor `0x21f0fe` and execute the submit body.
The next stop should be a new unmapped import or a sequencer boundary, not `0x416e34`.

SM8850 run413 passed the `0x634` read and stopped on the sequencer execute write.
The write targets `0x02230060` and carries `status | 0x200`.
Donor `0x21f0fe` wrote command `0x1c` before it.
The original ROM contains the same submit body at `0x21ef26..0x21ef9a`.
It writes command `0x1c`, zero at `0x04`, zero at `0x0c`, and the service context at `0x2c`.
The execute helper is original slot `0x6c8`, which the map already binds to donor `0x6e8`.
The donor bridge is therefore correct for this request.
Command `0x1c` is the transport-key broadcast.
The source-only event dictionary name remains unestablished.
The value at `0x2c` is the request context, not a key identifier.
The model labels that field `REQUESTING_EE` as a descriptive name.
The sequencer model now derives a virtual transport key and delivers it to the GPCE.
The GPCE sets `0x01df1020` bit8 only after that delivery.
The mailbox evidence records the register indices, the original bodies, and the virtual seed boundary.
The map adds `638 658` for the paired completion.
Donor `0x21f184` preserves the incoming status and notifies through donor `0x558`.
The completion mapping does not produce the GPCE status bit.

### Data transform submit import `0x8c4`

SM8850 run414 passed the GPCE warm-up and stopped on a read of `0x4170c4`.
The import base is `0x416800`, so this address selects legacy slot `0x8c4`.
The faulting instruction is `0x2042df64` inside handler `0x2042de84`.

The handler loads four other TME imports, and the map already binds all of them:
`0x3b8 → 0x3b8`, `0x3dc → 0x3e8`, `0x314 → 0x310`, and `0x8c8 → 0x908`.
The `0x8c4` call at `0x2042df8c` therefore is the only unmapped dependency on this path.

The call passes five arguments:
- `a0` is the translated identifier from `0x20441970`.
- `a1` is the translated pointer from the same helper.
- `a2` is the request context.
- `a3` is the operation context at `sp + 0x18`.
- `a4` is the descriptor word assembled from request bytes `+0xc..+0xf`.

The instruction after the call accepts only a zero return.
A nonzero return selects OEM error `1`.

Original slot `0x7b4` is not the submit counterpart for this handler.
The original body at `0x21b796` and donor body at `0x21b8ca` build operation `0x1005`.
Both store the same five fields at descriptor offsets `+0x18`, `+0x1c`, `+0x20`, `+0x24`, and `+0x28`.
Both dispatch through original slot `0x528`, which the map binds to donor `0x540`.
The donor body returns the dispatch result.
The map therefore adds `8c4 904`, the donor table slot for `0x21b8ca`.

The success path then calls an OEM pointer loaded from `0x449528`.
That body is not decoded, and the map adds no completion binding.
The earlier candidate pair `0x7b0 → 0x7e8` is not confirmed for this handler.
It stays unmapped.
This binding establishes dispatch and argument structure, not authenticated transform output.

### Key import submit and completion imports `0x7b0` and `0x7b4`

SM8850 run415 executes the submit stub `0x2041f980` and reads `0x416fb0`, which is legacy slot `0x7b0`.
Operation `0x1005` selects that stub through the submit table at `0x204432e8`.
The stub calls the import with `a0 = context + 0x10` and treats a nonzero return as success.
The paired completion table at `0x204433d4` selects stub `0x2041fcd8` for the same operation.
That stub reads `0x416fb4`, which is legacy slot `0x7b4`.
The completion dispatcher supplies `a0 = context + 0x10` and `a1` as the completion status.
A nonzero return again selects OEM success.

Original body `0x21eb6a` implements the submit.
It writes sequencer registers `(1,0x0b)`, `(7,0)`, `(9,0)`, `(0x0a, descriptor+8)`, `(0x19, descriptor+4)`, and `(0x0f, descriptor+0)`.
It executes through legacy `0x6c8`.
It returns one on success and zero on failure.
The failure path calls the descriptor pointer at `+0x10` with status two.

Donor body `0x21ed64`, donor slot `0x7e8`, performs the same register sequence through donor `0x660` and the same execute through donor `0x6e8`.
It returns one on success and zero on failure.
Donor slot `0x3e8` replaces the original `0x3dc` call that precedes the register writes.

Original completion `0x21ec1e` snapshots through legacy `0x6b4`, transfers through legacy `0x718`, and notifies through legacy `0x540`.
It reads the output buffer from `descriptor+0x10` at `+0x1b0`.
It uses 100 bytes, or 92 bytes when the mode word at `descriptor+0xc` equals two.
It returns one after a successful transfer and zero otherwise.

Donor completion `0x21ee14`, donor slot `0x7ec`, uses donor `0x6d4`, `0x748`, and `0x558` with the same buffer, lengths, and return values.
The map already binds each legacy dependency to the donor slot the donor body calls:
`0x3dc → 0x3e8`, `0x640 → 0x660`, `0x6b4 → 0x6d4`, `0x6c8 → 0x6e8`, `0x718 → 0x748`, and `0x540 → 0x558`.
The completion path also uses mapped status getter `0x644 → 0x664`.

The map therefore adds `7b0 7e8` and `7b4 7ec`.
The submit writes sequencer command `0x0b`, the wrapped-key export request.
It supplies source slot `descriptor+8` and wrapping-key slot `descriptor+4`.
Donor `0x21ed7e..0x21ed84` logs event `0x301a` with the same three fields.
The source-only event dictionary name remains unestablished.
The model wraps with the transport key and marks the blob with a virtual authenticated format.
The silicon blob format and the real source-key producer remain unrecovered.
The GPCE accepts the blob only after its tag and destination checks.
See `models/qcom/sm88xx/tme_mailbox/evidence.md` and `models/qcom/gpce/evidence.md`.

### Fuse-programming wrapper import `0x064`

SM8850 run422 stops at TME PC `0x430b2a`, which reads `0x416864`.
The import base `0x416800` makes this legacy slot `0x064`.

The caller at `0x430b1e` obtains the vendor context from getter `0x41c660`.
That getter returns the global at `0x448028` and loads mapped import `0x078 → 0x07c`.
The caller zeroes the word at `s0-0x40`.
It calls the import with `a0` equal to the context and `a1` pointing at that zeroed word.
It stores the 32-bit return at `s0-0x3c`.
The handler already passes mapped imports `0x314 → 0x310` and `0x310 → 0x30c`.
It continues through `0x7e0 → 0x81c` after the call.

Original body `0x2192c4` validates `a0`, sets byte `0x41a124` to one, tail-calls legacy `0x040` with the same two registers, and returns its value.
Donor body `0x219274`, donor slot `0x068`, performs the same validation, sets donor byte `0x41cb04`, and tail-calls donor `0x044`.
The map already binds `0x040 → 0x044`.
The wrapper therefore adds only the enable latch and validation to the mapped service.
It does not replace the programming behavior with a success return.

The map adds `64 68`.

The import audit covers direct loads in `0x430000..0x443000`.
The map binds every other direct load on the `0x430b2a` handler path.
The wider stage still contains unmapped slots. Examples are `0x044`, `0x048`, `0x2b0`, `0x2b4`, `0x2c0`, `0x2c8`, `0x2e4`, `0x2f8`, `0x2fc`, `0x31c`, `0x320`, `0x3b0`, `0x490`, `0x57c`, `0x7cc`, `0x808`, `0x814`, `0x820`, `0x990`, and `0x9e4`.
Those slots stay unmapped until each has a matching donor body.

### Watchdog QBEC request at legacy slot `0x300`

SM8850 run680, run682, and run683 stop at TME PC `0x4287d6` on an import read at `0x416b00`.
The legacy table starts at `0x416800`, so the read selects slot `0x300`.
Run685 read the original table word with a diagnostic access. It contained zero because the reconstructed runtime does not export this slot.
The diagnostic did not change the guest transaction, and the code was removed.

The CPH2749 `RADIO/tme_fw.img` function at `0x4287a2` calls the mapped slot `0x2b8` first.
It then builds a stack request and calls slot `0x300` with five arguments.
The arguments are a descriptor at `0x443760`, mode one, the stack request, marker `0xaaaaaaaa`, and zero.
The descriptor begins with words `0x04204829` and `0x00024040`, then `QBEC` and version one.
It ends with `/sources/services/watchdog/src/WatchdogMgr.cpp`.
The caller consumes the result through its existing return and error path.

The donor table word at `0x233c84 + 0x2fc` points to body `0x231a4e`.
At `0x231a84..0x231a98`, the body treats the first two descriptor words as a key policy.
It creates internal operation `0x1001`, then calls the key-generation submitter at `0x21e58e`.
The version-four ECDH policy and `QBEC` tag support a key-generation interpretation.
The source path alone does not make this a log call.
The map binds `0x300` to donor `0x2fc` with recipe hash `1912743ac60c1b8f1273a61804b91b812af07303648e30717fa1ce36e2a2a0e9`.
The original export table is absent, so this binding remains a behavioral inference.
`models/qcom/sm88xx/tme_mailbox/evidence.md` records the policy decode and the generation profile.

After key generation, the donor body calls export `0x280` with internal operation `0x17`.
Run692 stops at OEM `0x41f924` on unmapped legacy submit `0x6a8`.
The map now binds `0x6a8 -> 0x6c8` and `0x6ac -> 0x6cc`.
These rows leave the deferred completion table above, because the donor itself built the descriptor.
`models/qcom/sm88xx/tme_mailbox/evidence.md` records the `GET_ECC_PK` command contract.

### QBEC public key import at legacy slot `0x2b4`

Run696 and run697 return the X coordinate twice, then stop at OEM `0x428858` on legacy slot `0x2b4`.
OEM function `0x428828` calls slot `0x2b4` with a key handle, curve `4`, requester one, and a stack buffer.
Donor slots `0x2a8` and `0x2ac` take the same four arguments.
Body `0x21b1a6` reaches `0x21b6f8`, which clears `xCoordOnly` and calls export `0x288`.
Body `0x21b1a8` sets `xCoordOnly` and calls export `0x284`.
Exports `0x284` and `0x288` read the public key, then call the follow-up checks at `0x3ec` and `0x3f0`.
The failure path of `0x288` logs `0x304b`, `SEQ_ECDSA_PCT_FAILED`.
The QBEC key is an ECDH key, so the ECDH path through `0x284` applies.
Neighboring rows `0x2ac -> 0x2a4` and `0x2b8 -> 0x2b0` share the same offset of eight.
The map therefore binds `0x2b4 -> 0x2ac`. The original export table is absent, so this binding is an inference.

### Variant imports at legacy slots `0x404` and `0x434`

Run704 passes the QSEE SHA-384 app gate. TME then stops at OEM `0x42f5f0` on legacy slot `0x434`.
OEM function `0x42f5cc` fills a stack value through the first method of its object argument.
It calls slot `0x434` with that value. When the result is nonzero, it returns the result of slot `0x404`.
Its only caller, `0x42fe76`, requires the return value four.
No other OEM instruction loads either slot.

Rows `0x3ec..0x3fc` and `0x49c..0x4dc` map with an offset of `0x14`.
The same offset selects donor `0x418` and `0x448`.
Donor `0x448`, body `0x221810`, returns whether the tag byte at offset zero equals four.
Donor `0x418`, body `0x221876`, returns the 16-bit field at offset eight.
These bodies match a type test followed by a value read.
The map therefore binds `0x404 -> 0x418` and `0x434 -> 0x448`.
The original export table is absent, so these bindings are inferences.

### Crypto request wrappers at legacy slots `0x2b0..0x2e4`

Run706 stops at OEM `0x42f9a4` on legacy slot `0x2cc`.
Two OEM request handlers use this slot family.
Each handler selects between two imports by a type value:

| OEM site | Condition | Legacy slot | Arguments |
| --- | --- | --- | --- |
| `0x42f4c4` | Type five | `0x2c8` | Six, context in `a5` |
| `0x42f506` | Other types | `0x2c4` | Six, key identifier in `a0`, context in `a5` |
| `0x42f962` | Type six | `0x2d4` | Seven, context in `a6` |
| `0x42f9be` | Other types | `0x2cc` | Six, key identifier in `a0`, context in `a5` |

Two other handlers call slot `0x2dc` with six arguments and slot `0x2e0` with seven arguments.
Both pass the constant three in `a0`.
Slots `0x2b0` and `0x2e4` take four arguments at `0x430548` and `0x430552`.
Slot `0x2c0` takes eight register arguments at `0x4288cc`.

The donor wrappers build internal requests and pass them to dispatcher slot `0x540`:

| Donor slot | Body | Operation | Arguments |
| --- | --- | --- | --- |
| `0x2a8`, `0x2e0` | `0x21b1a6`, `0x21b1b6` | `GET_ECC_PK` through `0x288` | Four |
| `0x2b8` | `0x21af4e` | `0x21`, zero context | Eight |
| `0x2bc` | `0x21af6a` | `0x21` | Eight and a stack context |
| `0x2c0` | `0x21acda` | `0x11` | Six |
| `0x2c4`, `0x2d8` | `0x21ad08`, `0x21aee8` | `0x12` | Six |
| `0x2c8` | `0x21ad36` | `0x13` | Six |
| `0x2cc` | `0x21adc4` | `0x15` | Eight and a stack context |
| `0x2d0`, `0x2dc` | `0x21ad88`, `0x21af14` | `0x14` | Seven |
| `0x2d4` | `0x21ae54` | `0x16` | Eight and two stack values |

The argument counts fix the alignment.
Legacy slots up to `0x2c0` map to the donor slot eight bytes lower.
Legacy slots from `0x2c4` map to the donor slot four bytes lower.
Donor `0x2bc` is the extra export, because donor `0x2b8` calls it with a zero context.
Both six-argument pairs then match operations `0x11..0x13`.
Both seven-argument calls match operation `0x14`.
Donor `0x2a8` and `0x2e0` are both aliases of `0x21b6f8`, which matches the two adjacent four-argument calls.
The map binds `0x2b0`, `0x2c0`, `0x2c4`, `0x2c8`, `0x2cc`, `0x2d4`, `0x2dc`, `0x2e0`, and `0x2e4` this way.
Slots without an OEM caller stay unmapped.

### Dispatcher pairs for operations `0x11..0x14`

These donor wrappers build donor-numbered requests.
The live OEM dispatcher selects imports from table `0x443264 + 4*(op-1)` and `0x443350 + 4*(op-1)`.
The donor dispatcher uses tables `0x2323cc + 4*(op-0x11)` and `0x232510 + 4*(op-0x11)`.
The pairs therefore follow the operation number:

| Operation | Legacy submit, completion | Donor submit, completion |
| --- | --- | --- |
| `0x11` | `0x754`, `0x758` | `0x784`, `0x788` |
| `0x12` | `0x75c`, `0x760` | `0x78c`, `0x790` |
| `0x13` | `0x790`, `0x794` | `0x7c0`, `0x7c4` |
| `0x14` | `0x7a0`, `0x7a4` | `0x7d8`, `0x7dc` |
| `0x17` | `0x6a8`, `0x6ac` | `0x6c8`, `0x6cc` |

The earlier rows `0x790 -> 0x7d8` and `0x794 -> 0x7dc` paired operation `0x13` with operation `0x14`.
Donor submit `0x7c0` writes signature command 8 with requester word four and argument word one equal to one.
Donor submit `0x7d8` takes the requester from word zero and writes argument word one as zero.
The image requests carry two in word zero and one in word four.
Both submit bodies therefore produce a valid command 8, so earlier runs did not expose the swap.
Run711 issues all 86 verifications in the inline form with requester one, and every verification passes.
The completion bodies `0x7c4` and `0x7dc` are identical in their use of context offset `0x18`.

### Message-memory context import `0xd4`

SM8850 run863 stops at an unmodeled TME read of `0x4168d4`.
The monitor reports `TLM_ADDRESS_ERROR_RESPONSE` at PC `0x42a702`, width four.
The guest trace shows `lui a0,0x417` at `0x42a6fe`, then `lw a1,-0x72c(a0)` at `0x42a702`.
The load address is `0x4168d4`, or legacy base `0x416800` plus import `0xd4`.
Instructions `0x42a706..0x42a70e` set `a0 = 0x4117e4` and call the loaded pointer with `c.jalr a1`.
The caller leaves the import-table address in `a1`.
Therefore the callee must ignore its second register.

The TME trace shows the call from `0x43427e` into handler `0x433ff4`.
The dispatcher reads table `0x444780` at `0x43425c..0x43427e`.
Its first pair is `{0, 0x433ff4}`.
Its second pair is `{1, 0x434120}`.
String `0x4447b4` in the same table region names `tme_messages_mem.cpp`.
The handler clears `0x426` bytes at `0x40c800`.
Then it calls `0x42a6a2(0x40c804, 0x41a, sp-0x24, sp-0x28)`.
OEM function `0x42a6a2` ends at `0x42a80c`, where the following function begins.
Its prologue saves those values in `s0`, `s3`, `s4`, and `s2` at `0x42a6d6..0x42a6dc`.
At `0x42a702`, it loads import `0xd4` into `a1`, sets `a0` to context `0x4117e4`, and calls at `0x42a70e`.
It saves the returned `a0` in `s1` and compares it with sentinel `0x050cce55` at `0x42a710..0x42a71a`.
An unequal result branches to cleanup at `0x42a7c4`.
Equality continues through response processing and the later `0x7cc` call.
The later copy at `0x42a736` uses the persistent `0x41a`-byte buffer at `0x413298`.
The dispatcher confirms message selector zero and routes it to `tme_messages_mem.cpp`.
The logs do not identify the APSS sender, an ABL call site, a QSEE command name, or the message payload.
System log line 25362 reports `SEQUENCER_SIGNATURE_VERIFY digest=inline owner=1 result=pass` before the access fault at line 25371.
[INFERENCE] This is an ABL/boot-chain QMP message-memory request, but the retained logs do not establish which QSEE client or payload issued it.

The context is the global object at `0x4117e4`.
Function `0x42a232` constructs it, and instruction `0x42a558` calls that constructor.
Instruction `0x42a25e` stores state three at context offset four.
Register `s1` holds context plus `0x918`.
Instructions `0x42a29e..0x42a2a2` store one at `+0x9d8` and `+0x9dc`.
Instructions `0x42a2ac..0x42a2b0` store minus one at `+0x9e0`, `+0x9e4`, and `+0x9e8`.
Instruction `0x42a2b6` stores `0x100` at `+0x9ec`.

The caller passes context offset `+0x9e0` to the next operation.
At `0x42a78c..0x42a798` it loads `a0 = [0x4117e4 + 0x9e0]`, `a1 = [+0x98]`, and `a2 = [+0x9d8]`.
The call at `0x42a7a8` selects legacy import `0x7cc`.
[INFERENCE] Import `0xd4` produces that field, because donor `0x22b66a` writes the matching donor field.

The original ROM file contains `0x20000` bytes mapped at `0x200000`, covering `0x200000..0x21ffff`.
The live legacy import table starts at `0x416800`, outside that capture.
The original table word for `0xd4` and its target body are therefore unavailable in `tme-original.bin`.
A scan finds no pointer table for late code addresses in the captured range.
The exact OEM body identity remains unavailable.

The donor export table starts at file offset `0x33c84`.
Its word at `0x233d58` selects donor slot `0xd4`, function `0x22b66a`.
The function accepts one argument in `a0`.
Instruction `0x22b6b2` reads the index at `+0x2fc`.
Instructions `0x22b6b6..0x22b71c` build the request and call `[0x419278]`.
On success, instructions `0x22b72c..0x22b72e` copy response word `+0x74` to `+0x304`.
Instructions `0x22b732..0x22b736` return `0x050cce55`.
A failed check returns `-0xb` at `0x22b720`.

Donor constructor `0x22b05a` initializes the same field group.
Instructions `0x22b078..0x22b082` store state three at `+4`.
Instruction `0x22b0c6` stores one at `+0x2fc`.
Instruction `0x22b0ca` stores one at `+0x300`.
Instructions `0x22b0d2..0x22b0d6` store minus one at `+0x304`, `+0x308`, and `+0x30c`.
Instruction `0x22b0d8` stores `0x100` at `+0x310`.

The two contexts share one field group with a constant delta of `0x6dc`.

| OEM offset | Donor offset | Initial value |
| --- | --- | --- |
| `+0x9d8` | `+0x2fc` | one |
| `+0x9dc` | `+0x300` | one |
| `+0x9e0` | `+0x304` | minus one |
| `+0x9e4` | `+0x308` | minus one |
| `+0x9e8` | `+0x30c` | minus one |
| `+0x9ec` | `+0x310` | `0x100` |

The OEM caller reads index `+0x9d8` and result `+0x9e0`.
Donor `0x22b66a` reads index `+0x2fc`, writes result `+0x304`, and returns sentinel `0x050cce55` on success.
The caller supplies its context in `a0`.
Donor `0x22b66a` uses that register and does not access `a1`.
The original body is outside the captured ROM, so its full register use and exact return behavior cannot be verified.
The donor contract is compatible with the observed OEM consumer only after the `0x6dc` context shift.
Across donor `0x22b66a`, `0x22c76a`, and the release helper at `0x22cf1c`, the only context-relative fields touched are donor `+0x2fc` and `+0x304`.
The table maps those fields to OEM `+0x9d8` and `+0x9e0`, respectively.
These donor bodies read or write no other fields in the context object at the shifted base.
Shift the context argument by `0x6dc` before the donor body.
Only then does its contract match the OEM operation.
A plain slot translation passes the unshifted OEM object and is layout-unsafe.

Run866 records the plain-translation failure.
Donor `0x22b66a` returns `-0xb` at `0x22b720`.
The service call at `0x22b71c` returns zero, but the response check at `0x22b724..0x22b726` finds a nonzero word at stack offset `0x68`.
The OEM caller branches at `0x42a71a` to cleanup at `0x42a7c4`.
It does not reach the subsequent import `0x7cc`.
This failure does not establish that the OEM request itself failed.
The trace shows that the direct translation passed the wrong context layout.

### Cleanup import `0xb4`

The OEM caller at `0x42a7c4..0x42a7d6` loads legacy import `0xb4` from `0x4168b4`.
It passes `0x4117e4` in `a0` and ignores the return value.
This path runs after the import `0xd4` operation, including the error branch seen in run866.

Donor slot `0xb4` selects `0x22c70c`.
That body clears donor fields `+0x308` and `+0x30c`.
It does not release the result field `+0x304`.
Donor slot `0xb8` selects `0x22c76a`.
Its body reads result field `+0x304` at `0x22c78a`, skips release when the value is `-1`, calls the release helper through `0x22cf1c` otherwise, and stores `-1` at `0x22c79a`.
The OEM cleanup follows the `0xd4` operation.
That operation stores its result at `+0x9e0`.
With the same `0x6dc` context shift, donor `+0x304` is OEM `+0x9e0`.
Therefore the cleanup contract pairs OEM `0xb4` with donor `0xb8`, not donor `0xb4`.

The adapter wrappers preserve the call contract.
`legacy_context_d4` adds `0x6dc` to non-null `a0` and tail-calls the function pointer in donor slot `0xd4`.
`legacy_context_b4` makes the same adjustment and tail-calls donor slot `0xb8`.
Both leave `a1..a7` unchanged and return the donor result unchanged.
They pass null `a0` through unchanged.
The wrappers are bound at free candidate slots `0x248` and `0x24c`.
The API map binds legacy `0xd4` to candidate `0x248` and legacy `0xb4` to candidate `0x24c`.
Donor slot `0x248` at file offset `0x33ecc` contains zero.
Donor slot `0x24c` at file offset `0x33ed0` also contains zero.
The copied donor table spans file offsets `0x33c84..0x34944`.
Both entries lie in that copied range.
The adapter writes wrapper pointers to runtime slots `0x489248` and `0x48924c`.

### Imports on the message-memory path

Handler `0x433ff4` calls `0x42a6a2`.
The handler's direct imports and conditional canary path are:

| Legacy slot | Load site | Use | Mapping |
| --- | --- | --- | --- |
| `0x314` | `0x434082` | Message object/context setup | Existing `0x314 -> 0x310` |
| `0x3b8` | `0x42a6d2` | Message buffer setup | Existing identity mapping |
| `0xd4` | `0x42a702` | Execute message request | `0xd4 -> 0x248`, shifted-context adapter |
| `0xd0` | `0x42a766` | One-argument context check | Unmapped |
| `0x7cc` | `0x42a788` | Six-argument request operation | Unmapped |
| `0xb4` | `0x42a7c8` | Release request result | `0xb4 -> 0x24c`, shifted-context adapter to donor `0xb8` |
| `0x7e0` | `0x4340bc` | Message response handling | Existing `0x7e0 -> 0x81c` |
| `0x96c` | `0x420894` | Fatal stack-canary path only | Existing `0x96c -> 0x9a4` |

The OEM `0xd0` caller passes only `a0` and requires sentinel `0x050cce55`.
Donor slot `0xd0` at `0x22b1b4` takes the context in `a0` and a selector in `a1`.
It rejects a null context at `0x22b1ee`, then reads context `+0x2fc` and `+0x300`.
The OEM call supplies no selector, so the donor contract does not match.
The OEM `0x7cc` caller passes `a0 = [context+0x9e0]`, `a1 = [context+0x98]`, `a2 = [context+0x9d8]`, `a3 = sp`, `a4 = 0xaaaaaaaa`, and `a5 = 0`.
It checks the return and output words at `[sp+0x20]` and `[sp+8]`.
It then stores `[sp+0x2c]`.
The inspected donor functions with six arguments treat `a4` as a pointer or use a different request signature.
Donor slot `0x7cc` exports `0x21de42`.
That body takes an object in `a0` and a value in `a1`, reads object `+0x24`, and calls `[0x4196d0]` and `[0x419558]`.
It performs no six-argument request and writes no caller buffer.
Donor slot `0xe0` at `0x22c104` is the nearest request match.
It reads context `+0x304` and `+0x2fc`, copies `0x64` bytes from its `a1` source, and calls `[0x419808]` with the same tag in `a4` and zero in `a5`.
It then checks its own buffer at `+0x20`, `+8`, and `+0x2c`, stores the third word at context `+0x308`, and returns the sentinel or `-0x13`.
The OEM caller instead passes the result value, the source pointer, and the index in `a0..a2`.
It then reads its own `a3` buffer at `+0x20`, `+8`, and `+0x2c`, and stores the third word to `0x447c8c`.
The donor `0xe0` body takes the context in `a0` and reloads both fields from that context.
A mapping would therefore need an adapter that copies donor-owned state into the caller buffer.
No OEM body or trace establishes that transfer.
Keep the slot unmapped.
The handler also calls object methods through a runtime table.
It calls a callback through `0x449528`.
The targets are indirect.
The run866 trace exits before those calls execute.


Donor slot `0xd8` selects function `0x22c994`.
That function reads `+4` at `0x22c9ac` and compares it with four at `0x22c9ae..0x22c9b0`.
It returns the sentinel on equality and `-0x3b` otherwise.
Both constructors store three at `+4`.
The inspected class bodies store no four there.
The function also writes no result at `+0x304`.
It cannot supply the value that the OEM caller reads at `+0x9e0`.
The map therefore does not select donor slot `0xd8`.

The map adds `b4 24c` and `d4 248`.
The original export table and the original bodies lie outside the captured ROM, so exact OEM body identities remain unavailable.
The mapping is a behavioral compatibility inference from the OEM callers and donor contracts.
The adapter performs only the context-layout adjustment and tail-calls the real donor functions.
It does not replace an operation, force success, or change an authentication decision.

The API map SHA-256 is `e7bc169cdd4bb88dab66b135dff546c8b3adbc991cbf8bca3bed7d614a3f1cc9`.
The relocation recipe records that identity.
The adapter binary is 2036 bytes with SHA-256 `5a6e89a480d564fe6301e8569ffdbd08e9a1a6f36f4eb532db32f92e0501b6c0`.
The relocation recipe SHA-256 is `57f490081ea7e53073a0076e13ed652ffca7c39847b76a65d082edbf9a54c738`.
The reconstructed ROM SHA-256 is `c52d43240e387986e37540f2a12a5a05cbcb28f1a3cd98399255437f00a17d65`.
A later change removed the per-span hashes from the recipe. The whole-image hashes of the inputs already pin those bytes.
The recipe SHA-256 is now `48bfca4e78f48fe635683034e8c221659e40e68e72ae755796629b31ed233543`.
The reconstructed ROM SHA-256 stays the same. A reconstruction from the private inputs reproduced it.

The map parser accepts only two fields per line and rejects comment text.
This evidence section and the adapter comments explain why.
Run866 shows that the unadjusted `0xd4` mapping reaches the donor body and returns `-0xb`.
It does not establish success after the adapter adjustment.
The evidence does not show that the later `0x7cc` call succeeds.
It does not prove image authenticity or kernel entry.

# DDR frontend programming controls

## Scope and image identity

This group implements the selected XBL's four DCB register lists, synchronous programming latch, and boot-pass controls.
It does not implement DDR geometry, SHRM commands, or analog training.
It does not map `0x31060000` as SRAM.
Other PHY routines remain separate devices.

Evidence comes from these package files:

- `IMAGES/xbl_sc.elf`, SHA-256 `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
- `IMAGES/xbl_config.elf`, SHA-256 `c35a368853ba57f00c9486d4b8fadeb0ded3a59ce410818a69ac415e3c44d3ef`.

The package directory is `CPH2749export_11_16.0.5.700EX01_2026022701130186`.
Instruction addresses below are selected XBL virtual addresses, decoded as AArch64.
Configuration offsets below are file offsets in the selected configuration ELF.

## Resolve the indirect interpreter

`0x148ad764` loads the function pointer at `0x14a14c28` and branches to that pointer.
Its zero ELF initializer is not the runtime target.
The installation sequence at `0x148734e0..0x148734fc` stores `0x148be4c0` in that slot.

`0x148be4c0..0x148be4e8` implements this loop:

1. Use the low sixteen bits of `w1` as the record count.
2. Read two little-endian 32-bit words from `x2`, then advance the source by eight bytes.
3. Add the first word to `w0`, with 32-bit arithmetic.
4. Store the second word at that address.

The arguments are base, record count, and record pointer.
This is an offset/value register interpreter, not `memcpy`.
It does not read destination state or interpret record values as another instruction stream.

At `0x148adac8..0x148adaec`, XBL reads the guest DCB pointer from `0x3104ac68`.
It reads the section offset and length at DCB `+0x80/+0x82`.
It passes base `0x31060000` and count `length >> 3` to the interpreter.
The first selected record therefore writes `0x310760a0`, not `0x31060000`.
The explicit store at `0x148adafc` then writes one to `0x3107694c`.

## Configuration payloads

The configuration directory contains `/A01B_8_0100_1_dcb.bin` and `/A01B_8_0200_1_dcb.bin`.
Their names occur at configuration file offsets `0x25c` and `0x294`.
Their payloads start at file offsets `0x42bcc` and `0x4fbd0` respectively.
Each ELF segment has file size `0xd004`.
The DCB headers declare meaningful lengths `0xc7d8` and `0xcba8` at header `+4`.

Both alternatives contain identical records for all four sections below.
Thus their selection does not change this bounded register contract.
No payload bytes or register reset images are installed by the host.

| Header pair | First payload section | Second payload section | Bytes | Records | Interpreter base |
| --- | --- | --- | --- | --- | --- |
| `+0x80` | `+0x5d10` | `+0x5e48` | `0x340` | 104 | `0x31060000` |
| `+0x84` | `+0x6050` | `+0x6188` | `0x80` | 16 | `0x310ba000` |
| `+0x88` | `+0x60d0` | `+0x6208` | `0x98` | 19 | `0x34bc0100` |
| `+0x8c` | `+0x6168` | `+0x62a0` | `0x20` | 4 | `0x34bc0000` |

`platforms/qcom/sm88xx/sm8850/ddr-controls.lua` lists every accepted offset from these sections.
It contains addresses only, not the DCB values.
All selected list accesses are 32-bit stores.
The register identities and reserved-bit masks are not established by these stores.
The retained-word policy therefore preserves all 32 guest bits without claiming a physical hardware mask.
Unknown SHRM configuration words use the logged retained policy below. Other unmapped addresses remain errors.

## Related explicit controls

`0x148ad968..0x148ad9e8` programs sections `+0x84`, `+0x88`, and `+0x8c`.
The caller reaches this function at `0x148a759c` through `0x148a7530`.
The normal HAL calls `0x148a7530` at `0x148a8464`, immediately after SHRM startup.
The section `+0x80` call occurs later at `0x148a84d4`.
Thus the group spans SHRM startup. It does not precede every SHRM operation.

The same routine performs these explicit stores:

| Selected instruction | Address | Value |
| --- | --- | --- |
| `0x148ada08` | `0x31bc0134` | `0x91` |
| `0x148ada10` | `0x32bc0134` | `0x90` |
| `0x148ada18` | `0x31fc0134` | `0x91` |
| `0x148ada20` | `0x32fc0134` | `0x90` |
| `0x148ada28` | `0x34bc0604` | `0xf0000` |
| `0x148ada2c` | `0x34bc0608` | `0x601` |
| `0x148ada30` | `0x34bc0610` | `0xd07` |
| `0x148ada34` | `0x34bc0618` | `0x100e` |

At `0x148ada38`, XBL reads the low nibble of `0x3104ac94`.
When it equals three, XBL reads bit zero of `0x310ba250` before issuing the apply operation.
At `0x148ada54..0x148ada68`, it reads `0x34bc0618`, reads `0x310ba250`, writes one, then polls until bit zero clears.
At `0x148ada6c..0x148ada80`, it reads `0x310ba210` and writes `(old & 0x11110) | 1`.
The model preserves this latter guest-written word. It does not manufacture additional status bits.

## Functional policy

`QcomDdrFrontendProgram` uses compatible key `qcom,ddr-frontend-program-v1`.
Lua supplies the aperture, sixteen configuration offsets, apply offset, and retained offset.
The model has no absolute board addresses.

The programming latch accepts an apply write only when:

- The write is an aligned 32-bit transaction without byte enables.
- The value is exactly one.
- The guest wrote all sixteen declared configuration words since cold reset.

Accepted programming completes synchronously in the ideal functional device.
The apply control reads zero when idle, including its initial idle state.
Invalid apply writes return a command error and do not erase configuration.
Configuration reads return the exact guest-written words.
Cold reset clears configuration, write acceptance state, and the retained control.
This is an explicit functional policy, not an observed physical timing guarantee.
The model does not claim that arbitrary register values produce physical DDR training success.

The ordinary DCB controls preserve all 32 guest bits with zero virtual reset values.
They do not assert completion, readiness, or geometry.
The SHRM command aperture at `0x31076000+0x70` remains exclusively owned by `qcom,virtual-shrm-v1`.
The processor control at `0x31079000` retains its separate owner.

The SHRM configuration window covers `[0x31076070, 0x31079000)`.
One `QcomDdrPhyProgram` instance replaces the separate retained words in that window.
Declared DCB words retain their previous behavior.
Other words retain writes and report every access through `UNMODELED_DDR_CONFIGURATION_SUBSTITUTE`.
This window is an explicit virtual configuration policy, not a complete physical register map.
It excludes the authoritative command, response, startup, and reset state.

SM8850 run81 and SM8845 run78 reach a read at `0x310760a4` after the direct DDR command sequence.
The selected helpers are SM8850 `0x148ae6f4..0x148ae71c` and SM8845 `0x148ace54..0x148ace7c`.
They update bit four, then write one to the existing configuration word at `0x3107694c`.
They do not consume a separate completion or data result.

## Early clock preparation

The selected XBL function at `0x148a082c..0x148a08e4` performs this complete bounded sequence:

| Instructions | Address | Operation | Ownership |
| --- | --- | --- | --- |
| `0x148a0834..0x148a0850` | `0x153028` | Clear bit 0, then set bit 1 | Retained mask `3` |
| `0x148a0854..0x148a0864` | `0x177004` | Clear bit 0, then wait for bit 31 set | Existing UFS GDSC |
| `0x148a0868..0x148a0888` | `0x15301c` | Clear bit 0, then set bit 1 | Retained mask `3` |
| `0x148a0890..0x148a089c` | `0x152000` | Set bit 2 | Retained mask `4` |
| `0x148a08a4..0x148a08b0` | `0x14b004` | Set bit 0 | Retained mask `1` |
| `0x148a08b4..0x148a08bc` | `0x14b008` | Set bit 0 | Retained mask `1` |
| `0x148a08c0..0x148a08c8` | `0x310a8178` | Set bit 0 | Retained mask `1` |
| `0x148a08cc..0x148a08d4` | `0x17707c` | Set bit 0 | Existing UFS ICE branch |
| `0x148a08d8..0x148a08e0` | `0x177028` | Set bit 0 | Existing UFS AHB branch |

Each operation reads the word before it writes the modified value.
The function returns zero in `w0`.
The six new retained words start at zero as explicit virtual policy.
Their masks cover only the fields modified by this selected function.
Unknown bits and addresses remain rejected.
These words preserve requests without asserting root readiness or peripheral completion.

`0x177004` is not a branch disable operation.
`platforms/qcom/sm88xx/ufs.lua` already maps this address and its companion word to `qcom,gdsc-v1`.
`QcomGdsc` interprets cleared bit 0 as a power request.
It sets bit 31 only after its bound UFS consumer accepts that request.
The XBL loop waits for this power-on acknowledgment, not CLK_OFF.
The DDR helper must not replace this mapping with a detached status source.

`platforms/qcom/sm88xx/reset.lua` already maps both UFS branches.
The ICE branch uses `cpucp_clock_root_21`.
The AHB branch uses the explicitly declared `ufs_ahb_reference`.
The DDR helper does not replace either upstream source or map duplicate branches.
The platform composition must retain these existing UFS mappings.

## Integration and verification boundary

Register `QcomDdrFrontendProgram` from `models/qcom/sm88xx/ddr/controls.h` under `qcom,ddr-frontend-program-v1`.
Apply the returned function from `platforms/qcom/sm88xx/sm8850/ddr-controls.lua` to the platform table.
Do not create an SRAM mapping at `0x31060000` for these records.
Do not replace the guest DCB, its loader, or its interpreter.

Static analysis used pyelftools and Capstone against the exact selected package.
The four record sections were compared across both configuration alternatives and were identical.
The latest native runs pass the initial DCB programming sequence.
SM8850 run83 and SM8845 run80 reach the later LLCC geometry transfer.

## SM8845 selected frontend

`platforms/qcom/sm88xx/sm8845/ddr-controls.lua` supplies independent CPH2771 address data.
Its inputs are `IMAGES/xbl_sc.elf` and `IMAGES/xbl_config.elf` from
`CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`.
The XBL identity is SHA-256 `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
All addresses in this section use ELF virtual coordinates.
Configuration payload positions use ELF file offsets.

The caller at `0x148751ec` calls entry `0x1489f140`.
The complete entry ends at `0x1489f1f8`.
It performs these operations in order:

| Instructions | Address | Effect |
| --- | --- | --- |
| `0x1489f148..0x1489f164` | `0x153028` | Clear bit zero, then set bit one |
| `0x1489f168..0x1489f178` | `0x177004` | Clear bit zero, then wait for power acknowledgment bit 31 |
| `0x1489f17c..0x1489f19c` | `0x15301c` | Clear bit zero, then set bit one |
| `0x1489f1a4..0x1489f1b0` | `0x152000` | Set bit two |
| `0x1489f1b8..0x1489f1c4` | `0x14b004` | Set bit zero |
| `0x1489f1c8..0x1489f1d0` | `0x14b008` | Set bit zero |
| `0x1489f1d4..0x1489f1dc` | `0x310a8178` | Set bit zero |
| `0x1489f1e0..0x1489f1e8` | `0x17707c` | Enable the existing UFS ICE branch |
| `0x1489f1ec..0x1489f1f4` | `0x177028` | Enable the existing UFS AHB branch |

The six new controls retain only the modified fields.
Their zero reset values are virtual policy.
The helper does not create UFS power or branch mappings.
The existing UFS consumer, roots, branch sources, and power acknowledgment remain authoritative.

The configuration directory names `/A022_8_0100_0_dcb.bin` and `/A022_8_0100_1_dcb.bin`.
Their payloads start at file offsets `0x3fb14` and `0x4cb18`.
Each segment contains `0xd004` bytes.
Header `+4` declares meaningful lengths `0xbe90` and `0xc700`.
The host installs neither payload.

| Pair | First section | Second section | Records, first/second | Base |
| --- | --- | --- | --- | --- |
| `+0x80` | `0x5db0` | `0x5db8` | 103 / 104 | `0x31060000` |
| `+0x84` | `0x60e8` | `0x60f8` | 16 / 16 | `0x310ba000` |
| `+0x88` | `0x6168` | `0x6178` | 21 / 20 | `0x34bc0100` |
| `+0x8c` | `0x6210` | `0x6218` | 4 / 4 | `0x34bc0000` |

XBL `0x148abdd0` dispatches the register interpreter.
The interpreter body at `0x148bc9e8..0x148bca10` reads offset/value pairs and performs word stores.
XBL `0x148abfd4..0x148ac0f8` programs pairs `+0x84`, `+0x88`, and `+0x8c`.
It also performs the explicit controller stores and apply operation described above.
XBL `0x148ac134..0x148ac170` programs pair `+0x80` and writes `0x3107694c`.
The platform accepts the union of addresses from both SM8845 alternatives.
It does not use the SM8850 address tables.

The sixteen apply prerequisites match between the two SM8845 alternatives.
The existing frontend model therefore applies without a model change.
Guest configuration values, synchronous completion, reset, and unknown-word rejection retain the existing generic contract.
The full-word retained masks describe preservation policy, not established physical reserved-bit masks.
No SRAM mapping exists at `0x31060000`.
The existing SHRM DMEM ends at that boundary.

## Boot-pass controls for both selected OEMs

Both selected packages use the same five related word addresses.
The package identities above remain authoritative.
The analysis decoded executable ELF segments with Capstone and inspected all direct `0x0c2f` address builders.
It also searched aligned load-segment words for literal addresses in `0x0c2f0000..0x0c2fffff`.
That literal search found no addresses.
This establishes the selected direct consumers, not every possible indirect access in other firmware images.
Physical reset values and reserved-bit masks remain unknown.

| Word | Selected consumer contract |
| --- | --- |
| `0x0c2f0004` | Bit 1 selects completed first-pass state. Guest code sets bit 8, sets or clears bit 9, and later clears bit 1. |
| `0x0c2f0024` | Bit 0 supplies the BIMC alternate-reset condition. |
| `0x0c2f1000` | Later cleanup reads this separate word, saves it when its destination exists, then writes `0xffffffff`. |
| `0x0c2f1004` | Bit 10 reports first-pass expiry. Bit 12 supplies the logged debug PDC condition. |
| `0x3108012c` | Bit 0 enables the optional VDDQ preparation branch before full initialization. |

Do not confuse `0x0c2f1000` with the adjacent expiry word.
The later cleanup needs both the status word and the earlier control word.
SHRM run state remains at `0x31076008`, under the existing SHRM model.
The boot-pass helper does not replace that model or its backend-derived readiness.

### Conditions and cold-boot route

Define `C` as control bit 1, `A` as alternate-reset bit 0, and `E` as expiry bit 10.
The first condition returns `(!C) || (C && A) || E`.
The complementary condition returns `C && !A && !E`.
The first-condition entry also returns zero when the guest's platform selector equals two.
That selector remains guest-owned.

| Operation | SM8845 virtual address | SM8850 virtual address |
| --- | --- | --- |
| First condition | `0x1486fa3c..0x1486fa88` | `0x148712d0..0x1487131c` |
| Complementary condition | `0x1486fa8c..0x1486fabc` | Cached guest byte at `0x14a0c100`, getter `0x14871320..0x14871330` |
| Full decision routine | `0x1486fac0..0x1486fe44` | `0x14871334..0x148716d4` |
| Read optional VDDQ flag | `0x1486fbe8` | `0x14871584` |
| Call HAL initialization dispatcher | `0x1486fc48` | `0x148715e4` |
| Call SDI recovery dispatcher | `0x1486fdc0` | `0x14871464` |
| Store bit 8 request | `0x1486fcfc` | `0x14871698` |
| Set bit 9 | `0x14842f10` | `0x148449dc` |
| Clear bit 9 | `0x1484329c` | `0x14844d68` |
| Read later status word | `0x14879944` | `0x1487b248` |
| Write later status word | `0x1487995c` | `0x1487b260` |
| Clear bit 1 after successful service call | `0x14879970` | `0x1487b274` |

SM8850 writes the cached first-condition result at `0x148713d4`.
Its alternate-reset or expiry path writes one to that byte at `0x148714f0`, which selects full initialization.
SM8845 evaluates the complementary condition directly.

The HAL dispatchers use installed function pointers, not their zero ELF initializers.
SM8845 installs `0x148a6a7c` into `0x14937d08` at `0x14871f70`.
SM8850 installs `0x148a841c` into `0x14a14d08` at `0x1487367c`.
SM8845 installs its recovery target `0x148bc394` into `0x14937d30` at `0x14872010`.
The selected log strings identify these calls as `HAL_DDR_Init` and `HAL_DDR_SDI_Recover`.
The strings start at SM8845 `0x148dc414` and SM8850 `0x148de42f`.

The zero cold state selects full HAL initialization, not SDI recovery.
After that call returns, guest code records its own completion byte and cookie, then stores control bit 8.
The wrapper returns normally.
It does not wait for control bit 1 or request a CPU reset on this route.
Thus this selected cold route does not require a synthesized two-pass reset.
The bit 8 write alone does not establish a reset command or first-pass completion.
The model must not convert it into either effect.

The direct selected writers do not set control bit 1, expiry bit 10, or debug bit 12.
These observations do not establish the physical producers of those fields.
The virtual cold profile therefore leaves them zero instead of inventing a retained boot or timer expiry.

### Exact ELF file coordinates

| Site | SM8845 file offset | SM8850 file offset |
| --- | --- | --- |
| First-condition entry | `0x4ba3c` | `0x4b2d0` |
| Decision entry | `0x4bac0` | `0x4b334` |
| Bit 8 store | `0x4bcfc` | `0x4b698` |
| Bit 9 set | `0x1ef10` | `0x1e9dc` |
| Bit 9 clear | `0x1f29c` | `0x1ed68` |
| Later cleanup entry | `0x5592c` | `0x55230` |
| HAL target installation | `0x4df70` | `0x4d67c` |
| Installed full HAL target | `0x82a7c` | `0x8241c` |

### Virtual retained policy

Both DDR control helpers map the five words through existing `qcom,retained-boot-control-v1` instances.
Each instance declares `reset_value=0` and `writable_mask=0xffffffff`.
Each word accepts normal full-width guest configuration and returns the exact retained value.
No physical reserved-bit mask is claimed.
Construction and explicit `cold_reset()` restore zero.
A CPU-only reset must not call these cold-reset methods.
This retention of control values does not establish valid retained DDR contents or permit backend recovery without real backend state.

The later `0xffffffff` write is retained as an explicit virtual policy.
Its selected consumer does not establish write-one-to-clear semantics through a readback.
The helper therefore does not invent such semantics.
It also does not populate reset causes or trigger a reset.
Unknown addresses remain unmapped.
No host code changes guest cookies, DCB records, topology, training output, or completion bytes.
SHRM and existing virtual DDR effects remain responsible for their own actual state.

### Native integration

Both existing DDR control helpers use the registered retained-control model.
SM8850 run59 and SM8845 run54 pass the original boot-pass status failure.
Both complete SHRM image loading and continue through platform selection and voltage-sense initialization.
Their UART logs remain in `build/private/run-sm8850-common-runtime59` and `build/private/run-sm8845-common-runtime54`.
The next faults are calibration reads at `0x221c89dc` and `0x221c2550`, respectively.
These runs do not establish DDR initialization completion or kernel execution.

## Railway MX prechange controls

SM8850 `0x14899ddc..0x14899ea8` and SM8845 `0x14898754..0x14898820` program four AOSS ACC words.
The original registration selects the `vddmx` rail and a prechange callback.
Registration occurs at `0x1489a0d4..0x1489a0f8` and `0x14898a4c..0x14898a70`, respectively.

| Address | Native write mask | Configuration |
|---|---|---|
| `0x0c260304` | `0x44000000` | MXA HMX selector and override enable |
| `0x0c260308` | `0x0c000000` | MXC HMX override value and enable |
| `0x0c260310` | `0x1c000000` | MXA LMX override value and enable |
| `0x0c260314` | `0x1c000000` | MXC LMX override value and enable |

The guest derives override values from its current and requested rail modes.
The native MXA HMX write clears bit 30 rather than programming bit 27.
The model preserves that operation instead of correcting the guest.
The hole at `0x0c26030c` remains unmapped.

Each word uses the existing retained-control model with virtual cold zero.
These words do not create analog measurements, voltage transitions, or completion.
The adjacent preparation routine uses four words already owned by `dcp-bootstrap.lua`.
No duplicate owner exists.

SM8850 run63 and SM8845 run60 pass this group.
SM8850 then reaches its normal watchdog pet.
SM8845 instead fails `pm_system_init` because its PMIC1 LDO driver is absent.
Those separate dependencies appear in `models/qcom/spmi/README.md`.

## DDR-stage interconnect tables

The next interconnect group follows `Pre_DDR_clock_init`.
SM8850 run65 reaches its first write at `0x1484e238`, targeting `0x010b9004`.
The selected interpreter uses records of 24 bytes.
Each record contains an address, operation, field mask, register mask, and guest operand.
The board metadata retains only the register facts, not guest operands.

| Image | Record table | Count | Descriptor |
|---|---|---|---|
| SM8850 | `0x14921700` | 205 | `0x14922a38` |
| SM8845 | `0x1491cd88` | 120 | `0x1491d8c8` |
| SM8845 companion | `0x1491c568` | 86 | `0x1491cd78` |

Both complete groups use four-byte accesses.
Operation two writes configuration, operation five reads a temporary, and operation eight writes that temporary.
Neither group contains a poll.
Equal field and register masks select a direct write.
Thus the first failing write has no preceding read at that address.

The existing DDR frontend owns producer `0x310ba050`.
The interpreter reads its low seventeen bits, then writes them to `0x010b9754`.
Later guest operations set bits 30 and 31.
The destination is `PERIOD_BUS_CFG`.
The bits request a period load and software override.
The model retains these requests without generating a measured period or completion.
The separate `PERIOD_BUS_STATUS` word at `0x010b9758` has no consumer in these tables.

The common helper preserves existing owners and rejects partial overlaps.
Every read producer requires an existing owner.
Missing destinations receive exact four-byte retained mappings with combined native masks and virtual cold zero.
The guest supplies every configuration value and performs every copy.

SM8850 run66 and SM8845 run64 pass these tables.
Both log `sbl1_ddr_set_params`, then enter `sbl1_ddr_init`.
The integration run passes 76 tests and Clippy with warnings denied.
Both next assert in a regulator-mode request, before DDR initialization completes.


## QSEE frontend profile words

SM8850 QSEE function `0x15770858` reads words at `0x310ba29c` and `0x310ba2a0`.
It retains the low 17 bits in its private state.
The function does not write these words or request a DDR update.

An Infiniti EL1 probe read `0x139` and `0x1dd` from these two CPU virtual addresses.
The probe used aligned four-byte loads after OEM initialization.
This result does not establish silicon reset values.

The SM8850 profile supplies these values through two read-only frontend words.
The existing 16 guest configuration words still control the apply operation.
The profile words do not satisfy an apply requirement or generate a completion.

Diagnostic run351 passed both reads and reached QSEE address `0xd8218cdc`.
That run disabled the unconnected APSS watchdog terminal policy.
Thus the run proves the profile reads, not normal watchdog behavior or kernel execution.

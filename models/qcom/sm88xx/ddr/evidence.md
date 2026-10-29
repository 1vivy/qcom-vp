# Virtual DDR evidence

## Scope

`platforms/qcom/sm88xx/sm8850/ddr.lua` defines the production board wiring for the virtual DDR and SHRM models. The helper contains all required geometry and mapping data. It does not load `build/private/xbl-ddr-geometry.lua` or its facts JSON at run time.

This is a functional substitute for physical LPDDR5 training and SHRM execution. It does not bypass signed-image authentication, image loading, UEFI or ABL execution, or kernel-log proof.

## Selected XBL

The source is `IMAGES/xbl_sc.elf` from the selected CPH2749 package. Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`. Addresses below are PT_LOAD virtual addresses decoded as AArch64.

The remap table at XBL `0x148f3a80` contains these base-capacity pairs:

- `0x80000000`, `0x80000000`
- `0x880000000`, `0x780000000`

The exact 4 GiB MC range is `[0x80000000, 0x180000000)`. XBL exposes it through these two 2 GiB CPU windows:

- MC `[0x80000000, 0x100000000)` maps to CPU `[0x80000000, 0x100000000)`.
- MC `[0x100000000, 0x180000000)` maps to CPU `[0x880000000, 0x900000000)`.

The helper preserves the CPU hole `[0x100000000, 0x880000000)`. It does not expose the upper MC address as an APSS mapping.

## Virtual geometry and transport

The virtual policy has four interleaved x16 controllers.
Each controller has one 8-Gbit rank and two x8 PXI lanes. Rank one is absent.
MR8 `0x10` selects density code four and width code zero.
The native geometry table supplies 15 row bits, ten column bits, and sixteen banks:

`2^15 rows * 2^10 columns * 16 banks * 2 bytes = 1024 MiB per controller`

Each x8 lane holds 512 MiB. Eight lanes provide 4 GiB, not 8 GiB.
Parameters `rank0_mib` and `rank1_mib` specify MiB per x8 lane.
The DDR shared object is at APSS `0x3104a800`.
Guest pointers, clock plans, DCB data, timing data, and flash headers remain under guest control.

At `0x14871234..0x14871250`, XBL writes controller mask `0xf` at `+0x47c` and controller count four at `+0x482`.
Native training loops, for example `0x148b1060..0x148b10a4`, stop at four controllers.
The ICB converter at `0x1484cb5c` consumes four controller capacities and multiplies each interleaved entry by four.
The native writer at `0x148b66f0` reconstructs capacities from density table `0x149267b6`.
Density four with width zero selects 1024 MiB and source shift 30.
Geometry table `0x1492678e` selects 15/10. The native producer supplies sixteen banks.
The writer stores four capacities at `0x148b69e4` and zero source bases at `0x148b69ec`.
Its controller loop ends at `0x148b6a00`.

The backend publishes these values before native reconstruction:

| Shared offset | Value |
| --- | --- |
| `+0x48`, rank-zero MiB | `1024,1024,1024,1024,0,0,0,0` |
| `+0x68`, rank-one MiB | Eight zeros |
| `+0x98`, rank-zero source bases | Eight zeros |
| `+0xd8`, rank-one source bases | Eight zeros |
| `+0x33/+0x35/+0x37` | `15/10/16` |
| `+0x39/+0x3b/+0x3d/+0x41` | `0/4/1/1` |
| AOP `+0x600/+0x604/+0x605` | Four controllers, eight Gbit, absent rank one |

The UART sum at `0x14871f74..0x14871f98` reports 4096 MiB.
DARE builder `0x148bfb00` calls capacity provider `0x14878f14`, which sums eight rank-size slots and returns total GiB.
At 4 GiB, region 27 ends at granule `0x8ffff`. At 2 GiB, its end `0x87fff` precedes its start `0x8c000`.
The old MR8 `0x08` made native XBL reconstruct only 512 MiB per controller.
Stale slots four to seven could hide this error from UART and DARE, but not from the four-controller ICB converter.
No firmware patch, LLCC bypass, or later shared-memory overwrite supplies this geometry.

### SM8845 native geometry

The selected CPH2771 `IMAGES/xbl_sc.elf` has SHA-256 `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
Direct ELF analysis shows the same MR8, geometry, capacity, and source-base contract.

| Consumer or table | SM8850 address | SM8845 address |
| --- | --- | --- |
| Capacity writer | `0x148b66f0` | `0x148b4b90` |
| Geometry table | `0x1492678e` | `0x14921096` |
| Density table | `0x149267b6` | `0x149210be` |
| DARE builder | `0x148bfb00` | `0x148bdf60` |
| DARE capacity provider | `0x14878f14` | `0x14877658` |
| ICB converter | `0x1484cb5c` | `0x1484b048` |

SM8845 geometry entry `0x149210a6` contains row/column values `15/10`.
Density entry `0x149210de` contains width-zero capacity/shift `1024/30` and width-one values `2048/31`.
The detector extracts density and width at `0x148b49d0/0x148b49d8` and supplies sixteen banks at `0x148b49ec`.
The writer loads width-zero capacity at `0x148b4d30`.
It stores rank-zero capacity at `0x148b4e84` and zero source base at `0x148b4e8c`.
Its controller loop ends at `0x148b4ea0`, with limit four.

The SM8845 ICB selector uses one candidate, not the four SM8850 candidates.
Its list at `0x1491de28` selects configuration `0x1491ddb8`, with ten regions and four controllers at `+0x18/+0x1c`.
The writer also stores a byte at shared `+0x88` through `0x148b4e88`.
That store does not change the normalized source bases at `+0x98`.
DARE uses the same capacity formula through call `0x148be00c` and endpoint calculation `0x148be020`.
Its region-25 default start is `0xa4e0`, not SM8850's `0xa4b0`.
These differences require no separate virtual geometry.

### Geometry verification

`make build` succeeds. The build reports existing OpenSSL SHA384 deprecation warnings.
Host tests pass: 47 passed, zero failed. The host command leaves 68 native tests ignored.
The selected native tests pass after the fixture updates:

- CPUCP shared-DDR execution and the former local-alias assertions. The latter are withdrawn as incorrect ownership checks.
- DDR commands, both rank policies, MR8 decoding, header publication, PXI memory effects, and corrupted-byte rejection.
- SHRM reset, startup, and pending commands.
- LLCC memory and security boundaries.
- Sequencer debug policy and stale address lists.

The first native run exposed old SHRM capacity/row checks and old PXI byte-address expectations.
The updated fixtures use 1024 MiB per controller, 15 rows, and one byte per x8 column.
No OEM boot or firmware modification formed part of this verification.


The SHRM transport aperture is APSS `[0x31076000, 0x31076070)`.
Its clock-gating control at offset four does not release the processor.
The separate processor control at `0x31079000` starts the backend after reset release.
A command completes synchronously only after all required backend transactions succeed.

## Backing and DMA coherence

Two `qcom,shared-executable-memory-v1` objects back the two CPU windows. Their ranges do not overlap. Each physical byte therefore has one backing object. Both objects use the APSS CPU as the code owner. They cold-zero and allocate memory in 2 MiB blocks.

UFS PRDT entries and sequencer hash requests contain guest system-physical buffer addresses. The existing DMA routers use that same system-address view. Each router has these aliases:

- DMA `[0x80000000, 0x100000000)` targets the low backing at the same APSS base.
- DMA `[0x880000000, 0x900000000)` targets the high backing at the same APSS base.

Each `addrtr` alias uses `mapped_base_addr=0` and `relative_addresses=false`. It sends a relative transaction address to the existing backing target socket. A DMA write therefore changes the same bytes that APSS reads or executes.

`SharedExecutableMemory` invalidates owner code at its physical base plus the relative transaction address. The remap table describes XBL's CPU windows and MC geometry. It does not establish a separate DMA address domain.

No MC router exists because no modeled initiator requires one.

## Exclusions

The helper does not contain vendor data, fake training headers, image preload, or guest-SMEM injection. It does not duplicate SHRM IRAM at `0x31020000` or SHRM DMEM at `0x31048000`. The reset platform owns those mappings and their UFS and sequencer aliases.

`0x31060000` is an interpreter base, not a DCB buffer destination.
The selected register writes already use the frontend controls described below.
The helper must not add RAM at that base.

## DCB interpreter base and ownership

This section resolves the earlier unknown destination.
Static analysis independently decoded both selected XBL images and all four selected DCB alternatives.
It also decoded the selected SHRM startup and snapshot routines.
Image identities and configuration payload coordinates appear in `controls-evidence.md`.

| Contract | SM8850 | SM8845 |
| --- | --- | --- |
| DCB section dispatcher | XBL `0x148adac8..0x148adb04` | XBL `0x148ac134..0x148ac170` |
| Word-store interpreter | XBL `0x148be4c0..0x148be4e8` | XBL `0x148bc9e8..0x148bca10` |
| SHRM memory-bound publication | SHRM `0x200d98..0x200dbc` | SHRM `0x200daa..0x200dce` |
| Physical snapshot routine | SHRM `0x208526` | SHRM `0x209354` |

The dispatcher reads the guest DCB pointer from shared `+0x468`, at APSS `0x3104ac68`.
It reads a section offset and byte length from DCB `+0x80/+0x82`.
It passes `0x31060000`, `length >> 3`, and the section pointer to the interpreter.
The interpreter consumes eight-byte records containing two little-endian words.
For each record, it adds the first word to the base using 32-bit arithmetic.
It stores the second word at the resulting address.
It does not copy the section into a contiguous destination.

### Extent and alignment

| Selected DCB | Records | Sparse write envelope, end exclusive |
| --- | --- | --- |
| SM8850 `/A01B_8_0100_1_dcb.bin` | 104 | `[0x310760a0, 0x31078004)` |
| SM8850 `/A01B_8_0200_1_dcb.bin` | 104 | `[0x310760a0, 0x31078004)` |
| SM8845 `/A022_8_0100_0_dcb.bin` | 103 | `[0x310760a0, 0x31077054)` |
| SM8845 `/A022_8_0100_1_dcb.bin` | 104 | `[0x310760a0, 0x31078004)` |

Each alternative writes distinct, four-byte-aligned words.
No record writes `0x31060000`.
These envelopes are not RAM extents or permission to accept their intervening addresses.
The SoC-specific `*-ddr-controls.lua` files list the exact accepted words.
Each dispatcher also writes one to the separate control at `0x3107694c`.

SHRM startup publishes local bounds `0x402000` and `0x418000`, plus length `0x16000`, at local `0x400000/+4/+8`.
It translates those bounds with `0x30c48000`.
The resulting APSS interval is `[0x3104a000, 0x31060000)`.
Thus `0x31060000` is also an exclusive memory endpoint, not a byte inside that interval.
The reset platform already backs this interval within its sole SHRM DMEM mapping, `[0x31048000, 0x31060000)`.
The XBL tables also contain the pair `0x31020000, 0x31060000`.
Their coordinates are SM8850 `0x148e4df0` and `0x148e6540`, and SM8845 `0x148e2f60` and `0x148e4698`.
Those pairs do not justify extending SRAM beyond its existing end.

### Contents, aliases, and lifetime

The original guest loads and authenticates the configuration image before it selects and interprets a DCB.
The DCB pointer, payload records, and programmed values remain guest-owned.
The frontend words represent configuration writes, not hardware training results or a second DCB image.
The dispatcher does not read those words or request hardware output from this section.
The later explicit control remains a retained guest write.
Its physical register identity and hardware side effects remain unestablished.

Both SHRM snapshot routines materialize APSS register bases `0x31076000`, `0x31072000`, and `0x31070000`.
This supports register ownership around the interpreted addresses, not a RAM alias beginning at `0x31060000`.
The functional SHRM backend substitutes ideal training and bounded snapshot effects as described above.
It reads the actual DCB through shared `+0x468`.
It does not relocate the DCB into the interpreter base.

No new CPU, UFS, sequencer, MC, or SHRM-local alias is required for this contract.
The existing DMA aliases continue to target the sole SRAM and DRAM backing objects.
The SHRM command aperture `[0x31076000, 0x31076070)` remains separate from the first interpreted word at `0x310760a0`.
The existing frontend instances retain all 32 guest bits, with zero initial values as explicit virtual policy.
They do not manufacture readiness, completion, geometry, flash headers, or SMEM.
Their lifetime follows the existing retained-control instances, not a transient host DCB allocation.
No vendor reset values or persistent training identity are inferred.

The existing controls implement this bounded contract on both SoCs.
No additional mapping, model registration, RAM substitute, or platform interface is required.
Accesses to the base itself and other unlisted words remain unknown accesses.
Physical register masks, analog effects, and unselected firmware variants remain outside this evidence.

## Verification

Executable verification is pending. That verification must load the helper and check these results:

1. The two APSS windows cover exactly 4 GiB and preserve the CPU hole.
2. UFS and sequencer DMA expose the same two system-physical windows as APSS.
3. Low and high DMA writes are visible through the corresponding APSS windows.
4. A high-window DMA write invalidates APSS code at `0x880000000` plus the transaction offset.
5. The DCB interpreter writes only the selected frontend words. Accesses to `0x31060000` itself remain unknown accesses.
6. The helper has no run-time dependency on `build/private`.

### Smoke procedure for the DCB section

Run this procedure separately with each selected OEM package and its existing runtime helper.
Both runtime helpers already compose their SoC-specific DDR, frontend-control, and PHY helpers before guest execution.

1. Retain normal signed-image authentication, UFS reads, access logging, and the original XBL instruction stream.
2. Break at SM8850 `0x148adac8` or SM8845 `0x148ac134`, after the actual guest reaches its DDR stage.
3. Read the pointer at `0x3104ac68`. Read the selected DCB section offset and length at pointer `+0x80/+0x82`.
4. At interpreter entry, check `w0=0x31060000`, `w1=length >> 3`, and `x2=pointer+offset`.
5. Trace the interpreter word stores. Compare each address and value with the corresponding live guest record.
6. Check the selected record count and sparse envelope against the table above. Check that no store targets the base itself.
7. Continue through SM8850 `0x148adafc` or SM8845 `0x148ac168`. Check the separate write of one to `0x3107694c`.
8. Read back the accepted configuration words. Their values must equal guest writes, without a host payload preload.
9. Continue through subsequent SHRM commands. Retain their requests, responses, unknown-access reports, and the guest boot log.
10. Check that SRAM owners, DMA coherence, and the CPU hole remain unchanged. Do not require a new alias at the interpreter base.

If authentication or another earlier stage blocks execution, report that first blocker.
Do not count static decoding or an isolated interpreter replay as proof that the real XBL DDR stage ran.
This slice performed static analysis only. Executable smoke results remain pending.

## SM8845 selected virtual DDR

`platforms/qcom/sm88xx/sm8845/ddr.lua` supplies independent CPH2771 board data.
`platforms/qcom/sm88xx/sm8845/runtime.lua` composes that helper, the selected frontend controls, and the selected PHY data.
The input package is `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`.
Static evidence uses its `IMAGES/xbl_sc.elf`, `IMAGES/xbl_config.elf`, and `IMAGES/shrm.elf`.
The XBL identity is SHA-256 `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
Addresses below use ELF virtual coordinates.
SHRM code uses RV32IMC, not AArch64.

### Shared ABI comparison

SM8845 SHRM reports version `5, 0x12, 0`.
SM8850 SHRM reports version `0x1d, 0, 0`.
These are metadata differences for the effects implemented by this model.
The backend does not select behavior from the version.

| Contract | SM8845 evidence | Compared SM8850 evidence |
| --- | --- | --- |
| Shared version producer | SHRM `0x200d92` stores at local `0x40280c`, `0x402810`, and `0x402814` | SHRM `0x200d82`, same destinations |
| Shared version consumer | XBL `0x148ac0fc` compares `5, 0x12, 0` at shared `+0xc/+0x10/+0x14` | XBL `0x148ada94`, compared fields and abort behavior |
| Guest SHRM release | XBL `0x148ac174` | XBL `0x148adb08`, same startup control and status |
| Mode and route | XBL `0x148ac260` | XBL `0x148adbf4`, identical register operations |
| Request and response | XBL `0x148ac288` | XBL `0x148adc1c`, identical request, trigger, and acknowledgment operations |
| Initialization request | XBL `0x148a6c0c` | XBL `0x148a8558`, same `0x09080000` plus low argument byte |
| AOP clock and timing consumer | XBL `0x148a681c` | XBL `0x148a81bc`, identical consumed offsets |
| Post-training restore data | XBL `0x148a693c` | XBL `0x148a82dc`, identical DCB and AOP offsets |
| Training callback request | XBL `0x148a830c` | XBL `0x148a9c54`, same `0x09800000` command |
| Exit request | XBL `0x148ab3bc` | XBL `0x148acd50`, same `0x09410000` command |
| Flash partition preparation | XBL `0x148a6fd8` clears `0xbb78` bytes through shared `+0x460` | XBL `0x148a8924`, same pointer and bound |
| Training save request | XBL `0x148a7074`, request `0x05060000` | XBL `0x148a89c0`, same command |
| Physical snapshot save/restore | SHRM `0x202b0a` / `0x202a52` call `0x209354` | SHRM `0x201fbc` / `0x201f4e` call `0x208526` |

The snapshot producers use the same local pointer at `0x402c60`.
They use the same translation constant `0x30c48000`.
Both use the descriptor-selected prefix of a `0xa000`-byte capacity at the external flash pointer plus `0x1a08`.
The selected SHRM class-one jump table at `0x20eaa0` assigns setup, save, and restore to the same subcommands.
The class-two handler table remains at `0x4000c0`.
Initialization, training callbacks, and exit retain their existing command classes.
No command or output layout changes with the startup metadata parameters.

The primary shared base remains APSS `0x3104a800`, corresponding to SHRM local `0x402800`.
The backend consumes the same flash, DCB, and AOP pointer fields at `+0x460/+0x468/+0x470`.
Firmware retains its callback pointer at `+0x478`.
The backend does not execute that callback.
The geometry fields remain `+0x30..+0x42`.
Per-controller sizes remain at `+0x48` and `+0x68`.
Per-controller bases remain at `+0x98` and `+0xd8`.
XBL `0x148728b4` consumes those sizes and bases through the unchanged conversion loops.
Its internal software-object offsets differ from SM8850.
Those objects remain guest-owned and are not backend ABI fields.

The AOP clock entries remain at `+0x80`, with stride `0x28`.
The index, frequency, and enable fields occupy entry offsets `+4`, `+8`, and `+0x10`.
Partition fields remain AOP `+0x600..+0x605`.
XBL `0x14872ae0` consumes those partition fields for its own granule calculation.
The runtime DCB fields and restore sections retain the existing backend contract.
XBL `0x148a7088..0x148a70b0` writes its own flash version `5, 0x12, 0`.
The backend does not write that header.

Startup metadata uses `startup_version_major`, `startup_version_minor`, and `startup_version_patch`.
Their defaults preserve SM8850 metadata.
The SM8845 platform explicitly selects `5`, `0x12`, and zero.
Startup writes only the existing twelve-byte version span.
The model keeps its command validation, shared offsets, state transitions, and output spans unchanged.
There is no SoC condition or version-dependent behavior in the generic model.

### Memory and functional policy

SM8845 XBL remap table `0x148f1488` contains the same two base-capacity pairs listed above.
The selected virtual topology therefore uses the same disjoint 2 GiB CPU windows.
The virtual geometry uses four x16 controllers, eight x8 lanes, one rank, and 4 GiB total.
It is not a captured CPH2771 part identity or capacity.

The reset platform remains the sole owner of APSS SRAM, SHRM IRAM, and SHRM DMEM.
The DDR helper requires those instances.
It does not create replacement shared objects.
New DRAM windows each receive one backing object and shared UFS/sequencer DMA aliases.
The original firmware still loads and authenticates SHRM and the DCB.
It still runs its geometry conversion, flash-header construction, memory-region publication, and later boot stages.

Virtual effects include synchronous SHRM completion, ideal electrical calibration, real digital comparisons, register snapshots, and the declared geometry.
Frontend update timing, reference observations, and unmeasured register reset values are also explicit virtual policy.
The physical DRAM part, physical capacity, training margins, voltage behavior, and analog timing remain unknown.
The PHY evidence records the remaining unknown physical masks and source relationships.
No host training header, guest DCB replacement, SMEM injection, or authentication bypass is present.

Static analysis parsed selected ELF segments and decoded AArch64 and RV32IMC instructions.
It did not run gates or calculate new artifact hashes.
Check startup metadata, shared output preservation, command effects, DMA coherence, and the retained boot log.


## Post-DDR CPUCP shared views

Both DDR helpers retain the existing `ddr_low` and `ddr_high` owners.
They add system-physical CPUCP views when the reset composition supplies a CPUCP router.
UFS DMA, sequencer authentication DMA, APSS, and CPUCP use those same backing objects.
No new memory overlaps `0x80000000..0xffffffff`.

Selected CPUCP ELF maps are recorded in `models/qcom/sm88xx/cpucp/evidence.md`.
Their zero-file virtual/physical pairs do not define local SRAM aliases.
The physical zero-fill extents remain in `ddr_low`.
The initialized SRAM and DDR image bytes retain their existing shared owners.

The reset composition owns a separate, persistent local SRAM interval at `0x18b30000+0x40000`.
The real runtime poisons independent DDR snapshot buffers, then copies complete live IRAM and local SRAM into them.
The former per-SoC local aliases corrupted this separation and are removed.
The [CPUCP evidence](../cpucp/evidence.md#local-sram-and-independent-ddr-snapshots) records both buffer producers and the invalidated alias claims.
The old alias-equality assertions are not valid evidence of native local-memory ownership.
Physical-DDR instruction replacement, shared DMA backing, CPU types, protection ports, authentication, and GIC PPI27 remain unchanged.
The rewritten native fixture preserves those independent checks and adds snapshot isolation and reset retention.
The rewritten fixture passes for both SoCs. The full integration run passes 47 host tests and 73 platform tests.
SM8850 run274 passes the former poisoned return. Its later watchdog stop does not invalidate the memory isolation result.

## TME memory views and SHRM reset

The TME CPU and sequencer authentication DMA are separate consumers.
Both require the actual guest-loaded bytes, not independent copies.
The reset platform now aliases APSS IMEM, SOCCP debug SRAM, CPUCP image SRAM, and SHRM IRAM/DMEM into the TME router.
Both DDR helpers also expose the existing low and high backings to that router.
The existing TME protection path remains authoritative.

SM8845 run52 failed a direct TME byte read at `0x31047e00`, from instruction `0x230bbc`.
The shared aliases resolve that access without replacing image data.
The next TME operation writes the separate processor control at `0x31079000`.
SM8850 instructions `0x4373a2/0x4373a6/0x4373a8` write ten, two, and zero.
SM8845 instructions `0x43761e/0x437622/0x437624` perform the same sequence.

Clock gating occupies CSR offset four, and processor status occupies offset eight.
The model uses the observed processor-control address.
Bits zero and one assert virtual reset.
Reset clears transport and backend state but preserves guest memory.
Release publishes the existing backend startup metadata before readiness.
Repeated release writes preserve active state.
The cold processor-control value of two is explicit virtual policy.

The guest regression `shrm_processor_reset_controls_startup_and_pending_commands` passes.
It checks shared reset views, clock-gating independence, pending-command removal, memory preservation, and metadata publication after another release.
SM8850 run59 and SM8845 run54 complete SHRM image loading with this behavior.
Their logs report 175160 and 178354 loaded bytes, respectively.
These observations do not establish complete boot-chain authentication qualification or kernel execution.

## Direct command engine and native geometry

The original XBL reconstructs geometry after the SHRM initialization response.
It uses mode-register reads, rather than retaining the backend's initial geometry fields.
SM8850 calls this path at `0x148a788c`. SM8845 calls it at `0x148a5ebc`.
The virtual direct engine supplies those reads from its own device state.

The broadcast base is `0x34a00000`.
The four pair bases are `0x31a00000`, `0x32a00000`, `0x31e00000`, and `0x32e00000`.
Each pair retains independent descriptor, mode-register, selection, and result state.
Broadcast writes update every pair. Broadcast reads select pair zero under explicit virtual policy.
Other broadcast configuration words retain their existing frontend owner.

| Offset | Guest-visible operation |
| --- | --- |
| `0x708` | Opcode `0x50` completion bits eight and nine |
| `0x5700 + 8*slot` | Descriptor with operation, rank mask, MR index, and data |
| `0x5704 + 8*slot` | Retained companion fields |
| `0x5800` | Descriptor selection bitmask |
| `0x5804` | Start request and completion |
| `0x5a38`, `0x5a3c` | Rank-zero and rank-one read results |
| `0x5c94` | Absent-rank status, at bits zero and four |

SM8850 writers are `0x148abca0` and `0x148abcfc`.
SM8845 writers are `0x148aa318` and `0x148aa374`.
Trigger helpers are `0x148abd1c` and `0x148aa394`.
They write one, then wait until bit zero clears.
The engine applies all selected MR effects before it clears that bit.
An unsupported descriptor fails the batch without publishing MR effects or completion.

Operation `0x70` reads an MR. Operation `0x74` writes an MR.
Operations `0x00`, `0x60`, `0x64`, `0xa0`, and `0xa4` use ideal electrical timing.
Their physical names remain unestablished.
The selection `0xb0f7` addresses slots zero, one, two, four, five, six, seven, twelve, thirteen, and fifteen.
Native code can omit the slot-fifteen write.
Its cold zero descriptor therefore remains an empty operation, not an invalid missing write.

Populated ranks report MR5 `0xff`, MR6 one, MR7 zero, and MR8 `0x10`.
These values specify a virtual identity and eight-Gbit x16 density.
The native geometry tables are `0x1492678e` and `0x14921096`.
They yield 15 rows and ten columns. The native producer supplies sixteen banks.
Absent MR reads return all ones. The native consumer checks rank status before using those results.

The absent-rank path also programs pair offsets `0x3080` and `0x6148`.
All four pairs retain these ordinary control words without a physical timing claim.
The selected sequences are SM8850 `0x148b647c..0x148b64b0` and SM8845 `0x148b491c..0x148b4950`.
Neither sequence polls these words for completion.

All four selected DCB headers declare runtime section `+0x38` with length `0xb00`.
Training flags at runtime offsets `0xbb4..0xbb6` belong to the adjacent section described at `+0x3c`.
The backend checks both readable sections and the flag containment separately.
It does not extend the declared runtime section or invent missing image data.

The AArch64 regression exercises sparse selection, MR retention, pair isolation, rank presence, geometry, and unsupported-operation rejection.
It uses the real command engine and DDR backend.
The earlier integration run passed 77 tests and Clippy, before the opcode extension below.

### Remaining native descriptor operations

Static analysis used the unmodified CPH2749 and CPH2771 `IMAGES/xbl_sc.elf` files.
Each image contains 89 direct branches or calls to its `+0x5700` descriptor writer.
Both images produce the same operation inventory and caller counts:

| Operation | Callers per image | Explicit virtual behavior |
| --- | --- | --- |
| `0x00` | 1 | Empty operation with ideal timing |
| `0x50` | 1 | Set requested-rank completion bits at `+0x708` |
| `0x58` | 2 | Ideal electrical operation, without MR or result changes |
| `0x60` | 4 | Ideal electrical operation |
| `0x64` | 4 | Ideal electrical operation |
| `0x70` | 4 | Read the selected MR |
| `0x74` | 66 | Write the selected MR before completion |
| `0x77` | 2 | Write zero to MR16 before completion |
| `0x8c` | 1 | Ideal electrical operation, without MR or result changes |
| `0x94` | 1 | Measure the configured virtual oscillator interval |
| `0xa0` | 2 | Ideal electrical operation |
| `0xa4` | 1 | Ideal electrical operation |

The new electrical forms require a nonzero rank mask and zero address/data fields.
The `0x77` form accepts only the observed MR16 address and zero data.
Its physical subtype remains unestablished.
The MR16 write is explicit virtual policy, based on the native MR-address/data request.
Other `0x77` payloads fail, rather than silently completing.

Saved-register producers do not introduce additional operations.
Every saved-register source resolves to `0x74` at these defining instructions:

| Saved register | SM8850 definitions | SM8845 definitions |
| --- | --- | --- |
| `w21` | `0x148b60d4`, `0x148b8704` | `0x148b4574`, `0x148b6ba4` |
| `w23` | `0x148b79f4`, `0x148bd278`, `0x148bd8f8` | `0x148b5e94`, `0x148bb778`, `0x148bbe20` |
| `w25` | `0x148b7ac8`, `0x148b7b84`, `0x148bd16c` | `0x148b5f68`, `0x148b6024`, `0x148bb5f8` |
| `w19` | `0x148b7be0`, `0x148bcfc8` | `0x148b6080` |
| `w22` | `0x148b80cc`, `0x148b830c`, `0x148b87d4` | `0x148b656c`, `0x148b67ac`, `0x148b6c74` |
| `w24` | `0x148bd844` | `0x148bbd6c` |
| `w27` | None | `0x148bb4b8` |

SM8850 `0x148b74f8..0x148b7520` issues operation `0x8c` in slot zero with zero address and data.
SM8845 `0x148b5998..0x148b59c0` issues the same request.
Both consumers wait for trigger completion, then continue guest-owned PXI preparation.
Neither consumer reads a data result from this operation.
The substitute completes its ideal electrical effect without changing MR state or shared memory.

SM8850 `0x148b7c88..0x148b7d24` selects operations `0xa0`, `0x64`, and `0x50`.
SM8845 `0x148b6128..0x148b61c4` selects the same sequence.
Both then poll `0x34a00708` bits eight and nine.
The engine owns this read-only status word and sets only requested-rank bits before completion.
The status is zero after cold reset and remains independent for each pair.
These bits acknowledge electrical requests, including requests for absent ranks.
They do not replace the separate rank-presence status.

The `0x58` producers are SM8850 `0x148b7dd0`/`0x148bd58c` and SM8845 `0x148b6270`/`0x148bba8c`.
They request zero address and data.
The `0x77` producers are SM8850 `0x148b7e94`/`0x148bd828` and SM8845 `0x148b6334`/`0x148bbd50`.
They request MR16 and zero data.
The second producer conditionally includes this write in a larger batch of MR writes.

### Oscillator request and result consumers

SM8850 `0x148aa1f4..0x148aa2b4` selects operation `0x94`, followed by an MR35 read.
SM8845 `0x148a88ac..0x148a896c` selects the same sequence.
Each consumer saves the low count bytes, then executes the separately selected MR36 read.
SM8850 `0x148aa3a0..0x148aa450` combines the bytes into per-lane 16-bit counts.
SM8845 `0x148a8a58..0x148a8b08` performs the same combination.
The consumers compare those counts with the low/high limits at pair offset `0x53a4`.
Later arithmetic divides the requested interval by each count.
A zero or fixed magic result cannot represent this protocol.

The guest writes MR37 before each measurement.
SM8850 `0x148aa688..0x148aa6e8` encodes the requested CK interval.
SM8845 `0x148a8d40..0x148a8da0` uses the same encoding.
Their tables at `0x148e5760` and `0x148e38b0` contain `128, 256, 512, 1008, 2048, 4096, 8192`.
The first four intervals use `MR37 = interval / 16`.
The last three use `0x40`, `0x80`, and `0xc0`.
SM8850 `0x148aacd4..0x148aad20` converts the interval into a companion wait using the configured frequency.
SM8845 `0x148a938c..0x148a93d8` performs the same conversion.
The conversion uses a 19.2 MHz reference and adds one reference tick.
The companion wait is not the oscillator interval.

The virtual oscillator has a fixed physical period of 400 ps.
Its count is the requested CK duration divided by twice that period, rounded to the nearest integer.
The backend derives CK duration from the active SHRM period and ratio.
The explicit reset clock period is 800 ps until the guest selects a plan.
The engine stores its low/high bytes in MR35/MR36 before it clears the trigger.
An MR35/MR36 read returns the corresponding count byte on both x8 lanes.
Other identity reads retain their existing single-byte representation.
An absent-rank read still returns all ones.
The substitute does not access guest-owned training outputs or adjust the guest's acceptance limits.

The batch validator tracks preceding MR37 writes without changing live state.
A populated rank needs a valid nonzero MR37 interval before operation `0x94`.
Zero intervals and unsupported extended encodings fail the batch.
Absent ranks need no oscillator state and receive no MR effects.
Validation covers every selected pair before any pair receives MR or completion effects.
Unknown operations and unsupported forms still fail without completion.

The existing guest fixture now covers the new operations, status isolation, MR16 effects, and interval-dependent oscillator results.
It also checks that electrical operations preserve the previous result.
The callback regression also changes the clock and ratio while it checks a stable measured physical period.

## Native AOP clock plan

SM8850 run 74 stops before the first SHRM trigger at XBL `0x148adc74`.
The native request is `0x09080051`.
The guest supplies DCB pointer `0x0b080000` and AOP pointer `0x3105f5a0`.
The shared object declares twelve primary levels and ten secondary levels.

Each AOP row occupies `0x28` bytes, starting at object offset `0x80`.
The word at row offset four is the level index, not a frequency.
The word at offset eight contains the frequency value.
The first row has index zero, frequency zero, and an enabled flag at offset `0x10`.
The second row has index one and frequency value `0x85980`.

The backend previously rejected the enabled first row because it treated the index as a required nonzero frequency.
The current contract accepts that native idle row.
It still checks the declared counts, enabled flags, selected indices, input ranges, and required command effects.
The SHRM guest regression now supplies an enabled idle row and checks the resulting rank capacity and geometry.

SM8850 run87 and SM8845 run84 return from the original `DDR_FW_Init`.
Both UART logs report `ddr_init = 1 cold boot`.
Both complete `sbl1_ddr_init` and report 4096 MB on rank zero, with no second rank.
SM8850 run88 and SM8845 run85 also pass the subsequent GEM NoC configuration.
Both enter `do_ddr_training` before they stop at legacy TME import `0x4168f0`.
The TME PCs are `0x42c374` and `0x42c3da`, respectively.
The later `DDR_FW_Exit`, UEFI, ABL, and kernel execution remain unobserved.

## SHRM training effects and clock state

Request `0x09800000` dispatches a runtime callback, not a clock-plan index.
The low 16 bits select one of 44 rows, each with a 64-byte stride.
The row contains its name at offset 22 and enable byte at offset 47.
SHRM `0x203b68` initializes the result buffers. Cleanup `0x203770` publishes their byte counts.

The internal object at local `0x405000` differs from the primary header at `0x402800`.
Its pointer fields have these roles:

| Offset | Consumer |
| --- | --- |
| `0x450` | Shared training scratch |
| `0x458` | Training parameters and measurements |
| `0x460` | Training data |
| `0x468`, `0x470` | Two result buffers, each with capacity `0xe00` |
| `0x478` | Runtime callback rows |
| `0x488`, `0x48c` | Published result byte counts |

The calibration adapter applies zero electrical correction and a declared HCD selector policy.
It preserves native MR copies, packed records, and failure records.
DIT uses the command engine's oscillator results and retains the native 100–880 ps acceptance range.
RX_OC remains a native abort path. The selected DCB disables WCK2CK and CBT.

DIT searches intervals `128, 256, 512, 1008, 2048, 4096, 8192`.
It selects the first interval whose measured margins reach 20, or the final interval.
The command engine performs each measurement. A zero margin does not become a fabricated positive margin.

Native HCD calibrates its starting state. It does not require that state to match a canonical table row.
The virtual policy assigns half-cycle weights to retained fields.
V2 uses `4*first + 2*second + third + code`. V1 uses `4*first + third + code`.
Codes five through seven contribute their weight. An index outside the table range fails.
The model logs noncanonical inputs and writes canonical fields to both live and profile registers.
These weights do not establish a recovered physical delay for an ambiguous starting state.

The V2 table has eleven rows at `0x400970`. The V1 table has eight rows at `0x400940`.
The following pattern table starts at `0x4009c8`.
Native writers `0x2149b0` and `0x214b66` establish the coupled live and profile effects.
The adapter processes DMI only when bit seven at `0x402fa0 + 6*fsp` enables it.

Result overflow follows `0x207a2a` and `0x2079f6`.
The adapter stores only the first `0xdfc` bytes and continues the byte count.
It sets the native overflow flags without failing calibration.

The PXI adapter reads the guest's pattern tables.
Its normalized transfer uses 1 KiB per selected channel through actual shared DRAM.
The adapter restores the retained native PXI program afterward.
Read-eye callbacks report actual comparison errors without rewriting corrupted bytes.
The exact physical transfer length and electrical margins remain unqualified.

SM8845 relocates immutable lookup tables by `0x38`.
The recovered `gp` values are `0x400be0` for SM8850 and `0x400c18` for SM8845.
Byte comparisons show matching HCD, pattern, pin, Vref, and half-channel tables at that displacement.
Result metadata at `0x4001cc` retains the same address.
CCI `training_data_offset` expresses this ABI difference without a SoC condition.

Snapshot descriptors select actual registers.
The selected SM8850 descriptor stream occupies 4592 bytes and selects 8980 words.
Save therefore writes 35920 bytes, not the entire `0xa000`-byte capacity.
Restore consumes guest bytes and writes the same registers.
The regression changes the saved bytes before restore and checks the resulting register values.

SHRM `0x2012e8` stages primary and secondary clock selections.
The primary table starts at `0x402cb8`, with a 20-byte stride.
Its frequency and period occupy offsets four and twelve.
SHRM `0x202f86` commits the level and period at `0x403a98` and `0x403a90`.
SHRM `0x202ff6` commits the secondary state at `0x403388`, `0x403380`, and `0x40337c`.
The adapter publishes these fields before acknowledgment and preserves the trained lifecycle across later clock changes.

## DDR finalization and period observation

SM8850 `0x148ab498` and SM8845 `0x148a9b50` read retained controls before clock gating.
The source group starts at `0x31211028` and ends at `0x31211280`.
The platform lists each source word and preserves existing owners.
Unwritten controls retain zero. These controls do not report completion.

Native SHRM `0x2012e8` selects the raw primary period from `0x402cc4 + 20*level`.
SHRM `0x20b3b0` writes that period to `0x34a00080`.
The functional adapter publishes the same register before it acknowledges a clock change.
The upper-FSP copy also publishes the selected period.

The AND view at `0x34e00000` reads all four frontend pairs.
It returns their bitwise AND. Writes reach all four pairs.
The existing broadcast view retains its first-pair read policy.

SM8850 `0x14872160` and SM8845 `0x148708ec` read `0x34e00080`.
The diagnostic masks the result with `0x1ffff` and calculates `1000000 / period`.
The oscillator model instead uses the derived CK period.
That period equals the raw period multiplied by four for ratio three, or by two otherwise.
The diagnostic must not receive the derived CK period.

### Verification

`make build` succeeds.
`cargo test --locked -- --include-ignored` passes all 91 tests.
`cargo clippy --locked --all-targets -- -D warnings` succeeds.
The regressions cover actual snapshot restoration, corrupted PXI data, clock transitions, HCD normalization, disabled DMI, DIT interval selection, and key publication.

SM8850 run112 and SM8845 run98 complete training, DARE initialization, and `DDR_FW_Exit`.
Both UART logs print `Current DDR Freq = 2096 MHz` and `Max enabled DDR Freq = 4780 MHz`.
Both then reach `xbl_map_smem` and `boot_smem_debug_init`.

At the native diagnostic, the SM8850 shared state contains current and target level five.
Both raw periods equal 477, and the selected ratio equals three.
The printed frequency therefore agrees with the raw period rather than the derived CK period.
The regressions also check raw-period publication, unequal pair values, and writes through the AND view.

The next fault reads power-domain control `0x17b068`.
The APSS PCs are `0x148608d4` and `0x1485ee4c`, respectively.
The logs remain under `build/private/run-sm8850-common-runtime112` and `build/private/run-sm8845-common-runtime98`.
Retention preserves their UART logs and compressed execution traces.
UEFI, ABL, and kernel execution remain unobserved.

## Post-training SRAM retention

SM8850 run114 and SM8845 run100 submit `0x05030000` after training and SMEM initialization.
The previous backend rejected that valid sequence.
The native command updates SRAM retention. It does not restart DDR.

SM8850 helper `0x201e0a` and SM8845 helper `0x20293c` perform these effects:

1. Add bits zero and one to local bytes `0x403538` and `0x403539`.
2. Derive the retention mask from flags at `0x403344..0x403348`.
3. Write that mask to `0x310a92bc`.
4. Retain only mask `0x89c0300f` in `0x310a80bc`.

Both selected images require two 32-KiB retention blocks.
The existing masks remain inputs. The backend does not replace them with constants.
With retention disabled, or tracing requested, the hardware mask equals `0xff`.
Otherwise, the selected code or data mask supplies the retained blocks.

The optional trace helpers are `0x203c78` and `0x204abc`, respectively.
They clear the trace request when either debug flag is active.
Otherwise, they publish local buffer `0x408000` through `gp+0xd4` and enable control `0x31078000`.
The existing ABI displacement also applies to that `gp`-relative pointer.

Startup now publishes the native SRAM bounds at `0x31048000`, `0x31048004`, and `0x31048008`.
Their values are `0x3104a000`, `0x31060000`, and `0x16000`.
The bounds describe the payload interval, not a fabricated protection grant.
Original XBL and TME code remain responsible for permission changes.

### Verification

`make build` succeeds.
The complete suite passes 90 tests and identifies an unrelated LLCC fixture routing error.
The fixed LLCC fixture then passes its focused test.
`cargo clippy --locked --all-targets -- -D warnings` succeeds.

The SHRM regression checks the startup bounds, retained masks, flag-dependent effects, trace publication, and preserved snapshot data.
Its later clock operation and training-state check also pass.
SM8850 run115 and SM8845 run101 complete `boot_populate_ddr_details_shared_table`.
Both reach `Pimem init cmd, entry`.
The SM8850 debugger observes the actual startup bounds and chip ID `0xa0`.

SM8845 subsequently reports `ae000065` from `boot_seclib_s.c`, line 934.
The paused SM8850 run ends with a watchdog bite, not a demonstrated PIMEM service result.
Neither run proves PIMEM completion or kernel handoff.


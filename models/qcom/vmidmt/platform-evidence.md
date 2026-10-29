# Selected XBL VMID mapper instances

## Scope and source identity

`platforms/qcom/sm88xx/xbl-vmid-mappers.lua` supplies platform data for `qcom,vmid-mapper-v1`.
It does not supply a second hardware implementation or modify firmware policy.
The helper contains no private file paths or vendor payload bytes.

The input packages are:

- SM8850: `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`.
- SM8845: `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`.

Each package supplies `IMAGES/xbl_sc.elf` and `IMAGES/xbl_ac_config.mbn`.
The following SHA-256 hashes identify the complete members:

| Platform | Member | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| SM8850 | `xbl_sc.elf` | 1174496 | `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a` |
| SM8850 | `xbl_ac_config.mbn` | 44388 | `afa0b63443a8d6a0588f4e8992a0efc2780b72f2cd1c4052e4357045bc515e39` |
| SM8845 | `xbl_sc.elf` | 1158112 | `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4` |
| SM8845 | `xbl_ac_config.mbn` | 43712 | `013c2d7749c85d692b231ee51168f956788e131e41b181ae715571b041cad83b` |

Field names come from the public Qualcomm TF-A source, not another platform's addresses:

<https://github.com/qualcomm-linux/trusted-firmware-a/tree/f7787c3627e200933f02b93b1c25b8eb5bcf797d/drivers/qti/accesscontrol/vmidmt>

The reference commit is `f7787c3627e200933f02b93b1c25b8eb5bcf797d` on `qcom-next`.
The reference files use BSD-3-Clause.

| Reference file | SHA-256 |
| --- | --- |
| `vmidmt.c` | `adb5475b45234330461fa53eff808280ca262c39cd04bf8854c4098c227bc5e4` |
| `vmidmt_internal.h` | `4c882bed61b431c177ded4035199e39dce6a6b538f0a5eb86ed3c5c3fcf9c67e` |
| `vmidmt_hal.c` | `4ec9cf694bab8356c4e9b42d22ddc5d01a00310c2bd14dae82611ade2d78245b` |
| `vmidmt_hal.h` | `e916333ded2b0dafb6625221a694aeae648f132be4865c670fd13c3992902b9f` |
| `vmidmt_hal_hwio.h` | `a0cb50752fac967c8a88d996e5e1f75e812f4da1daac00e00a508611d957f7b1` |

## Exact selected inventories

Both AC images load file offset `0x1000` at `0x14946000`.
Thus, AC coordinates below use `VA = file offset + 0x14945000`.
Both inventories begin at file offset `0x1f60`, VA `0x14946f60`.
Each packed record occupies 17 bytes.
This is the selected instance table itself, not an aligned search for individual base words.
In particular, the first base starts at unaligned file offset `0x1f6d`.

| Platform | Ordinal | Record file offset | Port | MMIO base | Virtual aperture | Runtime HAL descriptor |
| --- | ---: | --- | --- | --- | --- | --- |
| SM8850 | 0 | `0x1f60` | `0x3e` | `0x008c6000` | `0x008c6000..0x008c6fff` | `0x14a077f0` |
| SM8850 | 1 | `0x1f71` | `0x19` | `0x00ac6000` | `0x00ac6000..0x00ac6fff` | `0x14a075a0` |
| SM8850 | 2 | `0x1f82` | `0x5c` | `0x019c6000` | `0x019c6000..0x019c6fff` | `0x14a079d0` |
| SM8850 | 3 | `0x1f93` | `0x5d` | `0x01ac6000` | `0x01ac6000..0x01ac6fff` | `0x14a079e0` |
| SM8845 | 0 | `0x1f60` | `0x3e` | `0x008c6000` | `0x008c6000..0x008c6fff` | `0x1492a800` |
| SM8845 | 1 | `0x1f71` | `0x19` | `0x00ac6000` | `0x00ac6000..0x00ac6fff` | `0x1492a5b0` |
| SM8845 | 2 | `0x1f82` | `0x5c` | `0x019c6000` | `0x019c6000..0x019c6fff` | `0x1492a9e0` |

The apertures describe the virtual register windows, not measured physical decode limits.
No source evidence assigns these ports reliable device names.
The helper therefore uses numeric names rather than another SoC's enum aliases.

Inventory-region SHA-256:

- SM8850, file `[0x1f60,0x1fa4)`: `1f87df1b4d1cff091d5ead8486eeb9a695f6151acb8558bb47740dd3e2e36fbe`.
- SM8845, file `[0x1f60,0x1f93)`: `00213f3e21b66d5d0383c1d6159f68f18361e13a370b90debf1231f45354c8e3`.

All seven records have the same policy fields except the port and base:

| Packed offset | Width | Field | Value in every selected record |
| --- | ---: | --- | --- |
| `+0` | 1 | `table`, HAL port ID | See inventory |
| `+1` | 1 | `err_opt` | `0x1f` |
| `+2` | 8 | `ssd_table` | Null |
| `+10` | 1 | `ssd_table_num_elements` | 0 |
| `+11` | 1 | `static_cfg`, nonzero selection | 2 |
| `+12` | 1 | XBL secure private-namespace option | 0 |
| `+13` | 4 | XBL instance base | See inventory |

The first five names match `vmidmt_internal.h:31-37` and their XBL consumers.
The two trailing fields are XBL extensions, not the public C structure's native layout.
Do not reinterpret this packed firmware table with the public compiler's structure alignment.

## Count, selection, and import

The AC root table starts at `0x14946d10`.
Its `+0x118` field points to a pointer to the instance inventory.
Its `+0x120` field points to the 32-bit count.

| Platform | Count pointer / file offset | Count | Inventory-pointer location / file offset |
| --- | --- | ---: | --- |
| SM8850 | `0x14946fa4` / `0x1fa4` | 4 | `0x14946fa8` / `0x1fa8` |
| SM8845 | `0x14946f94` / `0x1f94` | 3 | `0x14946f98` / `0x1f98` |

SM8845 loads these fields at `0x1485a210..0x1485a240`.
SM8850 loads them at `0x1485bcdc..0x1485bd0c`.
Their count caches are `0x14929e00` and `0x14a06df0`, respectively.
Their inventory caches are `0x1493bbd0` and `0x14a18d10`.

The boot loops are SM8845 `0x1485a280..0x1485a318` and SM8850 `0x1485bd4c..0x1485bde4`.
Each loop tests record byte `+11` for nonzero, advances by 17 bytes, and stops at the imported count.
Thus, all three or four records participate in this path.
The value 2 is not a reason to skip them.
The helper does not replace either count with zero.

The import routines are SM8845 `0x1485a568..0x1485a5b0` and SM8850 `0x1485c034..0x1485c07c`.
Each routine reconstructs the four base bytes at `+13` and stores the result in the port-indexed descriptor.
Descriptor arrays start at `0x1492a420` and `0x14a07410`, with a 16-byte stride.
The reported SM8845 trace descriptor `0x1492a800` therefore selects port `0x3e`, not ordinal `0x3e`.

## SID and VMID configuration on this path

The selected AC records contain no SID list and no explicit SID-to-VMID mappings.
Their SSD pointers are null, so the selected path does not import an SSD table.
A zero SSD element count here is firmware data, not a zero capability count.
Do not replace it with a fabricated SID policy.

The initialization wrappers are SM8845 `0x1485a078` and SM8850 `0x1485bb44`.
They initialize the secure configuration to zero, select secure extensions `0xff`, and supply the configuration-access mask `{0xffffffff,0}`.
They pass null nonsecure and auxiliary configurations to the HAL.
The resulting initial bypass VMID is 0, with private namespace disabled for every selected port.
The public reference names VMID 0 `ACC_VMID_NOACCESS` in its default configuration.
This name is not a claim that a disconnected virtual data path enforces the policy.

The HAL initialization routines are SM8845 `0x1485a5e4` and SM8850 `0x1485c0b0`.
They initially write all VMID permission bits at base `+0x9c`, the configuration-access bitmap.
They initialize the stream match entries at `+0x800`, when SMS is set.
They initialize every stream configuration word at `+0xc00` to `0x10000` and every auxiliary word at `+0xe00` to zero.
The loops use the reported entry count.
They do not preload a nonzero VMID or a valid SID match.
The wrappers also configure both error-reporting banks with record value `0x1f`.

Later software can program individual SID/VMID contexts.
For example, SM8845 `0x14859f64` accepts port, index, VMID, and secure status.
Its callers at `0x1489de18`, `0x1489de30`, and `0x1489dea4` use a separate, dynamically selected device configuration.
Those calls do not add static mappings to the selected AC records above.
Their execution and resulting mappings remain unproved at the current first capability-read fault.

## Explicit virtual geometry

Every helper row declares all six capability parameters.
No physical reset values for the ID registers occur in the decoded instance records.
No physical geometry follows from the base address alone.
The following values are therefore explicit virtual capabilities for each selected instance:

| Parameter | Value | Rationale |
| --- | ---: | --- |
| `entry_count` | 128 | Nonzero complete initialization, within the separate S2VR and AS2VR windows |
| `stream_id_bits` | 15 | Full SID width supported by this model contract, without narrowing unknown later SID values |
| `ssd_index_bits` | 7 | Largest supported SSD register array, also within XBL's initialization bound |
| `input_address_bits` | 48 | Architecturally encodable IAS value 5, not a measurement of the attached masters |
| `vmid_count` | 32 | Full public VMID permission mask width, including the selected default VMID 0 |
| `stream_match_support` | true | Exposes real match-table initialization rather than bypassing it |

These values represent every mapping in the selected AC inventory: no valid SID matches and initial VMID 0 for each port.
They do not assert support for arbitrary later device policies beyond the declared limits.
In particular, entry counts above 128 would overlap the `+0xc00` and `+0xe00` tables.
SSD widths above 7 exceed this register implementation and the selected XBL initialization bound.

Public `vmidmt_hal.c:45-55` reads the same capability fields as both selected XBL getters:

- IDR0 at `+0x20`: SMS bit 27, NUMSIDB bits 12:9, NUMSMRG bits 7:0.
- IDR1 at `+0x24`: NUMSSDNDXB bits 11:8.
- IDR2 at `+0x28`: IAS bits 3:0.
- IDR5 at `+0x34`: NVMID bits 7:0.

The selected getters are SM8845 `0x1485a830..0x1485a87c` and SM8850 `0x1485c2fc..0x1485c348`.
The pre-model faults occurred at their first IDR0 loads, `0x1485a834` and `0x1485c300`.
The getter uses IDR1 only when the descriptor's SSD-width byte is zero.
The descriptor arrays start without an SSD-width override.
The getter therefore obtains that field from IDR1, independently of the private-namespace policy byte.

## Platform integration

Both runtime helpers invoke `xbl-vmid-mappers.lua` through their existing directory-relative loader.
The helper retains the XBL inventories above and adds each platform's TZ inventory.
See [the TZ evidence](tz-platform-evidence.md) for all records, QSEE consumers, reuse rules, and ownership checks.
The shared factory registers `qcom_vp::VmidMapper` as `qcom,vmid-mapper-v1`.

The helper creates one module per port and one `0x1000` APSS target per module.
It rejects unknown platform names, duplicate ports within a source inventory, duplicate module names, and overlapping APSS targets.
It reuses identical XBL/TZ port/base pairs within the helper's inventory union.
It checks the complete union before inserting any module.
It does not replace existing owners or silently reuse a differently configured module.

## Limits and unproved behavior

These instances are not the UFS SMMU at `0x15000000`.
The helper does not alias them to that device.
It supplies register targets only.
No DMA master, memory interconnect, UFS request, USB request, or other device transaction passes through these mappers through this helper.
Thus, register initialization does not prove SID routing, VMID propagation, memory isolation, or permission enforcement on those data paths.
No kernel execution or post-fault firmware completion follows from this evidence.
No authentication, rollback, fuse, root-hash, or lock policy changes occur here.
No lock semantics are inferred from an unrelated register.

Runs `run-sm8850-common-runtime13` and `run-sm8845-common-runtime12` pass the instance initialization group.
They subsequently reach authorization import `0x4169f4`.
The complete integration suite passes 62 tests.

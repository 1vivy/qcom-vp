# VMID mapper configuration plane

## Scope

`qcom_vp::VmidMapper` implements `qcom,vmid-mapper-v1` through `target_socket`.
The socket accepts device-relative addresses within a 0x1000-byte aperture.
The platform owns registration, address routing, and upstream MMIO protection.
This model does not replace the UFS SMMU at 0x15000000.

The implementation retains programmed VMID state. It has no downstream data socket, grant API, readiness flag, or completion signal.
No physical master is connected to these instances.
A successful register readback proves configuration storage, not an exercised downstream mapping.
The model neither changes existing protection domains nor authorizes memory access.

## Sources

The public reference is Qualcomm TF-A, branch `qcom-next`, commit
`f7787c3627e200933f02b93b1c25b8eb5bcf797d`:

- [vmidmt_hal.c](https://github.com/qualcomm-linux/trusted-firmware-a/blob/f7787c3627e200933f02b93b1c25b8eb5bcf797d/drivers/qti/accesscontrol/vmidmt/vmidmt_hal.c)
- [vmidmt_hal.h](https://github.com/qualcomm-linux/trusted-firmware-a/blob/f7787c3627e200933f02b93b1c25b8eb5bcf797d/drivers/qti/accesscontrol/vmidmt/vmidmt_hal.h)
- [vmidmt_hal_hwio.h](https://github.com/qualcomm-linux/trusted-firmware-a/blob/f7787c3627e200933f02b93b1c25b8eb5bcf797d/drivers/qti/accesscontrol/vmidmt/vmidmt_hal_hwio.h)

The source files carry BSD-3-Clause and the Qualcomm copyright below.
The implementation reuses register field definitions and semantic facts, not a captured register snapshot.

| Public source | Established fact |
| --- | --- |
| `hal.c:41–55`, `hwio.h:252–292,308–322` | Capability offsets, masks, and extraction |
| `hal.c:80–102` | VMID permission bitmap has ordinary masked read/write state |
| `hal.c:153–232` | Secure/nonsecure defaults and masked readbacks |
| `hal.c:237–295,350–382` | Independent match, attribute, and auxiliary context state |
| `hal.c:385–425` | CLIENTPD clearing and software read/AND/write SSD configuration |
| `hal.c:428–538` | Initialization writes and four-word maximum SSD table |
| `hal.c:570–591` | Context configuration can both set and clear SSD bits |
| `hal.c:594–877` | Match entry search, movement, invalidation, and masked stream groups |
| `hal.c:880–917` | Error-control fields retain guest configuration |
| `hal.c:920–1037` | Fault reads and RESTORE=0 clearing with readback |
| `hal.h:212–251` | Supported NSCFG, allocation, shareability, memory type, and transient modes |
| `hwio.h:19–250` | Control bank fields and offsets |
| `hwio.h:336–734` | Fault/status/syndrome fields and bank offsets |
| `hwio.h:736–816` | SSD, clock control, and VMID permission offsets |
| `hwio.h:887–961` | Stream match, context, and auxiliary register layout |

The conventional IAS encoding is 0→32, 1→36, 2→40, 3→42, 4→44, 5→48 bits.
For an independent consumer of this encoding, see Linux
[`arm_smmu_id_size_to_bits`](https://github.com/torvalds/linux/blob/master/drivers/iommu/arm/arm-smmu/arm-smmu.c).
That reference supplies encoding context only. No Linux implementation code is copied.
The VMID mapper does not inherit SMMU page translation, TLB, context-bank, or DMA behavior.

### Selected XBL evidence

Both private packages supply `IMAGES/xbl_sc.elf`:

- SM8850: `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`.
- SM8845: `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`.

Only derived facts and addresses appear here. No vendor code or bytes are included.

| Firmware address | Observation |
| --- | --- |
| SM8850 `0x1485c0b0–0x1485c2f8` | HAL initialization |
| SM8850 `0x1485c2fc–0x1485c348` | Capability getter, first MMIO load at `0x1485c300` |
| SM8845 `0x1485a5e4–0x1485a82c` | HAL initialization |
| SM8845 `0x1485a830–0x1485a87c` | Capability getter, first MMIO load at `0x1485a834` |
| SM8850 `0x1485c0e0`, SM8845 `0x1485a614` | Write all ones to offset 0x9c |
| SM8850 `0x1485c100–0x1485c204` | Control defaults, partition, clocks, fault clear |
| SM8845 `0x1485a634–0x1485a738` | Same initialization register group |
| SM8850 `0x1485c210–0x1485c24c`, SM8845 `0x1485a744–0x1485a780` | SSD width above seven is unsupported |
| SM8850 `0x1485c270–0x1485c2e4`, SM8845 `0x1485a7a4–0x1485a818` | Initialize SMR=0, S2VR=0x10000, AS2VR=0 |
| SM8850 `0x1485c34c–0x1485c648` | Default configuration and masked readbacks |
| SM8850 `0x1485c540–0x1485c564,0x1485c5e0–0x1485c604` | BPVMID writes/readbacks use low eight bits |
| SM8850 `0x1485c64c–0x1485c6d8` | CLIENTPD enable/disable with readback |
| SM8850 `0x1485c6dc–0x1485c750` | SSD bulk configuration computes old AND requested |
| SM8850 `0x1485c754–0x1485caec` | SSD bit update, match allocation, partition movement |
| SM8850 `0x1485caf0–0x1485cb90` | Copy match, context, and auxiliary entries |
| SM8850 `0x1485cb94–0x1485ccf8` | Context configuration preserves TYPE through mask 0x30fcfbff |
| SM8850 `0x1485ccfc–0x1485cde8` | Extended stream mask configuration |

Both getters permit a prefilled descriptor SSD width to suppress the IDR1 read.
This does not establish physical capability values.
The SM8845 execution trace identifies descriptor 0x1492a800, base 0x8c6000, and getter return 0x1485a60c.
Platform instance evidence belongs in `platform-evidence.md`.

Two public HWIO inconsistencies require explicit treatment:

1. CR2 RMSK is 0x0000ff00, but BPVMID is low eight bits. Selected XBL reads those low bits directly.
   The model implements BPVMID in bits 7:0, not the inconsistent RMSK.
2. SACR BPRCISH/BPRCOSH masks disagree with their shifts and the 0x70000013 aggregate mask.
   Selected XBL uses shifts 30, 29, 28, and 4 for auxiliary fields.
   AS2VR definitions also use bits 30, 29, and 28. The model uses aggregate mask 0x70000013 for both banks.

## Parameters

The platform must declare all six parameters before elaboration.
Capabilities are sampled at elaboration and remain fixed for the instance lifetime.
Invalid geometry throws `std::invalid_argument` before guest execution.

| Parameter | Supported values | Capability readback |
| --- | --- | --- |
| `entry_count` | 1..128 | IDR0 bits 7:0 |
| `stream_id_bits` | 1..15 | IDR0 bits 12:9 |
| `ssd_index_bits` | 0..7 | IDR1 bits 11:8 |
| `input_address_bits` | 32, 36, 40, 42, 44, 48 | IDR2 IAS encoding 0..5 |
| `vmid_count` | 1..32 | IDR5 bits 7:0 |
| `stream_match_support` | Explicit boolean | IDR0 bit 27 |

Unsigned defaults deliberately reject omitted geometry.
The CCI boolean has a false default, so the platform declaration must explicitly provide it.
No default asserts physical geometry.

The original 255-entry and 15-bit SSD bounds describe field encoding capacity, not this functional register group.
S2VR begins at 0xc00. AS2VR begins at 0xe00.
More than 128 contexts would overlap those arrays.
More than four SSD words would overlap clock control at 0x90.
The supported bounds prevent these aliases and agree with selected XBL's SSD limit.

## Access contract

Each transaction contains exactly one aligned 32-bit little-endian register.
Read and write commands are supported. Streaming width must be at least four bytes.
The model rejects byte enables, null buffers, other lengths, misalignment, and other commands.
DMI is always disabled. The model supplies no debug transport path.

Malformed shape returns `TLM_BURST_ERROR_RESPONSE`.
Byte enables return `TLM_BYTE_ENABLE_ERROR_RESPONSE`.
Other commands return `TLM_COMMAND_ERROR_RESPONSE`.
Unknown offsets, invalid indices, unsupported values, and forbidden register writes return `TLM_ADDRESS_ERROR_RESPONSE`.
Rejected accesses do not mutate state or the read buffer.

Secure and nonsecure names select distinct register banks by address.
They do not imply a new requester-security authorization mechanism.
Upstream routing and the existing protection model must retain control of MMIO access.

| Offset | Access and state |
| --- | --- |
| 0x000 / 0x400 | SCR0 / NSCR0. Retained controls, including CLIENTPD and error options |
| 0x004 | SCR1. Retained GASRAE bit 24 and NSNUMSMRGO bits 15:8. Partition cannot exceed `entry_count` |
| 0x008 / 0x408 | SCR2 / NSCR2. Retained BPVMID. Value must be below `vmid_count` |
| 0x010 / 0x410 | SACR / NSACR. Retained bits selected by 0x70000013 |
| 0x020,0x024,0x028,0x034 | Read-only IDR0, IDR1, IDR2, IDR5. Unadvertised capability bits are zero |
| 0x040,0x044 / 0x440,0x444 | Read-only fault address. Zero because no downstream source exists |
| 0x048 / 0x448 | Read-only fault status, mask 0xc00000a6. No downstream producer exists |
| 0x04c / 0x44c | RESTORE. Zero write clears status. Read returns status. Nonzero writes fail |
| 0x050,0x054,0x058 / 0x450,0x454,0x458 | Read-only syndrome. Zero because no downstream source exists |
| 0x080+4n | SSD word n. `n < max(1,2^ssd_index_bits/32)`. Ordinary retained 32-bit state |
| 0x090 / 0x490 | Retained CLKONOFFE bit 0. Other write bits fail |
| 0x09c | Retained VMID permission bits, masked to `vmid_count` |
| 0x800+4n | Match entry n. Requires SMS and `n < entry_count`. Valid bit 31, mask bits 30:16, ID bits 14:0 |
| 0xc00+4n | Context n. Requires `n < entry_count`. Attribute mask 0x30fcfbff plus retained TYPE bits 17:16 |
| 0xe00+4n | Auxiliary context n. Requires `n < entry_count`. Mask 0x70000013 |

SCR0 accepts mask 0x3fff0df5. NSCR0 accepts mask 0x0fff0df5.
These masks include default bit 8 because selected XBL writes 0x111.
Unknown control bits fail instead of becoming a register bucket.
NSCFG, allocation, and transient encoding 1 fail because the HAL does not define those modes.
MemAttr above seven fails. SMCFCFG fails when stream matching is unavailable.
Context VMIDs must be below `vmid_count`. TYPE values zero and one are retained. Other TYPE values fail.
The model does not infer downstream TYPE behavior from an unrelated SMMU.

Match writes mask the ID and mask fields to `stream_id_bits` and clear reserved bit 15.
Auxiliary, context, and permission writes clear bits outside their implemented masks.
This produces masked readbacks, not unconditional equality with the input word.
Masked HAL writes remain real read/modify/write operations over retained state.
Each context and each control bank has independent storage.

## Cold state, faults, and locks

Cold state is explicit virtual policy, not measured silicon reset state:

- Both CLIENTPD bits are one. Other control bits are zero.
- All match entries are invalid. All context and auxiliary words are zero.
- The partition, permission bitmap, SSD words, bypass VMIDs, and clocks are zero.
- Fault status and syndrome are zero. No connected master can produce a data-plane fault.

Initialization does not occur automatically when the model is constructed.
XBL must write its defaults, clocks, SSD table, contexts, and permissions itself.
The guest can disable or enable CLIENTPD repeatedly without losing configuration.
Clock controls retain guest writes. They do not indicate downstream readiness.

The HAL bulk SSD function is clear-only because it computes old AND requested in software.
SSD hardware storage is not modeled as write-once or sticky.
The same HAL's context function explicitly sets nonsecure bits after it clears secure bits.

The established fault operation is RESTORE=0 followed by a zero readback.
No nonzero fault injection, status write, status W1C rule, or fault producer is established for this configuration-only model.
Those unsupported writes fail. Invalid MMIO does not fabricate a data-plane syndrome.

No selected HAL producer or public register definition establishes a lock register or one-way lock transition.
GASRAE is retained configuration, not an invented lock.
The model does not add lock aliases, authentication success, rollback changes, fuse changes, or protection grants.
A future downstream master requires a separately evidenced mapping and fault contract before connection.

## Native execution

The shared factory registers this model for both OEM runtime profiles.
The complete integration suite passes 59 tests.
The new native regression checks the following boundaries:

1. A secure control change preserves nonsecure controls and each bank's bypass identity.
2. A masked context update preserves TYPE.
3. Match and auxiliary writes preserve the independent context attributes.
4. Match readback masks both SID fields to the declared width.
5. Client control changes preserve programmed context state.
6. The first unsupported context and the first invalid VMID fail at their actual MMIO addresses.

Both canonical OEM runs pass the previous VMID capability fault.
The decoded inventories require four instances on SM8850 and three on SM8845.
Runs `run-sm8850-common-runtime13` and `run-sm8845-common-runtime12` subsequently reach authorization import `0x4169f4`.
These results establish execution of the configuration group, not downstream VMID mapping or a kernel handoff.

## Reused definitions license

Copyright (c) 2026, Qualcomm Technologies, Inc. and/or its subsidiaries.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Native DCP SPMI contract

## Sources and scope

Both sources are local `RADIO/dcp.img` members from the selected OEM packages.
The ELF starts at image offset `0x16000` in each image.
No firmware bytes belong in the repository.

| SoC | Package | Image SHA-256 |
| --- | --- | --- |
| SM8850 | `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip` | `5863d9b9826426b507b194f7461c781ab7fe9a41d10063d410db20eee5cdd551` |
| SM8845 | `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip` | `2556992b799d102bad74235025e675c0620e957719fe18f92f14fce5789eb766` |

`llvm-objdump -d --mattr=+m,+c` supplies the instruction evidence below.
The core driver starts at `0xa00ef9ba` on SM8850 and `0xa411bba8` on SM8845.
Its 555 decoded instructions through `0xa00eff80` match after branch-address normalization.
The IRQ handler at `0xa00f0414..0xa00f04ec` also matches its SM8845 counterpart after normalization.
The PC displacement in these two blocks is `0x0402c1ee`.
These are static instruction results, not claims that every path executed in an OEM run.

The public [Linux SPMI driver](https://github.com/torvalds/linux/blob/master/drivers/spmi/spmi-pmic-arb.c) corroborates the v8 channel layouts.
The OEM instructions determine the packed offsets and capability fields used here.

## Register accesses

Let `B` mean system base `0x0c400000`, which the DCP normally sees at `0x4c400000`.
Let `b` mean bus zero or one, `e` mean EE 11, and `n` mean a bus-local APID.
Let `g` mean the global APID: `n` on bus zero and `2048+n` on bus one.
The driver obtains that start by adding preceding channel counts.
It does not obtain the start from capability bits 31:23.

| Register relative to B | Operation | SM8850 PC | SM8845 PC |
| --- | --- | --- | --- |
| `4+4*b` | Read capability word | `a00ef9e4` | `a411bbd2` |
| `3000+4*g` | Read PPID while scanning all channels | `a00efa6e` | `a411bc5c` |
| `2a000+11000*b+4*n` | Read write owner, bits 3:0 | `a00efc38` | `a411be26` |
| `3000+4*g` | Read IRQ-owner flag, bit 31 | `a00efc3a` | `a411be28` |
| `28000+11000*b` | Read bus error snapshot | `a00efc6e` | `a411be5c` |
| `2502c+11000*b` | Read bus snapshot | `a00efc82` | `a411be70` |
| `25028+11000*b` | Read bus snapshot | `a00efc90` | `a411be7e` |
| Channel `+8` | Read DONE before command | `a00efdd0` | `a411bfbe` |
| Write channel `+10` | Write WDATA0 | `a00efe1c` | `a411c00a` |
| Write channel `+14` | Write WDATA1 for transfers longer than four bytes | `a00efe4c` | `a411c03a` |
| Channel `+0` | Write command | `a00efe74` | `a411c062` |
| Channel `+8` | Read command status until nonzero | `a00efe90` | `a411c07e` |
| Observer channel `+18` | Read RDATA0 | `a00efec2` | `a411c0b0` |
| Observer channel `+1c` | Read RDATA1 for transfers longer than four bytes | `a00eff04` | `a411c0f2` |
| `25028+11000*b` | Read timeout diagnostic, bit zero | `a00eff3e` | `a411c12c` |
| `4c0000+10000*b+1000*e+4*k` | Read PIC group `k` | `a00f046c` | `a411c65a` |
| `500100+200*g` | Read IRQ access enable | `a00f049a` | `a411c688` |
| `500104+200*g` | Read peripheral IRQ status | `a00f04a4` | `a411c692` |
| `500108+200*g` | Read IRQ-clear word before replacement of bits 7:0 | `a00f04a8` | `a411c696` |
| `500108+200*g` | Write IRQ-clear bits 7:0 | `a00f04b4` | `a411c6a2` |
| `500104+200*g` | Read until the cleared IRQ bits disappear | `a00f04be` | `a411c6ac` |

All hexadecimal register offsets omit the `0x` prefix in this table.
The channel base is `500000+200*g` for writes and `c0000+40000*e+20*g` for observer reads.
The core driver does not read the version register or access channel CONFIG.
The IRQ handler reads access enable but does not write it.
Its caller obtains the EE from the bus descriptor at `a00f0798` / `a411c986`.

Initialization splits each capability word into channel count, bits 12:0, and PIC group count, bits 31:23.
The IRQ handler uses the second field as its loop bound at `a00f0456..a00f045a` / `a411c644..a411c648`.
The coherent model values are therefore `0x20000800` and `0x08000200`.
These describe 2048/512 channels and 64/16 groups of 32 channels.
Register zero reports version `0x80000000`. All three words are read-only.

PPID bits 12:8 select the SID, and bits 7:0 select the peripheral.
A zero peripheral marks an unused route.
The driver builds its own linked lookup table and preserves duplicate PPID routes with different owners.
It reads the same route and owner words that XBL programs.

The command contains opcode at bits 31:27, priority at bit 26, offset at bits 11:4, and byte-count-minus-one at bits 2:0.
The driver uses opcode zero for writes and one for reads, with at most eight bytes per command.
Its status path tests DROPPED, DONE, FAILURE, and DENIED at bits three, zero, one, and two.
The existing PMIC backend remains responsible for every data byte.

## Model behavior and integration

`QcomSpmiArbiter.dcp_socket` supplies a distinct EE11 endpoint on the existing arbiter instance.
It is an optional simple target socket, with relative addresses in the existing `0x900000` aperture.
It shares PPIDs, owners, bus gates, write channels, IRQ controls, and the PMIC backend with the APSS endpoint.
Its observer channels have separate command, status, and read-data state.
It rejects accesses to another EE's observer window.
It cannot write the configuration tables or use APSS secure-write grants.
Its write commands require owner 11, even when an upstream test CPU supplies secure attributes.

Keep this endpoint after the DCP remapper, rather than bypassing the programmed window entries.
The following Lua belongs in the DCP composition before bootstrap creation.
The separate SPMI router permits two narrow views to share one optional target binding.
The XPU hole still reaches the existing protected system owner.

```lua
p.dcp_system_router={moduletype='router'}
p.dcp_spmi_router={moduletype='router'}
p.xbl_spmi.dcp_socket={address=0,size=0x900000,
    bind='&dcp_spmi_router.initiator_socket'}
for index,range in ipairs({
    {0,0x0c400000},
    {0x0c41c000,0x2000},
    {0x0cd00000,0xf3300000},
}) do
    p['dcp_system_view_'..index]={moduletype='addrtr',mapped_base_addr=range[1],
        target_socket={address=range[1],size=range[2],relative_addresses=false,
            bind='&dcp_system_router.initiator_socket'},
        initiator_socket={bind='&apss_router.target_socket'}}
end
for index,range in ipairs({{0,0x1c000},{0x1e000,0x8e2000}}) do
    p['dcp_spmi_view_'..index]={moduletype='addrtr',mapped_base_addr=range[1],
        target_socket={address=0x0c400000+range[1],size=range[2],relative_addresses=false,
            bind='&dcp_system_router.initiator_socket'},
        initiator_socket={bind='&dcp_spmi_router.target_socket'}}
end
```

In `platforms/qcom/sm88xx/dcp-bootstrap.lua`, replace the native `soc_socket` binding with this binding:

```lua
p.dcp_bootstrap.soc_socket={bind='&dcp_system_router.target_socket'}
```

The remapper currently produces only 32-bit system addresses.
The final pass-through range therefore ends at `0x100000000`.
No APSS SPMI binding or XBL protection mapping needs a change.

## Verification and limits

The `spmi_dcp_shares_routes_and_pmic_without_apss_authority` fixture uses synthetic guest instructions.
It checks shared discovery, both capability counts, bidirectional PMIC visibility, independent observers, bus gates, and owner denial.
It also checks absent bus-one SIDs, deasserted IRQ state, read-only tables, cross-EE rejection, and unsupported PMIC bytes.
It does not use OEM firmware bytes.

`make build` passes with the existing OpenSSL SHA-384 deprecation warnings.
These focused platform tests pass:

- `spmi_dcp_shares_routes_and_pmic_without_apss_authority` (seven cases).
- `pmic_configuration_preserves_aliases_keys_and_uploaded_bytes` (thirteen cases).
- `ibus_retains_guest_configuration_and_rejects_unsupported_accesses`.
- `pbs_memory_preserves_data_and_rejects_invalid_lifecycles`.
- `pmic_discovery_preserves_seven_byte_revisions_and_unknown_accesses` (four cases).
- `pmic_virtual_absence_is_opt_in_bounded_and_logged_once` (ten cases).

No PMIC IRQ producer connects to this model.
PIC status and peripheral IRQ status therefore remain deasserted, and W1C does not create an event.
Do not bind a fabricated IRQ output to PLIC sources five or six.
A future real PMIC event producer must supply those two bus outputs before native IRQ delivery can be claimed.
The discovery policy adds no control, measurement, readiness, or IRQ result.
No OEM boot run forms part of this slice's verification.

## PMIC discovery after arbiter initialization

SM8850 runtime206 and SM8845 runtime164 reach a seven-byte read of bus zero, SID zero, address `0x0400`.
The backend rejects byte six because the declared absent record previously contained only six bytes.
This is a discovery-width mismatch, not evidence for a missing measurement or regulator-ready bit.
The board inventory explicitly declares peripheral four absent.
Neither image assigns it a GPIO, regulator, or other functional type before reading its revision.
Thus the probe does not identify a physical peripheral-four function.

The seven-byte record holds digital minor, digital major, analog minor, analog major, type, subtype, and dummy.
Equal subtype and dummy bytes identify an absent peripheral.
A zero type also identifies absence.
The DCP decoder contains both checks.

| PMIC operation or decision | SM8850 PC | SM8845 PC | Purpose |
| --- | --- | --- | --- |
| Read `0x0102..0x0106`, five bytes | `a00f0856` | `a411ca44` | Identify model, version, type, and SID span |
| Check identity type `0x51` | `a00f085c..a00f0864` | `a411ca4a..a411ca52` | Reject a non-PMIC identity |
| Read peripheral base `+0..+6`, seven bytes | `a00f0a8a` | `a411cc78` | Discover each selected peripheral |
| Compare subtype with dummy | `a00f0a98` | `a411cc86` | Detect repeated-data absence |
| Read type and reject zero | `a00f0a9c..a00f0aa0` | `a411cc8a..a411cc8e` | Detect explicit absent revision |
| Select model scan bitmap | `a00f0b52` | `a411cd40` | Select which peripheral bases to probe |
| Invoke revision discovery | `a00f0bac` | `a411cd9a` | Read each selected base |
| Bind GPIO driver for type `0x10` | `a00f0bc6` | `a411cdb4` | Allocate driver state from the revision |
| Bind RTC driver for type `7` | `a00f0bd0` | `a411cdbe` | Allocate driver state from the revision |
| Invoke the SPMI read adapter | `a00f0c72` | `a411ce60` | Submit the identity or revision burst |

The linked byte-array read adapter has only the two PMIC callers listed above.
The SPMI adapter at `a00effe4` / `a411c1d2` supplies operation one, which selects observer reads.
GPIO initialization at `a00f0e8c` / `a411d07a` stores driver pointers, base addresses, revision, and count.
RTC initialization at `a00f1134` / `a411d322` does the same and checks subtype one.
Neither initializer submits a PMIC control transaction.
The generic SPMI write implementation remains present, but this PMIC initialization adapter does not call it.
No additional PMIC configuration, voltage measurement, or panel-ready byte is justified by these initializers.

Both trees expose a TLMM controller under `gpio-controllers`, not a PMIC GPIO controller.
Their panel supply nodes use PRM resources, rather than direct SPMI regulator commands.
SM8850 declares `L11B_E0`, `L12B_E0`, `L13B_E0`, `L1D_E0`, and `L3D_E0`.
SM8845 declares `L10B_E0`, `L11B_E0`, `L13B_E0`, `L2F_E0`, and `L2I_E0`.
The linked resource layer identifies `pm_prm_populate_vrm_data`, command-database addresses, and `pm_rpmh_create_cmd_set`.
Those resource requests belong to the shared RPMh model, not fabricated PMIC register storage.

The model bitmap selector is `a00f1574` / `a411d762`.
Its pointer table is `a012c204` / `a415c0a0`.
Both images contain identical decoded scan masks for the inspected PMIC models.
The `0x5c` primary mask selects 186 peripheral numbers, including one and four.
The `0x5d` primary mask selects 87, and the `0x49` mask selects 73.
Model `0x61` selects the default first-bank mask, which probes peripherals one through 255.
Thus the DCP can probe more revision records than the existing XBL-specific masks declare.
This difference does not prove that those physical peripherals exist.

Evaluating the current PMIC Lua inventory selects 1971 scan slots for SM8850 and 636 for SM8845.
SM8850 selects 928 undeclared records. SM8845 selects none.
No selected declared peripheral lacks a revision record after this change.
These are static scan slots, not observed bus transactions.
The SPMI driver still requires a matching PPID route before it can submit each read.

### Bounded virtual absence policy

`virtual_absent_discovery` defaults to false.
Both selected OEM PMIC definitions enable it explicitly.
For a populated SID, an undeclared peripheral without exploratory writes can report absence during an exact seven-byte read at offset zero.
It returns seven zero bytes, which the public decoder identifies as absence.
Every other access follows the pre-existing model behavior.
Exploratory configuration retains its existing reads, writes, and substitute logs. Global lock handling is unchanged.
An accepted exploratory write, including zero, prevents a later absence response for that peripheral.
Its unknown revision bytes then keep their original error response.
Cold and warm reset clear this write history together with the exploratory byte storage.
Reading unwritten exploratory storage does not create write history.
An absent SID still returns its existing transport NACK.

Any declared byte, explicit absence record, or PBS service marks the peripheral declared.
Declared peripherals do not use this policy, even when their supported register set is incomplete.
Each new virtual absence produces one line per model instance:

```text
UNMODELED_PMIC_ABSENT bus=0 sid=0 peripheral=0x2 policy=virtual-absent
```

The bus, SID, and peripheral identify the substituted record.
The policy describes the virtual inventory, not a captured physical topology.
All declared revision services now include their read-only dummy byte.
Primary PMIC identity byte `0x0106` remains the declared SID span, not a dummy.
PBS byte `+7` remains its existing client count.

`pmic_discovery_preserves_seven_byte_revisions_and_unknown_accesses` checks all seven declared peripheral classes and explicit absence.
It also checks adjacent unknown bytes, unknown peripherals with the policy disabled, and read-only dummy bytes.
`pmic_virtual_absence_is_opt_in_bounded_and_logged_once` checks the explicit policy and its log.
It checks repeated discovery, retained configuration, zero writes, global locks, partial revision reads, absent SIDs, and incomplete declared services.


## GPIO revision provenance and consumer bounds

Commit `8b55c21` introduced digital revision 3.2 as virtual policy.
Its comment cites the SM8845 VIN decoder at `0x1487c004..0x1487c05c`.
That decoder reads subtype `+5` and accepts three VIN inputs for subtype `0x13`.
It does not read digital revision bytes `+0` and `+1`.
Thus that evidence establishes the subtype capability, not a requirement for revision 3.2.

The public [Linux GPIO driver](https://github.com/torvalds/linux/blob/master/drivers/pinctrl/qcom/pinctrl-spmi-gpio.c) identifies type `0x10` and subtype `0x13` as `GPIO_MV_VIN3`.
Its population path reads type and subtype, then assigns three power sources.
It does not check the digital revision.
No captured physical revision supplied the previous virtual value.

| Consumer | SM8850 PC | SM8845 PC | Decoded revision bound |
| --- | --- | --- | --- |
| DCP GPIO initializer | `a00f0f32..a00f0f4a` | `a411d120..a411d138` | Pack `(major<<4)|minor`. Reject values `>=0x32`. |
| XBL GPIO initializer | `1487df40..1487df48` | `1487c588..1487c590` | Pack the same fields. Accept values `<0x33`. |
| UEFI `PmicDxe` GPIO initializer | RVA `10bb8..10bc0` | RVA `11458..11460` | Pack the same fields. Accept values `<0x33`. |

The selected XBL hashes are `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a` and `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
Direct disassembly of both selected XBL images establishes the inclusive `0x32` bound.
The selected DCP bound remains stricter.

The selected UEFI containers decompress to DXE volumes of 4,685,832 and 4,472,840 bytes.
Their SHA-256 values are `0329b1be2fc320149437cc29d2de83a886fe40d0d4a348371cb56175fc92a4d6` and `ae119f8b8b083df44e131a7591b6a7bee7c04a88905c78214157095cba38e65e`.
The `PmicDxe` file GUID is `5776232e-082d-4b75-9a0e-fe1d13f7a5d9`.
The extracted PE hashes are `1446974a735fa8165ca8aad59c1c8c0ac0fcfd5675306578c9da8028d6c47cfa` and `78fd2030ea9a7151b654909bd8e0554c664c94dbb7574c36113daef21070e89c`.
Direct disassembly and decompilation establish the UEFI bounds in the table.
Neither selected UEFI initializer has a lower revision bound.

Both exact QSEE images contain DAL SPMI transport and PMIC resource strings.
Their named GPIO APIs and source strings refer to TLMM, including the secure eSE GPIO path.
No `pm_gpio`, `PM_GPIO`, `pmic_gpio`, or `gpio.c` string occurs in either image.
An executable-segment scan finds eight comparisons with `0x32` and seventeen with `0x33` in each image.
The scan permits every source register and both W and X register widths.
Targeted disassembly of SM8850 candidates and representative SM8845 sites finds switches, message fields, callback registration, and lengths.
It does not identify a PMIC GPIO revision gate or a requirement for 3.2.
For example, `156b05c8` / `156abf90` selects callback opcode `0x33`, not a GPIO revision.
Sites `1574dbcc..1574dbd4` / `15740830..15740838` switch on a function argument.
This is bounded negative evidence, not a complete QSEE function-reference proof.
Ghidra auto-analysis did not complete within its tool deadline.
An unrecognized internal routine with a different encoding remains outside that proof.
The QSEE hashes are `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932` and `b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09`.

The virtual GPIO revision is now 3.1, the highest minor revision below the selected DCP limit.
Type `0x10`, subtype `0x13`, analog revision, controls, and input behavior do not change.
This choice satisfies every identified binary bound and preserves the three-VIN capability.
It corrects a virtual identity choice. It does not patch firmware or claim a measured physical revision.


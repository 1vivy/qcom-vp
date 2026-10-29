# Packed SPMI arbiter and IBUS

`qcom,spmi-arbiter-packed-v1` owns the packed arbiter aperture.
The platform supplies its address, routes, executing owner, and completion policy.
The selected platform maps this aperture at `0x0c400000`, with size `0x900000`.
IBUS uses this existing owner. It does not require another register mapping.

## Secure FIFO ownership

`secure_write_owner_mask` declares foreign channel owners available to a secure requester.
Its default is zero. Unspecified or nonsecure transaction attributes do not grant this capability.
The executing owner retains its normal access. Other undeclared owners remain denied.
SM8850 declares foreign owners one through twelve. SM8845 declares owners one through eleven.
This is an explicit virtual access policy, not a recovered silicon permission table.

The selected SM8845 uploader alternates two command lanes at `0x1486eb24` and `0x1486ecec`.
It configures the reserved lane through `0x1486ee2c..0x1486ee58`.
Its search at `0x1486edfc..0x1486ee04` selects owner-mask bit four.
Owner-mask bit four selects AOP.
SM8850 diagnostic run39 confirms primary APID `0x5c`, owner zero, and reserved APID `0x7ff`, owner four.
The former unconditional owner check dropped the reserved upload. Original firmware verification detected the missing bytes.

The selected owner maps are SM8850 `0x148f3abe` and SM8845 `0x148f14c6`.
Both declare thirteen slots. SM8845's last slot has no owner mask.
The secure-loader capability follows the nonzero entries, not an assumed silicon permission table.
SM8850 run135 records transaction denial six for lock writes through other configured owners.
Run134 separately records zero from target preparation, channel configuration, hardware ports, and access-control setup.
Only the lock helper returns `0x80`.

The native PMIC fixture now verifies distinct payloads across alternating APIDs.
It also verifies that nonsecure and undeclared-owner writes leave RAM and the transfer pointer unchanged.

## Secure observer window

The package DT places the observer windows at `0x0c4c0000`, with one `0x40000` window for each EE.
QSEE reads its PMIC status through the EE1 window.
SM8850 QSEE `0x15738dc0` reads `0x0c500208`, which is APID 16, register `+8`.
SM8845 QSEE `0x1572ba14` reads `0x0c500508`, which is APID 40, register `+8`.

`secure_observer_ee` declares one additional observer window for secure APSS requests.
Its default selects no window. Both OEM profiles select EE1.
That window has its own command, status, and read-data state.
Nonsecure and unspecified requests to that window fail with a command error.
The APSS executing owner keeps its own observer window.
This is an explicit virtual access policy. QSEE and XBL use the same APSS security state.
A command in an observer window must be a read. Run535 and run538 wrote the EE1 command register at `0x0c510be0` after a rejected dump read.
Run539 shows no such write once the scan answers the PBS group.
The rejection therefore belongs to the QSEE error path and not to a write contract [INFERENCE].

## QSEE PMIC identity burst

SM8850 QSEE builds the observer command at `0x15738d40..0x15738db4`.
It selects the observer window at `0x0b000000 + 0x14c0000 + ee * 0x40000` and the write channel at `+0x1900000`.
The command forms `(opcode << 27) | (priority << 26) | (offset << 4) | (count - 1)`.
The value `[x29-0x24]` supplies the opcode and priority, and the store at `0x15738ea0` writes it.
The observer channel offset is `apid << 5`.
Run474 and run486 record the EE1 window command write at `0x0c500200`.
The same run records the status read at `0x0c500208`.

The command reads eight bytes at bus zero, SID zero, peripheral one, and offset zero.
The PMIC inventory now reports that burst.
The XBL probe at `0x148344a8..0x148345c4` reads seven bytes at `0x0100` and one byte at `0x0160`.
The seven bytes are digital minor, digital major, analog minor, analog major, type, subtype, and dummy.
No source gives a value for byte `0x0107`.
The identity inventory declares that byte read-only and retains the virtual cold zero.
Run486 rejected the burst at byte `0x0107` before this declaration.
The rejection set the channel to FAILURE, and the arbiter then failed the originating MMIO write.
[The stub ledger](../../../docs/qcom/sm88xx/stubs.md) records the substitute.

The live-phone regmap read of bus zero, SID zero supplies the block values (record `20261026T150037Z`).
Bytes `0x100..0x106` read `20 00 00 02 51 5c 01`.
The virtual identity matches `0x101..0x106` and reads zero at `0x100`.
Bytes `0x107..0x14f` read zero, so the inventory declares `0x107..0x10f` read-only zero.
Run489 and run490 read the second eight bytes at `0x108`.
The live DT names this peripheral `pmk8850@0` `pmic-ecid@100`.

Run494 stops at the eight-byte burst that starts at `0x0150` on bus zero, SID zero.
The same record supplies the tail `0x0152..0x01ef`.
Those bytes read zero, except byte `0x0161`, which reads `0x61`.
The inventory declares these bytes read-only on bus zero, SID zero only.
Byte `0x0160` keeps the identity variant owner, and no tail burst supplies it.
Byte `0x0154` keeps its declared OTP identifier row from `unassigned_manufacturing`.
Run496, run497, and run498 read the eight-byte bursts at `0x0158`, `0x0168`, and `0x0170`.
The declared zero block ends at `0x01ef`, because `0x01f0..0x01ff` keeps its manufacturing owner.
The record marks `0x01f8..0x01ff` as possibly device-unique, so this block excludes them.

The observer and write channel aperture offsets follow the public Linux driver.
`pmic_arb_offset_v8` uses `0x40000 * ee + 0x20 * apid` for observers and `0x200 * apid` for write channels.
`pmic_arb_fmt_cmd_v2` builds the same command fields, and `PMIC_ARB_CMD_MAX_BYTE_COUNT` is eight.
The DT node `arbiter@c400000` on the live phone names the regions `core`, `chnls`, `obsrvr`, and `chnl_map`.

## QSEE devcfg dump observation

Both OEM devcfg images enable the PMIC dump framework.
`PM_NEW_DUMP_FRAMEWORK` is an inline type-2 word, and both images hold the value one.
The QSEE accessor `0x15726a48` delivers that word from devcfg file offset `0x946c` for SM8850 and `0x93cc` for SM8845.
The framework at `0x15735970` dumps only when the word equals one and the bitmap pointer is not null.

`PM_PERIPH_LIST_BITMAP` uses property type `0x12`.
That type does not hold bytes in the image. It returns a `{size, pointer}` pair from the runtime blob registry at `ctx+8`.
SM8850 stores registry index `0x19` at file offset `0x9474`, and SM8845 stores index `0x14` at `0x93d4`.
Neither devcfg image holds a `0x380`-byte bitmap array.
The dump selection becomes visible only through the dump reads.

The platform selects the groups in CCI `dump_peripherals`, which has two forms.
The default is empty, and each row of the explicit form is a `bus sid peripheral` triple.
The value `present` selects every group that this model treats as present.
A group takes part only when the model reads a nonzero presence byte at peripheral offset four.
That byte comes from a declared revision record or from the PBS interface.
A group with a zero presence byte reports absence, so the dump skips it.

The `present` profile rests on three facts.
QSEE dumps only a peripheral that the bitmap selects and the offset-4 probe reports present.
The bitmap is a runtime blob, so no image supplies its bytes.
The dump stores its bytes and never reads them back.
A present-set superset therefore changes no read that QSEE issues.
The OEM platforms select `present`, because the exact bitmap stays unknown.

The arbiter marks each command from the secure observer window with `PmicObserverExtension`.
The QSEE dump reads through that window, because QSEE uses its own EE.
A driver read through the executing owner window carries no marker.
The PMIC model accepts that marker only as an additional data observation.
For a marked read in a selected group, a byte without an owner returns virtual cold zero.
That read also prints one `PMIC_DUMP_READ_UNDECLARED bus sid address` line per byte.
Declared bytes keep their owner, their value, and their access policy.
Writes, non-observer reads, and undeclared reads outside a selected group keep the existing rejection.
The marker grants no control path and no write permission.

The dump code stores bytes and does not act on them.
`0x15735cf4` zeroes a `0x100`-byte stack buffer, fills it only on a successful read, and copies it into a slot record.
The slot base `DAT_158065a8` has two references, and both sit inside the dump code.
Only the SPMI status and the presence byte at peripheral offset four select a branch.
The separate SYSD word producer `0x15736274` does branch on its data, and it is not this path.

The `pmic-dump` fixture checks the explicit-list contract.
The `pmic-dump-present` fixture checks the present profile, including a group that stays out.
It reads unowned bytes in a selected group, and it checks one log line per byte.
It rejects a write to an unowned byte, a read in an unselected group, and a read in a selected group with no type byte.
It also shows that the dump rule replaces exploratory substitution for a selected group.

## QSEE unmarked block scan

QSEE reads each present peripheral block again on an unmarked path.
Run530 reads `0x4008..0x40f8`, then `0x4108..0x41f8`, in eight-byte bursts.
The marker test fails for these reads, so they are not devcfg dump reads.
Run533 continues through the SID 1 inventory to `0x8836..0x883f`.
The reader then reads the rest of each block, including `0xd0..0xff`.
Run539 reads the PBS group at bus one, SID seven, `0x1608..0x16f8`.

CCI `scan_block_reads` selects the substitute.
When it is true, an eight-byte read at offset `0x08` or later of a declared peripheral answers each byte.
A declared byte keeps its owner, its value, and its access policy.
A PBS interface register returns its virtual value.
Any other byte reads cold zero and prints one `PMIC_SCAN_READ_UNDECLARED bus sid address policy=declared-zero` line.
The burst never moves the PBS transfer pointer and never changes RAM.
A read of a RAM port that the interface can serve stays a real transfer.
Therefore the XBL PBS upload and its readback keep their real bytes.
The lock bytes `0xd1..0xd3` keep their separate service.
Other widths, writes, and reads of an undeclared peripheral keep the existing rejection.

The reader stores the bytes.
No trace yet shows a branch on them, so this substitute stays explicit and unresolved [INFERENCE].

## IBUS version and producer policy

`ibus_policy` accepts two values:

- `disabled` is the default. The IBUS bank remains inaccessible.
- `disconnected` exposes the evidenced IBUS v1 configuration bank. No external PMIC alert producer connects to this bank.

The version register returns `0x100` under the disconnected policy.
This value identifies the implemented major version. It is not a ready or completion flag.
Cold reset clears all writable IBUS state. This is an explicit virtual reset policy, not a claimed silicon reset image.
The configured policy and version survive cold reset.

IBUS is an interrupt bus, not a firmware instruction engine or an SPMI transaction queue.
The model retains guest configuration without interpreting the selected firmware's table contents.
It never installs a vendor table, issues an SPMI operation from a table word, or fabricates an alert.

## Evidenced register group

Offsets below are relative to the packed arbiter aperture.
All accesses require one aligned 32-bit word without byte enables.

| Offset | Mode | Implemented state |
| --- | --- | --- |
| `0x69000` | RW | Retained enable bits `1:0` |
| `0x69004` | RO | Major version 1, encoded as `0x100` |
| `0x6900c` | RW | Retained 32-bit configuration word |
| `0x69010` | RW | Retained configuration bits `5:0` |
| `0x69100..0x6913c` | RW | Sixteen guest-supplied 32-bit table words |
| `0x69200..0x6923c` | RW | Sixteen retained controls, bits `5:0` |

The masks describe the supported virtual contract. They do not claim undocumented silicon fields.
Writes outside these masks fail without changing the retained word.
Writes to the version register fail.
Unknown addresses, including `0x69008`, remain inaccessible. The model does not infer status, trigger, acknowledge, or ready registers.

## Selected OEM evidence

The selected SM8850 XBL initializer is `0x1488d3c8..0x1488d4a4`.
Its original caller at `0x1483aed0` logs `Bootup: IBUS not supported` when initialization returns nonzero.
The diagnostic string starts at `0x148d6be2`.
The function uses physical base `0x0c469000`:

1. Instructions `0x1488d3e0..0x1488d3ec` require `(revision & 0xf00) == 0x100`.
2. Instructions `0x1488d424..0x1488d42c` preserve `0x0c260208` and set bit 29.
3. Instructions `0x1488d434..0x1488d444` set the low six bits at base `+0x10` to `0xd`.
4. Instructions `0x1488d448..0x1488d46c` set each control's low six bits to `2` and copy sixteen guest table words.
5. Function `0x1488d4a8..0x1488d52c` configures all eight nibbles of base `+0xc` through read-modify-write operations.
6. Instructions `0x1488d474..0x1488d48c` set enable bit 0, then bit 1.

The original firmware supplies its table from `0x148e08b0..0x148e08ef` through guest loads and stores.
The repository stores no bytes from that table.
The SM8850 profile maps `0x0c260208` through `qcom,retained-boot-control-v1`, with writable mask `0x20000000` and virtual cold value zero.
This separate AOSS word retains the request without generating IBUS activity.

The immediate successor at `0x1483aee4` calls `0x1487cbf0`, then continues normal PMIC initialization.
An executable scan of the selected XBL finds only two immediate constructions of the `0x0c46xxxx` base.
They occur in the initializer and its configuration helper.
A literal at `0x148e3e38` also records the base in an address-range descriptor.
These observations do not prove that later firmware never uses IBUS.
They provide no register-level runtime request or status contract.

SM8845 applicability remains unproved. Enable this policy for another image only after confirming its register group as platform data.
The model contains no SoC conditionals.

## External runtime contract and limits

Qualcomm's [US20260072859A1, figures 2–4 and 8–9](https://patents.google.com/patent/US20260072859A1/en)
describes IBUS as a low-latency PMIC alert channel alongside SPMI.
An external PMIC detects an event and requests a capture through its data line.
The master starts the clock. PMICs insert alerts in assigned timeslots.
The arbiter distributes these alerts to subsystem limit-management consumers.
The master ends the frame and returns to idle.

Power-event producers include current-limit, imminent-foldback, and battery-current alerts.

This public source establishes the producer/consumer mechanism. It does not establish these MMIO offsets or field encodings.
The selected guest instructions establish the configuration offsets above.
A disconnected producer cannot request a capture or supply an alert payload.
Thus configuration writes alone cause no capture, completion, interrupt, mitigation output, or PMIC side effect.

The disconnected policy does not model analog power, wire timing, external events, bidirectional traffic, or runtime status registers.
Supporting those operations requires their full register and signal contracts. Unknown accesses fail instead of reporting success.

The [Linux Qualcomm PMIC arbiter binding](https://www.kernel.org/doc/Documentation/devicetree/bindings/spmi/qcom%2Cspmi-pmic-arb.txt)
describes the separate SPMI command interface and arbitration mechanism.
It does not justify interpreting the IBUS table as SPMI commands.

## Native fixture

`tools/qcom/qcom-vp/tests/fixtures/ibus` supplies synthetic guest table values, not vendor data.
The positive case checks cold state, every table/control entry, and configuration retention after enabling the bank.
Compile-time negative cases are `IBUS_UNSUPPORTED_STATUS`, `IBUS_BAD_CONTROL`, and `IBUS_WRITE_REVISION`.
They must fail at the unknown status read, reserved control-bit write, and version write respectively.
The fixture does not assert undocumented readiness.
The registered fixture passes all four cases.

## SM8845 display phase request

SM8845 run29 reaches a rejected write through command lane `0x0c90ac00`.
The access monitor reports translation-block PC `0x1486ebd0`.
A native breakpoint identifies the actual store at `0x1486ebfc`.
The packed command is `0x040003d0`: one write byte at offset `0x3d`, through APID 86.
The captured payload is `0x02`.

The native caller chain includes return address `0x14839910`.
The corresponding call at `0x1483990c` supplies SID 4, address `0xf83d`, and the selected display property's byte at offset `0x11`.
The parser at `0x14835a50..0x14835aac` identifies that property as `ibb-phase-select`.
This establishes a phase request, not an absent-device probe or an analog readiness result.

The selected SM8845 profile adds the existing sparse byte contract `0 4 0xf83d 0 0xff`.
Virtual cold zero means no request.
The existing PMIC model retains the guest's byte without simulating analog output.
No controller validation or routing rule changes for this transaction.

The selected continuation writes PMK `0x9abf = 0`, then PMK `0x7158 = 1`.
Both bytes already belong to retained SDAM.
They remain guest writes, not preloaded success values.

## OEM replay

SM8850 run47 and SM8845 run44 complete the original `sbl1_pmic_init` callback.
Both execute device initialization, peripheral discovery, and driver initialization.
SM8850 also completes its original post-driver OPSI program.
The driver logs report 1332 and 724 SPMI transactions respectively.
Both then complete `EUD Post PMIC Init` and enter the UFS path.
Neither run establishes UEFI, ABL, or kernel execution.

## AOP revision discovery

The [AOP PMIC audit](aop-discovery-evidence.md) enumerates both mask-and-route intersections.
SM8845 has 621 mapped scan records. SM8850 has 712.
SM8845 has four partial revision records at bus zero, SID7: `49`, `f0`, `f1`, and `f2`.
Their PSI configuration records do not establish their real revision bytes.
The inventories retain these explicit evidence gaps instead of assigning invented types.
Both AOP configurations give slot7 zero regulator counts.

Native AOP discovery still reads mapped peripherals on zero-count slots.
Its revision caller ignores read status after it clears the seven-byte buffer.
A failed transaction therefore skips the peripheral, not the complete discovery pass.
The virtual service now follows that behavior and retains its failure diagnostic.
Classified regulator count errors and missing enabled rails still fail publication.
No PMIC revision declaration or expected rail name changes.

## PMIC initialization program

The selected PSI2 programs contain several contracts, not one flat register image.
SM8850 uses `0x148f4674..0x149009b0`. SM8845 uses `0x148f207c..0x148fbf34`.
The programs contain 4166 and 3387 records respectively, including their end records.
The guest executes all conditions, writes, delays, memory transfers, and SDAM operations.
The platform does not replace the interpreter or install its configured values.

The two `sm*-xbl-pmic-controls.lua` files declare addresses and cumulative write masks.
Each group records its source coordinates. Each file records the source image hash.
Immutable virtual identities exclude absent SIDs and incompatible variants.
Unknown predicates remain unknown. Their possible write targets do not imply known input values.

Ordinary controls retain actual guest requests from virtual cold zero.
Zero means no request, not a captured silicon reset value.
The model does not simulate electrical effects or invent completion from these writes.
Existing SDAM and functional services keep their own state.
The explicit exploratory policy below handles only unknown ordinary configuration bytes.

Identity bytes `0x0150/0x0151` are writable PSI version storage on every populated SID.
They do not change the read-only PMIC identity.
SM8845 writes them at `0x148fbde4..0x148fbf28`, then reads them at `0x14839ce0..0x14839d14`.
SM8850 writes them at `0x14900890..0x149009a4`.

### Virtual PMIC inputs

The profiles declare unassigned manufacturing fields at `0x01f1..0x01ff` and virtual OTP identifier zero at `0x0154`.
Selected SM8845 `0x1487fb88..0x1487fce8` implements the same reads.
These values identify the virtual device. They are not physical factory records or authentication results.

SM8850 PMIH uses one SID instead of the older rehost's unverified two-SID policy.
The selected metadata record at `0x148ee644` has no MISC field offsets.
Its supported flat path requires `SLAVE_COUNT=1`.
The removed secondary SID has no writes or predicates in the selected PSI program.
This correction does not claim a measured physical topology.

| Input policy | Virtual value | Consumer |
| --- | --- | --- |
| XO configuration selector `0x504b` | `1` | One-shot electrical configuration selection. The physical frequency encoding remains unknown. |
| Sleep-clock configuration selector `0x604a` | `0` | One-shot electrical configuration selection, not `CLK_OK`. |
| DCDC power path `SID7:0x270b` | `2` | Battery path with no external input or soft-start completion. |
| Other declared PSI options | `0` | Bounded configuration choices without fabricated positive completion. |

The selected PBS programs can write the XO selector and SID4 `0xf9cf`.
These bytes retain future requests. They are not immutable factory identity.
No autonomous PBS execution changes them in this virtual profile.
The PSI evaluator reads each predicate once. It does not poll these inputs for completion.
Unknown physical field names remain unknown. These substitutions do not prove future kernel-driver behavior.

### Disconnected interrupts and protected controls

`disconnected_irq_peripherals` declares interrupt banks without event producers.
The selected PSI writes establish the register roles.
The absent producers are virtual policy, not a claim that physical inputs remain inactive.

| Offset | Behavior |
| --- | --- |
| `0x10`, `0x18`, `0x19` | Read-only realtime, latched, and pending state. No connected producer can set these bits. |
| `0x11..0x13`, `0x1a`, `0x1b` | Guest-owned configuration bytes. |
| `0x14` | Write-one latch clear. Reads return zero because no input can set the latch. |
| `0x15` | Write-one enable set. Reads return the current enable mask. |
| `0x16` | Write-one enable clear. Reads return the same enable mask. |

UEFI SPMIDxe clears interrupts in its ExitBootServices handler.
The handler at RVA `0x5abc` writes `0x14` and `0x16` of 61 groups on both buses.
Run879 lists these groups. The SM8850 list in `platforms/qcom/sm88xx/sm8850/xbl-pmic-controls.lua` declares each group as a disconnected bank.

`pon_hlos_peripherals` declares a gen3 PON HLOS group.
Gen3 PON has two register sets: HLOS at `0x1300` and PBS at `0x0800`, as in the pmk8280 and pmk8550 device trees.
Linux [pm8941-pwrkey.c](https://github.com/torvalds/linux/blob/master/drivers/input/misc/pm8941-pwrkey.c) names subtype `0x09` `PON_SUBTYPE_GEN3_HLOS`.
The group reports type one and subtype `0x09` with virtual digital and analog revision 1.0.
Offsets `0x08..0x0f` read zero. Offsets `0x10..0x1f` form a disconnected bank, because no virtual key exists.
UEFI ButtonsDxe reads its real-time status. The SPMIDxe ExitBootServices handler clears its enables.
Before this group, discovery reported the peripheral absent, and ButtonsDxe reported `GetPonRtStatus failed`.

`protected_controls` associates one target byte with its peripheral's `0xd0` key register.
Writing `0xa5` authorizes one target write. Another key value clears that authorization.
The target write consumes the key. Reset also clears it.
The key register is write-only. A locked target write fails without changing the target.
This one-shot lifetime is explicit virtual policy.

The selected target is SID7 `0x2442`, with key register `0x24d0`.
SM8850 uses bus one. Its unlock records include `0x148fda04` and `0x148fdcc8`.
SM8845 has the unlock record `0x148f7854` on bus zero.
Its branch is inactive for the declared model `0x4c` identity, so the SM8845 profile declares no protected control.
Native SM8845 run171 makes no access to SID7 peripheral `0x24`.
This PMIC control does not affect image authentication or TME authorization.

### Broadcasts and initialization lifetime

SID 14 broadcasts writes to each populated primary SID on the selected bus.
SID 15 selects each populated secondary SID.
The identity span determines membership. These group addresses do not create physical identities.
The model validates every recipient before any recipient changes.
Broadcast reads and PBS transactions remain unsupported.

PON `0x088c` starts clear. Only the guest publishes device and driver initialization bits there.
PON `0x08da` starts with FOLLOW_WARM_RB bit two set.
This explicit reset policy permits the guest to retain its initialization spare during normal operation.
A warm reset clears the spare when that policy bit remains set.
The policy does not synthesize successful initialization.

### Post-PSI services

The selected callbacks consume GPIO, regulator, clock, and RTC interfaces after peripheral discovery.
The revision bytes are digital minor, digital major, analog minor, analog major, type, and subtype.

GPIO banks declare digital revision 3.1 and subtype 19.
The selected decoder at SM8845 `0x1487c004..0x1487c05c` accepts three VIN choices for that subtype.
Both selected DCP drivers require a packed revision below `0x32`.
Both XBL and UEFI GPIO drivers accept values below `0x33`.
The VIN capability depends on subtype, not digital revision.
Revision 3.1 replaces the earlier DCP-incompatible 3.2 policy without changing the subtype or GPIO controls.
See [the DCP evidence](dcp-evidence.md#gpio-revision-provenance-and-consumer-bounds) for the source history and exact consumer PCs.
Configuration bytes retain requests.
An enabled software output derives its level from the selected source and inversion bit.
Disconnected inputs and unsupported waveform sources do not generate pin events.

Regulators start disabled, without a voltage request or READY state.
Accepted enable requests produce ideal readiness. Voltage requests retain their validated values.
ULS threshold bytes `0x39/0x3a` and phase controls `0x98/0x99` retain guest requests.
LDO STATUS3 bit zero declares retention-control capability, not readiness.

Clock instances expose the selected BB, XO, and CALRC3 capabilities.
Their controls retain requests without generating oscillator waveforms or calibration results.
RTC digital revision 5.1 exposes cold-clear status bytes `0x08/0x09`.
The loss-of-power flag occupies `0x09` bit zero, as `drivers/rtc/src/pm_rtc_driver.c:51-56` specifies.
This bounded RTC service provides no running counter, timestamp, or valid-wall-time claim.

### Exploratory configuration

`exploratory_configuration` defaults to false. Both OEM profiles enable it explicitly.
It covers unknown offsets `0x40..0xcf` on populated SIDs, with peripheral IDs at least two.
These bytes start at zero and retain actual writes.
Every accepted exploratory transaction emits `UNMODELED_PMIC_SUBSTITUTE`.
Identity, status, key registers, and PBS interfaces remain outside this policy.
Existing register masks, protected-write checks, and functional service errors remain authoritative.
The model never retries a rejected owned operation through exploratory storage.

### PBS memory contract

`pbs_peripherals` declares the bus, SID, peripheral, revision, RAM base, and RAM capacity.
Both OEM profiles select virtual revision `0x0501` with RAM base zero.
The large peripheral-`0x16` instances provide `0xc000` bytes.
Other capacities round each selected upload maximum to 128 bytes.
These capacities describe the virtual device, not physical PMIC measurements.

The helper owns revision bytes, memory controls, transfer ports, and geometry registers.
The backend selects ownership before the transaction. It never retries a failed PBS access through sparse storage.
Other explicitly declared controls on the same peripheral remain independent.

| Registers | Behavior |
| --- | --- |
| `0x07` | Three declared passive client inputs. This count does not report execution or completion. |
| `0x20`, `0x27` | Read-only zero reports no autonomous producer. This is the native not-started combination. |
| `0x28..0x29` | Unsupported version input. No autonomous producer supplied a version. |
| `0xc3` | Retained control bit two. A pulse does not change RAM, the interface, or producer status. |
| `0x40`, `0x41`, `0x42..0x43` | Interface enable, access mode, and byte address. |
| `0x48`, `0x4c` | Four-byte write and read ports. |
| `0x80`, `0x88` | Revision-5.1 write and read windows. Burst transfers advance the RAM address. |
| `0xa0`, `0xa1` | RAM capacity in 128-byte units for the selected revision. |
| `0xa3`, `0xa4` | Reserved NVM geometry. The profiles declare no NVM contents. |

SM8845 setup at `0x1487f1d8` supplies byte addresses.
Its downloader at `0x1487f3ec` transfers four bytes per source word.
The packed driver at `0x1486eb24..0x1486eb68` repeats the same window address for each eight-byte chunk.
The geometry decoder at `0x1487ec64..0x1487ee48` combines the high capacity byte only when the low byte exceeds eight.
Thus `a0=0x80`, `a1=1` describes `0xc000` bytes.

RAM starts at deterministic virtual zero. Guest writes replace those bytes without supplying an expected image or version.
Disabled access, incorrect modes, invalid alignment, and capacity overflow fail atomically.
Cold reset clears RAM. Warm reset retains guest bytes and disables the interface.
The four-byte read port also works under revision-5.1 burst mode, as the native metadata consumer requires.
The model does not execute PBS code or supply ROM contents.
Version reads return actual RAM bytes, including untouched zero tails.
The QSEE scan burst reads interface registers and cold zeros, and it moves no transfer pointer.
The `pmic-dump` fixture checks that isolation: a scan burst at a disabled port leaves the interface unchanged.

The native `pmic-psi` fixture exercises interrupt aliases, one-shot keys, PSI version storage, and PBS transfer ownership.
The standalone `pbs-native` fixture exercises lifecycle failures, atomicity, retained bytes, and the largest selected upload size.
The PMIC fixture also checks broadcast membership, read rejection, and guest-owned initialization publication.
Native OEM execution establishes the callback completion reported above.
SM8845 still logs PBS CRC mismatches because this substitute supplies no autonomous PBS execution result.
The original guest handles those messages and continues. The model does not suppress them.

## Peripheral lock controls

The selected lock helpers are SM8850 `0x148849e4` and SM8845 `0x148840a4`.
They resolve properties `0x40` and `0x41`, then process three descriptor groups.
Each descriptor contains an array pointer, a register offset, and a record count.
The loader selects records through original channel ownership and SID checks.
The model does not preload the requested lock states.

| Register offset | SM8850 records | Count | SM8845 records | Count |
| --- | --- | --- | --- | --- |
| `0xd1` | `0x148ed8fc` | 751 | `0x148eb7ac` | 697 |
| `0xd2` | `0x148ee4b8` | 27 | `0x148ec290` | 18 |
| `0xd3` | `0x148ee524` | 3 | `0x148ec2d8` | 7 |

The descriptor bases are `0x148ee530` and `0x148ec2f8`.
Both D1 tables request modes zero, two, and three.
Both D2 and D3 tables request zero.

The model retains independent D1, D2, and D3 states for each peripheral and SID.
Zero is programmable open. One is reserved and rejected.
Two is terminal open. Three is terminal closed.
Later writes cannot change a terminal state.

A closed state blocks declared protected controls and PBS memory write ports.
Ordinary configuration, PBS read transport, and existing SPMI ownership remain independent.
The existing one-shot key requirement still applies to protected controls in open states.
This is an explicit virtual protection scope, not a complete physical assignment of registers to lock groups.
Unknown ordinary configuration retains its existing substitute log.

Run132 exposes an ordinary PSI write to bus one, SID seven, register `0xf14a` after a closed latch.
A complete peripheral freeze therefore does not match that consumer sequence.
Cold reset clears the latches. Virtual warm reset retains terminal states.
The native fixture checks terminal retention, ordinary GPIO behavior, protected writes with valid keys, and PBS read/write separation.

SM8850 run136 records zero from the lock helper and complete pre-AOP function.
SM8845 run113 records the same results from its original functions.
The retained `pmic-aop-probe.json` files contain their PCs and results.
Both UART logs then report AOP Config loading and start AOP loading.
Neither observation establishes AOP admission or kernel execution.

## Pre-DDR regulator and button consumers

SM8845 `pm_system_init` resolves interface `0x18`, then calls entry `+0x30` at `0x148393a0`.
The callback requests 1800 mV from PMIC1 LDO indices six and five.
Both requests use wrapper `0x1487db28`.
The driver pointer at `0x14934108` was null, so both requests returned four.
Run62 stops at `0x1482fcf8` and confirms the combined return value four.
The retained diagnostic is `build/private/run-sm8845-common-runtime62/pre-ddr-callback.json`.

The virtual inventory now includes PMIC1 peripherals `0xc1..0xd1`.
The selected discovery mask declares this bank.
Initializer `0x1487e124` allocates seventeen LDO entries relative to its first peripheral.
Thus the requested indices address peripherals `0xc7` and `0xc6`.
The existing regulator service owns their voltage, enable, and status behavior.
The board does not install driver pointers or override callback results.

SM8850 button reader `0x1489821c` consumes GPIO realtime status and inverts an active-low input.
The combined state at `0x14898550` assigns bits zero, one, and two to power, volume-up, and volume-down.
The selected descriptor requests a pull-up input.
Disconnected GPIO inputs now resolve their programmed pulls when enabled.
Pull encodings zero through three produce high, four produces low, and five retains the deterministic low policy.
Software outputs retain their programmed level.
Status and realtime reads use the same pin state, without creating interrupt latches.

The selected input setup at `0x14897e48` programs mode, VIN, and pull, but does not enable the pin.
The virtual GPIO banks now start enabled.
This explicit cold policy permits the programmed pull to determine the input.
It is not a claim about a measured physical reset value.
The guest can still disable each pin.

The native PMIC fixture checks disabled input, pull-up, pull-down, software output, and unchanged interrupt latches.
SM8850 run65 reported `010` under the earlier cold-disabled policy.
Run66 no longer prints that false volume-up state and completes `boot_fedl_check`.
SM8845 run64 passes the original regulator callback and completes `sbl1_hw_pre_ddr_init`.
The normal path at `0x14838c90` pets the watchdog even when no combination matches.
That watchdog access is not evidence of a fatal callback.

## Complete DDR regulator inventories

The board inventories use the selected discovery masks, provider counts, and native register strides.
They declare functional peripheral types as virtual policy, not captured silicon responses.
The existing service supplies guest-owned mode, voltage, enable, and status state.
No host code installs driver pointers or changes driver return values.

| Profile | SMPS rails | LDO rails | BOB rails |
| --- | ---: | ---: | ---: |
| SM8850 | 44 | 48 | 2 |
| SM8845 | 32 | 50 | 2 |

The SM8850 BOB bank keeps controls `0xe4` and `0xe6` at bus 0, SID 1.
The live-phone record `20261026T180725Z` reads type `0x29` and subtype `7` there.
Subtype `7` is `PM_HW_MODULE_BOB_CONFIG_PERIPH_SUBTYPE`.
The BOB driver reads a control index from the base address distance, with stride `0x200`.
The two controls therefore fill indices zero and one.

The same record reads type `0x14` and subtype `0x6b` at peripherals `0x40..0x43`.
Type `0x14` is `PM_HW_MODULE_MISC`, and its MISC subtype set has no `0x6b` entry.
The model now follows the record at both ranges.

Native run526 failed because it served the MISC identity at `0x40` and kept the BOB bank at `0x42`.
`pm_driver_init` then returned error 32 at line 164.
Error 32 is `PM_ERR_FLAG_INVALID_PERIPHERAL`.
Each allocated BOB slot clears its OCP status.
That error returns when the slot holds no discovered type.
The `PM_PROP_BOB_NUM` property declares two slots.
Run524 wrote no byte at `0x4046`.
Its only PMIC writes were `0x4146`, `0x4346`, `0x4d46`, and `0x4e46`.
The earlier claim of a required BOB control at `0x40` is therefore false.

Both providers accept BOB subtype `7`.
Subtypes `4`, `6`, and `8` skip BOB initialization.
The virtual SMPS revision 1.0 selects limit registers `0x68/0x69`, not the newer `0x39/0x3a` layout.
The BOB limit uses one byte multiplied by 32000 microvolts.
A unified rail uses two bytes at `0x68`. Run532 writes `0xe468`.
The BOB limit bytes are therefore writable, and the OEM records `0x148fb0ac` and `0x148f9f60` agree.
These contracts require no change to the common regulator model.

SM8850 selects ten SMPS controls on each of SIDs three, five, six, and eight.
Bus one SID nine supplies four more.
The controls start at peripheral `0x99`, with stride three.
Its provider allocation is at `0x14882b50..0x14882bcc`.
Register table `0x148f3db6` supplies byte stride `0x300`.
The LDO counts come from `0x1487fa7c..0x1487fb20`.

SM8845 selects eight SMPS controls on each of SIDs three, five, six, and eight.
Its allocation at `0x14882260..0x14882284` and table `0x148f17ce` establish the count and stride.
The discovery loop retains disabled PMIC ordinal positions.
Thus PMIC ordinal five addresses SID five, not SID six.
The LDO counts come from `0x1487e0d4..0x1487e154`.

SM8845 run66 captured an inconsistent earlier BOB inventory.
At `0x148810d4`, index one had type zero, with caller `0x14889c24`.
The retained capture is `build/private/run-sm8845-common-runtime66/pmic-postinit-status.json`.
The original provider allocates two slots and computes indices from the first discovered control, with stride `0x200`.
The selected mask at `0x148f191c` includes peripherals `0x3e` and `0x40`.
The corrected virtual placement uses both controls without changing the mask.

## Identified MISC peripherals

The `misc_peripherals` parameter declares a peripheral whose type has no function in this policy.
Each row is `bus sid peripheral type subtype`.
The service publishes the seven-byte revision and read-only zero bytes `0x08..0x1f`, and it supplies no control.

Value 20 (`0x14`) is `PM_HW_MODULE_MISC`. Its subtype set has no `0x6b` entry.
The live-phone record `20261026T180725Z` reads type `0x14` and subtype `0x6b` at peripherals `0x40..0x43` of bus 0, SID 1.
That block gives the identity and no function.
The same record reads type `0x29` and subtype `8` (`PM_HW_MODULE_BOB_MONITORING_PERIPH`) at peripherals `0xe5` and `0xe7`.
The platform declares both ranges.
The BOB driver returns before it allocates a control for subtype `8`.

The record shows other bytes after an Android boot.
Bytes `0x20..0x3f`, `0xd0..0xd9`, `0xdb..0xef`, and `0xf0..0xff` read cold zero.
That is a substitute for the unknown reset value, and the model records it in `docs/qcom/sm88xx/stubs.md`.
Byte `0xda` stays a guest-owned warm flag, so it also starts at zero.
Offsets `0x40..0xcf` stay with the exploratory service, which retains the guest writes.
Byte `0x48` has no owner and no declared name.
Byte `0xd1` remains the peripheral lock word.

The QSEE block reader scans every declared peripheral, not only these four.
Run533 reads the SID 1 GPIO block up to `0x8836..0x883f`.
The next stop was the scan byte at `0x88d0`, and the scan substitution now answers it.

# XBL cold USB-debug group

## Selected evidence

The selected input is the external `xbl_sc.elf` from `CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES`.
Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
No firmware bytes or tuning values belong to this model.

The selected binary establishes current addresses, counts, and operations.

| Selected address | Register role |
| --- | --- |
| `0x88e1000 + 4*i`, `i=0..3` | `EUD_DEV_ID_0..3`, low serial byte first, mask `0xff` |
| `0x88e2000` bit 0 | `EUD_EUD_EN2`, protected enable control, not PBUS completion |
| `0x88e0240` | `EUD_ENUM_STAT_REG`, read-only enumeration status |
| `0xc278000` bit 0 | `VIOCTL_EUD_DETECT`, software device-mode cable/VBUS indication request |
| `0x88e102c` bit 0 | `EUD_EUSB2PHY_XTRA_TUNE_PARARM_EN` |
| `0x88e1080 + 8*i`, `0x88e1084 + 8*i` | `EUD_Q2_TUNE_ADRnn` and paired data, byte-valued PHY tuning words |
| `0x88e1024` bit 0 | `EUD_PBUS_CTRL_EN`, PHY tuning program request |
| `0xa60c704` bit 31 | `USB3_PRI_DCTL.RUN_STOP`, not PHY lock |
| `0x1fd504c`, `0x1fd505c` bit 0 | USB reference-enable votes, corresponding to the PHY2/PHY3 QREF roles |

The tuning data is not a set of USB string descriptors.

## Complete cold path

The selected caller is `0x1488ec64`.
Its operations are:

1. Call `0x1489ab20` to read the chip serial from fuse address `0x221c8610`.
2. Call `0x1489acb8` to write four serial bytes into `DEV_ID_0..3`.
3. Call `0x1489ab30` to check `EUD_EN2` bit 0. The active path logs `ENUM_STAT_REG` and returns.
4. On the inactive path, call `0x1488e868(1)` to enable the named `usb30_prim` clock group.
5. Call `0x1489c99c` to configure EUD tuning storage.
6. Call `0x1489ab74` to read `DCTL.RUN_STOP`.
7. If RUN_STOP is clear, record the guest permission and call `0x1488e868(0)` before returning.


At `0x1489c99c..0x1489ca40`, the selected binary:

- Sets `VIOCTL_EUD_DETECT` bit 0 if it is clear.
- Calls `0x1489abb0(0,1)` to set both reference votes.
- Sets extra-tuning bit 0.
- Copies 17 address/value pairs from guest image locations `0x148e29c7` and `0x148e29d8`.
- Sets `PBUS_CTRL_EN` bit 0.

The helper retains all 34 guest tuning writes. It does not embed the source bytes.
It does not turn a PBUS request into `EUD_EN2` or an enumeration result.

## Named clock dependency

The selected lookup uses string address `0x148dc88d`, whose value is `usb30_prim`.
The group descriptor is at `0x148f2d00`.
Its nine clock nodes start at `0x148f32a0`, with a 32-byte stride.

| Branch | Selected group role |
| --- | --- |
| `0x139094` | First master-domain branch |
| `0x139090` | Second master-domain branch |
| `0x139018` | Third master-domain branch |
| `0x13902c` | Mock UTMI branch |
| `0x139028` | Sleep branch |
| `0x139068` | PHY AUX branch |
| `0x13906c` | PHY COM AUX branch |
| `0x139070` | PHY PIPE branch, flag `0x20`, no enable-status polling |
| `0x1fd504c` | Existing shared reference vote |

The first three nodes reference domain `0x148f2468`.
Its command address is `0x139034`, which already belongs to `cpucp_clock_root_20`.
The group uses GDSC `0x139004` and BCR `0x139000`.

The [pinned public Canoe clock driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c) corroborates the current master RCG at GCC offset `0x39034`.
It declares an eight-bit M/N field, a five-bit divider, and parent selectors 0 for XO, 1 for GPLL0 main, and 6 for GPLL0 even.
Its rate table lists 66,666,667, 133,333,333, 200,000,000, and 240,000,000 Hz.
These are supported rates, not an observed boot rate.
It names the first three branches AGGRE USB3 PRIM AXI, CFG NOC USB3 PRIM AXI, and USB30 PRIM MASTER.
It also identifies HWCG bit 1 on the two AXI branches and the PIPE branch.
The helper enables those control fields without inventing downstream demand.


The SM8850 composition reuses root 20 and both existing reference votes.
The shared helper adds all eight missing branch objects and the GDSC.
APSS and CPUCP aliases reach the same clock objects.
Unmodeled non-PIPE consumers use an explicit virtual reference. This is not a claim about physical parent topology.
The PIPE branch consumes the disconnected model, which always reports no PHY clock.
The firmware skips polling this branch, as its selected node requires.

The existing GDSC sends power requests to the disconnected model.
The model applies power to its virtual controller-register domain before the GDSC acknowledges the request.
DCTL access fails while this domain is off.
Power changes never start a controller session or connect a host.
BCR reset remains unsupported and unmapped.

## Virtual transport contract

`qcom,disconnected-usb-debug-v1` requires `disconnected=true`.
This policy declares no host endpoint and no inherited controller session.
It is not a security-fuse override or an enumeration shortcut.

- EUD enable starts at zero and retains guest writes of zero or one. Other values fail without changing the vote.
- Enumeration reads remain zero, including after an enable vote. Enumeration writes fail.
- The optional `attach_socket` exposes unasserted software-attach state at `0x88e1020`. Reads return zero and writes fail.
- DCTL returns its documented reset value `0x00f00000`, with RUN_STOP clear, while the virtual register domain is powered.
- DCTL writes fail. An enable vote never creates an inherited controller session, host connection, or PIPE clock.
- The detect control retains a write of one as a setup request. That request does not indicate a host connection.
- A write of zero to detect requests reset/recovery and fails.
- Serial, tuning, extra-tuning, PBUS, and reference votes use the existing retained-control model.
- EUD attach requests, reset, USB controller start, and USB service operations have no implementation. Unknown MMIO remains unmapped.

The new model has no interrupt, USB packets, physical endpoint, or completion timer.
No status read advances initialization.
The helper does not change UFS ownership or alias an EUD register to UFS.

## Integration and verification boundary

Register `QcomDisconnectedUsbDebug` from `disconnected_usb_debug.h` with compatible key `qcom,disconnected-usb-debug-v1`.
Both runtime factories call `platforms/qcom/sm88xx/xbl-eud.lua` after the reset clock roots and their boot clock policy helper exist.
Each factory supplies its selected second branch address and HWCG capability.
The helper finds the sole USB master RCG by address `0x139034`, not by a platform-specific module name.
It checks duplicate names and address overlap before each new target.
It reuses existing bit-zero reference votes and creates routed retained owners only when those words have no retained owner.
An incompatible existing target fails the overlap check instead of receiving a second owner.

The installed SM8850 run `build/private/run-sm8850-common-runtime2` executes this cold group.
Its UART records `usb_eud_serial`, `vioclt_eud_set`, and the completed `sbl1_hw_init` interval.
The first rejected access occurs later, at UFS UIC command address `0x1d84090`, from PC `0x148c984c`.
The EUD group does not establish kernel execution.

## SM8845 CPH2771 comparison

The native source is `IMAGES/xbl_sc.elf` in `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`.
Its SHA-256 is `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
The package and prepared input paths are recorded in `build/private/sm8845-reconstructed.toml` and `build/private/prepared-sm8845-common-runtime2/inputs.json`.
The runtime model contains no source image bytes.

### Complete selected cold sequence

The caller at `0x1488d144..0x1488d204` performs the same cold sequence as SM8850:

1. `0x14899460` reads the serial from `0x221c8610`.
2. `0x148995f8..0x14899648` writes four low-first serial bytes at `0x88e1000 + 4*i`.
3. `0x14899470` checks `EUD_EN2` bit zero. Its active path reads enumeration status at `0x88e0240`.
4. The inactive path calls `0x1488cd48(1)` to configure and enable `usb30_prim`.
5. `0x1489b264..0x1489b308` sets detect bit zero at `0xc278000`, if clear.
6. That routine calls `0x148994f0(0,1)` to set bit zero in both `0x1fd504c` and `0x1fd505c`.
7. It sets extra-tuning bit zero at `0x88e102c` and copies exactly 17 byte-valued address/value pairs.
8. The guest arrays start at `0x148e0a95` and `0x148e0aa6`. The destination addresses and byte masks match SM8850.
9. It sets PBUS control bit zero at `0x88e1024`, then returns without polling a fabricated completion.
10. `0x148994b4` reads `DCTL.RUN_STOP` bit 31 at `0xa60c704`.
11. RUN_STOP clear selects `0x1488d1c0..0x1488d1d8`, stores permission value two at `0x14914ae4`, and disables the clock group.

This policy never selects the inherited-session consumer at `0x1488d188`, which calls `0x1489b344`.
The guest makes the decision from the disconnected controller state, not a patched branch.
Attach at `0x1489b30c` and reset pulses at `0x1489953c..0x148995f4` remain unsupported.
The latter routines access `0x88e303c`, `0x88e3040`, and `0x88e3094`. These addresses remain unmapped.

### Complete selected clock group

String `0x148da7cf` is `usb30_prim`.
Descriptor `0x148f0720` supplies nine nodes at `0x148f0c58`, with stride 32 bytes.
The group contains one domain-root pointer at `0x148f0d78`, GDSC `0x139004` from table `0x148f0d80`, and BCR `0x139000`.

| Node | SM8845 address | Source or policy |
| --- | --- | --- |
| 0 | `0x139094` | Existing USB master RCG, HWCG bit 1 |
| 1 | `0x139030` | Existing USB master RCG, enable bit 0 only |
| 2 | `0x139018` | Existing USB master RCG |
| 3 | `0x13902c` | Explicit virtual non-PIPE reference |
| 4 | `0x139028` | Explicit virtual non-PIPE reference |
| 5 | `0x139068` | Explicit virtual non-PIPE reference |
| 6 | `0x13906c` | Explicit virtual non-PIPE reference |
| 7 | `0x139070` | Disconnected PHY clock, node flag `0x20`, no status polling |
| 8 | `0x1fd504c` | Shared retained reference vote |

Only node 1 differs in address from the selected SM8850 group.
Do not replace `0x139030` with `0x139090` because a public driver names the latter register.
The [pinned SM8845 public GCC driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8845/blob/2774f857d6e77443de95defbf1199e0afdc14d9a/drivers/clk/qcom/gcc-canoe.c)
corroborates RCG `0x139034` and HWCG bit 1 at `0x139094` and `0x139070`.
It declares `0x139090`, not selected node `0x139030`. No unproved HWCG field is enabled for `0x139030`.

The first three nodes reference descriptor `0x148eff78`, whose command address is `0x139034`.
Its single rate entry at `0x148eff58` requests 200 MHz with selector 1, divider three, and GCC source index four.
The source is GPLL0 main. The existing root already connects selector 1 to GCC PLL bank zero.
`qrd8850-reset.lua` creates its sole owner as `cpucp_clock_root_20`, with APSS and CPUCP aliases.
The SM8845 domain-root helper does not replace that owner.
The EUD helper discovers and consumes that same owner by address. It creates no second root or unconditional master clock.

`0x1485f010` enables the GDSC before iterating all nine branch nodes.
GDSC consumer `0x1485ec60` clears collapse bit zero and waits for power-on bit 31.
Branch consumer `0x1485ed10` sets enable bit zero and waits for clock-off bit 31 to clear.
It skips this wait when HWCG bit 1 or node flag `0x20` is set.
Disable consumer `0x1485f174` clears each branch enable through `0x1485edac`, then sets GDSC collapse through `0x1485ecdc`.
This sequence does not request BCR reset.

SM8845's boot clock policy supplies neither reference word.
The shared helper creates each missing retained vote once, with APSS and CPUCP aliases through the clock router.
SM8850's boot clock policy already supplies both words. The helper retains those owners unchanged.
The group disable clears `0x1fd504c` through its ninth node. The separate `0x1fd505c` tuning vote remains retained.

### Permitted substitutes and proof limit

Both compositions reuse the disconnected transport contract above without a SoC condition inside a model.
The substitutes are cold-zero retained controls, a disconnected host/session policy, virtual controller-register power, and virtual non-PIPE clock references.
The existing RCG and GCC PLL models supply the three master branches. The PIPE source remains absent.
GDSC acknowledgement follows virtual register-domain power. It does not claim physical USB PHY power or lock.
No tuning values, serial value, completion status, enumeration success, USB packets, or authentication result are supplied by the helper.
Authentication, rollback, AVB, and lock policy remain unchanged.

The retained SM8845 run stopped at `0x1489962c`, writing the first device-ID register.
Static ELF decoding establishes the complete group above. It does not establish execution of the newly composed SM8845 group.
Both real factories still need execution with their logs retained. This change ran no build, test, formatter, or firmware run.

## XBL Config post-PMIC group

The selected input identities remain the two SHA-256 values above.
The following addresses come from static decoding of those inputs, not a firmware run.
The lists use 16-byte callback/name pairs and terminate with a zero pair.

| Callback | SM8850 list `0x148ed448` | SM8845 list `0x148eb2f8` |
| --- | --- | --- |
| EUD Post PMIC Init | `0x1483886c` | `0x148369d4` |
| media_init:2 | `0x14838874` | `0x148369dc` |
| media_init:3 | `0x1483887c` | `0x148369e4` |
| boot_wdog_disable_check | `0x14838884` | `0x148369ec` |
| init-logstomedia | `0x14838918` | `0x14836a80` |

These callbacks follow `sbl1_pmic_init` in the same list.
The callback walker stops on a nonzero result.
SHRM follows the completed XBL Config record. This group does not establish SHRM or DDR completion.

### EUD authorization and disconnected state

The EUD wrappers call `0x14831ea8` / `0x1482ffb8` with phase one.
They obtain interfaces `0x37` and `0x35`.
Interface `0x37` method zero must succeed and produce a nonzero permission predicate before interface `0x35` method eight runs.
The predicate comes from consolidated debug vector zero, bit 27.
SM8850 reads this bit through `0x1484398c -> 0x1485e3b4 -> 0x1485e56c -> 0x1487af40`.
Instruction `0x1487af50` extracts bit 27 after `0x1487ad60` reads fuse-base offset `0x780`.
The current explicit virtual profile sets this fuse word to zero.
Thus the guest skips the hardware body. The model must not change that fuse to request EUD.

The authorized body differs between the OEMs:

- SM8850 calls `0x14842058 -> 0x1488ed80 -> 0x148c58bc(1)`.
  Descriptor `0x148e7128` supplies fuse `0x221c873c`, mask `0xf`.
  A masked value of `0xb` skips enable.
  Other values call `0x148c59d4`, which checks EN2 and the guest USB permission.
  With EN2 clear and nonzero USB permission, it writes zero to both delay bytes before it writes EN2 one.
  The byte addresses are `0x88e1180` and `0x88e1184`.
- SM8845 calls `0x1484058c -> 0x1488d260 -> 0x148c3aa8(1)`.
  Descriptor `0x148e52c8` supplies the same EN2, enumeration, fuse address, and fuse mask.
  This implementation checks EN2 before the phase and fuse predicates.
  Helper `0x148c3c0c` checks guest USB permission, writes EN2, reads `0x01fc8004 & 3`, then reads software-attach state.
  The software-attach address is `0x88e1020`, with mask `0xff`.
  SM8845 does not perform the SM8850 delay-byte writes.

These branches do not justify an enumeration result or a USB session.
The disconnected model now retains the guest EN2 vote independently from enumeration and software-attach state.
Its optional attach socket permits existing standalone fixtures to omit the new mapping.
Platform wiring for the attach socket and the two byte-wide retained delay controls remains open.
The delay fields are `EUD_EN_DELAY_LSB` and `EUD_EN_DELAY_MSB`.
Their masks are `0xff`, and their reset values are zero.
The separate charger path uses software vote `0x88e1014`, mask one. That vote is not this callback's entry point.

### Media phases and storage interfaces

The media wrappers pass phase indices one and two, not values two and three.
Their common walkers are `0x14839764` / `0x148378cc`.
Each walker scans the selected record table, resolves media sentinel `0x23`, and stops at sentinel `0x24`.
It processes each distinct resolved media ID once.
It saves context parameter `0xa`, installs the resolved media ID, calls the phase dispatcher, then restores the parameter.
Any failed operation returns its error. A model must not supply a successful callback result.

Dispatchers `0x1487a624` / `0x14878d68` allocate a `0x48`-byte media interface through memory-manager interface seven.
They populate that interface through `0x1482d978` / `0x1482ba88`.
The selected boot-media groups eight and five use the same staged property pattern.
Their descriptors are `0x148f0cc0` / `0x148f0d50` and `0x148ee950` / `0x148ee9e0`.
Each phase descriptor has stride `0x30`.

- Phase zero requests mode zero.
- Phase one requests mode one and sets the background-init deferral flag.
- Phase two requests mode one without changing that flag.

Media setters `0x1486c000` / `0x1486a71c` retain those properties.
Initializers `0x1486b244` / `0x14869960` consume them through coldplug interface `0xd`.
Coldplug method `+0x48` opens the boot device and records state bit zero.
Method `+0x40` starts background initialization and records state bit one.
The deferral flag permits the phase-one return at that point.
The next invocation finishes background initialization through `+0x50`, opens the device through `+0x58`, and records state bit two.
All three bits select the already-initialized return path.
Status zero and the selected coldplug sentinel `0xfffe` have separate handling. The guest retains pending status and retries through its own method calls.

This group needs the existing UFS controller, PHY, clocks, power, DMA route, and six mutable numbered LUN files.
It requires normal device queries, block reads, and GPT discovery beyond boot-WLUN reads.
No new media image, host file service, filesystem emulator, LUN alias, or authentication result is required.
The [UFS contract](../ufs/README.md) describes these existing device behaviors and their unsupported paths.

### Watchdog and streaming logs

Both selected `xbl_config.elf` inputs contain `/sw/boot` values `xbl_sc_wdog=1` and `save_logs_to_media=2`.
SM8850 base DTBs start at file offsets `0x2ac` and `0x10e14`.
SM8845 base DTBs start at `0x30c` and `0xf52c`.
These are decoded input values. No configuration bytes are copied into the model.

The watchdog callback initializes its local value to one and queries `xbl_sc_wdog`.
Only a successful query with value zero calls the disable helper.
That helper writes zero to `0x17908008`, at `0x1482efc8` / `0x1482d0d8`.
The selected value one leaves watchdog behavior unchanged.
A failed property query does not request a disable.

The logging callback accepts modes one and two. Other values return without registration.
Mode two stores the mode and registers logger table `0x148ebc88` / `0x148e9c88` through logger interface five, method `+0x10`.
SM8850 registrar `0x1482b96c` installs the table, calls its initializer, then streams the existing buffer through table method `+0x10`.
Thus registration can perform actual media writes in this callback. It is not only a pointer update.

Initializers `0x1482ff54` / `0x1482e064` open partition ID `0x21`, query its size, and read its 28-byte header.
The partition type GUID is `f7eecb66-781a-439a-8955-70e12ed4a7a0`, with name `XBL_LOGS_PARTITION_GUID`.
It is not the separate `logfs` filesystem partition.
The mutable prepared images already contain `xbl_sc_logs` on LUN four:

| Prepared inputs | Inclusive 4096-byte LBAs |
| --- | --- |
| `prepared-sm8850-common-runtime12/lun4.img` | `503326..503357` |
| `prepared-sm8845-common-runtime11/lun4.img` | `712206..712237` |

Both inspected headers contain zero initially.
The guest treats a header as reusable only when magic is `0x4c544d49`, version is one, record size is `0x5000`, and partition size matches.
The guest initializes an invalid header itself, selects offset `0x1000`, and writes the header through normal partition I/O.
Do not preformat this partition or supply a synthesized valid header.
Streaming writers `0x148303e4` / `0x1482e4f4` require initialized state, no prior failure, no recursive write, the header magic, and mode two.
They write the guest buffer through normal media operations and retain the resulting offset.
Failed media I/O remains a guest error. No model change suppresses it.

### Integration acceptance

Both platform compositions include the retained delay bytes and the optional attach mapping.
The native EUD fixture checks enable retention, rejected invalid values, and disconnected status after enable.
SM8850 run54 and SM8845 run48 complete `EUD Post PMIC Init` after the original PMIC callback.
Both complete `media_init:2` and pass UFS device readiness.

Both OEMs must complete these five callbacks in order after PMIC succeeds.
With the current fuse profile, the EUD outer predicate must remain zero and the hardware-enable body must remain unexecuted.
The watchdog callback must not disable the watchdog with `xbl_sc_wdog=1`.
Both media phases must preserve their context parameter and complete through the existing coldplug interface.
Logging acceptance must include real `xbl_sc_logs` header and stream writes to LUN four, not only an `init-logstomedia` start line.
Retain UFS request completion, DMA backing identity, and the changed partition bytes.
SM8845 run51 completes `media_init:3`, `boot_wdog_disable_check`, and `init-logstomedia`.
Its prepared log partition contains only zero bytes.
The executed partition starts with words `0x4c544d49, 1, 0x1000, 1, 0x5000, 0x1000, 0x20000`.
The partition contains `LOG_START-0000000001` at byte 4098.
It also contains `media_init:3` at byte 10235 and `boot_wdog_disable_check` at byte 10265.
These offsets are relative to the partition start at LUN-four LBA 712206.
The original firmware created both the header and the stream through actual UFS writes.
Its next failure is the DDR status read at `0x0c2f0004`.
Static coverage here does not prove the absence of a later device failure.

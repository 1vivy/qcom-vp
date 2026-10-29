# Hardware fact gathering

## Scope

Hardware research resolves model questions that firmware analysis cannot answer.
The reference evidence in this repository comes from an SM8850 phone, called the Infiniti, that the operator selects for the probe.
Any device with a similar memory probe can supply the same kind of evidence.
This includes Gobbl peek/poke, existing probe records, and crashdump collection.
The reference device runs Gobbl in its installed `efisp`, and `reboot bootloader` enters its fastboot endpoint.
Check the endpoint after each transition. Historical device state does not establish current state.

This workflow is separate from platform composition.
The virtual platform, builds, tests, and normal runs do not require a phone.
Hardware observations inform model evidence. They do not replace virtual execution proof.
Research permission does not authorize flashing, partition writes, GPT changes, or physical slot selection.
Device writes use the Bash approval boundary.

The [hardware probe campaign](hardware-probe-plan.md) defines staged passes, current priorities, and persistent-change exclusions.

## Existing tools and records

Each link pins a commit. The [reference manifest](../../../../references.toml) records evidence identities by store name and SHA-256.
A local source root is configuration, not a tracked default.
Bind it with the store's `QCOM_VP_REF_*` environment variable, or supply an explicit private `--manifest` whose store `root` resolves relative to that manifest.
The [evidence guide](../../../../evidence.md) defines those rules.
The linked projects own their tool contracts. This page does not define another command framework.

| Resource | Purpose |
| --- | --- |
| [Gobbl memory research](https://github.com/1vivy/gobbl/blob/ca55c5a5920685ea3f9528313c6b752b685f0bf3/docs/platform/qcom.md#generic-memory-research) | Command syntax, address semantics, widths, barriers, and failure limits |
| [Endpoint qualification](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/docs/device-results.md#generic-memory-research-endpoint-installed-2026-10-15) | Actual EL1 context and scratch-page comparisons |
| [Lab execution boundary](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/README.md#device-execution-and-the-permanent-safety-boundary) | Journaled commands and protected operations |
| [Session implementation](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/lab/phone/session.py) | `DeviceSession`, `Exec`, and `Journal` |
| [Record helpers](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/lab/records.py) | Record directories and `file_sha256` |
| Installed endpoint journal `gobbl-lab:records/20261015T033517Z-phone-research-endpoint/evidence/installed/commands.jsonl sha256=7e8abd30d02e1e30b1755a3742afff356ef138548c73e5296a1ee1cc99053bd2` | Exact commands, results, and capture references |
| [Firmware atlas](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261015-firmware-atlas/record.md) | Captured firmware regions and provenance |
| Privileged probe transcript `gobbl-lab:records/20261015-privileged-platform-probe/evidence/commands.txt sha256=56b35f632bb6c0e01891938448a53c609023fc3eff192611bd1ad179dac78bb0` | Existing context, image, and reserved-memory reads |
| [Small capture example](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261015T131413Z-phone-variable-store-fault/instrument/qcom-gpt-listener-objects.py) | Existing paired peek and upload pattern |

The lab journal records arguments, stdout, stderr, timestamps, exit status, and timeout status.
It does not serialize independent operators through a device lock.
Use one device operator for each experiment. Do not run independent device probes concurrently.

## Gobbl access semantics

Gobbl uses current CPU virtual addresses, not an implicit physical mapping.
`context` reports the current exception level and its translation registers.
A prior boot's address or translation table is not a reusable address guarantee.

| Command | Result |
| --- | --- |
| `fastboot -s "$SERIAL" oem surfacer context` | Current execution and translation context |
| `fastboot -s "$SERIAL" oem surfacer peek "$VA" "$BYTES" "$WIDTH"` | Exact-width loads into staged output |
| `fastboot -s "$SERIAL" get_staged "$OUTPUT"` | Raw captured bytes on the host |
| `fastboot -s "$SERIAL" oem surfacer poke "$VA" "$VALUE" "$WIDTH"` | One scalar store |
| `fastboot -s "$SERIAL" stage "$INPUT"` | Download bytes for a subsequent operation |
| `fastboot -s "$SERIAL" oem surfacer poke "$VA" download "$WIDTH"` | Stores from the current download |

Read the target serial from `fastboot devices`. Check the current target before each command.
Widths are 1, 2, 4, or 8 bytes. The default width is one byte.
The address and length must align to the width. Peek accepts at most 1 MiB.
Both operations execute `DSB SY` before and after access.
Neither operation changes mappings, cleans data caches, or synchronizes instruction caches.
Poke performs no automatic readback.

An invalid address or MMIO access can fault, hang, reset, or change hardware state.
Gobbl provides no exception recovery for these commands.
Do not treat a read as harmless merely because it does not execute a store.
A successful store proves neither peripheral completion nor visibility to another observer.

A successful peek replaces the previous provider capture.
GBL staged kernel, ramdisk, FDT, or trace output takes precedence until consumed.
Check the command response before interpreting an upload. Check the captured length and hash before recording a finding.

## Experiment procedure

1. State the model question and the smallest observation that can resolve it.
2. Read the existing probe records and firmware evidence for the target.
3. Check the device identity, Gobbl endpoint, firmware identity, and current execution context.
4. Establish the virtual address, access width, ownership, and possible read or write effects.
5. Check the crashdump catcher before a potentially faulting access.
6. Execute one bounded experiment through the existing lab journal.
7. Preserve the command results, raw capture, byte count, SHA-256, and boot context.
8. Associate any failure with its USB transition and crashdump directory.
9. Record the supported conclusion and its limits in the model's evidence file.
10. Check the resulting model change on the virtual platform.

Do not repeat an unsuccessful probe without changing the hypothesis or obtaining new evidence.
Do not reuse historical scratch addresses after their allocations end.
For a write experiment, define ownership, expected effects, and restoration before the write.
Readback can itself affect MMIO. Use only a justified observation to check the effect.

The 2026-10-15 endpoint qualification proves operations on an owned RAM page at EL1.
It does not qualify arbitrary MMIO, protected memory, instruction patching, or fault recovery.
The earlier reviewed records did not establish phone observations for the CPUCP cluster register questions.
The 2026-10-25 probe below adds an access failure, not a register value.

## Crashdump catcher

The reference host uses this capture path:

| Component | Location or behavior |
| --- | --- |
| Udev rule | `/etc/udev/rules.d/70-qcom-900e-autocapture.rules` |
| Trigger | USB device addition with VID:PID `05c6:900e` |
| Service | `qcom-900e-capture@.service` |
| Helper | `qcom-900e-capture`, installed under a local bin directory |
| Collector | `qcom-crashdump/qdl ramdump --debug`, installed under a local libexec directory |
| Output root | The `crashdumps` store named in the [reference manifest](../../../../references.toml) |
| Exclusive capture | `.capture.lock`, plus checks for competing `qdl` or `edl memorydump` processes |
| Evidence | `capture.log`, `host-metadata.txt`, `files.tsv`, `sha256sums.txt`, and captured regions |

Do not start another collector while the catcher owns the device.
A trigger applies only to the documented USB enumeration. It does not guarantee recovery from every hang or reset.
Use `load.cmm` and the capture log to interpret region addresses and lengths.
A captured RAM buffer that contains register values is not the original MMIO aperture.
Product firmware encrypts some regions. Do not interpret encrypted bytes as plaintext firmware state.

These host-only checks do not start a device capture.
`CAPTURE_HELPER` names the installed `qcom-900e-capture` executable.

```sh
"$CAPTURE_HELPER" --dry-run
systemctl show systemd-udevd.service --property=ActiveState --property=SubState
systemctl cat qcom-900e-capture@.service
```

On 2026-10-25, the dry run passed and `systemd-udevd` reported active/running.
The retained capture `20261015T093650Z-900e-2` passed `sha256sum --check --quiet sha256sums.txt`.
These checks establish host configuration and retained-file integrity, not a new capture or current phone state.

## Evidence separation

Keep raw firmware bytes, memory captures, and crashdumps outside Git.
Commit protocol facts, register observations, provenance, and limitations in the relevant model evidence file.
Include the firmware identity, boot phase, exception level, address interpretation, width, and command result for each hardware claim.
A Gobbl EL1 observation after OEM initialization is not a reset value or proof of CPUCP access permissions.
A failed access does not establish that a register is absent.
A hardware observation does not establish either virtual kernel handoff.

## Infiniti fabric access failure, 2026-10-25

The journal is record [`20261025T090340Z-phone-qcom-vp-context`](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261025T090340Z-phone-qcom-vp-context/record.md).
Gobbl returned EL1 context with TTBR0 `0xc6e8d000`.
Exact-width reads of the VBAR page and three translation descriptors succeeded.
The decoded descriptors describe an identity-mapped Device block containing `0x19830320`.
This interpretation assumes identical virtual and physical addresses for the inspected table pages.
It does not establish stage-two permissions.

A single eight-byte read at `0x19830320` lost the USB connection and returned no register value.
The catcher completed `20261025T090643Z-900e-2` with status zero.
Its complete SHA-256 check passed.
The captured UEFI log did not provide a readable fault report through a printable-byte inspection.
The exact fault syndrome and protection mechanism remain unknown.

Do not repeat this access from the same context without new evidence.
Do not infer register absence or a reset value from the failure.
The model still requires the firmware-derived CPUCP and secure APSS contracts.

## Passive RAM and GIC pass, 2026-10-25

The journal is record [`20261025T093009Z-phone-qcom-vp-passive-baseline`](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261025T093009Z-phone-qcom-vp-passive-baseline/record.md).
Its raw captures stay in the untracked `evidence` directory of that record.
The Android baseline identified CPH2747 with slot `_a` and boot reason `bootloader`.
The authorized reboot entered Gobbl at EL1 with TTBR0 `0xc6e87000`.
The reported bootloader label was `gbl.eng.vivy.2026-10-13`, not an artifact hash.

The existing atlas probe captured a fresh memory classification and interface census.
The capture contains 3,180,806 bytes with SHA-256 `d76886cb296180ec60a7ddcc9ad4933d665e323ddeb093184ddf5d88f9c97953`.
Its paired cleanup returned success with complete restoration and removed its private volatile marker.
The original capture and cleanup binaries match their previously qualified hashes.

### Shared RAM

The current stage-one entry for CPUCP RAM at `0x81210000` was invalid.
The experiment did not access that target or change its mapping.
The SMEM target pages had normal noncacheable mappings.
The reserved HYP page had a normal cacheable mapping.
The bounded reads below succeeded.

| Structure | Observation |
| --- | --- |
| SMEM directory at `0x81eff000` | `$TOC`, version one, 20 entries |
| APSS/SOCCP partition | Hosts `0/19`, offset `0xa6000`, size `0xa000`, cache line 32 |
| Global partition | Hosts `65534/65534`, offset `0x1000`, size `0x91000` |
| Global used extent | `0x192b0`, with 25 valid allocation records ending at that cursor |
| DDR item `0x25c` | Payload size `0x898` |
| DDR item `0x25b` | Payload size `0x368` |
| RAM partition item `0x192` | Payload size `0x918` |
| HYP information at `0x80d60000` | Version one, two valid DTB overlays, Gunyah VMID three |
| RM transport declarations | 240-byte messages, queue depth eight |
| Queued SCM declarations | 35 queues, interrupt tuple `(0,0x3c2,1)` |

These observations support the existing allocation formats and the APSS/SOCCP partition size.
They do not establish atomic snapshots or actual RM message execution.
The current RM capability values differ from the earlier capture and must not become model constants.

### Non-destructive GIC identity

The experiment read architectural identity fields without changing interrupt controls.
The first distributor target had a stage-one Device mapping.
All listed target reads returned their requested byte counts.

| Register | Address | Width | Captured value |
| --- | --- | --- | --- |
| `GICD_PIDR2` | `0x1700ffe8` | 4 | `0x30` |
| `GICD_TYPER` | `0x17000004` | 4 | `0xf96b011f` |
| `GICD_IIDR` | `0x17000008` | 4 | `0x47000070` |

`PIDR2` reports architecture revision three.
`TYPER` reports 32 interrupt groups and LPI support.
The architectural range still reserves the final special INTIDs.
This EL1 view can reflect hypervisor mediation.
It does not establish the secure boot view or reset identity.

The eight observed redistributors start at `0x17080000` with stride `0x40000`.
Each capture reads the 64-bit `GICR_TYPER` at offset eight.

| Slot | Affinity | Processor number | Last |
| --- | --- | --- | --- |
| 0 | `0x0` | 0 | 0 |
| 1 | `0x100` | 1 | 0 |
| 2 | `0x200` | 2 | 0 |
| 3 | `0x300` | 3 | 0 |
| 4 | `0x400` | 4 | 0 |
| 5 | `0x500` | 5 | 0 |
| 6 | `0x10000` | 8 | 0 |
| 7 | `0x10100` | 9 | 1 |

The topology supports the existing six-plus-two affinity layout and sparse redistributor stride.
The distributor revision and LPI fields differ from the current virtual profile.
Do not copy them into the CPH2749 secure boot profile without checking its consumers and firmware cohort.
No model behavior changed from this CPH2747 observation.

### Write and recovery boundary

No selected question justified a poke.
The experiment issued no unknown register writes and did not repeat the failed fabric access.
It issued no flash, partition write, slot change, fuse operation, or persistent variable change.
The atlas used owned RAM and its private volatile marker only.
Normal firmware logging and reboot behavior remain outside a promise of zero durable changes.
No crash occurred in this pass. The final Gobbl context command succeeded.

## Targeted queue and PLL follow-up

The follow-up ran after the passive pass, and the operator selected the targets before each command.
Its commands and captures continue in the same journal under distinct names.
The initial follow-up context retained TTBR0 `0xc6e87000`.

The APSS/SOCCP partition at `0x81da6000` has a used extent of `0x8238` bytes.
Its bounded snapshot contains these allocation payloads:

| Item | Size in bytes | Interpretation |
| --- | --- | --- |
| 478 | 32 | Shared GLINK descriptor |
| 480 | 16384 | Remote FIFO allocation |
| 617 | 340 | Additional shared item, not decoded here |
| 607 | 80 | Additional shared item, not decoded here |
| 479 | 16384 | APSS FIFO allocation |

The descriptor's first four words are `0x9b8`, `0x9b8`, `0x3608`, and `0x36c8`.
The remaining four words are zero.
The indices fit the captured FIFO sizes. This does not prove completion, queue ownership, or an atomic snapshot.
These observations support the existing descriptor and FIFO sizes without requiring model changes.

Offline inspection of the first fabric-failure dump found an entirely zero CPUCP log buffer.
Its CPUCP RAS region contains a structured header without a decoded fault.
The CPUCP DDR regions did not yield interpretable firmware state.
Opaque contents alone do not establish encryption or a fault cause.

The auxiliary PLL at `0x19aa1000` had stage-one descriptor `0x60000019a00401`.
The original CPUCP function reads MODE at `0x1d80dd42` and later polls that word.
One 32-bit Gobbl read lost USB and returned no value.
The host recorded USB removal at 09:56:25 UTC.
No adjacent offset, alternate width, second PLL, or write followed.
Stage-one mapping and CPUCP access do not establish nonsecure APSS access.
The exact fault cause remains unknown.
The catcher completed `20261025T095642Z-900e-2` with status zero.
Its complete SHA-256 check passed.
The phone subsequently returned to Android on slot `_a` with boot reason `bootloader`.
The experiment issued no recovery reboot or repair command.
The plaintext Surfacer log ends with `oem surfacer peek 0x19aa1000 4 4`.
It contains the preceding successful RAM reads and uploads but no later upload or decoded exception.
This associates the captured firmware session with the failed PLL command.
The CPUCP RAS region matches the earlier fabric-failure dump byte for byte.
It therefore provides no distinct decoded CPUCP fault for this experiment.
The UEFI, TZ, and HYP diagnostic regions did not yield readable exception reports.
The inspection recovered no ESR, FAR, ELR, or specific access-denial mechanism.

The experiment did not probe runtime controls at `0x17c30020`.
The original firmware accesses those words, but their read semantics require further evidence.
The experiment requested no unknown register write or persistent configuration change.


## Exception capture readiness

The plaintext Surfacer region already exists in the collected dumps.
The `minidump arm` command registers that region but does not capture exception registers.
The lab shadow copies the UEFI log at ExitBootServices, before its faulting experiment.
That earlier snapshot cannot include a later exception report.
The collector record `md_MDMP_LOG.BIN` also yielded no readable fault report.

One proposed option was a minidump or a redirect into the Gobbl log.
The next experiment therefore tested capture readiness without another uncertain MMIO access.
Its source, EFI images, and responses remain in the existing lab journal.
The `el1-fault-*` and `el1-proof-*` files identify these artifacts.

The temporary EFI instrument requested a synchronous exception callback through `EFI_CPU_ARCH_PROTOCOL`.
It allocated two reserved RAM pages at `0xb1000000`.
It refused to replace an existing handler or patch a vector.
It planned to publish its own plaintext region only after successful callback registration.
Its controlled `BRK` instruction also required successful registration.

The live registration returned `EFI_UNSUPPORTED`, value `0x8000000000000003`.
The retained result has magic `0x3150435845505651`, version 1, stage 2, and CurrentEL `0x4`.
The result file is `el1-fault-proof-v2-result.bin`.
Its SHA-256 is `817e333d1b8cefee6e7ccd66d2870ef65b6be8183973e2e285b563d0da0ae50f`.
The instrument therefore executed neither the controlled exception nor the minidump publication.
An older inventory established protocol presence, not successful exception registration.

The root table was at `0x81d0a9e8`.
Candidate slot 28 remained entirely zero after the experiment.
The cleanup application released the reserved pages.
The subsequent read found the firmware's `0xaf` pattern in the result area.
The final context retained TTBR0 `0xc6e87000` and VBAR `0xc6804000`.
The phone remained in Gobbl fastboot.

A working exception hook remains necessary for a direct EL1 syndrome record.
The shared serial library offers another possible redirect, but its connection to the firmware exception logger remains unproven.
A live alias of the UEFI log would share bytes that the vendor can encrypt during collection.
The existing private snapshot avoids that alias but does not solve capture timing.
No firmware handler, vendor descriptor, secure dump policy, or persistent configuration changed in this readiness experiment.

Even a working EL1 hook cannot record an exception that never reaches EL1.
An EL2 or secure-world reset can leave only the command and its earlier intent record.
The two failed MMIO reads still have no recovered syndrome.

## Guarded cluster watchdog read

The next experiment replaced the firmware vectors only while one temporary EFI application ran.
The application masked interrupts and restored VBAR and DAIF before it returned.
It did not patch firmware code or use `EFI_CPU_ARCH_PROTOCOL`.

The software proof executed `BRK #0x517`.
Its private record captured ESR `0xf2000517` and the exact ELR.
It restored VBAR `0xc6804000` and DAIF `0x300`.
The application published `QVP_EL1_VECTOR` in plaintext minidump slot 28.

The deliberate Gobbl crash produced capture `20261025T105416Z-900e-2`.
The catcher completed with status zero, and its complete SHA-256 check passed.
The first 224 bytes of `md_QVP_EL1_VECT.BIN` match the uploaded record byte for byte.
This result proves the fault-record path before the MMIO experiment.

The guarded 32-bit load from `0x17c30020` captured ESR `0x96000010`.
FAR was `0x17c30020`, and ELR identified the exact load.
DFSC `0x10` identifies a synchronous external abort.
The handler skipped the load and returned to the application.
The `Value` field equals the input address because the load did not complete.
It is not a register value.

The cleanup application released both reserved pages and cleared slot 28.
The final context retained VBAR `0xc6804000` and DAIF restoration was exact.
The experiment issued no MMIO write.
APSS EL1 therefore cannot read this CPUCP watchdog word in the tested context.
The result does not establish the physical reset value or watchdog behavior.

## QSEE control and debug probes

The 2026-10-25 probes used the installed Gobbl endpoint at EL1.
The context used current CPU virtual addresses and an enabled MMU.
Each probe used aligned loads and no store.

| CPU virtual address | Width | Result | QSEE consumer |
| --- | --- | --- | --- |
| `0x01fb300c` | 4 bytes | `0x00000001` | `0x15682af0` |
| `0x01fbc00c` | 4 bytes | `0x00000000` | `0x15682b90` |
| `0x310ba29c` | 8 bytes | `0x00000139, 0x000001dd` | `0x15770878` |

The capture includes the word at `0x01fb300c`.
Its physical register name lacks independent support in the [TME provenance review](../../provenance-tme.md).
QSEE retains the low 17 bits from both DDR words.

The 32-byte probe at `0x14680000` read pointer `0xaa980000` at offset `0x10`.
The virtual run instead reached QSEE with value minus one at this offset.
QSEE faulted when it read offset four through that invalid pointer.

The 64-byte probe at `0xaa980000` found a nonzero word at offset four.
Native run380 later identifies this pointer as fatal debug state, not a UEFI handoff result.
The captured words do not authorize a copied data structure.

The raw captures are outside Git in the `qcom-vp-probes` store:

| Capture |
| --- |
| `qcom-vp-probes:20261025T141413Z/infiniti-1fb300c.bin sha256=67abdd721024f0ff4e0b3f4c2fc13bc5bad42d0b7851d456d88d203d15aaa450` |
| `qcom-vp-probes:20261025T141413Z/infiniti-1fbc00c.bin sha256=df3f619804a92fdb4057192dc43dd748ea778adc52bc498ce80524c014b81119` |
| `qcom-vp-probes:20261025T141413Z/infiniti-310ba29c.bin sha256=8a1dbb57ab03868754a6a34e4031a30cc871fd96fcfa5f215ed9d3132c12632e` |
| `qcom-vp-probes:20261025T141413Z/infiniti-14680000.bin sha256=6377f02df9dfb4ea5b4032dcaa67a929a0b7a685466d8473cad8dbd69705bec5` |
| `qcom-vp-probes:20261025T141413Z/infiniti-aa980000.bin sha256=4936de8764285fc5c805320999a480ccbe18d192664050dca7ec8a0e4ea49f28` |

The phone returned to Android after the probes.
These post-initialization values do not establish silicon reset values.

## GPCE version access, 2026-10-25

Native run387 reads the GPCE version register at `0x01dfa000`.
The hardware question was its version value, not a cryptographic result.
The journal continues in record [`20261025T093009Z-phone-qcom-vp-passive-baseline`](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261025T093009Z-phone-qcom-vp-passive-baseline/record.md), commands 105 through 123.

Android identified CPH2747 with fingerprint `OnePlus/CPH2747/OP611FL1:16/BP2A.250605.015/B.R4T3.1abf132_aeee51_aea830:user/release-keys`.
Gobbl reported `gbl.eng.vivy.2026-10-13`, EL1, TTBR0 `0xc6e8d000`, and TCR `0x480803514`.
The captured stage-one walk ends at descriptor `0x0060000001dfa403`.
This descriptor maps `0x01dfa000` identically with Device attributes.
The walk does not establish stage-two or peripheral access permission.

One aligned four-byte peek at `0x01dfa000` timed out after twenty seconds.
The probe returned no register value, and the experiment did not repeat it.
No new crashdump accompanied the observation.
This probe does not establish the register version or the cause of the failure.

The subsequent fastboot reboot command failed because the device disconnected.
The Android endpoint then returned.
At `2026-10-25T21:31:00Z`, `sys.boot_completed` returned one.
The experiment issued no register write, flash command, partition write, or slot change.
The later Android `dmesg` command returned permission denied.

### Retained Android version evidence

The earlier log `gobbl-lab:records/20261012T090758Z-phone-loop/evidence/63-avc-dmesg.stdout sha256=21b24cd2ff320938126d32c1e7f75efb8777b89fcf0d0e9ce567d58c95109991` identifies the crypto core.
Line 1704 reports `qce 1de0000.qcedev: QTI Crypto 6.0.0 device found @0x1de0000`.

The [downstream QCE header](https://github.com/daimile2/hyperos-kernel-for-renoir/blob/miui-t-hyperos/drivers/crypto/msm/qcryptohw_50.h) places `CRYPTO_VERSION_REG` at offset `0x1a000`.
This offset gives `0x01dfa000` from the recorded device base.
The [public QCE field definitions](https://github.com/torvalds/linux/blob/master/drivers/crypto/qce/regs-v5.h) encode version 6.0.0 as `0x06000000`.

This value comes from an Android driver log, not a raw register capture.
The record identifies CPH2747, not a separate CPH2749 unit.
The exact driver module source was not available for this comparison.
The evidence does not establish crypto capability bits, secure-view identity, or completed cryptographic operations.


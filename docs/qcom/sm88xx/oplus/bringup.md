# Execution evidence

## Status

The 2026-10-29 catalog migration preserved SM8850 kernel entry, debugger detach, and early kernel UART output.
The next unsupported access still reads the timer at `0x17810008`.
See the [current environment record](../../../environment-inventory.md#catalog-migration-verification-2026-10-29) for the inputs, observation policy, and proof.
SM8845 kernel execution remains unverified and is not a required milestone for that reference.

### Historical kernel evidence, 2026-10-27

SM8850 run921 reaches the genuine OEM kernel and emits 4,573 kernel UART bytes.
The observer verifies the complete kernel hash, the first instruction, and debugger detach.
The kernel identifies itself as `6.12.23-android16-5-gb3b66ace21e0-ab14672634-4k`.
Its next unsupported access reads the timer at `0x17810008`.
This result proves kernel entry and early-console output, not a complete Android boot.
SM8845 kernel execution remains unverified. The project does not include an ODP lane.

The retained outcome is `artifacts:2026-10-27-kernel-uart/result.json sha256=8bf1dd46de6048ab59b0f97045caaf4169652eb94c60bef409d6c8b9131b8ecf`.
The entry record is `artifacts:2026-10-27-kernel-uart/kernel-entry.json sha256=6d408afd656929d98927fff8ea260d2b48f5af271c797c771a67fac08d62e36e`.
The provenance record is `artifacts:2026-10-27-kernel-uart/provenance.json sha256=fe9e71f8815a626985f5a0cf2f4611a1376603e7cd4566170c5368a6df92f1ce`.
The source run is `build/private/run-sm8850-common-runtime921/`.
Its `result.json` reports `kernel_entry=true`, `detach_confirmed=true`, and `run_succeeded=false`.
The final false value records the later unsupported timer access.
The [UART evidence](../../../../models/qcom/uart/evidence.md#real-sm8850-kernel-output) records the model change and proof.

The OEM preparation profile selects `omit_secdata = true` when the package lacks `IMAGES/sec.dat`.
Both reference manifests use that profile and record the omitted XML entry.
No physical-device access occurred.
Authentication remains `retail`, with qualification explicitly unestablished.
The omission is an explicit input limit. It does not change the authentication path.

## Artifact retention

The private artifact directory uses a 64 GiB limit for retained storage.
Enable the policy explicitly:

```sh
build/install/bin/qcom-vp retain --root build/private --limit-gib 64
```

The policy keeps the newest complete preparation and newest completed run for each platform.
It removes obsolete, manifest-listed LUN images.
Pruned outputs retain their manifests and a record that they are no longer runnable.
UART and console logs remain readable.
The policy compresses closed system and instruction traces larger than one MiB with `zstd`.
It verifies the restored SHA-256 and byte length before it removes an original log.

The `prepare` and `run` commands enforce the enabled policy before allocation and after exit, including failed runs.
Active guards protect prepared inputs and running outputs from cleanup.
Roots without a policy remain unchanged.
Unknown files and source firmware are not cleanup targets.
Install `zstd` before enabling retention.

The limit counts allocated file blocks, including repeated references to shared extents.
This conservative retained-storage budget is not a filesystem quota.
Active writes can temporarily exceed it.
The command fails if protected, active, incomplete, or unknown data prevents compliance.

Read an archived trace without restoring another permanent copy:

```sh
zstd -dc build/private/run-sm8850-common-runtime96/qemu-apss.log.zst
```

Verification used SM8850 run97 and preparation17.
The run retained the existing clock failure and reported no kernel entry.
Post-run cleanup retired run96's six LUN images.
Successful preparation17 retired preparation16's six LUN images.
Both operations preserved the current SM8845 preparation and run.
The complete build directory then occupied 47 GiB by `du`, below the configured artifact budget.

## Build and host checks

These commands passed:

```sh
make build
cargo clippy --locked --all-targets -- -D warnings
cargo test --locked -- --include-ignored
```

The final suite reported:

```text
test result: ok. 47 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
test result: ok. 44 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out
```

The next DDR changes also passed all 91 tests.
SM8850 run99 and SM8845 run92 completed five measurement rounds across all four MC banks.
Both then rejected a SHRM trigger at `0x31076064`.
Neither run reached the kernel.

`make test` builds the installed platform and executes the otherwise ignored guest regressions.
The guest programs use Clang, LLD, and `llvm-objcopy`.
They exercise the actual QBox executable, not mock model interfaces.

| Guest regression | Observed behavior |
| --- | --- |
| Shared executable RAM | ARM replaces previously executed RISC-V code. RISC-V observes the replacement without `fence.i`. |
| Transaction security | Secure and nonsecure ARM execution observe different GIC register views. |
| Shared timer | Frames share counter state. Signed deadlines, counter stop, and access restrictions pass. Both frames deliver real FIQs through the GIC. |
| UFS | Real media I/O, boot selection, DMA, IRQs, supply loss, controller reset, device reset, and SCSI reset attention pass. |
| SMMU | Nonidentity DMA, split pages, wide addresses, dedicated context IRQs, fault masks, and first-fault retention pass. |
| Memory protection | Permission changes invalidate old mappings. A write fails after the guest loses permission. |
| CPUCP reset | Interrupts do not resume a ceased core. A real reset resumes execution at the published vector. |
| PLL lifecycle | Shared votes control separate PLLs without copied configuration. Reset, outputs, RCG updates, branch gates, muxes, and source loss control availability. |
| CPU-domain power | Source dependencies and configuration control real CPU reset. Both lanes and per-core reset transitions pass. |
| GENI UART | The guest transmits exact binary bytes and receives two real GIC interrupts. Partial transfer and cancellation checks pass. |
| TME state | Incomplete headers and invalid queue counts cannot publish readiness. Header removal revokes initial readiness. Software-state aliases preserve independent banks. |
| Sequencer authentication | Real P-521 signatures and a program digest control success. Changed data, invalid chains, and missing trust fail. |
| Native clock epochs | ARM and RISC-V exchange two requests after initial and later debugger pauses. The shared timer continues to advance. |
| Sequencer SHA-384 | Split submissions produce standard digests from DMA bytes and serialized state. Invalid descriptors and phases fail. |
| Sequencer P-384 | Inline and DMA digests preserve real signature verification. Changed digests, signatures, and invalid keys fail. Later valid requests clear the error status. |
| VMID mapper | Independent security banks, context bounds, and capability-preserving updates pass. |
| XPU namespaces | Foreign locks survive cross-socket requests. Missing authority and nonsecure requests fail. Finite aperture limits preserve protected writes and neighboring access. |
| Sequencer memory clear | Real secure DMA clears unaligned ranges across chunk boundaries. Neighboring bytes remain unchanged. Invalid frames and denied writes cannot complete. |

The UFS interrupt regression first exposed a lock cycle between two QEMU instances.
The corrected bridges borrow foreign handles instead of copying them under the other instance's lock.
The final guest reaches its success terminal and preserves the expected media contents.

An earlier concurrent suite exceeded the UFS guest's ten-second host deadline.
The isolated UFS smoke completed in 1.53 seconds.
The current full suite passes with the default concurrent execution.
The SHA-384 helper uses deprecated OpenSSL context functions because EVP does not expose the required chaining state.
The build reports those warnings without suppression.

The build installs `qcom-vp` and `qcom-vp-system` together.
It builds the pinned AArch64, RV32, and RV64 libqemu targets.
The SystemC executable uses C++17, QBox's 1 ms quantum, and the upstream `sc_start()` lifecycle.
The CLI enforces the host timeout.
Do not replace the installed executable during a run.

## OEM preparation

These commands passed with the local manifests:

```sh
build/install/bin/qcom-vp prepare --firmware build/private/sm8850.toml --out build/private/prepared-sm8850-rooted
build/install/bin/qcom-vp prepare --firmware build/private/sm8845.toml --out build/private/prepared-sm8845-rooted
```

Each preparation checked both GPT copies and compared selected source extents with the resulting LUN files.
Prepared schema version 2 records boot capability and provisioned boot IDs separately.
The board files explicitly select boot ID one as virtual reset policy.
Neither provisioning capability nor the Android slot determines that selection.
Both profiles include the checked sequencer issuer key and the approved entitlement digest.
Each profile also supplies its distinct checked OEM root certificate.
The [sequencer evidence](../../../../models/qcom/sm88xx/tme_mailbox/evidence.md) and [fuse evidence](../../../../models/qcom/sm88xx/fuse_controller/evidence.md) record their provenance.

The SM8850 kernel reference contains 39,889,408 bytes from `IMAGES/boot.img`.
Its SHA-256 is `2609ff5297e2716fd061daf3f51661b4ea068ed96cd17a320bef32d7d3d71393`.
This reference supports host comparison. The platform does not load it directly into guest memory.

### Optional common runtime reconstruction

The installed CLI accepts the optional `[tme_runtime_donor]` table from
[`firmware/qcom/sm88xx/oplus/example.toml`](../../../../firmware/qcom/sm88xx/oplus/example.toml).
It requires the pinned original TME and donor hashes.
It supports `sm8850` and `sm8845`, with any board label.
The SM8845 profile requires `rom_compatibility = true`.
Without this table, preparation retains the existing ROM and patch behavior.

The platform build assembles the authored adapter with Clang for `rv32imac_zicsr` and the `ilp32` ABI, with relaxation disabled.
LLD links `.text` at `0x234c00`, with `runtime_entry=0x234a00` and `runtime_oem_resume=0x2309a8`.
`llvm-objcopy` extracts only `.text`.
Installation places `tme-runtime-adapter.bin` in `share/qcom-vp/firmware`, relative to the installation prefix.
The CLI embeds the public relocation recipe and API map.
The recipe holds offsets and relocation kinds. It holds no vendor byte and no hash of a short span.
It needs no Python program, private build file, or compiled private path.

Preparation applies generic patches in manifest order, then reconstructs TME.
Each generic patch checks its complete input hash and its changed span.
Reconstruction rejects any modified TME preimage, incorrect donor, or incorrect adapter.
PBL patches remain independent.
The reconstructed image preserves the original first `0x16000` bytes.

Preparation retains these local files:

| File | Purpose |
| --- | --- |
| `tme.bin` | Exact reconstructed ROM used by the platform |
| `tme-original.bin` | Unchanged original TME input |
| `tme-runtime-donor.bin` | Checked private donor |
| `tme-runtime-adapter.bin` | Checked authored adapter |
| `tme-runtime-api.map` | Exact checked mapping used by the platform |
| `inputs.json` | Resolved manifest input paths and SHA-256 identities |
| `prepared.json` | Local artifact paths, hashes, and reconstruction provenance in `tme_runtime` |

The provenance records both input hashes, the output hash, the adapter hash, the recipe hash, and the API-map hash.
It also records the evidence hashes, retained prefix length, runtime coordinates, and relocation counts.

| Artifact | SHA-256 |
| --- | --- |
| Original | `e5485515dde2da7b12ef0c06f1e5af8e1a56b693af2c49d43b68abd78db3deb3` |
| Donor | `94059e1c3ba90f7112fa600a7033d83aa71cee6ff5573219d72b63c69c7b67d7` |
| Adapter | `f2ee4872b42bdad1fedd4087eaa67d4bfe2c18164bf9fbffbc5bedc38fb977fb` |
| Reconstructed ROM | `76c35cabc4705565d5d08f0a393de761978e316d72948b27d233ff12605d0893` |
| API map | `ac79dbebcaf21de7cfe1202dae14f8563cf9f9a4fd711ba282c456adcc1ae31c` |
| Recipe | `741546aebf7e3170c495cf7e3457bb755b7973be2b740a1f4cacdc5d709baf3e` |

Run checks all retained artifact hashes and reconstructs the expected provenance before launch.
It rejects a donor manifest without reconstruction metadata.
It copies the checked artifacts into the run directory.
It passes the local `tme.bin` path as `options.tme` and the local map path as `options.tme_runtime_api_map`.
The map option enables the common runtime composition and the selected board data.
Authentication configuration, mutable LUN copies, tracing, and debugger options retain their existing behavior.

The reconstruction proof establishes byte identity, not authentication qualification or a kernel boot.
The two execution modes below retain distinct kernel-entry and continuation contracts.

## Kernel entry and continued capture

Select exactly one execution mode. These examples describe the CLI contract, not a completed kernel run.

```sh
build/install/bin/qcom-vp run --prepared build/private/prepared-sm8850-rooted --out build/private/entry-stop --stop-at kernel-entry --timeout-seconds 180
build/install/bin/qcom-vp run --prepared build/private/prepared-sm8850-rooted --out build/private/continued-capture --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
```

`--stop-at kernel-entry` preserves the entry-stop contract.
The observer verifies the first completed kernel instruction, then keeps APSS paused.
The host stops and reaps the platform before it closes that debugger connection.
This mode does not transmit GDB detach.

`--continue-after kernel-entry` requires the automatic APSS observer.
It cannot use a nonzero `--apss-gdb` port or `--debug-pause`.
After the same entry proof, the observer transmits GDB `D` and requires `OK`.
The host then retains the platform until process exit or the original execution deadline.
The host sets the deadline immediately before process creation.
The deadline includes entry observation.
Continuation does not receive a separate timeout budget.

Both modes retain the same proof requirements:

- The selected package supplies the kernel identity and expected hash.
- The observer follows actual UEFI and ABL execution through the hash-bound sites.
- ABL completes `ExitBootServices` and executes the kernel transfer.
- The loaded kernel bytes match the selected size and SHA-256.
- The FDT, entry registers, exception level, security state, MMU state, byte order, and interrupt masks pass the existing checks.
- Single-step execution completes the first kernel instruction without an execution-state change.

The [observer sites](observer-sites.md) document the binary coordinates.
No banner, host message, or UART file replaces these checks.
The observer does not write guest registers, memory, or the PC.
Image authentication remains unchanged.

### Stock OEM console option

`--enable-oem-uart` enables the stock Oplus UART record in the run's mutable LUN copy.
It does not change prepared media, signed images, fuses, authentication, rollback, or lock policy.
The default run leaves this record unchanged.

The host requires matching primary and backup GPTs and matching prepared partition identities.
It selects the unique `oplusreserve1` partition and requires at least `0x800` blocks.
The record occupies index `0x429` within the final `0x800` blocks.
Its absolute LBA is the partition's final LBA minus `0x3d6`.
Only its first 16 bytes become `ENABLE_UART:TRUE`.
The host preserves the remaining bytes, including other debug options.

The selected stock ABL images contain this interface:

| Board | ABL ELF SHA-256 | UART gate RVA | Enable-command RVA |
| --- | --- | --- | --- |
| SM8850 | `b3cd257aee11de8720fbf5557d52326d38bdf1e9176794d85c791efa4a714278` | `0x33810` | `0x1f238` |
| SM8845 | `83767a6f1b2ce098c6e4ebc60c546833652a923548f3b1d1cd737533f576b0bf` | `0x33740` | `0x1f158` |

Both gates compare exactly 16 bytes. They do not require a following NUL byte.
Their normal enabled branch appends `console=ttyMSM0,115200n8 earlycon`.
The option does not request factory mode, extended UART delay, or unrestricted kernel logging.
No physical fastboot command runs. Locked-device command admission remains unproved.

The SM8850 vendor command line contains `console=ttynull qcom_geni_serial.con_enabled=0`.
Its [exact GKI driver](https://github.com/aosp-mirror/kernel_common/blob/b3b66ace21e0/drivers/tty/serial/qcom_geni_serial.c) has no `con_enabled` gate.
It registers earlycon for `qcom,geni-debug-uart` and the normal `ttyMSM` console.
All 14 bundled SM8850 DTBs select that compatible at `0xa9c000` through `/chosen/stdout-path`.
All nine SM8845 DTBs make the same selection, and both packages carry the same vendor command-line console text.
The exact both-package gate evidence is in [the UART evidence](../../../../models/qcom/uart/evidence.md#both-oem-abl-to-linux-console-gates).
Actual kernel output must still establish console operation.

`oem-uart.json` records the LUN, absolute record LBA, and previous 16-byte prefix before firmware execution.
`result.json` retains the same configuration record, or null when the option was absent.
The SM8845 smoke run selected LUN five, LBA 1097.
A complete LUN comparison found only the 16 marker bytes changed, at byte offset 4493312.
This proves the configuration scope, not ABL execution or a kernel boot.


### Retained evidence and outcomes

The generated configuration uses `<out>/qemu` as its trace prefix.
The UART model writes accepted guest TX bytes to `<out>/qemu-uart.log`.
It flushes each accepted TX write.
The host does not replace, decode, trim, or remove this file on exit or failure.
The file can contain firmware output and kernel output.
Its existence or size does not establish a working kernel.
An absent file differs from an existing empty file.

`console.log` retains platform stdout. `system.log` retains platform stderr, including unmodeled-access diagnostics.
`loaded-kernel.bin`, `entry-fdt.bin`, and `kernel-entry.json` remain available after proof, including detach failure, timeout, or later child failure.
Incomplete observation can leave partial evidence files.
Their presence alone does not mean that the proof passed.

After process cleanup and observer completion, `result.json` separates these facts:

| Field | Meaning |
| --- | --- |
| `kernel_entry` | The complete entry proof passed. Later failure does not clear it. |
| `mode` | `stop-at-kernel-entry` or `continue-after-kernel-entry`. |
| `detach_confirmed` | The explicit GDB detach operation completed with `OK`. It does not prove another kernel instruction executed. |
| `termination` | `kernel-entry-stop`, `platform-exit`, `timeout`, `observer-failure`, `detach-failure`, or `process-error`. |
| `host_terminated` | The host sent a process termination request during cleanup. |
| `exit_code`, `exit_signal` | The reaped child status. A host kill is not a guest exit. |
| `observer` | Whether this run selected automatic observation. |
| `observation_time_policy` | `suspend-during-inspection` for automatic observation, otherwise `unchanged`. |
| `uart_log`, `uart_bytes` | The raw UART path and retained file size. The size is null if unavailable. Inspection errors fail the run. |
| `run_succeeded`, `reason` | The CLI outcome and accumulated failure details. Neither field asserts completed Linux boot. |

The host owns the child process and joins the observer thread.
On timeout or observer failure, it stops and reaps the child, then cancels and joins any pending observer.
It removes the private debugger socket only after the observer completes.
It retains a completed entry proof even when process exit and observer completion occur together.
The failure outcome still records the event that ended the run.

Entry-stop success requires the complete proof and no unexpected child or cleanup failure.
Continuation returns failure for timeout, detach failure, observer failure, nonzero child exit, or an unexpected termination signal.
A proven entry does not turn an unmodeled-access failure into success.
The current platform returns status two when `sc_start()` returns, including an unmodeled-access stop.
There is no guest kernel shutdown endpoint that establishes successful boot.
A future zero child exit would establish only successful process completion after detach, not working Linux.

The earlier canonical runs below exercise pre-entry observation and retained UART capture.
Those runs do not reach detach or post-entry execution.
The status section records later SM8850 kernel evidence. The earlier results do not describe the current status.

## Actual OEM scenario

Both preparations use the common donor and preserve their separate OEM roots:

```sh
build/install/bin/qcom-vp prepare --firmware build/private/sm8850-reconstructed.toml --out build/private/prepared-sm8850-common-runtime8
build/install/bin/qcom-vp prepare --firmware build/private/sm8845-reconstructed.toml --out build/private/prepared-sm8845-common-runtime7
build/install/bin/qcom-vp run --prepared build/private/prepared-sm8850-common-runtime8 --out build/private/run-sm8850-common-runtime13 --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
build/install/bin/qcom-vp run --prepared build/private/prepared-sm8845-common-runtime7 --out build/private/run-sm8845-common-runtime12 --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
```

Both preparations passed.
Both runs report `TME_SEQUENCER_AUTHENTICATED` and execute their unchanged OEM XBL images.
Both runs stop before the kernel and return CLI status one.

| Scenario | First rejected access | Retained UART bytes | Kernel entry |
| --- | --- | ---: | --- |
| SM8850 | TME PC `0x440f28`, authorization import read at `0x4169f4` | 3707 | False |
| SM8845 | TME PC `0x4411a6`, authorization import read at `0x4169f4` | 3675 | False |

The UART logs report the completed `XBL_AC_CFG` image load and the subsequent APDP metadata authentication interval.
These are firmware messages, not Linux output.
The observer connects to APSS without changing guest data or registers.
The hash-bound UEFI, ABL, and kernel observation sites remain unexercised.
See [observer sites](observer-sites.md) for the observation contract.

Earlier automatic-observer runs stalled after debugger pauses.
QBox compared independent native clock epochs with one SystemC clock.
TME waited for quantum budget while its deadline callback continued to advance that target.
The corrected mapping excludes native time spent waiting for synchronization.
It preserves the configured bounded quantum and native timer deadlines.

The separate `diagnostic-clock-epoch.lua` smoke exercised two APSS steps and a later breakpoint at `0x30f124`.
After a one-second pause, a real step published header bytes `4150424c020000000000000000000000`.
TME then consumed the request at `0x20c63e` and reached command one.
These observations prove cross-core progress after that debugger sequence, not kernel boot.


The SM8850 kernel experiment requires a real Linux boot log.
A kernel entry observation alone does not meet that requirement.
SM8845 does not require a kernel milestone.
Its experiments qualify the bounded behavior under investigation.
No recorded SM8845 run produced a kernel log.
The SM8845 composition separates two independently signed identities.
The OEM profile uses `0xa0220100`. The sequencer selector uses `0xa01b0200`.
The canonical run passes the sequencer check and executes CPH2771 XBL.
The [SM8845 evidence](../../../../models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md) records the checks and source coordinates.
The compatibility profile does not bypass either check.

### Reconstructed runtime boundary

The original TME capture contains `0x20000` bytes.
Its executed runtime call requires code at file offset `0x301da`.
The common reconstruction supplies the missing runtime through the pinned donor, relocation recipe, adapter, and checked API map.
It retains the original boot prefix and separate runtime state.
Both canonical runs execute this reconstruction.

The original and donor hashes remain explicit preparation requirements.
The platform does not wrap an invalid address, inject a firmware return value, or replace image authentication.
Successful XBL execution does not establish kernel execution or complete authentication qualification.

The latest canonical runs use the identities in the reconstruction table above.
Both preparations reproduce the checked ROM and adapter.
Both runs enable the OEM UART through the copied reserve partition.
Both UART logs contain `Auth Metadata` for `XBL_AC_CFG`.

Both runs pass the shared XBL policy controls and the independent mailbox index calculations.
The complete integration suite passes 62 tests.
Neither run establishes UEFI, ABL, or kernel execution.

The VMID mapper group uses the pinned public Qualcomm TF-A register contract.
Both canonical runs pass its four-instance or three-instance initialization loop.
This result does not establish a downstream VMID mapping or kernel execution.

Both runs advance beyond the previous XPU namespace and finite-aperture activation failures.
The configuration sockets share the existing protection state and retain independent trusted capabilities.
The native regressions preserve foreign locks and reject unauthorized configuration and protected data writes.
The [XPU evidence](../../../../models/qcom/sm88xx/tme_protection/namespace-platform-evidence.md) separates virtual geometry from observed firmware requirements.

The four authorization fuse views derive from one policy word.
The reconstructed profile sets `0x832`, which requires OEM authorization without changing lifecycle, root hashes, rollback, or secure-boot controls.
Both runs consume those views and reach the missing authorization API.
No constant result replaces that API or approves the APDP payload.
See [the fuse evidence](../../../../models/qcom/sm88xx/fuse_controller/evidence.md#oem-feature-authorization-group) for the source fields and virtual policy.


## Isolated firmware progress

Diagnostics pause selected cores and use read-only UFS files.
They expose independent hardware dependencies, not a complete concurrent boot.

| Scenario | Observed behavior | Next boundary |
| --- | --- | --- |
| TME ROM | Protection, host-backed entropy, timestamp access, and TPBL publication execute. | The ROM waits for APSS commands in shared mailbox RAM. |
| APSS and TME | The RNG, timer delay, and early RCG and branch controls execute. | The isolated run reached the UFS power-domain register `0x19e000`. |
| CPUCP ROM | PLLs, root configuration, domain initialization, and core power acknowledgement execute. | The integrated run passed `FEATURE_CONFIG11` and released the APSS CPU. |

The CPUCP diagnostic emits real APSS reset transitions from `platform.cpu_domains.reset_thread`:

```text
4.907768 ms: Start reset
5.129610 ms: Finished reset
5.129610 ms: Start reset
5.391481 ms: Finished reset
```

These transitions use the QBox CPU reset input.
No debugger operation changes the guest PC or supplies a firmware return value.
The later integrated run passes the UFS supplies, controller reset, device reset, reference gate, and TCSR identity.
It performs PHY initialization and link startup.
It completes link negotiation, reads UFS media, and loads both sequencer segments through the real APSS loader.
It verifies the sequencer certificate, command signature, and program digest before it reports success.
The APSS firmware then publishes its first request to the real TME mailbox.
The targeted UFS regression also passed reference-clock removal and restoration.


## Functional hardware policy

The models implement functional state, memory effects, protection, interrupts, and reset behavior.
They do not require analog or cycle-exact hardware simulation.
The substitution ledger distinguishes virtual policy from recovered silicon behavior.
Unknown accesses still fail with their address and PC.

The TME protection model now enforces region permissions.
The sequencer implements entropy, SHA-384, and P-384 verification with real inputs.
The implementation does not replace image authentication with a successful status.
The next work continues through auxiliary firmware, OEM UEFI, ABL, and Linux.
The ODP integration lane remains conditional on a proven kernel handoff.

## Paused-run debugger limits

A paused run needs all three debugger ports: `--apss-gdb`, `--tme-gdb`, and `--cpucp-gdb`.
`--debug-pause` also rejects `--continue-after` and requires `--stop-at`.
The platform then holds every instance before reset until each stub attaches.
Attach the APSS stub first, then attach and detach the TME and CPUCP stubs to release the hold.
The kernel-entry continuation is therefore unavailable in a paused run.

## Firmware volume map

The `uefi-map` action reads one OEM UEFI image without a platform boot.
It walks every firmware volume, including the volumes in LZMA guided sections.
It writes a JSON map, a Graphviz DOT graph, and a Markdown summary.
Use it to check the DXE dependency graph before a native run reaches driver dispatch.

1. Select the image. A ZIP package needs `--member`. A raw volume or partition needs no member.
2. Select the output directory with `--out`. Use a path under `build/private`.
3. Optionally add `--sdk` with a directory or file that contains GUID name definitions.
4. Add `--log` for correlation. The option accepts a plain log or a `.zst` log.

```sh
cargo run --locked -p qcom-vp -- uefi-map \
  --image "$PACKAGE" --member IMAGES/uefi.elf \
  --out build/private/uefi-map-700 \
  --log "$RUN/qemu-uart.log"
```

The tool writes three files to the output directory:

| File | Content |
| --- | --- |
| `uefi-map.json` | Volumes, files, sections, PE32 sizes and hashes, DEPEX trees, protocol table, dispatch result, log result |
| `uefi-map.dot` | Driver and protocol graph. A dashed edge is a DEPEX use. A solid edge is a candidate reference. |
| `uefi-map.md` | File table, APRIORI list, dispatch order, undispatched drivers with reasons, protocol table |

The dispatch simulation is static. A driver that contains a protocol GUID is a candidate producer.
The image does not prove that the driver installs the protocol.
The solver assumes only the three core protocols listed in the JSON output.
A dispatched reference candidate makes its protocol available in the simulation.
This rule does not prove a runtime installation.
The report lists drivers that remain unscheduled and their unsatisfied GUIDs.

The scheduling rules follow EDK2 `Dispatcher.c` and `Dependency.c`:

1. APRIORI schedules drivers in its own volume without a DEPEX check.
2. A driver without DEPEX requires all architectural protocols from `DxeProtocolNotify.c`.
3. BEFORE and AFTER identify FFS files, not protocols.
4. SOR requires an explicit driver request, which the simulation does not assume.
5. Each dependency pass schedules its batch before the batch changes protocol availability.

The corrected `.700` map contains 204 files and schedules 111 DXE drivers.
No DXE driver remains unscheduled under the candidate-producer assumption.
`DisplayDxe` and `UsbConfigDxe` have APRIORI entries, which bypass their DEPEX requirements.
`ScmDxeCompat` also has an APRIORI entry. Its absent DEPEX is not unconditional TRUE.
The map excludes applications and SMM-only files from DXE dispatch.
These results replace the earlier 112-driver simulation and its two false dependency gaps.
The smoke output is `build/private/uefi-map-700-corrected`.

The log parser reads DxeCore `Loading driver at ... EntryPoint=...` lines and `InstallProtocolInterface` lines.
It compares the observed load order with the static order.
It marks a protocol as observed when the log holds its install line.

Limits:

- A GUID that the code builds without a literal copy is not a reference candidate.
- A producer in another partition is absent from a single-image map.
- The tool records a Tiano compressed section but does not decode it.
- The map assumes that all decoded volumes are available before dispatch. It does not execute FV extraction drivers.
- GUID references can be consumers instead of producers. Static order does not prove runtime success.

Run the focused unit tests with:

```sh
cargo test --locked -p qcom-vp uefi
```

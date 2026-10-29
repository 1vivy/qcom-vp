# Environment inventory

This inventory separates reusable behavior from reference configurations and historical run state.
The [environment runbook](environments.md) defines the execution and evidence contract.
A listed capability is not a blanket qualification of a device or security policy.

Reusable components sit under `models/qcom/` and `platforms/qcom/`, grouped by manufacturer, family, and SoC override.
That grouping is a practical catalog. It does not certify that two devices or two family members behave the same.
An inferred family recipe qualifies only when it produces its declared observable effects through a guest or component consumer.

## Execution paths

| Path | Declared inputs and composition | Existing coupling | Qualification boundary |
| --- | --- | --- | --- |
| External research experiment | Schema 1 environment, complete file list, external QBox Lua | The operator trusts Lua code, which can introduce undeclared host dependencies | Process outcome plus experiment-specific checks, not boot qualification |
| SM8850 OEM boot | Private manifest, PBL, TME, package, roots, optional donor, `platforms/qcom/sm88xx/sm8850/platform.lua` | Fixed reference ROM geometry, package layout, reconstruction identities, and observer sites | Current evidence records kernel entry and early UART, not complete Android boot |
| SM8845 OEM boot | Separate OEM package and root, compatibility ROM inputs, `platforms/qcom/sm88xx/sm8845/platform.lua` | Reuses the QRD8850 reset composition with explicit overrides | Historical firmware execution does not establish native ROM provenance. Kernel entry is not a condition of this reference. |
| IPCC subsystem | Authored APSS and TME guests, shared memory, GIC, CLIC, and IPCC views | Synthetic reset conditions and modeled routing, not OEM initialization | Guest checks establish bounded queue, alias, and interrupt behavior |
| Other native fixtures | `tools/qcom/qcom-vp/tests/platform.rs` and per-fixture compositions | Compiler dependencies and fixture-specific observation rules | Each executed test qualifies only its asserted behavior |

## SM8850 reference contract

The platform composition is `platforms/qcom/sm88xx/sm8850/platform.lua`, with reset topology in `platforms/qcom/sm88xx/reset.lua`.
The optional reconstruction activates `platforms/qcom/sm88xx/sm8850/runtime.lua`.
The default board file selects UFS boot ID one.
The private manifest supplies firmware and trust identities. No connected phone is a runtime dependency.

Preparation creates copied ROMs, a board file, kernel reference, and six LUN images.
A run copies mutable storage into a new directory.
Generic patches and TME reconstruction remain explicit transformations with recorded identities.
The current preparation requires the board-file record. Older prepared directories need reconstruction through the preparation command.

The [status record](qcom/sm88xx/oplus/bringup.md#status) describes run921 and its exact limits.
At `f019bd6`, a later run again reached the first kernel instruction at `0xc7800000` in non-secure EL1.
The observer verified the full 39,889,408-byte image, that instruction, and the debugger detach.
The kernel emitted 4,573 UART bytes across 41 lines.
The unsupported timer read at `0x17810008` still follows the detach and gives `run_succeeded=false`.
That record is historical evidence. It does not qualify later model or launcher changes automatically.
The selected firmware remains private, so public users must supply compatible inputs before repeating that experiment.

## SM8845 reference contract

`platforms/qcom/sm88xx/sm8845/platform.lua` reuses the reset composition and changes the declared identity and runtime details.
The manifest must select the ROM compatibility label.
Its package, root certificate, and observation profile differ from the SM8850 reference.
The existing [OEM scenarios](qcom/sm88xx/oplus/bringup.md#actual-oem-scenario) record their inputs, first failures, and result boundaries.

Compatibility execution is not evidence that the supplied ROM originated from SM8845 hardware.
The current run reports the CPUCP command error at `0x183800f4` with PC `0x1d8066f0`.
That result is the qualification boundary for this reference.
Kernel entry is not an acceptance condition here, and no research interface change converts that result into success.

## IPCC reference contract

The [IPCC procedure](../environments/qcom/ipcc/README.md) reuses the existing native fixture rather than creating another guest implementation.
The composition defines reset addresses, shared-memory aliases, interrupt routes, and the absence of storage.
Preparation generates guest images from authored source and records their hashes.
A padded input demonstrates that file offsets and guest addresses are separate coordinates.

The guest checks, not a fabricated completion response, determine the terminal access.
The platform still reports the deliberate unmapped access as a process failure.
A separate checker establishes the narrower experiment result from the observed address and execution outcome.
This does not establish OEM firmware execution, retail authentication, physical interrupt timing, or an EL2 escalation.

## State and content contracts

The [state reconstruction plan](research-environments-plan.md#b3a-reconstruct-required-state-and-contents) covers required contents, not only interfaces.
The current environment contains functional state, but each research function needs its own qualification.

| Surface | Current contract | Remaining qualification |
| --- | --- | --- |
| UFS media | Preparation imports package contents. Each run receives independent mutable LUN copies. | Device-specific records and intentional reuse of prior state need a coherent input profile. |
| RPMB | The reference declares 4 MiB. The key, counter, and data persist within one run. Each new run begins unprogrammed. | Provisioned contents, region allocation, and persistence across runs remain separate requirements. |
| Hardware keys and identity | The composition supplies explicit virtual identity, policy, and key substitutes. | Captured sealed objects need compatible derivation inputs and validation, not only matching filenames. |
| Secure files | Selected firmware uses filesystem listeners and protected persistent objects. | The object format, version relationships, integrity checks, and owning service need qualification together. |
| RKP and trusted services | Published interfaces and firmware execution establish bounded availability. | Provisioning, certificate use, trusted-app persistence, StrongBox, and AVF need separate consumer experiments. |
| EFI and boot policy | Package media and guest writes supply state. Private records distinguish EFI variables from Keymaster DeviceInfo. | Initialization, authoritative values, durable backing, and reset survival need distinct evidence. |
| Shared handoffs | Firmware produces records in shared model memory. Existing physical captures identify selected structures and queue geometry. | A warm-state profile must preserve ownership and validity. Historical pointers and capabilities are not reset constants. |

The [storage evidence](../models/qcom/ufs/README.md#authenticated-rpmb) defines the RPMB operations and their limits.
The [substitute inventory](qcom/sm88xx/stubs.md) identifies virtual keys and identity.
The [hardware evidence](qcom/sm88xx/oplus/infiniti/hardware-evidence.md#shared-ram) separates captured handoff contents from reset behavior.

Selected CPH2747 captures establish useful structure without qualifying a complete provisioned environment.
The RPMB unit reports 16 MiB, while its region allocation remains unresolved.
Two secure-file indexes use an 88-byte prefix and 25-byte entries.
The matching TZ image uses that entry layout with a configurable capacity.
The captured capacity is 60 entries, not a universal hardware constant.

Three active index words match fields in their respective objects.
Primary and backup objects differ, and one backup index references a primary object version-like field.
These relationships rule out an assumption that every backup index selects every backup object.
Their authentication and commit semantics still require a matched consumer analysis.

The captured RKP database contains no provisioned-key rows.
That observation does not establish that all device credential stores are empty.
Private captures and the offline interpreter preserve the evidence without adding device payloads to the repository.

The private records separate observations, static firmware facts, and remaining dependencies:

- Reconciliation: `artifacts:2026-10-29-environment-state/findings.json sha256=3df154195c9cf61cacef170c733dc9cb36c51ba7bc2814d922ff92c022195b17`.
- Content interpretation: `artifacts:2026-10-29-environment-state/interpreted.json sha256=85c95db46459939df6b3eaa2a93d23aa6aa536068df6d4c3a76afde5d00e24e9`.
- Matched firmware: `artifacts:2026-10-29-environment-state/firmware-facts.json sha256=e95335ec81b3c4784cb483a91b06c65229b0666261b0729524c4438a37b7b8b5`.
- Offline interpreter: `artifacts:2026-10-29-environment-state/inspect-captures.py sha256=7087dd17c2a2534bdf5bf4a749a28d4f16fbae4417dc8d0b033075ab0a59cd8a`.

## Hidden dependencies and disposition

| Dependency | Disposition |
| --- | --- |
| Installed board lookup during OEM preparation | Reference workflow only. An explicit board file replaces it and enters the preparation record. |
| Fixed PBL and TME sizes in `firmware.rs` | Reference workflow only. External Lua uses its own memory loaders and reset geometry. |
| OEM flash ZIP layout | Reference workflow only. External compositions can use copied raw storage or omit storage. |
| Two automatic kernel profiles | Boot observation only. Research execution does not use those profiles. |
| Private manifests with local paths | Private inputs, not tracked prerequisites. Public examples use authored guests and explicit paths. |
| Accumulated `build/private` state | Historical evidence, not implicit experiment input. New runs require new output directories. |
| Lua helpers and guest binaries | Declare and hash every required file. The launcher does not discover dependencies hidden in Lua code. |
| Shared libraries and host toolchains | Record revisions and tool versions in procedures. The executable hash alone is insufficient. |
| Asynchronous execution and host entropy | Record the limitation. No deterministic scheduling or replay guarantee. |
| External source claims with removed citations | Treat as evidence-gap inventory entries. The retained source lists are research leads, not qualified evidence. |

## Provenance review

These reviews list the remaining evidence gaps after an earlier citation removal.
They are not proof that every listed fact lacks independent support.
The retained source-only lists are investigation leads, not qualified evidence.
Prior accumulated evidence, documentation, and device probes supplied the substantive basis.
The reviews inspect existing firmware records, device observations, and permitted public sources.
Each entry names the missing observation and the affected claim.

- [Processor and power review](qcom/sm88xx/provenance-processors.md)
- [TME, boot, and fuse review](qcom/sm88xx/provenance-tme.md)
- [Peripheral review](qcom/sm88xx/provenance-peripherals.md)
- [Shared composition review](qcom/sm88xx/provenance-shared.md)

An open entry does not block work on a different environment.
Keep a specific claim open until permitted evidence or an explicit virtual policy supports it.
Do not consult prohibited sources again, and keep nonredistributable material outside the repository.
Unresolved model evidence does not authorize breaking a working environment merely to pass a text search.
The [evidence guide](evidence.md) defines how the repository records source identities and private references.

## Verification record

Commands, input identities, and observed result lines belong with the affected environment procedure.
A host test result does not establish OEM execution.
A subsystem guest result does not establish physical equivalence.
A historical trace does not establish a new revision's result.


## Earlier requalification, 2026-10-28

Both private reference manifests completed fresh preparation with the rebuilt CLI.
Both preparations included the copied board file and the updated TME recipe.
They reconstructed TME with SHA-256 `c52d43240e387986e37540f2a12a5a05cbcb28f1a3cd98399255437f00a17d65`.
The runs used the same system executable as the IPCC verification.
No physical-device access occurred.

| Reference | Preparation | Current execution result |
| --- | --- | --- |
| SM8850 | Passed | Observer error `E14` for memory request `mc8238000,8000`. Kernel entry and detach remained false. |
| SM8845 | Passed | CPUCP command error at `0x183800f4`, PC `0x1d8066f0`. The observer disconnected before handoff. |

Both commands returned failure. Neither result qualifies a complete boot or proves that the earlier kernel result is reproducible.
These initial records did not establish the cause of the difference from historical execution.
Do not suppress either failure or substitute historical success for current qualification.

The commands used this form for each platform:

```sh
P=sm8850
MANIFEST="/path/to/$P.toml"    # operator-written manifest, never a tracked default
ROOT=$(mktemp -d build/oem-requalification.XXXXXX)
build/install/bin/qcom-vp prepare \
  --firmware "$MANIFEST" --out "$ROOT/prepared"
build/install/bin/qcom-vp run \
  --prepared "$ROOT/prepared" --out "$ROOT/run" \
  --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
```

Select `P=sm8845` for the second reference.
The retained preparation records identify the resolved inputs and hashes.

Retained evidence:

- SM8850 result: `artifacts:2026-10-28-environment-rework/run-sm8850/result.json sha256=6c3bba0f6bebfe80fa62165172225f5219bffa5372511f9f7f8013627390c57d`.
- SM8850 preparation: `artifacts:2026-10-28-environment-rework/run-sm8850/prepared.json sha256=91995c80f30285715dc16bc58a9337f8750bdc9f8ab7b8ed248e7fc2010c0b79`.
- SM8845 result: `artifacts:2026-10-28-environment-rework/run-sm8845/result.json sha256=ebb52d05249d3fcc88e3e4ac79d3082aa36bcc9c3ed3c033e2563e777f7e48ff`.
- SM8845 preparation: `artifacts:2026-10-28-environment-rework/run-sm8845/prepared.json sha256=4f6f4e843685ea1d374780250209af17ad68308b36448e7269ee55abc2df8ef8`.
- Terminal observations: `artifacts:2026-10-28-environment-rework/terminal-observations.txt sha256=54e184de2ba9a7e6287c8d6e04c82e2a274c278246828eda5050ba47586f2264`.
- Failure traces and result identities: `artifacts:2026-10-28-environment-rework/SHA256SUMS sha256=ab38a42d5f5e537a6f55dad41069c7ff1df4a9fcddf125faf58efb0be55a7181`.

The private evidence store retains compressed execution traces for both initial OEM failures.
It also retains the two bounded SM8850 memory captures. They do not constitute a restorable machine snapshot.
Decompression reproduced the original trace hashes before removal of the temporary run directories.

## Current requalification, 2026-10-29

SM8850 again reaches the genuine OEM kernel and emits 4,573 kernel UART bytes across 41 lines.
The observer verifies the complete 39,889,408-byte image, its first instruction, and debugger detach.
The kernel enters at `0xc7800000` in non-secure EL1.
The preparation record has the same SHA-256 as the failed SM8850 run above.
No physical-device access occurred.

The earlier failed trace contains an APSS watchdog bite during observation.
GDB stopped APSS while SystemC time continued.
The automatic observer now holds SystemC time during inspection through a private diagnostic socket.
Each completed memory read requires an unchanged timestamp.
All 238 pause/resume pairs retained the same timestamp in this run.
The [observer procedure](qcom/sm88xx/oplus/observer-sites.md#simulation-time-during-inspection) defines the policy and its limits.

The run reports `kernel_entry=true`, `detach_confirmed=true`, and `run_succeeded=false`.
The final false value records the unsupported timer read at `0x17810008`, after debugger detach.
This result restores the earlier milestone, not a complete Android boot.
Authentication qualification remains unestablished.
The SM8845 result above remains unresolved.

The proof used these commands:

```sh
make build JOBS=4
MANIFEST="/path/to/sm8850.toml"   # operator-written manifest, never a tracked default
build/install/bin/qcom-vp prepare \
  --firmware "$MANIFEST" \
  --out build/milestone-restore/prepared-sm8850
build/install/bin/qcom-vp run \
  --prepared build/milestone-restore/prepared-sm8850 \
  --out build/milestone-restore/run-sm8850 \
  --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
```

The run command exits with status 1 because the platform stops at the later unsupported access.
The result and entry records establish the bounded milestone despite that exit status.
The private evidence store retains the following records:

- Outcome: `artifacts:2026-10-29-observation-clock/result.json sha256=bb3fe707f1a1269c4e334c4717da29db4646ec1b7a212dbba6b97791bd8b3d80`.
- Entry: `artifacts:2026-10-29-observation-clock/kernel-entry.json sha256=877dbd2b041ef89affc9dc090e2db85d699461f241d29842b069fa2496bf8d84`.
- Preparation: `artifacts:2026-10-29-observation-clock/prepared.json sha256=91995c80f30285715dc16bc58a9337f8750bdc9f8ab7b8ed248e7fc2010c0b79`.
- Provenance: `artifacts:2026-10-29-observation-clock/provenance.json sha256=b36f839b7aef53ef7e78062fcbbbba97a7e64c27450c3b6610095dc01e0c4445`.
- UART: `artifacts:2026-10-29-observation-clock/qemu-uart.log sha256=e4156a1e50e34ce6a7eafef8c8be1458bf22873f48e508ff32e36e0672d8d020`.
- Clock and fault trace: `artifacts:2026-10-29-observation-clock/system.log.zst sha256=0301166fdc334316a4c06a3fa48047186f9abcffcb7a3dfae317005e4214894e`.

The final host suite passed 116 tests.
The final native suite passed 97 tests, including the new observation-clock regression.
That regression checks timer deadlines, watchdog deadlines, repeated requests, foreign holds, disconnect, reconnect, and shutdown.
The Lua syntax check passed. The three edited evidence documents passed STE lint with zero violations.

```sh
cargo test --locked -p qcom-vp -- --include-ignored --test-threads=4
luac -p platforms/qcom/sm88xx/reset.lua
```

The three edited evidence documents passed the controlled-language check with zero violations.

## Catalog migration verification, 2026-10-29

The scoped tree preserves the model contracts and runtime identifiers.
The migration changes source locations, installed recipe paths, and their consumers.
It does not require SM8845 kernel entry.
The [working catalog patterns](qcom/sm88xx/architecture.md#working-catalog-patterns) show shared recipes and specific data.

The build passed after removal of the old installed recipe paths that the migration replaced.
All 116 host tests and 97 native tests passed.
The scoped Lua compositions and boards passed the syntax check.
The primary runbooks passed STE lint with zero violations.
The broader evidence narratives still contain style findings. This migration does not rewrite those historical records for style.

Two independent IPCC preparations produced manifest SHA-256 `a56bf46fdf6e4b29e8d51df29c3315793af4640dc93c0245389448d194e9aefc`.
Both real platform executions reached the expected guest sentinel, and both experiment checkers accepted the result.
The scoped helper path is `qcom/apss-gic.lua` inside each input snapshot.
The [IPCC procedure](../environments/qcom/ipcc/README.md) contains the preparation, execution, and check commands.

Fresh SM8850 preparation and execution again verified the complete kernel image and its first instruction at `0xc7800000`.
The debugger detached, and the kernel emitted 4,573 UART bytes across 41 lines.
All 238 observation holds retained their timestamps.
The later unsupported read remains `0x17810008`.
The result still reports `run_succeeded=false` for that access, not a completed Android boot.

```sh
make build JOBS=4
cargo test --locked -p qcom-vp -- --include-ignored --test-threads=4
MANIFEST="/path/to/sm8850.toml"   # operator-written manifest, never a tracked default
build/install/bin/qcom-vp prepare \
  --firmware "$MANIFEST" \
  --out build/scope-rework/prepared-sm8850
build/install/bin/qcom-vp run \
  --prepared build/scope-rework/prepared-sm8850 \
  --out build/scope-rework/run-sm8850 \
  --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
```

The retained records identify both IPCC results, the SM8850 inputs, the executable hashes, and the kernel identity.
Trace decompression reproduced the original hash before removal of temporary storage.

- Record index: `artifacts:2026-10-29-catalog-scopes/SHA256SUMS sha256=ef2d9044466f8a39d355538088058ebd1385eac04473e8a342f9506fdbe2cb5f`.
- Provenance: `artifacts:2026-10-29-catalog-scopes/provenance.json sha256=07eaa57651e48982241f3ec36311aa801af4869ffc68c6afb0206b9b949739ee`.
- SM8850 outcome: `artifacts:2026-10-29-catalog-scopes/sm8850/result.json sha256=5687c39dfae7fb54024ffd76b058537ad0949ddc9b6a72a76d320dfaaac8c1d5`.
- SM8850 entry: `artifacts:2026-10-29-catalog-scopes/sm8850/kernel-entry.json sha256=7b024a07567f0e1e06d97343997e5006394754f9a0216fbcbbe5bcb31553bb57`.
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
| UFS media | OEM runs create fresh LUN copies. Stateful experiments declare and copy all persistent files together. | Device-specific records still need coherent contents and consumer checks. |
| RPMB | Optional backing retains key, counter, and media across processes. Reset clears only pending responses. | Physical region allocation and OEM provisioning remain separate questions. |
| Hardware keys and identity | Lua supplies explicit QCE and TME roots, geometry, and existing security policy inputs. | Captured sealed objects need compatible derivation, not only matching filenames. |
| Secure files | Original local-table routines qualify constructed contents, restart, and tamper rejection in bounded instruction emulation. Native QBox execution also qualifies the derivation provider's constructor and its QCE root operation. | The secure-file derivation call, active version authority, and complete index and payload lifecycles remain unqualified. |
| RKP and trusted services | Published interfaces and firmware execution establish bounded availability. | Provisioning, certificate use, trusted-app persistence, StrongBox, and AVF need separate consumer experiments. |
| EFI and boot policy | Package media and guest writes supply state. Private records distinguish EFI variables from Keymaster DeviceInfo. | Initialization, authoritative values, durable backing, and reset survival need distinct evidence. |
| Shared handoffs | Firmware produces records in shared model memory. Existing physical captures identify selected structures and queue geometry. | A warm-state profile must preserve ownership and validity. Historical pointers and capabilities are not reset constants. |

The [storage evidence](../models/qcom/ufs/README.md#authenticated-rpmb) defines the RPMB operations and their limits.
The [substitute inventory](qcom/sm88xx/stubs.md) identifies virtual keys and identity.
The [hardware evidence](qcom/sm88xx/oplus/infiniti/hardware-evidence.md#shared-ram) separates captured handoff contents from reset behavior.
The [RPMB experiment](../environments/qcom/rpmb/README.md) qualifies fresh, resume, and clone behavior through an authored AArch64 guest.
Its continuation checks authenticated counter and media reads, then rejects replayed, wrong-key, and changed frames without changing persistent bytes.
The [state lifecycle](environments.md#persistent-state) binds the complete declared collection to its manifest and system executable.

Selected CPH2747 captures establish useful structure without qualifying a complete provisioned environment.
The RPMB unit reports 16 MiB, while its region allocation remains unresolved.
Two secure-file indexes use an 88-byte prefix and 25-byte entries.
The matching TZ image uses that entry layout with a configurable capacity.
The captured capacity is 60 entries, not a universal hardware constant.

Three active index versions match their respective object versions.
Primary and backup objects differ, and one backup index references a primary object version.
These relationships rule out an assumption that every backup index selects every backup object.
The protocol investigation below qualifies selected validation and recovery paths.

The captured RKP database contains no provisioned-key rows.
That observation does not establish that all device credential stores are empty.
Private captures and the offline interpreter preserve the evidence without adding device payloads to the repository.

The private records separate observations, static firmware facts, and remaining dependencies:

- Reconciliation: `artifacts:2026-10-29-environment-state/findings.json sha256=3df154195c9cf61cacef170c733dc9cb36c51ba7bc2814d922ff92c022195b17`.
- Content interpretation: `artifacts:2026-10-29-environment-state/interpreted.json sha256=85c95db46459939df6b3eaa2a93d23aa6aa536068df6d4c3a76afde5d00e24e9`.
- Matched firmware: `artifacts:2026-10-29-environment-state/firmware-facts.json sha256=e95335ec81b3c4784cb483a91b06c65229b0666261b0729524c4438a37b7b8b5`.
- Offline interpreter: `artifacts:2026-10-29-environment-state/inspect-captures.py sha256=7087dd17c2a2534bdf5bf4a749a28d4f16fbae4417dc8d0b033075ab0a59cd8a`.

### Secure-file validation and recovery

The matched TZ image identifies separate index, metadata, payload, and version-table formats.
The index tag protects all configured entries, including unused bytes.
The reader then checks its namespace as a bounded string and compares its version with the selected authority.
Object metadata also carries a keyed tag.
Payload digests and cipher inputs reside inside that protected metadata.
All six captured payload segments reproduce their stored SHA-256 digests.

The version service can select a counter object, an RPMB table, or a protected `vTable` file.
The captured `vTable` entries match the index identities but contain different versions.
They therefore do not establish the active authority for those indexes.
The key wrapper delegates derivation through service `0x0b`.
The [native provider trace](#native-provider-and-hardware-dependency) identifies its constructor and QCE dependency in a different TZ image.
Neither investigation recovers physical keys.

Original firmware instructions executed in twelve bounded offline scenarios.
The experiment supplies constructed initialization state, a public test key, real HMAC-SHA256 operations, and an in-memory filesystem.
It does not replace an integrity decision with a successful status.

1. A correctly protected test index passes its reader.
2. Tampering, short reads, namespace mismatches, and version mismatches fail and request removal of the candidate.
3. A valid backup with the expected version replaces a rejected primary.
4. An older backup fails even when its test-key tag is valid.
5. The index write precedes the version update.
6. A failed index write returns before version persistence.

The experiment stops before the RPMB persistence operation.
It does not claim successful durable updates or physical recovery.
The validation path can request file removal, so invoking it on a phone is not a passive read.
No phone command occurred during this protocol investigation.

The captures remain private, and the experiment does not change production model behavior or authentication policy.
A generated profile still needs coherent keys, identities, versions, payloads, and durable authority.
The retained artifacts identify the supplied boundaries and the unresolved dependencies:

- Findings: `artifacts:2026-10-29-secure-file-protocol/findings.json sha256=866ad46ee7a6cd9ce5f0685758d335804068defaa3f103d1766f02240b7f2c7b`.
- Original-code execution: `artifacts:2026-10-29-secure-file-protocol/execution.json sha256=547dc8db42ba24ab9ca9d2d73a869fb06bd9b6c098ea793713f23a337f284758`.
- Capture interpretation: `artifacts:2026-10-29-secure-file-protocol/decoded.json sha256=573e848de955af60b436aa2ba48d2c9a923f840e561617df0d4935b8783835d3`.
- Execution instrument: `artifacts:2026-10-29-secure-file-protocol/execute-protocol.py sha256=2060522122b56b44169df5d7a8961c9e604b19822fa6845630ed9962246efc36`.
- Capture decoder: `artifacts:2026-10-29-secure-file-protocol/decode-captures.py sha256=b8cf1273a75949f73491effad4c47357c4c70c9e58fe7f51a7254e2333a464fc`.

### Stateful local authority and derivation boundary

The [local-authority experiment](../environments/qcom/secure-storage/README.md) needs no captured secure files.
It loads the matched TZ image from an explicit path and checks its complete hash.
Its entry state supplies a test identity, an initialized test key, and a local-file authority.
The host supplies HMAC-SHA-256 and durable file operations.

Separate processes execute the original version-table routines:

1. The firmware creates a 593-byte protected table.
2. The firmware stores version 7.
3. A new process reads version 7 from the retained table.
4. The instrument changes one authenticated byte.
5. Another process rejects the changed table with `0xf0100001` and removes its primary file.

The backup remains. This experiment does not qualify backup recovery or general secure-file payloads.
It uses bounded instruction emulation, not QBox execution.
No RPMB operation or physical key provider supplies its result.

A separate original-client trace reaches the derivation dispatch boundary.
The client looks up UID `0x0b` and invokes operation one.
Its four 16-byte argument descriptors contain buffer lengths 0, 33, 35, and 32.
The first pointer is null.
The `0x13` field encodes three input buffers and one output buffer, not a byte length.
The permitted CLO `SmciInvokeUtils.h` defines the matching packed-count ABI.
The trace stops before the instrument-supplied dispatch target returns a value.
It supplies neither a provider response nor derived material.

| Dependency | Current classification | Required next evidence |
| --- | --- | --- |
| Derivation client layout | Original instructions and the public object ABI establish the request shape | A real secure-file call through the native provider |
| Derivation algorithm and root | Native provider construction uses the virtual OEM key through QCE AES-256 CMAC | The cached-key operation and physical applicability |
| Local version authority | Selected original routines accept coherent constructed contents across process restart | Complete index and payload lifecycles |
| Phone authority selection | Existing records do not identify its active backend | An applicable trace or passive state observation |
| Hardware root models | QBox execution connects UID `0x0b` construction to the existing QCE OEM-key path | Software-key CMAC for the next derivation stage and separate TME consumers |

The passive phone check observed a live `qseecomd`, existing logs, and its open descriptors.
Those observations did not identify the active authority or derivation provider.
The phone remained in Android with the same slot.
The check issued no secure-service request, storage write, provisioning operation, reboot, or MMIO probe.
Authorized private content research remains available when a bounded observation can resolve a specific dependency.

Retained evidence:

- Dependency trace: `artifacts:2026-10-29-secure-state-dependencies/findings.json sha256=0f5ada47b083f4bddb3457af7f9ccee627e498a510fd530c4be7993745fdc6bd`.
- Original local-table execution: `artifacts:2026-10-29-secure-state-dependencies/execution.json sha256=7ba2b4450db01f31ba3f28fffde3389ac35442f5bd785a25816d2d6a2ad259bf`.
- Derivation dispatch: `artifacts:2026-10-29-secure-state-dependencies/derivation-request.json sha256=dada58ecc9d2408ab2a787920869f55678fc3b8c6a2aad4d8647b5f6644a83fe`.
- Dispatch instrument: `artifacts:2026-10-29-secure-state-dependencies/derivation-request.py sha256=7b364ed8db9344e78c5eabb544d7181518fb8c428ae1536e88a5fbe6ce91d590`.
- Passive device check: `artifacts:2026-10-29-secure-state-device/findings.json sha256=fa479e2a79eb7f7d4fdca2b63365b284f87c760bee2cd994813fc6e71b190108`.

### Native provider and hardware dependency

QBox executes the registered provider from the CPH2749 package's `IMAGES/tz.mbn`.
Its SHA-256 is `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932`.
The earlier phone-matched image has SHA-256 `f6317e33e3daf280d39a7912bb103e8a8840e0130e6ff1a4b158920e5e7e3fa2`.
These images differ. The runtime result applies to the first image and the declared virtual profile.

The observer uses hardware breakpoints and the existing simulation-time hold.
It reads registers, object metadata, and selected command fields.
It changes no guest register, guest memory, firmware instruction, or authentication result.
It reads no key bytes or identity bytes.
The existing reconstructed TME runtime remains an explicit input, not a newly recovered retail runtime.

The service lookup first searches a local cache.
On a cache miss, it invokes the environment opener through the real object syscall.
The native opener applies its permissions and searches registered factories.
The observed factory advertises both UID `0x0b` and UID `0x91`.
The commonlib cache insertion routine is not the native factory registry.

| Site in the selected ELF | Role | Evidence |
| --- | --- | --- |
| `0xd864d97c` | Commonlib lookup with opener fallback | Original instructions and live object dispatch |
| `0x156f390c` | Native environment opener | Live callback and registry inspection |
| `0x156962dc` | Registered factory | Live UID masks and constructor table |
| `0x157a8278` | UID `0x0b` constructor | Native entry and successful return |
| `0x157a8748` | Derivation service dispatcher | Original instructions and native release operation |
| `0x156b7978`, `0x156b76c4` | KDF policy and primitive | Native calls and selected policy value |
| `0x157a5034` | UID `0x91` enablement selector | Original instructions only |

ASLR changes both kernel and commonlib addresses between runs.
The observer derives both relocations from real transitions and checks 64 instruction bytes before it installs each fixed observation site.
An address from a previous run is not a reusable runtime address.

The UID `0x0b` constructor queries a 32-byte environment property named `did`.
Its fallback queries `vm` and uses a different label.
The observed constructor selects the label `core trustzone application key label securemsm`.
It supplies no initial key and requests 32 output bytes.
Its KDF policy flag is zero, so this instance does not select the preliminary hash branch.

The selected KDF executes a counter-mode MAC with a four-byte counter.
The observer follows its native MAC callback into the QCE driver.
Genuine command elements contain `AUTH_SEG_CFG=0x45e52`, a 16-byte segment, and GO value seven through `GOPROC_OEM_KEY`.
Those fields select AES-256 CMAC with `USE_HW_KEY_AUTH`.
Both the KDF and constructor return zero.
The returned object records an initialized key and an EL2-aware flag of zero.

The [QCE model](../models/qcom/gpce/evidence.md#aes-256-cmac-with-the-oem-hardware-key) supplies its explicit virtual OEM key.
This establishes the constructor's hardware dependency in this environment.
It does not establish the physical key value, physical derivation equivalence, or compatibility with captured secure files.
It establishes no connection between this provider and the separate TME root.

Static dispatcher analysis resolves the null-key question for operation one.
After initialization, a null key or zero key length selects the object's cached 32-byte key.
The primitive then receives an explicit key, rather than the constructor's absent-key request.
**Inference:** This next stage needs software-key CMAC through the selected driver.
The current QCE model rejects software-key CMAC.
The successful constructor therefore does not qualify secure-file derivation or remove that model gap.

The optional context helper opens UID `0x177`, opens subobject one, and requests property `id`.
It appends the returned bytes only when the object's EL2-aware flag is nonzero.
The observed flag is zero, so this trace does not exercise that service.
Its existence alone establishes no DICE, BCC, RKP, or physical identity contract.

Static analysis also identifies the native enablement policy:

1. The selector resolves policy from a valid 32-byte `did` property.
2. A successful `cmnlib_gppo_rpmb_enablement` query with a zero final word disables the feature.
3. Otherwise, secure-state bits satisfying `(state & 0x67) == 0x40` enable it.
4. Other states require the UID `0x171` dependency. Its success enables the feature, while its special `0x0b` result selects fallback status ten.

The counter object comes through UID `0x91` and uses cache key `0x8b`.
That cache key is not an independently advertised service in the observed registry.
The commonlib client still selects between its counter object, RPMB table, and protected local table.
The static policy does not identify the phone's active authority.

The armed sites report no authority call before the execution boundary.
The complete earlier trace contains the known-present key constructor as a positive control.
The retained provider traces stop at an unmodeled read of `0x17810008`, before an Android secure-file consumer runs.
The next authority observation needs a real permitted caller, not a supplied service reply or a constructed local-table state.
The provider's observed dispatch operation is release, not secure-file operation one.

The retained record and instrument remain private research evidence.
They are not prerequisites for a public build or an authored environment.

1. Set `FIRMWARE` to the explicit input manifest.
2. Set `TZ` to the selected extracted image and `WORK` to a new research directory.
3. Set `SYSTEM` and `SM8850_PLATFORM` to the installed executable and platform file.
4. Bind `EVIDENCE` to this record in the configured artifact store.
5. Prepare the inputs and execute the observer.

```sh
EVIDENCE="${QCOM_VP_REF_ARTIFACTS:?}/2026-10-29-live-secure-providers"
mkdir -p "$WORK"
qcom-vp prepare --firmware "$FIRMWARE" --out "$WORK/prepared"
python3 "$EVIDENCE/trace-provider.py" \
  --prepared "$WORK/prepared" --tz "$TZ" \
  --system "$SYSTEM" --platform "$SM8850_PLATFORM" \
  --out "$WORK/observation"
```

The observer requires Python with `pyelftools`. The exercised version is `0.33`.
The manifest preserves the retail profile and its declared TME reconstruction.
The observer creates fresh LUN copies and fresh RPMB state.
It does not resume captured phone storage.

The complete replay reports:

```text
NATIVE_PROVIDER uid=0x0b root_status=0 constructor_status=0 initialized=1
NATIVE_QCE oem_key_go=1 hardware_key_auth=1
AUTHORITY_OBSERVATIONS count=0
```

The native system still reports `RUN_FAILED reason=unmodeled-access`.
That result limits the observation. It is not a successful secure-file lifecycle or a complete boot.

Retained evidence:

- Native replay: `artifacts:2026-10-29-live-secure-providers/observation.json sha256=12b5250a05a4d94197fe3d05c729b40ac9daf2d8c88df5c505300733f3dee5fe`.
- Registry and opener: `artifacts:2026-10-29-live-secure-providers/registry.json sha256=87cfedf902943126ee6edc925b7419921149865ac4c3917ea1577a7a4cd2a216`.
- Earlier observations: `artifacts:2026-10-29-live-secure-providers/manual-observations.json sha256=f1c0654e5ed07ac251ead8cc6d63e3cc1da8d0cdeb55635fc0e45ec2729dae2b`.
- Replay instrument: `artifacts:2026-10-29-live-secure-providers/trace-provider.py sha256=f90b154be1d1eaa37a6d7e7656401b7625e1ced0c9134de3013af1d600e25ec9`.
- Read-only transport: `artifacts:2026-10-29-live-secure-providers/observe.py sha256=50cd7ada1823093a54b9fb9f1e17707489dcba22d153dd4132459f8e0f182d2d`.

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

### Stateful research qualification, 2026-10-29

`make build JOBS=4` completed the patched QBox runtime and launcher.
`cargo test --locked --bin qcom-vp` passed 123 host tests.
Six targeted platform tests passed with `--include-ignored`.
They cover explicit roots, the Lua profile, default crypto behavior, managed-key ownership, and existing UFS behavior.

The RPMB procedure passed fresh, resume, and clone execution through the authored guest.
All three runs reached `0x600d0000` after their assertions.
The child returned two for that deliberate unmapped access, and the launcher correctly returned failure.
The continued runs preserved the RPMB bytes while checking authenticated reads and rejected writes.

Additional executions rejected empty, truncated, corrupt, and wrong-geometry backing files.
They also checked writer exclusion, partial temporary-file recovery, and refusal of a temporary symlink.
The launcher rejected changed source bytes and a different manifest before guest execution.
The separate original-firmware procedure passed its five operations, including restart and tamper rejection.

These results do not establish a new OEM boot milestone or physical equivalence.
The retained record identifies inputs, commands, results, and limits:
`artifacts:2026-10-29-stateful-environments/qualification.json sha256=e06bdb953bbb5af840054bbc67bfa5e034ffc264e28d7acd03daeb2d436f68e5`.



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
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
| Secure files | Original local-table routines qualify constructed contents, restart, and tamper rejection in bounded instruction emulation. Native QBox execution also qualifies provider construction and three host-authored derivation calls. | Normal secure-file callers, active version authority, and complete index and payload lifecycles remain unqualified. |
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
| Derivation algorithm and root | Native construction uses the virtual OEM key. Host-authored native calls match an independent KDF for cached, AES-128, and AES-256 keys | Normal secure-file callers and physical applicability |
| Local version authority | Selected original routines accept coherent constructed contents across process restart | Complete index and payload lifecycles |
| Phone authority selection | Existing records do not identify its active backend | An applicable trace or passive state observation |
| Hardware root models | QBox connects UID `0x0b` construction to the QCE OEM-key path and its next derivation stage to supplied-key CMAC | Complete protected-file operations and separate TME consumers |

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
The [active native experiment](#native-supplied-key-derivation) exercises this stage through supplied-key CMAC.
The model now supports that request.
Constructor success alone still does not qualify secure-file derivation.

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

### Native supplied-key derivation

This experiment uses the same TZ image and declared virtual roots as the preceding provider trace.
Original boot execution creates the UID `0x0b` object and initializes its cached key.
The instrument then submits three host-authored calls to the original operation-one dispatcher.
The original allocator supplies the request memory.
The calls use a public label, a public context, and a 32-byte output buffer.

This is an active virtual experiment, not passive observation of a stock caller.
The instrument writes call arguments, the native entry PC, and the return address.
It writes descriptors and public inputs into its allocated memory.
It changes no firmware instruction, provider state, fuse policy, or provider result.
The instrument reads the virtual cached key to calculate an independent expected result.
No phone operation or physical-key capture occurs.

Python `cryptography` calculates counter-mode AES CMAC independently from the QCE implementation.
Each input contains a four-byte big-endian counter, the label, a zero separator, the context, and the requested bit length.
The result comparison covers all 32 output bytes.

| Native operation-one key input | Before the change | After the change |
| --- | --- | --- |
| Null input, selecting the cached key | Status one and `unsupported-aes-key-source` | Status zero and exact oracle match |
| Explicit 16-byte key | Not exercised | Status zero and exact oracle match |
| Explicit 32-byte key | Not exercised | Status zero and exact oracle match |

The native driver writes CONFIG `0xe020e` before its AUTH_KEY words.
Those words match little-endian loads from the supplied key.
The initial AUTH_SEG_CFG value is `0x25e42` for AES-128 or `0x25e52` for AES-256.
The driver writes GO value seven through plain GOPROC, not the OEM alias.
The [model contract](../models/qcom/gpce/evidence.md#cmac-with-a-supplied-key) records the write-time byte lanes and GO-time key capture.

Four boot attempts stopped before an active derivation call.
Their watchdog failures do not qualify this path.
The successful instrument removes the UEFI checkpoint and observes the native constructor directly after the verified secure bridge.
The failed attempts remain in the private record.
This result establishes neither deterministic boot nor the cause of those startup failures.

Use the explicit input variables from the preceding procedure.
The prepared firmware remains unchanged.
The active instrument also requires Python `cryptography`.

```sh
EVIDENCE="${QCOM_VP_REF_ARTIFACTS:?}/2026-10-29-software-key-cmac"
python3 "$EVIDENCE/active-provider.py" \
  --prepared "$WORK/prepared" --tz "$TZ" \
  --system "$SYSTEM" --platform "$SM8850_PLATFORM" \
  --out "$WORK/active-observation"
```

The completed experiment reports:

```text
NATIVE_DERIVATION cached status=0 oracle=True
NATIVE_DERIVATION supplied128 status=0 oracle=True
NATIVE_DERIVATION supplied256 status=0 oracle=True
EXPERIMENT_MODE active-virtual-call original-provider no-provider-substitution
```

The instrument stops its private VM after the calls.
It does not resume the stock caller after changing the call state.
This result qualifies the native derivation operation and its crypto dependency, not caller admission or a complete secure-file lifecycle.
The active version authority, protected-file cipher operations, and physical compatibility remain separate questions.

Retained evidence:

- Failing native call: `artifacts:2026-10-29-software-key-cmac/before.json sha256=d8abfc64aeff4f1c6aeb158ec777a3d382ecb6755934d1762e526fac6a4e5441`.
- Passing native calls: `artifacts:2026-10-29-software-key-cmac/final.json sha256=cbbc623813267d9459b71bbcebef6de17af5edff9ebaac3c0134ff1ab0b7c1dc`.
- Active instrument: `artifacts:2026-10-29-software-key-cmac/active-provider.py sha256=9161641fe600c31293ffa3ff6e7e22e9a08fb397432b57fd3b5dda5026febc87`.
- Startup limits: `artifacts:2026-10-29-software-key-cmac/prerequisite-failures.json sha256=f02d7355c403c3f8b8dbc9ebd6dece0abe3c8c1e5758cff6156cb4cd745449ea`.

### Nonsecure client transport and object contracts

A nonsecure client reaches QTEE through two layers: the `smcinvoke` driver and the QCOM SCM invoke service.
The [OnePlus driver interface](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/vendor/qcom/opensource/securemsm-kernel/include/uapi/linux/smcinvoke.h) defines the ioctl request, argument union, and callback structures.
The [SI client](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/vendor/qcom/opensource/securemsm-kernel/smcinvoke/si_core_xts/smci.c) and [compatibility client](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/vendor/qcom/opensource/securemsm-kernel/smcinvoke/compat/smcinvoke.c) define handles and object lifetime.
The [OnePlus SCM call](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/firmware/qcom/qcom_scm.c) and [register setup](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/firmware/qcom/qcom_scm-smc.c) define service identifiers and return registers.
The [object header](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/vendor/qcom/opensource/securemsm-kernel/include/linux/smcinvoke_object.h) defines counts and error codes.

These revisions publish OnePlus 15 release `16.0.9.400` for CPH2745, CPH2747, and CPH2749.
They date from July 2026, while the earlier Google securemsm reference dates from April 2024.
The UAPI, client-environment, object, and opener headers have identical Git blob IDs in both sources.
The SI kernel wrapper also matches, but the two main client implementations differ.
The comparison does not identify the compiled configuration of the selected `16.0.5.700` firmware.

The OnePlus securemsm subtree is `22c99ed40b699512f8244f0cccd23268b5af0d54`.
It also matches the branch's November 2025 initial import.
The July release names Qualcomm tag `AU_LINUX_ANDROID_LA.VENDOR.16.2.0.R1.11.00.00.1277.078` for securemsm.
The matching kernel names tag `AU_LINUX_KERNEL.PLATFORM.5.0.R1.00.00.00.099.064`.
These source identities establish lineage, not secure-firmware policy or physical-device behavior.

#### Transport framing

The following framing describes the selected ARM64 standard SCM path.
The compatibility backend also supports a legacy command.
SCM can select a HAB-mediated conduit from device-tree configuration.
Source availability alone does not establish the active backend.

1. The caller issues an SMC64 standard call with owner 50, function `(0x06, 0x02)`, and argument information `0x224`.
2. The four arguments are the inbound address, inbound size, outbound address, and outbound size. Both sizes are page multiples.
3. The reply returns transport status in `a0`, response type in `a1`, object result in `a2`, and data in `a3`.
4. The transport handles interrupted calls and wait-queue statuses before it maps a final error to a negative errno.
5. A callback response uses function `(0x06, 0x01)`, argument information `0x22`, and the outbound buffer address and size.
6. The inbound buffer starts with a 12-byte header of handle, operation, and counts.
7. The header precedes an argument array of 8-byte slots. A buffer slot holds an offset and a size. An object slot holds a 32-bit handle.
8. Payloads follow the array at 8-byte alignment. The counts field packs buffers-in, buffers-out, objects-in, and objects-out in four nibbles.
9. Operation `0xffff` is release. Operation `0xfffe` is retain.

The [SI allocator](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/firmware/qcom/si_core/si_core.c#L443-L491) aligns both buffers to pages.
It rounds the variable inbound allocation to a page multiple and allocates 32 KiB for the outbound buffer.
The bounded firmware probes below use smaller buffers and do not reproduce that allocator.
The [SI adapter](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/firmware/qcom/si_core/qcom_scm_invoke.c) owns busy retries outside the no-retry SCM call.

#### Root environment and object lifetime

- Userspace opens `/dev/smcinvoke`. The open file holds the shared root object with handle one.
- The ioctl argument union has 24 bytes, distinct from an 8-byte wire slot. The driver rejects a different argument size.
- Both userspace backends refuse root operations 4, 8, and 9.
- Userspace operation 5 requires counts `(0,0,1,1)` and a nonnull credential object.
- The kernel client uses operation 5 with a null credential object through its kernel object interface.
- The [registration interface](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/vendor/qcom/opensource/securemsm-kernel/include/linux/IClientEnv.h) defines four distinct operations.
- Operation 1, `registerLegacy`, takes a credential buffer and uses counts `0x1001`.
- Operation 2, `register`, takes a credential object and uses counts `0x1100`.
- Operation 3, `registerWithWhitelist`, also takes a UID array and uses counts `0x1101`.
- Operation 5, `registerWithCredentials`, takes a credential object and uses counts `0x1100`.
- The client environment serves operation 0 (`open`) with a 4-byte UID buffer and one output object. UID zero is not valid.
- A returned object becomes a file descriptor. The descriptor owns the reference, and its close releases the object.
- The root object is indestructible. The driver rejects its export and skips its release.
- Kernel clients process retain and release in the local object table. A userspace client must keep the descriptor to keep the object.

#### Buffer ownership and address translation

- The interface passes user virtual addresses. The driver copies the bytes into kernel memory or into page-aligned shared memory.
- The secure side receives offsets into the shared buffer or its physical address. It never receives a user address.
- The driver copies output bytes back only when the object result is zero.
- Memory objects are DMA-BUF references. The compatibility driver encodes a handle with a local bit, a 15-bit object identifier, and a 16-bit server identifier.
- The SI backend instead allocates object IDs with the nonsecure bit set. Its encoding does not use the compatibility split.
- Object arguments carry handles, never pointers.

#### Error domains

| Layer | Value | Meaning |
| --- | --- | --- |
| ioctl | `0` | The request completed. Read the object result. |
| ioctl | Negative errno | Argument, copy, memory, or descriptor failure in the driver. |
| SMC | `a0 = 0` | The transport completed. `a2` holds the object result. |
| SMC | `a0 != 0` | The transport returned a nonzero status. The public client treats the object result as invalid. |
| Object | `0` | Success. |
| Object | `1` to `4` | General error, invalid request, input size, output size. |
| Object | `10` and above | Caller-defined codes. |
| Object | `-90` through `-99`, and `-103` | Named transport object errors. The header does not define every intervening value. |

#### Executed transport observation

Five bounded runs use the same caller logic, selected TZ image, system executable, and prepared inputs.
They compare ID9 at EL2, an authored EL1 continuation, and the QHEE handoff at EL1.
The ID9 scratch window starts at `0xe0000000`, while the QHEE scratch window starts at `0xf0000000`.
The debugger reads the guest view of memory.

| Boundary | Observation |
| --- | --- |
| ID9 SMC delivery | The call traps to the TZ EL3 vector at `0x1468fc00`. `VBAR_EL3` reads `0x1468f800`. The vector literal names handler `0xd820cbc8`. |
| QHEE SMC delivery | The call traps to EL2 at `VBAR_EL2 + 0x400` in mode EL2h. The trap and handler bytes match the selected `hypvm.mbn` at its `0x2c00` and `0x2400` offsets. The hypervisor path then enters the TZ EL3 vector at `0x1468fc00` and handler `0xd820cbc8`. |
| ID9 continuation, EL2 | Every object request returns the 32-bit value `0x80000001` in `a0`. The register holds `0xffffffff80000001`. The inbound and outbound pages stay unchanged. |
| ID9 then authored EL1 handoff | The guest reports `CurrentEL` one. Every object request returns the same `0x80000001`. The pages stay unchanged. |
| ID9 continuation, EL2 | The call-availability probe returns `-1`. This result differs from the object requests for a different function identifier. |
| QHEE to UEFI entry, EL1 | The QSEECOM version query returns `0` in `a0`. The first result register reads `0x1402000`. |
| QHEE to UEFI entry, EL1 | `register` with null credentials returns `0` and exports object two. |
| QHEE to UEFI entry, EL1 | `registerWithCredentials` with a null object returns `0` and exports object three. |
| QHEE to UEFI entry, EL1 | `open` on the root object returns object result `2` for UID zero. |
| QHEE to UEFI entry, EL1 | `open` on the environment returns object result `11` for UID `0x0b` and for UID `0x91`. The interpretation is provisional. |
| QHEE to UEFI entry, EL1 | `retain` and `release` on the environment return object result `-92`. |
| QHEE to UEFI entry, EL1 | `open` on the environment still dispatches after a failed retain. |

The ID9 refusal remains at both tested exception levels.
The authored EL1 handoff uses the same caller logic, buffer layout, and TZ image.
The uniform `0x80000001` result also applies to a call with immediate arguments only, so the refusal is not a message-layout effect.
The two paths differ before EL3. At ID9 the SMC enters the TZ EL3 vector directly, and TZ refuses it.
After QHEE the same SMC enters the hypervisor at EL2 first. The hypervisor then enters the same TZ EL3 vector and handler, and the call returns success.
The trap vector and handler bytes match the selected `hypvm.mbn` and `tz.mbn` images.
The `hypvm.mbn` image has SHA-256 `1a480f0020c95fa1432f88e13543a6ebcc9521f0d3d3d67975a93cd861c9af6e`.
TZ is therefore in both paths. We did not establish which component decides the refusal, and the hypervisor stage is not modeled by this repository.
The object results use the public object table for `2` and `-92`.
The value `11` equals the privilege code in the public opener header. We did not check that the client environment uses that table.
We also did not establish which branch returned `11`. It may be an invalid-handle dispatch.

#### Forwarded call metadata

The QHEE path preserves the client function identifier, argument information, and arguments.
For the version query the EL2 trap, the EL3 vector, and the saved handler frame hold `x0=0x42000603`, `x1=0x1`, and `x2=0xa`.
For `register` they hold `x0=0x72000602`, `x1=0x224`, and `x2..x5=0xf0001000`, `0x1000`, `0xf0002000`, `0x1000`.
The saved frame at handler `0xd820cbc8` reproduces the same block.
The hypervisor sets `x7` to `3` and changes `x9` to a pointer. `VTTBR_EL2` carries VMID `3` in the same call. We did not qualify the relation between `x7` and the VMID.
The EL3 entry reports `ESR_EL3` exception class `0x17`, `SPSR_EL3` mode EL2h, and `ELR_EL3` inside the hypervisor.
`VTTBR_EL2` reads `0x30008bd900000` with `HCR_EL2=0x30c807d003b`.
The direct ID9 EL1 call enters the same vector with `x0..x5` equal to the caller, `x7=0`, `SPSR_EL3` mode EL1h, `VTTBR_EL2=0`, and `HCR_EL2=0x80000000`.
Both paths pass the same buffer addresses in the caller view. The hypervisor does not remap them before EL3.
A direct EL1 control sets `x7=3` and keeps every other field. The TZ EL3 vector receives `x7=3`, and the version query still returns `0x80000001`.
The `x7` value alone therefore does not admit the call. Its meaning remains unknown.
An implementable first transaction supplies the standard client SMC, the page addresses, and the caller context that the secure policy requires.
A producer of that context must supply the required value. This section does not recommend a fixed `x7` for other callers.
The remaining differences include the caller origin, preceding stage, and secure state that QHEE establishes.

#### Contract limits and next prerequisite

- The caller is an authored nonsecure SMC client. It is not the Linux driver, and it holds no descriptor table.
- The caller sends null credentials. It loads no TEE application and reaches no KeyMint or protected-file operation.
- The UID opens return a refusal from the secure side. Their interpretation as an authorization decision is provisional.
- The transport observations qualify framing, root registration, environment open admission, and the refusal paths.
- They do not qualify descriptor lifetime, callback service, memory objects, or a successful service open.
- The hypervisor stage between EL1 and the TZ EL3 vector is original firmware, not a modeled service.
- The `invoke-callback-response` probe used the invoke function with two arguments. It is a malformed invoke negative. No callback-response transaction occurred.
- The remaining prerequisite for a service transaction is a registered client environment that admits the target UID.
- We did not qualify the environment identity, the admitted operation set, or the origin of the `11` refusal.
- The next discriminator is operation 1 with a real credential buffer. The kernel descriptor path is an alternative.
- A read-only query on an admitted UID follows either path.
- Root and open readiness are separate from KeyMint and protected-file lifecycle readiness.

Retained evidence:

- ID9 refusal: `artifacts:2026-10-30-secure-client-contracts/result-id9.json sha256=ea9c6218546a08516a81c9f7f790690f658eba5c3dca204b8a59dec9cd4b5a9a`.
- ID9 EL1 control: `artifacts:2026-10-30-secure-client-contracts/result-id9-el1.json sha256=a3c35074034eea67076c9a3f019b7aecafe13ec0b58e828e0cd8c3832ab14acf`.
- EL1 caller: `artifacts:2026-10-30-secure-client-contracts/observe-client-id9-el1.py sha256=77b52bcf9ff1d00cb544ee81b3265a8f8ff9c48fcfa9bec29322037515f55cdb`.
- QHEE probes: `artifacts:2026-10-30-secure-client-contracts/result-qhee-probes.json sha256=6aef5c0a22d1adc4f8d0b478b535193ba06289640fad317edb108cd1dd3c6378`.
- QHEE SMC trail: `artifacts:2026-10-30-secure-client-contracts/result-qhee-smc-trace.json sha256=a577aaa5d8d2f80c4ac2f069b09874f35b838c73692940a2d9b777da127db6b6`.
- QHEE endpoint bytes: `artifacts:2026-10-30-secure-client-contracts/result-qhee-endpoint-trace.json sha256=6bcb840c52aee3cbba5b1c127ab775cd2e650f95bebd4717d6a227862fa4163a`.
- QHEE EL3 confirmation: `artifacts:2026-10-30-secure-client-contracts/result-qhee-el3-confirm.json sha256=a4629f0cfc3bba4282ede8f85143ac6c5eef3ddb6a8fb48d23c0f3067ecf2388`.
- QHEE forwarding metadata: `artifacts:2026-10-30-secure-client-contracts/result-qhee-forwarding.json sha256=25a1a923c1451d05cb14fb2831823d488b0cab18b44227909643551f4bb4a27b`.
- Forwarding instrument: `artifacts:2026-10-30-secure-client-contracts/observe-client-forward.py sha256=4709a186a1301faaa12c9c80ec9817e518407da8b471a687d883dd80eee2fe24`.
- ID9 direct vector registers: `artifacts:2026-10-30-secure-client-contracts/result-id9-direct-vector.json sha256=d48c3d1b1bf58969c63c1f995e1da53272865ee79029597105086f286ac94ad6`.
- ID9 `x7=3` control: `artifacts:2026-10-30-secure-client-contracts/result-id9-x7-3.json sha256=67b5b13ad802d4b0433cfac1af9e6b40f5c295b372e31be6cc77558c34d3b40f`.
- Direct-vector instrument: `artifacts:2026-10-30-secure-client-contracts/observe-client-direct.py sha256=e67f96cd69a1acc66dbc0c259e3d47f97dbe5787600105445df0759cb0306d3a`.
- Endpoint instrument: `artifacts:2026-10-30-secure-client-contracts/observe-client-endpoint.py sha256=eb68f3896a545d9498cd7011ae27c0e38b4504eb14ef3fb2065745562ee7254b`.
- EL3 instrument: `artifacts:2026-10-30-secure-client-contracts/observe-client-el3.py sha256=19fe403fe5b8d3e2d46018140c125984d0510c2f9df30791cc443bcaf95c8b5a`.
- QHEE object chain: `artifacts:2026-10-30-secure-client-contracts/result-qhee-chain.json sha256=56fa1322cb450720b105b985f2f3abe5d7c993d8f841615596caf3a8acdc3de9`.
- QHEE lifetime probes: `artifacts:2026-10-30-secure-client-contracts/result-qhee-lifetime.json sha256=c69010c8a37601bbedabb2dbf9f14b44178966138c0cf218f38c895b92881102`.
- ID9 caller: `artifacts:2026-10-30-secure-client-contracts/observe-client-id9.py sha256=801f62ca4fc0c2f972762584b255df10d3b80ee02bc5e24a96fc69107ba4384b`.
- QHEE caller: `artifacts:2026-10-30-secure-client-contracts/observe-client-qhee.py sha256=de1e63187785785aa6a5edd4446801d2feedf44598938c71f33c8793724ac9e7`.
- Vendor source comparison: `artifacts:2026-10-30-secure-client-contracts/vendor-source-review.json sha256=7f18eea9309127d16cc550e4628470a897c69be6dcc6427962f068b8de03ed50`.

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
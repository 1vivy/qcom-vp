# Research environments

qcom-vp combines component models, firmware, and QBox Lua descriptions into research environments.
A device boot is one experiment. A subsystem with authored guests is another.
Neither requires a separate hardware configuration framework above QBox.

## Responsibilities

| Location | Responsibility |
| --- | --- |
| `models/qcom/` | Shared component blocks, interfaces, and evidence |
| `models/qcom/sm88xx/` | Family blocks and inferred family recipes |
| `models/qcom/sm88xx/<soc>/` | Component evidence for one SoC |
| `platforms/qcom/` | Reusable composition such as the APSS GIC |
| `platforms/qcom/sm88xx/` | Family recipes and the SoC overrides |
| `boards/qcom/sm88xx/<vendor>/` | Device choices for the existing OEM boot workflow |
| `environments/` | Experiment procedures, declared conditions, preparation tools, and observation checks |
| `tools/qcom/` | Input preparation, execution control, and evidence handling |
| `docs/` | Runbooks, architecture, decisions, evidence reviews, and qualification limits |
| Ignored output | Local firmware, copied inputs, mutable disks, and run evidence |

The directory names group components by manufacturer, family, SoC, and board.
They form a practical catalog. They do not certify that two devices behave the same.
A family recipe records an inference from BSP or hardware-catalog patterns.
The recipe must produce observable effects. It must also name the evidence behind the pattern and any open part.

Models do not select behavior by phone name or SoC name.
A changed component contract needs a model version and evidence for that difference.
Do not create a version for each name when the contract does not change.
A provisional component identity is acceptable if the model states the provisional part.
Do not add a generic inheritance framework merely to avoid a small amount of composition data.

## Choose the execution path

`qcom-vp prepare` and `qcom-vp run` retain the existing OEM boot workflow.
That workflow assembles package storage and can observe a hash-bound kernel entry.
Its supported package layout and ROM geometry are reference constraints, not universal hardware facts.
See [firmware inputs](qcom/sm88xx/oplus/firmware-inputs.md) for that workflow.

`qcom-vp experiment` executes an external QBox Lua composition with declared input files.
It requires no OEM package, board whitelist, PBL size, or kernel profile.
Lua defines the required components, firmware loaders, reset vectors, and debugger settings.
A storage-less experiment declares no disk. An experiment with storage supplies its image files and describes their consumers in Lua.

```sh
export WORK="$PWD/build/private"    # any new output directory works
qcom-vp experiment --environment /path/to/environment.toml \
  --out "$WORK/experiment-1" \
  --system build/install/bin/qcom-vp-system \
  --timeout-seconds 30
```

The environment manifest is local configuration. The repository does not track one.

The output directory must be new.
Without `--system`, the launcher selects `qcom-vp-system` beside itself.
The [IPCC example](../environments/qcom/ipcc/README.md) supplies a complete preparation and observation procedure without vendor firmware.

## Environment contract

Schema 1 records the experiment purpose, entry conditions, security policy, limitations, and input identities.
Every required Lua helper, guest image, and storage image belongs in the file list.
Paths to source files are relative to the environment manifest unless absolute.
Names select destinations under the run's `inputs/` directory.

```toml
schema = 1
name = "local-subsystem"
description = "Which interrupt and queue transitions follow a peer request?"
entry = "Authored guests start at declared reset vectors with zeroed shared RAM."
security_policy = "Synthetic guests exercise modeled access boundaries. No OEM authentication claim."
limitations = ["No physical timing claim", "No OEM firmware or kernel execution"]
platform = "platform.lua"

[[files]]
name = "platform.lua"
path = "platform.lua"
sha256 = "<SHA-256 of this Lua file>"

[[files]]
name = "guest-dump.bin"
path = "guest-dump.bin"
sha256 = "<SHA-256 of the complete dump>"
```

The example hashes above are placeholders, not runnable input identities.
Preparation must calculate hashes from the actual files.
The launcher checks hashes and copies files before execution.
It records the environment manifest, input identities, and the selected system executable identity.

Lua receives these environment variables:

| Variable | Meaning |
| --- | --- |
| `QCOM_VP_ENV_ROOT` | Absolute path to the copied inputs |
| `QCOM_VP_RUN_ROOT` | Absolute path to the run output |
| `QCOM_VP_STATE_ROOT` | Absolute path to declared mutable state, when the environment declares state |

Lua should resolve input files from `QCOM_VP_ENV_ROOT` and write logs under `QCOM_VP_RUN_ROOT`.
Use `bin_file_offset` and `bin_file_size` to select bytes from a dump.
Use memory addresses and CPU reset parameters to define their place in the machine.
Those are different coordinates. A file offset never establishes a reset address.

### Persistent state

Optional `[[state]]` entries declare the complete collection of persistent files.
Each entry has a relative `name` and an optional `seed`.
The seed names an immutable file from `[[files]]`, not an arbitrary host path.
Without a seed, the file does not exist initially. Its component must initialize it through the declared behavior.

```toml
[[state]]
name = "lun0.img"
seed = "initial-lun0.img"

[[state]]
name = "rpmb.bin"
```

The manifest must also declare and hash `initial-lun0.img` in `[[files]]`.
Lua selects mutable paths under `QCOM_VP_STATE_ROOT`.
For example, the UFS parameter `rpmb_state_file` selects `QCOM_VP_STATE_ROOT .. '/rpmb.bin'`.
The component owns the file format and checks its contents.
The launcher does not interpret or invent provisioned records.
Stateful output directories use owner-only access because profiles and state can contain private virtual keys.

Stateful environments treat every file under `inputs/` as immutable.
The launcher checks those hashes after the process stops.
It records initial and final identities for every declared state file, including absence.
It rejects undeclared files, symbolic links, and nonregular files under the state directory.
A changed input or incomplete inventory prevents a final state record.

### Fresh, resume, and clone

| Choice | Initial persistent state | Identity |
| --- | --- | --- |
| No continuation option | Declared seeds, with unseeded files absent | The declared profile |
| `--resume-from RUN` | Complete final state copied from `RUN` | Preserved |
| `--clone-from RUN` | Complete final state copied from `RUN` | Preserved |
| New identity | A new profile with fresh seeds | Explicitly changed |

Both continuation options require a new output directory and preserve the source run.
Resume declares a continuation. Clone declares an independent branch with the same initial identity.
Neither option resumes processor execution or restores volatile state.
The composition still establishes its declared entry condition.

```sh
qcom-vp experiment --environment "$WORK/environment.toml" \
  --out "$WORK/fresh" --system build/install/bin/qcom-vp-system
qcom-vp experiment --environment "$WORK/environment.toml" \
  --resume-from "$WORK/fresh" --out "$WORK/resumed" \
  --system build/install/bin/qcom-vp-system
qcom-vp experiment --environment "$WORK/environment.toml" \
  --clone-from "$WORK/fresh" --out "$WORK/cloned" \
  --system build/install/bin/qcom-vp-system
```

Continuation requires the same manifest hash and system executable hash.
The launcher checks the source state against its final record before and after copying.
It also checks the copied state.
A modified profile, missing file, changed counter file, or incomplete result prevents execution.
All dependent media, protected files, counters, and provisioning records belong in the same declaration.

To initialize another identity, prepare a new profile and start without a continuation option.
Do not combine new root material with copied objects that depend on the previous identity.
The launcher deliberately provides no option to ignore that mismatch.

A final record identifies every declared state file after the process stops.
It does not mean that a guest transaction completed or that the files form a valid application state.
A failed guest can leave state that requires firmware recovery.
The experiment checker must qualify that recovery separately.
Do not change or run the source collection while copying it.

The OEM `run` command still creates fresh LUN copies and fresh RPMB state.
Its `rpmb.state` file retains the RPMB result for that run.
Use a declared experiment for complete persistent-state continuation.

### Explicit identity inputs

Keep the profile in a declared immutable Lua file.
The shared `platforms/qcom/sm88xx/security-roots.lua` helper applies its fields to existing component instances.
Both SoC wrappers accept those fields in their options.
The OEM launcher obtains them from its hashed board file.

| Input | Owner and effect | Empty or absent value |
| --- | --- | --- |
| `oem_hw_key_hex` | QCE uses exactly 32 bytes for OEM-key AES-256 CMAC | Existing public-label derivation |
| `virtual_root_hex` | TME uses exactly 48 bytes for its virtual managed-key derivation | Existing public-label derivation |
| `rpmb_size_kib` | UFS declares region geometry in multiples of 128 KiB | Shared reset composition uses 4096 KiB |
| `rpmb_state_file` | UFS retains the programmed key, counter, and media in a local file | State lasts only within the process |

Root strings contain hexadecimal characters without a prefix or whitespace.
The models reject invalid lengths and characters before simulation.
A nonempty TME root requires the existing `virtual_key_substitute=true` selection.
Explicit roots do not change image authentication or authorization policy.
Existing fuse and trust inputs continue to select those policies.

The [native provider trace](environment-inventory.md#native-provider-and-hardware-dependency) connects UID `0x0b` construction to the QCE OEM-key path.
The constructor binds its derived key to a firmware environment identity.
This result does not qualify the complete secure-file derivation or connect that provider to the TME root.
An explicit value needs provenance. Its presence alone proves neither a physical derivation nor compatibility with device-bound contents.
The default roots remain public test material.

## Trust and reproducibility boundaries

Lua executes with host permissions. The experiment is not a sandbox.
A script can access external files or programs. The launcher cannot prove that its declared file list is complete.
Review the composition for undeclared inputs before relying on repeatability.
Never run an untrusted composition merely because its hashes match.

The system executable hash does not identify every shared library or host dependency.
Record the checkout revision and dependency pins with the experiment procedure.
Record toolchain versions when generated guest bytes depend on them.
Stateless experiments can use copied storage under `inputs/`.
Stateful experiments use immutable inputs and separate files under `state/`.
The recorded input hashes describe initial bytes. Final state identities describe bytes after the process stops.

The experiment command does not apply the OEM artifact-retention policy.
Retain evidence that supports active claims before removing your own experiment output.
It never prunes another run to satisfy an OEM storage budget.

Matching inputs do not guarantee identical schedules, entropy, host timing, or device behavior.
No snapshot or deterministic replay capability follows from this manifest.
A memory capture alone does not restore processor, interrupt, timer, or peripheral state.

## Primary runbook

1. State the research question and requested stopping point.
2. Identify the behavior that must match the target.
3. Select independent evidence for the expected behavior.
4. Compose the components and their dependencies in Lua.
5. Declare firmware identities, initial state, entry procedure, and security policy.
6. Record substitutions and unresolved conditions that could affect the result.
7. Prepare the environment from its declared inputs.
8. Execute the experiment in a new output directory.
9. Inspect the process outcome and the relevant guest observations.
10. Exercise a negative case that distinguishes failure from the claimed result.
11. Repeat from the same declared initial conditions in another new directory.
12. Record the conclusion, commands, inputs, and limitations.

A requested state is a stopping point, not a reduced implementation contract.
Complete the affected behavior, including dependencies, effects, failures, evidence, and verification.
Do not add unrelated speculative hardware.
These obligations apply equally to local work, unpushed commits, forks, and upstream contributions.

## Interpret outcomes

The launcher retains `stdout.log`, `stderr.log`, and `result.json`.
The result distinguishes process exit, timeout, and launcher errors.
Nonzero platform status and timeouts remain command failures.
The launcher does not reinterpret an expected guest fault as process success.

An experiment-specific checker can establish a narrower result from those observations.
For example, the IPCC guest reaches a deliberate unmapped access only after its behavioral checks.
Its checker must distinguish that access from a different fault, timeout, or incomplete guest execution.
The resulting statement concerns those checks, not kernel entry or physical security qualification.

For EL2 work, state which translations, exception routes, interrupts, and privilege transitions the environment reproduces.
For TrustZone work, state the loader, client interface, shared-buffer rules, trust policy, and firmware dependencies.
An environment that omits authentication cannot qualify retail authentication.
A constructed entry state cannot establish that the omitted boot path executed.

## Model and evidence runbook

1. Locate the consumer and the missing or incorrect behavior.
2. Establish the required contract from permitted sources or firmware analysis.
3. Identify the relevant model version and composition dependencies.
4. Implement observable effects, ordering, completion, and failure paths within that bounded contract.
5. Exercise the behavior through a real guest or component consumer.
6. Retain regression tests for meaningful boundaries and transitions.
7. Update the component evidence and environment procedure.

An unsupported access is a research lead, not permission to return a convenient value.
Do not suppress faults or synthesize firmware acknowledgements to reach a milestone.
Preserve the distinction between observations, inferences, and deliberate virtual policies.

A family recipe is an inference, not a certified hardware fact.
Implement it through declared composition, and give it the effects that a guest can observe.
A recipe that only returns a convenient value does not satisfy this contract.
Record what evidence would support the pattern or require separate contracts.
Examples include a firmware trace at the affected register or a device probe of the same field.
A permitted BSP or catalog entry with a matching address map can also supply evidence.

Permitted evidence includes firmware analysis, device observations, and properly licensed public sources, including applicable CLO/LA material.
References to private research repositories are acceptable.
They identify evidence. They do not make a private archive a build prerequisite.
The retained source-only lists are investigation leads, not qualified evidence.
The [provenance reviews](environment-inventory.md#provenance-review) inventory the remaining evidence gaps as concrete questions.
They do not form a blanket publication blocker.
Keep each specific unresolved claim visible with its missing observation.
Do not consult prohibited sources again, and keep nonredistributable material outside the repository.
The [evidence guide](evidence.md) records how source identities and private references stay traceable.

## Reference qualification

The [environment inventory](environment-inventory.md) identifies the current execution paths and their limits.
The [rework plan](research-environments-plan.md) separates the repository boundary from later environment qualification.
Qualification applies to a stated behavior under stated conditions, not to every feature of a named phone.

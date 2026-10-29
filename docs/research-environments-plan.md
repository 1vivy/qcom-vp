# Research environments rework plan

Status: implementation active.
The environment contract, research launcher, runbooks, and bounded IPCC example have executable coverage.
The [environment inventory](environment-inventory.md) records the current capabilities and their limits.
The catalog migration is complete, and both OEM references have fresh preparation and execution records.
Complete reference qualification remains open in [B4](#b4-qualify-the-reworked-environments).

The repository boundary covers actual nonredistributable material exclusions and accurate statements of current capability.
The [evidence guide](evidence.md) records the source-identity and local-location rules.

## Purpose

qcom-vp provides models and execution environments for Qualcomm hardware and firmware research.
Researchers compose components, establish repeatable operating conditions, and use those environments for development, debugging, and security testing.
Modeling creates the environment. Researchers must also be able to work within it after modeling ends.

Specific devices and complete boot sequences are results of composition, not the limits of the project.
Infiniti and SM8845 remain reference configurations and evidence sources.
The real boot chain remains an important integration check and a way to establish firmware state.
It is not a prerequisite for every useful experiment.

Examples include EL2 escalation research, TrustZone interface analysis, and trusted application development.
These examples guide requirements. They do not establish current support or authorize physical-device operations.

## Principles

1. Reuse QBox composition, parameters, and interfaces instead of creating a competing hardware configuration framework.
2. Separate reusable components from environment definitions and experiment procedures.
3. Group reusable components by manufacturer, family, and SoC override. Treat that grouping as a catalog, not a certification.
4. Separate file offsets from guest addresses, reset vectors, and hardware geometry.
5. Declare firmware identities, security policy, dependencies, and initial conditions.
6. Preserve authentication and authorization behavior. Never substitute success for an unknown or rejected operation.
7. Distinguish observations, inferences, virtual policies, and functional substitutes.
8. Require evidence appropriate to each claim, rather than treating further boot progress as proof of correct behavior.

Repeatable virtual conditions do not establish equivalence to physical hardware.
Each experiment identifies the behavior that must match the target and the differences that could affect its conclusion.
Timing, entropy, asynchronous inputs, and scheduling can prevent identical execution even when inputs match.
Do not promise deterministic replay or complete state restoration without evidence for those capabilities.

### State goals and complete contributions

A requested state is a stopping point for an experiment, not a reduced correctness standard.
A request to reach a boot stage, exercise an interface, or load an application is a stopping point.
The work required to reach that state must produce correct, reusable models and a usable environment.

Each contribution must complete the bounded component or behavior that it changes.
Completion includes the required dependencies, observable effects, failure behavior, evidence, and relevant verification.
It does not require speculative modeling of unrelated hardware or the whole chip.
Reaching the requested state does not excuse an incomplete implementation of the affected behavior.
Neither an explicit shortcut nor an undocumented shortcut satisfies this requirement.

These requirements apply to local work, unpushed commits, forks, and upstream contributions.
Push status does not change the engineering contract.
Repository guidance and runbooks must state these obligations directly.
A request need not repeat them, and silence is not permission to weaken them.

### Existing knowledge and future source discipline

The existing structured documentation is a foundation for this rework, not material for a blanket rewrite.
Preserve useful component explanations, firmware coordinates, decisions, observations, and limitations.
Correct concrete gaps while retaining the knowledge that already supports further work.

Record permitted-source rules in repository guidance.
Preserve provenance when transferring facts into models, documentation, or experiment procedures.
Review affected claims when a source problem appears, rather than repeatedly auditing all accumulated knowledge.
Public availability alone does not establish permission to use source material.

## Repository boundary

The tracked tree contains no nonredistributable firmware, keys, or device dumps.
Firmware, private manifests, and evidence stores are local inputs.
A retained claim names the evidence that supports it, so a reviewer can inspect the basis.
Current capability statements include their limits.

The [provenance reviews](environment-inventory.md#provenance-review) inventory the remaining evidence gaps.
They are not a blanket declaration that every retained value lacks support.
A reviewer corrects an entry when firmware records or permitted sources contradict it.

References to private research repositories are acceptable.
They identify evidence without making a private archive a build prerequisite.
The [evidence guide](evidence.md) defines the identity and local-location rules for those references.


## Part one: align scope, structure, and runbooks

### A1. Define the environment contract

An environment description must answer these questions:

| Area | Required information |
| --- | --- |
| Purpose | Research question and required fidelity |
| Components | Hardware models, firmware components, versions, and explicit substitutes |
| Composition | Address maps, aliases, interfaces, connections, resets, and dependencies |
| Inputs | Firmware, storage, configuration, trust inputs, identities, and permitted sources |
| Initial conditions | Boot path or constructed entry state, processor state, memory contents, and required prior actions |
| Execution | Launch procedure, external inputs, debugger access, stop conditions, and failure behavior |
| Evidence | Expected observations, comparison source, results, limitations, and unresolved questions |

Keep these descriptions proportional to the experiment. Do not introduce a separate manifest for information that existing configuration already expresses.
Constructed entry states must identify what they omit. They must not claim that the omitted firmware path executed.
A saved memory image alone does not establish complete processor or peripheral state.

Deliverable: a documented contract with examples drawn from the existing configurations.

### A2. Define the repository responsibilities

Use the existing layout where it fits:

| Location | Responsibility |
| --- | --- |
| `models/qcom/`, `models/qcom/sm88xx/` | Shared and family component behavior, interfaces, evidence, and relevant tests |
| `platforms/qcom/`, `platforms/qcom/sm88xx/` | Reusable composition, family recipes, and the SoC overrides |
| `boards/qcom/sm88xx/<vendor>/` | Device-specific choices, without defining the entire research interface |
| `tools/qcom/` | Input preparation, execution control, and observation tools |
| `firmware/` | Input examples and releasable authored adapters, not vendor binaries |
| `docs/` | Architecture, runbooks, environment contracts, decisions, and qualified results |
| Private stores and ignored output | Firmware inputs, mutable execution state, and retained evidence |

Decide the location of environment definitions and experiments after examining the existing compositions.
Avoid directory changes that only rename the existing workflow.
Each new path needs a clear responsibility and an existing example that uses it.
The chosen paths group components by manufacturer, family, and SoC override.
A directory name does not certify identical behavior across a family.

Deliverable: a layout decision and a migration map for current files and callers.

### A3. Expose composition through the research interface

Audit the host tooling for restrictions that belong only to reference configurations.
Review the closed target list, ROM geometry, package assumptions, board selection, and firmware-specific observers.
Preserve the working reference configurations while removing unnecessary restrictions from general execution.

Input descriptions select bytes from supplied material.
Machine descriptions define addresses, reset state, topology, and connections.
Package import and kernel observation remain useful facilities, not universal prerequisites.
Research execution must distinguish its outcome from verified boot or physical security qualification.

Assess direct storage inputs, storage-less execution, and external composition against the agreed experiments.
Do not add arbitrary flags or bypass dependencies to claim portability.

Deliverable: an interface design with executable acceptance scenarios before implementation.

### A4. Write and exercise the runbooks

The primary runbook follows this sequence:

1. State the research question.
2. Identify the required behavior and an independent comparison source.
3. Select or compose the environment.
4. Declare inputs, initial conditions, substitutions, and unresolved dependencies.
5. Establish the initial conditions through the documented procedure.
6. Apply a bounded stimulus.
7. Collect observations at the relevant interface or state boundary.
8. Compare the observations with the expected result.
9. Repeat from the declared initial condition.
10. Record the conclusion and its limits.

Supporting runbooks cover component modeling, firmware integration, environment setup, experiment execution, and evidence review.
Extend existing documentation where possible rather than duplicating procedures.
Exploratory runs remain useful. Accepted discoveries must become explicit contracts or repeatable experiments, not undocumented exceptions.

Part one passes when a researcher can reproduce a bounded experiment without the author's accumulated local state.
The example must exercise composition and observation without requiring a complete phone boot.

## Part two: reconcile and qualify existing work

### B1. Inventory the current implementation

Map current models, firmware adaptations, compositions, host tools, tests, and evidence to the agreed layout.
Record hidden input dependencies, hardcoded device assumptions, local paths, and reliance on previous runs.
Include the recent evidence-review changes. Do not assume that new code already meets the environment contract.

Distinguish sound behavior with weak documentation from behavior that needs correction.
Do not discard working models merely because their original investigation was exploratory.

Deliverable: a bounded migration inventory with evidence gaps and affected consumers.

### B2. Restore evidence integrity

Review the claims that the [provenance reviews](environment-inventory.md#provenance-review) list as evidence gaps.
Prior accumulated evidence, documentation, and device probes supplied the substantive basis.
Use the retained source-only lists as investigation leads, not qualified evidence.

Firmware analysis, device observations, and permitted public sources can support replacement evidence.
CLO/LA sources are acceptable within their applicable licenses. BOOT.MXF is not an acceptable source for new work.
Private research references identify the evidence without becoming a hidden build prerequisite.

For each affected claim:

1. Identify any independent evidence already available.
2. Obtain permitted evidence where the existing record is insufficient.
3. Correct the explanation and implementation when the evidence requires it.
4. Remove a specific claim or dependent behavior from the publication candidate if no permitted basis exists.

Never relabel a source-derived value as virtual policy merely to preserve it.
A deliberate virtual policy needs its own rationale, observable effects, and limits.

Deliverable: an evidence-gap inventory with the exact missing observation for each open claim.

### B3. Migrate existing environments and experiments

Apply the runbooks to SM8850, SM8845, and representative subsystem work.
Select examples that expose different dependencies rather than repeating one successful path.

For each example:

1. Pin its inputs and composition.
2. Document how it establishes the initial conditions.
3. Separate firmware behavior from models, substitutes, and diagnostic intervention.
4. Replace accumulated local state with declared inputs or repeatable preparation.
5. Exercise the relevant transitions, effects, and failure paths.
6. Retain only evidence that supports an active claim or unresolved question.

Firmware reconstruction remains an explicit experiment input, not an implicit property of every device.
A development trust policy must remain distinguishable from retail policy.
A change to the allowed authentication policy needs an explicit, documented decision.

Deliverable: migrated reference environments with procedures that demonstrate their stated contracts.

### B3a. Reconstruct required state and contents

An environment needs coherent contents as well as device interfaces and parameters.
A successful boot does not qualify every service that the firmware publishes.
Qualification follows the research function, its consumers, and their required state.

The state review covers these surfaces:

| Surface | Required relationships |
| --- | --- |
| RPMB and secure files | Geometry, records, ownership, integrity, rollback state, initialization, and persistence |
| KeyMint, RKP, and StrongBox | Provisioning, protected key blobs, certificate chains, service dependencies, and lifecycle |
| EFI variables and boot policy | Values, formats, defaults, validation, and firmware decisions |
| Persistent partitions and OEM records | Required contents, optional contents, producers, and update rules |
| Shared memory and firmware handoffs | Structures, ownership, validity, publication order, and consumer expectations |
| Hardware identity and key services | Derivation inputs, policy, validation, and the effects of virtual substitutes |

The review first uses existing models and evidence.
The review must distinguish missing behavior from missing documentation or an untested service.
It must not assume that every state surface needs replacement.

For each selected research function:

1. Identify the consumer and its required initial state.
2. Trace that state to its producer, backing store, and validation rules.
3. Inspect relevant contents through an authorized observation path.
4. Record the input identity, checkpoint, interpretation, and consistency limits.
5. Compare the observation with current model parameters and contents.
6. Select captured inputs, firmware-generated state, or an explicit virtual profile.
7. Exercise the function with coherent state and effective failure checks.
8. Repeat the experiment from its declared initial condition.

Authorized research can inspect device-specific contents when they answer the selected question.
The observation boundary is not metadata versus contents.
Private capture and public distribution are separate decisions.
Relevant unique material remains private unless its release receives separate authorization.
Analysis outputs must not disclose secret values when structural results suffice.
Research authorization does not permit destructive changes or grant access to unrelated data.
The [hardware campaign](qcom/sm88xx/oplus/infiniti/hardware-probe-plan.md) defines the device-operation boundary.

Captured objects can depend on hardware secrets, counters, firmware versions, or other records.
A copied encrypted object does not establish a usable virtual input.
A research profile must keep keys, certificates, counters, identities, and policy consistent.
Test identities must remain distinguishable from retail identities.
Unknown contents must not become guessed certificates, successful authorization, or unexplained completion records.

The implementation can use four sources of state:

| Source | Acceptance condition |
| --- | --- |
| Observed device contents | The capture identifies its consumer, context, and portability limits. |
| Firmware-generated contents | A repeatable procedure establishes the required state through real firmware behavior. |
| Constructed research profile | Evidence supports the formats and relationships, with explicit test identities and limits. |
| Private device-bound inputs | The experiment establishes their required dependencies without claiming general portability. |

Deliverable: a state inventory that maps each selected function to supported contents, unresolved dependencies, and execution evidence.
An environment can qualify a bounded function without reproducing the whole device.
An interface census or boot milestone alone cannot qualify that function.

The stateful implementation now provides two bounded examples.
The [RPMB example](../environments/qcom/rpmb/README.md) uses authored firmware through QBox and the UFS request path.
The [local-authority example](../environments/qcom/secure-storage/README.md) executes original TZ instructions with explicit host services.
These examples qualify different boundaries. Neither proves the phone's key source or active backend.

The environment manifest separates immutable inputs from declared mutable state.
Resume and clone preserve the complete state collection and its profile identity.
A new identity requires fresh state instead of incompatible protected objects.
QCE and TME accept explicit roots through existing Lua composition.
Their existing public roots remain the defaults.

The [native provider trace](environment-inventory.md#native-provider-and-hardware-dependency) identifies the registered UID `0x0b` factory and constructor.
Original firmware derives its initial key through the existing QCE OEM-key path.
The trace does not qualify secure-file operation one or identify the phone's active version authority.
Those dependencies remain explicit instead of receiving invented replies.


### B4. Qualify the reworked environments

A result such as a later boot milestone is an observation, not proof of every preceding model.
Qualification must identify the behavior exercised and the conditions under which the result holds.
Tests protect relevant boundaries. Actual execution demonstrates the environment through its consumer.
Neither replaces physical qualification when the claim depends on physical behavior.

Acceptance criteria:

1. The documented procedure establishes each reference environment from declared inputs without hidden prior state.
2. A bounded subsystem experiment works independently of complete device boot.
3. Existing firmware paths retain their established behavior, or the documentation explains evidence-backed corrections.
4. Relevant failures remain visible and cannot become success through missing observers or substituted responses.
5. Permitted evidence or an explicit, justified virtual policy supports every retained claim.
6. Documentation distinguishes repeatability, model correctness, firmware execution, and physical equivalence.
7. Subsequent changes preserve the repository boundary and the permitted provenance of retained work.
8. The SM8845 reference qualifies the bounded experiments that its record declares.
   Kernel entry is not one of those conditions.

A family recipe is not qualified until it produces its declared observable effects.
A recipe that only selects convenient return values does not qualify.

The current tree implements the environment contract, launcher, runbooks, catalog layout, and inventory.
The remaining qualification work is B2 and B4 for the environments that B3 covers.

## Remaining work

1. Resolve the open design questions below where an experiment depends on them.
2. Address the remaining evidence gaps through B2 as each affected claim becomes active.
3. Complete the B3 migration for every reference environment that still depends on accumulated local state.
4. Apply B4 to qualify the reworked environments.
5. Keep the repository boundary current as new evidence and procedures land.

A failed example can require changes to the contract or layout.
The second part tests the first rather than merely reorganizing files to match it.

## Decisions exercised during implementation

| Area | Decision and evidence boundary |
| --- | --- |
| Entry conditions | Preserve real-reset OEM workflows. Use constructed APSS and TME reset conditions for the bounded example. No saved-state restoration claim. |
| Initial experiment | Reuse the IPCC native fixture with source-aware routing, queue operations, interrupts, and aliases. |
| External composition | Launch declared QBox Lua directly. The environment manifest pins files and describes conditions, not MMIO geometry. |
| Repeatability | Two independent preparations produced the same manifest hash and expected guest observation. This does not establish deterministic scheduling. |
| Storage | IPCC needs no storage. The RPMB example qualifies declared persistent state, authenticated operations, restart, and independent clones. |
| Qualification | Guest failure, hash rejection, and real-process timeout remain failures. No physical or retail-security equivalence claim follows. |

The [IPCC procedure](../environments/qcom/ipcc/README.md) records the executable commands and results.
The [provenance reviews](environment-inventory.md#provenance-review) inventory remaining evidence gaps rather than relabeling them.
Both OEM references have new preparation records.
At `f019bd6`, SM8850 again reached the first kernel instruction at `0xc7800000` in non-secure EL1.
The observer checked the complete image and received the debugger detach acknowledgement.
The later unsupported read at `0x17810008` remains open.
The SM8845 run reported the bounded CPUCP error at `0x183800f4`, and its record names that result as the qualification boundary.

## Open design questions

1. Which entry modes belong in the first supported scope: real reset, constructed subsystem state, or both?
2. Which bounded experiment should establish the initial environment contract?
3. How should researchers select external compositions while retaining QBox's existing configuration mechanisms?
4. Which state must the environment recreate, and which variation can the experiment tolerate?
5. Which storage and firmware interfaces must work without our current OEM package importer?
6. What evidence threshold permits a claim of physical equivalence for the chosen experiment?

EL2 and TrustZone work guide these decisions. Neither is an implied claim of current exploit reproduction or trusted application compatibility.

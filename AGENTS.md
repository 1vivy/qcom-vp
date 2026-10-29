# Agent Instructions

## Purpose

qcom-vp provides composable environments for Qualcomm hardware and firmware research through QBox.
Researchers model components and use the resulting environments for development, debugging, and security testing.
Real boot chains, subsystem experiments, and device configurations are results of that composition.
SM8850 and SM8845 are reference configurations, not the limits of the research interface.

## Structure

Directories group components by manufacturer, then by family pattern, then by SoC and board overrides.
`models/qcom/` holds shared component blocks. `models/qcom/sm88xx/` holds family blocks. `models/qcom/sm88xx/sm8845/` holds the evidence for that SoC.
`platforms/qcom/` holds reusable compositions. `platforms/qcom/sm88xx/` holds family recipes and the SoC overrides.
`boards/qcom/sm88xx/oplus/` holds the device choices.

The hierarchy is a practical catalog. A directory name does not certify identical hardware or behavior.
A family recipe records an inference from BSP or hardware-catalog patterns.
The recipe must name its evidence and the observable effect that it produces.
The inferred catalog can remain incomplete. The implemented behavior must satisfy its declared bounded contract.
Name the observation that could refine the inference or require a different component contract.
A provisional component identity is acceptable. Mark it provisional and keep the modeled contract explicit.

IP means a component design, not a phone or a device model.

- A model implements a component contract. Its identity can be provisional.
  The model key is a compatible string in the Linux form, for example `qcom,sm8550-ufshc`.
- A SoC is data: the memory map, the instances, the wiring and the fuse profile.
  A new SoC reuses models unless evidence or the experiment requires a different component contract.
- Do not test a SoC name inside a reusable model.
  If two SoCs need different behavior from one block, add a model version and its evidence.
  Split a version only when the behavior contract differs, not because a name differs.
- Each model and each memory-map entry records its evidence.
  Evidence is a ROM offset, a firmware image and offset, a device-tree node, a device probe, or a permitted BSP or catalog recipe.
- Separate observation from inference in the model and its evidence file.
- Write SystemC models in C++, because QBox requires it.
  Write platform data in Lua. Write host tools in Rust.
- Keep hardware geometry, reset vectors, and connections in QBox Lua descriptions.
  File offsets select input bytes. They do not define guest addresses.
- Keep environment contracts and experiment procedures in `environments/` and `docs/`.
  See [the environment runbook](docs/environments.md) before adding another execution path.

## Firmware

- Do not commit Qualcomm or OEM binaries.
  This rule includes ROM dumps, firmware images, disk images and byte copies of their code or data.
- Addresses, register names and protocol facts are not binaries. Record them with their evidence.
- Name each blob in `firmware.toml` by path and SHA-256.
  The tools check the hash before a run.

## Permitted evidence

Firmware analysis, device observations, and properly licensed public sources may support model contracts.
Private research repositories may supply evidence, but they must not become undeclared build dependencies.
Keep nonredistributable source and firmware bytes outside the repository.
Record observations, inferences, implemented assumptions, and unresolved questions separately.
Do not relabel a source-derived fact as intentional policy without a design basis.

Follow [the evidence guide](docs/evidence.md) for source identities and local configuration.
Keep machine-local paths, authorization exchanges, and agent handoffs out of tracked documentation.
Record technical findings and reproducible procedures instead.

## Complete contributions

A requested state is an experiment stopping point, not a reduced correctness standard.
Complete the bounded component or behavior that the task changes.
Include its required dependencies, effects, failures, evidence, and verification.
Do not add unrelated speculative hardware.
Local work, unpushed commits, forks, and upstream contributions follow the same contract.
Users need not repeat these obligations.
Neither an explicit shortcut nor an undocumented shortcut satisfies them.

Exploratory execution supplies leads, not automatic qualification.
Convert accepted discoveries into reusable models and repeatable procedures.
Distinguish constructed initial state from state established by real firmware execution.
Never claim complete state restoration or deterministic replay without checking those capabilities.

## Fidelity

- Do not stub image authentication.
  The fuse profile sets the secure-boot policy.
  The first profile is retail Oplus with secure boot on.
- Use a stub only for hardware that has no virtual equivalent, for example DDR training or the PMIC.
  Record each stub in `docs/qcom/sm88xx/stubs.md` with the behavior that it bypasses.
- Log each access to an address that has no model.
  Do not return a value for that address without a log line.

## Dependencies

- Pin QBox and libqemu by tag or commit in `package-lock.cmake`.
- Do not fork QBox.
- Fork libqemu only when a change cannot stay outside QEMU.
  Record the reason in the decision log.

## Integration

- odp-platform-qcom gets an opt-in E2E lane when the SM8850 reference reaches the kernel handoff.
- Do not add the lane before that handoff.
  After the handoff works, an open milestone does not block the lane.
- The SM8845 reference qualifies its stated bounded experiments.
  Kernel entry is not an acceptance condition for that reference.

## Commits

- Write the subject line in the imperative mood. Start it with a capital letter. Keep it to 50 characters or less.
- Put one blank line between the subject and the body.
- Wrap the body at 72 characters. Explain what changed and why.
- Add an `Assisted-by: AGENT_NAME:MODEL_VERSION` trailer to each commit that contains AI-assisted work. Check your
  own agent name and model version before you write the trailer.
- Do not add a `Signed-off-by` trailer. Only a human can certify the Developer Certificate of Origin.
- Do not rewrite the history that exists.

## Device access

Normal builds, tests, and virtual runs must not require a connected phone.
Physical-device research requires explicit authorization for the target and operations.
Keep that research separate from platform composition and normal execution.
The [Infiniti procedure](docs/qcom/sm88xx/oplus/infiniti/hardware-evidence.md) describes one reference device, not continuing access permission.
Respect the host tool's approval boundary for device writes.
Research permission does not authorize flashing, partition writes, or slot changes.
Prove model changes on the virtual platform and with relevant host tests.

## Proof

The author decides which commands prove a change. The pull request lists the commands and their result lines.
Research execution and verified boot have separate outcomes.
A successful host process does not prove a guest experiment or kernel entry.
Each experiment defines its own observations, negative cases, initial conditions, and fidelity limits.
A bounded firmware result qualifies only the behavior that the experiment observes.

## Writing

Write all prose in Simplified Technical English (ASD-STE100).

Prose means: pages in `docs/`, every README.md, code comments, commit bodies, PR descriptions.
Apply the structural rules in full:
- Use the active voice.
- Write one instruction per sentence.
- Keep a sentence to 20 words for instructions and 25 words for descriptions.
- Do not use semicolons.
- Do not use phrasal verbs. Use the single plain verb.
- Keep a noun cluster to three words.
- Keep the subject, the verb and the article in each sentence.
- Keep a hedge ("may", "can") as it is. Do not change a hedge into a fact.
- Use a numbered list for three or more steps.
- Keep one topic per paragraph, and no more than six sentences in a paragraph.
Apply the lexical rules as a direction. Use one word for one meaning in a document. Use a verb, not a noun, for an action.
Use Strict mode for console marker strings, error messages, CLI help text and tool descriptions: also one word, one meaning.
Review the wording against these rules. Automated style checks are aids, not substitutes for preserving technical meaning.

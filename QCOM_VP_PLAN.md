# qcom-vp: Firmware execution through kernel entry

This document records the end-to-end milestone proposal for the platform and the reference data that supports it.
The [research environments plan](docs/research-environments-plan.md), the [environment runbook](docs/environments.md), and the [evidence guide](docs/evidence.md) define the current scope, procedures, and input identities.
The stages below remain the roadmap for reaching and extending the kernel handoff.
They do not override current repository guidance or current results.

Authentication and authorization keep real checks, inputs, decisions, and failure paths.
Explicit substitutes appear in [model substitutes](docs/qcom/sm88xx/stubs.md) with their limits.

## Deliverable

Create a reusable Qualcomm virtual platform in the independent `qcom-vp` repository.
Use QBox to execute the supplied ROMs and the OEM firmware.
Complete the SM8850 boot path through UEFI and ABL to the first kernel instruction.
Then prove reuse with the SM8845 firmware through its stated bounded experiments and add the opt-in ODP lane.

A loader, a ROM trace, or a UEFI banner does not complete this deliverable.
The implementation stages below are dependencies, not alternative delivery endpoints.
A kernel failure after the first instruction does not invalidate the handoff.
Kernel initialization, Android userspace, and full peripheral coverage are later milestones.

### Milestone status

At `f019bd6`, the SM8850 reference satisfied the handoff check.
The kernel entered at `0xc7800000` in non-secure EL1.
The observer verified the complete 39,889,408-byte image, its first instruction, and the debugger detach.
The kernel emitted 4,573 UART bytes across 41 lines.
An unsupported read at `0x17810008` remains open and reports `run_succeeded=false`.
This result restores the handoff milestone at that revision.
It does not establish a complete Android boot or authentication qualification.
The [boot dependency record](docs/qcom/sm88xx/boot-dependencies.md) preserves earlier failure observations and their limits.

The SM8845 CPUCP error at `0x183800f4` with PC `0x1d8066f0` is a bounded observation.
The SM8845 reference qualifies the bounded experiments that it declares.
That error is not an unmet universal kernel milestone.

## Project decisions

1. Use `qcom-vp`, not a repository limited to Kaanapali.
2. Model IP blocks by interface and version. Describe SoCs through data and wiring.
3. Use C++ for SystemC models, Lua for platform data, and Rust for host tools.
4. Pin upstream QBox. Carry a narrow transaction-attribute patch until upstream supports the required interface.
5. Fork libqemu only when the required CPU or device behavior cannot remain external.
6. Use the supplied QRD8850 ROMs for the SM8845 compatibility experiment.
7. Report that experiment as compatibility, not proof of native SM8845 ROM identity.
8. Keep real authentication in every profile.
9. Permit explicit patches and hardware substitutes where they help execution reach the normal firmware path.
10. Preserve the actual transfer through OEM UEFI and ABL into the package kernel.
11. Add the ODP lane only after the SM8850 kernel handoff passes.
12. Keep proprietary inputs outside Git. A physical-device probe is an optional experiment, not a build or run dependency.

The QBox patch decision permits a narrow patch, not a permanent QBox fork.
Keep BSD-3-Clause-Clear for original code and preserve dependency license obligations.

For decision 2, "IP" means a component design, not a phone.
A provisional component identity is acceptable while the modeled contract stays explicit.
Split a version when the behavior contract differs, not merely because a SoC name differs.
Directory names group components into a practical catalog. They do not certify family-wide equivalence.

## Reference inputs

### Repository and dependencies

`qcom-vp` is the independent platform repository.
It contains the SystemC models, the platform and board data, the host CLI, and the research documentation.
The platform implementation is in `src/`, `models/`, `platforms/`, and `tools/qcom/qcom-vp/`.

Pin these dependency revisions:

| Dependency | Revision |
|---|---|
| QBox | `0a66507c5d8af7a0cd997a2517364a6b678e529e` |
| libqemu | `6ef1d0ee35929a157e8027db8d246412ee0df95c` |
| libqemu tag | `libqemu-v11.0-v0.17` |

Preserve the remaining versions from the pinned QBox lock file.
Build the `aarch64`, `riscv32`, and `riscv64` targets.
The [build instructions](README.md#build) list the host toolchain and workflow-specific dependencies.
The normal build and guest regressions require Clang, LLD, and `llvm-objcopy`.
Dependency sources and build files stay in the configurable cache directory.

QBox already provides CPU wrappers, routers, memory, loaders, interrupt connections, and debugger interfaces.
Its `ufs` component uses libqemu's existing `ufs-sysbus` device.
Reuse that device instead of converting the PCI controller.

### Supplied ROMs

The two SM8850 boot ROM files come from a private source directory that the operator selects.
The following hashes came from the actual files:

| File | Size | SHA-256 |
|---|---:|---|
| `pbl8850.bin` | `0x3c000` | `91bd8f81efa7a5940d4b6d4c220388b559ceef39b6b47dd7b4b0727a1beb1d1c` |
| `tme8850.bin` | `0x20000` | `e5485515dde2da7b12ef0c06f1e5af8e1a56b693af2c49d43b68abd78db3deb3` |

| Core | ISA | ROM mapping | Reset evidence |
|---|---|---|---|
| APSS | AArch64 | File offset `0`, address `0x300000` | Branch to `0x301800`, then EL3 setup |
| CPUCP | RV64 | File offset `0x34000`, address `0x334000`, size `0x8000` | Jump to `0x334040`, then RV64 `lwu` at `0x33411c` |
| TME | RV32 | Address `0x200000`, size `0x20000` | Absolute ROM pointers and the reset sequence |

APBL sets its initial stack from `0x14800000` and an offset of `0x3000`.
Its vector-table literal is `0x300800`.
TME sets its stack to `0x403000`, which is RAM, not the ROM base.
CPUCP writes custom CSR `0x7c1` at `0x334080`.
Model that CSR or patch the access explicitly.
Do not treat an unexpected trap as successful execution.

Do not inherit the earlier RV32 CPUCP claim or the stale TME Ghidra mapping.
The supplied files define the initial execution inputs.
This plan does not require another undiscovered ROM as a prerequisite.

### Firmware packages

Use these packages from the configured firmware store:

1. `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip` for SM8850.
2. `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip` for the SM8845 compatibility experiment.

The store root is local configuration, never a tracked default.
Select it per store as described in the [evidence guide](docs/evidence.md).
Direct ZIP reads worked for both packages.
Selected SM8850 members matched their extracted counterparts.
The archive contains the TME runtime, sequencer, CPUCP firmware, UEFI, ABL, and Android boot images.
These are available inputs, not missing prerequisites.

Actual SM8850 ELF headers give these coordinates:

| Image | ELF entry | Interpretation |
|---|---|---|
| `xbl_sc.elf` | `0x14827fa8` | AArch64 executable |
| `signed_firmware_soc_view.elf` | `0x2041c000` | TME runtime in the system address space |
| `sequencer_ram.elf` | `0x222a4000` | Sequencer image, not an assumed ARM executable |
| `cpucp.elf` | `0x1d800000` | ELF64 RISC-V executable |
| `uefi.elf` | `0xc6800000` | Firmware container |
| `abl.elf` | `0x9fa00000` | Firmware container |

Container metadata does not establish the ISA of its nested executable.
Derive segments from each selected image, not from a generic source-tree memory map.
The TME runtime and sequencer are separate images, not sections of `xbl_sc.elf`.

The SM8850 `boot.img` uses Android header version 4.
Its kernel size is `39889408` bytes.
An uncompressed AArch64 Image starts at file offset `4096` and contains the `ARMd` magic.
This provides a concrete kernel payload for the handoff check.

The selected ABL contains an LZMA guided section at file offset `0x1060`.
Its decompressed contents include EFISP loader diagnostics and the UTF-16 partition name `efisp`.
Runtime execution must still establish the loader's entry conditions.
An empty string search in the compressed file is not evidence that the loader is absent.

The SM8845 package contains different XBL-SC, CPUCP, and TME firmware.
Its shared `Setting.xml` still names SM8850.
Do not use that field alone to identify the target.
Its `Projectconfig.xml` names project `24877`.

### Authentication scope

Public Qualcomm and Oplus certificates are present.
The input review checked their certificate signatures.
This does not prove image authentication or establish physical fuse values.

The input manifest declares the trust anchors and fuse profile for the experiment.
Authentication and authorization must execute their real checks.
Do not substitute successful authentication results to reach the milestone.
Record hardware substitutes separately from authentication decisions.

## Repository layout

Use the following structure. Add models for the bounded component contracts that an experiment requires.

```text
qcom-vp/
  AGENTS.md
  LICENSE
  README.md
  CMakeLists.txt
  Makefile
  package-lock.cmake
  Cargo.toml
  Cargo.lock
  cmake/                         # External QBox build and patch application
  patches/qbox/                  # Narrow upstream compatibility patch
  patches/libqemu/               # Required CPU or device changes
  src/platform.cc                # QBox composition and simulation entry
  models/                        # Component models and evidence
  platforms/qcom/sm88xx/sm8850/platform.lua
  platforms/qcom/sm88xx/sm8845/platform.lua
  boards/qcom/sm88xx/oplus/infiniti.lua
  boards/qcom/sm88xx/oplus/cph2771.lua
  tools/qcom/qcom-vp/             # Rust CLI, storage assembly, and run control
  tools/qcom/qcom-vp/tests/       # Behavioral tests and authored guest fixtures
  firmware/qcom/sm88xx/oplus/example.toml
  firmware/qcom/sm88xx/tme-runtime/ # Authored adapter and reconstruction data
  docs/qcom/sm88xx/architecture.md
  docs/qcom/sm88xx/oplus/bringup.md
  docs/qcom/sm88xx/stubs.md
  docs/qcom/sm88xx/decisions.md
```

Keep build output, local manifests, firmware copies, disks, and run state outside tracked paths.
Use one host CLI rather than separate tools for each SoC.
Keep ODP-specific package construction in the ODP repository.

## Interface boundaries

### Model and platform contract

Each model exposes its register regions, initiator ports, interrupts, reset inputs, and configuration parameters.
The compatible string identifies a register contract, not a marketing generation.
A model must not branch on an SM number or board name.
A compatible change requires evidence of different behavior, not merely a different firmware filename.

Platform Lua files specify addresses, aliases, interrupts, clocks, reset wiring, and model parameters.
Board Lua files specify storage geometry, boot selection, memory configuration, and the chosen bring-up policy.
Share likely common patterns as explicit inferences. Keep known differences in SoC or board data.
Revise the inferred boundary when observations contradict it.
Do not create a generic inheritance framework merely to avoid a small amount of composition data.

These interfaces are project-local. Document the observed and inferred parts of each contract.
Version the manifest schema from its first implementation.
Do not promise a stable C++ ABI across QBox revisions.

### Firmware and patch contract

`firmware.toml` identifies the platform, board, ROM paths, package path, and input hashes.
It also selects the authentication policy and the exact patch recipes.
Record each consumed archive member and its hash during preparation.
Reject hash mismatches before execution.

Patch recipes identify the input hash, offset, expected-span hash, replacement, and reason.
Never scan for an approximate match and patch the first occurrence.
Apply patches only to private run copies.
Record the resulting hashes and applied recipe names.
Do not commit original firmware bytes in a patch or its expected-value field.

Use the smallest substitution that lets the firmware continue through its normal control flow.
A substitute must preserve the calling convention, output layout, and required state changes of the code it replaces.
Apply the same principle to other explicit substitutes.
Do not replace an entire protocol dispatcher when one operation blocks progress.

Missing image data, invalid storage offsets, and unsupported transactions remain distinct from authentication.
No substitute may manufacture an image, a grant, or a kernel handoff.
Record every approximation in `docs/qcom/sm88xx/stubs.md` with its scope and observable behavior.
Do not require removal of every approximation before this functional milestone can pass.

## Implementation stages

### 1. Establish the runnable platform

Preserve the repository rules, the existing history, and the license.
Install the build prerequisites from the distribution packages.
Build QBox externally from the pinned revision with all three required libqemu targets.

Disable the unused Python binder and graphics dependencies for the initial headless platform.
Use QBox's package configuration and exported CMake target.
Keep dependency source and build output outside the project source directories.
Make the patch application fail if its expected upstream source does not match.

Define `make build` as the complete dependency, SystemC, and Rust build.
Install its local result under `build/install`.
Place `qcom-vp` and `qcom-vp-system` together in `build/install/bin`.
The CLI finds its platform executable beside itself.
Install the required shared libraries with explicit runtime search paths.
Define `make test` as the host and model suite without proprietary firmware.

Compose the platform from QBox's existing container, routers, memories, CPU wrappers, and debugger support.
Use separate QEMU instances for APSS, TME, and CPUCP.
Use an available A-profile CPU as an architectural substitute for APSS.
Do not claim Oryon microarchitecture or performance fidelity.

Map the supplied ROM bytes at the verified addresses.
Set reset vectors through CPU configuration rather than a fabricated kernel bootstrap.
Describe initial reset states in platform data.
Connect later reset transitions to the modeled controllers.
Any synthetic reset transition must belong to a declared model or substitute.

Use a libqemu CPU extension when firmware depends on missing CSR semantics.
A hash-bound patch may bypass a nonessential CPU initialization step.
Keep CPU changes separate from platform address maps.
Record simplified CSR behavior explicitly rather than suppressing an unexpected trap.

Prove execution of all three reset paths with instruction traces.
These traces are intermediate checks, not the final result.

### 2. Preserve transaction attributes and shared memory behavior

The pinned QBox CPU socket receives `MemTxAttrs` but retains only the debug flag.
Carry the required attributes into a TLM extension before payload customization.
Keep the patch limited to that interface and its consumers.
Use the extension in project models for access routing and fault behavior.
Authentication bypasses do not make secure and non-secure address spaces interchangeable.

Use one backing object for each shared memory region.
Translate aliases explicitly for each address space.
Do not infer a whole-window alias from a few matching addresses.

The QEMU instances do not provide automatic shared translation-cache coherence.
Use the existing asynchronous CPU callback and `tb_invalidate_phys_range` interfaces when executable contents change.
Invalidate affected DMI mappings in the proper instance context.
Translate each physical range into that instance's address space.

Allow direct access only where the access rules and coherence contract permit it.
Do not expose writable DMI for shared executable storage without a way to observe its writes.
Preserve executable RAM mappings where instruction fetch requires them.
Use explicit invalidation at firmware publication and reset boundaries where appropriate.
Prove visibility with actual guest writes and execution through the other core's alias.

### 3. Assemble the OEM boot medium

Reuse `ufs-sysbus` and its logical-unit components.
Connect its DMA initiator to the correct system address space.
Implement missing Qualcomm controller behavior in a separate model.
Extend libqemu only for behavior that its UFS device must own.

Build private sparse LUN files from the selected package.
Use the numeric geometry in `provision_samsung.xml`, not its inconsistent comments.
The current package uses 4096-byte sectors and six enabled user LUNs.
LUN1 and LUN2 contain the boot-A and boot-B image sets.
Keep UFS boot-LUN selection separate from Android slot selection.

Implement the package's `rawprogram` and `patch` semantics in Rust.
Do not invoke Firehose or a physical-device utility.

1. Check source ranges, destination ranges, sector units, and arithmetic.
2. Apply the selected normal program files, not blank or wipe variants.
3. Expand Android sparse images where the package requests it.
4. Apply the selected patch expressions in their specified order.
5. Check both GPT copies, their CRCs, and their partition bounds.
6. Compare every populated source extent with the resulting LUN bytes.

Support the package expressions for `NUM_DISK_SECTORS`, trailing dots, and `CRC32(start,bytes)`.
Use the existing Firehose source only as a format reference.
An empty program filename does not create firmware data.
Do not apply both local-file and DISK versions of one patch twice.

Choose a virtual storage capacity consistent with the selected package's GPT and provisioning data.
Do not infer the physical device's capacity from a filename or a comment.
Model descriptors, attributes, boot selection, and the storage operations that firmware actually uses.
Use private, persistent RPMB state if the executed path requires it.
The profile may use an explicit RPMB substitute, but its counters and data must remain internally consistent.

### 4. Complete the firmware path

Advance from ROM execution through TME, XBL-SC, the required auxiliary firmware, UEFI, and ABL.
Let the firmware load subsequent images from the modeled medium.
Do not preload every stage and call that a storage boot.
An explicit temporary stage substitute must state which execution it replaces.

Use the existing ROM analysis, package DTBs, and Qualcomm sources to reconcile each required interface.
Do not start another broad prior-art investigation.
Prefer the selected image's observations when an older source map disagrees.

Implement the functional behavior required by the executed path:

| Block group | Required behavior |
|---|---|
| TME and RMB | Shared buffers, command fields, completion, status, and interrupts |
| Reset and clocks | Observable state transitions, core release, and timer progress |
| Interrupts | Delivery, masking, acknowledgment, and timer events |
| UFS | Descriptor queries, DMA, transfers, boot selection, and completion |
| Shared state | IMEM, SMEM, firmware handoff structures, and alias visibility |
| PMIC and DDR | Explicit functional substitutes where analog behavior has no useful virtual equivalent |
| Secure services | Required call layouts and state transitions, with declared authentication success |
| UEFI devices | The protocols and devices required for storage discovery and kernel loading |

Run real firmware for these services where it works.
Use a local substitute or exact patch where it avoids an unnecessary fidelity project.
Do not build unused crypto, DSP, modem, display, or analog subsystems merely to complete a device inventory.
Add them only when the normal boot path requires their behavior.

Log unmodeled accesses with the core, PC, address, width, and operation.
Report a stalled boot as the specific unmet transaction or state transition.
Do not hide all unknown registers behind one success-valued memory region.
For each required block, implement its observed transition and resume the same boot scenario.

The normal path must ultimately execute OEM UEFI, its storage drivers, ABL, and the package kernel.
Authentication keeps its real inputs and decisions in the completed result.
Do not replace the final ABL transfer with a host-written PC.

### 5. Prove the SM8850 kernel handoff

Use the selected package's actual ABL and kernel image.
Resolve the final transfer against that binary, not an address from another build.
The CLO reference describes the expected handoff, but it does not establish the OEM binary address.

Use debugger or execution callbacks to observe these conditions:

1. The required firmware path reaches UEFI and ABL.
2. ABL loads the kernel bytes from the selected boot image.
3. ABL completes its boot-services transition.
4. ABL transfers control to the loaded kernel entry.
5. The entry registers and machine state satisfy the AArch64 boot contract.
6. The first kernel instruction executes.

Check `x0` against a valid, accessible flattened device tree.
Check that `x1`, `x2`, and `x3` are zero.
Check the non-secure exception level and the required MMU and interrupt state.
Compare loaded executable bytes with the selected kernel payload.
Account explicitly for any legitimate image transformation.

A log message, a breakpoint before the branch, or a manually assigned PC cannot satisfy this gate.
Stop after the observed instruction when the run requests `kernel-entry`.
Keep the firmware console separate from the host's pass result.
Timeouts, early exits, and unhandled faults return failure.

### 6. Prove reuse and integrate ODP

Run the SM8845 package through `platforms/qcom/sm88xx/sm8845/platform.lua` and its board file.
Use the supplied QRD8850 ROMs under the approved compatibility label.
Apply only hash-bound patches that match these inputs.
Do not reuse SM8850 image offsets merely because the files have the same names.

Require the same kernel-entry evidence for this compatibility run.
Keep shared model implementations unchanged where the IP contract matches.
Represent genuine differences through versioned models or platform parameters.
A second Lua file without a firmware run is not proof of reuse.

After the SM8850 gate passes, change the ODP repository:

1. Add `e2e-tests/runner/src/qcom_vp.rs`.
2. Add its command variant and dispatch in `e2e-tests/runner/src/main.rs`.
3. Add an opt-in target in `e2e-tests/Makefile`.
4. Document the lane in the existing testing documentation.

Reuse `package::build_profile`, `package::build_package`, and `util::Guest`.
Do not reuse `fixtures::machine_args()` or `aa64::row_options()`.
Those paths select QEMU `virt` or instrument-specific authority behavior.
Keep the existing lanes unchanged.

Place the verified ODP package in `efisp` on a private LUN copy.
Respect its 3 MiB budget.
Exercise the OEM EFISP loader instead of replacing ABL with the ODP executable.
Select `ablre.authority=native`.
Do not install vendor-protocol fixtures or the emulated authority.

Use the instrument's existing discovery test for the initial native row.
Check its discovered partition identities against the prepared GPT, not only a nonzero count.
Exercise a native block read through the inherited firmware protocol.
Compare the returned bytes with the selected boot partition.
Keep this case separate from fixture-dependent tests and select it only for the new lane.
Also run the production entry through its normal native lifecycle.
Require native provider installation and the expected dispatcher transition, not only an entry marker.
Report this as ODP integration, separately from the earlier OEM kernel handoff.
Do not claim an ODP kernel boot from an entry marker or discovery result.

## CLI and usage contract

The current Rust CLI supports these workflows:

```sh
make build
make test
export PATH="$PWD/build/install/bin:$PATH"
: "${WORK:?Set WORK to a writable directory for local inputs and output}"
mkdir -p "$WORK/prepared" "$WORK/runs"
qcom-vp prepare --firmware "$WORK/sm8850.toml" --out "$WORK/prepared/sm8850"
qcom-vp run --prepared "$WORK/prepared/sm8850" --out "$WORK/runs/sm8850-1" --stop-at kernel-entry
qcom-vp prepare --firmware "$WORK/sm8845.toml" --out "$WORK/prepared/sm8845"
qcom-vp run --prepared "$WORK/prepared/sm8845" --out "$WORK/runs/sm8845-1" --stop-at kernel-entry
```

`$WORK/sm8850.toml` and `$WORK/sm8845.toml` are operator-written manifests.
They are not tracked files.

`prepare` checks inputs and creates the private boot medium.
`run` creates new mutable state from that prepared medium.
Neither command silently overwrites an existing output directory.
Both commands report the selected platform, authentication policy, input identities, and applied substitutes.
Keep logs and debugger output with the run.
The SM8845 stop point in that example declares an observation target.
It is not a kernel-entry acceptance condition for that reference.

Provide optional debugger endpoints for each CPU instance.
Use explicit pause behavior when debugging starts.
QBox's GDB port alone does not stop execution at reset.

A future ODP lane must accept the external CLI path and prepared platform path.
It must build its own package and create its own private run.
It must not depend on an installed proprietary firmware set in CI.

## Verification and completion

Add focused host and model tests for actual boundaries:

1. Reject a mismatched firmware hash or patch preimage.
2. Reject out-of-range storage writes and malformed patch expressions.
3. Check prepared LUN contents against their source extents.
4. Check mailbox ordering, completion, interrupts, and reset behavior.
5. Check shared data visibility and executable-memory invalidation.
6. Check that authentication and authorization preserve their ABI, required outputs, and failure paths.
7. Check that early termination cannot produce a kernel-handoff result.

Run the real SM8850 and SM8845 firmware scenarios.
Retain the exact commands and observed result lines.
Do not substitute unit tests for those executions.
Keep functional execution results separate from authentication qualification.

Run the new project's build and focused suite after integration.
Run `make fmt clippy test deny vet` once for the completed ODP changes.
Run the new ODP lane on the actual virtual platform.
Keep tests deterministic and independent of private input locations unless they are explicit firmware runs.

Update the architecture, usage, decision, and substitution documentation after smoke proof.
Record verified changes as focused commits.
Do not rewrite existing history or push automatically.

Completion requires the SM8850 firmware handoff, the stated bounded SM8845 experiments, and the native ODP lane.
List any remaining functional substitutes without treating them as hidden hardware fidelity.
Do not declare completion at a ROM trace, a UEFI banner, or a blocked authentication check.

## Limits and failure policy

This milestone proves functional execution, not physical-device equivalence.
It does not prove timing accuracy, secure isolation, retail fuse identity, or production authentication.
The SM8845 result proves compatibility with the supplied ROMs, not their native provenance.

Unreachable firmware data or a genuine required secret must produce a specific input error.
First use the approved functional substitutes where they can satisfy the actual behavior.
Do not request physical-device access or silently replace the deliverable with direct kernel boot.
Keep the SM8850 kernel-entry gate unchanged when an intermediate implementation stage fails.

# Architecture

## Functional scope

The platform uses functional models for SoC infrastructure.
The models preserve firmware-visible effects without reconstructing unnecessary analog or sequencer internals.
See [the modeling decision](decisions.md#model-infrastructure-by-observable-effects) for the boundary.

## Working catalog patterns

The catalog groups likely common designs, not certified hardware equivalents.
Observed accesses, accumulated device evidence, and repeated firmware structures guide the inferred boundaries.
The composition makes those inferences executable without requiring a known vendor revision name.

| Pattern | Shared implementation | Specific data or limit |
| --- | --- | --- |
| Reset composition | [Family reset recipe](../../../platforms/qcom/sm88xx/reset.lua) | Both SoC compositions reuse it. SM8845 uses supplied compatibility ROMs and explicit identity overrides. |
| Runtime reconstruction | [TME runtime recipe](../../../platforms/qcom/sm88xx/tme-runtime.lua) | The recipe consumes `hardware` data. Donor identity and reconstruction remain explicit inputs. |
| Clock and protection catalogs | [SM8850 data](../../../platforms/qcom/sm88xx/sm8850/runtime-data.lua) and [SM8845 data](../../../platforms/qcom/sm88xx/sm8845/runtime-data.lua) | Both contain the same four PLL addresses and branch list. Client lists and protection geometry differ. |
| OEM board choices | [Oplus board data](../../../boards/qcom/sm88xx/oplus) | The board selects boot storage. It does not select hidden behavior inside reusable component models. |

The repeated clock entries suggest a shared recipe. They do not imply that every register or reset value is identical.
Different client lists illustrate why a family pattern and its SoC data remain separate.
These relationships provide starting points for another device, even when the wider catalog remains incomplete.

1. Start from the closest recorded component contract and recipe.
2. Record the observations and inferences that support the choice.
3. Supply the target's known differences through composition data.
4. Exercise the required effects and failure paths.
5. Refine the data or separate the component contract when observations contradict the pattern.

SM8845 tests these abstractions through its stated bounded experiments. Kernel handoff is not its acceptance condition.

## Runtime

The C++ executable composes QBox modules through the Lua platform description.
APSS, TME, and CPUCP use separate QEMU instances.
APSS uses AArch64. TME uses RV32. CPUCP uses RV64.
The project-local `ApssCpu` uses QEMU's `max-arm-cpu` through QBox's A710 interface.
This is an architectural substitute, not an Oryon identity or performance model.
It preserves QBox's interrupt, reset, power, and exclusive-access interfaces.

The APSS execution group contains eight CPUs in one native instance.
Ordered realization assigns indices zero through seven before the GIC initializes.
Six cores use lane zero, and two cores use lane one.
The original CPUCP power controls drive each core's reset input.
Actual vector writes update RVBAR only while the corresponding CPU remains held.
Native PSCI interception remains disabled, so genuine secure firmware handles CPU_ON.

The local GIC wrapper retains one native redistributor region.
The plain `arm_gicv3` profile uses `0x20000`-byte native frames and no LPIs.
The grouped controller selects GICv4 with LPIs and one ITS at `0x17040000`.
`GICR_TYPER.VLPIS` grows each native frame to `0x40000`.
Each grouped slot aliases one native frame at the firmware's `0x40000`-byte stride.
QSEE derives the same stride from `GICD_PIDR2`, rather than using the device tree.
Every core has its own interrupt outputs, timer PPIs, maintenance PPI, and PMU PPI.
Shared executable memory and DARE invalidation cover every native execution view.

The same GIC owner retains QSEE fault routes and two 64-bit fault controls.
SM8850 initializer `0x15775620` and SM8845 initializer `0x157688f4` program this group.
They update distributor offsets `0x2e808/0x2e800` and write `0x108` to `0x20048/0x20088`.
Configuration does not create a hardware fault or interrupt.
The native grouped-CPU fixture checks that a 32-bit control update preserves the other half and the other control.

No host worker replaces the firmware scheduler, FV decoder, or secondary-entry code.

PAuth key registers and authentication instructions use the existing CPU implementation.
RNDR and RNDRRS use QEMU's host-backed `qemu_guest_getrandom` path, including its failure result and NZCV flags.
The pinned implementation uses the same entropy path for both instructions, without a separate reseed operation.
The wrapper defaults CNTFRQ to 19.2 MHz. QEMU's timer scheduler quantizes the tick period to integer nanoseconds.

The opt-in libqemu patch supplies MPAM v1.0 with only PARTID 0 and PMG 0 in each security space.
It reports those limits through MPAMIDR_EL1 and does not expose resource controllers or MPAM virtualization.
MPAM3_EL3 resets with MPAMEN clear and TRAPLOWER set.
MPAM2_EL2 and MPAM1_EL1 expose the shared enable bit without granting lower levels write access.
The patch implements EL3 and EL2 traps, VHE aliases, and NV2 redirection.
Unsupported MPAM virtualization registers and unknown system registers remain undefined.
Security attributes remain unchanged. The patch does not authenticate images or authorize protected memory.

Arm's [MPAM3_EL3 definition](https://developer.arm.com/documentation/ddi0595/2020-12/AArch64-Registers/MPAM3-EL3--MPAM3-Register--EL3-)
specifies EL3-only access and the reset values.
The public [MPAMIDR_EL1 description](https://df.lth.se/~getz/ARM/SysReg/AArch64-mpamidr_el1.html)
defines the implementation limits and optional virtualization controls.
Selected QSEE code reads MPAM3_EL3 at `0x1468e05c` and accesses PAuth keys at `0x1468e0ac..0x1468e0e4`.
Selected SM8850 QHEE code reads RNDRRS at `0x80000034` and RNDR at `0x8000003c` and `0x80000044`.
These consumers require CPU instruction emulation, not an MMIO substitute.

The `apss_executes_mpam_pauth_and_rng` regression passed through actual AArch64 execution.
It checks MPAM reset state, masks, access restrictions, shared enable state, and VHE aliases.
The guest handles 35 expected exceptions and verifies the PAuth keys and authentication instructions.
It also executes RNDR and RNDRRS through the real entropy path.
The probe does not exercise NV2 redirection.

The `apss_virtual_timer_delivers_ppi27` regression also passed through the production GIC composition.
The guest enters nonsecure EL1 through EL2 and programs CNTV.
An expired masked timer produces no interrupt.
Unmasking delivers INTID 27. The first handler rearms the timer, and the second disables it.
The guest then verifies that the disabled timer produces no further completion.

The platform maps each ROM at its evidenced reset address.
The initial RAM regions cover the stack and clearing operations visible in the supplied ROMs.
The platform does not load later firmware stages directly.
The platform connects UFS, the GIC, shared RAM aliases, and the observed timer frames.
Reset-controller and mailbox signaling remain incomplete.

The QBox patches preserve `MemTxAttrs` across both the initiator and target bridges.
The access monitor rejects unsupported transaction attributes and stops on failed transactions.
Its diagnostic identifies the core, PC, address, width, and operation.
The shutdown patch joins each native CPU thread before QBox destroys its CPU members.
It first cancels pending SystemC work so a stopped kernel cannot block the join.
An asynchronous halt request alone does not protect an in-flight MMIO callback.
Unmodeled access still stops the simulation and returns failure exit code 2.
The GIC regression checks different secure and nonsecure views through actual ARM execution.

The QBox patch preserves read-only permissions when it creates DMI regions and aliases.
The libqemu patch exports `memory_region_set_readonly` for that interface.
Each shared executable region permits DMI only for its execution owner.
Other initiators use observable transactions against the same backing bytes.

Foreign writes queue code invalidation on the owner's CPU thread.
The invalidator converts guest physical addresses to QEMU RAM offsets before it invalidates translated code.
The live regression replaces executed RISC-V code through an ARM alias without `fence.i`.
The implementation resolves the owner's default address space. Separate secure memory maps remain unsupported.

## Early boot storage on SM8850

The reset platform keeps the existing executable SRAM at `[0x14800000,0x14a80000)`.
It adds bounded, cold-zero storage for SYSTEM_IMEM and SHRM.
Cold-zero initialization is a virtual board policy, not a captured silicon reset state.
The platform does not construct cookies, valid record pointers, DDR results, or SMEM.
The guest retains all image loading, authentication, record validation, and shared-state publication.

The selected `xbl_sc.elf` has SHA-256 `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
Table coordinates below use virtual addresses from its `PT_LOAD` segments.
Selected ELF tables establish target-specific SHRM addresses.

| Storage | APSS interval, end exclusive | Evidence |
| --- | --- | --- |
| SYSTEM_IMEM | `[0x14680000,0x146ac000)` | The interval contains 176 KiB. |
| SHRM IRAM | `[0x31020000,0x31048000)` | XBL base-size pair at `0x148ebf88`, file offset `0xc2f88`: `0x31020000,0x28000`. |
| SHRM DMEM | `[0x31048000,0x31060000)` | XBL base-size pair at `0x148ebf98`, file offset `0xc2f98`: `0x31048000,0x18000`. |

The Linux Kaanapali DTS, lines 5536–5539, exposes only the first SYSTEM_IMEM page.
That Linux node does not bound firmware-owned storage.
XBL also describes `[0x14680000,0x146a7000)` at `0x148e4da8`, file offset `0xbeda8`.
Its finer table describes `[0x1468e000,0x1469a000)` and `[0x1469a000,0x1469d000)` at `0x148e6510` and `0x148e6528`.
These are subregions of SYSTEM_IMEM, not additional backing objects.

The XBL property table starts at `0x148e6350`, file offset `0xc0350`.
Its first three entries are `0x1468e000`, `0xf000`, and `0x14698fcc`.
The getter at `0x148becb0` supplies these entries to `0x14876978`.
Instruction `0x148769cc` reads the record pointer from `0x14698fcc`.
The function bounds the pointed record and checks its byte sum before copying the first `0x90` bytes.
The complete record occupies `0x98` bytes.
This slot is storage, not a ready register. Cold-zero storage does not fabricate a valid record.

The SHRM ELF confirms the load destinations without a host image load.
Its code segment uses local VA `0x200000`, physical address `0x31020000`, file offset `0x1000`, and size `0x271d8`.
Its data segment uses local VA `0x400000`, physical address `0x31048000`, file offset `0x29000`, and size `0xf00`.
The XBL interval tables at `0x148e4df0` and `0x148e6540` both describe `[0x31020000,0x31060000)`.
The selected DDR shared object at `0x3104a800` lies inside DMEM.
SHRM local `0x402800` names the same bytes. No separate local backing is allocated.

APSS owns one backing object for each interval.
UFS and sequencer DMA aliases translate to offsets within those same objects.
The platform rejects competing mappings on each affected router.
SHRM control and command registers at `0x31076000` remain MMIO, outside this memory group.
XBL `0x148adad4` selects `0x31060000` as a register-programming base, not a copy destination.
The installed callback at `0x148be4c0` interprets eight-byte offset/value records.
The separate [DDR controls](../../../models/qcom/sm88xx/ddr/controls-evidence.md) describe that bounded register group.
No speculative SRAM mapping covers those controls.

The supporting `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/boot_inputs.py`, lines 187..190, 244..248, and 559..589, constructs earlier-stage handoff data.
Its DT pointers, cookie validity header, boot-media enum, and optional SMEM are not evidence of reset contents.
This platform does not import those writes.

## Host preparation

The Rust CLI separates input validation, storage assembly, run isolation, and handoff verification.
The firmware manifest identifies each source by path and SHA-256.
It also identifies the platform, board, compatibility label, authentication policy, and optional patch recipes.
An optional trust pair pins a 136-byte P-521 sequencer root and its entitlement digest.
The CLI validates both, copies the root to the prepared directory, and checks it again before execution.
An independent `oem_root` input supplies the complete DER certificate for the OEM root.
The CLI checks its SHA-256 during preparation and before execution.
It computes SHA-384 from the checked run copy and configures the immutable fuse words.
The raw register layout preserves the byte order that the real TME ROM reconstructs.
Missing trust material cannot produce a successful hash read.

A recipe specifies its schema, name, target, complete input hash, offset, expected span hash, replacement hexadecimal bytes, and reason.
The current recipe targets are `pbl` and `tme`.
The CLI applies a recipe only to a private copy after both hashes match.
The resulting manifest records the recipe hash and the modified input hash.
No committed recipe contains proprietary bytes.

The storage assembler reads package members without extracting archive paths.
It rejects unsafe paths, duplicate names, and selected members that are not regular files.
Unselected archive symlinks do not prevent preparation.
The assembler derives image selection from `Setting.xml` and `Projectconfig.xml`.
It derives LUN geometry from numeric Samsung provisioning data.

The assembler supports raw data, Android sparse images, and the package's composite super format.
It evaluates `NUM_DISK_SECTORS`, decimal trailing dots, and ordered `CRC32` expressions.
It does not apply equivalent file and DISK patches twice.
It checks source ranges, destination ranges, GPT integrity, and source bytes after patch overlays.
Missing selected data produces an input error.

## Run isolation

Each run uses a new output directory.
The CLI checks the ROM hashes and each prepared LUN identity before execution.
It creates LUN copies with Linux reflinks or sparse extent copies.
Writes to a run copy do not modify the prepared file.

The sparse identity includes the logical file size and each nonzero 64 KiB block with its offset.
It does not depend on the allocation of zero-filled regions.
The hash format uses the domain `qcom-vp-sparse-sha256-v1`.
The CLI checks each copy against its prepared identity.

## Handoff boundary

The host requires a completed observation through a private GDB socket.
It observes the actual UEFI dispatch, ABL dispatch, and successful ExitBootServices return.
It then follows ABL's kernel branch and steps the first kernel instruction.
The observer selects exact firmware identities. See [observer sites](oplus/observer-sites.md).

The verifier checks the transition fields, completed instruction, entry registers, exception level, security state, and control registers.
It compares the guest kernel bytes with the selected Android kernel payload.
It checks the guest FDT structure and its reported address.
Stop mode retains the APSS pause and reaps the child after proof.
Continuation mode detaches only after proof, then retains console output until process exit or the original deadline.
Timeouts, failed detach, observation errors, and child failures cannot pass the run.

The SM8850 run reaches kernel entry, detaches, and continues in the guest kernel.
The SM8845 runs stop before UEFI. See [boot dependencies](boot-dependencies.md) for the recorded checkpoints.
The regression tests establish specific rejection and execution boundaries, not a firmware handoff.
A fabricated record cannot substitute for observed execution.

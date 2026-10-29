# Hardware probe campaign

## Objective

Resolve firmware-visible model questions with the smallest useful hardware observations.
The campaign covers the boot dependency chain, not every address in the SoC map.
Its first priority is CPUCP runtime initialization and the APSS handoff.
Later passes cover UEFI, ABL, and kernel dependencies as execution exposes them.
The campaign does not replace virtual kernel execution or its retained boot log.

A useful experiment may crash the device.
Permanent modifications remain excluded.
This plan permits no intentional persistent writes and no undocumented register writes.
Crash handling and normal boots can still persist diagnostic logs, counters, or reset reasons.
Thus the campaign cannot promise that every durable byte remains unchanged.

## What EL1 can establish

Gobbl can observe state that OEM firmware exposes to the nonsecure APSS environment after initialization.
It can read ordinary RAM and some mapped MMIO through the current virtual address space.
EL1 does not establish access to secure registers, EL2 state, CPUCP-local memory, or another processor's private registers.
A valid stage-one mapping does not establish stage-two or hardware access permission.

A snapshot establishes a value at one checkpoint.
It does not establish a reset default, writable mask, timing requirement, or transition dependency.
A controlled volatile experiment can establish behavior at that checkpoint.
It cannot reconstruct an earlier boot epoch without an appropriate observation point.

| Question | Best evidence | Limit |
| --- | --- | --- |
| Which region or interface exists? | EFI atlas, firmware tables, current mappings | Mapping or publication does not establish access or ownership |
| What did firmware program? | Bounded RAM or justified register capture | Values are post-initialization state |
| Which bits retain writes? | Owned scratch test or justified volatile control experiment | A read followed by a write is unsafe for some registers |
| What causes completion? | Controlled request plus observation of its effect | A ready bit alone does not prove DMA, IRQ, or reset delivery |
| Why did an access fail? | Command journal, USB events, readable crash state | USB loss alone does not identify XPU, stage-two, or bus failure |
| What happened during early CPUCP boot? | Firmware decode and virtual guest tracing | Gobbl cannot reproduce that processor's early execution context |

## Reuse before capture

The existing [hardware workflow](hardware-evidence.md) defines commands, journals, capture handling, and access limits.
Use the existing Gobbl and lab tools. Do not add a general probe framework or a platform dependency on the phone.

| Existing resource | Reuse |
| --- | --- |
| [Qualified EFI atlas](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261015-firmware-atlas/record.md) | Memory maps, interfaces, image extents, and checkpoint limits |
| Atlas decoder `gobbl-lab:records/20261015-firmware-atlas/evidence/qualified/instrument/qcom-atlas-decode.py sha256=7973815a1800b9cbed544cd3faa74502a82d7b355f69b26eb12bfac73ccc26c3` | Offline interpretation of the retained capture |
| Atlas probe source `gobbl-lab:records/20261015-firmware-atlas/evidence/qualified/instrument/qcom-atlas-probe.c sha256=08b8dd77fc3bb1947f757ac769273379b09d6d1a311bde7846907350e1c0a473` | Existing bounded ordinary-RAM reads and EFI enumeration |
| [Android read helpers](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/lab/phone/android_reads.py) | Root discovery, properties, and journaled read results |
| [Logfs collector](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/lab/phone/harvest.py) | Separate SFB read-only export workflow, not an Android memory reader |
| Privileged research `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:docs/privileged-platform-research.md` | Existing EL1 limits and rejected privilege-transition experiments |
| Memory-stage atlas `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:docs/memory-stage-atlas.md` | Distinct firmware checkpoints and image relationships |
| [Current probe record](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261025T090340Z-phone-qcom-vp-context/record.md) | Successful RAM captures and failed fabric read |

The qualified atlas already contains 371 handles, 113 loaded images, 325 distinct protocol GUIDs, and 12 configuration tables.
Its offline decoder reproduced the recorded SHA-256 on 2026-10-25.
Reuse that census for its recorded firmware and checkpoint. Refresh it only when a relevant identity or state changes.
The atlas source excludes MMIO and vendor callbacks. Do not silently extend its safety claim to either operation.

Android properties, exported firmware logs, and existing EFI records form a separate observation lane.
Do not assume root access or the availability of an EFI runtime service after ExitBootServices.
Do not invoke vendor callbacks merely because their names describe queries.
Their implementations can allocate resources, send secure requests, or change persistent state.

## Staged passes

1. Inventory the retained evidence and current firmware identities.
2. Establish one fresh Gobbl checkpoint and its translation context.
3. Capture selected shared RAM structures that answer current model questions.
4. Read individually justified MMIO words, with one uncertain access per experiment.
5. Exercise only approved volatile transitions with known ownership and bounded effects.
6. Compare the observations with both firmware consumers and the virtual model.

Stop after each pass to select the next useful question.
Do not execute a prebuilt address sweep after a failure.

### Pass one: passive baseline

Record the device serial, SKU, firmware build, Gobbl artifact identity, boot reason, slot, exception level, and translation context.
Use existing read-only identity mechanisms. Record unavailable fields instead of guessing them.
The live Android endpoint identified itself as CPH2747 in the current session.
The virtual SM8850 package is CPH2749. Do not silently treat their firmware or fuse profiles as identical.
Infiniti observations do not establish SM8845 hardware behavior.

Select ordinary-RAM structures from current descriptors and matching firmware symbols.
Examples include SMEM topology records, runtime descriptors, mailbox buffers, and CPUCP log records.
Check translation, range ownership, width, and lifetime before each capture.
Use bounded copies without changing producer indices, locks, queue pointers, or interrupt state.
For concurrently updated structures, report possible tearing unless their native publication protocol establishes consistency.

### Pass two: MMIO visibility and programmed state

Require a firmware or public-register reference for every exact address and width.
Classify the register before access: identity, configuration, counter, destructive read, FIFO, acknowledgement, or unknown.
Exclude destructive reads, FIFOs, acknowledgements, and unknown semantics from the initial pass.
A memory-map label or virtual model owner is not hardware access evidence.

Capture one uncertain word per experiment.
Record the request before transmission. Associate the result with the exact boot checkpoint.
After a fault, stop that address family until evidence supports a distinct next experiment.
Do not vary widths or nearby offsets solely to find an access that succeeds.

The failed fabric read at `0x19830320` blocks further speculative reads in that family from the same context.
A readable crash syndrome, documented access path, or justified different execution context can permit another experiment.
A crashdump alone does not provide that permission or explanation.

### Pass three: volatile behavior

Use an owned scratch allocation before any hardware write experiment.
The existing endpoint qualification already proves exact-width scalar and downloaded writes on such RAM.
Repeat that test only when the endpoint implementation or relevant memory contract changes.

A register experiment must name its owner, expected effect, writable fields, dependent resources, and restoration method.
It must also explain why the operation cannot command a persistent write or change security configuration.
A saved original value is insufficient for write-one-to-clear, write-once, FIFO, command, or self-clearing registers.
Do not use all-zero, all-one, or walking-bit tests on unknown MMIO.

Prefer the existing driver's bounded operation when its implementation satisfies these conditions.
For DMA, both source and destination must be owned scratch buffers with explicit bounds.
For an interrupt, the source, handler, acknowledgement, and cleanup must be known.
For clocks or resets, exclude resources that sustain storage, DDR, secure services, or the observation transport.
Unknown ownership or persistence means no poke.

## Current-stage priorities

These are research targets, not an executable address allowlist.

| Priority | Model question | First observation | Next step if inaccessible |
| --- | --- | --- | --- |
| 1 | Why does SM8845 wait at CPUCP `0x1d80bdb8`? | Identify the pending software request, ACEHR channel, completion, and IRQ state in the virtual run | Trace the original producer and handler before using hardware |
| 2 | Are CPUCP shared buffers and saved contexts consistent with the model? | Decode retained CPUCP RAM and relevant SMEM records at a labelled checkpoint | Use existing dumps and firmware layouts, not private SRAM sweeps |
| 3 | Which auxiliary PLL configuration does CPUCP use? | Decode descriptors and the original register sequence for `0x19aa1000` and `0x1aaa1000` | Keep firmware-derived behavior until an appropriate access path exists |
| 4 | Which CPUCP status and interrupt fields need real semantics? | Compare original producers, consumers, and any readable retained state | Use virtual transition probes rather than infer completion from idle hardware |
| 5 | What can nonsecure EL1 see in the fabric banks? | Analyze the first failed read and its crash association | Do not repeat the failed family without new evidence |
| 6 | What state does OEM UEFI inherit? | Reuse the qualified EFI atlas and matching shared-memory evidence | Refresh only the changed checkpoint or missing structure |
| 7 | Which later devices block ABL or kernel execution? | Use the actual virtual fault to select a specific storage, interrupt, timer, or peripheral question | Keep unrelated devices outside the immediate pass |

SM8850 run309 passes auxiliary PLL initialization and reaches `0x17c30020` at CPUCP PC `0x1d80428e`.
The pending virtual change composes the existing Zonda model at the auxiliary bases.
That native progress does not establish the physical PLL identity or a kernel handoff.

Storage, PMIC, security, cryptography, display, USB, and other blocks remain in the overall model inventory.
Their presence does not justify experimental writes.
Use existing driver contracts and passive evidence until a concrete boot dependency requires another observation.

## Persistent-change exclusions

The campaign excludes these operations:

- Flashing, partition writes, GPT changes, boot-LUN changes, and physical slot selection.
- Fuse or OTP programming, RPMB updates, provisioning, key changes, and rollback updates.
- Persistent EFI-variable writes, boot-entry changes, and security-policy changes.
- Storage-controller commands or DMA that can reach persistent media.
- PMIC nonvolatile programming, unbounded voltage changes, and undocumented power commands.
- Protection changes intended to force access through XPU, SMMU, EL2, or secure-world restrictions.

Volatile markers must belong to the probe and must have a documented cleanup path.
Crash tolerance does not authorize any excluded operation.
Reset is a recovery mechanism, not proof that every prior effect was volatile.

## Crash experiments and recovery

Check the catcher and the available host storage before a potentially faulting probe.
Keep one device operator and one outstanding uncertain access.
Preserve the pre-access journal even if the device never returns.
After failure, wait for USB events and the existing catcher rather than starting a competing collector.

Associate each dump by command time, USB transition, boot epoch, and available fault evidence.
Distinguish the first induced failure from later reset or direct-boot failures.
The second dump at `20261025T090714Z` remains unclassified.
One recorded explanation is reset-reason propagation. It is a hypothesis, not a proven cause.

Check capture completion, region lengths, and hashes.
Inspect plaintext fault records when available. Treat encrypted or absent records as unknown.
Before another experiment, establish a fresh endpoint and context.
If automatic recovery fails, stop device operations and request physical recovery rather than issue destructive repair commands.

Before repeating an access without a recovered syndrome, prove the intended exception capture path:

1. Establish a working callback or a verified route from the firmware exception logger.
2. Capture a controlled software exception in a private plaintext record.
3. Recover that record through the intended host collection path.

Protocol presence alone does not satisfy the first step.
The live synchronous callback request returned `EFI_UNSUPPORTED` on 2026-10-25.
The readiness instrument stopped before its controlled exception.
The existing Surfacer registration and the earlier EBS snapshot do not satisfy these steps.
Do not remove a foreign handler or change the vendor's dump policy to bypass this refusal.

## Per-experiment record

Use the existing lab record and command journal.
Add these facts in its normal notes, without introducing another report schema:

- Model question and the expected discriminating result.
- Firmware identities, boot phase, exception level, requester, and address interpretation.
- Exact address, length, width, operation, and source evidence.
- Ownership, possible side effects, persistence analysis, and recovery method.
- Raw responses, captured bytes, sizes, hashes, and any fault association.
- Supported conclusion, unresolved alternatives, and the corresponding virtual verification.

An experiment is complete when its observation answers the stated question or establishes a bounded access failure.
A first pass is complete when retained evidence or an explicit access limitation addresses every selected question.
More captured bytes are not a success criterion.
Model changes still require original firmware execution on the virtual platform.

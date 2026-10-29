# Implementation decisions

## Preserve the active authentication rule

`AGENTS.md` prohibits authentication stubs.
The implementation rejects an always-good manifest instead of representing that profile as retail authentication.
It keeps the real authentication path active.

## Preserve dependency identities

The project pins QBox to `0a66507c5d8af7a0cd997a2517364a6b678e529e`.
It pins libqemu to `6ef1d0ee35929a157e8027db8d246412ee0df95c`.
The lock file retains the pinned QBox versions of SystemC and SCP.
Original code retains BSD-3-Clause-Clear. Dependencies retain their own licenses.

## Record provenance as evidence scope

A report must separate a cited reference from proof of implementation derivation.
The removed vendor references supplied no meaningful implementation information.
Prior evidence pages, accumulated research, and device probes supply the technical basis.
A lead-list entry is a research pointer, not proof that a constant came from that reference.
A manufacturer or family catalog pattern can guide a model when the record states the pattern, its basis, and its limit.
A pattern from one family is not proof for another target.

An unresolved meaning stays an open question with its address, constant, and observation.
Do not label an inherited value an intentional policy without a recorded design basis.
Reports must not include nonredistributable source or binary material.
Provenance work grants no publication approval and changes no model behavior.

## Preserve memory transaction properties

The pinned QBox initiator discarded transaction attributes other than the debug flag.
The local patch carries the attributes in a TLM extension before payload customization.
The target bridge also discarded the extension. The GIC regression exposed that second loss.
A target patch now restores the original attributes before QEMU dispatch.

The upstream implementation ignored read-only DMI permissions when it created QEMU RAM mappings.
The patch preserves those permissions for regions and aliases.
The required read-only operation needs a libqemu export.
Code invalidation also needs QEMU's mapping from guest addresses to RAM offsets.
The CPU export patch adds that operation and a PC reader.
It also corrects the wrapper signature for `tb_invalidate_phys_range`.

The UFS interrupt test exposed a cross-instance lock cycle.
The interrupt path copied the APSS GPIO handle while it held the UFS lock.
The CPU path copied the UFS memory-region hint while it held the APSS lock.
Both temporary destructors attempted to acquire the other instance's lock.
The handle patch borrows these objects from their existing owners instead.
The actual guest then completed UFS requests and observed interrupt assertion and clearing.

The CPUCP runtime later exposed another handle copy in `QemuCpuHintTlmExtension::get_cpu`.
The native run329 host capture shows its temporary destructor waiting for the CPUCP global lock.
The getter now returns a const reference to the extension-owned CPU handle.
The patch changes no transaction attributes or CPU identity.

The run330 host capture then isolates a separate trace lock cycle between the two CPUCP harts.
One hart holds the trace lock while disassembly reads guest memory through SystemC.
The other hart holds the global lock while it waits to log an interrupt.
The first hart cannot restore the global lock after its memory transaction.
CPUCP now uses QEMU's existing per-thread trace mode with `qemu-cpucp-%d.log` files.
Both harts retain instruction and interrupt evidence without a shared trace lock.

The retention tool compresses these thread traces only after it restores and compares their contents.
Its regression preserves unrelated logs and checks the restored trace bytes.
All 74 platform tests pass after the CPU-hint patch.
SM8850 run331 passes the previous host stall and reports the next missing register at `0x18080008`.
At run331, kernel entry remained unverified.

The UFS patch selects the existing logical unit for boot well-known LUN requests.
It preserves the unit's real block backend and rejects invalid selections.
That protocol behavior belongs inside the QEMU UFS controller.
The libqemu cache key includes the revision and all patch digests.
This prevents an old patched checkout from surviving a protocol change.

The patch driver checks the preimage before application and recognizes an already applied patch.
The build tracks QBox patch files as patch-step dependencies.
The project does not carry a permanent QBox fork.

The QBox patch step first restores the fetched cache to its pinned revision.
`ResetSource.cmake` refuses a different HEAD before it removes tracked changes and untracked files.
Later patches change earlier preimages, so individual reverse checks cannot identify the complete applied chain.
The build then applies the complete chain to the clean cache.

## Implement CPUCP CEASE inside libqemu

The CPUCP ROM executes SiFive CEASE, instruction `0x30500073`.
The pinned RISC-V CPU does not implement that instruction.
An external peripheral cannot implement its architectural halt state.
The libqemu patch adds an opt-in `x-sifive-cease` property, with a disabled default.

CEASE requires machine privilege.
It halts the hart independently of ordinary interrupt wakeups.
Architectural reset clears the halt.
The boot controller changes QEMU's reset vector before it requests that reset.
The implementation does not modify the guest PC through a debugger.

The actual guest regression confirms that a pending interrupt does not release CEASE.
It confirms that vector publication alone does not restart the hart.
The separate reset request executes the published target.
The patch does not add migration support for this opt-in state.

## Reuse RNMI state for the CPUCP legacy window

Both selected CPUCP images use legacy RNMI CSRs `0x350..353` and MNRET.
Run252 traps at the second runtime instruction because QEMU assigns that window to indirect CSRs.
Keep CPUCP's `rv64` base and all existing instruction patches.
Add patch `0017-implement-legacy-rnmi.patch` after patch 0016.
The patch exposes `x-legacy-rnmi`, disabled by default.
The existing boot controller enables it only on the CPUCP instance.

Reuse Smrnmi state and MNRET instead of a second interrupt implementation.
The Rocket reference resets RNMI enabled, with previous privilege M.
This reset state permits ordinary machine traps and interrupts before the firmware writes RNMI state.
The disabled path retains QEMU's previous reset semantics.

Rocket's Chisel bundle places MPP at `12:11`, MPV at 7, and RNMI enable at 3.
These fields match QEMU's Smrnmi layout.
The legacy accessor excludes modern MNPELP and applies Rocket's privilege, cause, alignment, and enable-write rules.
Firmware saves whole MNSTATUS values but does not establish those bit positions independently.
The [CPUCP evidence](../../../models/qcom/sm88xx/cpucp/evidence.md#legacy-rnmi-instruction-boundary) records exact consumers and the reference revision.

Do not change the global CSR table or the other CPUs' indirect-CSR behavior.
Do not infer RNMI vectors or inject NMI events.
The observed instruction family does not identify a physical vendor core.
The new native fixture covers reset, register transitions, MNRET, normal traps, interrupts, and the disabled boundary.
Runtime success requires execution of those checks.

## Report APSS identification words inside libqemu

QHEE `FUN_00148100` reads `AMCFGR_EL0`, `AMCGCR_EL0`, `TRCIDR2`, `TRCIDR4`, and `TRCIDR5` before it enables any interrupt.
The pinned Cortex-A710 model does not implement these registers, so each read takes an Undefined Instruction exception and QHEE panics.
An external peripheral cannot answer a system register read.
Patch `0018-implement-qcom-identification-registers.patch` adds an opt-in `x-qcom-ids` property, disabled by default.
`ApssCpu` enables it on the APSS cores only.

The profile reports constant identification words and the matching `ID_AA64*` feature fields.
It retains `TRFCR_EL2` writes.
It adds no activity counter, no trace unit, and no trap change.
QEMU requires `PL1` access for AArch64 registers with `opc1` equal to one, so the `TRCIDR` registers use `PL1_R`.
The [APSS evidence](../../../models/qcom/apss/evidence.md) records each consumer address and required value.

## Model CPUCP initialization as hardware state

The selected CPUCP runtime now reaches its DDR-resident initialization routine.
Both images access control `0x18876104`, then clear the full local workspace `[0x18b50000,0x18b70000)`.
Use one persistent local SRAM owner for `[0x18b30000,0x18b70000)`.
Keep DDR snapshot buffers separate. Zero-file ELF virtual/physical pairs are not bus-alias definitions.
Preserve real loader zero-fill in DDR and real guest clears in local SRAM.
The later `0x40000`-byte snapshot copies establish the complete local range.
Do not skip the guest's poison, copy, or reset paths to hide an ownership error.

The following initializer uses a compact controller named `plicv2` in firmware.
Its geometry decoder, enable helper, and ISR table establish the source-to-claim path and architectural interrupt 16.
Use a dedicated register decoder rather than the unrelated TME CLIC layout or a standard PLIC register map.
The declared virtual geometry is 256 sources, two contexts, and 16 priorities.
It fits the guest's bounds and the fixed initialization registers without overlap.

The virtual subsystem control gates interrupt delivery until the guest clears its inhibit, enables the path, and commits.
This is explicit functional policy, not a recovered physical register name or measured completion latency.
Retain real pending, priority, enable, claim, completion, and level state behind that gate.
Do not turn repeated asserted signal writes into new interrupts while the source is in service.
Completion rearms a held level. Device clearing before completion must produce exactly one interrupt.
Keep the standard CPU base and existing local-interrupt machinery.

The same startup group initializes per-hart Rocket-compatible bus-error units and publishes two recovery-vector banks.
Implement the referenced BEU masks and accrued state rather than a RAM substitute.
Retain the guest-published vectors, but do not guess their interrupt-versus-exception assignment.
No cache-error producer or NMI event is invented.

CPUCP-side mailbox banks expose the recovered command, MAP, status, W1C, enable, and geometry interfaces.
All four registered devices are mapped. The two PDP transmitters retain separate commands and share one physical RX owner.
Their declared virtual geometry is 64 channels and one group.
A declared hardware peer receives a command and pending bit, never a synthesized SCMI response.
An absent peer makes command-high fail with a diagnostic.
Leave production peers unbound until cross-aperture routing is established.
The synthetic two-sided fixture declares its own route and does not claim a physical OEM route.

The next CPUCP fault at `0x18860000` is hardware-mutex arbitration, not another mailbox ID register.
Reuse the existing atomic mutex model for all ten configured words.
CPUCP lock one aliases the existing APSS QSEE guard at `0x18860004`.
Do not allocate a private second copy or initialize the guest's software mutex pools.

The next real stop is an MTIME read at `0x18b8bff8` in both firmware images.
Reuse QBox's native ACLINT machine timer at `0x18b84000`, with the recovered 19.2MHz rate.
Its comparator for hart zero drives architectural interrupt 7.
Keep the shared counter at `0x1781d000` separate. The real guest copies its value into MTIME.
Do not synthesize progress on polling reads or inject a timer event without a guest deadline.
This standard timer layout does not establish the physical CPU identity.

The later runtime initializer reads a corrected PTE manufacturing word at `0x221c20ec`.
Expose only that complete read-only word through the shared fuse owner.
Declare its virtual value as zero without claiming a physical fuse capture or a DIE_X interpretation.
Keep the paired `0x221c8780` read on the existing consolidated debug-vector policy.
It is not a `QC_CONFIG0` alias.
The real guest still extracts both bits and applies every configuration and command guard.

The same runtime group reads CPU presence and disabled LLC groups from the existing APSS identity block.
Use required, frozen policy fields rather than zero-filled status memory.
Declare CPU presence `0x033f` to match the existing six-plus-two APSS topology in both compositions.
Declare no disabled LLC groups and leave the real guest's per-SoC way-mask rules intact.
Do not change the existing boot selectors at offsets `0x18` and `0x24`.

The [subsystem evidence](../../../models/qcom/sm88xx/cpucp/subsystem_evidence.md) records exact PCs, virtual policies, public references, and the verification command.
No host callback replaces the real CPUCP service, changes FREE words, or initializes guest callback lists.

## Reset the real UFS device

APSS writes the active-low UFS reset pin at `0x0f1e9000`.
The QBox wrapper has no operation for the device's link state or its SCSI reset state.
A wrapper-only status value would leave the actual backend active.
The libqemu patch therefore adds a named `device-reset-n` input.

The wrapper rejects reset while a transfer remains active.
Assertion removes device presence and transfer readiness in QEMU.
It also resets each attached SCSI device through QEMU's real reset operation.
Release does not start the link.
The operation preserves host configuration, PHY configuration, provisioning, and completed media writes.
The model does not claim a complete UFS power cycle or analog reset timing.



## Retain required graphics exports

The upstream libqemu interface exports SDL functions unconditionally.
A build with SDL disabled failed because generated export code included `ui/sdl2.h` without SDL include paths.
The project retains the required upstream graphics configuration instead of deleting or replacing that interface.
The runtime remains headless. The build disables the Python binder and optional renderers.

## Require an explicit security-image omission

Both selected packages reference `IMAGES/sec.dat` through the selected `rawprogram4.xml`.
Neither archive contains that file.
The package checksums and selection metadata name `sec_smt.dat`, but provide no equivalence rule.
The assembler rejects the missing file by default.
The `omit_secdata = true` profile omits the `secdata` write for a virtual experiment.
The report retains each omitted XML record.
No physical-device access supplied the missing data.
The implementation does not infer that an authentication policy permits missing image data.

## Defer ODP integration until the required gate

The SM8850 run reaches kernel entry with retained early-console output.
The SM8845 run stops before UEFI, and SM8845 kernel execution is not a gate.
Complete Linux boot-log evidence remains pending, so the ODP gate stays closed.
The implementation does not change the ODP repository or add a lane that cannot execute its prerequisite.
The original completion gate remains unchanged.

## Map native CPU clocks before SystemC synchronization

Each QEMU instance has its own virtual-clock epoch.
The quantum keeper must compare mapped times, not raw clock values from different instances.
The narrow QBox patch maps elapsed native time onto each CPU's SystemC epoch.
It preserves native QEMU deadlines.

The mapping restarts after synchronization, idle resume, halt release, and reset.
This excludes native time that passes while the CPU waits for SystemC.
The deadline callback only requests CPU synchronization and rearms its native timer.
It does not move a synchronization target while the CPU waits.

MCIPS is not the platform default.
Its diagnostic reached an all-idle state with detached synchronization windows and no further SystemC progress.
The existing bounded quantum strategy remains selected.

The corrected automatic-observer run consumed the APBL request and reached sequencer command one.
A separate smoke executed two APSS steps and stopped at the actual publication instruction, `0x30f124`.
After a one-second debugger pause, a real step published the APBL header.
TME consumed the request after execution resumed.
The smoke then reached the same unsupported sequencer operation.
The debugger used execution control and breakpoints only.
It did not write guest registers or memory.


## Deliver TME CLIC interrupts through an explicit CPU boundary

The reconstructed runtime requires a CPU change, not only an MMIO model.
ROM `0x2284ca..0x2284d6` writes `mtvec = 0x228302`.
Mode 2 enters the common handler at `0x228300`.
The pinned QEMU rejects mode 2 in `target/riscv/csr.c:3047..3055`.
Its ordinary interrupt path also masks requests through `mie` and limits local IDs to 0..63.
The OEM configuration contains 49 handlers at IDs 16..64.
ROM `0x228358..0x22836a` extracts ten ID bits from `mcause`.
ROM `0x2282ac..0x2282b0` enables ordinary `mie` bits 7 and 11, not all CLIC source bits.

The default-off `x-qcom-clic` CPU property accepts mode 2.
The local `qcom,tme-clic-cpu-v1` adapter enables this property without a QBox fork.
Its 65 named GPIO inputs carry the controller's one-hot selection.
The CPU retains the selected ID in `mcause` and uses ordinary machine trap entry and `mret`.
The CLIC request observes global `mstatus.MIE`, but does not require a corresponding `mie` bit.
The interrupt calculation and WFI work calculation include the CLIC request.
Ordinary CPUs retain direct and vectored behavior.
The inspected runtime does not require `mtvt`, `mnxti`, `mintstatus`, or `mintthresh`.
This implementation does not claim support for those CSRs or selective hardware vectoring.

OEM TMEC descriptor `0x446a38` uses DJB2 hashes for its keys.
`CLICConfig:handlers` points to `0x4473d8`, with size `0x188`.
`CLICConfig:hart_base` at `0x447560` contains zero.
`CLICConfig:config` at `0x447564` contains the 16 layout words.
`CLICConfig:num_config_bits` at `0x4475b4` contains zero.
`CLICConfig:pending_ids` at `0x4475a4` maps IDs 28 and 33 to values 1 and 2.
OEM initializer `0x4377c4..0x4378e6` obtains these records and passes them to the ROM.

The `qcom,tme-clic-v1` controller exposes separate sockets.
Lua maps these sockets and connects each `irq_<id>` to the adapter's `clic_irq_<id>`.
Device interrupt outputs connect to `source_<id>`.

| Socket | Physical base | Width | Firmware evidence |
| --- | --- | --- | --- |
| `pending_socket` | `0x01800000` | byte, IDs 0..64 | `0x2237da..0x2237e6` reads pending before source clear |
| `enable_socket` | `0x01800400` | byte, IDs 0..64 | `0x22366a`, `0x223734`, `0x223774`, `0x223792` initialize, read, enable, and disable |
| `priority_socket` | `0x01800800` | byte, IDs 0..64 | `0x22381a..0x223838` writes and reads priority |
| `threshold_socket` | `0x01800c00` | byte | `0x22367e`, `0x22383a..0x223854` initialize, write, and read threshold |
| `source_socket` | `0x00c00100` | byte at +0, words elsewhere | `0x2237a0..0x2237d8` writes the configured software-pending values |
| `source_socket` + `0x08/0x0c` | `0x00c00108` | word | `0x22380a..0x223816` clears one source bit |
| `source_socket` + `0x10/0x14` | `0x00c00110` | word | `0x2237ec..0x223808` reads source bits, indexed from ID 21 |
| `source_socket` + `0x18/0x1c` | `0x00c00118` | word | `0x223856..0x2238e6` clears both mask words and changes source enables |

Unknown registers, unsupported widths, and unsupported commands fail explicitly.
Source clear removes software-pending state, but cannot suppress an asserted level input.
The controller gates external pending state through the source mask and the CLIC enable byte.
Priority and threshold then select one request.
Larger priority values win. Larger IDs resolve equal priorities.
This priority policy is a functional CLIC substitute, not a captured silicon reset specification.
The explicit `priority_reset` board parameter defaults to zero.
The firmware evidence proves byte accesses, but does not prove priority bit implementation or reset values.
The unused layout field at +`0x24` contains `0x00810010`.
No inspected CLIC routine accesses that field. The model does not fabricate its behavior.

The implementation does not infer edge triggers from level inputs.
No trigger-mode register appears in the inspected layout or routines.
The board supplies actual source levels.
The guest regression delivers ID 64 through a real timer output.
It checks WFI wakeup, global masking, threshold equality, equal-priority ordering, and persistent level inputs.
The guest waits for the pending bit before it checks an asserted timer level.
Timer MMIO uses the CPU's annotated local time, but interrupt publication uses SystemC time.
An expired relative deadline does not guarantee immediate publication during the next MMIO read.
The masked WFI loop checks pending state after each return because WFI can resume before the timer interrupt.
Each return still checks that global masking prevents handler execution.

CLIC CPU migration state uses the conditional `cpu/qcom-clic` subsection.
The subsection is needed only when `cpu.cfg.qcom_clic` is true.
The existing main CPU migration field sequence stays unchanged.

## Preserve live callbacks across reconstructed runtime interfaces

The private reconstruction keeps the OEM descriptor layout separate from the candidate runtime layout.
It moves candidate RAM by `0x70000` to prevent overlap with OEM state.
The OEM still uses its original import table at `0x416800`.
The candidate runtime uses its table at `0x489000`.

The opt-in `qcom,tme-runtime-abi-bridge-v1` adapter translates explicit slot pairs.
A guest write activates translation immediately before the candidate calls the OEM entry.
Mapped reads and writes use the same candidate slot.
This preserves callbacks that OEM constructors replace after initialization.
An unknown slot fails instead of selecting a guessed function.

An explicit `legacy` destination retains a callback in the original table's RAM.
For example, `250 legacy` does not change candidate slot `0x250`.
OEM `0x420e1a` uses two arguments to copy context metadata.
The candidate's slot `0x250` instead supplies a counter getter.
Equal offsets therefore do not establish compatible functions.
The private profile retains this OEM callback until a matching candidate export is established.

Retained callbacks execute their actual firmware bodies.
The adapter does not replace their return values or bypass the existing memory-permission checks.
The guest replaces a retained callback and executes it while the candidate callback at the same offset remains unchanged.

The table self-pointer returns the legacy address to legacy callers.
Candidate-owned objects retain their candidate addresses.
The adapter excludes legacy-table pages from DMI before and after activation.
The guest regression executes callbacks before activation, after activation, and after updates through both table views.

The buffer-validation cohort uses six explicit mappings:

| Legacy slot | Candidate slot | Candidate function | Preserved contract |
| --- | --- | --- | --- |
| `0x9cc` | `0xa04` | `0x2314a8` | Check containment in the shared mailbox buffer. Return status and a separate Boolean. |
| `0x9d0` | `0xa08` | `0x231324` | Check containment between two 64-bit ranges. Reject invalid lengths and overflow. |
| `0x9d4` | `0xa0c` | `0x2314ce` | Return whether a buffer avoids the reserved address range. |
| `0x9d8` | `0xa10` | `0x2313ca` | Check overlap between two 64-bit ranges. Preserve separate status and Boolean results. |
| `0x9dc` | `0xa14` | `0x231484` | Check overlap with `[0,0x0230c000)`. Return status and a separate Boolean. |
| `0x9e0` | `0xa18` | `0x231534` | Check a range against the guest-installed resource table and requested flags. |

The candidate template starts at ROM file offset `0x33c84`.
OEM callers at `0x43341a`, `0x434f08`, and `0x438b12` establish the containment, Boolean, and overlap interfaces.
OEM `0x42b2b0` supplies five arguments, including resource flags `0x104`.
Its table comes from the existing `0x9c8` mapping, whose candidate function retains the guest pointer and count.
Original `0x21bc86` uses the four-argument overlap interface.
Candidate `0x21bd30` instead uses its three-argument Boolean wrapper.
This distinction prevents substitution of an incompatible return convention.

A debugger stopped the actual OEM at `0x42b91c`, before its `0x9cc` import.
The stack contained buffer `0x40c000`, length `0xa4`, and system address `0x2040c000`.
The caller requests an explicit Boolean and then constructs a `0x800`-byte destination span.
Candidate `0x2314a8` checks the same `[0x40c000,0x40c800)` mailbox range through its real containment function.
These mappings execute existing firmware checks. They do not synthesize success or change authentication.

The span helpers retain the two-register pointer-and-length return convention.
Legacy slot `0x804` selects candidate slot `0x840`, whose actual function at `0x2166c4` preserves both input registers.
OEM `0x41e542` and `0x43d9d2` store both returned words.
No heap allocation occurs in this helper.
Legacy `0x7fc` selects candidate `0x838`, whose function at `0x216244` measures a terminated string and returns its span.
OEM `0x43d844` consumes both words from that single-pointer call.
The adapter invokes these existing functions rather than manufacturing a pointer or length.

The random-provider cohort adds two explicit mappings:

| Legacy slot | Candidate slot | Candidate function | Preserved contract |
| --- | --- | --- | --- |
| `0x5d0` | `0x5f0` | `0x217cac` | Accept a provider, request one byte, and perform the existing integrity check. |
| `0x5f8` | `0x618` | `0x21a162` | Fill a destination from a provider. Arguments are provider, length, and destination. Return the operation status. |

OEM `0x4399b4` reads `0x5d0` into the callback record at `0x4117d4`, field `+8`.
This instruction loads a callback. It does not call the callback.
Original wrapper `0x217f2e` obtains a provider through `0x5dc`, then transfers control through `0x5d0`.
Candidate wrapper `0x217e5a` performs the same sequence through `0x5fc` and `0x5f0`.
Original check `0x217cc4` and candidate `0x217cac` request one byte through their buffered random service.
Both preserve the provider argument and use a separate byte destination.
Both start the accumulator at `0x55aa` and add three until the counter reaches the returned byte.
Both check the same result and send failures to the real fatal handler with reasons `0x0f` or `0x10`.
Neither function returns a fabricated success value.

The existing `0x5f0 -> 0x610` mapping supplies that buffered service.
OEM `0x42082a..0x420844` confirms its provider, length, destination, and zero-success return convention.
Original refill call `0x217d7e..0x217d94` selects `0x5f8` with length `0x200` and a writable destination.
Candidate `0x217d70..0x217d82` selects `0x618` with the same arguments and status check.
Original backend `0x21a2ea` and candidate backend `0x21a162` preserve the same three arguments.
Both unwrap the provider through the existing `0x5d8 -> 0x5f8` mapping.
Both reject a null provider, zero length, or null destination with status two.
Both build the same output descriptor and call the existing `0x33c -> 0x338` operation.
Both return its nonzero status, or the operation result at stack offset `0x38`.
The provider getter remains the existing `0x5dc -> 0x5fc` mapping.
These dependencies use real firmware execution and retain its failure paths.

OEM `0x4399be` installs legacy `0x5c8` at callback field `+0x0c`.
The descriptor is stored at context offset `0x1e8`.
Original `0x202182..0x20218e` obtains that descriptor and invokes this callback with reason seven in `a0`.
Candidate `0x201f20..0x201f2c` preserves the same field and argument contract.
Original `0x201f4a..0x201f62` and candidate `0x201ce8..0x201cfe` also pass the selected reason without consuming a return value.

The map therefore connects `0x5c8` to candidate `0x5e8`, function `0x217e8a`.
That function preserves the input reason and forwards it to the existing fatal service.
Original forwarder `0x217f5e` dispatches legacy `0x96c` through `0x2182a6`.
Candidate `0x217e8a` dispatches mapped service `0x9a4` through `0x2181d6`.
The callback retains fatal handling instead of replacing a failure with success.
The descriptor consumer establishes this contract, not the numeric `+0x20` pattern.
Legacy `0x9e4` and `0x81c` remain unproved and unmapped.
This analysis reads OEM ELF load segments at their TME addresses, not at raw file offsets.
It does not change authentication, the relocation recipe, or vendor bytes.

The random-request resolver maps legacy `0x6c0` to candidate `0x6e0`, function `0x220234`.
This extends the existing random-provider group. It does not replace the provider or its integrity checks.
The canonical SM8850 trace stops at OEM `0x41f9da`, which reads `0x416ec0`.
That address is legacy table `0x416800` plus `0x6c0`, not an absent SRAM allocation.

The preserved original ROM supplies independent producer and backend evidence:

| Address | Contract |
| --- | --- |
| `0x20dd00..0x20dd34` | Accept provider, byte length, and destination. Reject null inputs. Construct `[0, destination, length, 0]`. |
| `0x20dd86..0x20dd9a` | Construct the completion object and call `0x20d64c(length, completion)`. |
| `0x20d64c..0x20d682` | Clear a `0x78`-byte message. Store selector `0x2002` at `+4`, byte length at `+0x10`, and completion at `+0x14`. Submit through `0x20d744`. |
| `0x213298 + 4` | The original dispatch table selects `0x20d9e0` for selector `0x2002`. |
| `0x20d9e0..0x20d9e4` | Pass message `+0x10` to original hardware operation `0x2116e0`. |

Original callers `0x200f44..0x200f4a` request one byte into `sp+0x0b`.
Caller `0x20034e..0x200352` requests four bytes into `sp+8`.
These calls establish byte units, not words or a destination pointer in payload word zero.
The preserved producer executes in the reconstruction because its address precedes the unchanged `0x216000` boundary.
The retained canonical trace executes both these calls, `0x20dd00`, and the selector construction at `0x20d674`.

OEM `0x41f6c6..0x41f6f4` copies the complete `0x78`-byte message before dispatch.
OEM `0x41f808..0x41f8ba` resolves its selector through the original OEM table at `0x44332c`.
Selector `0x2002` selects the import load at `0x41f9d6..0x41f9da`.
The common tail at `0x41fa2e..0x41fa38` passes only message `+0x10`.
It returns status two when the operation returns true, or zero when it returns false.
This is not a zero-success callback interface.

Original backend `0x2116e0..0x21173e` and candidate `0x220234..0x220298` consume the same two-word payload:

| Payload field | Preserved operation |
| --- | --- |
| `+0` | Byte length. Program hardware field seven with `(length + 3) >> 2`. |
| `+4` | Completion object. On failure, invoke its first virtual method with object in `a0` and reason two in `a1`. |

Both backends first prepare the hardware, select operation `0x13` through field one, then clear fields eight and nine.
Both execute the operation only after those writes succeed.
Both retain a false result on failure, regardless of the failure callback's return value.
Original completion table `0x21303c` selects `0x20d2c8`, which stores the supplied reason at object `+0x20`.
The donor resolver at `0x21c430..0x21c434` exports this operation through slot `0x6e0`.
Its common tail `0x21c53e..0x21c548` preserves the same Boolean-to-status conversion.
The existing mappings `0x710 -> 0x730`, `0x640 -> 0x660`, and `0x6c8 -> 0x6e8` supply preparation, field writes, and execution.
No result, entropy byte, completion, or authentication decision is supplied by the bridge.

The transfer resolver cohort maps five wrappers and their shared context services.
The CPH2771 OEM image supplies independent callsites, not shifted SM8850 program counters.
Its `signed_firmware_soc_view.elf` SHA-256 is `6a781e1eb215fbcd2d157bebb7b25502f6ae74ed9bce7810dc61f98545c10fb4`.
The retained SM8845 trace stops at `0x41f90c`, reading legacy `0x6dc` at `0x416edc`.

CPH2771 resolver `0x41f868` selects these cases through its table at `0x443268`:

| Selector | CPH2771 import load | Legacy slot | Candidate slot | Original handler | Candidate handler |
| --- | --- | --- | --- | --- | --- |
| `1` | `0x41f90c` | `0x6dc` | `0x6fc` | `0x21f0f0` | `0x21f282` |
| `3` | `0x41f968` | `0x6cc` | `0x6ec` | `0x21f1a8` | `0x21f324` |
| `5` | `0x41f9ae` | `0x6e0` | `0x700` | `0x21f250` | `0x21f3d0` |
| `6` | `0x41f954` | `0x6d0` | `0x6f0` | `0x21f2fa` | `0x21f474` |
| `7` | `0x41f95e` | `0x74c` | `0x77c` | `0x21f3a4` | `0x21f51e` |

The common tail at `0x41f9b2..0x41f9be` passes message `+0x10` and message `+8`.
These are separate payload and result pointers, unlike the one-pointer random operation.
True preserves resolver status two. False changes that status to zero.
The bridge does not manufacture either the Boolean result or the result byte.

The preserved original producers establish the payload independently.
Original `0x20d334..0x20d398` clears a `0x78`-byte message and writes selector one at `+4`.
It saves the completion object at message `+0x28`, which is payload `+0x18`.
It also writes the original first argument to completion field `+0x28`.
Original `0x20d3be..0x20d40c` constructs the same selector-one layout with a different state field.
The retained CPH2771 trace executes both constructors.
CPH2771 `0x41f746..0x41f774` copies the complete message before calling resolver `0x41f868`.
The donor wrappers preserve the original handlers' field loads and argument roles:

| Selector | Builder arguments `a2`, `a3`, `a4` | Descriptor pointer, count | Executor arguments `a1`, `a2` | Completion field |
| --- | --- | --- | --- | --- |
| `1` | payload `+8`, `+0x0c`, zero | payload `+0x10`, one | payload `+0x14`, minus one | `+0x18` |
| `3` | payload `+0x0c`, `+0x10`, zero | payload `+0x14`, one | payload `+0x18`, payload `+8` | `+0x1c` |
| `5` | payload `+8`, `+0x0c`, zero | payload `+0x10`, payload `+0x14` | payload `+0x18`, minus one | `+0x1c` |
| `6` | payload `+0x0c`, `+0x10`, zero | payload `+0x14`, payload `+0x18` | payload `+0x1c`, payload `+8` | `+0x20` |
| `7` | payload `+8`, `+0x0c`, payload `+0x10` | payload `+0x14`, payload `+0x18` | payload `+0x1c`, minus one | `+0x20` |

Every builder receives a cleared `0x8c`-byte context in `a0` and payload word `+4` in `a1`.
Its completion object occupies `a7`. The ninth argument is the separate result pointer on the stack.
Each executor receives that context in `a0`, payload word zero in `a3`, completion in `a4`, and result pointer in `a5`.
A null completion prevents wrapper execution and returns false.

Three additional mappings complete the shared dependency group:

| Legacy slot | Candidate slot | Original body | Candidate body | Preserved role |
| --- | --- | --- | --- | --- |
| `0x4d4` | `0x4e8` | `0x21f48e` | `0x21f5ee` | Initialize the context and reject an unsupported block selector. |
| `0x4dc` | `0x4f0` | `0x21f5a8` | `0x21f6ec` | Build bounded descriptor and partial-block state. |
| `0x67c` | `0x69c` | `0x21f7a0` | `0x21f8c0` | Program and execute the real operation, then report completion. |

Original helper `0x21fb46` loads `0x4dc`; candidate helper `0x21fc46` loads `0x4f0`.
Both clear the caller's result byte before the builder call.
Both initializers use the existing `0x160 -> 0x164` block-selection service.
They retain the same context fields `+0` through `+0x1c`, including sign-derived modes and states zero through five.

Both builders reject more than eight descriptors.
They consume 16-byte descriptor entries, choosing address word `+4` or fallback word `+0` and length word `+0x0c`.
They accumulate the length at context `+0x70` and retain descriptor count at `+0x6c`.
They preserve partial-block fields `+0x7c`, `+0x80`, `+0x84`, and `+0x88`.
Capacity failures and incompatible state transitions invoke the real completion method with reason two and return false.
The context initializer, provider lookup, and capacity operation use existing mappings `0x4d4 -> 0x4e8`, `0x158 -> 0x15c`, and `0x4d8 -> 0x4ec`.

Both executors prepare hardware and program the same fields through the existing `0x640 -> 0x660` service.
They preserve context state in fields one, seven, eight, nine, thirteen, fourteen, and fifteen.
They use the existing `0x6fc -> 0x71c`, `0x700 -> 0x720`, `0x704 -> 0x724`, and `0x708 -> 0x728` transfer operations.
The final operation still executes through `0x6c8 -> 0x6e8`.
The real result controls the completion reason: zero for success or two for failure.
The guest-owned completed-context path remains in firmware. The host does not seed its flags.
These mappings preserve the shared layout and operation contract, not instruction-for-instruction identity between ROM versions.
The original export template is unavailable, so shared-service slot attribution also uses the inspected consumer and body dataflow.
It is an explicit compatible attribution, not an inference from nearby offset deltas.
Selector equality alone does not authorize a mapping.
For example, OEM selector `0x13` uses legacy `0x790`, whose retained evidence selects candidate configuration slot `0x7d8`.
The donor's selector `0x13` instead selects `0x7c0`, a handler that invokes reason two and returns false.
This numbering mismatch does not authorize a change to the existing `0x790` mapping.

The [execution evidence](oplus/bringup.md#optional-common-runtime-reconstruction) records the current common map and recipe identities.
The original, donor, adapter, and reconstructed ROM hashes remain unchanged.
This evidence comes from static instructions and retained logs. It does not claim a successful replay or kernel execution.

Legacy slot `0x3c0` maps to candidate `0x3c0` under an explicit cold-only compatibility policy.
This is a behavioral inference, not recovery of the unavailable original ROM tail.
The candidate template at `0x233c84 + 0x3c0` points to `0x2311ea`.
Its instructions read `0x103f0040` and compare the result with `0xa0c00c1e`.
The common `seqz` tail at `0x231260` returns the equality result.
The bridge executes that real predicate. It does not replace the function with a constant.
Candidate slot `0x3d4` instead points to the unrelated always-false function `0x2311e6` and is not substituted.

Both known OEM consumers preserve their existing cold outcomes:

| Consumer | False result | True result |
| --- | --- | --- |
| `0x420410..0x420438` | Return false before configuration lookup. | Require `0x421638(0x21) == 5`. |
| `0x427ae2..0x427b38` | With initialization flag `0x4480f4` clear, invoke legacy `0x91c(1)` through `0x427cf0`. Leave the flag clear. | Validate the LPE table, initialize interrupt 24, install vector `0x428050`, then set the flag. |
| `0x4372a4..0x43734c` | After object classification seven, return zero before optional power preparation. | Continue object checks, configure power levels, and issue the existing notification. |

The second predicate reader is at `0x4372cc`. It is not an isolated LPE-only interface.
Its true branch calls `0x435cfa`, which requires `0x420410` again.
That function reads `cx.lvl` and `mx.lvl`, encodes voltage indices, and writes `0x22370a8` and `0x22370ac`.
It sets `0x448189` only after both values pass validation.
The caller then obtains legacy `0x3b8` and submits its descriptor through legacy `0x3f8(4, 2, descriptor)`.
The existing donor notification uses request opcode `0x2009` and hardware operation `0x1d`.
Those true-branch hardware effects remain outside the cold-only compatibility claim.
The inspected predicate consumers do not directly grant authentication, rollback, lock, or debug authorization.
The second path is PIL-adjacent, so this evidence does not establish unrestricted warm-start compatibility.

The selected OEM package independently uses the same readiness cookie.
Its `IMAGES/pdp.elf` SHA-256 is `74fb165a6619f2451b56360eddd2628827095497593474089316056de8437e8d`.
Thumb `0x81301454..0x8130148e` reads header `0x0c3f0000`.
It polls `*(header[0] + 0x0c300000)` for `0xa0c00c1e`, then returns `header[3]`.
If the cookie remains absent, the bounded polling loop returns zero.
This establishes an OEM readiness-cookie consumer, not the header-derived marker offset or a positive-cookie producer.
Candidate `0x2311fc` clears `0x103f0040`; the inspected candidate code contains no positive-cookie writer.

The platform supplies coherent, guest-owned AOP message RAM and its TME view.
Cold startup leaves the configuration unpublished, with no valid cookie or table pointer.
The marker-offset correspondence is an explicit compatibility inference, not recovered original code or a measured reset value.
The donor predicate reads this shared memory. Its real clear routine can clear the same memory.
The platform does not replace the marker with a private fixed-zero register or replace the callback with a constant.
An unpublished cookie selects the firmware's real false branch without a fabricated success result.
Neither the platform nor the bridge seeds `0x4480f4`, voltage tables, or completed power preparation.
Authentication and rollback continue through their existing firmware paths.

The original input ends at ROM address `0x220000`, before the candidate predicate's address.
Its inspected instructions do not recover the marker comparison.
The OEM load segments contain no replacement store to legacy slot `0x3c0`.
Retained reconstructed RAM contains a donor-derived pointer, not independent original backend evidence.
These limits remain explicit. This mapping claims only the selected cold-state behavior, not general original ABI equivalence.

Legacy delay slot `0x91c` maps to candidate `0x954`, function `0x2228d2`.
The legacy attribution is a behavioral compatibility inference from the complete inspected timing-consumer group, not the numeric displacement.
The original input does not contain this runtime tail.
The bridge executes the candidate's real counter wait. It does not advance time, supply a return status, or manufacture lock.

OEM consumers establish a scalar duration and no consumed return value:

| OEM instructions | Timing contract |
| --- | --- |
| `0x422b62..0x422b7c` | Poll PLL MODE, delay with argument one, and decrement a 500-iteration bound. Preserve the existing lock failure. |
| `0x422bb4..0x422bf2` | Write PLL controls, delay 100, change the next control, delay 300, clear that control, then delay 100. |
| `0x422c34..0x4232aa` | The remaining clock-control routines use the same delay import between control writes and bounded readiness polls. |
| `0x427cf0..0x427cfe` | The cold LPE path tail-calls the delay with argument one. |
| `0x429c60..0x429c6a` | Tail-call the same delay with argument ten. |
| `0x4376d2..0x4376fc` | Copy and clear four pending-state words, then delay with argument one. |
| `0x441e12..0x441e24` | Poll an operation's real completion field with argument-one waits and a finite iteration bound. |

These are microsecond-scale stabilization and polling contracts.
The existing clock models also use microsecond acquisition, not read-count readiness.
That supports the legacy unit inference. The candidate arithmetic establishes its unit independently.

Candidate `0x2228d2..0x222926` performs these operations:

1. Read the 64-bit counter through `0x22aac8`, returning low word in `a0` and high word in `a1`.
2. Zero-extend the original scalar argument into `a0:a1`. Set conversion selector `a2` to one.
3. Call converter `0x22aadc`. Add its two-word result to the captured counter with carry.
4. Read the counter until its unsigned high/low words reach the computed deadline.
5. Restore the caller's saved registers and return. The OEM callers do not interpret a success code.

The converter's table at `0x2331f4` establishes the related unit cohort:

| Selector | Target | Conversion to counter ticks |
| --- | --- | --- |
| `1` | `0x22aafc` | Microseconds: multiply by frequency, then divide by 1,000,000. |
| `2` | `0x22ab50` | Milliseconds: multiply by frequency, then divide by 1,000. |
| `3` | `0x22ab30` | Seconds: multiply by frequency. |
| `4` | `0x22ab1e` | Minutes: multiply by 60, then frequency. |
| `5` | `0x22ab0c` | Hours: multiply by 60 twice, then frequency. |

The microsecond small-input path is `0x22abd2..0x22abee`.
It applies to every zero-extended 32-bit delay argument.
The other branch handles larger two-word inputs with quotient/remainder arithmetic.
The millisecond implementation is at `0x22abf0..0x22aca2`.
The converter preserves its existing fallback for selectors outside one through five; this mapping does not add such callers.

Initializer `0x22aa84..0x22aac6` obtains configuration base `0x00c20000` and counter frame `0x00c22000`.
It records the frequency from configuration word zero, or uses the existing fallback when that word is zero.
The fallback data at candidate `0x234930` is 19,200,000 Hz.
Counter `0x22aac8..0x22aada` reads high, low, and high again, retrying if the high words differ.
It does not synthesize progress by counting reads.
The existing local timer uses a separate 19.2 MHz virtual counter that advances with simulation time.
Its contract is recorded in `models/qcom/sm88xx/apss_boot/evidence.md`, under TME local timer frames.
At that rate, the candidate converts one microsecond to 19 ticks, 100 microseconds to 1,920 ticks, and 300 microseconds to 5,760 ticks.
The scheduler's separate 600 MHz machine timer does not define this conversion.

The directly related cohort consists of this delay, the converter, the stable counter reader, and their frequency initialization.
Those candidate-internal calls already execute directly and need no additional legacy mappings.
Neighboring exported slots implement other interfaces and are not added by displacement.
The firmware retains its existing deadline arithmetic, counter-width behavior, timeout bounds, and failure paths.
This change does not alter authentication, rollback, lock state, or debug authorization.


## Switch the virtual requester at runtime activation

The reconstructed path needs two CPU requester identities.
The original boot ROM requires domain one for its write to `0x408004`.
OEM `0x426162` later installs domain-two-only permissions over its executing image.
A single static domain fails one of these operations.
The observed OEM CPU still uses machine privilege and protection mode `0x0a`.
Those states do not identify a usable transition.

The compatibility adapter therefore publishes its existing activation event as `runtime_active`.
An explicitly connected protection port selects its configured `runtime_qad` when that signal becomes true.
The private profile changes only the three TME CPU ports from domain one to domain two.
DMA requester domains remain unchanged.
The ports invalidate direct mappings when their identity changes.
They do not retain both domains as permanent capabilities.

This event describes the virtual compatibility boundary.
It does not claim a physical QAD control register or remove image authentication.
An unconnected port retains its original identity.
The actual boot passes the previously denied OEM instruction fetch and reaches a later protection-register write.


## Enable architectural protection in the TME CPU

The reset composition enables QEMU's physical memory protection support for the TME CPU.
QBox otherwise disables this capability by default.
The OEM reads `pmpcfg0` at `0x42ae18`.
Without this capability, that instruction raises an illegal-instruction exception and the firmware enters its fatal loop.

The real runtime now reads all four configuration registers.
It writes and reads all sixteen address registers before it writes the configuration registers.
It then reaches the next initialization call at `0x41c276`.
This proves the initialization path, not every protection rule or later privilege transition.
QEMU retains architectural PMP enforcement.


## Curve descriptor classification

SM8850 wrapper `0x435c70` calls legacy import `0x2ac` through helper `0x435ce0`.
CPH2771 reaches the same import at `0x435d4a`.
The helper passes descriptor word `+8`.
The wrapper writes a result other than `-1` into descriptor word zero.
It then calls the retained signature service.

Donor import `0x2a4` exports classifier `0x21abe2`.
It returns class one for IDs `2`, `3`, `19`, and `24`.
It returns class two for IDs `4` and `21`.
It returns class three for IDs `5` and `22`.
Other IDs return `-1`.
Original ROM function `0x21ae14` implements the same classification through explicit comparisons.

The compatibility map therefore selects `0x2ac -> 0x2a4`.
This decision uses the descriptor contract, not the distance between import numbers.
The function computes a control class.
It does not validate a signature or replace an authentication result.

### SM8850 image-authentication hash services

The reported fault at `0x43a9c4` reads `0x416ca8`, legacy import `0x4a8`.
Static decoding uses the selected CPH2749 package's `RADIO/tme_fw.img` ELF.
Its load address `0x2041c000` corresponds to TME address `0x41c000`.
The original and donor ROM identities are recorded in `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md`.
This investigation does not execute firmware or claim successful image authentication.
Both current OEM runs print `Auth Metadata`.
That observation supersedes the earlier metadata-wait boundary, but does not prove this hash operation completed.

The following group follows matching authentication callers, argument registers, descriptor fields, and backend phases.
The original ROM lacks its export table.
Original wrapper attribution therefore remains a behavioral inference, supported by its retained callers and matching bodies.
All addresses below use the TME view.

| Legacy → donor | SM8850 import load | Original import load | Matching donor import load | Original wrapper → donor wrapper | Operation |
| --- | --- | --- | --- | --- | --- |
| `0x4a8 → 0x4bc` | `0x43a9c4` | `0x219b26` | `0x219a16` | `0x21a56c → 0x21a42c` | Single-descriptor one-shot hash |
| `0x4ac → 0x4c0` | `0x43b422` | `0x219d34` | `0x219c0a` | `0x21a5da → 0x21a48c` | Single-descriptor finalization |
| `0x4b0 → 0x4c4` | `0x43aed4` | `0x219e3c` | `0x219ce8` | `0x21a6b8 → 0x21a55a` | Vector one-shot hash, retain the existing mapping |
| `0x4b4 → 0x4c8` | `0x43af0a` | `0x21a008` | `0x219eb0` | `0x21a71c → 0x21a5be` | Vector finalization |
| `0x4b8 → 0x4cc` | `0x43aef0` | `0x219efe` | `0x219dae` | `0x21a6f2 → 0x21a5a0` | Vector update |
| `0x4bc → 0x4d0` | `0x43b35c` | `0x219c5a` | `0x219b3e` | `0x21a5b6 → 0x21a470` | Single-descriptor update |

Donor export words at `0x233c84 + donor_import` independently identify all six donor wrappers.
The current authority is `firmware/qcom/sm88xx/tme-runtime/tme-runtime-api.map`, not the obsolete private automatic maps.
It already contains `0x4b0 → 0x4c4`. The other five rows are supported additions.

Let `h` be the hash selector, `i` the input descriptor pointer, `s` the saved-state object, and `c` the context.
Let `n` be the descriptor count and `w` the caller's execution-mode argument.
The wrappers preserve these contracts:

| Service | Caller arguments | Donor backend arguments |
| --- | --- | --- |
| Single one-shot | `(h, i, c, w)` | `0x298(h, 0, 3, i, zero_state, c, w)` |
| Single update | `(h, i, s, c, w)` | `0x298(h, 1, 3 + (s[3] != 0), i, s, c, w)` |
| Single final | `(h, i, s, c, w)` | `0x298(h, 1, 5, i, s, c, w)` |
| Vector one-shot | `(h, i, n, c, w)` | `0x29c(h, 0, 3, i, n, zero_state, c, w)` |
| Vector update | `(h, i, n, s, c, w)` | `0x29c(h, 1, 3 + (s[3] != 0), i, n, s, c, w)` |
| Vector final | `(h, i, n, s, c, w)` | `0x29c(h, 1, 5, i, n, s, c, w)` |

`zero_state` is a real 16-byte, stack-owned zero object, not a fabricated digest.
The wrappers return the backend's actual result.
They do not convert a backend error to success.
Donor backend `0x298` is `0x21a802`, corresponding to original `0x21aa2a`.
Donor backend `0x29c` is `0x21a97a`, corresponding to original `0x21aaa8`.
They construct zero-initialized `0x78`-byte requests with opcode one and opcode five, respectively.
Both retain the mode, phase, selector, input pointer, context pointer, and execution-mode argument.
The vector request also retains the scalar count and saved-state pointer.
Both store `h` at context offset `0x28`.
Phase three clears context offset `0x34`. Later phases preserve that field.

One-shot mode invokes the existing context initialization through donor `0x15c`.
The backend then calls the real dispatcher through donor `0x540`, function `0x21c142`.
Its status, synchronization, callback, and error paths remain active.
No new legacy mapping is needed for these donor-internal dependencies.
Existing `0x158 → 0x15c` and `0x528 → 0x540` mappings preserve the related OEM interfaces.

The faulting caller constructs a 16-byte input object at `sp+0x24`.
Its words are zero, the actual data pointer, the byte length, and the same byte length.
It rejects pointer-plus-length wrap before the call at `0x43a9b4`.
The output object at `sp+0x34` contains zero, a buffer pointer, capacity `0x30`, and initial output length zero.
Constructor `0x41cfb2` stores that output object at context offset `0x1b0`.
The initialized context at `sp+0x64` retains status at `+0x20` and synchronization state at `+0x24`.
The caller passes `w=1`.
These objects remain guest-owned. The mapping neither allocates replacement objects nor changes ownership.

The hash selector comes from `0x43984c`.
Its table at `0x4452c4` maps caller IDs two through five to `1, 2, -1, 3`.
The caller rejects `-1` at `0x43a8e0`.
For this SHA-384 path, the caller reserves 48 output bytes.
At `0x43a9d2..0x43a9e0`, it rejects either a nonzero service result or context status, then requires exactly 48 output bytes.
The later checks at `0x43a9ee` and `0x43aa02` must return the existing success token `0x2ec88ff0`.
This mapping does not replace those checks, the preceding signature path, or the version-policy checks.

The vector caller at `0x43ab00` constructs 16-byte input records from actual pointer/length pairs.
It uses one-shot import `0x4b0` when the whole vector fits the current batch.
Otherwise it batches at most eight records and preserves the state object at `sp+0x40`.
Import `0x4b8` processes intermediate batches. Import `0x4b4` processes the final batch.
All three paths preserve the backend return and context status checks at `0x43af1e..0x43af24`.
The single update/final pair independently shares state at `0x447a30` and the context pointer at `0x448200`.
The final call uses an empty input descriptor. It still finalizes the actual retained state.

Rejected candidates explain why ordinal matching is insufficient:

- `0x4b0 → 0x4c0` is incorrect. The OEM passes a scalar count, but `0x4c0` treats that argument as a state pointer.
- `0x4ac → 0x4d0` is incorrect. The OEM uses this call for finalization, while `0x4d0` selects phase three or four.
- Swapping vector `0x4b4` and `0x4b8` is incorrect. The decoded batching branches distinguish final phase five from update phases.
- Donor import `0x4a8` clears the readiness cookie through `0x2311fc`. It is not the faulting legacy hash service.

No additional neighboring import is proposed without a decoded caller and argument contract.
This evidence does not establish unobserved descriptor variants or successful native execution.
The existing hash submission, DMA protection, SHA-384 completion, output transfer, and notifier dependencies remain required.
The hardware completion contract remains `0x6e4 → 0x704`, as documented in the runtime ABI evidence.
No root hash, authentication result, rollback result, fuse state, or cryptographic output becomes a host-supplied constant.

## APSS architectural instruction coverage

The APSS wrapper uses QEMU's maximum AArch64 CPU through the existing QBox A710 interface.
This substitute supplies architectural instructions without a new QBox CPU implementation.
It does not claim Oryon identity, performance, or cycle timing.
The selected QSEE entry requires MPAM and PAuth instructions.
The selected SM8850 QHEE entry also requires RNDR and RNDRRS.

The pinned QEMU implementation already supplies PAuth and host-backed random instructions.
Patch `0008-implement-single-partition-mpam.patch` adds an opt-in MPAM implementation with one partition in each security space.
External SystemC devices cannot implement CPU system registers, exception-level traps, or instruction semantics.
This limitation requires the libqemu patch.
The patch preserves architectural access checks and does not authorize memory or authenticate images.
Unknown system registers remain undefined.

The genuine AArch64 regression `apss_executes_mpam_pauth_and_rng` passed.
It verifies MPAM restrictions, VHE aliases, PAuth operations, and random instructions.
Its guest handles 35 expected exceptions.
NV2 redirection remains implemented but untested.
The original SM8850 boot also reached PMIC completion and UFS initialization with this CPU.

## UFS initialization and device readiness

Local UIC capability reads must remain available before HCE and PHY setup.
The selected XBL performs these reads after its host reset.
Live peer requests still require device presence and a released reset.
Link startup publishes the locally cached peer identity without changing that distinction.

PHY start does not require writes for an unused lane.
The selected XBL omits the second-lane table when the virtual device advertises one lane.
Power, reset release, live clocks, PLL enable, and explicit start still control readiness.
The regression fails before this correction and passes afterward.

The pinned QEMU backend omits TEST UNIT READY for the UFS device LUN.
The original OEM request targets LUN `0xd0` with zero command parameters.
Patch `0009-implement-ufs-device-readiness.patch` implements this hardware command in the existing backend.
This placement preserves the UTP queue, SCSI sense data, interrupt, and DMA ownership.
An external completion override would duplicate that state and conceal genuine failures.
The patch checks existing readiness state and rejects invalid command fields.

Both OEMs pass the corrected readiness operation and print the QEMU UFS inquiry identity.
Later configuration and WriteBooster corrections are described below.
The complete integration run passes 74 tests and Clippy with warnings denied.

## UFS configuration and WriteBooster

Patch `0010-expose-ufs-configuration.patch` derives the read-only configuration descriptor from existing QEMU device state.
Its virtual allocation unit contains one 4096-byte block.
This geometry keeps allocation counts consistent with actual backing sizes, including small fixtures.
The existing configuration lock remains authoritative.

Ordinary UFS initialization enables three WriteBooster flags.
QEMU already implements writes and buffer accounting, but rejects the hibernation flag.
Patch `0011-enable-ufs-hibernation-flush.patch` connects that flag to the existing flush engine.
The link state selects the active or hibernating flush policy.
The idle timer uses virtual time to avoid dependence on host execution speed.
No separate storage engine or completion override is necessary.

The guest regression checks allocation against SCSI capacity and checks flush effects under both link states.
It also verifies that disabled flushing preserves occupancy and that actual media bytes survive.
SM8845 run51 completes all three media phases and writes its log partition through this backend.
The integration run passes 74 tests and Clippy with warnings denied.
Kernel execution remains unobserved.

## SHRM release and direct TME consumers

Sequencer DMA aliases do not cover direct TME CPU accesses.
The platform exposes the existing image backings to both consumers without copying or replacing guest data.
The TME protection path remains unchanged.

The selected TME writes ten, two, and zero to SHRM processor control at `0x31079000`.
This control owns backend reset and startup.
The APSS clock-gating register retains its separate function.
Reset removes staged commands and backend state without clearing guest memory.
Readiness follows successful backend startup, not a clock-gating write.

The selected cold DDR route needs retained status controls, not fabricated first-pass completion or an artificial CPU reset.
Both boards use explicit zero cold values for the five observed boot-pass words.
SM8850 run59 and SM8845 run54 complete SHRM image loading and reach calibration reads.
The focused guest regression verifies reset, shared control views, memory preservation, and repeated startup.
The complete integration run passes 75 tests and Clippy with warnings denied.
Kernel execution remains unobserved.

## CPR calibration and selected chip revision

The existing fuse controller owns raw calibration reads through all existing aliases.
A bounded policy supplies zero corrections for 58 selected words.
The SM8850 PDP image adds two proven groups to the 33 XBL words.
The corrected plan selector uses the existing read-only memory convention.
No new security, identity, or authorization fallback exists.
The guest retains its voltage tables and derived rail state.

SM8850 run572 shows that declared zero is not sufficient for the real PDP path.
The VPP path asserts at `0x8132937e` with `VPP: Voltage Setting Fuses not valid`.
The voltage-setting record must have nonzero int16 fields at `+0x1c` and `+0x1e`.
One private read of a selected phone's raw QFPROM page supplies that record.
The record is `20261026T230217Z-phone-qfprom-calibration-read`.
The page covers `0x221c8000..0x221c8fff` and has a fixed SHA-256.
The page and its values stay out of Git.
The manifest input `calibration_fuses` names the path and hash.
Prepare and run require exactly 4096 bytes and the declared hash.
The model serves the page word for every listed offset and keeps unlisted offsets closed.
SM8845 rejects the input and keeps the zero policy.
Without the input, the model prints `source=declared-zero`.
With the input, the model prints `source=private-page` and the page hash.

SM8845 rejects virtual chip revision 2.0 because its selected CPR plan permits only revisions 1.0 through 1.255.
The board now reports revision 1.0 through its existing identity inputs.
This change preserves OEM SoC identity `0xa022` and the separate sequencer identity.
The model does not change the firmware's plan selection or suppress its assertion.

SM8850 publishes a PMIC-derived selector at `0x19978178`.
The existing retained-control model owns that word without producing a voltage or completion.
SM8850 run61 and SM8845 run57 pass these dependencies and reach the first CPR controller write at `0x62800c`.
The integration run passes 75 tests and Clippy with warnings denied.
Kernel execution remains unobserved.

## Target-derived CPR measurements

One shared controller model serves both selected board inventories.
The selected XBL tables supply sensor counts, thread counts, and bases.
The selected instructions supply the thread stride and target layout.

The virtual oscillator follows its programmed target after the measurement prerequisites hold.
This policy permits ideal analog behavior without fabricating a firmware decision or a missing input.
The model retains configuration and implements mask publication, measurement control, and interrupt status.
Voltage services and authentication remain separate.

The AArch64 regression passes, and the integration run passes 76 tests with Clippy.
SM8850 run62 advances to AOSS configuration at `0x0c260304`.
SM8845 run58 stalls during an earlier APSS reset and does not exercise the controller.
The reset regression requires separate investigation.
Kernel execution remains unobserved.

## Complete pre-DDR hardware groups

The board inventories now supply all four Railway ACC controls and both complete DDR interconnect groups.
The guest programs exact retained fields.
The existing DDR frontend supplies the source for the guest's period-value copy.
No model invents measured timing or poll completion.

The APSS watchdog reuses the existing timer engine through a separate KPSS register version.
Its pet operation remains effective even when the early debug-power policy leaves the timer disabled.
An unconnected bite remains a logged terminal event, not a fabricated reset.

SM8845 supplies its complete seventeen-entry PMIC1 LDO bank through the existing regulator service.
GPIO inputs follow guest-programmed pulls under an explicit cold-enabled policy.
The guest can disable each pin.
No forced button value or recovery bypass exists.

The integration run passes 76 tests and Clippy.
SM8850 run66 and SM8845 run64 complete pre-DDR initialization and enter `sbl1_ddr_init`.
Both then assert on a separate regulator-mode request.
The APSS reset race remains unresolved despite later successful boot attempts.
Kernel execution remains unobserved.

## Complete DDR entry dependencies

The board inventories now supply 94 SM8850 rails and 84 SM8845 rails.
Selected discovery masks, provider counts, and register strides constrain their virtual placement.
The common regulator model retains its existing revision, voltage, enable, and status behavior.

SM8845 originally declared one BOB control despite the provider's two allocated slots.
Run66 captured the second slot with type zero in the original status accessor.
The corrected virtual controls use mask-selected peripherals `0x3e` and `0x40`, with the native stride.
No firmware mask or return value changes.

The two AOP SRAM regions now expose TME views of the existing APSS memory.
SM8850 run67 established the missing byte-copy input at TME address `0x0f08d000`.
The alias does not create a second backing or supply image bytes.

The integration run passed 76 tests and Clippy.
SM8850 run68 and SM8845 run67 pass the regulator requests and report DDR initialization with the original SHRM image.
Both next wait for the DDR reference acknowledgement.
Kernel execution remains unobserved.

## Stateful DDR configuration and native geometry

The DDR reference handshake now uses the existing source's live input reference.
The selected firmware does not establish a requirement for both PLL outputs.
The branch owners also retain the selected bit-27 request.
The generic PLL behavior remains unchanged.

MC and LLC configuration now has four independent banks and one broadcast view.
Known command words retain dedicated behavior.
Undeclared configuration within these bounded regions uses logged retention from cold zero.
This policy permits functional progress without silently inventing ready bits or clearing unknown requests.

The direct command engine applies mode-register writes and publishes read results before completion.
Its virtual identity supplies the same 4 GiB capacity as the SHRM backend.
Native geometry uses 14 row bits, ten column bits, and sixteen banks.
The model does not write the guest's converted geometry objects or flash header.

The backend now respects the selected DCB runtime length and adjacent extension.
A native debugger capture also established that AOP row offset four contains an index, not a frequency.
The enabled idle row at index zero is valid.
The guest regression verifies initialization with that row.

The integration run passes 77 tests and Clippy.
The guest tests cover sparse descriptors, rank state, broadcast behavior, independent configuration, and reset ownership.
Kernel execution remains unobserved.

SM8850 run83 and SM8845 run80 pass native geometry publication, the next direct command, and subsequent SHRM and reference configuration updates.
Both next require the LLCC register group at `0x348640b0`.

## Execute PXI traffic against shared DRAM

The frontend now executes the guest's DQ packet lists against the existing DRAM backing.
It compares actual read bytes with guest patterns or guest-seeded PRBS output.
A completion response therefore follows data effects, rather than replacing them.
The virtual interleave and RRBC policies remain explicit.

The descriptor engine also derives oscillator results from the guest's MR37 duration.
Its ideal oscillator period is one CK.
It validates each selected batch before it publishes MR effects.

Configuration windows preserve unknown writes with substitute logs.
The existing request, completion, masked-control, and live-source owners remain authoritative.
This policy avoids a separate mapping change for every ordinary configuration word.

The guest regression checks shared-memory writes, channel isolation, aperture crossings, and corruption-dependent PXI mismatches.
The integration run passes 77 tests and Clippy.
Neither these checks nor the current native checkpoint establishes kernel execution.

## Declare the APSS SIMD reset policy

The common APSS model selects `CPACR_EL1.FPEN=1` at architectural reset.
This value permits EL1 SIMD and retains the EL0 trap.
Guest writes control all later access.
The model does not change instruction decoding, exception priority, or authentication.

Arm defines the FPEN warm-reset value as UNKNOWN.
The reference is [CPACR_EL1](https://df.lth.se/~getz/ARM/SysReg/AArch64-cpacr_el1.html).
QEMU's previous zero was an emulator choice, not a measured Oryon reset value.
The selected value is an explicit virtual policy for both SoCs.

SM8850 run170 and SM8845 run135 trap on their first selected EL1 SIMD helper.
The instructions are at original addresses `0x157bf044` and `0x157b6824`.
Both exceptions remain at EL1 with syndrome `0x1fe00000`.
A read-only capture in SM8850 run171 records `CPACR_EL1=0` before the secure EL1 transition and at `0x14694100`.
The capture also records `CPTR_EL2=0`, `CPTR_EL3=0x1100`, and `SVCR=0`.
The capture changes no guest register, memory, or execution address.

The selected EL1 handler permits lazy SIMD access from EL0, not EL1.
The selected context code restores FPEN to one.
These consumers support EL1 permission with an independent EL0 trap.
The model does not disable SME or intercept the fault to grant access.

Patch `0012-configure-fp-reset.patch` adds the native `reset-fpen` property.
The property defaults to zero and accepts the two-bit FPEN range.
The existing reset callback applies it through `cpacr_write()`.
QBox has no external API for this architectural reset policy, so this change must remain inside libqemu.
The active patch step tracks the patch files and the package lock.

The native APSS fixture fails with the previous CPU implementation.
The updated implementation executes EL1 `DUP` and verifies both vector lanes.
Guest FPEN zero restores the EL1 trap with the expected syndrome and instruction address.
FPEN one retains the EL0 trap, and guest FPEN three permits EL0 execution.
The fixture also checks the SVC return address after the instruction.
The corrected fixture and Clippy pass.

## Align GIC discovery with redistributor placement

The grouped APSS controller selects QEMU's native GICv4 revision without LPIs.
The standalone single-CPU helper retains its existing GICv3 revision.
The change does not alter interrupt grouping, routing, priority, security, or acknowledgment.

QSEE `0x15752154..0x15752164` reads `GICD_PIDR2` and tests mask `0xc0`.
A clear result selects a `0x20000` stride.
A nonzero result selects a `0x40000` stride.
The previous GICv3 identity contradicted the platform's `0x40000` placement.
The device tree independently supplies that placement.

The existing native fixture now derives its stride through the same discovery operation.
Before the correction, it fails at the unmapped second frame address `0x170a0008`.
After the correction, it verifies all eight affinities and the final-only `Last` bit.
It also preserves the existing cross-CPU executable-memory check.
All 100 tests and Clippy pass.

Native frames remain `0x20000` bytes because QEMU derives their size from `GICR_TYPER.VLPIS`, which remains clear.
The wrapper therefore preserves its native offsets and maps each frame at the declared physical stride.
This profile does not claim physical or virtual LPI support.
No native patch or fabricated register read is required.


## Preserve protection during TME retained sleep

The original TME saves integer registers and five CSRs before its collapse WFI.
Its restore routine does not restore PMP or security state.
A normal QEMU reset unlocks PMP entries and can clear security state.
Therefore a reset pulse does not implement this firmware contract.

Patch `0013-implement-tme-retained-power.patch` supplies an opt-in retained-power lifecycle inside libqemu.
The explicit virtual bank-seven request sequence arms one transition.
Actual WFI consumes the arm, and actual eligible interrupt state permits wake.
The CPU then enters the guest-published vector with machine interrupt delivery disabled.
Original firmware restores its saved state and decides when to permit delivery.
The transition neither acknowledges pending sources nor changes protection or privilege.

QBox exposes no external operation for retained WFI entry and pre-dispatch resume.
Thus this behavior must remain inside the RISC-V execution implementation.
The SystemC controller uses existing safe property setters and does not modify QBox.
The package lock tracks the patch through the active patch and configure steps.

This policy models a retained CPU transition, not measured silicon power sequencing.
External wake inputs remain inactive because the platform has no connected producers for that separate bank.
The original code still checks actual CLIC pending and enable state independently.

## Archive interactive MMIO repair

The interactive MMIO experiment passed scalar retry probes on AArch64, RV32, and RV64.
Its synthetic suite passed 48 host tests and 75 native tests.
The OEM probe required additional debugger coordination and did not establish a faster discovery workflow.
Archival applies when reconciliation costs exceed the saved boot time.

The source snapshot is `build/private/debug-access-archived-20261025.tar.gz`.
The archive passed extraction and content comparison before removal from the active build.
OEM probe logs remain under `build/private/run-sm8850-debug-access1`.
Normal execution retains the existing fail-on-unmodeled-access policy.

## Apply the TZ core start request

The selected TZ writes one 64-bit pair for each APSS core at `0x19050000 + core*0x100000 + lane*0x1000000`.
The pair holds a payload at `+0x8` and a command word at `+0x0`.
The CPU domain now treats command zero or one as a start request for the addressed core.
An explicit payload replaces the published warm vector before reset release.
Payload `-1` keeps the warm vector that CPUCP published at descriptor `+0x328`.

The domain also publishes the PDP boot status block at `0x81f5a000 + lane*0x40000`.
PDP firmware `0x81321b80` defines the header `0xabcd9876` at `+0x30` and version two at `+0x34`.
TZ `0x1573e644..0x1573e69c` waits for that header and for bit `16+core` of the first word.
The model writes the same bytes into the existing DDR owner through two `addrtr` aliases.
It reports a core after that core is attached, powered, and out of reset.

This contract is functional inference, not a silicon register claim.
The block identity at `0x19050000` remains open.
See `models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md`.

## Retain the cluster bring-up words

TZ `0x1573ed44` sets bit zero at `0x1887004c` during cluster bring-up.
It clears bit two at `0x17952000` after the domain startup words.
The model retains both words with writable masks one and four, from reset zero.
Neither word creates completion, power, or reset.
Only the APSS view is mapped. The CPUCP path is unobserved.
See `models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md`.

## Compose the secure-camera protection bank

QSEE `0x15779c18` (SM8850) and `0x1576d008` (SM8845) write nine camera protection words.
Each write requires an equal readback.
`platforms/qcom/sm88xx/qsee-camera-protection.lua` lists the nine addresses.
Both runtimes compose them through `qsee-retained-controls.lua` with label `camera`.
Each word retains the guest value and creates no protection, interrupt, or completion.
Run408 stops at the first write, `0x09000064`, before this composition.
The SM8850 optional physical-lane words stay unmapped.
The camera initializer then sets bit zero at `0x01fc0800`.
That TCSR word retains bit zero only, and its other bits read zero as unmodeled.
A passive root read can capture those bits without a write.
See `docs/qcom/sm88xx/boot-dependencies.md`.

## Retain only the UFS ICE error-branch words

QSEE `0x1577b1d8` (SM8850) and `0x1576e730` (SM8845) run the ICE error branch.
The branch writes zero to `0x01d8e204`, reads `0x01d8e200`, and writes bits 31:30 to `0x01d8e208`.
It then registers INTID 289 and may read the ICE version at `0x01d88008`.
The model retains these four words with fixed masks.
The two read words return virtual zero.
No ICE engine, key slot, HWKM register, status producer, or interrupt source is modeled.
Unmapped key registers fault, so key material stays unreadable.
Run410 stops at the first write, `0x01d8e204`.
See `docs/qcom/sm88xx/boot-dependencies.md`.

## Enable the GICv4 ITS and declare the 1-of-N capability

The grouped APSS controller now enables LPIs and binds one native `arm-gicv3-its` at `0x17040000`.
The Linux Kaanapali device tree declares that node with a `0x40000` register range.
The live phone tree reports the same node with size `0x20000`.
The model uses `0x20000`, which also equals the native QEMU frame size.
Run 434 read `GITS_TYPER` at `0x17040008` from QHEE address `0x45fd0` and stopped on the missing device.
QHEE `gicv3_its_init_one` requires `GITS_TYPER.PHYSICAL` and `.VIRTUAL`.
QEMU supplies both for a GICv4 parent.

`GICR_TYPER.VLPIS` now grows each native redistributor frame to `0x40000`.
The wrapper aliases each frame at that stride, and the platform slot size follows.
The previous `0x20000` alias made slot one read inside CPU 0's frame.
The platform suite detected that mismatch in `apss_sibling_executes_external_code_updates`.

QHEE `gicv3_handle_irq_init` reads `GICD_TYPER` and branches to a panic at `gicv3.c:830` when No1N is one.
QHEE instruction `0x45d5c` implements that test.
The GIC-700 TRM reports No1N zero and supports 1 of N SPI selection.
A passive EL1 read returned `GICD_TYPER = 0xf96b011f`, with bit 25 clear.
The later `GICD_IIDR = 0x47000070` read identifies Implementer `0x070`, not Arm `0x43b`.
[INFERENCE] These registers describe QHEE's virtual GIC, not the silicon distributor.
The No1N model choice rests on QHEE's assertion and the GIC-700 TRM, not a silicon measurement.
QEMU hardcodes one and implements no 1-of-N target selection.
A distributor read window therefore clears that bit and applies the declared ProductID override.
Other accesses reach the native device.
This is an explicit capability advertisement substitute.
`docs/qcom/sm88xx/stubs.md` and `models/qcom/apss/evidence.md` record it.

QHEE also asserts `GITS_TYPER.VMAPP` and `.VSGI`, and ITS initialization requires a nonzero `SVPET`.
QEMU reports none of those fields.
Run440 executed the capability check and then entered the QHEE panic loop at image offset `0xd505c`.
`qcom,apss-its-v1` now sets bits 39, 40, and 41 on reads of the `GITS_TYPER` dword and forwards every other access to the native device.
The first window in run441 dispatched the container region directly and returned decode errors.
The model now forwards through an address space over the native ITS region.
QEMU implements the GICv4.0 VMAPP layout and no direct virtual SGI injection, so the fields are a declared capability substitute.

QHEE then enables the ITS with `GITS_CTLR = 0x80000001` and polls bit 31 until it clears.
Arm defines that bit as read-only: zero while `Enabled` is one, and one while `Enabled` is zero and the ITS is idle.
QEMU sets Quiescent only in its reset callback, and its CTLR write changes `Enabled` alone.
The native register therefore keeps bit 31 set, and runs 443 through 448 stopped at image offset `0x4660c`.
`qcom,apss-its-v1` now ignores written bit 31 and derives the read value from `Enabled`.
A Ghidra decode places `gicv3_its_init_one` at `0x454e0..0x480eb`.
The next stage is the per-CPU redistributor initialization at `0x48100`, then resource manager activation with PSCI `CPU_ON` `0xc4000003`.

Run449 passed the enable wait and reached `0x46800`.
It then stopped during ITS initialization at `0x46c54` with the message `No ITS had a shared vPE table`.
That path requires a nonzero `GITS_TYPER.SVPET` at bits 42:41, which the model did not publish.
The window now sets SVPET to one.
SVPET one compares affinity level three only, which matches `GITS_MPIDR` zero and every APSS affinity here.
Run451 passed that path and reached `0x468a0`.

Run451 then stopped at the distributor identity check at `0x46c60`.
QHEE requires `GICD_IIDR` Implementer (bits 11:0) `0x43b` and ProductID (bits 31:24) two or four.
QEMU reports `0x43b` with a zero ProductID, which identifies a GIC-500.
The [TF-A GICv3 header](https://github.com/ARM-software/arm-trusted-firmware/blob/master/include/drivers/arm/gicv3.h) defines `GIC_PRODUCT_ID_GIC700` as four.
The model now reports ProductID four on `GICD_IIDR` and `GITS_IIDR`.
The live EL1 read `0x47000070` is a mediated view and does not establish the raw value.
The guest continued its EL1 loop, and the run made no unmodeled access.
Run452 crossed the identity check and stopped at an unmodeled four-byte write to `0x17600024`.
The PC was `0xffffff966e046df0`, QHEE image offset `0x46df0`, and the write value was `0xf`.
The Kaanapali DT identifies that register range as the APSS watchdog.

QEMU places `GITS_TRANSLATER` at ITS frame offset `0x10000` rather than `0x40`.
QSEE and QHEE send no MSI to the ITS in these runs.

Run443 passed the capability check and initialized the ITS.
It executed GITS_CTLR and GITS_BASER accesses through the native device.
The guest executed 16,305 translated blocks at EL1.
It produced no UART output after `SBL1, End`.
The run reached its 900-second timeout in a guest SMC and timer-interrupt loop.
No unmodeled access occurred.

The full platform suite passes 78 tests after these changes.

## MPAM hypervisor control and virtual list registers

Two libqemu patches extend the opt-in CPU profiles for SM8850 bring-up.
Both stay off by default and add no hardware beyond the named word or count.

`0019-implement-mpamhcr-el2.patch` adds `MPAMHCR_EL2` (`S3_4_C10_C4_0`) to the single-partition MPAM profile and reports `MPAMIDR_EL1.HAS_HCR` as one.
The genuine QHEE executes `msr MPAMHCR_EL2` at image offset `0x5293c`.
Silicon that lacked the register would raise an Undefined Instruction exception there, so the firmware establishes that the register exists.
The word is retained, and the profile still declares no controller, monitor, or partition.

`0021-implement-ete-programmer-words.patch` adds the ETE words QHEE uses: `TRCPRGCTLR` and `TRBLIMITR_EL1` retained, `TRCSTATR` derived, identification words constant. It produces no trace output and keeps `TraceBuffer` zero.

`0020-expose-virtual-list-register-count.patch` exposes the virtual GIC list-register count as `x-qcom-gic-num-lrs`.
QHEE writes `ICH_LR4_EL2` (`S3_4_C12_C12_4`) at image offset `0x…`, and every max TCG core otherwise defines four list registers.
The APSS profile requests eight, because QHEE switches over ICH_LR0..LR7 at image offsets 0x91fa4..0x92054 and never reads ICH_VTR_EL2.

`0027-expose-pmu-counter-count.patch` exposes the PMU event counter count as `x-qcom-pmu-num-counters`.
QHEE saves and restores `PMEVCNTR0..11_EL0` and `PMEVTYPER0..11_EL0` at image offsets `0x6a7c0..0x6a9cc` and never reads `PMCR_EL0.N`.
The max TCG core reports six counters, so `PMEVCNTR6_EL0` traps and QHEE loops at `0x6a96c`.
An external SystemC device cannot add CPU system registers, so this change needs libqemu.
The APSS profile requests twelve counters. The default of zero keeps the CPU model value.

## Preserve translations across split MMIO callbacks

Patch `0022-preserve-split-mmio-translations.patch` snapshots each translated page before a split MMIO access starts.
The snapshot retains the original transaction attributes, physical address, and region offset until all subaccesses finish.
QBox callbacks can invalidate IOMMU mappings and trigger a TLB resize.
The previous helpers retained pointers into the TLB allocation across these callbacks.
A resize freed that allocation before later subaccesses read its attributes.

The shared lookup boundary captures both pages before the first callback.
This also protects split loads and failure reporting.
Direct RAM accesses do not copy the translation records.
The change adds no device substitute and does not alter AccessMonitor policy.
The reported UEFI pair-store corruption matches this lifetime defect, but its exact runtime resize trigger remains unverified.

## PMIC SID1 BOB placement follows the phone type map

The live-phone record `20261026T180725Z` reads type `0x14` and subtype `0x6b` at bus 0, SID 1 peripherals `0x40..0x43`.
Type `0x14` identifies `PM_HW_MODULE_MISC`.
The same record reads type `0x29` and subtype `7` at peripherals `0xe4` and `0xe6`.
Subtype `7` identifies `PM_HW_MODULE_BOB_CONFIG_PERIPH_SUBTYPE`.
The model now places the two BOB controls at `0xe4` and `0xe6` and the MISC identities at `0x40..0x43`.

Native run526 failed for a different reason.
It served the MISC identity at `0x40` and left one BOB control at `0x42`.
XBL then returned error 32 at `pm_driver_init` line 164.
Error 32 is `PM_ERR_FLAG_INVALID_PERIPHERAL`.
The regulator initialization clears the OCP status of every allocated BOB slot.
The power driver rejects a slot with no discovered type.
The BOB slot count is two.
The MISC identity at `0x40` therefore removed the first slot, not a required control.

An earlier note claimed that run524 wrote `0x4046`.
The run524 model log holds no such write.
Its PMIC writes are `0x4146`, `0x4346`, `0x4d46`, and `0x4e46`.
Those bytes keep the exploratory service.

Run533 boots XBL to DXE with this placement and produces 16870 UART bytes.
The phone runs CPH2747, and the selected images come from the `.700` cohort.
This placement removes the conflict for the two ranges that the record covers.

## M-profile reset reads retain architectural attributes

Patch `0023-preserve-m-profile-reset-attributes.patch` changes the two vector reads in QEMU's `async_reset_msp_pc`.
The reads now carry the current M-profile security state instead of unspecified transaction attributes.
They remain privileged data reads.
The change does not grant a new security state or bypass AccessMonitor.

The isolated PDP smoke rejected the original reset reads at `0x20000000`.
With the patch, the strict monitor accepts the guest-loaded vectors.
The native log reports SP `0x2001f500` and PC `0x200018a5`.
The CPU remains in reset for that smoke.
Integrated execution requires a separate proof.

## Execute the selected PDP image

The SM8850 runtime uses two M7-compatible execution instances for the selected PDP firmware.
The CPUCP loader copies their private SRAM images through the existing ACEHR model.
The functional NCC sequencer uses the post-copy flag and the existing power gates to release each instance.
The exact silicon reset circuit remains an inference.
The model does not construct SCMI replies or CPU online records.

The firmware configures its address windows, SysTick, and mailbox banks itself.
The memory aliases preserve the existing protected DDR owner.
The SM8845 runtime retains its existing composition until its separate compatibility proof.
`models/qcom/sm88xx/pdp/evidence.md` records the selected instructions, addresses, and limits.

## Model infrastructure by observable effects

The platform needs the real firmware to reach the kernel.
It does not need a reconstruction of every internal hardware mechanism.
The t8030 approach guides this design, but it does not supply Qualcomm register evidence.

The model preserves register accesses, requests, effective clock and power states, completion, and errors.
A request completes only after its required modeled effects occur.
The model abstracts analog behavior, exact timing, and internal sequencer instructions.
An internal detail needs reconstruction only when it changes observable behavior or explains a relevant boot dependency.

The NCC controller therefore applies requests through shared clock and power owners.
The model retains uploaded CPRH programs without interpreting their instructions.
It does not replace PDP firmware or produce SCMI replies.


## Report APSS WFI to the CPU domain

The PDP ACD calibration waits until one powered core reports C-state 2 in `NCC_BOOT_STS1`.
The core enters that state when it waits in WFI.
The QBox CPU interface reports stop state, but it does not report the architectural halt.

The two existing observation patches therefore add one read accessor.
Patch `0002-export-cpu-observation.patch` exports `libqemu_cpu_get_halted`, which reads `CPUState::halted`.
Patch `0002-observe-cpu-and-invalidate-code.patch` adds `Cpu::get_halted` to libqemu-cxx.
The accessor does not change execution, interrupts, or wake rules.

The CPU domain reads the flag only for a powered core that is not in reset.
For that core, the flag means WFI.
`models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md` records the C-state encoding and its consumers.

## Return the UFS peer rate through libqemu

UEFI uses DME_PEER_GET for `TX_HSRATE_Series` after its first UFS transfer.
The UFS wrapper cannot supply a peer attribute without separating it from the QEMU link state.
Patch `0024-report-peer-ufs-hs-rate.patch` returns the retained `PA_HSSeries` through the QEMU UIC handler.
The handler still requires device presence and rejects unsupported peer attributes.

## Report RPMB readiness through libqemu

UFSDxe sends TEST UNIT READY to RPMB well-known LUN `0xc4` before its descriptor query.
The existing QEMU well-known LUN handler rejected it with INVALID_OPCODE in run677.
Patch `0025-report-ufs-rpmb-readiness.patch` uses the existing device and link checks for that LUN.
The patch does not implement an RPMB frame, key, or authentication response.

## Implement authenticated RPMB in libqemu

QSEE app loads require an RPMB capacity of `0x100..0x8000` sectors from UFSDxe.
The RPMB well-known LU belongs to the QEMU UFS device, so the frame protocol lives in libqemu.
Patch `0026-implement-ufs-rpmb.patch` implements JESD220 key programming, counter reads, authenticated writes and reads, and result reads.
It uses QEMU's HMAC-SHA256 and keeps the key, the counter, and the data in device state for one run.
The SM8850 board declares a 4 MiB region. This value is a virtual profile, because no device capture names the size.
Without a backing file, each process starts unprogrammed, as a factory-fresh device.
Secure write protection requests remain unsupported and return CHECK CONDITION.

## Persist RPMB mutations inside the native owner

Patch `0030-persist-ufs-rpmb.patch` adds optional durable state to the existing UFS owner.
The SystemC wrapper cannot observe accepted RPMB mutations or delay their responses until storage completes.
The native owner must commit key, counter, and media changes before it reports success.
This requirement places persistence inside libqemu rather than a separate storage proxy.

The backing format is virtual. It is not a physical RPMB dump format.
An atomic replacement keeps the committed counter and media together.
A stable adjacent lock excludes another writer during replacement.
Directory-flush failure remains a failed request with indeterminate crash durability.
The environment launcher copies the complete declared collection instead of claiming a live-machine snapshot.
The [RPMB procedure](../../../environments/qcom/rpmb/README.md) exercises that boundary through authored firmware.


## Add a bulk memory access path for device DMA

UFS DMA into emulated memory uses one blocking SystemC handoff for each beat.
QEMU splits each access to a non-RAM region into runs of eight bytes or less.
ABL therefore reads boot_a at about 1.2 MB/s and the OEM Phoenix watchdog resets the device.
A direct host pointer to the region is not an option, because the shared executable memory refuses it to non-owner initiators.

Patch `0028-add-bulk-memory-access.patch` adds optional byte-buffer callbacks to `MemoryRegionOps`.
A region that sets them receives one call with the complete run inside one memory section.
The flat-view read and write steps keep the BQL release and the access check on this path.
A region without the callbacks keeps the per-beat path.
The exported setter `mr_ops_set_bulk_cb` installs both callbacks with one opaque pointer.

Patch `0008-add-bulk-memory-access.patch` installs those callbacks on one QBox initiator socket, after `enable_bulk_access()`.
That callback builds one payload with the full length and then uses the existing access branches and locks.
It rejects a length above `UINT_MAX` instead of truncating the length.
Only `UfsController` opts in.
Every other initiator and region keeps the per-beat behavior.


## Implement AMUv1 activity monitors under the identification profile

The genuine SM8850 kernel runs `__cpu_setup` at image offsets
`0xc97736ec..0xc97736f8`. It reads `ID_AA64PFR0_EL1.AMU`, finds the
feature present, and executes `msr amuserenr_el0, xzr`.
The pinned CPU model defined no AMUv1 system register behind that
identification claim. The write took an Undefined Instruction exception
at EL1. UEFI's stale nonsecure vectors caught it, and the kernel parked
before any console output.

Patch `0029-implement-qcom-amu.patch` adds a minimum functional FEAT_AMUv1
implementation under the existing `x-qcom-ids` profile. It follows Arm
DDI 0601 for every register, access control, and trap. `AMUSERENR_EL0.EN`
gates EL0 access, with `HCR_EL2.TGE` choosing the trap target.
`CPTR_EL2.TAM` and `CPTR_EL3.TAM` trap EL1 and EL2 access. A write to any
other controlling register takes an Undefined Instruction exception
unless the write comes from the highest implemented EL. Real hardware
keeps counter enable under that same firmware rule.

`CPU_CYCLES` and `CNT_CYCLES` count from `QEMU_CLOCK_VIRTUAL`, the
timeline that already clocks the generic timer. `CPU_CYCLES` uses a
declared 1 GHz virtual core frequency, the same constant QEMU's PMU cycle
counter uses. `CNT_CYCLES` uses the generic timer counter value, so it
runs at `CNTFRQ_EL0` like `CNTVCT_EL0`. `INST_RETIRED` counts retired
instructions only when QEMU uses precise icount. Without precise icount it
has no source, and a clock value must not stand in for retired
instructions. `STALL_BACKEND_MEM` and the eight auxiliary counters also
have no source. Each one retains the guest's last write. `docs/qcom/sm88xx/stubs.md`
records these source limits.

`AMCFGR_EL0` and `AMCGCR_EL0` move out of the trace-only
`qcom_ids_reginfo` array into the new AMU array. Their reported words do
not change, so patch `0018`'s evidence for those constants still applies.
`tools/qcom/qcom-vp/tests/fixtures/apss/producer.S` extends
`apss_executes_mpam_pauth_and_rng` with AMUv1 coverage: the constant
words, counter enable and disable, a stopped counter's retention, the
highest-EL write rule, both TAM traps, and the `AMUSERENR_EL0.EN` gate.
The trap total moves from 38 to 44.

# CPUCP initial CPU-domain sequence

## Source

The input is a private `pbl8850.bin` copy.
Its SHA-256 is `91bd8f81efa7a5940d4b6d4c220388b559ceef39b6b47dd7b4b0727a1beb1d1c`.
Execution base is `0x300000`. CPUCP code uses RV64 with compressed instructions.
File offset equals execution address minus this base.
No firmware bytes belong in the repository.

The private `qcom-vp-cpucp` Ghidra project and Capstone RV64 decoding establish these facts:

- `0x3347a0` accepts lane IDs below two.
- It reads the word at `0x336394 + lane*0x78` and counts its set bits before it checks a core index.
- The two words are `0x3f` and `0x3`. They describe groups of six and two cores.
- It calls `0x3348ca`, then `0x334ba4`, then writes one to `0x1887c000 + lane*4`.

[INFERENCE] These groups are the two APSS CPU clusters.
These addresses come from the firmware lane descriptors and the ROM instruction sequence.
Neither the core masks nor the ROM instructions establish a complete architectural reset topology.

## Initial routine

`0x3348ca-0x334b90` performs this sequence:

1. Write zero to five words at `0x17960020..0x17960030`.
2. Read the descriptor at `0x336390 + lane*0x78`.
3. Set its lane tag in six words. Preserve all other fields.
4. Call clock operation 2 at `0x334970`.
5. Write zero to `0x1887c0a0 + lane*4`.
6. Call clock operation 0 at `0x3349c0`. Delay one unit through `0x33556a`.
7. Retain only bit 1 at `0x18879020 + lane*4`.
8. Call clock operation 3 at `0x334a08`.
9. Write zero to `0x188784a0 + lane*4`.
10. Retain only bits 1 and 3 at `0x18879000 + lane*4`.
11. Call clock operation 1 at `0x334a3a`.
12. Set bit 3 at `0x1887c020 + lane*4`. Delay, clear it, then delay again.
13. Set bit 0 in that word and poll bit 1 until it becomes one.
14. Clear bit 0, then call `0x334ed0(lane)`.
15. Program bits `[19:12]` of three lane-tag words from byte `0x3364a0`.

SM8850 runtime function `0x1d80885c` accesses `0x17951060 + lane*4`.
The selected cold path first executes this function in run336.
It preserves bit one, sets bit zero, and polls bit zero.
The virtual profile retains these two exact control words with writable mask three.
The source does not establish a separate status bit or a delayed completion.

The runtime later uses 64-bit accesses for route offsets `0x28`, `0x38`, and `0x40`.
Earlier boot code uses 32-bit accesses to the same fields.
The owner accepts both widths and retains the common low 20-bit value.
It rejects any nonzero upper bits.

SM8850 run338 passes the runtime gates, the per-core configuration, and these route accesses.
It next reads `0x198b0050`.
The focused CPU-domain test passes.
Kernel entry remains unverified.

The lane tags use these descriptor entries:

| Descriptor offset | Lane 0 base | Lane 1 base | Word offsets |
|---|---|---|---|
| `0x10` | `0x18000000` | `0x18200000` | `0x630`, `0x30`, `0x38`, `0x40` |
| `0x18` | `0x18040000` | `0x18240000` | `0x418`, `0x18` |
| `0x20` | `0x18060000` | `0x18260000` | `8` |

The low twelve bits hold the lane ID.
The later routine changes bits `[19:12]` at tag offset `0x18`, `0x30`, and `0x38`.
The initial byte at `0x3364a0` is `0x14`.

## Clock operations

The dispatcher at `0x3360e0` selects the table at `0x336ef8`:

| Operation | Function | Effect |
|---|---|---|
| 2 | `0x3359e4` | Enable the common branch and two lane branches |
| 0 | `0x335a2c` | Set lane request at `0x17c020b4` bit 0 or 1 |
| 3 | `0x335aac` | Wait for `0x17c020a4` bit 1 or 3 to clear |
| 1 | `0x335a66` | Clear the request and wait for status bit 0 or 2 to clear |

The common branch is `0x17c0402c`.
Lane 0 branches are `0x17c04030` and `0x17c04038`.
Lane 1 branches are `0x17c04034` and `0x17c0403c`.
These are the existing five branch model's indices 0, 1/3, and 2/4.

[INFERENCE] The associated programming clock source is the existing root at `0x17c02004`.
The address grouping and the request/status bank support this association.
The ROM alone does not provide a netlist.
Platform data selects the root through `root_path`.

## Downstream request

`0x334ed0` reads descriptor offset `0x20`.
It sets bit 2 at that base plus eight, polls bit 1, then clears bit 2.
The two physical words are `0x18060008` and `0x18260008`.
This is a second handshake, not a constant success bit.

XBL also accesses the same word through 64-bit loads and stores.
SM8850 routine `0x148becf4` uses descriptor `0x14927118`.
SM8845 routine `0x148bd1e0` uses descriptor `0x14921a20`.
Both descriptors contain `{0x18060008,2,1,0}`.
The fields identify the address, request bit, acknowledgement bit, and pulse bit.

Each routine clears request bit two, pulses bit zero, then sets request bit two.
The routine polls acknowledgement bit one before it clears the request.
Bit zero inhibits acknowledgement while asserted under the virtual policy.
The pulse does not reset the CPU or invalidate an initialized domain.
The existing supply, clock, isolation, and reset prerequisites still control completion.

The next outer operation is `0x334ba4`.
It first changes `0x198f0028/38/40` or `0x1a8f0028/38/40`.
It then programs 64-bit controls in descriptor offset `0x28`, including offsets `0x258`, `0x260`, and status `0x270`.
Later calls program descriptor offsets `0x30`, `0x38`, and `0x58`.
These are a separate core-start sequence after the initial routine.
The extension in `cpu_domain_core.h` implements their observed cold-start controls.
See `cpu_domain_core_evidence.md` for the functional state machine and additional mappings.
Neither model claims an APSS firmware load, authentication, or a Linux jump.

## Functional virtual policy

`cpu_domain_power.h` defines `QcomCpuDomainPower`.
The model contains no SoC selector or physical address.
The initial control sockets accept aligned, little-endian 32-bit accesses without byte enables.
The downstream sockets also accept aligned 64-bit accesses to the same word.
No socket grants DMI.
Reserved values and unknown accesses return TLM errors for the platform AccessMonitor.
The five initialization controls accept aligned 32-bit writes and retain their values.
Only a zero write to a word marks that ROM clear as complete.
A nonzero write is runtime configuration. It does not complete or acknowledge initialization.

## Runtime initialization group

Both shipping CPUCP images write the same five-word group at `0x17960020..0x17960030`.
SM8850 uses the group from `0x1d804394`. SM8845 uses the group from `0x1d8044b8`.
The recovered order is:

1. Write zero to `+0x20`, `+0x24`, and `+0x28`.
2. Read `+0x30` and clear bits `[17:0]` with mask `0xfffc0000`.
3. Set bit 1 in `+0x20`.
4. Set bits 1 and 23 in `+0x20` again with `0x06800000`.

SM8850 `0x1d808766` and SM8845 `0x1d808b18` clear all five words.
These clears run before the group above.

Two consumers read the same bank.
SM8850 `0x1d80f3b4` and SM8845 `0x1d80fa8c` read `+0x20` and then set bit 11.
SM8850 `0x1d80f4b4` and SM8845 `0x1d80fbac` read `+0x3c`, which is outside the modeled aperture.
The `+0x500..+0x510` reads at `0x1d80f43a` and `0x1d80fb32` use a different bank.

The model retains all five values. The reads at `+0x20` return the stored configuration.
The ROM clear tracking still requires five zero writes.
The runtime group therefore cannot complete an uninitialized domain, and it never reports a completion bit.

The following interpretations describe explicit virtual policy. They do not establish vendor register names:

- The first zero control releases the supply condition.
- The two masked control words represent isolation conditions.
- The second zero control releases the reset gate.
- Bit 3 in the final control asserts domain reset.
- Bit 0 requests initialization. Bit 1 acknowledges functional completion.
- Clock status bits 0/2 indicate an active requested clock. Bits 1/3 indicate a request with no available source.

Clock availability requires the root's actual `output_hz()` and all three relevant branches.
An unavailable source cannot acknowledge a clock request.
A cleared request produces neither active nor pending status.
The request does not create frequency or force a PLL lock.

Clock dependencies use change events, not a periodic polling loop.
`QcomGccPll::state_changed` reports register, vote, and cold-reset changes.
Its `lock_changed(index)` events report each programmed acquisition deadline.
Stops cancel the relevant deadline. Reprogramming replaces it.
The root worker receives these events and its own update and acquisition events.
It emits `QcomClockRoot::state_changed` after it refreshes functional frequency state.
Branch writes and cold reset emit `QcomClockBranchEnables::state_changed`.
The domain worker waits for these root and branch events, or its own control-write event.
Source loss therefore affects a live domain without another guest access.

The domain initialization acknowledgement requires all five initialization writes and all six correct lane tags.
It also requires supply release, reset-gate release, no isolation, an available clock, and a completed real reset release.
A reset clears the downstream operation state. It preserves the upstream lane configuration.
The downstream bit-2 request completes only while these conditions remain true.
Its bit-1 acknowledgement clears when the request clears or a prerequisite disappears.

The model latches domain power when a prepared initialization request arrives.
The temporary programming-clock request can then clear without removing domain power.
After CPUCP takes supply control, isolation, reset-gate assertion, explicit reset, or source loss holds the domain in reset.
Reset callbacks execute only in an `SC_THREAD`.
For an attached architectural CPU, the model drives its actual active-high reset input.
It does not write a PC, load executable bytes, or simulate a successful boot.
An accepted write to power-bank `+0x328` publishes the actual reset vector.
The APSS wrapper retains the latest published vector while the CPU remains held.
It applies that vector through architectural reset before release.
Publication cannot reset a running CPU.

Reset assertion returns only after architectural reset completes and the native loop acknowledges a pause for the same generation.
Before an eligible release, the controller repeats assertion to prepare the pending vector while the CPU remains held.
The controller then checks the vector generation and all reset gates again.
A changed generation requires another preparation.
An asserted gate prevents release.
The final release does not suspend.

Supply status `+0x270` acknowledges power loss only after all attached CPUs complete reset assertion and the current request generation completes.
The fixture polls that existing acknowledgement before it checks held counters.

The APSS wrapper preserves QBox's existing callback after its observation at the end of the native loop.
It publishes the reset generation from the native job only after `reset(true)` returns.
The loop acknowledges that generation only when native `can_run()` returns false under the BQL.
Pending stop processing or queued native work makes that predicate return true.
Architectural reset clears `halted`, and the topology rejects `start_powered_off`.
Thus, false establishes completed native pause for this reset generation.
The SystemC assertion waits for this event, not a timer or a polling interval.
The final release rejects an incomplete pause generation without waiting.

Suite `bg932` failed the unchanged supply-off hold check at stage 52.
The diagnostic trace in artifact `7165` later reached `0x600d0000`, but it did not reproduce that failure.
Several assertion callbacks returned while native `can_run()` still returned true.
The old completion event therefore proved architectural reset, not completed native pause.
[INFERENCE] This gap could let delivery acknowledgement precede native stop completion.
The generation barrier closes that gap for initial assertion, later assertion, and vector preparation.
The diagnostic pass does not establish that the barrier corrects the stage-52 failure.
Suite `bg936` passed all 98 tests, including the unchanged native fixture.
SM8850 run167 and SM8845 run132 both report `SBL1, End` after the barrier change.
Their next faults occur during TME IPCC initialization and QSEE EL3 mutex acquisition, respectively.

The production board contains eight APSS CPUs in one QEMU instance.
The attached masks are `lane0_attached_mask=0x3f` and `lane1_attached_mask=3`.
Each attached core has a `reset_lN_cM` output and an optional `vector_lN_cM` output.
The production vector outputs connect to the corresponding APSS `reset_vector` inputs.

The `vector_lN_cM_target` parameters name those inputs.
The controller binds these 64-bit signals directly because QBox's named binder supports only boolean and 32-bit signals.
Every attached core requires exactly one reset consumer.
Only the configured boot core can execute before the outer sequencer owns core reset.

The production board uses `initially_powered_mask=0`.
Both lanes start unpowered and isolated.
The APSS startup barrier holds every native CPU until the original CPUCP sequence releases it.
The isolated regression uses `initially_powered_mask=1` to test ownership transfer from an already-running boot CPU.
These values are board policy, not measured silicon reset values.

## Platform wiring

`src/platform.cc` registers `qcom_vp::QcomCpuDomainPower` as `qcom,cpu-domain-power-v1`.
`platforms/qcom/sm88xx/reset.lua` creates one shared instance for APSS and CPUCP.
The instance uses `cpucp_clock_root_3` and `cpucp_clock_branches`.
Its separate reset outputs control all eight real APSS CPUs.

The shared router subtracts each socket base.
The large control socket span rejects gaps.
Processor forwarding exposes only the known words.
The existing RCG and branch instances retain their mappings.

`boot_status_socket` is an optional memory initiator and stays disabled by default.
`platforms/qcom/sm88xx/pdp-scmi.lua` enables it and binds two `addrtr` aliases into the existing DDR owner.
The domain writes the PDP header and the per-core online bitmap through that socket.
The `APSS CPU boot status block` section of `cpu_domain_core_evidence.md` records the source.


## Verification boundary

The isolated CPUCP run passed `0x3348ca` and `0x334ed0`.
It emitted APSS reset start and finish messages twice at simulation times `4.907768–5.391481 ms`.
It then reached the outer routine at `0x334bd0`, which read `0x198f0028`.
The later integrated run `run-sm8850-functional-controls7` passed the outer initialization and core power acknowledgement.
That run stopped at the separate `FEATURE_CONFIG11` read.


The actual guest regression is `cpu_domain_acknowledgements_require_live_hardware_and_deliver_reset` in `tools/qcom/qcom-vp/tests/platform.rs`.
Its `cpu-domain` fixture connects reset to eight real APSS consumer CPUs.
It checks both lanes, missing clocks and branches, a bad lane tag, source loss, and explicit reset.
Shared heartbeat and boot counters distinguish reset hold from actual architectural restart.
The fixture also exercises the outer sequencer and combined per-core reset.
This regression passed during `make test` with the integrated outer extension.
That suite also exposed separate timer and UFS fixture failures.

### Native XBL gasket completion

SM8850 `run-sm8850-common-runtime19` and SM8845 `run-sm8845-common-runtime18` complete the original gasket polls.
Read-only debugger captures observe value six before the request clears.
The capture PCs are SM8850 `0x148bedc4` and SM8845 `0x148bd2b0`.
Both captures identify register `0x18060008`.
The captures did not change firmware instructions, registers, or authentication results.

The native regression now exercises both lanes with 32-bit and 64-bit accesses.
It checks pulse inhibition, request completion, and coherent readback across widths.
The real consumer retains its boot count and advances its heartbeat during both pulses.
Clippy and all 63 tests passed after this extension.
Both OEM boots next reached the separate SPMI initialization group.

### Reset requests during suspended callbacks

QBox reset callbacks can suspend the model thread.
A producer can change its control word during that interval.
A cleared reset bit must not discard an undelivered assertion.

Each lane now records request and delivery generations.
The model completes only the generation sampled before the reset callback.
It marks an assertion delivered only after the callback, or while that assertion remains active.
Completion requires valid sampled gates and valid current gates.
The outer sequencer and attached CPU selection remain unchanged.

The guest regression now issues consecutive control writes eight, zero, and one.
It requires an architectural restart, domain acknowledgement, and cleared downstream completion.
Failure addresses encode the stage as `0xbad00000 + stage * 4`.

An earlier integration failure stopped at `0x3006c0` in the shared hold check.
That output omitted the stage value. The temporary directory did not survive cleanup.
Its connection to the reset delivery race remains unproven.
The same limit applies to the earlier SM8845 stalls before UART initialization.

### Boot CPU reset ownership

SM8845 run 69 stopped after an APSS reset assertion, before UART initialization.
XBL instruction `0x148260fc` writes a scratch marker to `0x19880398`.
It next reads `0x19880290` at `0x14826118`.
The subsequent instructions set bits `0x101`, write resume address `0x14826000`, and request reset through `RMR_EL3`.

Previously, any write to this register bank transferred reset ownership to the outer core model.
The scratch write therefore asserted reset from cold power state before APSS completed its reset preparation.
Only writes to the power-control register at offset `0x260` now transfer that ownership.
Other configuration writes retain their values and update existing qualification state.
The CPUCP sequence still writes `0x260` before supply qualification and core release.

The guest regression writes the marker, reset configuration, and resume address before the first power-control write.
It requires the same boot count and continued execution.
The subsequent power-control write must still hold the real CPU in reset.

CPUCP took a separate error path in run 69.
Routine `0x3360b2` returned minus one, and `0x3343c0` rejected the result against signature `0xc5ab3000`.
The reporter received `./cpucp/pbl_mc.c` and line 178 before the ROM entered its terminal loop.
The terminal loop does not prove successful CPUCP initialization.
The reset ownership correction does not change this result or bypass the reported failure.


### Per-core reset regression

The native fixture attaches eight APSS consumers with independent boot and heartbeat counters.
It exercises all six lane-zero cores and both lane-one cores.
Policy writes at `+0x150` retain bits 50, 55, 56, and 57 without releasing reset.
The fixture checks core isolation, lane isolation, clock loss, supply loss, and short reset requests.
Its final access is `0x600d0000` from producer PC `0x300588`.
The standalone diagnostic reached that marker with exit status two.

The consumers execute both published warm entries, `0x334100` and `0x334200`.
The producer checks that the latest publication wins while reset remains asserted.
A publication during execution must preserve the boot count, cold-entry marker, and heartbeat progress.
The integration fixture passes after synchronous assertion and release preparation.
This synthetic execution does not establish an OEM PSCI request or secondary firmware startup.

### CPUCP runtime release words

SM8850 CPUCP instructions `0x1d80f3b4..0x1d80f3ce` configure two runtime release words.
They write one to `0x18874004` and `0x50` to `0x18874014`.
The same path sets bit 23 at `0x17960020`.
The existing notification model owns that third operation.

SM8850 CPUCP function `0x1d80f2f0` writes three words in the same block.
It writes `0x7ffff` to `0x18874010`, `0xfffff` to `0x18874014`, and three to `0x18874008`.
Instructions `0x1d80f2e4..0x1d80f2ea` write one to `0x18874004`.
The selected CPUCP image has no read of this block.
Function `0x1d80f2f0` then calls `0x1d81c60c` and `0x1d81c5d8` with IRQ `0x2510`.
These helpers set the priority of PLIC source 36 and enable it.

The virtual profile retains mask one at `0x18874004` and mask three at `0x18874008`.
It retains mask `0x7ffff` at `0x18874010` and mask `0xfffff` at `0x18874014`.
These words do not release a core, create an interrupt, or acknowledge a request.
The event that drives PLIC source 36 is not identified, so no model drives that source.

SM8850 run341 passes both writes and reaches `SBL1, End`.
The run loads APPSBL before the handoff.
Run611 stops at the first write of `0x1d80f2f0` to `0x18874010`.
Run613 passes the four words and stops at a later CPUCP write to `0x19871000`.
These runs do not prove UEFI entry or kernel execution.

### PDP status read at `0x18878304`

PDP function `0x813210a4` reads `0x18878304` and returns bit 1.
Its only caller is handler `0x81307bf4` through veneer `0x8130c860`, for request `0x12` with type 2.
Both values of the bit select a log message and complete the request with status zero and length four.
The selected CPUCP, PDP, and TZ images contain no write to this word.
The control bank reads zero at this offset and rejects writes.
Run641 stops at the first read from PC `0x813210a6`.

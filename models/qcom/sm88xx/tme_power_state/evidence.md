# Retained TME power dependencies

## Scope and sources

`platforms/qcom/sm88xx/tme-power-state.lua` reuses existing storage, timer, CLIC, and resource models.
The CPU extension implements an explicit virtual retained-power lifecycle. RAM and protection state remain intact.
Guest configuration alone does not produce completion, readiness, authorization, or an interrupt.

The sources are the unmodified `RADIO/tme_fw.img` members of these packages:

| SoC | Package | Image SHA-256 |
| --- | --- | --- |
| SM8850 | CPH2749export_11_16.0.5.700EX01_2026022701130186.zip | 4258fc24317e3467b39144ab72a46d85e4494c80bddc37b14de16177ff0c4a19 |
| SM8845 | CPH2771export_11_16.0.1.305EX01_2025121000260242.zip | 6a781e1eb215fbcd2d157bebb7b25502f6ae74ed9bce7810dc61f98545c10fb4 |

The analysis checked both image hashes. ELF `0x204xxxxx` addresses alias local TME `0x4xxxxx` addresses.
The tables below use local addresses. Static RV32 disassembly supplies these contracts, not measurements from silicon.
This document contains no firmware bytes.

## Producers, consumers, polls, and interrupts

| Operation | SM8850 evidence | SM8845 evidence | Contract |
| --- | --- | --- | --- |
| Scheduler gate | `0x427b42..0x427b54` | `0x427a7e..0x427a90` | Enter critical section, then call legacy slot `0x968`. Only zero/nonzero selects the branch. |
| Gate false | `0x427d12..0x427d1a` | `0x427c64..0x427c6c` | Restore the critical section without entering the sleep group. |
| Queue gate | `0x427b74..0x427b8c` | `0x427ac0..0x427ad8` | Require a queue object, a zero first query, and a nonzero second query. Do not replace queue results. |
| Queue unavailable | `0x427d00..0x427d08` | `0x427c52..0x427c5a` | Restore the critical section and execute WFI. |
| Configuration save | `0x427b94..0x427bac` | `0x427ae0..0x427af8` | Read four addresses from records at `0x446a14`. Store each value in its record. |
| Configuration restore | `0x427c34..0x427c4a` | `0x427b7e..0x427b94` | Write the saved values to the same four addresses. |
| RSC prepare | `0x4285ac..0x42864e` | `0x4284fc..0x42859e` | Check TCS1 idle, mask and clear its interrupt, stage two sets, and clear AMC mode. |
| Bank-seven request | `0x427bd2` | `0x427b1e` | Write `0x21` to `0x02237144`. No direct status poll follows this write. |
| Bank-seven control | `0x427bde..0x427be8` | `0x427b28..0x427b32` | Read `0x02237000`, preserve bits 31:8, and replace bits 7:0 with `0x23`. |
| Bank-seven control clear | `0x427f64..0x427f70` | `0x427eb6..0x427ec2` | Read the same control word and clear bits 7:0. |
| Entry diagnostics | `0x427bfc`, `0x427c0e` | `0x427b46`, `0x427b58` | Write entry tag to `0x00c04ff0` and counter to `0x00c04ff4`. |
| Timer preparation | `0x427d30..0x427ddc` | `0x427c82..0x427d2e` | Query legacy slot `0x1e4`. On success and output bit one, arm timer frame one for frame-zero time plus 100 microseconds. |
| Context save and WFI | `0x427f90..0x42804a` | `0x427ee0..0x427f9a` | Save integer registers and five CSRs, enable machine interrupts, and execute WFI. The next instruction is EBREAK. |
| PC-exit restore | `0x428050..0x428100` | `0x427fa0..0x428050` | Restore saved integer registers and five CSRs, then return. A vector write alone must not execute this routine. |
| RSC restore | `0x4286f8..0x42871e` | `0x428648..0x42866e` | Preserve RSC control bit 24, set bit 16, and write three to outer word `0x00c00214`. |
| Exit diagnostics | `0x427cc2`, `0x427cd4` | `0x427c14`, `0x427c26` | Write exit tag to `0x00c04ff0` and counter to `0x00c04ff8`. |
| Interrupt registration | `0x427e04..0x427e24` | `0x427d56..0x427d76` | Register handlers for IDs 29 and 22. |
| Interrupt 22 | `0x427ea4..0x427ec4` | `0x427df6..0x427e16` | Increment diagnostic `0x00c04fec`, then enter timer preparation. |
| Interrupt 29 | `0x427e78..0x427ea2` | `0x427dca..0x427df4` | Increment diagnostic `0x00c04fe8`, then write `0x40` to `0x0f028018`. |
| RSC interrupt 24 | `0x428538..0x4285aa` | `0x428488..0x4284fa` | Clear TCS1 pending bit at `0x02210d08`, then use the original scheduler callbacks. |

The save records name `0x00811000`, `0x0081101c`, `0x008110d0`, and `0x008110e8` in both images.
Existing runtime memory owns the first and third words. The helper preserves those objects and adds only the missing words.
A conflicting mapping causes a composition error instead of creating a second owner.

The milestone helper at `0x427d1e` / `0x427c70` ORs stage bits into RAM `0x447c88`.
It does not poll hardware or acknowledge interrupts.
The diagnostics already belong to `TmeLowPowerControl`. This helper does not replace that bank.

### RSC staging and polls

The prepare path requires bit zero at `0x02210fc8`. A false value takes the original fatal path, not a retry loop.
The existing synchronous RSC supplies idle because it has no outstanding batch.
The path writes zero to `0x02210d00` and two to `0x02210d08`.
It writes one to retained outer word `0x00c00214`.
It clears command enable at `0x02210fcc`, then stages command sets at relative bases `0xd2c` and `0xfcc`.
Each set uses stride `0x2a0`, command stride `0x18`, and at most five commands.
The staging routine is `0x428650..0x4286f6` / `0x4285a0..0x428646`.
It writes address, data, message controls, and a command-enable mask. It does not trigger a batch.
The final control write preserves only bit 24 of `0x02210fc4`.
The restore routine adds bit 16 without inventing an interrupt or a completion.

`aop-resource-service.lua` already owns the full RSC window and its resource effects.
Its completion output reaches CLIC source 24. Its existing command validation remains active.
The power helper does not execute staged commands or discard mailbox work.
Triggered sleep/wake execution remains outside the existing RSC contract.

### Timer and wake policy

OEM helpers `0x422b22` and `0x422b28` return timer frame bases `0x00c22000` and `0x00c23000`.
Timer preparation reads frame-zero count with a high/low/high stability loop at `0x4228b8..0x4228ca`.
It converts 100 microseconds through `0x4228cc`, writes frame-one compare, and enables frame one.
The compare and enable helpers are `0x422a96` and `0x422ab8`.
SM8845 corresponding helpers are `0x422a4c` and `0x422a6e`.
The timer diagnostic uses `0x00c04ffc` at `0x427db8` / `0x427d0a`.

[INFERENCE] Frame-one expiry feeds source 22 because handler 22 re-arms that frame in both selected images.
The new composition makes this virtual route explicit with `tme_timers.irq_1 -> tme_clic.source_22`.
The existing timer computes expiry from its real counter and the guest compare value.
CLIC source masks, interrupt enables, priorities, and threshold still govern CPU delivery.
This route is not a measured silicon interconnect. The model adds no unconditional pending state.

## Retained substitute and limits

The helper uses `qcom,retained-boot-control-v1` for missing configuration words.
Each configuration word starts at virtual zero and retains all 32 guest-written bits.
`TmeLowPowerControl` owns the two direct bank-seven request words and their explicit virtual power effects.
Zero is a cold virtual policy, not a measured reset value.
RAM remains in its existing backing. The helper does not clear RAM or simulate power removal.

The two bank-seven controls are not a general bank-seven sequencer implementation.
OEM `0x432fa0` passes bank-seven base to an indirect driver through `0x42a224`.
That driver and its dynamic consumers are not resolved by the direct LPE accesses.
The helper maps only `0x02237000` and `0x02237144`, not the complete bank or any status aperture.
Do not infer ownership, command completion, or a successful response from these retained values.
Voltage configuration at `0x022370a8` and `0x022370ac` keeps its existing owner.

The following status-generating operations remain unresolved:

- Interrupt 29 needs an external producer and the real acknowledgment contract for `0x0f028018`.
  The helper neither asserts source 29 nor supplies a successful acknowledgment register.
- General bank-seven sequencer operations need the indirect driver's register and status contracts.
- Triggered RSC sleep/wake batches need a real lifecycle event and checked resource effects.

These limits remain visible to original firmware and the existing unsupported-access monitor.
Authentication, authorization, rollback, and mailbox ownership remain unchanged.

## External wake eligibility and ownership

SM8850 `0x4376ac..0x437762` corresponds to SM8845 `0x437928..0x4379de`.
Both inspect four status words at `0x00810010..0x0081001c` against four masks at `0x00810000..0x0081000c`.
Any masked external status rejects the sleep decision.
With no such status, firmware saves and clears all four masks.
The SM8850 save loop is `0x4376e2..0x4376f0`. SM8845 uses `0x43795e..0x43796c`.
The mask restore routines are `0x437764` and `0x4379e0`.

The model retains writes to the external masks. Policy `inactive-no-external-producers` supplies zero for read-only external status.
Zero declares absent external producers. It does not mirror CLIC state or claim a measured reset value.
The model supplies no arbitrary external event or successful acknowledgment.

Firmware separately checks eleven packed pending/enable words at `0x01800000` and `0x01800400`.
The loops are SM8850 `0x437716..0x437726` and SM8845 `0x437992..0x4379a2`.
These accesses must use the existing CLIC owner and preserve its byte-per-ID layout.
The CLIC transport accepts aligned four-byte accesses and preserves each interrupt's byte position.
Any enabled pending CLIC source still rejects sleep.
The rejection restores the saved external masks before returning false.

Firmware then saves outer words `0x00c00118` and `0x00c0011c`.
The read loops are `0x43774a..0x437754` and `0x4379c6..0x4379d0`.
The restore paths are `0x437782` and `0x4379fe`. They also restore CLIC enables and external masks.
The outer words already belong to `tme_clic.source_socket` at offsets `0x18` and `0x1c`.
Do not create a second backing for them.

## Retained CPU lifecycle

Patch `0013-implement-tme-retained-power.patch` adds an opt-in CPU policy, not a physical-collapse simulation.
`TmeClicCpu.retained_power=true` enables it.
The control model publishes the guest vector through `x-qcom-power-vector`.
It uses QBox property setters, which dispatch safely to the QEMU I/O thread.
The control model never directly changes a running CPU PC.

The request contract is explicit virtual policy:

1. A write of `0x21` to `0x02237144` starts one request and cancels any previous arm.
2. A following control write with low byte `0x23` arms one transition.
   A nonzero, instruction-aligned exit vector must already exist.
3. Another control value cancels the arm.
4. Actual WFI consumes the arm and snapshots the published exit vector.
5. Actual eligible pending interrupt state permits wake.
6. The CPU enters the captured vector with `mstatus.MIE` clear.
7. The original restore code restores its saved CSRs and controls subsequent IRQ delivery.

An ordinary WFI without a new arm keeps architectural behavior.
The patch neither clears pending sources nor generates them.
The patch does not reset the CPU, change privilege, unlock PMP, or clear security state.
It does not suppress EBREAK. The armed power transition enters the restore vector instead of the ordinary WFI continuation.
No host timer drives this transition.

The integer save bases are `0x448770` and `0x448750`.
The CSR save bases are `0x4487f0` and `0x4487d0`.
Both restore routines restore `mie`, `mtvec`, `mscratch`, `mstatus`, and `mepc`, then return through the saved `ra`.
PMP and security state are not in that save set. A normal QEMU reset would therefore have incorrect protection effects.

The observed request sequence and restore code bound this policy.
The model uses inferred virtual request bits and a retained-power transition, not measured silicon behavior.
The external source-29 and general sequencer limits above still apply.

## Integration and focused verification

After the existing runtime, AOP resource service, and cold-control composition, call:

```lua
dofile(platform_directory..'tme-power-state.lua')(platform)
```

Both runtime factories call this helper after the existing TME and AOP composition.
The low-power controller receives the existing TME CPU as its constructor argument.
It owns the request, control, external mask, and external status sockets.
The existing diagnostic, vector, timer, and CLIC owners remain unchanged.

The package lock includes the retained-power patch after the existing CLIC implementation.
The active patch step and configure step track every patch file.
The separate runtime map connects the scheduler query to the live donor scheduler.
It does not substitute a fixed nonzero result.

Run the focused native fixtures after `make build`:

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform tme_retained_power_requires_wfi_and_real_wake_and_preserves_protection -- --ignored --exact
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform timer_frames_share_clock_and_preserve_signed_deadlines -- --ignored --exact
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform clic_delivers_id64_and_preserves_masks_priority_and_level_sources -- --ignored --exact
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform rpmh_completes_active_batches_and_isolates_requesters -- --ignored --exact
```

The new native fixture checks one-shot arming, cancellation, masked-source wake exclusion, vector updates, pending IRQ delivery, and retained PMP locks.
It also checks RAM, `mscratch`, external mask retention, and independence of external status from CLIC state.
The fixture uses actual timer deadlines and CLIC delivery. It includes no host-driven wake or mocked CPU.
The fixtures do not prove the integrated OEM collapse path.
Replay both original OEM paths with retained CPU and MMIO logs.
Check the gate result, saved-word identity, bank-seven writes, actual timer pending transitions, and the first unresolved operation.
Retain an EBREAK or unsupported-access failure rather than changing it into a successful sleep cycle.

The current integration run passed 47 host tests and 57 of 58 native tests.
The new retained-power fixture initially disabled QBox's optional PMP implementation.
The fixture now enables PMP explicitly, so it can exercise real lock retention.
The focused rerun passed all three retained-power scenarios.
Clippy passed with warnings denied.

The packed CLIC regression failed before the model change at pending-bank address `0x0180001c`.
After the change, the native integration fixture passed.
Its packed reads preserve individual pending bytes.
Its packed enable writes preserve pending sources and the existing interrupt delivery order.

## Integrated OEM observation

SM8850 run184 and SM8845 run145 use the scheduler query and paired interrupt-control bindings.
Both traces reach the original WFI and restore-vector blocks.
The respective WFI addresses are `0x428044` and `0x427f94`.
The respective restore addresses are `0x428050` and `0x427fa0`.
Neither trace reports a breakpoint exception at the EBREAK instruction after WFI.
These observations extend the isolated fixture proof but do not establish kernel execution.

The first host unmodeled-access reports concern APSS addresses `0x174044` and `0x1dfa104`.
The SM8850 TME trace also contains a store fault at `0x437718`, targeting `0x449258`.
Its position relative to the APSS failure remains unqualified because the separate trace streams do not share event timestamps.
The SM8845 TME trace contains no non-ECALL synchronous exception before shutdown.
Do not infer a completed kernel handoff or an authenticated boot from these observations.

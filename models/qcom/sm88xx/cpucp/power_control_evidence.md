# CPUCP early power control

The platform implements the PS_HOLD level and an explicit virtual power boundary.
The pad and boot models below provide retained state and a functional CPU reset path.

## MPM_PS_HOLD

`mpm_ps_hold.h` provides the header-only `qcom_vp::MpmPsHold` model.
The platform registers it as `qcom,mpm-ps-hold-v1`.
The model needs C++17, SystemC, CCI, and the existing TLM utility headers.

The register identity and behavior are:

- `AOSS_BASE` is `0x0b000000`.
- The hold word is at `0x0c264000`, which is AOSS offset `0x01264000`.
  Its register mask is `1`, and bit 0 is the only field.
- The private layout names the word `MPM_PS_HOLD` and the field `PSHOLD_MPM`.
- No other consumer of the word appears in the selected images.

CPUCP ROM `0x3352d6-0x3352f8` calls `0x33539c` with argument zero.
On success, it reads `0x0c264000`, sets bit 0, and writes the word.
The instruction sequence is `lui a1, 0xc264` at `0x3352e8`, `lw` at `0x3352ec`, `ori a2, a2, 1` at `0x3352ee`, and `sw` at `0x3352f2`.
The ROM input is `pbl8850.bin`, CPUCP file offset `0x34000`, execution base `0x334000`.
For these PCs, file offset equals PC minus `0x300000`.

The model implements the physical hold level, not a general register bank:

- Local offset zero accepts aligned, little-endian, 32-bit reads and writes.
- Bit 0 reads the programmed hold level. Writes change the mandatory `ps_hold` output.
- Initiators share the same state and mandatory signal socket.
- An accepted write changes the output without an invented silicon propagation delay.
- `initial_hold` must explicitly equal zero or one. Neither value claims a measured reset state.
- Reserved-bit writes fail without changing state. Other offsets, widths, commands, and byte enables fail.
- The model does not grant DMI.

The PMIC determines reset versus shutdown.
This register model does not reset CPUs or change a CPU PC.
An unbound output fails elaboration.
The platform connects it to `qcom,virtual-pmic-power-v1`.

The virtual board starts powered.
After the guest asserts PS_HOLD, a later release reports `UNMODELED_POWER_TRANSITION` and stops simulation.
The consumer does not invent reset or shutdown semantics without PMIC configuration.
Both APSS and CPUCP access the same register instance.

An actual ARM guest proved the initial zero, assertion, shared-alias read, and release boundary.
It stopped before the guest's failure terminal.
The simulator returned status 2 with `RUN_FAILED reason=missing-kernel-handoff`.
This test proves the explicit unsupported transition, not a complete PMIC.
The class contains no physical address or SoC selector.

## Retained pad word at 0x0f1e0000

`cpucp_pad_control.h` provides `qcom_vp::QcomCpucpPadControl`.
ROM `0x33539c-0x3353a8` passes address `0x0f1e0000`, mask `0x1c0`, shift 6, and its argument to helper `0x335982`.
The caller at `0x3352da` supplies zero.
Helper `0x335988-0x335998` performs `old & ~mask | ((value << shift) & mask)`.
This precedes assertion of MPM_PS_HOLD.

The model retains the complete word.
It does not name the pad or assign electrical behavior.
Its required `reset_value` parameter specifies virtual initial state, not a measured reset value.
All initiators share the word.
CPU reset does not clear it.
The transport contract matches the scratch model: aligned, little-endian 32-bit accesses, no byte enables, and no DMI.

These address facts do not establish this word's function:

- A private TLMM layout gives `TLMM_BASE` `0x0f000000` and a register base `0x00100000` above it.
- That layout places its pad-drive word at `0xde000` in the register block, which is `0x0f1de000`, not `0x0f1e0000`.
- The same layout names the word at `0xe0000`.
- No firmware access establishes the function of either word.

Thus the model is not GPIO224, SDC2, or a measured PS_HOLD pad model.
The unknown electrical function does not prevent retained functional state.

## Boot publication and external reset

`cpucp_boot_control.h` provides `qcom_vp::QcomCpucpBootControl`.
ROM `0x33462c-0x33463a` writes `0x1d800000` to `0x17c40000` and `0x17c40004`.
ROM `0x33463c-0x334642` then sets bit 0 at `0x17c40060`.
The model retains both vectors and this selection word.
These stores do not reset the CPU.
The later CPUCP initializer also publishes handler pointers at `+0x20/+0x24` and `+0x40/+0x44`.
The model retains these two per-hart recovery-vector banks separately, with virtual-zero cold state.
They survive CPU reset and do not themselves request reset or inject NMI events.
The [subsystem evidence](subsystem_evidence.md#adjacent-per-hart-initialization) records their exact producers and the remaining bank-assignment uncertainty.

The supplied package identifies the later reset source.
Its manifest names `IMAGES/xbl_sc.elf`, SHA-256 `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
The following facts come from that image:

1. File offsets `0xb0097` and `0xb00aa` contain the prepare and reset log names.
2. Procedure records at file `0xc4880` and `0xc48a0` associate them with `0x148387a8` and `0x14838808`.
3. Both functions request service `0x34` through resolver `0x1482cac4`.
4. Prepare calls service slot zero. Reset calls service slot `+0x10`.
5. The registration record at file `0xc2940` names service `0x34` and table `0x148f0410`.
6. The reset slot points to `0x14841f94`, which branches to `0x14892abc`.
7. That function clears bits `[23:0]` at `0x1887761c` and bit 0 at `0x1887768c`.
8. It then sets bit 0 at `0x17c42000`, at PC `0x14892aec`.

The target is CPUCP, not an APSS core.
ROM vector `0x1d800000` matches the supplied CPUCP ELF entry.
Its first instructions read `mhartid`, select per-hart data, and save registers through that data.
The retail log `qcom-rehost:inputs/evidence/logfs/UefiLog1.txt sha256=978d57403389b751f1b0a26f558c50eade4161cfa85926ddf8ef52b0481b2b7e`, lines 265-274, independently records prepare, CPUCPFW load, then reset.
This establishes an ordinary boot lifecycle.
It does not establish reset polarity, propagation delay, self-clear timing, or electrical handshakes.

## Explicit virtual reset policy

The model uses these rules instead of invented silicon timing:

- `target_socket` exposes retained vector words at local `0/4` and selection bit 0 at `0x60`.
- `reset_socket` exposes the retained request bit at local zero.
- Each accepted write of one requests one real CPU reset.
  A zero write only changes retained state.
- A request requires selection bit 0 and a previously written, halfword-aligned vector for the connected hart.
- Requests and vector writes fail while a reset is pending.
- Without runtime configuration, `setup_socket` retains the observed words at local `0`, `0x1c`, and `0x8c`.
  These words implement no electrical or clock acknowledgement.
- Vectors, selection, request, and setup words start at virtual zero and survive CPU reset.
- Unknown offsets, widths, byte enables, and reserved control bits fail.
  No socket grants DMI.

An `SC_THREAD` sets the QEMU `resetvec` property under the instance lock.
It then asserts and deasserts the existing `QemuCpu::reset` signal.
QBox queues the safe CPU reset and waits for its assertion before release.
The QEMU RISC-V `resetvec` property explicitly permits changes after realization.
The architectural reset handler selects the PC from this property.
The model never writes the PC through a debugger.

Only one CPU consumes each controller.
`hart_index` selects vector zero or one for that CPU and defaults to zero.
The current single-hart board uses zero.
Retaining the second vector does not claim a second running hart.
Keep guest-loaded executable memory in `SharedExecutableMemory`.
The controller neither loads firmware nor bypasses authentication or executable publication.
`MpmPsHold` remains a separate shared power latch.

## CEASE instruction

ROM `0x3346ea` executes SiFive CEASE, encoding `0x30500073`.
`patches/libqemu/0004-implement-sifive-cease.patch` adds only this instruction.
The opt-in QOM property `x-sifive-cease` defaults to false.
The controller's CCI parameter `sifive_cease` also defaults to false.
When explicitly enabled, the controller sets that property before CPU realization.
The board must enable it for this ROM.

The instruction requires machine privilege and system emulation.
Without the property, the decoder rejects it.
CEASE sets a distinct halt latch and exits CPU execution.
Pending interrupts do not release that latch.
Architectural CPU reset clears it.
This is not WFI, a fake completion marker, or a broad illegal-instruction substitute.
The patch does not add migration support for the new latch.
The platform does not use CPU migration.

## Platform integration

`src/platform.cc` registers `qcom,cpucp-pad-control-v1` and `qcom,cpucp-boot-control-v1`.
The pad uses explicit initial value zero at `0x0f1e0000`.
The boot controller maps control at `0x17c40000`, reset at `0x17c42000`, and setup at `0x18877600`.
APSS and CPUCP access the same instance.
Its reset output connects to the real CPUCP CPU, with `hart_index=0` and `sifive_cease=true`.
The libqemu patch participates in the checked patch list and cache identity.

The actual guest regression is `cpucp_cease_requires_reset_and_uses_the_published_vector`.
It passed during `make test`.
The guest checks that interrupts and vector publication cannot release CEASE.
A separate architectural reset executes the published vector.
The integrated CPUCP ROM also passes these boot controls and reaches its later domain initialization.

## Extended reset layout

`QcomCpucpBootControlV2` selects `RegisterLayout::v2` in the shared implementation.
Register it as `qcom,cpucp-boot-control-v2`.
The existing two-argument `QcomCpucpBootControl` constructor still selects V1.
V1 retains its existing registers, defaults, access rules, and reset behavior.
It still rejects setup offsets `0x14` and `0x18`.
The C++ implementation has no SoC-name test.

The SM8845 source is `IMAGES/xbl_sc.elf`, SHA-256
`a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
Service `0x34` has table `0x148ee0a0`.
Reset wrapper `0x148404c8` reaches implementation `0x148912ec..0x14891390`.
Predicate `0x148912ac` reads `0x221c8cf8` and masks the low 16 bits.
These bytes establish the following guest sequence:

1. Clear bits `[23:0]` at `0x1887761c`.
2. Clear bit 0 at `0x1887768c`.
3. For predicate values below 3, set `0x17c42000.bit0` and return.
4. Otherwise, set bit 8 at `0x18877614`, then at `0x18877618`.
5. Set `0x17c42000.bit0` once.
6. Clear bit 8 at `0x18877618`, then at `0x18877614`.
7. Set bit 0 at `0x18877600`.

V2 retains the two added bit-8 fields at setup offsets `0x14` and `0x18`.
Both fields start at virtual zero and survive CPU reset.
Without runtime configuration, writes with other bits set fail without changing either field.
The existing setup words at offsets `0`, `0x1c`, and `0x8c` retain their complete values.
Bit-8 setup writes produce no clock, power, reset, or acknowledgement effect.
In particular, the final `0x18877600` write does not request another reset.
The guest supplies the sequence. The model does not execute a replacement callback.
The optional runtime configuration extension adds bit 12 to both layouts.
It uses the existing live clock and the bounded initializer configuration to qualify hardware status.
See `cpu_domain_core_evidence.md` for the distinct runtime contract and source uncertainty.
Both branches use the same real RV64 CPU and the existing reset thread.
Each accepted reset-request write of one still requests exactly one architectural reset.

### Revision input ownership and virtual policy

The predicate address already belongs to the shared `FuseController`.
Its `read_word(0xcf8)` returns the existing `patch_version` parameter.
The initial platform value is zero.
The TME owner maps `0x021c8000+0x2000`.
The APSS and CPUCP aliases map `0x221c8000+0x2000` through that owner.
The secondary TME view at `0x02308000` also targets the same controller.
Thus `0x021c8cf8`, `0x221c8cf8`, and `0x02308cf8` read the same value.
Do not map separate revision RAM or a second register at any of these addresses.
The TCSR identity at `0x01fc8000` is a different owner and is not this input.

The address ownership is established. The predicate's physical silicon-ID meaning is not.
The existing fuse evidence calls this word `PATCH_VERSION`.
APSS ROM `0x30d718` also consumes it.
Do not rename it to a physical silicon revision or give different values to its aliases.

`platforms/qcom/sm88xx/sm8845/cpucp-extended-reset.lua` exposes an explicit virtual revision policy:

- `below3`, the default, sets the shared `patch_version` input to zero.
- `atleast3` sets the same input to three.

These choices select the short and extended reset branches, respectively.
They are not measured fuse values or claims about a physical device.
The second choice also changes the ROM patch-version input.
Native proof must check that consumer before attributing an early boot difference to CPUCP.
The helper checks the existing owner and both APSS/CPUCP alias definitions.
It adds no memory, IRQ, CPU, reset connection, or executable publication.
It changes only the existing controller type and the shared input parameter.

### Integration

`src/platform.cc` registers V2 separately and preserves the existing V1 registration.
SM8845 composition selects `sm8845-cpucp-extended-reset.lua` with policy `below3`.
SM8850 retains V1.
The CPU binding, CEASE support, reset signal, executable memory, and aliases remain unchanged.
The existing socket mappings remain:

| Socket | Address | Size |
| --- | --- | --- |
| `target_socket` | `0x17c40000` | `0x64` |
| `reset_socket` | `0x17c42000` | `4` |
| `setup_socket` | `0x18877600` | `0x90` |


### Verification boundary

The existing native reset regression now exercises V1 and V2.
The V2 guest checks zero initial pulse fields, retained writes, and independent clearing.
It publishes both vectors and issues one reset request.
The actual RV64 consumer leaves CEASE and reaches the published vector.
The final setup write preserves the consumer's state during a bounded observation window.
A duplicate reset would make that unchanged consumer report failure.

These checks prove the model sequence, not execution of the selected OEM reset callback.
Native execution of the original CPUCP ELF and the extended OEM branch remain unobserved.
The `atleast3` policy also requires proof against the ROM patch-version consumers.
No debugger PC write, instruction substitute, firmware-completion marker, or electrical ACK is authorized.

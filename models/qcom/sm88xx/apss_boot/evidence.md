# APSS boot timer and identity evidence

## Model and composition

`system_timer.h` implements `qcom_vp::SystemTimer` under `qcom,sm8850-qtimer-v1`.
One instance owns the shared counter, seven physical timer states, per-frame virtual offsets, and frame 0's optional EL0 view.
APSS and CPUCP use the same instance through the platform's timer router.

| Socket | Physical address | Function |
| --- | --- | --- |
| `control_socket` | `0x0c210000` | Enable or stop the shared counter. |
| `config_socket` | `0x17810000` | Set frequency metadata and frame access. |
| `frame0_socket` | `0x17811000` | Access the first physical timer. |
| `frame0_el0_socket` | `0x17812000` | Access the second view of physical frame 0. |
| `frame1_socket` | `0x17813000` | Access the second physical timer. |
| `frame2_socket` | `0x17815000` | Access physical frame two. |
| `frame3_socket` | `0x17817000` | Access the first selected QSEE timer. |
| `frame4_socket` | `0x17819000` | Access the second selected QSEE timer. |
| `frame5_socket`, `frame6_socket` | `0x1781b000`, `0x1781d000` | Access the remaining physical frames. |

CPUCP PCs `0x33552a` through `0x335566` initialize these registers.
The ROM writes frequency 19200000 and frame access masks `0x3f`.
It sets IMASK in both timer controls.
The platform starts the counter enabled because APSS reads it before CPUCP completes initialization.
This reset state is virtual board policy, not a measured silicon state.

### Frame 0 EL0 view

The [Canoe device tree](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/kernel_platform/qcom/opensource/devicetree/qcom/canoe.dtsi#L906-L912)
lists `0x17811000` and `0x17812000` in the same frame-0 `reg` property.
The second address is CNTEL0Base0, not another timer or a PLL.

Static analysis used each selected package's original `IMAGES/xbl_sc.elf`.
The package paths and hashes remain in the prepared-run `inputs.json` manifests.
No firmware bytes or image changes form part of this model.

| Contract | SM8850 XBL | SM8845 XBL |
| --- | --- | --- |
| Descriptor: configuration base, physical base, frame index, second-view base | `0x148e1bb0`, `0x148e1bb4`, `0x148e1bb8`, `0x148e1bc0` | `0x148dfc70`, `0x148dfc74`, `0x148dfc78`, `0x148dfc80` |
| Initialize CNTACR0 to `0x3f` and cache physical frequency | `0x1485f724..0x1485f764` | `0x1485dc68..0x1485dca8` |
| Initialize CNTACR0 to `0x3f` and CNTEL0ACR to `0x303` | `0x1487b410..0x1487b454` | `0x14879b0c..0x14879b50` |
| Read second-view high, low, high until stable, then frequency | `0x14844e0c..0x14844e44` | `0x14843340..0x14843378` |

Both descriptor sets contain configuration `0x17810000`, physical frame `0x17811000`, index zero, and second view `0x17812000`.
SM8850 native run29 faults at the first high-word load, PC `0x14844e24`.
The new socket maps the missing view onto frame 0's existing state.
It creates no new counter, compare value, control word, or interrupt output.
SM8845 uses the same view through the shared reset platform.

[CNTEL0ACR](https://fooptrvoid.github.io/arm-mra-2024-12-sysreg/ext-cntel0acr.html)
at physical offset `0x14` adds access controls for the second view.
Its implemented mask is `0x303`.
Bit 0 exposes CNTPCT. Bit 9 exposes the physical comparison and control registers.
Either bit 0 or bit 1 exposes CNTFRQ.
Each permission also requires its corresponding CNTACR0 permission.
Disabled registers read zero and ignore writes.
CNTEL0ACR is not writable through the second view.
Other frames do not implement CNTEL0ACR.
Unimplemented virtual counter, offset, and timer registers read zero and ignore writes in both views.
Bits 1 and 8 do not create a virtual timer or interrupt.

The model preserves its existing security boundary.
CNTNSAR bit 0 gates Non-secure transactions to both frame-0 views, including CNTEL0ACR.
The model rejects denied Non-secure transactions with an address error.
This fail-closed transport policy differs from architectural RES0 behavior.
It neither grants Non-secure access nor changes an existing security permission.
The [CNTNSAR description](https://fooptrvoid.github.io/arm-mra-2024-12-sysreg/ext-cntnsar.html)
specifies the shared gate for CNTBaseN and CNTEL0BaseN.

The board explicitly sets `frame0_el0_access_reset=1`.
This permits only the early physical-counter and frequency consumer before firmware initializes CNTEL0ACR.
Arm defines these reset bits as UNKNOWN.
The value is virtual reset policy, not a measured reset state or a fabricated readiness value.
The model default remains zero.
Firmware can revoke this permission and later writes its own `0x303` value.

The existing `timer` fixture now checks the OEM counter loop against the stopped physical counter.
It also checks shared frequency, compare writes, signed TVAL, both access-control layers, and second-view CNTEL0ACR protection.
Its frame-0 interrupt test enables the timer through the second view and masks it through the physical view.
The existing physical frame-1 and frame-2 checks remain.
The fixture passes, including shared IRQ delivery through the second view.

SM8850 run31 and SM8845 run29 complete `boot_pre_ddr_dtb_load` and both thermal callbacks.
The original second-view counter consumer no longer faults.
These runs stop during subsequent PMIC initialization and do not prove kernel execution.

### TME local timer frames

The TME ROM accesses separate local frames at `0x00c22000` and `0x00c23000`.
Instructions `0x200752..0x200764` read each control word at offset `0x2c` and clear bit 1.
The existing timer model implements this IMASK field.
The platform uses a separate instance so these writes cannot change the APSS timer controls.

The local counter uses an explicit virtual 19.2 MHz source from reset.
Both frame access masks start at `0x3f`.
The platform does not expose unobserved counter-control or configuration addresses.
Those optional sockets use the single-target type that QBox's factory supports.
The frame sockets retain multiple-initiator support.

No TME timer interrupt route has been established.
The model rejects an enabled, unmasked timer when its interrupt output has no receiver.
It does not fabricate interrupt delivery.


## Boot identity contract

`boot_identity.h` supplies the ROM selector words and the later CPUCP topology words.
APSS and CPUCP share one instance.
The actual CPUCP execution passes both identity queries and reaches its next transport dependency.
The [Canoe device tree](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/kernel_platform/qcom/opensource/devicetree/qcom/canoe.dtsi) does not identify this word.

### Identity field consumers

The following evidence comes from static disassembly of the supplied `pbl8850.bin`.
The ROM base is `0x00300000`. No firmware bytes are included here.

| ROM address | Contract |
| --- | --- |
| `0x30cdc0`, `0x30ce0c`-`0x30ce24` | Dispatch a command through the halfword table at `0x31d178`. |
| `0x30d524`-`0x30d53c` | Command `0x6b` restores the caller registers and branches to `0x30d774`. |
| `0x30d774`, `0x30d778` | Reject a zero controller argument or null output pointer with errors 3 or 1. |
| `0x30d77c`-`0x30d78c` | Materialize `0x17651024` and read one 32-bit word. |
| `0x30d790`, `0x30d794` | Extract bits 23:16 and store the zero-extended result as a **32-bit word**, not a byte. |
| `0x30d780`, `0x30d788`, `0x30d798` | Return success `0x050cce55`, independent of the extracted value. |
| `0x302e24`-`0x302e34` | The sole direct command-`0x6b` caller stores the result at `0x148166b4`. |
| `0x302e38`-`0x302e4c` | Check helper success, then reject unsigned field values greater than 3. |
| `0x303078` | Return the caller's error. The range-error value is `0x00028200`, prepared at `0x302e10`/`0x302e18`. |

The cache belongs to the structure at `0x14816688`, at offset `0x2c`.
The initializer begins at `0x302ca8`. It publishes signature `0x1211d02e` at `0x303064` after the remaining queries succeed.
The getter path at `0x301ea0`-`0x301eac` exports this structure's address through the common return at `0x30206c`.
The field therefore remains visible to later consumers. The model must not replace the helper with a forced success return.
Static address tracing found no other direct read of `0x148166b4` in this PBL beyond the range check at `0x302e44`.
This does not establish the meaning of the selector in later firmware.

### Declared virtual identity policy

`qcom_vp::QcomApssBootIdentity` implements five read-only words.
The compatible key is `qcom,apss-boot-identity-v1`.
The board sets `profile_selector` in `[0, 3]` and `variant_selector` in `[0, 2]`.
Their invalid defaults prevent implicit identities.
The model freezes all four policy values at elaboration and logs them.

The board selects zero for both selectors.
These are the lowest selectors that the ROM accepts, not measured chip identities.
Each selector word contains its selector in bits 23:16.
All other bits in those two words are zero under virtual policy.

CPUCP reads `0x17651024` at PC `0x335812`.
Its command-`0x6b` caller caches the result at `0x18b33014` and rejects values above three.
CPUCP reads `0x17651018` at PC `0x335668`.
Its command-`0x6a` caller caches the result at `0x18b33018` and rejects values above two.

The socket accepts aligned, little-endian, 32-bit reads at relative offsets `0x00`, `0x0c`, `0x18`, `0x20`, and `0x24`.
It rejects writes, unknown offsets, null buffers, byte enables, partial transfers, and streaming widths below four.
It does not grant DMI.

The supplied `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/machine.py`, lines 154-158, uses explicit virtual policy for a different identity register.
It constructs TCSR word `0xa01b0200` at `0x01fc8000` from XBL encoding evidence and a declared virtual revision.
`qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/boot_inputs.py`, lines 268-271, distinguishes constructed SMEM identity from captured physical identifiers.
Those values do not establish the APSS selector at `0x17651024`. This model does not transplant them.

### Physical selector access failure

The Infiniti experiment is record [`20261026T210853Z-phone-pdp-cpucp-contracts`](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261026T210853Z-phone-pdp-cpucp-contracts/record.md).
The Android exports report SM8850 revision 2.0 and two functional clusters.
Gobbl then reports EL1, TTBR0 `0xc6e89000`, and current CPU virtual addresses.
Three table reads establish a stage-one Device block containing `0x17651018`.
The block maps that address identically under the recorded table-page assumption.

One 32-bit read at `0x17651018` loses USB and returns no value.
The automatic catcher collects `20261026T211217Z-900e-2`, and the complete SHA-256 check passes.
The retained Surfacer log contains the exact read command.
The phone returns to Android afterward.
The observation establishes no selector value and changes no virtual identity.
Do not repeat this access from the same context without new evidence.

### CPUCP runtime topology words

SM8850 run263 and SM8845 run212 pass the PTE fuse reads.
They stop at R32 `0x1765100c`, from PCs `0x812d425e` and `0x8124420a`.
Static analysis of both selected `cpucp.elf` files establishes the following contracts.
These labels describe the consumers, not recovered vendor register names.

| Address | SM8850 read PC | SM8845 read PC | Selected consumer |
|---|---|---|---|
| `0x1765100c` | `0x812d425e` | `0x8124420a` | Active-high CPU presence bitmap |
| `0x17651020` | `0x812d4386` | `0x81244332` | Active-high disabled LLC group bitmap |

The CPU-present initializer retains bits 23:0.
Bits 7:0 describe cluster zero, bits 15:8 describe cluster one, and bits 23:16 describe the next cluster byte.
The later two-cluster loop selects one byte with a shift of `8 * cluster`.
It intersects that byte with the cluster descriptor and counts the surviving CPU bits.
The guest also retains the `cfg_cpu_present` property restriction when its existing policy permits that override.
The model does not precompute or write the resulting guest structures.

Both images contain the same cluster descriptors at `0x1d82d8f0` and `0x1d82d930`.
Their CPU masks at offset one are `0x3f` and `0x03`.
Their LLC way masks at offset eight are both `0x0fff`.
The existing shared reset composition instantiates six APSS CPUs in lane zero and two in lane one.
The SM8845 helpers retain that CPU list.
Thus `cpu_present_mask=0x033f` declares exactly those eight virtual CPUs.
It does not turn the absent third cluster byte into more CPUs.

The LLC decoder retains the low 16 bits, inverts them, and selects four bits with a shift of `4 * cluster`.
Each surviving bit enables four contiguous ways:

| Bit within cluster nibble | Disabled ways when the register bit is one | Expanded enabled mask when that is the sole surviving bit |
|---|---|---|
| 0 | 3:0 | `0x000f` |
| 1 | 7:4 | `0x00f0` |
| 2 | 11:8 | `0x0f00` |
| 3 | 15:12 | `0xf000` |

The selected guest can further restrict these groups through `llc_way_mask`.
It then applies its own SoC rule and the descriptor mask.
For hardware prefix `0xa0220000`, cluster one is limited to `0x00ff`.
The relevant branches are `0x812d4444..0x812d4456` and `0x812443f0..0x81244402`.
The SM8845 runtime declares TCSR identity `0xa0220100`, which selects that real guest branch.
The SM8850 profile retains `0xa01b0200`.

The board declares `llc_group_disable_mask=0`.
This means no hardware-disabled LLC groups, not a missing cache.
Without an additional guest property restriction, the resulting SM8850 way masks are `0x0fff/0x0fff`.
For SM8845 they are `0x0fff/0x00ff`.
The identity owner does not implement this SoC rule or manufacture cache state.
The real guest still computes these masks.

Both new parameters are required.
CPU presence is limited to 24 bits, and the disabled-group mask is limited to 16 bits.
The model returns zero in the remaining bits as explicit virtual policy, not as a recovered silicon reset value.
The existing profile and variant words at offsets `0x24` and `0x18` retain their exact encodings.
No writes, status transitions, or additional offsets become valid.

The selected CPUCP images read the same block at `0x17651000`.
Their consumers read `0x0c`, `0x18`, `0x20`, and `0x24`.
The selected ROM selector reads bits `23:16` of the word at `0x24`.
The source-only block name, the register names, and the `APSS_CSR_NSEC + 0x1000` composition remain unestablished.
This review found no independent record of that base expression.
The SM8850 block is at `0x17651000`, not at an address computed from that expression.
These names do not establish SM8850 reset values.

CPUCP `0x1d802ab6` reads `APSS_HW_VERSION` and logs it as `CPUSS HW Version`.
It stores the word at `0x1d82d8d0`. No other recovered CPUCP instruction refers to that structure.
The required `hardware_version` parameter supplies the word.
The board uses `0x10010000`, with major one, minor one, and step zero.
This value is not an SM8850 measurement. Run619 stops at the first read of this word.

The native subsystem fixture reads all four words through both APSS and CPUCP views of one owner.
It uses selectors 2/1, CPU mask `0x033f`, and nonzero disabled-group mask `0x0021`.
The latter disables cluster-zero group zero and cluster-one group one.
The CPUCP guest expands those nibbles to `0xfff0/0xff0f`, then applies descriptor masks to obtain `0x0ff0/0x0f0f`.
It also checks the SM8845 cluster-one cap, which reduces the second example to `0x000f`.
This checks byte order, disable polarity, nibble selection, quartet expansion, and unchanged selector positions.

Verification command:

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform cpucp_subsystem_routes_real_mailbox_interrupts_and_preserves_workspace -- --ignored --exact
```

The focused subsystem fixture passes with these identity values.
SM8850 run264 and SM8845 run213 pass topology discovery and reach the cluster configuration read at `0x198985d0`.
Their failing PCs are `0x812d44a6` and `0x81244452`, respectively.
The complete integration run passes 47 host tests and 71 platform tests. Clippy passes with warnings denied.

### TME consistency boundary

The comparison at `0x30d9cc`-`0x30d9e8` uses a **different** RAM word, `0x148137f0`.
It returns whether that unsigned word is at most `0x2223604c[23:16]`.
Callers `0x30dd54`, `0x30dd7c`, and `0x30ddec` use that comparison to select readiness paths.
This is not the cache at `0x148166b4`.
The supplied evidence does not establish an equality requirement between these two fields.
Do not copy this APSS selector into the TME field or fabricate a readiness transition.

The supplied RISC-V TME accessor at `0x211ecc` materializes `0x0223004c`.
That address is not `0x2223604c`. No established alias connects them.
The APSS identity model therefore neither maps that bank nor supplies a TME handoff value.
Future producer evidence must preserve the actual comparison and any established address translation.

### Integration

The platform registers the model with `gs::ModuleFactory::ModuleRegistrationWrapper`.
The APSS router maps its aperture at `0x17651000`, with size `0x28`.
The CPUCP router forwards the same aperture to the APSS router.
Both cores therefore read the same frozen profile.

## Register behavior

The model accepts aligned, little-endian, 32-bit accesses without byte enables.
Physical and virtual counter slots also accept aligned 64-bit reads.
All other access widths remain unchanged.
Unknown registers and unsupported transactions fail.
The model does not grant DMI.

| Frame offset | Register | Behavior |
| --- | --- | --- |
| `0x00`, `0x04` | CNTPCT | Read either 32-bit half, or one 64-bit sample at offset zero. |
| `0x08`, `0x0c` | CNTVCT | Read the counter minus this frame's offset. Offset eight also accepts one 64-bit sample. |
| `0x18`, `0x1c` | CNTVOFF | Read the low or high 32-bit word of this frame's virtual offset. |
| `0x10` | CNTFRQ | Read frequency metadata. |
| `0x14` | CNTEL0ACR | Control frame 0's second view from its physical view. Other locations read zero and ignore writes. |
| `0x20`, `0x24` | CNTP_CVAL | Read or write the low or high 32-bit word of the comparison value. |
| `0x28` | CNTP_TVAL | Write a signed interval relative to the current counter. |
| `0x2c` | CNTP_CTL | Control ENABLE and IMASK. Read the enabled timer condition through ISTAT. |

The counter advances with simulation time, not the number of reads.
The fixed `clock_hz` parameter determines its rate.
Writes to CNTFRQ change metadata, not that rate.
The control register stops and resumes one counter shared by all frames.

CNTACR at configuration offsets `0x40..0x58` controls the seven frames independently.
Disabled fields read zero and ignore writes.
CNTNSAR at offset `4` controls nonsecure frame access.
The model checks the preserved transaction security attribute.

The comparison uses signed 64-bit subtraction, including negative TVAL intervals near counter zero.
Each optional `irq` output asserts when ENABLE is one, IMASK is zero, and the timer condition is true.
An enabled, unmasked timer requires a connected interrupt route.
The board connects frame 0 to GIC SPI 8, architectural INTID 40.
It connects frame 1 to GIC SPI 9, architectural INTID 41.
The model does not implement virtual comparators or virtual timer interrupts.

### Interrupt route evidence

The [Canoe device tree](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/kernel_platform/qcom/opensource/devicetree/qcom/canoe.dtsi#L898-L919)
defines `timer@17810000` with frequency 19200000.
Its `frame@17811000` lists physical SPI 8 first and virtual SPI 6 second.
Its `frame@17813000` lists physical SPI 9.
Both physical interrupts use `IRQ_TYPE_LEVEL_HIGH`.
The disabled Linux status of frame 1 does not disconnect its hardware interrupt.
These IDs come from the device tree, not from a guessed virtual assignment.

The assigned timer interrupt `0x28` agrees with INTID 40.
The reference register addresses differ from the SM8850 map.
This agreement does not independently establish the SM8850 address map.

Direct A64 disassembly of the supplied PBL establishes both timer consumers:

| ROM address | Operation |
| --- | --- |
| `0x30ccb4`–`0x30ccbc` | Select frame 0 and write its signed TVAL interval. |
| `0x30ccc0`–`0x30ccc8` | Set ENABLE without changing IMASK. |
| `0x30cccc`–`0x30ccdc` | Poll ISTAT and clear ENABLE after expiry. |
| `0x30ccec`, `0x30ccf8` | Select frame 1 at `0x1781302c`. |
| `0x30cd10`–`0x30cd20` | Disable frame 1, write TVAL, and enable it again. |
| `0x30cd28`–`0x30cd4c` | Poll frame 1 ISTAT and disable an expired timer. |
| `0x30cd9c`–`0x30cdb8` | Clear IMASK in both frames. |

The delay routine polls ISTAT. It does not require an interrupt handler to finish.
The connected GIC still retains and delivers an enabled interrupt according to its guest configuration.
The platform does not force GIC enable, group, priority, or routing registers.

`irq` uses QBox `InitiatorSignalSocket<bool>`, which the container can bind to `QemuTargetSignalSocket`.
The old raw `sc_signal_out_if<bool>` port did not match the container's supported signal connection types.
The Lua names `irq_0` and `irq_1` follow SystemC vector element names.
QBox `spi_in_8` and `spi_in_9` take SPI indices, not architectural INTIDs.
The four existing CPU timer PPI connections remain separate.

Timer writes publish the interrupt level before the transaction returns.
They retain event scheduling for future counter deadlines.
A deferred delta-cycle update lets QEMU execute EOI and ERET before the timer lowers its interrupt.
The GIC can then deliver the same level interrupt again.

A private diagnostic run captured this race in the timer guest.
GDB stopped at `0x300154`, the second interrupt wait loop, with `w12=3` and `w13=w14=41`.
The loop requires `w12=2`, so repeated delivery causes a permanent wait.
QEMU exception tracing also recorded another FIQ immediately after the second handler returned to `0x300154`.
The mask write must therefore lower the source synchronously, before the handler issues EOI.

### Guest regression

The existing `timer_frames_share_clock_and_preserve_signed_deadlines` guest also exercises both physical GIC routes.
It configures secure Group 0 and receives actual EL3 FIQ exceptions.
The handler reads INTID 40 or 41, masks that timer, and issues EOI.
The first frame expires while masked, then delivers after the guest clears IMASK.
The second frame uses a future deadline while the shared counter runs.
The guest requires both interrupts and rejects repeated delivery after the handlers mask their sources.
The earlier checks retain negative TVAL, 64-bit CVAL, counter stop, shared state, and CNTACR coverage.
The extended regression passed during `make test`, including concurrent execution with the other eight guest regressions.

```sh
cargo test --locked --test platform timer_frames_share_clock_and_preserve_signed_deadlines -- --ignored
```

The Arm register descriptions define [CNTACR](https://fooptrvoid.github.io/arm-mra-2024-12-sysreg/ext-cntacrn.html)
and [CNTP_CVAL](https://fooptrvoid.github.io/arm-mra-2024-12-sysreg/ext-cntp_cval.html).

## Direct ROM and trace evidence

The input is `pbl8850.bin`, length `0x3c000`, mapped at `0x00300000`.
Each ROM file offset equals the PC minus `0x00300000`.
No firmware bytes are included here.

`build/private/run-sm8850-3/qemu-apss.log`, lines 16-60, records this path:

1. Reset enters `0x301800`.
2. `0x301818` reads the stack base literal at `0x31ed28`, whose value is `0x14800000`.
3. `0x30181c` adds `0x3000`. `0x301820` sets SP to `0x14803000`.
4. `0x301828` sets VBAR_EL3 to `0x300800`, from the literal at `0x301950`.
5. `0x30182c` calls the integer-register clear routine at `0x30c6d4`.
6. `0x301830` calls `0x30cd6c`.
7. `0x30cd74` loads a 32-bit word from `0x17813000` and aborts.

Map the supplied initial stack range as writable RAM: `[0x14800000, 0x14804000)`.
Do not place the initial SP at the range's upper end. The ROM selects an interior address.
Later ROM initialization also uses RAM near `0x14812000` through `0x14818c64`.
The stack range alone is not the complete PBL RAM map.

The timer routines provide these further observations:

| ROM PCs | Address | Observed behavior |
| --- | --- | --- |
| `0x30ccbc` | `0x17811028` | Write the delay interval. |
| `0x30ccc0`-`0x30ccc8` | `0x1781102c` | Read, set bit 0, then write control. |
| `0x30cccc`-`0x30ccd0` | `0x1781102c` | Poll until bit 2 becomes one. |
| `0x30ccd4`-`0x30ccdc` | `0x1781102c` | Clear bit 0 after expiry. |
| `0x30cd00`-`0x30cd20` | `0x17813028`, `0x1781302c` | Disable, set the interval, then enable the second timer. |
| `0x30cd30`-`0x30cd4c` | `0x1781302c` | Test bit 2, then clear bit 0 after expiry. |
| `0x30cd5c`-`0x30cd64` | `0x1781302c` | Clear bit 0. |
| `0x30cda4`-`0x30cdb8` | `0x1781102c`, `0x1781302c` | Clear bit 1 in both timer controls. |

At `0x30cc58`, a delay input of 5 selects 96 ticks.
At `0x30ccb0`, a delay input of 1000 selects 19200 ticks.
At `0x30cd7c`-`0x30cd94`, the conversion approximates counter ticks divided by 19.2.
[INFERENCE] These constants identify a 19.2 MHz timer and microsecond delay inputs.
The target frequency is therefore an evidence-based profile value, not a hard-coded model constant.

## Architectural analogue

The model's timer frame layout is:

- CNTPCT_LO at `+0`, CNTPCT_HI at `+4`, and CNTFRQ at `+0x10`.
- CNTP_CVAL_LO/HI at `+0x20/+0x24`.
- CNTP_TVAL at `+0x28` and CNTP_CTL at `+0x2c`.
- CTL ENABLE at bit 0, IMASK at bit 1, and ISTAT at bit 2.
- The high counter and compare words contain 24 implemented bits.

The selected ROM establishes the interval write at `+0x28` and the control word at `+0x2c`.
Its control bits zero and two match ENABLE and ISTAT.
Its clear of control bit one matches IMASK.
The counter, frequency, and compare offsets and the 24-bit high-word width come from the architectural analogue, not from the selected code.

A rollover-safe read reads HI, then LO, then HI again.
That sequence is a model property, not a recovered hardware requirement.

This analogue does not prove SM8850 addresses or reset values.
This review found no independent record of the analogue block base `0x16802000`.
The platform uses it as a declared aperture.
The SM8850 bases above come from the real ROM.
[INFERENCE] The matching ROM control sequence identifies the same physical-timer register layout.
The model uses the architectural 64-bit comparison rather than the analogue's truncated high-word mask.

## Immediate reset dependencies

These are static ROM paths beyond the executed abort. They are not claims of successful execution.

### Fuse-controller selection and scratch state

The next reset call at `0x301850` enters `0x30586c` and dispatches command `0x12` through `0x30cdc0`.
The dispatch table is at `0x31d178`. Command entries are 16-bit instruction offsets from `0x30ce28`.

- `0x30d660` reads `0x01fd4038` as a stack-canary value. `0x30d6b0` checks it again.
- `0x30d630` reads `0x221c8204` and extracts bit 30.
- `0x30d67c` selects the default controller base `0x221c0000`.
- If bit 30 is one, `0x30d69c` reads the override pointer at `0x221c9000`.
- A nonzero override replaces the controller base. Zero retains `0x221c0000`.
- The alternatives are the fixed default or the nonzero 32-bit override value. There is no second fixed base in this code.
- Command `0x12` reaches `0x30d7dc`. PCs `0x30d7ec/0x30d7f4` read and write `selected_base + 0x9004`.
- The reset call at `0x30185c` enters `0x30580c` and dispatches command `0x1f`.
- This command reaches `0x30d7ac`. PC `0x30d7cc` reads bit 31 from `selected_base + 0x9034`.
- A nonzero result from either reset helper takes the failure path at `0x30c750`.

At the default base, these registers are `0x221c9004` and `0x221c9034`.
Do not use arbitrary values to bypass these security decisions.
The shared fuse-controller model must preserve their actual meaning.

PC `0x305720` later writes a generated canary to `0x01fd4038`.
Thus this scratch location must retain writes. It is not a permanent zero-value register.

### Release, readiness, and mailbox dependencies

- `0x30de40/0x30de48` read and write `0x221c9040`, setting bit 0.
- This routine then polls `0x2223602c` for value 1 through `0x30da1c`.
- Other callers poll that state register for values 3, 4, or 5.
- `0x30ddc4` tests state 1. `0x30de1c` tests state 2.
- `0x302640`-`0x302668` checks those state helpers while it waits for command completion.
- `0x30d9dc` reads `0x2223604c`, extracts bits 23:16, and compares them with a byte in PBL RAM.
- `0x30de68/0x30de70` read and write `0x20c02018`, setting bit 2.

[INFERENCE] These control and state registers form a release/readiness handshake with the security processor.
Their authoritative register names and transition ownership remain unresolved.
The direct caller at `0x303390` follows command 3 submission at `0x303358`.
PCs `0x303368`-`0x30337c` first wait for state 1 or state 2.
Only state 2 selects the request routine at `0x30de2c`.
That routine sets request bit 0 and waits for state 1.
This path does not prove that the request releases the processor from cold reset.
Do not fabricate a state-2-to-state-1 transition or a cold-reset acknowledgement.
The supplied TME ROM does not directly materialize either APSS readiness address or the candidate address `0x02236000`.
Its accessors at `0x211f0e` and `0x211ecc` instead return `0x0223002c` and `0x0223004c`.
No supplied evidence establishes an alias between these banks.
They must remain separate until translation or producer evidence establishes that alias.

The command ring begins at `0x22002800`:

- `0x30f0f8` clears `0x7e0` bytes there. `0x30f124` writes the signature and capacity 2.
- `0x30f6b0` reads its signature. `0x30f0d8` accepts `0x4c425041` or `0x43535841`.
- `0x30f6bc` reads the producer count at `0x22002808`.
- `0x30f74c` reads producer and consumer counts at header offsets `8` and `12`.
- `0x30f75c` reads capacity at offset `4`.
- `0x30f77c` selects a `0x14`-byte slot at `base + 0x10 + (producer % capacity) * 0x14`.
- `0x30f6e8`-`0x30f6f8` write arguments, state, sequence, command, and the producer count.
- `0x30f868` reads slot offset `8`. State 2 signals completion.

[INFERENCE] This ring requires memory shared with the executing peer ROM.
Do not synthesize completed commands or bypass the consumer's authentication work.
The fallback path controlled by command `0x56` also exists. Its presence does not authorize selecting it to avoid a real peer.

### Shared SRAM extents and publication order

The supplied reset trace stops before any ring operation. The following order comes from static disassembly, not an executed handshake.

The request-ring clear at `0x30f11c` covers `[0x22002800, 0x22002fe0)`.
The response-ring clear at `0x30f0b4` covers `[0x22000000, 0x220007e0)`.
Both routines use length `0x7e0` and initialize capacity 2.
Each ring has a `0x10`-byte header and `0x14`-byte slots.
Two active slots therefore occupy `0x38` bytes including the header.
The larger clear extents still require writable backing memory.
These accesses establish required subranges, not the complete SRAM aperture size.

The response signature check at `0x30f078` accepts `0x4c425054` or `0x57464d54`.
The request signature check at `0x30f0d8` accepts `0x4c425041` or `0x43535841`.
Both signatures must identify a compatible pair, as checked by `0x30fac0`.

Request publication proceeds in this instruction order:

1. `0x30f6e8` writes arguments at slot offsets `8` and `12`.
2. `0x30f6ec` writes state zero at slot offset `16`.
3. `0x30f6f4` writes the sequence and command at slot offsets `0` and `4`.
4. `0x30f6f8` publishes the incremented producer count at header offset `8`.

The completion helper at `0x30f610` locates both corresponding slots.
The response slot comes from the ring at `0x22000000`.
The original request slot comes from the ring at `0x22002800`.
PCs `0x30f850/0x30f85c` check both sequence values.
PC `0x30f868` reads response-slot offset `8` and tests for completion state 2.
Do not confuse that response field with request-slot offset `8`, which contains an argument.

The boot path at `0x302610` submits command 4 with both arguments zero.
It checks completion at `0x30265c`, reads the result at `0x30267c`, and retires the command at `0x30268c`.
Other conditional paths submit command 5 at `0x303130` and command 3 at `0x303358`.
The static evidence does not establish one unconditional total order across those paths.

## TCSR SoC identity

The integrated run `run-sm8850-functional-controls11` reached APSS `0x30d86c`, which reads `0x01fc8004`.
The helper masks bits 1:0 and returns the emulation type.
The TME helper reads `0x05fc8000` and `0x05fc8008`, so the paired identity words are selected-code evidence.
The source-only TCSR offset names and the `0x88000` decomposition remain unestablished.
The emulation-type encodings below are the model's declared values, not recovered silicon encodings.

`qcom,tcsr-soc-identity-v1` shares immutable identity reads across the APSS and TME views.
The APSS aperture at `0x01fc8000` retains its two words.
The TME view maps `0x05fc8000`, `0x05fc8004`, and `0x05fc8008`, each as a four-byte register.
OEM `0x42040a` reads the emulation word and masks its low two bits.
The runtime reconstruction passes this read through the same identity model as APSS.
The board explicitly selects hardware version `0xa01b0200`, emulation type zero, and software-once version zero.
This version follows the constructed rehost profile: family 10, device 27, revision 2.0.
The rehost cites the matching XBL decode at `0x1485ebc8–0x1485ec50`.
These values are virtual policy, not captured silicon registers.
They select the normal firmware path instead of avoiding PHY initialization through an emulation branch.

The integrated run `run-sm8850-functional-controls31` stopped at TME `0x20cb5a`, reading four bytes from `0x05fc8000`.
The configured TME ROM in `build/private/sm8850.toml` contains the identity helper at `0x20cb4c–0x20cbaa`.
PC `0x20cb56` loads base `0x05fc8000`.
PCs `0x20cb5a/0x20cb5c` read its hardware-version word twice.
PC `0x20cb64` calls fuse query zero at `0x200ff8`, with an output word on the stack.
The helper reads the software-once word twice at `0x20cb80/0x20cb82` only if that query returns `0x050cce55` and nonzero output.
The helper combines family mask `0xf0000000` with device mask `0x0fff0000`.
If the combined software-once identity is zero, PCs `0x20cb94–0x20cba0` instead use the original hardware-version reads.

The TME helper combines a family mask `0xf0000000` with a device mask `0x0fff0000` at `0x20cb94`.
Those masks independently establish the family field at bits `31:28` and the device field at bits `27:16`.
The minor fields at bits `15:8` and `7:0` follow the matching XBL decode at `0x1485ebc8..0x1485ec50`.
The source-only register names remain descriptive labels.
The interface provides write macros but does not establish write-once lock behavior.
The model implements only the evidenced reads, not software-once writes or lock semantics.
The explicit zero software-once policy retains the original identity without changing the fuse query or authentication result.
The model accepts aligned 32-bit reads at offsets zero, four, and eight.
It also accepts one aligned 64-bit read at offset zero.
The hardware word occupies bits 31:0. The emulation word occupies bits 63:32.
The model rejects unknown constituent words, byte enables, and writes before it changes the transaction buffer.

CPH2771 XBL `0x14826094` reads both words in one transaction.
The first reconstructed SM8845 run rejected that eight-byte read.
After the correction, actual XBL execution reached the debug-power read at `0x14826270`.
The sequencer retains its separate signed hardware selector. This access change does not change authentication policy.

Run `run-sm8850-functional-controls32` passed these identity reads and reached the sequencer descriptor write.
The corrected automatic-observer run `run-sm8850-functional-controls33` then reached hash command one.
The SM8845 compatibility run `run-sm8845-functional-controls3` reached the same command through the shared model.
These results do not establish complete image authentication.

## Complete-runtime timer initialization

The complete `8975tme.bin` runtime configures three timer frames.
Instructions `0x22aa94..0x22aa98` write access value `0x3f` at offsets `0x40`, `0x44`, and `0x48`.
The base helper at `0x22acd4` returns `0x00c20000`.
The runtime reads the frequency at offset zero and supplies a default if necessary.

The timer model now retains three independent frame states.
The third frame socket is optional.
The private reconstruction probe maps the configuration aperture and connects frame zero to the RISC-V machine timer input.
The probe passed these writes and later stopped during descriptor traversal at `0x230610`.

The guest regression checks the third frame's counter, compare isolation, masked expiry, and independent control.
The complete integration suite passed all 44 cases.

## XBL sleep counter

The OEM `IMAGES/xbl_sc.elf` has entry address `0x14827fa8`.
Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
The real APSS ROM dispatched this entry after the TME completed its boot request.
XBL instruction `0x14827fbc` reads the word at `0x0c221000` before it initializes the stack.

The selected XBL read establishes the address, not the tick rate.
The model declares 32,768 Hz for this counter.
This rate declaration selects the observed timestamp conversions. It is not a measured clock.
This clock differs from the 19.2 MHz architectural timer.

`sleep_counter.h` implements `qcom,mpm-sleep-counter-v1`.
The counter follows simulation time, includes transaction delay, and wraps at 32 bits.
Its zero epoch is explicit virtual reset policy.
Only the observed four-byte read is supported.
Writes, other offsets, and unsupported transfer shapes fail.

The OEM run passed this read and reached XBL function `0x14828b6c`.
It next read debug-power status at `0x0c2a9010`, from PC `0x14828160`.
No Linux boot log was observed at this boundary.

## Private debug-power policy

Selected XBL reads this word at `0x14828160` and checks the combined status before it programs the APSS watchdog.
Bit 31 carries that status.
The source-only meanings of the remaining bits, including the software-request meaning of bit zero, remain unestablished.

The private reconstruction profile declares an external request for virtual debugger power.
The observed register therefore reports `0x80000000`.
The profile logs `DEBUG_POWER_SUBSTITUTE external_request=1`.
This power policy prevents XBL from starting its APSS watchdog.
It does not grant debug memory access or change authentication.
The TME debug-access policy remains disabled.

The actual OEM run passed this check and reached RNG channel three.
This policy does not implement APSS watchdog hardware.


## Selected QSEE timer cohort

Static analysis uses the selected SM8850 and SM8845 `RADIO/tz.img` images.
Their hashes are `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932` and `b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09`.

| Operation | SM8850 | SM8845 |
| --- | --- | --- |
| Timer initialization | `0x15772fac` | `0x15766294` |
| Selected mask helper | `0x15773bc8` | `0x15766eb0` |
| Interrupt registration | `0x157733f0` | `0x157666d8` |
| Physical compare programming | `0x15773048` | `0x15766330` |

The mask helper returns `0x18`, selecting physical frames three and four.
Firmware timer IDs two and three identify these frames, not frames two and three.
The SM8845 table at `0x157c9ae4` contains actual interrupt IDs `[29,42,43,44,45]`.
The registration and GIC operations preserve each selected ID without adding 32.
Thus frame three drives INTID 43, and frame four drives INTID 44.
The same table identifies physical frame two as INTID 42 and physical frame five as INTID 45.
The platform connects these existing physical timer states to SPI inputs ten and thirteen.

Initialization writes `CNTNSAR = 0x63` and `CNTACR = 0x3f` for frames zero, one, five, and six.
It preserves the access controls for frames two, three, and four.
The existing virtual profile supplies `frame_access_reset = 0x3f` to all seven frames.
No secure-access bypass replaces these controls.
Initialization also clears all seven virtual offsets at `0x17810080 + 8*n`.

The [architectural CNTVOFF reference](https://df.lth.se/~getz/ARM/SysReg/ext-cntvoffn.html) defines the split 64-bit offset registers and their CNTNSAR access gates.
The model retains each complete offset and applies it to that frame's virtual count.
It does not change the physical counter or another frame's offset.
The selected QSEE module uses physical counters and comparators, not virtual comparators.

The arm helper computes `physical_count + floor(microseconds * 19.2)`.
It disables the physical timer, writes both compare words, then enables the timer.
The existing scheduled comparator drives the actual interrupt from this state.
It does not report elapsed time from register reads or return a completion constant.

Suite `bg943` passes all 99 tests and Clippy with warnings denied.
The native timer fixture checks a nonzero 64-bit offset, subtraction across the word boundary, and isolation between frames.
It then receives actual GIC interrupts 43 and 44 from independently scheduled physical timers.
This fixture proof does not establish completion of native QSEE initialization.

Suite `bg945` also checks the existing frame-two deadline through actual INTID 42 after unmasking.

## CPUCP runtime full-width counter reads

These stops follow the identity transition fixes:

| Firmware | Run | Access | PC |
|---|---|---|---|
| SM8850 | run269 | R64 `0x1781d000` | `0x1d8023f6` |
| SM8845 | run215 | R64 `0x1781d000` | `0x1d802456` |

That address is frame six's physical counter slot.
The timer's former four-byte-only validator rejected these real eight-byte transactions with `TLM_BURST_ERROR_RESPONSE`.
The fault does not establish a bad firmware address or a missing counter.

Frame offsets zero and eight now accept aligned eight-byte reads for CNTPCT and CNTVCT.
The shared counter is sampled once per accepted transaction.
The physical result is that sample.
The virtual result subtracts the frame's retained virtual offset from the same sample, with 64-bit wraparound.
The model serializes all eight bytes in little-endian order.
It does not combine two independently timed word reads.

The existing CNTNSAR check still rejects an unauthorized nonsecure frame access.
CNTACR and frame-zero CNTEL0ACR still gate physical and virtual counter reads independently.
A closed read-zero gate clears the complete requested result, including all eight bytes of a full-width read.
The change does not add a permission bypass or a separate second-view counter.

The control and configuration interfaces remain four-byte-only.
Frequency, virtual-offset, compare, and timer-control accesses retain their previous widths.
Eight-byte counter writes remain unsupported, even when a counter permission is closed.
No new deadline or configuration write behavior is added.

The existing native timer fixture now maps frame six and compares its full-width counter with the other stopped frames.
It compares full-width reads with the existing low/high-word reconstruction.
It also checks physical and virtual read-zero gates through CNTACR and the optional EL0 view.
Its nonzero virtual offset checks full-width subtraction across the 32-bit boundary.
All existing signed-deadline and real interrupt checks remain in the normal variant.

Four additional variants require exact access failures for:

- An eight-byte frequency read at `0x17811010`.
- An eight-byte comparison read at `0x17811020`.
- An eight-byte configuration read at `0x17810000`.
- An eight-byte counter write at `0x17811000`.

Verification command:

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform timer_frames_share_clock_and_preserve_signed_deadlines -- --ignored --exact
```

The focused timer fixture passes, including all four rejection variants.
SM8850 run270 and SM8845 run216 pass the full-width counter read.
They next write `0x1818021c`, from PCs `0x1d805eb6` and `0x1d80616e`, respectively.
The complete integration run passes 47 host tests and 72 platform tests. Clippy passes with warnings denied.

## QSEE fatal debug state

The Infiniti probe read pointer `0xaa980000` at system IMEM offset `0x10`.
The table word at offset four was one.
These observations do not establish a UEFI handoff contract.

Run380 identifies a TME transport failure before the watchdog reset.
QSEE sends command `0x166` through function `0x15670458`.
Caller `0x15675c90` enters the fatal handler with reason `0x64`.
The subsequent debug path reads the pointer at `0x14680010`.
Its table access belongs to fatal diagnostics, not normal UEFI dispatch.

The platform removes the unsupported table publisher.
Diagnostic run379 used that publisher and disabled the unconnected watchdog bite action.
Its secure EL1 wait instruction does not prove boot progress.

Run378 records image `0x09` admission at sequence 69.
The trace records no UEFI dispatch or kernel entry.
The platform must first complete the genuine TME request and response.


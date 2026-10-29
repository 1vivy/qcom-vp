# APSS interrupt controller and ITS

## Selected firmware

The QHEE image is the `hyp_a` partition of LUN 4.
The partition uses LBAs 3590 to 5637 with 4096-byte sectors.
Its SHA-256 is `a43652e5a60bf8c71ea7123d1384f892d9911e15b625dbdf0a7fb342aecb3dfa`.
The image is a static PIE ELF with no section headers.
The first load segment has virtual address 0 and file offset `0x1000`, so file offset equals virtual address plus `0x1000`.
Run 434 entered QHEE at `0xffffffe94a800000`.
All QHEE addresses below are image virtual addresses before that slide.

## Distributor 1-of-N capability

QHEE initializes the distributor in `gicv3_handle_irq_init`.
The function reads `GICD_TYPER` with a 32-bit load from distributor offset 4 at `0x45ccc`.
It tests No1N at `0x45d5c`:

```text
0x45d5c  tbnz w22, #0x19, #0x47ee4
```

Bit 25 selects the panic block at `0x47ee4`.
That block passes these arguments to the panic routine at `0xd4fc4`:

| Argument | Image address | Content |
|---|---|---|
| File | `0xe49e5` | `hyp/platform/gicv3/src/gicv3.c` |
| Function | `0xf8f9c` | `gicv3_handle_irq_init` |
| Expression | `0xf21ca` | `!GICD_TYPER_get_No1N(&typer)` |
| Line | `0x33e` | 830 |

The [GIC-700 Technical Reference Manual](https://documentation-service.arm.com/static/66b0adde882fec713ef48410),
section 4.11.3, states that `GICD_TYPER.No1N==0` means the GIC-700 supports 1 of N SPI selection.
The register description also returns 0 for that field.
The [Kaanapali device tree](https://github.com/torvalds/linux/blob/9339897ad7eded77adf834e032482f7609911448/arch/arm64/boot/dts/qcom/kaanapali.dtsi#L5676-L5700)
declares `arm,gic-v3` at `0x17000000`, one redistributor region with `0x40000` stride, and an ITS child at `0x17040000`.

A passive read of the live phone returned `GICD_TYPER` value `0xf96b011f` at `0x17000004`.
The read used width 4 at EL1 on CPH2747, slot `_a`, with verified boot state `green` and device state `locked`.
Bit 25 is zero in that value.
The record is [`20261025T093009Z-phone-qcom-vp-passive-baseline`](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261025T093009Z-phone-qcom-vp-passive-baseline/record.md), reported in `docs/qcom/sm88xx/oplus/infiniti/hardware-evidence.md`.
The later EL1 read of `GICD_IIDR` returned `0x47000070`, with Implementer `0x070`, not Arm `0x43b`.
That record is [`20261026T100032Z-phone-gicd-iidr`](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261026T100032Z-phone-gicd-iidr/record.md).
[INFERENCE] These EL1 registers describe QHEE's virtual GIC, not the silicon distributor.
The phone TYPER value therefore does not establish the hardware No1N value.
The model choice rests on QHEE's assertion and GIC-700 TRM section 4.11.3.

QEMU hardcodes No1N to 1 in `hw/intc/arm_gicv3_dist.c`.
QEMU also has no 1-of-N target selection.
Its `gicv3_cache_target_cpustate` ignores `GICD_IROUTER.IRM` and matches one affinity only.

The model therefore clears bit 25 on reads of the TYPER dword.
It also reports the GIC-700 ProductID, as described below.
Other distributor accesses reach the native device.
This change is a capability advertisement substitute, not native routing support.
`docs/qcom/sm88xx/stubs.md` records the substitute.
Run 423 and run 424 stopped after QSEE cold initialization with the unmodified value.
Runs 430 through 434 passed this check.

## ITS window

Run 434 stopped at QHEE image offset `0x45fd0` with `ldr x25, [x19, #8]`.
The access was an 8-byte read of physical address `0x17040008`.
The guest fault record was ESR `0x96000010`, FAR `0xffffffd3a4240008`, and ELR `0xffffffe94a845fd0`.
`0x45fd0` reads `GITS_TYPER`, so `x19` holds the ITS base.

The same block then checks these TYPER fields:

| Instruction | Field | Panic block | Expression string | Source line |
|---|---|---|---|---|
| `0x45fd4 tbz w25, #0` | `PHYSICAL` | `0x47bb8` | `GITS_TYPER_get_Physical(&typer)` | 635 |
| `0x45fd8 tbz w25, #1` | `VIRTUAL` | `0x47bd8` | `GITS_TYPER_get_Virtual(&typer)` | 637 |
| `0x45ff0 b.ne` after `0x18000000000 & ~w25` | bits 39 and 40 | `0x47bf8` | `GITS_TYPER_get_VMAPP(&typer) && GITS_TYPER_get_VSGI(&typer)` | 648 |

The panic blocks name the function `gicv3_its_init_one` and the file `hyp/platform/gicv3/src/gicv3_its.c`.
The Linux Kaanapali device tree declares `msi-controller@17040000` with a `0x40000` register range.
The live phone device tree reports the same node with size `0x20000`.
The live record is [`20261026T074312Z-phone-passive-platform-facts`](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261026T074312Z-phone-passive-platform-facts/record.md).
It also reports the GIC at `0x17000000` with size `0x10000` and one redistributor region at `0x17080000` with size `0x200000` and stride `0x40000`.
The model uses the live ITS size of `0x20000`, which also equals the native QEMU frame size.

The model binds the native `arm-gicv3-its` device to the APSS GIC at `0x17040000`.
The parent link supplies the GIC, and the native device uses the GIC DMA address space.
The GIC enables LPIs, so QEMU sets `GICD_TYPER.LPIS` and `GICR_TYPER.PLPIS`, and the redistributor frames grow to `0x40000`.
The platform slot size and the native alias stride both use `0x40000`.
The earlier `0x20000` alias made slot one point inside CPU 0's frame when LPIs became active.
The platform suite detected it.

### Virtual capability advertisement

Run 440 executed the TYPER read and the panic block at `0x47bf8`.
QHEE then entered its panic loop at image offsets `0xd505c` and `0xd5060`.
The loop contains `wfe` and a branch back to it.
QHEE therefore requires the three fields that QEMU leaves at zero:

| Field | Bit | Meaning | QEMU value |
|---|---|---|---|
| `VSGI` | 39 | Direct injection of virtual SGIs | 0 |
| `VMAPP` | 40 | GICv4.1 VMAPP command layout | 0 |
| `SVPET` | 41 | Shared vPE table, low bit of the field at 42:41 | 0 |

Arm documents these fields as implementation-defined when the GIC implements GICv4.1.
The installed `gicv3_arm_its_realize` sets bits 32 through 37 and bits 0 and 1 only.
The pinned library contains no GICv4.1 or VSGI symbol, so no property can enable these bits.
The native ITS implements the GICv4.0 VMAPP command layout and no direct virtual SGI injection.

A passive Gobbl read of `0x17040000` on the phone lost the USB session and produced crashdump record [`20261026T075303Z-phone-gic-its-read`](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261026T075303Z-phone-gic-its-read/record.md).
It returned no GITS_TYPER value, and the hypervisor diagnostic region holds encrypted bytes.
The phone therefore establishes no capability value for this ITS.
The GIC-700 class of the SoC and the QHEE requirement are the basis for the two fields.

`qcom,apss-its-v1` adds a read window over the native ITS region.
Reads of the `GITS_TYPER` dword set bits 39, 40, and 41.
Every other access reaches the native device through an address space over its region.
Run 441 read the window through the container region directly and returned a decode error.
Run 442 showed an early return that skipped the bit change for 8-byte reads.
The current model uses an overlap test, which sets the bits for every access that covers the TYPER.
`docs/qcom/sm88xx/stubs.md` records the substitute.

The substitute affects only the capability advertisement.
QHEE image offsets `0x46000` and `0x46094` read GITS_CTLR and write GITS_BASER through the native device.
A later VMAPP command with GICv4.1-only fields, or a direct virtual SGI, would exceed the native implementation.
The current runs do not show either request.

Run 443 executed the TYPER read at `0x45fd0` and skipped the panic block at `0x47bf8`.
It then executed `0x46000` and `0x46094`.
The guest executed 16,305 translated blocks in its EL1 range.
The run made no unmodeled access.
It reached the 900-second timeout in a repeating FIQ, IRQ, and SMC sequence.
The guest issues an SMC, EL3 returns to the EL2 idle address `0x4660c`, and the next timer interrupt returns to the guest vector.
No UART output appears after `SBL1, End`.
The next investigation is that guest wait, not the interrupt controller.

The native ITS has two adjacent 64 KiB regions.
Its control registers occupy the first region, and `GITS_TRANSLATER` is at `0x10000`.
A GIC-700 frame places `GITS_TRANSLATER` at `0x40` inside a `0x40000` frame.
The model does not yet assert that translation layout because QSEE and QHEE send no MSI in these runs.

## ITS enable transition

Run 447 reached QHEE image offset `0x46600` with this sequence:

```text
0x46600  orr  w8, w21, #1
0x46604  stlr w8, [x19]
0x46608  ldapr w8, [x19]
0x4660c  tbnz w8, #31, #0x46608
```

`x21` held `0x80000000`.
`x19` is the ITS control frame, at `0xfffffff9d0240000` in run 447 and `0xffffff8598240000` in run 448.
The frame words were `GITS_CTLR = 0x80000001`, `GITS_IIDR = 0x43b`, `GITS_TYPER = 0x1bf0001efb3`, `GITS_CBASER = 0xb8000000802f0400` at `+0x80`, and the base registers from `+0x100`.
The TYPER value contains the VSGI and VMAPP bits that the window published in those runs.

QHEE enables the ITS and then polls `GITS_CTLR.Quiescent` (bit 31) until it clears.
Arm defines that bit as read-only: it reads zero while `Enabled` is one, and it reads one while `Enabled` is zero and the ITS is idle.
QEMU sets Quiescent only in its reset callback, and its CTLR write changes `Enabled` alone.
The native register therefore keeps bit 31 set and QHEE polls forever.
`qcom,apss-its-v1` now ignores written bit 31 and derives the read value from `Enabled`.
The earlier runs show the stuck value, and no software writer exists because the expected change is a register transition.

Run 449 passed the enable wait and executed `0x46610` and `0x46800`.
It then stopped during ITS initialization at `0x46c54`.
The failure message is `No ITS had a shared vPE table` at image va `0xe8049` and file `0xe9049`.
QHEE extracts `GITS_TYPER.SVPET` (bits 42:41) from the same frame word and requires a nonzero value.
The window now sets SVPET to one.
That value compares affinity level three only, which matches `GITS_MPIDR` zero and every APSS affinity here.
Run 451 repeated the path with SVPET set and executed `0x468a0`.
The shared-vPE-table failure did not recur.
The guest continued its EL1 loop after the stop, and the run made no unmodeled access.

## Distributor identity

Run 451 then executed the distributor identity check at `0x46c60` and stopped at the second assert.
The first assert passed: `GICD_IIDR_get_Implementer(&iidr) == 0x43bU` at line 951, panic block `0x47f24`.
The second assert failed: `(GICD_IIDR_get_ProductID(&iidr) == 2U) || (GICD_IIDR_get_ProductID(&iidr) == 4U)` at line 954, panic block `0x48028`.
The check reads distributor offset 8 with a 32-bit load.
QHEE places Implementer at bits 11:0 and ProductID at bits 31:24.
QEMU returns `gicv3_iidr()` value `0x43b` and describes itself as a GIC-500 r0p0 with a zero ProductID.
The [TF-A GICv3 header](https://github.com/ARM-software/arm-trusted-firmware/blob/master/include/drivers/arm/gicv3.h) defines the same field positions and these product identifiers:
`GIC_PRODUCT_ID_GIC600 = 2`, `GIC_PRODUCT_ID_GIC600AE = 3`, and `GIC_PRODUCT_ID_GIC700 = 4`.
The model now reports ProductID 4 on `GICD_IIDR` and `GITS_IIDR`.
The live EL1 read `0x47000070` does not establish the raw distributor value because QHEE mediates that view.

Run 452 passed the distributor identity check and stopped at the APSS watchdog page.
QHEE mapped `0x17600000` and wrote page offset `0x24`, bark, bite, and the enable bits.
The old socket ended at page offset `0x17`, so the offset `0x24` write failed.
The socket now covers `0x17600004` through `0x17600027`.
The model retains the `0x24` word without a timeout effect.

A Ghidra decode of the image places `gicv3_its_init_one` at `0x454e0..0x480eb`, with this sequence inlined.
The decode also agrees with the frame contents above and with the enable and disable waits at `0x46004` and `0x46604`.
The next stage is `0x48100`, the per-CPU redistributor initialization.
Resource manager activation follows, with PSCI `CPU_ON` `0xc4000003` and the `secondary cpu ({:d}) cold boot` path.

Run 451 used image base `0xffffffb92f800000`.
The assertion string is at image address `0xf9f90`, file offset `0xfaf90`.
The panic loop was `0xd505c..0xd5060`.
Only CPU zero took exceptions.
The guest continued its EL1 loop, and UART output still ended at `SBL1, End`.
The run reached its 1800-second timeout without kernel entry or an unmodeled access.

Run 452 crossed the distributor identity check and reached a new MMIO boundary.
QHEE wrote `0xf` to `0x17600024` at PC `0xffffff966e046df0`, image offset `0x46df0`.
The access width was four bytes, and the model returned `TLM_ADDRESS_ERROR_RESPONSE`.
The run exited with code two before kernel entry.
The Kaanapali DT at lines 5702 through 5706 declares `watchdog@17600000`, compatible with `qcom,kpss-wdt`, with a `0x1000` register range and SPI zero.
The next two instructions prepare `0xfffa` for writes at offsets `0x14` and `0x10`.
The next model contract is the APSS watchdog, not the GIC identity or SVPET.

The EL1 guest is APPSBL, not the resource manager.
BL31 transfers to EL1 at `0x14694170`, which loads the entry from `[x12, #8]`.
The guest code extends past `0x1bf380` bytes, which exceeds the QHEE image and fits APPSBL's `0x2a8000` bytes.

XBL reports `HYP Config - Image Loaded, Delta - (0 Bytes)`.
The `devcfg_a` image contains `/dev/hyp_config_oem` and `hlos_vm_config`.
QHEE reads ITS frame offset `0x18` during the same initialization.
QEMU returns zero for that read and logs an invalid guest read.
Arm defines that offset as `GITS_MPIDR`, which this model does not implement.

Physical reads through the debug stub at `0x80d60000` and `0x880000000` fail with `TLM_COMMAND_ERROR_RESPONSE` and stop the platform.
Use virtual reads at the current exception level, or the native boot log, for these regions.

## Redistributor GICv4.1 fields

QHEE runs `FUN_00148100` after the cold initialization returns.
The boot CPU calls it at image offset `0x67fc`, and every secondary CPU calls it at `0x6ab90`.
The function reads `GICR_TYPER` and tests two conditions:

| Instruction | Test | Failure |
|---|---|---|
| `0x4826c ldr x10, [x22, #8]` | Reads the 64-bit `GICR_TYPER` | — |
| `0x48270 tbz w10, #0, 0x489b0` | `PLPIS` must be one | Panic block `0x489b0` |
| `0x48274..0x48284` | Bits 26, 7, and 2 must all be one (`~typer & 0x04000084`) | Panic block `0x489d0` |

The panic string at image address `0xff4f0` is `GICR_TYPER_get_RVPEID(&typer) && GICR_TYPER_get_VSGI(&typer) && GICR_TYPER_get_Dirty(&typer)`.
GICv4.1 defines bit 2 as `Dirty`, bit 7 as `RVPEID`, and bit 26 as `VSGI`.
QEMU builds `GICR_TYPER` in `hw/intc/arm_gicv3_common.c` from the affinity, `CommonLPIAff`, the processor number, `PLPIS`, and `VLPIS`.
It sets none of the three GICv4.1 fields.

Each redistributor window of `qcom,apss-gic-v1` now forwards to its native frame and sets the three bits on reads of the low `GICR_TYPER` dword.
This change is a capability advertisement substitute.
The native model keeps the GICv4.0 `GICR_VPENDBASER` form and has no direct virtual SGI injection.
`docs/qcom/sm88xx/stubs.md` records the substitute.
The same function later checks AMU, debug, timer frequency, and ETE identification registers.
Those checks concern the CPU model, not the GIC.

## APSS identification words

QHEE reads the activity-monitor identification at image offset `0x48314` before it enables any interrupt.
QEMU defines no `AMCFGR_EL0` register, so the read raised an Undefined Instruction exception at EL2.
Run 460 records the access before the exception:

```text
read access to unsupported AArch64 system register op0:3 op1:3 crn:13 crm:2 op2:1
Taking exception 1 [Undefined Instruction] on CPU 0
...with ELR 0xffffffd163648314
```

The comparison values follow, in QHEE instruction order:

| Instruction | Required value |
|---|---|
| `0x4832c cmp x8, #0xb` | `AMCFGR_EL0[7:0]` is `0xb` |
| `0x4833c cmp x8, #0x804` | `AMCGCR_EL0[15:0]` is `0x804` |
| `0x48358 lsr x8, x8, #60` | `ID_AA64MMFR2_EL1[63:60]` is nonzero |
| `0x48370 and x22, x23, #0xf0` | `ID_AA64DFR0_EL1[7:4]` is nonzero |
| `0x483a0..0x483b0` | `TRCIDR2[9:5]` is four and `TRCIDR2[14:10]` is four |
| `0x483b4..0x483fc` | `TRCIDR4[3:0]` is two, `[19:16]` is four, `[23:20]`, `[27:24]`, and `[31:28]` are one |
| `0x48400..0x48428` | `TRCIDR5[27:25]` is four, `[11:9]` is four, and `[30:28]` is two |

QEMU clears `ID_AA64PFR0.AMU` and `ID_AA64DFR0.TRACEVER` for TCG and defines no activity-monitor or ETE identification register.
Patch `0018-implement-qcom-identification-registers.patch` adds an opt-in `x-qcom-ids` profile.
The profile reports `AMCFGR_EL0` `0xb`, `AMCGCR_EL0` `0x804`, `TRCIDR2` `0x1080`, `TRCIDR4` `0x11140002`, and `TRCIDR5` `0x28000800`.
It also reports `AMU`, `TRACEVER`, `E0PD`, `DebugVer` six, `BRPs` five, `WRPs` three, `MTPMU` one, and `ID_AA64ISAR2_EL1[31:28]` one.
`TRFCR_EL2` is a retained store with no trace hardware behind it.
It adds no counter, no trace unit, and no reset or trap change.
`ApssCpu` sets the property next to `x-qcom-mpam`.
`docs/qcom/sm88xx/stubs.md` records the substitute.

The `S2_` trace words need care.
`define_one_arm_cp_reg_with_opaque` derives an access mask from `opc1` and asserts that the declared right stays inside it.
`opc1` one accepts only `PL1_RW`, so a first trial that declared `PL0_R` aborted QEMU with `SIGABRT` before guest execution.
The registers declare `PL1_R`, which is inside that mask, and QHEE reads them at EL2.

Run 463 measured the final state.
The trace passes every comparison from `0x48314` through `0x48428`.
It passes the `ID_AA64MMFR1_EL1` read at `0x48790` and the `ID_AA64ISAR2_EL1` read at `0x487ac`.
It then writes `TRFCR_EL2`, which the profile retains.
The next stop is an unmodeled read of `0x31205020` with `TLM_ADDRESS_ERROR_RESPONSE`:

```text
UNMODELED_ACCESS core=platform.apss_access pc=0xfffffff2a26b7a14 address=0x31205020 width=4 operation=read
RUN_FAILED reason=unmodeled-access
```

`0x31200000` belongs to the GEM_NOC fault-monitor region that `platforms/qcom/sm88xx/qsee-fault-inventory.lua` already covers for writes.
That region is the next contract, and it is a platform map entry rather than a CPU profile entry.

## QHEE interrupt-setup words

QHEE continues past the identification checks and reads two more unowned words.
The first is `0x31205020`, then `0x31205028`, both at image offset `0x6b7a14` and `0x6b7a18`.
`platforms/qcom/sm88xx/qsee-fault-monitors.lua` now lists both words in the existing GEM_NOC retained bank.
Each returns the declared virtual cold value zero because no silicon capture gives a value.
The model creates no fault, no source identifier, and no completion.

QHEE then enters the APSS root-enable helper at image offset `0xa86a4`.
The block at `0x6a86c0` decodes as:

```text
mov  w9, #1
ldr  w10, [x8]
tst  w10, w9
orr  w9, w10, w9
ldrb w11, [x0, #0x15]
str  w9, [x8]
ret
```

The helper therefore reads one word, tests bit zero, sets it, and writes the word back.
The first word is `0x17c02c` and the second is `0x17c068`, a measured stride of `0x3c`.
Both addresses are in the APSS clock region around `0x140000` to `0x180000`.
The PBL descriptor table at `0x336d38` does not list either word, and the platform root table comes from that table.
The words are therefore not ROM-described clock roots.
`platforms/qcom/sm88xx/sm8850/runtime.lua` now gives each word a `qcom,retained-boot-control-v1` owner with `writable_mask` one and reset zero.
The owner keeps the guest's own bit and creates no clock, rate, or completion.
`docs/qcom/sm88xx/stubs.md` records both substitutes.

Run 464 measured the result.
The run records no unmodeled access.
It reaches the same end state as before: the `0x17600000` bank is petted once, the secure bank reaches its 384000-tick bite, and the platform reports `missing-kernel-handoff`.
QHEE has therefore passed these words but has not yet reached the code that enables INTID 32 and the EL2 timer PPIs.

### Root readiness poll

The helper at `0xa86a4` selects its enable mask from the descriptor: bit zero when the flag at `[desc+8]` is zero, and the word at `[desc+0xc]` otherwise.
It stores the previous state in `[desc+0x15]`, and the companion at `0xa86e0` clears the bit and stores the previous state in `[desc+0x1d]`.
The caller at `0xa3ac8` then polls with `0xa8a84`:

```text
0xa8a98  mov  w20, #-10000        ; 10000 attempts
0xa8a9c  mov  w0, #1
0xa8aa0  bl   0xb7358             ; udelay, reads the counter at 0x17813000
0xa8aac  ldr  w8, [x8]            ; the same control word
0xa8ab0  tbnz w8, #0x1f, success  ; bit 31 must be one
```

The delay at `0xb7358` builds the address `0x17813000` from `mov w8,#0x3000` and `movk w8,#0x1781,lsl #16`, so the poll advances on the APSS timer.
The caller clears the bit at `0xa3b14` before it polls, so bit 31 reports that the branch is off.
That is the `CLK_OFF` sense of a clock branch control, not a ready assertion.
`QcomRetainedBootControl` therefore takes optional `status_source_mask` and `status_bit` parameters.
The status bit reports that the named fields are clear, and a write that carries the status bit clears it before retention.
Both parameters default to off, so existing users keep their behaviour.
Both APSS words declare `writable_mask` one, `status_source_mask` one, and `status_bit` 31.

Run 465 showed no behaviour change because the first version derived the status with the opposite sense.
Run 466 uses the corrected sense.
It records the branch-success path zero times, the branch-failure path zero times, and no abort halt, so QHEE passes the whole clock setup without the poll.
The next stop is an unmodeled read of `0x3d96000`:

```text
UNMODELED_ACCESS core=platform.apss_access pc=0xffffffd437ca86c4 address=0x3d96000 width=4 operation=read
RUN_FAILED reason=unmodeled-access
```

`0x03d95000` and `0x03d99080` already belong to `qcom,gdsc-v1` owners in `platforms/qcom/sm88xx/xbl-power-domains.lua`, and QSEE enables `0x03d95000` for the GPU SMMU branch.
`0x3d96000` sits in that same GPUCC clock and power region and needs its own declared word.

## QHEE EL2 trap boundary

QHEE writes `MPAM2_EL2` and then `MPAMHCR_EL2` at image offsets `0x52934` and `0x5293c`.
QEMU has no `MPAMHCR_EL2`, so the write raises an Undefined Instruction exception at EL2.
QHEE's unknown-trap handler then reports `Unhandled EL2 trap` from `trap_dispatch.c` and stops at `0xd5100`, and its register dump never reaches the UART.

A trial profile added `MPAMHCR_EL2` (`S3_4_C10_C4_0`) as a retained word and set `MPAMIDR_EL1` `HAS_HCR` to one.
The trial boot moved the trap to the next register:

```text
write access to unsupported AArch64 system register op0:3 op1:4 crn:12 crm:12 op2:4
```

That encoding is the EL2 view of `ICC_CTLR_EL1`, which the native cpu interface defines only at `S3_0_C12_C12_4`.
The trial also failed `apss_executes_mpam_pauth_and_rng`: the fixture asserts `MPAMIDR_EL1` is zero at two sites and that `MPAMHCR_EL2` traps, which is the declared no-hypervisor-control MPAM profile.
The profile reports `HAS_HCR` one and keeps `MPAMHCR_EL2`, because the genuine firmware executes the store on the real SoC.
`docs/qcom/sm88xx/decisions.md` records the reasoning for patch `0019-implement-mpamhcr-el2.patch`.
`apss_executes_mpam_pauth_and_rng` now asserts `MPAMIDR_EL1` is one and that `MPAMHCR_EL2` retains a zero write, and its trap total drops from 39 to 38 because one expectation became a retention check.

The next trap is `S3_4_C12_C12_4`.
Arm numbers the virtual list registers as `ICH_LR<n>_EL2 = S3_4_C12_C(12+n/8)_(n%8)`, so that encoding is `ICH_LR4_EL2`, not an `ICC_CTLR_EL1` alias.
QEMU sets `cs->num_list_regs` from `cpu->gic_num_lrs`, and every max TCG core sets four, so only `ICH_LR0..LR3` exist.
QHEE never reads `ICH_VTR_EL2`; the image contains no reference to it.
Instead it switches over a fixed set: `msr ICH_LR0..LR7_EL2` at `0x92024`, `0x91fac`, `0x92044`, `0x91ff4`, `0x92034`, `0x91fd4`, `0x92054`, and `0x92014`, with the matching `mrs` forms at `0x917cc..0x917bc`.
The dispatcher at `0x91f88` bounds the index with `cmp w0, #0x3` and `cmp w0, #0x5` comparisons and an out-of-range arm.
The firmware therefore assumes eight list registers, which explains why it wrote `ICH_LR4_EL2` while QEMU reported four.
Patch `0020-expose-virtual-list-register-count.patch` adds the opt-in `x-qcom-gic-num-lrs` property, and `ApssCpu` requests eight.
Run 470 accepts every `ICH_LR` access and stops at `op0:2 op1:1 crn:0 crm:1 op2:0`, a read in the same `S2_` trace space as the `TRCIDR` words.

Run790 stopped in a QHEE loop after UEFI CPRDxe.
QHEE took 81 Undefined Instruction exceptions at the same EL2 address, image offset `0x6a96c`.
QEMU logged a read of the unsupported register `op0:3 op1:3 crn:14 crm:8 op2:6`, which is `PMEVCNTR6_EL0`.
The routine at `0x6a900..0x6a9cc` saves `PMEVCNTR0..11_EL0` and `PMEVTYPER0..11_EL0` without a test of `PMCR_EL0.N`.
The routine at `0x6a7c0..0x6a8f0` restores the same twelve pairs.
The firmware therefore assumes twelve event counters, and silicon with fewer counters would trap at the same instruction.
The max TCG core reports `PMCR_EL0.N` six.
Patch `0027-expose-pmu-counter-count.patch` adds the opt-in `x-qcom-pmu-num-counters` property, and `ApssCpu` requests twelve.
QEMU derives the counter registers, the counter masks, and the reset value of `MDCR_EL2.HPMN` from that field.
The counters count only the architectural events that QEMU implements.
After the QHEE loop, a secure FIQ entered QSEE, and QSEE parked CPU 0 in its fatal path at `0x15680264`.



`cargo test --locked --test platform -- --ignored` reports 78 passed.
The APSS group fixture checks every redistributor affinity and the final-only Last bit through this GIC.
The focused UART fixture `uart_transmits_exact_guest_bytes_and_delivers_irqs` also passes.

## ETE programmer words and first UEFI output

QHEE touches four trace registers and no others: `TRCPRGCTLR` (`S2_1_C0_C1_0`), `TRCSTATR` (`S2_1_C0_C3_0`), `TRCIDR2/4/5` (`S2_1_C0_C10/C12/C13_7`), and `TRBLIMITR_EL1` (`S3_0_C9_C11_0`).
These are the architectural ETE encodings.
Patch `0021-implement-ete-programmer-words.patch` adds them to the opt-in identification profile.
`TRCPRGCTLR` and `TRBLIMITR_EL1` retain their words, `TRCSTATR` reports `IDLE` as the inverse of `TRCPRGCTLR.EN` and `PMSTABLE` as one, and the identification words keep the values the firmware compares against.
`ID_AA64DFR0.TraceBuffer` stays zero, so the buffer words are retained without a buffer, and no trace output is produced.
`TRFCR_EL2` stays a retained store.
`docs/qcom/sm88xx/stubs.md` and `docs/qcom/sm88xx/decisions.md` record the substitute and the patch.

Run 470 records no unsupported system register access.
The UART log grows from 372 to 392 lines and now carries APPSBL:

```text
UEFI Start     [21567]
 - 0x0C6801000 [21603] Sec.efi
ASLR          : ON
DEP           : ON (RTB)
RAM Installed : 4096 MB (0x0100000000)
```

The guest therefore reaches the stock UEFI, which is the first application-processor output after SBL1.
Run 470 stopped at an unmodeled 64-bit write at `0xc7200008` from `pc=0xc6805560` inside `Sec.efi`.
That failure did not reproduce.
A later build with no functional change accepted the same write, and a probe showed the word reaching `platform.ddr_low` and returning a success response.
No `DARE_ACCESS_DENIED` line appeared in any run, so the LLCC data-access restriction did not refuse it.
The refusal had returned `TLM_COMMAND_ERROR_RESPONSE` without reaching any target, which matches only the attribute branch of the `AccessMonitor`.
The cause stays unknown, either a stale binary or a non-deterministic attribute difference, and this note records no fix.

## AMUv1 activity monitors and the real kernel entry

Run885 enters the real SM8850 kernel at EL1 under QHEE, non-secure EL2.
The kernel runs `__cpu_setup` at image offsets `0xc97736ec..0xc97736f8`.
It reads `ID_AA64PFR0_EL1.AMU` bits `47:44`, finds the activity-monitor
feature present under the opt-in identification profile, and executes
`msr amuserenr_el0, xzr`.
The pinned CPU defined `ID_AA64PFR0.AMU` for that profile but no AMUv1
system register behind it. The write took an Undefined Instruction
exception at EL1 and parked in UEFI's stale vectors.

Patch `0029-implement-qcom-amu.patch` adds the missing register set.
Silicon implements AMUv1 on every Oryon core, so the accepted fix is a
minimum functional model, not a narrower identification claim.
`docs/qcom/sm88xx/decisions.md` records the register set, the access control, and
the counting sources.

`apss_executes_mpam_pauth_and_rng` now also exercises this profile at
EL3, the highest implemented EL on `qcom,apss-cpu-v1`. It reads
`AMCFGR_EL0` (`0xb`), `AMCGCR_EL0` (`0x804`), and the four
`AMEVTYPER0<n>_EL0` constants. It enables `AMEVCNTR00_EL0` (`CPU_CYCLES`)
and `AMEVCNTR01_EL0` (`CNT_CYCLES`) and waits for each to advance. It
disables both and checks that a stopped counter keeps its value across a
bounded loop. It writes a marker to `AMEVCNTR02_EL0` (`INST_RETIRED`).
It then enables that counter and checks that the marker persists across a
bounded wait. A precise-icount run would advance the counter, so a pass
also shows that this run has no precise icount. It checks that
`AMEVCNTR03_EL0` (`STALL_BACKEND_MEM`) stays zero. EL3 keeps a write to a
disabled counter and to an auxiliary counter's event-type register. From
EL1, it checks that a write to `AMCNTENSET0_EL0` takes an Undefined
Instruction exception. It also checks that `CPTR_EL2.TAM` and
`CPTR_EL3.TAM` each trap an `AMCFGR_EL0` read. From EL0, it checks that
`AMUSERENR_EL0.EN` clear traps an `AMCFGR_EL0` read to EL1. It also
checks that `AMUSERENR_EL0.EN` set allows the same read. The fixture's
trap total moves from 38 to 44.

Run919 verifies the complete kernel hash and the first instruction at `0xc7800000`.
The APSS trace records the write at `0xc97736f8` and QHEE's return to EL1 at `0xc97736fc`.
Linux then reaches GENI initialization and writes DMA interrupt clear at `0xa9cc44`.
That unsupported UART access stops the platform before kernel UART output.
The full host suite reports 102 unit checks and 96 platform checks passed.

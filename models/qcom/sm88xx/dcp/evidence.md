# Selected DCP bootstrap hardware

## Static provenance

The selected XBL SHA-256 identities are:

- SM8850: `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
- SM8845: `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.

The DCP records are `0x148ec878` and `0x148ea838`. Both use security image ID `0x9d`.
Neither record has a direct preprocessor, postprocessor, cancellation, or jump callback.
The generic pipeline also resolves image-specific drivers after the authenticated load.
Its registry is `0x148dc0f0` / `0x148da0b0`, with five rows and stride `0x10`.
ID `0x9d` resolves constructors `0x1489a330` / `0x14898b34`.
The constructors install prepare at method `+8` and release at method `+0x10`.
Prepare runs at `0x1489a38c` / `0x14898b90`.
Release runs at `0x1489a628` / `0x14898e40`.
These functions establish the same DCP-local hardware contract for both SoCs.
The SM8845 prepare function additionally writes its DT pointer at `0x09ca0048`.

## Hardware behavior

`QcomDcpBootstrap` implements six independent, narrow register banks with one shared state.
`platforms/qcom/sm88xx/dcp-bootstrap.lua` maps them at absolute APSS addresses:

| Base | Socket | Selected use |
| --- | --- | --- |
| `0x09bab000` | `power_socket` | Bit 0 requests power collapse. Bit 31 reports power-on state. |
| `0x09bae000` | `branch_socket` | `+8` retains a guest branch-enable request. |
| `0x09bb4000` | `clock_socket` | Branch controls, root configuration/update, and processor reset state. |
| `0x09c70000` | `boot_socket` | `+0xc` retains the guest entry. `+0x10` retains the guest control word. |
| `0x09ca0000` | `dtb_socket` | SM8845 `+0x48` retains the guest DT address. |
| `0x09ca2000` | `config_socket` | Configuration and address-window records. |

Each bank occupies `0x1000` bytes. No broad DCP address aperture is installed.
The model starts with power collapsed, the processor halted, and all other state zero.
Guest writes, not host firmware files, supply the entry and configuration.
Unknown aligned word registers retain ordinary configuration writes.
Each access to an unknown register emits an exploratory-storage diagnostic.
Unknown registers have no inferred completion effect.
Malformed transfers fail through TLM.

Prepare clears collapse at `0x09bab000`. Its poll requires bit 31 to become one.
The virtual analog policy applies this power effect immediately after the request.
Prepare writes root configuration `0x101` at `0x09bb4014`, then requests UPDATE at `0x09bb4010`.
UPDATE clears only after power-on and the required source/divider fields are present.
An incomplete request remains pending.
Power collapse invalidates root completion but retains configuration words.

Release requires these effects before the controller can release the native CPU:

1. The power domain is on.
2. A root UPDATE completed with source/divider `0x101`.
3. Branch-enable bit zero is set at `0x09bae008` and `0x09bb4004/4008/402c/4030/4040`.
4. The guest wrote a nonzero entry at `0x09c7000c`.
5. Reset control at `0x09bb4030` has bit zero set and bits two/three clear.

Release first sets reset bits `0xc`, delays `0x12c`, then clears them and sets bit zero.
The poll at `0x1489a660` / `0x14898e78` waits for bit 31 to clear.
The controller clears bit 31 only after CPU reset release completes.
It sets bit 31 after reset assertion completes.
This status reports reset delivery, not firmware service readiness.
The CPU executes the guest-loaded DCP image. The controller produces no command replies or ready cookie.
It does not check signatures, issue grants, or change ACLs or loader results.

## Platform composition

The factory requires these registrations:

- `qcom,dcp-rv32-v1`: `qcom_vp::DcpCpu` from `models/qcom/sm88xx/dcp/cpu.h`, with constructor `(name, sc_object* qemu_instance)`.
- `qcom,dcp-bootstrap-v1`: `qcom_vp::QcomDcpBootstrap` from `models/qcom/sm88xx/dcp/bootstrap.h`, with constructor `(name, sc_object* cpu)`.

The CPU argument must identify a `DcpCpu`.
The reset controller requires exactly one reset consumer.
Bind its `reset` output to the CPU's inherited `reset` input.
All six register sockets remain required and use `relative_addresses=false`.
The CPU retains QBox's `initiator_socket` and 32 `irq_in` inputs.
The platform composition must connect the CPU to the existing guest-loaded backing.
No host image loader, copied firmware, or parking ROM supplies its instructions.

`platforms/qcom/sm88xx/dcp-runtime.lua` supplies the CPU instance and runtime connections.
The platform composition applies it before `platforms/qcom/sm88xx/dcp-bootstrap.lua`.
The bootstrap helper retains `p.apss_router` and its overlap checks.

Common clock operations remain outside this model:

- Guest RMW bit zero at `0x00127004` and `0x00127008`.
- The selected resource calls for `vdd_mmcx`.
- Preparation at `0x14899eac` / `0x14898824`, with base `0x0b7fea60`.
  It writes `+0xc0`, `+0x40`, `+0x80`, and the base word with intervening reads and delays.

The helper connects both shared branches to the existing boot reference.
It maps the four preparation words through retained boot controls.
These words retain actual guest writes from cold zero. They do not establish an authorization grant.

SM8850 run126 reaches the first shared branch from PC `0x1489a3e0`.
XBL constructs its address with low word `0x7004` and upper halfword `0x12`.
SM8845 constructs the same address at `0x14898bc8..0x14898bd0`.
These instructions correct the earlier mapping, which contained an extra hexadecimal zero.

## Native acceptance and unresolved service contract

The native hardware probe reaches its completion marker at `0x600d0000`.
It checks pending UPDATE before valid power/configuration, ordered release, reset assertion, and power-loss invalidation.
It also checks retained DCP configuration and the shared preparation words.
The probe uses synthetic guest code, not selected firmware bytes.

SM8850 run129 captures zero from both original prepare and release callbacks.
The entry must equal the authenticated guest loader's result, not a host-selected constant.
SM8850 selected DCP entry is `0xa00d0000`. SM8845 entry is `0xa4100000`.

Observe the first read after release from the DCP control region, shared DDR, or an interrupt controller.
Retain its PC, address, expected mask/value, and the producer that can satisfy it.
A halt-bit poll closes bootstrap hardware only.
A separate shared-memory or interrupt wait requires a real service contract before any reply can be modeled.

SM8850 run127 passes the corrected shared-clock accesses.
The UART then records subsequent callbacks through `boot_vsense_copy_to_smem`.
The next unsupported access is TLMM control `0x0f100010`, from PC `0x1489e908`.
This run verifies the corrected mapping, not DCP service execution or the selected entry value.

Run129 records guest entry `0xa00d0000` at the original store `0x1489a5dc`.
Prepare returns zero at `0x1489a618`, and release returns zero at `0x1489a68c`.
The retained evidence is `build/private/run-sm8850-common-runtime129/post-qup-probe.json`.
These results establish native hardware preparation and release, not DCP instruction execution.

## Native CPU reset lifecycle

`DcpCpu` derives from QBox `cpu_riscv32`.
It requires multithreaded TCG without coroutines.
It retains QBox's RISC-V interrupt sockets and reset registration.
Its configuration uses QEMU's bare RV32I model with M, A, C, Zicsr, Zifencei, and Zicntr.
QEMU architecture ID 42 identifies the substitute, not a physical E66 core.
The platform must enable PMP because both selected images access PMP registers.

The CPU remains cold-held at simulation start.
Its startup hook queues architectural reset without starting the quantum keeper or arming an execution deadline.
Its end-of-loop hook acknowledges each reset generation only after native reset completes and the CPU cannot run.
Atomic generation counters transfer this completion to the SystemC thread.
The shutdown hook permits QBox cleanup without releasing a cold CPU.

The bootstrap uses an `SC_THREAD` for reset delivery.
Power, clocks, entry, and reset-control changes schedule this thread.
Assertion waits for native completion before it reports the held state.
Before release, `prepare_reset_vector()` publishes the current guest entry through QEMU's I/O-thread property setter.
It then repeats architectural reset and waits for completion.
No operation writes the current PC directly.
The controller checks its generation again after each suspended operation.
A changed request cannot release a CPU with stale configuration.

First release starts the execution clock through QBox's reset-finish hook.
Only then does it remove the startup hold and arm the execution deadline under the QEMU lock.
Later assertion and power loss use the same native reset completion path.
The guest retains its reset pulse, delay, halt-status poll, and final write to `0x09c70010`.
The controller does not derive idle or power-down completion from reset release.

The native QSEE clock fixture passed with this cold-held CPU for both OEM compositions.
The integration command `make test` passed 47 host tests and 58 native tests.
SM8850 run190 executed the actual entry and reached the original DDR-to-DLS copy.
Run192 completed that copy and reached the missing upper interrupt controller.
No run proves DCP idle or kernel handoff.


## Shared QSEE gates

The QSEE descriptor group uses the existing DCP words `0x09bb4040` and `0x09bae004`.
The shared clock composition preserves their DCP owner rather than creating overlapping clock modules.
Both words retain the enable request and derive active-high `CLK_OFF` at bit 31.
The explicit virtual parent requires DCP power and completed root configuration.
This parent policy does not establish a physical mux or frequency.
Writes cannot forge the status bit.

The extended QSEE fixture exercises both gates through power removal and root configuration.
It checks that an enable request alone cannot report a running clock.
The focused native fixture passed for both selected OEM compositions.

## Display-domain collapse

Before native DCP execution, SM8850 run187 waited at QSEE source `0x15751db4` for `0x09bab004.bit15`.
SM8845 run147 waited at source `0x15744ad8` for the same bit.
Both runs then reached the original watchdog deadline.

QSEE cold-init stage 3 at `0x156e5b24` calls `0x156e57cc`, which calls `0x1575094c`.
That function selects the chip-specific operation `0x15751b9c` through `0x15751ab4`.
Stage 4 at `0x156e5b28` calls the opposite operation `0x15751f30`.
The stage-3 operation reads socinfo SMEM item `0x89`.
Subset part 4 selects display. A clear bit zero runs the display block.
That block waits for `0x09bab004` bit 15 before it clears HW_CONTROL and SW_COLLAPSE.
It then waits for bit 16.
Thus QSEE expects the display domain to be off after XBL has released the DCP.

`0x09bab000` is the display MDSS core GDSC at display clock controller offset `0x9000`.
Bit 0 is SW_COLLAPSE, bit 1 is HW_CONTROL, and bit 31 reports power.
In `0x09bab004`, bit 15 reports completed power-down and bit 16 reports completed power-up.
XBL `0x1489a3f4` clears only bit 0.
No decoded XBL, QSEE, EL3, or hypervisor path sets bit 1.

The DCP sets HW_CONTROL itself.
SM8850 `0xa00d8d90` and SM8845 `0xa4105e5a` load the mapped display clock base `0x103aa000`.
They read GDSCR at `+0x1000`, OR bit 1, and store it at SM8850 `0xa00d8dba` or SM8845 `0xa4105e82`.
DCP `0x103ab000` is system `0x09bab000`.

The deep-sleep path then changes core clock `disp_cc_dcp_proc_h_clk` at `0x09bb4030`.
SM8850 `0xa00eaba6` and SM8845 `0xa4116d50` set bit 14.
SM8850 `0xa00eabae` and SM8845 `0xa4116d58` set bit 1.
The DCP clock descriptors at SM8850 `0xa02114b8` and SM8845 `0xa423cc78` name this branch.
Bit 14 is memory-core retention, and bit 1 is hardware clock gating. Both readings are inferences from Qualcomm CBCR definitions.
The path then enters WFI at SM8850 `0xa00eabe0` or SM8845 `0xa4116d8a`.

The selected images program CESTA instructions and RSC sequences, but they do not reveal the instruction decoder or the signal map.
`QcomDcpBootstrap` therefore applies a functional policy.
Patch `0016-expose-riscv-wfi-output.patch` pulses the DCP `qcom.wfi` output at each executed WFI.
The domain collapses only when all of these conditions hold at that pulse:

1. Reset is released and the domain has power.
2. GDSCR HW_CONTROL is set.
3. The core clock has bits 14 and 1 set.

The collapse reports bit 15, clears GDSCR bit 31, and logs `DCP_DISPLAY_DOMAIN_COLLAPSE`.
It does not reset the DCP.
The DCP clock groups use the separate `a000` and `4000` reset groups, outside the MDSS core GDSC.
That classification is an inference from the DCP clock descriptors in `models/qcom/sm88xx/dcp/clocks-evidence.md`.
The DCP therefore stays in WFI until an interrupt wakes it.
A write that clears HW_CONTROL returns the domain to software control.
If SW_COLLAPSE is clear, the GDSC reports power again.
SW_COLLAPSE still removes the DCP supply, because XBL clears it before release.
The model does not execute the sleep or wake TCS commands of the display RSC.

## Core clock pages

`QcomDcpBootstrap` stays the single owner of system pages `0x09bab000`, `0x09bae000`, and `0x09bb4000`.
[The DCP clock evidence](clocks-evidence.md) names each word in these pages and its parent.

The core page holds four roots at offsets `0x10`, `0x44`, `0x60`, and `0x7c`.
Each root has CMD, CFG, M, N, and D words.
An UPDATE request completes only when the DCP supply is on, the divider field is not zero, and the selected source runs.
Selector 0 is XO and selector 1 is GPLL0. XBL selects GPLL0 before release, so the model treats it as running.
Selector 4 is display PLL1 main output. It runs only when the composed display PLL bank reports output one.
CMD bit 31 reports a stopped root.

Each branch reports CLK_OFF in bit 31 from its own enable bit, the DCP supply, and its parent.
External parents are the display AHB, vsync, and timer roots composed by `platforms/qcom/sm88xx/dcp-clocks.lua`.
Fixtures without these parents use the DCP core root as their parent.
The processor branch at `0x09bb4030` keeps its earlier contract. Its bit 31 reports completed reset delivery.
SW_COLLAPSE clears all four root completions.

## Native memory and interrupt composition

Both selected DCP images contain an ELF at image offset `0x16000`.
SM8845 `RADIO/dcp.img` SHA-256 is `2556992b799d102bad74235025e675c0620e957719fe18f92f14fce5789eb766`.
Its entry is `0xa4100000`.
Its FDT starts at ELF offset `0x8b000`, VA `0xa4244000`.

The SM8850 FDT declares DTIM at `0xa00d0000+0x300000`.
The SM8845 FDT declares DTIM at `0xa4100000+0x300000`.
Each native view reaches the existing protected DDR owner through an address translator.
The view preserves DARE checks and transaction attributes.
It does not borrow APSS requester authority or access raw backing storage.
The current DARE model checks addresses, not a new DCP requester identity.

SM8850 also declares independent DLS at `0x00400200+0x3fe00`.
Its cold routine at `0xa0124652` copies `0x2b00` bytes from DDR `0xa00d1000` to DLS `0x401000`.
The routine jumps through `0xa012193e` to memcpy at `0xa01270dc`.
The model does not alias or prepopulate this destination.
The first partial page disables DMI, and the remaining RAM uses a page-aligned mapping.
This split prevents QEMU's direct mapping from confusing guest and host page offsets.

Both FDTs identify the same interrupt geometry.

| Component | Native address | Contract |
| --- | --- | --- |
| CLINT software input | `0x100000+0x4000` | QBox SWI, hart zero, machine software IRQ 3 |
| CLINT timer | `0x104000+0xc000` | QBox ACLINT, compare offset zero, counter offset `0x7ff8`, IRQ 7 |
| PLIC | `0x800000+0x400000` | 52 external sources, seven priorities, one machine context |
| Qualcomm timer | `0xe01000+0x1000` | Existing timer frame, 19.2 MHz, local IRQ 27 |
| System timetick | `0xe02000+0x1000` | Existing timer frame, 19.2 MHz, local IRQ 12 |
| Wake timetick | `0xe03000+0x1000` | Existing timer frame, 19.2 MHz, local IRQ 13 |

The PLIC configuration includes reserved source zero, so QEMU receives `num_sources=53`.
The CLINT retains QBox's default 10 MHz frequency as an explicit virtual policy.
Neither selected FDT supplies its frequency.
The separate Qualcomm timer frequency does not establish the CLINT frequency.

SM8850 run192 ends at PC `0xa011c4ea` while it reads upper interrupt type word `0xe10210`.
The monitor reports `UNMODELED_ACCESS` rather than supplying a status value.
SM8845 run148 reaches a separate QEMU decoder assertion at PC `0xa414dffc`.
Its compressed instruction uses an I/O fetch because the native view has no DMI grant.
The subsequent page-end probe records an already fetched halfword.
The public `riscv_io_fetch_preserves_compressed_page_boundary` fixture reproduces this assertion without OEM firmware.

## Upper interrupt conditioning

Both FDTs contain `/soc/interrupt_conditioning` with compatible `qcom,interrupt-conditioning`.
Its base is `0xe10000`.
It names these offsets: PLIC mask `0x000`, PLIC type `0x100`, PLIC clear `0x180`, local mask `0x200`, local type `0x210`, and local clear `0x218`.
Thus `0xe10210` is a type register, not pending status.

| Effect | SM8850 PCs | SM8845 PCs |
| --- | --- | --- |
| Read PLIC type `e10100+4*((ID-1)/32)` | `a011c4c8`–`a011c4dc` | `a414acc0`–`a414acd4` |
| Read local type `e10210` | `a011c4e6`–`a011c4ea` | `a414acde`–`a414ace2` |
| Accept IRQ21 through IRQ25 and select `e10218` | `a011c4ee`–`a011c4f8` | `a414ace6`–`a414acf0` |
| Check type, clear MIE, write W1C bit, restore MIE | `a011c4fc`–`a011c50e` | `a414acf4`–`a414ad06` |
| Clear both external latch words | `a00eab94`–`a00eab98` | `a4116d3e`–`a4116d42` |
| Copy shifted PLIC enables into external masks | `a00eaec2`–`a00eaeda` | `a411706c`–`a4117084` |
| Write `mie >> 16` into the local mask | `a00eaf02`–`a00eaf0c` | `a41170ac`–`a41170b6` |
| Write zero to unexplained word `e11104` | `a00eaf14` | `a41170be` |
| Read external observation words `e10080` and `e10084` | `a00ead66`, `a00ead76` | `a4116f10`, `a4116f20` |

`qcom,dcp-interrupt-conditioning-v1` implements bounded masks, types, edge latches, and W1C clears.
A latch sets only on a real rising source edge.
A level source follows its input, and a clear cannot remove an asserted level.
Type, mask, and read operations never create an interrupt.
The wake outputs are an inference from the PM callers.
The model therefore observes sources and does not gate normal CPU or PLIC delivery.
Timer IRQ27 remains direct to the CPU.
QBox signal ports accept one consumer, and no current reader uses local observation state.
Local source 11 therefore stays unbound until a real consumer needs that mirror.
Timer IRQ12 and IRQ13 remain direct and outside this bank.
Zero edge types select an all-level functional reset policy.
Neither FDT specifies silicon reset types, and no direct instruction writes either type bank.

## SiFive cache operations

Both selected images contain these SiFive cache instructions:

| Word | Instruction | Scope |
| --- | --- | --- |
| `0xfc000073` | `CFLUSH.D.L1` | All lines |
| `0xfc050073` | `CFLUSH.D.L1` | The line at `x10` |
| `0xfc200073` | `CDISCARD.D.L1` | All lines |
| `0xfc250073` | `CDISCARD.D.L1` | The line at `x10` |

The [Vic_U7 manual section 9.1](https://www.scs.stanford.edu/~zyedidia/docs/sifive/sifive-vic-u7.pdf) defines CFLUSH with an optional rs1 address.
The [U54 manual sections 6.1 and 6.2](https://sifive-china.oss-cn-zhangjiakou.aliyuncs.com/Standard%20Core%20IP/u54_core_complex_manual_21G2.pdf) define CDISCARD and the M-mode restriction.
The U54 manual rejects addressed CFLUSH, but the Vic_U7 manual supports it.
The patch selects the line-capable form because the selected DCP code uses it.
No public E66 manual identifies the exact E66 variant.

Patch `0015-implement-sifive-cache-operations.patch` adds the default-false `x-sifive-cache-operations` property.
`DcpCpu.cache_operations` enables it by default.
Both operations require M-mode.
An addressed operation probes store permission at the operand byte, as the manuals describe for partial-line PMP.
Coherent QEMU memory has no private dirty lines, so neither operation changes RAM.

The `dcp_reset_gates_cache_operations_and_preserves_pmp` fixture checks three cases.
A synthetic DCP must not run before its controller supplies power, clocks, root configuration, and the reset request.
With the property disabled, each instruction raises an illegal-instruction exception.
With a locked read-only PMP region, each addressed form raises a store-access fault, and memory stays unchanged.

Patch `0014-stop-riscv-io-lookahead.patch` skips the page-end lookahead after the translation limit.
An I/O instruction fetch sets that limit to one instruction.

## System windows and local devices

Both FDTs declare `qcom,dcp-csr` at `0x1e40000+0x80000` with `dtb-address-offset=0x60048`.
They declare `qcom,rvcp-remapper` banks at `0x1ea2080+0x30` and `0x1ea2280+0x30`.
XBL writes the same words at `0x09ca0048`, `0x09ca2080`, and `0x09ca2280`.
`platforms/qcom/sm88xx/dcp-runtime.lua` maps only these three DCP views to the existing APSS-side owner.

SM8850 XBL `0x1489a490`–`0x1489a4d0` writes seven window-one entries at `0x09ca2080`–`0x09ca2098`.
Their values are `0x00000001`, `0x08000001`, and so on through `0x30000001`.
Each entry covers 128 MiB of DCP addresses from `0x40000000`.
Bit zero enables the entry, and bits 31:27 select the system target.
`QcomDcpBootstrap.remap_socket` applies this translation and forwards the access to the APSS system router.

A disabled entry passes its address unchanged.
This behavior is an inference from two facts.
Both trees declare `mem_port@80000000` with size `0x80000000`.
XBL enables entries zero through six only, so entries 8 through 11 must still reach DDR.
SM8845 run153 needed that pass-through: its DCP read DDR at `0x81eff3b0` through entry 8.

Both trees declare `qcom,sifive-beu` at `0x5000`.
SM8845 code at `0xa414e228` writes `0x22` to its `enable` and `local_interrupt` bytes.
The [U54 manual chapter 11](https://sifive-china.oss-cn-zhangjiakou.aliyuncs.com/Standard%20Core%20IP/u54_core_complex_manual_21G2.pdf) defines six eight-byte registers at offsets `0x00` through `0x28`.
Value `0x22` selects events 1 and 5, which are instruction and data bus errors.
The model maps those `0x30` bytes as cold-zero storage.
No modeled bus or ECC error exists, so cause, value, and accrued stay zero unless software writes them.

Each DCP reset assertion discards that instance's translated code.
APSS can publish a new image through its own direct mapping while the controller holds reset.
That write cannot invalidate translations in the separate DCP instance.

DCP addresses `0x10000000` through `0x103fffff` reach system `0x09800000` through `0x09bfffff`.
The DCP deep-sleep writes to `0x103b4030`..`0x103b4040` use the same bits as XBL's writes to `0x09bb4030`..`0x09bb4040`.
The DCP clock descriptors also place `disp_cc_dcp_proc_h_clk` at display clock offset `0xa030`.

The DCP-visible owners behind these windows are:

| Owner | System address | Evidence |
| --- | --- | --- |
| IPCC MPROC view, client 14 | `0x0110e000` | Tree `ipcc@4110e000`, PLIC source 4 |
| IPCC COMPUTE_L0 view, client 11 | `0x011cb000` | Tree `ipcc@411cb000`, PLIC source 2 |
| IPCC COMPUTE_L1 view, client 11 | `0x0128b000` | Tree `ipcc@4128b000`, PLIC source 3 |
| IPCC FENCE view, client 11 | `0x0140b000` | Tree `ipcc@4140b000`, PLIC source 1 |
| Display RSC DRV0 | `0x09bc2000` | Tree `power_rpmh`, `qcom,display-rsc-v1` |
| Display PDC DRV 10 | `0x0b2a0000`, `0x0b5f9810` | Global DRV 10, `qcom,display-pdc-v1` |
| Display CESTA banks | `0x09bcc000`..`0x09bcf000` | Tree `cesta@103cc000`, `qcom,cesta-v1` |

Each IPCC view joins the existing SOCCP protocol fabric. Its client count comes from the tree's client-mapping length.
[models/qcom/rpmh/evidence.md](../../rpmh/evidence.md) documents the RSC, PDC, and CESTA registers.

The DCP also uses two local watchdog words.
SM8850 `0xa00ef798`..`0xa00ef7ba` writes a timeout to `0xe07008` and sets enable bits in `0xe07000`.
SM8850 `0xa00ef778` and `0xa00ef7d8` pet through `0xe05000`.
SM8845 `0xa411b970` performs the same pet write.
No tree names a counter clock or expiry action.
The model therefore retains these words and never expires.


## Display initialization after clocks

This section uses static RV32 decoding of both selected `RADIO/dcp.img` files.
Each image contains its ELF at file offset `0x16000`.
No OEM execution or physical device access supplies the new evidence.

| Image | SHA-256 |
| --- | --- |
| SM8850 | `5863d9b9826426b507b194f7461c781ab7fe9a41d10063d410db20eee5cdd551` |
| SM8845 | `2556992b799d102bad74235025e675c0620e957719fe18f92f14fce5789eb766` |

The first unmapped SM8850 write is an interrupt-disable operation.
It is not a PLL enable, command transfer, or panel probe.
The generic writer PC alone does not identify the caller's operation.

### Shared address inventory

Both embedded FDTs declare the following ranges.
The system translation subtracts `0x06800000` from each DCP address.

| FDT register name | DCP base and size | System base | Ownership in this change |
| --- | --- | --- | --- |
| `mdp_phys` | `0x10000000+0x1c9000` | `0x09800000` | Not mapped |
| `vbif_phys` | `0x10316000+0x2008` | `0x09b16000` | Not mapped |
| `regdma_phys` | `0x10360000+0x10880` | `0x09b60000` | Not mapped |
| DSI controller 0 | `0x102c0000+0x1000` | `0x09ac0000` | Only `+0x110` accepts accesses |
| DSI controller 1 | `0x102c3000+0x1000` | `0x09ac3000` | Only `+0x110` accepts accesses |
| DSI PHY 0 | `0x102c1000+0xa00` | `0x09ac1000` | Not mapped |
| DSI PLL 0 | `0x102c1500+0x400` | `0x09ac1500` | Not mapped |
| DSI PHY 1 | `0x102c4000+0xa00` | `0x09ac4000` | Not mapped |
| DSI PLL 1 | `0x102c4500+0x400` | `0x09ac4500` | Not mapped |
| Shared `disp_cc_misc_cmd` | `0x103b1000+4` | `0x09bb1000` | Not mapped |

The PHY declarations include their PLL subranges.
Do not create overlapping bus owners for these ranges.
The controller compatible is `qcom,dsi-ctrl-hw-v2.10`.
The PHY compatible is `qcom,dsi-phy-v7.2`, with `pll-label=dsi_pll_3nm`.
These tree declarations establish addresses, not reset values or completion behavior.

### Cold interrupt sequence

The following sequence does not require a panel configuration.
The native catalog constructs one DSI interrupt descriptor per controller.
Each descriptor uses offset `0x110` for clear, enable, and status.

| Order | Operation | SM8850 PC | SM8845 PC |
| --- | --- | --- | --- |
| 1 | Construct each controller's interrupt object | `0xa0110ad0` calls `0xa010a792` | `0xa413c06a` calls `0xa4135f0c` |
| 2 | Call the disable-all routine | `0xa010a862` calls `0xa010a55e` | `0xa4135fdc` calls `0xa4135cd8` |
| 3 | Write zero to each descriptor's enable offset | `0xa010a584` | `0xa4135cfe` |
| 4 | Call the clear-all routine | `0xa010a866` calls `0xa010a50c` | `0xa4135fe0` calls `0xa4135c86` |
| 5 | Write `0x55115505` to each descriptor's clear offset | `0xa010a53c` | `0xa4135cb6` |
| 6 | Execute the I/O fence after each loop | `0xa00fea50` | `0xa4128d7e` |

The write call uses a function pointer through the common regmap interface.
Its complete low-level chain is:

| Layer | SM8850 PC | SM8845 PC |
| --- | --- | --- |
| Common write dispatcher | `0xa00fea06` | `0xa4128d34` |
| Write adapter | `0xa00fea78` | `0xa4128dbc` |
| Add descriptor base and register offset | `0xa00f71e0`–`0xa00f71e2` | `0xa411fab0`–`0xa411fab2` |
| Store the guest value | `0xa00f71e4` | `0xa411fab4` |
| Common read dispatcher | `0xa00fea10` | `0xa4128d3e` |
| Load the register value | `0xa00f71ec` | `0xa411fabc` |

The constructor's two loops contain no MMIO poll or timeout.
They iterate over descriptors, not completion state.
Neither loop starts a DSI engine.

### Later conditional access sequences

These paths require later display operations.
They are not one unconditional sequence after clock initialization.
Offsets in each row are relative to the named regmap.
The table preserves access order within each row.

| Path | Ordered accesses | SM8850 PCs | SM8845 PCs |
| --- | --- | --- | --- |
| MDSS UBWC configuration | Read `+0x58`, then write `+0x144`, `+0x150`, `+0x154` | `0xa0102a84`, `0xa0102afa`–`0xa0102b1a` | UBWC counterpart remains unresolved |
| MDP control-path reset | Write `+0x30=1`, then poll `+0x30[0]` clear | `0xa0104c62`, `0xa0104cee` | `0xa412f88a`, `0xa412f918` |
| PHY enable gate | Write `+0x1ac=1`, delay 500, then poll `+0x140[0]` set | `0xa0108bf8`–`0xa0108c3a` | `0xa4134108`–`0xa4134142` |
| PHY configuration, mode 1 | Write `24,3c,1c,114,18,20,110,10c,ec,104,108,f4,f8,100,24,a0` | `0xa0108c52`–`0xa0108d30` | `0xa413415c`–`0xa413423e` |
| PHY configuration, other mode | Write `24,3c,1c,114,20,110,30,10c,ec,104,f4,f8,100,24,a0,2c,1b0,14` | `0xa0108d66`–`0xa0108e68` | `0xa4134276`–`0xa413437c` |
| PHY disable | Read `+0x3c[0]`, write `+0x1ac=0`, then update `+0x24` and `+0xa0` | `0xa0108e9c`, `0xa0108efe`–`0xa0108f3c` | `0xa41343b8`, `0xa413441a`, remaining updates unresolved |
| PLL main program | Write `bc,e0,e4,e8,ec,158,194,250,248,258`, one computed offset, then `240,18,4,10,20,24,44,68,78,a8,b8,c8,160,168,170,170,178,190,90,90,94,94,260,260` | `0xa0109c38`–`0xa0109e1c` | `0xa41353b2`–`0xa4135596` |
| PLL SSC program | Write `120,124,128,12c,130,134,150` | `0xa0109e4a`–`0xa0109e96` | `0xa41355c4`–`0xa4135610` |
| PLL enable | Write PHY `+0x3c=1`, then poll PLL `+0x1b0[0]` set | `0xa010a072`, `0xa010a0e4` | `0xa41357ec`, `0xa413585e` |
| PLL disable | Write PHY `+0x3c=0`, then clear dependent state | `0xa010a0a8`–`0xa010a0c6` | `0xa4135822`–`0xa4135840` |

The hexadecimal PHY and PLL lists omit the `0x` prefix.
The PHY `+0x110` writes are distinct from controller interrupt writes.
The current model does not accept those PHY writes.

### Decoded poll conditions and failure paths

Delay counts below are native call arguments.
This analysis does not establish their physical time unit.

| Consumer | Condition and bound | Failure path | SM8850 PCs | SM8845 PCs |
| --- | --- | --- | --- | --- |
| MDP control-path reset | `+0x30[0]` clears, at most `0xfa0` reads, delay argument 5 | Poll returns 1, caller returns `-22` | `0xa0104cd6`–`0xa0104d0a`, caller `0xa0104c66`–`0xa0104c86` | `0xa412f8fe`–`0xa412f934`, caller around `0xa412f88e` |
| MDP interface write pointer | `+0x2b0 & mask` becomes nonzero, bound `argument/100`, delay argument 100 | Return `-116` | `0xa0103c70`–`0xa0103cdc` | `0xa412e7d2`–`0xa412e83e` |
| DSI MISR collection | Only when `+0x364[8]` is set, poll `+0x364[9]`, 200 attempts, delay argument 5 | Return `-62`, not a panel result | `0xa010838a`–`0xa01083f2` | `0xa4133650`–`0xa41336b8` |
| PHY reference readiness | `+0x140[0]` sets, 200 attempts, delay argument 5 | SM8845 returns 6. SM8850 returns without executing the configuration writes | `0xa0108c1c`–`0xa0108c3c` | `0xa4134120`–`0xa4134144`, return `0xa413438e` |
| PLL lock | PLL `+0x1b0[0]` sets, 50 attempts, delay argument 100 | Return 6 and log the failure | `0xa010a0d8`–`0xa010a116` | `0xa4135852`–`0xa4135890` |
| SM8845 PHY lane idle | PHY `+0x14c & mask == mask`, 10 attempts, delay argument 10 | Log failure, remaining return semantics unresolved | No equivalent decoded | `0xa4134638`–`0xa4134686` |
| MDP interrupt wait | Read descriptor status and select the descriptor's bit mask | Optional polling loop has no local timeout | `0xa01053fe`–`0xa0105426` | `0xa4130008`–`0xa4130030` |

The MDP interface mask is `0xffff` or `0xffffffff`, from a configuration flag.
The SM8845 lane-idle mask combines the requested low four bits with `0x10` or `0x30`.
The interrupt wait's caller must supply any event timeout.
No bound should be invented inside a register model.

### Panel decision and substitute boundary

The display FDT describes hardware, clocks, supplies, and PHY lane parameters.
It does not declare a connected panel.
The panel-init handler unpacks HFI key/value data from its request.
SM8850 dispatch `0xa00f8762` calls handler `0xa00f896c`.
The handler calls the unpacker at `0xa00f8982`.
SM8845 handler `0xa4121d86` calls its unpacker at `0xa4121d9c`.
The SM8850 selector at `0xa00f8750`–`0xa00f8762` accepts panel operation IDs `0x03000001` through `0x03000003`.
The earlier table handles IDs `0x02000001` through `0x0200000d`.
Thus panel parameters come through HFI, rather than the cold interrupt constructor.
The exact APSS producer and panel-presence policy remain unresolved.

`qcom,dsi-interrupts-v1` implements only the disconnected interrupt word.
Its enable bits retain the guest's request.
Event bits have W1C semantics and remain zero because no engine or panel supplies events.
The native clear mask is `0x55115505`, and its paired enable mask is `0xaa22aa0a`.
Reserved bits read zero under explicit virtual policy.
Each controller has independent state, and cold reset clears its enables.
Writes, reads, and mask changes cannot create completion events.
Other controller offsets return address errors.

The [Linux DSI register database](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/msm/registers/display/dsi.xml) documents paired event and mask bits.
Its legacy `INTR_CTRL` offset is `0x10c`.
The selected OEM catalog establishes `0x110` for these controllers.
The OEM clear mask also covers events outside that older public subset.

No virtual panel, ACK, frame completion, DMA completion, PHY readiness, or PLL lock is supplied.
A no-panel path is not yet proven safe for the full boot.
Returning success from those later paths would exceed the decoded contract.

### Integration and focused regression

`src/platform.cc` registers `qcom_vp::QcomDsiInterrupts` as `qcom,dsi-interrupts-v1`.
`platforms/qcom/sm88xx/dcp-runtime.lua` calls `platforms/qcom/sm88xx/dcp-display.lua` after it assigns the display window.
The helper binds two controller pages through the existing APSS router.
It does not create a second DCP window or bypass its access monitor.

The native fixture is `tools/qcom/qcom-vp/tests/fixtures/dsi-interrupts/consumer.cpp`.
`dsi_interrupt_configuration_cannot_create_completion` runs it in the integration suite.
It checks the OEM disable/clear sequence, retained masks, independent controllers, cold reset, rejected offsets, and malformed transfers.
It also checks that writes cannot manufacture any pending event.

### Unresolved active-display branches

This change closes the first unmapped interrupt access, not the full display path.
The following branches still require evidence and models:

- Resolve the APSS HFI producer, its requested panel properties, and its handling of missing-panel failures.
- Decode controller command submission, DMA memory effects, readback packets, ACK errors, and event delivery before enabling command completion.
- Decode the remaining MDP, VBIF, register-DMA, and `disp_cc_misc_cmd` accesses.
- Resolve the computed PLL register offset and all prerequisites for PHY readiness and PLL lock.
- Resolve the SM8850 PHY timeout return and its caller's response.
- Decode SM8845 idle, ULPS, dynamic refresh, and the remaining interrupt wait callers.
- Establish the complete cross-request ordering from native runs after integration.

## Idle path after interrupt initialization

Both selected images reach the DCP idle path after the cold DSI interrupt setup.
No HFI panel request occurs before that idle path in the current native runs.

| Operation | SM8850 PC | SM8845 PC | Contract |
| --- | --- | --- | --- |
| Store the next power-state code | `0xa00ea58c` | `0xa4116736` | Write `0`, `6`, or `0xf` to CSR `0x01e80000` |
| Copy conditioner words into a log record | `0xa00eafc0..0xa00eb02c` | `0xa411716a..0xa41171d6` | Read offsets `0`, `0x80`, `0x84`, `0x208`, and `0x280` |

The codes follow the Zephyr states ACTIVE, RUNTIME_IDLE, and SUSPEND_TO_RAM.
`dcp-runtime.lua` retains the code in a four-bit word. The word does not select or report a transition.
The log copy has no decoded consumer. The conditioner therefore retains `0x280` under its existing exploratory policy.

The GCC PLL vote alias at `0x15a020` uses absolute addresses.
Without that setting, the router passed a relative offset and the translator rejected the transaction.

SM8850 run220 and SM8845 run176 then report `DCP_DISPLAY_DOMAIN_COLLAPSE source=armed-wfi`.
QSEE continues after that collapse. These runs do not establish an active display or kernel entry.

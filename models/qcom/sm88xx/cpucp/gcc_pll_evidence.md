# CPUCP GCC PLL dependency

## Identification

The fault at CPUCP PC `0x3361d4` reads a PLL MODE register at `0x00100000`.
It does not read a message transport.
There is no peer handshake, DMA operation, or firmware authentication at this dependency.

The input is the same `pbl8850.bin` identified in `models/qcom/sm88xx/cpucp/evidence.md`.
CPUCP PC equals raw file offset plus `0x300000`.
The existing diagnostic log is `build/private/diagnostic-cpucp-functional.log`.
This work did not repeat that failed run.

The ROM gives the following evidence:

| ROM address | Meaning |
| --- | --- |
| `0x336c50` | First 64-byte PLL descriptor. Base `0x100000`, vote register `0x152020`, vote mask `1`. |
| `0x336c90` | Second descriptor. Base `0x104000`, same vote register, vote mask `0x10`. |
| `0x337390` | Function table: enable `0x336104`, status `0x3361d0`, configuration `0x3361e8`. |
| `0x335e52` | Dispatches the descriptor's status function through its function table. |
| `0x3361d0`–`0x3361e6` | Tests MODE against `0x20000004`. This tests ENABLE_STATUS and RESET_N, not lock detection. |
| `0x336138`–`0x336150` | Sets RESET_N, enables main/even outputs at `+0x18`, then sets the shared vote bit. |
| `0x33615c`–`0x336182` | Polls ENABLE_STATUS and RESET_N after the vote. |
| `0x336188`–`0x3361b6` | Direct path: OPMODE=0, RESET_N=1, OPMODE=1, poll MODE bit 31, enable outputs, set OUTCTRL. |
| `0x33622e`–`0x33625c` | Holds MODE in reset while programming L at `+0x10` and alpha at `+0x14`. |
| `0x336260`–`0x3362d4` | Programs USER_CTL at `+0x18`, including pre/post dividers and ENABLE_VOTE_RUN bit 25. |
| `0x335e90`–`0x335ec0` | Writes offset/value pairs relative to the PLL base. Skips sentinel `0xdeadc0de`. |
| `0x336f58` | Initial pairs: CONFIG_CTL `+0x20=0x25c400e7`, CONFIG_CTL_U `+0x24=0x0a8060e0`, CONFIG_CTL_U1 `+0x28=0xf51dea20`, TEST_CTL `+0x2c=0`, TEST_CTL_U `+0x30=0`, USER_CTL `+0x18=9`, USER_CTL_U `+0x1c=2`. |
| `0x337040` | First frequency descriptor: 600000000 Hz, L=31, alpha=`0x4000`. With the 19200000-Hz reference, `(31 + 0.25) * 19200000 = 600000000`. |
| `0x337010` | Second frequency descriptor: 806000000 Hz, L=41, alpha=`0xfaaa`. |

The pinned [Canoe GCC driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/gcc-canoe.c)
identifies its GPLL0 and GPLL4 as `CLK_ALPHA_PLL_TYPE_TAYCAN_EKO_T`.
Their GCC-relative offsets are `0` and `0x4000`.
The shared vote offset is `0x52020`, with bits 0 and 4.
These addresses match the observed ROM descriptors.
This older topology supports the functional register model, not an independent Kaanapali silicon identification.

The pinned [PLL header](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/clk-alpha-pll.h)
aliases the Taycan EKO T register type to Taycan ELU and its operations to Lucid EVO.
The corresponding [implementation](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/clk/qcom/clk-alpha-pll.c)
places L, alpha, and USER_CTL at `0x10`, `0x14`, and `0x18`.
Its enable operation distinguishes vote-run bit 25 from direct OPMODE RUN and global OUTCTRL.
GCC defines the even postdivider at USER_CTL bits 13:10. Encoding 1 selects divide-by-two.
The GPL driver supplies register evidence only. No GPL implementation was copied into this model.

APSS ROM `0x313f08-0x313f10` and `0x313f84-0x313f8c` enable USER_CTL bits 0 and 1 together.
ROM `0x3140b0-0x3140d8` accepts even divisors 2, 4, and 8 and writes `(divisor - 1) << 10`.
Its first GPLL0 frequency descriptor at `0x31d9f8` selects 600 MHz and an even divisor of 2.
These instructions confirm the divider encoding independently of an advertised root rate.

The compatible key names the implemented Taycan register contract.
The available Kaanapali DT names its GCC but does not establish the underlying PLL type.

## Implemented contract

`gcc_pll.h` defines `qcom_vp::QcomGccPll`.
Use compatible key `qcom,gcc-taycan-eko-t-pll-v1` for this IP version. There is no Rivian compatibility alias.
The target socket exposes PLL banks at a `0x1000` stride.
Each bank has its own configuration, acquisition deadline, and outputs.

The explicit `pll_count` selects one to twelve banks.
Vote bit N controls bank N.
It accepts aligned 32-bit little-endian accesses.
It rejects byte enables, other widths, unsupported commands, and unknown offsets.
It does not grant DMI.

Each PLL retains these registers:

| Local offset | Meaning |
| --- | --- |
| `0x00` | MODE controls. Status bits 31–29 are read-only and derive from PLL state. |
| `0x04` | OPMODE: STANDBY=0 or RUN=1. Other modes fail. |
| `0x10` | L multiplier and retained calibration field. |
| `0x14` | Fractional alpha multiplier. |
| `0x18` | USER_CTL, including vote mode and output enables. |
| `0x1c` | USER_CTL_U. |
| `0x20`, `0x24`, `0x28` | Configuration words written by the ROM. |
| `0x2c`, `0x30`, `0x34` | Test words. |

A PLL starts only when RESET_N is set and L is nonzero.
It also requires direct RUN or an enabled vote interface with its vote asserted.
ENABLE_STATUS reports this local running state.
LOCK_DET and fine lock assert after a functional acquisition delay of 5 microseconds.
This delay is virtual hardware policy, not a measured silicon value.
Changes to L, alpha, or the pre-divider restart acquisition.
Output-gate and postdivider writes propagate without restarting oscillator lock.
Reset assertion, STANDBY without a vote, or removal of the last modeled vote stops the PLL.
Stopped PLLs cannot report lock.

`main_output_hz(index)` exposes the actual configured main frequency to future clock consumers.
It returns zero before lock or when the output is disabled.
It accounts for L, alpha, and the pre-divider.
`even_output_hz(index)` applies the independent USER_CTL bit-1 gate and divides by `(USER_CTL[13:10] + 1)`.
It does not require the main-output bit-0 gate.
Both outputs require lock and the same global direct-output or vote-run condition.
PLL writes and lock events propagate source changes without polling.
It accepts the ROM's alpha format only.
M/N format, dynamic latch commands, and FREEZE fail instead of receiving false acknowledgments.
Other vote bits remain readable but do not create unimplemented PLLs.

Cold construction clears all retained words and votes.
`cold_reset()` restores that state.
Do not call it for a CPU-only reset.
The CPUCP architectural reset does not reset GCC.
The model does not modify guest memory, publish firmware, release CPUs, or report any external command complete.
Lock detection represents only local clock acquisition.
It does not assert that downstream branch clocks or consumers are operational.

## Platform wiring

`src/platform.cc` registers `qcom_vp::QcomGccPll` as `qcom,gcc-taycan-eko-t-pll-v1`.
The board creates one shared model for APSS and CPUCP.
The target socket maps twelve banks at `0x100000..0x10b037`.
Processor forwarding exposes only each bank's supported register aperture.
The vote socket exposes independent words at `0x152020` and `0x157020`.
Each master reads its own word. The oscillator uses their combined requests.
The explicit `vote_count` selects one or two words at a `0x5000` stride.
Both processors observe the same retained state.

The vote socket is optional for standalone PLL banks.
It uses the single-target socket type that the QBox factory supports.
The existing shared GCC router still connects its vote register.
An absent vote socket leaves all vote bits clear.

The private TME probe reuses this protocol for four OEM PLL descriptors at `0x44688c..0x44690b`.
Candidate routines `0x2232dc..0x22337e` program their configuration, enable acquisition, and wait for lock.
The observed boot passes these operations and reaches the subsequent branch-clock setup.
The register bases are `0x021e8000`, `0x081ae000`, `0x080c0000`, and `0x04100000`.



## Subsequent static boundary

The PLL dependency includes both MODE paths, all ROM programming registers, and the shared vote interaction.
The subsequent clock-tree operations are separate IP dependencies, not transport memory effects.

At `0x33605e`–`0x336078`, the ROM traverses clock and divider lists.
The initial entries at `0x336d10` and `0x336d28` contain null register bases.
Their routines skip null bases.
The later loop at `0x33608a`–`0x3360ac` configures clock roots from the table at `0x336d38`.
The first four roots use `0x17c01004`, `0x17c03004`, `0x17c02004`, and `0x17c00004`.
Their selected function is `0x3359c8`, through the table at `0x336c48`.
Other roots use the RCG update routine `0x335b10` through `0x337380`.
That routine sets command bit 0 and polls for hardware to clear it.
The corresponding bases are `0x1742d4`, `0x174400`, `0x174658`, `0x17407c`, `0x1484f0`, `0x148030`, and `0x14402c`.
A divider at `0x147110` uses `0x335bb8` through `0x337388`.

These root/divider devices are not PLL registers.
This model does not acknowledge their updates or assert their readiness.
A subsequent fault there requires a functional clock-root model, with source selection connected to this PLL state.

## Verification boundary

The real PLL guest regression passed during `make test` after the divided-output extension.
The integrated CPUCP ROM passed PLL initialization, root configuration, and CPU-domain initialization.
Its run log is `build/private/run-sm8850-functional-controls8/system.log`.
That run then stopped at the separate UFS controller reset register.
These observations prove firmware progress, not kernel execution.


## Divided-output guest regression

`tools/qcom/qcom-vp/tests/fixtures/pll/producer.S` retains the original PLL0/PLL4 vote, reset, multiplier, and direct-RUN checks.
The fixture now binds two actual RCG instances through `platform.lua`.
The guest selects GPLL0 even with selector 6 and the SDCC HID value.
It checks that an enabled even gate works with the main gate clear.
It also checks that enabling main completes its pending UPDATE, while disabling even revokes only the even root.
The guest changes the postdivider from divide-by-two to divide-by-four and programs dual-edge M/N.
An invalid complement must keep UPDATE pending and ROOT_OFF asserted.
Removing the PLL vote must revoke both root outputs.
The guest reaches the existing success address only after these checks.
This fixture observes availability and update behavior. It does not expose a frequency-measurement register.
Numeric frequencies use the configured L/alpha/predivider/postdivider/HID/M/N calculation, not guest-written success values.
The later source-dependent branch extension uses the same fixture.

The extended guest also checks independent master readback and retention of another master's request.
It checks configuration and vote isolation for bank eleven.


## XBL source banks

SM8850 XBL source descriptors `0x148f14a8..0x148f1710` identify twelve GCC PLL banks.
Each descriptor uses vote register `0x152020` and the bit that matches its bank index.
The common operation table starts at `0x148f37d8`.
XBL `0x14861814` writes OPMODE at offset four, releases reset, waits for lock, and enables output.
This register contract matches the existing Taycan model.

The private profile also uses this model for AOSS PLL0 and two LPASS PLLs.
Their descriptors are `0x148f13c8`, `0x148f1438`, and `0x148f1470`.
Their bases are `0x0c2a0000`, `0x310a0000`, and `0x310a1000`.
These instances have no vote-register connection.

The indexed target replaces the former two dedicated target sockets.
Clock consumers use the physical bank index, including index four for GPLL4.
Votes for undeclared banks remain readable without creating an oscillator.
An access to an undeclared bank returns an address error.

The XBL master table at `0x14925688` starts with the APSS address `0x157020`.
XBL `0x148619a4` reads that word before it adds a source vote.
Removing one master's request cannot stop an oscillator that another master still requests.

## AOSS Pongo source

XBL descriptor `0x148f1400` identifies Pongo at `0x0c2a1000`.
Its operation table starts at `0x148f3790`.
The model key is `qcom,aoss-pongo-pll-v1`.
XBL `0x14861768` sets L at `0x10`, RUN at `0x04`, reset release, and reference presence.
It then waits for MODE bit 31.

XBL `0x148615d0..0x148615f4` pulses the calibration request for one microsecond.
The model retains calibration values without simulating analog calibration.
Its acquisition delay is five virtual microseconds.
Unknown status registers and unsupported operational modes return errors.

The main and even outputs require their independent gates, an output enable, and lock.
The declared configuration uses L=2, with main and even outputs at 38.4 and 19.2 MHz.
XBL `0x14861694` also enables USER_CTL bit two.
The private profile assigns this calibration output a declared reference ratio of eight.
XBL root descriptor `0x148f1850` requires 153.6 MHz from selector two.
The ratio is explicit virtual policy, not an independent measurement of the physical calibration output.

## XBL clock verification

The real reset run passed the NCC, AOSS, and GCC source sequences.
The model did not inject a firmware completion or change image authentication.

`cargo test --locked -- --include-ignored --test-threads=1` passed all 50 tests.
This includes the master-vote, highest-bank, divided-output, and NCC clock-loss guest checks.
These results prove clock behavior and firmware progress, not kernel execution.


## XBL REFGEN NXGEN

`models/qcom/sm88xx/cpucp/refgen.h` defines one `qcom,refgen-nxgen-v1` instance.
The model covers the four registers that the OEM XBL uses.
It does not provide a PLL output or change image authentication.

### Primary evidence

The binary is `CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES/xbl_sc.elf`.
Its SHA256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
The register offsets below come from this binary, not another SoC's register header.

XBL `0x1484ee54..0x1484ee68` establishes these register pointers:

| Register pointer | Physical address | Meaning |
| --- | --- | --- |
| `x22` | `0x00ff105c` | North 0 CTRL1 |
| `x19` | `0x088e7044` | South status |
| `x21` | `0x00ff0044` | North 2 status |

The resulting block bases are `0x00ff1000`, `0x088e7000`, and `0x00ff0000`.
The same model serves each block.

| Block offset | Register | XBL instruction addresses | Supported access |
| --- | --- | --- | --- |
| `0xa8` | IP_TUNEH | `0x1484ee84`, `0x1484ee88`, `0x1484ee8c` | Read retained value. Write `0xf`. |
| `0x44` | RESTRIM_CODE_STATUS | `0x1484eea0`, `0x1484eea8`, `0x1484eeb0` | Read readiness at bit 7. No writes. |
| `0x5c` | CMN_REFGEN_CTRL1 | `0x1484eed0`, `0x1484eed8`, `0x1484eee0` | Read retained value. Write `8`. |
| `0x60` | CMN_REFGEN_CTRL2 | `0x1484eee8`, `0x1484eeec`, `0x1484eef0` | Read retained value. Write `3`. |

At `0x1484ee84`, the displacement `0x4c` from `x22` gives offset `0xa8`.
The other tuning writes use displacement `0x64` from each status pointer.
The first status read uses displacement `0x1000` from `x21`.
All three loops test status bit 7.

XBL `0x1484ee90..0x1484ee9c` reads `0x01fc8004` and compares its low two bits with one.
Only that value skips the readiness loops.
The REFGEN model neither maps this address nor forces this branch.

The model retains these three words and their written values.
The firmware does not establish the analog function of the words, and the model does not simulate it.
The private layout names the tuning word `IP_TUNEH` and the other two words `CMN_REFGEN_CTRL1` and `CMN_REFGEN_CTRL2`.

### Explicit substitute policy

The platform must select both `virtual_power=true` and `calibration_policy="bounded-virtual"` to obtain readiness.
The default policy is unpowered, with calibration disabled.
Those defaults never assert readiness.
The parameters apply at elaboration and remain fixed until the instance is destroyed.
This model has no runtime power-control interface.

Cold reset clears the retained controls and returns the model to the untuned state.
These zero values are virtual reset policy, not measured hardware reset values.
The first accepted IP_TUNEH write starts calibration only when both policy conditions hold.
The state changes from untuned to calibrating, then to ready after `calibration_delay_us`.
The default delay is five virtual microseconds.
Valid delays are one through 1000 microseconds.
This delay is a bounded substitute, not silicon timing.
Status reads observe the elapsed virtual time. Poll count does not control readiness.
An identical tuning write does not restart calibration or remove readiness.
Cold reset removes readiness and requires a new tuning write.

CTRL1 and CTRL2 retain their accepted values independently of readiness.
The model does not simulate analog training, resistor trim codes, voltage thresholds, noise, or rail collapse.
It does not connect external vote pins or model their electrical effects.
The CTRL1 and CTRL2 settings therefore describe retained firmware policy only.
There are no downstream analog consumers in this register subset.
Status bits other than REFGEN_READY read zero as substitute policy, not measured trim results.

The model accepts aligned, little-endian 32-bit accesses without byte enables.
It uses the shared clock transport validation and disables DMI.
Unknown offsets return an address error.
Status writes and unsupported control values return a command error without changing retained controls.
This subset does not silently accept other calibration modes.

### Integration boundary

`src/platform.cc` registers `qcom_vp::QcomRefgen` under `qcom,refgen-nxgen-v1`.
The private profile creates three independent instances with `0xac`-byte relative apertures.
Each instance declares `virtual_power=true`, `calibration_policy="bounded-virtual"`, and `calibration_delay_us=5`.

The real OEM XBL executed the readiness loops at `0x1484eea0`, `0x1484eea8`, and `0x1484eeb0`.
It then completed the control writes and advanced to the separate unused-domain collapse.
The environment selector remained zero. The firmware did not use its emulation shortcut.


## Coarse calibration

SM8845 XBL uses STATUS at PLL offset `0x0c` during the OffNoReCAL sequence.
The selected banks and call paths are recorded in `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md`.
The firmware shifts STATUS right by one and masks `0x1f000`.
Thus STATUS bits 17:13 supply CONFIG_CTL bits 16:12.
The transfer does not use STATUS bits 21:17.

`coarse_calibration_mask` explicitly selects banks with this virtual analog behavior.
`virtual_coarse_calibration_code` supplies a value from zero through 31.
A read requires a running oscillator and TEST_CTL bit 8.
Undeclared banks reject the offset. Writes and inactive calibration return command errors.
The code is declared virtual policy, not a measured trim value.

The enable poll at `0x1485f99c..0x1485f9a8` tests mask `0x80000004` with an any-bit condition.
RESET_N therefore permits the firmware to read coarse status before fine lock.
The model does not make fine lock or downstream clocks ready early.
Those states retain the five-microsecond acquisition delay.

The native guest transfers code 13 with the actual firmware masks and obtains CONFIG_CTL field `0xd000`.
The old status encoding failed this regression.
The corrected model passed the native suite.
Real SM8845 XBL completed the selected calibration cohort.

## Shared votes for separate PLLs

SM8845 source 14 has its own register aperture at `0x06e00000`.
Its descriptor uses the existing global vote register `0x152020`, bit zero.
This does not make its configuration or oscillator state identical to GPLL0.
`models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md` records the selected source loop and the complete wrapper path.

`external_vote_path` selects the existing register owner.
`external_vote_mask` selects one bit for a single separate PLL.
The external instance cannot own a second vote register.
The model rejects self-reference, observer chains, missing owners, and conflicting socket ownership.
Only configured external instances create an event observer.

Vote writes and cold reset notify that observer.
The external PLL retains its own L, alpha, mode, output gates, and acquisition deadline.
Register reads synchronize a pending vote transition before they report status.
The shared owner retains the individual master words and their combined vote.

The native guest completes a downstream RCG without first reading the separate PLL.
It checks independent configuration, the final-master transition, and direct RUN without a shared vote.
All 54 host and native tests passed after integration.
Real SM8845 XBL passed source initialization and next reached the scalar write at `0x183160`.

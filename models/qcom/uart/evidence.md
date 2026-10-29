# GENI UART v1

## Evidence

The behavior follows `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/uart.py`, class `GeniUart`.
That model records these matching OEM SEC sites:

- `0xc6833f5c` selects properties at `0xc683ff10` and `0xc683ff78`.
- Both properties select SE base `0x00a9c000`, common base `0x00ac0000`, and 115200 baud.
- The firmware image at file offset `0x40fe0` describes UART protocol 2, firmware `0x406`, and configuration 10.
- Its `0x40030000` core version describes the firmware image. It is not a measured hardware revision.

No firmware image bytes are included here. Guest firmware must write its configuration and instruction RAM.
The model retains these writes. It does not execute SE instructions or authenticate their contents.

### Console mapping and IRQ

The matching Kaanapali reference source is `qcom-rehost:inputs/reference/linux-sm8850/arch/arm64/boot/dts/qcom/kaanapali.dtsi sha256=61bc5f222bc638753d0a91f7fd41d322cc901915a197e43c51f28402b0d8049e`.
Lines 1519-1538 define `uart7: serial@a9c000` under `qupv3_1: geniqup@ac0000`:

- `compatible = "qcom,geni-debug-uart"`.
- SE register aperture `0x00a9c000`, size `0x4000`.
- `interrupts = <GIC_SPI 835 IRQ_TYPE_LEVEL_HIGH>`.
- Clock `GCC_QUPV3_WRAP1_S7_CLK`.

The preprocessed sibling `.kaanapali-oneplus-infiniti.dtb.dts.tmp` confirms `<0 835 4>` at lines 1577-1581.
Its SHA-256 is `dd5146f314b3f8c10a12dd2c9e390a4862af09fc592647cb2b76d2f2c082e61d`.
These are reference source files. They do not prove which embedded vendor_boot DTB the bootloader selects.

The requested public repository pin has a different SoC source:
[sun-qupv3.dtsi at 5ab2a689ff87d7d28c511f1762cf41c1b90d965a](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/kernel_platform/qcom/opensource/devicetree/qcom/sun-qupv3.dtsi#L501-L506).
It defines the same address but SPI 579. That Sun interrupt must not replace the matching Kaanapali SPI 835.
The fixture uses SPI 835, architectural INTID 867.

### Linux command control and completion

The pinned source is
[torvalds/linux at 6b4b543fa18757297ac756a6fe6e64f506b7844a](https://github.com/torvalds/linux/commit/6b4b543fa18757297ac756a6fe6e64f506b7844a).
All line numbers below refer to that commit.

Register and bit definitions come from
[include/linux/soc/qcom/geni-se.h](https://github.com/torvalds/linux/blob/6b4b543fa18757297ac756a6fe6e64f506b7844a/include/linux/soc/qcom/geni-se.h):

- Line 87 defines `SE_GENI_STATUS` `0x40`. Lines 168-169 define `M_GENI_CMD_ACTIVE` bit 0 and `S_GENI_CMD_ACTIVE` bit 12.
- Lines 97-98 define `SE_GENI_M_CMD0` `0x600` and `SE_GENI_M_CMD_CTRL_REG` `0x604`. Lines 106-107 define `SE_GENI_S_CMD0` `0x630` and `SE_GENI_S_CMD_CTRL_REG` `0x634`.
- Lines 198-199 define `M_GENI_CMD_CANCEL` bit 2 and `M_GENI_CMD_ABORT` bit 1. Lines 208-209 define the same two bits for the secondary sequencer.
- Lines 217-218 define `M_CMD_CANCEL_EN` bit 4 and `M_CMD_ABORT_EN` bit 5. Lines 250-251 define `S_CMD_CANCEL_EN` bit 4 and `S_CMD_ABORT_EN` bit 5. `M_CMD_DONE_EN` and `S_CMD_DONE_EN` are bit 0, at lines 213 and 246.
- Lines 265-266 define `S_COMMON_GENI_S_IRQ_EN`, which includes bits 5 through 1. An RX abort or cancel therefore latches an enabled secondary status bit.
- Lines 410-419 show that `geni_se_setup_s_cmd` reads `SE_GENI_S_CMD0`, masks the opcode and parameter fields, and writes the result.

The console and early console driver is
[drivers/tty/serial/qcom_geni_serial.c](https://github.com/torvalds/linux/blob/6b4b543fa18757297ac756a6fe6e64f506b7844a/drivers/tty/serial/qcom_geni_serial.c):

- Lines 312-320 read `SE_GENI_STATUS` to test the active bits.
- Lines 322-354 poll one register field every 10 microseconds for up to 20000 microseconds.
- Lines 357-364 write the TX length and `0x08000000` to `SE_GENI_M_CMD0`.
- Lines 366-379 poll `M_CMD_DONE_EN`. On timeout they write `M_GENI_CMD_ABORT` to `0x604`, poll `M_CMD_ABORT_EN`, and clear that bit through `0x618`.
- Lines 381-390 write `S_GENI_CMD_ABORT` to `0x634`, poll `0x634` until bit 1 clears, clear `S_CMD_DONE_EN | S_CMD_ABORT_EN` through `0x648`, and write `FORCE_DEFAULT` to `0x20`.
- Lines 642-655 drain or poll the main sequencer, then cancel an active M command during console takeover.
- Lines 918-931 cancel M. They write `M_GENI_CMD_CANCEL`, poll `M_CMD_CANCEL_EN`, abort after a timeout, clear `M_CMD_ABORT_EN`, and then clear `M_CMD_CANCEL_EN`.
- Lines 959-961 cancel S and poll `S_CMD_CANCEL_EN`.
- Lines 1686-1735 run early console setup. Setup polls TX done and aborts RX before it starts a command and before it enables the SE interrupt gates.

The engine wrapper driver is
[drivers/soc/qcom/qcom-geni-se.c](https://github.com/torvalds/linux/blob/6b4b543fa18757297ac756a6fe6e64f506b7844a/drivers/soc/qcom/qcom-geni-se.c):

- Lines 312-319 read `SE_IRQ_EN` `0xe1c` and set the M, S, DMA TX, and DMA RX gates.
- Lines 345-353 clear the M and S status registers and write `0xffffffff` to `SE_IRQ_EN`.
- Lines 364-382 add `M_COMMON_GENI_M_IRQ_EN` and `S_COMMON_GENI_S_IRQ_EN` to the M and S enable registers.

The model completes each abort and cancel before the control write returns.
The active command clears, and the matching `CMD_ABORT_EN` or `CMD_CANCEL_EN` bit latches.
The request bits in `0x604` and `0x634` read back as zero, so the early console poll succeeds on its first read.
An abort also completes when no command is active, because early console setup aborts RX before it starts a command.
The latched bits reach the interrupt output through the M and S enable registers and the `SE_IRQ_EN` gates.
The model does not insert the 20 millisecond poll timeout, and it does not model a request that stays pending.
The guest spin loop supplies the wait.
The first early console TX-done poll expires on a clean engine, because no command completed.
The driver then aborts and continues. The model does not invent a completion.

## Declared behavior

`hw_version=0x40030000` comes from the SM8850 phone kernel log.
The `msm_geni_serial_get_ver_info` line reports `HW Ver: 0x40030000`.
See record [`20261026T003332Z-phone-qup-hw-version`](https://github.com/1vivy/gobbl-lab/blob/00b81d02c9bdaba2a4870eb00719dd7fbc6547d2/records/20261026T003332Z-phone-qup-hw-version/record.md).
The reference phone is CPH2747. CPH2747 and CPH2749 differ only by region.
The selected SM8845 QUP firmware declares the same core version, but no SM8845 measurement exists.
`fifo_depth=16` is an explicit virtual capability. Both FIFOs report 32-bit words.

TX drains synchronously. The guest sets the command length and supplies every output byte.
Only the remaining byte count determines completion. The final word cannot emit bytes beyond that length.
The model accepts the documented four-byte UART packing only.
The empty TX FIFO asserts its watermark level while an active command waits for data.
Command completion, cancellation, and abort latch separate status bits.
An abort or cancel clears the active command and the request bits in `+0x604` and `+0x634`.
A read of either control register returns zero after the request completes.
W1C clears latched bits, but does not clear an active watermark level.
Master and secondary masks, mask aliases, and SE IRQ gates determine the output interrupt.
Each register write publishes the interrupt level before the transport returns.
This avoids the duplicate level interrupt caused by deferred masking before EOI.

RX commands can become active. There is no RX input source, so RX never fabricates bytes or completion.
Abort and cancel retain instruction RAM and configuration RAM.
Firmware revision aliases reflect the guest's writes.
DMA and GSI mode controls retain guest configuration.
Unsupported transfers, unsupported packing, and writes without an active TX command return TLM errors.
Unknown registers and invalid access widths also fail.
No electrical timing, external clock-tree enforcement, TLMM pins, DMA/GSI execution, or secure access policy is implemented here.

## Platform integration

`src/platform.cc` registers `GeniUart`, `GeniSe`, and `GeniCommon` as separate model types.
The compatible keys are `qcom,geni-uart-v1`, `qcom,geni-se-v1`, and `qcom,geni-common-v1`.
One common object supplies each physical wrapper, including the console wrapper.
The board connects these relative-address sockets to the APSS router:

| Owner and port | Base | Size |
| --- | --- | --- |
| `GeniCommon.target_socket` | `0x00ac0000` | `0x1000` |
| `GeniUart.se_socket` | `0x00a9c000` | `0x3000` |
| `qup_common_4.target_socket` | `0x01ac0000` | `0x1000` |
| `qup_common_2.target_socket` | `0x0008c0000` | `0x1000` |
| `i2chub_common.target_socket` | `0x0009c0000` | `0x1000` |

The third row applies to SM8850 only.
The fourth row applies to SM8845 only.
The fifth row applies to SM8850 only.
The selected SM8850 `QUPV3_HW_PROP` lists `QUP_1C` through `QUP_4C` and one `I2CHUB_0C` common block.
The selected SM8845 property lists `QUP_1C` through `QUP_3C` only.
`platforms/qcom/sm88xx/xbl-qup-firmware.lua` owns the wrappers in its ac_fw records: 1 through 3 on SM8850, and 1 and 3 on SM8845.
`platforms/qcom/sm88xx/qsee-qup-common.lua` adds the remaining wrapper and the i2chub common block for the QSEE checks.
Both composition paths instantiate the same `qcom,geni-common-v1` model.

QSEE `0x157160bc` reads the 32-bit word at wrapper `+4` for each QUP common entry and requires all values to match.
SM8845 `0x1570b010` performs the same read.
SM8850 run397 passes wrappers 1 through 3 and stops at `0x1ac0004` when wrapper 4 has no owner.
The equivalent SM8845 read is `0x8c0004` for wrapper 2.
Every instance reports `hw_version` `0x40030000`, so the equality check holds.

The SE-config prologue at `0x15715364` sets bit zero at `+0x118`, `+0x120`, and `+0x21c` on every common entry that it receives.
SM8850 run398 passes the QUP wrappers and stops at `0x9c0120` for the i2chub common block.
That block is `I2CHUB_0C` at base `0x9c0000`.
The model owns the three controls and retains their values.
The block receives no SE list from the common path itself.
The selected SM8850 OEM devcfg node declares `i2chub_perms` with two entries, so the QSEE list builder also has an i2chub permission set.
SM8845 has no i2chub common block and no equivalent access.

The firmware scan also finds the QSI record at `0x82461000`.
Its version field at `+8` is `0x40030000`.
The wrapper comparison at `0x15716408` skips that record when the common version is lower.
The wrapper version equals the record version, so QSEE selects the record.
The selected SM8845 `qupv3fw.elf` has SHA-256 `ac97f111b9d99decfc1a434a39cc4b8c0ea73c8c6e99cb0bd459a7ebc15751b8`.
Its QSI record at file offset `0x1000` also declares `0x40030000`.

After selecting the record, QSEE enables the per-engine SE clocks from the selected permission set.
SM8850 enables seventeen engines; SM8845 enables fourteen.
Run400 requests the QUP1 S0 vote at CBCR `0x118004`, mask `0x400000` in bank `0x153040`.
The selected descriptor is `0x157f4bd8`; the SM8845 descriptor is `0x157e8120`.
`qsee-clock-dependencies.lua` declares every enabled engine vote for both SoCs.
The two SM8850 hub permissions select the i2c S0 and S1 clocks at CBCR `0x117004` mask `0x400` and CBCR `0x117020` mask `0x800`.

Run404 passes those votes and reads `0x00a80080`, the QUP1 S0 SE control at `+0x80`.
The selected TZ sequence at `0x15715610..0x157158f4` reaches `+0x24`, `+0x28`, `+0x2000`, `+0x2004`, and `+0x2008`, copies configuration to `+0x1010`, and sets `+0x200c` bit zero.
`platforms/qcom/sm88xx/qsee-qup-common.lua` now owns the selected engines:
seventeen QUP engines plus the two hub engines on SM8850, and fourteen QUP engines on SM8845.
It reuses an existing `qcom,geni-se-v1` owner at the same base and rejects a partial overlap.
`GeniSe` now retains `+0x200c`.
The sequence does not poll that word, so this is configuration retention, not a completion.
The next access is the GSI firmware bootstrap at offset `0x60`.

The smaller windows follow the rehost model's implemented aperture.
The larger DT apertures do not establish support for additional registers.
The IRQ connects to `&apss_gic.spi_in_835`.
The shared helper exposes 864 SPIs to include that input.
This count is explicit virtual capacity, not a measurement of the physical GIC.

CCI parameters:

- `GeniCommon.hw_version`: unsigned, default `0x40030000`.
- `GeniSe.fifo_depth`: unsigned, default 16, accepted range 9 through 255.
- `tx_log`: a required nonempty output path for `GeniUart`. Protocol-neutral engines do not open logs.

The board sets `tx_log` to `<trace-prefix>-uart.log` in the dedicated run directory.
The model truncates that output file during elaboration.
Do not use a firmware input or an existing evidence file as the destination.
Each successful guest TX write flushes its bytes to that file.
Open, write, flush, and close failures throw host errors with the path.
The binary output remains separate from simulator diagnostics and instruction traces.

## Guest fixture and runner contract

`tools/qcom/qcom-vp/tests/fixtures/uart/producer.S` is an actual A64 test payload.
`platform.lua` supplies the UART, GIC, CPU, and ROM topology.
The payload checks these boundaries:

- Explicit virtual revision and FIFO capability values.
- Empty reset firmware state and guest-owned instruction RAM reads and writes.
- TX cancellation before output and RX abort without fabricated input.
- Primary cancel and primary abort latch `CMD_CANCEL_EN` and `CMD_ABORT_EN`.
- Secondary abort completes with no active command, as early console setup requires.
- Secondary cancel latches `CMD_CANCEL_EN` on an active RX command.
- Each control register read returns zero after the request completes.
- RAM retention after abort.
- Partial TX remains active and cannot complete early.
- Watermark status remains asserted after W1C until the level condition ends.
- Two real Group 0 interrupts arrive as INTID 867.
- The handler clears the source synchronously before EOI, with no duplicate interrupt.
- A masked zero-length command completes without output. Unmasking delivers its latched interrupt.
- Enable-set and enable-clear aliases share one register.

The Rust regression links the actual A64 guest at `0x300000`.
It requires the success write at `0x600d0000`.
It rejects the failure address, simulator errors, and timeout.
It then compares the binary UART output with these exact bytes:

```text
55 41 52 54 20 54 45 53 54 3a 00 ff 0a
```

This means `UART TEST:`, NUL, byte `ff`, and newline. It is test text, not a kernel banner.
The high bytes of the final FIFO word must not appear in the file.
The regression `uart_transmits_exact_guest_bytes_and_delivers_irqs` passed during `make test`.
That suite reported 30 passing host tests and nine passing guest tests.
The OEM run `run-sm8850-functional-controls10` reached the UFS reference-clock control without transmitting UART bytes.
The implemented serial path does not yet provide a Linux boot log.

## Selected SM8850 XBL path

This static analysis uses `xbl_sc.elf` from the CPH2749 export.
Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
All addresses below are guest virtual addresses unless marked as file offsets.
The ELF load headers establish these mappings:

| File interval | Guest base |
| --- | --- |
| `0x1000..0xae000` | `0x14827000` |
| `0xae000..0xc2000` | `0x148d4000` |
| `0xc2000..0x100000` | `0x148eb000` |
| `0x100000..0x11c000` | `0x14a00000` |

The intervals exclude their upper endpoint.
No vendor bytes are included in this repository.

### Property and clock selection

`0x148c587c..0x148c58a4` reads `0x01fc8004` and selects one of two property records.
Bits `[1:0] == 1` select `0x14927598`. Other values select `0x14927530`.
Both records specify SE `0x00a9c000`, common `0x00ac0000`, and baud 115200.
Both contain the clock-name pointer `0x148dc640`, which names `gcc_qupv3_wrap1_s7_clk`.
The property records occupy file offsets `0xfe530` and `0xfe598`.
The board must supply the strap register through its existing fuse/control model.
This helper does not override the strap.

The entry at `0x14844e78` calls the serial initializer at `0x1488de28`.
The boot dispatch table contains its pointer at `0x148f0c48`, file offset `0xc7c48`.
`0x1488de58` calls the UART initializer at `0x1488d610`.
Its baud lookup uses `0x148e0900`, which selects 7,372,800 Hz and divider 64 for 115200 baud.
`0x1488d6e4` calls clock-open at `0x1488e0e8` before it reads GENI status.
Clock-open resolves the property clock name, sets its frequency, and enables its branch.
`0x1488d6f0..0x1488d71c` selects serial clock value `0x21` for the default virtual hardware revision.
Both serial clock registers receive this value at `0x1488d7d8..0x1488d7f0`.

External clock dependencies belong to the clock helper, not this UART helper:

- RCG CMD `0x00118790`, with CFG and M/N/D words at offsets `4/8/12/16`.
- Branch CBCR `0x00118780`.
- Vote word `0x00152010`, mask `0x00010000`.
- XBL branch descriptor `0x148f2768` points to root descriptor `0x148f1fa8`.

These addresses come from the matching XBL clock descriptors.
The UART model retains its internal clock registers but does not enforce the external clock tree.

### Bounded GENI access sequence

The existing model covers this sequence without an additional readiness constant or register implementation.
This conclusion applies to the selected initialization and FIFO output routines.
It does not establish support for every later driver operation.

1. `0x1488d720..0x1488d72c` reads SE status at `+0x40`.
   An active secondary command skips firmware loading and register initialization.
2. `0x1488e2a4..0x1488e2f4` reads common version `+4` and SE hardware parameter `+0xe2c`.
   It obtains the firmware pointer through `0x148c5864`, which returns `0x14927600`.
3. `0x1488e328..0x1488e3a0` retains common controls `+0x120/+0x118`.
   It programs SE output, clock-gating, DMA-clock, and sequencer-clock controls at `+0x24/+0x80/+0x28/+0x2000/+0xe30`.
4. `0x1488e3bc..0x1488e428` programs SE initialization at `+0/+4` and the guest configuration table.
   That table has 69 entries covering `+0x100..+0x148` and `+0x200..+0x2c4`.
5. `0x1488e42c..0x1488e4cc` reads FIFO depth at `+0xe28` and sets the RFR watermark at `+0x814`.
   It selects FIFO mode at `+0x258`, IRQ gates at `+0xe1c`, and disables GSI events at `+0xe18`.
   It programs master/secondary masks and DMA-mask aliases at `+0x614/+0x644/+0xc4c/+0xd4c`.
6. `0x1488e4d0..0x1488e534` writes guest firmware revisions at `+0x1000/+0x1004`.
   It copies 454 instruction words into RAM at `+0x1010`.
   The image header declares UART protocol 2, firmware `0x406`, and configuration 10.
   The instruction bytes remain guest-owned input.
7. `0x1488e538..0x1488e5ac` programs `+0x20/+0x28`, enables `+0x2000/+0x2004`, and clears FIFO disable at `+0x2008`.
   The strap-selected path can also write `0x77` to `+0xe38`.
8. `0x1488d768..0x1488d898` reads FIFO capabilities and sets watermarks, framing, stale timeout, and packing.
   Its offsets are `+0xe24/+0xe28/+0x80c/+0x810/+0x814/+0x48/+0x4c`.
   Framing uses `+0x268/+0x28c/+0x2a4/+0x25c/+0x2a8/+0x280/+0x26c/+0x294`.
   Packing uses `+0x260/+0x264/+0x284/+0x288`, with `0x4380e/0xc3e0e` for each direction.
9. `0x1488d89c..0x1488d900` clears both IRQ latches, clears both masks, sets output control `+0x24`, and starts RX.
   The secondary command at `+0x630` is `0x08000000`. No RX input exists in this model.
10. `0x1488dc3c..0x1488dcb4` checks master activity, writes byte count `+0x270`, and starts master command `+0x600`.
    The master command is `0x08000000`.
    `0x1488dcf4..0x1488de24` reads TX FIFO occupancy `+0x800` and depth `+0xe24`, then writes words at `+0x700`.
    It clears the watermark enable through `+0x620` after it supplies the final bytes.

The output wrapper at `0x14844ec0` calls `0x1488df60` from `0x14844f00`.
It repeats until the guest buffer length reaches zero.
The existing byte-count completion, W1C latches, mask aliases, and exact binary output apply without changes.
RX FIFO reads remain unsupported because no input bytes exist.
DMA/GSI transfers, non-byte packing, and unsupported commands still fail.

### Shared TLMM digital model

`platforms/qcom/sm88xx/xbl-tlmm.lua` connects one `qcom,tlmm-v1` model at `0x0f100000`.
It replaces the separate UART and SPMI configuration mappings.
The selected `/soc/pinctrl@f100000` nodes declare 217 GPIOs for SM8850 and 205 for SM8845.
Both use one tile and stride `0x1000`.
The model rejects geometry holes and overlapping mappings.
Separate windows preserve the fuse-owned identity view at `0x0f1db014`.
An address translator connects auxiliary pads to the same model without mapping the intervening holes.

UART pins 62/63 use the same GPIO state as later native initialization.
Auxiliary SPMI pads 226/227 expose only configuration, with writable mask `0x1fff`.
They are not ordinary GPIOs.
These controls do not gate the UART output file.

| GPIO offset | Contract |
| --- | --- |
| `0x00` | Configuration, writable mask `0x33ff` |
| `0x04` | Output latch bit one and digital input bit zero |
| `0x08` | Interrupt configuration, writable mask `0x79f` |
| `0x0c` | Clear interrupt status. Writes create no event. |
| `0x10` | Read-only presence for a GPIO in this tile |
| `0x14` | Sleep configuration, writable mask `0xfff` |
| `0x18` | Sleep interrupt controls, writable mask `3` |

Controls start at zero. No external input, wakeup source, or eGPIO provider is connected.
GPIO output mode reads its own output latch when the mux selects GPIO and eGPIO enable is clear.
Input mode and peripheral muxes read disconnected-low input.
Pull settings remain configuration only.
Sleep programming does not change active state, and output transitions do not manufacture interrupts.

SM8850 HAL `0x1489e82c..0x1489ec14` and SM8845 HAL `0x1489d0f4..0x1489d4dc` establish the native register consumers.
The [Linux Kaanapali pinctrl driver](https://github.com/torvalds/linux/blob/master/drivers/pinctrl/qcom/pinctrl-kaanapali.c) supplies complementary register definitions.
The native initialization replaces static descriptor geometry with the selected DT geometry.
It scans tile presence, writes every GPIO's sleep configuration, and clears sleep interrupt controls when requested.
These are digital functional policies, not electrical measurements or security grants.

SM8850 run129 records initialization status zero at `0x1489e7a4`.
Its UART records `sbl1_tlmm_init`, then `sbl1_efs_handle_cookies`.
The retained register evidence is `build/private/run-sm8850-common-runtime129/post-qup-probe.json`.
All 94 integrated tests pass, including digital pin transitions, immutable presence, interrupt non-generation, and geometry denial.
Clippy passes with warnings denied.

The primary properties specify GPIO62/config `0x6471` and GPIO63/config `0x6431`.
The alternate properties mark all four GPIO entries as absent.
`0x1488e228..0x1488e28c` skips absent entries and dispatches the remaining entries through the GPIO driver.
`0x1489deec..0x1489df0c` selects the descriptor at `0x14923678`.
That descriptor defines base `0x0f000000`, stride `0x1000`, and configuration offset `0x100000`.
Its callback table at `0x149235e8` selects `0x1489e82c`.
`0x1489e840..0x1489e880` builds the configuration value and writes the computed register.
The drive conversion at `0x1489eae4` maps the requested value 200 to drive code zero.
With reset-zero controls, the two guest writes produce `0x207` and `0x205`.
The callback also transfers the old register's bit 11 into bit 12.
The model keeps bit 11 clear because no eGPIO provider is connected.

SM8850 run128 retains original XBL output through DCP loading and subsequent SMEM callbacks.
The output proves actual XBL transmission, not kernel entry or a Linux boot.

## Exact GKI console contract

The selected SM8850 kernel identifies commit `b3b66ace21e0`.
Its [GENI console driver](https://android.googlesource.com/kernel/common/+/b3b66ace21e0/drivers/tty/serial/qcom_geni_serial.c) packs four UART bytes per FIFO word.
Lines 1474–1528 register `qcom,geni-debug-uart` earlycon and initialize eight-bit FIFO operation.
Lines 443–493 count inserted carriage returns and poll watermark and command completion.
The existing model already implements that packing and exact command length.

The same driver's normal console can interrupt an active, partially queued TX command.
Lines 419–424 wait until `SE_GENI_M_GP_LENGTH` equals the number of queued bytes.
The [matching header](https://android.googlesource.com/kernel/common/+/b3b66ace21e0/include/linux/soc/qcom/geni-se.h) assigns this progress counter to offset `0x910`.
Its `geni_se_setup_s_cmd` helper also reads the secondary command register before it writes the new command.

The model now retains accepted command values for readback.
Its TX progress counter starts at zero for each accepted master command.
Each successful FIFO output increments it by the actual emitted byte count.
Padding bytes do not increment it.
Cancellation cannot manufacture transmitted bytes.
This behavior follows the model's synchronous drain policy. It does not simulate serial timing.

The native regression first failed at the unsupported progress read.
After that correction, it exposed the unsupported secondary command read.
The completed regression checks command readback, partial progress, final-word bounds, and progress reset for an empty command.
It retains the exact binary output and interrupt checks.
The complete integration suite then passed 58 tests.
No kernel executes in this fixture, so these results do not establish a Linux console or kernel boot.

## Selected post-DDR ABL and Linux consumers

The console gate and its stock configuration interface already appear in
[`docs/qcom/sm88xx/oplus/bringup.md`](../../../docs/qcom/sm88xx/oplus/bringup.md#stock-oem-console-option).
That interface preserves signed images, AVB, rollback, and device lock policy.
Do not replace it with a patched command line or an authorization bypass.
The original-entry requirements remain in
[`docs/qcom/sm88xx/oplus/observer-sites.md`](../../../docs/qcom/sm88xx/oplus/observer-sites.md).

Offline parsing of the selected packages establishes these additional facts:

| Object | SM8850 | SM8845 |
| --- | --- | --- |
| Decoded ABL PE SHA-256 | `372351c7223a15ea50c3c7e3466e6ba0596523b0e1421b5dae81f3b97902ff70` | `8dc6e24bd1a88d4fbfc5040adbf1bd9dbdc4875c6ea3b7afdc8c3a1964b249ab` |
| `vendor_boot.img` SHA-256 | `5ec9a93ff71ca8516ec1951538c7b807e173bfaaab85c3654e0463eba990a4e1` | `1736dde58f5d28a4d31cfeefd7e97316fc0cfd48ccfce87b21eab9a5d4ef2d72` |
| DTB area file offset / length | `0x10a9000 / 7560891` | `0x1034000 / 5063497` |
| Concatenated DTB count | 14 | 9 |
| Embedded Linux commit | `b3b66ace21e0` | `8c67d4274c0a` |

Every DTB in both areas selects this stdout path:
`/soc/qcom,qupv3_1_geni_se@ac0000/qcom,qup_uart@a9c000:115200n8`.
Every corresponding UART node has compatible `qcom,geni-debug-uart`, status `ok`,
and interrupt cells `<0 835 4>`.
This establishes the same UART and interrupt contract for both selected packages.
It does not establish the runtime board match or the final overlaid FDT.
Capture the FDT that the original ABL passes in `x0`, not an arbitrary bundled DTB.

The [exact SM8845 driver](https://android.googlesource.com/kernel/common/+/8c67d4274c0a/drivers/tty/serial/qcom_geni_serial.c)
uses the same console sequence as the previously recovered SM8850 driver.
Lines 419–424 poll `SE_GENI_M_GP_LENGTH` for actual queued-byte progress.
Lines 443–493 count carriage returns, pack four bytes, and poll watermark and completion.
Lines 1474–1528 check UART protocol, configure FIFO packing, and register the early console.
The source contains no `con_enabled` parameter.
The selected SM8845 vendor command line contains the same
`console=ttynull qcom_geni_serial.con_enabled=0` text as SM8850.
The stock ABL gate can append `console=ttyMSM0,115200n8 earlycon` on both OEMs.

The current model covers these statically recovered FIFO consumers.
No new UART behavior is justified by this analysis.
Before Linux earlycon, observe `0xa9c068`: its protocol field `[15:8]` must equal 2
because genuine firmware wrote its UART revision.
Observe commands at `0xa9c600`, length at `0xa9c270`, FIFO writes at `0xa9c700`,
status at `0xa9c610`, and progress at `0xa9c910`.
Successful output consumes exactly the command length and raises completion only afterward.
If earlycon returns `-ENXIO`, recover the missing firmware initialization or resource mapping.
Do not seed a firmware revision to conceal that failure.

Normal console operation also depends on guest-produced clock, pin, power, and IRQ setup.
DMA/GSI execution and external serial input remain outside this model.
An actual unsupported access must identify a new consumer before extending those contracts.
These results are static coverage only.
No new firmware run, regression, kernel entry, or Linux output is claimed here.

## Selected QUP firmware programming

The original SM8850 loader at `0x1489f42c` consumes `/soc/ac_fw`.
The selected FDT starts at file offset `0x10e14` in SM8850 `RADIO/xbl_config.img`.
The SM8845 FDT starts at `0xf52c` in its corresponding image.
`platforms/qcom/sm88xx/xbl-qup-firmware.lua` connects these selected engines without seeding their protocol or firmware revision.

| OEM | QUP and SE | SE base | Selected protocol |
| --- | --- | --- | --- |
| SM8850 | 1/5 | `0x00a94000` | `3` |
| SM8850 | 1/6 | `0x00a98000` | `0xd` |
| SM8850 | 2/2 | `0x00888000` | `2` |
| SM8850 | 1/3 | `0x00a8c000` | `3` |
| SM8850 | 3/0 | `0x01980000` | `3` |
| SM8845 | 1/3 | `0x00a8c000` | `3` |
| SM8845 | 1/5 | `0x00a94000` | `0xd` |
| SM8845 | 1/6 | `0x00a98000` | `2` |
| SM8845 | 3/4 | `0x01990000` | `3` |

SM8850 XBL `0x148a0054` programs configuration arrays, mode controls, revisions, and instruction RAM.
The selected protocol payload sizes are `0xa1c`, `0x6d8`, and `0x718` bytes.
The model retains those guest bytes and derives revision aliases from guest writes.
Programming does not create a transfer-completion interrupt.
Protocol-neutral engines reject transfer commands because they have no implemented transfer backend.
The existing UART backend retains its real output file and interrupt behavior.

### GSI initialization

`models/qcom/gsi/gsi.h` supplies `GsiController`, compatible `qcom,gsi-v1`.
Each selected wrapper has one control socket and one separate firmware socket.
The control aperture is `0x10000` bytes. Firmware RAM starts at base plus `0x4c000`, with capacity `0x6000`.
The model does not map or implement later GPII contexts, rings, or doorbells.

| QUP | GSI base | Common base |
| --- | --- | --- |
| 1 | `0x00a04000` | `0x00ac0000` |
| 2 | `0x00804000` | `0x008c0000` |
| 3 | `0x01904000` | `0x019c0000` |
| 4 | `0x01a04000` | `0x01ac0000` |

QUP4 exists only on SM8850.
The selected SM8850 TZ table at `0x157f1ab8` lists four GSI bases; the SM8845 table at `0x157e5078` lists three.
Both original `RADIO/qupfw.img` files contain a QSI header at file offset `0x1000`.
Its instruction count describes eight-byte instructions, starting at header offset `0x68`.
The selected `IMAGES/qupv3fw.elf` segment has virtual address `0x82461000` and the same header layout.

| OEM | Instruction count | Required bytes | QSI total |
| --- | --- | --- | --- |
| SM8850 | `0xa49` | `0x5248` | `0x52b0` |
| SM8845 | `0xa46` | `0x5230` | `0x5298` |

The original loaders are SM8850 `0x148a0554` and SM8845 `0x1489ee28`.
The selected TZ loaders are SM8850 `0x157234f0` and SM8845 `0x15717c54`.
XBL establishes this sequence:

1. Write the ten configuration words and the first firmware word.
2. Download the complete selected payload.
3. Enable the clock at `+0xb000`.
4. Write start value `5` at `+0`.
5. Clear `+0xf400`.
6. Write `0x61`, then `0x81`, at `+0xf018`.
7. Read initialization status at `+0xf400`.

TZ establishes this sequence:

1. Read `+0`; skip the instance when bit zero is set.
2. Read `+0x60`, clear bit one, and write the word back.
3. Write the ten configuration words from the QSI.
4. Write the first instruction word at `+8`.
5. Copy the instruction words to `+0x4c000`.
6. Set bit zero at `+0xb000`.
7. Write `5` at `+0`, then zero at `+0xf400`.
8. Write `0x81` at `+0xf018` until `+0xf400` reads nonzero.
9. Set bit one, then bit zero, at `+0xb008`.

The selected wrappers report `0x40030000`, so both take the `5` start value.
The model retains `+0x60` and `+0xb008` as ordered guest controls.
It accepts direct `0x81` for the TZ order and the `0x61` arm step for XBL.
It does not add a completion, a firmware response, or a version value.
The ten configuration values and the instruction payload remain guest-written.
The model tracks byte coverage, including interior holes and duplicate writes.
Start requires complete input, matching first-word state, and the clock request.
Mailbox clear or repeated start removes readiness until the guest repeats that sequence.
Unknown commands and invalid ordering return errors.

Microcode execution is an explicit functional substitute.
The model retains microcode but does not interpret it or establish its correctness.
The final mailbox request completes initialization synchronously after the required inputs and transitions.
Original image authentication remains outside this model and unchanged.
The public [Linux GPI driver](https://github.com/torvalds/linux/blob/master/drivers/dma/qcom/gpi.c) identifies later transfer interfaces.
The [Linux GENI driver](https://github.com/torvalds/linux/blob/master/drivers/soc/qcom/qcom-geni-se.c) supplies complementary programming definitions.
These sources provide protocol references, not copied implementations.

### Native proof and regression

SM8850 run128 records all five selected engines at original function `0x148a0054`.
Each programming call returns zero at `0x1489f854`.
The complete initialization function returns Boolean one at `0x1489a178`, before its wrapper discards that result.
This distinction prevents a later image-load message from concealing failed initialization.
The retained register evidence is `build/private/run-sm8850-common-runtime128/qup-probe.json`.

The integrated suite passes all 93 tests, including the original UART output and interrupt checks.
The GSI fixture covers both selected extents, incomplete inputs, invalid ordering, initialization, clear, restart, and unsupported commands.
Clippy passes with warnings denied.
These results establish QUP programming, not SPI/I2C/GPI execution, UEFI, ABL, or kernel boot.

## Both OEM ABL-to-Linux console gates

### Static package evidence

This audit reads the two private packages named in [observer-sites.md](../../../docs/qcom/sm88xx/oplus/observer-sites.md).
It does not execute firmware or establish a Linux boot.
The existing observer identities and entry requirements remain unchanged.

The console contract, both vendor-boot identities, and the stdout node are in
[the section above](#selected-post-ddr-abl-and-linux-consumers).
This audit adds these facts:

| Object | SM8850 / CPH2749 | SM8845 / CPH2771 |
| --- | --- | --- |
| First bundled DTB SHA-256 | `e4705a5f76c1d84ccfd0882abbeb761ffb3d1b59b02873fe20ee170e8798f23a` | `393c2fd6b9462fbf3bde055ae2fb9ccc2bf0da372e816fba5cc2f021afe368ce` |
| Embedded Linux version | `6.12.23-android16-5-gb3b66ace21e0-ab14672634-4k` | `6.12.38-android16-5-g8c67d4274c0a-ab14275539-4k` |
| DTBO entry count | 8 | 4 |
| ABL UART argument string literal RVA | `0x5ab7c` | `0x5a63f` |

The kernel hashes and lengths remain exactly the
[observer sites](../../../docs/qcom/sm88xx/oplus/observer-sites.md) values.
No identity, size, or entry requirement changed.

The DTB count follows the vendor header byte count at offset 2100.
Each FDT header length locates the next entry.
The 7,560,891-byte and 5,063,497-byte areas decode without trailing bytes.
The boot-image command lines are empty.
Both vendor command lines are exactly:

```text
video=vfb:640x400,bpp=32,memsize=3072000 log_buf_len=2M nosoftlockup console=ttynull qcom_geni_serial.con_enabled=0 bootconfig buildvariant=user
```

The console node also declares clock name `se`.
Its parent declares `m-ahb` and `s-ahb`.
Clock and pinctrl phandles differ between bundled DTBs.
They must resolve inside the tree that ABL actually passes.
No base `/chosen/bootargs` contains a `console=` or `earlycon` argument.
Their log policy is `loglevel=6` and `log_buf_len=512K`.
These are inputs. They are not ABL's final command-line merge.

All DTBO entries reference the console symbol `qupv3_se7_2uart`.
The console fragments set `status = "okay"` and replace `pinctrl-1`.
They keep the compatible, address, interrupt, and default pinctrl.
No inspected overlay writes `bootargs` or `stdout-path`.
This does not identify the base DTB or overlay that ABL selects.

Both ABL images contain `ENABLE_UART:TRUE` and this argument string:

```text
 console=ttyMSM0,115200n8 earlycon
```

[bringup.md](../../../docs/qcom/sm88xx/oplus/bringup.md#stock-oem-console-option) records the gate that selects this string.
The existing `--enable-oem-uart` option writes that record only in run media.
Do not replace it with signed-image edits or injected command lines.
Actual entry FDT bytes must establish the final arguments and stdout node.

### Existing implementation and remaining ordered constraints

This audit found no independent host-capture gap.
The runner is `tools/qcom/qcom-vp/src/main.rs`, not a separate `runner.rs`.
The existing UART model and board wiring already cover the selected console address and SPI 835.
The DT aperture is larger than the implemented register window.
That difference alone does not justify extra registers without a selected consumer.

The remaining gates, in execution order, are:

1. Complete genuine secure boot exit and enter the selected OEM UEFI.
2. Let UEFI publish real storage protocols and dispatch the authenticated ABL.
   ABL strings identify `vendor_boot`, DTBO loading, and vbmeta validation consumers.
   They do not prove protocol completion or select a slot.
   Preserve GPT, selected-slot media, AVB inputs, and original authentication decisions.
3. Let ABL select and load the kernel, ramdisks, and device tree from that media.
   The stock reserve-record consumer must append the UART arguments itself.
   Do not infer successful loads from strings or overwrite the final FDT.
4. Observe the actual ExitBootServices target and its zero return.
   The observer follows ABL's GetMemoryMap retry loop without host-generated success or retries.
5. Observe ABL's `blr x19`, check the complete loaded kernel hash and entry state, and complete the first kernel instruction.
   `x0` must identify the valid FDT. `x1`, `x2`, and `x3` must be zero.
   Existing EL, security, MMU, byte-order, and interrupt-mask checks remain required.
6. In continuation mode, require the debugger's `OK` reply to detach.
   Then allow the actual kernel to initialize earlycon and later the normal console.
   Earlycon needs the final stdout node and retained GENI state.
   Normal console probing additionally consumes clock, pinctrl, resource, and interrupt services.
   Those Linux paths remain OEM-unobserved. Do not add speculative acknowledgements to satisfy them.
7. Retain accepted guest TX bytes in `qemu-uart.log`.
   Evaluate those bytes separately from the observer verdict.
   ODP work remains outside this gate until actual kernel handoff.

The cheapest decisive probe is the existing automatic observer with continued capture and the stock UART option.
Its `entry-fdt.bin` records the actual ABL result once the entry gate passes.
`loaded-kernel.bin` and `kernel-entry.json` retain the independent identity and execution proof.
`result.json` records entry proof, detach status, termination, and raw UART length separately.
Neither a banner nor a nonempty UART file substitutes for entry proof.
Neither verified entry nor detach proves a retained Linux log.

Each accepted UART FIFO write flushes its bytes before the MMIO transaction completes.
The host leaves that raw file intact after observer failure, detach failure, timeout, or child failure.
`console.log` and `system.log` contain separate platform output.
Retention compression iterates only its five log names.
`qemu-uart.log` and `console.log` are absent from that list, so pruning never compresses or removes them.
The original execution deadline includes observation and continued execution.
A timeout remains a failed run even when entry proof and useful console bytes survive.

### Smoke commands after integration

This audit did not run builds, tests, or OEM scenarios.
After integration, the existing checks can run:

```sh
cargo test --locked profiles_require_exact_kernel_identities
cargo test --locked pruned_logs_remain_eligible_but_uart_and_console_remain_readable
cargo test --locked uart_transmits_exact_guest_bytes_and_delivers_irqs -- --include-ignored
```

The guest check requires the existing built platform and fixture toolchain.
It proves exact binary TX and IRQ behavior, not OEM Linux output.
The current full platform check remains `make test`.

Use distinct new output directories for the two OEM continuation runs.
Substitute the current prepared roots, without changing their checked provenance:

```sh
build/install/bin/qcom-vp run --prepared "$PREPARED_SM8850" --out "$OUT_SM8850" --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
build/install/bin/qcom-vp run --prepared "$PREPARED_SM8845" --out "$OUT_SM8845" --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
```

Run the OEM scenarios sequentially.
Before entry, the expected decisive artifact is the first actual rejected access or failed firmware transition.
After entry, require complete proof and confirmed detach before classifying later UART bytes as a Linux-console candidate.
The entry FDT must show the actual selected stdout node and merged UART arguments.
This static audit produced no Linux log.

## Real SM8850 kernel output

Run919 verifies kernel entry, then Linux writes `0xffffffff` to `0xa9cc44` during early-console initialization.
The exact kernel's [GENI helper](https://github.com/aosp-mirror/kernel_common/blob/b3b66ace21e0/drivers/soc/qcom/qcom-geni-se.c) calls `geni_se_irq_clear` before FIFO setup.
Its [register definitions](https://github.com/aosp-mirror/kernel_common/blob/b3b66ace21e0/include/linux/soc/qcom/geni-se.h) identify DMA TX status and clear at `0xc40` and `0xc44`.
The RX pair is at `0xd40` and `0xd44`.

The model now exposes both status banks and their write-one-to-clear controls.
The existing masks and SE gates select each bank's contribution to the IRQ line.
No DMA event producer exists, so the banks remain clear.
DMA transfer requests remain unsupported and fail.
The UART fixture clears both banks before it checks exact FIFO output and interrupts.

Run921 verifies the complete kernel hash, the first kernel instruction, and debugger detach.
The loaded image hash is `2609ff5297e2716fd061daf3f51661b4ea068ed96cd17a320bef32d7d3d71393`.
The final FDT contains `earlycon` and selects `/soc/qcom,qupv3_1_geni_se@ac0000/qcom,qup_uart@a9c000:115200n8`.
The UART contains 4,573 kernel bytes after the UEFI exit marker.
Linux reports version `6.12.23-android16-5-gb3b66ace21e0-ab14672634-4k` and reaches interrupt initialization.
The [execution evidence](../../../docs/qcom/sm88xx/oplus/bringup.md#status) identifies the retained logs.

Run922 repeats the result without temporary diagnostics and emits another 4,573 kernel bytes.
The complete host suite passes 102 unit checks and 96 platform checks.

The run then fails on an unsupported timer read at `0x17810008`.
The UART also reports that the distributor supports extended interrupt ranges but CPU0 does not.
These limits prevent a claim of complete kernel initialization or Android boot.

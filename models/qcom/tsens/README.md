# TSENS v2 boot group

## Model registration

Include `models/qcom/tsens/tsens.h` in the model registration unit. Register these types:

- `qcom,tsens-v2` → `qcom_vp::QcomTsensV2`.
- `qcom,tsens-calibration-profile-v1` → `qcom_vp::QcomTsensCalibrationProfile`.

Apply the selected helper once, after the APSS router exists and before XBL executes its thermal callbacks:

- `platforms/qcom/sm88xx/sm8850/tsens.lua` for SM8850.
- `platforms/qcom/sm88xx/sm8845/tsens.lua` for SM8845. `platforms/qcom/sm88xx/sm8845/runtime.lua` composes this helper.

Each controller has separate `srot_socket` and `tm_socket` windows. Socket addresses are relative. Do not combine their address spaces.

The helper maps seven controller pairs and one four-byte calibration selector. It rejects overlapping APSS targets. Do not enlarge the generic fuse aperture. If another target owns `0x221c2d18`, replace only that word's ownership before composition.

## Exact selected SM8850 evidence

The selected XBL is `CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES/xbl_sc.elf`.
Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
The addresses below come from its AArch64 instructions, decoded with Capstone and ELF PT_LOAD mappings.

The sibling `xbl_config.elf` has SHA-256 `c35a368853ba57f00c9486d4b8fadeb0ded3a59ce410818a69ac415e3c44d3ef`.
Its DTBs start at file offsets `0x2ac` and `0x10e14`. Their TSENS and boot-temperature properties agree.

| Selected entry | Contract |
| --- | --- |
| `0x14831388` | Looks up interface `0x22` through `0x1482cac4`, then calls table offset `+8`. Nonzero initialization status enters the existing firmware error path. |
| `0x148f0468` | Contains the initialization callback address `0x1486db34`. |
| `0x1486db34..0x1486e60c` | Parses the DT configuration, reads hardware revision, selects calibration, programs each channel, masks unused thresholds, enables controllers, and waits for conversions. |
| `0x148313e0` | Looks up interface `0xa`, calls `0x14870e08`, and preserves the firmware shutdown/error policy. |
| `0x14870e08..0x14870f40` | Reads `/soc/BootTempBsp/BootTempThres`, obtains a sample through `0x1486e610`, converts deci-degrees to degrees, and compares the guest thresholds. |
| `0x1486e610..0x1486e748` | Resolves controller/channel and tries up to three sample reads through `0x148a57c0`. |
| `0x148a57c0` | Reads TM `0xa0 + channel*4`. Bit 21 is validity. Bit 11 selects sign extension. |
| `0x148a5a3c` | Clears or sets SROT control bit 0. |
| `0x148a5a2c` | Programs the channel enable mask in control bits 3..18. |
| `0x148a5aa4` | Selects direct temperature mode with control bit 21. |
| `0x148a5acc` | Selects internal/external ADC clock with control bit 2. |
| `0x148a5af4` | Writes per-channel calibration at SROT `0x60 + channel*4`: zero code bits 0..9, slope bits 10..22, shift selector bits 23..24. |
| `0x148a5b20` | Writes sensor selection at SROT `0xa0 + channel*4`. |
| `0x148a5948` | Programs TM lower/upper thresholds at `0x20 + channel*4`, and critical thresholds at `0x60 + channel*4`. |
| `0x148a59a8` | Programs SROT minimum/maximum thresholds at `0x20 + channel*4`. |
| `0x148a5840` | Programs inverted lower/upper masks at TM `0x10`, and critical masks at `0x1c`. |
| `0x148a5800..0x148a583c` | Enables or disables thermal interrupt classes at TM `+4`. |
| `0x148a5c10` | Programs sideband enable at `+0xe8`. For revision `0x20000000`, initialization uses the TM window. |
| `0x148a5c34` | Reads only `0x221c2d18`, extracts bits 27..29, and translates the calibration selector. Selector zero returns calibration mode zero. |
| `0x1486de98..0x1486df78` | Calibration mode zero uses the DT default points. It does not read per-controller or per-sensor calibration fuse fields. |
| `0x1486e580..0x1486e5fc` | Enables initialized controllers, then waits `uSensorConvTime_us * maximum_enabled_channel_count`. |

Control bit 1 is software reset. The selected initialization does not require a software-reset pulse.

## Selected SM8850 board data

The DT supplies these values. They are not values from the supporting rehost's constructed handoff.

- SROT bases: `0x0c222000` through `0x0c228000`, stride `0x1000`.
- TM bases: `0x0c229000` through `0x0c22f000`, stride `0x1000`.
- Channel counts: `5, 12, 7, 4, 8, 12, 7`. Total: 55.
- Conversion time: 30 microseconds per channel.
- Default calibration codes: 480 at 30 C and 714 at 120 C. Shift: 10.
- Minimum threshold: -40 C. Maximum threshold: 130 C.
- Lower, upper, and critical thresholds: `0x7fff`, the firmware's unused-threshold marker.
- Active period: zero for controllers 0..5, 64 for controller 6. Sleep period: 64 for all controllers.
- Watchdog load: 9850. Zero-degree thresholds: 5 C and 10 C.
- Boot-temperature policy: sensor 0, -150 C through 150 C, 40 retries, 500000 microseconds per retry.

The guest reads these properties from its real DT. The model does not inject them into guest memory.

## Selected SM8845 evidence and board data

The private source package is `CPH2771export_11_16.0.1.305EX01_2025121000260242.zip`.
Its `IMAGES/xbl_config.elf` has SHA-256 `2effcfac7784d044a34dbff04c0da063c17806083707af285952b75964d8376c`.
Independent decoding of its DTBs at file offsets `0x30c` and `0xf52c` gives matching thermal properties.
This is static evidence, not native execution evidence. No source bytes belong in the repository.

The DT root `/soc/TsensBspSettings` has compatible `qcom,TsensBspSettings`.
Its `TsensBspType` node declares seven controllers, 49 sensors, and `uSensorConvTime_us = 30`.
The `sensorconfigs/sensorconfigs_0..48` nodes assign contiguous channels through their `controller` and `ucChannel` properties.
The `controllerconfigs/controllerconfigs_0..6` nodes supply these windows:

| Controller | Channels | SROT base | TM base |
| --- | --- | --- | --- |
| 0 | 5 | `0x0c222000` | `0x0c229000` |
| 1 | 12 | `0x0c223000` | `0x0c22a000` |
| 2 | 7 | `0x0c224000` | `0x0c22b000` |
| 3 | 4 | `0x0c225000` | `0x0c22c000` |
| 4 | 5 | `0x0c226000` | `0x0c22d000` |
| 5 | 8 | `0x0c227000` | `0x0c22e000` |
| 6 | 8 | `0x0c228000` | `0x0c22f000` |

Each window occupies `0x1000` bytes. All seven active periods are zero. Each sleep period is 64.
The default calibration codes are 480 at 30 C and 714 at 120 C, with shift 10.
The minimum and maximum thresholds are -40 C and 130 C. Lower, upper, and critical thresholds use `0x7fff`.
The node `/soc/BootTempBsp/BootTempThres` selects sensor zero, -150 C through 150 C, and 40 retries at 500000 microseconds.
These properties remain guest-owned configuration. The helper does not inject DT values into guest memory.

The selected `IMAGES/xbl_sc.elf` has SHA-256 `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
The [post-APDP dependency map](../../../docs/qcom/sm88xx/boot-dependencies.md#exact-post-apdp-xbl-order) records its real configuration and thermal callback order.
Static decoding identifies initialization interface callback `0x1482f498` and boot-temperature callback `0x1482f4f0`.
Its HAL uses the shared v2 contract:

- Sample method `0x148a3e54` reads TM `0xa0 + channel*4`, with validity bit 21 and sign bit 11.
- Methods `0x148a40b0`, `0x148a40a0`, and `0x148a4118` control enable, channel mask, and temperature mode.
- Method `0x148a4168` writes calibration at SROT `0x60 + channel*4`.
- Method `0x148a42b0` reads calibration selector `0x221c2d18[29:27]`.

SM8845 reuses `qcom,tsens-v2` without a model change. Its channel counts remain SoC-specific Lua data.
Both helpers use the same explicit virtual policy: 25 C, 30 microseconds per channel, and selector zero for default calibration.
This policy does not claim measured temperature, captured fuse values, or native callback completion.
The SM8845 helper preserves the SM8850 pattern's missing-router, duplicate-instance, duplicate-profile, and APSS-overlap guards.

## Native auto-adjust failure and recovered contract

SM8845 run27 reaches the original store at `0x148a4098`, with SROT base `0x0c222000`.
A later capture recorded `w8=0x81600000`, `w9=0x81200000`, `x1=1`, and return address `0x1486c7dc`.
This proves that the selected native initialization sets bit 22. It does not prove conversion completion.

Bit 22 is `AUTO_ADJUST_PERIOD_EN`, not an external-clock request.
It selects automatic period changes during sleep entry and exit.
The selected OEM instructions independently establish the offsets and masks below.

| Register or operation | SM8845 helper / producer | SM8850 helper / producer | Meaning |
| --- | --- | --- | --- |
| SROT `+8[7:0]` | `0x148a4060` / `0x1486c7c0` | `0x148a59ec` / `0x1486e0a4` | Active measurement period |
| SROT `+8[15:8]` | `0x148a4070` / `0x1486c7cc` | `0x148a59fc` / `0x1486e0b0` | Sleep measurement period |
| SROT `+4[22]` | `0x148a4080` / `0x1486c7d8` | `0x148a5a0c` / `0x1486e0bc` | Automatic active/sleep period selection |
| SROT `+4[2]` | `0x148a4140` / `0x1486c7e4` | `0x148a5acc` / `0x1486e0c8` | ADC clock, zero selects internal oscillator |
| SROT `+4[18:3]` | `0x148a40a0` / `0x1486c7f0` | `0x148a5a2c` / `0x1486e0d4` | Enabled channels |
| SROT `+0x60+4*n` | `0x148a4168` / `0x1486c990` | `0x148a5af4` / `0x1486e290` | Per-channel calibration |
| SROT `+4[0]` | `0x148a40b0` / `0x1486cca8` | `0x148a5a3c` / `0x1486e5a8` | Enable after configuration |
| TM `+0xa0+4*n` | `0x148a3e54` | `0x148a57c0` | Signed temperature, validity bit 21 |

Both producers disable controllers before configuration. They program periods, auto-adjust, internal clock, channel masks, thresholds, and calibration before final enable.
The later clock calls at `0x1486c854` and `0x1486e138` use the controller's clock property.
Neither selected platform supplies an external ADC clock producer.

The [public Linux v2 driver](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/thermal/qcom/tsens-v2.c) independently defines reset bit 1, enable bit 0, channel bits 3..18, and temperature bit 21.
It also defines calibration fields, TM validity bit 21, and `TRDY` at `+0xe4`.
Its reset sequence retains configuration writes while reset remains asserted. It does not define auto-adjust bit 22.

The bounded environment remains active. With auto-adjust enabled, the controller uses the active period, not the sleep period.
No synthetic sleep register or power-state bus interface exists.

Period zero converts continuously. Codes 1..254 add the documented approximate interval between completed scans.
The interval is `(code >> 6) * 250 ms + (code & 63) * 1.95 ms`.
Code 255 performs one scan and retains its valid result until reconfiguration.
The first scan still needs the configured time per enabled channel. The period does not delay that first scan.

Completed samples remain valid between scans. Reads do not restart conversion.
An unchanged configuration write also does not restart conversion.
Software reset holds conversion and invalidates the sample. It preserves calibration and configuration registers.
Reset release starts a new scan only when every conversion prerequisite holds.

The selected fixed virtual environment makes repeated scans return the same temperature.
The model latches that temperature only at an eligible completion observed through TM access.
It does not reconstruct unobserved historical changes to the environment parameter.
Sleep transitions, external clock delivery, analog conversion accuracy, and hardware timing tolerances remain outside this contract.

## Watchdog, TBCB, and remaining selected initialization

SM8850 run30 and SM8845 run28 pass bit 22, then stop at SROT `+0x140`.
The original stores are `0x148a5b90` and `0x148a420c`, respectively.
This register is `TSENS_WATCHDOG_CTRL`, not `TSENS_TBCB_CONTROL`.
The prior rejection of bit 0 incorrectly rejected the selected watchdog configuration.

A read-only SM8845 breakpoint confirms `x0=0xc222000`, `x1=1`, `x2=1`, `x3=0x267a`, and `w8=0x499e9`.
The return address is `0x1486c8c4`. This confirms the selected native watchdog arguments and store value.

The bounded register groups are:

| SROT offset | Fields |
| --- | --- |
| `0xec` | TBCB client enables `15:0`, clock divider `23:16`, acknowledge delay `31:24` |
| `0xf0..0x12c` | Sixteen client request words, sensor masks `15:0` |
| `0x130` | TBCB override enable bit 0, command bit 1, data `17:2` |
| `0x140` | Watchdog enable bit 0, trigger state bit 1, load `17:2`, self-reset bit 18, disable selector bit 19 |
| `0x18` | Zero-degree comparator enable bit 0 and channel mask `16:1` |
| `0x1c` | Zero-degree set threshold `23:12` and reset threshold `11:0` |
| `0x150` | Legacy AMUX selector written by the selected helpers |

The watchdog load unit is sleep-clock ticks.
Normal watchdog programming preserves the trigger state and disable selector.
The trigger values are `IDLE` and `GLOBAL_MAX`.
The disable-selector values are `TBCB_ONLY` and `MAIN_TSENS_EN`.
These names do not establish the exact expiry action, start condition, or reload condition.
The model does not claim those unproved details as silicon behavior.

Independent DT decoding checks all seven controllers in each selected image.
Every controller enables the watchdog and self-reset with load 9850. Normal programming produces `0x499e9` from the virtual cold state.
Every controller has `pauTbcbReqs=0`, `uNumTbcbClients=0`, standalone mode zero, and PWM mode zero.
The client-programming loops therefore skip their bodies in both selected OEM sequences.

The helper pairs `0x148a424c` / `0x148a5bd0` would program request words and update corresponding client-enable bits.
The selected initialization does not call them.
Active TBCB requests and overrides still fail closed because no external TBCB consumer exists.
Inactive controls and zero request words retain their programmed values.

The watchdog uses an explicit **normal-progress functional substitute**.
`sleep_clock_hz=32768` declares its virtual rate. This is not a measurement or a recovered OEM clock frequency.
The model preserves watchdog programming while the controller is disabled or held in software reset.
On operational enable, every enabled channel must have a valid conversion configuration.
The complete scan cadence must be strictly less than `load / sleep_clock_hz`.

This invariant also applies to later configuration changes. Failed writes restore the previous configuration.
One-shot scans, zero loads, active trigger states, and the alternate disable selector remain unsupported while the armed controller operates.
Configurations that can expire fail closed. The model does not fabricate timeout resets or silently suppress expiry.

For the selected policy, 9850 ticks permit approximately 300.6 milliseconds.
The longest selected cadence is SM8850 controller 6: 250 milliseconds plus seven 30-microsecond channel conversions.
Every selected controller satisfies the normal-progress bound.
The bound does not make temperature ready. Actual virtual conversion time must still elapse.

TM `+0x14[31]` is the watchdog bark event status.
TM `+0x13c[7:0]` is the bark-event count, not a free-running elapsed-time counter.
Both remain zero because admitted operation cannot expire. Neither field grants conversion readiness.

TM `+0xe0[0]` reports the zero-degree comparator state.
The virtual comparator asserts below the programmed set threshold, clears above the reset threshold, and retains state between thresholds.
It requires the comparator enable bit and completed samples from selected enabled channels.
The external comparator output remains disconnected.
The selected 25 C environment remains above the OEM reset threshold of 10 C.
Sideband enables retain the selected channel masks without inventing an external consumer.

Static analysis covers the remaining SM8845 initialization through final enable and the conversion wait.
The matching SM8850 group uses the same access classes.
After watchdog programming, both sequences program zero-degree controls and the legacy AMUX selector.
They then program channel IDs, calibration, SROT minimum/maximum controls, TM thresholds, and interrupt masks.
The selected unused-threshold markers disable thermal interrupt classes.

Final enable occurs at `0x1486cca8` / `0x1486e5a8`, followed by the firmware's conversion wait and existing sample helpers.
No additional TBCB acknowledgement or readiness register appears in these selected paths.

## State and boundaries

Cold reset disables conversion, removes calibration, and masks interrupts. This is an explicit virtual reset policy, not a measured silicon reset image.
The revision register reports the compatible virtual v2.0 value `0x20000000`.

The board profile returns zero only for the calibration selector word. This selects the actual firmware's default-calibration path.
It is not a captured fuse value. Other fuse words remain unmapped by this group.

The model supplies a deterministic virtual environment of 25 C. It does not claim a physical measurement or calibration accuracy.
A sample becomes valid only after the guest selects temperature mode, enables channels, and writes nonzero calibration slopes for all enabled channels.
Reset, bypass, and clamp must remain clear. The first scan needs 30 microseconds per enabled channel.

Disable, reset, and changed conversion configuration invalidate the previous scan. Threshold writes do not restart conversion.
The model evaluates all five threshold classes from the guest registers. The guest still executes its own thermal decision code.

The selected configuration uses the internal ADC oscillator. The decoded initialization group does not call an existing GCC clock API.
Therefore this group has no dependency on the existing PLL or root-clock paths. External ADC clock requests fail closed.

This bounded model supports masked boot operation. Interrupt enable requests fail closed because this group has no interrupt sink.
TBCB activation fails closed. PWM and power-removal dynamics remain outside the virtual environment.
Unknown register offsets, writes to sample registers, byte enables, and non-word accesses fail closed.
Analog dynamics, periodic thermal variation, watchdog expiry, and automatic power removal are outside this virtual environment.

Watchdog support uses the normal-progress bound above. Zero-degree controls drive the virtual hysteresis state, not resets or firmware handoff state.

## Verification

The build, Clippy, and all 67 tests pass.
The `tsens_oem_auto_adjust_calibration_reset_and_conversion` guest exercises calibration prerequisites, delayed validity, unchanged writes, reset, disabled channels, and controller disable.
It also checks watchdog programming, bark status and count, the comparator enable gate, and hysteresis.
The `tsens_rejects_a_watchdog_shorter_than_conversion` guest accepts disabled programming, then rejects operational enable at `0x0c222004`.

SM8850 run31 and SM8845 run29 complete the original `tsens_init_if` and `boot_check_device_temp` callbacks.
Their retained UART logs contain both completion markers.
No firmware patch, temperature-policy bypass, or pre-initialized handoff produces these markers.
SM8850 next reads unmapped PMIC initialization address `0x0c469004`.
SM8845 next reaches a rejected SPMI command at `0x0c90ac00`.
Neither run establishes UEFI, ABL, or kernel execution.

The canonical logs are:

- `build/private/run-sm8850-common-runtime31/qemu-uart.log`
- `build/private/run-sm8845-common-runtime29/qemu-uart.log`

Run the retained behavioral regressions with:

```sh
cargo test --test platform tsens_ -- --ignored
```

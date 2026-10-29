# NCC adaptive clock distribution

## Source

The PDP instance table supplies the base of this block for each lane.
Lane zero is local `0x409d0000` and system `0x199d0000`, and lane one adds `0x01000000`.
The PDP reaches its lane at local address `0x409d0000` through the NCC window in `platforms/qcom/sm88xx/pdp-runtime.lua`.
The PDP image names its driver `nccpalfw/drivers/power/acd/acd_calibration.c`.

## Instances

The PDP instance table starts at link `0x8131d0c8` with a stride of `0x188` bytes.
Each entry holds a local base at `+0x0` and a type byte at `+0x184`.

| Entry | Local base | Type | System address, lane zero |
| --- | --- | --- | --- |
| 0 | `0x409d0000` | 0 | `0x199d0000` |
| 1 | `0x40aa2000` | 1 | `0x19aa2000` |

SM8850 adds an auxiliary block at `0x19aa0000`, and its PLL at `0x19aa1000` has the same origin.
Both entries use the same driver functions and register offsets, so the model uses one layout for both.
[INFERENCE] The register set of entry 1 is the same as the register set of entry 0.

## Calibration request

UEFI `CalibrationDxe` sends SCMI request `0x12` with type two to the PDP of a cluster.
The PDP prepares the cluster in `0x81320a64`. See "Core C-state report" in `models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md`.
Then the PDP performs these steps for each ACD instance in its table:

1. `0x81321a38` programs `ACDTD` (`+0x8`), `ACDCR` (`+0x4`), and the averaging words `+0x4c..+0x54`.
2. `0x8132120e` writes `ACDSSCR` (`+0x28`) and then `ACDCR` (`+0x4`) from a table pair.
3. `0x813211c0` sets `ACDCR.UPDATEAUTOCAL` (bit 24) and clears it again.
4. `0x813210b4` reads `ACD_MISC_STATUS.CALIB_DONE` (`+0x44` bit 1).
   The caller polls it 1000 times with a delay of one microsecond.
5. `0x81321202` reads `ACD_DVMRC_CAPTURED_CODE` (`+0x110`) and stores the value.

If `CALIB_DONE` stays clear, the PDP prints `ACD Cal: Calibrate error Addr` and fails the request.
After the loop, `0x81321704` writes the post-calibration configuration.
That function does not compare the captured code with a limit.
`0x813211e8` and `0x81321214` compare and write `RDVMCR` (`+0xcc`) and `RDVMSSCR` (`+0xd8`).
`0x81321208` reads `RDVM_READOUT` (`+0xe0`).

## Model

`models/qcom/sm88xx/pdp/acd.h` (`qcom,ncc-acd-v1`) has one instance for each lane.
It accepts only aligned 32-bit accesses at the 58 declared offsets.

- Each register starts at its reset value.
- A register with access `RW` retains each write.
- A write to a register with access `R` fails.
- An offset that is not declared fails.
- `ACD_MISC_STATUS` bits 1:0 are status. The upper bits retain writes.
- `ACD_READOUT` (`+0x98`) and `RDVM_READOUT` (`+0xe0`) return the reset value for selection zero.
  For another selection, the read fails and the model logs `PDP_ACD_READOUT_UNSUPPORTED`.

## Calibration

A rising `UPDATEAUTOCAL` edge in a write to `ACDCR` runs one calibration.
The model clears `CALIB_DONE`, copies `ACDDVMRC` (`+0x14`) to `ACD_DVMRC_CAPTURED_CODE`, and sets `CALIB_DONE`.
It logs `PDP_ACD_CALIBRATION` with the `ACDCR` value and the captured code.
A held bit does not start another calibration.

Real hardware measures the delay of the clock path and captures the result.
The model has no clock path, so it reports the configured delay code as the result.
`docs/qcom/sm88xx/stubs.md` records this substitute.
The status, counter, and debug registers keep their reset values, because no modeled event changes them.

## Verification

The `pdp-power-controls` fixture checks the reset values, the calibration edge, the captured code, the status mask, and the failures.

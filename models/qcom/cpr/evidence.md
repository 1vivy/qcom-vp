# RPMH CPRF evidence

## Block identity

The block is `RPMH_CPRF_CPRF` at `AOSS+0x01200000` with size `0x10000`.
The AOSS base is `0x0b000000`, so the block aperture is `0x0c200000` through `0x0c20ffff`.
The register names and initial words come from a private layout. SM8850 reset values remain unmeasured. The selected binary defines the access shape.

`models/qcom/cpr/cprf.h` implements `qcom,rpmh-cprf-v1`.
`platforms/qcom/sm88xx/sm8850/cpr.lua` places it at `0x0c200000` with size `0x10000` and checks the router overlap.

## Decoded CPRDxe accesses

The consumer is UEFI `CPRDxe` from `IMAGES/uefi.elf` in `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`.
The PE file starts at firmware volume 1 offset `0x328c4`, and its `e_lfanew` is `0xe58`.
The RVAs below are PE-relative.

| RVA | Instruction | Register | Value and condition |
| --- | --- | --- | --- |
| `0xc194` | `lsl`/`add` | address builder | `0x0c20100c + 0x1000*id + 4*entry` for `VOLTAGE_LIMITS_n` |
| `0xc1ac` | `add w9,w8,#0x80` | address builder | `0x0c20108c + 0x1000*id + 4*entry` for `SAFE_VOLTAGE_n` |
| `0xc1b0` | `str w10,[x8]` | `CPRFm_VOLTAGE_LIMITS_n` | `vmax` at bits 28:16 and `vmin` at bits 12:0, from `cpr_hal_write_cprf_voltages` |
| `0xc1b4` | `str w2,[x9]` | `CPRFm_SAFE_VOLTAGE_n` | `vmax`, the safe voltage |
| `0xc21c` | `ldr w10,[x8,x9]` | `CPRFm_CTRL` | read for the read-modify-write |
| `0xc22c` | `str w10,[x8,x9]` | `CPRFm_CTRL` | `(read & 0x1ffff & ~4) | bit(2)` |
| `0xc230` | `ldr wzr,[x8,x9]` | `CPRFm_CTRL` | second read, value discarded |
| `0xc474` | `str w9,[x10,x8]` | `CPRFm_VRM_SETTINGS` | `((vrm & 0xff00) >> 8) | ((vrm & 0xffff) << 13)` |
| `0xc49c` | `str w10,[x9,x8]` | `CPRFm_ENABLE` | `1` |

Every RVA asserts `cprf_id < 12` before the access. The block declares `CPRFm` for `m` zero through 11.

### Readback check

CPRDxe reads `CPRFm_CTRL` again at `0xc230` and discards the value.
The private layout names that field `CTRL_INTERACTION_EN`.
No selected instruction compares the word, so the block needs no error path.
The block returns the retained word to the read.

### Which rows the firmware writes

Caller `0x3a8c` writes the voltage limits of one rail. It takes the CPRF id from the thunk `0x6ae8`, which branches to `0xb238`.
`0xb238` maps a rail resource type to a CPRF id.
Types `0x100` to `0x10a` map to ids 1, 0, 6, 5, 2, 7, 8, 9, 10, and 11.
Type `0x400` maps to 4, and type `0x401` maps to 3.
Any other type returns `0xff`, and the assertion stops the image.
Loop `0x3adc`..`0x3b14` writes entries 1 through the rail mode count. Each value comes from the last entry of that mode, at `+4` and `+8` of the entry block, divided by 1000.
Loop `0x3b28`..`0x3b74` repeats the same two values for entries up to 15 when the mode count is at most 14.
The GFX plan has 19 modes, as the CPR step-counter evidence records. Its writes therefore reach entry 19 at `0x6058` and `0x60d8`.
The GFX resource type is `0x103`, which maps to CPRF 5.

Run768 stopped at the first such write. Its access is `0x0c206010` with value `0x01e801b0`.
That address is CPRF 5, entry 1, with a 488 mV ceiling and a 432 mV floor.
The value matches the GFX rail with the mapping above.

### Absent accesses

No decoded instruction reads `HW_VERSION`, `INTERRUPT_STATUS`, `CPRF_IDLE_STATUS`, or any other status word of the block.
A scan of the whole `.text` section finds four `0xc20` address builders only, at `0xc1a0`, `0xc214`, `0xc468`, and `0xc490`.
No status or completion path exists, so the block has no completion to report.

## Functional policy

The block keeps each written word and returns it on a read.
A write to a bit outside the declared field fails, and the word keeps its earlier value.
An access to every other offset of the aperture fails.

| Register | Offset | Field | Initial |
| --- | --- | --- | --- |
| `HW_VERSION` | `0x0000` | read-only `MAJOR`, `MINOR`, `STEP` | `0x100b0000` |
| `CPRFm_CTRL` | `0x1004+0x1000m` | bits 16:0 | `0x0000801d` |
| `CPRFm_VRM_SETTINGS` | `0x1008+0x1000m` | bits 28:0 | `0x00001100` |
| `CPRFm_VOLTAGE_LIMITS_n` | `0x100c+0x1000m+4n` | `VMAX` 28:16, `VMIN` 12:0 | zero |
| `CPRFm_SAFE_VOLTAGE_n` | `0x108c+0x1000m+4n` | `VSAFE` 12:0 | zero |
| `CPRFm_ENABLE` | `0x1118+0x1000m` | `EN` bit 0 | zero |

The initial words come from a private layout.
No permitted source establishes the silicon reset words.
CPRDxe reads `CPRFm_CTRL` before it writes the word, but no instruction branches on the value.

The block produces no voltage effect. The CPR controllers of this platform use ideal sensors, so each measurement error is zero.
The platform has no PMIC regulator, and no firmware reads a value that a CPRF voltage step would affect.
`models/qcom/cpr/controller.h` records the same limit for the controllers.
The block creates no interrupt, ARC handshake, or completion.

## Deviations from the private layout

The declared layout describes an older SoC. The selected binary is SM8850, and two counts differ.

1. The declared layout gives `CPRFm_CTRL` bits 15:0. CPRDxe masks the read-modify-write with `0x1ffff`, so bit 16 also survives a merge. The model keeps bits 16:0.
2. The declared layout gives 16 `VOLTAGE_LIMITS_n` and 16 `SAFE_VOLTAGE_n` entries. CPRDxe writes the rail mode count, and the GFX count is 19. The model keeps 20 entries. Entry 20 and above fail.

The model keeps these two widths because the selected CPRDxe is the authority for the access shape.
Field names and initial values still come from the private layout.

## Verification

`tools/qcom/qcom-vp/tests/fixtures/cprf/consumer.cpp` is the native fixture.
It checks the reset values, the run768 write, per-row and per-entry retention, and the entry-19 write.
It also checks field rejection, the CTRL merge, the VRM word, `ENABLE`, and cold reset.
Unknown offsets, the interrupt status, the idle status, and an 8-byte access must fail.
`tools/qcom/qcom-vp/tests/platform.rs` runs it as `cprf_retains_voltage_limits_without_creating_a_voltage` with marker `CPRF_CONFIGURATION_PASS`.

The native platform run is the remaining check. The expected next frontier is the first CPRF access that this model does not decode, or the next consumer after the CPRF programming.

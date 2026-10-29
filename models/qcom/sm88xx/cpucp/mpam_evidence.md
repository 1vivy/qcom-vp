# NCC MPAM memory-system components

## Scope

This page covers the MPAM frames of the two NCC last-level caches.
`models/qcom/sm88xx/cpucp/mpam_msc.h` implements one frame as `qcom,ncc-mpam-msc-v1`.
The CPUCP image is `IMAGES/cpucp.elf` with SHA-256 prefix `3475ceab`.

## Frames

Each frame contains four modules:

| Module | Offset | Content |
|---|---|---|
| `NCC_MPAM_S_LLC_0` | `APSS_HM+0x02860000` | Identification, error, partition selection, and monitor configuration |
| `NCC_MPAM_S_LLC_1` | `APSS_HM+0x02861000` | `MPAMCFG_CPBM` |
| `NCC_MPAM_S_LLC_2` | `APSS_HM+0x02862000` | No register |
| `NCC_MPAM_S_LLC_3` | `APSS_HM+0x02863000` | `MPAMCFG_IMPL_SLC_SCID` |

The non-secure modules start at `APSS_HM+0x02870000`.
NCC1 repeats both frames at `APSS_HM+0x03860000` and `APSS_HM+0x03870000`.
The SM8850 address rule adds `0x17000000` to that offset.
The frames are therefore at `0x19860000`, `0x19870000`, `0x1a860000`, and `0x1a870000`.
The CPUCP device names `msc0@19870000` and `msc1@1a870000` agree with this rule.

## CPUCP users

| Address | Operation |
|---|---|
| `0x1d80a004`, `0x1d80a024` | Write zero to `+0x100`, clear `DSPRI` at `+0x400`, and write one to `+0x3000` |
| `0x812d8624..0x812d863c` | Write zero to `PART_SEL` and `PRI` of the secure frame |
| `0x1d8160b2..0x1d8160fe` | Write `PART_SEL`, then `CMAX` (string `percent`) |
| `0x1d816142..0x1d81617c` | Write `PART_SEL`, then `CPBM` (string `cpbm`) |
| `0x1d8161c6..0x1d816204` | Write `PART_SEL`, then `PRI` (string `dspri`) |
| `0x812d8d32..0x812d8d5c` | Write `PART_SEL`, then the SCID word at `+0x3000` (string `cpu_scid_gear`) |
| `0x812d963e..0x812d96ec` | Write `PART_SEL`, then read `CPBM`, `CMAX`, and `DSPRI` back |
| `0x1d815fa6..0x1d816048` | Write `MON_SEL`, then the CSU or MBWU filter and control words |

The monitor writer sets `EN` and `CAPT_EVNT` in the control word.
No CPUCP instruction reads `MSMON_CSU`, `MSMON_MBWU`, or their capture registers.
No CPUCP instruction reads an identification register.
Run613 stops at the first `CPBM` write, `0x19871000` from PC `0x1d81617c`.

## Register contract

The frame implements these registers.
The offsets of `PART_SEL`, `CMAX`, `PRI`, and `MON_SEL` match the Arm MPAM memory map.
The identification words, the initial partition words, and the initial monitor words are model-supplied.
No CPUCP instruction reads an identification word, and no permitted source establishes those values.

| Offset | Register | Behavior |
|---|---|---|
| `0x00` | `MPAMF_IDR` | `0x807b01003f`. 64-bit or 32-bit reads |
| `0x08` | `MPAMF_SIDR` | `0x0001000f`. Secure frame only |
| `0x18..0x50` | Identification words | Reset values |
| `0x80..0x90` | Monitor identification | `NUM_MON` is four (secure) or sixteen (non-secure) |
| `0xf0`, `0xf8` | `MPAMF_ECR`, `MPAMF_ESR` | Retained writable fields |
| `0x100` | `MPAMCFG_PART_SEL` | Selects one of 64 partitions |
| `0x108`, `0x400` | `MPAMCFG_CMAX`, `MPAMCFG_PRI` | Retained for the selected partition |
| `0x800` | `MSMON_CFG_MON_SEL` | Selects one monitor |
| `0x808` | `MSMON_CAPT_EVNT` | Reads zero. A nonzero write fails |
| `0x810..0x828` | CSU and MBWU filter and control | Retained for the selected monitor |
| `0x1000` | `MPAMCFG_CPBM` | Twelve portion bits for the selected partition |
| `0x3000` | `MPAMCFG_IMPL_SLC_SCID` | Six SCID bits for the selected partition |

Each partition starts with `CMAX` `0xf`, `PRI` `0x30000`, `CPBM` `0xfff`, and SCID one.
Each monitor starts with control values `0x43` (CSU) and `0x42` (MBWU).
A write changes only the `RW` fields. Read-only and reserved fields keep their value.
The CPUCP monitor writer depends on this rule, because it writes the control word without `TYPE`.
The secure filter `PARTID` has four bits. The non-secure filter `PARTID` has six bits.

## Virtual policy

The model records these substitutes:

- A `PART_SEL` value above 63 or a `MON_SEL` value above `NUM_MON - 1` fails with a command error.
  The architecture records such errors in `MPAMF_ESR`. The model does not implement that error path.
- No virtual cache or memory path reports partition occupancy or bandwidth.
  The counter and capture registers at `0x840..0x890` therefore fail with an address error.
  A capture event also fails.
- The configuration does not change cache allocation or memory bandwidth.
  The virtual LLC has no partition-based allocation.

## Fixture

`tools/qcom/qcom-vp/tests/fixtures/ncc-mpam-msc/consumer.cpp` uses one secure and one non-secure frame.
It checks identification values, partition isolation, monitor isolation, read-back, field masks, and selector errors.
It also checks that counter reads and capture events fail.

## Runs

Run613 stops at the first `CPBM` write before this model.
Run614 passes all MPAM writes and stops at a later CPUCP write to `0x34c91000`.
Kernel entry remains unverified.

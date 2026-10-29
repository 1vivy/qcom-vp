# Core activity monitors

## Firmware consumer

Run607 stops when CPUCP starts ACEHR0 channel 41 at `0x181800a8`, from PC `0x1d806438`.
The start executes the published slots, and the first DMA read of `0x19010000` fails.
Run608 reports the complete slot range of the failed channel:

| Slot | Source | Destination | Width and count |
| --- | --- | --- | --- |
| 728 | `0x19010000` | `0x1d8335b0` | 8 bytes, 4 beats |
| 729 | `0x19010100` | `0x1d8335d0` | 8 bytes, 8 beats |

A periodic CPUCP timer callback at `0x1d808026` starts this transfer through `0x1d807722`.
The CPUCP lane table at `0x1d82e710` pairs each lane's first AMU base with its core base:
`0x19010000` with `0x19000000`, and `0x1a010000` with `0x1a000000`.

## Register contract

The CPUCP lane table supplies the AMU base `0x19010000` for core zero of lane zero, and the ACEHR source address agrees.
Each core uses the next MiB, and lane one adds `0x01000000`.
The block is the memory-mapped view of the Arm AMUv1 activity monitors.
The model implements the counter, enable, and control offsets that the CPUCP consumer reaches.

| Offset | Register | Model |
| --- | --- | --- |
| `0x000..0x018` | `AMEVCNTR00..03` | Group-zero counters |
| `0x100..0x138` | `AMEVCNTR10..17` | Group-one counters |
| `0x400..0x40c` | `AMEVTYPER00..03` | Read-only `0x11`, `0x4004`, `0x08`, `0x4005` |
| `0x480..0x49c` | `AMEVTYPER10..17` | Retained 32-bit event types |
| `0xc00`, `0xc20` | `AMCNTENSET0`, `AMCNTENCLR0` | Group-zero enables |
| `0xc04`, `0xc24` | `AMCNTENSET1`, `AMCNTENCLR1` | Group-one enables |
| `0xce0`, `0xe00` | `AMCGCR`, `AMCFGR` | Read-only `0x804`, `0x11003f0b` |
| `0xe04` | `AMCR` | Retained 32-bit control |
| `0xe08`, `0xfa8..0xffc` | Identification | Read-only values |

The event types identify counter 0 as core cycles, counter 1 as constant-frequency cycles, counter 2 as retired instructions, and counter 3 as memory stalls.
The model supplies those words and the identification words.
No CPUCP instruction reads them, and no permitted source establishes their values.
No permitted source establishes the silicon reset value of the enable words, so the model starts them cleared as a declared initial state.

## Functional policy

Counter 0 counts at the lane PLL output while its enable bit is set.
A PLL change closes the interval at the previous rate.
Counter 1 counts at the 19.2 MHz reference while its enable bit is set.
A disabled counter keeps its value, and software can write a counter.

No platform source supplies retired instructions, memory stalls, or the eight auxiliary events.
Enabling counter 2, counter 3, or a group-one counter therefore fails with a command error.
This failure shows the next consumer instead of a fabricated count.

The model does not reduce counter 0 while a core waits in WFI or loses power.
No current consumer distinguishes these states.
Counters and enables start cleared as a declared initial state.
Run609 shows that no firmware writes an enable, so the snapshot copies retained zero counters.
No firmware decision in the selected images depends on a silicon counter or enable reset value.

## Verification

The native fixture drives a real Zonda PLL.
It checks the constants, a disabled interval, both counting rates, and a mid-interval frequency change.
It also checks a cleared enable, the rejected enables, 32-bit halves, holes, and cold reset.
Run609 passes the channel 41 snapshot and stops at a CPUCP read of the RPMh command database at `0x0c3f0000`.

Run620 stops when ACEHR0 channel 40 copies the core-one AMU at `0x19110000`.
The router reported every AMU page after core zero as shadowed.
The sparse per-core bank of each lane starts at `0x19032000` and spans all cores of that lane.
That bank answers only its declared words, but its span covered the AMU pages.
The bank now has router priority one, so the exact AMU owners win their pages.

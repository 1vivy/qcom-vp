# NCC CMU packed root

## Inputs

This contract uses the selected CPH2771 `IMAGES/xbl_sc.elf`.
Its SHA-256 is `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
The package and identity policy remain in `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md`.
No firmware bytes belong in Git.

The selected ELF supplies the installed addresses and selector records.

## Selecting condition

This is a reachable cold-boot path. It is not an SM8850 AOSS descriptor.
The domain name at `0x148da6f6` is `ncc0_ncc_clk_src`.
Its descriptor at `0x148effb8` contains:

| Offset | Meaning | Selected value |
| --- | --- | --- |
| `0` | Rate table pointer | `0x148eff98` |
| `8` | Active rate pointer | Zero in the selected image |
| `0x10` | Configuration base | `0x199a2010` |
| `0x14` | Domain flags | `0x2208` |
| `0x19` | Rate record count | One |

`0x1485eedc` is the initialization wrapper.
It skips initialization only when bit zero at `0x1492aa94` is already set.
The selected image initializes that flag to zero.
Otherwise, `0x1485ef00` calls `0x1489f138`, which branches to `0x1484d194`.
The wrapper sets its completion flag only after the routine returns zero.

Within that routine, `0x1484d220..0x1484d230` selects the last compatible record with performance level at most `0x100`.
The selector is `0x1485e714`. It uses 28-byte records and calls compatibility predicate `0x1485e4f4`.
The only record's version range and chip restriction are zero, so the predicate accepts it without a version restriction.
`0x1484d258..0x1484d260` skips the packed sequence only when the active pointer already equals that record.
Cold image state does not satisfy this equality.
An out-of-range source index also skips the sequence, but the selected index is valid.
No SoC identity branch excludes this domain under the declared profile.

The domain also carries the custom-control flag.
`0x1484d3a0..0x1484d3a8` skips generic configuration for domains with bit nine set.
The generic RCG implementation must not replace this custom sequence.
The selected code does not use the descriptor's ordinary mux/divider bytes to program an RCG CFG word.

## Source and rate

The 28-byte rate record at `0x148eff98` specifies:

- Nominal frequency: `2016000000` Hz.
- Performance level: `0x100`.
- Mux configuration bytes: selector one and divider encoding two, followed by zero M/N fields.
- Source index: 15 at record offset `0x10`.
- Source configuration index: zero at record offset `0x11`.
- Unrestricted compatibility fields at record offsets `0x14..0x1b`.

The source count at `0x148f09cd` is 17.
The source pointer array at `0x148f04b0`, reached through `0x148f0980`, selects descriptor `0x148ef338` for index 15.
That descriptor supplies PLL base `0x19a30000` and the control vtable at `0x148f1220`.
The rate configuration at `0x148da3a0` supplies integer L = 105 and fractional mode one with alpha zero.
The initial even-divider encoding is zero, which selects divide-by-one.
The source flags at descriptor offset `0x2e` include `0x20`, which enables the even output.
Thus the declared 19.2 MHz reference gives `105 * 19200000 = 2016000000` Hz on that even output.
The reference is an explicit platform input, not a measured oscillator frequency.

The selected Zonda methods match the existing `QcomZondaPll` protocol:

| Selected method | Behavior |
| --- | --- |
| `0x14860110` | Set mode bits 1 and 2, write operation mode one at `+0x30`, poll lock, then enable output bit zero |
| `0x14860188` | Test enabled mode and lock |
| `0x148601a0` | Enable main/even output fields at `+0xc` |
| `0x14860230` | Program configuration words through `+0x2c` |
| `0x148602a0` | Program L at `+4`, alpha at `+8`, and divider/fractional fields at `+0x10` |

The existing Zonda model therefore supplies live acquisition, output gates, and frequency calculation.
Neither the Pongo protocol nor the conventional `QcomClockRoot` command/update protocol represents the CMU's packed configuration registers.
The existing CPU-domain bridge already represents the CMU contract, as shown below.

## Selected transaction sequence

Every listed CMU operation is a little-endian 64-bit read-modify-write.
The base below is `0x199a2010`.

| Selected instructions | Relative register | Field and write |
| --- | --- | --- |
| `0x1484d294..0x1484d2a4` | `0x20` | `PLLSWCTRL`, bit 25 = one |
| `0x1484d2a8..0x1484d2b0` | `0x20` | `OVRCKMUXPLLFASTCLK`, bit 2 = one |
| `0x1484d2b4..0x1484d2c0` | `0` | `LOCKTMOUTCNT[15:0] = 960` |
| `0x1484d2c4..0x1484d2d0` | `0` | `LOCKDEASSERTTMOUTCNT[43:32] = 960` |
| `0x1484d2d4` | Upstream PLL | Call source enable `0x1485e570` |
| `0x1484d2dc..0x1484d2e4` | `0x20` | `SELCKMUXPLLEVENCLK`, bit 5 = one |
| `0x1484d2e8..0x1484d2f0` | `0x20` | `SELCKMUXPLLFASTCLK`, bit 3 = one |
| `0x1484d2f4` | Guest RAM | Cache the selected rate pointer |

A failed source enable skips the last two fields and the cache update.
The timeout fields are not clock dividers. They do not establish PLL lock.
The CMU selects the PLL's even output without another division.
[INFERENCE] A later DVFS change can change the upstream even divider.

## Existing authoritative model and routing

The selected protocol is already modeled. A second packed-root model or helper would duplicate live state.
No new C++ class, factory key, or runtime composition call is required.

`platforms/qcom/sm88xx/reset.lua:280-289` creates the authoritative lane-zero owners:

| Physical range | Shared owner |
| --- | --- |
| `0x199a2010..0x199a2017` | `cpu_domains.bridge0_socket`, timeout word |
| `0x199a2030..0x199a2037` | `cpu_domains.bridge0_socket`, clock-control word |
| `0x19a30000..0x19a30033` | Existing `ncc_pll0` Zonda instance |

The bridge socket has base `0x199a2000`, not the descriptor's timeout-word address.
Thus its relative offsets `0x10` and `0x30` match descriptor-relative offsets zero and `0x20`.
`qrd8850-reset.lua:301-307` routes both APSS and CPUCP aliases through the same `cpu_domain_router`.
Those aliases do not create independent state.
`cpu_domains.ncc_pll0_path` names that same `ncc_pll0` instance.
The inherited reset composition therefore supplies the selected SM8845 addresses without an extra mapping.

`CpuDomainCoreState::bridge_transport` in `cpu_domain_core.h:144-160` owns both packed words.
It accepts aligned eight-byte reads and writes, rejects byte enables, and checks streaming width and command.
Its timeout mask is `0x00000fff0000ffff`, exactly the two selected timeout fields.
Its control mask is `0x0200002f`, which contains all four selected controls.
The additional low two control bits belong to the existing outer CPU-domain sequence.
Both packed words start at zero.
The function validates the full incoming qword before assigning the stored value.
An invalid upper half cannot commit a valid lower half.
It rejects other relative offsets rather than inventing a status register.

`QcomCpuDomainPower::ncc_clock_available` in `cpu_domain_power.h:143-148` follows the shared control word.
Software selection of the fast PLL uses the existing Zonda main or even output, according to bit five.
The PLL remains the sole owner of acquisition, output gates, and calculated frequency.
`outer_transport` notifies the reset process after accepted writes.
`reset_thread` also subscribes to both NCC PLLs' configuration and acquisition events.
`combined_reset` checks that live source together with the existing power, sequence, and core-reset conditions.
Timeout writes do not synthesize acquisition or output status.

The existing AUX and hardware-control behavior remains declared virtual policy.
`ncc_clock_available` permits that path while the enclosing domain still requires its live reference and branch prerequisites.
These selected consumers do not establish the physical AUX parent.
This investigation does not replace that policy with a guessed source or an extra always-running PLL.

The hardware header also names `PLLLOCKCNT[31:16]`, additional clock-control fields, and a status register.
The selected consumer does not program those fields or read that status register.
The shared bridge rejects unsupported fields and does not expose a fabricated PLL-status encoding.
This evidence does not extend the existing contract to all CPUCP/PDP DVFS transactions.

Do not install another packed-root helper or factory for these addresses.
Keep the existing APSS and CPUCP aliases intact.

## Verification boundary

This investigation decoded the selected ELF and inspected the supplied source definitions.
It ran no build, test, formatter, firmware launch, or commit.
Check atomic rejection, supported qword RMW, source loss, acquisition propagation, and both selected output mux paths.
No kernel-entry or successful-boot claim follows from this static evidence.

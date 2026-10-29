# CPUCP reset and boot evidence

## Scope and result

The input is `pbl8850.bin`, SHA-256 `91bd8f81efa7a5940d4b6d4c220388b559ceef39b6b47dd7b4b0727a1beb1d1c`.
The CPUCP slice starts at file offset `0x34000` and executes at `0x334000`.
For this slice, file offset equals PC minus `0x300000`.
The ISA is RV64 with compressed instructions.

The current SM8850 run passes the pad register, clock configuration, domain power, and core power acknowledgement.
CPUCP then releases the real APSS CPU through its architectural reset input.
The new hardware path does not yet prove entry into the package's CPUCP firmware.
The SM8845 compatibility run `run-sm8845-functional-controls3` repeats this sequence and reaches the TME hash request.
No proprietary image bytes appear in these model files.

## Implemented register contract

`cpucp_boot_scratch.h` implements `qcom_vp::QcomCpucpBootScratch` under `qcom,cpucp-boot-scratch-v1`.
It retains the word at `0x01fd9000`.
ROM PC `0x3359a6` reads this word. PC `0x3359ae` writes it.
The required reset parameter supplies the virtual initial value.
The platform selects zero without claiming a measured silicon reset value.

The model accepts aligned, little-endian, 32-bit reads and writes at local offset zero.
Unknown offsets, other widths, byte enables, and unsupported commands fail.
The model does not grant DMI or generate hardware acknowledgments.

The shared timer model owns `0x0c210000` and `0x17810000`.
The separate [power model](power_control_evidence.md) owns `0x0c264000`.
The retained [pad model](power_control_evidence.md) owns `0x0f1e0000`.

## Shared physical timers

CPUCP and APSS use one `qcom,sm8850-qtimer-v1` instance.
Its two frames share the counter, frequency metadata, and control state.
The [timer evidence](../apss_boot/evidence.md) describes the implemented register contract.

CPUCP PCs `0x335550` through `0x335566` set IMASK in both frame controls.
PCs `0x3355dc` through `0x3355fc` write TVAL, set ENABLE, poll ISTAT, and clear ENABLE.
PC `0x335604` reads the second frame counter at `0x17813000`.
The frequency value is 19200000.

## Required RAM

All ranges are half-open.
They describe RAM requirements, not permissions to preload later firmware.

| Range | Evidence |
| --- | --- |
| `[0x18b30000, 0x18b31000)` | Reset sets SP=`0x18b31000` at `0x334060`. `0x3353aa` clears the lower `0xfe0` bytes while preserving its active stack. |
| `[0x18b31000, 0x18b31030)` | ROM `0x3354c4` clears this handoff structure. `0x33471a`-`0x334760` publishes fields here. |
| `[0x18b31030, 0x18b3122c)` | `0x3354fc` clears `0x1fc` bytes. Log metadata at `0x18b31210`-`0x18b31228` is accessed by `0x335870` and `0x3358f6`. |
| `[0x18b32000, 0x18b32008)` | `0x335386` writes the canary seed. Later routines load a 64-bit canary from this address. |
| `[0x18b33000, 0x18b33034)` | State fields initialized by `0x334458`. PC `0x334566` publishes magic `0x1211d02e` after initialization. |
| `[0x18b31030, 0x18b33034)` | Cleanup routine `0x33544c` ends with a clear of this entire span at `0x3354a8`-`0x3354c0`. |
| `[0x14a7fc00, 0x14a80000)` | `0x335518` clears this shared log destination. `0x334650`-`0x3346ba` scans and copies records into it. |

The initial ROM therefore needs `[0x18b30000, 0x18b34000)` when mapped as a page-aligned region.
The later package runtime needs more RAM, as described below.

## Image publication and reset boundary

PC `0x33462c` loads the entry literal at `0x33634c`, value `0x1d800000`.
PCs `0x334634` and `0x33463a` write it to `0x17c40000` and `0x17c40004`.
PCs `0x33463c`-`0x334642` then set bit 0 at `0x17c40060`.
The [boot controller](power_control_evidence.md) retains these words without an immediate reset.
The supplied XBL later requests a CPUCP reset through `0x17c42000`.
That external request causes an architectural CPU reset through QBox.

After those writes, the ROM changes MTVEC through `0x3341c8`, logs event `0x47`, and copies its shared log.
It sets bit 0 at `0x18877600` and `0x1887768c` through `0x3346ce`-`0x3346e6`.
The instruction at `0x3346ea` is SiFive CEASE, encoding `0x30500073`.
CEASE halts execution until reset.
It is not standard WFI.
The [SiFive description](https://docs.rs/sifive-core/latest/sifive_core/asm/fn.cease.html) specifies this distinction.
Do not replace CEASE with WFI or a host PC write.

The real package `IMAGES/cpucp.elf` has ELF entry `0x1d800000`.
`readelf -l` reports these exact load ranges:

| File offset | Physical publication range | Virtual range | File bytes | Memory bytes |
| --- | --- | --- | --- | --- |
| `0x1000` | `[0x1d800000, 0x1d835588)` | Same | `0x35588` | `0x35588` |
| `0x37000` | `[0x1d836000, 0x1d836848)` | Same | `0x848` | `0x848` |
| `0` | `[0x1d837000, 0x1d838000)` | Same | `0` | `0x1000` |
| `0` | `[0x81240000, 0x8124b610)` | `[0x18b3a000, 0x18b45610)` | `0` | `0xb610` |
| `0` | `[0x81280000, 0x81287ce8)` | `[0x18b30000, 0x18b37ce8)` | `0` | `0x7ce8` |
| `0x38000` | `[0x812d0000, 0x812fc230)` | Same | `0x2c230` | `0x2c230` |

The first and last ranges have executable ELF flags.
The platform must publish actual guest-produced writes to the executable consumers before it releases them.
The ELF ranges do not prove that the ROM already wrote those images.
The two unequal virtual/physical pairs use explicit per-OEM aliases in the DDR helpers.
This is virtual address wiring from the selected ELF layout, not proof of a physical translation controller.

The runtime's first instructions save registers to `mscratch` and CSR `0x350`.
CSR `0x350` is MISELECT (`miselect`) in the architectural AIA register space.
This slice does not broaden CPU CSR acceptance.
The ROM's existing CSR `0x7c1` read-zero/write-ignore substitute remains unchanged.

At runtime PC `0x1d800008`, a table address resolves to `0x1d82d970`.
Its first two 64-bit entries are `0x18b47800` and `0x18b50000`.
PC `0x1d800010` selects an entry by `mhartid`.
PC `0x1d80002e` stores a marker through the selected pointer.
Thus hart 0 additionally requires `[0x18b47800, 0x18b47808)` to be writable.
Hart 1 requires `[0x18b50000, 0x18b50008)` if that hart is enabled.
Do not infer complete runtime stack sizes from these marker addresses.

## Additional release dependencies in the ROM

These accesses are not covered by a return-zero bank or a forced-ready substitute.
They identify work that needs authoritative register semantics before the path can pass.

- `0x334878`-`0x33487e` writes value 1 at `0x1887c000 + 4 * cluster` after the cluster boot sequence.
- `0x334ed0` requests bit 2 at a table-selected register `+8`, polls bit 1, then clears bit 2.
- `0x334cb8` polls bit 0 at a table-selected register `+0x270` after a request at `+0x258`.
- `0x335140` polls a field from a table-selected register `+0x2b0` against bounds at `0x336480` and `0x336484`.
- `0x335a46` sets a channel bit at `0x17c020b4`.
- `0x335a66` clears that bit, then polls the corresponding field at `0x17c020a4` until it clears.
- `0x335aac` polls other channel fields at `0x17c020a4` until they clear.

The cluster descriptor table starts at `0x336390`, with stride `0x78`.
The first descriptor includes bases `0x198f0000`, `0x18000000`, `0x18040000`, `0x18060000`, and `0x19880000`.
Their presence does not justify broad register banks at those bases.
The boot controller must implement real CPU release after executable publication.

## Package DT evidence

`IMAGES/cpucp_dtbs.elf` contains a flattened device tree at file offset `0x124`.
It identifies `chip-id = "sm8850"` and runtime policy properties.
The tree has no timer addresses, interrupt routes, RAM `reg` properties, or reset-controller bindings.
It therefore does not supply missing reset or interrupt semantics.
The concrete addresses above come from ROM disassembly and the package ELF headers.

## Selected post-DDR CPUCP contract

This section is static evidence. It does not prove native package execution.
Selected XBL hashes are SM8850 `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`
and SM8845 `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.

| Record or object | SM8850 | SM8845 |
|---|---|---|
| CPUCP_DTB record | `0x148ecdc8` | `0x148ead88` |
| CPUCPFW record | `0x148ece50` | `0x148eae10` |
| DTB pre-list | `0x148ed888` | `0x148eb718` |
| Prepare callback | `0x148387a8` | `0x14836910` |
| DTB cancel callback | `0x14838368` | `0x148364d0` |
| Firmware post-list | `0x148ed8a8` | `0x148eb738` |
| Reset callback | `0x14838808` | `0x14836970` |
| CPUCP interface table | `0x148f0410` | `0x148ee0a0` |

Both records have load=1, authentication=1, optional=0, execute=0, and jump=0.
The synchronization field at `+0x10` is zero on SM8850 and one on SM8845.
CPUCP_DTB uses security type `0x64`. CPUCPFW uses `0x31`.
The DTB has one pre-callback and the separate cancel callback.
The cancel callback writes zero through its second argument and returns zero when both arguments are nonnull.
It does not silently cancel a missing authenticated image.
The firmware has one post-callback and no cancellation callback.
No direct execution or jump callback appears in either record.

Prepare obtains interface `0x34`, then calls table offset zero.
The table methods are prepare, unprepare, reset, and a fourth service method.
Prepare wrappers `0x14841f4c/0x14840480` call target predicates `0x14892ab0/0x148912a0`.
Both selected target predicates return true without hardware access.
The wrappers convert this Boolean into boot status zero.
Unprepare wrappers `0x14841f90/0x148404c4` target return-only routines.
Thus these selected prepare paths require no successful AOP or SOCCP remote request.
Do not add a synthetic remote completion to satisfy them.

The post-callback obtains the same interface and invokes offset `0x10`.
Its wrappers `0x14841f94/0x148404c8` enter reset routines `0x14892abc/0x148912ec`.
SM8850 clears the low 24 bits at `0x1887761c`.
It clears bit zero at `0x1887768c`, then sets bit zero at `0x17c42000`.
SM8845 performs the same initial clears.
It reads revision word `0x221c8cf8`, masks its low 16 bits, and compares against three.
Below three, it sets bit zero at `0x17c42000`.
At least three, it performs this sequence:

1. Set bit `0x100` at `0x18877614`, then `0x18877618`.
2. Set bit zero at `0x17c42000`.
3. Clear bit `0x100` at `0x18877618`, then `0x18877614`.
4. Set bit zero at `0x18877600`.

The final step is a setup write, not a second CPUCP reset request.
The SM8845 register base changes before that final write at `0x14891358`.
The V2 boot controller already retains both pulse words without a false acknowledgment.
Both versions retain the initial clears and invoke real architectural reset.
Neither derives the runtime PC from a host-selected ELF entry.
The post-wrapper returns zero after the void reset call.
This firmware behavior does not permit a model to convert failed memory or remote transactions into success.

### Loaded objects and OEM translation difference

Both DTB images enter and load at `0x812c0000`, length `0x103c`.
Hashes are SM8850 `997bd1cfb6a9f292819b78871f2bb73f1770d9d571f9cd028aad5f6a132cc137`
and SM8845 `25696c3a509fffb14cc97a490825e94e06d9148a0fbba3f104f5c54ff07de97c`.
Their whitelist is `[0x80000000,0xd8029000)` plus `[0xd8089000,0x100000000)`.
CPUCPFW also permits `[0x1d800000,0x1d840000)`.
The firmware hashes are SM8850 `3475ceab499e02fac22e249e7249ea9ee92c0eb21a260eb9b6f18d185ab70e24`
and SM8845 `5667791c9c681da9fe05c5a91985fa30b9d9d67a8bebfa149cc1193d75c336a4`.
The earlier load table applies only to SM8850.
SM8845 has these load ranges:

| Physical address | Virtual address | File bytes | Memory bytes |
|---|---|---|---|
| `0x1d800000` | Same | `0x35548` | `0x35548` |
| `0x1d836000` | Same | `0x848` | `0x848` |
| `0x1d837000` | Same | Zero | `0x1000` |
| `0x81240000` | Same | `0x315f0` | `0x315f0` |
| `0x812d0000` | `0x18b30000` | Zero | `0x8010` |
| `0x812da000` | `0x18b3a000` | Zero | `0xb610` |

Do not copy SM8850's physical aliases into SM8845.
In particular, SM8845 `0x81240000` contains initialized runtime data, not the SM8850 BSS destination.
These objects remain guest-produced and live across CPUCP's ROM-to-runtime reset.
A reset controller must not clear the executable publication or create guest runtime queues.

### SM8845 runtime configuration word

The SM8845 runtime first updates `0x14680cc8` before it selects a per-hart marker.
Instructions `0x1d800008..0x1d800022` construct that address, read it, set bit zero, and write it.
The SM8850 runtime does not contain this preamble.
The address lies inside the existing APSS IMEM backing at `0x14680000+0x2c000`.
The reset composition routes the entire CPUCP IMEM view to that same backing.
APSS, UFS DMA, sequencer authentication DMA, and CPUCP therefore retain the same bytes.
Cold-zero IMEM is explicit board policy, not a measured silicon reset value.
The word survives CPUCP architectural reset because that reset does not clear IMEM.
No separate runtime register, narrow shadow, acknowledgment, interrupt, or ready effect exists.
The obsolete V2 `runtime_socket` and its private storage were removed.


### Legacy RNMI instruction boundary

Run252 executes `csrw 0x350,t5` at `0x1d800004`.
The CPU raises illegal-instruction cause 2, then enters ROM `0x334180` through `0x3341dc`.
The ROM's CSR `0x7c1` and CEASE paths already execute.
Both paths remain unchanged.

Static disassembly uses `IMAGES/cpucp.elf` from both selected export ZIP files.
Their SHA-256 values match the firmware hashes above.
The following accesses occur in startup and RNMI handler instruction sequences, not an unrestricted opcode count.

| Consumer | SM8850 PC | SM8845 PC | Observed operation |
|---|---|---|---|
| Entry | `0x1d800004` | `0x1d800004` | Write saved `t5` to CSR `0x350` |
| Diagnostic state | `0x1d800326/32e/336` | `0x1d800380/388/390` | Read CSRs `0x351/352/353` into frame offsets `0x198/1a0/1a8` |
| Recovery return | `0x1d801178/17c` | `0x1d801178/17c` | Write CSR `0x351`, then execute `0x70200073` |
| Handler entry | `0x1d8012ba/2c0` | `0x1d8012be/2c4` | Save CSR `0x351/353` at stack offsets `0x80/88` |
| Handler dispatch | `0x1d8012f0` | `0x1d801312` | Read CSR `0x352`, remove the top interrupt bit, compare cause 2 |
| Handler return | `0x1d80135c` | `0x1d80137e` | Execute `0x70200073` |

The recovery target is `0x1d800396` on SM8850 and `0x1d8003f0` on SM8845.
The handler epilogues read `0x351/353`, then overwrite the temporary register from their saved stack slots.
They do not write those CSRs in those epilogues.
These consumers establish the legacy RNMI register roles and MNRET encoding.
They do not independently establish CSR `0x353` bit positions.
No bit test or write to CSR `0x353` appears in these identified consumers.

The semantic reference is Rocket Chip commit `ece7b9ad544b07df39cd13b6fd7236562a26355d`.
`CustomInstructions.scala:39-51` maps `0x350..353` to MNSCRATCH, MNEPC, MNCAUSE, and MNSTATUS.
`CSR.scala:61-68` defines the Chisel `MNStatus` bundle.
Chisel places the first declared field at the most significant end.
Thus MPP occupies bits `12:11`, MPV occupies bit 7, and MIE occupies bit 3.
The reversed interpretation, MPP at `1:0` and MIE at bit 9, is incorrect.
These three fields match QEMU Smrnmi's MNPP, MNPV, and NMIE.
Legacy MNSTATUS excludes the modern MNPELP bit 9.

`CSR.scala:517-524` resets previous privilege to M and RNMI enable to one.
`CSR.scala:1291-1300` permits software to set RNMI enable but not clear it.
It legalizes previous privilege, limits MPV to hypervisor configurations, aligns MNEPC, and masks MNCAUSE to the interrupt bit plus bits `1:0`.
`CSR.scala:1141-1145` restores previous privilege and virtualization on MNRET, enables RNMI, and returns through MNEPC.
The patch reuses QEMU's existing RNMI state, exception routing, interrupt gating, and MNRET implementation.
It selects these register semantics only when `x-legacy-rnmi` is true.
It does not change the global CSR table.
Disabled CPUs retain their existing indirect-CSR predicates and handlers.

The existing CPUCP boot controller exposes `legacy_rnmi`, disabled by default.
The reset platform enables it only for CPUCP and retains the existing `rv64` CPU base.
The RNMI instruction boundary alone does not establish a vector value or NMI source.
The [later initializer](subsystem_evidence.md#adjacent-per-hart-initialization) publishes two banks containing the identified RNMI handler pointer.
Their interrupt-versus-exception assignment remains unproven. The model retains the banks without inventing that assignment or injecting NMI events.
The legacy window, CEASE, and cache-operation evidence suggests a Rocket/SiFive family relationship.
It does not identify the physical CPUCP core or establish complete Rocket compatibility.

The native `cpucp-rnmi` fixture checks both initial reset and external architectural reset.
It checks legacy reads, writes, masked updates, modern shared state, MNSTATUS masks, MNEPC alignment, MNRET, ECALL, and a pending software interrupt.
Its disabled variant requires illegal-instruction traps for all four legacy CSRs and MNRET.
The fixture uses synthetic guest code, not firmware substitutions.
The focused RNMI, CEASE/reset, and DDR alias fixtures pass.
The complete integration run passes 47 host tests and 70 platform tests. Clippy passes with warnings denied.
SM8850 run256 and SM8845 run207 pass the former CSR trap and reach the runtime memory-clear boundary.
Runs258 and208 then execute genuine CPUCP code from DDR after the RAM extent correction.

### Runtime discriminators and acceptance

- Break at prepare's indirect call `0x148387d4/0x1483693c`.
  Confirm that the interface points to `0x148f0410/0x148ee0a0`.
  Constant-true target execution distinguishes selected prepare from an unresolved remote service.
- Break at post-reset indirect call `0x14838834/0x1483699c`.
  Observe the setup writes listed above and the retained vector/select words at `0x17c40000/+4/+0x60`.
  Architectural reset must execute guest-published `0x1d800000`.
  A missing vector or failed write must not become a fabricated release.
- Break CPUCP at `0x1d800000`, then marker store `0x1d80002e/0x1d80004c`.
  Both selected images obtain the per-hart pointers from `0x1d82d970`.
  Hart zero selects `0x18b47800`. Hart one selects `0x18b50000`.
  A store fault distinguishes missing SRAM backing from a reset failure.
- On SM8845, stop at load `0x1d800010` and store `0x1d800022`.
  The read at `0x14680cc8` must retain earlier writes.
  The store must set bit zero without changing unrelated bits or causing reset.
  Compare this IMEM word with the APSS view before and after architectural reset.
- Observe the first accesses to local `0x18b30000` and `0x18b3a000`.
  Both addresses belong to one retained local SRAM owner at `0x18b30000+0x40000`.
  They must remain independent of the ELF physical zero-fill destinations in DDR.
  Preserve earlier ROM writes until the actual guest clear or copy changes them.
  Observe the later DDR poison and snapshot copies without skipping either operation.
- Observe the APSS incoming SCMI slot `0x18b4e200`, APSS TX `0x18830300`, and APSS RX `0x17624c08`.
  This channel is separate from the PDP endpoint.
  Preserve real runtime execution and capture its first slot writer and doorbell consumer.
  QcomScmiDxe RVA `0x5448` is the APSS server callback for this slot, not its request producer.
  Do not answer CPUCP requests through PDP's two-domain virtual service.
- At reset routine `0x14892abc/0x148912ec`, capture any unexpected MMIO request and its original failure.
  The recovered routine requires only setup/revision/reset registers.
  AOP or SOCCP readiness must not be invented from record order.

Existing V1/V2 models cover every recovered reset effect in this section.
The reset platform owns CPUCP image RAM at `0x1d800000+0x40000`, with CPUCP as its executable owner.
APSS loading, UFS DMA, authentication DMA, and CPUCP fetch use that same image object.
Both DDR windows also expose system-physical CPUCP aliases without changing their APSS backing.
The local SRAM is separate. Zero-file ELF virtual/physical pairs do not establish bus aliases.
Native acceptance requires authenticated DTB and firmware loads, the original post-callback, architectural reset, and real CPUCP runtime requests.
Static reset coverage alone does not establish CPUCP SCMI or RPMh service completion.

## Runtime mailbox recovery

This section uses the two selected CPUCP firmware hashes above.
It records static disassembly, not a successful runtime transaction.
No build, test, formatter, native run, or commit ran for this recovery.

### Address conventions

CPUCP PCs in the first segment convert to ELF file offsets as `PC - 0x1d800000 + 0x1000`.
For example, SM8850 PC `0x1d803830` is file offset `0x4830`.
APSS addresses below are QcomScmiDxe or MailboxDxe PE RVAs.
The selected PE executable sections are identical between the OEMs.

### Distinguish the two SCMI directions

QcomScmiDxe RVA `0x5448` is an APSS **server** callback.
It does not submit an APSS request to CPUCP.
Its sequence is:

1. Construct slot address `0x18b4e200` at `0x5460..0x5464`.
2. Call dispatcher `0x54f4` at `0x5468`.
3. Call `0x5538` at `0x5488`. This helper writes FREE=1 at slot `+4`.
4. Call flag helper `0x5528` through the tail branch at `0x5498`.

Dispatcher `0x54f4` reads the protocol from header `+0x18`.
It selects Base `0x10`, Performance `0x13`, vendor `0x80`, or the unsupported handler.
The flag helper tests slot `+0x10` bit zero.
Its selected notification target `0x5524` returns without an MMIO write.
Thus this callback proves an APSS request consumer and completion producer.
It does not prove an APSS doorbell completion producer.
Do not describe this slot as proof of an APSS-produced CPUCP service request.

CPUCP also implements a server.
Its dispatch sequence starts at SM8850 `0x1d80e1e8` and SM8845 `0x1d80e854`.
It dispatches, writes FREE, invokes a protocol-specific post-hook, then tests the interrupt flag.
The FREE helpers are `0x1d81fa6a/0x1d81f21e`.
The interrupt-flag helpers are `0x1d81fa5e/0x1d81f212`.
The mailbox callback loads the slot pointer from its guest context at `+0x10`.
These callbacks are `0x1d80e20c/0x1d80e878`.
The guest registers them during SCMI initialization at `0x1d80e2e2/0x1d80e94e`.

The CPUCP channel arrays start at `0x1d82ef18/0x1d82ef80`, with stride `0x68`.
These are initialized guest objects, not hardware queues:

| Channel | CPUCP slot | Mailbox device | RX/TX channel |
|---|---|---|---|
| `apss_sec` | `0x1d83c400` | No mailbox pointer in this descriptor | Not established |
| `apss_nsec` | `0x1d838000` | `cpucp_ap_ns@17640000` | 0/0 |
| `hlos_notifier` | `0x1d838200` | Same device | 2/2 |

The selected APSS callback uses `0x18b4e200`, not `0x1d838200`.
The similar low offsets do not establish an alias.
No mapping between these addresses was recovered in this slice.
Existing post-DDR BSS aliases do not establish that mapping.
A virtual platform can declare this alias explicitly, then verify coherence through the actual producers and consumers.

### CPUCP hardware register consumers

The nonsecure mailbox device objects are `0x1d8235d8/0x1d8230e8`.
Their configuration objects are `0x1d82deb0/0x1d82de70`.
Configuration `+0x10` supplies TX base `0x17640000`.
Configuration `+0x20` supplies RX base `0x18820000`.
The secure configurations instead select TX `0x17648000` and RX `0x18828000`.
These are not the APSS apertures `0x18830300` and `0x17624c08`.

| Operation | SM8850 PC | SM8845 PC | Register effect |
|---|---|---|---|
| Read geometry | `0x1d803830` | `0x1d803954` | Read 32-bit IDR at base. Extract bit 0, bits 7:1, and bits 14:8. |
| Map channel | `0x1d803896` | `0x1d8039ba` | Read/OR/write a 64-bit MAP word at RX `+0x4000+8*group`. |
| Read status | `0x1d803906` | `0x1d803a2a` | Read 64-bit pending at RX `+0x4400+8*group`. |
| Enable channel | `0x1d80392e` | `0x1d803a52` | Read/modify/write a 64-bit mask at RX `+0x4c00`. |
| Clear pending | `0x1d803960` | `0x1d803a84` | Write captured bits to RX `+0x4800+8*group`. |
| Read command | `0x1d8039b2` | `0x1d803ad6` | Read 64-bit command at RX `+0x100+8*channel`, then clear that channel. |
| Send command | `0x1d803a18` | `0x1d803b3c` | Write low word, then high word at TX `+0x100+8*channel`. |
| Consume interrupt | `0x1d803a52` | `0x1d803b76` | Read pending, find registered guest callback, read command, clear, invoke callback. |

Initialization reads IDR and uses its counts to bound later accesses.
RX initialization clears masks and MAP words, and writes all ones to clear words.
Unlike the selected APSS consumer, CPUCP writes **nonzero MAP bits**.
Its callback registration calls the mapping helper for the selected channel.
The IDR values and cross-aperture routing remain unmeasured.
Returning the existing mailbox model's virtual IDR zero would prevent this path from registering its channels.

APSS MailboxDxe send `0x3658 -> 0x3c28` writes command-high after its caller prepares shared memory.
Receive `0x3914 -> 0x36ac` captures pending and clears it before command-high consumption.
CPUCP instead reads command before its per-channel clear.
Neither clear operation writes SCMI FREE or constructs response bytes.
Those writes belong to the processor that executes the SCMI server.

### Interrupt mapping boundary

CPUCP nonsecure setup `0x1d803cba/0x1d803dde` registers encoded interrupt `0x1d10`.
Secure setup `0x1d803c98/0x1d803dbc` registers `0x1910`.
PDP setup `0x1d803cdc/0x1d803e00` registers `0x2910`.
The external interrupt software path decodes its source index as `(encoded >> 8) - 1`.
Thus these indices are 28, 24, and 40, respectively.
They are not APSS GIC INTIDs.

SM8850 controller initialization `0x1d801b82` reads geometry at `0x18875000/+4`.
SM8845 initialization starts at `0x1d801be2`.
SM8850 `0x1d801c16..0x1d801c7a` derives trigger, priority, enable, and per-hart register pointers from that geometry.
The existing TME CLIC model does not implement this geometry-derived CPUCP controller.
The [subsystem recovery](subsystem_evidence.md#architectural-interrupt-input) now identifies architectural CPU input 16 through the enable helper and ISR table.
Source index 28 passes through the PLICv2 claim path before it reaches that CPU input.
Do not connect encoded `0x1d10` directly to CPU input 28 or 16.

The APSS descriptor `{0x3c,0}` passes through the MailboxDxe framework.
The same framework-translation gap described in the PDP evidence remains.
Absolute GIC INTID60 versus SPI60 cannot be selected from this descriptor alone.

### Model decision and integration

Do not integrate `QcomIpccLocal` for this path.
Its SEND/RECV client-signal registers differ from the recovered mailbox registers.
Do not attach `QcomPdpScmi` to a CPUCP slot.
The existing common mailbox models also lack three required behaviors:

- CPUCP 64-bit RX accesses.
- Nonzero MAP state with geometry from IDR.
- Guest-to-guest routing between the different TX and RX apertures.

The [subsystem model](subsystem_evidence.md) now implements CPUCP-side register banks, a compact PLICv2 controller, and explicit virtual geometry.
It supports declared same-channel hardware peers without constructing service responses.
Production cross-aperture routing remains unresolved. An absent peer makes a TX doorbell fail with a diagnostic.
All four registered mailbox devices now expose their configured apertures, including PDP1's second transmitter at `0x1a960000`.
PDP0 and PDP1 share one RX owner at `0x18808000`.
Those gaps do not require exact silicon values before implementation.
A bounded virtual topology can declare geometry, channel routing, shared aliases, and interrupt connections.
Each choice must remain an identified substitute, not a claim about physical routing.
A two-sided guest fixture can verify that declared contract.

Use one retained register owner for each RX bank.
Keep commands, pending, MAP, and enable state together.
Keep shared SCMI slots in one RAM owner.
CPUCP instruction execution must remain the CPUCP service and completion producer.
Connect the declared controller input rather than a host callback that executes a service.
Do not initialize slot headers, FREE words, grants, or guest callback lists.

### Subsystem initialization cutover

SM8850 run258 and SM8845 run208 reach the selected DDR-resident CPUCP initialization routine.
Both stop at a read of `0x18876104`.
The [subsystem evidence](subsystem_evidence.md) records the control sequence, the complete local workspace, and the next controller and mailbox dependencies.
The complete local interval `[0x18b30000,0x18b70000)` now has one persistent SRAM owner.
Its upper half includes the guest-cleared workspace. It has no inferred DDR or APSS alias.
Runs 259 and 209 then reach the hardware-mutex read at `0x18860000`.
The complete ten-word bank reuses the existing atomic mutex model.
CPUCP lock one shares the APSS `qsee_ticket_guard` owner at `0x18860004`.
The [full inventory](subsystem_evidence.md#hardware-mutex-inventory) distinguishes these locks from mailbox geometry and records both firmware initializers.
The physical control names, silicon geometry values, and APSS mailbox translations remain uncertain.
No fabricated firmware state or response producer replaces the actual CPUCP guest.

### Exact runtime discriminators

Run the two existing prepared inputs separately. These commands did not run for this evidence:

```sh
build/install/bin/qcom-vp run --prepared build/private/prepared-sm8850-common-runtime12 --out build/private/cpucp-transport-sm8850 --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
build/install/bin/qcom-vp run --prepared build/private/prepared-sm8845-common-runtime11 --out build/private/cpucp-transport-sm8845 --continue-after kernel-entry --enable-oem-uart --timeout-seconds 180
```

With the existing debugger harness, set CPUCP breakpoints at the table's PCs for the selected OEM.
At geometry entry, record `a0`, `a1`, the configuration pointer at `[a0+8]`, and the bounds that firmware consumes.
At mapping entry, record `a1=group`, `a2=channel`, the resulting address, and the complete 64-bit MAP value.
At command send, record both ordered stores and the pending transition on each candidate receive bank.
At interrupt consumption, record the actual controller source and the callback's slot pointer.
At FREE publication, record the writer PC, original header, payload, length, and both processors' memory views.
For APSS, relocate PE RVAs against the actual loaded module base before setting breakpoints.
Stop at QcomScmiDxe `base+0x5448` and `base+0x5538`.
Record the first writer of `0x18b4e200` before its APSS callback.
Do not infer that writer from the callback's name.

Accept routing only when one processor's command-high write causes the other processor's matching pending bit.
Require masking to retain pending and W1C to preserve unrelated bits.
Require a real processor to consume the command and publish the correlated response.
Record actual APSS GIC enable writes to distinguish INTID60 from SPI60.
Record CPUCP controller geometry and source handling to verify the recovered architectural input 16 connection.
These observations verify the declared virtual contract. They do not require or establish exact silicon geometry.

## Reconstructed TME local image view

SM8845 `run-sm8845-common-runtime123` completes CPUCP_DTB and reports CPUCPFW authentication metadata timing.
It then fails a byte read at local address `0x21835500`, from TME PC `0x230bbc`.
SM8850 `run-sm8850-common-runtime151` completes CPUCPFW and QSEE with the same ROM composition.
These observations do not establish CPUCP execution or kernel entry.

Both prepared inputs use reconstructed ROM SHA-256 `fcff1c3cf2dfeed272fe03b63dc52e52d06a443c0fdf076ba9472295299b12fa`.
Static disassembly identifies the pointer producer and consumer:

- Converter `0x231670` searches seven local/system/size records at `0x2335a0`.
- Record `0x2335ac` contains local base `0x04100000`, system base `0x00100000`, and size `0x1ef00000`.
- The converter subtracts the system base, checks the size, then adds the local base.
- Therefore system `0x1d835500` becomes local `0x21835500`.
- Inverse converter `0x2316a4` uses the same record and restores system `0x1d835500`.
- Copy routine `0x230bb6` reads from `a1` through `lbu a4,0(a1)` at `0x230bbc`.

The pointer is valid. Its local bus mapping was missing.
The SM8845 source byte is initialized CPUCP image data at ELF file offset `0x36500`.

### Complete image extent

The selected CPUCP ELF hashes are:

- SM8845: `5667791c9c681da9fe05c5a91985fa30b9d9d67a8bebfa149cc1193d75c336a4`.
- SM8850: `3475ceab499e02fac22e249e7249ea9ee92c0eb21a260eb9b6f18d185ab70e24`.

All ranges below are half-open physical publication ranges from the ELF program headers.
Each initialized range has equal file and memory sizes.
Each zero-fill range has zero file bytes.

| Segment | SM8845 range | SM8850 range | Storage |
| --- | --- | --- | --- |
| Initialized SRAM code/data | `[0x1d800000,0x1d835548)` | `[0x1d800000,0x1d835588)` | Existing `cpucp_image` |
| Initialized SRAM data | `[0x1d836000,0x1d836848)` | Same | Existing `cpucp_image` |
| SRAM zero-fill | `[0x1d837000,0x1d838000)` | Same | Existing `cpucp_image` |
| Initialized DDR code/data | `[0x81240000,0x812715f0)` | `[0x812d0000,0x812fc230)` | Existing `ddr_low` |
| Zero-file RAM segment publication | `[0x812d0000,0x812d8010)` | `[0x81280000,0x81287ce8)` | Existing `ddr_low` |
| Zero-file BSS segment publication | `[0x812da000,0x812e5610)` | `[0x81240000,0x8124b610)` | Existing `ddr_low` |

The corresponding ELF virtual addresses are `0x18b30000` and `0x18b3a000`.
These zero-file headers describe physical loader zero-fill, not permanent virtual-to-physical bus aliases.
The runtime clears its separate local SRAM through actual guest stores.
All initialized segments have equal virtual and physical addresses and retain their existing backings.
The DDR extents and CPUCP reset policy remain unchanged.

`platforms/qcom/sm88xx/tme-runtime.lua` maps local `[0x21800000,0x21840000)` directly to offset zero of `cpucp_image`.
The complete backing follows the selected XBL whitelist `[0x1d800000,0x1d840000)`.
This range contains the ELF segments, SCMI slots, and runtime handshake words.
APSS, CPUCP, UFS DMA, sequencer DMA, and both TME views retain one shared backing.
The alias neither initializes bytes nor changes authentication, admission, or reset decisions.

SM8845 run124 passes the failed read and completes CPUCPFW loading.
It subsequently completes TZ_QTI_Config, QSEE Dev Config, and QSEE loading.
The complete integration run passes 97 tests and Clippy with warnings denied.
The original error handler then reports code 72 at `sbl1_mc.c:2450`.
The later CPUCP cleanup rejection and SPMI abort do not identify its cause.
Neither OEM reaches UEFI, ABL, or the kernel.

## Runtime shared SRAM boundary

The initial `0x38000`-byte image mapping omitted shared state above the ELF segments.
The selected CPUCP configuration places secure SCMI at `0x1d83c400` and nonsecure SCMI at `0x1d838000`.
QSEE readers `0x15759130` and `0x1574bee8` access the CPUCP marker at `0x1d83c804`.
They write `INIT` to `0x1d83c800` only when the peer marker equals `0x494e4954`.
The platform expands the existing backing and every alias to `0x40000` bytes.
The initial bytes remain zero. Only guest execution can publish the marker or complete an SCMI request.

SM8850 run254 and SM8845 run205 exercise the expanded map during OEM boot.
Both stop in the TME QMP callback before this QSEE marker step.
These runs do not prove the later marker exchange or SCMI service.

## Runtime RAM initialization extent

SM8850 run256 passes the legacy RNMI access and faults at `0x18b37ce8` during the runtime memory clear.
SM8845 run207 reaches the corresponding boundary at `0x18b38010`.
The ELF zero-fill sizes do not describe the complete runtime RAM extent.

SM8850 `0x1d820ab2..0x1d820ae6` clears `[0x18b30000,0x18b37ce8)`, then `[0x18b37ce8,0x18b50000)`.
SM8845 `0x1d820266..0x1d82029a` splits the same total interval at `0x18b38010`.
Both paths use the actual guest memory-clear implementation.

The platform now supplies one persistent local SRAM range through `0x18b70000`.
The previous DDR-backed local aliases and separate upper workspace allocation were incorrect and are removed.
The guest performs both clears on that local owner.
Physical ELF zero-fill remains in the existing DDR backing.
No host copy or reset-time memory clear replaces either guest operation.

SM8850 run258 and SM8845 run208 pass both memory clears and execute CPUCP code from DDR.
They then read unmodeled `0x18876104` at PCs `0x812d41e6` and `0x81244192`, respectively.
These runs establish native CPUCP runtime execution, not complete initialization or kernel entry.

## Local SRAM and independent DDR snapshots

SM8850 run273 reaches the real snapshot-buffer initializer after passive ACEHR publication.
The initializer fills both DDR buffers with byte `0x5a`.
The former `0x18b30000 -> 0x81280000` alias exposed the live stack to that fill.
The return PC holds repeated `0x5a` bytes.
This identifies a memory-ownership error, not a reason to skip the fill or change emulation type.
SM8845 run218 instead reaches W32 `0x17960020` at `0x1d8044dc`.
Its different snapshot addresses explain why the same wrong alias did not immediately corrupt its return.
Neither observation establishes a completed CPUCP service.

The selected zero-file program headers are:

| Firmware | `p_vaddr` | `p_paddr` | `p_filesz` | `p_memsz` |
|---|---|---|---|---|
| SM8850 | `0x18b30000` | `0x81280000` | 0 | `0x7ce8` |
| SM8850 | `0x18b3a000` | `0x81240000` | 0 | `0xb610` |
| SM8845 | `0x18b30000` | `0x812d0000` | 0 | `0x8010` |
| SM8845 | `0x18b3a000` | `0x812da000` | 0 | `0xb610` |

The ELF parser reads `p_paddr` separately.
Physical loader publication and zero-fill remain on the DDR ranges above.
These headers contain no initialized bytes that need a local DDR alias.
Every initialized segment has `p_vaddr == p_paddr`.
Its SRAM or DDR storage remains unchanged.

The firmware itself establishes the independent snapshot relationship:

| Operation | SM8850 | SM8845 |
|---|---|---|
| Buffer initializer | `0x1d805740` | `0x1d8059d2` |
| IRAM snapshot destination | `0x81240000` | `0x822ffe00` |
| Local SRAM snapshot destination | `0x81280000` | `0x8233fe00` |
| First poison call | `0x1d805776` | `0x1d805a10` |
| Second poison tail call | `0x1d80578a` | `0x1d805a24` |
| Snapshot-copy routine | `0x1d805796` | `0x1d805a30` |
| IRAM copy call | `0x1d8057aa` | `0x1d805a44` |
| Local SRAM copy tail call | `0x1d8057c2` | `0x1d805a5c` |

Both buffer pointers reside in local SRAM, at `0x18b30050` for IRAM and `0x18b30048` for local SRAM.
Each poison uses length `0x40000` and byte `0x5a`.
The subsequent copies use source `0x1d800000` or `0x18b30000`, destination from the corresponding pointer, and length `0x40000`.
The copy routines are `0x1d81ee24` and `0x1d81e5d8`.
Thus the complete local source interval is `[0x18b30000,0x18b70000)`.
The selected path requires that source and its DDR destination to remain independent.
The guest owns publication, poison, save, and any later restoration.

The reset composition now allocates that full local interval once as `cpucp_ram`.
Cold zero is virtual board policy.
Architectural CPU reset does not replace or clear this memory.
The DDR helpers no longer reinterpret ELF metadata as local bus mappings.
The subsystem reuses the same owner instead of allocating `cpucp_work_ram`.
No additional APSS view is inferred.
No firmware bytes, address producers, reset decisions, or emulation-type values are changed.

The former alias-equality assertions in the native DDR fixture are withdrawn.
They tested an incorrect contract, despite passing.
The independent physical-DDR instruction and DMA-coherence checks remain valid and stay in the replacement fixture.
The revised synthetic guests check:

- Cold local SRAM and physical zero-fill extents as separate storage.
- DDR writes and loader-style zero-fill without a local SRAM change.
- Full-buffer poison while a real return address remains on the local stack.
- Executable IRAM bytes and shared-DDR instruction replacement through APSS and both DMA routers.
- Guest copies of both complete `0x40000`-byte live ranges into DDR snapshots.
- Memory preservation across an actual CPU reset requested through the existing boot controller.
- Independent live-memory changes followed by guest restoration from the saved bytes.

The native fixture supplies synthetic instructions, not OEM bytes or an OEM resume result.
Its success sentinel is `0x600d0000`.
Its failure sentinels are outside the mapped DDR windows.
Verification:

```sh
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform cpucp_preserves_local_sram_across_ddr_backup_and_reset -- --ignored --exact
cargo test --manifest-path tools/qcom/qcom-vp/Cargo.toml --test platform cpucp_subsystem_routes_real_mailbox_interrupts_and_preserves_workspace -- --ignored --exact
```

The corrected SRAM fixture and subsystem fixture pass for both selected compositions.
The secure-notification fixture also declares the complete local SRAM owner and passes.
The integration run passes 47 host tests and 73 platform tests. Clippy passes with warnings denied.
SM8850 run274 passes the former poisoned return and reaches the CPUCP wait at `0x1d80344a`.
SM8845 run219 also passes the corrected memory initialization.
Both runs later end after an APSS watchdog bite. Neither proves kernel entry.


## APSS secure notification and CPUCP source 117

This recovery uses static disassembly of both selected OEM packages.
It does not establish a completed OEM transaction.
The CPUCP hashes are the [subsystem inputs](subsystem_evidence.md#scope-and-runtime-observation).
The QSEE hashes are:

- SM8850: `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932`.
- SM8845: `b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09`.

### Secure endpoint identity

The five SCMI descriptors have stride `0x68`.
The first starts at `0x1d82ef18/0x1d82ef80`.
The following fields distinguish interrupt notifications from hardware mailbox channels:

| Field | Offset | Meaning in the selected consumers |
| --- | --- | --- |
| Slot pointer | `+0x10` | Guest-owned shared request and response memory |
| Slot size | `+0x18` | Length cleared by the guest initializer |
| Mailbox selection | `+0x1c` | Zero selects the direct interrupt path |
| Interrupt selector | `+0x20` | Source index matched by the direct handler |
| Completion word address | `+0x28` | Optional direct notification target |
| RX device and channel | `+0x30/+0x38` | Hardware mailbox receive endpoint |
| TX device and channel | `+0x40/+0x48` | Hardware mailbox transmit endpoint |

Both images contain this complete descriptor group:

| Peer | Slot | Size | Receive endpoint | Transmit endpoint |
| --- | --- | --- | --- | --- |
| `apss_sec` | `0x1d83c400` | `0x400` | Direct source 117 | No mailbox device or completion address |
| `apss_nsec` | `0x1d838000` | `0x200` | Nonsecure mailbox channel 0 | Same device, channel 0 |
| `hlos_notifier` | `0x1d838200` | `0x200` | Nonsecure mailbox channel 2 | Same device, channel 2 |
| `plat_pdp0` | `0x1d83c200` | `0x100` | PDP0 mailbox channel 3 | PDP0 transmitter, channel 1 |
| `plat_pdp1` | `0x1d83c300` | `0x100` | PDP0 mailbox channel 4 | PDP1 transmitter, channel 1 |

The secure SCMI descriptor has zero at `+0x1c`, `+0x28`, and both device pointers.
Its `+0x20` value is `0x75`, decimal 117.
It therefore does not select the hardware secure mailbox at `0x17648000/0x18828000`.
That mailbox remains a separate source-24 owner.
PDP0 and PDP1 retain their existing shared RX owner.

| Secure path | SM8850 | SM8845 |
| --- | --- | --- |
| Static ISR record | `0x1d834b80` | `0x1d834b20` |
| Record source index | 117 | 117 |
| Record callback | `0x1d8037ee` | `0x1d803912` |
| Direct source dispatcher | `0x1d80e506` | `0x1d80eb72` |
| SCMI server | `0x1d80e1e8` | `0x1d80e854` |
| FREE publication store | `0x1d81fa6c` | `0x1d81f220` |
| Direct notification target | `0x1d80e48c` | `0x1d80eaf8` |

The dispatcher compares its argument with descriptor `+0x20`.
It loads the selected slot from `+0x10`, checks the protocol, then calls the SCMI server.
The server dispatches the request before it writes FREE.
The direct notification helper finds the descriptor again.
For `apss_sec`, its zero completion address produces no peripheral write.
The interrupt callback does not clear a hardware mailbox status bit.

### INIT and request ownership

CPUCP initializes the separate handshake block through real instructions:

| Operation | SM8850 | SM8845 |
| --- | --- | --- |
| Clear `0x1d83c800..0x1d83c847` | `0x1d802fd0..0x1d802fde` | `0x1d803030..0x1d80303e` |
| Store `0x494e4954` to `0x1d83c804` | `0x1d802fea` | `0x1d80304a` |
| QSEE handshake helper | `0x15759130` | `0x1574bee8` |
| QSEE request-slot helper | `0x15759160` | `0x1574bf18` |
| QSEE notification helper | `0x15759170` | `0x1574bf28` |
| QSEE notification store | `0x15759180` | `0x1574bf38` |

QSEE copies INIT to `0x1d83c800` only when `0x1d83c804` already equals INIT.
The slot helper returns `0x1d83c400`.
Both notification helpers write `0x10000000` to `0x17960008`.
This is a 32-bit notification, not a 64-bit mailbox command.

SM8850 request submission `0x15759188` performs these operations:

1. Wait for slot `+4` to equal one.
2. Store the header and length, clear FREE at `+4`, and write flags at `+0x10`.
3. Copy request data through `0x15759330`.
4. Call notification veneer `0x157b760c`, which branches to `0x15759170`.
5. For synchronous mode, poll slot `+4` through `0x1575940c`.

Response extraction `0x15759298` requires FREE equal to one.
It then reads the response status and payload from the same slot.
Neither request submission nor the notification transport may construct a CPUCP response.

### Declared virtual notification latch

The firmware establishes the APSS writer and CPUCP source-117 consumer.
It does not measure the physical connection or its pulse-versus-level behavior.
The board therefore declares this bounded virtual connection explicitly:

`APSS W32 0x17960008, bit28 -> CPUCP PLIC pending source117 -> architectural input16`.

`platforms/qcom/sm88xx/cpucp-secure-notify.lua` connects the APSS router to the existing controller's optional `secure_notify_socket`.
The helper requires the CPUCP subsystem first.
It owns only the four-byte APSS aperture.
The helper does not alter the secure mailbox, PDP routes, shared RAM, or CPUCP firmware.

The socket accepts zero as a no-operation and bit 28 as a notification.
Unknown bits, reads, and unsupported widths fail.
Repeated notifications before claim coalesce into one pending bit.
A notification during service sets pending for the next claim after completion.
The direct pending update avoids a zero-time signal pulse and requires no host service callback.

The existing controller supplies the acknowledgement lifecycle:

1. A claim reads 118 for source 117 and removes its pending bit.
2. The controller excludes that source while the guest services the claim.
3. The guest executes the secure callback.
4. The guest writes zero to its completion register.
5. A notification that arrived during service becomes eligible after completion.

Claim helpers are `0x1d802034/0x1d802094`.
Completion helpers are `0x1d80201c/0x1d80207c`.
This virtual pending latch does not assert a persistent source level.
It cannot manufacture INIT, FREE, response bytes, or guest callback registrations.

### Boundary fixture and OEM probes

The new fixture is `tools/qcom/qcom-vp/tests/fixtures/cpucp-secure-notify`.
Its APSS guest writes requests and notifications.
Its CPUCP guest receives actual architectural interrupt 16 and claims 118.
The fixture checks masked pending retention and repeated pre-claim coalescing.
It checks a second notification during service and zero completion.
It checks that source 24 remains clear.
Only CPUCP guest stores produce the fixture response and FREE word.
The fixture also checks that no third claim remains.

The registered test is `cpucp_secure_notification_preserves_pending_through_service`.
It passes with the other five focused CPUCP and domain tests.
The complete integration run passes 47 host tests and 72 platform tests. Clippy passes with warnings denied.
OEM runs265 and214 stop earlier at `0x18877800`.
They do not yet prove genuine secure SCMI request processing.

For each OEM, stop at its INIT store, QSEE notification store, dispatcher, and FREE publication store.
Record both processor views of `0x1d83c400..0x1d83c847`.
With declared geometry, source 117 uses trigger word `0x18875014`, bit 21.
That word configures the input type. It does not expose pending state.
Its priority field starts at `0x18875060`, bit 20.
Hart-zero enable uses `0x188750b4`, bit 21.
Hart-zero claim/completion uses `0x188750f0`.
Require claim 118, the selected slot pointer, a correlated response, and the real FREE writer.
Record CPUCP input 16 and the guest completion write.
Do not accept a host-written response or a seeded INIT word as transport evidence.

These probes verify the declared virtual connection.
They do not prove that physical hardware uses an edge or a latch at `0x17960008`.

## Secure CPU_ON operation `0x4404`

This section corrects the earlier claim that selected CPUCP dispatch unconditionally returns `-3` for this operation.
The selected ELF images already contain real power-state handlers.
No later registration or runtime override is required to establish their dispatch.
This is static evidence, not proof that OEM execution reaches service readiness.

### APSS producer and static dispatch

| Operation | SM8850 | SM8845 |
| --- | --- | --- |
| Secure startup call to request helper | `0x1573e5b4` | `0x15731434` |
| Request helper entry | `0x15758fc0` | `0x1574bd60` |
| Submit `0x4404` | `0x15759020` | `0x1574bdc0` |
| CPUCP protocol dispatcher | `0x1d81f9fa` | `0x1d81f1ae` |
| Protocol `0x11` branch | `0x1d81fa40` | `0x1d81f1f4` |
| Power-domain command dispatcher | `0x1d81fb5e` | `0x1d81f312` |
| Command jump-table base | `0x1d82b6cc` | `0x1d82b784` |
| Command 4 branch | `0x1d81fb8c` | `0x1d81f340` |
| Power-state handler | `0x1d80e750` | `0x1d80edbc` |
| State-zero call | `0x1d80e878` | `0x1d80ef0a` |
| Power-on wrapper | `0x1d80b096` | `0x1d80b4cc` |

The request helpers use the existing secure slot and synchronous submission path.
They submit header `0x4404`, payload length 12, slot size `0x400`, and synchronous mode one.
The three request words are zero, `(requested_mpidr >> 8) & 0xffff`, and zero.
These occupy slot `+0x1c`, `+0x20`, and `+0x24`.
The final zero requests the on state.
The requested entry and context remain in secure software records, not these three words.

The CPUCP protocol dispatcher extracts `(header >> 10) & 0xff`.
Protocol `0x11` selects the power-domain command dispatcher.
That dispatcher reads the command byte at slot `+0x18`.
Its static command-4 entry branches directly to the real power-state handler.
The generic unsupported-response routines do not establish the selected command's result.

The handler converts the packed domain word into an internal cluster/core selector.
It checks the topology mask and reads requested state at slot `+0x24`.
For state zero, an already active core produces no second power-on request.
A supported inactive core follows the power-on wrapper.
The handler then constructs its response through guest instructions.
The surrounding SCMI server remains the real FREE producer.

### Initialization and core-release chain

The topology table starts at `0x1d82d8f0` in both images.
Each cluster record has stride `0x40`.
Its mask is at `+1`, core count at `+2`, and cumulative core index at `+3`.
The initial masks are `0x3f` and `3`.
The runtime constructor intersects these masks with the actual configured CPU-presence mask.
It counts the selected cores before publishing the runtime topology fields.
The count stores are `0x812d40f0/0x812440c2`.
These constructors start at `0x812d4000/0x81244000`.

The power-on wrappers require a valid cluster and a nonzero topology count.
They also acquire the cluster's guest lock.
They read cluster publication at `0x1887c000 + 4*cluster`.
A cold cluster follows preparation `0x1d8099ba/0x1d809d2a`.
An active cluster reaches per-core worker `0x1d809b0a/0x1d809e7a`.
The worker suppresses a duplicate request for an already active core.

| Core operation | SM8850 | SM8845 |
| --- | --- | --- |
| Per-core worker call | `0x1d80b164` | `0x1d80b59a` |
| Power-and-release helper | `0x1d8095ba` | `0x1d809920` |
| Set selected core in power `+0x278` | `0x1d8095a0` | `0x1d809906` |
| Poll power status `+0x2b0`, selected bit shifted by eight | `0x1d8095aa..0x1d8095b0` | `0x1d809910..0x1d809916` |
| Core policy store at per-core `+0x150` | `0x1d809562` | `0x1d8098c8` |
| Clear selected reset bit in power `+0x260[23:16]` | `0x1d8096b2` | `0x1d809a18` |

The cold path also programs warm vector `0x1468e000` through stores `0x1d80917a/0x1d80952c`.
The existing native domain controller retains this vector before it releases reset.
The core policy, power acknowledgement, reset hold, and vector remain separate effects.
No host SCMI implementation is required to perform this sequence.

The runtime domain records start at `0x1d82d980`.
Their strides differ: `0x268` on SM8850 and `0x248` on SM8845.
Guest initialization fills the additional cluster and core pointers.
Do not seed these records or the topology count from the host.

### Existing model coverage and remaining probes

The current native model implements the final core-policy, power request, acknowledgement, reset, and vector boundaries.
This does not establish complete coverage of runtime preparation.
Two statically visible boundaries remain outside the current ROM-derived register set:

- Cold preparation `0x1d809870` reads and writes power offsets `0x288`, `0x3f0`, `0x4e8`, and `0x4f8`.
  It also sets control `+0x260` bit 28.
  The existing power decoder does not accept these additional offsets or this control bit.
- Before reset release, hook `0x812d50c4/0x81245060` reads per-core descriptor `+0x140`, then register `+0xd00`.
  The initialized descriptor base is `0x19030000 + cluster*0x1000000 + core*0x100000`.
  Thus the hook reads `0x19030d00` for cluster zero, core zero.
  This is not the modeled core-policy aperture at `0x19032000 + ... + 0x150`.
  The hook passes the read value into registered guest predicates and callbacks.
  A constant host success value would not establish those semantics.

The second release hook is `0x812d55bc/0x81245558`.
The guest calls both hooks before it clears the selected reset bit.
The bounded next recovery must identify the `+0xd00` field and the cold power controls.
It must preserve guest callback ownership and derive any new status from actual modeled effects.
These gaps do not justify an SCMI response substitute or firmware patch.

For each OEM, stop at its command-4 handler and state-zero call.
Record the original header, three payload words, topology count, and current core bitmap.
Then capture the core power request, acknowledgement, both hooks, vector, and final reset-control store.
Require the actual secondary CPU to enter the secure warm vector.
Require secure software to retrieve the requested entry and context before the UEFI worker starts.

OEM runs265 and214 stop at the earlier constructor access `0x18877800`.
They do not prove this service, its response, or secondary execution.
This recovery ran no build, test, formatter, OEM run, or commit.

## Second-hart startup contract (SM8850 run276, SM8845-equivalent)

### Scope and result

Native `run276` reaches `0x1d81e592` with CPUCP RAM `0x18b301f0` zero.
This is a real wait on genuine second-hart startup, not the earlier memset panic barrier `0x33372`.
Static decoding of both `IMAGES/cpucp.elf` images establishes the software contract below.
The packages are CPH2749 `.700` and CPH2771 `.305`.

### Recovered contract

Hart 0 publishes an entry record and clears an acknowledgement word.
It dispatches through a started-hart mask and publishes the target hart ID at `0x18b3f600`.
The secondary path waits for that ID, loads its stack pointer, and writes the acknowledgement.
It then executes the assigned entry.

| Role | SM8850 | SM8845 |
| --- | --- | --- |
| Hart dispatch entry (every hart reaches this from `0x1d800000`) | `0x1d80178c` | `0x1d8017ec` |
| Hart-0 main call | `0x1d8017b2` → `0x1d81e342` | `0x1d801812` → `0x1d81daf6` |
| Nonzero-hart wait path | `0x1d8017b6` | `0x1d801816` |
| Release word (both OEMs use the identical address) | `0x18b3f600` | `0x18b3f600` |
| Stack-pointer word (`entry+0x800`) | `0x18b301e8` | `0x18b301f0` |
| Acknowledgement word | `0x18b301f0` | `0x18b301f8` |
| Register-and-wait function | `0x1d81e534` | `0x1d81dce8` |
| Record 1 (entry/context, stride `0x18`) | `0x18b3753c` | `0x18b36cf0` |
| Record 2 (scheduler, stride `0x38`) | `0x18b37958` | `0x18b37c80` |
| Dispatch/started-bitmask check | `0x1d803218` | `0x1d80327c` |
| Started-hart bitmask byte | `0x18b302ed` | `0x18b301c0` |
| DDR: publish NMI/recovery vector + clear PLIC `hart_init` + tail-call memset | `0x812d417a` | not re-decoded; same role confirmed present |
| DDR: subsystem control-register commit (`0x18876104`) | `0x812d41de` | `0x8124418a` |
| Release wait boundary (current native run stops here) | `0x1d81e592` | `0x1d81dd46` (per `acehr_evidence.md`) |
| Released-hart dispatcher | `0x1d81e5b8` | `0x1d81dd6c` |

SM8850 `0x812d417a` publishes `0x1d801298` at `0x17c40020+4*hart` and `0x17c40040+4*hart`.
It clears `0x18875100+4*hart`, updates the started mask, and tail-calls `memset(0x18b50000, 0, 0x20000)`.
The fixed destination does not establish a hart-specific memory allocation.

The routines at `0x812d41de` and `0x8124418a` clear control bit zero and set bit one.
They wait until bit one sets, then set bit 31.
The existing subsystem model reports this state through `ready()` at `0x18876104`.

### Release policy and limits

The inspected ROM prefix has no `mhartid` branch before its PLL configuration.
This supports a cold-held secondary, but does not prove the physical reset topology.
Both ROMs publish the two boot vectors at `0x17c40000` and `0x17c40004`.
The virtual platform releases hart 1 on the first subsystem commit.
This connection is virtual policy, not a measured silicon reset signal.
Only the executing guest writes the startup acknowledgement.

### Native cold hold

An initial reset signal during `start_of_simulation` failed with SystemC error `E521`.
QBox requires its reset callback to execute from an `SC_THREAD`.
The implementation instead sets the existing QEMU `start-powered-off` property before realization.
QEMU `hw/core/cpu-common.c` copies this property to the initial halted state.

On release, the controller requires a published second vector.
It sets `resetvec`, clears `start-powered-off`, and pulses the existing reset signal from its thread.
QBox completes the safe reset job before it releases the CPU.
Both harts use the same QEMU instance and shared CPUCP router.
The subsystem connects each hart to its own timer and interrupt output.

### Verification

The `cpucp-second-hart` fixture executes the publication, commit, and secondary acknowledgement sequence.
The cold entry rejects any hart except hart zero. The secondary entry requires `mhartid == 1`.
The producer waits for both guest-written completion bits before it reports success.
The focused native test passes after correction of the address immediate, poll polarity, and initial hold mechanism.
SM8850 run279 and SM8845 run221 execute the secondary dispatcher after the guest acknowledgement.
They next stop on the software-interrupt write at `0x18b80004`.
The PCs are `0x1d81e3ea` and `0x1d81db9e`, respectively.
The shared executable image grants DMI to both harts through `execution_sibling`.
The full integration run passes 47 host tests and 74 native tests. Clippy passes with warnings denied.

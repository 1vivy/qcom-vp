# CPUCP cluster watchdog configuration

## Scope

The selected CPUCP images configure a watchdog group before the APSS core release.
The code logs `Wdog cold boot setting done` or `failed` for each cluster.

SM8850 uses function `0x1d80417a` and first reads `0x17c30020` at `0x1d80428e`.
SM8845 uses the matching function at `0x1d80429e` and reads that word at `0x1d8043b2`.
Both functions use 32-bit accesses.

The direct image references identify these retained control pairs:

- `0x17c30000..0x17c30044`, with the gaps preserved.
- `0x17c2e050..0x17c2e05c`.
- `0x17c2480c` and `0x17c2580c`.
- `0x17c2b000` and `0x17c2b008`.
- `0x17c2c000` and `0x17c2c008`.

- `0x17c31010` and `0x17c31014`.
The cold sequence uses read-modify-write operations on each control word.
It writes decimal values `300000`, `301331`, or `301300` to the paired data words.
The source does not identify the source clock or the unit for these values.

Later helpers set command bits in some control words.
The observed cold path does not execute those helpers.
No selected source names the block or defines its register fields.
The virtual profile therefore does not name individual fields.

## Hardware observation

The Infiniti Gobbl context ran at EL1 with VBAR `0xc6804000`.
A temporary EFI instrument installed an app-owned vector table and executed `BRK #0x517`.
It captured ESR `0xf2000517` and the exact ELR in its private record.
It restored VBAR and DAIF before it returned.

The automatic catcher collected the same 224-byte prefix in `md_QVP_EL1_VECT.BIN`.
Capture `20261025T105416Z-900e-2` completed with status zero.
Its complete SHA-256 check passed.
This proves the plaintext fault-record path before an MMIO experiment.

The guarded 32-bit load from `0x17c30020` then captured ESR `0x96000010`.
FAR was `0x17c30020`, and ELR identified the exact load.
DFSC `0x10` identifies a synchronous external abort.
The handler skipped the load and restored VBAR and DAIF.
The result contains no register value.

The cleanup application released both reserved pages and cleared minidump slot 28.
The final context retained VBAR `0xc6804000`.
No MMIO write occurred.
The observation establishes that APSS EL1 cannot read this CPUCP-owned word.
It does not establish a reset value, a timeout rate, or an interrupt route.

## Virtual policy

Each observed 32-bit word starts at virtual cold zero and retains guest writes.
This substitute permits the original CPUCP cold configuration and readback operations.
It creates no timeout, bark, bite, interrupt, or reset.
It does not reuse the APSS watchdog at `0x17600000` and `0x17908000`.

The platform maps only the exact observed words.
Every gap remains an error.
Future command-bit execution requires a separate effect contract.
A retained command must not become an invented reset event.

SM8850 run310 and SM8845 run238 stop at the first previously unmapped word.
SM8850 run312 passes the complete mapped group and reaches CPUCP runtime configuration.
Run323 also passes the group but reaches the execution timeout without verified kernel entry.
These runs prove only the cold configuration substitute.
The watchdog event contract remains unknown.

## Second-cluster cold configuration

SM8850 `0x1d804246` selects base `0x17c2c000` for the second cluster.
It branches to the shared body at `0x1d804328`.
That body sets bit zero at offsets eight and zero through 32-bit read-modify-write operations.
The first-cluster path selects base `0x17c2b000` at `0x1d804324`.
The two branches use the same body.

Run552 stopped at the second-cluster read of `0x17c2c008`.
The composition now retains those two words under the existing cold-configuration policy.
Run553 passes this access and stops later at CPUCP write `0x1a850098`.
The retained words create no watchdog event and preserve all timeouts.

# TME fatal snapshot and XBL Config boundary

## Decision

Do not map `0x00810000` to RAM, an alias, or a ready-value register.
The retained runs reach this address inside a fatal diagnostic snapshot.
The selected OEM code reports a protection readback failure before that snapshot starts.
A new diagnostic peripheral model would conceal the first visible symptom without resolving that failure.


## Source identity

Both reconstruction manifests select these original inputs:

| Input | SHA-256 |
| --- | --- |
| Original `tme8850.bin` | `e5485515dde2da7b12ef0c06f1e5af8e1a56b693af2c49d43b68abd78db3deb3` |
| Donor `8975tme.bin` | `94059e1c3ba90f7112fa600a7033d83aa71cee6ff5573219d72b63c69c7b67d7` |
| CPH2771 `IMAGES/signed_firmware_soc_view.elf` | `6a781e1eb215fbcd2d157bebb7b25502f6ae74ed9bce7810dc61f98545c10fb4` |
| SM8850 `IMAGES/signed_firmware_soc_view.elf` | `4258fc24317e3467b39144ab72a46d85e4494c80bddc37b14de16177ff0c4a19` |

The manifests are `build/private/sm8845-reconstructed.toml` and `build/private/sm8850-reconstructed.toml`.
They retain the package paths and package hashes.
The prepared roots are `prepared-sm8845-common-runtime11` and `prepared-sm8850-common-runtime12` under `build/private`.
The donor file in the first root has the hash above.
Both selected ELF payloads also have the hashes above when read directly from their packages.

Donor coordinates use base `0x200000`, so file offset equals coordinate minus that base.
OEM coordinates below are TME-local addresses.
Their ELF load addresses start at `0x2041c000` and require subtraction of `0x20000000`, not interpretation as file offsets.

## Retained execution evidence

The relevant logs are under `build/private/run-sm8850-common-runtime24` and `build/private/run-sm8845-common-runtime25`.
Both `system.log` files report a four-byte read fault at donor PC `0x23178e`, address `0x00810000`.
The SM8850 UART log reaches `DP_Milestone`, `sbl1_xblconfig_init`, and `XBL Config - Image Load, Start`.

The instruction traces establish the earlier failure:

| Step | SM8850 | CPH2771 |
| --- | --- | --- |
| Permission readback comparison | `0x425d76`, `0x425d7a` | `0x425d2c`, `0x425d30` |
| Permission mismatch path, status `0xf053` | `0x425dce` | `0x425d84` |
| Expected/actual logging, event `0xf054` | `0x425dea..0x425dfc` | `0x425da0..0x425db2` |
| Transaction error fold | `0x4261ec..0x42621e` | `0x426194..0x4261c6` |
| Fatal caller, reason `0xf00a` | `0x424bc4..0x424bee` | `0x424b86..0x424bb0` |
| Fatal dispatcher | `0x420894` | `0x420914` |
| Snapshot import invocation | `0x4208fe..0x420906` | `0x42097e..0x420986` |
| Donor snapshot | `0x231738` | `0x231738` |

The traces contain the mismatch blocks before the diagnostic fault.
They do not retain runtime operand values at the comparisons.
A native SM8850 capture identifies the first mismatching operands at the original comparison instruction.

At the 528th validation, PC `0x425d76` had these values:

| Field | Value |
| --- | --- |
| Descriptor `s1` | `0x455304` |
| Region index `s0` | `6` |
| Controller base `*(s1 + 8)` | `0x05520000` |
| Descriptor resource field | `0x23` |
| Expected permission words `s6`, `s7` | `0x80000005`, `0` |
| Actual permission words `s9`, `s8` | `4`, `0` |
| Requested start and end | `0x1480e000`, `0x14810000` |
| Return address | `0x4261ea` |

The actual first word retains selector two but lacks the encoded selector-zero mask `0x80000001`.
The validator's mask preserves that entire missing mask.
Thus the comparison cannot succeed with the captured readback.
SM8850 run26 captured the complete region-write sequence:

1. APSS programs the bounds and permissions in namespace mode with no current owner.
2. APSS acquires ownership `0x80000001`.
3. TME acquires ownership `4` while read permission remains `0x80000005`.
4. APSS releases its ownership. The former model incorrectly reduces read permission to `4`.
5. TME repeats its acquisition and validates the previously acquired policy without direct reprogramming.

The remaining namespace owner requires its acquired policy, not merely its own QAD permission bit.
The model now retains the current policy separately for each trusted configuration authority.
An ownership release cannot remove a policy held by another authority.
This is a functional shared-ownership rule, not a claim about hidden silicon storage.

## Original region programming contract

Define `D` as the controller descriptor, `B = *(D + 8)`, and `R = B + 0x1000 + 64 * index`.
These definitions come from the original OEM instructions, not donor register numbering.

| Operation | SM8850 entry | CPH2771 entry |
| --- | --- | --- |
| Program a region | `0x426102` | `0x4260aa` |
| Validate a region | `0x425cfa` | `0x425cbc` |
| Program, acquire, and validate | `0x42617a` | `0x426122` |
| Find matching/free region and replace | `0x4264ec` | `0x426480` |
| Search descriptor region range | `0x425eba` | `0x425e70` |

The program routine receives this argument layout:

| Argument | Meaning established by stores |
| --- | --- |
| `a0` | Descriptor `D` |
| `a1` | Region index |
| `a2`, `a3` | Low and high start words |
| `a4`, `a5` | Low and high end words |
| `a6` | Also write region control `R + 0` when nonzero |
| `a7` | Permission word written to `R + 0x18` |
| Entry stack word zero | Permission word written to `R + 0x1c` |

The program routine performs this sequence:

1. If `D[0x10] & 3` is zero, write start low to `R + 0x0c` and end low to `R + 0x14`.
2. If that branch also has `D[0x17] & 0x20` set, write start high to `R + 8` and end high to `R + 0x10`.
3. Write the two permission words to `R + 0x18` and `R + 0x1c`.
4. Read `R + 4`, then write one to `R + 4`.
5. If `a6` is nonzero, read `R`, then write one to `R`.

The transaction routine preserves two additional stack arguments.
Entry stack word one requests ownership acquisition.
Entry stack word two suppresses direct programming when nonzero.
The routine calls the programmer with `a6 = 0` when direct programming is enabled.
It then performs optional acquisition and checks ownership before validation.
It does not OR the requested permission words with the previous region words.

SM8850 acquisition helper `0x426244` obtains a firmware mask from `0x427616`.
For its Boolean argument `v`, it writes `mask & -v` to `R + 0x30`.
The ownership predicate at `0x426282` obtains the same mask `M` and reads `O = *(R + 0x30)`.
It returns whether `M == (O & M & 0xc00007ff)`.
Thus acquisition and write-retention are separate from permission validation.
The inspected transaction passes one to acquisition when requested.

The ownership mask has a concrete cold-path producer.
SM8850 `0x427616` encodes selector two through `0x4274ee`, which returns four.
The encoder maps selector zero to `0x80000001`, selector seven to `0x40000001`, and other selectors to `1 << selector`.
Helpers `0x4275b8` and `0x4275e6` cache the special selector-zero and selector-seven masks.
Thus bits 31 and 30 are not unused padding.
Both special masks also contain bit zero.

Permission builder `0x427520` consumes 12-byte records containing selector, mode, and a flag.
Mode one adds the encoded selector to its first output.
Mode two adds the selector to its second output.
Mode three adds the selector to both outputs.
A nonzero flag adds the selector to its third output.
The caller must preserve these complete encoded words through the direct permission stores.
The exact security meaning of each selector is not established here.

The validator requires bit zero of `R + 4` to be set.
It then compares both permission words with the requested words.
SM8850 masks each read with `0xc00007ff` before comparison.
CPH2771 compares the complete read words.
Neither validator masks the expected operands at this comparison.
At the comparison instructions, `s6` and `s7` hold expected words, while `s9` and `s8` hold actual words.
Register `s1` holds `D`, and `s0` holds the index.

A permission mismatch returns `0xf053` and records expected/actual words through event `0xf054`.
A disabled region returns `0xf04e`.
For ordinary address-range descriptors, the validator also reads the stored bounds through `0x425e7c` on SM8850.
It compares both low/high pairs with the requested start and end.
A bounds mismatch returns `0xf050` and records two additional events.
Descriptor kinds with `D[0x10] & 3` nonzero skip the bounds check.

The transaction does not directly return `0xf053` after validation fails.
Its observed error block computes the nonzero status using the saved entry stack word two XOR `0xf057`.
This distinction prevents attribution of the fatal caller's status to a different register operation.

The search routine walks only the descriptor's advertised region range.
It tests region enable at `R + 4` and reads each enabled region's bounds.
It can also compare requested permissions against stored words combined with firmware-provided per-region permissions.
That search comparison does not change the direct programmer's two permission stores.
A selected region then reaches the transaction routine.
The outer caller invokes firmware bookkeeping only after the transaction returns zero.

## Configuration views and native replay

The first retained-policy implementation exposed both authorities' acquired permissions through each configuration socket.
SM8850 run28 showed that this readback changed the original range-selection algorithm.
APSS merged the metadata and payload into region seven, with bounds `[0x1480e000, 0x14821000)`.
The later release required the exact payload range `[0x14810000, 0x14821000)`.

SM8850 `0x426ba2` calls search `0x425eba` for that exact range.
The returned index was `0xffffffff`.
The wrapper returned `0xf049`, and caller `0x424d24` invoked fatal wrapper `0x420894` with `0xf00e`.
A native breakpoint captured the descriptor at `0x455304`, payload start `0x14810000`, and size `0x11000`.
Thus the diagnostic snapshot remained secondary to a range-selection failure.

Configuration readback now combines the base policy with only the caller's acquired policy.
Data authorization combines the base policy with both authorities' acquired policies.
Acquisition retains the caller's current configuration view, not another authority's private retained policy.

SM8850 run29 and SM8845 run27 pass both the earlier permission failure and this range failure.
SM8850 next reads the missing timer view at `0x17812004`.
SM8845 completes `boot_pre_ddr_dtb_load`, then reaches a rejected TSENS control write at `0x0c222004`.
The regression guest also verifies independent configuration views, shared effective data access, and revocation of cached access.
The build, Clippy, and all 65 tests pass.

## Donor diagnostic group

Donor `0x231738` writes signature `0x379bc963` to relocated runtime data `0x48607c`.
It saves machine CSRs at `0x486080` through `0x486098` using helper `0x2317c6`.
It clears the entry count at `0x48609c`.
It then uses two registered callbacks to enumerate addresses.

| Donor entry | Contract |
| --- | --- |
| `0x231702` | Reject null callback pointers with three. Otherwise retain both callbacks and return zero. |
| `0x23171c` | Reject null output pointers with three. Otherwise return list pointer `0x2335f4` and count `0x184`. |
| `0x2316d6` | Store the first list-entry pointer through the caller's output pointer. |
| `0x2316e2` | Advance the list-entry pointer by four. Return null after `0x610` bytes. |
| `0x231738` | Save CSRs, enumerate addresses, and record address/value pairs. |

The immutable list contains 388 words at `0x2335f4..0x233c03`.
The first word is `0x00810000`.
The snapshot stores each address at `0x4860a0 + 8*n`.
It reads that address and stores the value at `0x4860a4 + 8*n`.
It increments the count only after both stores.
The faulting load at `0x23178e` is the first such value read.
The snapshot does not set configuration, poll readiness, or authenticate an image.

The same list includes `0x00811000` and `0x008110d0`.
List membership alone does not prove a shared register ABI or justify a combined memory aperture.

Other donor bodies establish that the two pages contain more than passive storage:

- `0x223a3a..0x223a9c` enables separate controls through `0x00811000` and `0x008110d0`.
- `0x223a9e..0x223ba6` reads status `0x008110d4`, records several status-dependent payloads, and writes clear locations.
- `0x223c28..0x223cce` compares four words at `0x00810000` with four mask words at `0x00810010`.
- If no enabled event is present, that body saves the first four words and writes zero to them.
- `0x223cd0..0x223cec` restores those saved words.

These bodies support a diagnostic/event-control interpretation, not RAM.
They do not establish reset values, external event producers, or all field meanings.
The retained boot failure does not require a fabricated reset or ready value for this group.

## Integration decision and limits

The initiating failure concerns shared ownership, not diagnostic registers.
The region-write trace distinguishes a removed acquired policy from rejected direct stores.
The selected transaction deliberately skips direct programming before its final validation.

No `src/platform.cc` registration is required for this decision.
No platform composition change is justified by the snapshot fault alone.
Unknown addresses must continue to return errors.
Existing authentication results must remain unchanged.

A public search for both exact register addresses did not return a register specification.
Qualcomm's [Secure Boot and Image Authentication](https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/secure-boot-and-image-authentication.pdf) describes TME's trust role, not these registers.
It does not establish a reset state or register ABI for this model.

This evidence uses static disassembly, retained execution logs, native operand capture, and successful replay beyond the original fatal paths.
It does not prove kernel execution.

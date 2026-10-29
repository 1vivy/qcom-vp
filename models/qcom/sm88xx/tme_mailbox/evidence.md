# TME sequencer and mailbox evidence

## Status

`tme_sequencer.h` implements slot-zero ownership, command submission, entropy generation, output payloads, and completion acknowledgment.
The compatible key is `qcom,tme-sequencer-v1`.
Command completion follows the command's actual effects.
Unknown commands fail and log their opcode and arguments.

The actual TME ROM initializes protection and consumes its generated entropy.
It publishes its TPBL header and waits for APSS commands in shared mailbox RAM.
This result does not prove a complete mailbox service or kernel boot.

## Sources

- `tme8850.bin`, mapped at `0x00200000`, decoded as RV32 with compressed instructions.
- `pbl8850.bin`, mapped at `0x00300000`, decoded as AArch64 for APSS.
- The unmodified CPH2749 package `RADIO/tme_fw.img`.
  Its executable segment has file offset `0x1000`, APSS address `0x2041c000`, and TME address `0x0041c000`.
- Active QBox source `qemu-components/cpu_riscv/cpu_riscv32/include/riscv32.h`.

No source image bytes appear in this document.

## Bank addressing

The block at `0x02230000` is a per-slot sequencer CSR array.
It is separate from the CPU-layer mailbox RAM at `0x02000000`.
The source map places the sequencer inside the coarse TME hardware-layer region.
Observed slots zero, six, and seven retain separate identities.

The OEM firmware constructs TME address `0x02236000` at PC `0x00432f90`.
It passes that address and an output pointer to the function at `0x0042a224`.
PC `0x00432fa0` constructs `0x02237000` and calls the same function with a different output pointer.
PCs `0x00432fb0` through `0x00432fb8` pass both resulting objects to `0x00438bca`.
These calls establish separate base operands. They do not establish a bank-zero alias.

The function at `0x0042a224` dispatches through the function pointer at `0x00416dd4`.
The supplied base is an argument. A literal-only search does not resolve this dynamic driver path.
The pointer is zero at the observed first-stage ROM stop, before later firmware initialization.
The later implementation and its initialization remain unresolved.

APSS reads `0x2223602c` at PC `0x0030da1c` and reads `0x2223604c` at PC `0x0030d9dc`.
Thus APSS bank six and TME bank six name the same candidate physical bank through different address views.
[INFERENCE] The existing address translator can preserve bank six by subtracting `0x20000000`.
This does not establish any alias between TME bank zero and bank six.

A future model must preserve the bank identity until evidence establishes an alias.
[INFERENCE] APSS address `0x22236000` and TME address `0x02236000` must reach one bank-six state.
Neither address should become TME bank zero without separate evidence.
Bank seven contains additional controller accesses and must not become ordinary scratch storage.

## First hardware handshake

The protected ROM execution reached PC `0x0020ef48` and wrote `0x100` to `0x02230060`.
The earlier debugger observation identified the same request value.
Neither diagnostic changed the guest PC, registers, memory, or firmware.

The ROM does not treat offset `0x60` as a plain read/write word:

1. PCs `0x0020ef3c` through `0x0020ef48` obtain register ID zero and write bit eight.
2. PCs `0x0020ef50` through `0x0020ef62` obtain the same register and read it.
3. PC `0x0020ef6a` extracts bits `1:0`.
4. PCs `0x0020ef70` through `0x0020ef80` obtain a policy value and select expected state one or two.
5. PC `0x0020ef8a` compares the returned state against that policy-dependent value.
6. The loop reports failure after its bounded retry count. It does not accept the stored request word.

The policy address is `0x00c00200`.
The helper at `0x0020ec34` validates its address range and loads the word at PC `0x0020ec4a`.
The caller selects expected state two only when that word equals one. Otherwise it selects state one.
The helper's MMIO address and the expected state are not a measured reset value or an authentication result.
The board now exposes this word with explicit virtual policy zero.

The complementary path reads offset `0x60` at PC `0x0020ee94`.
PC `0x0020ee98` extracts bits `6:4`, and PC `0x0020ee9e` requires value one.
PCs `0x0020eeba` through `0x0020eebe` then set bit nine and write the word.
Another loop at `0x0020f0c0` waits until bits `6:4` equal four.

The CSR at offset `0x60` holds the owner and lock fields.
ROM accesses identify owner bits 1:0 and phase bits 6:4.
Bit eight requests acquisition. Bit nine submits the command.
The virtual engine starts idle and grants its single client the policy-selected owner.
It completes only after the implemented command produces its output.
Acknowledgment clears completion and returns the virtual engine to idle.
This policy does not claim physical arbitration timing or undocumented ownership identities.

The complete runtime writes policy one at `0x2308c0..0x2308c2` through helper `0x21cb14`.
The helper stores the value at `0x00c00200`.
The model permits policy zero or one while idle.
It rejects a policy change while a command owns the engine.
The next acquisition returns the selected owner without changing command results.
The guest regression hashes separate messages under both owners and checks both digests.
All 44 integration cases passed.

The earlier helper at `0x00200d70` also reads bits `6:4` from offset `0x60`.
It requests bit eight, waits for field value one, writes four payload words, and requests bit nine.
It then requires both offset `0x128` bit zero and field value four.
The sequencer or its peer must produce the completion notification.
The ROM then writes one to offset `0x130` as an acknowledgment.
This does not prove a complete interrupt route or the command service's behavior.
Setting GO alone cannot justify a successful command response.

## Completion interrupt

The runtime setter at `0x21d41a` selects register ID 24 through the generic write service.
The observed write reaches offset `0x12c`.
OEM `0x41c21e..0x41c224` writes zero during startup.
OEM `0x41eeaa` registers handler ID 23 before `0x41eeb8` writes one through the same setter.
These operations establish the completion interrupt's enable control and CLIC route.

The model retains enable bit zero at offset `0x12c`.
Its optional `irq` output asserts only when completion and this enable bit are both set.
Masking the interrupt does not clear completion.
Enabling an already completed operation asserts the output.
Acknowledgment at `0x130` clears completion and deasserts the output.
Unsupported commands still cannot produce completion.

The private profile connects this output to CLIC source 23.
The real OEM run now passes its initial interrupt-disable write.
The guest regression delivers three entropy completions through the CLIC and verifies interrupt cause 23.
It checks masked completion, masking an asserted source, later enable, and acknowledgment.

Register state changes synchronously, but a SystemC delta event propagates the IRQ level.
The guest waits for pending assertion before testing the sequencer mask.
It waits for pending deassertion before leaving the interrupt handler.
These observations replace scheduling assumptions without weakening the status or interrupt-count checks.

The runtime profile selects virtual CLIC reset priority one.
A live SM8850 snapshot found command `0x13` complete, with both interrupt gates enabled.
Source 23 remained pending because its earlier reset priority equaled the zero threshold.
Priority one permits delivery without creating a completion or changing its result.
Both OEM runs then reach the next unmapped import, `0x70c`.
This reset priority is virtual policy, not measured silicon state.


## Register accessor evidence

The first ROM dispatcher starts at `0x00211e82` and uses the table at `0x002136b8`.
The later dispatcher starts at `0x0021fbb6`.
Both construct bank-zero addresses. The following first-stage register IDs are useful during further investigation:

| Register ID | Local offset | Evidence |
| --- | --- | --- |
| 0 | `0x60` | `0x00211e98` through `0x00211ea0` |
| 1 | `0x00` | `0x00211f2a` |
| 2 | `0x144` | `0x00211f04` |
| 3 | `0x148` | `0x00211f18` |
| 4 | `0x160` | `0x00211ed6` |
| 5 | `0x128` | `0x00211f42` |
| 6 | `0x130` | `0x00211f60` |
| 11 | `0x4c` | `0x00211ecc` |
| 15 | `0x2c` | `0x00211f0e` |
| 17 | `0x18` | `0x00211f30` |
| 28 | bank seven, `0x10` | `0x00211f98` |
| 29 | bank seven, `0x14` | `0x00211fa0` |

An accessor returns an address. It does not establish that register's reset value, ownership, or side effects.

## Entropy command

ROM function `0x2116e0` submits opcode `0x13`.
Its first argument is the requested byte count, rounded upward to words.
The other two arguments are zero.
Function `0x211740` copies the response into the caller's buffer through `0x20ed72`.

The initializer at `0x200338` requests four bytes through `0x20dd00`.
It stores the returned word at `0x403210`, the stack-guard location.
The callers at `0x200f2c` and `0x200f8e` request one byte for randomized delay checks.
These consumers establish a random-data service, not a canned status command.

The virtual engine obtains the requested bytes from the host's `getrandom` interface.
It reports the resulting word count in status bits 23:8.
It does not expose host errors as successful commands.
It does not log the generated random values.

Function `0x211e34` bounds each payload at 1024 words.
Input words start at `0x02281000`.
Output words start at `0x02289000`.
Function `0x211fb6` reads the completed output count from offset `0x144`.
Function `0x20f266` acknowledges completion through offsets `0x128` and `0x130`.

## Interrupt and reset boundary

QBox exposes `platform.tme.irq_in[0..31]`.
`riscv32.h:130-133` connects each socket to the QEMU GPIO input with the same index.
This identifies a real signal interface, not the mailbox's interrupt route.
Do not connect an invented mailbox output to an arbitrary index.

The first-stage reset path clears `mstatus`, `mip`, and `mie` at PCs `0x00200048` through `0x00200050`.
The observed mailbox path polls registers. This path does not prove that a RISC-V interrupt is required.
An instruction scan found no first-stage writes that enable `mie`.
Later OEM firmware saves and restores `mie` at PCs `0x00428018` and `0x004280dc`.
Those operations do not identify the source, cause number, or controller route.

No instruction in this investigation establishes a core power/reset request and acknowledgement interface.
Do not replace that missing interface with a host write to the guest PC.

## Remaining service contracts

The later firmware requires additional sequencer commands and distinct slot-six and slot-seven behavior.
Each command must implement its memory effects or cryptographic operation before it can report completion.
The current entropy command does not bypass image authentication.

The implementation behind function pointer `0x00416dd4` still requires analysis.
An interrupt route is necessary if later firmware enables notifications.
The early ROM polls the implemented status registers and does not require an invented interrupt.


## Bank-six boot status

The integrated OEM run `run-sm8850-functional-controls14` reached APSS `0x30d9dc`.
It attempted the version read at `0x2223604c` after UFS link negotiation.
A read-only debugger capture stopped before that load.
The APSS minimum version word at `0x148137f0` was zero.
The real response header at `0x22000000` contained the TPBL signature, capacity two, and zero counters.
The request header at `0x22002800` remained zero.

`boot_status.h` implements `qcom,tme-boot-status-v1` for this separate bank.
The APSS view starts at `0x22236000`.
The TME view starts at `0x02236000`.
Neither view aliases sequencer bank zero.
The board explicitly selects interface version zero at bits 23:16 of offset `0x4c`.
This metadata is virtual policy, not a captured hardware revision.

Before startup, the status bridge reads the actual response header through a read-only TLM initiator.
It requires capacity two and an outstanding count no greater than that capacity.
It reports state five for a complete TPBL header.
It reports unavailable state zero for incomplete, unknown, or inconsistent headers.
Header removal revokes this initial readiness.
No poll writes memory or advances firmware.

This relation is explicit virtual wiring between firmware publication and channel readiness.
It does not establish the physical producer of each bank-six register.
APSS `0x30da1c` waits for state five before it initializes the request channel.
APSS `0x30ddbc` recognizes state one.
The response parser independently recognizes TPBL and TMFW headers at `0x30f078`.
These consumers define the virtual bridge's cold-boot contract.

The bridge never writes a response slot or changes a guest PC.
The authenticated startup operation implements only the sequencer image contract described below.
The real TME firmware remains responsible for its mailbox commands and later image authentication.
Other bank-six registers remain unsupported.
This model does not implement warm reset or the state-two request transition at `0x30de2c`.

The actual guest fixture checks incomplete publication, TPBL readiness, invalid queue counts, TMFW transition, and readiness removal.
The OEM run separately checks publication by the real TME ROM.

## Sequencer image storage

The selected package contains `IMAGES/sequencer_ram.elf`, with 4,864 bytes.
Its SHA-256 is `d7dc67ddd77332e558d14b7acb82bc42e1be66a07786403c96b655f46d019636`.
The ELF32 header declares machine one and entry `0x222a4000`.
Its two load segments define these destinations:

| APSS address | File offset | File and memory size | Backing |
| --- | --- | --- | --- |
| `0x222a4000` | `0x74` | `0x1000` | TME program RAM at `0x022a4000`. |
| `0x22281000` | `0x1074` | `0x28c` | The existing input RAM at `0x02281000`. |

The integrated run `run-sm8850-functional-controls15` reached the first destination through APSS copy instruction `0x30ca48`.
The ROM's allowlist also bounds these destinations to separate 4 KiB windows.
The platform now supplies those bounded views.
The real APSS loader reads the image from UFS and writes its segments.
The host does not preload either segment.

The program window retains guest bytes and starts at zero by explicit virtual policy.
The protected input view shares the sequencer's actual input array.
It permits loader access only while the engine is idle.
It cannot acquire the engine, change an active command, or publish a response.
Both APSS views preserve the TME protection checks.

The functional sequencer executes its implemented command semantics in C++.
It does not interpret the custom program image.
Loading an image alone does not implement another opcode or qualify authentication.
Unknown operations still fail instead of supplying a success value.

## Authenticated sequencer startup

The integrated run `run-sm8850-functional-controls16` copied both image segments through the real APSS loader.
It then wrote one to `0x22236000` at APSS PC `0x30d994`.
Function `0x30dbf8` requires result three at offset `0x18`.
Function `0x30daf8` then requires state three at offset `0x2c`.
A different result selects the error path and requires state one.

The input segment contains nested TLV records, not an opaque configuration block.
Each header contains a little-endian 16-bit tag and length.
Each record occupies a multiple of four bytes.
Scalar fields use little-endian encoding.
Public coordinates and signature scalars each occupy 68 bytes in big-endian encoding.

| Input offset | Tag | Meaning established by verification |
| --- | --- | --- |
| `0x000` | `0x0182` | The signed command envelope contains 648 payload bytes. |
| `0x00c` | `0x8291` | The command selects software ID `0x37`, version one, and hardware selector `0xa01bffff`. |
| `0x034` | `0x8384` | Algorithm five identifies the exact SHA-512 digest of the complete program segment. |
| `0x084` | `0x81ff` | The P-521 command signature verifies input bytes `[0,0x84)` with SHA-512. |
| `0x110` | `0x8080` | The certificate identifies key one and algorithm two. |
| `0x130` | `0x8281` | The complete signing entitlement occupies bytes `[0x130,0x200)`. |
| `0x174` | `0x8411` | The subject public key verifies the command signature. |
| `0x200` | `0x81ff` | The issuer signature verifies the complete signing entitlement with P-521 and SHA-512. |

The signed input selects software ID `0x37` at offset `0x00c`.
The source-only name `TME_SEQ_SS_SWID` remains unestablished.
The SM8845 package contains the identical sequencer image.
This fact does not establish the identity of other SM8845 hardware.

Two independent vendor entitlement certificates establish the issuer public key.

| Certificate | SHA-256 |
| --- | --- |
| `Dec-LimitedDebug-Unconstrained.bin` | `e200866bf0579395c53b8c55d006bfbe829704729d677bf7c6fa91c41b94ce0b` |
| `Dec-FullDebug-SerialNumberConstrained.bin` | `e0f9a7d4c1f682515552da636c4cb7b2545b3c2c176f8794f3e115f82f79b2f4` |

ECDSA public-key recovery produced two candidate points for each signature and candidate signed range.
Only one public point matched both certificates.
Both signatures cover their complete `0x8282` entitlement records, starting at file offset `0x20`.
That public point also verifies the sequencer's `0x8281` entitlement signature.
This calculation recovers a public key, not a private key.
It does not use a device service or authorize either debug entitlement.

The 136-byte issuer point has SHA-256 `018fb5e7548deb0dc12d1590cec9c6a38a38f6c3049a953a0a2eda5c9715b5f0`.
The signed sequencer entitlement has SHA-256 `041d04c97daf71bc6c6def710cbd5d14784874b8fb2ec986d6cac61c1eec737e`.
The private firmware manifest records the issuer file and its SHA-256.
Preparation and execution check that file before the platform starts.
The repository stores no vendor certificate bytes or public-point byte copy.

`sequencer_auth.h` uses OpenSSL 3 to verify the certificate, command signature, and actual program hash.
The virtual profile also pins the entire signed entitlement.
Some chip-constraint fields remain undecoded, so a different entitlement fails closed.
This restrictive policy does not claim to reproduce the physical retail fuse policy.
Missing trust, invalid signatures, a different hardware selector, or a changed program cannot produce success.

A write of one to offset zero starts verification against the actual program and input RAM.
The virtual model reports result three and state three only after all checks succeed.
It reports result one and state one after failure.
These failure values form an explicit virtual contract, not a recovered vendor error code.
A complete TMFW header requires authentication and both handoff flags before it selects state four.
The operation does not inject a mailbox response, modify firmware, or redirect execution.

A host smoke executed the actual C++ verifier against the selected OEM image and the derived issuer key.
It printed `REAL_CHAIN_PROGRAM_AND_COMMAND_VERIFIED`.
It rejected a changed program, command signature, certificate signature, and hardware selector.
The actual AArch64 guest regression passed eight independent MMIO cases with synthetic keys and signed images.
The valid chain produced result three and state three.
The changed program, command signature, certificate signature, trusted root, and entitlement each produced failure.
A malformed signature and absent trust also produced failure.
These cases execute the real model and OpenSSL, not a mock cryptographic service.

The integrated OEM runs `run-sm8850-functional-controls19` through `run-sm8850-functional-controls21` recorded `TME_SEQUENCER_AUTHENTICATED`.
The APSS firmware then published its first TME request.
Read-only debugger captures found identical APBL request headers through both processor views.
The TME firmware accepted that header and continued into its authentication-policy queries.
Those runs still stopped before OEM UEFI, ABL, or Linux.

## Sequencer SHA-384 submissions

The private run `run-sm8850-functional-controls33` reached command one at TME PC `0x20eebe`.
Its arguments were `{2,1,3}`.
The acquired registers held control one, input length `0x280`, and total length `0x280`.
Only four input words were written: `{1,0x20408c1e,0x280,0}`.
The remaining captured input bytes were stale bootstrap data.

The source descriptor at RV32 address `0x401c98` describes a 647-byte certificate.
Its first submission consumes 640 bytes.
The source certificate has SHA-384:

`7934b2f6511070d6d385a21347920eb68db889a9fd635316729c3acd9701c7234e64947a994678ddc1a2c4a2b5881552`

This digest matches the resident ROM bytes at `0x212e8b`.
The certificate is the QTI test root in `IMAGES/multi_image_qti.mbn` at offset `0x274e`.
It is not the production root of the loaded TME image.
The private captures and vendor bytes remain outside git.

Disassembly of the selected TME ROM establishes the following framing:

| ROM address | Operation |
| --- | --- |
| `0x20fc8c..0x20fcf8` | Build the generic descriptor and assign phase values three, four, and five. |
| `0x20fd6e..0x20fdea` | Split full blocks from the pending tail. Phase five includes the tail. |
| `0x20fdeE..0x20fe30` | Update the cumulative length and serialize it at descriptor offset `0x74`. |
| `0x20ff00..0x20ff56` | Write the command, arguments, current length, cumulative length, and control register. |
| `0x20ff60` | Write the source count as input word zero. |
| `0x20ff72..0x20ffb2` | Copy the previous raw hash output after word zero for phases four and five. |
| `0x20ffd8..0x210022` | Serialize the source descriptors. |
| `0x20efd6..0x20f078` | Write each descriptor as `{address,length,next}`. Zero terminates each observed source. |
| `0x210180..0x2101b8` | Return 64 output bytes for type two, argument one, and a non-final phase. Return 48 bytes for phase five. |

The model implements command one with arguments `{2,1,phase}` and control one.
Phase three initializes SHA-384. Phase four continues it. Phase five applies SHA-384 padding and returns the digest.
Phases three and four require complete 128-byte blocks.
Continuation headers contain 64 state bytes between the count word and the source descriptors.
The cumulative length reconstructs the SHA-512 bit counter.
The model imports those guest state bytes without retaining a hidden host hash handle.

The virtual state encoding uses eight big-endian 64-bit chaining words.
The ROM copies these bytes without conversion.
The ROM alone does not independently establish the physical engine's state byte order.
The guest regression tests this explicit virtual encoding through split submissions and a standard SHA-384 oracle.

The DMA initiator reads the TME RAM alias at `0x20400000..0x2045bfff`.
Both protection banks retain the TME requester domain.
Translation must not replace that domain with the APSS requester domain.
Run `run-sm8850-functional-controls34` exposed that error at the seven-byte tail address `0x20403440`.
Run `run-sm8850-functional-controls35` passed this access and reached signature command eight.
An unbound DMA socket cannot complete a hash command.
Each submission must write its complete header and descriptors after acquisition.
Malformed counts, inconsistent lengths, nonzero links, address overflow, and failed DMA reads prevent completion.
Unsupported algorithms, phases, controls, and command `0x25` remain unimplemented.

`sequencer_hash.h` isolates the OpenSSL SHA-512 context API.
EVP does not expose the chaining state required by this ABI.
The guest fixture uses synthetic bytes and compares two split hashes with RustCrypto SHA-384 results.
It changes an input byte and requires a different digest.
Separate cases require the submit write to fail for a missing descriptor, an unmapped source, and an unsupported phase.

The actual engine also hashed a 577-byte certificate body from `IMAGES/multi_image_qti.mbn`, offset `0x2493`.
A debugger capture produced SHA-384:

`5ac474962e1a2bb6664f2fc133f79818e9148abb2c527a88dde56b6f85a0f0c79a440395e3ec7544d4a5a98b30633207`

An independent host calculation matched this digest to the exact certificate body.
The certificate contains the QTI test SubCA public key.

## Sequencer P-384 signature verification

ROM `0x20f5cc..0x20f6a8` constructs command eight.
The captured command has arguments `{2,1,0}`.
The operation uses these acquired registers:

| Offset | Value | Observed role |
| --- | --- | --- |
| `0x2c` | `1` | Operation control. |
| `0x34` | `1` | Signature control. |
| `0x38` | `4` | P-384 curve selector. |
| `0x50` | `0` | Signature options. |
| `0x58` | `48` | Digest length in bytes. |

The input contains these fields:

| Offset | Size | Value |
| --- | --- | --- |
| `0` | `4` | Zero header word. |
| `4` | `96` | Public coordinates X and Y, each a 48-byte big-endian integer. |
| `100` | `96` | Signature integers R and S, each a 48-byte big-endian integer. |
| `196` | `48` | SHA-384 digest. |

A debugger captured all 244 bytes before submission.
The public coordinates match the certificate at offset `0x2493` in `IMAGES/multi_image_qti.mbn`.
The signature matches the certificate at offset `0x2218`.
The digest matches that certificate's signed body.
An independent host verifier accepted the captured signature, key, and digest as P-384 with SHA-384.

ROM `0x20f216..0x20f264` requires completion state `0x40`.
It then reads status offset `0x144` and rejects bit zero.
The signature consumer at `0x20f7d0` uses this result without reading an output payload.
The model uses OpenSSL EVP verification and reports this error bit for invalid keys, signatures, or digests.
Unsupported framing cannot produce a completion.
The model does not infer the physical error-detail registers from this status bit.

The synthetic guest regression passed valid and invalid P-384 submissions through the actual MMIO model.
It rejected zero public coordinates, zero signature integers, a changed signature, and a changed digest.
A subsequent valid signature cleared the error status.
An unsupported curve caused a submit fault instead of completion.
The integrated SM8850 run `run-sm8850-functional-controls36` continued to TME PC `0x205244`.
It stopped at an unmodeled read of `0xc0200c`, before UEFI or Linux.

## TME software state banks

The model key is `qcom,sm8850-tme-software-state-v1`.
ROM `0x205208..0x205262` accepts a bit index from zero through 63 and a guarded enable value.
It reads the selected state word, sets the requested bit, and writes the corresponding update alias.

| Bank | State read | Update write |
| --- | --- | --- |
| Low | `0x00c02008` | `0x00c0022c` |
| High | `0x00c0200c` | `0x00c00230` |

The platform supplies zero as the explicit virtual reset state of both banks.
Actual firmware writes change that state.
The model does not supply a fixed authentication result or set bits without a guest write.
The firmware preserves all previously set bits in the observed update sequence.
A write that clears a set bit remains unsupported because its physical semantics are unknown.
ROM `0x204d96..0x204d9e` also reads high-bank bit three during a later policy check.
The model does not name that policy or infer the other peripheral registers.

The native TME regression writes both banks and then updates the high bank.
The state reads preserve the low bank and all previously set high-bank bits.
The SM8850 run `run-sm8850-functional-controls37` passed the software-state update.
It stopped at TME PC `0x201062`, reading rollback word `0x02308458`.

### Shared handoff flags

TME `0x2005e8..0x2005ee` reads `0x00c02018`, sets bit 1, and writes the word.
APSS `0x30de68..0x30de70` performs the same operation with bit 2 through `0x20c02018`.
Both views reach one retained word through the existing protection routes.
Its explicit virtual reset value is zero.
Clearing flags or changing other bits remains unsupported.
The word does not create readiness transitions or replace firmware execution.

The native guest regression sets both flags and reads value six.
It also checks that both software-state banks retain their independent values.
The full suite passed all 44 cases.
Run `run-sm8850-functional-controls48` passed the TME flag write and entered the runtime at `0x216000`.



## Firmware staging and image hashing

Run `run-sm8850-functional-controls38` submitted a UFS write to `0x204084d0`.
That address belongs to the APSS view of TME staging RAM.
The UFS requester now traverses protection bank one and the existing external TME protection route.
The transfer updates the same backing memory that the real TME core reads.

Run `run-sm8850-functional-controls43` requested SHA-384 input from `0x14827000`.
The requested chunk contained 256 bytes of the UFS-loaded image in APSS SRAM.
The sequencer now accesses that backing memory through protection bank one with its TME requester identity.
Run `run-sm8850-functional-controls44` passed this hash request and reached the local timer control at `0x00c2202c`.
Neither route supplies bytes independently of the media or substitutes an authentication result.

## Secondary hardware binding

The CPH2771 QTI ELF contains hardware ID `0xa022` at file offset `0x2050`.
TME `0x20ace0..0x20ad74` compares the hardware-version upper halfword with twelve signed metadata entries.
The private `diagnostic-sm8845-status-tme.log` records the exhausted search and error 23 at `0x20ad5a`.
The real mailbox response then contains error `0x10017`.

The secondary profile initially reported hardware version `0xa0220200`.
The low revision remains virtual policy.
Run `run-sm8845-functional-controls6` rejects the loaded sequencer command with `wrong-hardware-version`.
The QRD8850 loader selects the package's `sequencer_ram.elf`, which binds its command to `0xa01bffff`.
The current shared hardware identity cannot satisfy that binding and the QTI image's `0xa022` requirement.
The compatibility label does not authorize a signature or hardware-policy bypass.
This result establishes a compatibility limitation, not a kernel handoff.

## Complete-ROM compatibility probe

The compatibility path reconstructs the called runtime code because the matching SM8850 dump lacks it.
The private probe uses the complete `8975tme.bin` as a separate experiment.
It does not append that file's tail to the SM8850 ROM.
Its chip provenance remains unestablished.

The complete ROM uses protection registers at `0x07a00000` and mode control at `0x07a28000`.
Instructions `0x20d924..0x20da02` establish the mode and region layout.
The second controller retains its `0x00c28000` base.
The private platform data moves the first model without changing its permission semantics.

The acquisition path reads `0x0228000c` twice at `0x20f744` and `0x20f760`.
It requires equal readings and zero before it accepts engine availability.
The optional model socket reports zero after the virtual payload arrays and ownership policy have initialized.
This is explicit virtual availability behavior, not an established physical register name or simulated memory-fault circuit.
The socket cannot acknowledge a command or authenticate an image.

The complete ROM polls timer frame zero at `0x2007cc..0x2007e0`.
The runtime enables machine timer interrupts at `0x2282b0` and returns the frame-zero base at `0x22acda`.
The private probe connects that timer output to the real RISC-V machine timer input, interrupt seven.
It does not remove the model's check for an unconnected interrupt output.

The mixed-ROM probe then reached an APSS write to `0x20408030` at PC `0x30cb34`.
The TME protection model rejected that write.
This result does not establish compatibility between the old APSS ROM and the complete TME ROM.

## Private runtime reconstruction

The reconstruction preserves the original boot ROM below `0x216000`.
It replaces the entire runtime region with the complete candidate, not an unmatched tail.
The candidate SHA-256 is `94059e1c3ba90f7112fa600a7033d83aa71cee6ff5573219d72b63c69c7b67d7`.
Its chip provenance remains unknown.

The decoded runtime has no direct jump into the boot-ROM region.
Its executable range ends at `0x231b48`.
The initialization function at `0x2309e2` copies data from `0x233c84` and clears runtime RAM.

The experiment relocates candidate RAM references by `0x70000`.
The candidate runtime uses private memory at `0x480000..0x49ffff`.
Its log page also moves from `0x404000` to `0x474000`, outside the OEM's locked buffer reservations.
This prevents its state and import table from overlapping OEM state.
In-place immediate changes relocate absolute and PC-relative RAM address loads without changing their registers.
Initialized pointers receive the same relocation.
The script checks both input hashes and preserves every original boot-ROM byte.

The boot ROM supplies eight image-record slots with stride `0x308`.
The candidate runtime requires stride `0x348`.
The private guest adapter preserves a separate legacy descriptor copy for the OEM.
It expands the candidate records and adjusts their affected pointers.
The OEM entry receives its original descriptor layout.

The [execution evidence](../../../../docs/qcom/sm88xx/oplus/bringup.md#optional-common-runtime-reconstruction) records the current reconstruction identity.
The real CPU enters the unmodified OEM TME image at `0x41c000`.
The live ABI bridge maps explicit legacy slots to candidate slots.
It preserves OEM callback updates instead of copying the import table once.
Unmapped slots stop execution.

OEM `0x423388..0x423474` installs its clock operations and preserves several original callbacks.
The operation tables at OEM `0x446808` and candidate RAM `0x419c58` establish the five PLL callback positions.
The runtime completes CLIC initialization and programs the four PLLs from OEM descriptors `0x44688c..0x44690b`.
The existing PLL model supplies functional acquisition and lock status.
The private platform supplies declared virtual references to the observed clock branches.
Guest writes still control each branch.

OEM `0x42960e` reads the additional part identifier at `0x131db014`.
The virtual value uses the declared JTAG part number, `0x2b3`.
OEM table entry `0x443888` maps this part to chip identifier `0x294`.
This value remains virtual profile data, not a captured register.

The boot run passes clock setup, chip identification, and the access-control configuration tables.
It initializes architectural PMP and creates OEM tasks, queues, and a counting semaphore.
The virtual monitor still stops on unsupported or denied accesses.
No UEFI dispatch, ABL dispatch, kernel entry, or Linux log has been observed.

The OEM locks the executable ROM range to `0x200000..0x237fff`.
The earlier relocation trampolines exceeded that range and caused an instruction-fetch fault at `0x238084`.
The reconstruction now changes 2,007 address-load instructions in place.
An independent check verified all 2,006 ordinary relocations and preserved register encodings.
The entry instruction uses an eight-byte adapter at `0x234a00`.
The descriptor adapter starts at `0x234c00`, within the permitted ROM range.

The OEM secondary log getter and setter use separate retained state in the compatibility adapter.
The candidate's indexed record writer cannot replace the original pointer setter.
The candidate log retains its own primary buffer layout.

The startup mapping from legacy `0x22c` to candidate `0x240` remains an inference.
Its candidate initializer consumes the matching ten-word OEM provider and writes fourteen-word core configuration storage.
The private bank at `0x07ad2000` retains these words without undocumented hardware effects.
The real run passes this initializer and reaches scheduler object creation.
This result does not prove instruction equivalence with the unavailable original initializer.

The candidate request-ring getter returns `0x02001000`.
The original APSS ROM publishes requests at `0x02002800`.
A live observation found request twelve, command five, pending in the original ring.
The compatibility adapter replaces that getter, not the ring contents or completion state.
The OEM then receives the request, publishes its response, observes peer consumption, and changes its response signature to `0x57464d54`.

Legacy slots `0x9ec`, `0x9f8`, `0x9f0`, and `0x9f4` map to candidate receive, completion, consumption-query, and initialization operations.
The candidate initializer retains the original capacity of two records.
The OEM publishes milestone five itself after the control transition.
The compatibility layer does not force that milestone.

## Debug-access policy command

Candidate `0x2203b6..0x220452` submits sequencer command `0x25`.
The input starts with an address count and then contains that many register addresses.
The handler does not rewrite command words one through three.
The observed word-one value, `0x80`, remains from the preceding entropy request.
It is not a payload length.

OEM `0x423f98..0x424000` submits groups of at most eight addresses.
The observed table at `0x449288` contains 31 DBGAR addresses.
Candidate completion code `0x220456..0x2204a8` copies output word zero into the caller's result.
The source-only dictionary name for failure `0xf025` remains unestablished.
It records the failed address with the error.

The virtual command requires an explicit `debug_access_value`.
It writes that policy through DMA and checks each register's readback.
It returns zero after all comparisons match, or the first mismatching address with an operation error.
An unsupported DMA transaction stops execution.
Each acquisition requires all bytes of its own address list.
The command cannot reuse stale payload addresses.

The private profile selects zero, which disables external debug access.
This choice does not reconstruct the physical fuse-to-DBGAR policy.
It does not alter ordinary region permissions.
The real firmware completed the DBGAR batches and advanced to protection geometry reads.
The guest exercise checks actual register changes, independent state, and rejection of a stale address list.


## TME watchdog

OEM `0x429560..0x4295d4` establishes the watchdog registers at `0x00c24000`.
Offset zero accepts a pet pulse.
Offset four retains the control word, whose bit zero enables counting.
Offset eight returns the low twenty counter bits.
Offsets `0x0c` and `0x10` select bark and bite deadlines.
OEM `0x428ff6` and `0x42900c` multiply milliseconds by 32 before programming those deadlines.

The private profile therefore selects a 32,000 Hz virtual counter.
OEM `0x4293dc` registers the bark handler as interrupt 27.
The private platform connects the bark output to CLIC source 27.
The `qcom,tme-watchdog-v1` model schedules both deadlines from elapsed simulation time.
Petting clears the counter and both deadline conditions.
Disabling freezes the counter and deasserts both outputs.

The model preserves control bits one and two without an additional physical-clock or debug-halt effect.
Its virtual counter saturates at the twenty-bit limit.
Both deadlines start at that limit while counting is disabled.
These reset and saturation rules describe virtual policy, not captured silicon state.

The optional bite output permits an explicit reset consumer.
An unconnected bite output logs `WATCHDOG_BITE` and stops simulation.
It does not acknowledge a reset that the platform cannot perform.
The private profile uses this terminal boundary.

The guest regression delivers bark and bite through separate CLIC inputs.
It verifies petting, frozen counts while disabled, continued counting after enable, and the earlier of two programmed deadlines.
The real OEM run programs its watchdog and reaches watchdog-cause publication at `0x429404`.


## Explicit runtime control substitute

The legacy attribution of slot `0x3f8` to opcode `0x2013` was not proved.
Two original wrappers accept the observed two scalar arguments and completion context.
The private experiment now maps this slot to candidate `0x40c`, which submits opcode `0x2009`.
Original `0x21bb1e` and candidate `0x21be7c` establish that wrapper contract.
The slot attribution remains an inference.

Candidate `0x220680` submits hardware command `0x1d` with those scalar arguments.
OEM startup supplies selector four and mode one.
OEM `0x43734a` supplies selector four and mode two after CX/MX voltage-table preparation.
This association does not establish the command's complete physical effects.

The explicit `control_state_substitute` option enables a virtual control sequence.
Command `0x1d` retains selector four's mode.
Mode one permits command `0x21` with three zero arguments.
That command records the boot transition and permits mode two.
Other selectors, modes, arguments, and premature mode-two requests remain unsupported.
Both commands use the existing completion and interrupt machinery.

Candidate `0x2202da` submits command `0x21` for the OEM's boot-transition request.
Its completion handler calls the real OEM callback without an output payload.
The substitute does not model undocumented physical effects.
It does not authenticate an image, modify signature results, or create a mailbox response.
Every accepted substitute command identifies itself in the run log.

A temporary guest exercised mode one, the boot transition, and mode two.
It checked completion status and acknowledgement after each operation.
Separate runs rejected a disabled substitute and a premature mode-two request.
The real OEM completed the APSS boot request and advanced to descriptor and fuse initialization.
These results do not prove kernel execution or physical command equivalence.

### Transport-key broadcast command `0x1c`

The source-only dictionary name for event `0x301e` remains unestablished.
The public API and the receiver sequence support the operation inference below.
[INFERENCE] Command `0x1c` performs that broadcast.

Three independent contracts support the inference:

1. The command carries no payload and no output buffer.
2. Public `tme_hwkm_master.h` declares `tme_hwkm_master_broadcast_transportkey` with no key argument.
3. GPCE `15703a28` opens a receive window and `15703a60` polls for its result.

Public `tme_hwkm_master_defs.h` names the key that the broadcast distributes:

| Field | Value |
| --- | --- |
| Key ID `11` | `TME_KID_TP`, a retained 528-bit entry |
| Key profile | `TME_KP_KWK_TRANSPORT`, a transport key-encryption key |
| Key destination | `TME_KD_GPCE_V3`, which includes this GPCE |

The master already holds the key.
The command distributes it and does not derive a new key per call.
[INFERENCE] The retained entry persists across command completions.
The TME key table retains it in the same way as the managed-key slots.

The original submit bodies establish the command fields.
Body `0x21ef26..0x21ef9a` writes command `0x1c`, zero at `+04`, zero at `0x0c`, and the service context at `+2c`.
Body `0x2115c2..0x21163c` adds zero at `+08`.
The register-write index `0xf` selects offset `+2c`, which the accessor table at `0x2326f0` maps.
The source-only field name for `+2c` remains unestablished, so the model labels the value `REQUESTING_EE` as a descriptive name.
The three bodies place the service context, not a key identifier, at that offset.
Neither body supplies an input payload or an output buffer, and the paired completion copies no blob.

GPCE `15703a28` reads `01df101c`, clears bit8, writes the word, sets bit8, then replaces the low byte with `0x3e`.
Wait `15703a60` polls `01df1020` bit8 up to `0x270f` times and then clears bit8 in the control word.
Public `ice.c` names the same field `QCOM_ICE_REG_HWKM_TZ_TPKEY_RECEIVE_CTL`.
It uses bit8 as `QCOM_ICE_HWKM_TPKEY_EN_BIT` and enables that window before it calls the TME broadcast API.
Both vendors therefore describe one handshake: the slave opens a receive window, and the broadcast fills it.

The model implements this handshake.
It derives a virtual transport key from a public seed and retains the result.
The broadcast then passes the key to the attached HWKM slave through a sideband.
The GPCE accepts the key only through that sideband and sets `01df1020` bit8.
Without command `0x1c` the status bit stays clear.
The GPCE defers a broadcast that arrives before the receive window opens.
The later window write accepts it.
That deferred acceptance is an explicit virtual rule.
The selected code enables the window before it polls, and the public driver retries a failed broadcast.

The real device seed and the real KDF inputs are not recovered.
The virtual seed is a public identity, not a device secret.
The model exposes no key read path and no blob export path.
The export format and its consumer checks are in the next section.

SM8850 run413 stopped on this command because `execute()` had no case for it.
The write faulted at TME PC `0x21cd7e`.
The native `tme-warmup` fixture now checks the command, the pending handshake, the missing-slave path, and the disabled-substitute path.

### Wrapped-key export command `0x0b`

SM8850 run416 reaches this command through legacy import `0x634`.
Donor submit body `0x21ed64` logs event `0x301a` before it writes the registers.
The dictionary name and field labels for event `0x301a` remain unestablished.
The selected register writes and the public key interface support the field interpretation below.
The body loads those fields from `D+8`, `D+4`, and `D+0`.
It writes the same three values through register indices `a`, `19`, and `f`.

| Register index | Sequencer offset | Recovered field |
| --- | --- | --- |
| `1` | `0x00` | Command `0xb` |
| `7` | `0x04` | Zero |
| `9` | `0x0c` | Zero |
| `a` | `0x10` | Source key slot |
| `19` | `0x1c` | Wrapping-key slot |
| `f` | `0x2c` | Requesting execution environment |

The wire request is `[3, 0xb, 0x15, 0]`.
The normalizer maps API `0x15` to hardware slot `0x1f` and API `0xb` to hardware slot `0x15`.
Thus this request exports temporary source slot `0x1f` with wrapping-key slot `0x15`.

Public `tme_hwkm_master_defs.h` identifies slot `0x15` semantics independently.
`TME_KID_TP` is key ID 11 with the `TME_KP_KWK_TRANSPORT` profile.
The transport key is therefore the only recovered wrapping-key slot for this command.
The model accepts `0x15` and rejects every other value.

Donor completion `0x21ee14` copies output words from index three.
Words zero through two are operation status words.
The copy length is `0x5c` bytes when `D+0x0c` equals two, and `0x64` bytes otherwise.
The model writes three zero status words, then a 92-byte wrapped core.
It zeroes the following eight bytes so the longer copy path stays deterministic.

The silicon blob cipher, field order, and tag position are not recovered.
The model uses a declared virtual authenticated format:
magic, length, nonce, AES-256-CTR ciphertext, and a truncated HMAC-SHA-384 tag.
Both keys derive from the retained transport key with HMAC-SHA-384 domain separation.
The producer reads the source key only after an ownership check.
The wrap fails with an operation error in four cases.
The transport key is absent, the source slot is empty, the source owner differs, or the wrapping slot is not `0x15`.
The model never returns plaintext and never reports a wrap without an authenticated blob.

SM8850 run416 derived slots `0x1e` and `0x1c` before this request.
It did not derive or import slot `0x1f`.
The model therefore rejects this export with an operation error instead of inventing source material.
The next prerequisite is the real producer of temporary slot `0x1f`.

The native `tme-wrap` fixture covers success, missing transport key, foreign requester, a wrong wrapping slot, install, and tamper rejection.

### Producer of temporary slot `0x1f`

SM8850 run417 logs these QSEE key-service requests before the export:

| Trace | Arguments |
| --- | --- |
| `0x360000a` | `0` |
| `0x360000b` | `15 80000 8 0 3` |
| `0x360000c` | `c8253c20 44108` |
| `0x360000d` | `66 58000 2b 28` |
| `0x3600015` | `1` |
| `0x3600007` | `2 ffffffff 0 0` |
| `0x2700011` | `1 2 ffffffff 0 0 400000001` |
| `0x3600010` | `b 15 0` |

The second KDF request names key id `0x15`, algorithm `0x80000`, and source slot `8`.
The map sends API key `0x15` to hardware slot `0x1f`.
The export then asks for source slot `0x1f`.
[INFERENCE] That KDF request creates slot `0x1f`.
The first group uses API key `0x12` and source slot `9`.

The model rejected the source-8 request because `derive_key()` accepted only source `9`.
The virtual profile now accepts both fused roots `8` and `9`.
It also binds the source slot into the KDF, so the two roots produce different keys.
The next model build logs every rejected derive shape with its fields.

The run then retried once and entered the fatal path:
`0x3600008 15`, `0x2700011 5 2 ffffffff 0 0 1500000001`, `0x2700008`, `0x700125`, `0x70039b ADSP 0`, `0x70039c 4fa200000`, `0x70022d 54`.
Records `0x70039b` and `0x70039c` register the ADSP minidump region.
`Debug reset main enter` is the minidump entry point.
The minidump then reports `registered SS region mapping failed` and `Cold init not done for SS`, and it loops at `-DEAD END-`.
[INFERENCE] The fatal is the failed key export, not the minidump.
The watchdog bite at 192000 ticks follows that reset path.

## Corrected runtime fuse view

OEM `0x436692` and `0x4366f2` check fuse-status masks before reading a corrected row.
Their four status words occupy `0x02302098..0x023020a7`.
The private profile provides zero read-only status words and does not inject fuse errors.
The complete hardware meaning of these status banks remains unproved.

OEM `0x446e88` describes field `0x9e` at raw address `0x021c023c`, with mask `0xff00` and shift eight.
The runtime normalizes the row address and reads the corrected view at `0x021c2238`.
OEM `0x43c014` counts the assembled field's set bits through legacy API `0x3c`.
Original `0x218ea4` uses the same call contract.
Original `0x21933e` and candidate `0x2192fa` implement the two-word population count.
The private map therefore translates legacy slot `0x3c` to candidate slot `0x40`.

The private profile sets the corrected rollback rows to zero, consistent with its existing minimum versions.
It rejects a conflicting nonzero value in any existing rollback parameter.
These read-only rows occupy `0x021c2220..0x021c2297`.
OEM records `0x447308` and `0x44732c` establish the final additional 64-bit field.
The firmware still performs version and signature checks.

The first complete descriptor pass exposed another compatibility difference.
Candidate normalizer `0x21960a` rejects addresses at or below `0x021c0237`.
Original normalizer `0x219616` rejects addresses at or below `0x021c0227`.
OEM record `0x446c48` requests field `0x35` at `0x021c0228`.
The private reconstruction copies the original bound instruction into candidate `0x21961a`.
The comparison and fatal path remain intact.
The former read of `0x00810000` came from that fatal path's diagnostic collector, not normal initialization.

## Runtime wake configuration

OEM `0x4299b2` accesses interrupt configuration words at base `0x0f2d0110`, with a `0x10000` bank stride.
It preserves bit three and replaces the low three type bits.
The [upstream PDC driver](https://github.com/torvalds/linux/blob/master/drivers/irqchip/qcom-pdc.c) defines this layout for PDC version 3.2.
The private substitute retains two configuration banks without external wake inputs.
It does not establish interrupt propagation or physical power transitions.

## Runtime IPCC initialization

OEM `0x4454b2` identifies the consumer as `IPCC_Subsys.c`.
Its instance at `0x447b0c` selects base `0x05100000` and a client map at `0x447a7c`.
OEM `0x4409ee` reads the revision at `0x05110000`.
The private profile selects virtual revision 3.0 and retains the observed configuration word at `0x051cf008`.
The real OEM passes these initialization accesses.
Other IPCC registers remain unsupported.

The [upstream IPCC driver](https://github.com/torvalds/linux/blob/master/drivers/mailbox/qcom-ipcc.c) defines configuration, send, receive, enable, disable, and clear registers.
The present initialization substitute does not implement message delivery or claim those operations.

## Runtime machine timer

Candidate `0x2281ee` selects comparator `0x01004000` and counter `0x0100bff8`.
It reads the counter with a high-low-high sequence and programs a deadline 600000 ticks later.
Candidate `0x2282b0` enables the machine timer and external interrupt bits.
The private profile uses QBox's existing `riscv_aclint_mtimer` module with timer interrupt seven.
Its virtual 600 MHz source makes that interval one millisecond.
This frequency is a virtual scheduling policy, not a captured hardware value.

The ACLINT module must follow CPU construction.
The pinned QEMU realization code skips timer allocation when the hart does not yet exist.
The private profile sets `construction_priority=1` for the timer and retains the CPU's default priority.
The real firmware then programs its comparator and enters the OEM sequencer task.
The previous direct QTIMER interrupt binding no longer drives the candidate scheduler's machine interrupt.

OEM `0x41eeca` installs notification callback `0x41f010` through legacy slot `0x7d4`.
Original `0x21c074` and `0x21c07e` store that callback at `0x417440`.
The private map selects candidate slot `0x810`, whose function `0x21c124` stores the corresponding callback.
The candidate notification wrappers invoke that retained pointer.

## Runtime resource geometry

Candidate `0x21e216` reads selector 29 from `0x02237014`.
The word contains four five-bit counts and a starting index at bits 28:24.
The function derives four consecutive ranges from these fields.
The private profile starts at `0x1c` and selects counts 21, 5, 5, and 5.
These ranges include the selected OEM destinations and remain separate from the fixed groups.
This is explicit virtual geometry, not a captured hardware value.

Original `0x21e270` and candidate `0x21e48e` publish the four range sizes at `0x00c00300..0x00c0030c`.
The private map translates legacy slot `0x4ec` to candidate slot `0x504`.
The publication registers retain the firmware's writes.
The real OEM passes this initialization and continues into its queue operations.
This geometry does not implement key storage or approve unsupported sequencer operations.

## Runtime queue status

OEM wrapper `0x429eac` forwards a queue handle through legacy slot `0xaf4`.
The private map selects candidate slot `0xb40`.
Its function `0x2265d8` reads the queue message count at offset `0x38` inside a critical section.
The neighboring verified queue exports retain the same displacement.

OEM `0x427ef2` publishes that count with tag `0xbeef0000` at `0x00c04fe4`.
OEM `0x427f48` publishes it with tag `0xfade0000` after another queue operation.
The private profile retains this diagnostic word without changing the queue.


## Runtime handoff acknowledgement

The reconstructed runtime completed boot request twelve and published a bounded TMFW response ring.
The earlier bridge incorrectly changed this successful publication into failure state one.

A debugger capture at APSS `0x303234` observed a successful transport result and response value zero.
APSS then set handoff bit two through `0x30de60`.
Function `0x30de50` waited for state four at `0x2223602c`.
The earlier bridge could not produce this required state.

The bridge now observes the existing handoff word through a read-only initiator.
Runtime readiness requires successful sequencer authentication, a bounded TMFW ring, and both handoff bits.
TME `0x2005e8` sets bit one.
APSS `0x30de68` sets bit two.
An authenticated channel remains in state three until those conditions hold.
An authentication failure remains state one even if firmware publishes a runtime ring.

This relation is explicit virtual wiring, not a recovered physical state machine.
The bridge does not write mailbox records, acknowledge requests, or change guest execution.
The real firmware run passed the previous wait and dispatched loaded APSS firmware at `0x14827fa8`.
It next stopped on an unmodeled timer read at `0x0c221000`, from PC `0x14827fbc`.
This result proves progress beyond the APSS ROM, not kernel execution.

## Pending-response publication order

A separate compatibility race caused APSS error `0x28300`.
At `0x303234`, a debugger observed transport result one and response value `0xffffffff`.
APSS had accepted a completion before the candidate runtime finished its pending-record initialization.

The original ROM writes the command, pending state, and invalid result before the new identifier.
Instructions `0x20c628..0x20c634` establish that order.
The candidate instead published the identifier first at `0x22d6d6`.
The reused slot still contained completion state two from request ten.
APSS could observe the new identifier with that stale state.

A temporary transaction trace observed APSS release request twelve before the TME completed it.
The reconstruction now reorders the four existing stores to match the original publication contract.
It preserves every stored value and does not fabricate completion.
The corrected trace placed APSS release after the real completion at `0x22d4ae`.
The firmware then entered XBL and passed its sleep-counter read.
The temporary transaction logging was removed.

## Watchdog IRQ acknowledgement

The watchdog previously delayed IRQ updates until a later SystemC delta.
After a pet write, the CPU could return from its handler while the old level remained asserted.
The next interrupt could therefore observe a counter below its deadline.
The guest regression exposed an intermittent failure during integration.

The model now publishes the new IRQ level synchronously with the register write.
An annotated transaction delay still schedules a later deadline update.
This matches the existing timer-frame acknowledgement rule.
The real firmware run reached XBL after this change.
The complete integration suite passed all 50 tests with one test thread.

### APSS watchdog reuse

The APSS KPSS watchdog uses the same counter, deadlines, enable control, and pet operation.
Its status word differs from the TME word.
The KPSS version packs the count in bits 20:1 and reports expiration in bit zero.
`ApssWatchdog` reuses the existing timer engine and supplies that status encoding.
The two model versions remain separate.

The Linux [Qualcomm watchdog driver](https://github.com/torvalds/linux/blob/master/drivers/watchdog/qcom-wdt.c)
defines reset, enable, status, bark, and bite at offsets `4`, `8`, `0xc`, `0x10`, and `0x14`.
Both board profiles map the relative socket at `0x17908004`.
SM8850 `0x1482f024` and SM8845 `0x1482d134` pet this watchdog.
Their timeout conversions at `0x1482f038` and `0x1482d148` use 32768 ticks per second.

The initial XBL evidence did not establish a GIC route.
The QSEE inventory below establishes additional watchdog instances and interrupt routes.
An unconnected bite still logs `WATCHDOG_BITE` and stops simulation through the existing terminal policy.
The model does not suppress deadlines or fabricate reset completion.
The guest controls enable and pet operations.

The watchdog regression exercises both register versions through real RV32 code.
It checks pet, disable, count progress, bark, and bite.
The KPSS case also checks packed count and expiration clearance after pet.
SM8850 run65 passes the original forced-download check and reaches `Pre_DDR_clock_init`.

QHEE maps the whole `0x17600000` page at image offset `0x46db4`.
It writes `0xf` to page offset `0x24` at `0x46df0`.
It writes `0xfffa` to bark and bite at `0x46df8` and `0x46dfc`.
It writes bits zero and one of the enable word at `0x46e30`.
The page socket now covers `0x17600004` through `0x17600027`.
The source-only names, mask, and access type for page offset `0x24` remain unestablished.
The selected QHEE instructions establish one write of `0xf`, not a hardware write-only contract.
The model's four-bit restriction and read rejection remain publication blockers.
The override polarity, electrical effects, and reset value remain unknown.

A Ghidra decode of all eight known consumers of the QHEE watchdog pointer at image address `0x24b290` found no later access to offset `0x24`.
The initialization writes `0xf` once, then programs deadlines, pets the counter, and updates enable.
Later consumers use status, pet, bark, and bite registers.
The model retains only the four configuration bits.
It rejects reserved-bit writes and reads of this register, and it preserves the security check.
It adds no pet, enable, deadline, bark, bite, or reset effect.
This is an explicit substitute for the undocumented electrical override behavior, not a completion response.
The TME register map does not gain this APSS-only register.
The regression writes `0xf` before checking normal counter, pet, bark, and bite behavior.
It also checks read rejection, reserved-bit rejection, and denial without nonsecure permission.
See `models/qcom/apss/evidence.md` for the run452 boot boundary.

Run `run-sm8850-common-runtime453-verified` tested the bounded chicken-register implementation.
The original run453 directory already existed, so this run used a new output directory.
The OS bank accepted value 15, programmed both deadlines to 65530 ticks, petted at 10.489 seconds, and enabled with control value three.
The run made no unmodeled access.
The separate secure bank `platform.apss_watchdog` then reached its 384000-tick bite deadline.
Its last pet was at 12.4906 seconds.
The run exited with code two without kernel entry.
This result does not establish the secure bank's reset-consumer policy.
The platform suite passed 78 tests, including the extended watchdog cases.
The four changed Lua files passed `luac -p`.

Runs 455 through 460 identify the secure bank writer and its layout.
The writes at `0x17908008` carry the secure attribute, so QSEE or the secure monitor programs this bank.
At approximately 8.0 and 10.8 seconds it writes control value two, then the deadline pair, then control value three, then pets.
The first pair is `192000` at page `0x10` and `704000` at page `0x14`.
The second pair is `384000` at page `0x10` and `192000` at page `0x14`.
The guest repeats the second pair at 13.0 seconds and pets at 12.9959 seconds.
No later pet appears.

The Qualcomm Linux debug guide states that the secure watchdog barks every six seconds to Qualcomm TEE as a fast interrupt request.
The same guide states that the QHEE handler pets the watchdog.
A six-second bark is `192000` ticks at 32768 Hz.
The guest writes `192000` to page `0x14`, so the secure bank maps bark to `0x14` and bite to `0x10`.
The OS bank keeps the Linux order, with bark at `0x10` and bite at `0x14`.
`ApssWatchdog` selects the reversed order through the `secure_bank` parameter, and the SM8850 and SM8845 profiles set it for `platform.apss_watchdog`.

The model has no QHEE-to-TZ fast-interrupt pet path on the `0x17908000` bank.
The bark output reaches SPI 26, and no pet follows the bark.
An unconnected bite on this bank still stops the simulation.
A run that ignores the bite does not establish normal watchdog operation, so the profiles do not ignore it.
The next contract is the firmware path that pets this bank after the bark.
See `docs/qcom/sm88xx/stubs.md` and `models/qcom/apss/evidence.md`.

### Deadline order correction

The `secure_bank` swap was wrong.
Socket offset `0xc` is therefore the bark and offset `0x10` is the bite for both banks.
Run 466 records one secure-bank program that wrote `192000` at offset `0xc` and `704000` at offset `0x10`.
Those values are a `192000`-tick bark and a `704000`-tick bite, which matches the six-second bark of the debug guide.
The swap made the model fire that bark as a bite, so the platform stopped 5.86 seconds after the pet and no bark ever reached QTEE.
Both SM8850 and SM8845 profiles no longer set `secure_bank`.

Run 467 measured the result.
The secure bank is petted at 14.1061, 19.9657, 25.8253, and 26.271 seconds, so the bark now reaches SPI 26, QTEE runs its `SWDogBark` handler, and the handler writes `0x17908004`.
The run lasts past 26 seconds instead of stopping at 15.4 seconds.
A later program writes `384000` at offset `0xc` and `192000` at offset `0x10`, which is a bark later than the bite, and the model then stops at the `192000`-tick deadline.
That inverted pair is the next contract.


### QSEE watchdog mode and interrupt inventory

The selected SM8850 QSEE table at `0x157f8de0` contains one 20-byte record.
Its writer is `0x157bc808`.
SM8845 uses the same record at `0x157ec5d8` and writer `0x157b30a4`.
The record selects address `0x17600000`, value one, condition byte zero, and enable byte one.

The pinned [Canoe device tree](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850/blob/5ab2a689ff87d7d28c511f1762cf41c1b90d965a/kernel_platform/qcom/opensource/devicetree/qcom/canoe.dtsi)
identifies `qcom,wdt@17600000` with compatible `qcom,msm-watchdog`.
Its interrupts are SPIs zero and one.
The source-only name `WDOG_SECURE` and its polarity remain unestablished.
The selected QSEE table establishes a write of one, not its hardware security meaning.
The mode implementation remains a publication blocker.

The platform reuses `ApssWatchdog` for this bank and the existing bank at `0x17908000`.
Each bank has independent counter, control, deadline, and mode state.
Both use the declared 32768 Hz virtual clock.
Mode accesses do not pet, enable, or reset the counter.

The virtual access policy permits secure timer accesses in either mode.
Nonsecure timer access requires mode one.
Only secure transactions can change the mode.
The mode word masks writes to bit zero.
These access rules are explicit conservative virtual policy, not a measured hardware claim.

Cold reset selects secure mode unless the profile explicitly sets `nonsecure_reset`.
The existing RV32 counter fixture selects nonsecure reset for its KPSS instance.
The new AArch64 fixture uses actual secure and nonsecure execution.
It verifies guest-controlled admission, denied mode writes, denied timer access, and revocation without loss of deadline state.

QSEE `0x1567d9bc` registers INTID 33 as `NSWDogBite` and INTID 782 as `RPMWDogBite`.
QSEE `0x1567de20` supplies INTID 58, and `0x1567de2c` registers `SWDogBark`.
The platform connects the `0x17600000` bark and bite outputs to SPIs zero and one.
It connects the `0x17908000` bark output to SPI 26.
The latter bank retains the terminal policy for an unconnected bite.
The model does not invent an RPM watchdog event.

The integration run passes 100 tests and Clippy with warnings denied.
SM8850 run173 passes the QSEE mode write and next reaches hardware lock nine at `0x01f49000`.
This proves progress through the selected mode write, not completion of every watchdog initialization path.
Kernel entry remains unproved.

### Secure watchdog bark service path

Run 460 and run 461 recorded the interrupt path that serves the `0x17908000` bank.
A hardware debugger stopped at the EL3 fast-interrupt entry `0xd820cf58` and read the live GIC.
The distributor and redistributor values follow:

| Register | Value | Meaning |
|---|---|---|
| `GICD_IGROUPR1` | `0xfbbee7fd` | INTID 58 is Group 0 |
| `GICD_ISENABLER1` | `0x04411002` | QSEE enabled INTIDs 33, 44, 48, 54, and 58 |
| `GICD_ISPENDR1` | `0x00000003` | INTIDs 32 and 33 are pending |
| `GICR_ISENABLER0` | `0x0` | CPU zero has no enabled PPI or SGI |
| `GICD_IROUTER58` | `0x0` | INTID 58 targets affinity zero |

Conjunction of `ISENABLER1` with `IGROUPR1` is zero.
No enabled SPI belongs to Group 1.
The bark on SPI 26 must therefore arrive as a fast interrupt request.
INTID 58 was not pending when the first request arrived.
INTIDs 32 and 33 belong to the `0x17600000` bank.
INTID 32 is not enabled, so the delivered request was INTID 33, the nonsecure watchdog bite.

QTEE registers INTID 33 with handler `0x1567d8a8` at `0x1567d9fc`.
QTEE registers INTID 58 with handler `0x1567deac` at `0x1567de2c`.
Both handlers call `0x1567e818`, which stores one to `0x17908004`.
The secure bark therefore has a pet path in QTEE firmware.
The model delivers the request, and QTEE aborts before a bark handler runs.

Run 460 shows one fast interrupt request from EL2 to EL3 at `0xd820cf58`.
The handler stores `0x00030e00` to `SCR_EL3` and enters the secure world.
QTEE then takes an IRQ at vector `0x530095280` and a data abort at `0x1574ecdc`.
The abort is `ldr w9, [x8, #4]` with a pointer value of `-1`.
The secure world stops after this abort.
No further fast interrupt request appears, and the next server of `0x17908000` never runs.

Run 461 moved the `0x17600000` bite output to an unused SPI input for one experiment.
The secure bank then received no pet near eleven seconds and bit at 192000 ticks.
The unconnected bite stopped the simulation at that deadline, as the terminal policy requires.
The pet and the deadline rewrite at 10.9379 and 10.9382 seconds follow the `0x17600000` bite.
Both events disappear when the bite has no GIC input, so the response is not independent of that request.
The platform wiring for both banks is consistent with the installed QTEE handler table.
No model fact changed for this boundary.

The aborting function is `FUN_1574ec7c`.
It calls `FUN_1574fba0`, which returns the fixed address `0x14680000`.
The function reads a pointer from field `0x10` of that record.
The record held `-1` at the abort, so the read of `[x8, #4]` faulted.
The retained snapshot `qemu-qsee-imem.bin` from run 460 holds these words:

| Offset | Value |
|---|---|
| `0x00` | `0x00000004c1f8db40` |
| `0x08` | `-1` |
| `0x10` | `-1` |
| `0x18` | `-1` |
| `0x20` | `-1` |
| `0x28` | `0x00000000ffffffff` |

The platform maps this range as the writable `apss_imem` region with cold-zero start.
The `-1` words therefore come from firmware, not from an uninitialized model region.
The abort instruction is at image address `0x1574ecdc` and physical address `0xd832ecdc`.
The abort is a consequence, not the first failure.

### QHEE APSS identity check stops the boot

QHEE calls `FUN_00148100` at image offset `0x48100`.
Run 460 enters that function at trace line 275935 and reaches the fatal function `0xd50d0` at line 278286.
The function reads the activity-monitor identification first:

```text
0x48314  mrs  x8, AMCFGR_EL0
0x48318  and  x8, x8, #0xff
0x4832c  cmp  x8, #0xb          ; mismatch branches to panic 0x489f0
0x48334  mrs  x8, AMCGCR_EL0
0x4833c  cmp  x8, #0x804        ; mismatch branches to panic 0x489fc
0x48354  mrs  x8, ID_AA64MMFR2_EL1
0x48358  lsr  x8, x8, #60       ; E0PD must be nonzero, else panic 0x48a08
0x48364  tst  w0, #0x200        ; with ID_AA64DFR0 bits 7:4 nonzero
0x48390  mrs  x10, TRCIDR2      ; bits 9:5 must be 4, bits 14:10 must be 4
0x48398  mrs  x9,  TRCIDR4      ; bits 3:0 two, 19:16 four, 23:20 one, 27:24 one, 31:28 one
0x4839c  mrs  x8,  TRCIDR5      ; bits 27:25 four, 11:9 four, 30:28 two
```

QEMU has no activity-monitor registers and no ETE identification registers.
The trace records the failing access before the exception:

```text
read access to unsupported AArch64 system register op0:3 op1:3 crn:13 crm:2 op2:1
Taking exception 1 [Undefined Instruction] on CPU 0
...from EL2 to EL2
...with ELR 0xffffffd163648314
```

`0xffffffd163648314` is the `mrs x8, AMCFGR_EL0` instruction.
The exception handler leads to the panic block at `0xd50d0` and the halt loop at `0xd5100`.
QHEE therefore never enables INTID 32, never enables the EL2 timer PPIs, and never pets `0x17600000`.
The 2-second bark and bite then run unchecked, and the `0x17908000` bark finds a stopped secure world.

The model must supply these values to pass the check:

| Item | Required value |
|---|---|
| `AMCFGR_EL0` low byte | `0xb` |
| `AMCGCR_EL0` low halfword | `0x804` |
| `ID_AA64MMFR2_EL1` bits 63:60 | nonzero |
| `ID_AA64DFR0_EL1` bits 7:4 | nonzero |
| `TRCIDR2` bits 9:5, 14:10 | four, four |
| `TRCIDR4` bits 3:0, 19:16, 23:20, 27:24, 31:28 | two, four, one, one, one |
| `TRCIDR5` bits 27:25, 11:9, 30:28 | four, four, two |

The QEMU TCG profile clears `ID_AA64PFR0.AMU`, `ID_AA64DFR0.TRACEVER`, and the trace fields at `target/arm/cpu.c`.
QEMU defines no `AMCFGR_EL0`, `AMCGCR_EL0`, or `TRCIDR2/4/5` register.
Patch `0018-implement-qcom-identification-registers.patch` adds `AMCFGR_EL0` and `AMCGCR_EL0` under the opt-in `x-qcom-ids` property, in the same way as the existing `x-qcom-mpam` profile.
The profile also reports `AMU`, `TRACEVER`, and `E0PD` present.
See `models/qcom/apss/evidence.md` for the property values and the substitute record.
Run 463 passes every identification comparison through `0x487ac` and writes `TRFCR_EL2`, which the profile retains.
It then stops on an unmodeled read of `0x31205020`, so the interrupt and timer bring-up still does not run.
INTID 32 and the EL2 timer PPIs stay disabled, and the watchdog chain above stays the measured behaviour.
The next contract is the GEM_NOC fault-monitor read at `0x31205020`.
After that region is modelled, QHEE reaches its interrupt and timer bring-up, which is the code that enables INTID 32 and the PPIs and pets `0x17600000`.



## XBL opcode-six request and completion wait

The SM8850 OEM package contains `IMAGES/xbl_sc.elf`.
Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
Direct ELF-segment disassembly matches the observed APSS wait addresses.
The following addresses refer to this image unless a paragraph identifies TME code.

### Request schema

Function `0x148a19ec` builds opcode six at APSS `0x2040c000`, which aliases TME `0x0040c000`.
The request length is `0xa4` bytes.
Its caller at `0x14877964` stages image-authentication metadata before submission.
The higher caller logs `Auth Metadata` at `0x1484220c..0x14842218` after successful return.
This is not a runtime-readiness query.

| Payload offset | Layout and established use |
| --- | --- |
| `0x00` | `u32` image identifier, from wrapper argument `w0` |
| `0x04` | `u32` first buffer address, from `w1` |
| `0x08` | `u32` first buffer length, from `w2` |
| `0x0c` | `u32` gap scalar, from `w3` |
| `0x10` | `u32` second buffer address, from `w4` |
| `0x14` | `u32` second buffer length, from `w5` |
| `0x18` | `u32` third buffer address, from `w6` |
| `0x1c` | `u32` third buffer length, from `w7` |
| `0x20..0x9f` | Up to sixteen pairs of `u32` values, initially zero |
| `0xa0` | `u32` pair count |

Instructions `0x148a1a28..0x148a1a3c` store the eight scalar arguments.
The wrapper clears `0x80` bytes at payload offset `0x20`.
Its ninth and tenth arguments supply an optional list address and its byte length.
It selects `min(length >> 4, 16)` records.
Each input record contains two 64-bit words.
Instructions `0x148a1a6c..0x148a1a9c` copy their low 32 bits into one eight-byte output pair.
The wrapper stores the count at `0x148a1ab8`.
These instructions do not establish semantic names for the pair values or individual authenticated buffers.

TME OEM table `0x444418` maps opcode six to handler `0x42b170`.
That handler requires at least `0xa4` bytes at `0x42b1a2`.
It reads the image identifier at `0x42b1d8`.
Calls at `0x42b240`, `0x42b252`, and `0x42b262` normalize the three buffer addresses through `0x42a21a`.
Instructions `0x42b282..0x42b294` require `address1 + length1 + gap == address2`.
Instructions `0x42b298..0x42b2a8` require `length1 + length2 <= 0x1000`.
The handler checks buffer permissions through legacy API `0x9e0`.
The first two checks use flags `0x104`.
The third uses flags `0x500`.
The subsequent validation compares results with magic `0x2ec88ff0`, not boolean true.

### Ring publication and waiter

APSS function `0x148cc208` publishes requests in the ring at `0x22002800`.
Its header contains signature, capacity, produced count, and released count at offsets zero, four, eight, and twelve.
It accepts signature `0x4c425041` or `0x43535841`.
Each request record has stride `0x14`.

| Record offset | Meaning |
| --- | --- |
| `0x00` | Sequence identifier |
| `0x04` | Opcode |
| `0x08` | Payload address |
| `0x0c` | Payload byte length |
| `0x10` | APSS release flag |

For produced count `p`, the function selects record `p % capacity` and sequence `p + 1`.
Instructions `0x148cc2ac..0x148cc2d0` store opcode, payload address, length, sequence, zero release flag, and then the new produced count.
The complete producer contains no doorbell write, IPCC operation, interrupt operation, or memory-barrier instruction.
Its caller `0x148a1558` immediately polls instead of waiting for an interrupt.

Response lookup `0x148cc120` selects `0x22000010 + ((sequence - 1) % response_capacity) * 0x14`.
Function `0x148cc174` returns both the response and request record addresses.
Its signature check requires matching `TMFW`/`AXSC` or `TPBL`/`APBL` ring identities.
The waiter `0x148cc2e0` first writes output state one.
Instructions `0x148cc344..0x148cc358` require both record identifiers to match the requested sequence.
Instructions `0x148cc35c..0x148cc370` report state two only when response record offset eight equals two.
The outer loop at `0x148a164c..0x148a167c` ignores transport errors and repeats until that output state equals two.

Function `0x148cc380` then reads the service result from response record offset `0x0c`.
A completed transport does not imply successful authentication.
Function `0x148cc430` releases the request.
Instructions `0x148cc4d8..0x148cc4ec` set request record offset `0x10` to one and increment the request header's released count.
Therefore a zero released count does not establish that TME failed to receive the request.

The non-halting snapshot showed sequence one, opcode six, payload `0x2040c000`, and length `0xa4`.
Its response record already contained sequence one, opcode six, pending state one, and result `0xffffffff`.
This is an allocated pending response, not proof of an authentication failure.

An additional RAM capture supplied these runtime payload values:

| Field | Observed value |
| --- | --- |
| Image identifier | `0x95` |
| First address and length | `0x149fff18`, `0x40` |
| Gap scalar | `0` |
| Second address and length | `0x149fff58`, `0xa8` |
| Third address and length | `0x1480e000`, `0x3000` |
| First pair | `0x14946000`, `0x1494e000` |
| Remaining pairs | Zero |
| Pair count | One |

The observed first address plus its length equals the second address.
The first two lengths sum to `0xe8`, below the handler's `0x1000` limit.
These checks do not prove the subsequent authentication result.

TME must produce the completion.
The OEM handler calls legacy completion API `0x9f8` at `0x42ba4a`.
Candidate instructions `0x22d49c..0x22d4ae` publish the service result and state two.
Neither APSS polling nor a native readiness model may replace that publication.

### Native visibility and notification boundary

`platforms/qcom/sm88xx/reset.lua` creates one shared `tme_mailbox` memory object with DMI disabled.
Its TME mapping starts at `0x02000000`.
The APSS alias at `0x22000000` reaches that same backing through the external protection router.
`TmeBootStatus` only reads the response header and handoff state.
It neither handles opcode six nor writes completion records.
The sequencer's interrupt belongs to the separate command engine at TME `0x02230000`.
It is not an APSS request-ring doorbell.

The runtime IPCC substitute retains initialization registers only.
The decoded APSS request path does not access those registers or require IPCC delivery.
No missing APSS doorbell or split mailbox backing is established by this investigation.
No native mailbox correction follows from the pending snapshot alone.
Further diagnosis must trace the real TME handler and its completion callback.
This investigation did not run a build, test, formatter, or live probe.

## Signed XBL_AC_CFG crypto contract

This investigation uses the SM8850 package
`CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES`.
The exact inputs are:

| Input | Bytes | SHA-256 |
| --- | --- | --- |
| `xbl_ac_config.mbn` | 44388 | `afa0b63443a8d6a0588f4e8992a0efc2780b72f2cd1c4052e4357045bc515e39` |
| `xbl_config.elf` | 385804 | `c35a368853ba57f00c9486d4b8fadeb0ded3a59ce410818a69ac415e3c44d3ef` |
| `xbl_sc.elf` | 1174496 | `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a` |

Despite its suffix, `xbl_ac_config.mbn` is an ELF64 image.
Its ELF header and three program headers occupy file range `[0,0xe8)`.
This length matches the request's adjacent `0x40` and `0xa8` header ranges.
Its load segment occupies file range `[0x1000,0x8000)` and virtual range `[0x14946000,0x1494d000)`.
The request pair ends at `0x1494e000`, not at the segment's exact end.
Do not treat that pair as proof of an additional signed segment.

### Envelope and algorithms

The authentication segment begins at file offset `0x8000`.
Its first ten little-endian words are
`{0,7,0x18,0xe0,0xe0,0x90,0x68,0xd20,0x68,0xd20}`.
The image identifier at `0x8030` is `0x95`.
The corresponding identifiers are `0x25` for `xbl_config.elf` and `0x36` for `xbl_sc.elf`.
ROM `0x205cda..0x205d62` checks version seven, size arithmetic, common size `0x18`, and metadata lengths zero or `0xe0`.
These checks execute in `irq-enabled-sm8850-tme.log`.

The hash table occupies `[0x8200,0x8290)` and contains three 48-byte entries:

1. SHA-384 of file range `[0,0xe8)`:
   `633e7c617a92aa91350b10cf0dd23896604696d6412a9a1e02af7a041f1a99318ec46cd8c6e1f19274fb631c4376517b`.
2. SHA-384 of file range `[0x1000,0x8000)`:
   `167d6d664be7b2e7c0880cf73593224a6f57ebc8f1bebd9b2f565084c8df8c653efa6e9be41760870d1d7def38589b41`.
3. An all-zero authentication-segment entry.

Independent file hashing establishes the first two matches.
ROM `0x205e22..0x205e4a` loads the selector at common-metadata offset `0x10`.
For this image, that word is three at file offset `0x8038`.
ROM `0x206782..0x2067aa` maps selector three to digest length `0x30`.
This envelope selector is not the sequencer's command argument two.

The QTI signature slot occupies `[0x8290,0x82f8)`.
Its DER signature uses 103 bytes inside the 104-byte slot.
The OEM signature slot occupies `[0x9018,0x9080)` and uses 104 DER bytes.
The QTI certificate chain starts at `0x82f8`, `0x856c`, and `0x881c`.
The OEM certificate chain starts at `0x9080`, `0x930f`, and `0x95ab`.
All six certificates use `secp384r1` keys and signature OID `1.2.840.10045.4.3.3`, ECDSA with SHA-384.
The QTI root is “SRoT MBNv7 Image Signing Root CA 6”.
The OEM root is “OPLUS ROOT CA 1”.
The `xbl_sc.elf` envelope contains the same intermediate and root certificates.
The `xbl_config.elf` envelope contains the same three OEM certificates and no QTI certificate chain.
Certificate identity and algorithm decoding do not establish successful firmware authentication.
ROM `0x20617c` builds the signed message for each signer, and ROM `0x206268` restores the envelope after the check.
The message is the authentication segment from offset zero to the end of the hash table.
For a dual-signed envelope, the OEM view sets header words six and seven to zero and fills the QTI metadata with zeros.
The QTI view sets header words eight and nine to zero and fills the OEM metadata with zeros.
A single-signer envelope uses the message without change.
Each signature is ECDSA P-384 with SHA-384 over its message.
An offline host check accepted both `xbl_ac_config.mbn` signatures with this rule.
The same check accepted every signature in the 84 v7 images of the selected package.
It rejected both TZ signatures after a one-bit change in the TZ hash table.
The OEM multi-image catalog digest is SHA-384 of the OEM-view message.
For the QTI view, ROM `0x2069dc` also hashes a trailing block with magic `0x51424543`.
The final images contain no such block, so the input of the QTI catalog digest remains unestablished.

### Model coverage and trust policy

The existing hash operation is command one with arguments `{2,1,phase}`.
Phases three, four, and five implement SHA-384 initialization, continuation, and finalization.
The existing signature operation is command eight with arguments `{2,1,0}`.
It requires operation control one, signature control one, curve selector four, options zero, and digest length 48.
Its input contains big-endian P-384 coordinates and signature integers.
OpenSSL supplies the SHA-384 and P-384 implementations.
Unsupported framing fails instead of publishing a successful result.
No absent crypto operation follows from these image algorithms.

The prepared SM8850 OEM root has SHA-384:
`31313ec6f0c237b69004d153b2ec2d3ddf1d826d0f60a952d039d5ded862ab0e515e4819d1844fdc91242304c685f202`.
This matches the complete DER certificate at `xbl_ac_config.mbn` offset `0x95ab`.
It also matches the OEM roots in both other inputs.
The CLI writes this digest to `platform.fuses.oem_pk_hash_sha384`.
The virtual profile retains lifecycle five, `qc_config1=0x88000000`, `secure_boot=0x130`,
`oem_hw_id=0x51`, and `oem_config5=0x0f`.
These are explicit virtual policies, not captured physical fuses.
This investigation changes none of them.

### Bounded next dependency and probes

The current trace follows `0x42b170 -> 0x439e06 -> 0x4398c6`.
The last wrapper loads its real ROM callback from table offset `0x20`.
The ROM parser executes `0x205cda..0x206206`, including the header and selector checks above.
It also executes metadata-field decoding through `0x20413e -> 0x202c40 -> 0x208500`.
Entropy command `0x13` already completes after the interrupt-priority correction.
Neither the pending mailbox response nor the later fault proves a signature rejection.

The current failure is an interrupt path:
`0x223454 -> 0x41eff4 -> 0x41fdd0 -> 0x420050 -> 0x41fdee`.
At `0x41fdf2`, firmware loads missing legacy import `0x70c` from virtual bridge address `0x416f0c`.
Resolve this callback dependency before adding crypto operations.
Do not debug-read that bridge address as RAM.

After the callback returns, trace these bounded authentication boundaries:

1. ROM parser return and metadata-policy results around `0x206202..0x206258`.
2. Hash submission `0x20ff00..0x210022`, including descriptors, phase, and cumulative length.
3. Signature submission `0x20f5cc..0x20f6a8`, including all selectors and the 244-byte input.
4. Completion check `0x20f216..0x20f264` and signature consumer `0x20f7d0`.

Correlate each digest with the exact file spans or certificate signed body before changing byte order or framing.
Capture the envelope's normalized signed message if signature verification fails after certificate validation.
Do not infer a new command from the envelope's selector three.
No concrete firmware/model incompatibility justified changes to the three crypto headers.
This investigation used offline file analysis and existing instruction traces only.
It did not build, run tests, launch native firmware, or attach to a live processor.

## Runtime SHA-384 finalization framing

The SM8845 trace `build/private/irq-enabled-sm8845-tme.log` reaches a rejected command one with arguments `{2,1,5}`.
The submit store faults at donor PC `0x21cd7e`, address `0x02230060`.
These arguments identify SHA-384 finalization. They do not identify the rejected input field.

Offline comparison used these private files:

| File | SHA-256 |
| --- | --- |
| `run-sm8845-common-runtime3/tme-runtime-donor.bin` | `94059e1c3ba90f7112fa600a7033d83aa71cee6ff5573219d72b63c69c7b67d7` |
| `run-sm8845-common-runtime3/tme-original.bin` | `e5485515dde2da7b12ef0c06f1e5af8e1a56b693af2c49d43b68abd78db3deb3` |

Both paths construct the same register and payload layout:

| Original ROM | Donor ROM | Contract |
| --- | --- | --- |
| `0x20ff3a..0x20ff56` | `0x21f9ba..0x21f9e6` | Register 13 selects input length, 14 selects cumulative length, and 15 selects operation control. |
| `0x20ff5c..0x20ff60` | `0x21f9ea..0x21f9f6` | Input word zero contains the source count. |
| `0x20ff72..0x20ffb2` | `0x21f9fc..0x21fa1e`, `0x21fb9c..0x21fbb6` | Continuation and finalization copy the saved context after the count word. |
| `0x20ffb8..0x210022` | `0x21fbbc..0x21fbd4`, `0x21fa26..0x21fa76` | Pending bytes and source descriptors follow the saved context. |
| `0x210180..0x2101b8` | `0x21f69e..0x21f6d8`, `0x21fc56..0x21fc62` | Type two, argument one returns 64 context bytes before finalization and 48 digest bytes after finalization. |

The donor accessor table at `0x2326f0` maps registers 13, 14, and 15 to offsets `0x58`, `0x5c`, and `0x2c`.
These offsets match the model.
The context serializer uses its descriptor's byte length. It does not insert an additional state header.
The model must still require all 64 context bytes for the supported SHA-384 continuation ABI.

Original helper `0x20efd6..0x20f078` and donor helper `0x21d004..0x21d080` serialize `{address,length,next}`.
Control one translates nonzero source and link addresses before serialization.
Other controls take the direct-address branch in these helpers.
That branch alone does not establish all supported hardware control values.
No control value can remove secure DMA ownership, descriptor bounds, or source-read failures.

### Missing runtime state producer

The bounded probe recorded these consecutive SM8845 requests:

| Phase | Control | Input bytes | Total bytes | Count | Bytes written after acquisition |
| --- | --- | --- | --- | --- | --- |
| Three | One | `0x280` | `0x280` | One | `0..15` |
| Five | One | `0x10` | `0x290` | One | `0..15` |

The final request omitted the 64-byte state. Its descriptor immediately followed the count word.
Earlier boot finalizations wrote all 80 bytes required for one source and a saved state.
The SM8850 probe found the same omission after a `0x200`-byte initial request.
Its final request supplied `0x79` bytes and cumulative length `0x279`.
These observations do not establish an engine-retained state mode.

The actual SM8845 submit path uses legacy import `0x6dc`, mapped to donor `0x6fc` at `0x21f282`.
Its descriptor supplies the context at offset `0x18`.
The completion resolver selects legacy import `0x6e4` at OEM PC `0x41fc64`.
The old mapping selected donor `0x6c4`, function `0x21e6ac`.
That function completes a different operation. It snapshots status and notifies the context without copying SHA output.

Donor table word `0x2324f4` selects resolver arm `0x21c650` for hardware opcode one.
That arm loads donor import `0x704`, function `0x21f2d4`.
This function reads the matching context at descriptor offset `0x18`.
It computes the output length from type, argument, and phase at offsets four, eight, and twelve.
It copies that many actual output bytes through `0x748` into the output object at context offset `0x1b0`.
Only then does it notify the context through `0x554`.
Original body `0x21f150..0x21f1a6` preserves these fields, the byte transfer, and the result-derived notification.

The map now selects `0x6e4 -> 0x704`.
The model still rejects a continuation that omits its state.
SHA-384, P-384, secure DMA, descriptor validation, and completion errors remain unchanged.
The temporary metadata probe was removed after both requests were captured.
Native verification and integration gates remain pending.



## Runtime P-384 digest DMA

Both OEM runs exposed a second P-384 input form after the curve-classifier mapping was added.
Donor submit function `0x21dca8` uses curve selector four, signature control one, options zero, and a 48-byte digest.
The original ROM uses the same curve and digest algorithm.
The two emitters use these distinct frames:

| Field | Original ROM | Donor runtime |
| --- | --- | --- |
| Command arguments | `{2,1,0}` | `{1,0,0}` |
| Operation control | One | Two |
| Input word zero | Zero | One |
| Written input bytes | 244 | 208 |
| Digest at input offset 196 | 48 inline bytes | One 12-byte DMA descriptor |

The public coordinates occupy input offsets `4..99`.
The signature integers occupy offsets `100..195`.
Donor `0x21dd50` copies the first object.
Helper `0x21e1d4` locates the second object after it.
Donor `0x21dd64..0x21dd86` then serializes the digest source.
Its descriptor contains address, byte count, and a zero next pointer.

The actual runtime source is local address `0x4077b0`, with length `0x30`.
The platform routes that view through the existing local protection port.
The DMA form requires one complete descriptor, exactly 48 bytes, no next pointer, and no 32-bit address overflow.
It uses the same secure DMA helper as SHA-384.
A failed read cannot publish a signature result.
Both forms call the unchanged OpenSSL P-384 verifier with the actual coordinates, signature, and digest.

The native regression exercises valid and altered requests through both forms.
It rejects invalid keys, invalid signatures, and changed digests.
It checks that a later valid request clears the error status.
The success terminal is separate from the unsupported-curve failure terminal.
This prevents an early command rejection from passing the positive cases.

Both OEM runs passed the new protected digest path.
SM8850 next stopped at PC `0x43a678`. SM8845 next stopped at PC `0x43a8f4`.
Both stops identify missing legacy import `0x68` at `0x416868`.
These results do not prove complete image authentication or kernel execution.

## Post-signature version policy

Legacy import `0x68` selects donor import `0x6c`, function `0x2191a0`.
Its original contract witness is `0x2191fc`.
SM8850 loads this import at `0x43a678`. SM8845 loads it at `0x43a8f4`.
Both callers retain the service result and diagnostic output.

The service accepts a context, selector, version byte, variant, and diagnostic pointer.
It obtains the selected record through donor import `0x9a8`.
A missing record returns one.
After the policy query through `0x58`, a version below record byte `+0x19` returns one.
An equal version returns zero.
The remaining branch queries the bound through `0x54` and propagates its error.

The update retains the smaller version, marks record byte `+0x20`, and calls the commit service through `0x44`.
The original body preserves the same comparison, minimum, record offsets, and returned commit status.
The mapping executes those instructions. It does not replace the version policy with a success constant.
Both OEM runs subsequently printed `Auth Metadata` and reached their image-hash service imports.

## One-shot SHA-384 command

Donor output-size helper `0x21fc56` tests mode one and a phase other than five.
Type-two dispatch through table `0x2326b4` selects this helper at `0x21f69e`.
The true result selects 64 bytes of chaining state. The false result selects a 48-byte digest.
The actual one-shot command uses arguments `{2, 0, 3}` and operation control one.

The sequencer now accepts mode zero only with phase three.
It hashes the complete DMA input and publishes the real 48-byte digest.
Mode one retains initial, continuation, and final phases three, four, and five.
Descriptor completeness, exact byte counts, address bounds, protected DMA, and saved-state checks remain active.

The native regression hashes two 647-byte messages through the one-shot path.
Independent SHA-384 values check both digests.
The second message changes byte 191.
The regression also rejects one-shot continuation and retains the split-state, missing-descriptor, unmapped-DMA, and unknown-phase cases.
The first real SM8850 run passed the command and reached the APSS crash-dump policy query.
This observation does not establish complete image authentication or kernel execution.

## Independent mailbox ring capacities

Donor initializer `0x22d7f6` clears `0x60` bytes and sets response capacity two.
PBL initially uses two request slots. XBL replaces that request ring with twenty slots at `0x02002800`.
Both protocols use a sixteen-byte header and twenty-byte records.

Donor pending selector `0x22d642` computes the request index from the response allocation count and request capacity.
It also returns that index for response allocation.
Allocator `0x22d6a0` uses the returned index without reducing it by response capacity.
Publisher `0x22d43e` later locates the response using its own capacity.
These operations disagree when the request ring has twenty slots and the response ring has two.

The SM8845 CPU trace establishes the resulting failure:

1. The first two requests allocate response records `0x02000010` and `0x02000024`.
2. The third request allocates record `0x02000038` with identifier three.
3. The publisher instead selects `0x02000010`, whose identifier remains one.
4. The publisher returns six at `0x22d4ce`.
5. OEM PC `0x42ba96` enters its fatal path. The later read at `0x00810000` belongs to its dump.

The authored adapter replaces donor pending selector `0x93c` only after OEM entry.
It computes the request and response indices independently.
It accepts the observed two-slot PBL protocol and twenty-slot XBL protocol, with response capacity two.
An empty ring returns no request and clears the request output.
Selection does not modify either header, allocate a response, publish a result, or bypass a service check.
The donor retains allocation, completion, identifier checks, ownership checks, and result publication.

The native regression includes the actual adapter source.
It exercises the initial protocol, the third XBL request, the final request slot, both rings' reuse, and the empty state.
It also checks that selection leaves both counters unchanged.

The initial canonical SM8845 run passed the allocation failure and reached legacy import `0x15c` at OEM PC `0x41f6b4`.
The corrected context-transfer mapping executes the original bounded-copy contract.
It does not return a synthetic authentication result.

A later request exposed a second capacity assumption.
Donor request-index helper `0x22d868` rejects an index of four or more, even when the request header declares twenty slots.
The OEM release query then enters its fatal path through `0x42bb0e`.
The adapter now replaces donor import `0x918` with a capacity-aware request-index helper.
It accepts the same two observed capacities and preserves rejection of sequence zero.
The response-index helper remains bounded by its two-slot ring.

The regression now includes request index four and the last declared request slot.
It checks request lookup, release lookup, independent response allocation, empty selection, and invalid sequence zero.
Both current canonical runs pass these mailbox failures and later stop at the feature-authorization import.
Both preparations reproduce the checked ROM, adapter, recipe, and map.
Neither run reaches the kernel.

## Sequencer memory clear

Direct RV32 disassembly establishes a zero-fill service, not a copy or cache-only operation.
The constructor receives `(address, byte_count, context)`.
The following coordinates use the original ROM and donor base `0x200000`.

| Operation | Original ROM | Donor |
| --- | --- | --- |
| Constructor entry | `0x21bbf0` | `0x21bcc2` |
| Address translation and range check | `0x21bc52..0x21bc9c` | `0x21bd20..0x21bd3c` |
| Short-range threshold | `0x21bc9e..0x21bca2` | `0x21bd3e..0x21bd42` |
| Direct zero fill | `0x21bca6..0x21bcac` | `0x21bd46..0x21bd4c` |
| Byte-fill implementation | `0x2180ce..0x2180dc` | `0x217ffc..0x21800a` |
| Command submission | `0x211a32..0x211a94` | `0x2205e8..0x220656` |
| Completion callback | `0x211a96..0x211ab0` | `0x220658..0x22067e` |

Both constructors reject zero byte counts before submission.
The threshold instruction loads `0x207`, which means 519 bytes, not 207 decimal bytes.
For lengths through that threshold, the constructor passes fill value zero to the byte-fill implementation.
That implementation stores the fill byte at each destination address.
Larger ranges use descriptor family `0x2006` through the synchronized submission path.
The legacy-to-donor constructor mapping is `0x8cc → 0x90c`.
The request and completion mappings are `0x7c0 → 0x7f8` and `0x7c4 → 0x7fc`.

Both submission implementations select command `0x11`.
They write indexed input words zero through three as `{1,address,byte_count,address}`.
They do not initialize the other command words or operation metadata.
The completion callback only notifies the context and returns the supplied predicate.
It does not read output words or snapshot status.
The short-path stores establish the service's zero-fill semantics.
They do not establish the physical engine's internal implementation.

The virtual model implements the observed single-range frame with actual secure DMA writes.
It uses a 256-byte zero buffer and checks the complete 32-bit range before the first write.
It requires all sixteen frame bytes from the current acquisition.
It rejects a different count, unequal address words, zero length, or address overflow.
It does not constrain stale command arguments or metadata that the source does not write.
It publishes completion only after every DMA write returns `TLM_OK_RESPONSE`.
Successful completion has no output words.
Failed DMA can leave preceding chunks cleared, but cannot publish completion.
No rollback or atomic-clear guarantee is claimed.

The native fixture `tests/fixtures/tme-memory-clear` checks actual backing bytes after command `0x11`.
It checks unaligned ranges of 1, 255, 256, 257, 519, 520, and 777 bytes.
It checks every byte of the surrounding 1024-byte region after each clear.
Each failure case first completes those valid operations.
It then checks a malformed count, unequal addresses, a stale frame word, overflow, denied DMA, or zero length.
Denied DMA targets the sequencer's output aperture, which rejects writes.
The expected success terminal is `0x600d0000`.
Each failure must stop at submission register `0x02230060`.

The full integration suite passes 62 tests, including this native clear regression.
The installed system performs the guest's actual DMA writes and observes every expected failure terminal.
Both canonical firmware runs subsequently reach APDP authorization.
Their progress alone does not establish whether they executed command `0x11`.

## Bounded authorization refusal

The model keeps command response and command error as separate fields.
The indexed reader maps this field to sequencer offset `0x148`.
Response status remains at `0x144`.
Supplemental status at `0x160` remains zero.
The unresolved field at `0x164` also remains zero.

Donor snapshot `0x21d1ac` copies the response into context `C+4` and the command error into `C+8`.
Donor PC `0x2249ee` reads `C+8`.
Its nonzero branch returns that command error without copying the entitlement mask.
It does not read callback reason `C+0x20`.
The genuine callback correctly stores reason two on refusal.
Previously, response bit zero indicated failure, but the constant-zero command error incorrectly selected importer success.

The virtual policy defines `virtual_policy_denied = 0xffffffff`.
This value is not a recovered vendor enumeration.
The bounded importer branches test only zero versus nonzero.
No supplementary error code is invented.
Both refusals complete with `0x144.bit0 = 1`, command error `0xffffffff`, and output length zero:

- Command `0x29` requires feature `0x186` and `deny_debug_authorization`.
- Command `0x18` requires feature `0x184` and `deny_rom_patch_authorization`.

Each policy parameter defaults to false.
The reconstructed runtime explicitly enables both refusals.
The envelope requires a four-byte header, its declared payload, and zero alignment padding.
The model requires every envelope byte from the current acquisition.
It rejects incorrect features, inconsistent lengths, missing bytes, and oversized or unaligned input.
Unrelated command words can retain earlier values.
Unknown commands and authorization commands without their denial policy remain unsupported.
Neither refusal evaluates credentials, publishes privileges, applies patches, performs DMA, or supports positive authorization.

The command error persists across reads until acknowledgment or a new acquisition clears it.
The existing response-error flag remains separate for signature verification.
Acknowledgment and acquisition clear both errors, so later commands do not inherit the refusal.

The genuine importer skips parsing when each token returns a nonzero command error.
With no successful records, its aggregate result is `4005`, not the per-token virtual error.
It skips the complete mask-publication path, retains a zero mask, and leaves the processed byte false.
Therefore, the speculative zero-only authorization socket at `0x00c00224..0x00c0022b` is removed.
The model does not replace the genuine importer, callbacks, parser, or publication logic.

The native fixture covers both command envelopes, retained error reads, zero output length, and stale unrelated command words.
It covers missing words, a missing byte, wrong features, malformed padding, inconsistent sizes, unknown commands, and disabled policies.
It also checks refusal, acknowledgment, and a later successful entropy command with a zero command error.
The assertions do not depend on entropy values.
These fixture changes are not run in this slice.
Integrated verification and actual firmware proof of aggregate failure and absent privilege publication remain pending.

## Passive authenticated-image handoff

`image_handoff_observer.h` implements `qcom,authenticated-image-handoff-v1`.
This observer forwards mailbox transactions to the existing `gs_memory` object.
It writes no request, response, authentication result, release flag, or service output.
Original OEM execution remains the only authentication producer.
The typed notification permits a named virtual service substitute, not a claim of physical processor release.

### Selected lineage coordinates

The selected `RADIO/tme_fw.img` hashes identify the following handler evidence:

| SoC | SHA-256 |
| --- | --- |
| SM8850 | `4258fc24317e3467b39144ab72a46d85e4494c80bddc37b14de16177ff0c4a19` |
| SM8845 | `6a781e1eb215fbcd2d157bebb7b25502f6ae74ed9bce7810dc61f98545c10fb4` |

Addresses below use the TME-local execution view unless the row names XBL.
The ELF address `0x2041c000` maps to TME-local `0x41c000`.

| Operation | SM8850 | SM8845 |
| --- | --- | --- |
| Metadata opcode 6 handler | `0x42b170` | `0x42b1b8` |
| Streaming opcode 7 handler | `0x42bc88` | `0x42bcd0` |
| Streaming segment finalizer | `0x43b8ca` | `0x43bb46` |
| Combined opcode `0x10` handler | `0x42b622` | `0x42b66a` |
| Combined segment verification | `0x43aff8` | `0x43b274` |
| Authentication cleanup | `0x42b13e` | `0x42b186` |
| Segment opcode `0x11` handler | `0x42bf98` | `0x42bfe0` |
| Finalization opcode `0x12` handler | `0x42c1a0` | `0x42c206` |
| Current-image context getter | `0x4389da` | `0x438c56` |
| Current-image context word | `0x448930` | `0x448910` |
| XBL finalization wrapper | `0x148a1e00` | `0x148a074c` |
| XBL wrapper caller | `0x1487809c` | `0x148767e0` |

Metadata uses the `0xa4`-byte input layout documented above.
Its image ID is payload word zero, not the previous secure context value.
The observer also checks the secure context after the matching metadata response and release.
This second check prevents a changed metadata input from joining the wrong image lineage.

Native SM8850 run 177 uses opcode `0x10`, not the separate `0x11` and `0x12` sequence.
Its AOP metadata request has sequence 40 and image ID `0x21`.
Sequence 41 uses opcode `0x10` with an `0x59`-byte payload.
The previous observer ignored this command and never notified the AOP publisher.
The publisher therefore left the resource discovery word at zero.

Opcode `0x10` requires at least `0x59` input bytes.
The original handler requires valid metadata and compares payload word zero with the current image ID.
It checks all required segments through the combined verification routine.
The SM8850 path at `0x43a870` hashes segment bytes and compares each result with the authenticated hash table.
The handler preserves verification errors and performs its permission operations before successful completion.
Both handlers clear the authentication context before they publish their response.

The observer captures the matching image ID before this cleanup.
It requires successful metadata in the same lineage, fresh matching completion fields, and the final consumer release.
It does not replace verification, interpret a metadata result as full authentication, or synthesize an AOP cookie.
The separate `0x11` and `0x12` path remains supported because the selected firmware also implements it.

Opcode 7 requires at least `0x1c` input bytes and the same valid image context.
Payload word one selects operation 1 to start, 2 to update, 3 to finalize, or 4 to perform no operation.
Only operation 3 can establish segment completion.
The SM8850 finalizer computes SHA-384 and compares its result with the authenticated segment hash at `0x43b9b0`.
It clears the pending bit at `0x43ba6e` only after its checks succeed.

The observer captures the operation before firmware execution.
It requires the matching successful response and consumer release before it records segment completion.
Start, update, and no-op requests cannot establish this state.
A failed streaming request invalidates the image lineage.
The final opcode `0x12` must still pass the original pending-bitmap predicate.

Opcode `0x11` compares input word zero with the current-image context.
The comparison occurs at SM8850 `0x42bff0..0x42c008` and SM8845 `0x42c038..0x42c050`.
The handler validates the segment index and pending state before clearing its pending bit.
The observer requires an input of at least 16 bytes and a matching image ID.
It records segment success only after the matching successful response and consumer release.
The genuine finalizer, not a host segment counter, decides whether all required segments completed.

Finalization uses exactly one payload byte.
Value zero skips required work and cannot admit a service.
Value one invokes the pending-bitmap predicate at SM8850 `0x42c1f2` or SM8845 `0x42c258`.
The bitmap starts at `0x40f380`.
A nonempty bitmap produces genuine error `0x7022`.
SM8850 cleanup at `0x42c22e` calls `0x42b13e`, which clears authentication globals.
Therefore, the observer captures the context ID when the request publishes, before cleanup.
It never reads a cleared context as the final image identity.

### First capture and last release

The request header is mailbox-relative `0x2800`.
The response header is relative zero.
Both headers contain signature, capacity, produced count, and released count.
The observer accepts the OEM `AXSC`/`TMFW` pair.
It derives request and response indices independently from their headers.
Supported request capacities are two and twenty.
The response capacity is two.
Each ring has a sixteen-byte header and twenty-byte slots.
The existing boot-status reader still sees the original backing, including the earlier `TPBL` header.

1. Capture each request after the producer writes header `+8`.
2. Require a new consecutive sequence, a complete freshly written slot, and release value zero.
3. Start a new lineage on every metadata request, including a request that later fails.
4. Require successful metadata and either combined authentication or the separate segment/finalization sequence.
5. Capture the terminal command's image ID before the handler can clear its context.
6. Require fresh response fields: matching sequence and opcode, state two, and service result zero.
7. Admit only after the matching request release field receives a complete word write of one.

The normal APSS release writer is XBL `0x148cc4d8..0x148cc4ec`.
The observer uses this request ownership field, not a CPU PC or a log message.
It does not infer readiness from metadata alone, loaded bytes, or global TME state four.
Response fields must all receive writes after the captured request.
Old bytes in a reused response slot cannot satisfy this condition.
Changed request fields, overlapping authentication requests, failed results, partial releases, and inconsistent headers discard lineage.
Counter rollback, repeated publication, and sequence wrap disable further admission for the module lifetime.
Ring reuse does not create a reset generation.

The implementation uses twenty pending slots and sixteen subscriber slots.
Subscription must occur during elaboration.
Each subscriber receives at most one admission for its image in the cold module generation.
Multiple subscribers for one image receive the same event.
Callback failure consumes that admission and reports `IMAGE_HANDOFF_VIRTUAL_SERVICE_FAILED`.
The original successful authentication result remains unchanged.
Callback exceptions and observation exceptions also produce separate diagnostics.
No warm-reset API exists.
Construct a new platform to obtain a new cold observer and cold services.

### Integration and evidence boundary

`platforms/qcom/sm88xx/tme-image-admission.lua` installs nothing unless `current_image_context_address` is nonzero.
Call it after base composition with `0x448930` for SM8850 or `0x448910` for SM8845.
It moves the existing mailbox routing descriptor to `tme_image_admission.target_socket`.
The aperture remains TME `0x02000000+0x3800`, with relative addresses.
The existing APSS alias remains unchanged.
`backing_socket` binds directly to `tme_mailbox.target_socket`.
`memory_socket` binds to the selected TME router.
Boot-status reads retain their original direct backing binding.
Neither a duplicate memory object nor a second alias is created.
DMI remains disabled on the backing and observer.

All blocking and debug transactions forward their bytes and original results.
Debug writes revoke pending evidence and cannot create admission.
Observation reads use separate payloads and do not change the guest transaction's delay annotation.
Mailbox inspection uses `backing_socket`, so it cannot recursively enter the observer.
Context and payload inspection use `memory_socket`.
The selected wrappers stage inputs in APSS `0x20400000..0x2045bfff`, aliased to TME `0x400000..0x45bfff`.
The observer accepts this bounded alias or its local view.
Other input address domains remain unsupported and cannot admit a service.
No firmware bytes are embedded in the model or helper.

### Verification boundary

Both genuine OEM boots now use the observer and preserve their original report-query and NCC execution.
Neither boot has reached native admission for AOP or PDP.
The observer still requires a new platform construction for a new cold generation.

Actual-model guest fixtures supply synthetic protocol responses.
They verify rejection of failed metadata, failed segments, failed finalization, byte-zero finalization, wrong context, wrong sequence, and missing release.
They also reject stale response bytes and repeated publication.
The valid path captures the image before context cleanup and emits only after the matching release.
A failed resource callback leaves the original successful completion word unchanged.

The permanent PDP fixture reuses ring slots and verifies unchanged bytes outside the two admitted FREE words.
These synthetic checks establish observer behavior, not genuine authentication.
Native image admission, selected UEFI consumers, and Linux output remain separate proof requirements.

## Virtual managed-key lifecycle

The reconstructed runtime explicitly enables `virtual_key_substitute`.
The default model keeps this option disabled.
The profile implements command two for derivation, command four for HMAC, and command `0xd` for cleanup.
It implements command `0xb` for authenticated export and command `0x1c` for the transport-key broadcast.
It does not change image-signature verification or optional-authorization denial.

### Recovered request contracts

Donor `0x21e93e` submits SYSTEM_KDF with mode zero and PRF selector three for the selected DDR request.
It supplies source slot nine, destination `0x1e`, requester two, and normalized credential two.
The GPCE root request at run417 supplies source slot eight.
The virtual profile accepts both fused roots.
It binds the source slot into the virtual KDF so the two roots stay distinct.
The input contains two opaque policy words, a binding-state word, secondary input, and a padded label.
Metadata `0x40` gives the secondary length. Metadata `0x44` gives the padded label length.
Metadata `0x48` records whether the binding-state word is nonzero.
The observed word `0x30` does not establish a requested key length.
The model does not reinterpret it as one.

The DDR request contains 64 secondary bytes and a 22-byte label with two padding bytes.
The complete frame contains 100 bytes.
The firmware copies the policy words without decoding them.
The older public HWKM layout does not establish this TME version's policy encoding.
The physical root material and its initialization remain unknown.

OEM opcode `0x17` calls legacy slot `0x394` with an encoded algorithm, key slot, input descriptor, credential, and context.
Donor export `0x394`, function `0x21a4e4`, preserves that contract.
It constructs internal operation three.
The existing submission and completion mappings select donor `0x21f324` and `0x21f376`.
The outer mailbox opcode does not identify the internal operation.

For encoded algorithm `0x80000002`, the donor submits command four with arguments two, one, and three.
It supplies the selected key slot, requester two, and matching current and total byte counts.
The input frame contains a segment count and triples of address, length, and zero.
The selected DDR request supplies one 32-byte DMA segment.
The compatible completion selects 48 output bytes and publishes the actual copied length.

### Explicit virtual policy

With an empty `virtual_root_hex`, the model derives its 48-byte default root from the public string `qcom-vp virtual TME managed-key profile v1` with SHA-384.
This deterministic identity is not a device secret and provides no production key protection.
It permits reproducible virtual state without copying physical secrets.
Only requester two with normalized credential two can derive.
The permitted sources are the fused roots `8` and `9`.
Destinations occupy slots `0x1c..0x3f`.
Root replacement, foreign use, foreign cleanup, and use of an absent key fail.

The profile derives 48-byte child keys through HMAC-SHA-384 in SP800-108 counter form.
Its label contains the supplied padded bytes, followed by a zero separator.
Its context contains the following fields in order:

1. The model encodes the L2 identifier, requester, and normalized credential as little-endian words.
2. The model appends the source root slot.
3. The model appends the supplied secondary bytes.

The counter is big-endian one. The final big-endian length is 384 bits.
This binding layout is an explicit substitute, not a recovered silicon serialization.
The opaque policy words affect the derived bytes.
They do not establish an implementation of the vendor's usage permissions or binding-state interpretation.

### Configured virtual root input

The model accepts the optional CCI parameter `virtual_root_hex`.
The SoC platform wrappers apply it through `platforms/qcom/sm88xx/security-roots.lua`.
Omit the field or set it to an empty string to keep the SHA-384 digest of the public identity above.
A nonempty value must contain exactly 96 hexadecimal characters.
Do not add a `0x` prefix or whitespace. The model accepts uppercase and lowercase letters.
The model decodes the characters to the 48-byte root before simulation starts.
The parameter requires `virtual_key_substitute=true`.
The default model keeps that opt-in disabled.

This root keys only the virtual command-two managed-key derivation.
It does not change generated ECDH keys, transport-key derivation, image verification, or authorization.
The root remains in the sequencer across command release, key cleanup, and guest reset.
A new simulator process initializes it again from its profile.

The public-label value is public test material.
An explicit value needs its own provenance. The virtual derivation does not establish the physical TME algorithm.
The profile does not export phone identity by default or provide production key protection.
The virtual profile permits only internal HMAC use of its child keys.
Authentication qualification therefore remains separate from functional boot progress.

The HMAC operation checks key presence, requester ownership, input coverage, descriptor bounds, and the complete byte count.
It reads each DMA segment and computes HMAC-SHA-384 with the retained key.
Cleanup checks ownership and erases the key before completion.
These commands do not export root or child material.
State failures use the existing virtual policy error `0xffffffff`, not an invented vendor error number.
Unsupported command shapes retain the unmodeled-command failure.

### Verification

The `tme-keys` guest compares complete HMAC results against independent Python `hashlib` and `hmac` calculations.
It covers changed input bytes, changed derivation context, foreign use, foreign cleanup, missing keys, protected-root replacement, and independent slots.
It also clears and recreates a key, then checks the reproducible result.
The complete host suite passes 79 tests. Clippy passes with warnings denied.
SM8850 run95 and SM8845 run90 execute native derivation, HMAC, and cleanup.
Their APSS traces reach HMAC-success branches `0x148c06cc` and `0x148beb24`, then the successful cleanup return paths.
Both report `DDR: Start of DDR Training Init` and subsequently poll PHY offset `0x8f98`.
Neither run establishes complete DDR training, authentication qualification, or kernel entry.

## QSEE-to-TME QMP notification

### Selected package sources

This analysis uses both unmodified `RADIO/tme_fw.img` files.
Instruction addresses below use the local TME view.
The image hashes are:

| Package | TME firmware SHA-256 |
| --- | --- |
| SM8850 CPH2749 | `4258fc24317e3467b39144ab72a46d85e4494c80bddc37b14de16177ff0c4a19` |
| SM8845 CPH2771 | `6a781e1eb215fbcd2d157bebb7b25502f6ae74ed9bce7810dc61f98545c10fb4` |

Both QSEE configurations select remote host 14, name `tme`, and edge name `tme_tz`.
Their direct notification pointer is `0x17960014`, and their command value is one.
The pointer occurs at config file offsets `0x16ac0` and `0x13ef8`, respectively.
QSEE performs a descriptor readback and `DSB SY` before notification.
The notification helper writes the configured word without a status read or acknowledgement poll.
The shared descriptor remains at APSS `0x22002800`, which already aliases TME `0x02002800`.
No additional descriptor backing is needed.

### Receive interrupt selection

The TME configuration records occur at file offsets `0x298c8` and `0x2995c`.
Each record has size `0x24` and belongs to the reverse TZ edge.
The fields at `+4`, `+0x14`, `+0x18`, and `+0x1c` contain seven, minus one, 28, and `0x02002800`.
The consumers establish the interrupt meaning. Field proximity alone does not establish it.

| Operation | SM8850 | SM8845 |
| --- | --- | --- |
| Registration caller | `0x4352f4..0x435312` | `0x43534c..0x43536a` |
| Enable caller | `0x435314..0x435328` | `0x43536c..0x435380` |
| Registration wrapper | `0x434fc8` | `0x435020` |
| Enable wrapper | `0x434f9e` | `0x434ff6` |
| Direct registration tail | `0x41ea7a` | `0x41eafa` |

The registration caller loads legacy import `0x99c` at `0x41719c`.
It supplies selector minus one, interrupt 28, the callback from import `0xb9c`, the edge context, and remote host seven.
The enable caller uses legacy import `0x9a0` with the same selector, interrupt, and host.
The registration wrapper selects its direct path only for host seven and selector minus one.
That path forwards interrupt 28, callback, and context to the direct registration tail.
The tail stores the callback and registers its wrapper through the actual interrupt-controller interface at legacy `0xad8`.
The enable wrapper forwards interrupt 28 to that interface's enable operation at legacy `0xae4`.
Other selectors use the separate IPCC path.
Thus this edge uses CLIC interrupt 28, not a guessed neighboring interrupt or the Linux IPCC client number.

The existing SM8850 run251 trace contains this direct registration path.
Its trace executes `0x434fee..0x434ff8`, `0x41ea7a..0x41ea92`, and the direct enable tail at `0x434fae..0x434fb8`.
This observation predates the notification change. It is not proof that the new route executes.
The preserved OEM wrappers own these imports.
Existing mappings `0x99c → 0x9d4` and `0x9a0 → 0x9d8` preserve the installed callbacks.
The callback at legacy `0xb9c` remains guest-owned.

### Incoming notification latch

The selected TMEC configuration at SM8850 `0x447564` declares first external ID 21.
It supplies pending-source read address `0x00c00110` and clear address `0x00c00108`.
Its software-pending table maps command one to interrupt 28.
The same configuration selects signal 28 for the outgoing IPCC send.
Donor interrupt backend `0x2237da..0x223818` checks the source bit and writes its one-bit mask to the configured clear bank.
For interrupt 28, the mask is `1 << (28 - 21)`, or `0x80`.
The existing CLIC owner already implements that W1C operation and the interrupt masks.

The shared runtime maps APSS `0x17960014`, size four, to an optional receive socket on that CLIC owner.
An aligned 32-bit write of one latches interrupt 28.
Zero has no effect. Reads, other command bits, byte enables, and unsupported access widths fail.
Repeated writes coalesce while the latch remains pending.
The latch survives disabled source masks and CPU interrupt masks.
The guest's existing `0x00c00108` W1C clears the latch, which permits a later notification.
An asserted independent hardware source still follows the existing level-source policy.

This is an explicit virtual connection between evidenced endpoints.
The generated silicon register name and its internal wiring remain unknown.
The coalescing latch and zero-write policy are functional choices, not recovered silicon timing or readback semantics.
No physical IPCC register identity is claimed for the receive socket.
No model reads or writes the QMP descriptor, buffer lengths, link state, channel state, or acknowledgement fields.

### Outgoing peer notification

The command register at `0x00c00100` offset zero is an outgoing notification.
It is not a local pending write.
Donor export `0xbfc` selects `0x22fafe`, the QMP peer-notify function.
It tailcalls OEM `0x9a0`.
The donor twin `0x9d8` runs through the bridge as `0x434f9e`, then legacy `0xae4`.
Donor `0xb30` = `0x223566`, then `0x990` = `0x2237a0`, performs the byte store to `0x00c00100`.
The other branch of original `0x434f9e` runs `0x4403e0 -> 0x44080c -> 0x440942`.
That branch writes an IPCC send word, host `<< 16 | signal`, to base plus `0xc`.
Configuration `0x4448c8` names the edge `tz`, sets remote host seven, selector minus one, and signal 28.
The incoming acknowledgement path at `0x00c00108` is separate.

Run385 replaced the local latch with a log statement for this command.
The real command consumer at TME `0x420398` then executed.
The previous loop disappeared.
Execution stopped at a missing legacy import read of `0x41701c` at TME PC `0x4419fe`.
Run383 repeated outbound call `0x22fe56` ten times with sp `0x40afb0` and task `0x40ee18`.
The queue producer wrote 65 bytes and cleared the waiter, but the loop starved the task.
Runs380 and 383 timed out on QSEE command `0x166` for the same reason.

The CLIC model now drives two optional boolean outputs, `peer_notify_0` and `peer_notify_1`.
They represent command bits zero and one.
Each bound output goes high for one delta and returns low in the next delta.
A zero command raises no event.
A command does not change `software_`, pending state, or CPU interrupt output.
An output with no binding reports the positive notification on stderr as `TME_PEER_NOTIFICATION` with its command value and unrouted bit mask.
The log does not acknowledge a received event.

QSEE polls through `0x15671458 -> 0x156713e4 -> 0x1572bea4`.
The evidence does not establish a receiving IRQ connection for these outputs.
The runtime platform therefore leaves both outputs unbound.
The missing physical route stays explicit.
This is not a claim that a notification interrupt reaches the peer.
The genuine TME firmware remains the only QMP peer.

### Fixture coverage

The `clic` guest fixture drives incoming notification only through the APSS QMP write at `0x17960014` and the timer source at ID 64.
It covers masked pending state, source-mask writes, repeated-write coalescing, guest W1C, equal-priority tie resolution to the larger ID, and a second notification after acknowledgement.
It writes command bits to `0x00c00100` with both source masks enabled.
Raw state, pending state, and interrupt delivery must stay clear.
A local latch from the outgoing command therefore fails the fixture.

The `clic` native consumer binds `peer_notify_0` and `peer_notify_1` to `sc_signal<bool>` observers.
It checks each output pulse, the following low level, zero commands, and both command bits.
It also checks that outgoing commands leave local pending state and CPU interrupts clear.
An incoming QMP write still latches, coalesces, and clears by W1C in the same fixture.
A second CLIC instance leaves both outputs unbound and checks the unrouted log line.
This observation adds no peer firmware and no receiving route.

SM8850 run254 and SM8845 run205 enter the genuine TME receive path after the QSEE notification.
They stop at PCs `0x435852` and `0x4358aa`, respectively, while reading legacy import `0x417278`.
These runs prove interrupt delivery, not complete QMP negotiation.

The focused CLIC fixture passes with the outgoing pulses separate from the incoming latch.
SM8850 run387 completes the genuine TME response after the predicate and decoder bindings.
QSEE accepts its CRC and returns transport success.
The run stops on the GPCE version read, before kernel entry.

## QSEE TME interrupt controls

SM8850 QSEE reads bit zero at `0x01fb300c` from function `0x15682af0`.
When the bit is one, QSEE writes one to `0x06800040`.
The source-only name `SOC_PRNG_IRQ_ENABLE` remains unestablished.
The selected QSEE branch and the retained EL1 probes support the one-bit access contract.

QSEE then reads bit zero at `0x01fbc00c` from function `0x15682b90`.
When this bit is one, QSEE writes one to `0x26300040`.

Infiniti EL1 probes read one at `0x01fb300c` and zero at `0x01fbc00c`.
The probes used aligned four-byte loads after OEM initialization.
These results do not establish silicon reset values.

The SM8850 profile retains both one-bit controls.
It also retains the QSEE write at `0x06800040`.
The controls do not create a PRNG event, an interrupt, or an acknowledgement.

Diagnostic runs347, 349, and 350 passed these accesses in order.
Each run disabled the unconnected APSS watchdog terminal policy.
Run350 next reached the DDR frontend read at `0x310ba29c`.
These runs do not prove normal watchdog behavior or kernel execution.


## Command 3: GENERATE_KEY

Run687 and run689 reject sequencer command `3` with arguments `{0,0,0}`.
The donor body submits it after an inferred watchdog QBEC import binding.
The OEM descriptor contains policy-shaped words and a `QBEC` tag.

### Independent naming sources

The OEM event sequence and public interfaces identify this operation.

1. The OEM submit body at donor `0x21e58e` logs event `0x300f` before the
 register writes, with three fields, then event `0x3010`, with two fields.
2. The public kernel driver
 `drivers/soc/qcom/hwkm.c` (`android-msm-p11-5.15-tm-wear-kr3-dr-p11-qpr3-release`)
 and its header `tme_hwkm_master_intf.h` declare
 `TME_HWKM_CMD_GENERATE_KEY` with a request structure
 `{ key_id, struct tme_key_policy key_policy, cred_slot }`.
 The structure's field order and the two logged events match exactly:
 a destination key ID, a two-word key policy, and a credential slot.
3. Public `tme_hwkm_master_defs.h` defines `struct tme_key_policy`.
 The structure holds two packed `uint32_t` words, `low` and `high`.
 The header lists a complete bit-field catalog for key type, length,
 profile, operation, algorithm, security level, destination, owner,
 lineage, wrapping constraints, and policy version.

Command `3` is therefore `TME_HWKM_CMD_GENERATE_KEY`, not an unnamed or
speculative operation.

### Register and payload contract

The donor accessor table at `0x2326f0` (functions `0x21fcc2..0x21fdde`)
resolves each register index to its sequencer offset.
Direct disassembly of every table entry gives this complete map for the
indices the GENERATE_KEY submitter uses:

| Register index | Sequencer offset | Field |
| --- | --- | --- |
| `0` | `0x60` (dispatcher-selected, not table-selected) | Control |
| `1` | `0x00` | Command |
| `7` | `0x04` | Argument 0 |
| `8` | `0x08` | Argument 1 |
| `9` | `0x0c` | Argument 2 |
| `0x10` | `0x14` | Allocated destination key slot |
| `0xf` | `0x2c` | Requesting EE |
| `0x12` | `0x30` | Requested `DEST_KEY_SLOT` / `CRED_SLOT` |

The body writes command `3` and zero at the three argument offsets.
The inferred QBEC binding reaches it through OEM dispatch table `0x443264`, index `0x21`.
The body writes the requesting EE at `0x2c` and the descriptor's
`CRED_SLOT` field at `0x30`.
It calls allocator `0x21e360` with the requesting EE and a fourth
descriptor word, and writes the allocator's returned slot at `0x14`.
It transfers the key policy through donor transfer slot `0x6fc -> 0x71c`
as two words. The model's input aperture receives them as eight input
bytes: `KP_lo` at input offset `0`, `KP_hi` at offset `4`.
Completion `0x21e6ac` copies no output words on success.

OEM TME firmware `RADIO/tme_fw.img` inside the selected CPH2749 `.700`
package is an ELF. Its code segment sits at physical `0x2041c000`,
mapped to TME view `0x41c000`.
Donor runtime `tme-runtime-donor.bin` and reconstructed `tme.bin`
(`prepared-sm8850-common-runtime46`, SHA-256 `94059e1c3ba9…` and
`76c35cabc470…`) carry the executed submit and allocator bodies.
The reconstruction's instruction relocations placed the runtime
import-table base at `0x489000` in the executed image.
The unrelocated donor file keeps `0x48d000` and other bases there.

### Policy decode

The donor treats two OEM descriptor words as a key policy.
The descriptor also contains `QBEC` and version one before the source path.
The public field layout decodes low word `0x04204829` as follows:

| Bits | Value | Public name |
| --- | --- | --- |
| `2:0` | `1` | ECC key type |
| `6:3` | `5` | 384-bit length |
| `13:10` | `2` | Key operation two |
| `19:14` | `1` | ECDH algorithm |
| `21:20` | `2` | Hardware key |
| `29:26` | `1` | TME firmware owner |

High word `0x00024040` gives policy version four and authorized user TME firmware.
Its wrapping-constraint bits are zero, so the policy permits no key export.
These facts identify an internal P-384 ECDH key for QBEC image decryption.
The public names for operation two differ between versions, so the model does not rely on that field.

### Functional generation profile

The `virtual_key_substitute` profile accepts command `3` only with this exact policy pair.
Other policies keep the unmodeled-command failure.
The model requires the three zero arguments and eight written input bytes.
It rejects an occupied or out-of-range slot and a credential slot above two.

Host entropy gives a 48-byte big-endian scalar. Rejection sampling keeps it in `[1, n - 1]` for the P-384 order.
The key keeps its requester as the owner and stays in the model.
HMAC and wrapped export reject the ECDH key.
Cleanup erases it under the existing ownership check.
Completion copies no output words, as donor completion `0x21e6ac` expects.
The physical random source and the later ECDH operation remain unrecovered.
No command derives a shared secret from this key yet.

Run692 logs `SEQUENCER_KEY_GENERATED slot=25 owner=1 credential=2 type=ecdh-p384 bytes=48`.
Donor body `0x231a4e` then reaches export `0x280`, body `0x21b1b8`, with internal operation `0x17`.
The OEM dispatcher at `0x41f924` reads unmapped legacy submit slot `0x6a8`.
Donor submit `0x21de92` turns this operation into a sequencer command.
Run693 shows command `0x28`, so the earlier selector assumption was wrong.

### Commands `0x0a` and `0x28`: GET_ECC_PK

Export `0x280` builds request word `+4` equal to `0x17`.
The OEM dispatcher passes request offset `+0x10` to the submit import, as for the other mapped families.
The map binds legacy submit `0x6a8` to donor `0x6c8` and legacy completion `0x6ac` to donor `0x6cc`.
The donor built this request, so its own descriptor layout matches both bodies.

The descriptor holds the requester, the key slot, the curve, the bit length, and one flag byte.
Donor `0x2a0` maps the QBEC policy low word to curve `4`.
Donor `0x2b0` maps curve `4` to 384 bits.
ROM command 8 uses the same curve value `4` for P-384.

Donor submit `0x21de92` selects the command as follows:

| Curve | `xCoordOnly` | Command |
| --- | --- | --- |
| `0x21` or `0x22` | Any | `0x23` |
| `0x18` | Any | `0x12` |
| Other | Zero | `0x0a` |
| Other | Nonzero | `0x28` |

Accessor table `0x2326f0` resolves the submitted registers.
The key slot goes to `0x10`, the requester to `0x2c`, and the curve to `0x38`.
The input byte count at `0x58` is zero, and the three arguments are zero.
Completion `0x21df70` copies `4 * (((bits + 31) >> 5) << shift)` bytes from output word zero.
The shift is one minus `xCoordOnly`, so P-384 copies 96 or 48 bytes.
Wrapper `0x21b1a8` sets `xCoordOnly` before it calls export `0x284`.

The model implements commands `0x0a` and `0x28` for curve `4` and a generated ECDH key.
Command `0x0a` returns 24 words: big-endian X, then Y.
Command `0x28` returns 12 words: big-endian X.
ROM `0x20f5cc` supplies P-384 coordinates to command 8 in this byte order.
The model rejects a foreign owner, an absent key, and an HMAC key.
Other curves and nonzero input keep the unmodeled-command failure.
Commands `0x12` and `0x23` stay unmodeled.
The physical output encoding remains unverified.

### Policy words in run691

Run691 reached command `3` after the `0x300 -> 0x2fc` runtime binding.
The rejection log records words `0x04204829` and `0x00024040`.
It records allocated slot `0x19`, selector `0x2`, and requesting EE `1`.
The input aperture contains eight written bytes.
The length registers still hold `0x002a8000` from earlier activity.
They are not the policy length.
The older public policy header names version three, but the descriptor records version four.
Its low-word names cannot prove the selected policy meaning.

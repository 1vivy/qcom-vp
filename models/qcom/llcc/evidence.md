# LLCC translator state, DARE state, and memory effects

## Recovered firmware protocol

The protocol comes from a bounded static review of the selected OEM `RADIO/xbl.img`.
Instruction addresses below are ELF virtual addresses. Subtract `0x14826000` to obtain the member file offset.

The consumer at `0x148ad148..0x148ad1d0` programs four groups of four key entries.
For group `G` and entry `E`, it uses these addresses:

- OR-view key base: `0x34910000 + G*0x1000 + E*0x100`.
- AND-view marker: `0x34d10080 + G*0x1000 + E*0x100`.

The consumer installs six words when the marker is zero.
It copies source words at offsets `0,4,8,0xc,0x10,0x14` to register offsets `0x20,0x24,0x28,0x2c,0,4`, respectively.
The validator at `0x148ad5bc` requires the complete marker to equal `0xf03`.
The consumer does not write that marker.
The exact hardware publication trigger and individual marker bits remain unknown.

The producer at `0x148bf524..0x148bf568` obtains 16 bytes and eight bytes from service function `0x148790cc`.
A short service result returns error `0x00500003`.
The model therefore retains supplied words. It does not generate substitute keys.

## Functional implementation contract

`QcomLlcc` retains the six supplied words in its existing register backing.
A separate six-bit receipt mask belongs to each bank and each of the sixteen slots.
The implementation applies these rules:

- Reset clears retained words, receipt masks, and markers.
- Each required word sets one receipt bit, even when its value is zero.
- Repeated writes to one word do not satisfy another word.
- A partial slot has marker zero. Only six received words publish `0xf03`.
- A write to a published slot starts replacement and clears its receipt mask before recording that word.
- Replacement requires all six words again. Old backing words remain until software replaces them, but they cannot publish a marker.
- Each local write changes only its selected bank.
- A broadcast write reaches every configured bank through either view.
- OR-view reads aggregate bank values with OR. AND-view reads aggregate them with AND.
- Markers are read-only observations. Software cannot write a ready value.
- Recognized key words and markers never enter the diagnostic path that prints register values.

The six-word publication rule and replacement boundary are functional policies, not recovered physical triggers.
A valid marker means that the model received a complete slot. It does not mean that a DARE memory operation succeeded.

Security apertures at offsets `0x60000..0x63fff` and `0xb0000..0xbffff` retain their existing owners and rejection paths.
Region initialization uses the same bank state and supplied keys.

## Behavioral regression

The existing `tools/qcom/qcom-vp/tests/fixtures/llcc/producer.S` fixture now covers:

- Empty markers for all sixteen slots.
- Partial writes and repeated writes without publication.
- Publication only after the sixth distinct word.
- Independent bank state and independent group/entry state.
- OR and AND aggregation of markers and supplied words.
- Broadcast writes through both views.
- Replacement invalidation, six fresh words, and zero-valued material.
- Retention of every supplied word in all sixteen slots.
- Data preservation and actual auxiliary writes.
- Different allocations for modes two and three.
- Reinitialization after metadata corruption.
- Invalid keys, operation values, and submitted bounds.
- Row and array completion, followed by cleanup.
- Key, metadata, and configuration revocation after an owner memory access.
- Foreground priority, including row 28.
- Equal configuration writes and low-byte clears without context loss.

The fixture exercises replacement rather than an external reset input.
Reset clears the same receipt masks directly in `cold_reset()`.
`make build` succeeds. The complete suite passes all 91 tests.
`cargo clippy --locked --all-targets -- -D warnings` succeeds.
The initial integration omitted LLCC from the CPUCP fixture. The corrected fixture uses the production LLCC helper.

SM8850 run110 completes eleven initialization rows across all four banks.
SM8845 run96 completes ten rows across all four banks.
Every initialization reports `completed=0xf failed=0x0`.
Both firmwares then execute the subsequent DDR configuration code.
SM8850 stops at PC `0x148ab700`, which reads the unmapped address `0x312110d0`.
SM8845 stops at PC `0x148a9da8`, which reads the unmapped address `0x31211198`.
The retained logs reside under the corresponding `build/private/run-*` directories.
Neither run reaches UEFI, ABL, or the kernel.

## Region initialization

The consumer at `0x148acfcc` uses 32 descriptors with stride `0x20`.
The descriptor supplies data bounds, metadata bounds, key selection, and policy fields.
Bounds use 64 KiB units. The data end is inclusive.

Each hardware row starts at `0x34920000 + row*0x1000`.
Its AND view starts at `0x34d20000 + row*0x1000`.
The important fields are:

| Offset | Meaning |
|---|---|
| `0x00`, `0x04` | Data start, low and high words |
| `0x08`, `0x0c` | Data end granule, low and high words |
| `0x10`, `0x14` | Metadata start when MTE is disabled |
| `0x18`, `0x1c` | Metadata start when MTE is enabled |
| `0x30..0x3c` | Submitted bounds for command three |
| `0x40` | Encryption mode, ECC, MTE, and foreground selection |
| `0x44` | Selected key slot |
| `0x60` | Operation value |
| `0x64` | Start and cleanup pulses |
| `0x6c` | Cleanup request |
| `0x70` | Separate operation-completion condition |
| `0x74` | Completion and bank errors |
| `0x78` | Operation error fields |

Instructions `0x148ad340..0x148ad354` select operation three for encryption modes two and three.
They select operation one for modes zero and one.
The `CSINC` at `0x148ad34c` selects three when its condition is equal.
This corrects the earlier analysis, which reversed the operation values.
Live run109 also records operation three for row zero in mode three.

The row mechanism publishes status three after successful effects.
Cleanup clears that status without destroying the initialized context.
The array mechanism uses request `0x34903010 + row*8` and completion bit four at the adjacent word.
Both mechanisms use the same initialization engine.
Invalid requests retain bank errors and do not publish successful completion.

The producer registers metadata intervals through `0x148beff0`, `0x14856be4`, and `0x14858e3c`.
Those intervals are `[metadata_start << 16, (metadata_end + 1) << 16)`.
The consumer does not program the metadata end.
The model derives capacity from the selected producer's calculations at `0x148c01ac..0x148c01fc`.
For data size `D`, the calculations are:

| Encryption mode | Metadata capacity |
|---|---|
| Two | `(D/8 + 0xffff) & 0x3fff0000` |
| Three | `(D/4 + D/64 + D/512 + D/4096 + 0x107ff) & 0x7fff0000` |

The mode-three shifts at `0x148e6930` are `{-6, -2, -9, -12}`.
Run109 supplies these mode-three examples:

| Row | Data interval | Metadata interval |
|---|---|---|
| Zero | `0x80000000..0x813fffff` | `0x81400000..0x8195ffff` |
| One | `0xd8000000..0xd9ffffff` | `0xe2c00000..0xe349ffff` |
| 28 | `0xc3dd0000..0xc45cffff` | `0xc45d0000..0xc47fffff` |

Foreground selection comes from policy bit 16, not the row index.
Row 28 is foreground and overlaps a background allocation.

## Explicit virtual memory policy

The recovered instructions do not establish the initialized data value or the native metadata encoding.
The virtual operation preserves existing logical data and installed keys.
It clears the actual auxiliary allocation to canonical zeros, then commits the initialized context.
These zeros are a virtual encoding, not recovered authentication tags.
Each new request repeats the auxiliary transition. Identical banks share one clear operation within that request.

The context retains its bounds, policy, and selected key generation.
Normal CPU and DMA writes remain coherent under that context.
Key replacement, changed configuration, and metadata writes invalidate affected contexts.
Subsequent protected accesses fail until valid initialization restores the context.
Equal configuration writes and global low-byte clears preserve the context.
Reset clears contexts, keys, and publication state.

`qcom,llcc-dare-port-v1` places this policy before both shared DDR backings.
All CPU, DMA, and local CPUCP aliases traverse that port.
Metadata remains readable and writable, but the port never grants direct memory access to it.
The port clips data mappings at policy boundaries.
Control changes queue code and mapping invalidation before the control transaction returns.

QEMU applies that invalidation at the next translation-block boundary.
The regression uses `DSB SY` and `ISB` before its owner access.

The XBL DDR path leaves rows three and four in mode one with slot zero, without initialization.
TME later supplies their keys and starts the PIMEM operation.
Before that operation, the selected platform declares slot zero as an opaque encryption-only domain.
This policy creates no key bytes and publishes no key marker.
It cannot satisfy modes two or three. An explicit key write disables the implicit domain.

Both native initialization sequences leave MTE and ECC disabled.
The model rejects requests for those unimplemented effects and logs the feature bits.
The model does not simulate raw DRAM corruption, ciphertext, physical tags, or replay attacks.
No recovered boot consumer requires those interfaces.
Image authentication and authorization remain with their existing owners.

## Public reference limits

[Linux LLCC](https://github.com/torvalds/linux/blob/master/drivers/soc/qcom/llcc-qcom.c) describes cache controls, not these DARE operation values.
[QEMU MTE](https://github.com/qemu/qemu/blob/master/target/arm/tcg/mte_helper.c) demonstrates separate tag storage, but does not establish DARE behavior.
The selected firmware supplies the register protocol. The virtual policy supplies the undocumented internal representation.

## COMMON translator retained-state contract

The selected `IMAGES/xbl_sc.elf` programs these fields through `0x1484d460`.
The selected `RADIO/tme_fw.img` checks them through `0x43ee78`, after subtracting `0x20000000` from TME ELF addresses.
This bounded static analysis identifies a register contract. These addresses do not establish execution of either path.

The XBL table at `0x149232d8` identifies translator configuration at these bank addresses:

- `0x31865000`
- `0x32865000`
- `0x31c65000`
- `0x32c65000`

The additional-region table at `0x149232f8` identifies the corresponding `+0x65100` blocks.
Offsets below are relative to each LLCC bank, not the COMMON sub-block.

| Bank offset | Retained field |
|---|---|
| `0x640f8` | Firmware lock, bit zero |
| `0x65000` | Complete translator configuration word, including enable bit zero and error bits `[13:4]` |
| `0x65004 + 0x10*i`, `i=0..5` | Region offset, low word |
| `0x65008 + 0x10*i`, `i=0..5` | Region offset, high word |
| `0x64ffc + 0x10*i`, `i=1..5` | Region input base, low word |
| `0x65000 + 0x10*i`, `i=1..5` | Region input base, high word |
| `0x65100 + 0x10*(i-6)`, `i=6..9` | Region input base, low word |
| `0x65104 + 0x10*(i-6)`, `i=6..9` | Region input base, high word |
| `0x65108 + 0x10*(i-6)`, `i=6..9` | Region offset, low word |
| `0x6510c + 0x10*(i-6)`, `i=6..9` | Region offset, high word |

Region zero has implicit base zero and no base register.
The original block ends at `0x65058`. The additional block ends at `0x6513c`.
The additional block places region six at offset zero.
The selected binary supplies the additional table and ten-region count.

The model retains complete 32-bit configuration, base, and offset words independently for each bank.
It does not mask high words to four bits or discard their high byte.
Retention of high-word bits `[31:8]` is a functional storage policy, not proof that silicon implements those bits.
TME compares the complete configuration word and low words. Its region checks consume high-word bits `[7:0]`.
XBL produces ten regions, although this TME validator checks only six.
No bank copies another bank. OR and AND reads retain their existing aggregation rules.

Cold reset clears all translator words and all locks.
These zeros represent unprogrammed, disabled storage. They do not represent an accepted translation map.
Software supplies the disable, error-mask, base, offset, and enable writes in their original order.
Accepted writes become visible before the transaction returns. The model does not introduce a ready handshake.
Native TME retains responsibility for equality, ordering, signed-offset interpretation, and overlap checks.
The model neither repairs invalid fields nor forces those checks to succeed.

### Conservative functional lock policy

The selected TME writes one to all four `+0x640f8` words, then requires each complete readback to equal one.
The lock name is `HTMEFW_LOCK`. Exact requester authorization, clear behavior, and reset-domain semantics remain unproved.
The model applies this explicit functional policy:

- An unlocked bank accepts lock values zero and one. It rejects other values.
- An accepted assertion becomes immediately readable as one.
- A locked bank accepts another assertion of one.
- A locked bank rejects every translator-register write, including an equal-value write.
- A locked bank rejects lock-clear writes.
- Only `cold_reset()` clears an asserted lock.
- Each broadcast write checks every selected bank before any bank changes.
- A rejection returns `TLM_COMMAND_ERROR_RESPONSE` and leaves all selected banks unchanged.

The existing transaction validator still checks access shape before register handling.
The existing exclusions still reject offsets `0x60000..0x63fff` and `0xb0000..0xbffff`.
The lock applies to the translator configuration and all ten regions, not to DARE registers or unrelated COMMON words.
The same mutex orders bank reads, writes, and cold reset.

This policy is not a silicon authorization model.
It does not identify a trusted requester or grant access through an XPU.
Existing routing and access-control owners must enforce requester authorization before these transactions reach LLCC.
Shared APSS/TME routing and native lock acceptance still require runtime evidence.

### Translation limitation and verification

No existing LLCC memory consumer uses these COMMON fields to remap memory transactions.
`DareMemoryPort` uses the separate DARE policy and shared physical backing.
This change adds no translation engine and does not change those memory addresses.
It creates no substitute translation map or permission grant.
Execution that requires translated memory routing needs a separate, evidenced consumer implementation.

The existing LLCC fixture adds these regression cases:

- Complete low and high word retention at the original and additional region boundaries.
- Independent configuration and offset state, with OR and AND aggregation.
- Visibility of disabled and enabled configuration, including all ten error bits.
- Original-region and additional-region boundaries without aliases.
- Immediate lock observation, invalid lock values, and repeated assertion.
- Local lock-clear rejection and rejection of low, high, base, offset, and configuration writes after lock.
- Atomic broadcast rejection through both views when only the last bank is locked.
- Continued writes to an unlocked bank without changes to a locked bank.
- Existing DARE behavior after translator locks become asserted.

The fixture recovers only expected synchronous data aborts at its dedicated probe instruction.
Unexpected exceptions still enter the failure path.
The fixture does not expose cold reset. Cold-reset clearing remains explicit in `cold_reset()`.
`make build` succeeds.
The first suite run finds that the fixture monitor terminates expected rejection probes.
The corrected fixture gives only those probes a direct alias to the same controller.
All other accesses retain the monitor.
`cargo test --locked --test platform llcc_requests_preserve_bank_state_memory_and_security_boundaries -- --include-ignored` passes all seven fixture variants.
Clippy also passes.
Native runs SM8850 run115 and SM8845 run101 reach PIMEM initialization.
Neither establishes execution of the TME translator validator or native lock acceptance.

### Native TME translator validation

The TME router now exposes the same nonsecurity LLCC views as the APSS router.
Both processors use the existing bank storage and OR/AND semantics.
The security apertures retain their separate ownership and rejection paths.

SM8850 run122 executes original validator `0x43ee78`.
At its caller return, `0x43317a`, register `a0` is zero.
This proves native acceptance of the translator locks, bank comparisons, and range checks for that run.
It does not prove translated memory routing or physical lock fidelity.

The next native DARE operation reports `row=3 completed=0x0 failed=0xf`.
The original PIMEM service does not publish milestone nine.
The complete suite passes 92 tests. Clippy passes with warnings denied.

## PIMEM DARE command closure

Selected XBL producer `0x148bf978` assigns rows three and four to TME.
Instructions `0x148bfa94` and `0x148bfa9c` clear their initialization flags.
The submission loop at `0x148ad2fc..0x148ad304` skips those rows.
It therefore does not write their submitted bounds at offsets `0x30..0x3c`.

Original TME helper `0x4417a2` validates the configured ranges, mode one, and key slot zero.
It supplies all six key words through `0x441d90`.
Helper `0x441e6c` then starts command one without submitted bounds.
This producer-consumer sequence establishes a full-row command, not a special encoding for zero bounds.

SM8850 run123 records the rejected request in all four banks:

| Field | Observed value |
| --- | --- |
| Row and command | Three and one |
| Configured interval | `0x8aa00000..0x9957ffff` |
| Policy and slot | `0x10001`, zero |
| Key receipt | Complete, `0x3f` |
| Backing | Mapped |
| Submitted start and end | Both zero |

Command one now initializes the configured extent after the existing geometry, backing, policy, and key checks.
It does not consume submitted bounds.
Command three retains its submitted-range checks and auxiliary effects.
The operation preserves logical data under the existing explicit virtual encoding.

Successful effects publish state three at `+0x74` and completion one at `+0x70`.
Failure clears completion and publishes nonzero errors at `+0x78`.
Original TME consumers are `0x4420c4`, `0x4420fc`, and `0x44210a`.
Consumer `0x44216c` also requires bit seven at `0x34d03500 + row*4`.
The model derives that bit from the valid context, so key, metadata, and configuration revocation clear it.
These observations are read-only.

The acknowledgement at `+0x6c`, followed by the cleanup pulse, clears operation observations but preserves the valid context.
That clearing rule is an explicit virtual policy. The selected instructions do not establish every physical clearing rule.

The fixture verifies zero and invalid submitted bounds for command one.
It preserves command-three rejection cases and verifies completion, cleanup, read-only observations, and context revocation.
SM8850 run124 reports `row=3 completed=0xf failed=0x0`.
Native row initialization returns zero at `0x4418a4`. SHRM protection returns zero at `0x4331ce`.
The milestone call returns at `0x433218`. The APSS service wrapper returns zero at `0x1487706c`.
The retained console reports `Pimem init cmd, exit`, external heap completion, and post-DDR DT loading.
It then reports loaded `HYP_AC_CFG` and `QUPFW` images.

The next failure is a clock read at `0x00118534`, from PC `0x148614e8`.
UEFI, ABL, and kernel execution remain unproved.
`make build` succeeds. The complete suite passes 92 tests. Clippy passes with warnings denied.


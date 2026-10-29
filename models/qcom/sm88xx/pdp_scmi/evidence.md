# Authenticated virtual PDP SCMI

## Scope

`qcom,pdp-scmi-virtual-v1` supplies bounded services after genuine image authentication.
It does not execute PDP firmware or claim physical PDP readiness.
It uses the shared `ImageHandoffSource` interface in `models/qcom/sm88xx/tme_mailbox/image_handoff.h`.
The only subscription is image `0x91` during elaboration.

The observer owns metadata, segment, finalization, response correlation, and guest-release checks.
The endpoint cannot produce an authentication result.
Admission writes only FREE=1 at `0x81f08104` and `0x81f08184`.
It preserves every other slot byte and requires a later doorbell before a response.
`PDP_VIRTUAL_AVAILABLE` distinguishes this publication from the observer's authenticated event.

The existing observer defines one cold generation per module lifetime.
This interface has no warm-reset or admission-revocation callback.
The endpoint rejects a different admission event after admission and never repeats FREE initialization.
A future warm-reset implementation must extend the shared observer contract before services can survive that reset.
No warm-reset closure is claimed here.

## Selected inputs

These hashes bind the selected SM8850 CPH2749 and SM8845 CPH2771 inputs.
Executable addresses below are PE RVAs unless identified as PDP Thumb addresses.

| Image | SM8850 SHA-256 | SM8845 SHA-256 |
|---|---|---|
| uefi.elf | da8ad1b0ffc2eff91d7d34d8574a682b7007e0b052439fbbcde989d5393071f5 | 13722b7ed849359e7afc731dfdc1eb0c1fc1815907d708f4b94cadb7e03be4f4 |
| MailboxDxe PE | 17c2b0b9cc619d173bed6be2213ddd59e3d724664703539cb037355723777942 | f9f2ab0990e58515e7ac88049ed5004a8c7b9989eaefb3332be0605f8cfa8ee4 |
| QcomScmiDxe PE | 0455dbc700a1cdd8b58e1584e94c106dce6f298648f2a1541d96703fc863285f | b41ca9547a5f38992bd9e11a060fa914ee3e1c4aec20716f2556a983b0170420 |
| ClockDxe PE | c27e141dcfd0e26c9ff7cd7c9c20c0a5cedd09fa5e86a5630fea95540cf85b70 | 1336c9f313f9323500d430c13d4af5ffc03eac73bf37ae913461cdb3ae6b769c |
| pdp.elf | 74fb165a6619f2451b56360eddd2628827095497593474089316056de8437e8d | 31488acc76154ad223a44a587607cf01c8fddb5c78e80e3c1b8d9a4f63a48fc2 |

The selected MailboxDxe and QcomScmiDxe executable sections are identical between these packages.
PDP is ELF32 EM_NONE with entry zero, not evidence for a CPU entry at `0x81300000`.
Its first load segment contains Thumb vectors linked at `0x20000000`.
The vectors start `0x2001f500,0x20001ac9,0x20001a15`.
No qcom-rehost driver supplies authority for this implementation.

## Mailbox transport

Selected board DTBs begin at xbl_config offsets SM8850 `0x10e14` and SM8845 `0xf52c`.
Their common records specify:

| Remote | TX aperture | RX aperture | TX/RX channels | Interrupt pair |
|---|---|---|---|---|
| PDP0 | `0x19980000+0x300` | `0x17610000+0x4c08` | 0/0 | `{0x42,0}` |
| PDP1 | `0x1a980000+0x300` | same physical bank | 0/1 | `{0x42,0}` |
| CPUCP | `0x18830000+0x300` | `0x17620000+0x4c08` | 0/0 | `{0x3c,0}` |

This helper creates only the two PDP TX objects and one PDP RX object.
It creates no CPUCP mailbox, reset controller, CPU, or service slot.

MailboxDxe send `0x3658 -> 0x3c28` writes `0x494d4353` to TX `+0x100+8*channel+4`.
Receive `0x3914 -> 0x36ac` reads two status words at RX `+0x4400/+0x4404`.
It writes captured bits to W1C `+0x4800/+0x4804` before reading the command-high word.
Close `0x353c -> 0x3bb8` updates enable words at `+0x4c00/+0x4c04`.
The common register model retains all 64 channel command pairs.
The endpoint accepts TX channel zero only and publishes RX channels zero and one separately.

Additional selected MailboxDxe analysis establishes the limited MAP and IDR policy:

- IDR offsets enter record fields `+0x20/+0x40`, but normal selected transactions do not read IDR.
- RX MAP offset enters record field `+0x48`.
- Initializer `0x37b4` calls `0x38d8` with zero at `0x3828/0x3838/0x3848` for enable, clear, and MAP.
- Helper `0x38d8` writes low and high words through `0x3c64`.
- MAP therefore receives only zero writes at `+0x4000/+0x4004` in this consumer.
- Read helper `0x3cf0` serves command-high reads at `0x3a3c/0x3a88` and status/enable helper `0x3b98`.
- The selected UEFI does not use command-low words.

IDR reads return an explicit virtual zero, not a recovered hardware identity.
IDR writes fail. MAP reads return zero, and nonzero MAP writes fail.
TX command-low words retain values without effects. RX command pairs are read-only.
Status is read-only. Clear is write-only W1C. Enable uses ordinary retained read/write words.
Each enable write preserves the other 32-bit half.
Unknown offsets and unsupported register directions return TLM command errors.

Register accesses require aligned little-endian 32-bit transfers without byte enables or streaming wraps.
Partial, unaligned, burst, and byte-enabled accesses fail before any effect.
All register targets disable DMI.

The RX output is the level expression `(pending & enabled) != 0`.
Masked completion retains pending. Unmasking asserts the retained level.
W1C affects only written bits. Clearing one channel preserves another channel's completion and command.
No per-TX copy of RX state exists.
A pending channel refuses replacement until the guest clears its pending bit.

## Slot ownership and correlation

QcomScmiDxe contexts `0x8150/0x8170` select `0x81f08100/0x81f08180`.
Getter `0x549c` accepts platform zero or one only.
CPUCP callback `0x5448` uses the unrelated incoming slot `0x18b4e200`.
The endpoint rejects slot parameters other than the selected two disjoint addresses.

Each slot has size `0x80` and this layout:

| Offset | Meaning |
|---|---|
| `+4` | Status, bit zero FREE, bit one error |
| `+0x10` | Flags, only interrupt flag bit zero is accepted |
| `+0x14` | Length including the four-byte SCMI header |
| `+0x18` | Header with message, protocol, and token |
| `+0x1c` | Up to 100 payload bytes |

Preparation `0x37dc` requires FREE, clears FREE, writes length/header, and sends the doorbell.
Wait `0x3588` polls FREE. Timeout becomes EFI_TIMEOUT `0x8000000000000012`.
Common transaction `0x247c` compares the complete returned header.
Every nonzero SCMI status becomes EFI_DEVICE_ERROR at `0x2570`.

The endpoint requires admission, the claimed channel, doorbell magic, and status exactly zero.
It rejects unknown flag bits, reserved header bits, and non-command message types.
Lengths must be aligned, at least four, and at most 104 bytes including the header.
A bounded request snapshot is checked again before publication.
The response bytes and unchanged header are checked before FREE publication.
Malformed transport frames produce neither FREE nor pending nor a state update.
Well-formed service requests with invalid arguments receive negative SCMI status, not a successful operation.

Publication order is response payload, response length, correlated header, retained effect, FREE, then RX command/pending/IRQ.
A failed FREE write rolls back the staged effect and faults the endpoint.
A failed memory transaction prevents subsequent requests from reusing a partial publication.
The endpoint never waits or schedules deferred completion.
Its two fixed endpoint records, request buffers, response buffer, domains, and level tables need no per-request allocation.

## Base and Performance services

Discovery `0x1428..0x1728` installs Base `0x10`, Performance `0x13`, and vendor `0x80` candidates.
Base version check `0x14c0..0x14d4` accepts versions through `0x20000`.
List discovery `0x2324` requires positive progress while advertised entries remain.
The virtual Base version is `0x20000`.
Its attributes advertise one protocol and one agent.
The only listed protocol is Performance `0x13`. Base is implicit.
Vendor strings are `qcom-vp-virtual` and `virtual-pdp`, not OEM identities.
Messages zero through six are implemented. Message-attribute queries report this exact set.

Performance version is `0x40000` with two domains and no statistics or notification flags.
The only domain capabilities are SET_LIMITS and SET_PERF, bits 31 and 30.
Domain names are `virtual-cpu0` and `virtual-cpu1`.
The following entries are explicit virtual policy, not recovered physical operating points:

| Level/index | Power | Latency | Indicative kHz | Effective Hz |
|---|---|---|---|---|
| 0 | 0 | 30 | 1000000 | 1000000000 |
| 1 | 0 | 30 | 2000000 | 2000000000 |

Each domain starts at level zero with limits min zero and max one.
Sustained frequency is 1000000 kHz and sustained level is zero.
Describe-levels returns one 20-byte entry per page with a decreasing remaining count.
Start zero returns `1 | 1<<16`. Start one returns one. Larger indices return OUT_OF_RANGE.
Every entry contains level, power, 16-bit latency/reserved, indicative kHz, and index.

Selected PDP handler `0x2000be48` confirms the 20-byte layout and one-entry pagination.
Handler `0x2000bdba` confirms two domains. Handler `0x2000bdf4` checks domain zero or one.
The virtual implementation does not copy physical capability flags `0xea000000`.
Actual counts use runtime descriptors at `0x2001e1d8` through `0x20008108`.
Sustained rates use indirect helper `0x2000cce0`, so complete physical operating points remain unproved.

ClockDxe initialization is SM8845 `0x4ae0..0x4d2c`, approximately +0x20 on SM8850.
It calls DESCRIBE_LEVELS first with capacity zero, then allocates count*20 and repeats.
It multiplies indicative kHz by 1000 for its visible frequency table.
Fastchannel discovery failure leaves that table installed.
Set-level fallback is SM8845 `0x505c..0x5098`, approximately +0x40 on SM8850.
Get-level fallback is `0x4f64..0x4fc4`.
This implementation therefore refuses fastchannels but supplies real retained set/get effects.

LEVEL_SET retains a supported level within current limits.
LIMITS_SET retains valid limits and clamps the current level into that interval.
LEVEL_GET and LIMITS_GET read the same retained state.
`PerformanceResource::performance_state` exposes that state and its effective Hz to host consumers.
Unavailable or invalid-domain reads return false and preserve the caller's output.
No requested level changes CPU instruction execution, architectural timer frequency, or interrupt counts.

Bad request sizes return INVALID_PARAMETERS `-2`.
Unknown domains return NOT_FOUND `-4`.
Unsupported levels, invalid limits, and out-of-range indices return OUT_OF_RANGE `-5`.
Unknown commands and protocols return NOT_SUPPORTED `-1`.
Performance messages nine, ten, and eleven, vendor `0x80`, Clock `0x14`, and Reset `0x16` have no successful path.
This refusal matches the selected consumer fallback, not proof that every later kernel accepts these omissions.
CPUCP remains the sole reset owner for its real RV64 execution.

Standard layout references:

- <https://github.com/torvalds/linux/blob/master/drivers/firmware/arm_scmi/base.c>
- <https://github.com/torvalds/linux/blob/master/drivers/firmware/arm_scmi/perf.c>

## IRQ evidence boundary

MailboxDxe registration `0x4760` loads the first interrupt word.
At `0x478c`, both words pass unchanged through `0x2894` to the framework registration method.
Enable `0x4798 -> 0x2874` passes the same first word.
No +32 operation appears in that selected chain.
ArmGicDxe `0x1fc0` treats its source as an absolute GIC INTID.
The remaining framework provider is not decoded.

The helper proposes absolute INTID66 and binds `&apss_gic.spi_in_34`.
`platforms/qcom/apss-gic.lua` exposes `arm_gicv3` SPI inputs.
Existing runtime helpers convert absolute GSIV to SPI index with `gsiv-32`.
The selected firmware's final framework translation remains unproved.
Capture actual GIC enable/pending registers before claiming IRQ closure.

## Integration

`src/platform.cc` registers the common TX model, the shared RX model, and the virtual PDP endpoint.
Both runtime profiles compose `pdp-scmi.lua` after DDR, the APSS GIC, and `tme_image_admission`.
The endpoint and performance-resource object are both `pdp_scmi`.
A host clock consumer can resolve that object and cast it to `qcom_vp::PerformanceResource`.
Read `performance_state(domain, output)` rather than deriving a second frequency state.

The helper requires one existing writable RAM owner for `0x81f08100+0x100`.
It creates one narrow address-translator alias to that same owner for the endpoint's memory router.
It never overlays DDR or adds another copy of either slot.
It rejects overlapping MMIO owners and duplicate instance names.
No new CPUCP mappings or runtime entrypoints are part of this helper.

## Verification boundary

The native `pdp-scmi` fixture connects the actual observer, mailbox models, service, shared memory, and GIC.
It supplies synthetic protocol responses, not genuine image authentication.
The valid path changes only the two FREE words during admission.
Failed authentication stages and a missing release leave both slots unchanged.
The separate CPUCP slot retains its sentinel throughout all service operations.

The guest checks Base discovery, bounded pagination, both performance domains, level retention, limits, and clamping.
Both endpoints observe the same domain state.
Unsupported services and malformed service payloads preserve that state.
Separate scenarios reject unadmitted requests, invalid doorbells, pending-channel reuse, oversized transport lengths, reserved header bits, and requests against FREE slots.

Both channels retain pending completion before either clears.
Masking preserves pending bits, and each W1C operation preserves the other channel.
The configured line reaches GIC pending word two, bit two, which is INTID66.
This proves the model binding, not the undecoded UEFI framework translation.

The selected UEFI's enumeration, fastchannel fallback, and native image admission remain unobserved.
UEFI service success alone cannot prove kernel boot.
Actual Linux console output remains required.

## Selected post-DDR records

This section records static coverage, not native admission or execution.
XBL hashes are SM8850 `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`
and SM8845 `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4`.
Addresses in this section are XBL ELF virtual addresses unless stated otherwise.

| Record | SM8850 | SM8845 | Security type | Output |
|---|---|---|---|---|
| PDP_CDB | `0x148eccb8` | `0x148eac78` | `0x9c` | Guest-loaded `0x813cc000+0x5d` |
| PDP | `0x148ecd40` | `0x148ead00` | `0x91` | Guest-loaded PDP segments starting at `0x81300000` |

Both records have load=1, authentication=1, optional=0, execute=0, and jump=0.
All pre, post, cancel, execution, metadata, and jump callback pointers are null.
The synchronization field at record `+0x10` is zero on SM8850 and one on SM8845.
Do not interpret that OEM difference as permission to skip authentication.
The common walker copies this field into context property `0x13`.
It copies the load flag at `+0x18` into property `0x14`.
It converts authentication at `+0x1c` into the inverse skip-auth property `0x0f`.
These operations occur at SM8850 `0x148293b0`, `0x148293d8`, and `0x148294d8..0x1482950c`.
The pipeline returns each nonzero stage result.

Both records whitelist `[0x80000000,0xd8029000)` and `[0xd8089000,0x100000000)`.
These ranges permit loading. They do not create backing memory or authorize host preload.
The CDB image hashes are SM8850 `757abacb474b87d9b7bf3f5fb5a0a1572206f0d46514dfe0361cf9b7797c3754`
and SM8845 `da095eaefc26432add360d01edc9c2cf269b72652bf711d69bb5c970b7bfe0f7`.
Their single load-segment payloads are identical.
The payload starts with word `0x0003dbcf` and count three.
The three 16-byte records start at `+0x10`, `+0x20`, and `+0x30`.
Their first words are `0x0e675698`, `0x7ac09099`, and `0xb47a3c0b`.
No recovered consumer establishes these values as AOP resource IDs.
The CDB remains a guest-produced image, not an injected command database.

### AOP dependency

The PDP image contains a concrete AOP publication consumer on both OEMs.
SM8850 Thumb function `0x81301454..0x8130148e` reads header `0x0c3f0000`.
SM8845 uses `0x81301678..0x813016b2`.
Each computes the marker address as `header[0]+0x0c300000`.
Each polls that address for `0xa0c00c1e`, then returns `header[3]`.
The timeout path returns zero.
The selected AOP producer publishes the command database pointer at header `+0x0c`.
It publishes the cookie at `0x0c3f0040` last.
The database pointer is `0x81c60000`, with lengths `0xb40` and `0xb00` for SM8850 and SM8845.
The AOP model owns this publication after genuine image `0x21` admission.
The PDP service must not create another header or an unconditional ready cookie.

These are APSS publication addresses in the ELF load view.
PDP's vectors link its private execution view at `0x20000000`.
They do not establish a CPU entry at `0x81300000`.
The current bounded SCMI service replaces neither XBL loading nor AOP authentication.
It publishes availability only after the existing image `0x91` handoff observer succeeds.
No direct SOCCP request occurs in either PDP record's callback fields.
Record order alone does not prove an image-internal SOCCP service dependency.

### Exact unresolved observations

- Stop the walker at SM8850 `0x148292d4` or SM8845 `0x148273e4`.
  Select the record addresses above through `x19`.
  Observe synchronization property `0x13` and the subsequent authentication result.
  A failed result must stop that image pipeline and must not publish PDP availability.
- Observe guest writes to `0x813cc000..0x813cc05c`.
  Stop its first PDP reader and record the offset, record count, and lookup result.
  A missing CDB must remain missing until the loader authenticates and writes it.
  The smallest candidate substitute is a reader for the actual guest-produced CDB.
  The hash-to-resource meaning remains unproved.
- Stop the AOP consumer at SM8850 `0x8130146e` or SM8845 `0x81301692` in the load-address view.
  Observe `r4`, its marker word, and header `+0x0c`.
  Cookie match must return the guest-produced database pointer.
  Timeout must return zero.
  If this code executes in the linked private view, resolve its PC from the loaded segment mapping first.
- Observe slot `0x81f08100/0x81f08180` status `+4`, header `+0x18`, and the TX doorbell.
  Admission may change only FREE. Only an accepted later request may publish payload, FREE, and pending.
  Capture GIC enable/pending state for absolute INTID66 to resolve the remaining framework translation.
- If PDP requests SOCCP service, capture the first write outside its loaded segments and shared SCMI slots.
  Correlate that write with the current PC and the SOCCP queue descriptor before adding a service.
  No current static callback requires a fabricated SOCCP reply.

Existing admission, mailbox, slot, and SCMI models already implement all recovered effects in this scope.
No additional remote-success path follows from these records.
Native acceptance still requires the real loader, AOP publication, PDP admission, and correlated consumer requests.

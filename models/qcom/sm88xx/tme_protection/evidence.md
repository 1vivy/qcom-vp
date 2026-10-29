# TME protection evidence

## Observed interface

The model key is `qcom,sm8850-tme-protection`.
The supplied TME ROM accesses banks at `0x05f00000` and `0x00c28000`.
It also writes the separate mode register at `0x05fd0000`.
The reset profile declares 16 regions, 32 address bits, and 4 KiB alignment.
The identification word at offset zero reports the region count minus one in bits 25:16.
ROM `0x20cf8c..0x20cfa0` extracts that field before it enumerates regions.
Run `run-sm8845-functional-controls4` reached this read through bank two.

The public [Qualcomm TF-A XPU3 driver](https://github.com/qualcomm-linux/trusted-firmware-a/blob/f7787c3627e200933f02b93b1c25b8eb5bcf797d/drivers/qti/accesscontrol/xpu/xpu3.c)
encodes the region count minus one in `IDR0[25:16]`, which is the field that the TME ROM extracts.
That revision also represents region permission as a mask with one bit for each requester domain and a separate secure bit.
Those files carry BSD-3-Clause identifiers.
They do not define this controller's offsets, which the selected ROM establishes.
They do not establish the TME's protected address paths, requester identities, or permission-bit positions.

## Functional behavior

The model retains mode values zero and `0x0a`.
The ROM writes `0x0a` at PC `0x20d0ca` and checks its readback.
Region activation now enables permission checks instead of stopping the platform.

Each enabled region describes a half-open address range.
Read and write permissions use separate words.
For QAD zero, bit 31 admits secure transactions and bit 30 admits nonsecure transactions.
Secure QAD-zero transactions can also consume nonsecure policy.
Other QADs require their own permission bit, regardless of transaction security.
The shared encoding bit zero does not independently authorize a data access.
The declared domain count controls the permission mask.
The original seven-domain profile uses bits 6:0.
The eleven-domain profile also uses bits 10:8.
Domain seven has no permission bit.
The ROM table at `0x212fd4` contains permission classes `0x80000001`, `2`, and `4`.
The SDX75 analogue names these fields `RDA_S`, `RDA_NS`, `RDA_QAD`, `WRA_S`, `WRA_NS`, and `WRA_QAD`.

The platform assigns virtual domain zero to APSS and CPUCP.
It assigns virtual domain one to TME.
The APSS aliases preserve the TME address view before they check permissions.
The ports preserve the transaction's security attributes.
These assignments describe the virtual platform, not captured silicon identities.

Every matching region must permit an access.
This intersection rule is a conservative virtual policy, not a claim about physical region precedence.
The model retains configuration ownership state.
The board exposes its configuration registers through the TME router.
The APSS aliases expose shared memory, not the configuration registers.

The configuration lock prevents changes to the region bounds, enable bit, and access permissions.
The ROM writes the ownership word after this lock, so that word remains writable.
Permission changes invalidate existing direct mappings.
Restricted regions use transactions because a QEMU read-only RAM alias silently discards writes instead of reporting a permission fault.
Configuration callbacks queue CPU invalidation before the configuration transaction returns.
The receiving CPU processes invalidation through its existing QEMU callback.
This sequence does not claim instantaneous revocation across all running CPUs.

QBox installs direct mappings without a security attribute in the shared ARM memory root.
APSS therefore requires nonsecure read and write permission before it grants a direct mapping.
Secure-only regions retain native instruction and data access through transactions.

## Client-relative apertures

The platform declares a `0x100000`-byte TCSR client aperture at system address `0x01f00000`.
Selected consumers read `0x01fc8000` and `0x01fc8004`.
The aperture base and size are platform declarations, not recovered permutations of hardware geometry.
The TME view starts at `0x05f00000`.
The protection registers describe offsets within that aperture.
For example, region offset `0xd0000` selects the mode register at TME address `0x05fd0000`.
The platform now declares these bases on each TCSR protection port.

The virtual board places the RPMH MPU at AOSS offset `0x01292000`.
AOSS starts at system address `0x0b000000` and spans `0x04000000` bytes.
Those three values are platform declarations.
This review found no independent record of them, and the denied region below establishes only relative bounds.
Its TME view starts at `0x0f000000`.
The private reconstruction declares these client apertures for the runtime controller.
It does not treat the controller's relative bounds as absolute CPU addresses.

The previous wiring denied APSS ROM execution at `0x30f2dc`.
The active RPMH region covered relative offsets `0x2e4000..0x310000`.
The source map places modem PDC registers in that range, not APSS ROM.
The corrected run passes that failure and reaches a later TME configuration write.

An optional `protected_size` selects a client aperture.
Zero retains the complete-address-space behavior.
The port normalizes only permission checks and preserves the forwarded transaction address.
It restricts direct mappings to the applicable side of each aperture boundary.
Thus an unprotected neighboring access cannot grant direct access through a protected range.

## Regression evidence

The actual guest writes and reads RAM before it enables protection.
The guest uses the advertised region count to select the final region.
It then permits reads, denies writes, and locks the region.
An attempted permission change does not remove that lock.
The guest can still read the original value.

Its next RAM write returns a permission error at `0x14800000`.
This sequence also checks revocation of the earlier direct mapping.

The configured half-open range is `[0, 0x10000)` relative to the aperture at `0x14800000`.
The guest writes and reads the neighboring words below and above that aperture.
Neither access removes the protected range's write restriction.

The wide-address case uses RAM at `0x114800000` and an unscoped protection port.
The guest derives its address mask from IDR1 before it programs the range.
It checks neighbor access, locked permissions, and revocation of the previous direct mapping above 4 GiB.
The incorrect address-MSB encoding let the forbidden write continue to the guest's failure marker.

```sh
cargo test --locked --test platform protection_revokes_existing_write_access -- --ignored
```

The integrated run passed this test.
The current access monitor stops the platform on the denied transaction.
This result does not prove architectural exception delivery or physical fault-syndrome registers.

## Fidelity limits

The implementation does not claim physical requester assignments, arbitration timing, or production isolation qualification.
The mode register retains the values that the ROM uses.
Its other silicon effects remain outside this model.
These limits do not require a blanket rejection of ordinary region activation.

## Runtime geometry and debug policy

OEM `0x4268ce..0x4268fa` reads IDR0, IDR1, IDR2, and REV.
The model reports its declared address widths, alignment, region count, domain count, and revision.
OEM `0x425c72..0x425ca0` consumes the domain count directly.
Its table at `0x443730` defines the permission bits for eleven domains.
The observed OEM configuration requires mask `0xc000077f`.
The original ROM profile retains seven domains.

IDR1 bits 13:8 contain `ADDR_MSB`, not an inverted address width.
The SDX75 header names this field at lines 87 and 88.
OEM `0x427834..0x427868` uses this field to mask both bounds before it programs a region.
The model reports `address_bits - 1` and accepts widths from one through 63 bits.
The previous expression gave the correct result only for 32-bit addresses.

The live OEM table at `0x45b2b8` references a range that ends at the exclusive 64 GiB limit.
The private controller at local address `0x05540000` therefore declares 40 address bits.
The corrected run passes its activation at `0x055418c4` and reaches the next runtime API request.

DBGAR at offset `0x304` retains the declared external-debug permissions.
The SDX75 header defines the security bits and the seven-domain mask at lines 176 through 191.
The extended profile applies its declared domain mask to this word.
DBGAR does not override the normal memory-region permissions.
The sequencer uses real DMA writes and readback for command `0x25`.

The private runtime profile supplies additional controllers from the OEM tables.
One observed table starts at `0x4555e4` and contains 50 descriptors.
Its first descriptor names system address `0x07f20000` and TME address `0x0bf20000`.
Most instances declare 64 regions, eleven domains, and virtual revision `4.0.0`.
These values describe the virtual capacity, not captured physical identification registers.
The additional controllers have no connected virtual data clients.

OEM `0x4269b8` reads resource 258 at `0x130050b0` and resource 93 at `0x05f02770`.
These accesses require larger register apertures.
The two private instances expose all 1024 resource indices from IDR0's ten-bit count field.
The model allocates each instance's declared region array once during elaboration.
The guest checks activation and locked permissions at index 1023 without changing index zero.

## Fixed-resource interface

The separate model key is `qcom,xpu-fixed-resources-v1`.
OEM `0x426102..0x426178` omits address programming when IDR0 selects fixed resources.
The model reports type one and retains activation, permissions, ownership, and locks.
It does not require a programmable address range for activation.
Its data-authorization interface denies accesses because no fixed-resource client mapping exists.
It cannot silently authorize a new data path.

OEM `0x4266b4` reads 16-byte records for fixed resources and 32-byte records for programmable address ranges.
The live configuration table at `0x45b2b8` contains four arrays with the fixed-resource layout.
Their system addresses are `0x002f0000`, `0x010d0000`, `0x0f000000`, and `0x20c2a000`.
The private profile selects the fixed-resource model for these four controllers.
The configuration format provides this evidence, not a captured silicon identification register.

The array at `0x458fa8` instead uses programmable byte ranges, including `[0x24,0x40)`.
Its controller at `0x34cb0000` requires zero alignment bits.
Declaring page alignment erased both bounds and incorrectly rejected region activation.

The integrated suite passed 48 tests.
The protection guest checks the eleven-domain mask and retains its denied-write and configuration-lock checks.
The sequencer guest checks fixed-resource activation, immutable locked permissions, and actual DBGAR changes.
The runtime guest changes the CPU requester domain and checks revocation of the previous direct mapping.

The original boot path requires domain one. The OEM-private ranges require domain two.
The private compatibility adapter selects that transition explicitly.
It does not add both identities to one permanent permission mask.

Runtime image hashing reads its residual buffer through system address `0x20411574`.
The previous sequencer ports retained domain one after the TME CPU entered domain two.
Both OEM runs then reported a denied DMA read from that buffer.
The runtime profile now changes the sequencer's global, runtime, and local ports with the same phase signal.
This virtual requester policy changes no region permission bits, security attributes, fuse values, or image checks.
APSS and UFS retain their separate requester domains.

Both OEM runs then passed the protected DMA reads and completed further SHA-384 operations.
SM8850 stopped at PC `0x435cf2`. SM8845 stopped at PC `0x435d4a`.
Both stops report the unresolved runtime import at `0x416aac`, which corresponds to API offset `0x2ac`.
This result does not prove image authentication or kernel execution.

The runtime signature emitter supplies local digest address `0x4077b0`, not system address `0x204077b0`.
Both address views now enter one router before the sequencer's local protection port.
They share the existing TME backing and the same requester identity.
The local view covers `[0x400000,0x45c000)`.
The system view covers `[0x20400000,0x2045c000)`.
Neither view bypasses the local controller or creates a second RAM object.


## Global programming mode

GCR is at offset `0x100`.
OEM `0x426aac..0x426b66` reads bit zero before it selects a region-programming operation, so bit zero gates that path.
The source-only names `APNSPE` and the dynamic-clocking meaning of bit one remain unestablished.
The model retains bits zero and one. Other bits remain errors.
The model rejects clearing bit zero after it is set.

### Original OEM witnesses

The source is CPH2749 `IMAGES/signed_firmware_soc_view.elf`.
Its SHA-256 is `4258fc24317e3467b39144ab72a46d85e4494c80bddc37b14de16177ff0c4a19`.
The segment at file offset `0x1000` loads at system address `0x2041c000`.
The local TME view subtracts `0x20000000`.
Thus programmer `0x4264ec` starts at file offset `0xb4ec`.
Its bytes through `0x4266b4` match the retained OEM RAM witness.
That witness is `tme-oem-configuration.ram`, mapped at local address `0x400000`.
Its SHA-256 is `aca310644510807f718a8442a5db0ef098e330580d648d9b116bddf471595084`.
The model contains no vendor bytes.

OEM dispatch `0x426aac..0x426b66` reads GCR bit zero.
The namespace branch rejects a nonzero write-once request at `0x426b34`.
It requires a nonzero domain-lock request at `0x426b50`.
It then calls namespace programmer `0x4264ec`.
The programmer searches active ranges through helper `0x425eba`.
That helper reads activation at region offset `+4`, bounds at `+8..+0x14`, and permissions at `+0x18/+0x1c`.
The search can reuse an exact range or extend an adjacent compatible range.
It uses firmware shadows when it compares the requested permissions.
It does not access another hardware region bank.

Namespace programmer `0x426598` calls common programmer `0x42617a`.
Common programmer `0x4261c2` calls register writer `0x426102` when the region requires initialization.
The writer uses the existing `0x40` stride and offsets `0x1000..0x1030`.
It omits bounds for fixed-resource controllers.
It writes the high bounds only when the declared address width requires them.
It writes permissions before activation and applies ownership through helper `0x426244`.
Ownership helper `0x426244..0x42626e` writes QADRGL at region offset `+0x30`.
Reader `0x426282..0x4262b2` checks the caller's capability bits in that word.
Release helper `0x4262c6` clears those bits and checks readback.
Its APNS branch at `0x426302..0x42630a` skips the legacy explicit activation-clear sequence.
This branch alone does not prove automatic hardware deactivation.

OEM `0x42770c` updates packed two-bit entries through software pointers.
Reader `0x427652` interprets those entries as read/write permission shadows.
They are firmware RAM state, not new MMIO registers.
The model does not invent bitmap-register aliases.

### APSS witnesses and capabilities

The APSS source is the CPH2749 XBL image identified below.
XBL `0x14859db4..0x14859ddc` reads GCR, writes literal one, and verifies bit zero.
The corresponding SM8845 sequence is `0x148582e8..0x14858310`.
No revision or fuse value changes to skip these sequences.

XBL writer `0x1485a854..0x1485a920` uses the same bounds, permissions, activation, and write-once registers.
Helper `0x1485b484..0x1485b4f8` changes QADRGL ownership.
Its unlock request writes the caller capability minus the requested release bits.
Its partial lock request preserves existing caller bits before it adds the requested bits.
It does not authorize modification of another caller's owner bits.
Helper `0x1485a780` checks ownership readback and then inspects activation.
Replacement path `0x1485b018..0x1485b054` explicitly clears activation only after all owner bits are zero.
Therefore the model retains activation when ownership changes.

XBL capability getter `0x1485b644` supplies domain identifiers zero and seven to `0x1485b680`.
That mapping produces `0xc0000001`.
The SM8845 getter is `0x14859b78`.
OEM configuration getter `0x427616` maps domain two to mask `4`.
SM8845 uses getter `0x427552`.
Ownership writer `0x426244` and reader `0x426282` use this getter directly.
Getter `0x4275b8` instead maps domain zero to `0x80000001` for software permission shadows.
It does not supply the configuration-owner mask.
The configured capability follows the ownership operations, not an inferred data-QAD transition.
Secure transaction attribution does not add ownership bits to either capability.

XBL `0x1485b810` reads permission shadows through descriptor pointers.
Writer `0x1485b880` stores those shadows in RAM after hardware programming.
For example, the descriptor for resource `0x73` uses XBL BSS addresses `0x14a14628/0x14a14634`.
These addresses do not identify hardware registers.
The SM8845 counterparts are writer `0x14858d88`, ownership helper `0x148599b8`, and shadow writer `0x14859db4`.

### Namespace revocation evidence

Both XBL images map logical domain zero to `0x80000001` and domain seven to `0x40000001`.
The SM8845 mapper is `0x14859bb4`. The SM8850 mapper is `0x1485b680`.
Their shared bit zero is not an independent permission class.

SM8845 `0x148589c8..0x148589e4` computes the release mask from the requested read and write permissions.
Its release wrapper at `0x14858cb4` changes QADRGL, then updates software shadows.
It does not directly write the hardware permission words.
Validator `0x14858be0` checks the actual permission readback.
The corresponding SM8850 functions use an address delta of `0x1acc`.

SM8845 run22 captured the failing TCSR region 25 transition.
The active range was `[0xd9000,0xda000)`, with write-once state zero and foreign owner `4`.
APSS wrote ownership value `0x80000000`.
XBL expected read permission `0x80000035` and write permission `0x80000011`.
The former model retained bit 30 in both words, so the original validator returned seven.
The later scratch query at `0x01fd400c` reported this failure rather than causing it.

SM8845 run24 exposed a separate consumer requirement before namespace activation.
Secure XBL at `0x1485ed6c` read TCSR offset `0xd504c`.
Region 21 had read and write permissions `0x40000025`, owner `4`, and GCR zero.
Thus a secure-only selector rejects a required access to nonsecure policy.
The functional consumer permits this access without permitting the reverse security transition.
This observation establishes firmware demand, not a physical measurement.

SM8850 run26 captured the later IMEM ownership transfer.
APSS programmed region six at TME controller `0x05520000`, then acquired ownership `0x80000001`.
TME acquired ownership `4` while read permission was `0x80000005` and write permission was zero.
APSS then released its complete ownership.
The original TME consumer skipped direct programming and required the previously acquired policy to remain effective.
See [the complete producer and consumer contract](../../../../docs/qcom/sm88xx/tme-runtime-fatal-snapshot.md).

SM8850 run28 exposed a separate configuration-view requirement.
Global readback of TME's retained policy let APSS merge the metadata and payload into `[0x1480e000, 0x14821000)`.
The later exact-range release required `[0x14810000, 0x14821000)` and returned `0xf049`.
Configuration readback must not expose another authority's retained policy.
Data access still requires the complete effective policy.

### Implemented state and security boundaries

Both configuration sockets share the original region array, GCR, RSR, and invalidation callbacks.
The primary socket uses explicit `config_owner_mask`.
The optional `apss_config_socket` uses explicit `apss_config_owner_mask`.
Both masks default to zero, which leaves namespace writes unauthorized.
The profile must declare disjoint masks that fit the instance's permission geometry.
The selected secure profiles use masks `4` and `0xc0000001`, respectively.

Namespace writes require secure transaction attributes.
The primary socket can attribute unspecified transactions as secure only through explicit `config_secure_unspecified=true`.
The APSS socket has no such fallback.
Explicit nonsecure requests fail closed.
An unproved nonsecure namespace capability remains unsupported.
The model does not derive authority from QEMU CPU numbers or requester identifiers.

In namespace mode, an APSS QADRGL write changes only bits in the APSS capability.
A TME claim also changes only the TME bit.
A TME release of an owned region frees the region for every owner, as [the image-range section](#primary-release-of-image-ranges) describes.
New namespace ownership retains the current read and write policy for that configuration authority.
This acquisition cannot add permission because it retains only access that already exists.
Legacy ownership does not create this retained policy when GCR enables namespaces.
A qualifier-only write can retain an acquired security class, but cannot acquire a new class without bit zero.

A release revokes its logical classes from the base policy and that authority's retained policy.
Releasing all ownership also clears that authority's retained policy.
The shared bit zero remains while either security qualifier survives in that permission word.
Permissions retained by another namespace owner remain effective.

Configuration readback includes the base policy and the caller's retained policy.
It also includes a handoff seed that the caller programmed, while the other owner retains that seed.
Data authorization uses the union of the base policy and both retained policies.
Repeated ownership writes never restore revoked permissions.

An APSS release preserves bounds, unrelated permissions, and foreign owners.
A nonzero owner word blocks direct changes to bounds, permissions, and activation.
This configuration-lock policy blocks direct reprogramming until all owners release their locks.
Explicit disable and subsequent reprogramming remain available after all owners release their locks.
Unchanged locked-register readback remains unchanged, not synthetic successful programming.

Write-once requests in namespace mode return an error.
Existing write-once locks survive GCR activation.
A release that would change locked permissions returns an error before changing any state.
Legacy register programming keeps its existing lock behavior.
Fixed-resource data authorization remains denied.
The normal intersection policy continues to check every matching enabled range.
RSR derives its free count and first index from the same region activation state.
GCR and ownership changes synchronously invoke the shared invalidation callbacks.
Existing activation, range, and permission changes retain their invalidation behavior.
Unknown registers and malformed transactions remain errors.

Logical revocation, shared policy retention, and empty-region reclamation are functional substitutes, not complete physical namespace specifications.
Physical permission provenance, nonsecure configuration rules, and dynamic-clock timing remain unproved.
A final release deactivates a region only when no owner or effective permission remains and no write-once lock exists.
Foreign ownership, nonempty policy, and write-once activation remain protected.
No new compatible key is required because this change adds a supported feature to the same register owner.

### Native verification

The namespace guest checks both configuration sockets with their independent capabilities.
It verifies foreign-lock retention, logical revocation, repeated acquisition, final-owner release, empty-region reclamation, and RSR changes.
Nonsecure read and write attempts fail after revocation.
Removing both security classes also revokes the earlier secure direct mapping.
The guest checks write-once rejection and secure access to retained nonsecure policy.
Separate QAD-two accesses cannot borrow the secure APSS permission.

Undeclared authority and nonsecure configuration writes fail at their actual register addresses.
The legacy protection regressions continue to verify denied data writes and direct-mapping revocation.

Cases 16 and 17 configure secure-only read and write permission.
Each guest writes and executes a native routine in protected RAM.
Each then uses native `eret` without another policy change.
The subsequent nonsecure load or store fails at `0x14800000`.
The complete suite passes 94 tests, including both cases and the existing revocation cases.
SM8850 run135 also executes the original firmware through the PMIC lock helper.
Its lock helper still returns error `0x80` because of a separate SPMI owner restriction.

The aperture guest preserves an encoded zero endpoint at an explicitly declared finite limit.
It reads protected data, writes both neighboring words, and observes rejection of a protected write.
The full-aperture case also protects the first word.
Inverted nonzero bounds and endpoints beyond the declared aperture remain errors.
See [the platform evidence](namespace-platform-evidence.md#finite-client-apertures) for both firmware tables and the virtual endpoint rule.

SM8850 run29 and SM8845 run27 pass the original `DP_Milestone` callback.
Both retain the genuine debug-policy denial.
Both complete `sbl1_xblconfig_init` and pass the former TME permission and range failures.
SM8850 next reads the missing timer view at `0x17812004`.
SM8845 completes `boot_pre_ddr_dtb_load`, then reaches a rejected TSENS control write at `0x0c222004`.
These runs do not establish UEFI, ABL, or kernel execution.

### Native namespace reclamation

SM8845 run104 exposes the partial SHRM grant at controller `0x310c4000`, region zero.
Its range is `[0x31020000,0x31039000)`.
The original allocator later needs code, data, and startup ranges within its three-region cohort.
The model does not enlarge that cohort.

APSS claims namespace `0x40000001`, and TME claims namespace `4`.
APSS then writes `0x80000000` at `0x14859a2c` to release its nonsecure class.
That value is the caller capability minus the released class.
The earlier model incorrectly acquired a previously absent secure qualifier.
A qualifier-only release now preserves only qualifiers that already exist.
An explicit full-class claim still acquires its qualified ownership.

SM8845 run106 verifies owner readback `4` after APSS release and zero after TME release.
The original TME reads these words at `0x42624a`.
The later read at `0x425f1e` still sees activation one, which prevents allocation.
The retained capture is `build/private/run-sm8845-common-runtime106/reclamation-probe.json`.

Original TME helper `0x426264` releases ownership without a separate namespace disable.
Its legacy path can write disable, but its namespace path omits that write.
The virtual controller therefore retires an unlocked region after the final release removes every effective permission.
RSR and direct-mapping invalidation use the resulting activation state.
This is a functional reclamation policy, not a measured silicon specification.
It does not change image-authentication results.

The integrated suite passes all 94 tests, and Clippy passes with warnings denied.
SM8845 run108 passes the former three-slot SHRM allocation failure.
Its next request accesses controller `0x310c0000`, region 84, outside that controller's separately declared geometry.
This progress does not establish complete PIMEM initialization or kernel execution.

### Primary release of image ranges

Both packages load PDP_CDB at `[0x813cc000,0x813cd000)`.
That range is inside the PDP span `[0x81300000,0x813e1000)`.
XBL `0x14878220` joins all eight PDP load segments into one range and bridges their gaps.
The XBL APSS policies for image `0x91` at `0x148e6de8` and image `0x9c` at `0x148e6e00` have flag zero.
XBL therefore keeps both ranges on DDR controller `0x34c60000` after authentication.

TME finalizer `0x42b776` calls `0x438c20`.
That routine calls `0x438b56` for each authenticated range.
`0x438b56` releases domain two through API slot `0x830`.
No decoded XBL, TME, or CPUCP path releases the APSS owner bits of these two ranges.

QSEE loop `0x15762064` reads each region of the controller.
Gate `0x15762090..0x1576214c` imports a region when its owner word contains bit 0 or bit 28.
It skips a region whose owner word has neither bit but whose policy grants one of them.
Overlap check `0x15764c44` returns two for a partial overlap.
QSEE `0x15764f70` then returns error `0x2a`, and QSEE stops in `0x15759b20`.

The former model kept APSS owner `0x80000001` after the TME release.
QSEE imported the PDP_CDB region first and rejected the PDP region.
A debugger stop in a temporary SM8850 run confirmed the stop at `0x15765080`.
At that stop, the QSEE assignment list `0x1580cab0` contained no range that covers the PDP span.

The model now applies this rule:

1. A TME release of a region that TME owns frees the complete region.
2. The owner word becomes zero.
3. The model revokes the classes of every previous owner from the base policy.
4. The model clears the retained APSS policy.
5. An unlocked region with no remaining policy then retires.

These firmware contracts agree with the rule:

- XBL release check `0x1485a7d4..0x1485a7e4` accepts APSS bits that are already absent.
- XBL reuses its temporary image region after its own release, without an activation write.
- TME allocation `0x425f6a..0x425f76` needs a disabled region, and its namespace release omits the disable.

Four other rules stopped genuine firmware before QSEE:

| Rejected rule | Result |
| --- | --- |
| A TME claim replaces the owner word | TME reports `0xf049` at `0x426c10`, or XBL reports `0x9c001008` from `boot_seclib_s.c` line 594 |
| A TME claim also revokes the APSS policy | TME enters its fatal path |
| Each release to a zero owner word retires the region | XBL writes a nonzero value to `0x17960020`, and the CPU-domain model rejects it |
| A TME write without its bit clears the owner word, also when TME is not an owner | XBL or TME stops before QSEE |

This rule is a functional inference from firmware contracts.
It is not a measured silicon specification.
SM8850 run246 and SM8845 run199 passed the QSEE memory assignment.
Both packages then stopped when the QSEE crash path read `0x19830320`.

### Handoff seed readback

The crash in run246 came from QSEE SMEM validation `0x157337a8`.
That routine assigns each SMEM partition through `0x15733ca0`, `0x1575c348`, and `0x15768e44`.
Check `0x15768e44` requires the partition range in the managed list at `0x1580c650`.
The QSEE import loop `0x15762064` fills that list from the controller regions.

XBL programs region 14 at `[0x81db0000,0x81db3000)` with read and write policy `0x24`.
XBL does not claim that region.
At the end of XBL, TME claims regions 7, 13, 14, 15, and 16.
The handoff rule moves the unowned policy of region 14 into the TME retained policy.
The former APSS readback was zero, so QSEE did not import the region.
The partition at `0x81db0000` then failed with error `0x3d`.
QSEE logged "TZ Permissions assign failed" and sent SGI 13 to its crash handler `0x1567d988`.

Region 12 at `[0x81db5000,0x81de5000)` has policy `0x20` and no owner.
QSEE imports it from that readback.
A debugger capture in a temporary SM8850 run showed this difference in the managed list.

The APSS view now also includes a handoff seed that APSS programmed, while TME retains it.
The TME view, the data policy, and the TME release are unchanged.
A later permission write clears the seed.
The seed has no effect on co-owned regions.
Thus the run28 restriction on foreign retained policy remains in effect.

SM8850 run247 and SM8845 run200 pass the SMEM validation.
Both packages then stop when QSEE reads `0x00189000`.

## XBL protection cohort

The input is the CPH2749 package's `IMAGES/xbl_sc.elf`.
Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
The following addresses are ELF physical addresses.

XBL `0x14856a94..0x14856aa0` selects the configuration root at `0x148e3298`.
The alternate root at `0x148e32b0` selects the same controller table.
Both roots contain a pointer at offset `0x10`.
Those pointers select `0x14925740` and `0x14925918`, respectively.
Both structures contain table pointer `0x14925af0` at offset `0x38`.
Both contain count pointer `0x148e3460` at offset `0x40`.
The count is 33.
The reader at `0x14859328` follows these pointers.

Lookup `0x1485b8c8..0x1485b924` advances by `0x28` bytes.
Each record contains the system base at `+8`, configuration base at `+0xc`, and resource identifier at `+0x10`.
The two bases are equal in all 33 records.
The helper `platforms/qcom/sm88xx/sm8850/xbl-protection.lua` records every resource identifier and base in table order.
The record for identifier `0x23` is at `0x14925c08`.
It selects base `0x01520000`.
The older SSC IBI2 name does not identify this current controller.

Reader `0x14859ca4..0x14859cfc` reads the revision at base offset `0xc`.
It reads IDR0, IDR1, and IDR2 at offsets zero, four, and eight.
Its masks are `0x03ffffff & 0xffff007f`, `0x3f3f3f3f`, and `0x3f1f`, respectively.
Caller `0x14858ac4` caches the revision in descriptor offset `0x20`.
Programming code `0x1485a324..0x1485a374` uses region stride `0x40`.
It accesses ownership at `0x1030`, read permissions at `0x1018`, and write permissions at `0x101c`.
Code `0x1485a3b4..0x1485a3e4` checks the lock at `0x1000` and activation at `0x1004`.
These register contracts match the existing protection model.
No second XBL register model is required.

The runtime composition already supplies 31 of these controllers.
For example, system base `0x01520000` selects the existing TME-view controller at `0x05520000`.
The helper translates APSS configuration transactions to that shared TME router.
It does not create another region array or another permission-change event.
It preserves each existing controller's geometry, fixed-resource type, revision, locks, and permissions.
It also preserves all existing client ports and requester assignments.
Configuration writes therefore affect the existing clients and invalidate their direct mappings.
The helper rejects overlapping configuration mappings instead of selecting an arbitrary target.

Only system bases `0x01d98000` and `0x17e08000` lack existing controllers.
The helper creates their shared controllers at TME bases `0x05d98000` and `0x1be08000`.
Their records have identifiers `0x67` and `0xb5`.
Both records contain a null pointer at offset zero.
No available evidence identifies their physical client aperture or resource type.
The explicit virtual substitute uses programmable ranges, 1024 regions, 40 address bits, byte alignment, and eleven domains.
Its revision is the existing virtual value `0x40000000`.
This capacity permits all indices in IDR0's ten-bit region field.
These declarations are not physical identification measurements.
No virtual data client is connected to either new controller.
Their configuration state does not authorize an otherwise absent resource.
Fixed-resource client mappings, missing physical clients, architectural fault delivery, and SMMU translation remain outside this helper.

The helper requires the existing reconstructed SM8850 runtime platform.
Call it after the runtime creates the shared controllers and packed SPMI arbiter, before elaboration.
It returns the same platform object.
The helper does not alter authentication, image data, or the runtime's security-domain transition.
Launch `bg342` stopped during elaboration because the initial helper detected an overlapping SPMI mapping.

### SPMI configuration hole

The packed SPMI aperture originally covered `[0x0c400000,0x0cd00000)`.
The cohort's identifier `0x28` selects `[0x0c41c000,0x0c41e000)`.
The SPMI model's `table()` and `transport()` functions do not decode offsets `[0x1c000,0x1e000)`.
This overlap was a composition error, not an additional SPMI register bank.

The helper replaces the broad SPMI mapping with two disjoint address translations.
They expose `[0x0c400000,0x0c41c000)` and `[0x0c41e000,0x0cd00000)`.
Both preserve the arbiter's original register offsets.
Both connect to the same existing SPMI object through its multi-target socket.
The shared XPU controller owns the excluded range.
The helper does not change SPMI routing state, PMIC state, or command behavior.
Overlap errors now identify the module, socket, bus, and both address ranges.
This investigation ran no builds, tests, or launches for this correction.

Native run `bg344` elaborated without overlap warnings.
It authenticated the sequencer and passed the previous APSS read failure at `0x0152000c`.
Its next first fault was a TME read at `0x4171cc`, with OEM PC `0x42b91c`.
That fault is at the runtime import boundary.
This run exercises the actual XBL alias path.
It does not prove programming of all 33 controllers or security coverage for additional data clients.


### SM8845 shared configuration aliases

CPH2771 XBL selects a separate 31-record cohort at `0x14920428`.
Its count is at `0x148e1580`.
The roots at `0x148e13b8` and `0x148e13d0` select this same cohort.
Reader `0x148581d8..0x14858230` uses the same revision and configuration masks.
Code `0x14858858..0x148588a8` uses the same ownership, permission, and region-stride contract.

`platforms/qcom/sm88xx/xbl-protection.lua` now implements the shared alias construction.
`sm8850-xbl-protection.lua` preserves its original 33-entry board cohort and two explicit substitute controllers.
`sm8845-xbl-protection.lua` supplies the independent CPH2771 cohort.
That cohort omits SM8850 resources `0xf6` and `0xf8`.
Each wrapper retains the firmware's own identifiers and addresses.
The helper rejects duplicate identifiers, duplicate addresses, and overlapping APSS ownership.

All 31 CPH2771 controllers already exist in the reconstructed runtime.
The helper adds no SM8845 controller state or geometry.
In particular, the existing SM8845 clients already supply resources `0x67` and `0xb5`.
APSS configuration accesses therefore reuse the same TME-view locks, permissions, and permission-change events.
The SPMI hole uses the same disjoint views of one packed arbiter.

See [the SM8845 composition evidence](../sm8845/tme_runtime_abi/evidence.md#xbl-protection-aliases-and-connected-spmi-inventory) for exact PMIC source addresses and permitted substitutes.
The reported pre-change replay fault was APSS PC `0x148581e8`, reading `0x0152000c`.
This slice used static firmware and package evidence only.
Builds, tests, actual replays, and retained UART output remain pending.

## Shared XBL DCR and immediate control words

`platforms/qcom/sm88xx/xbl-boot-policy.lua` adds the APSS view of the existing TCSR mode word.
APSS `0x01fd0000` translates to TME `0x05fd0000`, owned by `tme_protection1.mode_socket`.
The ROM write of `0x0a` at `0x20d0ca` remains the producer.
No second cold word, hard-coded getter result, or new mode encoding is introduced.
The existing zero construction policy and accepted mode values remain unchanged.
Neither construction policy nor the ROM write establishes a physical reset value.

The same helper connects three four-byte control words to their existing protection owners:

| AC resource | APSS control word | Existing TME-view region owner |
| --- | --- | --- |
| `0x84` | `0x01de4000` | `0x05de6000` |
| `0x96` | `0x07f08000` | `0x0bf30000` |
| `0xb5` | `0x17d00008` | `0x1be08000` |

`TmeProtection::policy_control_socket` retains full-word writes at local offset zero.
It uses the existing transaction validation and denies DMI.
`policy_control_reset` is required when the optional socket is connected.
The helper explicitly selects zero as a virtual cold state without retained programming.
It does not claim a physical register reset value.
No reset input is added, so a CPU reset cannot clear retained configuration.

This control retention is a bounded hardware substitute.
The selected firmware requires masked writes and exact readback.
The available evidence does not establish the controls' full physical side effects.
This state is separate from the existing `DBGAR` field.
It does not alter region locks, permissions, requester domains, authorization checks, or invalidation events.
No new virtual data client receives access through these words.
Unknown neighboring addresses remain unsupported.

Both OEM policy images supply identical immediate control descriptors.
See [the boot-control evidence](../boot_control/evidence.md#xbl-boot-mode-query-and-immediate-policy-controls) for hashes, firmware PCs, table coordinates, and native execution.
Both runtime helpers apply this group after the XBL protection aliases.
The native protection regression confirms that policy-control writes cannot unlock a region or authorize a denied access.

## Error-reporting control words

The model retains error-reporting controls at offsets `0x208` and `0x20c`.
Selected SM8850 code at `0x14859f80..0x14859fc4` reads, masks, modifies, writes, and verifies the word at `0x20c`.
Its mask is `0xc00007ff`, which covers eleven low bits and bits 30 and 31.
The source-only names `CFGERE` and `CLERE` and the `0x208` pairing remain unestablished.
SM8850 run156 and SM8845 run125 stop at this previously absent word after `SBL1, End`.

The model retains both controls in the shared controller.
Each mask contains the declared QAD bits and bits 30 and 31.
Reserved write bits clear.
Both words start at explicit virtual zero.
They do not change region permissions, ownership, locks, or debug access.

The virtual fault policy remains host logging and simulation stop.
These controls do not produce a physical XPU interrupt or fabricate syndrome state.
Unsupported diagnostic registers still fail through the access monitor.
The namespace fixture checks shared views, separate controls, write masks, and existing access-denial behavior.

SM8850 run159 and SM8845 run126 pass the reporting-control sequence after `SBL1, End`.
Both next stop in TME at missing software import `0x417238`.
These runs establish the exercised register behavior, not kernel handoff.

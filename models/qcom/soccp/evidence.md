# SOCCP boot and remote service

## Scope and substitute policy

`QcomSoccp` replaces the physical SOCCP processor with a functional remote endpoint.
It does not replace XBL, UEFI, ABL, image authentication, or the package kernel.
The real XBL loader must load and authenticate the SOCCP image before it writes the release register.
The model does not supply an EFI protocol, an XBL handoff, or a kernel result.

The selected service uses GLINK over SMEM and IPCC.
It does **not** use the selected MailboxDxe transport.
That transport lists `pdp0`, `pdp1`, and `cpucp`, not SOCCP.
The SOCCP helper does not add a MailboxDxe channel.

The helper declares these virtual battery inputs:

| Input | Policy |
| --- | --- |
| Battery count | One |
| Voltage | 8800 mV |
| Design and full energy | 11400 mWh |
| Remaining energy | 5700 mWh |
| Temperature | 250 deci-degrees C |
| Current and energy rate | Zero |
| Battery and power flags | Zero, idle |
| External power source | None |

These values are board policy, not phone measurements.
The model does not attach a USB cable, negotiate power, transfer charge, or implement a charger controller.
The PD-enable command retains its requested policy only.
It does not claim a physical negotiation result.

## Selected inputs

The analysis read these files from the selected package directory:
`CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES`.
No vendor bytes were added to this repository.

| Input | SHA-256 |
| --- | --- |
| `xbl_sc.elf` | `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a` |
| `xbl_config.elf` | `c35a368853ba57f00c9486d4b8fadeb0ded3a59ce410818a69ac415e3c44d3ef` |
| `uefi.elf` | `da8ad1b0ffc2eff91d7d34d8574a682b7007e0b052439fbbcde989d5393071f5` |
| `soccp.bin` | `3e1eecf7ef492368f58e08d3231ab8be82f508b489466442d212d63251404ad0` |
| FAT file `IMAGE/SOCCP.MBN` | `aea3445b58c7e34d1ee6fbc0d6324bf5e8719bd2d2a88803319fac6d6c7a007b` |

The UEFI ELF contains its firmware volume at file offset `0x1000`.
The PE modules were decompressed and inspected in memory.
Their addresses below are PE RVAs, not runtime addresses.

| Selected PE | SHA-256 |
| --- | --- |
| `PmicGlinkDxe` | `bd22326a89726977459db473bce8a00d7f7632d527882a77f12d4eee3d0da4e0` |
| `GlinkDxe` | `acbc4086e7d95f0ed938e91a1ec3c8f14e369243ed15cf29a337ecbdfff7725a` |
| `IPCCDxe` | `efdcf3dacad53b3960429ffec19257185d9787fb8100b9027b8eb6b070293c73` |
| `SmemDxe` | `e6e14967dc10fc24f37245dbd53a35f835232da0fdb248216b476bdc1698c0ca` |
| `MailboxDxe` | `17c2b0b9cc619d173bed6be2213ddd59e3d724664703539cb037355723777942` |
| `QcomChargerDxeLA` | `9f8281d91ac8293346fea22cd14c476da588d62f1822766520c96d6c37491ed1` |

The supporting implementation is `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/soccp.py` and `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/ipcc.py`.
Its `.601` package is not the authority for this `.700` package.
The selected consumers confirm the framing and fields used here.
Its preconstructed SMEM root and partition publication are not used.

## Complete XBL boot group

The record table starts at `0x148ec328` with stride `0x88`.
Interpreter `0x148291f4` registers the callback lists from offsets `+0x38` and `+0x40`.
The following selected records have **null values in both fields**:

| Record | Address | Image identifier at `+8` |
| --- | --- | --- |
| SOCCP_DTB | `0x148eca98` | `0x9f` |
| SOCCP | `0x148ecb20` | `0x88` |
| SOCCP_DEBUG | `0x148ecba8` | `0x89` |
| SOCCP_DCD | `0x148ecc30` | `0x8a` |

Null record callbacks do not mean that the image has no hardware operations.
Dispatcher `0x1484c96c` searches five image-operation entries at `0x148dc0f0`.
The entries include `{0x88, 0x1489a6e4}` and `{0x9f, 0x1489a2d0}`.

The SOCCP constructor `0x1489a6e4` installs this related operation group:

1. `0x1489a690` issues the existing memory-permission call through `0x14864328` with selector `0x3c`.
2. `0x1489a748` configures remaps, publishes the loaded entry address, sets modes, and enables clocks.
3. `0x1489a8fc` pulses the release clock control and releases the processor.

The model does not intercept the permission call or the loader's authentication calls.
It accepts only the hardware-side transactions in the following table.

| Selected operation | Physical address | Model behavior |
| --- | --- | --- |
| Remap words at `0x1489a774`, `0x1489a7a0..ac` | `0x00da2080`, `84`, `88`, `8c`, `0x00da20a8` | Retain each guest publication |
| Loaded image entry at `0x1489a7cc` | `0x00d7000c` | Retain an aligned, nonzero entry |
| Four mode updates at `0x1489a7d0..80c` | `0x00da2000`, `04`, `08`, `0c` | Retain full words, including guest-preserved upper bits |
| Other mode updates at `0x1489a810..848` | `0x00da2020`, `28`, `0x00da2200`, `0x00da2218` | Retain full words, including guest-preserved upper bits |
| Branch enables at `0x1489a84c..8b4` | `0x001a400c`, `04`, `08`, `40`, `2c`, `54`, `24`, `10`, `50` | Existing branch model, with real gate state |
| Root update at `0x1489a8b8..8e8` | Command `0x001a406c`, configuration `0x001a4070` | Existing RCG model, including update completion |
| Release at `0x1489a914..95c` | Pulse bit 2 and enable bit 0 at `0x001a402c`, then write zero at `0x00d70010` | Change remote lifecycle after entry, DTB, configuration, and clock publication |

The release operation polls branch bit 31 until it clears.
The helper uses the existing RCG and branch models instead of returning a constant clock-ready value.
Source selector 6 uses XO as explicit virtual wiring.
The physical source frequency is not established by this policy.
An established branch and its aliases are reused rather than duplicated.

The DTB constructor `0x1489a2d0` installs `0x1489a264`.
That operation calls `0x1489a184` with compatible string `qcom,soccp-csr` at `0x148e237c`.
The operation resolves the loaded image address and the DT property `dtb-address-offset`.
Its final store is `0x1489a250`.
The selected board DT gives base `0x00d40000`, size `0x80000`, and offset `0x60048`.
Thus, the DTB publication word is `0x00da0048`, not a ready flag.

The signed physical SOCCP ELF is RV32 with entry `0xa03d0000`.
Its first instructions initialize `gp` and `mtvec`.
The virtual endpoint does not claim that these instructions execute.
The guest still loads and authenticates that exact image through its normal path.

## Guest initialization and remote allocation

The selected board DT is the second DTB in `xbl_config.elf`, at file offset `0x10e14`.
Its SMEM memory-map node gives base `0x81d00000` and size `0x200000`.
Partition `part-14` has hosts `0/19`, size `0xa000`, flags `3`, cache line `32`, and zero exclusions.

The model waits for both events:

1. The real XBL release register receives its supported release write.
2. The guest sends the SOCCP GLINK IPCC signal after it initializes SMEM.

Release alone performs no memory write and generates no service reply.
The first doorbell discovers the **guest-created** `$TOC` at the end of the SMEM region.
It then finds the guest-created APSS/SOCCP `$PRT` entry.
It does not create or repair either structure.
Missing or malformed guest initialization fails closed.

Selected root evidence:

- XBL `0x1484fa0c..58` publishes `$TOC`, its version, and its entry count in the last 4 KiB.
- XBL `0x1484ff2c..6c` initializes `$PRT`, host IDs, size, and the two allocation cursors.
- XBL `0x14851124..60` and SmemDxe `0x3354..90` require TOC version 1 and fewer than 61 entries.
- SmemDxe `0x34c4..4ec` checks partition alignment, magic, and size.
- XBL `0x14850d3c..e80` and SmemDxe `0x2e68` scan published partition allocations.
- XBL `0x14850f50..98` and SmemDxe `0x310c..54` define the 16-byte allocation header.
- SmemDxe `0x2cf0..d70` publishes an uncached allocation and updates `offset_free_uncached` at partition offset `12`.

The virtual remote allocates only item `478` when absent and its own FIFO item `480`.
It discovers guest FIFO item `479` but never allocates it for the AP.
Each new allocation receives a `0xa5a5` header and zeroed remote-owned bytes before cursor publication.
An existing descriptor is not reset on the AP's behalf.
The peer requires initially inactive remote-owned indices before it adopts that descriptor.

SmemDxe `0x37d4..381c` claims a hardware mutex with `host+1` and reads its owner.
SmemDxe `0x3834..3864` releases that mutex with zero.
Its BSP at `0x7c94` gives base `0x01f00000` and lock-3 offset `0x43000`.
APSS, TME, and the virtual remote use one `QcomSmemMutex` at `0x01f43000`.
APSS uses owner one. TME uses owner 15. The virtual remote uses owner 20.
The TME alias at `0x07a43000` reaches that same object.
The peer holds this mutex while it scans and extends the partition heap.
It does not yield during publication.

The peer's memory initiator uses the existing APSS memory route.
No second SMEM backing exists in the helper.
All firmware and service views therefore access the same bytes.
Additional board aliases must route to that same backing or target object.
They must not instantiate a second memory or mutex.

A nonzero write claims a free mutex. A competing claim leaves the existing owner unchanged.
A zero write releases the mutex. Atomic updates preserve one owner across concurrent processor accesses.
The native TME claim at donor `0x22e580` requires owner 15.
The former APSS-only value check rejected that claim in SM8850 run121.

The public Qualcomm driver uses this same claim, owner-read, and zero-release protocol.
Its `qcom_hwspinlock_unlock` writes zero even after it reports a different owner.
The [original Linux driver patch](https://git.cdn.openwrt.org/openwrt/svn-archive/openwrt/plain/target/linux/ipq806x/patches-4.1/031-hwspinlock-qcom-Add-support-for-Qualcomm-HW-Mutex-bl.patch) establishes that software protocol.
The mutex does not establish image authorization.

The native `smem-mutex` fixture verifies TME acquisition, APSS contention, release, and subsequent APSS acquisition.
It also verifies that repeated release leaves the mutex free.
SM8850 run122 completes the original TME lookup of the existing SMEM item.
The complete suite passes 92 tests. Clippy passes with warnings denied.

### Complete TCSR lock bank

The pinned Canoe tree declares `syscon@1f40000` with size `0x20000`.
Its `qcom,tcsr-mutex` node selects a `0x1000`-byte stride.
This describes 32 independent lock words.
The platform creates each word with the existing atomic mutex model.
Lock three retains the same object and the existing APSS, TME, and remote views.
Unspecified bytes between lock words remain unsupported.

SM8850 QSEE `0x157b3d4c..0x157b3d6c` selects the lock index from `0x1580e928`.
It writes owner eight, reads the owner, masks its low byte, and compares against eight.
SM8850 run173 and SM8845 run137 select lock nine at `0x01f49000`.
The write previously failed because only the SMEM lock existed.
SM8850 run174 and SM8845 run138 pass this acquisition and next read RPMh configuration at `0x1890000c`.
The model preserves contention and release semantics instead of returning a constant owner.


## GLINK and IPCC

Selected board node `/sw/glink/xport-smem-config/edge-04` gives:

- Remote host 19.
- FIFO size `0x4000` and MTU `0x1000`.
- Outgoing physical IPCC client 46, signal 0.

Selected GlinkDxe `0x7738..7784` reads and packs that configuration.
The wire uses item 478 as a 32-byte descriptor and items 479/480 as the AP/remote FIFOs.
The descriptor has AP transmit indices at `+0/+4` and remote transmit indices at `+8/+12`.
The remote writes only `+0` and `+12` after creation.
The AP owns `+4` and `+8`.

The transport implements the related base-protocol group:

- VERSION request and acknowledgment negotiate version 1 and base feature 1.
- OPEN sends both the matching remote OPEN and the AP OPEN acknowledgment.
- Six remote intents of `0x1000` bytes exist for each supported channel.
- AP intents have channel-local ownership and retain their supplied identifiers.
- TX and continuation commands check the intent, fragment size, total size, and publication boundary.
- Response packets consume a sufficient AP intent until RX_DONE or RX_DONE_REUSE releases it.
- CLOSE exchanges both close halves and removes the channel's intents.
- READ_REQ handles FIFO backpressure. The producer preserves the eight-byte gap.
- Channel signal updates retain their requested state without a physical modem effect.
- The selected APSS SSR cleanup resets only remote-owned indices and closes this edge without an invented ACK.

The supported channel names are `PMIC_RTR_SOCCP_APPS`, `PMIC_LOGS_SOCCP_APPS`, and `glink_ssr`.
Selected PmicGlinkDxe contains both PMIC channel names.
Selected GlinkDxe `0x6470..64c8` checks the remote-owned indices during cleanup.
No tracer, zero-copy transport, alternate channel, or additional remote is synthesized.

All indices must be eight-byte aligned and less than the FIFO size.
The model uses fixed-size channel, intent, fragment, response, and output arrays.
A full FIFO retains an unpublished response and requests reader progress.
It does not overwrite an AP-owned intent or publish a partial response batch.

Selected IPCCDxe `0x2d94..2e78` initializes all five BSP rows at `0x7650`.
Lookup `0x2648` reads each physical client identifier at client-entry offset 8 with stride 16.
The protocol-zero table at `0x7110` places local physical client 8 at index 6 and SOCCP client 46 at index 13.
The helper exposes virtual IPCC revision `3.0.0`, not a captured hardware revision.
For major versions above 2, the driver uses those BSP indices and a protocol stride of `0xc0000`.

| Protocol | Local aperture | INTID | Signals | Local index | Clients |
| --- | --- | --- | --- | --- | --- |
| 0 | `0x01106000` | 278 | 8 | 6 | 17 |
| 1 | `0x011c3000` | 262 | 32 | 3 | 16 |
| 2 | `0x01283000` | 190 | 32 | 3 | 16 |
| 3 | `0x01343000` | 747 | 32 | 3 | 10 |
| 4 | `0x01403000` | 102 | 32 | 3 | 25 |

The model separates outgoing doorbells, pending receive signals, enable masks, IRQ level, and receive consumption.
The selected driver configures clear-on-read at `+8`, clears pending state at `+0x38`, and reads receive ID at `+0x48`.
Enable, disable, send, and per-signal clear use `+0x14`, `+0x18`, `+0xc`, and `+0x1c`.
Selected consumer sites include `0x26d0`, `0x28cc`, `0x29b4`, `0x2ab8`, and `0x2c18`.
Per-client enable, disable, status, and enable-status low words use `+0x200`, `+0x400`, `+0x600`, and `+0x800`.
An absent remote retains its outgoing doorbell but produces no response.
Only protocol zero, client index 13, signal zero connects to this SOCCP endpoint.
Local views of one protocol share a routing directory through `fabric_path`.
The owner view leaves this parameter empty.
Other views name that owner and must use the same protocol and signal geometry.
The owner's `fabric_client_count` bounds the shared directory.
Each view keeps its own declared client range, including an evidenced shorter prefix.
An unsupported source ordinal remains outgoing without entering the receiver's pending state.
Each view retains its own configuration, pending signals, enable masks, and IRQ output.
A send to an attached view records the sender's BSP index in that receiver.
Clear, mask, and receive-consumption operations affect only the addressed view.
The SOCCP service still consumes only the original AP view's outgoing signal.
A secure sender does not invoke that AP queue service.

## Service request group

PmicGlinkDxe `0x2d70` transmits the real request and polls the real GLINK receive path.
Receive function `0x2f50` dispatches owner at byte 0 and opcode at byte 8.
The PMIC header is `{owner, type, opcode}` with 32-bit little-endian words.
The selected request producers use type 1.
There is no PMIC sequence word to invent or replace.
GLINK channel and intent identifiers provide transport correlation.

| Owner / opcode | Accepted request bytes | Response bytes | Selected consumer |
| --- | --- | --- | --- |
| `0x800a / 0` | 16, battery index zero | 16 | Producer `0x3f5c`, receive `0x304c..3080` |
| `0x800a / 1` | 16, battery index zero | 48 | Producer `0x4010..4038`, receive `0x3094..31a4` |
| `0x800a / 9` | 16, battery index zero | 736 | Producer `0x3810..383c`, receive `0x3290..32c0` |
| `0x800a / 0x30` | 24, battery zero, property 6 | 24 | Producer `0x4110..413c`, receive `0x32e0..3348` |
| `0x800a / 0x300` | 24, battery zero, property `0x7b`, Boolean value | 24 | Producer `0x3e84..3eb8`, retained detached-connector policy |
| `0x8011 / 0x90` | 24, battery zero, property `0x30100`, length 4 | 1040 | Pmic receive `0x35c8..3640`, Charger `0x14174..14340` |
| `0x8011 / 0x91` | 1048, battery zero, property `0x180`, length 4, stage 1..3 in low nibble | 16 | Producer `0x47c0..48a4`, receive `0x3644..3680`, Charger `0x148d0..14994` |
| Log `0x800a / 0x19` | 32, category mask and level 0..5 | 16 | Producer `0x5be8..5c1c`, receive `0x58c4..5904` |
| Log `0x800a / 0x18` or `0x23` | 16, requested maximum 1..8192 | 8204 | Receive `0x5920` and `0x5980` |
| Log `0x800d / 0x36` or `0x39` | 16, cell zero or one | `0x9a0` or `0xe0` | Receive `0x5a40` and `0x5a9c` |

The charger-status fields are state at `0xc`, remaining energy at `0x10`, rate at `0x14`, voltage at `0x18`, power flags at `0x1c`, source at `0x20`, temperature at `0x24`, and current at `0x28`.
The final word at `0x2c` is an explicit virtual zero with no asserted physical meaning.
The battery-information reply supplies full energy at `0x14`, which the selected AP uses as its capacity denominator.
The OCV reply supplies microvolts at `0x10`, which the selected AP divides by 1000.

The OEM communication probe returns four opaque virtual zero bytes.
Selected Charger `0x141d0` checks exchange status, not those returned bytes.
The host-stage SET retains only the low-nibble stage because the producer does not initialize the other bits.
The virtual endpoint retains the log filter but has no physical SOCCP log producer or QBG context.
Its log buffers are empty, and its diagnostic dump size is zero.
These envelopes do not claim captured logs or a physical gauge dump.

Unknown owners, commands, properties, malformed requests, and unsupported transport commands fail closed.
In particular, power setter `0x31`, shutdown `0x47`, and AP-sent notification `7` do not receive successful replies.
The selected reply handler does not establish a usable refusal field for the unimplemented power setter.
An acknowledged no-op would therefore be incorrect.
Errors return a TLM failure and report the protocol error through SystemC.
The normal platform access monitor remains in the transaction path.

## Integration contract

Register these constructor-only modules in the shared registration file:

| Compatible | C++ type | Header |
| --- | --- | --- |
| `qcom,soccp-virtual-peer-v1` | `qcom_vp::QcomSoccp` | `models/qcom/soccp/soccp.h` |
| `qcom,soccp-virtual-peer-v2` | `qcom_vp::QcomSoccpV2` | `models/qcom/soccp/soccp.h` |
| `qcom,ipcc-local-v3` | `qcom_vp::QcomIpccLocal` | `models/qcom/soccp/ipcc.h` |
| `qcom,smem-mutex-v1` | `qcom_vp::QcomSmemMutex` | `models/qcom/soccp/mutex.h` |

Each constructor takes only `sc_core::sc_module_name`.
The helper reuses registered `qcom,rcg-v1` and `qcom,clock-branch-v1` modules.
It requires existing APSS DDR/SMEM backing, the APSS router, the PLL object, and GIC.
It does not create a new memory region.

For the existing APSS composition, the helper call is:

```lua
dofile(platform_directory..'sm8850-soccp.lua')(platform, {
    router='apss_router',
    memory_bind='&apss_router.target_socket',
    pll_path='gcc_plls',
    secure_irq_bind='&apss_gic.spi_in_229',
    tme_irq_bind='&tme_clic.source_38',
    irq_binds={
        '&apss_gic.spi_in_246',
        '&apss_gic.spi_in_230',
        '&apss_gic.spi_in_158',
        '&apss_gic.spi_in_715',
        '&apss_gic.spi_in_70',
    },
})
```

The IRQ binding numbers are SPI indices, not INTIDs.
Registration, runtime composition, gates, and formatting remain pending.
This slice ran no build, test, formatter, or OEM launch.
Binary hashes, boot records, instruction decoding, and selected DT/PE inspection are the evidence exercised here.
No firmware-execution or kernel-proof claim follows from this static evidence.

The integration gate must exercise the complete path, not a fabricated ready read:

1. Keep SOCCP image loading and authentication on the OEM path.
2. Observe the real DTB, remap, entry, clock, and release writes.
3. Let the guest initialize the SMEM root and partition before the first remote allocation.
4. Check that only remote allocations 478/480 extend the guest heap and that the mutex returns to zero.
5. Negotiate GLINK, open both channel halves, and retain intent ownership until RX_DONE.
6. Send selected battery and OEM requests through guest FIFO 479 and read their exact-sized replies from FIFO 480.
7. Check wraparound, fragmented requests, response backpressure, clear-on-read IRQ consumption, and SSR cleanup.
8. Check that malformed sizes, unknown commands, unknown MMIO offsets, and unsupported power effects fail closed.
9. Continue real OEM execution to the package kernel and retain its actual kernel log.

## SM8845 extended control layout

The selected SM8845 inputs are:

| Image | SHA-256 |
| --- | --- |
| `xbl_sc.elf` | `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4` |
| `xbl_config.elf` | `2effcfac7784d044a34dbff04c0da063c17806083707af285952b75964d8376c` |
| `uefi.elf` | `13722b7ed849359e7afc731dfdc1eb0c1fc1815907d708f4b94cadb7e03be4f4` |
| `soccp.bin` | `0e9f5ae7f474008cb29edf50fccdf4d9d655b93627c63b391db33b7587864fbe` |

The selected control producer differs from SM8850:

| Producer coordinate | Contract |
| --- | --- |
| Constructor `0x14898f94` | Permission/configure/release slots point to `0x14898f08`, `0x14898ff8`, and `0x148991dc` |
| Permission `0x14898f08` | Selector `0x3c` through `0x148629e0`. The model does not supply permission success |
| Configure store `0x14899064` | Additional remap `0xda2090=0x20000001` |
| Loaded-image query `0x148640e0` | Query failure returns `-1` before clock programming and release |
| DTB constructor `0x1489924c`, setter `0x14898f5c` | Publish the loaded DTB through global `0x1493b768` |
| Configure store `0x1489909c` | Write that DTB address to `0xda0048` |
| Configure store `0x148990ec` | Additional mode word `0xda2010`, low nibble four |
| Configure `0x14898ff8` | Root `0x1a4070` selects source one and HID one |
| Release store `0x1489923c` | Write zero at `0xd70010` after the existing branch reset pulse |

The corresponding SM8850 constructor is `0x1489a6e4`.
Its configure/release functions are `0x1489a748` and `0x1489a8fc`.
It writes neither added word and selects source six.
Version one retains its thirteen required words and rejects the added offsets.
Version two requires all fifteen words before release.
Both versions share the same heap, protocol, service, and lifecycle implementation.
The layout version is immutable constructor state, not a writable platform parameter.
There is no SoC-name branch in C++.

The shared mode/remap values remain the table above.
The model retains declared remaps and complete mode words, rather than simulating physical address translation.
The real guest still performs authentication and permission operations.
An entry-register write is not evidence that authentication succeeded.
Neither compatible provides an authentication result.

### Clock source and composition policy

The selected peripheral rate table at `0x148ef9a0` establishes selector one as GPLL0 main.
It is byte-identical to the selected SM8850 table at `0x148f1e40`.
See `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md` and `platforms/qcom/sm88xx/xbl-peripheral-clocks.lua`.
The established root at `0x118014` binds selector one to bank zero of `gcc_plls`.
The SM8845 helper resolves that existing connection before it adds modules.
It rejects a missing, duplicate, or incompatible parent.
It reuses the same PLL object, with its real programmed output gate and lock state.
Source one is never mapped to XO.
The SM8845 root exposes only source one, so its cold mux value cannot supply an unprogrammed XO clock.
This is explicit virtual wiring from the selected table, not a recovered SOCCP physical netlist.
The existing RCG still controls update completion and live source availability.
The SM8850 helper and its explicit source-six virtual XO policy remain unchanged.

The SM8845 IPCCDxe BSP starts at PE RVA `0x7690`.
Its virtual revision-three apertures, INTIDs, signal counts, and local indices match the table above.
Its client counts are `18/17/17/11/25`.
Only protocol zero connects client 46 at index 13 to this endpoint.
Protocol-one/two client 46 moves to index 16, but those banks have no attached remote.
The helper preserves existing branch owners and rejects conflicting target apertures before module insertion.
`platforms/qcom/sm88xx/sm8845/runtime.lua` applies this helper after the selected clock composition.

The SM8845 board DTB begins at config file offset `0xf52c`.
It declares SMEM `0x81d00000+0x200000`, hosts 0/19, and partition `part-10`.
The model searches host IDs, not the partition number.
The guest must publish the TOC, partition header, and heap before any remote allocation.
Release alone neither writes SMEM nor emits an IRQ or reply.
Malformed guest structures still fail closed.
The existing battery and empty-log replies remain declared virtual stimuli.
Unknown requests and unsupported physical power effects do not become successful no-ops.

### Native acceptance criteria

These are required checks, not executed results.
This change ran no build, test, formatter, or native launch.

Use the actual model and each board helper for a small control-only guest.
Use fresh model state for each missing-prerequisite variant.
All accesses below are aligned 32-bit transactions.

1. Publish a nonzero word-aligned DTB address at `0xda0048`.
   Publish a nonzero even loaded entry at `0xd7000c`.
   For example, a control fixture can use `0xa4a00000` and `0xa4400000`.
   These fixture addresses do not replace authenticated loading in the canonical boot.
2. Write common remaps `0xda2080=1`, `84=0x08000001`, `88=0x10000001`, `8c=0x18000001`, and `a8=0xd0000001`.
   Write modes `0xda2000/04/08/0c=4`, `0xda2020/28=0`, `0xda2200=0`, and `0xda2218=2`.
   For version two, also write `0xda2090=0x20000001` and `0xda2010=4`.
   Read each supported word back and compare its value.
3. Enable bit zero at branches `0x1a400c/04/08/40/2c/54/24/10/50`.
   Program `0x1a4070=0x101` for SM8845 or `0x601` for SM8850.
   Set update bit zero at `0x1a406c`.
   With SM8845 GPLL0 stopped, the update remains pending and the release branch remains unavailable.
4. For a minimal explicit clock fixture, program GPLL0 bank-zero offsets `0x10=0x1f`, `0x18=1`, `4=1`, and `0=5`.
   These offsets are relative to the existing PLL target, not a new memory view.
   Allow the model's five-microsecond PLL acquisition and one-microsecond RCG acquisition.
   The RCG update bit must then clear.
   Disable GPLL0 output again and confirm the release branch becomes unavailable.
   Restore it before a successful release.
5. Pulse bit two at `0x1a402c`, clear it, and enable bit zero.
   The OEM sequence delays `0x12c` between set and clear.
   Poll branch bit 31 until clear, then write `0xd70010=0`.
   The model checks live branch availability, not physical reset timing.
   A supported complete sequence returns `TLM_OK_RESPONSE` and release reads zero.
6. Send a release before DTB, entry, any required configuration word, or a live release clock.
   Each variant must return `TLM_COMMAND_ERROR_RESPONSE`, with release still reading one.
   In particular, omitting either version-two added word must reject release.
   Version one must return `TLM_ADDRESS_ERROR_RESPONSE` for either added offset.
   A version-one-only control sequence must not release version two.
7. Snapshot the SMEM backing before control programming and after release.
   The contents must not change, and no remote IRQ must appear.
   Do not fabricate a guest heap or use a large GLINK fixture for this control check.
8. Run both canonical OEM consumers with genuine authenticated loading.
   Capture the actual DTB, entry, remap, mode, clock, reset, and release writes.
   Then observe guest-created SMEM before the first IPCC-triggered allocation.
   Preserve native operation failures and the actual firmware error paths.
   Continue to genuine Linux logs before declaring the overall boot goal complete.

### Native control verification

Six actual-model control cases ran after this implementation.
The control guests were compiled with Clang/LLD and run with `qcom-vp-system`.
These cases used the real board helpers, PLL, RCG, branches, GIC, and SOCCP model.
They did not execute the OEM XBL release path.

| Case | Observed result |
| --- | --- |
| Version-one complete sequence | Reached success marker PC `0x300260` |
| Version-two complete sequence | Reached success marker PC `0x3002b4` |
| Version two without the added mode word | Release write at `0xd70010` returned `TLM_COMMAND_ERROR_RESPONSE` |
| Version one with the added remap | Access at `0xda2090` returned `TLM_ADDRESS_ERROR_RESPONSE` |
| Version two with GPLL0 disabled after RCG update | Release write at `0xd70010` returned `TLM_COMMAND_ERROR_RESPONSE` |
| Version two without DTB publication | Release write at `0xd70010` returned `TLM_COMMAND_ERROR_RESPONSE` |

Both successful guests wrote and read back all configuration words.
They used root configurations `0x601` and `0x101`, observed update completion, and read release state zero.
Both scanned the entire two-megabyte SMEM region before and after release and found it unchanged at zero.
These checks establish control-layout separation, the added mode-word prerequisite, and live PLL dependence.
They do not establish GLINK behavior, authenticated OEM release, or Linux boot.

A build, Clippy, and 63 tests passed in job `bg480`.
Canonical SM8850 run 16 and SM8845 run 15 were still running when this evidence was recorded.
No canonical result is claimed here.


## Post-DDR static contract closure

This section uses the two selected XBL hashes above.
The analysis decoded their original ELF bytes with Capstone and read the selected package ELF and FAT12 metadata.
It did not execute firmware, a build, a test, or a formatter.
Static coverage does not establish native admission, service execution, or boot completion.

### Records, predicates, and callbacks

The selected records have stride `0x88`.
The record fields are media, image type, load, authentication, execution, jump, and the optional word.
The selected bytes supply the following values.

| Record | SM8850 address | SM8845 address | Partition / image ID | Optional |
| --- | --- | --- | --- | --- |
| SOCCP_DTB | `0x148eca98` | `0x148eaa58` | `0x34 / 0x9f` | 0 |
| SOCCP | `0x148ecb20` | `0x148eaae0` | `0x2b / 0x88` | 0 |
| SOCCP_DEBUG | `0x148ecba8` | `0x148eab68` | `0x2c / 0x89` | 0 |
| SOCCP_DCD | `0x148ecc30` | `0x148eabf0` | `0x2d / 0x8a` | 1 |

All eight records have media `0x23`, image type 1, load 1, authentication 1, execution 0, and jump 0.
The sync-policy word at `+0x10` is zero on SM8850 and one on SM8845.
The optional word is at `+0x14`. It is not the authentication word at `+0x1c`.
All record execution, jump, pre-list, cancellation, and metadata-loader pointers are null.
All post-lists are null except SM8845 SOCCP_DEBUG.
Thus, optional DCD absence does not authorize an authentication-success substitute.
The real generic loader owns presence, admission, segment loading, and error propagation.

The SOCCP_DEBUG post-list at `0x148eb758` contains one `{function, name}` pair and a zero terminator:
`{0x1483735c, 0x148d3e9c}`, where the name is `dump dependency ready`.
The callback preserves its incoming configuration handle.
It sets `w1=0x20`, `w2=1`, then tail-calls `0x1482ad30`.
It does not read a remote register, send a message, wait for an IRQ, or prove that SOCCP runs.

The setter validates context magic `0x43464358` through `0x1482a834`.
It requires property index below `0x32` and propagates validation or replacement errors.
The terminal setter at `0x1482a97c` stores the value at `context + 0x10 + index*0x18`.
It stores scalar-kind 1 at `context + 0x20 + index*0x18`.
For property `0x20`, these offsets are `+0x310` and `+0x320`.
Property replacement first invokes `0x1482b090`, which can release an older owned pointer value.
The host must not prepopulate this context or intercept the setter.
The object belongs to XBL's configuration lifecycle, not to the SOCCP endpoint.

The genuine crash handler reads property `0x20` at `0x1483fa44..54`.
It passes handle `-1`, which resolves the current context through global `0x14924010`.
Its local value at `[fp-0x1c]` contributes to decisions at `0x1483fd00..14`.
Those decisions also use `[fp-0x20]` and a dump-state result at `[fp-8]`.
This is a dump-policy dependency, not unconditional permission to enter a dump.
SM8850 has the corresponding property read at `0x14841510..20`, but no SOCCP_DEBUG callback in this selected table.
Do not copy the SM8845 callback into SM8850.

### Admission destinations and object lifetime

The whitelist pairs are identical on both OEMs:

| Record | Allowed base / extent pairs |
| --- | --- |
| SOCCP_DTB | `0x80000000/0x58029000`, `0xd8089000/0x27f77000` |
| SOCCP | `0xec0000/0xc000`, then both SOCCP_DTB pairs |
| SOCCP_DEBUG | `0xd20000/0xc000`, `0x1468e000/0x4000`, then both SOCCP_DTB pairs |
| SOCCP_DCD | `0xd20000/0xc000` |

The tables begin at `0x148ec0e8/0x148ea0e8`.
Their SOCCP, DEBUG, and DCD offsets are `+0x20`, `+0x50`, and `+0x90`.
These are permitted ranges, not instructions to allocate objects or proof of authentication.

The selected FAT12 containers contain genuine `IMAGE/SOCCP.MBN` and the short-name `SOCCP_DT.MBN`.
Their hashes are:

| File | SM8850 SHA-256 | SM8845 SHA-256 |
| --- | --- | --- |
| SOCCP.MBN | `aea3445b58c7e34d1ee6fbc0d6324bf5e8719bd2d2a88803319fac6d6c7a007b` | `af938920f0efec5d88b93fc447a6cf50b1d261622bbf7e696ca0846e4ac1cd63` |
| SOCCP_DT.MBN | `d4012a89a1887947546223b3095b034d1d7ebb3c9b22a4289917ef93d60f2295` | `ac975e7996884e8efe2b0418f6768d9b07d8c06e9d7d524360f3194151180529` |
| RADIO/soccp_debug.img | `f31c6bcc150653e6ffe95aa0f5f7460e3daf212718073829c5f544f8d966bccb` | `dc8230fb90ed353d46dc01ca376b8afb73c791d579dc4b8fe8785d55131656c9` |
| IMAGES/dcd.mbn and RADIO/soccp_dcd.img | `25b0e69f020d888fbf5a757bc6124c7a9be454bd1e0011864b996f79655bdf9c` | `19e88988e5489a0ac8a3cc75bb6500c4ec897809257b5679832cb27d3891cb45` |

The DT ELF payload targets are `0xa08d0000+0x12090` and `0xa4a00000+0x13950`.
The RV32 SOCCP entries are `0xa03d0000` and `0xa4400000`.
SM8850 additionally maps virtual `0x401000` to physical `0xa03d1000` for a `0x73ba` executable segment.
The remaining image data and BSS follow the original ELF program headers.
The endpoint must not translate these segments itself or assign the guest's entry.

Both DEBUG ELFs load SRAM `0xd20000+0x4000`, SRAM `0xd24000+0x4000`, and DRAM `0x823a0000+0x40000`.
Their entry is `0xd20000`, but the records do not jump there.
The first two file sizes are `0x2b80/0x2fd2` on SM8850 and `0x2b40/0x2ea2` on SM8845.
Both DRAM file sizes are `0x233c`.
Both DCD ELFs target `0xd28000`.
Their file and memory sizes are `0x1cb8` and `0x211c`, respectively.
These destinations require ordinary shared RAM, including loader zero-fill.
They are not MMIO registers that can return success without retaining bytes.
Shared backing and composition remain separate.

The five-entry hardware-operation tables at `0x148dc0f0/0x148da0b0` contain image IDs `0x88`, `0x9f`, `0x9d`, `0x9e`, and sentinel `0x24`.
Neither DEBUG `0x89` nor DCD `0x8a` has a hardware-operation constructor there.
The selected records add no separate release operation for those images.
Their lifetime extends from genuine loader publication until guest overwrite or reset.
Static XBL evidence does not establish an internal remote consumer's later reads of DEBUG/DCD.
No successful DCD parser or debug service is invented.

### Configuration, release, and completion coverage

The DT parser and release coordinates above remain authoritative.
SM8850 `0x1489a184` first queries the loaded image through `0x14865a28`.
It resolves a configuration node, obtains its base, and reads `dtb-address-offset`.
It ORs the two final query statuses and returns `-1` if either fails.
Only its successful path stores the loaded address at `base+offset`.
SM8845 instead retains the DTB address in guest global `0x1493b768` until SOCCP configuration publishes it.
Neither path permits a host-seeded DTB register.

The model now retains all 32 bits of each supported mode/configuration register.
Both selected configure functions use `AND 0xfffffff0`, then insert the low-nibble mode.
The upper bits are ordinary retained configuration, not an unsupported-command error or a completion condition.
This removes an artificial restriction without adding new offsets, reset values, ready flags, or admission results.
All existing required-word, DTB, entry, and live-clock release predicates remain unchanged.
V1 still rejects V2-only offsets.

Release state reads one before successful release and zero afterward.
Release does not allocate shared objects, complete GLINK, or assert an interrupt.
The first accepted IPCC doorbell after release discovers the guest's SMEM structures.
The peer then publishes only its remote-owned objects and signals actual publication.
The detailed TOC, partition, heap, FIFO, ownership, and IRQ layouts above apply to both OEMs.
The peer's state lasts for the platform instance.
SSR closes this edge and resets only its remote-owned indices.
It does not imply a new authenticated image admission or a new boot epoch.

The PDP/CPUCP owner found no SOCCP service request in those selected XBL callbacks.
Their stages and the SOCCP stage use separate operation groups.
This does not prove the signed remote images have no internal dependencies.

The selected XBL images contain no literal `glink`, `ipcc`, `glink_ssr`, or selected PMIC channel names.
This negative string evidence does not prove that no indirect transport call exists.
The established XBL consumers are the loader operations, DT parser, register polling, SMEM initializer, and dump-policy context reader.
The named PMIC GLINK request/reply consumers above are selected UEFI modules, not recovered XBL PMIC clients.

### Bounded remaining probes

| Unknown | Exact observation | Discriminating outcomes and smallest candidate |
| --- | --- | --- |
| Actual record completion and optional DCD behavior | Break at walker advances `0x1482970c/0x1482781c`, retaining record pointer, status, and the original authentication result | A completed record must preserve the loader's decision. Missing optional DCD can follow the existing absence path. A failed signature must retain its original failure. No model-side admission candidate is needed. |
| Actual DT selection and publication | Break at `0x1489a250` on SM8850 and stores `0x1489908c/0x1489909c` on SM8845. Capture loaded-image query result, source pointer, and stored value | Matching selected ELF addresses establish publication. A changed allocation requires the guest's real destination, not a hard-coded value. Existing retained words are the smallest hardware substitute. |
| DEBUG/DCD retention | Observe genuine segment writes and later reads in `0xd20000..0xd2bfff` and `0x823a0000..0x823dffff` | Correct bytes plus ELF zero-fill require only RAM. A later device transaction outside these ranges needs its address, width, value, PC, and subsequent poll before a new effect is justified. |
| SM8845 dump marker and lifetime | Break at `0x1483735c`, setter `0x1482a9ac`, and consumer `0x1483fa50`. Record context, magic, `+0x310/+0x320`, and global `0x14924010` | Same-context value 1 proves guest publication. Different contexts require the actual guest lifecycle. A missing marker is not repaired in the endpoint. |
| Native release and clock dependence | Observe final stores `0x1489a95c/0x1489923c`, branch bit 31 at `0x1a402c`, and retained DTB/entry/config words | Valid prerequisites permit lifecycle release. A pending clock update must keep the clock unavailable. Existing clock and endpoint models are sufficient candidates. |
| First real downstream GLINK consumption | Record first protocol-zero IPCC send `+0xc`, then TOC/PRT bytes, mutex owner, items 478/479/480, FIFO producer/consumer indices, and receive ID `+0x48` | Guest-created roots and AP FIFO permit remote allocation/reply. Missing roots or malformed queues must not trigger repair. Existing request-driven peer is the candidate, not a prebuilt queue. |
| Internal signed-remote DEBUG/DCD use or SOCCP-to-PDP dependency | Trace first read of loaded DEBUG/DCD bytes and first request naming another remote in the original image. Retain PC, address, payload, and reply predicate | A plain table read needs shared bytes only. A hardware/service request needs that exact request contract. Do not invent a generic completion or cross-remote grant. |

Run these probes with both canonical OEM sequences.
The earlier control-fixture results do not replace this native evidence.

### Shared DEBUG and DCD backing integration

The reset composition owns one cold-zero SRAM object at `0xd20000+0xc000`.
The first two 16 KiB pages contain the selected DEBUG segments.
The third page contains DCD, including the maximum selected extent `0x211c`.
Page padding is ordinary zero-initialized RAM, not an additional service object.
APSS, UFS DMA, sequencer authentication DMA, and CPUCP aliases reach the same backing.
The existing `ddr_low` object covers DEBUG DRAM at `0x823a0000+0x40000` and both OEM SOCCP/DTB destinations.
The SOCCP endpoint continues to use the APSS memory bus and does not own another image copy.
Its two platform helpers require no new RAM or completion state.
The genuine loader still supplies file bytes and segment zero-fill.
Later internal DEBUG/DCD consumers remain subject to the probes above.
This integration has no new native or behavioral verification.

## Secure QSEE IPCC view

The selected SM8850 `RADIO/tz.img` SHA-256 is
`afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932`.
The selected SM8845 `RADIO/tz.img` SHA-256 is
`b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09`.

Both images select protocol zero and local physical client one.
Their client tables place that client at BSP ordinal one.
The resulting local aperture is `0x01101000`.
The existing AP view at `0x01106000` uses ordinal six.
These views share one protocol fabric, not one receive queue.

| Contract | SM8850 linked VA | SM8845 linked VA |
| --- | --- | --- |
| First cold-stage call | `0x156e5738` | `0x156da6b4` |
| IPCC initializer | `0x157571fc` | `0x15749f9c` |
| Protocol descriptor | `0x157f8780` | `0x157ebff8` |
| Revision-read call | `0x15757b94` | `0x1574a934` |
| Actual revision load | `0x157576fc` | `0x1574a49c` |
| Pending-clear call | `0x15757450` | `0x1574a1f0` |
| Clear-on-read configuration write | `0x1575747c` | `0x1574a21c` |
| Interrupt-registration call | `0x157574a0` | `0x1574a240` |

The SM8850 descriptor supplies 17 clients and eight signals per client.
The SM8845 descriptor supplies 18 clients and eight signals per client.
Both descriptors supply interrupt number 261.
The first load reads revision at `0x01101000`.
Protocol open writes one to `0x01101038`, then sets configuration bit zero at `0x01101008`.
The existing virtual revision `0x00030000` selects receive offset `0x48`.
No new ready word or unsolicited response exists.

### Interrupt numbering

SM8850 registration passes 261 unchanged through `0x1567f4fc` and `0x15770a2c`.
GIC enable function `0x1575253c` uses that number directly.
At `0x15752594..0x157525d8`, it selects distributor offset `0x120` and bit five.
This is `GICD_ISENABLER8`, so the architectural INTID is 261.
The corresponding SPI input index is 229, not 261.
SM8845 uses the same direct calculation at `0x1574528c..0x157452d0`.
Both runtime callers therefore supply `secure_irq_bind='&apss_gic.spi_in_229'`.

The model retains source identity across attached local views.
It keeps the existing SOCCP remote service on the original AP view.
Unattached destinations retain outgoing signals without completing a guest queue.
The protocol owner rejects duplicate local indices and incompatible view geometry.

### Regression scope

The `ipcc` guest fixture exercises both client counts through separate protocol fabrics.
It checks cross-view source identity, loopback, local clear and mask state, and clear-on-read behavior.
It also checks protocol isolation, empty receive state, unattached destinations, and rejected client or signal indices.
Suite `bg940` passes all 99 tests, including this native fixture.
Clippy also passes with warnings denied.


## TME protocol-zero IPCC view

SM8850 `RADIO/tme_fw.img` has SHA-256
`4258fc24317e3467b39144ab72a46d85e4494c80bddc37b14de16177ff0c4a19`.
SM8845 `RADIO/tme_fw.img` has SHA-256
`6a781e1eb215fbcd2d157bebb7b25502f6ae74ed9bce7810dc61f98545c10fb4`.
The ELF view uses addresses `0x20000000` above the TME local addresses below.

Both selected driver roots at `0x447b20` contain one descriptor, at `0x447b0c`.
They select physical client 23 through the table at `0x447a7c`.
That table places client 23 at ordinal 16.
The descriptor supplies protocol zero, eight signals, 18 clients, and interrupt number 38.
Its base is TME `0x05100000`.
The resolved local bank is TME `0x05110000`, with system alias `0x01110000`.
No other protocol bank follows from this descriptor.

| Operation | SM8850 TME PC | SM8845 TME PC |
| --- | --- | --- |
| Descriptor initializer | `0x440962` | `0x440be0` |
| Client lookup call | `0x4409cc` | `0x440c4a` |
| Revision-read call | `0x4409ee` | `0x440c6c` |
| Distinct common-control write call | `0x440a1a` | `0x440c98` |
| Protocol open | `0x4404ae` | `0x44072c` |
| Pending-clear call | `0x4404f0` | `0x44076e` |
| Clear-on-read write call | `0x440504` | `0x440782` |
| Interrupt-registration call | `0x44051c` | `0x44079a` |
| Actual MMIO store | `0x44095e` | `0x440bdc` |

The initializer reads the local revision and writes one to TME `0x051cf008`.
This second address is a distinct retained common-control word.
It is not evidence for another protocol's local receive bank.
The composition preserves its existing backing and adds system alias `0x011cf008`.
The old standalone revision memory at `0x05110000` is removed.
The complete local bank now uses the shared protocol owner.

Protocol open writes one at local offset `0x38`, then sets configuration bit zero at offset eight.
The driver consumes receive IDs at offset `0x48` for the selected virtual revision.
Send, enable, disable, and explicit clear retain the existing local-bank semantics.
The channel callbacks and their original error paths remain firmware-owned.

### CLIC source and import arithmetic

SM8850 wrapper `0x440870` receives the descriptor interrupt in `a1`.
It receives callback `0x440720` in `a4` and descriptor `0x447b0c` in `a5`.
It passes `(38, callback, descriptor)` to the original registration service.
SM8845 wrapper `0x440aee` uses callback `0x44099e` with the same descriptor.

At SM8850 `0x44087c`, `LUI a0,0x417` produces `0x417000`.
The load at `0x440880` uses offset `0x2d8`, so its effective address is `0x4172d8`.
Relative to legacy base `0x416800`, this is import `0xad8`, not import `0x2d8`.
Both installed maps select donor `0xb24`, body `0x2235ca`.
That body preserves the incoming interrupt number in `s0` and restores it for its final provider call.
This is the same registration service used for sequencer interrupt 23.
The IPCC output therefore connects to CLIC source 38 without a GIC-style offset.
The CLIC external source mask uses bit `38-21=17`.
Firmware still controls that source mask, enable byte, priority, and threshold.

Adjacent synchronization wrappers load effective addresses `0x417314` and `0x417318`.
These are legacy imports `0xb14` and `0xb18`.
Their donor mappings are `0xb60` and `0xb64`, bodies `0x227556` and `0x227578`.
Those bodies preserve the original interrupt-disable and nesting behavior.
No imported callback receives a forced successful result from the IPCC model.

### Prefix geometry and regression

Both TME drivers expose 18 clients.
The SM8850 QSEE and AP views expose only the matching first 17 clients.
The shared owner therefore declares directory capacity 18 without widening either older view.
Current attached source ordinals one, six, and sixteen fit every receiving view.
A valid command from ordinal seventeen to an older view remains undelivered and outgoing.
It does not fail the sender's MMIO transaction or fabricate a reply.

The existing `ipcc` fixture now includes a real RISC-V CLIC guest.
The AP guest sends to ordinal sixteen through the shared fabric.
The TME guest receives source 38, consumes source ordinal six, and sends its reply from ordinal sixteen.
The fixture also checks shared common-control backing and the shorter receiver namespace.
The native fixture passes with actual APSS and TME execution in suite `bg940`.
SM8850 run168 and SM8845 run133 pass the former TME IPCC access failure.
Both next reach the separate interrupt-priority import at effective address `0x4172e4`.


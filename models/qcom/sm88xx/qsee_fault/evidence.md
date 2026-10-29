# Immediate fault setup model

## Model contract

This is ahead-of-execution work. It does not establish native execution of these initializers.
The source directory below records exact selected image hashes, table addresses, and consumer addresses.

`QcomFaultMonitor` implements a bounded retained-word substitute for immediate setup.
Each listed word starts at virtual cold value zero. Each aligned 32-bit write retains all bits.
Reads return that same word. Configuration does not create a transaction, fault, interrupt, or permission grant.
The model has no interrupt output. It does not implement a deferred handler or assume write-one-to-clear semantics.
The model logs and rejects every unlisted word and malformed transaction. No access creates a new word.

SM8845 words `17e80010` and `17e80030` use this same opaque retained-word contract.
Their physical reset values and status semantics remain unresolved. They are not ready or identification constants.
The default zero state does not force the conditional five-write branch.
The five conditional targets retain writes but do not acquire fabricated clear semantics.
The two fixed control words retain writes through shared clock-router aliases.
These controls do not model clock rates or hardware readiness.

The Lua inventory records all 17 SM8850 and 16 SM8845 NOC monitors.
It records six global writes per SoC and 35 common DDRSS/CMSR operations.
Native monitor stride is `0x148`. OEM monitor stride is `0x48`.
Selector strides are separate and must not replace monitor strides.
Values in the inventory describe guest operations. Composition never pre-applies them.

The helper requires the existing LLCC and DDR command owners.
It extends only their declared configuration offsets. It preserves all existing offsets and state.
LLCC offsets are `68058,6800c,70000,70024`. MC fallback offsets are `9118,9114,52a4`.
The helper preserves the existing LLCC, DARE, memory, and MC apertures.
NOC/CMSR pages reject offsets outside their exact declared word lists.
The helper checks existing APSS mappings before it adds any owner.
It also checks both other clock views before it adds control aliases.

## Verification boundary

The native fixture checks independent banks and shared control ownership across APSS and CPUCP.
Negative cases check an unknown read and a byte write.
The focused fixture passes for both selected SoCs.
The integration run passes 47 host tests and 57 native tests after correction of the fixture dependencies.
Deferred status and event behavior require separate source evidence and real producers.

## Firmware source evidence

The static analysis used Python zipfile, pyelftools, and Capstone on both selected private packages.
It read bytes through each ELF PT_LOAD interval, not a global file-offset delta.
Addresses below are hexadecimal unless a label states otherwise.

| SoC | Package | RADIO/tz.img SHA256 |
| --- | --- | --- |
| SM8850 | CPH2749export_11_16.0.5.700EX01_2026022701130186.zip | afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932 |
| SM8845 | CPH2771export_11_16.0.1.305EX01_2025121000260242.zip | b75222e7bd584d204b686fa5fc4f217b5f341d4ee5c128d23ad2bdd051c1fa09 |

## Actual producer and loader evidence

The missing directory is not a hardware register bank. It is the authenticated `RADIO/tz_qti_config.img` ELF payload.
`IMAGES/tz_qti_config.mbn` supplies the same named configuration payload in each package.
The OEM configuration remains a separate `RADIO/devcfg.img` object.

| SoC | QTI PT_LOAD VA = PA | File-backed size | QTI image SHA256 | OEM PT_LOAD VA = PA |
| --- | --- | --- | --- | --- |
| SM8850 | d80eb000 | 1e000 | 46f6b44e0474bb0adf87a8085a87e896ab97d065637259b1f683ee519172bfb6 | d80e0000 |
| SM8845 | d81eb000 | 1a000 | b0da9952ca94f0dde273baa4112c334dcfa27e9ffb76393ac5129657cbec9a58 | d81e0000 |

Both OEM file-backed payloads have size a000. Their separate 801fd000 segment is not the native fault directory.
The common object needs no inferred relocation to match the consumed address. Its ELF destination already matches.
The pointers decoded below are absolute pointers within these loaded RAM objects.

Actual retained loader evidence:

- `build/private/run-sm8850-common-runtime174/qemu-uart.log:341-352` records TZ_QTI_Config start, metadata authentication, segment hash checking, and load completion.
- That log reports 134564 loaded bytes. It then records QSEE Dev Config and QSEE load completion.
- `build/private/run-sm8850-common-runtime151/qemu-uart.log:341-352` records the same image sequence and byte count.
- `build/private/run-sm8845-common-runtime124/qemu-uart.log:334-342` records metadata authentication and TZ_QTI_Config completion with 117504 bytes.
- It then records QSEE Dev Config and QSEE completion. Unlike the SM8850 log, this excerpt has no separate segment-hash line.

These are existing loader logs, not execution evidence for the later fault initializer.
The ELF establishes the load destination. The UART establishes that the real loader selected and loaded the object.
This investigation does not independently reproduce cryptographic authentication.

### DAL source selection

SM8850 `15726f30` reads identity word `01fc8000` into RAM `15805288`.
It passes d80eb000 to `15726e44` and d80e0000 to `15726e88`.
Each setter compares object word +0 with the upper halfword of that identity word.
The QTI and OEM root words are a01b on SM8850 and a022 on SM8845.
Each accepted object publishes the pointer at object +8. A null or unmatched source does not become an empty successful directory.

SM8850 `15726ecc(0)` returns the common DAL source. Argument 3 returns the OEM source.
`15726878` attempts source 0, then source 1. Source 1 returns null in the examined getter.
The OEM readers explicitly try source 3 first. They only try the common source after an OEM lookup failure.

The accepted DAL source has this layout:

| Offset | Field |
| --- | --- |
| +0 | DAL property binary pointer |
| +8 | Structure-descriptor array pointer |
| +10 | Named-device count |
| +18 | Named-device array pointer |

A named-device record has stride 28. Its name pointer is +0, name hash is +8, and property offset is +c.
The property binary contains a header, a name-string area, and typed property records.
A fault property record uses type 12 with its name flag set, followed by a 32-bit structure index.
The next word is the ff00ff00 property terminator.
`15726d4c` resolves that index with stride 10. Descriptor +0 is a size and descriptor +8 is the actual object pointer.
Each selected fault descriptor reports size 8. That descriptor size is not the nested table length.
Each pointed fault object contains a 32-bit selector count at +0 and an eight-byte selector pointer at +8.
Every selected root here has count one.

## Selected property records

The table records actual payload addresses. It does not contain synthetic replacement data.

| SoC | Property | Structure index | Root | Selector | Selected payload |
| --- | --- | --- | --- | --- | --- |
| SM8850 | `/dev/NOCError:NOCError_Propdata` | 12b | d8102b40 | d8102af8 | d8101460 |
| SM8850 | `/dev/NOCErrorOEM:NOCError_Propdata_oem` | 1a | d80e9720 | d80e96e8 | d80e9200 |
| SM8850 | `/dev/DDRSSError:DDRSSError_Propdata` | 12c | d81010d0 | d81010a8 | d8101038 |
| SM8850 | `/dev/DDRSSErrorOEM:DDRSSError_Propdata_oem` | 1b | d80e9760 | d80e9738 | d80e9730 |
| SM8850 | `/dev/CMSRError:CMSRError_Propdata` | 12d | d8100c38 | d8100c10 | d8100bc8 |
| SM8845 | `/dev/NOCError:NOCError_Propdata` | f7 | d8202348 | d8202300 | d8200db0 |
| SM8845 | `/dev/NOCErrorOEM:NOCError_Propdata_oem` | 15 | d81e9428 | d81e93f0 | d81e8f50 |
| SM8845 | `/dev/DDRSSError:DDRSSError_Propdata` | f8 | d8204078 | d8204050 | d8203fe0 |
| SM8845 | `/dev/DDRSSErrorOEM:DDRSSError_Propdata_oem` | 16 | d81e8870 | d81e8848 | d81e8840 |
| SM8845 | `/dev/CMSRError:CMSRError_Propdata` | f9 | d8202e60 | d8202e38 | d8202df0 |

### Selector layouts

All native selectors require chip a0 on SM8850 or a7 on SM8845. Their revision threshold is 00010000.
Byte +4 is zero. Thus these rows accept the threshold revision or a later revision, not only an exact match.
The consumer checks the cached chip/revision values, not the object identity word a01b/a022.
The hardware-predicate pointers below are zero. These rows require no extra predicate-register read.

- NOC native selector, stride 48: chip +0, exact-revision byte +4, revision +8, monitor count +c, monitors +10.
- It also contains global-write count +18, target-pointer array +20, another count +28, and another pointer +30.
- Its optional predicate is +38 with mask/value +40/+44. The selected monitor counts are 17 and 16, respectively.
- NOC OEM selector, stride 38: common chip/revision prefix, count +c, OEM monitor records +10, global values +18.
- Its +20 pointer supplies other OEM data. Its optional predicate is +28 with mask/value +30/+34.
- DDRSS and CMSR native selectors use stride 28. They contain the common chip/revision prefix, predicate +10, mask/value +18/+1c, and payload +20.
- DDRSS OEM uses that same 28-byte selector layout.

SM8850 selector readers: NOC `15755d8c`, OEM NOC `15755ecc`, DDRSS `15756840`, OEM DDRSS `15756980`, CMSR `15757050`.
SM8845 top-level `15748a60` calls NOC `15748548`, DDRSS `157493bc`, no-op `15749838`, and CMSR `15749c34`.
SM8850 top-level `15755d68` calls `15755850`, `15756670`, no-op `15756aec`, and `15756ee8`.

### NOC monitor records

The native NOC monitor stride is **148**, not the selector stride 48.
The OEM NOC monitor stride is **48**, not the OEM selector stride 38.
The earlier group-8 report described selector strides. Do not apply those strides to monitor records.

| Native offset | Field used during immediate initialization |
| --- | --- |
| +0 | Name pointer |
| +8 / +10 | Logger offset-table pointer / logger MMIO base |
| +18 | Architectural INTID, or all-ones sentinel |
| +20 / +28 / +30 | Sideband count / offset-table pointers / MMIO-base pointers |
| +38 / +40 | Explicit-write count / address pointers |
| +b8 / +c0 / +c8 | Byte-value monitor count / offset tables / bases |
| +d0 / +d8 / +e0 | Bit-zero monitor count / offset tables / bases |
| +e8 / +f0 / +f8 | Word-value monitor count / offset tables / bases |
| +110 / +118 / +120 | Optional query count / query IDs / runtime skip byte |
| +128 | Optional read/OR/write register |
| +140 | Extra monitor object pointer |

OEM record +8 is the registration/configuration enable byte. It equals one for all selected monitors.
OEM +10 points to six-word sideband values per sideband. OEM +20 supplies explicit-write values.
OEM +28/+30/+38 supply byte, bit-zero, and word values. OEM +40 supplies the optional OR mask.
Native sideband offsets at +4,+6,+c,+e,+14,+16 select the six word writes.
Offset ffff means absent. Extra monitors write one at their selected +4 offset.
Bit-zero operations read a word, clear bit zero, then OR the selected OEM byte.
The final logger write uses its offset-table halfword +4 and value three.
All selected native +110 query counts are zero. Their initial +120 skip bytes are zero.
The immediate inventory below applies when earlier initializer dependencies succeed and these loaded tables remain unchanged.

### SM8850 NOC records and immediate operations

`W` means a 32-bit write. `B0` means read/clear-bit-zero/OR. Values and addresses are hexadecimal.

Global writes, before the monitor loop: `31100408=W(7)`, `31180408=W(7)`, `31100808=W(7)`, `31180808=W(7)`, `31100c08=W(7)`, `31101008=W(7)`.

#### 0: CNOC_MAIN

INTID 226 (SPI 194). Native record `d8101460`. OEM record `d80e9200`.

- `01500240=W(5)`, `01500440=W(7fffff)`, `01501040=W(106)`, `01501048=W(106)`
- `01501050=W(106)`, `01501058=W(106)`, `01501060=W(106)`, `01501068=W(106)`
- `01501070=W(106)`, `01501078=W(106)`, `01501080=W(106)`, `01501088=W(106)`
- `01501090=W(107)`, `01501098=W(106)`, `015010a0=W(106)`, `015010a8=W(106)`
- `015010b0=W(106)`, `015010b8=W(10b)`, `01501008=W(1)`, `01500008=W(3)`

#### 1: CNOC_CFG

INTID 226 (SPI 194). Native record `d81015a8`. OEM record `d80e9248`.

- `01600240=W(ffffffff)`, `01600244=W(ffffffff)`, `01600250=W(ffffffff)`, `01600254=W(1)`
- `01601040=W(106)`, `01601048=W(106)`, `01601050=W(106)`, `01601058=W(106)`
- `01601060=W(106)`, `01601068=W(106)`, `01601070=W(106)`, `01601078=W(106)`
- `01601080=W(106)`, `01601088=W(106)`, `01601090=W(106)`, `01601098=W(106)`
- `016010a0=W(106)`, `016010a8=W(106)`, `016010b0=W(106)`, `016010b8=W(106)`
- `016010c0=W(106)`, `016010c8=W(106)`, `016010d0=W(106)`, `016010d8=W(106)`
- `016010e0=W(106)`, `016010e8=W(106)`, `016010f0=W(106)`, `016010f8=W(106)`
- `01601100=W(106)`, `01601108=W(106)`, `01601110=W(106)`, `01601118=W(106)`
- `01601120=W(106)`, `01601128=W(106)`, `01601130=W(106)`, `01601138=W(106)`
- `01601140=W(106)`, `01601148=W(106)`, `01601150=W(106)`, `01601158=W(106)`
- `01601160=W(106)`, `01601168=W(106)`, `01601170=W(106)`, `01601178=W(106)`
- `01601180=W(106)`, `01601188=W(106)`, `01601190=W(106)`, `01601198=W(106)`
- `016011a0=W(106)`, `016011a8=W(106)`, `016011b0=W(106)`, `016011b8=W(106)`
- `016011c0=W(106)`, `016011c8=W(106)`, `016011d0=W(106)`, `01601008=W(1)`
- `01600008=W(3)`

#### 2: DC_NOC_DCH

INTID 290 (SPI 258). Native record `d81016f0`. OEM record `d80e9290`.

- `310e0240=W(1ffffff)`, `310e0440=W(105)`, `310e0448=W(106)`, `310e0450=W(106)`
- `310e0458=W(105)`, `310e0460=W(105)`, `310e0468=W(105)`, `310e0470=W(105)`
- `310e0478=W(105)`, `310e0480=W(105)`, `310e0488=W(105)`, `310e0408=W(1)`
- `310e0008=W(3)`

#### 3: GEM_NOC

INTID 108 (SPI 76). Native record `d8101838`. OEM record `d80e92d8`.

- `31201040=W(7ffff)`, `31101408=W(1)`, `31180c08=W(1)`, `31100008=B0(1)`
- `31180008=B0(1)`, `31200008=B0(1)`, `31200408=B0(1)`, `31202040=W(10f)`
- `31202048=W(10f)`, `31202050=W(10d)`, `31202058=W(10d)`, `31202060=W(113)`
- `31202008=W(1)`

#### 4: A1NOC

INTID 201 (SPI 169). Native record `d8101980`. OEM record `d80e9320`.

- `016e0240=W(1)`, `016e0088=W(1)`, `016e0008=W(3)`

#### 5: A2NOC

INTID 234 (SPI 202). Native record `d8101ac8`. OEM record `d80e9368`.

- `01700240=W(3f)`, `01700088=W(1)`, `01701040=W(105)`, `01701048=W(105)`
- `01701050=W(105)`, `01701008=W(1)`, `01700008=W(3)`

#### 6: SYSTEM_NOC

INTID 200 (SPI 168). Native record `d8101c10`. OEM record `d80e93b0`.

- `01681040=W(7f)`, `01680088=W(1)`, `01680108=W(1)`, `01680188=W(1)`
- `01680288=W(1)`, `01680008=W(3)`

#### 7: MMSS_NOC

INTID 186 (SPI 154). Native record `d8101d58`. OEM record `d80e93f8`.

- `01780440=W(7ff)`, `01780088=W(1)`, `01780108=W(1)`, `01780188=W(1)`
- `01780208=W(1)`, `01780288=W(1)`, `01780308=W(1)`, `01781040=W(105)`
- `01781048=W(105)`, `01781050=W(105)`, `01781058=W(105)`, `01781008=W(1)`
- `01780008=W(3)`

#### 8: LPASS_AG_NOC

INTID 323 (SPI 291). Native record `d8101ea0`. OEM record `d80e9440`.

- `07f40240=W(22f)`, `07f41040=W(105)`, `07f41048=W(105)`, `07f41050=W(105)`
- `07f41058=W(10d)`, `07f41060=W(106)`, `07f41008=W(1)`, `07f40008=W(3)`

#### 9: PCIE_ANOC

INTID 238 (SPI 206). Native record `d8101fe8`. OEM record `d80e9488`.

- `016c0240=W(1f)`, `016c0088=W(1)`, `016c1040=W(105)`, `016c1048=W(105)`
- `016c1008=W(1)`, `016c0008=W(3)`

#### 10: NSP_NOC

INTID 462 (SPI 430). Native record `d8102130`. OEM record `d80e94d0`.

- `260c0240=W(ff)`, `260c0440=W(105)`, `260c0408=W(1)`, `260c0008=W(3)`

#### 11: DC_NOC_CH02

INTID 299 (SPI 267). Native record `d8102278`. OEM record `d80e9518`.

- `327f0240=W(1ff)`, `327f0440=W(105)`, `327f0448=W(105)`, `327f0450=W(105)`
- `327f0458=W(105)`, `327f0408=W(1)`, `327f0008=W(3)`

#### 12: DC_NOC_CH13

INTID 299 (SPI 267). Native record `d81023c0`. OEM record `d80e9560`.

- `337f0240=W(1ff)`, `337f0440=W(105)`, `337f0448=W(105)`, `337f0450=W(105)`
- `337f0458=W(105)`, `337f0408=W(1)`, `337f0008=W(3)`

#### 13: APSS_NOC

INTID 54 (SPI 22). Native record `d8102508`. OEM record `d80e95a8`.

- `17e80240=W(3fff)`, `17e80440=W(105)`, `17e80448=W(105)`, `17e80450=W(105)`
- `17e80458=W(105)`, `17e80460=W(105)`, `17e80468=W(105)`, `17e80408=W(1)`
- `17e80008=W(3)`

#### 14: LLCLPI_NOC

INTID 299 (SPI 267). Native record `d8102650`. OEM record `d80e95f0`.

- `31330240=W(9dfff)`, `31347040=W(3)`, `31330440=W(105)`, `31330448=W(105)`
- `31330450=W(105)`, `31330458=W(105)`, `31330460=W(105)`, `31330468=W(105)`
- `31330470=W(105)`, `31330478=W(105)`, `31330480=W(105)`, `31330488=W(105)`
- `31330490=W(105)`, `31330498=W(105)`, `313304a0=W(105)`, `313304a8=W(105)`
- `313304b0=W(105)`, `313304b8=W(105)`, `31330408=W(1)`, `31346040=W(105)`
- `31346048=W(105)`, `31346008=W(1)`, `31330008=W(3)`

#### 15: QTB_NOC_LPASS

INTID 391 (SPI 359). Native record `d8102798`. OEM record `d80e9638`.

- `007b6240=W(7)`, `007b6008=W(1)`, `007b6440=W(107)`, `007b6448=W(107)`

#### 16: QTB_NOC_NSP

INTID 392 (SPI 360). Native record `d81028e0`. OEM record `d80e9680`.

- `007d6240=W(f)`, `007d6008=W(1)`, `007d6088=W(1)`, `007d6440=W(107)`
- `007d6448=W(107)`

### SM8845 NOC records and immediate operations

`W` means a 32-bit write. `B0` means read/clear-bit-zero/OR. Values and addresses are hexadecimal.

Global writes, before the monitor loop: `31100408=W(7)`, `31180408=W(7)`, `31100808=W(7)`, `31180808=W(7)`, `31100c08=W(7)`, `31101008=W(7)`.

#### 0: CNOC_MAIN

INTID 226 (SPI 194). Native record `d8200db0`. OEM record `d81e8f50`.

- `01500240=W(5)`, `01500440=W(1fffffff)`, `01501040=W(106)`, `01501048=W(106)`
- `01501050=W(106)`, `01501058=W(106)`, `01501060=W(106)`, `01501068=W(106)`
- `01501070=W(106)`, `01501078=W(106)`, `01501080=W(106)`, `01501088=W(106)`
- `01501090=W(106)`, `01501098=W(106)`, `015010a0=W(106)`, `015010a8=W(106)`
- `015010b0=W(106)`, `015010b8=W(106)`, `015010c0=W(107)`, `015010c8=W(106)`
- `015010d0=W(106)`, `015010d8=W(106)`, `015010e0=W(106)`, `015010e8=W(10b)`
- `01501008=W(1)`, `01500008=W(3)`

#### 1: CNOC_CFG

INTID 226 (SPI 194). Native record `d8200ef8`. OEM record `d81e8f98`.

- `01600240=W(ffffffff)`, `01600244=W(ffffffff)`, `01600250=W(3fffffff)`, `01601040=W(106)`
- `01601048=W(106)`, `01601050=W(106)`, `01601058=W(106)`, `01601060=W(106)`
- `01601068=W(106)`, `01601070=W(106)`, `01601078=W(106)`, `01601080=W(106)`
- `01601088=W(106)`, `01601090=W(106)`, `01601098=W(106)`, `016010a0=W(106)`
- `016010a8=W(106)`, `016010b0=W(106)`, `016010b8=W(106)`, `016010c0=W(106)`
- `016010c8=W(106)`, `016010d0=W(106)`, `016010d8=W(106)`, `016010e0=W(106)`
- `016010e8=W(106)`, `016010f0=W(106)`, `016010f8=W(106)`, `01601100=W(106)`
- `01601108=W(106)`, `01601110=W(106)`, `01601118=W(106)`, `01601120=W(106)`
- `01601128=W(106)`, `01601130=W(106)`, `01601138=W(106)`, `01601140=W(106)`
- `01601148=W(106)`, `01601150=W(106)`, `01601158=W(106)`, `01601160=W(106)`
- `01601168=W(106)`, `01601170=W(106)`, `01601178=W(106)`, `01601180=W(106)`
- `01601188=W(106)`, `01601190=W(106)`, `01601198=W(106)`, `016011a0=W(106)`
- `016011a8=W(106)`, `016011b0=W(106)`, `016011b8=W(106)`, `016011c0=W(106)`
- `016011c8=W(106)`, `016011d0=W(106)`, `016011d8=W(106)`, `016011e0=W(106)`
- `016011e8=W(106)`, `016011f0=W(106)`, `016011f8=W(106)`, `01601200=W(106)`
- `01601208=W(106)`, `01601210=W(106)`, `01601218=W(106)`, `01601220=W(106)`
- `01601228=W(106)`, `01601230=W(106)`, `01601008=W(1)`, `01601440=W(106)`
- `01601448=W(106)`, `01601408=W(1)`, `01600008=W(3)`

#### 2: DC_NOC_DCH

INTID 290 (SPI 258). Native record `d8201040`. OEM record `d81e8fe0`.

- `310e0240=W(ffffff)`, `310e0440=W(105)`, `310e0448=W(106)`, `310e0450=W(106)`
- `310e0458=W(105)`, `310e0460=W(105)`, `310e0468=W(105)`, `310e0470=W(105)`
- `310e0478=W(105)`, `310e0480=W(105)`, `310e0408=W(1)`, `310e0008=W(3)`

#### 3: GEM_NOC

INTID 108 (SPI 76). Native record `d8201188`. OEM record `d81e9028`.

- `31201040=W(7ffff)`, `31100008=B0(1)`, `31180008=B0(1)`, `31200008=B0(1)`
- `31200408=B0(1)`, `31202040=W(10f)`, `31202048=W(10f)`, `31202050=W(10d)`
- `31202058=W(10d)`, `31202060=W(113)`, `31202008=W(1)`

#### 4: A1NOC

INTID 201 (SPI 169). Native record `d82012d0`. OEM record `d81e9070`.

- `016e0240=W(1)`, `016e0008=W(3)`

#### 5: A2NOC

INTID 234 (SPI 202). Native record `d8201418`. OEM record `d81e90b8`.

- `01700240=W(3f)`, `01700840=W(105)`, `01700848=W(105)`, `01700850=W(105)`
- `01700808=W(1)`, `01700008=W(3)`

#### 6: SYSTEM_NOC

INTID 200 (SPI 168). Native record `d8201560`. OEM record `d81e9100`.

- `01681040=W(7f)`, `01680008=W(3)`

#### 7: MNOC_HF

INTID 186 (SPI 154). Native record `d82016a8`. OEM record `d81e9148`.

- `017c0440=W(ff)`, `017c1040=W(105)`, `017c1048=W(105)`, `017c1050=W(105)`
- `017c1008=W(1)`, `017c0008=W(3)`

#### 8: MNOC_SF

INTID 605 (SPI 573). Native record `d82017f0`. OEM record `d81e9190`.

- `01780440=W(1f)`, `01781040=W(105)`, `01781048=W(105)`, `01781008=W(1)`
- `01780008=W(3)`

#### 9: LPASS_AG_NOC

INTID 323 (SPI 291). Native record `d8201938`. OEM record `d81e91d8`.

- `07f40240=W(22f)`, `07f41040=W(105)`, `07f41048=W(105)`, `07f41050=W(105)`
- `07f41058=W(10d)`, `07f41060=W(106)`, `07f41008=W(1)`, `07f40008=W(3)`

#### 10: PCIE_ANOC

INTID 238 (SPI 206). Native record `d8201a80`. OEM record `d81e9220`.

- `016c0240=W(1f)`, `016c1040=W(105)`, `016c1048=W(105)`, `016c1008=W(1)`
- `016c0008=W(3)`

#### 11: NSP_NOC

INTID 462 (SPI 430). Native record `d8201bc8`. OEM record `d81e9268`.

- `260c0240=W(3f)`, `260c0440=W(105)`, `260c0408=W(1)`, `260c0008=W(3)`

#### 12: DC_NOC_CH02

INTID 299 (SPI 267). Native record `d8201d10`. OEM record `d81e92b0`.

- `327f0240=W(1ff)`, `327f0440=W(105)`, `327f0448=W(105)`, `327f0450=W(105)`
- `327f0458=W(105)`, `327f0408=W(1)`, `327f0008=W(3)`

#### 13: DC_NOC_CH13

INTID 299 (SPI 267). Native record `d8201e58`. OEM record `d81e92f8`.

- `337f0240=W(1ff)`, `337f0440=W(105)`, `337f0448=W(105)`, `337f0450=W(105)`
- `337f0458=W(105)`, `337f0408=W(1)`, `337f0008=W(3)`

#### 14: APSS_NOC

INTID 54 (SPI 22). Native record `d8201fa0`. OEM record `d81e9340`.

- `17e80240=W(3fff)`, `17e8c040=W(7)`, `17e80440=W(105)`, `17e80448=W(105)`
- `17e80450=W(105)`, `17e80458=W(105)`, `17e80460=W(105)`, `17e80468=W(105)`
- `17e80470=W(105)`, `17e80478=W(105)`, `17e80480=W(105)`, `17e80408=W(1)`
- `17e80008=W(3)`

#### 15: LLCLPI_NOC

INTID 299 (SPI 267). Native record `d82020e8`. OEM record `d81e9388`.

- `31330240=W(9dfff)`, `31347040=W(3)`, `31330440=W(105)`, `31330448=W(105)`
- `31330450=W(105)`, `31330458=W(105)`, `31330460=W(105)`, `31330468=W(105)`
- `31330470=W(105)`, `31330478=W(105)`, `31330480=W(105)`, `31330488=W(105)`
- `31330490=W(105)`, `31330498=W(105)`, `313304a0=W(105)`, `313304a8=W(105)`
- `313304b0=W(105)`, `313304b8=W(105)`, `31330408=W(1)`, `31346040=W(105)`
- `31346048=W(105)`, `31346008=W(1)`, `31330008=W(3)`

## Fixed prerequisite operations

Before NOC table writes, SM8850 `157571a4` performs two read/OR/write operations:

- `00153038 |= 00100000`.
- `00145010 |= 1`.

SM8845 `15749ef0` performs those same operations, with additional conditional work between them:

1. Read `17e80010` and `17e80030`.
2. If the first equals one and the second equals zero, write one to each address below.
3. Addresses: `17e80018`, `01680018`, `01500018`, `01600018`, `31200018`.

These two reads and five conditional writes are part of the bounded immediate inventory.
The model must not force this conditional branch. The values must come from the actual retained banks.

## DDRSS selected layout and operations

Both SoCs select the same physical addresses and values for immediate DDRSS configuration.
Their native DDRSS payload has this layout:

| Offset | Field |
| --- | --- |
| +0 / +8 | LLCC offset tables for its two error blocks |
| +10 / +18 | LLCC bank count four / bank records |
| +20 | LLCC INTID 614 |
| +28 | MC offset table |
| +30 / +38 | MC count four / MC records |
| +40 | MC INTID 257 |
| +48 / +50 | Additional MC offset tables |
| +58 / +60 | Threshold-record count four / threshold records |

The OEM payload is four enable bytes, all one. Bytes +0 and +2 enable LLCC and MC handler registration.
The LLCC native record stride is 60. Its name pointer is +0, first base is +8, and second base is +10.
The MC native record stride is 20. Its name pointer is +0 and base is +8.
The threshold record stride is 38. Its register base is +8 and threshold word is +1c, equal to e.

LLCC offset-table halfwords are `8,c,1c,20,24,28,2c,30,34,50,54,58,60`.
Its second table starts `0,4,28,8,c,10,14,18,1c,20,24`.
The later entries differ between SoCs, but the immediate path does not consume that difference.
Firmware performs these operations for each LLCC bank:

1. It writes one at first-base +58.
2. It ORs 10000 at first-base +c.
3. It writes zero at second-base +0.
4. It writes one at second-base +24.

The MC table starts with halfwords `110,114,118,180,184,1e0`.
Firmware first writes 10000 at MC-base +118. It writes threshold e at threshold-base +2a4.
It then writes 10 at MC-base +114 and ORs 10 at MC-base +118.
The native count equality check permits these threshold/enable operations because both counts equal four.
These are real table-selected writes, not a query for hardware readiness.

### Complete immediate DDRSS and CMSR inventory, common to both SoCs

The table preserves operation order within each bank. `OR` includes a read before its write.

| Group | Address | Operation | Value |
| --- | --- | --- | --- |
| llcc | 31868058 | WRITE | 1 |
| llcc | 3186800c | OR | 10000 |
| llcc | 31870000 | WRITE | 0 |
| llcc | 31870024 | WRITE | 1 |
| llcc | 32868058 | WRITE | 1 |
| llcc | 3286800c | OR | 10000 |
| llcc | 32870000 | WRITE | 0 |
| llcc | 32870024 | WRITE | 1 |
| llcc | 31c68058 | WRITE | 1 |
| llcc | 31c6800c | OR | 10000 |
| llcc | 31c70000 | WRITE | 0 |
| llcc | 31c70024 | WRITE | 1 |
| llcc | 32c68058 | WRITE | 1 |
| llcc | 32c6800c | OR | 10000 |
| llcc | 32c70000 | WRITE | 0 |
| llcc | 32c70024 | WRITE | 1 |
| mc | 31a09118 | WRITE | 10000 |
| mc | 31a052a4 | WRITE | e |
| mc | 31a09114 | WRITE | 10 |
| mc | 31a09118 | OR | 10 |
| mc | 32a09118 | WRITE | 10000 |
| mc | 32a052a4 | WRITE | e |
| mc | 32a09114 | WRITE | 10 |
| mc | 32a09118 | OR | 10 |
| mc | 31e09118 | WRITE | 10000 |
| mc | 31e052a4 | WRITE | e |
| mc | 31e09114 | WRITE | 10 |
| mc | 31e09118 | OR | 10 |
| mc | 32e09118 | WRITE | 10000 |
| mc | 32e052a4 | WRITE | e |
| mc | 32e09114 | WRITE | 10 |
| mc | 32e09118 | OR | 10 |
| cmsr | 01e42008 | WRITE | ff |
| cmsr | 01e4200c | WRITE | bb8 |
| cmsr | 01e0105c | WRITE | 1 |

## CMSR selected layout

The native payload contains an offset-table pointer at +0, agent count one at +8, and agent array pointer at +10.
The agent record stride is 38. Its name pointer is +0 and MMIO base is +8, equal to 01e00000.
The selected agent's subordinate-count field is 17 on SM8850 and 1f on SM8845.
The immediate initializer iterates the top-level count one, not this subordinate count.

The offset-table words are `44000,42008,4200c,105c,44000,44030`.
The immediate initializer uses only the +4,+8,+c words, giving the three CMSR addresses above.
Payload +24 supplies bb8, +34 supplies ff. Firmware writes one at base +105c.
The later count/pointer fields describe deferred handler work. They do not add immediate initialization writes.

## IRQ semantics and real fault production

All INTIDs in this report are architectural interrupt IDs. The corresponding SPI index is INTID minus 32.
NOC uses INTIDs from the per-monitor sections. Shared INTIDs do not imply separate physical GIC lines.
`1575454c` suppresses duplicate NOC registration when an earlier monitor already uses that INTID.
For example, CNOC_MAIN and CNOC_CFG share 226. Several DDR interconnect monitors share 299.
This suppression does not skip those later monitors' configuration writes.

| Source | INTID | SPI | Registration flags | Callback, SM8850 |
| --- | --- | --- | --- | --- |
| NOC monitors | Per-monitor table | Per-monitor table | 40000000 | 15754df4 |
| LLCC_ERR | 614 | 582 | 40000000 | 15756584 |
| MC_ERR | 257 | 225 | 40000000 | 15756584 |
| CMSR agent timeout | 276 | 244 | 40000001 | 15756df8 |
| CMSR core timeout | 231 | 199 | 40000001 | 15756df8 |
| CMSR boot-mode timeout | 277 | 245 | 40000001 | 15756df8 |

All three CMSR registration bytes are one. LLCC and MC registrations each request enable value one.
The NOC registrations use each OEM enable byte, also one.

SM8850 `1567f4fc` stores the callback and context in the software interrupt directory.
It calls `15770a2c` to configure routing and enablement. It does not call the stored fault handler during registration.
`15770a2c` passes flags bit zero to `1575286c`, which controls the GIC configuration bit.
Thus NOC/LLCC/MC request level-sensitive delivery. CMSR requests edge-sensitive delivery.
Flag bit 30 selects the all-core software mask in `1567f7a8`.
Registration success does not prove the presence of a fault and must not assert a GIC input.
Registration failures retain firmware fatal paths. The decoded counts bound the initializer loops.

The tables enable error detection and supply fault-reporting/clear configuration.
They do not produce NOC transactions, ECC errors, MC violations, or CMSR timeout events.
A real event producer must set the retained source status and drive the selected interrupt with the configured trigger semantics.
A deferred handler must consume the actual status and clear the actual source condition.
This investigation does not establish a complete deferred-handler register inventory or every source's clear-bit semantics.
Do not infer write-one-to-clear behavior merely from the table name or the value one.

## Existing ownership and integration constraints

| Address family | Existing owner or precise gap |
| --- | --- |
| LLCC first bases 31868000,32868000,31c68000,32c68000 and second bases +8000 | `QcomLlcc`, `models/qcom/llcc/llcc.h`. Mapped by `platforms/qcom/sm88xx/llcc.lua:69-96`. Reuse these banks. |
| MC bases 31a09000,32a09000,31e09000,32e09000 and threshold bases 31a05000,32a05000,31e05000,32e05000 | `ddr_command` pair sockets from `platforms/qcom/sm88xx/ddr-command.lua:18-33`, with `ddr_phy_llc` fallback. Do not add overlapping MC fault apertures. |
| DDR physical memory and DARE metadata | Existing virtual DDR backing and `QcomLlcc` DARE port contract. The fault initializer is not a new memory producer. |
| GEM/interconnect policy words already in `platforms/qcom/sm88xx/gem-noc.lua` | Existing narrow `gem_noc_configuration*` owners. Their 100-byte windows do not cover this report's GEM error registers. Preserve their state. |
| DDR fabric words selected by XBL | Narrow owners in `platforms/qcom/sm88xx/sm8850/ddr-phy.lua:46-80` and its SM8845 counterpart. Nearby training/policy addresses are not proof of error-register coverage. |
| NOC error addresses in the complete lists above | `QcomFaultMonitor` retains the listed configuration words. It does not produce faults. |
| CMSR 01e42008,01e4200c,01e0105c | `QcomFaultMonitor` retains these words separately from protection base 01e50000. |
| 00153038 and 00145010 | Each word has one retained owner with APSS and CPUCP aliases. The physical control identities remain unknown. |
| GIC delivery | Existing APSS GIC. Add real source signals to the selected SPI lines, not an independent interrupt controller. |
| Common and OEM directory bytes | Loaded RAM owned by the actual authenticated image loader. Do not replace them with model-generated property tables. |

An address mapping alone does not establish physical fault semantics.
`QcomLlcc` currently retains configuration words and has specific request/observation rules.
Its current retained-word behavior does not by itself prove LLCC error clearing, fault latching, or a fault IRQ producer.
The same caution applies to the DDR fallback configuration model.

Preserve all existing DARE region rows, key receipts, translation state, auxiliary-memory relationships, and retained LLCC contents.
Do not reset the LLCC to initialize its fault-monitor sub-blocks.
Do not add another LLCC or DARE state object for these new consumers.
None of the decoded immediate LLCC writes addresses the DARE region-row fields.

## Remaining producer links and limits

1. **Common directory producer: resolved.** It is the selected TZ_QTI_Config image, loaded before QSEE Dev Config and QSEE.
2. **OEM producer: resolved.** It is the selected devcfg image. The native directory is not missing from the package.
3. **Table selection: statically bounded.** Each root has one selector and no optional hardware predicate. Cached identity/revision still governs actual selection.
4. **Runtime modifications: not observed.** The inventory uses the authenticated package's bytes. A prior runtime write could change fields or skip flags.
5. **SM8845 conditional logger state: unresolved at runtime.** Reads 17e80010/17e80030 determine five additional writes. Do not force those values.
6. **Fault producers: unresolved per hardware block.** Neither loader completion nor handler registration creates an actual source fault.
7. **Deferred handler details: not fully decoded.** This artifact bounds immediate setup, not all future fault-dependent MMIO.
8. **Clock prerequisite ownership: resolved.** The two fixed words use shared retained controls, not duplicate state.
9. **Execution coverage: not claimed.** Existing UART logs prove image loading, not execution of this later fault-monitor initializer.

This evidence does not justify empty tables, ready constants, fabricated faults, authentication bypass, or replacement DARE state.

# Bounded SMMUv2 platform

## Status and source identity

SM8850 run114 and SM8845 run100 complete `sbl1_smmu_init` and `boot_smem_init`.
Both then complete `boot_ddr_share_data_to_aop`.

The selected `IMAGES/xbl_sc.elf` has SHA-256
`b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
Its normal DDR post-list calls `sbl1_smmu_init` at `0x14861e54` before `boot_smem_init`.
The supporting implementation is `qcom-rehost@c2579413cf6a166436cc77ed3acf06a21cb06100:qcom_rehost/smmu.py`.
That implementation starts from conditioned state. This model does not copy that state.

Static analysis used ELF PT_LOAD data, Capstone, and the installed UEFI firmware-volume parser.
The selected `IMAGES/uefi.elf` contains one load segment with SHA-256
`b1d43f0595cd3711bd22d260922d36065279e7842f1c220bbb3292929530bddd`.
The reference rehost UEFI load segment differs. Its extracted driver is not the selected driver.
The selected volume independently supplied these PE images:

| Image | SHA-256 |
| --- | --- |
| HALIOMMU | `f84e26b123831a942990fe9f7152d8d86a5dfa391e80d31fcfb9c94d3b551fc4` |
| UFSDxe | `60e978a3e3eb438fed97a797585ababe81d826533e6e67bdb3ea87e1e69eebd7` |

All driver addresses below are PE RVAs. No extracted image or vendor payload enters this repository.

## Selected topology

The selected `IMAGES/xbl_config.elf` has SHA-256
`c35a368853ba57f00c9486d4b8fadeb0ded3a59ce410818a69ac415e3c44d3ef`.
Both embedded FDTs, at file offsets `0x2ac` and `0x10e14`, supply the following IORT data:

- `/soc/iort/APPS_MMU500_SMMU_APP`: base `0x15000000`, Span `0x80000`, model 3, flags 3, and 112 context interrupts.
- The global interrupt GSIV is 97. The configuration interrupt GSIV is zero.
- `/soc/iort/NAMEDNODE_UFS_95MEM`: object `UFS_MEM`, address width 36, and one SID mapping.
- Its input base is `0x03030000`. Its output SID is `0x60`.
- Each mapping references its FDT's APPS SMMU node. The phandle differs between the two FDTs.

`platforms/qcom/sm88xx/sm8850/smmu.lua` retains the complete decoded context GSIV list.
The list is not a consecutive range. In particular, entries 80 through 95 include GSIVs 450, 739, and 722.
The full model aperture is 1 MiB. ID1 places the context banks at aperture offset `0x80000`.
Thus the IORT Span describes the global half, not a second RAM backing.

## Selected consumers

XBL `0x14861f88..0x14862088` initializes the stream and context configuration:

- `0x14862534` reads ID0. `0x14862548` reads ID1.
- `0x14862508` writes SMR at base plus `0x800 + index * 4`.
- `0x148624f4` writes S2CR at base plus `0xc00 + index * 4`.
- The stream loop writes invalid SMRs and S2CR type 2, which is fault, not bypass.
- `0x14862490` writes CBAR at base plus `0x1000 + index * 4`.
- The context loop writes CBAR type 2 and clears CBA2R at `0x1800 + index * 4`.
- `0x14862570` and `0x14862584` access the non-secure sCR0 alias at base plus `0x400`.
- `0x14862594` writes global invalidation at `0x64`, writes TLBSYNC at `0x70`, and polls TLBSTATUS at `0x74`.

The model selects the global bank from the transaction security attribute.
A secure access below `+0x400` reaches the secure bank.
A non-secure access there, and any access at `+0x400..0x7ff`, reaches the non-secure bank.
Each bank controls the transactions of its own Security state.
XBL uses `+0x400`. Thus its configuration reaches the non-secure bank, as before this change.

XBL `0x14861f70..0x14862088` writes `(old & 0xffdff8c8) | 0x00200227` to NSCR0.
This sets CLIENTPD, GFRE, GFIE, GCFGFIE, GSE, and SMCFCFG. It leaves USFCFG clear.
Static review found no HALIOMMU write to sCR0 or NSCR0.
Thus these selected consumers do not establish enabled translation on the UFS path.
Their static configuration selects identity traffic through CLIENTPD, independently of the context tables.
This is not runtime evidence that UEFI executed.

Selected UFSDxe `0x1c47c` obtains the HALIOMMU interface and creates a domain.
At `0x1c564`, it calls domain configuration through interface offset `0x40`.
At `0x1c5a0..0x1c5bc`, it attaches the domain to `UFS_MEM`, with the two remaining selectors zero.
The object string is at `0x21634`.
The default domain-mode word at `0x23c78` is 2, which means that property resolution is still required.
The caller reads the `EnableUfsIOC` property before domain configuration.
Do not copy a resolved value from another execution.

The selected HALIOMMU interface points to the method table at `0xb0d0`.
Its configuration method at `0x20f0` accepts modes 0 and 1.
It clears the domain's TTBR words, MAIR words, and software architecture field.
It selects SCTLR `0xe0` or `0x0a9f00e0`. Both leave SCTLR.M clear.
It then calls the ordinary domain update path at `0x202c`.
This is static evidence for a real disabled-translation configuration, not proof that the current boot reached it.

HALIOMMU `0x1a9c..0x1b0c` programs the context from its domain object:

| Helper | Register |
| --- | --- |
| `0x22ec` | SCTLR |
| `0x253c` | TTBR0 low/high |
| `0x25a4` | TTBR1 low/high |
| `0x226c`, `0x22ac` | MAIR0, MAIR1 |
| `0x260c`, `0x264c` | TCR, TCR2 |

These helpers derive the context base and page stride from ID1.
HALIOMMU `0x1bbc..0x1be0` writes a valid SMR through `0x239c` and writes the selected context index through `0x2388`.
The latter helper writes S2CR at `0xc00 + index * 4`.
The former helper writes SMR at `0x800 + index * 4`.
HALIOMMU `0x23f0` invalidates the context at `0x618`, writes sync at `0x7f0`, and polls `0x7f4`.
The selected driver contains its own page-table construction path at `0x2700`.
The model never replaces that path or inserts page tables.

## QSEE secure configuration

QSEE also programs the APPS SMMU after the IMAGE_HANDOFF admission.
Its accessor table is SM8850 `0x15727100..0x157272d8`.
The SM8845 bit clearer and bit setter are at `0x1571b9e8` and `0x1571ba18`.

- Accessors read and write sCR0 at `+0x0`, sCR1 at `+0x4`, and NSCR0 at `+0x400`.
- Accessors read nsGFAR, nsGFSR and nsGFSYNR0..2 at `+0x440..0x458`.
- Accessors write SMR at `+0x800` and S2CR at `+0xc00`.
- Accessors set and clear one bit in the word at `+0x4000 + (index / 32) * 4`.

The secure bank retains sCR0, sCR1, sCR2, sACR, sGFAR, sGFSR and sGFSYNR0..2.
Non-secure accesses to sCR1 and sCR2 read zero and ignore writes.
The virtual sCR1 reset gives all contexts, 128 stream match groups and one interrupt to the non-secure state.
Secure software can move that boundary at run time.
The model retains the security state table at `+0x4000..0x4fff` with a zero virtual reset.
The model does not consult this table and does not derive a StreamID index from it.
sCR1.NSNUMSMRGO allocates stream groups `0..NSNUMSMRGO-1` to the non-secure state.
sCR1.NSNUMCBO allocates context banks `0..NSNUMCBO-1` to the non-secure state.
The secure state owns the remaining groups and banks.
A non-secure read of IDR0.NUMSMRG reports NSNUMSMRGO.
A non-secure read of IDR0.NUMIRPT reports NSNUMIRPTO.
A non-secure read of IDR1.NUMCB reports NSNUMCBO.
A secure read reports the implemented geometry and the dedicated interrupt count.
The non-secure fault path also records the faulting address in nsGFAR.

### QSEE hardware settings

QSEE reads the `NumSettings` and `RegisterSettings` properties from `RADIO/tz_qti_config.img`.
Each record holds a 64-bit address and a 32-bit value in 16 bytes.
The records start at file offset `0x1130` in both packages.
The writer loops are SM8850 `0x15729940..0x15729964` and SM8845 `0x1571e348..0x1571e36c`.
Each loop stores each value to its address and does not read the target.

| Target group | SM8850 records | SM8845 records | Model owner |
| --- | --- | --- | --- |
| APPS SMMU `+0x22fc..0x2648` | 6 | 4 | Implementation-defined page 2 in the SMMU model |
| APPS SMMU sACR and nsACR | 2 | 2 | Existing secure and non-secure banks |
| `0x151fc010..0x151fc0d8` | 19 | 17 | Retained words with label `hsr` |
| GPU SMMU `+0x22fc..0x2634` | 4 | 4 | Implementation-defined page 2 in the SMMU model |
| GPU SMMU sACR and nsACR | 2 | 2 | Existing secure and non-secure banks |
| `0x03de0010..0x03de0040` | 3 | 3 | Retained words with label `hsr` |
| `gpu_qtb` `0x03de8008`, `0x03de8020` | 2 | 2 | Retained words with label `hsr` |

`platforms/qcom/sm88xx/qsee-retained-controls.lua` creates the labeled retained words.
Every selected vendor_boot DTB declares `gpu_qtb@3de8000` as `qcom,qsmmuv500-tbu` and `qcom,qtb500`.
The SM8850 Canoe SoC DTB at file offset `0x1537df8` declares `apps_smmu_pmu` and `gfx_smmu_pmu`.
Both nodes use `arm,smmu-pmu` at SMMU offsets `+0x2000` and `+0x7000`.
The SM8845 Alor SoC DTB at file offset `0x1034000` does not declare these PMU nodes.
No selected DTB node declares `0x151fc000` or `0x03de0000`. Their hardware identity remains unresolved.
The model retains the guest values with a zero virtual reset. No retained value changes translation, power or a completion state.

### QSEE GMU words

After the SMMU setup, QSEE calls SM8850 `0x156e5d38` from `0x157299c4`.
SM8845 `0x156dacb0` first calls `0x1571c168` with the GPU SMMU base `0x03da0000`.
It writes the words only when that call returns zero.

| Address | Value |
| --- | --- |
| `0x03d60000` | `0x000c0000` |
| `0x03d60004` | `0x000c1000` |
| `0x03d6000c` | `0x000c2000` |
| `0x03d60008` | `0x00000002` |

Both selected Canoe and Alor SoC DTBs declare `qcom,gen8-gmu` at `0x03d37000+0x68000`.
That window contains these words. The KGSL gen8 register header does not name them.
Retained words with label `gmu` keep the values. They do not start or configure a GMU.

## Native run393 Secure SMMU state

The verified state comes from `build/private/run-sm8850-common-runtime393`.
Its `RADIO/tz.img` has SHA-256 `afdfaf27b33616718249ac857b7021001f33fe767c74c6e877cd280a3ee99932`.
That image belongs to the selected CPH2749 16.0.5.700 package.
The rehost lane uses a different `tz.img` and runs no TZ kernel.

| Register | Captured value | Model interpretation |
| --- | --- | --- |
| NSCR0 (`+0x400`) | `0x00200226` | Non-secure CLIENTPD clear, GFRE, GFIE, USFCFG |
| sCR0 (`+0x0`) | `0x20200626` | Secure CLIENTPD clear, GFIE, GFRE, SMCFCFG, sCR0.NSCFG=2 |
| sCR1 (`+0x4`) | `0x0000796f` | NSNUMSMRGO=121, NSNUMCBO=111 |
| SMR127 (`+0x9fc`) | `0x800000c2` | Valid Secure group, StreamID `0xc2`, mask zero |
| S2CR127 (`+0xdfc`) | `0x0308006f` | Type 0, CB111, NSCFG=2 Secure, PRIVCFG=3 |
| CBAR111 (`+0x11bc`) | `0x0001f000` | Stage 1, stage 2 bypass |
| CBA2R111 (`+0x19bc`) | `0x00000001` | AArch64 table format |
| CB111 SCTLR (`+0xef000`) | `0x200500e1` | M=1, CFRE, CFIE, CFCFG, SCTLR.NSCFG=2, MTCFG=0 |
| CB111 TCR (`+0xef030`) | `0x00802d1c` | T0SZ=28, 36-bit input, TG0=4 KiB, EPD1=1 |
| CB111 TCR2 (`+0xef010`) | `0x40038001` | PASize=1 (36-bit), SEP=7, NSCFG0=0, NSCFG1=1 |
| CB111 TTBR0 (`+0xef020`) | `0xd83f7000` | Captured table address |

The sCR1 value allocates groups 0..120 and banks 0..110 to the Non-secure state.
The Secure state owns groups 121..127 and bank 111.
SMR127 and S2CR127 are therefore Secure-only.
S2CR127 selects CB111 without borrowing a Non-secure bank.
S2CR127.NSCFG=2 keeps a downstream access Secure when firmware selects that policy.
SCTLR.NSCFG applies only when M is zero, so this capture does not use it.
TCR.EPD1=1 disables the TTBR1 region.
TCR2.NSCFG1=1 is a documented TTBR1 walk attribute.
The model accepts that bit and creates no TTBR1 path.
TTBR0 is the only walked table.
The captured TTBR0 value is a guest table address, not a fixture physical address.
The model reproduces the register format and the walk rules, not the captured table contents.
A 36-bit input limit bounds this state.

## Crypto IORT mapping

Two selected `tz_qti_config.img` images supply the current CRYPTO translation, not the public device-tree patch.

| Package | `RADIO/tz_qti_config.img` SHA-256 | IORT offset | CRYPTO node |
| --- | --- | --- | --- |
| CPH2749 16.0.5.700 (SM8850) | `46f6b44e0474bb0adf87a8085a87e896ab97d065637259b1f683ee519172bfb6` | `0x14f5e`, length `0x1b1d` | `0x15496` |
| CPH2771 .305 (SM8845) | `b0da9952ca94f0dde273baa4112c334dcfa27e9ffb76393ac5129657cbec9a58` | `0x1283a` | `0x12d72` |

Both nodes report address width 36 and six mappings.
The first mapping is 20 bytes in little-endian order: `{0x81010000, 1, 0xc2, 0x30, 0}`.
Bit 31 of the input base is set, so the mapping is Secure.
QSEE `0x157279f0` treats input-base bit 31 as Secure and bits 23:16 as a VMID.
QSEE `0x15729f84` assigns the secure clients.
Thus StreamID `0xc2` is the current firmware CRYPTO Secure mapping.
The next mappings use input bases `0x03030001` and `0x03030002` and output SIDs `0xc1` and `0xc0`.
Their input-base bit 31 is clear, so they are Non-secure.
The remaining mappings follow the same decode.
The firmware mapping, not a device-tree guess, selects the StreamID for the crypto producer.
The model still requires the matching SMR, S2CR and context state before it translates.
A StreamID value alone does not create a translation.

## Cohort boundary

The reuse rules separate architecture and interface contracts from executable identity.

- Cohort A is the native qcom-vp package CPH2749 16.0.5.700 for SM8850 and CPH2771 .305 for SM8845.
  The captured register state, QSEE offsets, IORT mappings and StreamID `0xc2` belong to cohort A.
- Cohort B is the rehost lane, OnePlus 15 GLO 16.0.10.601.
  It runs no TZ kernel, PBL, XBL, AOP or CPUCP.
  Its `tz.img` SHA-256 `f6317e33e3daf280d39a7912bb103e8a8840e0130e6ff1a4b158920e5e7e3fa2` differs from the cohort A image.
  Do not copy cohort A offsets or register state into cohort B.
- The reusable items are the ARM SMMU rules, the model contract, the stream-port interface and the isolation tests.
  The rehost lane may reuse the contract, never the captured values.

## Post-QSEE context diagnostics

SM8850 `run-sm8850-common-runtime151` stops at APSS PC `0x14862718` on an eight-byte read from `0x150ed070`.
The complete trace places the SMMU abort before the SPMI abort at PC `0x14870300` and FAR `0x0c428000`.
The SPMI abort occurs during simulator shutdown. It is not the initiating failure.
SM8845 run124 completes these SMMU reads and prints the original fatal diagnostics.
The SMMU fault status is zero.
The original handler reports code 72 at `sbl1_mc.c:2450` before its CPUCP cleanup access fails.
This correction exposes that error. It does not resolve the error or establish a successful boot.

Static analysis used the selected XBL image identified above.
The neighboring helpers read this diagnostic group:

| Helper | Instruction | Context offset | Register |
| --- | --- | --- | --- |
| `0x148625d0` | `0x14862608`, 32-bit read | `0x58` | FSR |
| `0x14862614` | `0x1486264c`, 64-bit read | `0x60` | FAR |
| `0x14862658` | `0x14862690`, 32-bit read | `0x68` | FSYNR0 |
| `0x1486269c` | `0x148626d4`, 32-bit read | `0x6c` | FSYNR1 |
| `0x148626e0` | `0x14862718`, 64-bit read | `0x70` | IPAFAR |

Each helper reads IDR1 at base plus `0x24`.
Bit 31 selects a 12-bit or 16-bit page shift.
Bits 30:28 select the global size, in units of `2 << NUMPAGENDXB` pages.
Each helper then adds the context index shifted by 12.
With the advertised APPS geometry, `0x150ed070` selects context 109.

[Arm IHI 0062D.b](http://kib.kiev.ua/x86docs/ARM/SMMU/IHI0062D_b_system_mmu_architecture_specification.pdf)
Table 17-1 assigns IPAFAR to context offsets `0x70..0x77`.
Section 17.3.2 defines IPAFAR as a read-only FAR alias when nested translation is absent.
Writes to that alias have no effect.
Section 16.5.11 defines FSYNR0.WNR, PNU, PTWF, and PLVL.
It leaves FSYNR1 fields implementation-defined.

The model now supports the complete diagnostic group for each active context.
Both IPAFAR words read the corresponding FAR words. They do not use independent writable storage.
Reset diagnostics remain zero. Software can still restore FAR and the syndrome registers.
A first context fault records FAR, access direction, selected privilege, and the applicable walk level.
An external descriptor-read fault also sets FSYNR0.PTWF.
Pre-walk faults use level zero. A first fault clears implementation-defined FSYNR1 to zero.
Later faults retain these diagnostics and set MULTI.
Existing FSR clearing, IRQ delivery, translation decisions, and permission checks remain unchanged.

The stage-1 register map reserves IPAFAR. This model exposes the alias there for firmware diagnostic enumeration.
The alias does not fabricate a successful translation.
A stage 2 fault records its IPA in the FAR of the stage 2 bank. See "Stage 2 and nested translation".
Vendor-specific FSYNR1 fields and stalled transactions remain unsupported.

The existing SMMU fixture now covers context 109 through firmware-derived geometry.
It checks reset diagnostics, 32-bit and 64-bit IPAFAR reads, ignored alias writes, and separate context state.
Fault checks compare IPAFAR with FAR, including addresses above 32 bits and retained first faults.
The fixture also checks syndrome replacement, leaf walk levels, privileged faults, and pre-walk level zero.
The complete integration run passes 97 tests and Clippy with warnings denied.
SM8845 run124 reads FAR, IPAFAR, FSYNR0, FSYNR1, and CBFRSYNRA as zero for context 109.
It then reports error code 72 at `sbl1_mc.c:2450`.
The retained UART establishes successful diagnostic reads, not further boot progress.

## Model contract

`qcom_vp::ArmSmmuV2` implements `arm,smmu-v2-v1`.
Its bounded virtual capabilities include 128 stream groups, 4 KiB register pages, and AArch64 stage 1.
The APPS instance has 112 contexts. The GPU instance has 25 contexts.
ID values describe that virtual implementation. They are not captured silicon ID values.
`context_count` selects 1 through 112 active banks within fixed-capacity storage.
`global_span` selects a global half of `0x20000` or `0x80000` bytes.
ID1 advertises that size and context count. Context registers start at the global half boundary.
The MMIO aperture contains two halves. Inactive contexts return address errors.
The GPU instance has no producer. It needs no stream port.
See `sm8845-evidence.md` for both selected GPU nodes and their interrupt order.

One `ArmSmmuV2` owns every register, stream match, context bank and translation decision.
`qcom_vp::ArmSmmuStreamPort` implements `arm,smmu-stream-port-v1`.
Its constructor takes the owner object pointer.
It has a required 16-bit `stream_id`, one target socket and one initiator socket.
A producer binds its DMA output to `port.target_socket`.
The port binds `port.initiator_socket` to that producer's physical egress.
The port passes its StreamID, the transaction and the physical bus to the owner.
The owner keeps no per-producer copy of SMMU state.
Two ports can share one owner with different StreamIDs, input security and physical egress.
The Lua factory passes the owner with `args={'&apps_smmu'}`.

Reset sets CLIENTPD and faults all stream entries. Firmware must program later state.
The following control bits can select identity traffic:

1. sCR0.CLIENTPD disables the client interface.
2. An unmatched stream bypasses only when sCR0.USFCFG is clear.
3. A matching S2CR with type 1 selects bypass.
4. A stage-1-only context with SCTLR.M clear disables translation.

Security ownership follows IHI 0062D.b sections 2.3.9, 3.7 and 9.6:

1. sCR1.NSNUMSMRGO allocates groups `0..NSNUMSMRGO-1` to the non-secure state.
2. sCR1.NSNUMCBO allocates context banks `0..NSNUMCBO-1` to the non-secure state.
3. A secure transaction matches only secure-owned groups.
4. A non-secure transaction matches only non-secure-owned groups.
5. Two groups can carry the same StreamID when they belong to different states.
6. The register owner selects one partition from the transaction security attribute.
7. sCR1 and sCR2 are secure-only. A non-secure access reads zero and ignores writes.
8. A non-secure access to a secure-owned group, bank, context or CBFRSYNRA is RAZ/WI.
9. A non-secure read of IDR0.NUMSMRG reports NSNUMSMRGO.
10. A non-secure read of IDR1.NUMCB reports NSNUMCBO. The secure view keeps the implemented counts.
11. A stream group may select only a context bank of its own security state.
12. A non-secure group that names a secure bank faults with UUT. It borrows no mapping.
13. An absent or unspecified input attribute is Non-secure. It never becomes Secure.

NSCFG selects the downstream security attribute after context determination:

- sCR0.NSCFG applies to a secure transaction that bypasses the stream table.
- S2CR.NSCFG applies to a bypass or translated transaction from a secure stream group.
- SCTLR.NSCFG applies when that context has M clear. It exists only in a secure bank.
- The encodings are 0 default, 1 reserved, 2 secure and 3 non-secure.
- SMMUv2 interprets the reserved encoding as non-secure.
- A non-secure origin never produces a secure walk or a secure downstream access.

An enabled translation context uses the selected SMR, S2CR, CBAR, CBA2R, SCTLR, TCR, TCR2, and TTBR0.
The walker reads each descriptor from the port's physical bus.
It supports 4 KiB pages and level-1/level-2 blocks, with 25 through 48 input address bits.
It enforces access flags, leaf AP bits, inherited table permissions, and the S2CR privilege selection.
It checks physical address width, block alignment, descriptor form, and the implemented context range.

TCR2.NSCFG0 selects the TTBR0 walk attribute.
NSCFG0=0 keeps a secure walk secure. NSCFG0=1 selects non-secure tables.
TCR2.NSCFG1 is stored for the TTBR1 walk attribute.
TTBR1 selection and TTBR1 traffic remain unsupported, so NSCFG1 changes no model behavior.
A table descriptor with NSTable set makes the next-level table walk non-secure.
A leaf or block descriptor with NS set makes the downstream access non-secure.
A non-secure walk or a non-secure origin stays non-secure.
Extended stream IDs, AArch32 translation, big-endian walks, other granules, and TTBR1 traffic fault.
HA, HD, HAD and TBI remain unsupported and fault with UUT.
Multiple stream matches fault when SMCFCFG is set. They bypass when SMCFCFG is clear, as the architecture specifies.
Invalid descriptors never select identity traffic.

The model has a bounded, direct-mapped TLB for successful leaf translations.
It caches stage-1-only, stage-2-only, and nested stage-1 plus stage-2 outcomes.
Each entry stores a 4 KiB VA page, a PA page, and downstream security.
The key includes input security, stream ID, stream match, stage-1 context bank, stage-2 bank, bus, access direction, and privilege.
Access-specific entries preserve the walker's permission checks. The model never caches faults or bypass outcomes.
Every MMIO register write flushes the TLB. This includes TLBI writes and writes to configuration or context-bank registers.
TLBI status reads report no pending work because invalidations complete synchronously.
Guest table changes without TLBI can remain stale, as they can on hardware.
DMA splits at 4 KiB boundaries. Writes validate all translations before the first physical store.
Each forwarded segment rechecks its translation. A physical bus error can leave partial DMA, as on an ordinary bus.
The model does not promise transaction atomicity or allocate a segment vector.

Faults return a TLM error and latch global or context fault state.
A global fault latches in the global bank of the faulting transaction's security state.
A context fault latches in the selected context bank.
The first fault retains its status, address, access direction, and stream ID.
Later faults set MULTI without replacing that state. FSR and GFSR use write-one-to-clear semantics.
Context IRQs use the context index and SCTLR.CFIE, not CBAR.IRPTNDX.
ID0.NUMIRPT is one, which selects dedicated context pins. ID0.ATOSNS is set because address translation operations are not implemented.
The global IRQ asserts when either bank has pending status and that bank's GFIE is set.
A non-secure configuration access cannot read or clear a secure context fault.
Stalled transaction retry and separate configuration interrupt delivery are not implemented.
The model always returns an error for faults, including when GFRE or CFRE is clear.
It does not implement the architecture's abort-suppression or stall behavior.
Unsupported modes terminate the DMA with an error. They never complete the operation successfully.

## Integration contract

`src/platform.cc` registers `qcom_vp::ArmSmmuV2` as `arm,smmu-v2-v1`.
It registers `qcom_vp::ArmSmmuStreamPort` as `arm,smmu-stream-port-v1`.
The stream-port factory receives the owner object as its constructor argument.

Both runtime compositions apply `platforms/qcom/sm88xx/sm8850/smmu.lua` after the UFS physical path exists.

Each runtime binds GIC GSIVs to SPI indices through its existing `irq_bind` function.
The helper requires the existing physical protection input. It rejects a direct RAM-only replacement.
The helper creates the APPS SMMU owner and the UFS stream port:

```text
UFS -> ufs_dma_access -> ufs_smmu_port.target_socket
    -> ufs_smmu_port.initiator_socket -> ufs_dma_protection -> ufs_ram_router
```

The crypto producer uses the same owner with the firmware-selected Secure StreamID:

```text
crypto_bam.dma_socket -> crypto_smmu_port.target_socket
    -> crypto_smmu_port.initiator_socket -> apss_router
```

The port initiator carries both translated data and page walks.
Each producer therefore keeps its own physical egress.
This avoids binding two initiators to one protection port's simple target socket.
Translated data and page walks copy every other upstream attribute onto a stack holder.
The security attribute is resolved at the SMMU boundary and restored before return.
No path creates a per-transfer heap object, a RAM model or an alternate security route.
All CPU and DMA aliases must continue to share the actual RAM backing.

The OEM boot must retain actual firmware and kernel output.
Do not require nonidentity translation from XBL and HALIOMMU while their selected path leaves CLIENTPD set.
No current evidence proves real firmware-owned nonidentity DMA or Linux execution.

## Consumer regression and semantic references

The native guest fixture is `tools/qcom/qcom-vp/tests/fixtures/smmu`.
It programs its own tables and enters through one `ArmSmmuStreamPort` with StreamID `0x60`.
The guest executes at EL3, so every probe transaction is Secure.
The fixture therefore assigns all groups and context banks to the Secure state with sCR1=0 before translation.
This aperture is test-only. It does not replace production UFS or change production runtime wiring.
Expected DMA errors reach the guest's synchronous-abort handler. Only terminal markers use the existing AccessMonitor harness.
The guest checks nonidentity reads and writes, discontiguous pages, addresses above 32 bits, and a 512-byte initial table.
It also checks context seven with IRPTNDX zero, IRQ masks, write-one-to-clear transitions, and first-fault retention.
Boundary cases include EPD0, VA range, invalid descriptors with high address bits, PA width, and unsupported granules.
Global cases include stream conflicts, unmatched streams, S2CR fault encodings, invalid context indices, and unsupported CBAR types.
The guest also checks that a Secure global fault leaves the Non-secure GFSR zero.
The regression explicitly forces privileged and unprivileged attributes when it checks privileged-only leaves.
It does not establish the correct default privilege of physical UFS traffic.

The native Secure fixture is `tools/qcom/qcom-vp/tests/fixtures/smmu/consumer.cpp`.
It is not yet registered in `platform.rs`.
Add `run_native_consumer("smmu", "SMMU_STREAM_SECURITY_PASS")` to the registration.
It uses the captured run393 format for SMR127, S2CR127, CBAR111, CBA2R111, SCTLR, TCR and TCR2.
It gives two ports the same StreamID `0xc2` with different input security and separate physical egress.
It checks these behaviors:

- A Secure transaction matches SMR127 and CB111 and reaches only the Secure egress.
- A Non-secure transaction with the same StreamID matches SMR0 and CB0 and reaches only the Non-secure egress.
- A Non-secure S2CR that names CB111 faults with UUT and touches no memory.
- A Non-secure read of sCR1, SMR127, CBAR111, CBA2R111 or the CB111 fault state returns zero.
- The Non-secure IDR0 reports NUMSMRG 121 and NUMIRPT 0. IDR1 reports NUMCB 111.
  The Secure view keeps 128, 1 and 112.
- S2CR.NSCFG=3 sends both the walk and the data downstream as Non-secure.
- SCTLR.NSCFG selects the identity security state when M is clear.
- A leaf NS bit makes only the data access Non-secure.
- NSTable makes the next-level walks Non-secure and never returns to Secure.
- The captured TCR2 with NSCFG1=1 still walks TTBR0. A VA at bit 36 faults.
- No unspecified attribute reaches an egress, and no Non-secure origin produces a Secure access.

Run these integrated checks after the change. This section does not claim a pass:

```sh
make build
cargo test --locked -- --include-ignored
cargo clippy --locked --all-targets -- -D warnings
```

The GPU fixture checks the last active context and rejects the first inactive context across its register arrays.
This proves the synthetic DMA and configuration contracts, not a Linux boot or firmware-owned translated UFS transfer.

SM8850 run114 reaches the native SMMU success tail at `0x14862130` with inventory count two.
The guest then publishes SMEM flag `0x14a0c0d8=1`.
Its `$TOC` header at `0x81eff000` reports version one and twenty entries.
Both OEM UART logs contain the SMMU, SMEM, and AOP-data callback markers.
The next failure is SHRM request `0x05030000`, not an SMMU access.
Logs remain under `build/private/run-sm8850-common-runtime114` and `build/private/run-sm8845-common-runtime100`.

The semantic references are not implementation sources for this BSD-licensed header:

- [Arm IHI 0062D.b](http://kib.kiev.ua/x86docs/ARM/SMMU/IHI0062D_b_system_mmu_architecture_specification.pdf),
  sections 2.3.9, 2.3.10, 3.3, 3.6.2, 3.7, 3.8, 9.6.1, 9.6.3, 9.6.15, 10.2.1, 16.5.38, 16.5.39, and 16.5.40.
  Section 2.3.9 defines the common stream table and the Security ownership of its groups.
  Section 2.6 defines the NSCFG encodings and the reserved-value rule.
  Table 16-1 places TCR2 at context offset `0x10`, TCR at `0x30` and CONTEXTIDR at `0x34`.
- Linux `drivers/iommu/arm/arm-smmu/arm-smmu.c`, revision `fc30e54174d254ff7f33622a9278e4435f6718d2`.
  Context setup uses `cbndx` as the v2 interrupt index. Capability discovery tests ATOSNS before enabling translation operations.
- [QEMU `hw/arm/smmu-common.c`](https://github.com/qemu/qemu/blob/master/hw/arm/smmu-common.c),
  `select_tt` and `smmu_ptw_64_s1`, for VA-range faults, disabled walks, initial table size, and invalid descriptor precedence.
  Linux and QEMU are GPL-licensed references. No code was copied from them.

TTBR1 traffic, AArch32, other granules, big-endian walks, extended stream IDs, and translation operations remain unsupported.
Vendor IMPL_DEF registers, hardware-managed access flags, transaction retry, and full Linux probe support are not implemented.
The Qbox SMMU500 is not a replacement for this path.
Its 32-bit VA handling, stream semantics, whole-transfer single-page translation, and separate walk/DMI path do not satisfy this contract.

## Post-DDR selected UEFI closure

This section adds static evidence for both selected OEM images.
It does not establish native SEC, DXE, or ABL execution.
No model change is required by these selected consumers.

Both selected configuration FDTs set
`/sw/uefi/int_param/EnableUfsIOC` to the 64-bit value `1`.
The FDT starts at ELF offset `0x10e14` on SM8850 and `0xf52c` on SM8845.
Thus the selected property supplies mode 1 to HALIOMMU when runtime resolution succeeds.
The property does not authorize forcing a successful lookup.
Mode 1 selects SCTLR `0x0a9f00e0`, with translation disabled.
The existing shared register backing and synchronous invalidations cover this path.

HALIOMMU's explicit DEPEX requires all twelve architectural protocols:
BDS, CPU, Metronome, MonotonicCounter, RTC, Reset, Runtime, Security,
Timer, VariableWrite, Variable, and Watchdog.
Its protocol install call is PE RVA `0x1578` in both selected images.
The static interface is at PE RVA `0xb0b8`.
Its GUID is `54b6d3b4-5d33-4f91-8600-6c41d5deb19a`.
Initialization after publication calls RVA `0x52f4`.
Observe both the install return and this initialization result.

The critical dependency order is:

```text
SmemDxe -> DALSys -> HWIO -> ChipInfo
CmdDbDxe + DALSys -> RpmhDxe
CmdDbDxe + DALSys + ChipInfo -> ClockDxe
ArmGicDxe -> ArmCpuDxe + ArmTimerDxe
twelve architectural providers -> HALIOMMU
DALSys -> PILDxe
UFSDxe -> block handles -> partition discovery -> PILDxe -> ABL FV
ABL FV -> LinuxLoader LoadImage -> actual DXE StartImage dispatch
```

This order describes producer prerequisites, not a forced dispatcher schedule.
UFSDxe and QcomBds have explicit `TRUE` DEPEX expressions.
They still have runtime protocol dependencies.
An absent DEPEX is different from `TRUE`.
The dependency evaluator requires `CoreAllEfiServicesAvailable()` for an absent DEPEX.
The EDK II dispatcher implements this rule when `DriverEntry->Depex` is null.
See [Dependency.c at edk2-stable202605](https://github.com/tianocore/edk2/blob/edk2-stable202605/MdeModulePkg/Core/Dxe/Dispatcher/Dependency.c).
This public source supports the dispatcher rule, not proof that the selected firmware ran this exact implementation.
This rule affects ScmDxeCompat, VerifiedBootDxe, and several storage consumers.
Do not install host-created protocols to release their dependencies.

Both selected ArmTimerDxe images use the architectural virtual timer.
The IRQ selector at PE RVA `0x3ee8` is INTID 27.
Initialization starts at RVA `0x1608`.
Counter helpers access CNTFRQ at `0x3d58`, CNTVCT at `0x3d70`,
CNTV_CTL at `0x3d60`/`0x3d68`, and CNTV_CVAL at `0x3d78`/`0x3d80`.
The APSS MMIO timer frames do not replace these system-register interfaces.
The platform composition must preserve the CPU virtual-timer route to GIC INTID 27.

PILDxe resides in the second guided FV, not the core DXE FV.
The enclosing FFS GUID is `d14c51ed-4f4d-2f2d-eae4-77914692d046`.
Its guided section starts at UEFI ELF offsets `0x1040c8` / `0xff1f8`.
The complete decoded buffers, including their eight-byte prefixes, have SHA-256:

- SM8850: `c4d00e9a5cad26148150274de8c6780dc3072cf3533fabcd22d98f5b7150ca55`.
- SM8845: `321caa1db4fa3b294ab76812d5562b1be4d47fe81c75608ce105f008506040ba`.

Both selected board FDTs name `LinuxLoader` as `DefaultBDSBootApp`.
Their PIL AUTO list is empty.
Their ABL configuration uses Type 1, subsystem ID `0x15`, and Unlock 1.
The partition type GUID is `bd6928a1-4ce0-a038-4f3a-1495e3eddffb`.
Both prepared manifests identify this partition as `abl_a`.
PILDxe installs its protocol at RVA `0x1748`.
The interface at RVA `0x161b8` exposes ProcessPilImage at offset 8, targeting RVA `0x1790`.
The protocol GUID is `1f4f1da3-4d8d-4894-ba2a-13d3207f9739`.

The selected PIL FV method table is at RVA `0x117a8` / `0x117e8`.
It selects load `0x30d4`, metadata copy `0x3468`, validation `0x2a70`,
memory setup `0x2b84`, authentication `0x3d7c`, and post-load `0x3e9c`.
Authentication calls the real SCM command `0x02000205`.
Post-load conditionally calls unlock command `0x02000206`, then processes the FV.
These calls remain secure-firmware-owned decisions.
No SMMU completion bit can substitute for their returns.

BDS calls ProcessPilImage for UTF-16 `ABL` at RVA `0x64b4` / `0x64e0`.
The common guided-app helper calls LoadImage at `0x121ac` and StartImage at `0x122b8`.
These service sites do not identify the selected image by themselves.
The actual DXE dispatch at RVA `0x6f44` must use the selected ABL entry bytes.
Derive its relocated base from the runtime entry minus RVA `0x189c`.
Keep guest FV backing, image pages, and provider interfaces alive through their real consumers.
Temporary metadata cleanup must not remove mounted FV or loaded-image pages.

First-run SMMU probes must retain the resolved property, domain mode, CR0,
SMR/S2CR, SCTLR, TTBRs, and the first UFS DMA address.
Observe actual block bytes, PIL authentication returns, FV publication, and ABL dispatch separately.
CLIENTPD or SCTLR.M clear permits identity DMA but does not prove translated DMA.
No HOBs, protocols, page tables, partition bytes, or success returns are supplied by this analysis.

## Fault-status translation format

The original SM8850 fault scanner starts at `0x14862138`.
It skips contexts with `CBAR.TYPE=2`.
For the other contexts, it reads FSR and compares the complete word with `0x400` at `0x148621e0`.
A different value enters the diagnostic path and returns `0x72`.
Thus zero fault bits alone do not satisfy this original check.

[Arm IHI 0062D.b](http://kib.kiev.ua/x86docs/ARM/SMMU/IHI0062D_b_system_mmu_architecture_specification.pdf), section 16.5.9, defines FSR.Format in bits `[10:9]`.
This read-only field derives from `CBA2R.VA64` and the effective `TCR.EAE` value.
The AArch64 encoding is two, which contributes `0x400` without indicating a fault.
The architecture excludes Format from its fault predicate and interrupt condition.
Linux also separates `ARM_SMMU_CB_FSR_FORMAT` from `ARM_SMMU_CB_FSR_FAULT` in its [SMMU register definitions](https://github.com/torvalds/linux/blob/master/drivers/iommu/arm/arm-smmu/arm-smmu.h).

The model now derives Format during each FSR read.
It stores only fault state in the backing word.
W1C writes cannot clear the format field.
Format alone cannot assert an interrupt, retain a first fault, or set MULTI.
This change does not add AArch32 translation or permit unsupported context types.

The fixture checks format changes, W1C behavior, and the interrupt state before any fault.
Its existing fault checks now require the format field alongside the actual fault bits.
All 97 tests pass, and Clippy passes with warnings denied.

SM8850 run156 and SM8845 run125 complete the original `sbl1_smmuv2_fault_scan`.
Both subsequently load QHEE and APPSBL and report `SBL1, End`.
Both next read XPU offset `0x20c` at system address `0x0152020c`.
The PCs are `0x14859f80` and `0x148584b4`, respectively.
These logs establish SBL1 completion, not UEFI, ABL, kernel execution, or authentication qualification.

## Stage 2 and nested translation

QHEE owns the APPS SMMU in UEFI. It traps the UEFI HALIOMMU attach for UFS stream `0x60` and programs the hardware itself.
Run674 recorded these values:

| Register | Value | Writer | Meaning |
| --- | --- | --- | --- |
| SMR0 | `0x80000060` | QHEE | Valid, ID `0x60`, mask 0 |
| S2CR0 | `0x00000000` | QHEE | Translation context, CB0 |
| CBAR0 | `0x00036a03` | QHEE | Type 3 nested, CBNDX `0x6a`, low byte `0x03` |
| CB0 SCTLR | `0x0a9f00e0` | UEFI | M=0 disables stage 1 |
| CBAR106 | `0x00000003` | QHEE | Type 0 stage 2, VMID 3 |
| CBA2R106 | `0x00000001` | QHEE | AArch64 tables |
| CB106 SCTLR | `0x00cf00e1` | QHEE | M=1, CFRE, CFIE, CFCFG |
| CB106 TTBR0 | `0x8bd900000` | QHEE | Stage 2 root table |
| CB106 TCR | `0x0001355c` | QHEE | T0SZ=28, SL0=1, 4 KiB granule, PASize=1 |

The model accepted only CBAR type 1. The first UFS transfer therefore faulted with GFSR bit 0 (run670 through run674).

IHI 0062D.b defines CBAR type 0 as a stage 2 context and type 3 as stage 1 nested in the stage 2 bank CBNDX.
The model now implements both types for Non-secure stream groups.
A Secure group that selects type 0, 2, or 3 still faults, because stage 2 exists only in the Non-secure state.

The stage 2 walk uses the AArch64 stage 2 TCR format with a 4 KiB granule.
T0SZ sets the IPA size from 25 to 48 bits. SL0 selects the start level from 0 to 2.
The start level can resolve up to 13 bits, which allows 16 concatenated tables.
PASize bounds the table and output addresses.
Walks are Non-secure. A leaf needs the access flag unless SCTLR.AFFD disables access flag faults.
S2AP bit 6 permits reads, and bit 7 permits writes.
A disabled stage 2 bank passes the IPA unchanged.

In a type 3 bank, stage 2 translates each stage 1 table address and the final IPA.
With stage 1 disabled, the input address is the IPA.
The stage 2 bank records a stage 2 fault. Its FAR holds the IPA, and CBFRSYNRA holds the StreamID.
IDR0 now reports S2TS and NTS.
Memory attributes, VMID-tagged invalidation, and stage 2 fault syndromes in the stage 1 bank are not modeled.

The native consumer checks a stage 2 bank, a nested bank with stage 1 disabled, and an S2AP write fault.
It also checks a nested stage 1 walk and a stage 1 table read that faults at stage 2.

# Functional substitutes

## Modeling policy

Ordinary hardware uses permissive stateful behavior.
Configuration requests retain their values, reset state is deterministic, and modeled operations produce their required effects.
Analog effects can be idealized. Exact silicon details are not prerequisites for an explicitly identified virtual substitute.

Unknown accesses can receive exploratory responses when the model logs each access prominently.
Those responses remain substitutes, not evidence of correct hardware behavior.
Blanket successful results without corresponding state do not satisfy this policy.

Each declared substitute records the observation, the inferred manufacturer or family pattern, or the virtual policy behind it.
The record also states the limit of that basis.
Removing a reference from the repository does not by itself make a working substitute invalid or proven wrong.
Such a finding is a research question.
It is not a verified hardware fact, and it is not a blanket publication prohibition.

Authentication and authorization retain their actual checks, inputs, decisions, and failure paths.
No substitute can manufacture a grant or successful verification.
Kernel execution evidence and authentication qualification remain separate claims.

## Declared substitutes

| Substitute | Scope | Observable behavior | Evidence |
|---|---|---|---|
| Architectural APSS CPU | Explicit `qcom,apss-cpu-v1` model | QEMU's maximum AArch64 CPU executes EL2, EL3, PAuth, and RNG instructions. MPAM exposes one partition per security space, with architectural traps. It does not model Oryon identity or performance. | PBL enters at `0x300000`. QSEE accesses MPAM and PAuth at `0x1468e05c..0x1468e0e4`. SM8850 QHEE reads RNDR and RNDRRS at `0x80000034..0x80000044`. See `docs/qcom/sm88xx/architecture.md`. |
| APSS identification words | Opt-in `x-qcom-ids` libqemu profile | Reads of `AMCFGR_EL0`, `AMCGCR_EL0`, `TRCIDR2`, `TRCIDR4`, and `TRCIDR5` return the fixed words that the firmware compares against. `AMU`, `TRACEVER`, `E0PD`, `DebugVer`, `BRPs`, `WRPs`, `MTPMU`, and `ID_AA64ISAR2_EL1[31:28]` report the required values. `TRFCR_EL2` keeps the guest value. No activity counter exists, no trace unit exists, no trace output appears, and no execution path changes. | QHEE `0x48314..0x48428`, `0x48790`, and `0x487ac` compare those words with fixed values, then write `TRFCR_EL2`. QEMU defines no activity-monitor or trace identification register. See `models/qcom/apss/evidence.md`. |
| APSS interrupt controller | Native GICv4 with LPIs for `qcom,apss-gic-v1` | One native region preserves affinities, grouping, routing, and the final redistributor marker. Explicit `0x40000` placement agrees with QSEE architecture discovery. The grouped platform enables physical and virtual LPIs and one ITS. GICv4.1 virtual SGIs remain absent. | QSEE `0x15752154..0x15752164`, the Canoe device tree, QHEE `gicv3_its_init_one`, and the native eight-frame discovery fixture. See `docs/qcom/sm88xx/architecture.md` and `models/qcom/apss/evidence.md`. |
| GIC distributor 1-of-N advertisement | Distributor read window of `qcom,apss-gic-v1` | Reads of the `GICD_TYPER` dword clear No1N (bit 25). Other fields remain native except the declared ProductID override. Target selection keeps one affinity per SPI, and QEMU ignores `GICD_IROUTER.IRM`. | QHEE `0x45d5c` panics at `gicv3.c:830` when No1N is one. GIC-700 TRM section 4.11.3 reports No1N zero. The phone's EL1 TYPER view does not establish silicon behavior. See `models/qcom/apss/evidence.md`. |
| GICv4 LPI and ITS | Native `arm-gicv3-its` at `0x17040000` | LPIs use the native distributor and redistributor registers with the GIC DMA address space. The ITS command queue and tables are real guest memory. QEMU places `GITS_TRANSLATER` at frame offset `0x10000`, not `0x40`. | Kaanapali `msi-controller@17040000` and QHEE `gicv3_its_init_one` at `0x45fd0`. See `models/qcom/apss/evidence.md`. |
| GICv4.1 ITS capability advertisement | `GITS_TYPER` reads of `qcom,apss-its-v1` | Reads of the `GITS_TYPER` dword set VSGI (bit 39), VMAPP (bit 40), and SVPET (bit 41). Other accesses retain native behavior except the declared CTLR and ProductID overrides. QEMU keeps the GICv4.0 VMAPP layout, no direct virtual SGI injection, and no shared vPE table. | QHEE panics at `gicv3_its.c:648` without VSGI and VMAPP. ITS initialization stops with `No ITS had a shared vPE table` without SVPET. Arm defines these fields as implementation-defined for GICv4.1. See `models/qcom/apss/evidence.md`. |
| GIC-700 model identity | `GICD_IIDR` and `GITS_IIDR` reads | Reads keep the native Implementer `0x43b` and report ProductID four. QEMU identifies itself as a GIC-500 with ProductID zero. | QHEE requires ProductID two or four at image offset `0x46c7c`. The TF-A GICv3 header defines `GIC_PRODUCT_ID_GIC700` as four. See `models/qcom/apss/evidence.md`. |
| GICv4.1 redistributor capability advertisement | `GICR_TYPER` reads in each redistributor window of `qcom,apss-gic-v1` | Reads of the low `GICR_TYPER` dword set Dirty (bit 2), RVPEID (bit 7), and VSGI (bit 26). Every other redistributor access reaches the native frame. QEMU keeps the GICv4.0 `GICR_VPENDBASER` form and no direct virtual SGI injection. | QHEE `FUN_00148100` tests mask `0x04000084` at image offsets `0x4826c..0x48284` and panics with `GICR_TYPER_get_RVPEID(&typer) && GICR_TYPER_get_VSGI(&typer) && GICR_TYPER_get_Dirty(&typer)`. See `models/qcom/apss/evidence.md`. |
| APSS watchdog override configuration | Page offset `0x24` of `qcom,kpss-watchdog-v1` | The model retains the low four bits and rejects reads or other bit writes. It preserves security checks and creates no pet, enable, deadline, bark, bite, or reset effect. | QHEE writes `0xf` once at `0x46df0`. That observation does not establish read rejection, a complete mask, or physical override effects. The [TME review](provenance-tme.md) records the unresolved implementation dependency. |
| GIC fault configuration | Four extension registers in the existing GIC owner | Routing and controls retain guest writes. No fault source or fault interrupt appears from configuration. | Both QSEE initializers and the native partial-write regression in `docs/qcom/sm88xx/architecture.md`. |
| QHEE setup words `0x31205020`, `0x31205028`, `0x17c02c`, `0x17c068` | Existing GEM_NOC retained bank and two `qcom,retained-boot-control-v1` owners | The NOC words read as the declared virtual cold value zero. The APSS words keep bit zero of each 32-bit write and start clear. No fault, clock, rate, source identifier, or completion appears. | QHEE reads `0x31205020` at `0x6b7a14`, and the helper at `0x6a86c0` reads, sets bit zero, and writes back each APSS word. The PBL descriptor table at `0x336d38` lists neither APSS word. See `models/qcom/apss/evidence.md`. |
| MPAM hypervisor control | Opt-in `x-qcom-mpam` profile | `MPAMIDR_EL1` reports `HAS_HCR` one and `MPAMHCR_EL2` keeps the guest word. No resource controller, monitor, or partition exists. | QHEE `msr MPAMHCR_EL2` at image `0x5293c`, and Arm DDI 0601 for the encoding. See `docs/qcom/sm88xx/decisions.md`. |
| ETE programmer words | Opt-in `x-qcom-ids` profile | `TRCPRGCTLR` and `TRBLIMITR_EL1` keep the guest words, `TRCSTATR` reports `IDLE` from program control and `PMSTABLE` one, and the identification words are constant. `TraceBuffer` stays zero, and no trace unit, buffer, or output exists. | QHEE `msr TRCPRGCTLR`, `mrs TRCSTATR`, and `msr TRBLIMITR_EL1`; Arm DDI 0601 encodings. See `models/qcom/apss/evidence.md`. |
| Virtual GIC list registers | Opt-in `x-qcom-gic-num-lrs` property on `qcom,apss-cpu-v1` | The cpu interface defines eight `ICH_LR*_EL2` registers, the set QHEE switches over. No virtual interrupt is injected, so the capacity is inert. | QHEE `msr ICH_LR7_EL2` at image `0x92054` and `mrs ICH_LR7_EL2` at `0x917bc` bound the set, and QEMU otherwise defines four. See `docs/qcom/sm88xx/decisions.md`. |
| Cold APSS release | Explicit virtual board policy | All APSS CPUs start held. The original CPUCP power sequence releases each modeled core. TME and CPUCP retain independent reset paths. | SM8850 run146 observes CPUCP release before the first APSS instruction. Per-core execution remains subject to the actual reset controls. |
| QRD8850 ROMs for SM8845 | Compatibility experiment only | The second platform uses the supplied ROMs with a required compatibility label. | `QCOM_VP_PLAN.md`, project decisions 6 and 7. |
| Omitted `secdata` write | Explicit `omit_secdata = true` manifest profile | The assembler leaves `secdata` unpopulated and records the omitted XML write. Authentication stays `retail`, and the omission remains a stated input limit. The profile does not infer that a retail device permits missing image data. | Both initial package runs rejected an absent `IMAGES/sec.dat`. The profile records the omission for each run. |
| CPUCP CSR `0x7c1` | Pinned libqemu RISC-V implementation | Reads return zero. Writes have no effect. | `target/riscv/csr.c:6815-6819` identifies a vendor core-disable optimization. |
| Virtual UFS physical link | Explicit `virtual_phy = true` platform option | One QMP v4 contract retains guest calibration bytes at observed PBL and XBL addresses. Reset release, power, live source clocks, PLL enable, accepted configuration, and start precede PCS ready. No analog calibration or signal-quality validation occurs. | PBL `0x3079f8..0x307a7c`. XBL tables at configuration file offsets `0x2948`, `0x2bf4`, and `0x2b7c`. See `models/qcom/ufs/README.md`. |
| GENI and GSI initialization | Selected QUP firmware consumers | Guest downloads populate real model RAM. Configuration and ordered start requests precede initialization. The selected TZ `+0x60`, `+0xb008`, and SE `+0x200c` controls are retained. Microcode is retained but not interpreted. SPI/I2C/GPI transfers remain unsupported. | `models/qcom/uart/evidence.md` records both OEM payloads and the original SM8850 completion results. |
| QUP and I2C common clock votes | Selected `QUPV3_HW_PROP` clock arrays | Vote-only branches use the recorded GCC status words. An explicit virtual boot-clock reference supplies each branch. A vote does not create a QUP wrapper, an SE engine, or a firmware response. | SM8850 `0x15715c24` and `0x157517e0`. SM8845 `0x1570ab78` and `0x15744504`. See `models/qcom/sm88xx/cpucp/clock_roots_evidence.md`. |
| Serial engine clock votes | Selected devcfg `/dev/buses/qupac` entries | Each enabled engine declares its CBCR branch and GCC vote mask. The branch keeps a virtual or existing source. A vote does not claim a root rate or a transfer. | SM8850 descriptors `0x157f4bd8..0x157f5318` and hub `0x157f4998`/`0x157f49d8`; SM8845 `0x157e8120..0x157e8720`. See `models/qcom/sm88xx/cpucp/clock_roots_evidence.md`. |
| QUP common wrapper version | SM8850 `qup_common_4` at `0x01ac0000` and SM8845 `qup_common_2` at `0x008c0000` | The existing GENI common model returns its declared virtual revision at offset `0x4`. A write to that word fails. The word does not identify silicon. | Selected `QUP_4C` and `QUP_2C` entries, run397, and the measured `0x40030000` value. See `models/qcom/uart/evidence.md`. |
| i2chub common controls | SM8850 `i2chub_common` at `0x009c0000` | The existing GENI common model retains bit zero at `+0x118`, `+0x120`, and `+0x21c`. The two selected hub engines at `0x980000` and `0x984000` reuse the protocol-neutral GENI SE model. | Selected `I2CHUB_0C` entry, run398 read at `0x9c0120`, and OEM `i2chub_perms` IDs `0xa8`/`0xa9`. See `models/qcom/uart/evidence.md`. |
| QSEE serial engines | Selected engine bases from `tz_qti_config` hardware arrays | Existing `qcom,geni-se-v1` owners retain configuration and guest RAM. No transfer, reset, interrupt, or completion is added. Unmatched bases are created only from selected permission entries. | SM8850 seventeen QUP engines and two hubs; SM8845 fourteen QUP engines. See `models/qcom/uart/evidence.md`. |
| IPA secure configuration | `qcom,ipa-v5.5` declared IPA and GSI words | Full words retain guest writes from virtual zero. Unknown offsets log and fail. No packet engine, DMA, or interrupt source exists. | Both QSEE secure initializers and `models/qcom/ipa/evidence.md`. |
| GSI shared RAM pointer | Read-only `0x03e0425c` | Virtual value `0x3fb` selects one retained canary word at `0x03e07fd4`. It does not claim a silicon reset value. Adjacent undeclared RAM words fail. | Public IPA5.5 HWIO names the user-info pointer. Both QSEE routines place the canary four bytes before its eight-byte offset. See `models/qcom/ipa/evidence.md`. |
| IPA GCC block reset | `0x00189000` | Bit zero retains the reset request from virtual zero. No downstream reset signal clears IPA state. Explicit cold reset clears that state. | QSEE set/delay/clear sequence and the existing GCC BCR pattern in `platforms/qcom/sm88xx/dcp-clocks.lua`. See `models/qcom/ipa/evidence.md`. |
| Shared TLMM digital pins | Selected GPIO geometry and auxiliary SPMI pads | Guest controls retain configuration. GPIO output mode reads its own latch. Disconnected inputs read low. Sleep programming and output transitions create no interrupt. Tile presence describes geometry only. | `models/qcom/uart/evidence.md` records both selected DTs, native HAL offsets, and the public pinctrl reference. |
| Virtual UFS configuration | Same explicit physical-link option | CFG0 bits zero and five and DEBUG_SPARE_CFG bit 31 retain guest writes. CFG1 implements idle-only PHY reset and device-reference gating with a fixed-high warm-reset policy. Independent warm-policy changes fail. HW_VERSION declares virtual revision 3.0.1, not measured silicon identity. | PBL `0x307a30..0x307a38` and `0x307a8c..0x307a9c`. Selected XBL `0x148cae88..0x148cafe8` and matching HAL reset sequence. See `models/qcom/ufs/README.md`. |
| Local UFS capability engine | Explicit virtual PHY profile | Digital power and source clocks permit local DME reads before HCE and PHY setup. Link startup publishes cached peer identity. Reset preserves that identity without preserving live peer availability. Vendor timing attributes retain independent request words. | Selected XBL `0x148c9a88..0x148c9be4`, native run49, and the public U-Boot UFS clock implementation. See `models/qcom/ufs/README.md`. |
| Virtual UFS allocation and buffering | Existing QEMU UFS backend | One allocation unit contains one 4096-byte block. Configuration describes actual logical units and remains read-only. Existing WriteBooster accounting drains under the active or hibernating policy in virtual time. SCSI performs the actual media writes. | The public ufs-utils layout and native SM8845 run51. See `models/qcom/ufs/README.md`. |
| Virtual PMIC power boundary | Explicit `qcom,virtual-pmic-power-v1` consumer | The board starts powered. PS_HOLD release after assertion logs an unsupported power transition and stops simulation. It does not generate a reset or shutdown acknowledgment. | The PMIC configuration precedes the `MPM_PS_HOLD` clear. See `models/qcom/sm88xx/cpucp/power_control_evidence.md`. |
| Virtual boot identity | Explicit hardware version, profile, and variant | APSS and CPUCP read one frozen profile. Zero selects the lowest ROM-supported values. The hardware version is the declared reset value `0x10010000`, not an SM8850 measurement. | `models/qcom/sm88xx/apss_boot/evidence.md` records the field consumers and the register layout. |
| Virtual protection domains | CPU transaction paths | Region permissions restrict accesses and invalidate old direct mappings. Domain assignments describe this virtual platform. | `models/qcom/sm88xx/tme_protection/evidence.md` records the ROM permission table and the actual guest regression. |
| Host entropy source | Sequencer command `0x13`, the APSS RNG, and both cluster RNGs | The host supplies real random bytes. Acquisition failures stop execution. Analog noise measurements are not modeled. | `models/qcom/sm88xx/tme_mailbox/evidence.md` and `models/qcom/rng/evidence.md`. |
| Functional PLL acquisition | Taycan EKO-T PLL registers | Configuration, reset, and votes control the output. Lock requires five virtual microseconds. | `models/qcom/sm88xx/cpucp/gcc_pll_evidence.md`. |
| CPUCP pad state | Virtual pad register | The register retains guest writes. It does not simulate electrical behavior. | CPUCP `0x335988` changes mask `0x1c0`. |
| Functional domain power | CPUCP domain controls | Power, isolation, clocks, and reset conditions control real APSS reset. Internal phase names describe virtual policy. | `models/qcom/sm88xx/cpucp/cpu_domain_power_evidence.md`. |
| Boot branch topology | Branches at `0x174070` and `0x174074` | Both branches use the root at `0x17407c`. Source availability controls branch status. | `models/qcom/sm88xx/cpucp/clock_roots_evidence.md` separates ROM register evidence from virtual wiring. |
| Virtual reference sources | Clock branches, symbol muxes, and the UFS TCSR gate | Declared upstream references remain available. Guest controls and real PHY state determine downstream availability. | `models/qcom/sm88xx/cpucp/clock_roots_evidence.md` and `models/qcom/ufs/README.md`. |
| PLL coarse calibration | Explicit bank mask and five-bit virtual code | STATUS requires a running oscillator and TEST_CTL bit 8. Fine lock and clock outputs retain their acquisition delay. | `models/qcom/sm88xx/cpucp/gcc_pll_evidence.md` records the firmware transfer and the declared substitute. |
| Functional GENI UART | FIFO-mode serial engine | Guest commands and byte counts control all transmitted bytes. The model replaces electrical timing and SE instruction execution. DMA interrupt status starts clear and accepts write-one-to-clear. No DMA event producer exists, and DMA transfer requests still fail. | `models/qcom/uart/evidence.md`. |
| TME channel status | Bank-six readiness and runtime acknowledgement | A bounded TPBL ring establishes cold readiness. Runtime readiness requires authentication, a bounded TMFW ring, and both handoff flags. The bridge never fabricates a response slot. | `models/qcom/sm88xx/tme_mailbox/evidence.md` separates this virtual wiring from recovered register facts. |
| Sequencer trust profile | Explicit issuer key and entitlement digest | OpenSSL verifies both signatures and the program hash. An unknown entitlement fails closed because some chip constraints remain undecoded. | `models/qcom/sm88xx/tme_mailbox/evidence.md` records the independent issuer recovery and the signed ranges. |
| Denied optional authorization | Explicit debug and ROM-patch denial policies | Commands `0x29` and `0x18` complete with no output or grant. `CMD_ERROR_STATUS` reports virtual value `0xffffffff`. This is not a recovered vendor code. Image authentication remains unchanged. | `models/qcom/sm88xx/tme_mailbox/evidence.md` and `docs/qcom/sm88xx/boot-dependencies.md` record the separate result fields and native denial path. |
| Denial-report compatibility | Explicit reconstructed-runtime adapters | The adapters preserve the original OEM report. A validated empty grant context permits one zero-flags result. The retained report control has no authorization effect. | `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md` records both native handler returns and their unchanged denial records. |
| No-grant summary views | Both optional-authorization denial policies | Two read-only summary words return zero only when both denial policies are active. Writes and unsupported accesses fail. | Selected summary imports `0x584/0x588` and `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md`. |
| Virtual fuse identity | Explicit serial, company, OEM, product, and lifecycle fields | The profiles supply declared values. XBL tables establish the part numbers, not every identity bit. | `models/qcom/sm88xx/fuse_controller/evidence.md` separates binary evidence from constructed policy. |
| Virtual fuse self-test | `QFPROM_BIST_CTRL` at `0x1054` and `QFPROM_FC_STATUS` at `0x1058` | A start bit completes at the write and reports sense done. START and the status start at virtual zero. No injected FEC error becomes a BIST error bitmap. No fuse value or authentication input changes. | The status words occupy range-five offsets `0x54` and `0x58`. TME run419 store at `0x4325d8`. See `models/qcom/sm88xx/fuse_controller/evidence.md`. |
| Fuse secure-key status | Explicit `secondary_hw_key_programmed` profile parameter, default false | Bit 28 reports the parameter value. The other status bits stay zero. The hidden secondary hardware key is not modeled. | SM8850 QSEE `0x15695c68` and `qsee_get_secure_state` bit 1. See `models/qcom/sm88xx/fuse_controller/evidence.md`. |
| QSEE fuse-protection views | Profile words at `0x40..0x54`, `0x318`, and `0x630` | The low protection words carry the QSEE-required retail bits. The high words, `OEM_CONFIG6`, and `MRC_0` are declared profile values. The model does not recover their physical fuse bits or root-certificate lists. | SM8850 QSEE `0x15696058` and `0x1577a054`. The retail Oplus fuse profile declares the values. See `models/qcom/sm88xx/fuse_controller/evidence.md`. |
| Spare root-hash bits | The 128 logical bits outside SHA-384 | The model supplies zero for unused bits. It preserves the ROM's alternating 32-bit and 24-bit register layout. | TME `0x201264` and `0x20bfec..0x20c00e` establish digest reconstruction, not physical spare values. |
| Sequencer hash state | Command-one SHA-384 continuation | The model exports eight big-endian chaining words. The ROM preserves those bytes between submissions. | `models/qcom/sm88xx/tme_mailbox/evidence.md` distinguishes this virtual encoding from unconfirmed physical byte order. |
| Virtual managed keys | Explicit `virtual_key_substitute` option and optional `virtual_root_hex` | The default public root or an explicit 48-byte root derives internal keys. HMAC reads guest DMA bytes. Ownership checks reject foreign use and cleanup erases keys. Opaque policy inputs affect derivation, but vendor policy enforcement remains unqualified. | Selected DDR requests and `models/qcom/sm88xx/tme_mailbox/evidence.md` define the functional profile and its limits. |
| Virtual transport key | Explicit `virtual_key_substitute` sequencer profile | A public seed derives a retained 528-bit transport key. TME command `0x1c` broadcasts it to the GPCE hidden store. The GPCE sets `01df1020` bit8 only after delivery. No read, export, unwrap, or authentication path exists. | TME command event `0x301e`, public `tme_hwkm_master_defs.h`, GPCE `15703a28`/`15703a60`, and `models/qcom/sm88xx/tme_mailbox/evidence.md`. |
| Virtual wrapped key export | Explicit `virtual_key_substitute` sequencer profile | Command `0x0b` wraps a managed key with the transport key. The blob uses a declared virtual authenticated format. The GPCE installs it only after the tag and destination checks. The silicon format remains unrecovered. | TME event `0x301a`, donor `0x21ed64`/`0x21ee14`, and both model evidence files. |
| Virtual ECDH key generation | Explicit `virtual_key_substitute` sequencer profile | Command `3` accepts only the OEM QBEC version-four ECC P-384 ECDH policy. Host entropy produces a private scalar in the curve order range. Commands `0x0a` and `0x28` return its public point or X coordinate as big-endian bytes. The private scalar stays internal and cannot be wrapped or used for HMAC. No ECDH operation exists yet. | OEM descriptor `0x443760`, donor `0x21e58e`/`0x21e6ac`/`0x21de92`/`0x21df70`, and `models/qcom/sm88xx/tme_mailbox/evidence.md`. |
| Virtual RPMB region | `rpmb-size-kib` and optional `rpmb-state-file` on the UFS controller | Fresh state has no key, counter zero, and zero data. The model checks authenticated requests and persists key, counter, and media together. Explicit continuation retains that state. Retail provisioning remains separate. | QSEE `0x157485a4..0x157485d8`, UFSDxe `UFS.c:2717`, and `models/qcom/ufs/README.md` "Authenticated RPMB". The authored experiment is `environments/qcom/rpmb/README.md`. |
| Virtual GPCE OEM hardware key | `GOPROC_OEM_KEY` AES-256 CMAC in `qcom,qce-v6` | The default key is the SHA-256 digest of a public label. `oem_hw_key_hex` selects an explicit 32-byte key. QSEE derives its RPMB key with the selected value. CMAC runs in full. Supplied-key AES-128/256 CMAC uses the guest's AUTH_KEY words instead. The QC key and AES ciphers stay rejected. | QSEE `0x1574c3f8`, gate `0x156d2448`, run727 `AUTH_SEG_CFG` `0x65e52`, Linux `regs-v5.h`, and `models/qcom/gpce/evidence.md`. |
| Virtual HWKM identity and key table | Shared group `01df0000..01df000f` in `qcom,qce-v6` | The identity words use the declared reset values: IPCAT `0x02020000` and key policy `4`. The slot count is the declared geometry of 16. The busy bit and the key-manager status follow engine state, not a constant. No BIST engine and no boot command list exist, so their error and completion bits stay zero. | QSEE `0x15703ac8` reads the shared group. `CRYPTO0_CRYPTO_KM_RG_SHARED` supplies the reset values. See `models/qcom/gpce/evidence.md`. |
| Absent SPU image | Unwritten `spunvm` partition and retained SP_SCSR words | The package flashes no `spunvm` content, and the virtual disk keeps that partition empty. UEFI SPSSDxe cannot mount it, PIL loads no SPU image, and the driver sets load-failure bit 0 at `0x01881100`. It then writes a 16-bit status word with the UFS bit 15 to `0x0188b028` and one byte to `0x0188b02c`. These words retain their declared fields only. No SPU executes or reads them, and no SPU completion exists. | `IMAGES/rawprogram4.xml` (`spunvm`, empty filename), run747 and run751 UART, SPSSDxe RVAs `0x2ae4`, `0x2c98`, and `0x2cb4`, and the retained `SP_SCSR` word definitions. |
| TME software state | Two state banks, update aliases, and shared handoff flags | The state starts at explicit virtual zero. Guest writes preserve prior bits and independent banks. Clearing remains unsupported. | TME `0x205208..0x205262` updates the banks. TME `0x2005e8` and APSS `0x30de68` update the handoff word. |
| TME image vector | Mutable `TME_TEST_IMAGE_VECTOR_1` status | Guest writes replace the retained word. The explicit virtual reset value is zero. The word does not change authentication policy. | TME `0x2013ca`. |
| Sequencer payload availability | Optional complete-ROM probe socket | Initialized virtual payload arrays report zero-ready. This value does not acknowledge commands or authenticate images. | Complete ROM reads `0x0228000c` at `0x20f744` and `0x20f760`. The physical register name remains unknown. |
| Reconstructed TME runtime | Explicit common donor profile for both boards | The original boot ROM calls a relocated candidate runtime. Separate descriptors and a live slot map preserve the OEM interface. Unknown imports fail. | `docs/qcom/sm88xx/oplus/bringup.md` records checked inputs and actual canonical execution. |
| Cold AOP configuration | Shared APSS `0x0c3f0000..0x0c3f03ff` and TME `0x103f0000..0x103f03ff` | The shared bytes start zero. The resource service publishes only after matching image admission. Warm resume remains unsupported. | Original PDP `0x81301454`, selected AOP export code, and `models/qcom/rpmh/evidence.md`. |
| Functional CLIC priorities | Explicit `qcom,tme-clic-v1` controller | Larger priorities win. Larger IDs resolve ties. The runtime profile starts priorities at one, above the zero threshold. Firmware retains masks and priority controls. | `docs/qcom/sm88xx/decisions.md` records the layout and interrupt-64 exercise. `models/qcom/sm88xx/tme_mailbox/evidence.md` records the real pending completion that zero priority blocked. |
| TME boot-clock parents | Private reconstruction platform | Declared virtual references supply the observed branches. Guest controls still gate each output. | OEM descriptors `0x44685c..0x44688b` and routines `0x4231a0..0x4232ea` establish the branch registers, not their physical parents. |
| TME external fault configuration | Private reconstruction platform | Observed configuration words retain writes. The virtual monitor stops on denied accesses instead of delivering physical access-violation interrupts. | Candidate `0x223a3a..0x223a9c` configures external faults. TCSR words at `0x6598` and `0x9756` hold the ownership and violation controls. |
| Virtual external-debug policy | Explicit sequencer `debug_access_value` | Command `0x25` writes DBGAR values through DMA and checks their readback. The private profile disables external debug access. | OEM `0x423f98` submits the list. Candidate `0x2203b6` constructs the command. |
| Additional XPU geometry | Reconstructed runtime profiles | Controllers retain permissions and locks. Two XBL controllers declare 1024 regions, 40-bit addresses, and eleven domains. They have no connected data clients. | `models/qcom/sm88xx/tme_protection/evidence.md` separates observed register contracts from virtual topology. |
| Runtime requester transition | Explicit ABI activation signal | The three TME CPU ports change from domain one to domain two and invalidate direct mappings. DMA identities do not change. | `docs/qcom/sm88xx/decisions.md` records the two incompatible static-domain observations and the virtual transition. |
| TME core configuration | Private reconstruction platform | Fourteen words retain guest configuration. The bank does not simulate undocumented hardware effects. Architectural PMP remains active. | Candidate `0x2247ae` and `0x2247c8` establish bounded write and read operations at `0x07ad2000`. |
| Functional TME watchdog | Explicit `qcom,tme-watchdog-v1` model | Virtual time drives the counter and bark/bite outputs. An unconnected bite logs the event and stops simulation instead of fabricating a reset. | OEM `0x429560..0x4295d4` and `models/qcom/sm88xx/tme_mailbox/evidence.md`. |
| Functional APSS watchdog | Two explicit KPSS banks | The shared engine supplies packed status and guest-controlled deadlines at 32768 Hz. Guest mode controls nonsecure access. Established IRQ routes carry real deadline events. An unconnected bite stops simulation. | Selected XBL and QSEE functions, the public Linux driver, and `models/qcom/sm88xx/tme_mailbox/evidence.md`. The evidence states the virtual access policy. |
| Secure APSS watchdog register order | Bank at `0x17908000`, `secure_bank` parameter | The bank maps bark to page offset `0x14` and bite to `0x10`, the reverse of the OS bank. An unconnected bite still stops simulation. | QSEE writes `192000` to page offset `0x14` with the secure attribute. The Qualcomm debug guide states a six-second secure bark. See `models/qcom/sm88xx/tme_mailbox/evidence.md`. |
| TME low-power control | Explicit `cold-no-external-inhibit` policy | The disconnected inhibit input reads zero. Firmware owns the exit vector and eight diagnostic words. No collapse, resume, CPU redirect, interrupt injection, or debug grant occurs. Unsupported accesses fail. | `models/qcom/sm88xx/boot_control/evidence.md`. |
| Disabled TME crash dumps | Explicit `disabled` policy at APSS `0x20c02000` | Zero denies mini, non-secure, and unencrypted full dumps. The policy grants no debug access and changes no authentication result. Writes remain unsupported. | `models/qcom/sm88xx/boot_control/tme_crashdump_evidence.md`. |
| XBL AC policy controls | Three selected control words | Each existing protection owner retains its word from explicit cold zero. Writes do not change region permissions, locks, or debug grants. APSS reads the real ROM-written DCR mode through an alias. | `models/qcom/sm88xx/boot_control/evidence.md` records both OEM tables and native execution. |
| VMID mapper configuration | Four SM8850 instances and three SM8845 instances | Explicit virtual capabilities permit the real guest to initialize independent control, stream, and auxiliary state. No downstream DMA master is connected. The model grants no existing protection domain. | `models/qcom/vmidmt/evidence.md` and `models/qcom/vmidmt/platform-evidence.md` identify the pinned public driver and both firmware inventories. |
| XBL namespace policy | Shared TME and APSS configuration sockets | Capabilities restrict ownership changes. Configuration reads exclude foreign retained permissions, except a handoff seed that the reader programmed. Data access uses their union. A TME release of an owned region frees the region for every owner and revokes their policy. Final release retires only an unlocked region with no owner or effective permission. APSS writes cannot change foreign policy. Finite apertures interpret an encoded zero end as their exclusive limit. | `models/qcom/sm88xx/tme_protection/evidence.md` records these functional substitutes and native execution. `namespace-platform-evidence.md` records both signed tables and the endpoint rule. |
| XPU error-reporting controls | Shared words at offsets `0x208` and `0x20c` | Guest writes retain the implemented mask. These controls do not grant access. The host monitor logs and stops denied accesses instead of producing physical XPU fault interrupts. | The selected SM8850 consumer masks `0xc00007ff` at `0x14859f84`. Physical field names and complete reporting semantics remain unresolved in the [TME review](provenance-tme.md). |
| OEM authorization fuse views | Reconstructed OEM profile | Four read-only views derive from the same policy word. Mask `0x832` requires OEM authorization for debug, fuse overrides, and ROM updates. It grants no debug access. | `models/qcom/sm88xx/fuse_controller/evidence.md` records both consumers and the provisioning policy. |
| TME startup controls | Private reconstruction platform | Observed words retain writes without additional hardware effects. External fault injection remains absent. | OEM `0x436350`, `0x4363d2`, `0x41c2bc`, and `0x41c2c2`. |
| Runtime control sequence | Explicit `control_state_substitute` option | Selector four retains mode one. The boot transition permits mode two. Unsupported requests still fail. Each accepted substitute command logs its use. | Candidate `0x220680` and `0x2202da` establish commands `0x1d` and `0x21`. Their complete physical effects remain unproved. See `models/qcom/sm88xx/tme_mailbox/evidence.md`. |
| Corrected fuse status | Existing policy and aggregate FEC state | The corrected row preserves the policy word. Zero FEC state produces zero error bitmaps. Nonzero FEC state produces all-one bitmaps. This mapping is virtual policy, not a vendor encoding. | Both OEM descriptors select mask `0x20`. See `models/qcom/sm88xx/fuse_controller/evidence.md`. |
| Runtime wake configuration | Private reconstruction profile | Two PDC configuration banks retain writes without external wake inputs. | OEM `0x4299b2` and the upstream PDC version 3.2 register layout. See `models/qcom/sm88xx/tme_mailbox/evidence.md`. |
| Runtime IPCC initialization | Private reconstruction profile | A virtual revision word selects version 3.0. The observed common configuration word retains writes. Other registers remain unsupported. | OEM `0x4409ee`, `0x440a1a`, and the instance at `0x447b0c`. See `models/qcom/sm88xx/tme_mailbox/evidence.md`. |
| Runtime resource geometry | Private reconstruction profile | Variable groups contain 21, 5, 5, and 5 entries from slot `0x1c`. Fixed groups remain separate. Firmware retains its range checks and publishes the sizes. | Both OEM identifier tables and candidate `0x21e216`. See `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md`. |
| Virtual TME scheduler clock | Private reconstruction profile | An architectural machine timer runs at 600 MHz and delivers interrupt seven. The observed 600,000-tick interval becomes one virtual millisecond. | Candidate `0x2281ee` and `0x2282b0` establish the timer registers and interrupt mask, not the physical frequency. |
| Early APSS counter view | Shared physical frame-zero state | The board initializes CNTEL0ACR to one for the early counter and frequency consumer. Firmware later programs its own access policy. This value is explicit virtual reset policy. | Both original XBL timer initializers and the Canoe device tree. See `models/qcom/sm88xx/apss_boot/evidence.md`. |
| APSS SIMD reset policy | Common architectural CPU model | Reset selects `CPACR_EL1.FPEN=1`. EL1 can execute SIMD. EL0 still traps. Guest writes control later access. This is virtual policy, not measured silicon state. | Arm defines the FPEN reset value as UNKNOWN. Both QSEE images use EL1 SIMD before a guest FPEN write. See `docs/qcom/sm88xx/decisions.md`. |
| Virtual debugger power | Private reconstruction profile | An asserted external power request sets `CDBGPWRUPREQ` status. XBL does not start its APSS watchdog. The request grants no debug access and changes no authentication result. | XBL `0x14828160`. See `models/qcom/sm88xx/apss_boot/evidence.md`. |
| Virtual feature fuses | Private reconstruction profile | Corrected CPU and peripheral fields report no fused defects. The raw SKU fields use explicit zero. | XBL descriptors `0x148f3600..0x148f3750`, `0x148ddb10`, and `0x148ddb28`. See `models/qcom/sm88xx/fuse_controller/evidence.md`. |
| NCC clock policy | Explicit AUX and Zonda PLL paths | Software control retains the five-microsecond acquisition policy. Hardware requests apply their effective rates synchronously. Missing or disabled selected outputs hold reset. | XBL `0x1484ed64..0x1484edc0` and `models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md`. |
| AOSS control retention | Private reconstruction profile | Mode selectors, divider words, and the reference selector retain writes. The PLL reference remains the explicit 19.2 MHz virtual source. The profile does not simulate automatic mode control or cycle-accurate divider timing. | XBL `0x1484ecd4..0x1484ece8` and `0x1484edc8..0x1484ee70`. |
| AOSS Pongo calibration | Explicit virtual oscillator policy | Calibration completes after one virtual microsecond. The profile assigns the calibration output a reference ratio of eight. Main and even outputs use their programmed dividers. | XBL `0x148615d0..0x148615f4`, descriptor `0x148f1850`, and `models/qcom/sm88xx/cpucp/gcc_pll_evidence.md`. |
| REFGEN analog calibration | Explicit power and calibration policies | A tuning write starts five virtual microseconds of acquisition. Readiness follows elapsed time. Hardware-vote and clamp controls retain state without electrical effects. | XBL `0x1484ee78..0x1484eef0` and `models/qcom/sm88xx/cpucp/gcc_pll_evidence.md`. |
| Boot branch references | Explicit private clock topology | Unresolved branches use a declared virtual reference. Known source connections and UFS dependencies remain live. Independent votes cannot override source loss. | XBL tables `0x148f2d44`, `0x148f2f28`, and `models/qcom/sm88xx/cpucp/clock_roots_evidence.md`. |
| Clock policy retention | Explicit register masks | Scalar and DCD controls retain declared fields. They do not simulate physical memory collapse, clamp timing, or dynamic division. | XBL `0x1484ef88..0x1484f3a0` and table `0x148f11f4`. |
| Interconnect policy retention | Bounded OEM register program | The guest writes all 118 control values. The virtual interconnect retains declared fields without physical timing or arbitration effects. This program contains no status waits. | XBL table `0x14920bc8`, count at `0x149216d8`, and interpreter `0x1484de38`. |
| SPMI synchronous transport | Explicit `synchronous-virtual` completion policy | Guest commands use retained routing, ownership, and FIFO bytes. Absent SIDs return FAILURE. Unsupported owned operations fail. No wire timing or external IRQ input is simulated. | Selected XBL `0x1486e8c4`, `0x14870264..0x148709e4`, and descriptors `0x148f3ad8..0x148f3bc7`. See the contract below. |
| SPMI secure observer window | Explicit `secure_observer_ee=1` | Secure APSS requests may use the EE1 observer window. That window keeps separate command, status, and read-data state. Nonsecure and unspecified requests fail. | QSEE `0x15738dc0` (SM8850) and `0x1572ba14` (SM8845). See `models/qcom/spmi/README.md`. |
| Virtual PMIC register state | Explicit identity and byte inventories | Identity is read-only. Masked controls retain guest writes. Functional services own their status. Empty routing does not establish identity or readiness. | Selected XBL identity scan `0x148343c4..0x14834620` and `models/qcom/spmi/README.md`. Earlier rehost UEFI policies are not XBL evidence. |
| PMIC identity burst tail | Read-only bytes `0x0107..0x014f` on every populated SID | The identity block ends at `0x0106`. The adjacent bytes read virtual zero and reject writes. | QSEE command writes from `0x15738ea0` read eight-byte bursts at `0x100`, `0x108`, `0x110`, and `0x150` (runtime486..494). The live-phone regmap record `20261026T150037Z` reads zero at `0x107..0x14f`. See `models/qcom/spmi/README.md`. |
| PMIC identity burst tail on every populated SID | Read-only bytes `0x0152..0x01ef`, and `0x0161` on bus zero, SID zero | On bus zero, SID zero the bytes carry the live-phone values, and no source gives a per-SID value, so other SIDs read virtual zero (UEFI run522 reads bus zero, SID one). The block reads zero except byte `0x0161`, which reads `0x61`. Writes fail, and bytes `0x0154`, `0x0160`, and `0x01f0..0x01ff` keep their existing owners. | QSEE run494 reads the eight-byte bursts at `0x0150`, and run496..run498 read `0x0158`, `0x0168`, and `0x0170`. The live-phone regmap record `20261026T150037Z` supplies the values. See `models/qcom/spmi/README.md`. |
| PMIC PON revision and status bytes | Read-only zero at `pon_peripherals` base plus seven, eight to fifteen, `0x17`, and `0x1c..0x1f` | The revision block declares seven bytes, and QSEE reads eight. Both interrupt groups have no event producer in this policy. Those bytes read virtual zero and reject writes. The phone record `20261026T161459Z` reports warm latched state there, and the model does not copy it. | QSEE run500..run504 read the bursts at bus zero, SID zero, `0x0800`, `0x0808`, `0x0810`, and `0x0818`. The XBL consumer reads seven revision bytes only. See the PON contract below. |
| PMIC PON HLOS group | Explicit `pon_hlos_peripherals` row at bus zero, SID zero, `0x13` | The group reports type one, subtype `0x09`, and virtual digital and analog revision 1.0. Bytes `0x08..0x0f` read zero. The interrupt block has no event producer, because no virtual key exists. | Linux `pm8941-pwrkey.c` subtype `PON_SUBTYPE_GEN3_HLOS` and the pmk8550 device tree `pon@1300`. See `models/qcom/spmi/README.md`. |
| SM8845 PMIC inventory | Explicit CPH2771 virtual profile | The profile selects PMK8850 from the base tree. PMD8028 substitutes for the unnamed PMD802x suffix. Primary revision 1.0, variant zero, and one-SID spans are virtual policy. PON, SDAM, PBS memory, and declared request bytes have separate owners. Other discovery revisions report zero. | Package topology, XBL `0x14882da0`, and `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md`. |
| Virtual SHRM transport | Explicit `qcom,virtual-shrm-v1` model and DDR backend | Processor reset clears backend and command state without clearing guest memory. Release establishes readiness after startup effects. Clock gating remains separate. Completion requires successful backend effects and the exact XBL response. | Selected TME writes at `0x4373a2` and `0x43761e`. See `models/qcom/sm88xx/ddr/evidence.md` and the contract below. |
| Disconnected EUD transport | Explicit no-host policy | Serial, tuning, and enable votes retain guest writes. Enumeration and software-attach status remain clear. Powered controller status reports no inherited session. | Selected XBL `0x1488ec64` and `models/qcom/eud/evidence.md`. |
| DDR frontend programming | Selected DCB words and bounded MC/LLC regions | Four banks retain independent guest values. Broadcast writes update all banks. Undeclared configuration accesses emit substitute logs and retain values from cold zero. Known commands preserve their effects before completion. The model does not simulate physical timing. | `models/qcom/sm88xx/ddr/controls-evidence.md` and `models/qcom/sm88xx/ddr/phy-evidence.md`. |
| DDR reference acceptance | Existing PLL reference input | Accepted, ordered reference requests complete with a nonzero source input. This ideal analog policy does not require an unproven PLL output or change its gates and lock behavior. | Selected source helpers and handshakes at `0x310a9134/9138`. See `models/qcom/sm88xx/ddr/phy-evidence.md`. |
| DDR analog calibration | Explicit `mc_calibration` option | Enabled requests complete after a publication edge and available programming sources. Each five-bit result receives virtual midpoint code 16. New requests invalidate old results. Banks retain independent state. | SM8845 `0x148ae6cc..0x148ae9e0`, SM8850 `0x148aff60`, and `models/qcom/sm88xx/ddr/phy-evidence.md`. |
| DDR selected measurement | Explicit `mc_calibration` option | Enabled requests publish selected five-bit results and completion together. Each result uses virtual midpoint code 16. Cleanup retains results. New requests invalidate old completion. | SM8845 producer `0x148b99b8`, consumer `0x148b9aa0`, and `models/qcom/sm88xx/ddr/phy-evidence.md`. |
| DDR direct commands | Four controller pairs and one broadcast view | Selected descriptors apply retained MR writes and publish MR reads before completion. Known electrical commands use ideal timing. Native firmware reconstructs geometry from the declared device identity. | Both selected command writers, rank consumers, and `models/qcom/sm88xx/ddr/evidence.md`. |
| DDR oscillator measurement | Guest MR37 duration and an ideal 400 ps physical period | Operation `0x94` derives MR35/MR36 counts from the active clock and retained duration. The firmware retains its acceptance limits. | Both selected duration tables and `models/qcom/sm88xx/ddr/evidence.md`. |
| DDR PXI traffic | Guest packet lists, patterns, and seeds | Transfers use the real shared DRAM backing. Comparisons report actual mismatches. RRBC translation, 64-byte channel stripes, and the PRBS recurrence are explicit virtual policies. Completion follows successful transfers. | Selected packet producers, public SDM670 field definitions, and `models/qcom/sm88xx/ddr/phy-evidence.md`. |
| SHRM training callbacks | Guest runtime rows and native result streams | Calibration uses zero electrical correction and a declared HCD selector policy. Digital training writes and compares real shared memory. The adapter retains profile settings and copies actual MR caches. | Selected SHRM callback table, wrapper `0x203b68`, and `models/qcom/sm88xx/ddr/evidence.md`. |
| SHRM clock transitions | Guest clock plans and retained training profiles | Clock changes complete synchronously. The adapter publishes current levels, periods, and resource state before acknowledgment. It does not simulate physical switching delays. | SHRM `0x2012e8`, `0x202f86`, and `0x202ff6`. |
| Virtual CPR calibration | Declared zero policy, or the private SM8850 page | The existing fuse owner supplies 58 listed raw words. SM8850 returns the private calibration page when its manifest supplies `calibration_fuses`. Other runs return zero. Declared zero fails the VPP voltage-setting check at `0x8132937e`, so SM8850 needs the page. A separate corrected word supplies zero plan IDs. The guest retains its voltage tables, margins, and calculations. Security fuses remain unchanged. | Both selected XBL descriptor groups, PDP consumers `0x81329dd8..0x8132a6ac`, SM8850 run572, and `models/qcom/sm88xx/fuse_controller/evidence.md`. |
| Virtual CPR controller | Target-derived ideal oscillators | Guest targets, gates, masks, and thread state determine measurement results. Ideal sensors produce zero voltage error. The model does not drive voltage or external interrupts. Unsupported aging remains explicit. | Selected controller tables and `models/qcom/sm88xx/fuse_controller/evidence.md`. |
| RPMH CPRF block | One `qcom,rpmh-cprf-v1` owner at `0x0c200000` with size `0x10000` | The block keeps the voltage limits, safe voltage, control, VRM, and enable word of each rail. The profile declares the reset values. A write outside the declared field fails. No voltage change, interrupt, or completion appears, because the CPR sensors are ideal and no PMIC regulator exists. Unlisted offsets keep their address error. | CPRDxe `0xc1b0`, `0xc1b4`, `0xc22c`, `0xc474`, and `0xc49c`. See `models/qcom/cpr/evidence.md`. |
| Railway ACC overrides | Four AOSS control words | Exact native masks retain guest configuration from virtual cold zero. The model supplies no comparator measurement or voltage transition. | Both selected prechange callbacks and `models/qcom/sm88xx/ddr/controls-evidence.md`. |
| Virtual temperature and thermal watchdog | Seven TSENS banks and explicit calibration profile | Calibrated conversions report 25 degrees Celsius after the configured scan time. The environment remains awake. An armed watchdog requires scan cadence below its declared virtual deadline. Unsupported expiry configurations fail. Guest thresholds, masks, and thermal decisions remain effective. | `models/qcom/tsens/README.md` records the normal-progress substitute, virtual clock rate, and both native thermal callback completions. |
| CPUCP limits manager block | Two `qcom,cpuss-llm-v1` owners at `0x181c0000` and `0x183c0000` | The blocks retain guest configuration and interrupt enable words. Sample words report 25 degrees with validity bit 16. The two limit status words start at zero, so the driver reads no measurement. No current or leakage measurement drives throttling. Unlisted offsets keep their address error. | PDP `0x813263d4` writes the configuration. Limit paths `0x8130279c..0x81302b6c` read and update it. See `models/qcom/sm88xx/cpucp/subsystem_evidence.md`. |
| CPUCP post-ROM controls | Nineteen exact configuration words | The words retain guest writes from virtual zero. They do not change clock, reset, interrupt, or completion state. Hardware side effects remain unresolved. Existing domain registers retain their separate owners. | SM8850 CPUCP `0x1d808e16..0x1d808ef6` and `0x1d808fdc`. See `models/qcom/sm88xx/cpucp/subsystem_evidence.md`. |
| Disconnected IBUS | Explicit SM8850 `disconnected` policy | The existing arbiter retains guest-owned configuration and table words. No external PMIC alert producer connects. The separate AOSS bit retains its request from virtual cold zero. No capture, interrupt, mitigation, or status completion is fabricated. | `models/qcom/spmi/README.md` records the original initializer, public producer contract, and unsupported runtime boundary. |
| Virtual display phase request | SM8845 SID 4 register `0xf83d` | Existing sparse PMIC storage retains the guest request byte. Virtual cold zero means no request. The byte does not report an analog result or readiness. | The native caller at `0x1483990c` writes the selected `ibb-phase-select` property. See `models/qcom/spmi/README.md`. |
| Inactive PMIC electrical controls | Exact PSI address and mask inventories | Declared bytes retain actual guest requests from virtual cold zero. The guest executes the original program and verifies its writes. Unknown input values and completion flags are not supplied. | Both selected PSI2 programs and `models/qcom/spmi/README.md`. |
| Disconnected PMIC interrupts | Explicit peripheral list | Set and clear aliases update one enable mask. No input producer can set realtime, latched, or pending state. | Selected PSI writes and the native `pmic-psi` fixture. |
| One-shot PMIC control key | Explicit SID7 target `0x2442` | Key `0xa5` permits one target write. A different key or reset removes permission. The target write consumes permission. | Selected unlock records and `models/qcom/spmi/README.md`. The one-shot lifetime is virtual policy. |
| PMIC peripheral locks | Guest-programmed D1, D2, and D3 states | Terminal open and closed states retain their values. Closed states block declared protected controls and PBS memory writes. Ordinary configuration remains independent. | Both selected lock tables, native fixture cases, and `models/qcom/spmi/README.md`. The register-group scope is virtual policy. |
| Virtual PBS memory | Explicit revision 5.1 and per-instance RAM geometry | Mode, enable, address, and capacity govern transfers. Cold reset clears RAM. Warm reset retains guest bytes and disables access. No PBS code execution, ROM contents, or expected image version is supplied. | Selected downloader and geometry decoder, `models/qcom/spmi/pmic_pbs.h`, and the native fixtures. |
| PMIC exploratory configuration | Explicit OEM option, disabled by default | Unknown ordinary configuration bytes retain writes from cold zero. Each accepted transaction logs its substitute status. Identity, status, keys, PBS, and existing service failures remain protected. | `models/qcom/spmi/README.md` defines the exact range and ownership rules. |
| PMIC group writes | Broadcast SIDs 14 and 15 | Identity spans select primary or secondary recipients. The model validates all recipients before changing any byte. Group reads and PBS transfers fail. | Selected SM8850 PSI operations. See `models/qcom/spmi/README.md`. |
| PON warm-reset policy | FOLLOW_WARM_RB bit two, initially set | A warm reset clears the guest initialization spare under this policy. Normal writes retain the spare. The guest alone publishes initialization bits. | Selected native status consumers and `models/qcom/spmi/README.md`. |
| PMIC post-PSI services | Declared GPIO, regulator, clock, and RTC instances | GPIO outputs follow controls, and enabled inputs follow pulls. GPIO banks start enabled under explicit virtual cold policy. Regulator readiness follows enable. Clock controls retain requests. Cold RTC status reports no loss of power, without a counter. | Selected DT operation tables and `models/qcom/spmi/README.md`. |
| PMIC regulator placement | Selected masks, native counts, and strides | SM8850 exposes 94 rails. SM8845 exposes 84 rails. Functional peripheral types and cold revisions are virtual policy, not captured silicon responses. The BOB bank at bus 0, SID 1 keeps controls `0xe4` and `0xe6`, and the MISC blocks stay at `0x40..0x43`. | Both selected provider initializers, the native status capture in `models/qcom/spmi/README.md`, and the phone type map record `20261026T180725Z`. |
| Identified MISC peripherals | Explicit `misc_peripherals` rows and read-only bytes | The service reports the declared type and subtype with digital and analog revision zero. Bytes `0x08..0x1f` read zero and reject writes. It supplies no peripheral function. | Type `0x14` is MISC and BOB subtype `8` is monitoring. The live-phone record `20261026T180725Z` reads type `0x14`, subtype `0x6b` at bus 0, SID 1 peripherals `0x40..0x43`, and type `0x29`, subtype `8` at `0xe5` and `0xe7`. |
| Phone zero spans at MISC blocks | Read-only zero at bus 0, SID 1, for every recorded byte outside another owner | The bytes read zero and reject writes. The spans are substitutes grounded in the same record. Wide bursts read the lock bytes `0xd1..0xd3` as zero, which is their cold open state. Offsets `0x40..0xcf` stay with the exploratory service, which retains the guest request bytes. | The live-phone record `20261026T180725Z` reads zero at `0x20..0x3f`, `0xd0..0xd9`, `0xdb..0xef`, and `0xf0..0xff` of each block. QSEE run533 reads the blocks in eight-byte bursts. |
| Warm flag at the MISC blocks | Writable bytes `0xda` from cold zero on peripherals `0x40..0x43` | The guest owns the warm flag, and a cold boot has no warm flag. No model behavior depends on the byte. | OEM address tables at `0x148f63a8` and `0x148fd680` list the warm byte on peripherals `0x41` and `0x43`. The live-phone record reads `0x05` and `0x01` at `0x40da` and `0x41da` after a warm boot, so the value is state, not a reset value. |
| QSEE unmarked scan reads | Eight-byte read bursts from offset `0x08` to the block end of a declared peripheral, including a PBS group | A declared byte or PBS interface register keeps its value. Any other byte reads cold zero and prints one `PMIC_SCAN_READ_UNDECLARED` line each. The burst moves no PBS transfer pointer and changes no RAM, so an upload readback stays real. Writes, other widths, and reads of an undeclared peripheral keep the existing rejection. | QSEE (tz.mbn) run530, run533, and run539 traces, the reader shape, and `models/qcom/spmi/README.md`. Run539 reads the PBS group at bus one, SID seven, `0x1608..0x16f8`. No disassembly yet proves a branch on these bytes, so this is a substitute. |
| Passive PBS clients | Three virtual client inputs per declared PBS interface | Discovery reports the declared input count. No client executes code or produces a completion result. | `models/qcom/spmi/README.md`. |
| APSS scratch words | Sixteen shared 32-bit words | Guest writes retain independently. The canary has an explicit reset value. No debug header appears without a guest write. | Both selected scratch getters and `models/qcom/sm88xx/boot_control/evidence.md`. |
| DCP bootstrap hardware | Six narrow banks and shared clock/preparation controls | Power, root configuration, branch enables, entry, and reset determine hardware release. Ordinary configuration retains writes. No DCP instruction or service reply is produced. | Selected DCP constructors, native hardware smoke, and `models/qcom/sm88xx/dcp/evidence.md`. |
| Disconnected DSI interrupts | Controller 0 and 1, offset `0x110` only | Enable bits retain guest requests. W1C writes never create events. No panel or display engine supplies a pending source. Other controller offsets fail. | Both selected DCP catalogs, the native `dsi-interrupts` fixture, and `models/qcom/sm88xx/dcp/evidence.md`. This does not implement active display transfers. |
| Virtual RPMh resources | Selected ARC and BCM resources shared by three requesters | ARC uses the maximum present vote. BCM uses commit groups and independent X/Y maxima. Completion follows accepted effects. | `models/qcom/rpmh/evidence.md` separates the virtual policy from physical arbitration. |
| RSC instruction storage | Declared APSS sequencer capacity | Uploads retain instructions and branch addresses. Uploads do not execute power sequences or create timestamp events. | Selected QSEE uploader and `models/qcom/rpmh/evidence.md`. |
| APSS PDC configuration | Ownership, BCM controls, and timestamp configuration | Runtime profiles declare no capture source. Configuration creates no event, interrupt, or permission. | Both QSEE tables and the native negative-event fixture in `models/qcom/sm88xx/boot_control/evidence.md`. |
| QSEE clock and power controls | Selected direct register group | Clock enable and accepted power transitions cause status changes. Physical GPU supplies remain idealized. | Both OEM functions and the native fixture in `models/qcom/sm88xx/cpucp/clock_roots_evidence.md`. |
| Virtual AOP startup | Image `0x21` handoff and hash-checked guest tables | The service replaces AOP resource initialization. It preserves both slave-three directories and publishes the resource cookie last. It does not replace image authentication. | `models/qcom/rpmh/evidence.md` records both computed database lengths and actual-model publication checks. |
| Virtual PDP services | SM8845 image `0x91` handoff and two SCMI slots | The service implements Base and Performance protocols through one shared RX owner. SM8850 instead executes the real PDP image. | `models/qcom/sm88xx/pdp_scmi/evidence.md` records the virtual policy. `models/qcom/sm88xx/pdp/evidence.md` records the execution contract. |
| Virtual PDP CPU boot status | SM8845 APSS CPU domain with `boot_status_enable` | The domain writes the header and online-core bitmap at `0x81f5a000 + lane*0x40000`. SM8850 disables this publication. Only its PDP guest code owns these records. | PDP `0x81321b80` writes the header. TZ `0x1573e630..0x1573e69c` reads it. See `models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md`. |
| PDP architectural CPU | SM8850 M-profile execution | QEMU Cortex-M7 executes the guest vectors and code. This is not a measured silicon CPU identity. | The reset, VFP, cache, and SysTick consumers appear in `models/qcom/sm88xx/pdp/evidence.md`. |
| PDP functional reset release | SM8850 NCC sequencer | The post-copy flag, initialized cluster, available clock, and released domain reset permit architectural release. No firmware return or completion record comes from the host. | CPUCP `0x1d80b73e` sets the flag. The exact silicon reset circuit remains an inference. |
| NCC timer clock | PDP timestamp counter | The counter advances with simulation time at an explicit virtual frequency of 19.2 MHz. Its frequency register reports the same value. | PDP `0x81327540` consumes the frequency. `0x2000686c` consumes the counter. The physical rate remains unmeasured. |
| PDP BSA watchdog clock | `pdp_wdog@409c0000` refresh and control frames | The WOR, WCS, and WCV registers follow the Arm BSA generic watchdog. The counter uses an explicit virtual 19.2 MHz rate. WS0 interrupts the PDP. A second expiry stops the simulation. Control offset `0x100` retains its value without an effect. | PDP driver `wdt_bsa`, IRQ 6 handler `0x8132acd4`, and enable path `0x8132ac54`. See `models/qcom/sm88xx/pdp/evidence.md`. |
| PDP speed-bin fuse row | Corrected words `0x221c20d0..0x221c20d7` | The words read zero, so the PDP selects speed bin zero. This is virtual fuse policy, not a phone measurement. | PDP `0x20005744` builds the key `ncc%d.v%d.mcn%d.sb%d`. The phone exposes only the raw QFPROM page through nvmem. |
| PDP corrected calibration words | Read-only zero words at `0x221c20d8` and `0x221c24c8` | The profile declares both words zero. The DPM IDDQ path then uses its default-leakage keys, and the VPP bit stays clear. The raw voltage words come from the private page. This is virtual fuse policy, not a phone measurement. | PDP `0x81324cb8`, `0x81324cee`, `0x81324d04`, and `0x81328f6a`. See `models/qcom/sm88xx/fuse_controller/evidence.md`. |
| PDP raw calibration words | Private SM8850 page selected by the manifest | The listed raw words return the page values. Unlisted offsets keep their address error. Declared zero asserts `VPP: Voltage Setting Fuses not valid` at `0x8132937e`. The page stays private. Record `20261026T230217Z` and its hash identify it. | PDP `vpp_process_pegasus.c`, SM8850 run572, and `models/qcom/sm88xx/fuse_controller/evidence.md`. |
| PDP IDDQ configuration | Seven APSS words at `0x17913004..0x17915000` and two CPRH doublewords at `0x180c05e0 + (lane << 21)` | The words retain guest writes from zero without a leakage or power effect. The shared CPRH owner preserves later reads and partial writes. | PDP `0x81324d64` and `0x81323172..0x813231da`. See `models/qcom/sm88xx/pdp/evidence.md`. |
| CPRH program and configuration | Shared lane owners at `0x180c0000` and `0x182c0000` | Guest uploads and configuration retain their words. Power requests apply selected profiles through two functional target owners. The model does not interpret internal programs. Unknown status reads fail. | PDP `0x81324298`, `0x81323060`, `0x81323130`, and the CPUCP type-10 and type-11 uploads. See `models/qcom/sm88xx/pdp/evidence.md`. |
| CPRH target configuration | Four shared targets at `0x181c8000`, `0x181d0000`, `0x183c8000`, and `0x183d0000` | A power request applies the larger initial or floor step count. The configured base and multiplier determine voltage. Chain-check configuration does not start a measurement. Unknown accesses fail. | PDP `0x81323600`, `0x81322958`, and `0x813226ec`. See `models/qcom/sm88xx/pdp/evidence.md`. |
| CPRH diagnostics | Shared sequencer and target status | Synchronous power requests leave idle sequencers. Target status reports applied corners and virtual voltage. Explicit samples publish their result flags. | PDP `0x81320ea0`. See `models/qcom/sm88xx/pdp/evidence.md#initialization-diagnostics`. |
| Adaptive clock distribution | `qcom,ncc-acd-v1` banks at `0x199d0000` and `0x19aa2000` plus `lane << 24` | Configuration registers retain writes from declared reset values. Status, counter, and debug registers keep reset values. A calibration edge completes at the write and captures the configured delay code. Clock-droop response has no modeled effect. | PDP `0x81320fec`, `0x81320848`, and `0x81321704`, and the `ncc_acd_acd_ncc` register bank. See [the NCC ACD calibration result](#ncc-acd-calibration-result) and `models/qcom/sm88xx/pdp/acd_evidence.md`. |
| CPRH ideal sensors | Explicit reference and oscillator clocks | Manual samples count a fixed virtual oscillator without aging drift. Regulated samples match their programmed targets. Completion requires available inputs and published results. Firmware calculates its own aging adjustment. | PDP `0x81323250`, `0x813235f0`, and `0x81323456`. See `models/qcom/sm88xx/pdp/evidence.md#functional-aging-measurements`. |
| PDP DPM and limiter controls | Nine cluster DPM doublewords, 33 doublewords for each present core, and fourteen limiter windows per lane | Guest configuration starts at virtual zero and retains writes. No physical power or clock limit comes from these words. Unknown offsets fail. | PDP `0x81326c40`, `0x81326eb0..0x81326f38`, `0x81327020`, and `0x813271cc`. See `models/qcom/sm88xx/pdp/evidence.md`. |
| Clock-gating overrides | Five NSI banks per lane and nine CPUCP clock-controller words | Retained override words. NSI words use declared reset values and fields. The clock-controller words use declared virtual zero and full 32-bit fields, because no source defines them. Hardware clock gating has no modeled effect. | CPUCP `0x1d808d08` and `0x1d80ad6c`. See `models/qcom/sm88xx/cpucp/subsystem_evidence.md#clock-gating-overrides`. |
| Core activity monitors | Group-zero and group-one AMU counters for each present core | Core cycles count at the lane PLL and constant cycles at the reference, only while enabled. Retired instructions, memory stalls, and auxiliary events have no source, so their enables fail. WFI and power loss do not reduce the cycle count. | CPUCP ACEHR0 channel 41 and the `NCC_AMU_0` register bank. See `models/qcom/sm88xx/cpucp/amu_evidence.md`. |
| AMUv1 system-register counter sources | Opt-in `x-qcom-ids` libqemu profile | `CPU_CYCLES` and `CNT_CYCLES` count from the QEMU virtual clock that also clocks the generic timer. `CPU_CYCLES` uses a declared 1 GHz core frequency. `CNT_CYCLES` uses the generic timer frequency. `INST_RETIRED` counts retired instructions only with precise icount. Without precise icount it has no source, and a clock value does not stand in for retired instructions. `STALL_BACKEND_MEM` and the eight auxiliary counters also have no source. A counter without a source retains the guest's last write. A clock-based counter keeps counting during an idle vCPU. Genuine AMUv1 silicon stops counting in WFI and WFE. | `__cpu_setup` at SM8850 kernel image offsets `0xc97736ec..0xc97736f8`. Arm DDI 0601 defines `CPU_CYCLES`, `CNT_CYCLES`, `INST_RETIRED`, and `STALL_BACKEND_MEM`. See `docs/qcom/sm88xx/decisions.md` and `models/qcom/apss/evidence.md`. |
| Functional NCC DVFS | Shared request, state, and configuration registers | Configuration alone creates no transition. A request changes its selected clock, power, and memory-access states before completion. A locked or invalid request preserves the applied state. | PDP `0x20003cbc`, `0x20004684`, and `0x200036e0`. See `models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md`. |
| Per-core DVFS arbitration | Six software slots per lane | Enabled slots request a shared operating state. The virtual policy selects the highest effective frequency, not the highest state index. Effects precede completion. Silicon priority remains unmeasured. | PDP `0x200039ec`, `0x20003a54`, and `0x20003b80`. See `models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#per-core-software-votes`. |
| Boot SRAM read-assist | NCC memory-access configuration and status | Powered virtual SRAM applies a complete three-word policy when `BOOTMEMACCEN` is set. Completion clears without the request or power. The model bypasses analog timing and voltage effects. | SM8850 ROM `0x334ff6`. See `models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md`. |
| Virtual SOCCP services | Released remote peer over SMEM, GLINK, and IPCC | The peer requires guest-created SMEM structures. It allocates remote-owned items and answers accepted requests from declared virtual state. | `models/qcom/soccp/evidence.md` records the release sequence, protocol, and battery policy. |
| AOP QMP endpoint | `qcom,aop-qmp-v1` peer over `0x0c300000..0x0c3003ff` and IPCC client zero, signal zero | The peer publishes the descriptor, the AOP state, and the mailbox geometry from declared virtual values. It acks the link and channel handshake and clears a length only after a declared effect. The model logs each clock vote and each PASR request. No clock, refresh, or power effect follows. A message with an unknown class, an unknown resource, an oversize length, or no terminator stays in the mailbox. The peer acknowledges no other message. | Selected GlinkDxe RVAs `0x7a44..0x7a84`, `0x8120..0x8290`, `0x8598`, and config `0xdb18`. See `models/qcom/aop/evidence.md`. |
| Packed NCC AUX source | Explicit existing clock source | Software mux selection uses live AUX or PLL availability. Timeout fields retain counts and do not manufacture lock status. | `models/qcom/sm88xx/cpucp/packed_root_evidence.md`. |

No substitute supplies an authentication result. The RPMB model performs its declared key, MAC, counter, and range checks.
Modeled remote services derive replies from accepted guest requests and declared virtual state.
The implementation uses the pinned CPUCP CSR substitute without a firmware patch.
Unsupported transactions stop execution. Explicit exploratory configuration logs every accepted substitute access.
These statements do not establish hardware fidelity or either kernel handoff.

## SPMI and PMIC contract

Owner boundary: this section covers `models/qcom/spmi/spmi.h` and `models/qcom/spmi/pmic.h`.
The integration owner controls factory registration, address maps, and private board data.
The headers alone do not establish successful PMIC initialization or kernel entry.
SM8850 run47 and SM8845 run44 complete the original PMIC callback through device and driver initialization.

### Selected binary evidence

The source is `CPH2749export_11_16.0.5.700EX01_2026022701130186/IMAGES/xbl_sc.elf`.
Its SHA-256 is `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a`.
ELF program headers map file `0x1000` to `0x14827000` and file `0xc2000` to `0x148eb000`.
File `0xae000` maps to `0x148d4000`. These mappings establish the coordinates below.

- `0x1487b284` obtains PMIC interface `0x18`. Calls at `0x1487b2c0` and `0x1487b3a0` use vtable offsets zero and `0x20`.
  The associated log labels identify device initialization and driver initialization.
- `0x148398a8` performs device setup. It calls SPMI setup through `0x1483992c`, then identity discovery at `0x148343c4`.
- `0x148344a8..0x148345c4` probes SIDs zero through thirteen on both buses.
  It reads four bytes at `0x0102`, seven bytes at `0x0100`, and one variant byte at `0x0160`.
  Type `0x51` identifies a PMIC. Bytes `0x0102`, `0x0103`, and `0x0105` supply minor revision, major revision, and model.
  Nonzero `0x0106` supplies the SID span. A zero span invokes firmware defaults.
- `0x1486e8c4..0x1486e9ec` programs ownership before PPID routing.
  Dynamic allocation writes guest-selected APIDs. No earlier-stage APID assignment is reconstructed by the model.
  Setup calls `0x1486e83c` with `w1=1` at `0x1483994c`.
  This sets the dynamic-allocation flag at structure offset `0x17`.
  Reset first clears routes at `0x1486f8e4`. The selected path can therefore start with an empty routing table.
- Descriptors `0x148f3b18` and `0x148f3b70` supply common base `0x0c400000`.
  They select channel counts 2048 and 512, with global APID starts zero and 2048.
  Descriptor field `+0x48` selects hardware-interface offsets `0xb000` and `0x16000`.
- Geometry at `0x148f3ad8` and `0x148f3b00` supplies six/two PVC ports with sixteen entries each.
  It also supplies thirteen masters, nine/five arbiter priorities, and ten/two MGPI ports.
- `0x14870264..0x14870794` encodes commands and preserves command/data order.
  `0x14870844..0x14870904` interprets DONE, FAILURE, DENIED, and DROPPED as bits zero through three.
  `0x14870908..0x148709e4` writes little-endian FIFO data before a write command.
- `0x1486f2e0..0x1486f358` programs one common VRM table at offset `0x2300`.
  Bits `22:21` select the bus, bits `20:16` select the SID, and bits `15:8` select the peripheral.
  This differs from the earlier Python rehost's per-bus VRM table assumption.

The following direct PMIC calls are reachable in the linked initialization routines.
They establish register operations, not captured reset values.

| XBL call site | Bus/SID | Register contract |
|---|---|---|
| `0x14834ef8`, `0x14834f4c` | 0/0 | Read `0x088c`, masked by `0x20` or `0x10`. |
| `0x14834f0c`, `0x14834f60` | 0/0 | Read `0x08da`, masked by four. |
| `0x148399d4`, `0x1483b0f0` | 0/0 | Read-modify-write `0x088c`, mask/value `0x20` or `0x10`. |
| `0x1483b664` | 0/0 | Write one or two to `0x9a5a`. |
| `0x1483b8fc` | 0/0 | Read two diagnostic bytes at `0x0150`. |
| `0x1483c8f0` | 0/0 | Tail-call a byte write at `0xa245`, with value zero or `0x80`. |

`qcom_rehost/spmi.py` and `qcom_rehost/pmic.py` supplied comparative behavior evidence.
Their PmicDxe coordinates, discovery masks, battery stimuli, regulator profiles, RTC, and SDAM policies are not selected-XBL evidence.
This implementation does not copy those peripheral inventories or seed them from routing.

### Transport registers and transitions

All MMIO addresses below are relative to the arbiter socket.
Only aligned, little-endian, four-byte MMIO transfers are supported.
Byte enables and unsupported bursts fail.

| Offset | Contract |
|---|---|
| `0x3000 + global_apid * 4` | Retained PPID route. Mask `0x80001fff`. SID occupies bits `12:8`, peripheral occupies `7:0`, and bit 31 records IRQ ownership. |
| `0x2a000 + bus * 0x11000 + local_apid * 4` | Retained owner index, mask fifteen. Only master indices zero through twelve are supported. |
| `0x2300 + entry * 4` | Shared VRM route, 256 entries, mask `0x7fff00`. |
| `0x500000 + global_apid * 0x200` | Write-channel aperture. |
| `0xc0000 + owner * 0x40000 + global_apid * 0x20` | Observer aperture. Only the configured executing owner can access it. |
| Channel `+0`, `+4`, `+8` | Command, retained CONFIG bit zero, and read-only status. |
| Write channel `+0x10`, `+0x14` | Retained WDATA words. Each command requires every requested write byte to be staged again. |
| Channel `+0x18`, `+0x1c` | RDATA words. Reads require a successful preceding read command. Unused bytes read zero. |
| Write channel `+0x100`, `+0x104`, `+0x108` | Retained IRQ enable bit, deasserted status, and idle W1C clear. |
| `0x4c0000 + bus * 0x10000 + owner * 0x1000 + group * 4` | Read-only, deasserted PIC status for all thirteen master banks. Reset reads every master bank. |

Command bit 27 selects read rather than write. Bit 26 retains write priority.
Bits `11:4` select the peripheral byte offset. Bits `2:0` encode length minus one.
Commands transfer one through eight bytes and cannot cross a peripheral boundary.
Other opcodes and reserved bits fail. Observer writes cannot issue peripheral write commands.

The explicit functional policy starts each channel in DONE.
A valid command clears status and invalidates old RDATA before it accesses the PMIC.
The synchronous target then returns one of these results:

- Success sets DONE and publishes read bytes only for a read command.
- An absent SID sets DONE|FAILURE. It publishes no data.
- A disabled bus or an unauthorized write owner sets DONE|DENIED. It changes no PMIC byte.
- Unsupported owned PMIC operations set FAILURE and fail the originating MMIO transfer.

The explicit secure-owner mask can admit declared foreign owners for specified secure transactions.
Unspecified or nonsecure attributes do not acquire that capability.
The OEM profiles declare secure access to the nonzero owners in their selected firmware maps.
This is an explicit loader capability, not recovered physical permission data.
The explicit exploratory policy handles only the ordinary configuration range documented in `models/qcom/spmi/README.md`.

Cold reset clears routes, owners, FIFO bytes, IRQ enables, and retained controller controls.
It restores the explicit initial routes, owners, and inherited bus enables.
There is no asynchronous queue, retry, external IRQ producer, or DROPPED-event producer.
The policy does not simulate link latency.

Controller initialization starts at SM8850 `0x1486f9e0` and SM8845 `0x1486e0e0`.
It retains per-bus words `0x25000`, `0x25010`, `0x25014`, `0x25018`, `0x25048`, `0x27800`, `0x27804`, and `0x27808`.
Their masks are `1`, `1`, `3`, `0x27f`, `1`, `3`, `0x1c600bab`, and `1`.
Setup functions `0x1487b4f4` and `0x14879bf0` set bits 22 and 21 at `0x27804`.
The bus stride is `0x11000`. Diagnostics at `0x25028`, `0x2502c`, and `0x28000` report no injected errors.
Global words `0x3c`, `0x44`, and `0x48` retain bit zero.
Dynamic setup also retains bit zero at `0x27814 + bus*0x11000`.
Arbiter priority words at `0x100 + bus*0x50 + input*4` retain four bits.
Their nine/five entry counts come from `0x1486e7d0..0x1486e808`.
Reset writes a distinct bus-one owner bank at `0x3d000..0x3d7fc`.
The model retains that bank without an unproved alias to local ownership.

Timing writers `0x148bdba0` and `0x148bc0c8` program 31 words in each bank.
The banks occupy `0x25100..0x25178` and `0x36100..0x36178`.
Each word retains all 32 bits. Cold reset clears both banks.
The model does not simulate analog timing or generate PMIC commands from these values.

SM8850 also programs GPIO226 and GPIO227 at `0x0f1e2000` and `0x0f1e3000`.
Stores `0x1487b544/0x1487b548` write `0x1f01`.
The existing retained-control model accepts mask `0x1fff` from explicit cold zero.
The same setup retains full words at `0x2781c/0x27820` in each bus bank.
The selected SM8845 contexts set the skip-pad flag at offset `0x50`.
No electrical behavior or additional readiness effect follows these retained writes.

The three SPMI2 branches use the existing calibration root, not an independent always-on source.
Their source descriptors are SM8850 `0x148f1850` and SM8845 `0x148ef408`.

| Branch | SM8850 descriptor / register | SM8845 descriptor / register |
| --- | --- | --- |
| AOD serial | `0x148f2508` / `0x0c2a81dc` | `0x148f0018` / `0x0c2a81e8` |
| Fast serial | `0x148f2528` / `0x0c2a81d4` | `0x148f0038` / `0x0c2a81e0` |
| Serial | `0x148f2548` / `0x0c2a81cc` | `0x148f0058` / `0x0c2a81d8` |

SM8850 run31 and SM8845 run29 pass this initialization group and the later protection-policy transitions.
Both complete the thermal callbacks before entering the subsequent PMIC initialization group.
These runs do not establish completed PMIC discovery, UEFI, or kernel execution.

The PVC, VRM, MGPI, and VIOCTL configuration routines at `0x1486f06c..0x1486f7cc` retain their bounded digital tables.
These tables do not generate PMIC commands without an external hardware request.
No such request input is connected.

PVC words use offsets `0x300 + bus*0x100`, `0x308 + bus*0x100 + port*4`, and `0x900 + bus*0x400 + port*0x40`.
VRM enables use `0x2100` and `0x2110 + bus*0x40`.

MGPI configuration uses its descriptor offset, eight-byte port strides, and a global enable at `+0x100`.
Priority uses `+0x1004 + port*0x1000`.

SM8845 descriptors `0x148f1520/0x148f1578` select the same offsets and ten/two port counts.
The selected helpers are SM8850 `0x1486f404` and SM8845 `0x1486db04`.
Their ten-entry defaults start at `0x148e0f30` and `0x148def30`, with ten-byte records.

The model retains paired edge configuration, enable bits, priority, and each global gate.
These gates cannot bypass software bus enable or APID ownership.

VIOCTL uses positive-edge words at `0x27a00`, self-clear words at `0x27a80`, and negative-edge words at `0x27b80`.
Each bank uses bus stride `0x11000` and four-byte port strides.
The edge word mask is `0xff8fffff`. The self-clear word mask is one.
These are not MGPI port banks or arbiter priority banks.

Both selected VIOCTL tables contain nine records and use indices `1,4,5,6,7,8,9,10,12`.
SM8850 pointers start at `0x14913c40`, with records at `0x148e0f94`.
SM8845 pointers start at `0x14914c10`, with records at `0x148def94`.
Each record has eleven bytes.
The model provides thirteen retained slots per bus as a virtual capacity, not a physical silicon limit.

The selected SM8850 helper is `0x1486f5f0`.
This operation is `SpmiBusCfg_ConfigureVioctl`.
The VIOCTL register definitions supply the three masks.
Unknown words remain unsupported.

Run131 records zero from ten MGPI programming calls, global enable, nine VIOCTL calls, and the complete configuration function.
The respective return sites are `0x1483a184`, `0x1483a274`, `0x1483a300`, and `0x1483a150`.
Evidence: `build/private/run-sm8850-common-runtime131/spmi-rsc-probe.json`.

### Virtual PMIC state and integration

`qcom_vp::QcomVirtualPmic` implements a sparse byte target.
The target address is `(bus << 20) | (sid << 16) | register_address`.
It accepts one through eight bytes without a peripheral boundary crossing.
It validates the whole burst before it changes any byte.
Read-only writes, undeclared bytes, and changes outside writable masks fail.
An absent SID alone returns `TLM_ADDRESS_ERROR_RESPONSE`, which the arbiter converts to NACK.
Other PMIC errors remain fatal MMIO errors.

CCI `identities` contains semicolon-separated rows:
`bus sid model major minor span variant`.
Each number accepts decimal or `0x` notation.
Each primary identity declares its complete bounded SID span.
Continuation banks mirror revision fields and report their remaining span.
Digital revision bytes `0x0100..0x0101` are virtual zero.
These values are explicit virtual conditioning, not measured PMIC identity.
No identity exists by default.
The integration inventory below supplies all eleven primary identities. It does not rely on an empty default.
Selected-XBL selector `0x14885064` accepts models `0x35..0x64` and indexes the pointer table at `0x148df9b0`.

| Declared model | Selector entry | Discovery mask address |
|---|---|---|
| `0x5c` | `0x148dfae8` | `0x148f3ea4` |
| `0x5d` | `0x148dfaf0` | `0x148f3ee4` |
| `0x61` | `0x148dfb10` | `0x148f3f44` |
| `0x56` | `0x148dfab8` | `0x148f4104` |
| `0x60` | `0x148dfb08` | `0x148f3f64` |
| `0x48` | `0x148dfa48` | `0x148f40c4` |
| `0x41` | `0x148dfa10` | `0x148f3fe4` |

These selectors prove firmware recognition. They do not prove physical board presence or reset revisions.
The virtual identity inventory uses earlier declared models and revisions, not physical captures.
SM8850 PMIH now declares one SID because the selected metadata record requires the flat identity path.
Other bounded spans retain their earlier explicit virtual policy.
See `models/qcom/spmi/README.md` for the selected-image evidence and manufacturing inputs.

CCI `pon_peripherals` accepts rows `bus sid peripheral`.
Each row creates a virtual PON type-one, subtype-eight revision block with digital and analog revisions 1.0.
XBL `0x14833050` requires type one. Its bounds at `0x14833108..0x14833130` admit these revisions and require subtype eight.
QSEE also reads the eighth revision byte in the same burst (run500).
No source supplies a value, so that byte stays read-only zero.
Run501 and run502 read the status field at `base + 8` and the interrupt block at `base + 0x10`.
The status field reads cold zero because this policy has no event producer.
Offsets `0x17` and `0x1c..0x1f` complete both interrupt groups and also read zero.
The phone record `20261026T161459Z` reads those groups after a warm reboot and reports latched state in them.
QSEE then reads `0x20..0x3f` and `0xd0..0xd7` (run505, run506).
The record reads zero at `0x20..0x3f` and reads warm state at `0xd1`, so both blocks stay at cold zero.
The virtual profile keeps the cold value and rejects writes, so no warm boot cause enters the model.
The upstream [qcom-pon driver](https://github.com/torvalds/linux/blob/master/drivers/power/reset/qcom-pon.c) names only the soft-spare byte at `0x8f` for reason bits, and this model keeps that owner.
The explicit cold PON group also retains offsets `0x60`, `0x8c`, `0x8d`, and `0xda`.
Their masks are `0x80`, `0x30`, one, and four. They start at virtual zero.
The observed calls at `0x1483318c`, `0x148331b0`, `0x148399d4`, and `0x1483b0f0` establish those controls.
These writes change digital control state only. They do not produce reset or power acknowledgments.

CCI `registers` contains semicolon-separated rows:
`bus sid address reset writable_mask`.
Every row declares one virtual constant or retained byte on an existing SID.
A zero writable mask makes the byte read-only.
Rows cannot replace identity bytes or overlap.
Cold reset restores declared byte values.
The profile must name each required byte and its evidence.
It must not seed command-completion, regulator-ready, ADC-result, or power-good bytes to bypass an unimplemented device operation.
Analog voltage, temperature, power transitions, PBS execution, charger operation, and RTC progression remain unsupported.
The register inventory is not a complete implementation of all PMIC driver initialization.

The integration owner must add these two includes to `src/platform.cc`:
`"spmi/spmi.h"` and `"spmi/pmic.h"`.
Use the existing `ModuleRegistrationWrapper` pattern with these factory pairs:

- `qcom,spmi-arbiter-packed-v1` → `FactoryMaker<qcom_vp::QcomSpmiArbiter>`.
- `qcom,virtual-pmic-registers-v1` → `FactoryMaker<qcom_vp::QcomVirtualPmic>`.

```cpp
static gs::ModuleFactory::ModuleRegistrationWrapper spmi_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,spmi-arbiter-packed-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomSpmiArbiter>(
            "qcom,spmi-arbiter-packed-v1"),
        "SPMI arbiter constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper pmic_registers_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,virtual-pmic-registers-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomVirtualPmic>(
            "qcom,virtual-pmic-registers-v1"),
        "Virtual PMIC constructor", cci::CCI_ABSOLUTE_NAME);
});
```

Both classes take only `sc_module_name`.
Map arbiter `target_socket` at `0x0c400000`, size `0x00900000`, through the APSS router with relative addresses.
Bind arbiter `pmic_socket` directly to PMIC `target_socket`.
Do not map the PMIC target into CPU MMIO.
Do not replace the existing PS_HOLD power consumer.

Lua arbiter fields are `moduletype='qcom,spmi-arbiter-packed-v1'`, `executing_owner=0`, and `completion_policy='synchronous-virtual'`.
Set `target_socket={address=0x0c400000,size=0x00900000,bind='&apss_router.initiator_socket'}`.
Set `pmic_socket={bind='&xbl_pmic.target_socket'}` when the PMIC instance is named `xbl_pmic`.
Its `moduletype` is `qcom,virtual-pmic-registers-v1`.
Supply `identities` and `registers` from the explicitly selected virtual board inventory.
`initial_bus_enable` is a two-bit inherited-state declaration. Its default is zero.
CCI `initial_routes` accepts rows `bus apid sid peripheral owner irq_owner`.
Rows use local APIDs and semicolon separators. They declare constructed routing, not captured earlier-stage assignments.
The default is empty. Each identity probe requires a route for peripheral one, including absent SID probes.
Other routes must follow the finite peripheral inventory. A route does not create a PMIC peripheral.
The selected device-setup path enables dynamic route allocation after it clears the initial table.
The selected XBL enables bus zero at `0x1486fb80..0x1486fb88`.
It enables bus one at `0x1487b61c..0x1487b638`, called from `0x1483b2ec`.
The concrete inventory below therefore starts both inherited bus-enable bits at zero.
The headers contain no physical SoC base and no SoC-name conditionals.

This change does not install the integration.
After installation, check command packing, staged-data rejection, NACK, ownership denial, unknown-byte failure, burst atomicity, reset, and board initialization.

### Concrete virtual identity and control inventory

The integration owner can use this Lua group after the two factory registrations.
It declares compatible PON groups on every populated virtual SID, as the earlier rehost did.
This is a virtual control inventory, not a claim that every physical PMIC contains that PON block.
The code supplies no regulator-ready, ADC-result, PBS-completion, or power-good byte.

```lua
local identities = {
    {0,0,0x5c,2,0,1,0},
    {0,1,0x5d,2,0,2,0},
    {0,3,0x61,2,1,2,0},
    {0,5,0x61,2,1,1,0},
    {0,6,0x61,2,1,2,0},
    {0,8,0x61,2,1,2,0},
    {1,0,0x5c,2,0,1,0},
    {1,7,0x56,2,1,2,0},
    {1,9,0x60,2,1,1,0},
    {1,10,0x48,2,0,2,0},
    {1,12,0x41,1,3,2,0},
}
local identity_rows, pon_rows = {}, {}
for _, p in ipairs(identities) do
    identity_rows[#identity_rows+1] = table.concat(p, ' ')
    for sid=p[2],p[2]+p[6]-1 do
        pon_rows[#pon_rows+1] = string.format('%d %d 8', p[1], sid)
    end
end
platform.xbl_pmic = {
    moduletype='qcom,virtual-pmic-registers-v1',
    identities=table.concat(identity_rows, ';'),
    pon_peripherals=table.concat(pon_rows, ';'),
    registers='0 0 0x0150 0 0;0 0 0x0151 0 0;' ..
              '0 0 0x9a5a 0 3;0 0 0xa245 0 0x80',
}
platform.xbl_spmi = {
    moduletype='qcom,spmi-arbiter-packed-v1',
    executing_owner=0,
    initial_bus_enable=0,
    initial_routes='',
    completion_policy='synchronous-virtual',
    target_socket={
        address=0x0c400000, size=0x00900000,
        bind='&apss_router.initiator_socket',
    },
    pmic_socket={bind='&xbl_pmic.target_socket'},
}
```

The two diagnostic bytes at `0x0150` start at explicit virtual zero.
The raw control bytes at `0x9a5a` and `0xa245` retain the actual guest writes.
No adjacent register exists unless another declared group supplies it.
The explicit PON spare bytes select a cold boot, without a retained warm-reset claim.

The next established peripheral group is the SDAM/PON history service.
In `0x14832fd4`, the next direct PMIC read after PON revision discovery is bus zero, SID zero, byte `0x726b`, at `0x14833168`.
Its bit six controls a later spare-bit write. It is not an analog-ready flag.
After that read, `0x14833274` reads `0x7446`, `0x7445`, and `0x7447` at `0x148332d4`, `0x148332ec`, and `0x14833304`.
It then reads 117 bytes from `0x744b`, through eight-byte transport chunks, at `0x14833328`.
A nonzero selector additionally reads 117 bytes from `0x754b` at `0x14833350`.
Those transactions intentionally remain unsupported by the identity/control group.
They require a coherent virtual history layout, not a fabricated ready byte.

Later driver discovery at `0x14884c44..0x14884c68` reads six revision bytes for each selected discovery bit.
For the declared primary model `0x5c`, the first non-identity probe is peripheral four, address `0x0400`, count six.
The mask at `0x148f3ea4` proves that probe.
This group does not replace unresolved peripherals with generic MISC identities.
Their type, register protocol, and functional policy must be established before their drivers can bind.

### DCP discovery and explicit virtual absence

Both OEM PMIC definitions enable `virtual_absent_discovery`. Its default is false.
This policy applies only to populated SIDs and peripherals without any declared byte, PBS service, or earlier exploratory write.
An exact seven-byte read at peripheral offset zero returns seven zero bytes.
All other reads and writes keep their pre-existing behavior, including exploratory configuration and global locks.
An accepted exploratory write, including zero, prevents an absence response for that peripheral.
Unknown discovery bytes then remain errors.
Cold and warm reset clear this write history with the exploratory storage.
Partial revision reads do not receive an absence response.
Absent SIDs still return a transport NACK.

Each substituted peripheral produces one `UNMODELED_PMIC_ABSENT` line per model instance.
The line records the bus, SID, peripheral, and `policy=virtual-absent`.
This is the selected virtual inventory, not a claim about physical PMIC presence.
Declared peripherals keep their existing identity, controls, and error behavior.
No generic MISC identity, measurement, or completion value is added.

DCP reads seven revision bytes at `0xa00f0a8a` on SM8850 and `0xa411cc78` on SM8845.
The seventh byte is the discovery dummy.
Equal subtype and dummy bytes identify absence in the public Qualcomm decoder and both selected images.
Existing declared revision records now include that read-only dummy byte.
This corrects the SID0 `0x0400` probe without assigning a new physical peripheral type.
See `models/qcom/spmi/dcp-evidence.md` for both images' register callers, scan masks, and remaining limits.

The separate GPIO identity correction declares digital revision 3.1, with the existing type `0x10` and subtype `0x13`.
Both selected DCP drivers reject packed revisions `>=0x32`.
Both selected XBL and UEFI drivers accept values `<0x33`.
Three-VIN capability depends on the subtype, not revision 3.2.
This corrects the earlier virtual choice without changing GPIO controls or patching firmware.


### QSEE devcfg dump observation

Both OEM devcfg images enable the new PMIC dump framework.
`PM_NEW_DUMP_FRAMEWORK` is an inline type-2 word, and both images hold the value one.
The QSEE accessor `0x15726a48` delivers that word from devcfg file offset `0x946c` for SM8850 and `0x93cc` for SM8845.
The framework at `0x15735970` dumps only when the word equals one and the bitmap pointer is not null.

`PM_PERIPH_LIST_BITMAP` uses property type `0x12`.
That type returns a `{size, pointer}` pair from the runtime blob registry at `ctx+8`, indexed by the property value.
SM8850 stores index `0x19` at file offset `0x9474`, and SM8845 stores index `0x14` at `0x93d4`.
The registry and the bitmap bytes are therefore runtime data.
Neither image holds a `0x380`-byte bitmap array.

The platform selects the dump groups in `dump_peripherals`.
The explicit form lists `bus sid peripheral` rows, and the value `present` selects every present group.
A group takes part only when the model reads a nonzero presence byte at peripheral offset four.
That byte comes from a declared revision record or from the PBS interface.

The `present` profile rests on three facts.
QSEE dumps only a peripheral that the bitmap selects and the offset-4 probe reports present.
The bitmap is a runtime blob, so no image supplies its bytes.
The dump stores its bytes and never reads them back.
A present-set superset therefore changes no read that QSEE issues.
An observer read of an unowned byte in that group returns virtual cold zero.
It also prints one `PMIC_DUMP_READ_UNDECLARED` line for that byte.
Writes, non-observer reads, and every other undeclared read keep the existing rejection.
This rule lets the firmware complete a data-only dump without inventing control state.

The dump data path stores bytes and makes no decision on them.
`0x15735cf4` zeroes a `0x100`-byte stack buffer, fills it on a successful read, and copies it into a slot record.
Only the SPMI status and the presence byte at peripheral offset four select a branch.
The slot base `DAT_158065a8` has two references, and both sit inside the dump code.

Both OEM platforms select the `present` profile.
QSEE run507 first read bus zero, SID zero, PON address `0x08d8`, and later runs read the PBS groups at `0x1608`, `0x1c08`, and `0x2208`, then the group at `0x5000`.
The bitmap contents remain unproved.
A group that the bitmap omits receives no read, and a group that it selects stays inside this contract.

## DDR and SHRM contract

The virtual SHRM transport replaces SHRM execution effects, not image authentication.
Both selected OEM runs complete the original signed SHRM loading path.
Do not remove that boot record or change its authentication result.
Do not skip the DDR phase or fabricate SMEM item 603.
Unchanged XBL must create geometry, regions, and shared handoff state from coherent backend results.

### Selected image evidence

`build/private/shrm-command-facts.json` records the selected hashes, ELF mappings, command handlers, and limitations.
It uses each image's own `PT_LOAD` coordinates.
The SHRM entry is RISC-V address `0x200000`, not its APSS load address `0x31020000`.
The SHRM data segment maps local `0x400000` to APSS `0x31048000`.

XBL startup at `0x148adb08` writes zero to `0x31076004`.
It waits for bit four at `0x31076008`.
Mode control at `0x148adbf4` writes inverse enable to `0x31076010` and enable to `0x31076060`.
Command helper `0x148adc1c` writes the request at `0x31076068`, then writes one to trigger `0x31076064`.
It waits for `(request & 0xfcff0000) | 0xf0` at `0x3107606c`, then clears the trigger.
That formula identifies completion. It does not establish command effects.

| Operation | Selected request | SHRM handler |
|---|---|---|
| SRAM retention | `0x05030000` | Class-one dispatch to `0x201f78`, then helper `0x201e0a`. |
| Clock transition | `0x09080000` plus primary and secondary level nibbles | Opcode one, `0x201214`. |
| Training snapshot | `0x05060000` | Class-one dispatch to `0x201fbc`. |
| Restore snapshot | `0x05070000` | Class-one dispatch to `0x201f4e`. |
| Training callback | `0x09800000` plus a 16-bit runtime index | Opcode sixteen, `0x2014f2`, then `0x203c58`. |
| Exit | `0x09410000` | Opcode eight, `0x201c3e`. |

Class one redirects commands through handler 26.
Its subcommand selects the jump table at `0x20df30`.
Training and restore use the bounded register inventory at local `0x4054b4`.
Helper `0x208526` transfers at most `0xa000` bytes between registers and a translated external snapshot buffer.
These operations do not prove a physical training solver.
The virtual backend must provide a coherent training representation before it acknowledges either operation.

The training handler selects a 64-byte runtime row through the internal object's field `+0x478`.
The runtime index is not a clock-plan index.
The adapter implements the selected callback effects. It does not execute arbitrary guest function pointers.
SHRM pointer conversion uses internal object `0x405000` and translation addend `0x30c48000`.
That object translates to APSS `0x3104d000`. It is not the primary shared header.
The primary header at local `0x402800` translates to selected XBL base `0x3104a800`.
Startup function `0x200d82` writes version words `{0x1d, 0, 0}` at primary shared offsets `0x0c`, `0x10`, and `0x14`.
After readiness, XBL `0x148ada94` checks those exact words.
The board backend must preserve the distinction between the primary header and SHRM's internal pointer object.
Startup also publishes payload bounds `{0x3104a000, 0x31060000, 0x16000}` at APSS `0x31048000`.
These words supply native permission requests. They do not authorize them.

Request `0x05030000` remains valid after training.
The backend preserves DDR state while it updates SRAM retention masks and the selected trace state.
`models/qcom/sm88xx/ddr/evidence.md` records the exact flag-dependent effects and both OEM helper addresses.


The adapter preserves the internal object's scratch, parameter, data, runtime, and result pointers.
It clears each `0xe00`-byte result buffer before the callback.
It publishes byte counts at internal offsets `+0x488/+0x48c` only after the effects succeed.
Native result metadata determines each packed record's width and destination.

The digital substitute uses 256 four-byte transfers per selected channel.
Each channel therefore transfers 1 KiB through the actual shared DRAM.
The adapter restores the retained PXI program after each normalized transfer.
Read-eye callbacks compare existing bytes without rewriting corrupted bytes.
The ideal electrical eye spans zero through 112. This range is virtual policy, not a physical margin.

Snapshot operations interpret the guest's register descriptors at local `0x4054b4`.
Save reads actual register words. Restore writes actual guest snapshot bytes.
Neither operation substitutes a zero-filled image.
The descriptor count limits the written prefix within the `0xa000`-byte capacity.

SM8845 selects lookup-table relocation `0x38` through `training_data_offset`.
Its result metadata and mutable runtime fields retain their original addresses.
The model contains no SoC condition.

### Transport state and failure policy

`qcom_vp::QcomVirtualShrm` owns `target_socket` and resolves `qcom_vp::QcomVirtualDdr` through CCI `backend_path`.
Resolution accepts an absolute object path or a sibling path relative to the parent module.
A missing backend or unsupported policy fails elaboration.
All register offsets are relative to the mapped target aperture.

| Offset | Read behavior | Write behavior |
|---|---|---|
| `0x04` | Retained clock-gating bit. | Accept zero or one. Neither value releases processor reset. |
| `0x08` | Bit four reflects startup readiness. Other bits are zero. | Unsupported. |
| `0x10` | Retained mode bit. | Accept zero or one while idle and trigger-clear. |
| `0x60` | Retained route bit. | Accept zero or one while idle and trigger-clear. |
| `0x64` | One during execution or after successful completion until clear. | One submits a fresh request. Zero clears a completed trigger. |
| `0x68` | Last staged request. | Stage a request only after startup, while idle and trigger-clear. Clear the previous response. |
| `0x6c` | Last successful response, or zero before completion. | Unsupported. |

CCI `startup_policy='guest-release'` starts with readiness clear.
The separate `processor_control_socket` retains the processor-control word at board address `0x31079000`.
Bits zero and one assert virtual reset.
Clearing both bits calls backend `startup()` before readiness becomes visible.
`startup()` must publish the interface version and complete all required startup effects.
Failure leaves readiness clear and fails the startup transaction.
Repeated release writes are idempotent. They do not reset backend state.
This transition replaces SHRM startup execution. It does not claim measured CPU, PHY, or pin timing.

CCI `completion_policy='synchronous-virtual'` completes commands synchronously through `execute(request, response)`.
The backend must finish every modeled shared-state effect before it returns true.
The transport publishes only the exact expected acknowledgment after that success.
A backend rejection or incorrect response fails the trigger transaction and publishes no completion.
The guest must stage a fresh request after rejection.

The transport requires mode zero and route one for submission.
It rejects every write during backend startup or command execution, including nested submissions.
Reads remain available during execution.
There is no asynchronous queue, retry, interrupt, or physical timing model.
Request execution uses fixed transport storage without host allocation.

Public `reset()` clears startup readiness, controls, staged request, trigger, and response.
It returns false during backend startup or command execution and does not cancel backend work.
It also resets the bound backend without clearing guest memory.
Only aligned, little-endian, four-byte transfers are supported.
Byte enables, unsupported bursts, unknown registers, invalid trigger sequences, and debug writes fail.
DMI is never granted. Debug reads have no command side effects.

### Registration and board integration

The integration owner must include `"ddr/shrm.h"` and `"ddr/virtual_dram.h"` in `src/platform.cc`.
Use the existing factory registration pattern:

```cpp
static gs::ModuleFactory::ModuleRegistrationWrapper virtual_shrm_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,virtual-shrm-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomVirtualShrm>(
            "qcom,virtual-shrm-v1"),
        "Functional SHRM transport constructor", cci::CCI_ABSOLUTE_NAME);
});
static gs::ModuleFactory::ModuleRegistrationWrapper virtual_ddr_registration([] {
    return new cci::cci_param<gs::cci_constructor_vl>(
        CCI_GS_MF_NAME "qcom,virtual-ddr-v1",
        gs::cci_constructor_vl::FactoryMaker<qcom_vp::QcomVirtualDdr>(
            "qcom,virtual-ddr-v1"),
        "Functional DDR backend constructor", cci::CCI_ABSOLUTE_NAME);
});
```

Map the transport registers and the separate processor-control socket.
Expose the same processor-control state to APSS and TME through the existing board routers.
Bind the backend's `memory_socket` to the router target that reaches XBL shared memory and DDR.
Supply all backend topology, frequency, and shared-memory coordinates through the selected board configuration.
Do not embed board physical addresses in either model.
The following transport group assumes that the board already configures a backend named `virtual_ddr`:

```lua
platform.virtual_ddr.moduletype = 'qcom,virtual-ddr-v1'
platform.virtual_ddr.memory_socket = {bind='&apss_router.target_socket'}
platform.virtual_shrm = {
    moduletype='qcom,virtual-shrm-v1',
    backend_path='virtual_ddr',
    startup_policy='guest-release',
    completion_policy='synchronous-virtual',
    target_socket={
        address=0x31076000, size=0x70,
        bind='&apss_router.initiator_socket',
    },
    processor_control_socket={
        address=0x31079000, size=4,
        bind='&apss_router.initiator_socket',
    },
}
```

Do not overlay these registers with a generic ready or mailbox-response stub.
Do not execute a physical SHRM CPU against the same command registers while this substitute owns them.
Keep authenticated image loading independent from that execution choice.
The production DDR helpers include the TME alias for this shared processor-control socket.
The focused reset regression and both native SHRM loading paths pass.
See `models/qcom/sm88xx/ddr/evidence.md` for retained run identifiers and the next failure.

## LLCC coherent-memory policy

`qcom,llcc-v6` preserves four local banks and shared OR/AND broadcast views.
The virtual platform keeps CPU and DMA memory coherent without private cache lines.
Slice requests change activation state before they publish completion.
Completion clearing preserves the active or inactive state.
PMGR and LPI requests expand the guest's selected mask into the corresponding result words.

The FEAC update has no private dirty lines to drain under this policy.
The model preserves guest memory and publishes the observed completion fields after the request.
This policy does not reproduce physical cache timing or replacement behavior.
Unknown ordinary configuration retains guest writes and emits substitute logs.

The platform excludes both security apertures from these configuration windows.
Existing protection controllers retain their authority.

DARE key installation and region initialization are separate operations.
The model retains six actual guest key words per slot and bank.
It publishes marker `0xf03` only after all six words arrive.
A replacement requires six fresh words. Reset clears the material and publication state.

DARE initialization preserves logical data and keys under an explicit virtual policy.
It clears the real auxiliary allocation to canonical zeros before it commits the context.
Those zeros represent virtual metadata, not native cryptographic tags.
The model retains distinct capacities for modes two and three.

Key replacement, changed configuration, and metadata writes revoke affected contexts and cached memory mappings.
The port keeps metadata accesses observable and checks later protected accesses.
QEMU commits queued revocation at the next translation-block boundary.

The selected platform declares an opaque encryption-only slot for native rows that skip key installation.
That policy publishes no marker and cannot satisfy modes two or three.

MTE and ECC remain unsupported. Both native initialization sequences leave those features disabled.
Raw DRAM attacks and physical ciphertext remain outside this boundary.
The key marker does not manufacture image authentication or authorization decisions.
See `models/qcom/llcc/evidence.md` for the lifecycle, public reference limits, and native proof.
See `models/qcom/sm88xx/ddr/phy-evidence.md` for the selected instruction coordinates and native verification.


## Isolated power domains

The selected XBL inventories include domain latches without a modeled peripheral consumer.
Their GDSC instances explicitly set `virtual_supply=true`.
An enable request clears collapse and powers the isolated latch.
A disable request sets collapse and removes its power state.
Read-only status follows that state, not the value written to the status bit.

This policy idealizes the supply and transition timing.
It does not assert readiness for an absent GPU, camera, or MMU.
UFS and USB retain their actual consumer bindings.
An unbound domain without the explicit virtual supply still rejects power-on requests.
See `models/qcom/sm88xx/cpucp/clock_roots_evidence.md` for the native inventories and transition evidence.

## AOP transfer and reset lifecycle

The AOP controller retains configuration and advances two ordered transfer phases.
Request acknowledgments reflect the corresponding retained request.
Halt channels report no autonomous activity or denied request.
Their request bit and upstream clock determine whether the connected clock runs.
These policies idealize hardware timing without changing image admission or authorization.
The selected root generators use an explicit virtual reference, not an inferred physical mux.

The shared AOSS reset controller advances from POR to IDLE after a SystemC delta.
Its control and debug configuration registers retain guest writes independently from its read-only status.
The debug configuration starts at zero and does not report reset completion.
FIRST_PASS and other status flags remain zero under the cold profile.
An AOP reset, image claim, or policy read cannot advance this controller.
The block contains the debug configuration, control, and status registers.
APSS and TME reach the same controller.

The separate image-policy words start at zero and retain original guest writes.
The original claim function owns initialization at TME `0x04d42000`.
The original initializer computes the summary and publication words.
The models do not supply policy bitmaps, authorization grants, or authenticated results.

The cold profile holds APSS until the original CPUCP reset-release store.
The held CPU keeps its quantum keeper stopped.
The existing reset callback restores time synchronization before the wrapper permits execution.
Debugger resume does not remove this hardware hold.
SM8850 run146 records CPUCP `0x33529a` before APSS reset vector `0x300000`.

Both OEMs subsequently load AOP, TZ_AC_CFG, SOCCP_DTB, and SOCCP.
This evidence does not establish UEFI, ABL, kernel execution, or authentication qualification.
See `models/qcom/rpmh/evidence.md` and `docs/qcom/sm88xx/boot-dependencies.md` for the remaining boundaries.

QSEE programs the selected PDC trigger bank at `0x179600f0..0x17960168`.
The platform retains these writes without generating interrupts or preloading the firmware table.
These bits select trigger sensitivity, not completion or pending state.
The shared hardware mutex at `0x18860004` uses the existing atomic claim/read/release model.
It protects actual firmware ticket allocation without replacing the software lock algorithm.
See `models/qcom/sm88xx/boot_control/evidence.md` for both images and execution levels.

## Namespace policy transfer

An unowned permission word records its programming authority.
Its first foreign owner retains that policy without a duplicate base copy.
The last owner can then release the complete transferred policy.
Existing co-owner policies and locks remain separate constraints.
This rule is a functional ownership model, not a measured silicon specification.

SM8845 run121 identifies the duplicated seed during SOCCP_DEBUG loading.
Run122 passes the failed allocation after the correction.
Runs151 and123 subsequently pass the guarded queries for an absent dump policy.
See `models/qcom/sm88xx/tme_protection/namespace-platform-evidence.md` for the producer, consumer, and native evidence.

## Queries for an absent dump policy

The original capture does not include providers for legacy imports `0x1c4` and `0x1d0`.
The authored adapter handles only the empty authorization context.
It shares the denied-report guard and obtains the context, processed state, and grant mask through original OEM callbacks.
It reports absence only when the valid context contains zero bytes and both state values are zero.
Other contexts produce an unavailable result, not a successful bitmap.

The adapter never returns the success sentinel or writes either output.
It does not parse policy records, create grants, or change authentication checks.
Its numeric errors implement compatibility, not recovered original provider behavior.
The original caller takes its own absent-policy branch.
Positive debug-policy contexts remain unsupported.

SM8850 run151 passes this boundary and loads QSEE.
SM8845 run123 passes this boundary and reaches CPUCPFW loading.
These observations do not establish authentication qualification, UEFI, ABL, or kernel execution.
See `models/qcom/sm88xx/sm8845/tme_runtime_abi/evidence.md` for the guard, callback contract, tests, and source limits.

## QSEE fault monitor configuration

The virtual fault monitors retain only the configuration words in the selected firmware tables.
Aligned 32-bit writes preserve all bits. Independent banks retain independent state.
The fixed clock controls share one owner across APSS and CPUCP.
The existing LLCC and DDR owners retain their apertures and state.

Configuration does not create faults, interrupts, or permission grants.
Unlisted status accesses and malformed transactions fail with a diagnostic.
The model does not infer fault-clear semantics from configuration writes.
SM8845 predicate words `0x17e80010` and `0x17e80030` start at explicit virtual zero.
Their physical reset values remain unknown.
See [the source inventory and limits](../../../models/qcom/sm88xx/qsee_fault/evidence.md).


## TME RSC outer controls

The TME RSC composition retains words `0x00c00214` and `0x00c00218`.
The original SM8850 firmware writes `3` and `1`, respectively.
The virtual physical resources remain active. The CPU can enter the separate retained-sleep lifecycle below.
The exact hardware meaning of these outer words remains unqualified.
The existing RSC model completes accepted command batches and raises only enabled completion interrupts.
This substitute does not grant image admission or report AOP readiness.

## Retained TME power lifecycle

The shared power composition retains the four configuration words that the original firmware saves and restores.
It reuses existing storage and adds only the missing words at `0x0081101c` and `0x008110e8`.
The power controller owns direct controls at `0x02237000` and `0x02237144`.
Their explicit virtual request sequence arms one retained sleep.
An actual WFI consumes that arm.
An eligible pending interrupt then resumes through the guest-published restore vector.

Timer frame one generates its actual expiry signal through CLIC source 22.
The original handler re-arms that frame in both selected images.
This route is an explicit virtual inference, not measured silicon wiring.
Guest interrupt masks, pending state, and timer deadlines remain authoritative.

The virtual platform preserves RAM, PMP locks, privilege, security state, and pending interrupt sources.
It does not simulate physical power removal or use architectural reset.
The original restore code controls subsequent interrupt delivery.
External wake status remains inactive under the explicit absent-producer policy, separate from actual CLIC state.
See [the dependency contract and limits](../../../models/qcom/sm88xx/tme_power_state/evidence.md).

## GPCE cold startup configuration

The GPCE startup model supplies explicit virtual geometry and completed cold BIST without an error.
It retains the decoded controls and keeps the existing XPU owner separate.
The named destination selectors do not create keys or authorization.
The model retains the QCE key and nonce words that QSEE programs.
Key reads return zero, and the retained words enable no keyed operation.
A delivered TME transport-key broadcast sets the `01df1020` bit8 status.
The retained key has no read path.
The GPCE unwraps an imported blob only after its tag and destination checks pass.
A FIFO request publishes status one and installs nothing when the command or the authentication fails.
See [the exact source accesses and policy](../../../models/qcom/gpce/evidence.md).

## Native DCP architectural policies

The DCP uses QEMU's bare RV32I CPU with the ISA extensions that both selected images require.
QEMU architecture ID 42 identifies the substitute, not a physical E66 core.
The opt-in `x-sifive-cache-operations` property decodes the four observed SiFive cache instructions.
These instructions execute only in M-mode.
Addressed forms check normal store permission at the operand address, including locked PMP entries.
QEMU memory stays coherent, so the operations do not write back, discard, or erase data.

The upper interrupt banks retain masks, types, and edge latches.
They mirror real sources for power management and do not gate normal CPU or PLIC delivery.
All-level type reset is a functional policy, because neither selected tree specifies a silicon value.
Unexplained bank words retain their values and log one diagnostic.
The bank covers offset `0x280`, because both selected images copy that word into power-management log records.
No decoded consumer gives that word a meaning.

The display domain uses a functional collapse policy.
The selected images program CESTA instructions and RSC sequences without their instruction decoder or signal map.
The model collapses the MDSS core GDSC only at an executed DCP WFI.
The DCP must first set GDSC HW_CONTROL, core-clock retention, and core-clock hardware gating.
The collapse reports power-down completion. The DCP stays in WFI and keeps its reset state.
The model does not execute the sleep or wake TCS commands of the display RSC.
The display PDC declares zero TCS groups, because no evidence supplies its preprogrammed records.

The DCP local watchdog retains its timeout, control, and pet words.
No tree names its counter clock or expiry action, so it never expires.
See [the DCP evidence](../../../models/qcom/sm88xx/dcp/evidence.md).

The DCP CSR word at `0x01e80000` retains the next Zephyr power-state code.
The selected images write `0` for ACTIVE, `6` for RUNTIME_IDLE, and `0xf` for SUSPEND_TO_RAM.
The word does not select a power transition and does not report completion.

## QSEE GPLL vote

The QSEE `gcc_gpll0` and `gcc_gpll7` records vote bits zero and seven at `0x153058`.
The shared GCC PLL owner keeps that vote in a fourth independent master slot.
A voted PLL runs only when its FSM vote mode is enabled. The poll then reads the existing lock status.
See [the boot clock evidence](../../../models/qcom/sm88xx/cpucp/clock_roots_evidence.md).

## QSEE SMMU configuration

The SMMU model keeps separate secure and non-secure global banks.
The `sCR1` value assigns stream groups and contexts to each security state.
A transaction can match only an entry that its security state owns.
The model retains the security state table, but translation does not read it.
Each DMA producer uses its own stream port and physical bus.

QSEE writes TZ_QTI_Config hardware settings to SMMU page 2, to `0x151fc000`, to `0x03de0000` and to `gpu_qtb`.
It also writes four GMU words at `0x03d60000`.
These words retain the guest values. They do not change translation, power, or a completion state.
The `gpucc_gpu_smmu_clk` branch at `0x03d95000` uses the virtual boot-clock reference.
See [the SMMU evidence](../../../models/qcom/smmu/evidence.md).

## Crypto BAM

The crypto BAM model keeps the BAM v2.0 configuration registers.
Its `BAM_REVISION` revision field is the virtual value `0x20`.
The model processes the descriptors that software writes to each programmed ring.
It reads rings and payloads through the APPS SMMU crypto stream `0xc2`.
It writes command elements to physical registers through a separate bus.
It moves pipe data through the QCE stream endpoint.
A completion sets the pipe status, and `P_IRQ_CLR` clears it.
A malformed ring, an unbound endpoint, a failed transfer or an unknown command opcode
prints `BAM_ERROR` and never reports completion.
See [the BAM evidence](../../../models/qcom/bam/evidence.md).

## MDSS software fuse words

QSEE writes four words in the MDSS `swfuse_phys` block at `0x09aa0000`.
SM8850 `0x1574cbd0` and SM8845 `0x1573f834` write `0x11`, `1`, `0x11`, and `1`.
Each write is followed by a check of bit zero.
The words retain the guest values. They do not disable a display feature.

## QSEE ICB boot program

QSEE reads the `icbcfg_info` property of `/dev/icbcfg/boot` from `tz_qti_config.img`.
SM8850 entry `0x15753618` runs 191 records from file offset `0x1cfa8`.
SM8845 entry `0x15746310` runs 91 records from file offset `0x193d8`.
Each record is a direct 32-bit write. The programs contain no read, poll, or delay.
The written words include QTB and NoC QoS registers.
`platforms/qcom/sm88xx/sm8850/qsee-icb.lua` and `platforms/qcom/sm88xx/sm8845/qsee-icb.lua` list each address without another APSS owner.
These words retain the guest values. They do not change bus arbitration or timing.

## QSEE protection controllers

QSEE reads the TZ AC controller inventory through APSS configuration views.
The TZ policy arrays select the virtual type and capacity of each controller.
SM8850 adds ten controllers that have no TME owner. Their geometry is virtual.
A retained secure qualifier keeps the shared QAD-zero owner bit on a release.
QSEE policy-control words without an XBL owner retain the guest values.
See [the TZ inventory evidence](../../../models/qcom/sm88xx/tme_protection/tz-inventory-evidence.md).

## SPSS shared-memory advertisement

SM8850 QSEE `0x15731b20` publishes the SPSS transport allocation at `0x01887000`.
The four retained words have offsets `0`, `8`, `0xc`, and `0x10`.
QSEE writes `1`, the allocation address, zero, and the returned allocation size.
The allocation uses SMEM item 478 for remote host 11 and requests `0x1018` bytes.
The publisher contains no read, poll, or acknowledgement wait.
These words retain the advertisement. They do not emulate an SPSS peer or complete FIFO commands.

The selected Canoe DT names the neighboring Linux advertisement at `0x01885008` and `0x01885010`.
The downstream `drivers/rpmsg/qcom_glink_spss.c` uses the same address and size publication format.
Its transport contains six descriptor words and two `0x800`-byte FIFOs.
The selected SM8845 QSEE configuration does not initialize this transport.

SM8850 run252 passes the advertisement and stops at the TME notification write to `0x17960014`.
This result does not prove SPSS peer execution.

## QSEE-to-TME QMP notification

Both selected packages notify TME through a 32-bit write of one to APSS `0x17960014`.
The shared runtime routes this command to the existing TME CLIC owner.
The OEM registration wrappers prove that the TZ QMP edge selects direct interrupt 28.
The model latches that source until the guest clears bit `0x80` at `0x00c00108`.
Repeated commands coalesce. Source masks and CPU interrupt masks remain effective.
Zero has no effect. Unsupported commands and reads fail rather than return invented status.

The connection and latch behavior are explicit virtual policies.
The generated silicon register name and internal wiring remain unknown.
The model does not modify the shared QMP descriptor or manufacture a peer response.
APSS `0x22002800` and TME `0x02002800` retain their existing shared backing.
The genuine TME firmware owns the peer state and protocol acknowledgements.
See [the notification evidence](../../../models/qcom/sm88xx/tme_mailbox/evidence.md#qsee-to-tme-qmp-notification).

## CPUCP subsystem geometry and local state

The CPUCP interrupt controller implements the compact layout that both selected firmware images derive from their geometry registers.
The virtual geometry declares 256 sources, two contexts, and 16 priorities.
Only context zero connects to a CPU, through evidenced architectural interrupt 16.
The control word at `0x18876104` gates interrupt delivery through explicit inhibit, enable, and commit states.
These control meanings and cold values are virtual policy, not measured silicon behavior.

The local workspace covers `[0x18b50000,0x18b70000)` with cold zero storage.
Actual guest stores clear the workspace. Control writes do not clear it.
Two Rocket-compatible BEU banks retain error masks and accrued state without fabricating cache errors or NMI events.
The guest publishes both recovery vectors. Their physical destination mapping remains unproven.

CPUCP mailbox banks declare 64 channels and one interrupt group.
The production configuration does not invent APSS or PDP peer routes.
A transmission to an absent peer logs a diagnostic and fails.
The hardware model never constructs SCMI replies or writes guest completion words.
Ten hardware mutexes reuse the existing arbitration model and share lock one with QSEE.
See [the subsystem evidence](../../../models/qcom/sm88xx/cpucp/subsystem_evidence.md).

## CPUCP PTE fuse identity

The existing fuse owner supplies corrected `PTE_ROW4_MSB` at TME `0x021c20ec` and APSS/CPUCP `0x221c20ec`.
The profile explicitly selects virtual zero. No physical die-coordinate meaning or fuse measurement is claimed.
Both CPUCP images use bit eight to gate configuration overrides and service paths.
The register remains read-only. Neighboring corrected rows remain unmapped.
The separate consolidated debug vector, authentication policy, and rollback values remain unchanged.
See [the fuse evidence](../../../models/qcom/sm88xx/fuse_controller/evidence.md#cpucp-manufacturing-and-debug-identity-group).

## CPUCP topology identity

The shared boot identity owner exposes CPU presence at `0x1765100c` and disabled LLC groups at `0x17651020`.
The virtual presence mask `0x033f` matches the existing six-plus-two APSS topology.
The disabled-group mask is zero. Both words are frozen and read-only.
Firmware applies its own descriptor masks and the SM8845 cluster-one limit.
These inputs do not claim measured hardware values or replace guest topology discovery.
See [the topology evidence](../../../models/qcom/sm88xx/apss_boot/evidence.md#cpucp-runtime-topology-words).

## CPUCP cluster configuration and secure notification

Both cluster configuration banks retain the guest's bounded table and control writes.
The QMX disable bit starts clear as explicit virtual policy, not a measured silicon value.
Native doubleword accesses and word halves share the same state.
These configuration writes do not release reset, create power acknowledgements, or complete firmware requests.
See [the cluster evidence](../../../models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#selected-runtime-cluster-configuration).

The selected QCCE callbacks also modify explicit power, fabric, sequence, and auxiliary policy words.
The existing owners retain those writes independently for each lane.
The model does not reproduce undocumented microarchitecture errata or electrical effects.
These words do not create completion or alter architectural reset.
See [the QCCE evidence](../../../models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#runtime-qcce-policy-cohort).

APSS `0x17960008` bit 28 sets a virtual pending latch for CPUCP source 117.
Both firmware packages register their secure SCMI callback on this source, separately from hardware mailbox source 24.
Pre-claim notifications coalesce. A notification during service remains pending until completion permits another claim.
The physical edge-versus-level behavior remains unmeasured.
Only genuine guest stores publish INIT, FREE, or response data.
See [the secure notification evidence](../../../models/qcom/sm88xx/cpucp/evidence.md#apss-secure-notification-and-cpucp-source-117).

## CPUCP runtime configuration qualification

The existing boot controller owns the recovered event, policy, and clock configuration words.
Ordinary configuration words retain all guest bits. Unknown addresses remain errors.
The virtual configuration-apply policy requires the declared lane fields, all configuration writes, and the existing live domain clock.
Setup bit 12 at `0x18877614` and `0x18877618` independently qualifies status bits 18 and 19 at `0x18877810`.
Cleared requests, removed enables, or source loss revoke qualification.
These are hardware configuration states, not guest protocol replies or authentication results.
The physical event meanings and timing remain unmeasured.
See [the transition evidence](../../../models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#runtime-configuration-apply-transition).

## ACEHR transfer policy

The ACEHR model declares 64 channels and 1024 descriptor slots per instance.
Queue publication retains bounds without executing a transfer.
A software start executes mode-zero copies through actual DMA bus transactions.
The functional transfer is synchronous. Completion requires every selected read and write to succeed.
The service interrupt follows the selected bus work and one additional virtual microsecond.
Each element contributes one read beat and one write beat.
The common platform selects a 19.2 MHz virtual bus.
This policy accounts for request length but does not establish measured hardware timing.
The DMA requester uses explicit secure, privileged attributes on the shared virtual bus.
Address-range flags do not select a separate translation or coherence domain in this profile.
Unsupported descriptor modes and bus failures remain errors.
The platform does not synthesize external snapshot events or firmware responses.
See [the ACEHR evidence](../../../models/qcom/sm88xx/cpucp/acehr_evidence.md).

## CPUCP secondary release

The virtual platform holds hart 1 through QEMU's native `start-powered-off` property.
The first subsystem commit at `0x18876104` requests release through the published second boot vector.
This release connection is virtual policy. The physical reset signal remains unmeasured.
The real secondary executes the OEM runtime and publishes its own acknowledgement.
The host does not write the release word, stack pointer, or acknowledgement.
See [the secondary evidence](../../../models/qcom/sm88xx/cpucp/evidence.md#release-policy-and-limits).

## CPUCP interrupt configuration words

The domain controller retains `0x188787a0` and `0x18878424` from a virtual zero state.
Both OEM initialization functions set fields in these words after interrupt setup.
The retained values have no interrupt or reset effect in this profile.
See [the register evidence](../../../models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#runtime-interrupt-configuration).

## CPUCP sequencer storage

The platform retains the instruction stores, sparse branch words, selectors, and configuration records written by the selected CPUCP initialization tables.
The guest supplies every instruction and record. The model does not embed firmware bytes.
Configuration storage starts at zero and rejects undeclared record indices and padding words.
The stores do not interpret internal programs or create completion interrupts.
Functional request owners apply the required clock and power effects separately.
See [the NCC request contract](../../../models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#functional-dvfs-requests).
See [the upload evidence](../../../models/qcom/sm88xx/cpucp/subsystem_evidence.md#sequencer-instruction-and-configuration-storage).

## CPUCP low-power telemetry

The telemetry bank at `0x18871000` retains enable controls but has no connected low-power transition source.
Its event counts and residence durations remain zero in the continuously awake virtual profile.
Enable writes do not create activity. The model rejects writes to counters and accesses to unknown offsets.
See [the telemetry evidence](../../../models/qcom/sm88xx/cpucp/subsystem_evidence.md#low-power-telemetry).

The CPUSS telemetry counters at `0x199e0000` and `0x199e1000`, plus the lane offset, follow the same policy.
The table banks retain the packed records, but the counters read zero, and counter writes fail.
The records describe low-power entry counts and residence times. CPUCP publishes them every 500 milliseconds.
No recovered boot path branches on these values.
See [the counter evidence](../../../models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#telemetry-counters).

## CPUCP core hang detectors

The two detectors at `0x18870000` retain their enable, threshold, clear, and override fields.
Their status words stay zero, because an emulated hart has no stall source that the detector can count.
A host stall of the simulator does not count as a hart stall.
See [the detector evidence](../../../models/qcom/sm88xx/cpucp/subsystem_evidence.md#core-hang-detectors).

## CPUCP level settling

The four level request controllers retain their configuration and accepted level.
Enabled requests complete synchronously instead of waiting for an analog regulator.
Disabled level requests and unsupported commands fail. Configuration writes do not change the level.
See [the command evidence](../../../models/qcom/sm88xx/cpucp/subsystem_evidence.md#level-request-controllers).

## CPUCP clock handover

The boot controller retains the handover selector and enable words at `0x18877678` and `0x18877680`.
The virtual disable operation has no delayed phase. Status bit zero at `0x18877694` remains clear.
The runtime root still requires a programmed level and a live clock source before it acknowledges a table request.

## CPU thermal environment

The CPU thermal sample banks report the declared virtual temperature of 25 degrees with validity bit 16.
The samples belong to the limits manager block at `0x181c0000` and `0x183c0000`.
The PDP initializer executes 143 configuration stores in the same block.
The model retains the declared configuration words from virtual cold zero and rejects undeclared offsets.
Separate interrupt enable words retain their values without pending events.
The CPUCP router and the APSS router reach the same owner, so the PDP and the CPUCP DMA share one block.
The guest's ACEHR transfers and thermal calculations remain active.
See [the source evidence](../../../models/qcom/sm88xx/cpucp/subsystem_evidence.md#cpu-thermal-snapshot-sources).

The limit status words at `0xb90` and `0x2890` start at zero.
Their validity bit 16 stays clear, so the driver reads no measurement.
The model supplies no current, leakage, or voltage value, so the limit paths cannot throttle a cluster.
This limit path needs no firmware poll. Every recovered limit path completes without a completion bit.

## CPUCP cluster watchdog configuration

The selected CPUCP images apply a cold watchdog configuration for each APSS cluster.
The profile retains only the exact 32-bit words that the original code accesses.
Each word starts at virtual cold zero and returns the latest guest value.
The substitute creates no timeout, bark, bite, interrupt, or reset.

A guarded Infiniti read from `0x17c30020` produced a synchronous external abort at APSS EL1.
The fault record recovered ESR `0x96000010` and FAR `0x17c30020`.
The observation provides no register value or reset state.
See [the cluster watchdog evidence](../../../models/qcom/sm88xx/cpucp/cluster_watchdog_evidence.md).

## CPU domain startup events

The cold startup sequence clears lane words at `0x1887c180` and `0x18878080`, with a four-byte lane stride.
The virtual profile supplies zero because no modeled producer raises these event bits.
It accepts zero writes and rejects nonzero writes.
The retained enable controls do not create an interrupt or reset event.
This substitute does not model the unnamed physical event sources.
See [the startup evidence](../../../models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#startup-clear-and-enable-sequence).

## Per-core debug configuration

The profile retains the two debug controls that CPUCP programs for each selected APSS core.
No external debugger or trigger producer connects to these controls.
The substitute creates no debug halt or reset event.
It does not model the complete CTI routing network or change debug authentication.
See [the debug evidence](../../../models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#per-core-debug-configuration).

## NCC ACD calibration result

The ACD banks at `0x199d0000` and `0x19aa2000`, plus `lane << 24`, retain their configuration registers.
A rising `UPDATEAUTOCAL` edge completes one calibration at the write.
The captured code is the configured `ACDDVMRC` delay code, because the model has no clock path to measure.
The status, counter, and debug registers keep their reset values.
A readout selection other than zero fails, because no source defines its data.
See [the ACD evidence](../../../models/qcom/sm88xx/pdp/acd_evidence.md#calibration).

## Core C-state encoding

`NCC_BOOT_STS1` reports zero for an executing core and four for a core without power.
The firmware comparisons bound these values, but no document states the encoding.
A core in WFI reports C2, C3, or C4 from its `NCC_PWR_CFG1` enables.
The model does not save or lose core state in C3, or in C4 while the core waits for an interrupt.
A start request for a core in C4 holds the core in reset and releases it from the warm vector.
See [the C-state evidence](../../../models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md#core-c-state-report).

## CPRH level limit word

The level request owner reads offset `+0x2608` of each CPRH target as virtual zero and rejects writes.
PDP `0x813233de` compares that word with the applied CPRH voltage before an ACD calibration.
No firmware image writes the word, and no source defines it.
With zero, the PDP uses the target status bits that the modeled closed-loop measurement sets.
See [the level request evidence](../../../models/qcom/sm88xx/cpucp/subsystem_evidence.md#level-request-controllers).

## QFPROM ROM error status

The virtual fuse profile supplies zero for `QFPROM_ROM_ERROR` at `0x221c2090`.
The model returns the complete declared word and logs each read.
The virtual fuse ROM has no recorded ROM read error. This status is not a device measurement.
The model does not assign a meaning to bits 17:16, which QSEE reads during Oplus storage initialization.
The symbolic register identity requires independent provenance. See the [TME review](provenance-tme.md).
See [the fuse evidence](../../../models/qcom/sm88xx/fuse_controller/evidence.md#qfprom-rom-error-status-for-uefi).

## QCE extended capability bitmap

The virtual QCE reports zero at offset `0xa140` and logs each read.
QSEE uses that word to reject a request when the engine does not advertise the requested capability.
The model advertises only its implemented SHA and AES CMAC capabilities at offset `0xa108`.
It does not return a successful cipher operation or a fabricated authentication result.
See [the GPCE evidence](../../../models/qcom/gpce/evidence.md#qsee-extended-crypto-capabilities).

## UEFI camera RSCC identification words

The TITAN camera RSCC model returns four declared power-on values.
The words are `0x00030800`, `0x00010100`, `0x03100214`, and `0x60004104` at offsets 0, 4, 8, and 0xc.
No SM8850 source gives these values.
UEFI RscDxe stores them and compares none of them.
The sequence memory, the branch table, the override address, and the hidden TCS owner keep the values that the firmware writes.
The model runs no sequencer, because this path writes no start request.
Busy, the program counter, the sequencer trigger, and the hidden TCS trigger fail.
See [the camera RSCC evidence](../../../models/qcom/rpmh/evidence.md#uefi-camera-rscc).

## UEFI PCIe RSCC words

The PCIe RSCC model returns four words from the generic RSCC HWIO power-on values.
The words are `0x00030800`, `0x00010100`, `0x01100214`, and `0x08000001` at offsets 0, 4, 8, and 0xc.
No SM8850 source gives these values.
UEFI RscDxe stores them and compares none of them.
The model retains the five-word sequence, the hidden-TCS data words, the PDC timer owner, and the override word.
The model runs no sequencer and no force-wake sequence.
The busy bit, the program counter, the timestamp units, and the trigger registers fail.
The three hidden-TCS data words configure the timer path only, because the firmware writes no trigger.
See [the PCIe RSCC evidence](../../../models/qcom/rpmh/evidence.md#uefi-pcie-rscc).

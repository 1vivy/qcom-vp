# TME crash-dump policy query

## Sources

The instruction evidence uses unmodified `IMAGES/xbl_sc.elf` from the selected packages.
Addresses are ELF virtual addresses. Static decoding used pyelftools and Capstone.

| Package | XBL SHA-256 |
| --- | --- |
| CPH2749 / SM8850 | `b5f4891e7e217d2ae105d1b881741bd0d20eac8a68b7c3551d444c3e178aef9a` |
| CPH2771 / SM8845 | `a2011f59d74956ce3536c924ce9995dd0222b23492d713741b9397453540adc4` |

The selected XBL consumers below distinguish three dump flags with values one, two, and four.
Their branches support the bounded disabled-dump policy.
The source-only API name `dbgil_get_crashdump_options` remains unestablished.
This review does not retain the source-only requirement about encryption or clearing sensitive areas.
No vendor bytes are stored here.

## First fault and its caller

The pre-model APSS trace first stopped at the crash-dump policy read.
It reported PC `0x1487ad84`, FAR `0x20c02000`, and ESR `0x96000010`.
The preceding executed call chain is:

1. Function `0x14858720` composes a policy word at `0x14a06dd8`.
2. It reads lifecycle at `0x148587c0` and the chip version through `0x1485f3c4`.
3. PC `0x14858810` queries subsystem five's invasive-debug enable.
4. The existing fuse vector supplies that query. The clear bit skips `0x14858818`.
5. PC `0x14858830` calls `0x1485e178`, the global crash-dump option query.
6. That query calls `0x1485e404`, which constructs the external control descriptor.
7. Getter `0x1487ad78` reads descriptor base plus `0x2000` and returns bits 2:0.
8. After return, `0x14858834..0x14858840` sets policy bit three only when dump options are nonzero.

The descriptor initializer `0x1487ad18` supplies base `0x20c00000`.
Resolver `0x1485e614` loads the resolved base from descriptor field `+0x18`.
The sole direct caller of `0x14858720` is `0x14856a84`.
That caller checks the returned status at `0x14856a88`.
The call composes configuration policy. It is not an observed exception entry.
The pre-model trace contained no earlier exception.
Therefore the observed call does not support classifying this read as a secondary crash-dump failure.
The later exception is separate: PC `0x1486f8a8`, FAR `0x0c900108`, ESR `0x96000250`.

Other consumers confirm the flags' meaning.
Function `0x148439bc` selects full, mini, or non-secure dump flags with priority 4, 1, 2.
Function `0x14843a08` rejects values at least five and tests the minidump flag.
Function `0x1485e30c` reads subsystem options only when global bit zero is set.
Function `0x1487711c` also branches on the returned dump flags.
Zero therefore denies dumps. It does not mean authentication succeeded, firmware is ready, or a boot stage completed.

## Bounded dependency group

The nearby getters use different descriptor classes. Their physical addresses must not share a guessed MMIO bank.

| Operation | SM8850 getter | SM8845 getter | Resolved address / owner |
| --- | --- | --- | --- |
| Global dump options | `0x1487ad78` | `0x148794bc` | `0x20c02000`, new disabled-dump policy |
| Consolidated debug vector, index 0–3 | `0x1487ad40` | `0x14879484` | `0x221c8780 + index*4`, existing fuse controller |
| Subsystem options, frontend index 0–15 | `0x1487ad94` | `0x148794d8` | `0x01fd4000 + index*4`, scratch publication contract |
| Serial binding query | `0x1487af24` | Same accessor family | `0x221c8604` bit five, existing secure-boot fuse word |
| Additional debug query | `0x1487af40` | Same accessor family | `0x221c8780` bit 27, existing debug vector zero |
| OEM lifecycle query | `0x1487af74` | Same accessor family | `0x221c8cf0`, existing OEM policy word |

SM8845 independently uses wrapper `0x1485c6ac`, frontend `0x1485c938`, resolver `0x1485cb48`, and initializer `0x1487945c`.
Its faulting-equivalent load is `0x148794c8`. Its initializer also supplies `0x20c00000`.
The same model needs no SoC branch.

The selected debug predicates request only vector zero or one.
Both already have one authoritative owner in `FuseController`.
The generic vector getter bounds indices at three, but this does not justify inventing vectors two and three.

Subsystem option frontend `0x1485e44c` selects descriptor class two, whose initializer supplies `0x01f40000`.
Its getter adds `0x94000`, not `0x94000` to the external control base.
Function `0x1485e1c0` first requires options word three to contain tag byte `0x81`.
It validates the start and count against sixteen before reading further words.
This path follows the global minidump flag check at `0x1485e31c..0x1485e32c`.
The disabled policy prevents entry into this publication parser.
The existing canary occupies `0x01fd4038`, inside this generic sixteen-word address range.
Do not create another RAM bank or zero table there.
A future positive dump policy needs an authenticated publication contract and one shared scratch owner.
No such producer contract is established by this bounded investigation.

The source-only name `TME_BOOT_STAGE_VECTOR` remains unestablished.
The selected XBL consumer establishes the address and low-three-bit query.
Do not derive readiness transitions from an unestablished register name.

## Explicit virtual policy

`tme_crashdump_policy.h` implements `qcom,tme-crashdump-policy-v1`.
The required CCI policy string is `disabled`. Missing or other values reject elaboration.
Construction establishes one epoch without any authorized dump policy.
An aligned four-byte read at local offset zero returns zero.
Reads do not change state. All writes fail, including writes of zero.
Unknown offsets, byte enables, invalid buffers, unsupported widths, short streaming widths, and unsupported commands fail.
The model grants no DMI.
There is no reset, policy-install, debug-grant, or positive-value interface.
Thus neither a CPU reset nor a guest write can enable a dump.

This policy disables all three dump classes. It does not fabricate a successful image check.
It does not change fuse values, authentication, rollback, AVB, lock state, or mailbox responses.
It neither acknowledges exceptions nor clears primary failure records.
A later crash must remain visible in the exception trace and retained UART log.
A successful read alone proves neither boot progress beyond the caller nor kernel execution.

## Platform integration

`src/platform.cc` registers the header-only model as `qcom,tme-crashdump-policy-v1`.
The shared reset factory invokes `platforms/qcom/sm88xx/tme-crashdump-policy.lua` after it creates the protected external router.
Both selected SoCs use that wiring.

The helper maps exactly four bytes at local TME `0x00c02000`.
The APSS `0x20c02000` view translates into `tme_external_router`, not directly into the model.
This preserves the existing external protection route.
It does not replace software-state banks, handoff flags, fuse backing, or scratch backing.

The canonical SM8850 run passed the global dump-policy query.
Its next unsupported read was APSS `0x01fd0000` at PC `0x14858648`.
The complete integration suite passed 58 tests.
The run did not reach the kernel.

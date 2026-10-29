# Shared executable memory

`qcom,shared-executable-memory-v1` uses one backing region and one native execution owner.
The owner is either an APSS execution group or an independent CPU.
All group members share one QEMU instance and can obtain writable DMI.
The optional `execution_participant` parameter names CPUs in other native instances.
The parameter holds a space-separated list. Each CPU must belong to a different instance.
The optional `execution_sibling` parameter names one CPU in the owner's native instance.
These CPUs can also obtain writable DMI. Other initiators cannot obtain DMI.
The sibling shares the native RAM identity and needs no duplicate native write.
This restriction does not prohibit another CPU from executing through the transaction path.

External writes use SystemC transactions against the same bytes.
The model writes through each participating instance's native RAM interface before it completes the transaction.
Each instance DMI manager gives these writes and its CPU aliases the same RAM identity.
QEMU copies the bytes and invalidates translated code synchronously in each instance.
No asynchronous publication window permits a consumer to execute stale code after it observes the producer's completion flag.
No CPU callback or exclusive rendezvous blocks a firmware-driven DMA write.

## Platform evidence

| Region | Evidence |
| --- | --- |
| TME RAM at `0x00400000`, length `0x5c000` | TME ROM descriptors at `0x213000` and `0x213008` |
| APSS alias at `0x20400000` | The selected TME ELF segments span `0x2041c000` through `0x2044a000` |
| APSS SRAM at `0x14800000`, length `0x280000` | APBL sets its stack at base plus `0x3000`. Descriptors at `0x31ed98` and `0x31edd8` cover initialization through `0x14a7fc00`. |
| CPUCP log at `0x14a7fc00`, length `0x400` | CPUCP `0x335518` clears the final SRAM range through `0x14a80000`. The alias uses offset `0x27fc00` in the APSS backing region. |

The writer uses a cached native address space for each backing span.
It does not require CPU0 to map a page that only a sibling executes.
Separate secure memory maps remain unsupported.

Permission-limited CPU grants can narrow a region inside QBox's DMI manager.
Each native write restores the full backing span before access.
This restoration does not change the bounds of any CPU alias or bypass the upstream permission checks.

## Independent DCP execution

Set `execution_participant='dcp'` on both `ddr_low` and `ddr_low_backing` after the platform creates the DCP CPU.
The parameter accepts an absolute object path or a path relative to the owner's parent.
The participant must belong to a different QEMU instance.
An empty parameter preserves the existing owner-only policy, including the TME and CPUCP transaction paths.

The backing grants full DMI, not read-only DMI.
The existing DCP DTIM translator bounds the reachable bytes.
`DareMemoryPort::dmi` still applies the DARE permission bounds to every grant.
Its execution views include the participant when DARE revokes code and upstream DMI.
Each instance receives its own safe code-invalidation job before the final callback revokes DMI.

All valid transaction writes use every native RAM identity when a participant exists.
This includes owner writes that reach `b_transport` and writes from foreign initiators.
Each instance needs a native RAM write because QEMU does not share translated-code state between instances.
The second native write repeats the byte copy to invalidate that instance's translated code synchronously.
Neither write changes a CPU alias's permission bounds.

Direct DMI stores do not pass through SystemC.
QBox gives different QEMU instances distinct RAM identities, even when they use the same host bytes.
No existing mechanism propagates translated-code invalidation between those identities for direct DMI stores.
Thus, concurrent cross-instance code changes through DMI remain unsupported without an explicit invalidation boundary.
The DCP reset controller must invalidate DCP translated code after reset assertion completes and before each reset release.
This boundary covers APSS publication of DCP code before release.
It does not cover DCP modification of running APSS code or APSS modification of running DCP code.
The DCP participant receives no additional route to APSS memory.
The existing DTIM range and DARE policy still restrict its memory access.

The `participant-dmi` fixture uses two independent RV32 instances and one foreign ARM writer.
Both RV32 instances execute shared code, acknowledge execution with stores, and execute the foreign replacement without `fence.i`.
The second instance's trace must contain a shared-RAM translation block with both instructions.
This requirement rejects transaction-only instruction fetches. The acknowledgement rejects read-only DMI.
The control run removes both participant parameters and checks that transaction-only fetches lack this translation block.
The focused regression passed with both execution modes.

## Independent PDP execution

The SM8850 PDP composition appends `pdp0 pdp1` to the same two parameters.
The DDR owners then have three participants: the DCP and both PDP CPUs.
Each participant instance receives its own native write and its own code-invalidation job.
An M-profile QEMU device is an `armv7m` container, not the CPU object.
`PdpCpu` implements `NativeCpuContainer`, so the views use the architectural CPU.
The PDP reset controller invalidates all PDP translated code at each reset assertion.
`models/qcom/sm88xx/pdp/evidence.md` records the run573 failure that required direct PDP fetches.

## Execution regression

The ARM producer installs a RISC-V function through an alias.
The RISC-V consumer executes that function and signals completion.
The producer replaces its first instruction and publishes another flag.
The consumer executes the replacement without `fence.i` and checks the new return value.

The original `gs_memory` composition executed stale code and reached the failure branch.
The new model reached the success branch with the same guest programs.
The test uses an unmapped address as its terminal observation, not as a kernel-success marker.
The sources reside in `tools/qcom/qcom-vp/tests/fixtures/coherence`.

The APSS group fixture isolates a mapping that only CPU7 creates.
An external instance writes code that returns eleven, then replaces it with code that returns twenty-two.
CPU7 executes both versions without guest cache maintenance.
CPU0 observes completion without accessing the code page.

The fixture also leaves six CPUs in cold reset through shutdown.
The APSS wrapper must stop and join those native threads without releasing architectural reset.
CPU0 also checks all eight redistributor affinities and requires the Last bit only on the final frame.

The regression reached its success marker and exited with status two.
These synthetic guests do not establish OEM secondary startup.

```sh
make build
cargo test --locked --test platform -- --ignored
```

## CPUCP DDR and local SRAM diagnostic

The earlier minimal diagnostic established physical-DDR instruction replacement by an APSS guest.
CPUCP returned `1`, observed a foreign rewrite, then returned `2` without `fence.i`.
The retained logs are under `build/private/cpucp-ddr-probe/before-sm8850/` and `before-sm8845/`.
Those observations remain evidence of the exercised shared-DDR execution path.

The later diagnostic also asserted equality between local SRAM and ELF physical zero-fill ranges.
That contract was wrong.
SM8850 run273 poisoned its live return address because the modeled local alias overlapped a real DDR snapshot buffer.
The alias-equality and bidirectional local-alias claims are withdrawn.
The old complete logs remain under `build/private/cpucp-ddr-probe/complete/`.
Their passing assertions do not validate local SRAM ownership.
This correction does not invalidate the independent physical-DDR instruction or DMA-coherence observations.

The fixture remains in `tools/qcom/qcom-vp/tests/fixtures/cpucp-ddr`.
It uses both production DDR helpers, one independent local SRAM owner, and synthetic guest instructions.
Its APSS guest drives UFS and authentication DMA routers through fixture-only windows.
These windows do not emulate DMA engines or authenticate an image.

| Firmware layout | Executable DDR address | IRAM snapshot | Local SRAM snapshot |
|---|---|---|---|
| SM8850 | `0x812d0000` | `0x81240000` | `0x81280000` |
| SM8845 | `0x81240000` | `0x822ffe00` | `0x8233fe00` |

Each snapshot is `0x40000` bytes.
The live sources are IRAM at `0x1d800000` and local SRAM at `0x18b30000`.
The ELF zero-file physical destinations remain ordinary shared DDR.
They do not alias local SRAM.

The replacement fixture checks:

- Cold local SRAM and each physical zero-fill extent independently.
- Physical DDR writes and DMA zero-fill without changing local SRAM.
- Full DDR-buffer poison while a real return address resides on the local stack.
- Execution of APSS-published IRAM instructions before and after poison.
- DDR return values `1`, `2`, `3`, and `4` after APSS, UFS-router, and authentication-router writes.
- Both complete snapshots copied by the CPUCP guest and read through all three DDR views.
- Retention of live SRAM, initialized image bytes, and snapshots across an architectural CPU reset.
- Local changes that leave snapshots unchanged, followed by guest restoration from those snapshots.

The existing boot controller supplies the reset request.
No host callback copies memory, repairs a stack, or changes firmware state.
The synthetic guest's success sentinel remains `0x600d0000`.
Its failure sentinels are outside the mapped DDR windows.

Run the focused regression after the platform build:

```sh
cargo test --locked --test platform cpucp_preserves_local_sram_across_ddr_backup_and_reset -- --ignored --exact
```

The existing Rust harness compiles both synthetic platform layouts.
The revised fixture passes for both platform layouts after the local-memory correction.
The fixture does not establish an OEM suspend/resume result or native CPUCP service completion.

## Direct TME image consumers

The TME CPU does not use the sequencer's authentication DMA router for every image access.
It now shares the existing APSS IMEM, SOCCP debug SRAM, CPUCP image SRAM, SHRM SRAM, and DDR backings.
The aliases preserve each owner's bytes without copying them.
They do not replace the existing TME protection path.

SM8845 run52 failed a direct byte read at `0x31047e00`.
SM8850 run59 and SM8845 run54 pass the shared-memory path and complete SHRM image loading.
This observation proves the exercised SHRM consumer, not every future remote-image consumer.

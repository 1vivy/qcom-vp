# Shared composition provenance review

The source inventory is `artifacts:2026-10-28-source-only-facts/source-only-docs.md sha256=5a0a78ad3f71a30052c18e51f7b8868ed5aa9cc2468552d2551cee538f0edfba`.
The line numbers below refer to that inventory, not current repository line numbers.
Every listed shared claim has a review destination below.
A review destination identifies the evidence question. It is not independent support for the claim.
Companion reviews: [processors and clocks](provenance-processors.md), [peripherals](provenance-peripherals.md), [TME and boot](provenance-tme.md).

## Disposition

The earlier citation removal preserved facts whose recorded basis was sparse.
Prior evidence pages, accumulated research, and device probes supplied the substantive basis.
This review does not certify those facts or change their modeled behavior.
A destination below names a concrete evidence or interpretation question, not a citation verdict.
Shared documentation and platform constants inherit the unresolved status of their underlying contract.
A repeated statement in another document does not supply independent evidence.
A recorded manufacturer or family catalog pattern can support an implementation with a stated basis and limit.

| Inventory lines | Shared claim or dependency | Required evidence and disposition |
| --- | --- | --- |
| 7, 41 | SYSTEM_IMEM full extent | Selected XBL subregions in `docs/qcom/sm88xx/architecture.md` do not establish the full physical extent. The `0x2c000` aperture remains unresolved. |
| 8 | Sequencer dictionary entry | The [TME review](provenance-tme.md) must distinguish an observed register value from a dictionary name. |
| 9 | UFS WriteBooster flags | Selected driver transactions can establish the requests. Retain no claim of a universal initialization policy without that scope. See the [peripheral review](provenance-peripherals.md). |
| 10–14, 27, 53–55, 62 | PMIC type, subtype, BOB slots, errors, and OCP behavior | Hardware records and selected firmware support numeric values and bounded transactions. Names and general driver conditions need independent support. See the peripheral review. |
| 15 | PRNG interrupt-enable name | An observed address and write do not establish the symbolic name or full interrupt behavior. See the TME review. |
| 16 | Watchdog override fields | Field names, masks, access types, and effects need separate support. See the TME review. |
| 17 | PMIC configuration before PS_HOLD | The [processor review](provenance-processors.md) addresses the selected firmware sequence. The claim must remain specific to that path. |
| 18, 42 | APSS hardware-version reset word | A useful declared response does not establish a physical reset value. See the TME review for the remaining identity dependency. |
| 19 | Fuse BIST controls | The TME review covers access offsets and behavior. Selected accesses alone do not establish masks or completion semantics. |
| 20 | QBEC profile, events, and key purpose | The TME review must separate firmware observations from inherited event names and policy interpretations. |
| 21 | Key-manager reset words | The TME review covers the shared crypto window and reset responses. These remain blocked where only inherited definitions support them. |
| 22, 50–52 | SPSS mailbox and image-hash words | The TME review covers related firmware interfaces. Physical names, access permissions, and reset words need independent support. |
| 23 | TME image-vector status | The selected TME read supports the accessed word. The TME review determines which status semantics the record establishes. |
| 24 | TCSR ownership and violation words | The TME review covers access observations and authorization behavior. Do not infer complete permission semantics from a successful boot. |
| 25 | XPU error controls | The TME review covers the masks and report behavior. Keep any unsupported fields as explicit open questions. |
| 26 | Debugger power request | Selected XBL reads support the consumer. Bit meanings and security effects require independent evidence in the TME review. |
| 28, 40 | PDP ACD and per-core DPM banks | The processor review covers selected accesses, table contents, and unsupported catalog-derived geometry. |
| 29, 38, 44 | NSI and CPUCP control-plane geometry | The processor review separates observed addresses from inherited register names, masks, and reset words. |
| 30, 45 | AMU banks | The processor review covers architectural behavior and target-specific placement. Generic architectural support does not establish the complete target layout. |
| 31 | Boot memory-access policy | The processor review covers the selected firmware transaction and unresolved field interpretation. |
| 32 | SPMI helper identity | The selected function address and masks support a bounded operation. Its source-level function name needs independent support in the peripheral review. |
| 33, 43 | AOSS reset configuration and state machine | The TME review covers implemented transitions and the remaining register-definition dependencies. |
| 34 | Camera RSCC power-on words | The peripheral review identifies unresolved returned constants. Do not relabel them as virtual policy to close the question. |
| 39 | Four MCCC slave banks | Selected accesses can establish used banks. Complete hardware extents remain unresolved in the peripheral review. |
| 46 | MPAM frame placement | The processor review covers architectural registers and the unresolved target placement and reset values. |
| 47 | Complete TCSR aperture | Observed accesses do not establish a complete one-MiB hardware aperture. The TME review retains that geometry question. |
| 48 | CPRF block and extent | The processor review covers selected UEFI accesses and unsupported reset or layout constants. |
| 49 | GPUCC base derivation | Selected firmware addresses can establish placement. The processor review covers the scope of that evidence. |
| 56 | OEM authorization profile and gate mask | The TME review checks permitted security-profile evidence and firmware behavior separately. |
| 57–59 | PCIe and GPU clock words | The processor review covers public clock definitions and selected firmware accesses. Reset words need separate evidence. |
| 60 | RPM vote bit wiring | The processor review separates public branch fields from inherited vote-bank assignments. |
| 61 | PBS information and OTP reads | Selected firmware reads establish consumed locations. Physical meaning requires the independent evidence identified in the peripheral review. |

## Review status

These rows list open evidence and interpretation questions.
Citation removal alone is not a prohibition for other work, and it is not proof of wrong behavior.
The full SYSTEM_IMEM extent remains a shared open question in addition to the component-review findings.
A later correction must keep these open findings visible rather than replace them with a clean-source claim.
This review includes no nonredistributable source or binary material.

The corrected [boot evidence](oplus/bringup.md#status) now identifies retained records by file hash.
Those records establish their historical observations, not the provenance of unrelated register definitions.
The environment interface can progress independently while these model-specific questions remain open.

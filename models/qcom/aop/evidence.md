# AOP QMP endpoint

This file records the selected AOP QMP protocol and the virtual endpoint in `models/qcom/aop/qmp.h`.
The AOP firmware does not execute in this platform.
The model publishes the descriptor and answers the handshake of the selected GlinkDxe transport.
All protocol values below come from the selected binary, the selected package source, or the Linux driver.

## Selected image

The source package is `CPH2749export_11_16.0.5.700EX01_2026022701130186.zip`.
Its `IMAGES/uefi.elf` SHA-256 is `da8ad1b0ffc2eff91d7d34d8574a682b7007e0b052439fbbcde989d5393071f5`.
The recorded `GlinkDxe` PE SHA-256 is `acbc4086e7d95f0ed938e91a1ec3c8f14e369243ed15cf29a337ecbdfff7725a`.
`models/qcom/soccp/evidence.md` records both hashes.

The firmware volume is an LZMA FORMAT_ALONE stream at file offset `0x1040e0`.
It expands to `0x3c3008` bytes.
The `GlinkDxe` PE header sits at decompressed offset `0x17de6c`.
Its `.text` section maps file offset `0x17ee6c` to RVA `0x1000` and spans `0xe000` bytes.
The `.data` section starts at RVA `0xf000` and the `.reloc` section at RVA `0x10000`.

Two trace addresses fix the load base at `0xbd48f000`:

1. The run752 fault PC `0xbd49712c` maps to RVA `0x812c`.
2. The trace base `0xbd497868` maps to RVA `0x8868`, which maps to decompressed offset `0x1866d4`.

The fault is the 32-bit read of `0x0c300000` at RVA `0x812c`.
That read is `ch_desc->magic` in `xport_qmp_check_link`.

## Transport configuration

`xport_qmp_devcfg_remote_procs` sits at RVA `0xdb18`.
The strings are `aop_apps` at RVA `0xda7b` and `rpm` at RVA `0xcfbf`.

| Field | Offset | Value |
|---|---|---|
| Channel name | `+0` | `aop_apps` |
| Remote subsystem name | `+8` | `rpm` |
| Remote host identifier | `+0x10` | `0xffff`, invalid |
| Mailbox area size | `+0x14` | `0x400` |
| Master mailbox size | `+0x18` | `0` |
| Maximum outstanding messages | `+0x1c` | `3` |
| IPCC client | `+0x20` | `0`, AOP |
| IPCC signal | `+0x24` | `0` |
| Master flag | `+0x28` | `0`, the APSS is the slave |
| Mailbox descriptor address | `+0x30` | `0x0c300000` |

## Descriptor layout

Linux `drivers/soc/qcom/qcom_aoss.c:21-42` defines the same offsets with its own names.

| Offset | AOP field | Writer |
|---|---|---|
| `0x00` | Magic `0x4d41494c` | AOP |
| `0x04` | Core version `1` | AOP |
| `0x08` | Core features `0` | AOP |
| `0x0c` | AOP link state | AOP |
| `0x10` | AOP link state acknowledgement | APSS |
| `0x14` | AOP channel state | AOP |
| `0x18` | AOP channel state acknowledgement | APSS |
| `0x1c` | AOP mailbox size | AOP |
| `0x20` | AOP mailbox offset | AOP |
| `0x24` | APSS link state | APSS |
| `0x28` | APSS link state acknowledgement | AOP |
| `0x2c` | APSS channel state | APSS |
| `0x30` | APSS channel state acknowledgement | AOP |
| `0x34` | APSS mailbox size | AOP |
| `0x38` | APSS mailbox offset | AOP |

State `0x0000ffff` means up or connected.
State `0xffff0000` means down or disconnected.
The disassembly at RVA `0x8124-0x8130` and `0x7a48-0x7a5c` supplies these constants.

Each mailbox starts with a 32-bit length and then the message bytes.
The transport reads the length at RVA `0x8598` of the interrupt handler and writes it at RVA `0x8060` of the transmit path.

## Negotiation sequence

The APSS runs `xport_qmp_init` and then `xport_qmp_check_link` (RVA `0x8100-0x82a0`).
The AOP answers each doorbell in the model.

1. The APSS reads `0x0c300000` at RVA `0x812c` and compares the word with `0x4d41494c` at RVA `0x8130`.
2. The APSS derives its mailbox from `+0x34` and `+0x38` at RVA `0x8144-0x8190`.
3. The APSS acks the AOP state into `+0x10` and `+0x18` and sets its own state: link up at `0x24`, channel down at `0x2c`, length zero at `0x40`. See RVA `0x7a44-0x7a84`.
4. The APSS rings the doorbell in `check_link` at RVA `0x828c`, after the version callback returns.
5. The AOP writes its link acknowledgement at `+0x28` and its channel acknowledgement at `+0x30`, then rings the doorbell.
6. The APSS reaches `LINK_UP` and calls `rx_cmd_version_ack` with version `1` and features `0` (RVA `0x8440-0x8484`).
7. The APSS opens `aop_apps`: it sets channel state up at `0x2c`, state three, and rings the doorbell at RVA `0x7bd4` (RVA `0x7ba0-0x7bd4`).
8. The AOP acks at `+0x30` and opens its own channel at `+0x14`.
9. The APSS reaches `E2ECONNECTED` (RVA `0x8514-0x8518`) and transmits.

The AOP must also clear a received message length.
`xport_qmp_tx_cmd_local_rx_done` writes zero to the remote mailbox length and rings the doorbell (RVA `0x7ec8-0x7ef0`).
Only then does the APSS report transmit completion at RVA `0x8584-0x85dc`.

## Interrupt route

The transport uses one IPCC pair: client `0` and signal `0`.

`IPCC_C_AOP` is at index zero of `IPCCClientMproc`.
The same table lists `IPCC_C_APSS_NS0` at index six.
`platforms/qcom/sm88xx/sm8850/soccp.lua` uses local index six for the APSS bank at `0x01106000`.
That bank carries the MPROC protocol interrupt on `apss_gic.spi_in_246`.

The model consumes the doorbell on client zero through `QcomIpccLocal`.
It raises the APSS pending bit for client zero and signal zero.
The APSS bank interrupt then reaches the GIC, so the transport interrupt handler runs.

## Messages during UEFI

Both clients transmit one fixed buffer and wait for transmit completion.
They do not inspect the reply content.

| Message text | Bytes | Caller | Required effect |
| --- | --- | --- | --- |
| `{class: clock, res: qdss, val: <state>}` and `qdss_ao` | `64` | QDSS client | AOP clock vote |
| `{class: ddr, event: pasr, addr_hi: 0x<32-bit>, addr_lo: 0x<32-bit>, refresh: off}` | `100` | PASR client | AOP DDR refresh change |

The clock vote buffer is 64 bytes. The DDR refresh buffer is 100 bytes.
Both calls pass the whole buffer, not the string length.
The bytes after the terminator stay undefined, so the model reads only the bytes before the first zero byte.

The acknowledgement is the cleared mailbox length.
It reports that the peer consumed the message.
It does not prove that the clock or DDR effect occurred.

## Remote requirement and timeout

The link comes up only after the AOP publishes the magic and acknowledges the link state.
Without the AOP, `check_link` never calls `link_up`, and the version handshake never starts.
The clients then stop on their own bounds.

Each client loop runs 200 iterations of `Stall(10)`.
UEFI `Stall` takes microseconds, so the bound is about 2 ms.
Link, channel open, and transmit use separate loops.
A transmit timeout returns `EFI_DEVICE_ERROR`.
The QDSS client then returns its previous state, and the PASR client logs the failure and continues.
UEFI therefore continues without the remote, but every QMP request fails.

## Virtual values

The AOP firmware is absent, so these values are virtual.

| Value | Declared | Reason |
|---|---|---|
| APSS mailbox | Offset `0x40`, size `0x180` | The descriptor leaves the geometry to the AOP. Both mailboxes fit in the `0x400` area. |
| AOP mailbox | Offset `0x200`, size `0x180` | The model writes no messages. The region must exist for the APSS receive intent. |
| AOP link state | Up after reset | The AOP runs before the APSS reads the descriptor. Linux acks this state unconditionally. |
| AOP channel state | Follows the APSS channel | The model opens `aop_apps` with the client. No independent AOP service exists. |

## Message effects

The model accepts only the two messages above.
It records a clock vote and logs it.
It records a PASR request and logs it.
No clock changes and no refresh changes, because the platform has no AOP clock controller and no DDR refresh model.
`docs/qcom/sm88xx/stubs.md` records this policy.

A message with an unknown class, an unknown resource, a length above the mailbox, or no terminator stays in the mailbox.
The model logs one `AOP_QMP_REJECT` line and rings no doorbell.
The APSS transmit then times out, and the client reports an error.
The model never clears the length of a message that it did not apply.

## Known interaction

The AOP message RAM also holds the command database header at `0x0c3f0000`.
`docs/qcom/sm88xx/decisions.md` records that the PDP reads `header[0] + 0x0c300000` and waits for cookie `0xa0c00c1e`.
Before the resource service publishes the header, that expression reads `0x0c300000`.
The word now returns the QMP magic, not a fault.
The bounded PDP loop then returns zero, as before.
The CPUCP consumer has no documented bound, so its first run after this change needs a check.

## Limits

The selected binary establishes the descriptor, the state words, the config table, and the interrupt pair.
The selected AOP firmware is not available, so the mailbox geometry and the AOP-side ordering are virtual.
The model does not prove that the AOP firmware uses the same mailbox offsets as this model.
A later AOP image or a hardware capture can replace these values.

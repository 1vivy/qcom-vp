# Crypto BAM evidence

## Status

QSEE initializes the crypto BAM after the GPU GMU writes.
SM8850 run227 and SM8845 run183 stop on a read of `0x01dc5000`.
The SM8850 PC is `0x15721df0`, and the SM8845 PC is `0x15716628`.
This read is `BAM_REVISION` in the BAM v2.0 layout.

## Identity

The [upstream Kaanapali DT](https://github.com/torvalds/linux/blob/9339897ad7eded77adf834e032482f7609911448/arch/arm64/boot/dts/qcom/kaanapali.dtsi#L2627-L2638) declares `cryptobam`.
The node uses `qcom,bam-v2.0.0` at `0x01dc4000+0x22000`, `num-channels = <20>` and `qcom,num-ees = <4>`.
The node also declares `qcom,controlled-remotely`.

The selected SM8850 and SM8845 vendor_boot DTBs declare `qcom,qcedev`.
Its `crypto-bam-base` is the same `0x01dc4000+0x22000` window.

The model window is `0x01dc4000+0x20000`. The 20 pipe pages end at that boundary.
XBL boot policy already owns the AC policy control word at `0x01de4000`.
AC table resource `0x84` identifies that owner. Thus the model does not claim the last `0x2000` bytes.

The [upstream BAM driver](https://github.com/torvalds/linux/blob/a50184171235045d96104811414b25cf5338cf86/drivers/dma/qcom/bam_dma.c#L202-L229) gives the v2.0 layout.
`BAM_REVISION` is at `+0x1000` and `BAM_NUM_PIPES` is at `+0x1008`.
The pipe registers start at `+0xc000` with a `0x1000` stride.
The OEM [SPS BAM driver](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/soc/qcom/sps/bam.c) uses the same pipe base for `CONFIG_QCE_OPTIMIZED_BAM`.
That driver accepts revision values from 2 to `0x2f`.

## QSEE consumer

QSEE reads property `bam_tgt_config` below `/core/hwengines/bam` from `RADIO/tz_qti_config.img`.
The record is at file offset `0x78f8` for SM8850 and `0x6818` for SM8845.
Both records select base `0x01dc4000` and execution environment 2.
Their pipe tables have the same values.

| Pipe mask | Execution environment |
| --- | --- |
| `0x03fc` | 3 |
| `0xfc03` | 2 |

The table below lists the SM8850 functions. The SM8845 revision reader at `0x15716624` has the same instructions.

| Function | Register effect |
| --- | --- |
| `0x15721dec` | Reads `BAM_REVISION`, then `BAM_CTRL.BAM_EN` or `PIPE_ATTR_EE` bit 31 |
| `0x15721adc` | Pulses `BAM_SW_RST`, sets `BAM_EN`, and writes `DESC_CNT_TRSHLD`, `CNFG_BITS`, `IRQ_SRCS_MSK_EE`, `IRQ_CLR` and `IRQ_EN` |
| `0x15721b90` | Writes the execution environment field of `P_TRUST` at `+0x2020 + pipe * 4` |
| `0x15721bc0` | Writes `BAM_TRUST` at `+0x2000` |
| `0x15721cf8` | Reads `BAM_REVISION` and `BAM_NUM_PIPES` fields |

The pipe masks configure pipes 0 to 15. The driver loop does not compare a pipe with `BAM_NUM_PIPES`.

## Model contract

`qcom,bam-v2.0.0` implements the global, trust and execution environment registers.
It also keeps the pipe control and FIFO configuration words for each configured pipe.
`BAM_NUM_PIPES` reports 20, and `BAM_REVISION` reports four execution environments.
The revision field is the virtual value `0x20`. No selected source gives the silicon value.

`PIPE_ATTR_EE` bit 31 follows `BAM_EN`. Its pipe bits follow the `P_TRUST` execution environment field.
`BAM_SW_RST` and `P_RST` return the state to the virtual reset values.
`P_RST` keeps `P_TRUST`, because the trust register is outside the pipe page.

An access to an offset without a listed register returns an address error, which the access monitor logs.

## Interface

The model exposes these ports.

| Port | Kind | Contract |
| --- | --- | --- |
| `target_socket` | target | BAM register window at `0x01dc4000+0x20000` |
| `dma_socket` | optional initiator | Ring reads, payload reads and RAM data writes |
| `command_socket` | optional initiator | Opcode 4 register writes only. Physical path, no translation |
| `stream_socket` | optional initiator | Pipe data. The payload address is the pipe index |
| `stream_ready` | target signal | Endpoint capacity. A false-to-true change retries backpressured pipes |
| `irq` | optional initiators, `irq_0` to `irq_7` | One level output per execution environment |

The parameters are `num_pipes`, `num_ees`, `revision`, `address_bits` and `command_base`.
The platform binds `dma_socket` to the DMA path that reaches RAM and binds `command_socket`
to the physical register path. It binds `stream_socket` to the QCE stream target and binds
`stream_ready` to the QCE stream-ready output. A command write never uses `dma_socket`, and
no path uses the other socket. An unbound `command_socket` latches a pipe error when
a command element needs a register write, and that descriptor never completes.

`BamStreamExtension` carries `transferred` and `end_of_transfer`. A write moves RAM bytes to
the endpoint. A read moves endpoint bytes to RAM. A zero `transferred` value is backpressure.
Only the final chunk of an `EOT` descriptor sets `end_of_transfer`.

## Descriptor engine

A pipe consumes the 8-byte descriptors of the ring that `P_DESC_FIFO_ADDR` and the low half
of `P_FIFO_SIZES` describe. The Linux
[`bam_desc_hw`](https://github.com/torvalds/linux/blob/master/drivers/dma/qcom/bam_dma.c#L49-L53)
names the three fields: an address, a size and a flag word.
The QSEE descriptor writer at `0x157bfcfc` packs word 1 as
`size16 | address_high8 << 16 | flags8 << 24`, and word 0 holds address bits 31:0.

`bam_chan_init_hw` writes the descriptor base and size, then the channel control word
([bam_dma.c](https://github.com/torvalds/linux/blob/master/drivers/dma/qcom/bam_dma.c#L528-L558)).
It sets `P_DIRECTION` bit 3 only for `DMA_DEV_TO_MEM`. A set bit therefore means that the
endpoint is the source and that data moves to RAM.
A clear bit means that the pipe takes data from RAM and sends it to the endpoint.
The OEM SPS driver writes the same field from its connection direction
([bam.c](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/soc/qcom/sps/bam.c#L1379-L1380)).

Software publishes descriptors with the `P_EVNT_REG` doorbell. Linux writes the producer
byte offset, not a descriptor index
([bam_dma.c](https://github.com/torvalds/linux/blob/master/drivers/dma/qcom/bam_dma.c#L1115-L1116)).
`P_SW_OFSTS` reports the byte offset that the engine processes
([bam_dma.c](https://github.com/torvalds/linux/blob/master/drivers/dma/qcom/bam_dma.c#L368-L369)).
The model keeps both offsets, advances the consumer offset by 8 bytes per accepted descriptor
and wraps it to zero at the ring size.
An accepted descriptor keeps its address, length, direction and flag byte for its whole
transfer, so a register write during that transfer cannot change them.

The QSEE writer encodes a large transfer in 64 KiB units. At `0x157bfdc8` it compares the
length with `0x10000`. At `0x157bfdd0` it tests the low 16 bits, at `0x157bfdf0` it copies the
low half into the size field, and at `0x157bfdfc` it copies the high half instead. At
`0x157bfe04` it sets bit 24, which is bit 0 of the flag byte. The model therefore treats a set
flag bit 0 as "the size field counts 64 KiB units" and multiplies the size field by `0x10000`.

The other flag-byte bits keep the SPS flag positions inside that byte
([msm-sps.h](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/include/linux/msm-sps.h#L75-L85)):
`INT` `0x80`, `EOT` `0x40`, `EOB` `0x20`, `NWD` `0x10`, `CMD` `0x08`, `LOCK` `0x04` and
`UNLOCK` `0x02`.
Linux declares the same descriptor bits for `INT`, `EOT`, `EOB`, `NWD` and `CMD`
([bam_dma.c](https://github.com/torvalds/linux/blob/master/drivers/dma/qcom/bam_dma.c#L57-L61)).
The model supports 32-bit and 36-bit DMA addresses.
The 36-bit profile reports `BAM_NUM_PIPES[15:14] = 1`.
The OEM driver identifies this field as `BAM_DATA_ADDR_BUS_WIDTH` and reports 36 bits for a nonzero value.
QSEE checks this field at `0x15721ac0` before it accepts a buffer above 4 GiB.
Run 389 rejected such a buffer at `0x157bfdc4` because the model reported zero.
The current `tz_qti_config` IORT establishes the width of this DMA path.
The file `14f5e` has SHA-256 `46f6b44e0474bb0adf87a8085a87e896ab97d065637259b1f683ee519172bfb6`.
Its CRYPTO node is at `0x15496` with type 1, and byte `+0x1c` reports 36 address bits.
Mapping 0 at `0x154d3` holds the words `81010000`, `00000001`, `000000c2`, `00000030` and `00000000`.
The mapping therefore carries SID `c2` and output reference `30`.
Output reference `30` names the APPS SMMU at `0x15000000`.
The model reports 36-bit addressing because the secure SID `c2` path has this width, not because
of a virtual platform preference.
An address outside the configured width causes a pipe error.

The high address parts are `P_DESC_FIFO_ADDR_MSB` at `+0x914`, `P_DATA_FIFO_ADDR_MSB` at
`+0x924` and `P_EVNT_DEST_ADDR_MSB` at `+0x934`, as the OEM
[SPS register table](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/soc/qcom/sps/bam.c#L672-L684)
lists them for `CONFIG_QCE_OPTIMIZED_BAM`.

A `CMD` descriptor keeps the transfer layout, and its address points at a payload in memory.
QSEE builds an array of 8-byte command elements at `0x157219d4`.
The first word holds the address in bits 23:0 and the opcode in bits 31:24.
The second word holds the 32-bit value.
The element address is relative to the crypto register top, which the model reads from the
`command_base` parameter. Opcode 4 writes the value word to `command_base + address` as one
4-byte register write. The model fails the pipe on any other opcode, and it never treats an
unknown opcode as a no-op. A payload length that is not a multiple of eight bytes also fails.
The descriptor size selects the payload length, so the same 64 KiB unit rule applies.

## Transfer and completion policy

A descriptor moves data only through real bus transactions. The model reads the ring and the
command payload through `dma_socket`. It writes opcode 4 register targets through
`command_socket`, which is a separate physical path. The captured attributes travel unchanged
on both paths, so a register write cannot borrow a different security identity from the RAM
path or the reverse. Data crosses the pipe through
`stream_socket`, whose payload address is the pipe index and whose
`BamStreamExtension.transferred` reports the accepted byte count.

A zero byte count is backpressure. The model then keeps the accepted descriptor, keeps the
consumer offset and retries on the next `stream_ready` false-to-true change. Each retry also
repeats the remaining bytes after a partial acceptance, so no byte is lost or sent twice.
A zero-progress attempt stops the trigger, so no retry spins. One trigger walks at most the
whole programmed ring once, and each step moves at most 4 KiB.
A retry delta continues a longer ring or a larger descriptor.
The programmed ring size and the fixed scratch buffer bound the work without an extra platform setting.

A descriptor completes only after its data transactions or its command elements succeed.
Completion then advances the consumer offset and sets the pipe status:
`P_IRQ_STTS` bit 0 for `INT` and bit 5 for `EOT`. Linux and SPS define the same bits
([sps_bam.h](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/soc/qcom/sps/bam.c#L180-L191)).
`P_IRQ_CLR` is write-one-to-clear. `BAM_IRQ_STTS` and its `BAM_IRQ_CLR` follow the same rule
([bam.c](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/soc/qcom/sps/bam.c#L84-L95)).

`BAM_IRQ_SRCS` reports enabled pending pipe status in bits 0 to 30.
Bit 31 reports the enabled global BAM source, not a pipe summary
([bam_dma.c](https://github.com/torvalds/linux/blob/master/drivers/dma/qcom/bam_dma.c#L855-L866)).
`BAM_IRQ_EN` gates the global status bits, and `IRQ_SRCS_MSK_EE` masks both the pipe bits and
bit 31 for each EE. `IRQ_SRCS_EE` and the per-EE interrupt outputs `irq_0` to `irq_7` report
that masked result, so an EE sees the global source only when it unmasks bit 31. An unbound
output is not driven.

`P_PRDCR_SDBND` and `P_CNSMR_SDBND` hold peripheral FIFO byte counts in bits 15:0, which stay
zero because the model has no data FIFO.
Bit 17 reports real stream backpressure.
The model sets it when the endpoint returns zero bytes and clears it when the endpoint accepts bytes.
Bit 20
toggles on each completed end-of-transfer descriptor, so it follows a real peripheral packet
and not a doorbell or a ring count. Bit 24 is event state, and a read does not clear it.
QSEE reads the consumer toggle at `0x157220c8` during pipe initialization and reset
([bam.c](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8850/blob/fc30e54174d254ff7f33622a9278e4435f6718d2/drivers/soc/qcom/sps/bam.c#L221-L235)).
`P_SW_OFSTS` remains the primary completion offset, and `INT` and `EOT` set the pipe status.
Equal producer and consumer offsets mean an empty ring, so a ring keeps one descriptor slot
free.

A DMA transfer, a command register write and a stream request carry the attributes of the access that programmed the
pipe. The model copies that attribute structure unchanged, attaches it only when the
initiating access carried one, and never adds, removes or tightens a security field. A
nonsecure submission therefore cannot borrow an earlier secure identity. An accepted
descriptor keeps its own copy, so a later write cannot change the identity of a transfer that
is already in flight.

A malformed ring or an unbound DMA, stream or command socket latches `P_IRQ_STTS` bit 4 and `BAM_IRQ_STTS` bit 2.
An unsupported opcode or a command payload with an invalid length latches the same error bits.
A rejected bus transaction also latches `P_IRQ_STTS` bit 7 and
`BAM_IRQ_STTS` bit 1. Either case prints one `BAM_ERROR` line and never reports completion.

`BAM_SW_RST` and `P_RST` cancel accepted work.
A bus or command callback can cause such a reset.
The engine keeps a generation for the BAM and for each pipe.
It tests both generations after each bus or stream callback.
Cancelled work does not complete, advance the consumer offset, or execute the remaining command elements.
It does not mark the new pipe state as failed.

A global reset also cancels a pending retry.
A pipe reset keeps the shared retry because another pipe can use that event.
The generation test still cancels the old work of the reset pipe.
The reset pipe reads as empty.

## Fixture transport proof

The `qce-bam` native fixture now binds `dma_socket` to a real `ArmSmmuStreamPort` for secure
SID `c2`. The stream port shares the `ArmSmmuV2` owner, which uses SMR127, context 111 and the
captured `TCR2` value `0x40038001`. The fixture builds level 1, level 2 and level 3 tables in
physical RAM. It maps IOVA `0x782740000` and the following 32 pages to a separate physical
RAM array at a different address. A descriptor, payload or digest address that remains
untranslated cannot reach that array.
The fixture binds `command_socket` to the physical register router. Opcode 4 writes reach the
QCE `GOPROC` register and the BAM control register without translation.
A second BAM instance leaves `command_socket` unbound and checks that its command list fails
before it reaches a register.
The nonsecure submission matches an NS-owned SMR entry that selects secure context 111.
The stream group ownership rule faults before any physical access.
The changed fixture passed with the real BAM, QCE and SMMU models.
It prints one `BAM_ERROR` for the unbound-command case and one `BAM_DMA_ERROR` plus one
`BAM_ERROR` for the rejected nonsecure descriptor. These three lines show expected failures.
The fixture exercises the existing SHA-256 `abc`, SHA-1 continuation and interleaving,
pre-GO backpressure, self-reset and nonsecure rejection cases.

## Native boundary

The retained SM8850 run 393 system log records a BAM payload at IOVA `0x27c400670` with length
424. That access fails because the BAM bypasses SMMU translation for this buffer.
QSEE calls `156b38b8`, which calls `15729c00` and deliberately returns a virtual address and
synchronizes the SMMU.
The ring IOVA `0x82740000` in the same capture coincides with physical RAM, so it does not
prove that the bypass is correct.
The native DMA path remains unverified after run 393.
The model and the fixture do not claim a native boot or a completed QSEE handoff.

## Model limits

The model does not implement pipe lock arbitration, the inactivity timers, the data FIFO, or
descriptor processing inside a system-mode peripheral. `LOCK`, `UNLOCK`, `EOB` and `NWD` are
accepted and carry no local register effect. A zero-length descriptor issues no stream access,
so it cannot carry an end-of-transfer signal.

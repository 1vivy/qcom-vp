// SPDX-License-Identifier: BSD-3-Clause-Clear
// Test payload only. This fixture is not a firmware or kernel boot log.
.section .text
.globl _start
_start:
    mov x20, #0x00a90000
    add x20, x20, #0xc000
    mov x21, #0x17000000
    mov x22, #0x00ac0000
    // Explicit virtual capability registers.
    ldr w5, [x22, #4]
    mov w6, #0x40030000
    cmp w5, w6
    b.ne failed
    ldr w5, [x20, #0xe24]
    mov w6, #0x20100000
    cmp w5, w6
    b.ne failed
    // Firmware/configuration memory is guest-owned and initially empty.
    ldr w5, [x20, #0x1000]
    cbnz w5, failed
    add x23, x20, #0x1000
    ldp x5, x6, [x23, #0x10]
    orr x5, x5, x6
    cbnz x5, failed
    mov x5, #0x5678
    movk x5, #0x1234, lsl #16
    movk x5, #0xdef0, lsl #32
    movk x5, #0x9abc, lsl #48
    mov x6, #0xbeef
    stp x5, x6, [x23, #0x10]
    ldp x7, x8, [x23, #0x10]
    cmp x5, x7
    b.ne failed
    cmp x6, x8
    b.ne failed
    mov w4, #0x200
    str w4, [x20, #0x1000]
    str w4, [x20, #0x1004]
    mov w4, #1
    str w4, [x20, #0x2000]
    mov w4, #8
    str w4, [x20, #0x268]
    mov w4, #0x380e
    movk w4, #4, lsl #16
    str w4, [x20, #0x260]
    mov w4, #0x3e0e
    movk w4, #0xc, lsl #16
    str w4, [x20, #0x264]
    // Linux clears DMA status before FIFO output. This must not create completion.
    mov w4, #-1
    str w4, [x20, #0xc44]
    str w4, [x20, #0xd44]
    ldr w5, [x20, #0xc40]
    cbnz w5, failed
    ldr w5, [x20, #0xd40]
    cbnz w5, failed
    // Start TX, then cancel without generating any output bytes.
    mov w4, #13
    str w4, [x20, #0x270]
    mov w4, #0x08000000
    str w4, [x20, #0x600]
    mov w4, #4
    str w4, [x20, #0x604]
    // The cancel request bit reads back clear after the command completes.
    ldr w5, [x20, #0x604]
    cbnz w5, failed
    ldr w5, [x20, #0x40]
    cbnz w5, failed
    ldr w5, [x20, #0x610]
    cmp w5, #0x10
    b.ne failed
    str w5, [x20, #0x618]
    // Cancellation before FIFO writes must not report transmitted bytes.
    ldr w5, [x20, #0x910]
    cbnz w5, failed
    // Abort on an active TX command latches CMD_ABORT_EN and clears active.
    mov w4, #13
    str w4, [x20, #0x270]
    mov w4, #0x08000000
    str w4, [x20, #0x600]
    ldr w5, [x20, #0x40]
    cmp w5, #1
    b.ne failed
    mov w4, #2
    str w4, [x20, #0x604]
    ldr w5, [x20, #0x604]
    cbnz w5, failed
    ldr w5, [x20, #0x40]
    cbnz w5, failed
    ldr w5, [x20, #0x610]
    cmp w5, #0x20
    b.ne failed
    str w5, [x20, #0x618]
    ldr w5, [x20, #0x610]
    cbnz w5, failed
    // Abort must not emit the bytes of the aborted command.
    ldr w5, [x20, #0x910]
    cbnz w5, failed
    // Earlycon setup aborts RX before any command starts. Abort still completes.
    mov w4, #2
    str w4, [x20, #0x634]
    ldr w5, [x20, #0x634]
    cbnz w5, failed
    ldr w5, [x20, #0x640]
    cmp w5, #0x20
    b.ne failed
    mov w4, #0x21 // Linux clears S_CMD_DONE_EN with S_CMD_ABORT_EN.
    str w4, [x20, #0x648]
    ldr w5, [x20, #0x640]
    cbnz w5, failed
    // RX abort latches its IRQ, but cannot manufacture input.
    mov w4, #0x08000000
    str w4, [x20, #0x630]
    // The kernel preserves unrelated command bits before it starts RX.
    ldr w5, [x20, #0x630]
    cmp w5, w4
    b.ne failed
    ldr w5, [x20, #0x40]
    cmp w5, #0x1000
    b.ne failed
    mov w4, #2
    str w4, [x20, #0x634]
    // Linux polls S_GENI_CMD_ABORT until the request bit clears.
    ldr w5, [x20, #0x634]
    cbnz w5, failed
    ldr w5, [x20, #0x640]
    cmp w5, #0x20
    b.ne failed
    mov w4, #0x21
    str w4, [x20, #0x648]
    ldr w5, [x20, #0x40]
    cbnz w5, failed
    ldr w5, [x20, #0x640]
    cbnz w5, failed
    // Cancel on an active RX command latches CMD_CANCEL_EN.
    mov w4, #0x08000000
    str w4, [x20, #0x630]
    ldr w5, [x20, #0x40]
    cmp w5, #0x1000
    b.ne failed
    mov w4, #4
    str w4, [x20, #0x634]
    ldr w5, [x20, #0x634]
    cbnz w5, failed
    ldr w5, [x20, #0x640]
    cmp w5, #0x10
    b.ne failed
    str w5, [x20, #0x648]
    ldr w5, [x20, #0x40]
    cbnz w5, failed
    // Abort does not remove the guest's RAM contents.
    ldr x7, [x23, #0x10]
    mov x6, #0x5678
    movk x6, #0x1234, lsl #16
    movk x6, #0xdef0, lsl #32
    movk x6, #0x9abc, lsl #48
    cmp x7, x6
    b.ne failed
    // Secure Group 0 SPI835 has architectural INTID867.
    msr spsel, #1
    adr x4, vectors
    msr vbar_el3, x4
    mov x4, #0xf
    msr ICC_SRE_EL3, x4
    isb
    mrs x4, scr_el3
    orr x4, x4, #4
    msr scr_el3, x4
    isb
    mov x4, #0xff
    msr ICC_PMR_EL1, x4
    mov x4, #1
    msr ICC_IGRPEN0_EL1, x4
    mov w4, #0x11
    str w4, [x21]
    mov w4, #8 // INTID867: ISENABLER27, bit3.
    str w4, [x21, #0x16c]
    mov w12, #0
    mov w4, #4
    str w4, [x20, #0xe1c]
    mov w4, #1
    str w4, [x20, #0x61c]
    ldr w5, [x20, #0x614]
    cmp w5, #1
    b.ne failed
    dsb sy
    isb
    msr daifclr, #1
    mov w4, #0x08000000
    str w4, [x20, #0x600]
    // Emit 'UART'. Nine bytes remain, so command completion must stay clear.
    mov w4, #0x4155
    movk w4, #0x5452, lsl #16
    str w4, [x20, #0x700]
    ldr w5, [x20, #0x910]
    cmp w5, #4
    b.ne failed
    ldr w5, [x20, #0x610]
    cbnz w5, failed
    ldr w5, [x20, #0x40]
    cmp w5, #1
    b.ne failed
    cbnz w12, failed
    // Empty FIFO watermark is a level source, not W1C storage.
    mov w4, #1
    str w4, [x20, #0x80c]
    ldr w5, [x20, #0x610]
    mov w6, #0x40000000
    cmp w5, w6
    b.ne failed
    str w6, [x20, #0x618]
    ldr w5, [x20, #0x610]
    cmp w5, w6
    b.ne failed
    str wzr, [x20, #0x80c]
    // Emit ' TES', then 'T:', NUL, ff.
    mov w4, #0x5420
    movk w4, #0x5345, lsl #16
    str w4, [x20, #0x704]
    mov w4, #0x3a54
    movk w4, #0xff00, lsl #16
    str w4, [x20, #0x77c]
    ldr w5, [x20, #0x610]
    cbnz w5, failed
    cbnz w12, failed
    // Only the low newline byte belongs to this command. Discard high bytes.
    mov w4, #0x580a
    movk w4, #0x5858, lsl #16
    str w4, [x20, #0x700]
1:  cmp w12, #1
    b.ne 1b
    ldr w5, [x20, #0x40]
    cbnz w5, failed
    ldr w5, [x20, #0x610]
    cbnz w5, failed
    // The progress counter excludes padding in the final FIFO word.
    ldr w5, [x20, #0x910]
    cmp w5, #13
    b.ne failed
    // Mask aliases must modify the same enable register.
    mov w4, #1
    str w4, [x20, #0x620]
    ldr w5, [x20, #0x614]
    cbnz w5, failed
    // A zero-byte command completes, but emits no additional bytes.
    str wzr, [x20, #0x270]
    mov w4, #0x08000000
    str w4, [x20, #0x600]
    ldr w5, [x20, #0x910]
    cbnz w5, failed
    ldr w5, [x20, #0x610]
    cmp w5, #1
    b.ne failed
    cmp w12, #1
    b.ne failed
    mov w4, #1
    str w4, [x20, #0x61c]
2:  cmp w12, #2
    b.ne 2b
    // The handler clears its source before EOI. No duplicate IRQ may arrive.
    mov w4, #1000
3:  subs w4, w4, #1
    b.ne 3b
    cmp w12, #2
    b.ne failed
    mov x0, #0x600d0000
    str w12, [x0]
4:  b 4b
failed:
    mov x0, #0xbad00000
    str w5, [x0]
5:  b 5b

.balign 2048
vectors:
    b failed
    .balign 128
    b failed
    .balign 128
    b failed
    .balign 128
    b failed
    .balign 128
    b failed
    .balign 128
    b failed
    .balign 128
fiq:
    mrs x14, ICC_IAR0_EL1
    cmp x14, #867
    b.ne failed
    ldr w15, [x20, #0x610]
    cmp w15, #1
    b.ne failed
    str w15, [x20, #0x618]
    dsb sy
    msr ICC_EOIR0_EL1, x14
    isb
    add w12, w12, #1
    eret
    .balign 128
    b failed

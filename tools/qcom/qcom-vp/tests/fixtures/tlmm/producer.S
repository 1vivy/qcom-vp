// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x0, #0x0f100000
#ifdef TLMM_HOLE
    add x0, x0, #217, lsl #12
    ldr w2, [x0, #0x10]
#elif defined(TLMM_PAD_IO)
    add x0, x0, #226, lsl #12
    ldr w2, [x0, #4]
#elif defined(TLMM_WRITE_PRESENCE)
    str wzr, [x0, #0x10]
#elif defined(TLMM_RESERVED)
    ldr w2, [x0, #0x1c]
#else
    // Discover the last ordinary GPIO without extending into the geometry hole.
    add x3, x0, #216, lsl #12
    ldr w2, [x3, #0x10]
    cmp w2, #1
    b.ne failed
    // An output latch does not invent an external input when direction is input.
    mov w1, #2
    str w1, [x0, #4]
    ldr w2, [x0, #4]
    cmp w2, #2
    b.ne failed
    // GPIO output has coherent local readback.
    mov w1, #0x200
    str w1, [x0]
    ldr w2, [x0, #4]
    cmp w2, #3
    b.ne failed
    // A peripheral mux disconnects GPIO readback but retains the output latch.
    mov w1, #0x204
    str w1, [x0]
    ldr w2, [x0, #4]
    cmp w2, #2
    b.ne failed
    // Neither a pull nor a forged eGPIO-present bit supplies an external input.
    mov w1, #0x803
    str w1, [x0]
    ldr w2, [x0]
    cmp w2, #3
    b.ne failed
    ldr w2, [x0, #4]
    cmp w2, #2
    b.ne failed
    // Sleep configuration is retained, not activated by writing its enable bit.
    mov w1, #0xe00
    str w1, [x0, #0x14]
    ldr w2, [x0, #4]
    cmp w2, #2
    b.ne failed
    // Interrupt requests and output transitions never generate status.
    mov w1, #0x1f
    str w1, [x0, #8]
    mov w1, #0x200
    str w1, [x0]
    str wzr, [x0, #4]
    ldr w2, [x0, #0xc]
    cbnz w2, failed
    mov w1, #1
    str w1, [x0, #0xc]
    ldr w2, [x0, #0xc]
    cbnz w2, failed
    // Native LPM interrupt setup clears independent retained control bits.
    mov w1, #3
    str w1, [x0, #0x18]
    str wzr, [x0, #0x18]
    ldr w2, [x0, #0x18]
    cbnz w2, failed
    mov x0, #0x600d0000
    str w2, [x0]
#endif
failed:
    mov x0, #0xbad00000
    str w2, [x0]
1:  b 1b

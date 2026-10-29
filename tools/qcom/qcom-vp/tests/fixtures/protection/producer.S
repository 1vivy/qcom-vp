#ifndef RAM_BASE
#define RAM_BASE 0x14800000
#endif
.section .text
.globl _start
_start:
    ldr x2, =RAM_BASE
    mov w3, #0x1234
    str w3, [x2]
    ldr w4, [x2]
    cmp w3, w4
    b.ne failure
    mov x0, #0x05f00000
    ldr w5, [x0, #8]
    cmp w5, #11
    b.ne failure
    ldr w5, [x0, #0xc]
    mov w6, #0x40000000
    cmp w5, w6
    b.ne failure
#ifdef WIDE_ADDRESS
    ldr w7, [x0, #4]
    ubfx w7, w7, #8, #6
    mov x8, #-1
    mov x9, #63
    sub x7, x9, x7
    lsrv x8, x8, x7
#endif
    ldr w5, [x0]
    ubfx w5, w5, #16, #10
    add x0, x0, #0x1000
    add x0, x0, x5, lsl #6
#ifdef WIDE_ADDRESS
    ldr x1, =RAM_BASE
    and x1, x1, x8
    str w1, [x0, #0x0c]
    lsr x1, x1, #32
    str w1, [x0, #8]
    ldr x1, =RAM_BASE+0x10000
    and x1, x1, x8
    lsr x7, x1, #32
    str w7, [x0, #0x10]
#else
    mov w1, #0
    str w1, [x0, #0x0c]
    mov w1, #0x10000
#endif
    str w1, [x0, #0x14]
    mov w1, #0x480
    str w1, [x0, #0x18]
    ldr w1, [x0, #0x18]
    cmp w1, #0x400
    b.ne failure
    mov w1, #0x80000000
    str w1, [x0, #0x18]
    str wzr, [x0, #0x1c]
    mov w1, #1
    str w1, [x0, #4]
    str w1, [x0]
    // Policy-control programming must not unlock or authorize the protected region.
    mov x10, #0x01de0000
    add x10, x10, #0x4000
    mov w11, #-1
    str w11, [x10]
    ldr w12, [x10]
    cmp w11, w12
    b.ne failure
    mov w1, #0x80000000
    str w1, [x0, #0x1c]
    ldr w1, [x0, #0x1c]
    cbnz w1, failure
    ldr x6, =RAM_BASE-4
    str w3, [x6]
    ldr w4, [x6]
    cmp w3, w4
    b.ne failure
    ldr x6, =RAM_BASE+0x10000
    str w3, [x6]
    ldr w4, [x6]
    cmp w3, w4
    b.ne failure
    ldr w4, [x2]
    cmp w3, w4
    b.ne failure
    str w1, [x2]
failure:
    mov x0, #0xbad00000
    str w1, [x0]
1:  b 1b

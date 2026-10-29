// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x0, #0x14800000
    // The APSS view must use the same frozen identity owner as CPUCP.
    movz x9, #0x1000
    movk x9, #0x1765, lsl #16
    ldr w10, [x9, #0x0c]
    cmp w10, #0x33f
    b.ne failure
    ldr w10, [x9, #0x20]
    cmp w10, #0x21
    b.ne failure
    ldr w10, [x9, #0x18]
    mov w11, #0x10000
    cmp w10, w11
    b.ne failure
    ldr w10, [x9, #0x24]
    mov w11, #0x20000
    cmp w10, w11
    b.ne failure
1:  ldr w1, [x0]
    cbz w1, 1b
    cmp w1, #1
    b.ne failure
    mov x8, #0x18860000
    mov w9, #2
    str w9, [x8]
    str w9, [x8, #4]
    ldr w10, [x8]
    cmp w10, #1
    b.ne failure
    ldr w10, [x8, #4]
    cmp w10, #1
    b.ne failure
    ldr w10, [x8, #0x24]
    cbnz w10, failure
    mov x2, #0x17620000
    mov x3, #1
    str x3, [x2, #0x4000]
    mov x4, #0x4c00
    str x3, [x2, x4]
    mov x5, #0x18830000
#ifdef ABSENT_PEER
    mov w6, #0x1234
    str w6, [x5, #0x100]
    str w6, [x5, #0x104]
    b failure
#endif
    // Channel 63 exercises the high MAP/status bit and unrelated W1C retention.
    mov x6, #0x5678
    movk x6, #0x1234, lsl #16
    str x6, [x5, #0x2f8]
    mov w6, #0x7788
    movk w6, #0x5566, lsl #16
    str w6, [x5, #0x100]
    mov w7, #0x10000
2:  ldr w1, [x0]
    cmp w1, #1
    b.ne failure
    subs w7, w7, #1
    b.ne 2b
    mov w6, #0x3344
    movk w6, #0x1122, lsl #16
    str w6, [x5, #0x104]
3:  ldr w1, [x0]
    cmp w1, #1
    b.eq 3b
    cmp w1, #2
    b.ne failure
    mov w9, #2
    str w9, [x8]
    str w9, [x8, #4]
    ldr w10, [x8]
    cmp w10, #2
    b.ne failure
    ldr w10, [x8, #4]
    cmp w10, #2
    b.ne failure
    str wzr, [x8]
    str wzr, [x8, #4]
    mov x4, #0x4400
4:  ldr x3, [x2, x4]
    cbz x3, 4b
    cmp x3, #1
    b.ne failure
    ldr x3, [x2, #0x100]
    mov x6, #0x3210
    movk x6, #0x7654, lsl #16
    movk x6, #0xba98, lsl #32
    movk x6, #0xfedc, lsl #48
    cmp x3, x6
    b.ne failure
    mov x3, #1
    mov x4, #0x4800
    str x3, [x2, x4]
    mov x4, #0x4400
    ldr x3, [x2, x4]
    cbnz x3, failure
    mov x0, #0x600d0000
    str w1, [x0]
failure:
    mov x0, #0xbad00000
    str w1, [x0]
5:  b 5b

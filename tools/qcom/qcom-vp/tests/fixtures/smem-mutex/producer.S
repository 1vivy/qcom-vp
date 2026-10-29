// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x0, #0x3000
    movk x0, #0x1f4, lsl #16
    ldr w2, [x0]
    cbnz w2, failed
    // TME claims the free mutex. APSS cannot replace its owner.
    mov w1, #15
    str w1, [x0]
    mov w1, #1
    str w1, [x0]
    ldr w2, [x0]
    cmp w2, #15
    b.ne failed
    // Release permits APSS to acquire the same mutex.
    str wzr, [x0]
    ldr w2, [x0]
    cbnz w2, failed
    str w1, [x0]
    ldr w2, [x0]
    cmp w2, #1
    b.ne failed
    str wzr, [x0]
    // A repeated release leaves the mutex free.
    str wzr, [x0]
    ldr w2, [x0]
    cbnz w2, failed
    mov x0, #0x600d0000
    str w2, [x0]
failed:
    mov x0, #0xbad00000
    str w2, [x0]
1:  b 1b

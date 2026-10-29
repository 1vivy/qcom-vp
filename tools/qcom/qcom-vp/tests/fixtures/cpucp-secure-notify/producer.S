// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x0, #0x14800000
1:  ldr w1, [x0]
    cmp w1, #1
    b.ne 1b
    // Guest-owned request and response words. The transport must not change them.
    mov w1, #0x1357
    str w1, [x0, #0x40]
    str wzr, [x0, #0x44]
    movz x2, #8
    movk x2, #0x1796, lsl #16
    mov w3, #0x10000000
    dsb sy
    // Two rings before claim must produce only one pending notification.
    str w3, [x2]
    str w3, [x2]
    str wzr, [x2]
    ldr w1, [x0, #0x40]
    mov w4, #0x1357
    cmp w1, w4
    b.ne failure
    ldr w1, [x0, #0x44]
    cbnz w1, failure
    mov w1, #2
    str w1, [x0]
2:  ldr w1, [x0]
    cmp w1, #3
    b.ne 2b
    // A ring after claim must remain pending through the first completion.
    str w3, [x2]
    dsb sy
    mov w1, #4
    str w1, [x0]
3:  ldr w1, [x0]
    cmp w1, #6
    b.ne 3b
    ldr w1, [x0, #0x40]
    mov w4, #0x2468
    cmp w1, w4
    b.ne failure
    ldr w1, [x0, #0x44]
    cmp w1, #1
    b.ne failure
    mov x0, #0x600d0000
    str w1, [x0]
failure:
    mov x0, #0xbad00000
    str w1, [x0]
4:  b 4b

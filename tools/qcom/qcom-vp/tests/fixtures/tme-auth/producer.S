// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    // Eight independent sequencers occupy consecutive 4 KiB windows.
    mov x1, #0x6000
    movk x1, #0x2223, lsl #16
    mov w3, #0
next_case:
    // A complete TPBL ring must not become authentication success.
    ldr w2, [x1, #0x4c]
    cbnz w2, failed
    ldr w2, [x1, #0x2c]
    cmp w2, #5
    b.ne failed
    mov w2, #1
    str w2, [x1]
    dsb sy
    // The interface version remains unchanged after the start request.
    ldr w2, [x1, #0x4c]
    cbnz w2, failed
    // Only case zero has a valid, approved authentication chain.
    mov w4, #1
    cbnz w3, check_result
    mov w4, #3
check_result:
    ldr w2, [x1, #0x18]
    cmp w2, w4
    b.ne failed
    ldr w2, [x1, #0x2c]
    cmp w2, w4
    b.ne failed
    // Runtime publication must preserve authentication and require both acknowledgements.
    mov x5, #0x24020000
    add x5, x5, x3, lsl #12
    mov x6, #0x24030000
    str wzr, [x6]
    mov w2, #0x4d54
    movk w2, #0x5746, lsl #16
    str w2, [x5]
    dsb sy
    ldr w2, [x1, #0x2c]
    cmp w2, w4
    b.ne failed
    mov w2, #2
    str w2, [x6]
    dsb sy
    ldr w2, [x1, #0x2c]
    cmp w2, w4
    b.ne failed
    mov w2, #6
    str w2, [x6]
    cbnz w3, check_runtime
    mov w4, #4
check_runtime:
    dsb sy
    ldr w2, [x1, #0x2c]
    cmp w2, w4
    b.ne failed
    // An inconsistent runtime ring must revoke runtime readiness.
    mov w2, #3
    str w2, [x5, #8]
    cbnz w3, check_invalid_ring
    mov w4, #3
check_invalid_ring:
    dsb sy
    ldr w2, [x1, #0x2c]
    cmp w2, w4
    b.ne failed
    add x1, x1, #0x1000
    add w3, w3, #1
    cmp w3, #8
    b.ne next_case
    mov x0, #0x600d0000
    str w2, [x0]
1:  b 1b
failed:
    mov x0, #0xbad00000
    str w3, [x0]
2:  b 2b

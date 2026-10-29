// SPDX-License-Identifier: BSD-3-Clause-Clear
.macro idle
    ldr w4, [x20]
    cmp w4, #1
    b.ne failed
.endm
.macro retain
    str w3, [x19]
    ldr w4, [x19]
    cmp w4, w3
    b.ne failed
    idle
.endm
.section .text
.globl _start
_start:
    mov x19, #0x100000
    add x20, x19, #0x1000

    // The cold startup must reach IDLE before the guest runs.
    // Exact equality also rejects FIRST_PASS and other status flags.
    idle

    // Debug-reset configuration must not change the cold FSM status or control.
    add x21, x19, #4
    ldr w4, [x21]
    cbnz w4, failed
    mov w3, #0x70
    str w3, [x21]
    ldr w4, [x21]
    cmp w4, w3
    b.ne failed
    ldr w4, [x19]
    cbnz w4, failed
    idle

    // Retain control bits without replacing IDLE with a control-derived state.
    mov w3, #0
    retain
    mov w3, #1
    retain
    mov w3, #0x5aa5
    movk w3, #0xa55a, lsl #16
    retain
    mov w3, #-1
    retain
    mov w3, #0
    retain
    ldr w4, [x21]
    cmp w4, #0x70
    b.ne failed

#if CASE == 1
    // Software cannot write status, even when it retains the current state.
    mov w3, #1
    str w3, [x20]
#endif
    mov x0, #0x600d0000
    ldr w0, [x0]
    b .
failed:
    mov x0, #0xdead0000
    ldr w0, [x0]
    b .

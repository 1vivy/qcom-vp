// SPDX-License-Identifier: BSD-3-Clause-Clear
// Failure address is 0xdead0000 + x28 * 4. Stages: 1=CPU identity,
// 2=code11, 3=code22, 4=GICR affinity, 5=GICR Last, 6=fault controls.
.section .text
.globl _start
_start:
    ldr x19, =0x14800000
    mrs x0, mpidr_el1
    and x0, x0, #0xffffff
    cbz x0, boot
    mov x28, #1
    ldr x1, =0x10100
    cmp x0, x1
    b.ne fail
    ldr x20, =0x80000000
    mov x28, #2
1:  ldr w0, [x19]
    cmp w0, #1
    b.ne 1b
    dmb sy
    blr x20
    cmp w0, #11
    b.ne fail
    mov w0, #1
    dmb sy
    str w0, [x19, #4]
    mov x28, #3
2:  ldr w0, [x19]
    cmp w0, #2
    b.ne 2b
    dmb sy
    // No guest cache-maintenance instruction hides missing native invalidation.
    blr x20
    cmp w0, #22
    b.ne fail
    mov w0, #2
    dmb sy
    str w0, [x19, #8]
3:  wfi
    b 3b
boot:
    // A 32-bit update preserves the other half of a 64-bit fault control.
    mov x28, #6
    ldr x20, =0x17020048
    mov x0, #0x108
    movk x0, #0x1234, lsl #32
    str x0, [x20]
    mov w1, #0x5678
    str w1, [x20, #4]
    ldr x2, [x20]
    movk x0, #0x5678, lsl #32
    cmp x2, x0
    b.ne fail
    mov x1, #0x109
    str x1, [x20, #0x40]
    ldr x2, [x20]
    cmp x2, x0
    b.ne fail
    // Each frame must expose its native affinity. Only the final frame sets Last.
    // QSEE derives the frame stride from the distributor's architecture field.
    ldr x6, =0x1700ffe8
    ldr w6, [x6]
    mov x7, #0x20000
    mov x8, #0x40000
    tst w6, #0xc0
    csel x6, x7, x8, eq
    adr x4, affinities
    mov x5, #0
    ldr x20, =0x17080008
5:  mov x28, #4
    ldr x2, [x20]
    ldr w3, [x4, x5, lsl #2]
    cmp x3, x2, lsr #32
    b.ne fail
    mov x28, #5
    ubfx x2, x2, #4, #1
    cmp x5, #7
    cset x3, eq
    cmp x2, x3
    b.ne fail
    add x20, x20, x6
    add x5, x5, #1
    cmp x5, #8
    b.lo 5b
    // CPU0 never accesses the code page that only CPU7 maps and executes.
4:  ldr w0, [x19, #8]
    cmp w0, #2
    b.ne 4b
    dmb sy
    ldr x0, =0x600d0000
    ldr w1, [x0]
    b .
fail:
    ldr x0, =0xdead0000
    add x0, x0, x28, lsl #2
    str w28, [x0]
    b .
.balign 4
affinities:
    .word 0x00000, 0x00100, 0x00200, 0x00300
    .word 0x00400, 0x00500, 0x10000, 0x10100

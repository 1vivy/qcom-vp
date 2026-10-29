.section .text
.globl _start
_start:
    mov w3, #0
    b record
.org 0x100
    mov w3, #1
    b record
.org 0x200
    mov w3, #2
record:
    mrs x1, mpidr_el1
    ubfx x2, x1, #16, #8
    ubfx x1, x1, #8, #8
    mov x4, #6
    madd x1, x2, x4, x1
    mov x0, #0x14800000
    add x0, x0, x1, lsl #4
    str w3, [x0, #8]
    dmb sy
    ldr w1, [x0]
    add w1, w1, #1
    str w1, [x0]
    dmb sy
1:  ldr w1, [x0, #4]
    add w1, w1, #1
    str w1, [x0, #4]
    dmb sy
    b 1b

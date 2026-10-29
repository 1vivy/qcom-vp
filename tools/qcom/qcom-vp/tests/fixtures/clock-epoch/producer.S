.section .text
.globl _start
_start:
    mov x0, #0x20400000
    mov w1, #1
    // The consumer must execute before the first publication.
1:  ldr w2, [x0, #8]
    cbz w2, 1b
    b publish_first
.org 0x100
publish_first:
    str w1, [x0]
    dmb sy
2:  ldr w2, [x0, #4]
    cmp w2, #1
    b.ne 2b
    mov w1, #2
    b publish_second
.org 0x200
publish_second:
    str w1, [x0]
    dmb sy
3:  ldr w2, [x0, #4]
    cmp w2, #2
    b.ne 3b
    // Require SystemC timer progress after both debugger stops.
    mov x3, #0x17810000
    mov w4, #0x3f
    str w4, [x3, #0x40]
    add x3, x3, #0x1000
    ldr w4, [x3]
4:  ldr w5, [x3]
    sub w5, w5, w4
    mov w6, #19200
    cmp w5, w6
    b.lo 4b
    mov x3, #0x600d0000
    str w2, [x3]
5:  b 5b

.section .text
.globl _start
_start:
    mov x0, #0x02000000
    mov x1, #0x6000
    movk x1, #0x2223, lsl #16
    ldr w2, [x1, #0x4c]
    cbnz w2, failed
    ldr w2, [x1, #0x2c]
    cbnz w2, failed
    mov w2, #0x5054
    movk w2, #0x4c42, lsl #16
    str w2, [x0]
    ldr w2, [x1, #0x2c]
    cbnz w2, failed
    mov w2, #2
    str w2, [x0, #4]
    dsb sy
    ldr w2, [x1, #0x2c]
    cmp w2, #5
    b.ne failed
    mov w2, #3
    str w2, [x0, #8]
    ldr w2, [x1, #0x2c]
    cbnz w2, failed
    mov w2, #1
    str w2, [x0, #12]
    mov w2, #0x4d54
    movk w2, #0x5746, lsl #16
    str w2, [x0]
    dsb sy
    ldr w2, [x1, #0x2c]
    cbnz w2, failed
    str wzr, [x0]
    dsb sy
    ldr w2, [x1, #0x2c]
    cbnz w2, failed
    ldr x1, =0x00c02008
    ldr x2, =0x00c0022c
    mov w3, #4
    str w3, [x2]
    mov w4, #8
    str w4, [x2, #4]
    ldr w5, [x1, #4]
    cmp w5, w4
    b.ne failed
    orr w5, w5, #0x10
    str w5, [x2, #4]
    ldr w6, [x1, #4]
    cmp w6, w5
    b.ne failed
    mov w7, #2
    str w7, [x1, #0x10]
    ldr w7, [x1, #0x10]
    orr w7, w7, #4
    str w7, [x1, #0x10]
    ldr w7, [x1, #0x10]
    cmp w7, #6
    b.ne failed
    ldr w6, [x1, #4]
    cmp w6, w5
    b.ne failed
    ldr w6, [x1]
    cmp w6, w3
    b.ne failed
    mov x0, #0x600d0000
    str w2, [x0]
1:  b 1b
failed:
    mov x0, #0xbad00000
    str w2, [x0]
2:  b 2b

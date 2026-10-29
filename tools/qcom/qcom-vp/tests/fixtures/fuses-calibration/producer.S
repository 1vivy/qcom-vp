.section .text
.globl _start
_start:
    movz x0, #0x8000
    movk x0, #0x221c, lsl #16

    // A listed VPP word returns the raw page word.
    ldr w1, [x0, #0x9e0]
    movz w2, #0x5678
    movk w2, #0x1234, lsl #16
    cmp w1, w2
    b.ne failed

    // A listed IDDQ word uses the same page offset.
    ldr w1, [x0, #0xb0c]
    movz w2, #0xcdef
    movk w2, #0x90ab, lsl #16
    cmp w1, w2
    b.ne failed

    // The gap between the listed groups still has no owner.
    ldr w1, [x0, #0xa28]

failed:
    movz x0, #0
    movk x0, #0xbad0, lsl #16
    str w1, [x0]
1:  b 1b

// SPDX-License-Identifier: BSD-3-Clause-Clear
.macro check base, offset, value
    ldr w4, [\base, #\offset]
    mov w5, #\value
    cmp w4, w5
    b.ne failed
.endm
.macro retained offset
    str w3, [x19, #\offset]
    ldr w4, [x19, #\offset]
    cmp w4, w3
    b.ne failed
.endm
.section .text
.globl _start
_start:
    mov x19, #0x100000
    mov x20, #0x110000
    mov x21, #0x120000
    mov x22, #0x130000
    mov x23, #0x140000
    mov x24, #0x150000
    add x25, x21, #0x100
    add x26, x23, #0x100

    check x19, 0x408, 0
    check x19, 0x404, 0
    check x19, 0x414, 0
    mov w3, #0x5678
    movk w3, #0x1234, lsl #16
    retained 0x10
    retained 0x18
    retained 0x1c
    retained 0x40
    retained 0x60
    retained 0x64
    retained 0x68
    retained 0x6c
    retained 0x70
    retained 0x74
    check x20, 0x10, 0
    // Bit zero starts the phase without prerequisites at +400 or +410.
    mov w3, #0x52
    retained 0x404
    check x19, 0x408, 0
    mov w3, #0x53
    retained 0x404
    check x19, 0x408, 0x80000000
    check x20, 0x408, 0
    mov w3, #0x77
    retained 0x400
    mov w3, #0x88
    retained 0x410
    check x19, 0x408, 0x80000000
    mov w3, #0x42
    retained 0x414
    check x19, 0x408, 0x80000000
    mov w3, #0x43
    retained 0x414
    check x19, 0x408, 1
    str wzr, [x19, #0x404]
    str wzr, [x19, #0x414]
    check x19, 0x404, 0
    check x19, 0x414, 0
    check x19, 0x408, 1
    mov w3, #1
    str w3, [x19, #0x404]
    check x19, 0x408, 0x80000000
    str w3, [x19, #0x414]
    check x19, 0x408, 1

    check x21, 0, 0
    check x21, 4, 0
    mov w3, #0x23
    str w3, [x21]
    check x21, 0, 0x23
    check x21, 4, 0x23
    check x25, 4, 0
    str wzr, [x21]
    check x21, 0, 0
    check x21, 4, 0

    // Acceptance and request share state. The other instance starts halted.
    check x23, 0, 1
    check x23, 0xc, 1
    check x23, 4, 0
    check x23, 8, 0
    check x26, 0, 0
    check x26, 0xc, 0
    mov w3, #1
    str w3, [x24]
    check x24, 0, 0x80000001
    str w3, [x22]
    check x24, 0, 1
    // A halt changes real source availability, not the retained branch enable.
    str wzr, [x23, #0xc]
    check x23, 0, 0
    check x23, 0xc, 0
    check x24, 0, 0x80000001
    mov w3, #0x83
    str w3, [x23, #0xc]
    check x23, 0, 0x83
    check x23, 0xc, 0x83
    check x23, 4, 0
    check x23, 8, 0
    check x26, 0, 0
    check x24, 0, 1
    // Request bit zero, not merely a nonzero request, gates the clock.
    mov w3, #2
    str w3, [x23, #0xc]
    check x23, 0, 2
    check x24, 0, 0x80000001
    mov w3, #1
    str w3, [x23, #0xc]
    check x24, 0, 1
    str wzr, [x22]
    check x23, 0, 1
    check x24, 0, 0x80000001
    str w3, [x22]
    check x24, 0, 1

    // Each negative case must reach AccessMonitor after all behavioral checks.
#if CASE == 1
    str wzr, [x19, #0x408]
#elif CASE == 2
    str wzr, [x21, #4]
#elif CASE == 3
    str wzr, [x23]
#elif CASE == 4
    str wzr, [x23, #4]
#elif CASE == 5
    str wzr, [x23, #8]
#elif CASE == 6
    ldr w4, [x19, #0x20]
#elif CASE == 7
    strb wzr, [x19, #0x10]
#elif CASE == 8
    strb wzr, [x21]
#elif CASE == 9
    strb wzr, [x23, #0xc]
#endif
    mov x0, #0x600d0000
    ldr w0, [x0]
    b .
failed:
    mov x0, #0xdead0000
    ldr w0, [x0]
    b .

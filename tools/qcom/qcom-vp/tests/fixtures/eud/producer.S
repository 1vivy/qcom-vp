// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x20, #0x2000
    movk x20, #0x88e, lsl #16
    mov x21, #0x240
    movk x21, #0x88e, lsl #16
    mov x22, #0x1020
    movk x22, #0x88e, lsl #16
    ldr w0, [x20]
    cbnz w0, failed
    mov w0, #1
    str w0, [x20]
    ldr w0, [x20]
    cmp w0, #1
    b.ne failed
    // An enable request cannot create enumeration or an attached host.
    ldr w0, [x21]
    cbnz w0, failed
    ldr w0, [x22]
    cbnz w0, failed
    mov x23, #0x15000000
    mov w0, #1
    str w0, [x23]
    mov x24, #0xc704
    movk x24, #0xa60, lsl #16
    ldr w0, [x24]
    mov w1, #0xf00000
    cmp w0, w1
    b.ne failed
    ldr w0, [x21]
    cbnz w0, failed
    ldr w0, [x22]
    cbnz w0, failed
#if CASE == 1
    mov w0, #2
    str w0, [x20]
    b failed
#elif CASE == 2
    str wzr, [x22]
    b failed
#else
    str wzr, [x20]
    ldr w0, [x20]
    cbnz w0, failed
    ldr w0, [x21]
    cbnz w0, failed
    ldr w0, [x22]
    cbnz w0, failed
    mov x0, #0x600d0000
    str w0, [x0]
#endif
failed:
    mov x0, #0xbad00000
    str w0, [x0]
1:  b 1b

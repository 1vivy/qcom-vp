// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x0, #0x9000
    movk x0, #0xc46, lsl #16
    ldr w2, [x0, #4]
    cmp w2, #0x100
    b.ne failed
    ldr w2, [x0]
    cbnz w2, failed
    ldr w2, [x0, #0xc]
    cbnz w2, failed
    ldr w2, [x0, #0x10]
    cbnz w2, failed
    // Every slot and control starts at the declared virtual cold zero.
    mov x1, #0
cold:
    add x3, x0, x1
    ldr w2, [x3, #0x100]
    cbnz w2, failed
    ldr w2, [x3, #0x200]
    cbnz w2, failed
    add x1, x1, #4
    cmp x1, #0x40
    b.ne cold
    mov w2, #0xd
    str w2, [x0, #0x10]
    // Synthetic guest data. No selected firmware table bytes are reproduced.
    mov x1, #0
program:
    add x3, x0, x1
    mov w2, #2
    str w2, [x3, #0x200]
    mov w2, #0xa5000000
    orr w2, w2, w1
    str w2, [x3, #0x100]
    add x1, x1, #4
    cmp x1, #0x40
    b.ne program
    mov w2, #0x3335
    movk w2, #0x6576, lsl #16
    str w2, [x0, #0xc]
    mov w2, #1
    str w2, [x0]
    mov w2, #3
    str w2, [x0]
    mov x1, #0
check:
    add x3, x0, x1
    ldr w2, [x3, #0x100]
    mov w4, #0xa5000000
    orr w4, w4, w1
    cmp w2, w4
    b.ne failed
    ldr w2, [x3, #0x200]
    cmp w2, #2
    b.ne failed
    add x1, x1, #4
    cmp x1, #0x40
    b.ne check
    ldr w2, [x0]
    cmp w2, #3
    b.ne failed
    ldr w2, [x0, #0x10]
    cmp w2, #0xd
    b.ne failed
    ldr w2, [x0, #0xc]
    mov w4, #0x3335
    movk w4, #0x6576, lsl #16
    cmp w2, w4
    b.ne failed
#ifdef IBUS_UNSUPPORTED_STATUS
    // No known status contract exists. This read must fault, not report ready.
    ldr w2, [x0, #8]
    b failed
#endif
#ifdef IBUS_BAD_CONTROL
    mov w2, #0x40
    str w2, [x0, #0x200]
    b failed
#endif
#ifdef IBUS_WRITE_REVISION
    str wzr, [x0, #4]
    b failed
#endif
    mov x0, #0x600d0000
    str w2, [x0]
failed:
    mov x0, #0xbad00000
    str w2, [x0]
1:  b 1b

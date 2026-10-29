// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.global _start
_start:
    mov x20, #0x8000
    movk x20, #0x62, lsl #16
    mov w24, #1

    // Configuration remains pending until the controller starts.
    mov w0, #-2
    str w0, [x20, #0x120]
    ldr w1, [x20, #0x140]
    cmp w0, w1
    b.eq fail
    str wzr, [x20, #0x200]
    mov w0, #0xfffc
    str w0, [x20, #0x80c]
    mov w0, #123
    str w0, [x20, #0x840]
    mov w0, #456
    str w0, [x20, #0x844]
    mov w0, #1
    str w0, [x20, #4]
    ldr w0, [x20, #0x810]
    tbz w0, #0, fail
    ldr w0, [x20, #0x824]
    cbnz w0, fail
    ldr w0, [x20, #0x140]
    cmn w0, #2
    b.ne fail

    // A valid measurement uses the programmed targets.
    mov w24, #2
    mov w0, #10
    str w0, [x20, #0xa0]
    str w0, [x20, #0xa4]
    mov w0, #4
    str w0, [x20, #0x81c]
    str wzr, [x20, #0x800]
    ldr w0, [x20, #0x810]
    cbnz w0, fail
    ldr w0, [x20, #0x814]
    mov w1, #0x807b
    movk w1, #0x101c, lsl #16
    cmp w0, w1
    b.ne fail
    ldr w0, [x20, #0x824]
    cmp w0, #5
    b.ne fail
    mov w0, #1
    str w0, [x20, #0x820]
    ldr w0, [x20, #0x824]
    cmp w0, #4
    b.ne fail

    // A new enable uses the changed target and ring mask.
    mov w24, #3
    str wzr, [x20, #4]
    mov w0, #321
    str w0, [x20, #0x840]
    mov w0, #0xfffe
    str w0, [x20, #0x80c]
    mov w0, #1
    str w0, [x20, #4]
    ldr w0, [x20, #0x814]
    mov w1, #0x1141
    movk w1, #0x14, lsl #16
    cmp w0, w1
    b.ne fail
    mov x0, #0x600d0000
    str wzr, [x0]
    b .
fail:
    mov x0, #0xbad00000
    str w24, [x0, x24, lsl #2]
    b .

// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.global _start
.macro request value
    str wzr, [x20, #0x64]
    ldr w0, =\value
    str w0, [x20, #0x68]
    mov w0, #1
    str w0, [x20, #0x64]
    ldr w0, [x20, #0x6c]
    ldr w1, =((\value & 0xfcff0000) | 0xf0)
    cmp w0, w1
    b.ne fail
.endm
_start:
    mov x20, #0x6000
    movk x20, #0x3107, lsl #16
    add x21, x20, #0x3000
    mov x22, #0xa800
    movk x22, #0x3104, lsl #16
    mov x23, #0x40000000
    mov w24, #1

    // Clock configuration cannot release the processor.
    str wzr, [x20, #4]
    ldr w0, [x20, #8]
    cbnz w0, fail
    mov w0, #1
    str w0, [x20, #4]
    ldr w0, [x20, #4]
    cmp w0, #1
    b.ne fail
    ldr w0, [x20, #8]
    cbnz w0, fail

    // Both requester views control the same processor and backend.
    mov w24, #2
    mov w0, #0xa
    str w0, [x23]
    ldr w0, [x21]
    cmp w0, #0xa
    b.ne fail
    mov w0, #2
    str w0, [x23]
    mov w0, #0x5a5a
    str w0, [x22, #0x20]
    str wzr, [x23]
    ldr w0, [x20, #8]
    cmp w0, #0x10
    b.ne fail
    ldr w0, [x22, #0xc]
    cmp w0, #0x1d
    b.ne fail
    ldr x28, =0x31048000
    ldr w0, [x28]
    ldr w1, =0x3104a000
    cmp w0, w1
    b.ne fail
    ldr w0, [x28, #4]
    ldr w1, =0x31060000
    cmp w0, w1
    b.ne fail
    ldr w0, [x28, #8]
    ldr w1, =0x16000
    cmp w0, w1
    b.ne fail

    // Reset discards staged commands without clearing guest memory.
    mov w24, #3
    mov w0, #1
    str w0, [x20, #0x10]
    str w0, [x20, #0x60]
    mov w0, #0x1234
    str w0, [x20, #0x68]
    str w0, [x22, #0xc]
    mov w0, #2
    str w0, [x21]
    ldr w0, [x20, #8]
    cbnz w0, fail
    ldr w0, [x20, #0x10]
    cbnz w0, fail
    ldr w0, [x20, #0x60]
    cbnz w0, fail
    ldr w0, [x20, #0x68]
    cbnz w0, fail
    ldr w0, [x22, #0x20]
    mov w1, #0x5a5a
    cmp w0, w1
    b.ne fail

    // A fresh release performs startup again after the backend reset.
    mov w24, #4
    str wzr, [x21]
    ldr w0, [x22, #0xc]
    cmp w0, #0x1d
    b.ne fail
    ldr w0, [x20, #8]
    cmp w0, #0x10
    b.ne fail

    // An enabled idle row has index zero and frequency zero.
    mov w24, #5
    ldr x25, =0x31050000
    ldr x26, =0x31051000
    str x25, [x22, #0x468]
    str x26, [x22, #0x470]
    mov w0, #0x0202
    strh w0, [x22, #0x2c]
    ldr w0, =0x000100a4
    str w0, [x25, #0x34]
    ldr w0, =0x0b000200
    str w0, [x25, #0x38]
    ldr w0, =0x00c00d00
    str w0, [x25, #0x3c]
    ldr w0, =0x00010dc0
    str w0, [x25, #0x40]
    mov w0, #1
    str w0, [x26, #0x90]
    str w0, [x26, #0xac]
    str w0, [x26, #0xb8]
    ldr w0, =550000
    str w0, [x26, #0xb0]
    // The SHRM runtime tables supply the active period and resource state.
    ldr x28, =0x3104acb8
    mov w0, #10000
    str w0, [x28, #0xc]
    mov w0, #1827
    str w0, [x28, #0x20]
    ldr w0, =550000
    str w0, [x28, #0x18]
    ldr x28, =0x3104b36f
    mov w0, #2
    strb w0, [x28]
    ldr x28, =0x3104adbc
    ldr w0, =200000
    str w0, [x28]
    mov w0, #1
    str w0, [x20, #0x60]
    ldr w0, =0x09080011
    str w0, [x20, #0x68]
    mov w0, #1
    str w0, [x20, #0x64]
    ldr w0, [x20, #0x6c]
    ldr w1, =0x080800f0
    cmp w0, w1
    b.ne fail
    ldr w0, [x22, #0x48]
    cmp w0, #1024
    b.ne fail
    ldrb w0, [x22, #0x33]
    cmp w0, #15
    b.ne fail
    ldrb w0, [x22, #0x41]
    cmp w0, #1
    b.ne fail
    ldr x28, =0x34a00080
    ldr w0, [x28]
    cmp w0, #1827
    b.ne fail
    ldr x28, =0x3104ba90
    ldr w0, [x28]
    cmp w0, #1827
    b.ne fail
    ldrb w0, [x28, #8]
    cmp w0, #1
    b.ne fail
    ldr x28, =0x3104b364
    ldrh w0, [x28]
    cmp w0, #2
    b.ne fail
    ldr x28, =0x3104b380
    ldr w0, [x28]
    ldr w1, =200000
    cmp w0, w1
    b.ne fail
    ldr x28, =0x3104b350
    ldr w0, [x28]
    ldr w1, =550000
    cmp w0, w1
    b.ne fail
    // Save actual controls without changing the unused snapshot capacity.
    mov w24, #6
    ldr x27, =0x50000000
    str x27, [x22, #0x460]
    ldr x28, =0x3104d4b4
    mov w0, #0x0201
    strh w0, [x28]
    mov w0, #0x1b80
    strh w0, [x28, #2]
    strh wzr, [x28, #4]
    mov w0, #1
    strh w0, [x28, #6]
    strh wzr, [x28, #8]
    ldr x29, =0x31b80000
    ldr w0, =0x12345678
    str w0, [x29]
    ldr w0, =0x89abcdef
    str w0, [x29, #4]
    ldr w0, =0xcafebabe
    str w0, [x27, #0x1a00]
    str w0, [x27, #0x1a10]
    request 0x05060000
    ldr w0, [x27, #0x1a08]
    ldr w1, =0x12345678
    cmp w0, w1
    b.ne fail
    ldr w0, [x27, #0x1a0c]
    ldr w1, =0x89abcdef
    cmp w0, w1
    b.ne fail
    ldr w0, [x27, #0x1a00]
    ldr w1, =0xcafebabe
    cmp w0, w1
    b.ne fail
    ldr w0, [x27, #0x1a10]
    cmp w0, w1
    b.ne fail
    // Restore from guest bytes, not a host copy or a constant image.
    mov w24, #7
    str wzr, [x29]
    str wzr, [x29, #4]
    ldr w0, =0xdeadbeef
    str w0, [x27, #0x1a08]
    request 0x05070000
    ldr w0, [x29]
    ldr w1, =0xdeadbeef
    cmp w0, w1
    b.ne fail
    ldr w0, [x29, #4]
    ldr w1, =0x89abcdef
    cmp w0, w1
    b.ne fail
    // Retention updates preserve trained state and existing SRAM masks.
    mov w24, #9
    ldr x28, =0x3104b538
    mov w0, #0x1080
    strh w0, [x28]
    request 0x05030000
    ldrh w0, [x28]
    mov w1, #0x1383
    cmp w0, w1
    b.ne fail
    ldr x28, =0x310a92bc
    ldr w0, [x28]
    cmp w0, #0xff
    b.ne fail
    ldr x28, =0x310a80bc
    ldr w0, [x28]
    ldr w1, =0x89c0300f
    cmp w0, w1
    b.ne fail
    ldr w0, [x29]
    ldr w1, =0xdeadbeef
    cmp w0, w1
    b.ne fail
    ldr x28, =0x3104b344
    mov w0, #0x10000
    str w0, [x28]
    request 0x05030000
    ldr x0, =0x310a92bc
    ldr w0, [x0]
    cmp w0, #0xa3
    b.ne fail
    mov w0, #1
    strb w0, [x28]
    request 0x05030000
    ldr x0, =0x310a92bc
    ldr w0, [x0]
    cmp w0, #0xf3
    b.ne fail
    mov w0, #1
    strb w0, [x28, #4]
    request 0x05030000
    ldrb w0, [x28, #4]
    cbnz w0, fail
    strb wzr, [x28]
    mov w0, #1
    strb w0, [x28, #4]
    request 0x05030000
    ldr x0, =0x31048cb4
    ldr w0, [x0]
    ldr w1, =0x408000
    cmp w0, w1
    b.ne fail
    ldr x0, =0x31078000
    ldr w0, [x0]
    cmp w0, #1
    b.ne fail
    // A later clock switch must preserve the trained lifecycle.
    mov w24, #8
    request 0x09080000
    ldr x28, =0x34a00080
    ldr w0, [x28]
    mov w1, #10000
    cmp w0, w1
    b.ne fail
    ldr x28, =0x3104ba90
    ldr w0, [x28]
    mov w1, #10000
    cmp w0, w1
    b.ne fail
    ldrb w0, [x28, #8]
    cbnz w0, fail
    request 0x09410000
    mov x0, #0x600d0000
    str wzr, [x0]
    b .
fail:
    mov x0, #0xbad00000
    str w24, [x0, x24, lsl #2]
    b .

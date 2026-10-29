.section .text
.globl _start
_start:
    ldr x19, =0x416800
    ldr x20, =0x489000
    ldr x21, =0x4a0000
    ldr x22, =0x501000
    mov w9, #0x400000
    str w9, [x22, #0xc]
    ldr w9, =0x401000
    str w9, [x22, #0x14]
    str w9, [x22, #0x4c]
    ldr w9, =0x402000
    str w9, [x22, #0x54]
    mov w9, #2
    str w9, [x22, #0x18]
    str w9, [x22, #0x1c]
    mov w9, #4
    str w9, [x22, #0x58]
    str w9, [x22, #0x5c]
    mov w9, #1
    str w9, [x22, #4]
    str w9, [x22, #0x44]
    ldr x9, =0x400000
    str wzr, [x9]
    ldr x9, =0x419000
    str wzr, [x9]
    adr x9, old_callback
    str w9, [x19, #8]
    adr x9, runtime_callback
    str w9, [x20, #0x14]
    str w20, [x20, #4]
    adr x9, old_callback
    str w9, [x19, #0x10]
    adr x9, runtime_callback
    str w9, [x20, #0x10]
    mov w0, #5
    ldr w9, [x19, #8]
    blr x9
    cmp w0, #8
    b.ne failed
    ldr x23, =0x448f1c
    ldr x24, =0x464200
    mov w9, #0x1234
    str w9, [x23, #0x7c]
    mov w9, #0x5678
    str w9, [x23, #0xa8]
    mov w9, #0x9abc
    str w9, [x23, #0x1f4]
    mov w9, #-1
    str w9, [x24, #0xa8]
    mov w9, #1
    str w9, [x21]
    ldr w25, [x21, #4]
    ldr w26, [x21, #8]
    ldr w9, [x26, #0x7c]
    mov w10, #0x1234
    cmp w9, w10
    b.ne failed
    ldr w9, [x26, #0x218]
    mov w10, #0x5678
    cmp w9, w10
    b.ne failed
    ldr w9, [x26, #0x4d4]
    mov w10, #0x9abc
    cmp w9, w10
    b.ne failed
    ldr w9, [x26, #0xa8]
    cbnz w9, failed
    mov w9, #0x1357
    str w9, [x26, #0xa8]
    ldr w9, [x25, #0xa8]
    mov w10, #0x5678
    cmp w9, w10
    b.ne failed
    mov w9, #0x2468
    str w9, [x25, #0x1e4]
    ldr w10, [x26, #0x4c4]
    cmp w9, w10
    b.ne failed
    mov w9, #0x3579
    str w9, [x26, #0x2ac]
    ldr w10, [x25, #0x13c]
    cmp w9, w10
    b.ne failed
    ldr x9, =0x123456789abcdef0
    add x10, x25, #0x1f4
    str x9, [x10]
    ldr w10, [x26, #0x4d4]
    cmp w9, w10
    b.ne failed
    lsr x9, x9, #32
    ldr w10, [x25, #0x1f8]
    cmp w9, w10
    b.ne failed
    ldr w9, [x26, #0x4d8]
    cbnz w9, failed
    mov w9, #1
    str w9, [x21]
    ldr w9, [x26, #0xa8]
    mov w10, #0x1357
    cmp w9, w10
    b.ne failed
    ldr x9, =0x401000
    mov w10, #0x1234
    str w10, [x9]
    ldr w9, [x9]
    cmp w9, w10
    b.ne failed
    mov w0, #5
    ldr w9, [x19, #8]
    blr x9
    cmp w0, #10
    b.ne failed
    adr x9, oem_callback
    str w9, [x19, #8]
    mov w0, #5
    ldr w9, [x20, #0x14]
    blr x9
    cmp w0, #15
    b.ne failed
    adr x9, updated_callback
    str w9, [x20, #0x14]
    mov w0, #5
    ldr w9, [x19, #8]
    blr x9
    cmp w0, #12
    b.ne failed
    mov w0, #5
    ldr w9, [x19, #0x10]
    blr x9
    cmp w0, #8
    b.ne failed
    adr x9, oem_callback
    str w9, [x19, #0x10]
    mov w0, #5
    ldr w9, [x19, #0x10]
    blr x9
    cmp w0, #15
    b.ne failed
    mov w0, #5
    ldr w9, [x20, #0x10]
    blr x9
    cmp w0, #10
    b.ne failed
    ldr w9, [x19, #4]
    cmp w9, w19
    b.ne failed
    ldr w9, [x20, #4]
    cmp w9, w20
    b.ne failed
    ldrh w9, [x19, #6]
    mov w10, #0x41
    cmp w9, w10
    b.ne failed
#ifdef REVOKED_DOMAIN
    ldr x9, =0x400000
    ldr w9, [x9]
    b failed
#else
    ldr w9, [x19, #12]
1:  b 1b
#endif
failed:
    mov x9, #0xbad00000
    str w0, [x9]
2:  b 2b
old_callback:
    add w0, w0, #3
    ret
runtime_callback:
    lsl w0, w0, #1
    ret
oem_callback:
    add w0, w0, w0, lsl #1
    ret
updated_callback:
    add w0, w0, #7
    ret

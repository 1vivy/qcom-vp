// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.global _start
_start:
    ldr x19, =0x09bb4000
    ldr x20, =0x09bab000
    ldr x21, =0x09c70000
    ldr x22, =0xa4200000
    mov w0, #1
    str w0, [x19, #0x30]
    ldr w0, =0xa4100000
    str w0, [x21, #0xc]
    bl require_cold
    str wzr, [x20]
    mov w0, #1
    str w0, [x19, #4]
    str w0, [x19, #8]
    str w0, [x19, #0x2c]
    str w0, [x19, #0x40]
    ldr x0, =0x09bae008
    mov w1, #1
    str w1, [x0]
    mov w0, #0x100
    str w0, [x19, #0x14]
    mov w0, #1
    str w0, [x19, #0x10]
    bl require_cold
    mov w0, #0x101
    str w0, [x19, #0x14]
    mov w0, #1
    str w0, [x19, #0x10]
1:  wfe
    b 1b
require_cold:
    mov w2, #256
2:  ldr w0, [x22]
    cbnz w0, failed
    subs w2, w2, #1
    b.ne 2b
    ret
failed:
    ldr x0, =0xbad00000
    ldr w0, [x0]

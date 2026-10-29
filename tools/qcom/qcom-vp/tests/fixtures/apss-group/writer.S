// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    ldr x19, =0x14800000
    ldr x20, =0x80000000
    ldr w0, =0x52800160 // mov w0, #11
    str w0, [x20]
    ldr w0, =0xd65f03c0 // ret
    str w0, [x20, #4]
    dmb sy
    mov w0, #1
    str w0, [x19]
1:  ldr w0, [x19, #4]
    cmp w0, #1
    b.ne 1b
    dmb sy
    ldr w0, =0x528002c0 // mov w0, #22
    str w0, [x20]
    dmb sy
    mov w0, #2
    str w0, [x19]
2:  wfi
    b 2b

// SPDX-License-Identifier: BSD-3-Clause-Clear
// This original guest checks watchdog access across security transitions.
.section .text
.global _start
_start:
    ldr x10, =0x17600000
    adr x0, vectors
    msr vbar_el3, x0
    str wzr, [x10]
#ifndef DENY_CONTROL
    mov w0, #-1
    str w0, [x10]
    ldr w0, [x10]
    cmp w0, #1
    b.ne fail
#endif
    mov x0, #0x431
    msr scr_el3, x0
    mov x0, #(1 << 31)
    msr hcr_el2, x0
    adr x0, nonsecure
    msr elr_el3, x0
    mov x0, #0x3c5
    msr spsr_el3, x0
    isb
    eret
nonsecure:
#ifdef DENY_MODE
    str wzr, [x10]
    b fail
#endif
#ifdef DENY_CHICKEN
    mov w0, #15
    str w0, [x10, #0x24]
    b fail
#endif
    ldr w0, [x10, #8]
#ifdef DENY_CONTROL
    b fail
#endif
#ifdef READ_CHICKEN
    ldr w0, [x10, #0x24]
    b fail
#endif
#ifdef INVALID_CHICKEN
    mov w0, #16
#else
    mov w0, #15
#endif
    str w0, [x10, #0x24]
    mov w0, #77
    str w0, [x10, #0x10]
    ldr w1, [x10, #0x10]
    cmp w0, w1
    b.ne fail
#ifdef REVOKE_CONTROL
    smc #0
    ldr w0, [x10, #8]
    b fail
#endif
    ldr x0, =0x600d0000
    str wzr, [x0]
    b fail
.balign 2048
vectors:
.rept 16
    b exception
    .balign 128
.endr
exception:
    mrs x0, esr_el3
    lsr x0, x0, #26
    cmp x0, #0x17
    b.ne fail
    ldr w0, [x10, #0x10]
    cmp w0, #77
    b.ne fail
    str wzr, [x10]
    eret
fail:
    ldr x0, =0xbad00000
    str wzr, [x0]
    b .

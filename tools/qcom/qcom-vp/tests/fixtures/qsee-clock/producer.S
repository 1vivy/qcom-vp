// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    // GCC ACGCR: bit zero enables the clock and bit 31 is active-high CLK_ON.
    ldr x19, =0x00185000
    ldr w0, [x19]
    cbnz w0, failed
    mov w0, #1
    str w0, [x19]
    ldr w0, [x19]
    ldr w1, =0x80000001
    cmp w0, w1
    b.ne failed
    ldr x20, =0x40000000
    ldr w0, [x20]
    cmp w0, w1
    b.ne failed
    str w0, [x19]
    str wzr, [x19]
    ldr w0, [x20]
    cbnz w0, failed

    // GPU CX and GX retain bit eight. Their CFG words follow the collapse request.
    ldr x19, =0x03d99080
    bl probe_domain
    ldr x19, =0x03d68024
    bl probe_domain

    // Ordinary CBCRs keep active-high CLK_OFF status.
    ldr x19, =0x03d990d4
    bl probe_branch
    ldr x19, =0x03d990e8
    bl probe_branch
    // GPU policy bit 13 retains state without enabling a disabled branch.
    ldr x19, =0x03d990d4
    mov w0, #0x2000
    str w0, [x19]
    ldr w0, [x19]
    ldr w1, =0x80002000
    cmp w0, w1
    b.ne failed
    mov w0, #0x2001
    str w0, [x19]
    ldr w0, [x19]
    mov w2, #0x2001
    cmp w0, w2
    b.ne failed
    mov w0, #0x2000
    str w0, [x19]
    ldr w0, [x19]
    cmp w0, w1
    b.ne failed

    // The DCP owner derives both completion bits from its collapse request.
    ldr x19, =0x09bab000
    ldr w0, [x19]
    cmp w0, #1
    b.ne failed
    ldr w0, [x19, #4]
    mov w1, #0x8000
    cmp w0, w1
    b.ne failed
    ldr w0, =0x18008
    str w0, [x19, #4]
    ldr w0, [x19, #4]
    mov w1, #0x8008
    cmp w0, w1
    b.ne failed
    str wzr, [x19]
    ldr w0, [x19, #4]
    ldr w1, =0x10008
    cmp w0, w1
    b.ne failed
    mov w0, #1
    str w0, [x19]
    ldr w0, [x19, #4]
    mov w1, #0x8008
    cmp w0, w1
    b.ne failed
    // Both shared DCP gates require power and a completed root configuration.
    ldr x20, =0x09bb4040
    bl probe_dcp_branch
    ldr x20, =0x09bae004
    bl probe_dcp_branch


    ldr x0, =0x600d0000
    str wzr, [x0]
1:  b 1b

probe_domain:
    ldr w0, [x19]
    cmp w0, #1
    b.ne failed
    ldr w0, [x19, #4]
    mov w1, #0x8000
    cmp w0, w1
    b.ne failed
    mov w0, #0x100
    str w0, [x19]
    ldr w0, [x19]
    ldr w1, =0x80000100
    cmp w0, w1
    b.ne failed
    ldr w0, [x19, #4]
    mov w1, #0x10000
    cmp w0, w1
    b.ne failed
    mov w0, #0x101
    str w0, [x19]
    ldr w0, [x19]
    cmp w0, #0x101
    b.ne failed
    ldr w0, [x19, #4]
    mov w1, #0x8000
    cmp w0, w1
    b.ne failed
    ret

probe_branch:
    ldr w0, [x19]
    ldr w1, =0x80000000
    cmp w0, w1
    b.ne failed
    mov w0, #1
    str w0, [x19]
    ldr w0, [x19]
    cmp w0, #1
    b.ne failed
    str wzr, [x19]
    ldr w0, [x19]
    cmp w0, w1
    b.ne failed
    ret

probe_dcp_branch:
    ldr w0, [x20]
    tbz w0, #31, failed
    mov w0, #1
    str w0, [x20]
    ldr w0, [x20]
    tbz w0, #31, failed
    str wzr, [x19]
    ldr w0, [x20]
    tbz w0, #31, failed
    ldr x21, =0x09bb4010
    mov w0, #0x101
    str w0, [x21, #4]
    mov w0, #1
    str w0, [x21]
    ldr w0, [x21]
    tbnz w0, #0, failed
    ldr w0, [x20]
    cmp w0, #1
    b.ne failed
    mov w0, #1
    str w0, [x19]
    ldr w0, [x20]
    tbz w0, #31, failed
    ret

failed:
    ldr x0, =0xbad00000
    str w1, [x0]
2:  b 2b

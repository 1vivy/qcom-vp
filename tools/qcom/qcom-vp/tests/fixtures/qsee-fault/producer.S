// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
#if CASE == 1
    // Deferred fault status has no established semantics.
    ldr x0, =0x01500020
    ldr w1, [x0]
    b failed
#elif CASE == 2
    ldr x0, =0x01500008
    strb wzr, [x0]
    b failed
#else
    // Source banks remain independent despite their shared architectural INTID.
    ldr x19, =0x01500008
    ldr x20, =0x01600008
    mov w0, #0x55
    str w0, [x19]
    mov w0, #0xaa
    str w0, [x20]
    ldr w0, [x19]
    cmp w0, #0x55
    b.ne failed
    // APSS and CPUCP reach the same retained control word.
    ldr x19, =0x00153038
    ldr x20, =0x10153038
    ldr w0, =0x80000004
    str w0, [x19]
    ldr w1, [x20]
    orr w1, w1, #0x100000
    str w1, [x20]
    ldr w0, [x19]
    ldr w2, =0x80100004
    cmp w0, w2
    b.ne failed
    // A distinct control must not overwrite the shared word.
    ldr x20, =0x10145010
    mov w1, #1
    str w1, [x20]
    ldr w0, [x19]
    cmp w0, w2
    b.ne failed
    ldr x0, =0x600d0000
    str wzr, [x0]
1:  b 1b
#endif
failed:
    ldr x0, =0xbad00000
    str w1, [x0]
2:  b 2b

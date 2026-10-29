// The same real reset consumer stays at boot count two throughout these checks.
runtime_probe:
    mov x0, #0x14800000
    mov x6, #0x7600
    movk x6, #0x1887, lsl #16
    mov x10, #0x7800
    movk x10, #0x1887, lsl #16
    ldr w5, [x10, #0x10]
    cbnz w5, failure
    // A request without applied configuration cannot acknowledge.
    mov w7, #0x1000
    str w7, [x6, #0x18]
    str w7, [x6, #0x14]
    ldr w5, [x10, #0x10]
    cbnz w5, failure
    mov w3, #-1
    str w3, [x10]
    mov w3, #15
    movk w3, #0xa5a5, lsl #16
    str w3, [x10, #4]
    ldr w5, [x10, #4]
    cmp w5, w3
    b.ne failure
    mov w3, #0x4000
    movk w3, #0x1234, lsl #16
    mov x2, #0x2158
    movk x2, #0x17c0, lsl #16
    str w3, [x2]
    ldr w5, [x2]
    cmp w5, w3
    b.ne failure
    mov x2, #0x3004
    movk x2, #0x1790, lsl #16
    str w3, [x2]
    mov x2, #0x800c
    movk x2, #0x1886, lsl #16
    str w3, [x2]
    mov x2, #0x2010
    movk x2, #0x181b, lsl #16
    str w3, [x2]
    mov x2, #0x2010
    movk x2, #0x183b, lsl #16
    str w3, [x2]
    ldr w5, [x2]
    cmp w5, w3
    b.ne failure
    // A native doubleword and separate halves share each lane policy word.
    mov x3, #2
    movk x3, #0x1234, lsl #48
    mov x2, #0x3018
    movk x2, #0x1800, lsl #16
    str x3, [x2]
    ldr x5, [x2]
    cmp x5, x3
    b.ne failure
    mov w4, #0x5678
    str w4, [x2, #4]
    ldr w5, [x2]
    cmp w5, #2
    b.ne failure
    ldr w5, [x2, #4]
    cmp w5, w4
    b.ne failure
    mov x2, #0x3018
    movk x2, #0x1820, lsl #16
    str x3, [x2]
    mov x11, #0x3000
    movk x11, #0x1795, lsl #16
    mov w3, #1
    movk w3, #0xabcd, lsl #16
    str w3, [x11]
    mov w4, #0x34560000
    str w4, [x11, #4]
    str w4, [x11, #8]
    ldr w5, [x11, #4]
    cmp w5, w4
    b.ne failure
    str w3, [x11, #0xc]
    str w3, [x11, #0x10]
    ldr w5, [x11, #0x10]
    cmp w5, w3
    b.ne failure
    // The final clock word is still absent.
    ldr w5, [x10, #0x10]
    cbnz w5, failure
    mov w3, #2
    movk w3, #0x4321, lsl #16
    str w3, [x11, #0x30]
    // Complete configuration with a stopped actual branch is insufficient.
    ldr w5, [x10, #0x10]
    cbnz w5, failure
    mov x12, #0x7000
    movk x12, #0x17c0, lsl #16
    mov w3, #1
    str w3, [x12]
    dmb sy
    mov w8, #0xc0000
    ldr w5, [x10, #0x10]
    cmp w5, w8
    b.ne failure
    // Each request clears independently. Repeated requests are idempotent.
    str wzr, [x6, #0x14]
    ldr w5, [x10, #0x10]
    cmp w5, #0x80000
    b.ne failure
    str w7, [x6, #0x14]
    str w7, [x6, #0x14]
    str wzr, [x6, #0x18]
    ldr w5, [x10, #0x10]
    cmp w5, #0x40000
    b.ne failure
    str w7, [x6, #0x18]
    ldr w5, [x10, #0x10]
    cmp w5, w8
    b.ne failure
    // Applied policy loss and source loss both revoke qualification.
    mov x2, #0x2158
    movk x2, #0x17c0, lsl #16
    str wzr, [x2]
    ldr w5, [x10, #0x10]
    cbnz w5, failure
    mov w3, #0x4000
    str w3, [x2]
    str wzr, [x12]
    dmb sy
    ldr w5, [x10, #0x10]
    cbnz w5, failure
    mov w3, #1
    str w3, [x12]
    dmb sy
    ldr w5, [x10, #0x10]
    cmp w5, w8
    b.ne failure
    str wzr, [x11]
    ldr w5, [x10, #0x10]
    cbnz w5, failure
    str w3, [x11]
    ldr w5, [x10, #0x10]
    cmp w5, w8
    b.ne failure
    mov w4, #0x10000
.Lruntime_count:
    ldr w1, [x0]
    cmp w1, #2
    b.ne failure
    subs w4, w4, #1
    b.ne .Lruntime_count
    ret

.section .text
.globl _start
_start:
    mov x0, #0x0c210000
    mov x1, #0x17810000
    add x2, x1, #0x1000
    add x3, x1, #0x3000
#if defined(TIMER_WIDE_FREQUENCY)
    ldr x5, [x2, #0x10]
    b failed
#elif defined(TIMER_WIDE_COMPARE)
    ldr x5, [x2, #0x20]
    b failed
#elif defined(TIMER_WIDE_CONFIGURATION)
    ldr x5, [x1]
    b failed
#elif defined(TIMER_WIDE_COUNTER_WRITE)
    str xzr, [x2]
    b failed
#endif
    mov w4, #0x3f
    str w4, [x1, #0x40]
    str w4, [x1, #0x44]
    mov w4, #-1
    str w4, [x2, #0x28]
    mov w4, #3
    str w4, [x2, #0x2c]
    ldr w5, [x2, #0x2c]
    tbz w5, #2, failed
    ldr w5, [x2, #0x24]
    mov w6, #-1
    cmp w5, w6
    b.ne failed
    mov w4, #1
    str w4, [x2, #0x28]
    ldr w5, [x2, #0x2c]
    tbnz w5, #2, failed
    ldr w5, [x2]
    cbnz w5, failed
    str w4, [x0]
1:  ldr w5, [x2]
    cbz w5, 1b
    str wzr, [x0]
    ldr w5, [x2]
    ldr w6, [x3]
    cmp w5, w6
    b.ne failed
    ldr w6, [x2]
    cmp w5, w6
    b.ne failed
    // One 64-bit sample must match the stopped counter's existing halves.
    ldr x5, [x2]
    ldr w6, [x2]
    ldr w7, [x2, #4]
    orr x6, x6, x7, lsl #32
    cmp x5, x6
    b.ne failed
    ldr x6, [x3]
    cmp x5, x6
    b.ne failed
    mov w4, #0x3f
    str w4, [x1, #0x58]
    add x20, x1, #0xd000
    ldr x6, [x20]
    cmp x5, x6
    b.ne failed
    ldr w5, [x2, #0x2c]
    tbz w5, #2, failed
    mov w4, #0x3b
    str w4, [x1, #0x44]
    ldr w5, [x3, #0x10]
    cbnz w5, failed
    ldr w5, [x2, #0x10]
    cbz w5, failed
    // Frame 0's EL0 view has independent access gates, not independent state.
    add x17, x1, #0x2000
    ldr w5, [x17, #0x10]
    cbnz w5, failed
    ldr x5, [x17]
    cbnz x5, failed
    ldr x5, [x17, #8]
    cbnz x5, failed
    mov w4, #0x303
    str w4, [x2, #0x14]
    ldr w5, [x2, #0x14]
    cmp w5, w4
    b.ne failed
    // Match the OEM high/low/high counter loop while the counter is stopped.
7:  ldr w5, [x17, #4]
    ldr w6, [x17]
    ldr w7, [x17, #4]
    cmp w5, w7
    b.ne 7b
    ldr w7, [x2]
    cmp w6, w7
    b.ne failed
    ldr w7, [x2, #4]
    cmp w5, w7
    b.ne failed
    ldr x5, [x17]
    ldr x6, [x2]
    cmp x5, x6
    b.ne failed
    ldr x5, [x17, #8]
    cmp x5, x6
    b.ne failed
    ldr w5, [x17, #0x10]
    ldr w6, [x2, #0x10]
    cmp w5, w6
    b.ne failed
    mov w4, #0x1234
    str w4, [x17, #0x20]
    ldr w5, [x2, #0x20]
    cmp w5, w4
    b.ne failed
    mov w4, #-1
    str w4, [x17, #0x28]
    ldr w5, [x2, #0x28]
    cmp w5, w4
    b.ne failed
    // CNTACR still overrides enabled second-view gates.
    mov w4, #0x3a
    str w4, [x1, #0x40]
    ldr w5, [x17]
    cbnz w5, failed
    ldr w5, [x17, #0x10]
    cbnz w5, failed
    ldr x5, [x17]
    cbnz x5, failed
    ldr x5, [x2]
    cbnz x5, failed
    ldr x5, [x17, #8]
    ldr x6, [x3]
    cmp x5, x6
    b.ne failed
    mov w4, #0x3d
    str w4, [x1, #0x40]
    ldr x5, [x17, #8]
    cbnz x5, failed
    ldr x5, [x2, #8]
    cbnz x5, failed
    mov w4, #0x3f
    str w4, [x1, #0x40]
    // Either counter gate exposes frequency, but not the physical counter.
    mov w4, #2
    str w4, [x2, #0x14]
    ldr w5, [x17]
    cbnz w5, failed
    ldr w5, [x17, #0x10]
    cbz w5, failed
    ldr x5, [x17]
    cbnz x5, failed
    ldr x5, [x17, #8]
    ldr x6, [x3]
    cmp x5, x6
    b.ne failed
    mov w4, #1
    str w4, [x2, #0x14]
    ldr x5, [x17, #8]
    cbnz x5, failed
    ldr x5, [x17]
    cmp x5, x6
    b.ne failed
    mov w4, #2
    str w4, [x2, #0x14]
    // Timer writes through a closed EL0 gate must not change physical state.
    ldr w7, [x2, #0x20]
    ldr w8, [x2, #0x24]
    str wzr, [x17, #0x20]
    ldr w5, [x17, #0x20]
    cbnz w5, failed
    ldr w5, [x2, #0x20]
    cmp w5, w7
    b.ne failed
    ldr w5, [x2, #0x24]
    cmp w5, w8
    b.ne failed
    // CNTEL0ACR itself cannot be changed through the second view.
    str wzr, [x17, #0x14]
    ldr w5, [x2, #0x14]
    cmp w5, #2
    b.ne failed
    mov w4, #0x203
    str w4, [x2, #0x14]
    // The third frame shares the counter but retains its own compare state.
    add x16, x1, #0x5000
    mov w4, #0x3f
    str w4, [x1, #0x48]
    ldr w5, [x2]
    ldr w6, [x16]
    cmp w5, w6
    b.ne failed
    mov w4, #0x1234
    str w4, [x16, #0x20]
    ldr w5, [x16, #0x20]
    cmp w5, w4
    b.ne failed
    ldr w5, [x3, #0x20]
    cbnz w5, failed
    mov w4, #-1
    str w4, [x16, #0x28]
    mov w4, #3
    str w4, [x16, #0x2c]
    ldr w5, [x16, #0x2c]
    cmp w5, #7
    b.ne failed
    ldr w5, [x3, #0x2c]
    cbnz w5, failed
    // The secure frames share time but keep offsets and deadlines independent.
    add x18, x1, #0x7000
    add x19, x1, #0x9000
    mov w4, #0x3f
    str w4, [x1, #0x4c]
    str w4, [x1, #0x50]
    mov w4, #0x63
    str w4, [x1, #4]
    mov w4, #7
    str w4, [x1, #0x98]
    mov w4, #1
    str w4, [x1, #0x9c]
    ldr w5, [x18]
    ldr w6, [x18, #4]
    orr x5, x5, x6, lsl #32
    mov x6, #0x100000000
    add x6, x6, #7
    sub x5, x5, x6
    ldr w6, [x18, #8]
    ldr w7, [x18, #0xc]
    orr x6, x6, x7, lsl #32
    cmp x5, x6
    b.ne failed
    // The full virtual read must preserve subtraction across the 32-bit boundary.
    ldr x6, [x18, #8]
    cmp x5, x6
    b.ne failed
    ldr x6, [x18]
    mov x7, #0x100000000
    add x7, x7, #7
    sub x6, x6, x7
    cmp x5, x6
    b.ne failed
    ldr w5, [x19]
    ldr w6, [x19, #8]
    cmp w5, w6
    b.ne failed
    ldr w5, [x18, #0x18]
    cmp w5, #7
    b.ne failed
    ldr w5, [x18, #0x1c]
    cmp w5, #1
    b.ne failed
    str wzr, [x1, #0x98]
    str wzr, [x1, #0x9c]
    // Enable secure Group 0 delivery to the EL3 FIQ vector.
    msr spsel, #1
    adr x4, vectors
    msr vbar_el3, x4
    mov x4, #0xf
    msr ICC_SRE_EL3, x4
    isb
    mrs x4, scr_el3
    orr x4, x4, #4
    msr scr_el3, x4
    isb
    mov x4, #0xff
    msr ICC_PMR_EL1, x4
    mov x4, #1
    msr ICC_IGRPEN0_EL1, x4
    mov x10, #0x17000000
    mov w4, #0x11
    str w4, [x10]
    mov w4, #0x1f00
    str w4, [x10, #0x104]
    dsb sy
    isb
    mov w12, #0
    mov w13, #40
    msr daifclr, #1
    // An expired masked timer must not deliver an interrupt.
    mov w4, #-1
    str w4, [x2, #0x28]
    mov w4, #3
    str w4, [x2, #0x2c]
    dsb sy
    mrs x5, ICC_IAR0_EL1
    cmp x5, #1023
    b.ne failed
    cbnz w12, failed
    // Unmask the expired frame and require its architectural INTID.
    mov w4, #1
    str w4, [x17, #0x2c]
4:  cmp w12, #1
    b.ne 4b
    // Let the second frame expire through the scheduled compare event.
    mov w13, #41
    mov w4, #19200
    str w4, [x3, #0x28]
    mov w4, #1
    str w4, [x0]
    str w4, [x3, #0x2c]
5:  cmp w12, #2
    b.ne 5b
    // Both handlers mask their source before EOI. Neither may repeat.
    mov w4, #1000
6:  subs w4, w4, #1
    b.ne 6b
    cmp w12, #2
    b.ne failed
    ldr w5, [x2, #0x2c]
    cmp w5, #7
    b.ne failed
    ldr w5, [x3, #0x2c]
    cmp w5, #7
    b.ne failed
    // The third frame must deliver its pending deadline after unmasking.
    mov w13, #42
    mov w4, #1
    str w4, [x16, #0x2c]
10: cmp w12, #3
    b.ne 10b
    // Both selected secure frames must deliver their own architectural interrupt.
    mov w13, #43
    mov w4, #19200
    str w4, [x18, #0x28]
    mov w4, #1
    str w4, [x18, #0x2c]
8:  cmp w12, #4
    b.ne 8b
    mov w13, #44
    mov w4, #19200
    str w4, [x19, #0x28]
    mov w4, #1
    str w4, [x19, #0x2c]
9:  cmp w12, #5
    b.ne 9b
    mov x0, #0x600d0000
    str w5, [x0]
2:  b 2b
failed:
    mov x0, #0xbad00000
    str w5, [x0]
3:  b 3b

.balign 2048
vectors:
    b failed
    .balign 128
    b failed
    .balign 128
    b failed
    .balign 128
    b failed
    .balign 128
    b failed
    .balign 128
    b failed
    .balign 128
fiq:
    mrs x14, ICC_IAR0_EL1
    cmp w14, w13
    b.ne failed
    cmp w14, #40
    mov w15, #3
    b.eq frame0
    cmp w14, #41
    b.eq frame1
    cmp w14, #42
    b.eq frame2
    cmp w14, #43
    b.eq frame3
    str w15, [x19, #0x2c]
    b complete
frame3:
    str w15, [x18, #0x2c]
    b complete
frame2:
    str w15, [x16, #0x2c]
    b complete
frame0:
    str w15, [x2, #0x2c]
    b complete
frame1:
    str w15, [x3, #0x2c]
complete:
    dsb sy
    msr ICC_EOIR0_EL1, x14
    isb
    add w12, w12, #1
    eret
    .balign 128
    b failed

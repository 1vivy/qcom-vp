// SPDX-License-Identifier: BSD-3-Clause-Clear
// Original architectural probe, not OEM firmware. Link at 0x300000.
// Contract: EL3 -> nonsecure EL2 -> EL1h, CNTV -> GIC PPI27.
// Pass: first unmapped write 0x600d0000. Fail: 0xbad00000, x28 is stage.
// Every wait has an instruction bound, including waits for a stopped counter.
// Two real IRQs are required. The first handler rearms, the second disables.
// No IRQ may repeat during a further two-period observation window.
.arch armv8-a
.section .text
.global _start

.macro bound
    mov x10, #0x1000000
.endm
.macro next_or_fail target
    subs x10, x10, #1
    b.eq fail
    b \target
.endm

_start:
    mov x28, #1
    msr daifset, #0xf
    mrs x0, CurrentEL
    cmp x0, #12
    b.ne fail
    adr x0, vectors
    msr vbar_el3, x0
    msr vbar_el2, x0
    msr vbar_el1, x0
    ldr x0, =0x14010000
    mov sp, x0
    msr sp_el2, x0
    msr sp_el1, x0
    mov x0, #0xf
    msr ICC_SRE_EL3, x0
    isb

    // Secure setup assigns PPI27 to Group1NS. Use the production GIC map.
    mov x28, #2
    mov x6, #0x17000000
    mov w0, #0x30                 // ARE_S | ARE_NS, all groups disabled.
    str w0, [x6]
    bound
1:  ldr w0, [x6]
    tbz w0, #31, 2f
    next_or_fail 1b
2:  mov x7, #0x17080000
    ldr w0, [x7, #0x14]           // GICR_WAKER
    bic w0, w0, #2
    str w0, [x7, #0x14]
    bound
3:  ldr w0, [x7, #0x14]
    tbz w0, #2, 4f
    next_or_fail 3b
4:  mov x8, x7
    add x7, x7, #0x10000          // SGI/PPI frame
    mov w0, #(1 << 27)
    str w0, [x7, #0x180]          // ICENABLER0
    str w0, [x7, #0x280]          // ICPENDR0, remove reset-state pending only.
    str w0, [x7, #0x80]           // IGROUPR0: Group1NS
    str wzr, [x7, #0xd00]         // IGRPMODR0
    ldr w1, [x7, #0xc04]          // ICFGR1: level-sensitive PPI27
    bic w1, w1, #(3 << 22)
    str w1, [x7, #0xc04]
    mov w1, #0x80
    strb w1, [x7, #0x41b]         // IPRIORITYR for INTID27
    str w0, [x7, #0x100]          // ISENABLER0
    bound
5:  ldr w0, [x8]                 // GICR_CTLR.RWP
    tbz w0, #3, 6f
    next_or_fail 5b
6:  mov w0, #0x32                 // ARE_S | ARE_NS | EnableGrp1NS
    str w0, [x6]
    dsb sy
    bound
7:  ldr w0, [x6]
    tbz w0, #31, 8f
    next_or_fail 7b
8:  // Do not route NS IRQs to EL3 or EL2. Enter each level with ERET.
    mov x0, #0x501               // SCR: NS | RW | HCE
    msr scr_el3, x0
    isb
    adr x0, el2
    msr elr_el3, x0
    mov x0, #0x3c9
    msr spsr_el3, x0
    eret

el2:
    mov x28, #3
    mrs x0, CurrentEL
    cmp x0, #8
    b.ne fail
    mov x0, #(1 << 31)           // HCR.RW, no IRQ routing or timer traps.
    msr hcr_el2, x0
    // EL1 registers are banked by security state. Set the NS bank here.
    adr x0, vectors
    msr vbar_el1, x0
    ldr x0, =0x14010000
    msr sp_el1, x0
    mov x0, #0xf                 // Enable EL1 system-register access.
    msr ICC_SRE_EL2, x0
    mov x0, #3
    msr cnthctl_el2, x0
    msr cntvoff_el2, xzr
    msr cntv_ctl_el0, xzr
    isb
    adr x0, el1
    msr elr_el2, x0
    mov x0, #0x3c5
    msr spsr_el2, x0
    eret

el1:
    mov x28, #4
    mrs x0, CurrentEL
    cmp x0, #4
    b.ne fail
    mov x0, #1
    msr ICC_SRE_EL1, x0
    isb
    mov x0, #0xff
    msr ICC_PMR_EL1, x0
    msr ICC_BPR1_EL1, xzr
    msr ICC_CTLR_EL1, xzr        // EOImode=0: EOIR drops priority and deactivates.
    mov x0, #1
    msr ICC_IGRPEN1_EL1, x0
    isb
    mrs x0, cntfrq_el0
    ldr x1, =19200000
    cmp x0, x1
    b.ne fail
    mov x20, #0                  // Completed handler count, shared with IRQ.
    mov x22, #19200              // Nominal 1 ms, not a silicon timing assertion.
    isb
    mrs x23, cntvct_el0
    add x21, x23, x22
    msr cntv_cval_el0, x21
    mrs x0, cntv_cval_el0
    cmp x0, x21
    b.ne fail
    mov x0, #3                   // ENABLE | IMASK
    msr cntv_ctl_el0, x0
    isb
    mrs x0, cntv_ctl_el0
    and x0, x0, #7
    cmp x0, #3                   // Future deadline must not already be pending.
    b.ne fail
    msr daifclr, #2              // IRQ enabled while the source remains masked.

    mov x28, #5
    bound
9:  mrs x0, cntv_ctl_el0
    tbnz x0, #2, 10f
    next_or_fail 9b
10: cbnz x20, fail
    isb
    mrs x0, cntvct_el0
    cmp x0, x23                  // Genuine virtual-counter progression.
    b.ls fail
    cmp x0, x21
    b.lo fail
    mrs x0, ICC_IAR1_EL1
    cmp x0, #1023                // Masked pending timer must not reach the GIC.
    b.ne fail
    mov x28, #6
    mov x0, #1                   // Unmask the expired source, no host injection.
    msr cntv_ctl_el0, x0
    isb
    bound
11: cmp x20, #2
    b.eq 12f
    b.hi fail
    next_or_fail 11b

12: mov x28, #7
    mrs x0, cntv_ctl_el0
    tbnz x0, #0, fail            // Second handler must disable the timer.
    isb
    mrs x23, cntvct_el0
    add x23, x23, x22, lsl #1
    bound
13: cmp x20, #2
    b.ne fail
    isb
    mrs x0, cntvct_el0
    cmp x0, x23
    b.hs 14f
    next_or_fail 13b
14: msr daifset, #2
    isb
    cmp x20, #2
    b.ne fail
    mrs x0, ICC_IAR1_EL1
    cmp x0, #1023
    b.ne fail
    mov x28, #8
    mov x0, #0x600d0000
    str x20, [x0]
    b .

fail:
    mov x0, #0xbad00000
    str x28, [x0]
    b .

// Any exception other than a same-EL EL1h IRQ fails the probe.
.balign 2048
vectors:
.rept 5
    b fail
    .balign 128
.endr
    b irq
    .balign 128
.rept 10
    b fail
    .balign 128
.endr

irq:
    stp x0, x1, [sp, #-48]!
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    mrs x0, CurrentEL
    cmp x0, #4
    b.ne fail
    mrs x4, ICC_IAR1_EL1
    cmp x4, #27
    b.ne fail
    isb
    mrs x0, cntvct_el0
    // QEMU converts ticks to integer nanoseconds. Permit one early tick.
    // Do not assert exact elapsed cycles or bound interrupt latency.
    add x0, x0, #1
    cmp x0, x21
    b.lo fail
    cbnz x20, second
    mov x28, #61
    mrs x0, cntvct_el0
    add x21, x0, x22
    msr cntv_cval_el0, x21       // First handler acknowledges by rearming.
    isb
    mrs x0, cntv_ctl_el0
    and x0, x0, #7
    cmp x0, #1
    b.ne fail
    b eoi
second:
    cmp x20, #1
    b.ne fail
    mov x28, #62
    msr cntv_ctl_el0, xzr       // Second handler removes the level source.
    isb
eoi:
    dsb sy
    msr ICC_EOIR1_EL1, x4
    isb
    add x20, x20, #1
    ldp x4, x5, [sp, #32]
    ldp x2, x3, [sp, #16]
    ldp x0, x1, [sp], #48
    eret

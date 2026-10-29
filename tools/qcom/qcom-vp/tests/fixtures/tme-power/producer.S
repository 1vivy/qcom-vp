.option norelax
.section .text
.globl _start
_start:
    li s0, 0x400000
    li s1, 0x00c22000
    li s2, 0x00c23000
    li s3, 0x01800000
    li s4, 0x01800400
    li s5, 0x00c00208
    li s6, 0x02237000
    li s7, 0x00810000
    sw zero, 0(s0)
    li t0, 0x12345678
    sw t0, 4(s0)
    csrw mscratch, t0
    # A locked, inactive PMP entry must remain locked across retained sleep.
    li t0, 0x100000
    csrw pmpaddr0, t0
    li t0, 0x80
    csrw pmpcfg0, t0
    csrw mie, zero
    csrci mstatus, 8
    la t0, trap
    ori t0, t0, 2
    csrw mtvec, t0
    la t0, resumed
    sw t0, 0(s5)
    lw t1, 0(s5)
    bne t0, t1, failed
    # Independent external wake masks retain all four words.
    li t0, -1
    sw t0, 0(s7)
    sw t0, 4(s7)
    sw t0, 8(s7)
    sw t0, 12(s7)
    lw t1, 12(s7)
    bne t0, t1, failed
    lw t1, 16(s7)
    lw t2, 20(s7)
    or t1, t1, t2
    lw t2, 24(s7)
    or t1, t1, t2
    lw t2, 28(s7)
    or t1, t1, t2
    bnez t1, failed
    # Source 22 expires first but has no CPU enable. It must not wake the CPU.
    li t0, 1
    sb t0, 23(s4)
    li t0, 6
    li t1, 0x00c00118
    sw t0, 0(t1)
    li t0, 5000
    sw t0, 0x28(s2)
    li t0, 1
    sw t0, 0x2c(s2)
    li t0, 20000
    sw t0, 0x28(s1)
    li t0, 1
    sw t0, 0x2c(s1)
    li t0, 0x21
    sw t0, 0x144(s6)
#if CASE != 1
    li t0, 0x23
    sw t0, 0(s6)
#endif
#if CASE == 2
    # Clearing the control cancels a previously armed transition.
    sw zero, 0(s6)
#endif
#if CASE == 0
    csrsi mstatus, 8
#endif
    wfi
#if CASE == 0
    # Returning here instead of the published vector is a real failure.
    ebreak
    j failed
#else
    j ordinary_return
#endif
resumed:
#if CASE != 0
    j failed
#endif
    csrr t0, mstatus
    andi t0, t0, 8
    bnez t0, failed
    lw t0, 0(s0)
    bnez t0, failed
ordinary_return:
    # The later eligible timer, not the earlier masked timer, caused wake.
    lw t0, 0x2c(s1)
    andi t0, t0, 4
    beqz t0, failed
    lbu t0, 22(s3)
    beqz t0, failed
    lbu t0, 23(s3)
    beqz t0, failed
    # External wake levels remain inactive even while CLIC sources are pending.
    lw t0, 16(s7)
    bnez t0, failed
    csrr t0, pmpcfg0
    li t1, 0x80
    bne t0, t1, failed
    csrr t0, mscratch
    li t1, 0x12345678
    bne t0, t1, failed
    lw t0, 4(s0)
    bne t0, t1, failed
    # The original restore can enable IRQs. Pending delivery was not discarded.
    csrsi mstatus, 8
wait_handler:
    lw t0, 0(s0)
    beqz t0, wait_handler
    li t1, 1
    bne t0, t1, failed
    csrci mstatus, 8
    sw zero, 0x2c(s2)
#if CASE == 0
    # No second request: ordinary WFI must return normally, not use the vector.
    la t0, unexpected_resume
    sw t0, 0(s5)
    li t0, 5000
    sw t0, 0x28(s1)
    li t0, 1
    sw t0, 0x2c(s1)
    wfi
    lbu t0, 23(s3)
    beqz t0, failed
    sw zero, 0x2c(s1)
wait_clear:
    lbu t0, 23(s3)
    bnez t0, wait_clear
    # A fresh request must use a freshly published vector.
    la t0, resumed_again
    sw t0, 0(s5)
    li t0, 5000
    sw t0, 0x28(s1)
    li t0, 1
    sw t0, 0x2c(s1)
    li t0, 0x21
    sw t0, 0x144(s6)
    li t0, 0x23
    sw t0, 0(s6)
    wfi
    ebreak
    j failed
resumed_again:
    lbu t0, 23(s3)
    beqz t0, failed
    csrr t0, pmpcfg0
    li t1, 0x80
    bne t0, t1, failed
#endif
    li t0, 0x600d0000
    sw zero, 0(t0)
1:  j 1b
.balign 4
trap:
    csrr t0, mcause
    li t1, 0x80000017
    bne t0, t1, failed
    sw zero, 0x2c(s1)
    lw t0, 0(s0)
    addi t0, t0, 1
    sw t0, 0(s0)
    mret
unexpected_resume:
failed:
    li t0, 0xbad00000
    sw zero, 0(t0)
2:  j 2b

.option norelax
.section .text
.globl _start
_start:
    li s0, 0x400000
    sw zero, 0(s0)
    li s1, 0x00c21000
    li s2, 0x01800400
    li s3, 0x01800800
    li s4, 0x01800000
    li s5, 0x01800c00
    li s6, 0x00c00100
    li s7, 0x17960014
    csrw mie, zero
    csrci mstatus, 8
    la t0, trap
    ori t0, t0, 2
    csrw mtvec, t0
    csrr t1, mtvec
    bne t0, t1, failed
    li t0, 0x80
    sb t0, 64(s3)
    sb t0, 0(s5)
    li t0, 1
    sb t0, 64(s2)
    li t0, 0x800
    sw t0, 28(s6)
    li t0, -1
    sw t0, 0x28(s1)
    li t0, 1
    sw t0, 0x2c(s1)
    # The timer publishes its level after the annotated MMIO time.
wait_initial_pending:
    lbu t1, 64(s4)
    beqz t1, wait_initial_pending
    bne t0, t1, failed
    csrsi mstatus, 8
    lw t1, 0(s0)
    bnez t1, failed
    li t1, 0x800
    sw t1, 12(s6)
    lbu t1, 64(s4)
    bne t0, t1, failed
    sw zero, 0x2c(s1)
    lbu t1, 64(s4)
    bnez t1, failed
    csrci mstatus, 8
    li t0, 0x7f
    sb t0, 0(s5)
    li t0, 1000
    sw t0, 0x28(s1)
    li t0, 1
    sw t0, 0x2c(s1)
    # WFI can resume before the timer asserts ID 64.
wait_masked_pending:
    wfi
    lw t0, 0(s0)
    bnez t0, failed
    lbu t0, 64(s4)
    beqz t0, wait_masked_pending
    li t1, 1
    bne t0, t1, failed
    csrsi mstatus, 8
1:  lw t0, 0(s0)
    beqz t0, 1b
    li t1, 64
    bne t0, t1, failed
    csrci mstatus, 8
    # Select the timer and QMP sources above the zero threshold.
    sb zero, 0(s5)
    li t0, 0x40
    sb t0, 28(s3)
    sb t0, 33(s3)
    sb t0, 64(s3)
    li t0, 1
    sb t0, 28(s2)
    sb t0, 33(s2)
    sb t0, 64(s2)
    li t0, 0x1080
    sw t0, 24(s6)
    li t0, 0x800
    sw t0, 28(s6)
    # An outgoing peer command must not change the local receive state.
    sw zero, 0(s0)
    csrsi mstatus, 8
    sb zero, 0(s6)
    li t0, 3
    sb t0, 0(s6)
    sb t0, 0(s6)
    lw t0, 0(s0)
    bnez t0, failed
    lw t0, 16(s6)
    bnez t0, failed
    lbu t0, 28(s4)
    bnez t0, failed
    lw t0, 32(s4)
    bnez t0, failed
    csrci mstatus, 8
    # The APSS QMP write is the receive input. Repeated writes coalesce.
    sw zero, 0(s7)
    lw t0, 16(s6)
    bnez t0, failed
    li t1, 1
    sw t1, 0(s7)
    sw t1, 0(s7)
    lw t0, 16(s6)
    li t1, 0x80
    bne t0, t1, failed
    lbu t0, 28(s4)
    li t1, 1
    bne t0, t1, failed
    lw t0, 32(s4)
    bnez t0, failed
    # Packed enable-bank writes do not acknowledge the pending source.
    sw zero, 28(s2)
    lw t0, 28(s2)
    bnez t0, failed
    lw t0, 16(s6)
    li t1, 0x80
    bne t0, t1, failed
    li t0, 1
    sw t0, 28(s2)
    lw t1, 28(s2)
    bne t1, t0, failed
    # A cleared source mask hides pending state but keeps the latch.
    sw zero, 24(s6)
    lw t0, 24(s6)
    bnez t0, failed
    lbu t0, 28(s4)
    bnez t0, failed
    lw t0, 16(s6)
    li t1, 0x80
    bne t0, t1, failed
    li t0, 0x1080
    sw t0, 24(s6)
    lw t0, 24(s6)
    li t1, 0x1080
    bne t0, t1, failed
    lbu t0, 28(s4)
    li t1, 1
    bne t0, t1, failed
    # Arm the timer source. Equal priorities resolve to the larger ID.
    li t0, -1
    sw t0, 0x28(s1)
    li t0, 1
    sw t0, 0x2c(s1)
wait_tie_pending:
    lbu t0, 64(s4)
    beqz t0, wait_tie_pending
    li t1, 1
    bne t0, t1, failed
    lbu t0, 28(s4)
    bne t0, t1, failed
    csrsi mstatus, 8
2:  lw t0, 0(s0)
    beqz t0, 2b
    li t1, 64
    beq t0, t1, 2b
    li t1, 0x401c
    bne t0, t1, failed
    csrci mstatus, 8
    lw t0, 16(s6)
    bnez t0, failed
    lbu t0, 28(s4)
    bnez t0, failed
    # A new incoming notification after guest W1C asserts again.
    sw zero, 0(s0)
    li t1, 1
    sw t1, 0(s7)
    sw t1, 0(s7)
    csrsi mstatus, 8
4:  lw t0, 0(s0)
    beqz t0, 4b
    li t1, 28
    bne t0, t1, failed
    csrci mstatus, 8
    lw t0, 16(s6)
    bnez t0, failed
    lbu t0, 28(s4)
    bnez t0, failed
    li t0, 0x600d0000
    sw zero, 0(t0)
6:  j 6b
.balign 4
trap:
    csrr a0, mcause
    bgez a0, failed
    slli a0, a0, 1
    srli a0, a0, 1
    lw a1, 0(s0)
    slli a1, a1, 8
    or a1, a1, a0
    sw a1, 0(s0)
    li a1, 64
    beq a0, a1, timer_interrupt
    li a1, 33
    beq a0, a1, software_high
    li a1, 28
    bne a0, a1, failed
    li a1, 0x80
    sw a1, 8(s6)
    mret
software_high:
    li a1, 0x1000
    sw a1, 8(s6)
    mret
timer_interrupt:
    sw zero, 0x2c(s1)
    mret
failed:
    li t0, 0xbad00000
    sw zero, 0(t0)
5:  j 5b

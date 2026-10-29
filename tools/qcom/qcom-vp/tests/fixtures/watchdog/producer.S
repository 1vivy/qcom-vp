.option norelax
.section .text
.globl _start
_start:
    li sp, 0x401000
    li s0, 0xc24000
    li s1, 0xc25000
    li s2, 0
    li s3, 0
#if PACKED_STATUS
    # QHEE's write-only chicken configuration must not pet or enable the timer.
    li t0, 15
    sw t0, 32(s0)
    lw t0, 8(s0)
    bnez t0, failure
#endif
    csrw mie, zero
    csrci mstatus, 8
    la t0, trap
    ori t0, t0, 2
    csrw mtvec, t0
    li t0, 0x1800400
    li t1, 1
    sb t1, 27(t0)
    sb t1, 28(t0)
    li t0, 0x1800800
    sb t1, 27(t0)
    sb t1, 28(t0)
    li t0, 0xc00118
    li t1, 0xc0
    sw t1, 0(t0)
    li t0, 4
    sw t0, 12(s0)
    li t0, 40
    sw t0, 16(s0)
    li t0, 7
    sw t0, 4(s1)
    sw t0, 4(s0)
    csrsi mstatus, 8
    li t2, 1
first_bark:
    bne s2, t2, first_bark
    li t0, 6
    sw t0, 4(s0)
    lw s4, 8(s0)
    lw s5, 8(s1)
wait_disabled:
    lw t0, 8(s1)
    sub t0, t0, s5
    li t1, 5
    bltu t0, t1, wait_disabled
    lw t0, 8(s0)
    bne t0, s4, failure
    li t0, 1
    bne s2, t0, failure
    bnez s3, failure
    li t0, 7
    sw t0, 4(s0)
    li t2, 2
second_bark:
    bne s2, t2, second_bark
    li t0, 6
    sw t0, 4(s0)
    li t0, 20
    sw t0, 12(s0)
    li t0, 3
    sw t0, 16(s0)
    li t0, 1
    sw t0, 0(s0)
    li t0, 7
    sw t0, 4(s0)
wait_bite:
    beqz s3, wait_bite
    li t0, 2
    bne s2, t0, failure
    li t0, 1
    bne s3, t0, failure
    lw t0, 8(s0)
#if PACKED_STATUS
    srli t0, t0, 1
#endif
    li t1, 3
    bltu t0, t1, failure
    li t0, 0x600d0000
    lw zero, 0(t0)
failure:
    li t0, 0xbad00000
    lw zero, 0(t0)
    j failure

.balign 4
trap:
    addi sp, sp, -16
    sw t0, 0(sp)
    sw t1, 4(sp)
    csrr t0, mcause
    li t1, 0x8000001b
    beq t0, t1, bark
    li t1, 0x8000001c
    bne t0, t1, failure
    addi s3, s3, 1
    li t0, 6
    sw t0, 4(s0)
    j return
bark:
    lw t0, 8(s0)
#if PACKED_STATUS
    andi t1, t0, 1
    beqz t1, failure
    srli t0, t0, 1
#endif
    li t1, 4
    bltu t0, t1, failure
    addi s2, s2, 1
    li t0, 1
    sw t0, 0(s0)
#if PACKED_STATUS
    lw t0, 8(s0)
    andi t0, t0, 1
    bnez t0, failure
#endif
return:
    lw t0, 0(sp)
    lw t1, 4(sp)
    addi sp, sp, 16
    mret

.option norelax
.section .text
.globl _start
_start:
    li sp, 0x401000
    li s0, 0x02230000
    li s1, 0x01800000
    li s2, 0
    csrw mie, zero
    csrci mstatus, 8
    la t0, trap
    ori t0, t0, 2
    csrw mtvec, t0
    li t0, 0x01800400
    li t1, 1
    sb t1, 23(t0)
    li t0, 0x01800800
    sb t1, 23(t0)
    li t0, 0x00c00100
    li t1, 4
    sw t1, 24(t0)
    li t0, 1
    sw t0, 0x12c(s0)
    lbu t0, 23(s1)
    bnez t0, failed
    sw zero, 0x12c(s0)
    lw t0, 0x12c(s0)
    bnez t0, failed
    csrsi mstatus, 8
    call submit
    lw t0, 0x128(s0)
    li t1, 1
    bne t0, t1, failed
    lbu t0, 23(s1)
    bnez t0, failed
    bnez s2, failed
    sw t1, 0x12c(s0)
    li t2, 1
1:  bne s2, t2, 1b
    lw t0, 0x128(s0)
    bnez t0, failed
    csrci mstatus, 8
    call submit
    // Completion status is synchronous. The IRQ crosses a SystemC delta event.
    // Observe assertion before testing its removal by the sequencer mask.
wait_pending:
    lbu t0, 23(s1)
    beqz t0, wait_pending
    li t1, 1
    bne t0, t1, failed
    sw zero, 0x12c(s0)
wait_masked:
    lbu t0, 23(s1)
    bnez t0, wait_masked
    lw t0, 0x128(s0)
    bne t0, t1, failed
    sw t1, 0x12c(s0)
    csrsi mstatus, 8
    li t2, 2
2:  bne s2, t2, 2b
    call submit
    li t2, 3
3:  bne s2, t2, 3b
    lw t0, 0x128(s0)
    bnez t0, failed
    lbu t0, 23(s1)
    bnez t0, failed
    li t0, 0x600d0000
    sw zero, 0(t0)
4:  j 4b
submit:
    li t0, 0x100
    sw t0, 0x60(s0)
    li t0, 0x13
    sw t0, 0(s0)
    li t0, 1
    sw t0, 4(s0)
    sw zero, 8(s0)
    sw zero, 12(s0)
    li t0, 0x200
    sw t0, 0x60(s0)
    ret
.balign 4
trap:
    addi sp, sp, -16
    sw t0, 0(sp)
    sw t1, 4(sp)
    csrr t0, mcause
    li t1, 0x80000017
    bne t0, t1, failed
    lw t0, 0x128(s0)
    li t1, 1
    bne t0, t1, failed
    lw t0, 0x144(s0)
    li t1, 0x100
    bne t0, t1, failed
    li t0, 1
    sw t0, 0x130(s0)
    // ACK clears status now, but IRQ deassertion uses the scheduled event.
    // Do not return with the old level or publish completion before it clears.
wait_ack:
    lbu t0, 23(s1)
    bnez t0, wait_ack
    lw t0, 0x128(s0)
    bnez t0, failed
    addi s2, s2, 1
    lw t0, 0(sp)
    lw t1, 4(sp)
    addi sp, sp, 16
    mret
failed:
    li t0, 0xbad00000
    sw zero, 0(t0)
5:  j 5b

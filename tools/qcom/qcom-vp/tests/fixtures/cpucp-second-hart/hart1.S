// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.option norvc
.globl _start
_start:
    // The secondary starts only after the controller releases its native cold hold.
    csrr t0, mhartid
    li t1, 1
    bne t0, t1, failure
    li s0, 0x14800000
wait_commit:
    lw t0, 0(s0)
    andi t1, t0, 1
    beqz t1, wait_commit
    la t0, interrupt
    csrw mtvec, t0
    li t0, 8
    csrs mie, t0
    li t1, 0x18b80004
    li t2, 1
    sw t2, 0(t1)
    csrs mstatus, t0
spin:
    wfi
    j spin
.balign 4
interrupt:
    csrr t0, mcause
    li t1, 0x8000000000000003
    bne t0, t1, failure
    li t1, 0x18b80004
    sw zero, 0(t1)
    lw t2, 0(t1)
    bnez t2, failure
    li s0, 0x14800000
    lw t0, 0(s0)
    ori t0, t0, 2
    sw t0, 0(s0)
    mret
failure:
    li s0, 0xbad00000
    sw t0, 0(s0)
1:  j 1b

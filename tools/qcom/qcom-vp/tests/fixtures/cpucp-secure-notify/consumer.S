// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.option norvc
.globl _start
_start:
    li s0, 0x14800000
    li s1, 0x18875000
    li s2, 0
    li s3, 0x200000
    la t0, trap
    csrw mtvec, t0
    li t0, 0x18876104
    sw zero, 0(t0)
    li t1, 0x80000002
    sw t1, 0(t0)
    // Source117 has priority1. Its enable bit stays masked for the first rings.
    li t0, 0x100000
    sw t0, 0x60(s1)
    sw zero, 0xe8(s1)
    li t0, 0x10000
    csrw mie, t0
    li t0, 1
    sw t0, 0(s0)
1:  lw t0, 0(s0)
    li t1, 2
    bne t0, t1, 1b
    // Masking retains pending. Neither request data nor FREE changes.
    csrr t0, mip
    li t1, 0x10000
    and t0, t0, t1
    bnez t0, failure
    lw t0, 0x40(s0)
    li t1, 0x1357
    bne t0, t1, failure
    lw t0, 0x44(s0)
    bnez t0, failure
    sw s3, 0xb4(s1)
    csrsi mstatus, 8
2:  li t0, 2
    bne s2, t0, 2b
    csrci mstatus, 8
    // No third interrupt remains after the second claim and completion.
    lw t0, 0xf0(s1)
    bnez t0, failure
    li t0, 6
    sw t0, 0(s0)
3:  j 3b
trap:
    csrr t0, mcause
    li t1, 1
    slli t1, t1, 63
    ori t1, t1, 16
    bne t0, t1, failure
    lw t0, 0xf0(s1)
    li t1, 118
    bne t0, t1, failure
    addi s2, s2, 1
    li t0, 1
    bne s2, t0, second
    li t0, 3
    sw t0, 0(s0)
4:  lw t0, 0(s0)
    li t1, 4
    bne t0, t1, 4b
    // An in-service source cannot be claimed again before completion.
    lw t0, 0xf0(s1)
    bnez t0, failure
    lw t0, 0x44(s0)
    bnez t0, failure
    sw zero, 0xf0(s1)
    mret
second:
    li t0, 2
    bne s2, t0, failure
    // Only this guest instruction sequence produces the response and FREE word.
    li t0, 0x2468
    sw t0, 0x40(s0)
    fence rw, rw
    li t0, 1
    sw t0, 0x44(s0)
    sw zero, 0xf0(s1)
    mret
failure:
    li t0, -1
    sw t0, 0(s0)
5:  j 5b

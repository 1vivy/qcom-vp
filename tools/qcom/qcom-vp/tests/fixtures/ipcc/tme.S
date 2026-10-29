// SPDX-License-Identifier: BSD-3-Clause-Clear
.option norelax
.section .text
.globl _start
_start:
    li s0, 0x400000
    li s1, 0x05110000
    csrw mie, zero
    csrci mstatus, 8
    la t0, trap
    ori t0, t0, 2
    csrw mtvec, t0
    lw t0, 0(s1)
    li t1, 0x30000
    bne t0, t1, failed
    lw t0, 4(s1)
    li t1, 0x100000
    bne t0, t1, failed
    li t0, 1
    sw t0, 0x38(s1)
    sw t0, 8(s1)
    li t1, 0x051cf008
    sw t0, 0(t1)
    li t0, 0x60003
    sw t0, 0x14(s1)
    li t0, 0x01800400
    li t1, 1
    sb t1, 38(t0)
    li t0, 0x00c00118
    li t1, 0x20000
    sw t1, 0(t0)
    csrsi mstatus, 8
    li t0, 1
    sw t0, 0(s0)
wait:
    wfi
    j wait
.balign 4
trap:
    csrr t0, mcause
    li t1, 0x80000026
    bne t0, t1, failed
    lw t0, 0x48(s1)
    li t1, 0x60003
    bne t0, t1, failed
    lw t0, 0x48(s1)
    li t1, -1
    bne t0, t1, failed
    li t0, 0x051cf008
    lw t0, 0(t0)
    li t1, 0x2a
    bne t0, t1, failed
    li t0, 38
    sw t0, 4(s0)
    li t0, 0x60004
    sw t0, 0xc(s1)
    mret
failed:
    li t0, 0xbad00000
    sw zero, 0(t0)
1:  j 1b

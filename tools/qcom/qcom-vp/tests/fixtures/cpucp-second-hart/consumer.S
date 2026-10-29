// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.option norvc
.globl _start
_start:
    csrr t0, mhartid
    bnez t0, failure
    li s1, 0x14800000
wait_vector:
    lw t0, 4(s1)
    beqz t0, wait_vector
    // Reproduce the recovered OEM sequence at DDR 0x812d41de exactly: clear the
    // virtual cold inhibit (bit 0), set the commit bit (bit 1), poll until the
    // model accepts it, then publish the commit latch (bit 31).
    li s0, 0x18876104
    lw t0, 0(s0)
    andi t0, t0, -2
    sw t0, 0(s0)
    lw t0, 0(s0)
    ori t0, t0, 2
    sw t0, 0(s0)
poll:
    lw t0, 0(s0)
    andi t1, t0, 2
    beqz t1, poll
    lw t0, 0(s0)
    lui t1, 0x80000
    or t0, t0, t1
    sw t0, 0(s0)
    // Mark hart 0's real completion. Only this hart ever sets bit 0.
    li s1, 0x14800000
    li t0, 1
    sw t0, 0(s1)
spin:
    j spin
failure:
    li t0, 0xbad00000
    sw zero, 0(t0)
    j failure

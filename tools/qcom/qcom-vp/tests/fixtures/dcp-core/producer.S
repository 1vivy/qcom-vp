// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.option norvc
.global _start
_start:
    la t0, trap
    csrw mtvec, t0
    li a0, 0xa4200000
    li t0, 0x24681357
    sw t0, 0(a0)
    li s2, 0
#if CASE == 2
    li t0, 0x29080001
    csrw pmpaddr0, t0
    li t0, 0x99
    csrw pmpcfg0, t0
#endif
    la s1, flush_all
flush_all:
    .word 0xfc000073
    la s1, flush_line
flush_line:
    .word 0xfc050073
    la s1, discard_all
discard_all:
    .word 0xfc200073
    la s1, discard_line
discard_line:
    .word 0xfc250073
    lw t0, 0(a0)
    li t1, 0x24681357
    bne t0, t1, failed
#if CASE == 1
    li t0, 4
#elif CASE == 2
    li t0, 2
#else
    li t0, 0
#endif
    bne s2, t0, failed
    li t0, 0x600d0000
    lw zero, 0(t0)
trap:
    csrr t0, mepc
    bne t0, s1, failed
    csrr t0, mcause
#if CASE == 1
    li t1, 2
    bne t0, t1, failed
    csrr t0, mtval
    lw t1, 0(s1)
    bne t0, t1, failed
#elif CASE == 2
    li t1, 7
    bne t0, t1, failed
    csrr t0, mtval
    bne t0, a0, failed
#else
    j failed
#endif
    addi s2, s2, 1
    addi t0, s1, 4
    csrw mepc, t0
    mret
failed:
    li t0, 0xbad00000
    lw zero, 0(t0)

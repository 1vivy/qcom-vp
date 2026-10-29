// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.option norvc
.globl _start
_start:
    li s0, 0x14800000
    lw t0, 0(s0)
    bnez t0, failure
    jal s7, checks
    li t0, 1
    sw t0, 0(s0)
    fence rw, rw
    .word 0x30500073
    j failure
.org 0x100
restarted:
    li s0, 0x14800000
    lw t0, 0(s0)
    li t1, 1
    bne t0, t1, failure
    jal s7, checks
    li t0, 2
    sw t0, 0(s0)
    fence rw, rw
    .word 0x30500073
    j failure
checks:
    la t0, trap
    csrw mtvec, t0
    li s1, 0
#ifdef DISABLED
    // Neither the legacy window nor MNRET exists on the default RV64 CPU.
    csrr t0, 0x350
    csrr t0, 0x351
    csrr t0, 0x352
    csrr t0, 0x353
    .word 0x70200073
    li t0, 5
    bne s1, t0, failure
#else
    // Repeat after external reset, not just the first power-on reset.
    csrr t0, 0x353
    li t1, 0x1808
    bne t0, t1, failure
    li t0, 0x123456789abcdef0
    csrw 0x350, t0
    csrr t1, 0x740
    bne t0, t1, failure
    li t2, 0x10
    csrrc t1, 0x350, t2
    bne t0, t1, failure
    xor t0, t0, t2
    csrr t1, 0x350
    bne t0, t1, failure
    li t0, -1
    csrw 0x352, t0
    csrr t1, 0x352
    li t2, 0x8000000000000003
    bne t1, t2, failure
    csrr t1, 0x742
    bne t1, t2, failure
    csrw 0x353, zero
    csrr t1, 0x353
    li t2, 8
    bne t1, t2, failure
    // MPV shares bit 7 with Smrnmi, but exists only with H.
    li t0, 0x1888
    csrw 0x353, t0
    csrr t1, misa
    andi t1, t1, 0x80
    li t2, 0x1808
    or t2, t2, t1
    csrr t1, 0x353
    bne t1, t2, failure
    csrr t1, 0x744
    bne t1, t2, failure
    // A modern-only MNPELP bit must not leak through legacy MNSTATUS.
    li t0, 0x1a08
    csrw 0x353, t0
    csrr t1, 0x744
    li t0, 0x1808
    bne t0, t1, failure
    ecall
    li t0, 1
    bne s1, t0, failure
    // A real pending supervisor software interrupt reaches the M trap handler.
    li t0, 2
    csrw mie, t0
    csrs mip, t0
    csrsi mstatus, 8
1:  li t0, 2
    bne s1, t0, 1b
    csrci mstatus, 8
    la t0, returned
    ori t0, t0, 1
    csrw 0x351, t0
    csrr t1, 0x351
    andi t0, t0, -2
    bne t0, t1, failure
    csrr t1, 0x741
    bne t0, t1, failure
    .word 0x70200073
    j failure
returned:
    csrr t0, 0x353
    li t1, 0x1808
    bne t0, t1, failure
#endif
    jr s7
.balign 4
trap:
    csrr t0, mcause
#ifdef DISABLED
    li t1, 2
    bne t0, t1, failure
#else
    beqz s1, synchronous
    li t1, 0x8000000000000001
    bne t0, t1, failure
    li t0, 2
    csrc mip, t0
    csrw mie, zero
    addi s1, s1, 1
    mret
synchronous:
    li t1, 11
    bne t0, t1, failure
#endif
    csrr t0, mepc
    addi t0, t0, 4
    csrw mepc, t0
    addi s1, s1, 1
    mret
failure:
    li t0, 0xbad
    sw t0, 0(s0)
2:  j 2b

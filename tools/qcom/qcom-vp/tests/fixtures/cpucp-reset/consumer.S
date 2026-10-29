.section .text
.globl _start
_start:
    li t0, 0x14800000
    lw t1, 0(t0)
    bnez t1, failure
    li t1, 2
    csrw mie, t1
    csrw mip, t1
    csrr t2, mip
    and t2, t2, t1
    beqz t2, failure
    li t1, 1
    sw t1, 0(t0)
    fence rw, rw
    .word 0x30500073
failure:
    li t0, 0x14800000
    li t1, 0xbad
    sw t1, 0(t0)
1:  j 1b
.org 0x100
restarted:
    li t0, 0x14800000
    lw t1, 0(t0)
    li t2, 1
    bne t1, t2, failure
    li t1, 2
    sw t1, 0(t0)
    fence rw, rw
    .word 0x30500073
    j failure

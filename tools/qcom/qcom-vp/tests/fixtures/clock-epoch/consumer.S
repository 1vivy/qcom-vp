.section .text
.globl _start
_start:
    li t0, 0x400000
    li t1, 1
    sw t1, 8(t0)
1:  lw t2, 0(t0)
    bne t2, t1, 1b
    fence rw, rw
    sw t2, 4(t0)
    li t1, 2
2:  lw t2, 0(t0)
    bne t2, t1, 2b
    fence rw, rw
    sw t2, 4(t0)
3:  j 3b

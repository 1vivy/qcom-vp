.option norvc
.section .text
.globl _start
_start:
    li s0, 0x402000
    li s1, 0x401000
1:  lw t0, 8(s0)
    beqz t0, 1b
    jalr ra, s1, 0
    li t0, 1
    bne a0, t0, failed
    sw t0, 0(s0)
2:  lw t0, 4(s0)
    beqz t0, 2b
    jalr ra, s1, 0
    li t0, 2
    bne a0, t0, failed
    li t1, 0x600d0000
    sw a0, 0(t1)
3:  j 3b
failed:
    li t1, 0xbad00000
    sw a0, 0(t1)
4:  j 4b

// SPDX-License-Identifier: BSD-3-Clause-Clear
.option norvc
.section .text
.globl _start
_start:
    li s0, 0x80002000
    li s1, 0x80001000
1:  lw t0, 8(s0)
    beqz t0, 1b
    jalr ra, s1, 0
    li t0, 1
    bne a0, t0, failed
    // This page already has DMI. A read-only grant discards this store.
#ifdef OWNER
    sw t0, 12(s0)
#else
    sw t0, 0(s0)
#endif
2:  lw t0, 4(s0)
    beqz t0, 2b
    // No fence.i hides a missing foreign-write invalidation.
    jalr ra, s1, 0
    li t0, 2
    bne a0, t0, failed
#ifdef OWNER
    sw t0, 16(s0)
#else
5:  lw t0, 16(s0)
    beqz t0, 5b
    li t1, 0x600d0000
    sw a0, 0(t1)
#endif
3:  j 3b
failed:
    li t1, 0xbad00000
    sw a0, 0(t1)
4:  j 4b

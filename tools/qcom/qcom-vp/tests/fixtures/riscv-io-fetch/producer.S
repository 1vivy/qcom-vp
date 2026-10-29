// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.option rvc
.org 0xff8
.global _start
_start:
    .option push
    .option norvc
    addi s0, zero, 40
    .option pop
    c.addi s0, 1
    c.addi s0, 1
    li t0, 42
    bne s0, t0, failed
    li t0, 0x600d0000
    lw zero, 0(t0)
failed:
    li t0, 0xbad00000
    lw zero, 0(t0)

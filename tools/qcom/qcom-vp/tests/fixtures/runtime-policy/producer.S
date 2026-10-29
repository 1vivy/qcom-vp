// SPDX-License-Identifier: BSD-3-Clause-Clear
.option norelax
.section .text
.globl _start
_start:
    li sp, 0x400ff0
    li s2, 0x400000
    li t0, 0x416800
    la t1, context_get
    sw t1, 0x198(t0)
    la t1, processed_get
    sw t1, 0x208(t0)
    la t1, mask_get
    sw t1, 0x1f8(t0)
    li t0, 0x12345678
    sw t0, 0x500(s2)
    li t0, 0x9abcdef0
    sw t0, 0x504(s2)
    la s0, cases
    la s1, cases_end
next_case:
    addi t0, s2, 0x100
    addi t1, t0, 0x29c
clear_context:
    sw zero, 0(t0)
    addi t0, t0, 4
    bne t0, t1, clear_context
    lw t0, 0(s0)
    addi t0, t0, 0x100
    add t0, t0, s2
    lw t1, 4(s0)
    sw t1, 0(t0)
    lw t0, 8(s0)
    sw t0, 0x400(s2)
    lw t0, 12(s0)
    sw t0, 0x404(s2)
    lw t0, 16(s0)
    sw t0, 0x408(s2)
    addi a0, s2, 0x500
    addi a1, s2, 0x504
    lw t0, 24(s0)
    andi t1, t0, 1
    beqz t1, first_output_ready
    li a0, 0
first_output_ready:
    andi t1, t0, 2
    beqz t1, second_output_ready
    li a1, 0
second_output_ready:
    call legacy_empty_dump_policy
    lw t0, 20(s0)
    bne a0, t0, fail
    // Failure must not manufacture a policy pointer or length.
    lw t0, 0x500(s2)
    li t1, 0x12345678
    bne t0, t1, fail
    lw t0, 0x504(s2)
    li t1, 0x9abcdef0
    bne t0, t1, fail
    addi s0, s0, 28
    bne s0, s1, next_case
    li t0, 0x600d0000
    lw t0, 0(t0)
fail:
    li t0, 0xbad00000
    lw t0, 0(t0)
    j fail
context_get:
    li t0, 0x400408
    lw a0, 0(t0)
    ret
processed_get:
    li t0, 0x400400
    lw a0, 0(t0)
    ret
mask_get:
    li t0, 0x400404
    lw a0, 0(t0)
    ret
.balign 4
cases:
    // Each row selects the context word, processed state, grant mask, context pointer, expected result, and null outputs.
    .word 0,     0, 0, 0, 0x400100, 0x4006, 0
    .word 4,     1, 0, 0, 0x400100, 0x4005, 0
    .word 0x298, 1, 0, 0, 0x400100, 0x4005, 0
    .word 0,     0, 1, 0, 0x400100, 0x4005, 0
    .word 0,     0, 0, 1, 0x400100, 0x4005, 0
    .word 0,     0, 0, 0, 0,        0x4005, 0
    .word 0,     0, 0, 0, 0x400101, 0x4005, 0
    .word 0,     0, 0, 0, 0x400100, 0x4002, 1
    .word 0,     0, 0, 0, 0x400100, 0x4002, 2
cases_end:
.set runtime_entry, _start
.set runtime_oem_resume, _start
#define _start unused_runtime_adapter_entry
#include "../../../../../../firmware/qcom/sm88xx/tme-runtime/tme-runtime-adapter.S"
#undef _start

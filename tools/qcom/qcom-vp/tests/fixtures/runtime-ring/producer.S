// SPDX-License-Identifier: BSD-3-Clause-Clear
.option norelax
.section .text
.globl _start
_start:
    li sp, 0x400ff0
    li s0, 0x400000
    li t0, 2
    sw t0, 4(s0)
    la s1, cases
    la s2, cases_end
next_case:
    lw t0, 0(s1)
    lw t1, 12(s1)
    sw t1, 0x104(s0)
    sw t0, 8(s0)
    addi t0, t0, 1
    sw t0, 0x108(s0)
    mv a0, s0
    addi a1, s0, 0x100
    addi a2, s0, 0x300
    addi a3, s0, 0x304
    call legacy_pending_request
    li t0, 1
    bne a0, t0, fail
    lw t0, 4(s1)
    addi t0, t0, 0x100
    add t0, t0, s0
    lw t1, 0x300(s0)
    bne t0, t1, fail
    lw t0, 8(s1)
    lw t1, 0x304(s0)
    bne t0, t1, fail
    // Selection must not consume or publish a request.
    lw t0, 0(s1)
    lw t1, 8(s0)
    bne t0, t1, fail
    addi t0, t0, 1
    lw t1, 0x108(s0)
    bne t0, t1, fail
    // Release queries must select every declared request slot.
    lw a0, 0(s1)
    addi a0, a0, 1
    addi a1, s0, 0x100
    addi a2, s0, 0x308
    call legacy_request_index
    bnez a0, fail
    lw t0, 4(s1)
    addi t0, t0, -16
    li t1, 20
    divu t0, t0, t1
    lw t1, 0x308(s0)
    bne t0, t1, fail
    addi s1, s1, 16
    bne s1, s2, next_case
    // An empty ring must not return the previous request.
    lw t0, 8(s0)
    sw t0, 0x108(s0)
    mv a0, s0
    addi a1, s0, 0x100
    addi a2, s0, 0x300
    addi a3, s0, 0x304
    call legacy_pending_request
    bnez a0, fail
    lw t0, 0x300(s0)
    bnez t0, fail
    li t0, -1
    sw t0, 0x308(s0)
    li a0, 0
    addi a1, s0, 0x100
    addi a2, s0, 0x308
    call legacy_request_index
    li t0, 6
    bne a0, t0, fail
    lw t0, 0x308(s0)
    li t1, -1
    bne t0, t1, fail
    li t0, 0x600d0000
    lw t0, 0(t0)
fail:
    li t0, 0xbad00000
    lw t0, 0(t0)
    j fail
.balign 4
cases:
    // The initial two-slot protocol precedes the XBL twenty-slot protocol.
    .word 12, 16, 0, 2
    // Third request, final request slot, and first request after both rings wrap.
    .word 2, 56, 0, 20
    .word 4, 96, 0, 20
    .word 19, 396, 1, 20
    .word 20, 16, 0, 20
cases_end:
.set runtime_entry, _start
.set runtime_oem_resume, _start
#define _start unused_runtime_adapter_entry
#include "../../../../../../firmware/qcom/sm88xx/tme-runtime/tme-runtime-adapter.S"
#undef _start

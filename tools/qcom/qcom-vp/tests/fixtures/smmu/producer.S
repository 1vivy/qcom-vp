// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    msr daifset, #0xf
    msr spsel, #1
    mov x0, #0x14820000
    mov sp, x0
    adr x0, vectors
    msr vbar_el3, x0
    isb
    bl test
    b unexpected

// Return zero on success and one on an expected synchronous data abort.
// Only these two load/store instructions may recover from an abort.
.globl probe_read
probe_read:
    mov x2, #0
    mov x3, #0
read_instruction:
    ldr w3, [x0]
    str w3, [x1]
    mov x0, x2
    ret
.globl probe_write
probe_write:
    mov x2, #0
write_instruction:
    str w1, [x0]
    mov x0, x2
    ret

.balign 2048
vectors:
.rept 4
    b unexpected
    .balign 128
.endr
    b data_abort
    .balign 128
.rept 11
    b unexpected
    .balign 128
.endr

data_abort:
    mrs x16, esr_el3
    lsr x16, x16, #26
    cmp x16, #0x25
    b.ne unexpected
    mrs x16, elr_el3
    adr x17, read_instruction
    cmp x16, x17
    b.eq recover
    adr x17, write_instruction
    cmp x16, x17
    b.ne unexpected
recover:
    mov x2, #1
    add x16, x16, #4
    msr elr_el3, x16
    eret
unexpected:
    mov x16, #0xbad00000
    str wzr, [x16]
1:  b 1b

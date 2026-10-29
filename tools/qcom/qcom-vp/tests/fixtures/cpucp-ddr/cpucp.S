// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "layout.h"
.section .text
.option norvc
.globl _start
.macro check address, size, value
    li t1, \address
    li t2, \size
    li t3, \value
1:  ld t0, 0(t1)
    bne t0, t3, fail
    addi t1, t1, 8
    addi t2, t2, -8
    bnez t2, 1b
.endm
.macro fill address, size, value
    li t1, \address
    li t2, \size
    li t3, \value
1:  sd t3, 0(t1)
    addi t1, t1, 8
    addi t2, t2, -8
    bnez t2, 1b
.endm
.macro copy source, destination
    li t1, \source
    li t2, \destination
    li t3, SPAN
1:  ld t0, 0(t1)
    sd t0, 0(t2)
    addi t1, t1, 8
    addi t2, t2, 8
    addi t3, t3, -8
    bnez t3, 1b
.endm
.macro same source, destination
    li t1, \source
    li t2, \destination
    li t3, SPAN
1:  ld t0, 0(t1)
    ld t4, 0(t2)
    bne t0, t4, fail
    addi t1, t1, 8
    addi t2, t2, 8
    addi t3, t3, -8
    bnez t3, 1b
.endm
.macro publish value
    li t0, \value
    fence rw, rw
    sw t0, 4(s0)
.endm
.macro wait value
1:  lw t0, 8(s0)
    li t1, \value
    bne t0, t1, 1b
.endm
.macro execute value
    // Foreign DDR writes must invalidate already executed instructions.
    jalr s1
    li t0, \value
    bne a0, t0, fail
    publish \value
.endm
.macro image
    jalr s2
    li t0, 85
    bne a0, t0, fail
.endm
_start:
    li s0, FLAGS
    li s1, CODE
    li s2, IMAGE
    li sp, STACK_TOP
    lw t0, 16(s0)
    beqz t0, cold
    li t1, 1
    bne t0, t1, fail
    li t0, 2
    sw t0, 16(s0)
    j resumed
cold:
    check LOCAL_RAM, SPAN, 0
    check ELF_ZERO0, ELF_ZERO0_SIZE, 0
    check ELF_ZERO1, ELF_ZERO1_SIZE, 0
    li t0, 1
    sw t0, 12(s0)
1:  lw t0, 0(s0)
    beqz t0, 1b
    // Loader writes into DDR must not populate or clear local SRAM.
    check LOCAL_RAM, SPAN, 0
    check ELF_ZERO0, ELF_ZERO0_SIZE, 0x1111111111111111
    check ELF_ZERO1, ELF_ZERO1_SIZE, 0x2222222222222222
    fill LOCAL_RAM, SPAN, LOCAL_PATTERN
    image
    jal ra, poison_buffers
    // Restore the scratch stack word after the real call returned.
    li t0, LOCAL_PATTERN
    sd t0, -8(sp)
    check LOCAL_RAM, SPAN, LOCAL_PATTERN
    image
    execute 1
    wait 2
    check LOCAL_RAM, SPAN, LOCAL_PATTERN
    check ELF_ZERO0, ELF_ZERO0_SIZE, 0
    check ELF_ZERO1, ELF_ZERO1_SIZE, 0
    execute 2
    wait 3
    execute 3
    wait 4
    execute 4
    wait 5
    // Copy both complete live ranges into separate DDR snapshots.
    copy IMAGE, SNAP_IMAGE
    copy LOCAL_RAM, SNAP_RAM
    same IMAGE, SNAP_IMAGE
    same LOCAL_RAM, SNAP_RAM
    publish 5
reset_wait:
    j reset_wait
resumed:
    // Architectural CPU reset must not reset any of these memory owners.
    check LOCAL_RAM, SPAN, LOCAL_PATTERN
    image
    jalr s1
    li t0, 4
    bne a0, t0, fail
    same IMAGE, SNAP_IMAGE
    same LOCAL_RAM, SNAP_RAM
    // A live-memory change must not change the saved bytes.
    li t1, LOCAL_RAM
    sd zero, 0(t1)
    li t1, (LOCAL_RAM + SPAN - 8)
    sd zero, 0(t1)
    li t1, (IMAGE + 0x100)
    sd zero, 0(t1)
    check SNAP_RAM, 8, LOCAL_PATTERN
    check (SNAP_RAM + SPAN - 8), 8, LOCAL_PATTERN
    check (SNAP_IMAGE + 0x100), 8, IMAGE_PATTERN
    // The synthetic guest, not a host callback, restores the saved bytes.
    copy SNAP_IMAGE, IMAGE
    copy SNAP_RAM, LOCAL_RAM
    check LOCAL_RAM, SPAN, LOCAL_PATTERN
    same IMAGE, SNAP_IMAGE
    image
    publish 6
    wait 6
    li t0, 0x600d0000
    lw t1, 0(t0)
fail:
    li t0, 0x400d0000
    lw t1, 0(t0)
poison_buffers:
    // Keep a live return address inside the former erroneous DDR alias.
    addi sp, sp, -16
    sd ra, 8(sp)
    fill SNAP_IMAGE, SPAN, POISON
    fill SNAP_RAM, SPAN, POISON
    ld ra, 8(sp)
    addi sp, sp, 16
    ret

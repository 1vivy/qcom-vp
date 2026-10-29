// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "layout.h"
.section .text
.globl _start
.macro fill address, size, value
    ldr x3, =\address
    ldr x4, =\size
    ldr x5, =\value
1:  str x5, [x3], #8
    subs x4, x4, #8
    b.ne 1b
.endm
.macro check address, size, value
    ldr x3, =\address
    ldr x4, =\size
    ldr x6, =\value
1:  ldr x5, [x3], #8
    cmp x5, x6
    b.ne fail
    subs x4, x4, #8
    b.ne 1b
.endm
.macro same source, destination
    ldr x3, =\source
    ldr x4, =\destination
    ldr x7, =SPAN
1:  ldr x5, [x3], #8
    ldr x6, [x4], #8
    cmp x5, x6
    b.ne fail
    subs x7, x7, #8
    b.ne 1b
.endm
.macro wait offset, value
1:  ldr w2, [x1, #\offset]
    cmp w2, #\value
    b.ne 1b
.endm
.macro publish offset, value
    dmb sy
    mov w2, #\value
    str w2, [x1, #\offset]
.endm
_start:
    ldr x1, =FLAGS
    wait 12, 1
    // Publish synthetic image bytes and both actual ELF physical zero extents.
    fill ELF_ZERO0, ELF_ZERO0_SIZE, 0x1111111111111111
    fill ELF_ZERO1, ELF_ZERO1_SIZE, 0x2222222222222222
    fill IMAGE, SPAN, IMAGE_PATTERN
    ldr x0, =IMAGE
    ldr x2, =0x0000806705500513 // addi a0, zero, 85 followed by ret
    str x2, [x0]
    ldr x0, =CODE
    ldr w2, =0x00100513 // addi a0, zero, 1
    str w2, [x0]
    ldr w2, =0x00008067 // ret
    str w2, [x0, #4]
    publish 0, 1
    wait 4, 1
    // CPUCP poison writes reach the same DDR bytes through every DMA view.
    check SNAP_IMAGE, SPAN, POISON
    check UFS_VIEW(SNAP_IMAGE), SPAN, POISON
    check AUTH_VIEW(SNAP_IMAGE), SPAN, POISON
    check SNAP_RAM, SPAN, POISON
    check UFS_VIEW(SNAP_RAM), SPAN, POISON
    check AUTH_VIEW(SNAP_RAM), SPAN, POISON
    // Genuine loader-style zero-fill must remain a DDR effect, not a local clear.
    fill UFS_VIEW(ELF_ZERO0), ELF_ZERO0_SIZE, 0
    fill AUTH_VIEW(ELF_ZERO1), ELF_ZERO1_SIZE, 0
    ldr w2, =0x00200513 // addi a0, zero, 2
    str w2, [x0]
    publish 8, 2
    wait 4, 2
    ldr x0, =UFS_VIEW(CODE)
    ldr w2, =0x00300513 // addi a0, zero, 3
    str w2, [x0]
    publish 8, 3
    wait 4, 3
    ldr x0, =AUTH_VIEW(CODE)
    ldr w2, =0x00400513 // addi a0, zero, 4
    str w2, [x0]
    publish 8, 4
    wait 4, 4
    publish 8, 5
    wait 4, 5
    same IMAGE, SNAP_IMAGE
    same IMAGE, UFS_VIEW(SNAP_IMAGE)
    same IMAGE, AUTH_VIEW(SNAP_IMAGE)
    check SNAP_RAM, SPAN, LOCAL_PATTERN
    check UFS_VIEW(SNAP_RAM), SPAN, LOCAL_PATTERN
    check AUTH_VIEW(SNAP_RAM), SPAN, LOCAL_PATTERN
    // Request an architectural reset through the existing boot controller.
    publish 16, 1
    ldr x0, =0x17c40000
    ldr w2, =0x334000
    str w2, [x0]
    mov w2, #1
    str w2, [x0, #0x60]
    ldr x0, =0x17c42000
    str w2, [x0]
    wait 4, 6
    publish 8, 6
1:  b 1b
fail:
    ldr x0, =0x400d1000
    ldr w1, [x0]

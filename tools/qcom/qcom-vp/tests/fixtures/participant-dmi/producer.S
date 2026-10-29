// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x0, #0x20400000
    add x1, x0, #0x1000
    add x2, x0, #0x2000
    mov w3, #0x0513
    movk w3, #0x0010, lsl #16
    str w3, [x1]
    mov w3, #0x8067
    str w3, [x1, #4]
    mov w3, #1
    str w3, [x2, #8]
1:  ldr w3, [x2]
    cbz w3, 1b
2:  ldr w3, [x2, #12]
    cbz w3, 2b
    mov w3, #0x0513
    movk w3, #0x0020, lsl #16
    str w3, [x1]
    dmb sy
    mov w3, #1
    str w3, [x2, #4]
3:  b 3b

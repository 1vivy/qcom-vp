// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x0, #0x14000000
    add x0, x0, #0x10000
    mov sp, x0
    bl ipcc_probe
1:  b 1b

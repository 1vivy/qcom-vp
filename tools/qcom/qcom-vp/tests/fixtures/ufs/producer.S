.section .text
.globl _start
_start:
    mov x0, #0x14810000
    mov sp, x0
    bl test
1:  b 1b

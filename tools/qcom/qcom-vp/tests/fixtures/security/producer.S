.section .text
.globl _start
_start:
    mov x0, #0x17000000
    mov w1, #1
    str w1, [x0]
    ldr w2, [x0]
    tbz w2, #0, failed
    mrs x1, scr_el3
    orr x1, x1, #1
    orr x1, x1, #0x400
    msr scr_el3, x1
    mov x1, #0x80000000
    msr hcr_el2, x1
    mov x1, #0x3c5
    msr spsr_el3, x1
    adr x1, nonsecure
    msr elr_el3, x1
    isb
    eret
nonsecure:
    ldr w2, [x0]
    tbnz w2, #0, failed
    mov x0, #0x600d0000
    str w2, [x0]
1:  b 1b
failed:
    mov x0, #0xbad00000
    str w2, [x0]
2:  b 2b

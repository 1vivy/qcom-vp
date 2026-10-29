// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x20, #0x02230000
    mov x21, #0x1000
    movk x21, #0x0228, lsl #16
    mov x22, #0x9000
    movk x22, #0x0228, lsl #16
    mov x23, #0x20400000
    add x24, x23, #0x800
    mov w25, #2
    mov x26, #0x00c00000
    add x26, x26, #0x200
    mov w27, #1
next_message:
#if CASE == 4
    mov x10, x23
    mov w11, #647
    mov w12, #647
    mov w13, #3
    bl submit
#else
    mov x10, x23
    mov w11, #128
    mov w12, #128
    mov w13, #3
#if CASE == 2
    mov x10, #0x65000000
#endif
#if CASE == 3
    mov w13, #6
#endif
#if CASE == 5
    mov w13, #4
#endif
    bl submit
    add x10, x23, #128
    mov w11, #512
    mov w12, #640
    mov w13, #4
    bl submit
    add x10, x23, #640
    mov w11, #7
    mov w12, #647
    mov w13, #5
    bl submit
#endif
    mov x0, #0
compare:
    ldrb w1, [x22, x0]
    ldrb w2, [x24, x0]
    cmp w1, w2
    b.ne failed
    add x0, x0, #1
    cmp x0, #48
    b.ne compare
    add x23, x23, #0x400
    add x24, x24, #48
    subs w25, w25, #1
    b.eq complete
    // Release the boot owner before the runtime selects its ownership policy.
    mov w0, #1
    str w0, [x20, #0x130]
    str w0, [x26]
    ldr w1, [x26]
    cmp w1, w0
    b.ne failed
    mov w27, #2
    b next_message
complete:
    mov x0, #0x600d0000
    str wzr, [x0]
1:  b 1b
submit:
    mov w0, #1
    str w0, [x20, #0x130]
    mov w0, #0x100
    str w0, [x20, #0x60]
    ldr w0, [x20, #0x60]
    and w0, w0, #3
    cmp w0, w27
    b.ne failed
    mov w0, #1
    str w0, [x20]
    mov w0, #2
    str w0, [x20, #4]
    mov w0, #1
#if CASE == 4 || CASE == 5
    str wzr, [x20, #8]
#else
    str w0, [x20, #8]
#endif
    str w13, [x20, #12]
    str w0, [x20, #0x2c]
    str w11, [x20, #0x58]
    str w12, [x20, #0x5c]
#if CASE == 1
    mov w0, #0
#endif
    str w0, [x21]
    add x14, x21, #4
    cmp w13, #3
    b.eq descriptor
    mov x0, #0
copy_state:
    ldr w1, [x22, x0]
    str w1, [x14, x0]
    add x0, x0, #4
    cmp x0, #64
    b.ne copy_state
    add x14, x14, #64
descriptor:
    str w10, [x14]
    str w11, [x14, #4]
    str wzr, [x14, #8]
    dsb sy
    mov w0, #0x200
    str w0, [x20, #0x60]
    ldr w0, [x20, #0x128]
    cmp w0, #1
    b.ne failed
    ldr w0, [x20, #0x144]
#if CASE == 4
    mov w1, #0xc00
#else
    mov w1, #0x1000
    cmp w13, #5
    b.ne size_check
    mov w1, #0xc00
#endif
size_check:
    cmp w0, w1
    b.ne failed
    ret
failed:
    mov x0, #0xbad00000
    str wzr, [x0]
2:  b 2b

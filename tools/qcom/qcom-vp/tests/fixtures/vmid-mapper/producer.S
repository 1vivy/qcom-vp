// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x20, #0x008c0000
    add x20, x20, #0x6000
#if CASE == 1
    // Four contexts must not expose a fifth context.
    mov w0, #0x10000
    str w0, [x20, #0xc10]
    b failed
#elif CASE == 2
    // Eight VMIDs must not accept VMID eight.
    mov w0, #8
    str w0, [x20, #8]
    b failed
#else
    // Secure client changes must not modify the nonsecure client's controls.
    mov w0, #0x111
    str w0, [x20]
    str w0, [x20, #0x400]
    ldr w0, [x20]
    bic w0, w0, #1
    str w0, [x20]
    ldr w1, [x20, #0x400]
    cmp w1, #0x111
    b.ne failed
    // The two security banks must retain distinct bypass identities.
    mov w0, #3
    str w0, [x20, #8]
    mov w0, #5
    str w0, [x20, #0x408]
    str wzr, [x20, #0x400]
    ldr w1, [x20, #8]
    cmp w1, #3
    b.ne failed
    ldr w1, [x20, #0x408]
    cmp w1, #5
    b.ne failed
    // The guest's masked update must retain the initialized TYPE field.
    mov w0, #0x10000
    str w0, [x20, #0xc0c]
    ldr w1, [x20, #0xc0c]
    ldr w2, =0x30fcfbff
    bic w1, w1, w2
    mov w3, #0x3002
    and w3, w3, w2
    orr w1, w1, w3
    str w1, [x20, #0xc0c]
    // Auxiliary and match writes must not alias that context.
    ldr w0, =0x70000013
    str w0, [x20, #0xe0c]
    mov w0, #-1
    str w0, [x20, #0x80c]
    ldr w1, [x20, #0xc0c]
    mov w2, #0x3002
    movk w2, #1, lsl #16
    cmp w1, w2
    b.ne failed
    // The mask must retain only the declared five SID bits in both fields.
    ldr w1, [x20, #0x80c]
    mov w2, #0x1f
    movk w2, #0x801f, lsl #16
    cmp w1, w2
    b.ne failed
    // Re-enabling a client must not discard its programmed context.
    ldr w0, [x20]
    orr w0, w0, #1
    str w0, [x20]
    ldr w1, [x20, #0xc0c]
    mov w2, #0x3002
    movk w2, #1, lsl #16
    cmp w1, w2
    b.ne failed
    mov x0, #0x600d0000
    str w1, [x0]
#endif
failed:
    mov x0, #0xbad00000
    str w1, [x0]
1:  b 1b

.section .text
.globl _start
_start:
    mov x0, #0x14800000
1:  ldr w1, [x0]
    cbz w1, 1b
    cmp w1, #1
    b.ne failure
    mov x2, #0x0f1e0000
    mov w3, #0x1234
    movk w3, #0xabcd, lsl #16
    str w3, [x2]
    ldr w4, [x2]
    bic w4, w4, #0x1c0
    str w4, [x2]
    ldr w5, [x2]
    bic w3, w3, #0x1c0
    cmp w3, w5
    b.ne failure
    mov x2, #0x17c40000
    mov w3, #0x4100
    movk w3, #0x33, lsl #16
    str w3, [x2]
    str w3, [x2, #4]
    mov w3, #1
    str w3, [x2, #0x60]
#ifdef EXTENDED
    // Setup fields retain state but must not release the real CEASE latch.
    mov x6, #0x7600
    movk x6, #0x1887, lsl #16
    ldr w5, [x6, #0x14]
    cbnz w5, failure
    ldr w5, [x6, #0x18]
    cbnz w5, failure
    str wzr, [x6, #0x1c]
    str wzr, [x6, #0x8c]
    mov w7, #0x100
    str w7, [x6, #0x14]
    ldr w5, [x6, #0x14]
    cmp w5, w7
    b.ne failure
    str w7, [x6, #0x18]
    ldr w5, [x6, #0x18]
    cmp w5, w7
    b.ne failure
#endif
    mov w4, #0x10000
2:  ldr w1, [x0]
    cmp w1, #1
    b.ne failure
    subs w4, w4, #1
    b.ne 2b
    mov x2, #0x2000
    movk x2, #0x17c4, lsl #16
    str w3, [x2]
3:  ldr w1, [x0]
    cmp w1, #1
    b.eq 3b
    cmp w1, #2
    b.ne failure
#ifdef EXTENDED
    // The consumer reached the published vector after the single reset request.
    // Both pulse fields must survive that architectural reset.
    ldr w5, [x6, #0x14]
    cmp w5, w7
    b.ne failure
    ldr w5, [x6, #0x18]
    cmp w5, w7
    b.ne failure
    str wzr, [x6, #0x18]
    ldr w5, [x6, #0x18]
    cbnz w5, failure
    ldr w5, [x6, #0x14]
    cmp w5, w7
    b.ne failure
    str wzr, [x6, #0x14]
    ldr w5, [x6, #0x14]
    cbnz w5, failure
    str w3, [x6]
    ldr w5, [x6]
    cmp w5, #1
    b.ne failure
    // A duplicate reset reenters the consumer with count two and reports failure.
    // Observe its shared word after the final setup write, not a model counter.
    mov w4, #0x10000
5:  ldr w1, [x0]
    cmp w1, #2
    b.ne failure
    subs w4, w4, #1
    b.ne 5b
    ldr w5, [x6, #0x14]
    cbnz w5, failure
    ldr w5, [x6, #0x18]
    cbnz w5, failure
#endif
    bl runtime_probe
    mov x0, #0x600d0000
    str w1, [x0]
failure:
    mov x0, #0xbad00000
    str w1, [x0]
4:  b 4b

#include "runtime.inc"

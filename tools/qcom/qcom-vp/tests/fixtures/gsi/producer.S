// SPDX-License-Identifier: BSD-3-Clause-Clear
// Synthetic instruction words only. No OEM microcode or config values.
.section .text
.globl _start
_start:
    ldr x20, =0xa04000
    ldr x21, =0xa50000
    mov x22, #0xf400
    mov x23, #0xf018
    ldr w0, [x20]
    cbnz w0, failed
    ldr w0, [x20, x22]
    cbnz w0, failed
#if CASE == 3
    mov w0, #0x61
    str w0, [x20, x23]
    b failed
#endif
    adr x4, config_offsets
    mov w5, #10
1:  ldr w6, [x4], #4
#if CASE == 5
    cmp w5, #1
    b.eq 2f
#endif
    str w6, [x20, x6]
    ldr w7, [x20, x6]
    cmp w7, w6
    b.ne failed
2:  subs w5, w5, #1
    b.ne 1b
    mov w0, #0x100
    str w0, [x20, #8]
    mov w4, #0
    mov w5, #FW_SIZE
3:  add w0, w4, #0x100
#if CASE == 1
    // Leave one interior word unwritten. Repeated writes cannot fill the hole.
    cmp w4, #0x100
    b.eq 4f
#endif
    str w0, [x21, x4]
    str w0, [x21, x4]
4:  add w4, w4, #4
    cmp w4, w5
    b.lo 3b
#if CASE == 6
    mov w0, #0x101
    str w0, [x20, #8]
#endif
#if CASE != 7
    mov w0, #1
    mov x4, #0xb000
    str w0, [x20, x4]
#endif
    mov w0, #5
    str w0, [x20]
    ldr w0, [x20]
    cmp w0, #5
    b.ne failed
    ldr w0, [x20, x22]
    cbnz w0, failed
#if CASE == 9
    str wzr, [x21]
    b failed
#endif
#if CASE != 4
    str wzr, [x20, x22]
#endif
#if CASE == 2
    // Selected TZ 0x157234f0 sends 0x81 without the XBL 0x61 step.
    mov w0, #0x81
    str w0, [x20, x23]
    ldr w0, [x20, x22]
    cmp w0, #1
    b.ne failed
    ldr x0, =0x600d0000
    str w0, [x0]
    b failed
#endif
    mov w0, #0x61
    str w0, [x20, x23]
    ldr w0, [x20, x22]
    cbnz w0, failed
    mov w0, #0x81
    str w0, [x20, x23]
    ldr w0, [x20, x22]
    cmp w0, #1
    b.ne failed
#if CASE == 8
    // Unknown mailbox commands must not simulate channel execution.
    mov w0, #0x91
    str w0, [x20, x23]
    b failed
#elif CASE == 10
    str w0, [x20, x22]
    b failed
#elif CASE == 11
    mov x4, #0xf020
    str w0, [x20, x4]
    b failed
#endif
    // Clear invalidates the reply and requires a fresh mailbox pair.
    str wzr, [x20, x22]
    ldr w0, [x20, x22]
    cbnz w0, failed
    mov w0, #0x61
    str w0, [x20, x23]
    mov w0, #0x81
    str w0, [x20, x23]
    ldr w0, [x20, x22]
    cmp w0, #1
    b.ne failed
    // Repeated start invalidates readiness and requires clear/setup/request.
    mov w0, #5
    str w0, [x20]
    ldr w0, [x20, x22]
    cbnz w0, failed
    str wzr, [x20, x22]
    mov w0, #0x61
    str w0, [x20, x23]
    mov w0, #0x81
    str w0, [x20, x23]
    str w0, [x20, x23]
    ldr w0, [x20, x22]
    cmp w0, #1
    b.ne failed
    // Initialization does not replace or alter downloaded guest bytes.
    mov w4, #0
5:  ldr w0, [x21, x4]
    add w1, w4, #0x100
    cmp w0, w1
    b.ne failed
    add w4, w4, #4
    cmp w4, w5
    b.lo 5b
    ldr w0, [x20, #0x400]
    cmp w0, #0x400
    b.ne failed
    ldr x0, =0x600d0000
    str w0, [x0]
failed:
    ldr x0, =0xbad00000
    str w0, [x0]
6:  b 6b
.balign 4
config_offsets:
    .word 0x400, 0x418, 0x424, 0x428, 0x404, 0x42c, 0x44c, 0x420, 0x430, 0x43c

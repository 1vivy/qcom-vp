// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
.macro route apid
    mov x20, #(\apid * 0x200)
    movk x20, #0xc90, lsl #16
    mov x21, #(\apid * 0x20)
    movk x21, #0xc4c, lsl #16
.endm
.macro write offset, value, count=1
    movz w0, #(\value & 0xffff)
    movk w0, #((\value >> 16) & 0xffff), lsl #16
    str w0, [x20, #0x10]
    mov w1, #((\offset << 4) | (\count - 1))
    str w1, [x20]
    ldr w1, [x20, #8]
    cmp w1, #1
    b.ne failed
.endm
.macro read offset, count=1
    mov w1, #((\offset << 4) | (\count - 1))
    orr w1, w1, #0x08000000
    str w1, [x21]
    ldr w1, [x21, #8]
    cmp w1, #1
    b.ne failed
    ldr w0, [x21, #0x18]
.endm
_start:
#if CASE == 0
    // Both MGPI banks are passive configuration, not software command lanes.
    // Exercise every port, both edge fields, priority, and the global gate.
    mov x10, #0xb000
    movk x10, #0xc40, lsl #16
    mov x12, #0x7a00
    movk x12, #0xc42, lsl #16
    mov w11, #10
    bl configure_mgpi
    mov x10, #0x6000
    movk x10, #0xc41, lsl #16
    mov x12, #0x8a00
    movk x12, #0xc43, lsl #16
    mov w11, #2
    bl configure_mgpi
    // Enabled edge controls must not manufacture a PMIC transaction or bypass
    // the software bus gate. A denied command must not change its PMIC byte.
    mov x10, #0x7804
    movk x10, #0xc42, lsl #16
    ldr w12, [x10]
    bic w13, w12, #1
    str w13, [x10]
    route 0
    mov w0, #0x5a
    str w0, [x20, #0x10]
    mov w0, #0x800
    str w0, [x20]
    ldr w0, [x20, #8]
    cmp w0, #5
    b.ne failed
    str w12, [x10]
    read 0x80
    cbnz w0, failed
    // An enabled open input follows its pull without creating a latched IRQ.
    route 12
    write 0x46, 0
    write 0x40, 0
    write 0x42, 0
    read 0x10
    cbnz w0, failed
    write 0x46, 0x80
    read 0x10
    cmp w0, #1
    b.ne failed
    read 8
    cmp w0, #1
    b.ne failed
    read 0x18
    cbnz w0, failed
    write 0x42, 4
    read 0x10
    cbnz w0, failed
    read 8
    cbnz w0, failed
    // A software output controls the same pin independently of its pull.
    write 0x40, 1
    write 0x44, 0x80
    read 0x10
    cmp w0, #1
    b.ne failed
    read 8
    cmp w0, #0x81
    b.ne failed
    write 0x44, 0
    read 0x10
    cbnz w0, failed
#endif
#if CASE == 6
    // Exploration cannot create identity or OTP metadata.
    // Byte 0x1f0 stays undeclared, unlike the declared identity tail.
    route 2
    read 0xf0
    b failed
#elif CASE == 7
    // Exploration cannot replace a PBS service rejection.
    route 1
    read 0x28, 2
    b failed
#elif CASE == 8
    // A broadcast group has no combined read response.
    route 5
    read 0x80
    b failed
#elif CASE == 9
    // A terminal open latch cannot return to the programmable state.
    route 0
    write 0xd1, 2
    write 0xd1, 0
    read 0xd1
    cmp w0, #2
    b.ne failed
    // A closed latch does not freeze ordinary digital configuration.
    route 12
    write 0x40, 1
    write 0x44, 0
    write 0x46, 0x80
    write 0xd1, 3
    write 0x44, 0x80
    read 0x10
    cmp w0, #1
    b.ne failed
#elif CASE == 10
    // A closed latch rejects a protected write even with a valid key.
    route 0
    write 0xd0, 0xa5
    write 0xd2, 3
    write 0x42, 0x82
    b failed
#elif CASE == 11
    // The reserved mode cannot create an access grant.
    route 0
    write 0xd3, 1
    b failed
#endif
    route 0
#if CASE == 0
    // Terminal open latches permit the configuration and key checks below.
    write 0xd1, 0x020202, 3
#endif
    // Unknown ordinary configuration retains independent byte values.
    read 0x80, 2
    cbnz w0, failed
    write 0x80, 0x3a5c, 2
    read 0x80, 2
    mov w2, #0x3a5c
    cmp w0, w2
    b.ne failed
    read 0x81
    cmp w0, #0x3a
    b.ne failed
    // Set and clear aliases share one enable state. They do not replace it.
    write 0x15, 0x05
    write 0x15, 0xa0
    write 0x16, 0x24
    read 0x15
    cmp w0, #0x81
    b.ne failed
    read 0x16
    cmp w0, #0x81
    b.ne failed
    write 0x14, 0xff
    read 0x14
    cbnz w0, failed
    read 0x15
    cmp w0, #0x81
    b.ne failed
    read 0x18, 2
    cbnz w0, failed
#if CASE == 1
    // A protected write requires a key from this peripheral.
    write 0x42, 0x82
    b failed
#endif
#if CASE == 2
    write 0xd0, 0x5a
    write 0x42, 0x82
    b failed
#endif
    write 0xd0, 0xa5
    write 0x42, 0x82
    read 0x42
    cmp w0, #0x82
    b.ne failed
#if CASE == 3
    // The first protected write consumes the key.
    write 0x42, 0x01
    b failed
#endif
    write 0xd0, 0xa5
    write 0x42, 0x01
    read 0x42
    cmp w0, #1
    b.ne failed
    route 2
    write 0x50, 0x35a1, 2
    read 0x50, 2
    mov w2, #0x35a1
    cmp w0, w2
    b.ne failed
    // The PBS interface and ordinary controls share a peripheral, not ownership.
    route 1
    write 0x50, 0x37
    read 0x50
    cmp w0, #0x37
    b.ne failed
    read 0, 6
    mov w2, #0x0501
    movk w2, #0x0100, lsl #16
    cmp w0, w2
    b.ne failed
    write 0x40, 0x0000e080, 4
    mov w0, #0xcdef
    movk w0, #0x90ab, lsl #16
    str w0, [x20, #0x14]
    write 0x80, 0x12345678, 8
#if CASE == 12
    write 0xd3, 3
    // A closed PBS latch prevents another memory write.
    write 0x80, 0x12345678, 8
    b failed
#endif
    write 0x40, 0
    write 0x40, 0x0000a080, 4
    read 0x88, 8
    mov w2, #0x5678
    movk w2, #0x1234, lsl #16
    cmp w0, w2
    b.ne failed
    ldr w0, [x21, #0x1c]
    mov w2, #0xcdef
    movk w2, #0x90ab, lsl #16
    cmp w0, w2
    b.ne failed
    read 0x42, 2
    cmp w0, #8
    b.ne failed
#if CASE == 4
    // The same foreign owner remains forbidden after a nonsecure transition.
    mrs x0, scr_el3
    orr x0, x0, #1
    orr x0, x0, #0x400
    msr scr_el3, x0
    mov x0, #0x80000000
    msr hcr_el2, x0
    mov x0, #0x3c5
    msr spsr_el3, x0
    adr x0, nonsecure
    msr elr_el3, x0
    isb
    eret
nonsecure:
#endif
#if CASE == 4 || CASE == 5
    write 0x41, 0xe0
#if CASE == 4
    route 3
#else
    // Secure state alone does not authorize an undeclared foreign owner.
    route 4
#endif
    mov w0, #0xdead
    str w0, [x20, #0x10]
    str w0, [x20, #0x14]
    // Byte 0x850 stays undeclared, unlike the declared PON bursts at 0x800.
    mov w0, #0x850
    str w0, [x20]
    ldr w0, [x20, #8]
    cmp w0, #5
    b.ne failed
    route 1
    read 0x42, 2
    cmp w0, #8
    b.ne failed
    write 0x41, 0xa0
    read 0x88, 8
    cbnz w0, failed
    ldr w0, [x21, #0x1c]
    cbnz w0, failed
#else
    // Two APIDs feed one PBS pointer without dropping alternating chunks.
    write 0x40, 0x0000e080, 4
    mov w0, #0x1122
    str w0, [x20, #0x14]
    write 0x80, 0x1111, 8
    route 3
    mov w0, #0x3344
    str w0, [x20, #0x14]
    write 0x80, 0x2222, 8
    route 1
    mov w0, #0x5566
    str w0, [x20, #0x14]
    write 0x80, 0x3333, 8
#if CASE == 9
    // The closed latch preserves read transport after all memory writes.
    write 0xd3, 3
#endif
    write 0x40, 0x0000a080, 4
    read 0x88, 8
    mov w2, #0x1111
    cmp w0, w2
    b.ne failed
    ldr w0, [x21, #0x1c]
    mov w2, #0x1122
    cmp w0, w2
    b.ne failed
    read 0x88, 8
    mov w2, #0x2222
    cmp w0, w2
    b.ne failed
    ldr w0, [x21, #0x1c]
    mov w2, #0x3344
    cmp w0, w2
    b.ne failed
    read 0x88, 8
    mov w2, #0x3333
    cmp w0, w2
    b.ne failed
    ldr w0, [x21, #0x1c]
    mov w2, #0x5566
    cmp w0, w2
    b.ne failed
#endif
    // A group write changes every selected member, not the other group.
    route 5
    write 0x80, 0x5a96, 2
    route 7
    read 0x80, 2
    mov w2, #0x5a96
    cmp w0, w2
    b.ne failed
    route 9
    read 0x80, 2
    cmp w0, w2
    b.ne failed
    route 8
    read 0x80, 2
    cbnz w0, failed
    route 10
    read 0x80, 2
    cbnz w0, failed
    route 6
    write 0x80, 0x3311, 2
    route 8
    read 0x80, 2
    mov w2, #0x3311
    cmp w0, w2
    b.ne failed
    route 10
    read 0x80, 2
    cmp w0, w2
    b.ne failed
    route 7
    read 0x80, 2
    mov w2, #0x5a96
    cmp w0, w2
    b.ne failed
    route 9
    read 0x80, 2
    cmp w0, w2
    b.ne failed
    // The reset policy cannot publish the guest's initialization result.
    route 11
    read 0xda
    cmp w0, #4
    b.ne failed
    read 0x8c
    cbnz w0, failed
    write 0x8c, 0x10
    read 0x8c
    cmp w0, #0x10
    b.ne failed
    mov x0, #0x600d0000
    str w0, [x0]
failed:
    mov x0, #0xbad00000
    str w0, [x0]
1:  b 1b

#if CASE == 0
configure_mgpi:
    mov x13, x10
    add x14, x10, #0x1000
    mov w15, #0x0fff
    movk w15, #0x0fff, lsl #16
    // Enabled VIOCTL configuration does not create an external input edge.
    mov w16, #0x0880
    movk w16, #0x8080, lsl #16
    mov w17, #1
1:
    ldr w0, [x13]
    orr w0, w0, w15
    str w0, [x13]
    ldr w0, [x13, #4]
    orr w0, w0, #3
    str w0, [x13, #4]
    ldr w0, [x14, #4]
    bfi w0, w11, #0, #3
    str w0, [x14, #4]
    add x13, x13, #8
    add x14, x14, #0x1000
    subs w11, w11, #1
    b.ne 1b
    ldr w0, [x10, #0x100]
    orr w0, w0, #1
    str w0, [x10, #0x100]
    mov w11, #13
2:
    str w16, [x12]
    str w16, [x12, #0x180]
    str w17, [x12, #0x80]
    add x12, x12, #4
    subs w11, w11, #1
    b.ne 2b
    ret
#endif

// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
.macro route apid
    mov x20, #(\apid * 0x200)
    movk x20, #0xc90, lsl 16
    mov x21, #(\apid * 0x20)
    movk x21, #0xc4c, lsl 16
.endm
.macro route_secure apid
    mov x20, #(\apid * 0x200)
    movk x20, #0xc90, lsl 16
    mov x21, #(\apid * 0x20)
    movk x21, #0xc50, lsl 16
.endm
.macro write offset, value, count=1
    movz w0, #(\value & 0xffff)
    movk w0, #((\value >> 16) & 0xffff), lsl 16
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
    route_secure 0
#if CASE == 0
    // A present PON group takes the dump path on every unowned byte.
    read 0x90, 8
    ldr w2, [x21, #0x1c]
    orr w0, w0, w2
    cbnz w0, failed
#elif CASE == 1
    // The present profile also covers a GPIO bank with no explicit list.
    route_secure 1
    read 0x20
    cbnz w0, failed
#elif CASE == 2
    // A group whose offset-4 presence byte reads zero stays out of the profile.
    route 3
    read 0x20
    b failed
#elif CASE == 3
    // The profile still grants no write. Byte 0xd9 is outside the retained
    // exploratory range, so the write must fail.
    write 0xd9, 0x5a
    b failed
#endif
    mov x0, #0x600d0000
    str w0, [x0]
failed:
    mov x0, #0xbad00000
    str w0, [x0]
1:  b 1b

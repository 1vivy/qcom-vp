// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
.macro route apid
    mov x20, #(\apid * 0x200)
    movk x20, #0xc90, lsl 16
    mov x21, #(\apid * 0x20)
    movk x21, #0xc4c, lsl 16
.endm
.ifndef SECURE_EE
.equ SECURE_EE, 1
.endif
// The observer window of one EE. EE1 is the secure window that QSEE uses.
.macro route_secure apid
    mov x20, #(\apid * 0x200)
    movk x20, #0xc90, lsl 16
    mov x21, #(\apid * 0x20)
    movk x21, #(0xc4c + SECURE_EE * 4), lsl 16
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
// An unmarked read through the owner window, as a driver issues it.
.macro read_owner offset, count=1
    mov w1, #((\offset << 4) | (\count - 1))
    orr w1, w1, #0x08000000
    str w1, [x20]
    ldr w1, [x20, #8]
    cmp w1, #1
    b.ne failed
    ldr w0, [x20, #0x18]
    ldr w2, [x20, #0x1c]
.endm
_start:
#if CASE < 10
    route_secure 0
#endif
#if CASE == 0
    // The dump reads a whole peripheral. Bytes 0x90..0x97 have no owner.
    read 0x90, 8
    ldr w2, [x21, #0x1c]
    orr w0, w0, w2
    cbnz w0, failed
#elif CASE == 1
    // Bytes 0x4c..0x51 of the PON group have no owner.
    read 0x4c
    cbnz w0, failed
#elif CASE == 2
    // A declared byte keeps its declared value. No dump line covers it.
    read 0xda
    cmp w0, #4
    b.ne failed
#elif CASE == 3
    // A group outside the dump keeps the unknown-byte rejection.
    route 1
    read 0x20
    b failed
#elif CASE == 4
    // The dump observes data. It does not grant an unowned write.
    write 0xd8, 0x5a
    b failed
#elif CASE == 5
    // Selection alone is insufficient. The absent group reads zero at offset four.
    route 3
    read 0x20
    b failed
#elif CASE == 6
    // A dump byte takes the dump contract, not exploratory substitution.
    read 0x50
    cbnz w0, failed
#elif CASE == 7
    // Every byte of a burst reports its own line.
    read 0xd9
    cbnz w0, failed
#elif CASE == 8
    // The PBS interface answers the presence byte, so its RAM offsets take the
    // dump path. Bytes without an owner read zero and report one line.
    route_secure 2
    read 0xb0
    cbnz w0, failed
#elif CASE == 9
    // A mixed burst keeps the interface bytes and reports only the others.
    route_secure 2
    read 0x20, 8
    ldr w2, [x21, #0x1c]
    orr w0, w0, w2
    cbnz w0, failed
#elif CASE == 10
    // The unmarked scan burst reads a PBS block. Bytes without a value read
    // zero, and the burst changes no transfer state.
    route 2
    read_owner 0x08, 8
    orr w0, w0, w2
    cbnz w0, failed
#elif CASE == 11
    // The geometry bytes stay real. Only the bytes without a value read zero.
    route 2
    read_owner 0xa0, 8
    cmp w0, #0x20
    b.ne failed
    cbnz w2, failed
#elif CASE == 12
    // A four-byte driver read of the disabled RAM port keeps its refusal.
    route 2
    read_owner 0x4c, 4
    b failed
#elif CASE == 13
    // An eight-byte scan burst at the disabled port reads zero, and the
    // interface registers keep their values.
    route 2
    read_owner 0x4c, 8
    orr w0, w0, w2
    cbnz w0, failed
    read_owner 0x40
    cbnz w0, failed
    read_owner 0x42
    cbnz w0, failed
    read_owner 0x43
    cbnz w0, failed
#endif
    mov x0, #0x600d0000
    str w0, [x0]
failed:
    mov x0, #0xbad00000
    str w0, [x0]
1:  b 1b

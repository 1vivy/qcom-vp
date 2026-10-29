// SPDX-License-Identifier: BSD-3-Clause-Clear
// One declared capture slot appears in the capability field at 0x1004.
.macro expect_count
    add x20, x19, #0x1000
    ldr w4, [x20, #4]
    mov w3, #0x1000
    cmp w4, w3
    b.ne failed
.endm
.section .text
.globl _start
_start:
    mov x19, #0x0b200000
#if CASE == 0
    expect_count

    // Configure the declared slot, an event selection, a domain comparison,
    // and an ownership record. No event producer is attached to any of them.
    add x20, x19, #0x4000
    mov w3, #1
    str w3, [x20, #0xf00]
    mov w3, #0xabcd
    movk w3, #0x8000, lsl 16
    str w3, [x19, #0x48]
    mov w3, #0x5678
    movk w3, #0x8000, lsl 16
    str w3, [x19, #0x38]
    mov w3, #0x1234
    str w3, [x19, #0x40]
    mov w3, #4
    str w3, [x20, #0x600]
    mov w3, #2
    str w3, [x20, #0x640]

    // The configuration must not produce a valid capture or a timestamp.
    add x21, x19, #0x1000
    ldr w4, [x21, #0x200]
    cbnz w4, failed
    ldr w4, [x21, #0x204]
    cbnz w4, failed
    ldr w4, [x21, #0x208]
    cbnz w4, failed

    // Releasing the bitmap record must leave the owner word unchanged.
    str wzr, [x20, #0x600]
    ldr w4, [x20, #0x640]
    cmp w4, #2
    b.ne failed
    mov x0, #0x600d0000
    ldr w0, [x0]
    b .
#elif CASE == 1
    // A capture word is read-only. A write cannot spoof an event.
    add x20, x19, #0x1000
    ldr w4, [x20, #0x204]
    cbnz w4, failed
    str wzr, [x20, #0x204]
    b failed
#elif CASE == 2
    // The slot count is a capability, not writable policy.
    expect_count
    str wzr, [x20, #4]
    b failed
#elif CASE == 3
    // A retained control word is live. The next unimplemented offset is not.
    add x20, x19, #0x5000
    mov w3, #1
    str w3, [x20, #0x504]
    add x20, x19, #0x6000
    ldr w0, [x20]
    b failed
#elif CASE == 4
    // Capture registers exist only at the five-word slot stride.
    add x20, x19, #0x4000
    mov w3, #1
    str w3, [x20, #0xf00]
    ldr w0, [x20, #0xfd4]
    b failed
#else
#error "unsupported PDC fixture case"
#endif
failed:
    mov x0, #0xdead0000
    ldr w0, [x0]
    b .

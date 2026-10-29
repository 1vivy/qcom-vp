// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x0, #0x2000
    movk x0, #0xc22, lsl #16
    add x1, x0, #0x7000
    ldr w2, [x1, #0xe4]
    cbnz w2, failed
    ldr w2, [x1, #0xa0]
    cbnz w2, failed
    // Replay the native control prefix, including bit22 and PSHOLD policy.
    mov w3, #0
    movk w3, #0x8160, lsl #16
    str w3, [x0, #4]
    ldr w2, [x0, #4]
    cmp w2, w3
    b.ne failed
    // Active continuous, sleep period 64. The environment remains awake.
    mov w2, #0x4000
    str w2, [x0, #8]
    orr w3, w3, #0x18
    orr w3, w3, #1
    str w3, [x0, #4]
    // Enable alone cannot produce a calibrated temperature.
    ldr w2, [x1, #0xe4]
    cbnz w2, failed
    mov w2, #0x400
    str w2, [x0, #0x60]
    ldr w2, [x1, #0xe4]
    cbnz w2, failed
    mov w2, #0x400
    str w2, [x0, #0x64]
    ldr w2, [x1, #0xe4]
    cbnz w2, failed
    bic w4, w3, #1
    str w4, [x0, #4]
    // Native watchdog programming occurs while the converter is disabled.
#ifdef TSENS_SHORT_WATCHDOG
    mov w4, #5
#else
    mov w4, #0x99e9
    movk w4, #4, lsl #16
#endif
    str w4, [x0, #0x140]
    ldr w2, [x0, #0x140]
    cmp w2, w4
    b.ne failed
    str w3, [x0, #4]
#ifdef TSENS_SHORT_WATCHDOG
    b failed // The short-deadline variant must fault at the enable store.
#endif
    ldr w2, [x1, #0xe4]
    cbnz w2, failed
first_conversion:
    // An unchanged control write must not restart an in-flight conversion.
    str w3, [x0, #4]
    ldr w2, [x1, #0xe4]
    cbz w2, first_conversion
    ldr w2, [x1, #0xa0]
    tbz w2, #21, failed
    and w2, w2, #0xfff
    cmp w2, #250
    b.ne failed
    ldr w2, [x1, #0xa4]
    tbz w2, #21, failed
    // WATCHDOG_LOG is a bark-event count, not a running elapsed-time counter.
    ldr w2, [x1, #0x13c]
    cbnz w2, failed
    ldr w2, [x1, #0x14]
    tbnz w2, #31, failed
    // No TBCB clients exist in either selected DT.
    ldr w2, [x0, #0xec]
    cbnz w2, failed
    ldr w2, [x0, #0xf0]
    cbnz w2, failed
    // Exercise the zero-degree comparator and its hysteresis at virtual 25 C.
    mov w2, #6
    str w2, [x0, #0x18]
    mov w2, #0xc190
    movk w2, #0x12, lsl #16
    str w2, [x0, #0x1c] // set=300, reset=400 deci-C
    ldr w2, [x1, #0xe0]
    cbnz w2, failed
    mov w2, #7
    str w2, [x0, #0x18]
    ldr w2, [x1, #0xe0]
    cmp w2, #1
    b.ne failed
    mov w2, #0x812c
    movk w2, #0xc, lsl #16
    str w2, [x0, #0x1c] // set=200, reset=300 retains the asserted state.
    ldr w2, [x1, #0xe0]
    cmp w2, #1
    b.ne failed
    mov w2, #0x40c8
    movk w2, #6, lsl #16
    str w2, [x0, #0x1c] // set=100, reset=200 clears the state.
    ldr w2, [x1, #0xe0]
    cbnz w2, failed
    // Reset holds conversion. It does not erase programmed calibration.
    orr w4, w3, #2
    str w4, [x0, #4]
    ldr w2, [x1, #0xe4]
    cbnz w2, failed
    ldr w2, [x1, #0xa0]
    cbnz w2, failed
    ldr w2, [x0, #0x64]
    cmp w2, #0x400
    b.ne failed
    str w3, [x0, #4]
    ldr w2, [x1, #0xe4]
    cbnz w2, failed
second_conversion:
    ldr w2, [x1, #0xe4]
    cbz w2, second_conversion
    // Stall injection is supported only after disabling the armed watchdog.
    str wzr, [x0, #0x140]
    // Removing a slope invalidates the whole enabled conversion group.
    str wzr, [x0, #0x64]
    ldr w2, [x1, #0xa0]
    cbnz w2, failed
    // Disable the uncalibrated channel, leaving channel zero operational.
    bic w3, w3, #0x10
    str w3, [x0, #4]
third_conversion:
    ldr w2, [x1, #0xe4]
    cbz w2, third_conversion
    ldr w2, [x1, #0xa4]
    cbnz w2, failed
    ldr w2, [x1, #0xa0]
    tbz w2, #21, failed
    bic w3, w3, #1
    str w3, [x0, #4]
    ldr w2, [x1, #0xe4]
    cbnz w2, failed
    ldr w2, [x1, #0xa0]
    cbnz w2, failed
    mov x0, #0x600d0000
    str w2, [x0]
failed:
    mov x0, #0xbad00000
    str w2, [x0]
1:  b 1b

// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
#if CASE == 0
    // DRV0 and DRV2 retain independent completion state.
    mov x19, #0x18900000
    mov x21, #0x18920000

    // Reserved enable bits are ignored. The active nonzero bit is retained.
    ldr w4, [x19, #0xd04]
    cbnz w4, failed
    mov w3, #0xffff
    lsl w3, w3, #16
    orr w3, w3, #1
    str w3, [x19, #0xd00]
    ldr w4, [x19, #0xd00]
    cmp w4, #1
    b.ne failed
    mov x20, x19
    mov w3, #0x101
    lsl w3, w3, #8
    orr w3, w3, #8
    str w3, [x20, #0xd34]
    mov w3, #3
    lsl w3, w3, #16
    str w3, [x20, #0xd38]
    mov w3, #0x55
    str w3, [x20, #0xd3c]
    mov w3, #1
    str w3, [x20, #0xd2c]
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    ldr w4, [x19, #0xd04]
    cbnz w4, failed

    // The trigger publishes command completion.
    mov w3, #0x101
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    ldr w4, [x19, #0xd04]
    cmp w4, #1
    b.ne failed
    ldr w4, [x20, #0xd40]
    mov w5, #0x101
    lsl w5, w5, #8
    orr w5, w5, #1
    cmp w4, w5
    b.ne failed
    ldr w4, [x21, #0xd04]
    cbnz w4, failed
    ldr w4, [x19, #0xd00]
    cmp w4, #1
    b.ne failed

    // W1C removes completion only. The command status remains accepted.
    mov w3, #1
    str w3, [x19, #0xd08]
    ldr w4, [x19, #0xd04]
    cbnz w4, failed
    ldr w4, [x20, #0xd40]
    cmp w4, w5
    b.ne failed

    // Clear and raise the trigger to reuse the same active TCS.
    mov w3, #3
    lsl w3, w3, #16
    orr w3, w3, #0x10
    str w3, [x20, #0xd38]
    mov w3, #0xaa
    str w3, [x20, #0xd3c]
    mov w3, #1
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    mov w3, #0x101
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    ldr w4, [x19, #0xd04]
    cmp w4, #1
    b.ne failed
    mov w3, #1
    str w3, [x19, #0xd08]
    ldr w4, [x19, #0xd04]
    cbnz w4, failed

    // DRV2 has independent enable and completion state.
    mov x20, x21
    mov w3, #0x101
    lsl w3, w3, #8
    orr w3, w3, #8
    str w3, [x20, #0xd34]
    mov w3, #3
    lsl w3, w3, #16
    orr w3, w3, #0x10
    str w3, [x20, #0xd38]
    mov w3, #0xaa
    str w3, [x20, #0xd3c]
    mov w3, #1
    str w3, [x20, #0xd2c]
    mov w3, #2
    str w3, [x21, #0xd00]
    ldr w4, [x21, #0xd00]
    cmp w4, #2
    b.ne failed
    mov w3, #1
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    mov w3, #0x101
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    ldr w4, [x21, #0xd04]
    cmp w4, #1
    b.ne failed
    ldr w4, [x19, #0xd04]
    cbnz w4, failed
    ldr w4, [x19, #0xd00]
    cmp w4, #1
    b.ne failed

    // DRV2 TCS2 is active. Selective W1C keeps its completion set.
    add x20, x21, #0x540
    mov w3, #0x101
    lsl w3, w3, #8
    orr w3, w3, #8
    str w3, [x20, #0xd34]
    mov w3, #3
    lsl w3, w3, #16
    str w3, [x20, #0xd38]
    mov w3, #0x33
    str w3, [x20, #0xd3c]
    mov w3, #1
    str w3, [x20, #0xd2c]
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    mov w3, #0x101
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    ldr w4, [x21, #0xd04]
    cmp w4, #5
    b.ne failed
    mov w3, #1
    str w3, [x21, #0xd08]
    ldr w4, [x21, #0xd04]
    cmp w4, #4
    b.ne failed
    mov w3, #4
    str w3, [x21, #0xd08]
    ldr w4, [x21, #0xd04]
    cbnz w4, failed

#elif CASE == 1
    // TME owns requester 3 and uses its third TCS as the last active TCS.
    mov x19, #0x02210000
    ldr w4, [x19, #0xd04]
    cbnz w4, failed
    mov w3, #5
    str w3, [x19, #0xd00]
    ldr w4, [x19, #0xd00]
    cmp w4, #5
    b.ne failed
    add x20, x19, #0x540
    mov w3, #0x101
    lsl w3, w3, #8
    orr w3, w3, #8
    str w3, [x20, #0xd34]
    mov w3, #3
    lsl w3, w3, #16
    str w3, [x20, #0xd38]
    mov w3, #0x44
    str w3, [x20, #0xd3c]
    mov w3, #1
    str w3, [x20, #0xd2c]
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    mov w3, #0x101
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    ldr w4, [x19, #0xd04]
    cmp w4, #4
    b.ne failed

    // TCS0 completes independently. W1C clears only the selected bit.
    mov x20, x19
    mov w3, #0x101
    lsl w3, w3, #8
    orr w3, w3, #8
    str w3, [x20, #0xd34]
    mov w3, #3
    lsl w3, w3, #16
    orr w3, w3, #0x10
    str w3, [x20, #0xd38]
    mov w3, #0x11
    str w3, [x20, #0xd3c]
    mov w3, #1
    str w3, [x20, #0xd2c]
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    mov w3, #0x101
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    ldr w4, [x19, #0xd04]
    cmp w4, #5
    b.ne failed
    mov w3, #1
    str w3, [x19, #0xd08]
    ldr w4, [x19, #0xd04]
    cmp w4, #4
    b.ne failed
    mov w3, #4
    str w3, [x19, #0xd08]
    ldr w4, [x19, #0xd04]
    cbnz w4, failed
    ldr w4, [x19, #0xd00]
    cmp w4, #5
    b.ne failed

#elif CASE == 2
    // DRV0 TCS2 stores sleep commands but rejects an AMC trigger.
    mov x19, #0x18900000
    add x20, x19, #0x540
    b stage_valid_rejection

#elif CASE == 3
    // The address is not in the owner's resource inventory.
    mov x19, #0x18900000
    mov x20, x19
    mov w3, #0x101
    lsl w3, w3, #8
    orr w3, w3, #8
    str w3, [x20, #0xd34]
    mov w3, #3
    lsl w3, w3, #16
    orr w3, w3, #0xf0
    str w3, [x20, #0xd38]
    mov w3, #1
    str w3, [x20, #0xd3c]
    str w3, [x20, #0xd2c]
    mov w3, #0x101
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    b failed

#elif CASE == 4
    // TME TCS3 is the first sleep TCS and cannot trigger.
    mov x19, #0x02210000
    add x20, x19, #0x7e0
    b stage_valid_rejection

#elif CASE == 5
    // A resource read has no backend and fails when the batch triggers.
    mov x19, #0x18900000
    mov x20, x19
    mov w3, #0x108
    str w3, [x20, #0xd34]
    mov w3, #3
    lsl w3, w3, #16
    str w3, [x20, #0xd38]
    mov w3, #1
    str w3, [x20, #0xd2c]
    mov w3, #0x101
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    b failed
#else
#error Unsupported CASE
#endif

    mov x0, #0x600d0000
    ldr w0, [x0]
    b .

stage_valid_rejection:
    mov w3, #0x101
    lsl w3, w3, #8
    orr w3, w3, #8
    str w3, [x20, #0xd34]
    mov w3, #3
    lsl w3, w3, #16
    str w3, [x20, #0xd38]
    mov w3, #0x22
    str w3, [x20, #0xd3c]
    mov w3, #1
    str w3, [x20, #0xd2c]
    ldr w4, [x19, #0xd04]
    cbnz w4, failed
    mov w3, #0x101
    lsl w3, w3, #16
    str w3, [x20, #0xd24]
    b failed

failed:
    mov x0, #0xdead0000
    ldr w0, [x0]
    b .

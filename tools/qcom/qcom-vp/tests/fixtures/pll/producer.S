.section .text
.globl _start
_start:
    mov x19, #0x100000
    add x20, x19, #0x4000
    mov w21, #0x2020
    movk w21, #0x15, lsl #16
    mov w22, #0xe0000000
    // A missing source must not complete a runtime table request.
    mov x9, #0x200000
    add x9, x9, #0x940
    mov w3, #1
    str w3, [x9]
    mov w3, #0x100
    str w3, [x9, #8]
    str wzr, [x9, #0x20]
    mov w3, #1
    str w3, [x9, #0x24]
    ldr w4, [x9, #0x24]
    tbnz w4, #31, failed
    // The live crystal permits the same requested level to complete.
    str wzr, [x9, #8]
    str w3, [x9, #0x24]
runtime_wait:
    ldr w4, [x9, #0x24]
    tbz w4, #31, runtime_wait
    str wzr, [x9, #0x24]
    ldr w4, [x9, #0x24]
    cbnz w4, failed
    // A pending replacement must preserve the established crystal output.
    sub x10, x9, #0x40
    mov w3, #2
    str w3, [x10]
    mov w3, #0x100
    str w3, [x9, #8]
    mov w3, #1
    str w3, [x9, #0x24]
    ldr w4, [x10]
    tbnz w4, #31, failed
    str wzr, [x9, #8]
    str w3, [x9, #0x24]
runtime_reselect:
    ldr w4, [x9, #0x24]
    tbz w4, #31, runtime_reselect
    str wzr, [x9, #0x24]
    // The isolated supply follows collapse requests, not the written status bit.
    mov x9, #0x200000
    add x9, x9, #0x600
    str wzr, [x9]
    ldr w4, [x9]
    mov w3, #0x80000000
    cmp w4, w3
    b.ne failed
    orr w4, w4, #1
    str w4, [x9]
    ldr w4, [x9]
    cmp w4, #1
    b.ne failed
    ldr w4, [x9, #4]
    mov w3, #0x8000
    cmp w4, w3
    b.ne failed
    // A separate PLL consumes the shared vote, not PLL0 configuration or output.
    mov x16, #0x120000
    mov x17, #0x200000
    add x17, x17, #0x500
    mov w3, #32
    str w3, [x16, #0x10]
    mov w3, #1
    movk w3, #0x200, lsl #16
    str w3, [x16, #0x18]
    mov w3, #4
    str w3, [x16]
    mov w3, #0x103
    str w3, [x17, #4]
    mov w3, #3
    str w3, [x17]
    mov w3, #31
    str w3, [x19, #0x10]
    mov w3, #0x4000
    str w3, [x19, #0x14]
    mov w3, #1
    movk w3, #0x200, lsl #16
    str w3, [x19, #0x18]
    // A stopped oscillator accepts the latch without a prior lock.
    mov w3, #0x4000
    str w3, [x19]
    ldr w4, [x19]
    mov w3, #0x6000
    cmp w4, w3
    b.ne failed
    // Software cannot clear ACK while the latch input remains asserted.
    bic w3, w4, #0x2000
    str w3, [x19]
    ldr w4, [x19]
    tbz w4, #13, failed
    // A read-modify-write carries ACK back, but deassertion still clears it.
    bic w3, w4, #0x4000
    str w3, [x19]
    ldr w4, [x19]
    cbnz w4, failed
    // Neither ACK nor lock status can be forged with a MODE write.
    orr w3, w22, #0x2000
    str w3, [x19]
    ldr w4, [x19]
    cbnz w4, failed
    mov w3, #4
    str w3, [x19]
    ldr w4, [x19]
    tst w4, w22
    b.ne failed
    mov w3, #1
    str w3, [x21]
    mov x0, x19
    bl wait_lock
    // A shifted nine-bit divider supplies a real branch clock.
    mov x9, #0x200000
    add x9, x9, #0x800
    mov w3, #0x1000000
    str w3, [x9]
    ldr w4, [x9]
    cmp w4, w3
    b.ne failed
    mov w3, #1
    str w3, [x9, #4]
    ldr w4, [x9, #4]
    cmp w4, #1
    b.ne failed
    str wzr, [x21]
    ldr w4, [x9, #4]
    tbz w4, #31, failed
    str w3, [x21]
    mov x0, x19
    bl wait_lock
    // Root completion requires the vote event without a status read from the separate PLL.
    mov x0, x17
    bl wait_root
    ldr w4, [x16, #0x10]
    cmp w4, #32
    b.ne failed

    mov w3, #41
    str w3, [x20, #0x10]
    mov w3, #0xfaaa
    str w3, [x20, #0x14]
    mov w3, #1
    movk w3, #0x200, lsl #16
    str w3, [x20, #0x18]
    mov w3, #4
    str w3, [x20]
    mov w3, #0x11
    str w3, [x21]
    mov x0, x20
    bl wait_lock

    // Each master reads its own vote. Their requests combine at the oscillator.
    add x27, x21, #0x5000
    mov w3, #1
    str w3, [x27]
    ldr w4, [x21]
    cmp w4, #0x11
    b.ne failed
    mov w3, #0x10
    str w3, [x21]
    ldr w4, [x19]
    and w4, w4, w22
    cmp w4, w22
    b.ne failed
    ldr w4, [x27]
    cmp w4, #1
    b.ne failed
    ldr w4, [x17]
    cmp w4, #2
    b.ne failed
    // The DCP vote remains independent of the other two masters.
    add x28, x27, #0x5000
    mov w3, #1
    str w3, [x28]
    str wzr, [x27]
    ldr w4, [x27]
    cbnz w4, failed
    ldr w4, [x28]
    cmp w4, #1
    b.ne failed
    ldr w4, [x19]
    and w4, w4, w22
    cmp w4, w22
    b.ne failed
    // Removing the final vote stops both consumers, but not PLL4.
    str wzr, [x28]
    ldr w4, [x17]
    tbz w4, #31, failed
    ldr w4, [x16]
    tst w4, w22
    b.ne failed
    ldr w4, [x19]
    tst w4, w22
    b.ne failed
    ldr w4, [x20]
    and w4, w4, w22
    cmp w4, w22
    b.ne failed

    // Reset and an invalid multiplier must each stop a voted PLL.
    str wzr, [x20]
    ldr w4, [x20]
    tst w4, w22
    b.ne failed
    mov w3, #4
    str w3, [x20]
    mov x0, x20
    bl wait_lock
    str wzr, [x20, #0x10]
    ldr w4, [x20]
    tst w4, w22
    b.ne failed
    mov w3, #41
    str w3, [x20, #0x10]
    mov x0, x20
    bl wait_lock

    // Direct RUN does not require a vote.
    mov w3, #1
    str w3, [x19, #0x18]
    str w3, [x19, #4]
    mov w3, #5
    str w3, [x19]
    mov x0, x19
    bl wait_lock
    ldr w4, [x17]
    tbz w4, #31, failed
    str wzr, [x19, #4]
    ldr w4, [x19]
    tst w4, w22
    b.ne failed

    // GPLL0 even output: 600 MHz / 2. Keep the main gate clear.
    mov w3, #0x402
    movk w3, #0x200, lsl #16
    str w3, [x19, #0x18]
    mov w3, #1
    str w3, [x21]
    mov x0, x19
    bl wait_lock
    mov x23, #0x200000
    add x24, x23, #0x100
    // SDCC ROM selector and HID: 300 MHz * 2 / 8 = 75 MHz.
    mov w3, #0x607
    str w3, [x23, #4]
    mov w3, #3
    str w3, [x23]
    mov x0, x23
    bl wait_root
    // The unrelated main output must remain unavailable.
    // The pending UPDATE keeps the established crystal output.
    mov w3, #0x103
    str w3, [x24, #4]
    mov w3, #3
    str w3, [x24]
    ldr w4, [x24]
    tbz w4, #0, failed
    tbnz w4, #31, failed

    // Enabling main must resolve its pending UPDATE without a second command.
    ldr w3, [x19, #0x18]
    orr w3, w3, #1
    str w3, [x19, #0x18]
    mov x0, x24
    bl wait_root
    // Disable only even. Main must remain usable and locked.
    bic w3, w3, #2
    str w3, [x19, #0x18]
    ldr w4, [x23]
    tbz w4, #31, failed
    ldr w4, [x24]
    tbnz w4, #31, failed
    // Even UPDATE must wait for its own gate.
    mov w3, #3
    str w3, [x23]
    ldr w4, [x23]
    tbz w4, #0, failed
    ldr w3, [x19, #0x18]
    orr w3, w3, #2
    str w3, [x19, #0x18]
    mov x0, x23
    bl wait_root

    // Reprogram a running PLL using the OEM L, alpha, USER, latch sequence.
    mov w3, #32
    str w3, [x19, #0x10]
    str wzr, [x19, #0x14]
    ldr w3, [x19, #0x18]
    str w3, [x19, #0x18]
    ldr w3, [x19]
    orr w3, w3, #0x4000
    str w3, [x19]
    ldr w4, [x19]
    tbz w4, #14, failed
    tbz w4, #13, failed
    bic w3, w4, #0x4000
    str w3, [x19]
    ldr w4, [x19]
    tbnz w4, #14, failed
    tbnz w4, #13, failed
    mov x0, x19
    bl wait_lock
    ldr w4, [x19, #0x10]
    cmp w4, #32
    b.ne failed
    ldr w4, [x19, #0x14]
    cbnz w4, failed
    // Both consumers recover without another root UPDATE request.
    mov x0, x23
    bl wait_root
    mov x0, x24
    bl wait_root

    // Change the actual postdivider from /2 to /4, then use dual-edge M/N.
    ldr w3, [x19, #0x18]
    orr w3, w3, #0xc00
    str w3, [x19, #0x18]
    mov w3, #0x2607
    str w3, [x23, #4]
    mov w3, #2
    str w3, [x23, #8]
    mov w3, #-7
    str w3, [x23, #12]
    mov w3, #-9
    str w3, [x23, #16]
    mov w3, #3
    str w3, [x23]
    mov x0, x23
    bl wait_root
    // A malformed complement cannot receive an UPDATE acknowledgment.
    // The root keeps its active configuration.
    str wzr, [x23, #16]
    mov w3, #3
    str w3, [x23]
    ldr w4, [x23]
    tbz w4, #0, failed
    tbnz w4, #31, failed
    mov w3, #-9
    str w3, [x23, #16]
    mov w3, #3
    str w3, [x23]
    mov x0, x23
    bl wait_root
    // Branch controls are retained. HW_CTL uses explicit continuous demand.
    add x25, x23, #0x200
    add x26, x23, #0x300
    ldr w4, [x25]
    mov w3, #0x80000000
    cmp w4, w3
    b.ne failed
    mov w3, #1
    str w3, [x25]
    ldr w4, [x25]
    cmp w4, #1
    b.ne failed
    mov w3, #2
    str w3, [x25]
    ldr w4, [x25]
    cmp w4, #2
    b.ne failed
    // The symbol mux starts on the live PLL-backed root.
    ldr w4, [x26]
    cbnz w4, failed
    // Removal of the PLL source revokes both active root outputs.
    str wzr, [x21]
    ldr w4, [x23]
    tbz w4, #31, failed
    ldr w4, [x24]
    tbz w4, #31, failed
    // Polling an enabled branch cannot restore its missing source.
    mov w5, #32
7:  ldr w4, [x25]
    mov w3, #2
    movk w3, #0x8000, lsl #16
    cmp w4, w3
    b.ne failed
    subs w5, w5, #1
    b.ne 7b
    // The actual map uses selector 2 for crystal, not selector 1.
    mov w3, #2
    str w3, [x26]
    ldr w4, [x26]
    cmp w4, #2
    b.ne failed
    ldr w4, [x25]
    cmp w4, #2
    b.ne failed
    // Returning to the absent PHY revokes the branch without changing enables.
    str wzr, [x26]
    ldr w4, [x25]
    mov w3, #2
    movk w3, #0x8000, lsl #16
    cmp w4, w3
    b.ne failed
    // Explicit disable remains off even with the crystal selected.
    str wzr, [x25]
    mov w3, #2
    str w3, [x26]
    ldr w4, [x25]
    mov w3, #0x80000000
    cmp w4, w3
    b.ne failed
    // An independent branch vote cannot override source loss.
    mov w29, #0x400
    movk w29, #0x20, lsl #16
    mov w3, #2
    str w3, [x29, #8]
    ldr w4, [x25]
    tbz w4, #31, failed
    mov w3, #1
    str w3, [x29]
    ldr w4, [x25]
    cbnz w4, failed
    str wzr, [x29, #8]
    ldr w4, [x25]
    cbnz w4, failed
    str wzr, [x26]
    ldr w4, [x25]
    tbz w4, #31, failed
    mov w3, #2
    str w3, [x26]
    ldr w4, [x25]
    cbnz w4, failed
    str wzr, [x29]
    ldr w4, [x25]
    tbz w4, #31, failed
    // Either independent bank can retain the request after the other releases it.
    mov w3, #4
    str w3, [x29, #0x40]
    ldr w4, [x25]
    cbnz w4, failed
    mov w3, #1
    str w3, [x29]
    str wzr, [x29, #0x40]
    ldr w4, [x25]
    cbnz w4, failed
    mov w3, #4
    str w3, [x29, #0x40]
    str wzr, [x29]
    ldr w4, [x29]
    cbnz w4, failed
    ldr w4, [x29, #0x40]
    cmp w4, #4
    b.ne failed
    ldr w4, [x25]
    cbnz w4, failed
    // The second bank also cannot override an absent upstream source.
    str wzr, [x26]
    ldr w4, [x25]
    tbz w4, #31, failed
    mov w3, #2
    str w3, [x26]
    ldr w4, [x25]
    cbnz w4, failed
    str wzr, [x29, #0x40]
    ldr w4, [x25]
    tbz w4, #31, failed
    // A third independent bank keeps the branch running alone.
    mov w3, #8
    str w3, [x29, #0x80]
    ldr w4, [x29, #0x80]
    cmp w4, #8
    b.ne failed
    ldr w4, [x25]
    cbnz w4, failed
    // Each bank retains the request in turn while the other two stay released.
    mov w3, #1
    str w3, [x29]
    str wzr, [x29, #0x80]
    ldr w4, [x25]
    cbnz w4, failed
    mov w3, #4
    str w3, [x29, #0x40]
    str wzr, [x29]
    ldr w4, [x25]
    cbnz w4, failed
    mov w3, #8
    str w3, [x29, #0x80]
    str wzr, [x29, #0x40]
    ldr w4, [x25]
    cbnz w4, failed
    // Removing the last of the three votes stops the branch.
    str wzr, [x29, #0x80]
    ldr w4, [x25]
    tbz w4, #31, failed
    // An additional source must wait for its own PLL.
    mov w3, #0x301
    str w3, [x24, #4]
    mov w3, #3
    str w3, [x24]
    ldr w4, [x24]
    tbz w4, #0, failed
    tbz w4, #31, failed
    // The highest bank has independent configuration and vote bit eleven.
    add x28, x19, #0xb000
    mov w3, #25
    str w3, [x28, #0x10]
    mov w3, #1
    movk w3, #0x200, lsl #16
    str w3, [x28, #0x18]
    mov w3, #4
    str w3, [x28]
    mov w3, #0x100
    str w3, [x28, #0x2c]
    mov w3, #0x800
    str w3, [x27]
    // Transfer the coarse code with the firmware's register masks before waiting for fine lock.
    ldr w4, [x28, #0xc]
    lsr w4, w4, #1
    and w4, w4, #0x1f000
    mov w3, #0xd000
    cmp w4, w3
    b.ne failed
    orr w4, w4, #0x400
    str w4, [x28, #0x20]
    str wzr, [x28, #0x2c]
    mov x0, x28
    bl wait_lock
    mov x0, x24
    bl wait_root
    ldr w4, [x19, #0x10]
    cmp w4, #32
    b.ne failed
    str wzr, [x27]
    ldr w4, [x28]
    tst w4, w22
    b.ne failed
    ldr w4, [x24]
    tbz w4, #31, failed
    // Select another additional source. PLL11 cannot satisfy its request.
    mov w3, #0x701
    str w3, [x24, #4]
    mov w3, #3
    str w3, [x24]
    mov w3, #0x800
    str w3, [x27]
    mov x0, x28
    bl wait_lock
    // The pending UPDATE keeps the restored PLL11 output.
    ldr w4, [x24]
    tbz w4, #0, failed
    tbnz w4, #31, failed
    mov w3, #0x10
    str w3, [x21]
    mov x0, x24
    bl wait_root
    str wzr, [x21]
    ldr w4, [x24]
    tbz w4, #31, failed
    ldr w4, [x28]
    and w4, w4, w22
    cmp w4, w22
    b.ne failed
    // DFS enable does not complete an unprogrammed level.
    mov x17, #0x200000
    add x17, x17, #0x700
    mov w3, #2
    str w3, [x17]
    mov w3, #0x21
    str w3, [x17, #0x14]
    ldr w4, [x17, #0x14]
    tbz w4, #9, failed
    ldr w4, [x17]
    tbz w4, #31, failed
    // Populate level zero after enable. Completion requires acquisition.
    mov w3, #1
    str w3, [x17, #0x1c]
    mov x0, x17
    bl wait_root
    ldr w4, [x17, #0x14]
    cmp w4, #0x21
    b.ne failed
    // The last implemented level retains all four fields, including full complements.
    mov w3, #4
    str w3, [x17, #0x78]
    mov w3, #-22
    str w3, [x17, #0xb8]
    mov w3, #-26
    str w3, [x17, #0xf8]
    ldr w4, [x17, #0xf8]
    cmp w4, w3
    b.ne failed
    mov w3, #0x2601
    str w3, [x17, #0x38]
    // Select level seven through software override. GPLL0 is unavailable.
    mov w3, #0x3c21
    str w3, [x17, #0x14]
    ldr w4, [x17, #0x14]
    tbz w4, #9, failed
    tst w4, #0x1e
    b.ne failed
    mov w3, #1
    str w3, [x21]
    mov x0, x17
    bl wait_root
    ldr w4, [x17, #0x14]
    mov w3, #0x3c2f
    cmp w4, w3
    b.ne failed
    // Loss of the selected PLL stops the output without another DFS request.
    str wzr, [x21]
    ldr w4, [x17]
    tbz w4, #31, failed
    // The first unsupported level does not wrap or acknowledge.
    mov w3, #0x4421
    str w3, [x17, #0x14]
    ldr w4, [x17, #0x14]
    tbz w4, #9, failed
    and w4, w4, #0x1e
    cmp w4, #14
    b.ne failed
    // Disabling DFS returns to the conventional shadow/update contract.
    str wzr, [x17, #0x14]
    mov w3, #1
    str w3, [x17, #4]
    mov w3, #3
    str w3, [x17]
    mov x0, x17
    bl wait_root
    mov x0, #0x600d0000
    str w4, [x0]
1:  b 1b

wait_lock:
    mov w5, #10000
2:  ldr w4, [x0]
    and w4, w4, w22
    cmp w4, w22
    b.eq 3f
    subs w5, w5, #1
    b.ne 2b
    b failed
3:  ret

wait_root:
    mov w5, #10000
5:  ldr w4, [x0]
    cmp w4, #2
    b.eq 6f
    subs w5, w5, #1
    b.ne 5b
    b failed
6:  ret

failed:
    mov x0, #0xbad00000
    str w4, [x0]
4:  b 4b

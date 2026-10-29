.section .text
.globl _start
_start:
    movz x0, #0x8000
    movk x0, #0x221c, lsl #16

    // The XBL load combines the serial word first and the chip word second.
    ldr x1, [x0, #0x610]
    movz x2, #0xcdef
    movk x2, #0x89ab, lsl #16
    movk x2, #0x4567, lsl #32
    movk x2, #0x0123, lsl #48
    cmp x1, x2
    b.ne failed

    // Existing 32-bit reads retain their values.
    ldr w1, [x0, #0x610]
    movz w2, #0xcdef
    movk w2, #0x89ab, lsl #16
    cmp w1, w2
    b.ne failed
    ldr w1, [x0, #0x614]
    movz w2, #0x4567
    movk w2, #0x0123, lsl #16
    cmp w1, w2
    b.ne failed

    // Each authorization view extracts its bit from the authoritative policy word.
    ldr w3, [x0, #0xcf0]
    cmp w3, #0x810
    b.ne failed
    ldr w1, [x0, #0x71c]
    ubfx w2, w3, #1, #1
    cmp w1, w2
    b.ne failed
    ldr w1, [x0, #0x724]
    ubfx w2, w3, #4, #1
    cmp w1, w2
    b.ne failed
    ldr x1, [x0, #0x728]
    ubfx w2, w3, #5, #1
    ubfx w4, w3, #11, #1
    orr x2, x2, x4, lsl #32
    cmp x1, x2
    b.ne failed

    // The corrected row preserves policy. Aggregate errors select both failure bitmaps.
    movz x4, #0x2090
    movk x4, #0x0230, lsl #16
    ldr x1, [x4]
    cmp x1, x3
    b.ne failed
    ldr x1, [x4, #8]
    cmn x1, #1
    b.ne failed
    ldr x1, [x4, #0x10]
    cmn x1, #1
    b.ne failed
    ldr w1, [x4, #0xc]
    cmn w1, #1
    b.ne failed
    ldr w1, [x4, #0x14]
    cmn w1, #1
    b.ne failed

    // The two writable control words remain writable through 32-bit transfers.
    mov w2, #0x123
    str w2, [x0, #0x1004]
    ldr w1, [x0, #0x1004]
    cmp w1, w2
    b.ne failed
    movz w2, #0xcdef
    movk w2, #0x89ab, lsl #16
    str w2, [x0, #0x100c]
    ldr w1, [x0, #0x100c]
    cmp w1, w2
    b.ne failed

    // The complete PTE word remains distinct from debug-vector and QC policy.
    movz x4, #0x20ec
    movk x4, #0x221c, lsl #16
    ldr w1, [x4]
    movz w2, #0x0100
    movk w2, #0xa5a5, lsl #16
    cmp w1, w2
    b.ne failed
    ubfx w2, w1, #8, #1
    cmp w2, #1
    b.ne failed
    ldr w1, [x0, #0x780]
    cmp w1, #0x800
    b.ne failed
    ldr w1, [x0, #0x200]
    cbnz w1, failed
#if defined(FUSE_PTE_WRITE)
    str wzr, [x4]
    b failed
#elif defined(FUSE_PTE_NEIGHBOR)
    ldr w1, [x4, #4]
    b failed
#endif

    // QFPROM ROM error status preserves the declared two-bit field.
    movz x4, #0x2090
    movk x4, #0x221c, lsl #16
    ldr w1, [x4]
    movz w2, #3, lsl #16
    cmp w1, w2
    b.ne failed
    ubfx w1, w1, #16, #2
    cmp w1, #3
    b.ne failed
#ifdef FUSE_ROM_ERROR_WRITE
    str wzr, [x4]
    b failed
#endif

    // QSEE qsee_get_secure_state reads SW_RANGE4 + 0x10 after the fuse
    // self-test. Bit 28 reports the secure HW key as programmed. The selected
    // profile programs the OEM root hash. The word accepts reads only.
    ldr w1, [x0, #0x10]
    movz w2, #0x1000, lsl #16
    cmp w1, w2
    b.ne failed
#ifdef FUSE_KEY_STATUS_WRITE
    str wzr, [x0, #0x10]
    b failed
#endif

    // QSEE secure-state checks also read the fuse-protection and MRC words.
    ldr w1, [x0, #0x40]
    movz w2, #0xc060
    movk w2, #0x1800, lsl #16
    cmp w1, w2
    b.ne failed
    ldr w1, [x0, #0x44]
    cbnz w1, failed
    ldr w1, [x0, #0x48]
    movz w2, #0x1000, lsl #16
    cmp w1, w2
    b.ne failed
    ldr w1, [x0, #0x4c]
    cbnz w1, failed
    ldr w1, [x0, #0x50]
    movz w2, #0x800, lsl #16
    cmp w1, w2
    b.ne failed
    ldr w1, [x0, #0x54]
    cbnz w1, failed
    ldr w1, [x0, #0x318]
    cbnz w1, failed
    ldr w1, [x0, #0x630]
    cbnz w1, failed

    // QFPROM_BIST_CTRL reports idle before a start. A zero write changes
    // nothing. A start bit completes the virtual fuse self-test, clears
    // START, and sets FUSE_SENSE_DONE in FC_STATUS. Reserved bits are ignored.
    ldr w1, [x0, #0x1054]
    cbnz w1, failed
    ldr w1, [x0, #0x1058]
    cbnz w1, failed
    str wzr, [x0, #0x1054]
    ldr w1, [x0, #0x1058]
    cbnz w1, failed
    mov w2, #1
    str w2, [x0, #0x1054]
    ldr w1, [x0, #0x1054]
    cbnz w1, failed
    ldr w1, [x0, #0x1058]
    cmp w1, #1
    b.ne failed
    mov w2, #-1
    str w2, [x0, #0x1054]
    ldr w1, [x0, #0x1054]
    cbnz w1, failed
    ldr w1, [x0, #0x1058]
    cmp w1, #1
    b.ne failed

    // The first word exists, but the adjacent reserved word does not.
    ldr x1, [x0, #0x548]
    b failed

failed:
    movz x0, #0
    movk x0, #0xbad0, lsl #16
    str w1, [x0]
1:  b 1b

// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
.macro write address, value
    ldr x0, =(\address)
    ldr w1, =(\value)
    str w1, [x0]
.endm
.macro expect address, value
    ldr x0, =(\address)
    ldr w1, [x0]
    ldr w2, =(\value)
    cmp w1, w2
    b.ne failed
.endm
.macro reject_write address, value
    ldr x0, =(\address)
    mov x16, #0x1000000000
    add x0, x0, x16
    ldr w1, =(\value)
    bl probe_rejected_write
    cmp x2, #1
    b.ne failed
.endm
.macro key_words base
    write (\base + 0x20), 0x10203040
    write (\base + 0x24), 0x50607080
    write (\base + 0x28), 0x90a0b0c0
    write (\base + 0x2c), 0xd0e0f000
    write (\base + 0x00), 0x11223344
    write (\base + 0x04), 0x55667788
.endm
.macro fill first, last, value
    ldr x3, =(\first)
    ldr x4, =(\last)
    ldr w5, =(\value)
1:
    str w5, [x3], #4
    cmp x3, x4
    b.lo 1b
.endm
.macro check_range first, last, value
    ldr x3, =(\first)
    ldr x4, =(\last)
    ldr w5, =(\value)
1:
    ldr w6, [x3], #4
    cmp w6, w5
    b.ne failed
    cmp x3, x4
    b.lo 1b
.endm
.macro dare_config mode
    write 0x34920000, 0x80000000
    write 0x34920004, 0
    write 0x34920008, 0x80030000
    write 0x3492000c, 0
    write 0x34920010, 0x80060000
    write 0x34920014, 0
    write 0x34920040, (\mode)
    write 0x34920044, 0
    write 0x34920030, 0x80000000
    write 0x34920034, 0
    write 0x34920038, 0x80030000
    write 0x3492003c, 0
.endm
.macro dare_start operation
    write 0x34920060, (\operation)
    write 0x34920064, 0
    write 0x34920064, 1
.endm
.macro dare_cleanup
    write 0x3492006c, 1
    write 0x34920064, 2
.endm
.macro check_preserved
    check_range 0x80000000, 0x80040000, 0xa5c37912
    expect 0x8005fffc, 0x31415926
    expect 0x80080000, 0x27182818
    expect 0x34d10080, 0xf03
    expect 0x34d10020, 0x10203040
    expect 0x34d10004, 0x55667788
    expect 0x34d10000, 0x11223344
    expect 0x34d10024, 0x50607080
    expect 0x34d10028, 0x90a0b0c0
    expect 0x34d1002c, 0xd0e0f000
.endm
_start:
#ifdef REJECTED_OFFSET
    write (0x34800000 + REJECTED_OFFSET), 1
    b failed
#endif
    msr spsel, #1
    adr x0, vectors
    msr vbar_el3, x0
    isb

    // Preserve boundary fields across both native region blocks.
    write 0x34865008, 0x5a5a00f0
    write 0x3486500c, 0x89abcdf0
    write 0x34865010, 0xa5a50081
    write 0x34865058, 0x5a5a00f5
    write 0x34865100, 0x89abcdf5
    write 0x34865104, 0xa5a50086
    write 0x34865108, 0xfedcba9e
    write 0x3486513c, 0x5a5a00f9
    // Retain complete configuration words, ten error bits, and bank differences.
    write 0x31865000, 0x80003ff0
    write 0x32865000, 0x40003ff1
    write 0x31c65000, 0x20003ff0
    write 0x32c65000, 0x10003ff1
    expect 0x31865000, 0x80003ff0
    expect 0x32865000, 0x40003ff1
    expect 0x31c65000, 0x20003ff0
    expect 0x32c65000, 0x10003ff1
    expect 0x34865000, 0xf0003ff1
    expect 0x34c65000, 0x00003ff0
    write 0x31865004, 1
    write 0x32865004, 2
    write 0x31c65004, 4
    write 0x32c65004, 8
    expect 0x34865004, 15
    expect 0x34c65004, 0
    write 0x34c65004, 0x12345678
    expect 0x34c65004, 0x12345678
    // Publish disabled, programmed, then enabled state without a ready substitute.
    write 0x34865000, 0x3ff0
    expect 0x34c65000, 0x3ff0
    write 0x34865000, 0x3ff1
    expect 0x34c65000, 0x3ff1
    // Lock the last bank so a sequential, non-atomic broadcast would alter earlier banks.
    reject_write 0x318640f8, 2
    expect 0x318640f8, 0
    write 0x32c640f8, 1
    expect 0x32c640f8, 1
    expect 0x348640f8, 1
    expect 0x34c640f8, 0
    reject_write 0x34865000, 0
    expect 0x34c65000, 0x3ff1
    reject_write 0x34c65004, 0
    expect 0x34c65004, 0x12345678
    reject_write 0x3486513c, 0
    expect 0x34c6513c, 0x5a5a00f9
    reject_write 0x34c640f8, 0
    expect 0x318640f8, 0
    expect 0x328640f8, 0
    expect 0x31c640f8, 0
    expect 0x32c640f8, 1
    // Other banks remain writable until their own lock assertion.
    write 0x3186513c, 0x81
    expect 0x3186513c, 0x81
    expect 0x32c6513c, 0x5a5a00f9
    write 0x348640f8, 1
    expect 0x34c640f8, 1
    write 0x34c640f8, 1
    reject_write 0x318640f8, 0
    reject_write 0x31865000, 0x3ff1
    .irp address,0x31865004,0x32865008,0x31c6500c,0x32c65010,0x31865058,0x32865100,0x31c65104,0x32c65108,0x3186513c
        reject_write \address, 0
    .endr
    expect 0x31865004, 0x12345678
    expect 0x32865008, 0x5a5a00f0
    expect 0x31c6500c, 0x89abcdf0
    expect 0x32c65010, 0xa5a50081
    expect 0x31865058, 0x5a5a00f5
    expect 0x32865100, 0x89abcdf5
    expect 0x31c65104, 0xa5a50086
    expect 0x32c65108, 0xfedcba9e
    expect 0x3186513c, 0x81
    expect 0x34c640f8, 1
    write 0x80000000, 0xa5c37912
    // A local request must not complete another bank's operation.
    expect 0x34803004, 2
    write 0x34803000, 2
    expect 0x34803004, 2
    write 0x31803000, 3
    expect 0x31803004, 0x11
    expect 0x32803004, 2
    expect 0x34803004, 0x13
    expect 0x34c03004, 0
    write 0x31803008, 1
    expect 0x31803004, 1
    write 0x34803000, 3
    expect 0x31803004, 1
    expect 0x34c03004, 1
    expect 0x34803004, 0x11
    write 0x34803000, 2
    write 0x34803000, 3
    expect 0x34c03004, 0x11
    write 0x34c03008, 1
    expect 0x34803004, 1
    write 0x34803000, 4
    write 0x34803000, 5
    expect 0x34c03004, 0x12
    write 0x34803008, 1
    expect 0x34803004, 2

    // Mask results change only when the controller accepts the request.
    write 0x3487c020, 0x80010005
    write 0x3487c024, 0x7ffefffa
    expect 0x3487c038, 0
    write 0x3487c028, 2
    expect 0x34c7c038, 0x22
    expect 0x34c7c03c, 0x80000002
    write 0x3487c024, 0x7ffefffe
    expect 0x3487c038, 0x22
    write 0x3487c028, 2
    expect 0x3487c038, 2
    // Disabled high bits must not enter the completion mask.
    write 0x3487c020, 0x60000005
    write 0x3487c024, 0x7ffffffa
    write 0x3487c028, 2
    expect 0x3487c038, 0x22
    expect 0x3487c03c, 0
    write 0x348a6050, 0x10005
    write 0x348a6010, 0xfffefffa
    write 0x348a6014, 5
    expect 0x348a6024, 0
    write 0x348a6018, 2
    expect 0x34ca6024, 0x22
    expect 0x34ca6058, 2

    expect 0x34c45240, 0
    write 0x34845218, 0xfffffffa
    write 0x34845214, 0x01000000
    expect 0x34c45240, 1
    write 0x34843908, 1
    expect 0x34c43408, 0x10000000
    expect 0x80000000, 0xa5c37912

    // Each group and entry starts empty. Duplicate writes are not six words.
    .irp group,0,1,2,3
        .irp entry,0,1,2,3
            expect (0x34910080 + \group * 0x1000 + \entry * 0x100), 0
            expect (0x34d10080 + \group * 0x1000 + \entry * 0x100), 0
        .endr
    .endr
    write 0x31910020, 0
    write 0x31910020, 0x10203040
    write 0x31910024, 0x50607080
    write 0x31910028, 0x90a0b0c0
    write 0x3191002c, 0xd0e0f000
    write 0x31910000, 0x11223344
    expect 0x31910080, 0
    expect 0x34910080, 0
    expect 0x34d10080, 0
    write 0x31910004, 0x55667788
    expect 0x31910080, 0xf03
    expect 0x32910080, 0
    expect 0x34910080, 0xf03
    expect 0x34d10080, 0
    expect 0x31910180, 0
    expect 0x31911080, 0
    // Complete the other banks through both broadcast write views.
    write 0x34910020, 0x10203040
    expect 0x31910080, 0
    write 0x34d10024, 0x50607080
    write 0x34910028, 0x90a0b0c0
    write 0x3491002c, 0xd0e0f000
    write 0x34910000, 0x11223344
    expect 0x34d10080, 0
    write 0x34910004, 0x55667788
    expect 0x34d10080, 0xf03
    // Replacement changes only bank zero and requires six fresh words.
    write 0x31910000, 0
    expect 0x31910080, 0
    expect 0x32910080, 0xf03
    expect 0x34910080, 0xf03
    expect 0x34d10080, 0
    expect 0x31910000, 0
    expect 0x32910000, 0x11223344
    expect 0x34910000, 0x11223344
    expect 0x34d10000, 0
    write 0x31910004, 0
    write 0x31910020, 0
    write 0x31910024, 0
    write 0x31910028, 0
    expect 0x31910080, 0
    write 0x3191002c, 0
    expect 0x34d10080, 0xf03
    // Exercise all sixteen slots without aliasing their retained words.
    .irp group,0,1,2,3
        .irp entry,0,1,2,3
            key_words (0x34910000 + \group * 0x1000 + \entry * 0x100)
            expect (0x34d10080 + \group * 0x1000 + \entry * 0x100), 0xf03
            expect (0x34d10000 + \group * 0x1000 + \entry * 0x100), 0x11223344
            expect (0x34d10004 + \group * 0x1000 + \entry * 0x100), 0x55667788
            expect (0x34d10020 + \group * 0x1000 + \entry * 0x100), 0x10203040
            expect (0x34d10024 + \group * 0x1000 + \entry * 0x100), 0x50607080
            expect (0x34d10028 + \group * 0x1000 + \entry * 0x100), 0x90a0b0c0
            expect (0x34d1002c + \group * 0x1000 + \entry * 0x100), 0xd0e0f000
        .endr
    .endr
    write 0x31913320, 0xabcdef01
    expect 0x31913380, 0
    expect 0x31913280, 0xf03
    expect 0x31912380, 0xf03
    expect 0x32913380, 0xf03
    expect 0x31913220, 0x10203040
    expect 0x31912320, 0x10203040

    // Initialization must preserve all data and the installed keys.
    fill 0x80000000, 0x80040000, 0xa5c37912
    fill 0x80060000, 0x80080000, 0xdeadbeef
    write 0x8005fffc, 0x31415926
    write 0x80080000, 0x27182818
    write 0x34903350, 1
    dare_config 0
    dare_start 3
    expect 0x34d20074, 0x600
    expect 0x34d20070, 0
    expect 0x34d03500, 0
    reject_write 0x34903500, 0x80
    check_range 0x80060000, 0x80080000, 0xdeadbeef
    dare_cleanup
    // Command one uses configured bounds, not a stale submitted interval.
    write 0x34920030, 0x80060000
    write 0x34920038, 0x80050000
    dare_start 1
    expect 0x34d20074, 3
    check_preserved
    check_range 0x80060000, 0x80080000, 0xdeadbeef
    dare_cleanup
    expect 0x34d20074, 0
    expect 0x34d20070, 0
    expect 0x34d03500, 0x80

    dare_config 1
    // The native TME path starts encryption-only rows without submitted bounds.
    write 0x34920030, 0
    write 0x34920038, 0
    dare_start 1
    expect 0x34d20074, 3
    expect 0x34d20070, 1
    expect 0x34d20078, 0
    expect 0x34d03500, 0x80
    reject_write 0x34920070, 0
    check_preserved
    check_range 0x80060000, 0x80080000, 0xdeadbeef
    dare_cleanup

    // Mode two uses operation three and only its auxiliary allocation.
    dare_config 2
    dare_start 1
    expect 0x34d20074, 0x600
    check_range 0x80060000, 0x80080000, 0xdeadbeef
    dare_cleanup
    dare_start 3
    expect 0x34d20074, 3
    check_range 0x80060000, 0x80070000, 0
    check_range 0x80070000, 0x80080000, 0xdeadbeef
    check_preserved
    dare_cleanup

    // A failed key or bounds check must not clear metadata.
    fill 0x80060000, 0x80080000, 0xdeadbeef
    write 0x34913320, 0xabcdef01
    write 0x34920044, 15
    dare_start 3
    expect 0x34d20074, 0x600
    check_range 0x80060000, 0x80080000, 0xdeadbeef
    dare_cleanup
    write 0x34920044, 0
    write 0x34920038, 0x80040000
    dare_start 3
    expect 0x34d20074, 0x600
    check_range 0x80060000, 0x80080000, 0xdeadbeef
    dare_cleanup

    // Mode three clears a larger allocation than mode two.
    dare_config 3
    dare_start 3
    expect 0x34d20074, 3
    check_range 0x80060000, 0x80080000, 0
    check_preserved
    dare_cleanup

    // Each request must clear metadata again, including a corrupted allocation.
    write 0x80060000, 0x12345678
    write 0x8007fffc, 0x87654321
    dare_start 3
    expect 0x34d20074, 3
    check_range 0x80060000, 0x80080000, 0
    check_preserved
    dare_cleanup
    write 0x80060000, 0x12345678
    write 0x8007fffc, 0x87654321
    write 0x34903010, 0
    write 0x34903010, 1
    expect 0x34d03014, 0x10
    expect 0x34d20074, 3
    check_range 0x80060000, 0x80080000, 0
    check_preserved
    write 0x34903338, 1
    expect 0x34d03014, 0
    expect 0x34d20074, 0
    write 0x34900088, 1
    dsb sy
    isb
    expect 0x80000000, 0xa5c37912

    // Equal configuration writes and the native low-byte clear preserve context.
    dare_config 3
    write 0x34900088, 0x101
    ldr x0, =0x34900088
    strb wzr, [x0]
    write 0x34900088, 0x101
    dsb sy
    isb
    expect 0x80000000, 0xa5c37912
#ifdef REVOKE_BACKGROUND
    // Row 28 is foreground because its policy has bit 16 set.
    write 0x3493c000, 0x80000000
    write 0x3493c004, 0
    write 0x3493c008, 0x80000000
    write 0x3493c00c, 0
    write 0x3493c040, 0x10000
    write 0x3493c044, 0
    write 0x3493c030, 0x80000000
    write 0x3493c034, 0
    write 0x3493c038, 0x80000000
    write 0x3493c03c, 0
    write 0x34903350, 0x10000001
    write 0x3493c060, 1
    write 0x3493c064, 1
    expect 0x34d3c074, 3
    dsb sy
    isb
    expect 0x80000000, 0xa5c37912
    expect 0x80010000, 0xa5c37912
    write 0x80060000, 0x12345678
    dsb sy
    isb
    expect 0x80000000, 0xa5c37912
    expect 0x80010000, 0xa5c37912
    b failed
#endif
#if defined(REVOKE_METADATA)
    write 0x80060000, 0x12345678
#elif defined(REVOKE_KEY)
    write 0x34910020, 0x10203040
#elif defined(REVOKE_CONFIG)
    write 0x34920044, 1
#endif
#if defined(REVOKE_METADATA) || defined(REVOKE_KEY) || defined(REVOKE_CONFIG)
    expect 0x34d03500, 0
    dsb sy
    isb
    expect 0x80000000, 0xa5c37912
    b failed
#endif
    write 0x600d0000, 1
failed:
    write 0xbad00000, 1
1:  b 1b
.ltorg

// Recover only the exact expected write instruction. Other faults still fail.
probe_rejected_write:
    mov x2, #0
rejected_write_instruction:
    str w1, [x0]
    ret
.balign 2048
vectors:
.rept 4
    b failed
    .balign 128
.endr
    b expected_data_abort
    .balign 128
.rept 11
    b failed
    .balign 128
.endr
expected_data_abort:
    mrs x16, esr_el3
    lsr x16, x16, #26
    cmp x16, #0x25
    b.ne failed
    mrs x16, elr_el3
    adr x17, rejected_write_instruction
    cmp x16, x17
    b.ne failed
    mov x2, #1
    add x16, x16, #4
    msr elr_el3, x16
    eret

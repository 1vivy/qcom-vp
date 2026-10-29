// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef RANK1_PRESENT
#define RANK1_PRESENT 0
#endif
.section .text
.globl _start

// x0 points at the pair's physical +0x5700.
.macro descriptor slot, op, ranks, mr=0, data=0
    ldr w2, =((\op) | ((\ranks) << 12) | ((\mr) << 16) | ((\data) << 24))
    str w2, [x0, #(8 * (\slot))]
.endm
.macro execute mask
    ldr w2, =(\mask)
    str w2, [x0, #0x100]
    mov w2, #1
    str w2, [x0, #0x104]
    // A clear trigger is valid only when the corresponding result is available.
    ldr w2, [x0, #0x104]
    tbnz w2, #0, failed
.endm
.macro expect offset, value
    ldr w2, [x0, #(\offset)]
    ldr w3, =(\value)
    cmp w2, w3
    b.ne failed
.endm
.macro expect_ready value
    sub x9, x0, #5, lsl #12
    ldr w2, [x9, #8]
    cmp w2, #(\value)
    b.ne failed
.endm
// Submit through the production SHRM frontend, not through PXI task registers.
.macro shrm_request request, response
    ldr x0, =0x31076000
    str wzr, [x0, #0x64]
    mov w2, #1
    str w2, [x0, #0x60]
    ldr w2, =(\request)
    str w2, [x0, #0x68]
    mov w2, #1
    str w2, [x0, #0x64]
    ldr w2, [x0, #0x6c]
    ldr w3, =(\response)
    cmp w2, w3
    b.ne failed
.endm
_start:
#if !RANK1_PRESENT && !defined(UNSUPPORTED_OPCODE)
    ldr x0, =0x34b875fc
    mov w2, #0x1234
    str w2, [x0]
    adr x19, mc_pairs
    mov w20, #4
mc_loop:
    ldr x0, [x19], #8
    expect 0, 0x1234
    subs w20, w20, #1
    b.ne mc_loop
    ldr x0, =0x31b875fc
    mov w2, #0x5678
    str w2, [x0]
    expect 0, 0x5678
    ldr x0, =0x32b875fc
    expect 0, 0x1234
    ldr x0, =0x34b875fc
    expect 0, 0x5678
    mov w2, #0xabcd
    str w2, [x0]
    ldr x0, =0x31b875fc
    expect 0, 0xabcd
    ldr x0, =0x32f875fc
    expect 0, 0xabcd
    ldr x0, =0x34b86788
    expect 0, 0
    mov w2, #3
    str w2, [x0]
    ldr x0, =0x31b86788
    expect 0, 3
    mov w2, #2
    str w2, [x0]
    expect 0, 2
    ldr x0, =0x32b86788
    expect 0, 3
    // Calibration requires a start request and a publication edge.
    ldr x0, =0x31b88000
    expect 0xf98, 0
    expect 0xfa0, 0
    expect 0xfa4, 0
    ldr x1, =0x31b86ecc
    mov w2, #0x20000
    str w2, [x1]
    expect 0xf98, 0
    str wzr, [x1]
    mov w2, #0x02000000
    str w2, [x0, #0xe74]
    mov w2, #2
    str w2, [x0, #0xe98]
    mov w2, #0x1001
    str w2, [x0, #0xe7c]
    expect 0xf98, 0
    expect 0xfa4, 0
    mov w2, #0x20000
    str w2, [x1]
    expect 0xf98, 0x200
    expect 0xfa0, 0x10000000
    expect 0xfa4, 0x1010
    // A new request invalidates the old result before publication.
    mov w2, #0x1000
    str w2, [x0, #0xe7c]
    expect 0xfa4, 0x1010
    mov w2, #0x1001
    str w2, [x0, #0xe7c]
    expect 0xf98, 0
    expect 0xfa4, 0
    mov w2, #0x20000
    str w2, [x1]
    expect 0xf98, 0
    str wzr, [x1]
    mov w2, #0x20000
    str w2, [x1]
    expect 0xf98, 0x200
    // Another bank uses the alternate status field and independent state.
    ldr x0, =0x32b88000
    expect 0xf98, 0
    expect 0xfa4, 0
    mov w2, #2
    str w2, [x0, #0xea4]
    mov w2, #0x1001
    str w2, [x0, #0xe7c]
    ldr x1, =0x32b86ecc
    mov w2, #0x20000
    str w2, [x1]
    expect 0xf98, 0
    expect 0xfa4, 0x1010
    ldr x0, =0x32b87000
    expect 0xb4, 0x40000
    // Broadcast publication also completes the fourth bank.
    ldr x0, =0x34b88000
    mov w2, #2
    str w2, [x0, #0xe98]
    mov w2, #0x1001
    str w2, [x0, #0xe7c]
    ldr x1, =0x34b86ecc
    str wzr, [x1]
    mov w2, #0x20000
    str w2, [x1]
    ldr x0, =0x32f88000
    expect 0xfa4, 0x1010
    ldr x0, =0x32f87000
    expect 0xb4, 0x40000
    // The second calibration operation publishes both selected results.
    ldr x0, =0x31b86000
    expect 0x538, 0
    mov w2, #6
    str w2, [x0, #0x4b4]
    expect 0x538, 0
    str wzr, [x0, #0x4b4]
    mov w2, #3
    str w2, [x0, #0xcb8]
    mov w2, #0x0f000000
    str w2, [x0, #0x4ac]
    mov w2, #6
    str w2, [x0, #0x4b4]
    expect 0x538, 0x11010
    // Cleanup retains results. A new request replaces them.
    str wzr, [x0, #0x4b4]
    expect 0x538, 0x11010
    mov w2, #4
    str w2, [x0, #0x4b4]
    expect 0x538, 0x10010
    str wzr, [x0, #0x4b4]
    mov w2, #7
    str w2, [x0, #0x4b4]
    expect 0x538, 0x11000
    // Missing request selection cannot reuse the previous completion.
    str wzr, [x0, #0x4b4]
    str wzr, [x0, #0x4ac]
    mov w2, #6
    str w2, [x0, #0x4b4]
    expect 0x538, 0
    ldr x0, =0x32b86000
    expect 0x538, 0
    // Broadcast requests produce independent results in every selected bank.
    ldr x0, =0x34b86000
    str wzr, [x0, #0x4b4]
    mov w2, #3
    str w2, [x0, #0xcb8]
    mov w2, #0x0f000000
    str w2, [x0, #0x4ac]
    mov w2, #6
    str w2, [x0, #0x4b4]
    ldr x0, =0x32f86000
    expect 0x538, 0x11010
#endif
    ldr x0, =0x34a00000
    mov w2, #0x321
    str w2, [x0, #0x80]
    expect 0x80, 0x321
    ldr x0, =0x31a00000
    expect 0x80, 0x321
    mov w2, #0x456
    str w2, [x0, #0x80]
    ldr x0, =0x32a00000
    expect 0x80, 0x321
    ldr x0, =0x34a00000
    expect 0x80, 0x456
    ldr x0, =0x31a05700
#ifdef UNSUPPORTED_OPCODE
    descriptor 0, 0x3ff, 1
    execute 1
    b failed
#endif
    // Unselected slot zero must not execute. Both sparse endpoints must execute.
    descriptor 0, 0x3ff, 1
    descriptor 2, 0x74, 1, 13, 0x5a
    descriptor 31, 0x70, 1, 13
    ldr w2, =0x130123
    str w2, [x0, #0xfc]
    execute 0x80000004
    expect 0x338, 0x5a
    expect 0xfc, 0x130123
    expect 0x100, 0x80000004
    // A read of another MR must replace the previous result before completion.
    descriptor 31, 0x70, 1, 14
    execute 0x80000000
    expect 0x338, 0
    descriptor 31, 0x70, 1, 13
    execute 0x80000000
    expect 0x338, 0x5a
    // Rank one must never alias rank zero, whether present or absent.
    descriptor 2, 0x74, 2, 13, 0xa6
    descriptor 31, 0x70, 3, 13
    execute 0x80000004
    expect 0x338, 0x5a
#if RANK1_PRESENT
    expect 0x33c, 0xa6
#else
    expect 0x33c, 0xffffffff
#endif
    // No local MR write may change another controller pair.
    ldr x0, =0x32a05700
    descriptor 0, 0x70, 3, 13
    execute 1
    expect 0x338, 0
#if RANK1_PRESENT
    expect 0x33c, 0
#else
    expect 0x33c, 0xffffffff
#endif
    // Opcode50 publishes only the requested pair's rank-completion bits.
    expect_ready 0
    descriptor 0, 0x50, 1
    execute 1
    expect_ready 0x100
    ldr x0, =0x31e05700
    expect_ready 0
    // Broadcast writes and descriptors must reach all four independent banks.
    ldr x0, =0x34a05700
    descriptor 5, 0x74, 3, 13, 0x3c
    descriptor 7, 0x70, 3, 13
    execute 0xa0
    expect 0x338, 0x3c
    adr x19, pairs
    mov w20, #4
pair_loop:
    ldr x0, [x19], #8
    descriptor 7, 0x70, 3, 13
    execute 0x80
    expect 0x338, 0x3c
#if RANK1_PRESENT
    expect 0x33c, 0x3c
    expect 0x594, 0
#else
    expect 0x33c, 0xffffffff
    expect 0x594, 0x10
#endif
    // Reconstruct the native rank mask from status, not from an MR constant.
    ldr w4, [x0, #0x594]
    mov w5, #0
    tbnz w4, #0, no_rank0
    orr w5, w5, #1
no_rank0:
    tbnz w4, #4, no_rank1
    orr w5, w5, #2
no_rank1:
    cmp w5, #(1 + 2 * RANK1_PRESENT)
    b.ne failed
    // Decode MR8's density and width fields as the native geometry consumer does.
    descriptor 7, 0x70, 1, 8
    execute 0x80
    ldr w4, [x0, #0x338]
    and w5, w4, #3
    cbnz w5, failed
    ubfx w5, w4, #2, #4
    cmp w5, #4
    b.ne failed
    ubfx w6, w4, #6, #1
    cbnz w6, failed
    // The native density/width lookup supplies row and column address widths.
    adr x9, geometry
    add w10, w5, w5
    add w10, w6, w10
    lsl w10, w10, #1
    ldrb w5, [x9, x10]
    add x10, x10, #1
    ldrb w6, [x9, x10]
    cmp w5, #15
    b.ne failed
    cmp w6, #10
    b.ne failed
    add w7, w5, w6
    mov x8, #1
    lsl x8, x8, x7
    lsl x8, x8, #5
    lsr x8, x8, #20
    cmp x8, #1024
    b.ne failed
    lsl x8, x8, #2
    cmp x8, #1, lsl #12
    b.ne failed
    // Each x8 transport lane owns half of the x16 controller capacity.
    lsr x8, x8, #3
    cmp x8, #512
    b.ne failed
    // Electrical/control commands also complete on the absent-rank route.
    descriptor 0, 0x60, 2
    descriptor 1, 0xa4, 3
    descriptor 2, 0x00, 0
    descriptor 3, 0xa0, 3
    descriptor 4, 0x64, 3
    descriptor 5, 0x8c, 3
    descriptor 6, 0x58, 3
    descriptor 7, 0x50, 3
    execute 0xff
    expect_ready 0x300
    // Control commands must preserve the preceding mode-register result.
    expect 0x338, 0x10
    // The flagged native MRW must change MR16 before completion.
    descriptor 0, 0x74, 3, 16, 0x5a
    descriptor 1, 0x77, 3, 16, 0
    descriptor 2, 0x70, 3, 16
    execute 7
    expect 0x338, 0
#if RANK1_PRESENT
    expect 0x33c, 0
#else
    expect 0x33c, 0xffffffff
#endif
    // MR37 controls the ideal oscillator count on both x8 lanes.
    // Its write and measurement can share one atomically validated batch.
    descriptor 0, 0x74, 3, 37, 8
    descriptor 1, 0x94, 3
    descriptor 2, 0x70, 3, 35
    execute 7
    expect 0x338, 0x8080
#if RANK1_PRESENT
    expect 0x33c, 0x8080
#else
    expect 0x33c, 0xffffffff
#endif
    // Change the interval: 512 CK produces low zero and high two.
    descriptor 0, 0x74, 1, 37, 32
    descriptor 1, 0x94, 1
    descriptor 2, 0x70, 1, 35
    execute 7
    expect 0x338, 0
    descriptor 2, 0x70, 3, 36
    execute 4
    expect 0x338, 0x0202
#if RANK1_PRESENT
    expect 0x33c, 0
#else
    expect 0x33c, 0xffffffff
#endif
    // Electrical operations preserve the measured MR values and result.
    descriptor 0, 0x8c, 3
    descriptor 1, 0x58, 3
    execute 3
    expect 0x338, 0x0202
    // Extended MR37 encoding must also derive a count, not a ready constant.
    descriptor 0, 0x74, 1, 37, 0x80
    descriptor 1, 0x94, 1
    descriptor 2, 0x70, 1, 36
    execute 7
    expect 0x338, 0x1010
    // Native rank-absence controls retain requests without fabricating status.
    add x9, x0, #0xa48
    sub x10, x0, #2, lsl #12
    sub x10, x10, #0x680
    mov w2, #-1
    str w2, [x9]
    ldr w2, [x9]
    ldr w3, =0x01000ff0
    and w2, w2, w3
    orr w2, w2, #0x10000000
    str w2, [x9]
    ldr w4, [x9]
    ldr w3, =0x11000ff0
    cmp w4, w3
    b.ne failed
    mov w2, #1
    str w2, [x10]
    ldr w4, [x10]
    cmp w4, #1
    b.ne failed
    ldr w2, [x9]
    ldr w3, =0x01000ff0
    and w2, w2, w3
    str w2, [x9]
    ldr w4, [x9]
    cmp w4, w3
    b.ne failed
    str wzr, [x10]
    ldr w4, [x10]
    cbnz w4, failed
    subs w20, w20, #1
    b.ne pair_loop
    // Apply the native rank policy after op50 sets both observations.
    ldr x0, =0x31a05700
    expect_ready 0x300
    expect 0x50c, 0x30000
    sub x10, x0, #0x700
    ldr w2, =0xa5000003
    str w2, [x10, #0x328]
    mov w2, #1
    str w2, [x10, #0x20]
    // Clear rank zero with the native mask. Rank one must remain set.
    ldr w2, [x10, #0x328]
    and w4, w2, #2
    ldr w3, =0xf33fffb0
    and w2, w2, w3
    orr w2, w2, w4
    str w2, [x10, #0x328]
    expect_ready 0x300
    expect 0x50c, 0x30000
    mov w2, #1
    str w2, [x10, #0x20]
    expect_ready 0x200
    expect 0x50c, 0x20000
    ldr w2, [x10, #0x328]
    ldr w3, =0xa1000002
    cmp w2, w3
    b.ne failed
    // A local policy must not change another pair or its mode registers.
    ldr x0, =0x32a05700
    expect_ready 0x300
    expect 0x50c, 0x30000
    ldr x0, =0x31a05700
    descriptor 0, 0x70, 1, 36
    execute 1
    expect 0x338, 0x1010
    // Restore the selected rank through the same accepted request.
    ldr w2, [x10, #0x328]
    orr w2, w2, #1
    str w2, [x10, #0x328]
    expect_ready 0x200
    mov w2, #1
    str w2, [x10, #0x20]
    expect_ready 0x300
    expect 0x50c, 0x30000
    // The final publication follows accepted runtime configuration.
    ldr x0, =0x34a06000
    expect 0x604, 0
    mov w2, #1
    str w2, [x0, #0x41c]
    str w2, [x0, #0x20]
    expect 0x20, 0
    expect 0x604, 0
    mov w2, #1
    str w2, [x0, #0x604]
    expect 0x604, 1
    // PXI must change shared memory before it reports completion.
    ldr x0, =0x80000000
    ldr w2, =0xdeadbeef
    str w2, [x0, #0x80]
    ldr x0, =0x31a0e000
    mov w2, #0x40000000
    str w2, [x0, #0x308]
    ldr w2, =0x00100010
    str w2, [x0, #0x600]
    orr w2, w2, #0x40000000
    str w2, [x0, #0xa00]
    mov w2, #0x0c000000
    str w2, [x0, #0x680]
    str w2, [x0, #0xa80]
    ldr w2, =0x11223344
    str w2, [x0, #0x780]
    str w2, [x0, #0xb80]
    ldr w2, =0x55667788
    str w2, [x0, #0xf80]
    str w2, [x0, #0xd80]
    ldr x0, =0x31a08000
    mov w2, #3
    str w2, [x0, #0x20]
    expect 0x2c, 0x1f
    expect 0x70, 34
    ldr x0, =0x80000000
    expect 0, 0x11223344
    expect 0x3c, 0x11223344
    expect 0x40, 0x55667788
    expect 0x80, 0xdeadbeef
    // The last repetition crosses both a channel stripe and the aperture gap.
    ldr x0, =0x90000000
    expect 0, 0x11223344
    expect 0x40, 0x55667788
    // CPU corruption must become a PXI mismatch, not an unconditional pass.
    ldr x0, =0x80000000
    ldr w2, =0x11223345
    str w2, [x0]
    ldr x0, =0x31a08000
    mov w2, #1
    str w2, [x0, #0x20]
    expect 0x2c, 0x1f
    expect 0x70, 68
    ldr x0, =0x31a0f000
    expect 0xce8, 1
    expect 0xcec, 0
    ldr x0, =0x31a08000
    mov w2, #1
    str w2, [x0, #0x74]
    expect 0x70, 0
    ldr x0, =0x31a0f000
    expect 0xce8, 1
    // Exercise runtime row12 with only two clock-plan levels.
    // Callback selection must not use the clock-plan count as a row bound.
    ldr x20, =0x3104a800
    ldr x21, =0x31050000
    ldr x22, =0x31051000
    str x21, [x20, #0x468]
    str x22, [x20, #0x470]
    mov w2, #0x0202
    strh w2, [x20, #0x2c]
    ldr w2, =0x000100a4
    str w2, [x21, #0x34]
    ldr w2, =0x0b000200
    str w2, [x21, #0x38]
    ldr w2, =0x00c00d00
    str w2, [x21, #0x3c]
    ldr w2, =0x00010dc0
    str w2, [x21, #0x40]
    mov w2, #1
    str w2, [x22, #0x90]
    ldr w2, =550000
    str w2, [x22, #0x88]
    ldr x0, =0x31079000
    str wzr, [x0]
    // Native reconstruction must not leave stale inactive sizes or source bases.
    add x9, x20, #0x48
    mov w10, #52
    mov w11, #-1
seed_publication:
    str w11, [x9], #4
    subs w10, w10, #1
    b.ne seed_publication
    shrm_request 0x09080000, 0x080800f0
#if !RANK1_PRESENT
    // Publication uses controller capacities, not the eight PXI lane capacities.
    mov x0, x20
    expect 0x30, 0x0f0008ff
    expect 0x34, 0x10000a00
    expect 0x38, 0x04000000
    expect 0x3c, 0x00000100
    expect 0x40, 0x00000100
    expect 0x48, 1024
    expect 0x4c, 1024
    expect 0x50, 1024
    expect 0x54, 1024
    add x9, x20, #0x58
    mov w10, #12
publication_sizes:
    ldr w11, [x9], #4
    cbnz w11, failed
    subs w10, w10, #1
    b.ne publication_sizes
    add x9, x20, #0x98
    mov w10, #16
publication_bases:
    ldr x11, [x9], #8
    cbnz x11, failed
    subs w10, w10, #1
    b.ne publication_bases
    mov x0, x22
    expect 0x600, 4
    ldrh w2, [x0, #0x604]
    cmp w2, #8
    b.ne failed
#endif
    // The AND view combines every pair and shares their actual retained state.
    ldr x0, =0x31a00000
    mov w2, #0xff0
    str w2, [x0, #0x80]
    ldr x0, =0x32a00000
    mov w2, #0xf0f
    str w2, [x0, #0x80]
    ldr x0, =0x31e00000
    mov w2, #0xfff
    str w2, [x0, #0x80]
    ldr x0, =0x32e00000
    mov w2, #0x3ff
    str w2, [x0, #0x80]
    ldr x0, =0x34e00000
    expect 0x80, 0x300
    mov w2, #1827
    str w2, [x0, #0x80]
    expect 0x80, 1827
    ldr x0, =0x31a00000
    expect 0x80, 1827
    ldr x0, =0x32a00000
    expect 0x80, 1827
    ldr x0, =0x31e00000
    expect 0x80, 1827
    ldr x0, =0x32e00000
    expect 0x80, 1827
    // Use the real shadow context and guest-owned output buffers.
    ldr x21, =0x3104d000
    ldr x2, =0x31060000
    str x2, [x21, #0x450]
    ldr x2, =0x3105c000
    str x2, [x21, #0x468]
    ldr x2, =0x3105d000
    str x2, [x21, #0x470]
    ldr x22, =0x31052000
    str x22, [x21, #0x478]
    mov w2, #1
    str w2, [x20, #0x47c]
    // Select rank0, level0, profile0, and both x8 lanes of pair0.
    strb w2, [x20, #0x41]
    ldr x0, =0x3104c05a
    mov w2, #3
    strb w2, [x0]
    ldr x0, =0x31048c1d
    mov w2, #1
    strb w2, [x0]
    ldr x0, =0x31048ac8
    mov w3, #0
pin_map:
    strb w3, [x0], #1
    add w3, w3, #1
    cmp w3, #9
    b.ne pin_map
    mov w3, #0
pin_map_half1:
    strb w3, [x0], #1
    add w3, w3, #1
    cmp w3, #9
    b.ne pin_map_half1
    // Row names are synthetic fixture data, not captured firmware bytes.
    add x0, x22, #0x316
    adr x1, callback_write_name
    bl copy_name
    mov w2, #1
    strb w2, [x22, #0x32f]
    add x0, x22, #0x356
    adr x1, callback_eye_name
    bl copy_name
    mov w2, #1
    strb w2, [x22, #0x36f]
    ldr x0, =0x90000dfc
    ldr w2, =0xabcdef01
    str w2, [x0]
    ldr x0, =0x90002c80
    str w2, [x0]
    // The callback must replace the stale comparison error with real traffic.
    shrm_request 0x0980000c, 0x088000f0
    ldr x0, =0x31a08000
    expect 0x70, 512
    expect 0x2c, 0x1f
    ldr x0, =0x31a0f000
    expect 0xce8, 0
    expect 0xcec, 0
    ldr x0, =0x90000e00
    expect 0, 0x11223344
    expect 0x40, 0x55667788
    ldr x0, =0x90002c00
    expect 0x3c, 0x11223344
    expect 0x7c, 0x55667788
    expect 0x80, 0xabcdef01
    ldr x0, =0x90000dfc
    expect 0, 0xabcdef01
    // The normalized program must not replace the retained native program.
    ldr x0, =0x31a0e000
    expect 0x600, 0x00100010
    expect 0xa00, 0x40100010
    expect 0x680, 0x0c000000
    expect 0x308, 0x40000000
    // A read-eye callback must compare without repairing DRAM.
    shrm_request 0x0980000d, 0x088000f0
    // HCD calibrates a noncanonical baseline and preserves disabled DMI pins.
    add x0, x22, #0x696
    adr x1, callback_hcd_name
    bl copy_name
    mov w2, #1
    strb w2, [x22, #0x6af]
    ldr x0, =0x3104b880
    mov w2, #70
    strb w2, [x0]
    strb w2, [x0, #8]
    ldr x0, =0x31048988
    mov w2, #0x10000
    str w2, [x0]
    str w2, [x0, #4]
    ldr x0, =0x31b80000
    str wzr, [x0, #0x140]
    str wzr, [x0, #0x88]
    mov w2, #1
    str w2, [x0, #0x154]
    mov w2, #0x1000
    str w2, [x0, #0x168]
    mov w2, #7
    str w2, [x0, #8]
    mov w2, #0x1234
    str w2, [x0, #0x1820]
    shrm_request 0x0980001a, 0x088000f0
    ldr x0, =0x31b80000
    expect 0x88, 1
    expect 0x154, 1
    expect 0x168, 0
    expect 0x1a00, 1
    expect 0x20c8, 1
    expect 0x21a4, 0
    expect 8, 7
    expect 0x1820, 0x1234
    ldr x0, =0x310642b0
    expect 0, 3
    // A fixed physical oscillator must remain stable across clock changes.
    ldr x21, =0x3104d000
    ldr x23, =0x31054000
    str x23, [x21, #0x458]
    add x0, x22, #0x616
    adr x1, callback_dit_name
    bl copy_name
    mov w2, #1
    strb w2, [x22, #0x62f]
    ldr x0, =0x3104b1f6
    strb w2, [x0]
    mov w2, #10
    strb w2, [x23, #0xc30]
    ldr x0, =0x3104c0e0
    mov w2, #0xffff
    strh w2, [x0]
    ldr x0, =0x3104829e
    mov w2, #16
    strb w2, [x0]
    strb w2, [x0, #4]
    ldr x0, =0x3104acbc
    ldr w2, =547200
    str w2, [x0]
    ldr x0, =0x3104ba90
    mov w2, #1827
    str w2, [x0]
    shrm_request 0x09800018, 0x088000f0
    mov x0, x23
    expect 0, 585
    expect 0xc30, 0x018f000a
    expect 0x600, 128
    ldr x0, =0x3104b1f6
    mov w2, #3
    strb w2, [x0]
    ldr x0, =0x3104acbc
    ldr w2, =4800000
    str w2, [x0]
    ldr x0, =0x3104ba90
    mov w2, #209
    str w2, [x0]
    shrm_request 0x09800018, 0x088000f0
    mov x0, x23
    expect 0, 268
    expect 0xc30, 0x018f000a
    expect 0x600, 256
    // A zero margin exhausts the interval list without inventing a margin.
    ldr x0, =0x3104c0e0
    strh wzr, [x0]
    shrm_request 0x09800018, 0x088000f0
    mov x0, x23
    expect 0, 8561
    expect 0x600, 8192
    expect 0x630, 0
    ldr x0, =0x90002c7c
    ldr w2, =0x54667788
    str w2, [x0]
#ifdef CORRUPT_CALLBACK
    // Expected failure variant: no SHRM acknowledgment or eye records.
    shrm_request 0x0980000d, 0x088000f0
    b failed
#else
    // Observe the corrupted byte through the same normalized comparison engine.
    ldr x0, =0x31a0e000
    mov w2, #0x44000000
    str w2, [x0, #0x308]
    ldr w2, =0x401000ff
    str w2, [x0, #0xa00]
    ldr x0, =0x31a08000
    mov w2, #1
    str w2, [x0, #0x74]
    mov w2, #0x1d
    str w2, [x0, #0x20]
    expect 0x70, 512
    ldr x0, =0x31a0f000
    expect 0xce8, 0
    expect 0xcec, 0x01000000
#endif
    mov x0, #0x600d0000
    str w2, [x0]
failed:
    mov x0, #0xbad00000
    str w2, [x0]
1:  b 1b
copy_name:
    ldrb w2, [x1], #1
    strb w2, [x0], #1
    cbnz w2, copy_name
    ret
callback_write_name:
    .asciz "RD_Low_Freq_Write"
callback_eye_name:
    .asciz "PHY_SED_RD"
callback_dit_name:
    .asciz "DIT - Periodic Training"
callback_hcd_name:
    .asciz "PHY_Trn_WR_HCD_V2"
.balign 8
pairs:
    .quad 0x31a05700, 0x32a05700, 0x31e05700, 0x32e05700
mc_pairs:
    .quad 0x31b875fc, 0x32b875fc, 0x31f875fc, 0x32f875fc
.ltorg
// The fixture uses only the evidenced width-zero density-four geometry.
geometry:
    .zero 16
    .byte 15, 10, 0, 0

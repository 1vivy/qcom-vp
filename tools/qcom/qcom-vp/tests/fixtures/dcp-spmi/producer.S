// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
.macro address reg, value
    movz \reg, #(\value & 0xffff)
    movk \reg, #((\value >> 16) & 0xffff), lsl #16
.endm
.macro expect reg, value
    address w15, \value
    cmp \reg, w15
    b.ne failed
.endm
.macro discover peripheral, revision, kind
    mov w0, #\peripheral
    str w0, [x11]
    address w0, 0x08000006
    str w0, [x23]
    ldr w0, [x23, #8]
    expect w0, 1
    ldr w0, [x23, #0x18]
    expect w0, \revision
    ldr w0, [x23, #0x1c]
    expect w0, \kind
.endm
_start:
    // XBL configures both routes before the DCP reads the shared tables.
    address x10, 0x0c403000
    mov w0, #0x16
    str w0, [x10]
    address w0, 0x80000016
    str w0, [x10, #4]
    address x10, 0x0c42a000
    str wzr, [x10]
    mov w0, #11
    str w0, [x10, #4]
    address x10, 0x0c427804
    mov w0, #1
    str w0, [x10]
    address x10, 0x0c438804
    str w0, [x10]
    address x10, 0x4c400000
    ldr w0, [x10]
    expect w0, 0x80000000
    ldr w0, [x10, #4]
    expect w0, 0x20000800
    ldr w0, [x10, #8]
    expect w0, 0x08000200
    address x10, 0x4c403000
    ldr w0, [x10, #4]
    expect w0, 0x80000016
    address x10, 0x4c42a000
    ldr w0, [x10, #4]
    expect w0, 11
#if CASE == 1
    // DCP cannot change the shared ownership table.
    str wzr, [x10, #4]
    b failed
#elif CASE == 2
    // The DCP endpoint cannot use the EE0 observer bank.
    address x10, 0x4c4c0000
    ldr w0, [x10, #8]
    b failed
#elif CASE == 3
    // The APSS endpoint cannot use the EE11 observer bank.
    address x10, 0x0c780000
    ldr w0, [x10, #8]
    b failed
#elif CASE == 4
    // Bus one's unimplemented APID gap stays inaccessible.
    address x10, 0x4ca40000
    ldr w0, [x10, #8]
    b failed
#elif CASE == 5
    // An unknown PMIC byte remains a visible backend error.
    address x10, 0x4c780000
    address w0, 0x08000510
    str w0, [x10]
    b failed
#elif CASE == 6
    // Capabilities are read-only on the configuration endpoint too.
    address x10, 0x0c400004
    str wzr, [x10]
    b failed
#endif
#if CASE >= 7
    // Seven-byte reads preserve each declared type and its discovery dummy.
    address x11, 0x0c403000
    address x23, 0x4c780000
    discover 4, 0, 0
    discover 8, 0x01000100, 0x0801
    discover 0x70, 0x01000100, 0x012e
    discover 0x88, 0x01000301, 0x1310
    discover 0x50, 0x00010100, 0x0106
    discover 0x61, 0x01000501, 0x0107
    discover 0xc1, 0x01000100, 0x3004
    discover 0x16, 0x01000501, 0x0116
#if CASE >= 11
    // The explicit virtual absence policy logs this peripheral only once.
    discover 2, 0, 0
    discover 2, 0, 0
#if CASE == 12
    address w0, 0x08000070
    str w0, [x23]
    b failed
#elif CASE == 13
    // Revision writes keep their existing rejection.
    mov w0, #2
    str w0, [x11, #4]
    address x22, 0x4c900200
    str wzr, [x22, #0x10]
    mov w0, #0
    str w0, [x22]
    b failed
#elif CASE == 14 || CASE == 19
    // Exploratory writes retain state and prevent a later absence response.
    mov w0, #2
    str w0, [x11, #4]
    address x22, 0x4c900200
#if CASE == 19
    mov w0, #0
#else
    mov w0, #0x5a
#endif
    str w0, [x22, #0x10]
    mov w0, #0x400
    str w0, [x22]
    ldr w0, [x22, #8]
    expect w0, 1
    address w0, 0x08000400
    str w0, [x23]
    ldr w0, [x23, #8]
    expect w0, 1
    ldr w0, [x23, #0x18]
#if CASE == 19
    expect w0, 0
#else
    expect w0, 0x5a
#endif
    // A separate APID distinguishes this error from the successful control read.
    mov w0, #2
    str w0, [x11, #8]
    address x24, 0x4c780040
    address w0, 0x08000006
    str w0, [x24]
    b failed
#elif CASE == 15
    // An exploratory read does not create state or block absence discovery.
    address w0, 0x08000400
    str w0, [x23]
    ldr w0, [x23, #8]
    expect w0, 1
    ldr w0, [x23, #0x18]
    expect w0, 0
    discover 2, 0, 0
#elif CASE == 16
    // The policy cannot populate an absent SID.
    mov w0, #0x216
    str w0, [x11]
    address w0, 0x08000006
    str w0, [x23]
    ldr w0, [x23, #8]
    expect w0, 3
#elif CASE == 17
    // Only the complete seven-byte discovery request can report absence.
    address w0, 0x08000005
    str w0, [x23]
    b failed
#elif CASE == 18
    // A sparse declared control does not gain an invented revision.
    mov w0, #0x30
    str w0, [x11]
    address w0, 0x08000006
    str w0, [x23]
    b failed
#elif CASE == 20
    // Global lock writes keep the pre-existing XBL behavior.
    mov w0, #2
    str w0, [x11, #4]
    address x22, 0x4c900200
    str w0, [x22, #0x10]
    mov w0, #0xd10
    str w0, [x22]
    ldr w0, [x22, #8]
    expect w0, 1
    address w0, 0x08000d10
    str w0, [x23]
    ldr w0, [x23, #8]
    expect w0, 1
    ldr w0, [x23, #0x18]
    expect w0, 2
#endif
#endif
#if CASE == 8
    // The absent revision does not expose its adjacent byte.
    mov w0, #4
    str w0, [x11]
    address w0, 0x08000070
    str w0, [x23]
    b failed
#elif CASE == 9
    // A peripheral without a declared revision remains unknown.
    mov w0, #2
    str w0, [x11]
    address w0, 0x08000006
    str w0, [x23]
    b failed
#elif CASE == 10
    // Discovery dummy bytes cannot accept writes.
    address x22, 0x4c900200
    str wzr, [x22, #0x10]
    mov w0, #0x60
    str w0, [x22]
    b failed
#endif
    address x0, 0x600d0000
    str w0, [x0]
#endif
    address x20, 0x0c900000
    address x21, 0x0c4c0000
    address x22, 0x4c900200
    address x23, 0x4c780000
    // The DCP owns APID1. Its write reaches the same PMIC byte as XBL's APID0.
    mov w0, #0x5a
    str w0, [x22, #0x10]
    mov w0, #0x500
    str w0, [x22]
    ldr w0, [x22, #8]
    expect w0, 1
    address w0, 0x08000500
    str w0, [x21]
    ldr w0, [x21, #8]
    expect w0, 1
    ldr w0, [x21, #0x18]
    expect w0, 0x5a
    // XBL then changes the byte. DCP observes the new value independently.
    mov w0, #0xa5
    str w0, [x20, #0x10]
    mov w0, #0x500
    str w0, [x20]
    ldr w0, [x20, #8]
    expect w0, 1
    address w0, 0x08000500
    str w0, [x23]
    ldr w0, [x23, #8]
    expect w0, 1
    ldr w0, [x23, #0x18]
    expect w0, 0xa5
    ldr w0, [x21, #0x18]
    expect w0, 0x5a
    // Even secure CPU attributes cannot lend APSS authority to the DCP endpoint.
    address x10, 0x4c900000
    mov w0, #0x33
    str w0, [x10, #0x10]
    mov w0, #0x500
    str w0, [x10]
    ldr w0, [x10, #8]
    expect w0, 5
    address w0, 0x08000500
    str w0, [x23]
    ldr w0, [x23, #0x18]
    expect w0, 0xa5
    // The bus gate applies to both EEs.
    address x10, 0x0c427804
    str wzr, [x10]
    address w0, 0x08000500
    str w0, [x23]
    ldr w0, [x23, #8]
    expect w0, 5
    mov w0, #1
    str w0, [x10]
    // Absent bus-one SIDs report DONE|FAILURE, not a fabricated byte.
    address x10, 0x0c405000
    mov w0, #0x16
    str w0, [x10]
    address x10, 0x4c790000
    address w0, 0x08000500
    str w0, [x10]
    ldr w0, [x10, #8]
    expect w0, 3
    // PIC groups and channel IRQ state stay deasserted without a producer.
    address x10, 0x4c8cb000
    ldr w0, [x10]
    expect w0, 0
    address x10, 0x4c8db03c
    ldr w0, [x10]
    expect w0, 0
    mov w0, #1
    str w0, [x22, #0x100]
    ldr w0, [x22, #0x100]
    expect w0, 1
    ldr w0, [x22, #0x104]
    expect w0, 0
    mov w0, #0xff
    str w0, [x22, #0x108]
    ldr w0, [x22, #0x108]
    expect w0, 0
    address x0, 0x600d0000
    str w0, [x0]
failed:
    address x0, 0xbad00000
    str w0, [x0]
1:  b 1b

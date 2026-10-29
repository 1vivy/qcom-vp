// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    mov x20, #0x05f00000
    mov x21, #0x01f00000
    // Both configuration views share reporting controls, not access permissions.
    mov w1, #-1
    str w1, [x20, #0x208]
    str w1, [x21, #0x20c]
    ldr w2, [x21, #0x208]
    mov w3, #0x7ff
    movk w3, #0xc000, lsl #16
    cmp w2, w3
    b.ne failed
    ldr w2, [x20, #0x20c]
    cmp w2, w3
    b.ne failed
    str wzr, [x21, #0x208]
    ldr w2, [x20, #0x208]
    cbnz w2, failed
    ldr w2, [x20, #0x20c]
    cmp w2, w3
    b.ne failed
#if CASE == 1
    // A route without declared configuration authority cannot enable namespaces.
    mov x0, #0x06f00000
    mov w1, #1
    str w1, [x0, #0x100]
    b failed
#elif CASE == 2
    mov w1, #1
    str w1, [x21, #0x100]
    // The secure APSS profile must not authorize a nonsecure transaction.
    mrs x0, scr_el3
    orr x0, x0, #1
    orr x0, x0, #0x400
    msr scr_el3, x0
    mov x0, #0x80000000
    msr hcr_el2, x0
    mov x0, #0x3c5
    msr spsr_el3, x0
    adr x0, nonsecure
    msr elr_el3, x0
    isb
    eret
nonsecure:
    mov w1, #1
    add x0, x21, #0x1000
    str w1, [x0, #0x30]
    b failed
#elif CASE == 16 || CASE == 17
    add x22, x20, #0x1000
    mov x24, #0x14800000
    str wzr, [x22, #0xc]
    mov w1, #0x1000
    str w1, [x22, #0x14]
    mov w1, #0x80000000
    str w1, [x22, #0x18]
    str w1, [x22, #0x1c]
    mov w1, #1
    str w1, [x22, #4]
    dsb sy
    isb
    // A secure-only range must retain native instruction and data access.
    mov w1, #0x4682
    movk w1, #0x5282, lsl #16
    str w1, [x24]                 // mov w2, #0x1234
    mov w1, #0x03c0
    movk w1, #0xd65f, lsl #16
    str w1, [x24, #4]             // ret
    mov w2, #0x5678
    str w2, [x24, #0x100]
    ldr w3, [x24, #0x100]
    cmp w2, w3
    b.ne failed
    dsb sy
    isb
    blr x24
    mov w3, #0x1234
    cmp w2, w3
    b.ne failed
    // No policy change occurs between secure access and nonsecure denial.
    mrs x0, scr_el3
    orr x0, x0, #1
    orr x0, x0, #0x400
    msr scr_el3, x0
    mov x0, #0x80000000
    msr hcr_el2, x0
    mov x0, #0x3c5
    msr spsr_el3, x0
    adr x0, cross_security_nonsecure
    msr elr_el3, x0
    isb
    eret
cross_security_nonsecure:
#if CASE == 16
    ldr w2, [x24]
#else
    str w2, [x24]
#endif
    b failed
#elif CASE == 13 || CASE == 14
    add x22, x20, #0x1000
    mov x24, #0x15800000
    mov w1, #0x1234
    str w1, [x24]
    str wzr, [x22, #0xc]
    mov w1, #0x1000
    str w1, [x22, #0x14]
#if CASE == 13
    mov w1, #1
    movk w1, #0x8000, lsl #16
#else
    mov w1, #4
#endif
    str w1, [x22, #0x18]
    str w1, [x22, #0x1c]
    mov w1, #1
    str w1, [x22, #4]
    dsb sy
    isb
    // A secure transaction must still use its assigned ordinary QAD.
    ldr w2, [x24]
    mov w3, #0x1234
    cmp w2, w3
    b.ne failed
    mov w2, #0x5678
    str w2, [x24]
    ldr w3, [x24]
    cmp w2, w3
    b.ne failed
    // A QAD-two permission must not authorize the APSS port.
    mov x24, #0x14800000
    ldr w2, [x24]
    b failed
#elif CASE >= 7
    add x22, x20, #0x1000
    add x23, x21, #0x1000
#if CASE == 15
    // Match the OEM handoff: APSS programs and claims its initial policy.
    mov x22, x23
#endif
    mov x24, #0x14800000
    mov w1, #0x1234
    str w1, [x24]
    str wzr, [x22, #0xc]
    mov w1, #0x1000
    str w1, [x22, #0x14]
    mov w1, #0x35
    movk w1, #0xc000, lsl #16
    str w1, [x22, #0x18]
    mov w1, #0x11
    movk w1, #0xc000, lsl #16
    str w1, [x22, #0x1c]
    mov w1, #1
    str w1, [x22, #4]
#if CASE == 11
    str w1, [x22]
#endif
#if CASE != 15
    // Legacy ownership does not acquire a namespace policy.
    mov w1, #2
    str w1, [x22, #0x30]
#endif
    mov w1, #1
    str w1, [x21, #0x100]
    movk w1, #0xc000, lsl #16
    str w1, [x23, #0x30]
#if CASE == 15
    add x22, x20, #0x1000
    mov w1, #2
    str w1, [x22, #0x30]
#endif
    // Establish a direct mapping before the permission transition.
    ldr w2, [x24]
    str w2, [x24]
#if CASE == 10 || CASE == 15
    mov w1, #0
#elif CASE == 12
    mov w1, #0x40000000
#else
    mov w1, #0x80000000
#endif
    str w1, [x23, #0x30]
    dsb sy
    isb
#if CASE == 7
    // The shared bit zero must not restore the revoked nonsecure principal.
    ldr w2, [x23, #0x18]
    mov w3, #0x35
    movk w3, #0x8000, lsl #16
    cmp w2, w3
    b.ne failed
    ldr w2, [x23, #0x1c]
    mov w3, #0x11
    movk w3, #0x8000, lsl #16
    cmp w2, w3
    b.ne failed
    // The surviving secure class keeps the shared QAD-zero owner bit.
    ldr w2, [x23, #0x30]
    mov w3, #3
    movk w3, #0x8000, lsl #16
    cmp w2, w3
    b.ne failed
    // Acquiring ownership must not restore revoked data permission.
    mov w1, #1
    movk w1, #0xc000, lsl #16
    str w1, [x23, #0x30]
    str w1, [x23, #0x30]
    ldr w2, [x23, #0x18]
    mov w3, #0x35
    movk w3, #0x8000, lsl #16
    cmp w2, w3
    b.ne failed
    // Releasing both APSS principals must preserve unrelated permission bits.
    str wzr, [x23, #0x30]
    ldr w2, [x23, #0x18]
    cmp w2, #0x34
    b.ne failed
    ldr w2, [x23, #0x1c]
    cmp w2, #0x10
    b.ne failed
    ldr w2, [x23, #0x30]
    cmp w2, #2
    b.ne failed
    ldr w2, [x23, #4]
    cmp w2, #1
    b.ne failed
    // Releasing a nonsecure claim must not create a secure owner.
    mov w1, #1
    movk w1, #0x4000, lsl #16
    str w1, [x23, #0x30]
    mov w1, #0x80000000
    str w1, [x23, #0x30]
    ldr w2, [x23, #0x30]
    cmp w2, #2
    b.ne failed
    ldr w2, [x23, #0x18]
    cmp w2, #0x34
    b.ne failed
    mov x0, #0x600d0000
    str w2, [x0]
#elif CASE == 15
    // A namespace co-owner retains the policy after APSS releases its ownership.
    ldr w2, [x22, #0x18]
    mov w3, #0x35
    movk w3, #0xc000, lsl #16
    cmp w2, w3
    b.ne failed
    ldr w2, [x22, #0x1c]
    mov w3, #0x11
    movk w3, #0xc000, lsl #16
    cmp w2, w3
    b.ne failed
    ldr w2, [x23, #0x30]
    cmp w2, #2
    b.ne failed
    // APSS must not see another namespace's retained policy in configuration reads.
    ldr w2, [x23, #0x18]
    cmp w2, #0x34
    b.ne failed
    ldr w2, [x23, #0x1c]
    cmp w2, #0x10
    b.ne failed
    ldr w2, [x24]
    mov w3, #0x1234
    cmp w2, w3
    b.ne failed
    str w2, [x24]
    // Releasing the remaining co-owner also removes its retained permission.
    str wzr, [x22, #0x30]
    ldr w2, [x23, #0x18]
    cmp w2, #0x34
    b.ne failed
    ldr w2, [x23, #0x1c]
    cmp w2, #0x10
    b.ne failed
    dsb sy
    isb
    ldr w2, [x24]
#elif CASE == 10
    // Removing both security classes must revoke the earlier secure mapping.
    ldr w2, [x24]
#elif CASE == 12
    // Secure XBL consumes nonsecure TCSR policy before namespace activation.
    ldr w2, [x23, #0x18]
    mov w3, #0x35
    movk w3, #0x4000, lsl #16
    cmp w2, w3
    b.ne failed
    ldr w2, [x24]
    mov w3, #0x1234
    cmp w2, w3
    b.ne failed
    mov w2, #0x5678
    str w2, [x24]
    ldr w3, [x24]
    cmp w2, w3
    b.ne failed
    mov x0, #0x600d0000
    str w2, [x0]
#else
    mrs x0, scr_el3
    orr x0, x0, #1
    orr x0, x0, #0x400
    msr scr_el3, x0
    mov x0, #0x80000000
    msr hcr_el2, x0
    mov x0, #0x3c5
    msr spsr_el3, x0
    adr x0, revoked_nonsecure
    msr elr_el3, x0
    isb
    eret
revoked_nonsecure:
#if CASE == 8
    ldr w2, [x24]
#else
    str w2, [x24]
#endif
#endif
    b failed
#elif CASE >= 3
    mov x24, #0x14800000
    add x25, x24, #4, lsl #12
    mov w1, #0x1234
    str w1, [x24]
    str w1, [x24, #0x3ffc]
    add x22, x21, #0x1000
    mov w1, #1
    str w1, [x21, #0x100]
#if CASE == 4
    str wzr, [x22, #0xc]
#else
    mov w1, #0x3000
    str w1, [x22, #0xc]
#endif
#if CASE == 5
    mov w1, #0x2000
#elif CASE == 6
    mov w1, #0x5000
#else
    mov w1, wzr
#endif
    str w1, [x22, #0x14]
    mov w1, #0x80000000
    str w1, [x22, #0x18]
    str wzr, [x22, #0x1c]
    mov w1, #1
    str w1, [x22, #4]
    // Preserve the encoded zero end and protect the final aperture word.
    ldr w2, [x22, #0x14]
    cbnz w2, failed
    ldr w2, [x24, #0x3ffc]
    mov w3, #0x1234
    cmp w2, w3
    b.ne failed
    // The next word must remain writable after the earlier direct mapping.
    mov w1, #0x5678
    str w1, [x25]
    ldr w2, [x25]
    cmp w2, w1
    b.ne failed
#if CASE == 3
    str w1, [x24, #0x2ffc]
    ldr w2, [x24, #0x2ffc]
    cmp w2, w1
    b.ne failed
    str w1, [x24, #0x3ffc]
#else
    str w1, [x24]
#endif
    b failed
#else
    add x22, x20, #0x1000
    add x23, x21, #0x1000
    // Establish one active, read-only region before namespace activation.
    str wzr, [x22, #0xc]
    mov w1, #0x1000
    str w1, [x22, #0x14]
    mov w1, #0x80000000
    str w1, [x22, #0x18]
    str wzr, [x22, #0x1c]
    mov w1, #1
    str w1, [x22, #4]
    str w1, [x21, #0x100]
    // APSS and TME must acquire only their declared ownership bits.
    mov w1, #1
    movk w1, #0xc000, lsl #16
    str w1, [x23, #0x30]
    mov w1, #2
    str w1, [x22, #0x30]
    ldr w2, [x23, #0x30]
    mov w3, #3
    movk w3, #0xc000, lsl #16
    cmp w2, w3
    b.ne failed
    // Ownership must prevent range, permission, and activation changes.
    mov w1, #0x2000
    str w1, [x22, #0x14]
    mov w1, #-1
    str w1, [x22, #0x18]
    str w1, [x22, #0x1c]
    str wzr, [x22, #4]
    ldr w2, [x23, #0x14]
    cmp w2, #0x1000
    b.ne failed
    ldr w2, [x23, #0x18]
    mov w3, #0x80000000
    cmp w2, w3
    b.ne failed
    ldr w2, [x23, #0x1c]
    cbnz w2, failed
    ldr w2, [x23, #4]
    cmp w2, #1
    b.ne failed
    // A primary release frees the region for every owner and revokes their policy.
    str wzr, [x22, #0x30]
    ldr w2, [x23, #0x30]
    cbnz w2, failed
    ldr w2, [x22, #4]
    cbnz w2, failed
    ldr w2, [x22, #0x18]
    cbnz w2, failed
    ldr w2, [x20, #0x104]
    cmp w2, #2
    b.ne failed
    // The final APSS release retires an unlocked region only when its policy is empty.
    mov w1, #0x80000000
    str w1, [x22, #0x18]
    mov w1, #1
    str w1, [x22, #4]
    movk w1, #0xc000, lsl #16
    str w1, [x23, #0x30]
    str wzr, [x23, #0x30]
    ldr w2, [x22, #4]
    cbnz w2, failed
    ldr w2, [x22, #0x18]
    cbnz w2, failed
    ldr w2, [x20, #0x104]
    cmp w2, #2
    b.ne failed
    mov w1, #0x2000
    str w1, [x23, #0x14]
    mov w1, #0x12
    str w1, [x23, #0x18]
    str w1, [x23, #0x1c]
    mov w1, #1
    str w1, [x23, #4]
    mov w1, #2
    str w1, [x22, #0x30]
    // APSS cannot clear the primary owner's bit or modify its permissions.
    str wzr, [x23, #0x30]
    mov w1, #-1
    str w1, [x23, #0x18]
    ldr w2, [x22, #0x30]
    cmp w2, #2
    b.ne failed
    ldr w2, [x22, #0x18]
    cmp w2, #0x12
    b.ne failed
    // The primary owner retains the transferred policy. APSS still reads the policy it programmed.
    ldr w2, [x23, #0x18]
    cmp w2, #0x12
    b.ne failed
    ldr w2, [x22, #4]
    cmp w2, #1
    b.ne failed
    // Its final release removes the seed, including the unrelated data-domain bit.
    str wzr, [x22, #0x30]
    ldr w2, [x22, #0x18]
    cbnz w2, failed
    ldr w2, [x23, #0x1c]
    cbnz w2, failed
    ldr w2, [x22, #4]
    cbnz w2, failed
    ldr w2, [x20, #0x104]
    cmp w2, #2
    b.ne failed
    mov x0, #0x600d0000
    str w2, [x0]
#endif
failed:
    mov x0, #0xbad00000
    str w2, [x0]
1:  b 1b

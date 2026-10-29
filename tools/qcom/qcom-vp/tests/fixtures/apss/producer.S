// SPDX-License-Identifier: BSD-3-Clause-Clear
// Original guest, not firmware bytes. Link at 0x300000 and start at EL3.
// Pass: first unmapped write is 0x600d0000. Fail: 0xbad00000, x28 is stage.
.arch armv8.3-a
.section .text
.global _start
#define MPAM3 S3_6_C10_C5_0
#define MPAM2 S3_4_C10_C5_0
#define MPAM1 S3_0_C10_C5_0
#define MPAM0 S3_0_C10_C5_1
#define MPAMSM S3_0_C10_C5_3
#define MPAMIDR S3_0_C10_C4_4
#define MPAM1_EL12 S3_5_C10_C5_0
#define RNDR S3_3_C2_C4_0
#define RNDRRS S3_3_C2_C4_1

// A trap must reach the specified EL with the specified syndrome and PC.
.macro expect_mrs reg, el, ec
    mov x20, #(\el * 4)
    mov x21, #\ec
    adr x22, .Lfault\@
.Lfault\@:
    mrs x0, \reg
    cbnz x20, fail
.endm
.macro expect_msr reg, el, ec
    mov x20, #(\el * 4)
    mov x21, #\ec
    adr x22, .Lfault\@
.Lfault\@:
    msr \reg, x0
    cbnz x20, fail
.endm
.macro expect_dup el, ec
    mov x20, #(\el * 4)
    mov x21, #\ec
    adr x22, .Lfault\@
.Lfault\@:
    dup v0.2d, x3
    cbnz x20, fail
.endm
.macro check_dup value
    mov x3, #\value
    dup v0.2d, x3
    umov x4, v0.d[0]
    umov x5, v0.d[1]
    cmp x4, x3
    b.ne fail
    cmp x5, x3
    b.ne fail
.endm
.macro key reg, value
    mov x0, #\value
    msr \reg, x0
    mrs x1, \reg
    cmp x0, x1
    b.ne fail
.endm
.macro wait_amu_change reg
    mrs x4, \reg
    mov x5, #0x1000000
.Lamu_wait\@:
    mrs x6, \reg
    cmp x6, x4
    b.ne .Lamu_done\@
    subs x5, x5, #1
    b.ne .Lamu_wait\@
    b fail
.Lamu_done\@:
.endm
.macro enter label, mode
    adr x19, .Lresume\@
    adr x0, \label
    msr elr_el3, x0
    mov x0, #\mode
    msr spsr_el3, x0
    eret
.Lresume\@:
.endm
.macro random reg
    mov x2, #16
.Lretry\@:
    mrs x0, \reg
    mrs x1, nzcv
    cbz x1, .Lsuccess\@
    mov x3, #0x40000000
    cmp x1, x3
    b.ne fail
    cbnz x0, fail
    subs x2, x2, #1
    b.ne .Lretry\@
    b fail                       // Real entropy failure must not pass.
.Lsuccess\@:
.endm

_start:
    mov x28, #1
    mov x20, #0
    mov x23, #0
    mrs x0, CurrentEL
    cmp x0, #12
    b.ne fail
    adr x0, vectors
    msr vbar_el3, x0
    msr vbar_el2, x0
    msr vbar_el1, x0
    ldr x0, =0x14010000
    mov sp, x0
    msr sp_el2, x0
    msr sp_el1, x0
    isb

    mrs x0, MPAM3
    mov x1, #(1 << 62)
    cmp x0, x1
    b.ne fail
    mrs x0, MPAMIDR
    cmp x0, #1                 // HAS_HCR is one for this CPU.
    b.ne fail
    mrs x0, id_aa64pfr0_el1
    ubfx x0, x0, #40, #4
    cmp x0, #1
    b.ne fail
    mrs x0, id_aa64pfr1_el1
    ubfx x0, x0, #16, #4
    cbnz x0, fail
    mrs x0, cntfrq_el0
    ldr x1, =19200000
    cmp x0, x1
    b.ne fail

    mov x28, #2
    mov x0, #-1
    msr MPAM3, x0
    mrs x1, MPAM3
    mov x2, #(3 << 62)
    cmp x1, x2
    b.ne fail
    msr MPAM2, x0
    mrs x1, MPAM2
    ldr x2, =0x8007000000000000
    cmp x1, x2
    b.ne fail
    msr MPAM1, x0
    mrs x1, MPAM1
    mov x2, #(1 << 63)
    cmp x1, x2
    b.ne fail
    msr MPAM0, x0
    mrs x1, MPAM0
    cbnz x1, fail
    msr MPAMSM, x0
    mrs x1, MPAMSM
    cbnz x1, fail
    msr MPAM3, xzr
    msr MPAM2, xzr
    mrs x1, MPAM1
    cbnz x1, fail
    mrs x1, MPAM2
    cbnz x1, fail
    expect_mrs S3_6_C15_C15_7, 3, 0
    // HAS_HCR is one, so MPAMHCR is implemented and retains its word.
    msr S3_4_C10_C4_0, xzr
    mrs x1, S3_4_C10_C4_0
    cbnz x1, fail

    mov x28, #3
    key apiakeylo_el1, 0x1234
    key apiakeyhi_el1, 0x5678
    key apibkeylo_el1, 0x2345
    key apibkeyhi_el1, 0x6789
    key apdakeylo_el1, 0x3456
    key apdakeyhi_el1, 0x789a
    key apdbkeylo_el1, 0x4567
    key apdbkeyhi_el1, 0x89ab
    key apgakeylo_el1, 0x5678
    key apgakeyhi_el1, 0x9abc
    mrs x0, sctlr_el3
    orr x0, x0, #(1 << 31)
    msr sctlr_el3, x0
    isb
    adr x0, _start
    mov x1, #0x123
    mov x2, x0
    pacia x0, x1
    autia x0, x1
    cmp x0, x2
    b.ne fail

    mov x28, #4
    mrs x0, id_aa64isar0_el1
    ubfx x0, x0, #60, #4
    cmp x0, #1
    b.ne fail
    random RNDRRS
    random RNDR
    random RNDR

    // Allow AArch64 nonsecure EL2/EL1 and PAuth. Leave RNG trapping off.
    ldr x0, =0x30431
    msr scr_el3, x0
    ldr x0, =0x30080000000
    msr hcr_el2, x0
    isb
    mov x28, #12
    enter el1_simd, 0x3c5
    mov x0, #(1 << 62)
    msr MPAM3, x0
    mov x28, #5
    enter el2_trapped, 0x3c9
    mov x28, #6
    enter el1_trapped_el3, 0x3c5

    // EL3 trapping outranks simultaneous EL2 trapping.
    ldr x0, =0x3000000000000
    msr MPAM2, x0
    enter el1_trapped_el3, 0x3c5
    msr MPAM3, xzr
    mov x28, #7
    enter el1_trapped_el2, 0x3c5

    mov x0, #(1 << 63)
    msr MPAM3, x0
    mov x0, #(1 << 50)
    msr MPAM2, x0
    mov x28, #8
    enter el1_allowed, 0x3c5
    mov x28, #9
    enter el2_vhe, 0x3c9

    // Secure EL1 also obeys TRAPLOWER, even with Secure EL2 disabled.
    ldr x0, =0x30430
    msr scr_el3, x0
    mov x0, #(1 << 62)
    msr MPAM3, x0
    isb
    mov x28, #10
    enter el1_trapped_el3, 0x3c5

    // AMUv1 under x-qcom-ids: constants, counting, retention of a stopped
    // counter, write-at-non-highest-EL UNDEF, CPTR_EL2/EL3.TAM traps, and
    // the AMUSERENR_EL0.EN gate. Return to the earlier nonsecure state.
    mov x28, #20
    ldr x0, =0x30431
    msr scr_el3, x0
    isb
    mrs x0, S3_3_C13_C2_1          // AMCFGR_EL0
    cmp x0, #0xb
    b.ne fail
    mrs x0, S3_3_C13_C2_2          // AMCGCR_EL0
    ldr x1, =0x804
    cmp x0, x1
    b.ne fail
    mrs x0, S3_3_C13_C6_0          // AMEVTYPER00_EL0: CPU_CYCLES
    cmp x0, #0x11
    b.ne fail
    mrs x0, S3_3_C13_C6_1          // AMEVTYPER01_EL0: CNT_CYCLES
    ldr x1, =0x4004
    cmp x0, x1
    b.ne fail
    mrs x0, S3_3_C13_C6_2          // AMEVTYPER02_EL0: INST_RETIRED
    cmp x0, #0x8
    b.ne fail
    mrs x0, S3_3_C13_C6_3          // AMEVTYPER03_EL0: STALL_BACKEND_MEM
    ldr x1, =0x4005
    cmp x0, x1
    b.ne fail
    mrs x0, S3_3_C13_C2_3          // AMUSERENR_EL0 reads zero at reset.
    cbnz x0, fail

    mov x28, #21
    mov x0, #3
    msr S3_3_C13_C2_5, x0          // AMCNTENSET0_EL0: enable counters 0, 1.
    isb
    mrs x0, S3_3_C13_C2_4          // AMCNTENCLR0_EL0 reads the same enable word.
    cmp x0, #3
    b.ne fail
    wait_amu_change S3_3_C13_C4_0  // AMEVCNTR00_EL0 (CPU_CYCLES) advances.
    wait_amu_change S3_3_C13_C4_1  // AMEVCNTR01_EL0 (CNT_CYCLES) advances.

    mov x28, #22
    mov x0, #3
    msr S3_3_C13_C2_4, x0          // AMCNTENCLR0_EL0: disable counters 0, 1.
    isb
    mrs x0, S3_3_C13_C2_5
    cbnz x0, fail
    mrs x7, S3_3_C13_C4_0
    mov x5, #0x40000
.Lamu_hold:
    subs x5, x5, #1
    b.ne .Lamu_hold
    mrs x0, S3_3_C13_C4_0
    cmp x0, x7
    b.ne fail                      // A stopped counter keeps its value.

    mov x28, #23
    mov x0, #0x1234
    msr S3_3_C13_C4_2, x0          // A disabled counter accepts a highest-EL write.
    mov x0, #0xc
    msr S3_3_C13_C2_5, x0          // Enable counter 2 (INST_RETIRED) and 3 (STALL).
    isb
    mrs x0, S3_3_C13_C4_2
    ldr x1, =0x1234
    cmp x0, x1
    b.ne fail                      // This run has no precise icount, so the word stays.
    mrs x0, S3_3_C13_C4_3
    cbnz x0, fail                  // Counter 3 has no source and stays zero.
    mov x5, #0x1000000
.Lamu_inst_hold:
    subs x5, x5, #1
    b.ne .Lamu_inst_hold
    mrs x0, S3_3_C13_C4_2
    cmp x0, x1
    b.ne fail                      // An enabled counter without a source does not advance.
    mov x0, #0xc
    msr S3_3_C13_C2_4, x0
    isb

    mov x28, #24
    msr S3_3_C13_C4_0, xzr         // At EL3 (highest EL), a write is allowed.
    mrs x0, S3_3_C13_C4_0
    cbnz x0, fail
    mov x0, #0x1234
    msr S3_3_C13_C12_0, x0         // AMEVCNTR10_EL0 retains an explicit write.
    mrs x1, S3_3_C13_C12_0
    cmp x0, x1
    b.ne fail
    mov x0, #0x11
    msr S3_3_C13_C14_0, x0         // AMEVTYPER10_EL0 is writable and retained.
    mrs x1, S3_3_C13_C14_0
    cmp x0, x1
    b.ne fail

    mov x28, #25
    enter el1_amu_write, 0x3c5

    mov x28, #26
    mrs x0, cptr_el2
    orr x0, x0, #(1 << 30)         // CPTR_EL2.TAM
    msr cptr_el2, x0
    isb
    enter el1_amu_tam_el2, 0x3c5
    mrs x0, cptr_el2
    bic x0, x0, #(1 << 30)
    msr cptr_el2, x0
    isb

    mov x28, #27
    mrs x0, cptr_el3
    orr x0, x0, #(1 << 30)         // CPTR_EL3.TAM
    msr cptr_el3, x0
    isb
    enter el1_amu_tam_el3, 0x3c5
    mrs x0, cptr_el3
    bic x0, x0, #(1 << 30)
    msr cptr_el3, x0
    isb

    mov x28, #28
    enter el1_amu_userenr, 0x3c5

    mov x28, #29
    cmp x23, #44              // AMUv1 adds six traps: UNDEF, two TAM, and three EN.
    b.ne fail
    ldr x0, =0x600d0000
    str x23, [x0]
    b .

// Test reset permission before the first guest CPACR write.
el1_simd:
    check_dup 0x1357
    mov x28, #13
    mrs x0, cpacr_el1
    bic x0, x0, #(3 << 20)
    msr cpacr_el1, x0
    isb
    mov x3, #0x2468
    expect_dup 1, 7

    mov x28, #14
    mrs x0, cpacr_el1
    orr x0, x0, #(1 << 20)
    msr cpacr_el1, x0
    isb
    check_dup 0x2468

    mov x28, #15
    adr x18, el1_simd_grant_el0
    adr x0, el0_simd_trapped
    msr elr_el1, x0
    mov x0, #0x3c0
    msr spsr_el1, x0
    eret
el1_simd_grant_el0:
    mov x28, #16
    mrs x0, cpacr_el1
    orr x0, x0, #(3 << 20)
    msr cpacr_el1, x0
    isb
    adr x18, el1_simd_done
    adr x0, el0_simd_allowed
    msr elr_el1, x0
    mov x0, #0x3c0
    msr spsr_el1, x0
    eret
el1_simd_done:
    smc #0
    b fail

el0_simd_trapped:
    mov x3, #0x369c
    expect_dup 1, 7
    b el0_simd_return
el0_simd_allowed:
    check_dup 0x48ad
el0_simd_return:
    mov x20, #4
    mov x21, #0x15
    adr x22, el0_simd_svc + 4
el0_simd_svc:
    svc #0
    b fail

el2_trapped:
    expect_mrs MPAMIDR, 3, 0x18
    expect_mrs MPAM2, 3, 0x18
    expect_msr MPAM2, 3, 0x18
    expect_mrs MPAM1, 3, 0x18
    expect_mrs MPAM0, 3, 0x18
    expect_mrs MPAMSM, 3, 0x18
    expect_mrs MPAM3, 2, 0
    smc #0
    b fail

el1_trapped_el3:
    expect_mrs MPAMIDR, 3, 0x18
    expect_mrs MPAM1, 3, 0x18
    expect_msr MPAM1, 3, 0x18
    expect_mrs MPAM0, 3, 0x18
    expect_mrs MPAMSM, 3, 0x18
    expect_mrs MPAM3, 1, 0
    smc #0
    b fail

el1_trapped_el2:
    expect_mrs MPAM1, 2, 0x18
    expect_msr MPAM1, 2, 0x18
    expect_mrs MPAM0, 2, 0x18
    expect_mrs MPAMSM, 2, 0x18
    mrs x0, MPAMIDR           // HAS_TIDR=0, HAS_HCR=1, no EL2 ID trap.
    cmp x0, #1
    b.ne fail
    smc #0
    b fail

el1_allowed:
    mrs x0, MPAM1
    mov x1, #(1 << 63)
    cmp x0, x1
    b.ne fail
    msr MPAM1, xzr
    mrs x0, MPAM1
    cmp x0, x1
    b.ne fail
    mrs x0, MPAM0
    cbnz x0, fail
    mrs x0, MPAMSM
    cbnz x0, fail
    expect_mrs MPAM2, 1, 0
    expect_mrs MPAM3, 1, 0
    expect_msr MPAMIDR, 1, 0
    smc #0
    b fail

el2_vhe:
    mrs x0, hcr_el2
    orr x0, x0, #(1 << 34)
    msr hcr_el2, x0
    isb
    mov x0, #-1
    msr MPAM1, x0            // EL2+E2H aliases MPAM2.
    mrs x1, MPAM2
    ldr x2, =0x8007000000000000
    cmp x1, x2
    b.ne fail
    mrs x1, MPAM1
    cmp x1, x2
    b.ne fail
    mrs x1, MPAM1_EL12       // Guest EL1 view has no EL2 control bits.
    mov x2, #(1 << 63)
    cmp x1, x2
    b.ne fail
    msr MPAM1_EL12, xzr
    mrs x1, MPAM1_EL12
    cmp x1, x2
    b.ne fail
    mrs x0, hcr_el2
    bic x0, x0, #(1 << 34)
    msr hcr_el2, x0
    isb
    expect_mrs MPAM1_EL12, 2, 0
    smc #0
    b fail

el1_amu_write:
    mov x0, #3
    expect_msr S3_3_C13_C2_5, 1, 0   // AMCNTENSET0_EL0 write is UNDEFINED at EL1.
    smc #0
    b fail

el1_amu_tam_el2:
    expect_mrs S3_3_C13_C2_1, 2, 0x18  // AMCFGR_EL0 read traps to EL2.
    smc #0
    b fail

el1_amu_tam_el3:
    expect_mrs S3_3_C13_C2_1, 3, 0x18  // AMCFGR_EL0 read traps to EL3.
    smc #0
    b fail

el1_amu_userenr:
    msr S3_3_C13_C2_3, xzr           // AMUSERENR_EL0.EN = 0, write allowed at EL1.
    isb
    adr x18, el1_amu_userenr_allow
    adr x0, el0_amu_trapped
    msr elr_el1, x0
    mov x0, #0x3c0
    msr spsr_el1, x0
    eret
el1_amu_userenr_allow:
    mov x0, #1
    msr S3_3_C13_C2_3, x0            // AMUSERENR_EL0.EN = 1.
    isb
    adr x18, el1_amu_userenr_done
    adr x0, el0_amu_allowed
    msr elr_el1, x0
    mov x0, #0x3c0
    msr spsr_el1, x0
    eret
el1_amu_userenr_done:
    smc #0
    b fail

el0_amu_trapped:
    expect_mrs S3_3_C13_C2_1, 1, 0x18  // AMCFGR_EL0 read traps to EL1 (EN=0).
    b el0_amu_return
el0_amu_allowed:
    mrs x0, S3_3_C13_C2_1              // AMCFGR_EL0 read succeeds (EN=1).
    cmp x0, #0xb
    b.ne fail
el0_amu_return:
    mov x20, #4
    mov x21, #0x15
    adr x22, el0_amu_svc + 4
el0_amu_svc:
    svc #0
    b fail

.balign 2048
vectors:
.rept 16
    b exception
    .balign 128
.endr
exception:
    mrs x24, CurrentEL
    cmp x24, #12
    b.eq exception_el3
    cmp x24, #8
    b.eq exception_el2
    mrs x25, esr_el1
    mrs x26, elr_el1
    b check_exception
exception_el2:
    mrs x25, esr_el2
    mrs x26, elr_el2
    b check_exception
exception_el3:
    mrs x25, esr_el3
    mrs x26, elr_el3
    lsr x27, x25, #26
    cmp x27, #0x17
    b.eq return_el3
check_exception:
    cmp x24, x20
    b.ne fail
    lsr x25, x25, #26
    cmp x25, x21
    b.ne fail
    cmp x26, x22
    b.ne fail
    mov x20, #0
    add x23, x23, #1
    add x26, x26, #4
    cmp x24, #12
    b.eq resume_el3
    cmp x24, #8
    b.eq resume_el2
    cmp x21, #0x15
    b.eq return_el1
    msr elr_el1, x26
    eret
resume_el2:
    msr elr_el2, x26
    eret
resume_el3:
    msr elr_el3, x26
    eret
return_el1:
    msr elr_el1, x18
    mov x24, #0x3c5
    msr spsr_el1, x24
    eret
return_el3:
    cbnz x20, fail
    msr elr_el3, x19
    mov x24, #0x3cd
    msr spsr_el3, x24
    eret
fail:
    ldr x0, =0xbad00000
    str x28, [x0]
    b .

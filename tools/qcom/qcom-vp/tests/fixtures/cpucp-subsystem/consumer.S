// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.option norvc
.globl _start
_start:
    li s0, 0x14800000
    li s1, 0x18875000
    li s2, 0
    li s9, 0
    li s3, 0x18820000
    li s4, 0x18876104
    li s8, 0x18860000
    // Enable writes cannot create low-power events or elapsed residence time.
    li t0, 0x18871000
    li t1, 7
    sw t1, 0(t0)
    sw t1, 0x100(t0)
    lw t1, 8(t0)
    lw t2, 0x10(t0)
    or t1, t1, t2
    lw t2, 0x18(t0)
    or t1, t1, t2
    lw t2, 0x108(t0)
    or t1, t1, t2
    lw t2, 0x10c(t0)
    or t1, t1, t2
    bnez t1, failure
    li t0, 0x181d2600
    li t1, 0x11
    sw t1, 4(t0)
    li t1, 0x30080
    sw t1, 0(t0)
    li t1, 0x100388
    sw t1, 0(t0)
    lw t2, 0xe0(t0)
    li t3, 0x388
    bne t2, t3, failure
    li t1, 0x100308
    sw t1, 0(t0)
    lw t2, 0xe0(t0)
    li t3, 0x308
    bne t2, t3, failure
    li t1, 0x30000
    sw t1, 0(t0)
    lw t2, 0xe0(t0)
    bne t2, t3, failure
    // CPU presence is active high, with one little-endian byte per cluster.
    li t0, 0x17651000
    lw t1, 0x0c(t0)
    li t2, 0x033f
    bne t1, t2, failure
    andi t2, t1, 0xff
    li t3, 0x3f
    bne t2, t3, failure
    srli t2, t1, 8
    li t3, 3
    bne t2, t3, failure
    // CPUCP 0x1d802ab6 reads APSS_HW_VERSION at offset zero.
    lw t1, 0(t0)
    li t2, 0x10020003
    bne t1, t2, failure
    // Existing variant/profile words retain their exact field positions.
    lw t1, 0x18(t0)
    li t2, 0x10000
    bne t1, t2, failure
    lw t1, 0x24(t0)
    li t2, 0x20000
    bne t1, t2, failure
    // Disable group0 in cluster0 and group1 in cluster1.
    lw a1, 0x20(t0)
    li t1, 0x21
    bne a1, t1, failure
    not a0, a1
    andi a0, a0, 15
    jal ra, expand_llc_groups
    li t1, 0xfff0
    bne a0, t1, failure
    li t1, 0x0fff
    and a0, a0, t1
    li t1, 0x0ff0
    bne a0, t1, failure
    not a0, a1
    srli a0, a0, 4
    andi a0, a0, 15
    jal ra, expand_llc_groups
    li t1, 0xff0f
    bne a0, t1, failure
    li t1, 0x0fff
    and a0, a0, t1
    li t1, 0x0f0f
    bne a0, t1, failure
    // The real SM8845 cluster1 rule further limits the result to eight ways.
    andi a0, a0, 0xff
    li t1, 0x0f
    bne a0, t1, failure
    li t0, 1
    sw t0, 0(s8)
    sw t0, 4(s8)
    li t0, 2
    sw t0, 0x24(s8)
    li t0, 1
    sw t0, 0x24(s8)
    lw t0, 0x24(s8)
    li t1, 2
    bne t0, t1, failure
    sw zero, 0x24(s8)
    lw t0, 0(s8)
    li t1, 1
    bne t0, t1, failure
    la t0, trap
    csrw mtvec, t0
    lw t0, 0(s1)
    li t1, 0x20100
    bne t0, t1, failure
    lw t0, 4(s1)
    li t1, 16
    bne t0, t1, failure
    lw t0, 0(s3)
    li t1, 0x181
    bne t0, t1, failure
    li t0, 0x17640000
    lw t2, 0(t0)
    bne t1, t2, failure
    li t0, 0x17648000
    lw t2, 0(t0)
    bne t1, t2, failure
    li t0, 0x18828000
    lw t2, 0(t0)
    bne t1, t2, failure
    li t0, 0x18808000
    lw t2, 0(t0)
    bne t1, t2, failure
    li t0, 0x19960000
    lw t2, 0(t0)
    bne t1, t2, failure
    li t0, 0x1a960000
    lw t2, 0(t0)
    bne t1, t2, failure
    // PDP0 and PDP1 retain separate transmit commands but share one RX owner.
    li t0, 0x19960000
    li t1, 0x1234
    sw t1, 0x100(t0)
    li t0, 0x1a960000
    li t2, 0x5678
    sw t2, 0x100(t0)
    lw t3, 0x100(t0)
    bne t2, t3, failure
    li t0, 0x19960000
    lw t3, 0x100(t0)
    bne t1, t3, failure
    // The full local workspace exists. Control writes must not clear it.
    li s5, 0x18b50000
    li s6, 0x18b70000
    li t0, -1
    sd t0, 0(s5)
    sd t0, -8(s6)
    lw t0, 0(s4)
    li t1, 1
    bne t0, t1, failure
    sw zero, 0(s4)
    li t0, 2
    sw t0, 0(s4)
    lw t1, 0(s4)
    bne t0, t1, failure
    ld t0, -8(s6)
    li t1, -1
    bne t0, t1, failure
    mv t0, s5
1:  sd zero, 0(t0)
    addi t0, t0, 8
    bne t0, s6, 1b
    ld t0, 0(s5)
    bnez t0, failure
    ld t0, -8(s6)
    bnez t0, failure
    sw zero, 0x100(s1)
    sw zero, 0x104(s1)
    li t0, 0x17c40000
    la t1, trap
    sw t1, 0x20(t0)
    sw t1, 0x40(t0)
    lw t2, 0x20(t0)
    bne t1, t2, failure
    lw t2, 0x40(t0)
    bne t1, t2, failure
    // Packed source priority, per-hart enable, and threshold follow geometry.
    li t0, 0x90000
    sw t0, 0x34(s1)
    li t0, 0x10000000
    sw t0, 0xa8(s1)
    li t0, 9
    sw t0, 0xe8(s1)
    sw zero, 0xf8(s1)
    sw zero, 0xfc(s1)
    sw zero, 0x200(s1)
    li t0, 0x8000000000000001
    li t1, 0x4000
    add t1, t1, s3
    sd t0, 0(t1)
    li t0, 0x10000
    csrw mie, t0
    csrsi mstatus, 8
    li t0, 1
    sw t0, 0(s0)
    // MAP admits pending even while the channel's interrupt is disabled.
    li s7, 0x4400
    add s7, s7, s3
2:  ld t0, 0(s7)
    andi t0, t0, 1
    beqz t0, 2b
    bnez s2, failure
    li t0, 1
    li t1, 0x4c00
    add t1, t1, s3
    sd t0, 0(t1)
    // Even a real pending source cannot bypass the uncommitted subsystem gate.
    bnez s2, failure
    li t0, 0x80000002
    sw t0, 0(s4)
    // Equal priority remains masked after commit.
    bnez s2, failure
    li t0, 8
    sw t0, 0xe8(s1)
3:  beqz s2, 3b
    li t0, 1
    bne s2, t0, failure
    csrci mstatus, 8
    // The per-channel W1C must not remove channel 63 or its command.
    ld t0, 0(s7)
    li t1, 0x8000000000000000
    bne t0, t1, failure
    ld t0, 0x2f8(s3)
    li t1, 0x12345678
    bne t0, t1, failure
    li t0, 0x4800
    add t0, t0, s3
    li t1, 0x8000000000000000
    sd t1, 0(t0)
    ld t0, 0(s7)
    bnez t0, failure
    // A receiver may clear its command words. Those writes cannot signal itself.
    sw zero, 0x2f8(s3)
    sw zero, 0x2fc(s3)
    ld t0, 0x2f8(s3)
    bnez t0, failure
    li t0, 0x5a5a
    sd t0, 0x118(s3)
    ld t1, 0x118(s3)
    bne t0, t1, failure
    ld t0, 0(s7)
    bnez t0, failure
    // Exercise real BEU levels through claims, not configuration-word reads.
    li t2, 0x18b70000
    li t0, 0x99000000
    sw t0, 0x34(s1)
    li t0, 0xc0000000
    sw t0, 0xa8(s1)
    sw zero, 0xe8(s1)
    sw zero, 8(s1)
    sw zero, 0x18(t2)
    li t0, 4
    sw t0, 0x28(t2)
    sw t0, 0x20(t2)
    lw t0, 0xf0(s1)
    li t1, 32
    bne t0, t1, failure
    sw zero, 0xf0(s1)
    // A held level requests service again after completion.
    lw t0, 0xf0(s1)
    bne t0, t1, failure
    sw zero, 0x20(t2)
    sw zero, 0xf0(s1)
    lw t0, 0xf0(s1)
    bnez t0, failure
    sw zero, 0x28(t2)
    li t0, 4
    sw t0, 0x18(t2)
    // Trigger configuration alone must not synthesize a pulse.
    li t0, 0x40000000
    sw t0, 8(s1)
    lw t0, 0xf0(s1)
    bnez t0, failure
    li t0, 4
    sw t0, 0x20(t2)
    lw t0, 0xf0(s1)
    li t1, 31
    bne t0, t1, failure
    sw zero, 0xf0(s1)
    // A held edge input cannot request service a second time.
    lw t0, 0xf0(s1)
    bnez t0, failure
    sw zero, 0x20(t2)
    li t0, 4
    sw t0, 0x20(t2)
    lw t0, 0xf0(s1)
    bne t0, t1, failure
    sw zero, 0x20(t2)
    sw zero, 0xf0(s1)
    lw t0, 0xf0(s1)
    bnez t0, failure
    sw zero, 8(s1)
    sw zero, 0xa8(s1)
    sw zero, 0x18(t2)
    sw zero, 0x28(t2)
    // Use the native ACLINT counter and comparator, not a read-count poll model.
    li s10, 0x18b84000
    li a5, 0x18b8bff8
    li t0, -1
    sw t0, 0(s10)
    sw t0, 4(s10)
    ld t1, 0(s10)
    bne t0, t1, failure
    li t0, 0x100000000
    sd t0, 0(a5)
    lwu t1, 4(a5)
    li t2, 1
    bne t1, t2, failure
    ld t1, 0(a5)
    bltu t1, t0, failure
    li t0, 0x2345
    sw t0, 0(a5)
    li t0, 2
    sw t0, 4(a5)
    lwu t1, 4(a5)
    bne t1, t0, failure
    ld t1, 0(a5)
    li t0, 0x200002345
    bltu t1, t0, failure
    // A past comparison raises MTIP while global MIE remains clear.
    li t0, 0x80
    csrs mie, t0
    sd zero, 0(s10)
timer_pending:
    csrr t1, mip
    andi t1, t1, 0x80
    beqz t1, timer_pending
    bnez s9, failure
    li t0, -1
    sd t0, 0(s10)
timer_clear:
    csrr t1, mip
    andi t1, t1, 0x80
    bnez t1, timer_clear
    // The firmware's 19200-tick period is one millisecond at 19.2MHz.
    ld s11, 0(a5)
    li t0, 19200
    add s11, s11, t0
    sd s11, 0(s10)
    ld t1, 0(s10)
    bne t1, s11, failure
timer_wait:
    // Global masking closes the check/WFI race. Local MTIE still wakes WFI.
    csrci mstatus, 8
    bnez s9, timer_done
    wfi
    csrsi mstatus, 8
    j timer_wait
timer_done:
    li t0, 1
    bne s9, t0, failure
    ld t0, 0(a5)
    // QEMU schedules tick deadlines with integer nanosecond conversion.
    addi t0, t0, 1
    bltu t0, s11, failure
    sw zero, 0(s8)
    sw zero, 4(s8)
    li t0, 0x17640000
    li t1, 0xfedcba9876543210
    sw t1, 0x100(t0)
    srli t1, t1, 32
    sw t1, 0x104(t0)
    li t0, 2
    sw t0, 0(s0)
    fence rw, rw
4:  wfi
    j 4b
.balign 4
trap:
    csrr t0, mcause
    li t1, 0x8000000000000007
    beq t0, t1, timer_trap
    bnez s2, failure
    li t1, 0x8000000000000010
    bne t0, t1, failure
    lw t0, 0xf0(s1)
    li t1, 29
    bne t0, t1, failure
    ld t0, 0x100(s3)
    li t1, 0x1122334455667788
    bne t0, t1, failure
    li t0, 0x4800
    add t0, t0, s3
    li t1, 1
    sd t1, 0(t0)
    sw zero, 0xf0(s1)
    addi s2, s2, 1
    mret
timer_trap:
    bnez s9, failure
    li t0, -1
    sd t0, 0(s10)
timer_trap_clear:
    csrr t0, mip
    andi t0, t0, 0x80
    bnez t0, timer_trap_clear
    addi s9, s9, 1
    mret
failure:
    li t0, 0xbad00000
    sw zero, 0(t0)
5:  j 5b

// The selected firmware expands one enabled group into four contiguous ways.
expand_llc_groups:
    li t2, 0
    li t3, 0
expand_llc_loop:
    andi t4, a0, 1
    beqz t4, expand_llc_next
    li t4, 15
    sll t4, t4, t2
    or t3, t3, t4
expand_llc_next:
    srli a0, a0, 1
    addi t2, t2, 4
    li t4, 16
    bltu t2, t4, expand_llc_loop
    mv a0, t3
    ret

// SPDX-License-Identifier: BSD-3-Clause-Clear
// Synthetic PDP image. No firmware bytes and no recovered code.
// The linker layout fixes the addresses this fixture asserts:
//   0x20000000 cold vector table, reset entry 0x20000100
//   0x20000200 warm reset entry
//   0x20000300 fault entry
//   0x20004000 warm vector table
// Each reset entry publishes its own address and the loaded stack pointer.
// It then counts heartbeats.
// A reader that observes the boot count therefore observes the entry identity
// and the stack pointer too.
    .syntax unified
    .thumb

    .section .pdp_cold_vectors, "a"
    .align 2
    .word 0x2001e000
    .word cold_entry
    .rept 14
    .word fault_entry
    .endr

    .section .pdp_warm_vectors, "a"
    .align 7
    .word 0x2001f000
    .word warm_entry
    .rept 14
    .word fault_entry
    .endr

    .section .pdp_cold_entry, "ax"
    .align 2
    .globl cold_entry
    .thumb_func
cold_entry:
.Lcold_pc:
    movw r4, #0x0800
    movt r4, #0x1480
    adr r0, .Lcold_pc
    str r0, [r4, #4]
    mov r0, sp
    str r0, [r4, #8]
    ldr r1, [r4]
    adds r1, #1
    str r1, [r4]
cold_loop:
    ldr r1, [r4, #12]
    adds r1, #1
    str r1, [r4, #12]
    b cold_loop

    .section .pdp_warm_entry, "ax"
    .align 2
    .globl warm_entry
    .thumb_func
warm_entry:
.Lwarm_pc:
    movw r4, #0x0800
    movt r4, #0x1480
    adr r0, .Lwarm_pc
    str r0, [r4, #4]
    mov r0, sp
    str r0, [r4, #8]
    ldr r1, [r4]
    adds r1, #1
    str r1, [r4]
warm_loop:
    ldr r1, [r4, #12]
    adds r1, #1
    str r1, [r4, #12]
    b warm_loop

    .section .pdp_fault_entry, "ax"
    .align 2
    .globl fault_entry
    .thumb_func
fault_entry:
    movw r4, #0x0810
    movt r4, #0x1480
    movs r0, #0xde
    str r0, [r4]
fault_loop:
    b fault_loop

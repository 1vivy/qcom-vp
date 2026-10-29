/* Agent-authored SM8850 runtime layout and legacy OEM contract adapter.
 * No vendor instruction or data payload is embedded here.
 * Link .text=0x234c00, runtime_entry=0x234a00,
 * runtime_oem_resume=0x2309a8. Do not enable linker relaxation.
 */
.option norelax
.section .text
.globl _start
_start:
    addi sp, sp, -64
    sw t0, 0(sp)
    sw t1, 4(sp)
    sw t2, 8(sp)
    sw t3, 12(sp)
    sw t4, 16(sp)
    sw t5, 20(sp)
    sw t6, 24(sp)
    sw a1, 28(sp)
    sw a2, 32(sp)
    sw a3, 36(sp)
    sw a4, 40(sp)
    sw a5, 44(sp)
    sw a6, 48(sp)
    sw a7, 52(sp)
    lw t0, 0(a0)
    li t1, 0x10000
    bne t0, t1, invalid
    lw t2, 0x1c(a0)
    lw t0, 0(t2)
    bne t0, t1, invalid
    lw t0, 0x48(t2)
    li t1, 8
    bgtu t0, t1, invalid
    lw t0, 0x10(a0)
    lw t1, 0x14(a0)
    add t1, t0, t1
    li t3, 0x1890
    add t3, t2, t3
    bgeu t3, t1, invalid
    andi t4, t1, 3
    bnez t4, invalid
    li t4, 0x4000
    sub t5, t1, t0
    bgtu t5, t4, invalid
    mv t4, t0
    li t5, 0x460000
    sub a4, t5, t0
copy_legacy_buffer:
    lw t6, 0(t4)
    sw t6, 0(t5)
    addi t4, t4, 4
    addi t5, t5, 4
    bne t4, t1, copy_legacy_buffer
    add a5, a0, a4
    add a6, t2, a4
.macro shadow_pointer offset, base
    lw t6, \offset(\base)
    bltu t6, t0, shadow_skip\@
    bgeu t6, t1, shadow_skip\@
    add t6, t6, a4
    sw t6, \offset(\base)
shadow_skip\@:
.endm
    shadow_pointer 12, a5
    shadow_pointer 24, a5
    shadow_pointer 28, a5
    li a7, 0x19f0
    add a6, a6, a7
    shadow_pointer 0, a6
    shadow_pointer 12, a6
    shadow_pointer 36, a6
    lw t4, 32(a6)
    beqz t4, legacy_buffer_ready
    lw t4, 36(a6)
    beqz t4, invalid
    shadow_pointer 8, t4
legacy_buffer_ready:
    li t4, 0x464000
    li t5, 0x10000
    sw t5, 0(t4)
    sw a5, 4(t4)
    mv a2, t3
    mv a3, t1
    mv t4, t1
    addi t5, t1, 0x200
copy_tail:
    addi t4, t4, -4
    addi t5, t5, -4
    lw t6, 0(t4)
    sw t6, 0(t5)
    bne t4, t3, copy_tail
    addi t4, t3, 0x200
    li a1, 8
expand_record:
    addi t5, t4, -0x40
clear_extension:
    addi t4, t4, -4
    sw zero, 0(t4)
    bne t4, t5, clear_extension
    addi a4, t3, -0x308
copy_record:
    addi t3, t3, -4
    addi t5, t5, -4
    lw t6, 0(t3)
    sw t6, 0(t5)
    bne t3, a4, copy_record
    mv t4, t5
    addi a1, a1, -1
    bnez a1, expand_record
    li t3, 0x1bf0
    add t3, t2, t3
.macro relocate offset, base
    lw t5, \offset(\base)
    bltu t5, a2, skip\@
    bgeu t5, a3, skip\@
    addi t5, t5, 0x200
    sw t5, \offset(\base)
skip\@:
.endm
    relocate 0, t3
    relocate 12, t3
    relocate 36, t3
    lw t4, 32(t3)
    beqz t4, records_ready
    lw t4, 36(t3)
    beqz t4, invalid
    relocate 8, t4
records_ready:
    lw t0, 0x14(a0)
    addi t0, t0, 0x200
    sw t0, 0x14(a0)
    lw t0, 0(sp)
    lw t1, 4(sp)
    lw t2, 8(sp)
    lw t3, 12(sp)
    lw t4, 16(sp)
    lw t5, 20(sp)
    lw t6, 24(sp)
    lw a1, 28(sp)
    lw a2, 32(sp)
    lw a3, 36(sp)
    lw a4, 40(sp)
    lw a5, 44(sp)
    lw a6, 48(sp)
    lw a7, 52(sp)
    addi sp, sp, 64
    j runtime_entry
invalid:
    ebreak
    j invalid

.balign 1024
.globl legacy_oem_entry
legacy_oem_entry:
    addi sp, sp, -16
    sw t0, 0(sp)
    # APSS publishes requests in the original ring at 0x02002800.
    li t0, 0x48991c
    la a1, legacy_request_ring
    sw a1, 0(t0)
    # The legacy request ring and donor response ring have different capacities.
    li t0, 0x48993c
    la a1, legacy_pending_request
    sw a1, 0(t0)
    li t0, 0x489918
    la a1, legacy_request_index
    sw a1, 0(t0)
    # Original 0x21780c stores the secondary log descriptor separately.
    # Candidate 0x2177d2 appends a record and cannot replace that setter.
    li t0, 0x416db0
    la a1, legacy_secondary_log_get
    sw a1, 0(t0)
    la a1, legacy_secondary_log_set
    sw a1, 12(t0)
    li t0, 0x464100
    sw zero, 0(t0)
    # Only denial-path count/error callbacks bridge to retained OEM slots.
.macro bind_authorization_callback slot, target
    li t0, 0x489000 + \slot
    la a1, \target
    sw a1, 0(t0)
.endm
    bind_authorization_callback 0x1fc, legacy_authorization_count_get
    bind_authorization_callback 0x228, legacy_authorization_error_set
    bind_authorization_callback 0x22c, legacy_authorization_count_set
# Keep donor +0x2fc..+0x310 aligned with OEM +0x9d8..+0x9ec.
.macro bind_context_layout slot, target
    li t0, 0x489000 + \slot
    la a1, \target
    sw a1, 0(t0)
.endm
    bind_context_layout 0x248, legacy_context_d4
    bind_context_layout 0x24c, legacy_context_b4
    # Report queries retain the OEM record buffer and reject unsupported grants.
    li t0, 0x4169a8
    la a1, legacy_denied_report_prepare
    sw a1, 0(t0)
    li t0, 0x4169d8
    la a1, legacy_denied_report_flags
    sw a1, 0(t0)
    # A fully denied context has no admitted dump-policy record.
    li t0, 0x4169c4
    la a1, legacy_empty_dump_policy
    sw a1, 0(t0)
    sw a1, 12(t0)
    li t0, 0x464104
    sw zero, 0(t0)
    # Each legacy channel occupies 0xa8 bytes. The donor requires 0x218 bytes.
.macro bind_channel_callback slot, target
    li t0, 0x416800 + \slot
    la a1, \target
    sw a1, 0(t0)
.endm
    bind_channel_callback 0xb94, legacy_channel_open
    bind_channel_callback 0xb9c, legacy_channel_interrupt
    bind_channel_callback 0xba8, legacy_channel_initialize
    li t0, 0x00c00210
    sw zero, 0(t0)
    li t0, 0x4a0000
    li a1, 1
    sw a1, 0(t0)
    li t0, 0x464000
    lw a0, 8(s1)
    sw a0, 8(t0)
    mv a0, t0
    li a1, 1
    lw t0, 0(sp)
    addi sp, sp, 16
    j runtime_oem_resume

.macro channel_callback name, slot
\name:
    li t6, 0x489000 + \slot
    j legacy_channel_pointer
.endm
channel_callback legacy_channel_open, 0xbe0
channel_callback legacy_channel_interrupt, 0xbe8
channel_callback legacy_channel_initialize, 0xc00

legacy_channel_pointer:
    li t0, 0x4a0004
    lw t1, 0(t0)
    sub t2, a0, t1
    li t3, 0x1f8
    bgeu t2, t3, channel_pointer_ready
    lw t1, 4(t0)
    add a0, t1, t2
    li t3, 0xa8
    bltu t2, t3, channel_pointer_ready
    addi a0, a0, 0x170
    li t3, 0x150
    bltu t2, t3, channel_pointer_ready
    addi a0, a0, 0x170
channel_pointer_ready:
    lw t6, 0(t6)
    jr t6

legacy_secondary_log_get:
    li a0, 0x464100
    lw a0, 0(a0)
    ret

legacy_secondary_log_set:
    li t0, 0x464100
    sw a0, 0(t0)
    ret

legacy_request_ring:
    li a0, 0x02002800
    ret

legacy_pending_request:
    beqz a2, no_pending_request
    sw zero, 0(a2)
    beqz a3, no_pending_request
    lw t0, 8(a0)
    lw t1, 8(a1)
    bgeu t0, t1, no_pending_request
    # PBL uses two request slots. XBL uses twenty. The donor keeps two response slots.
    lw t1, 4(a0)
    li t2, 2
    bne t1, t2, no_pending_request
    lw t3, 4(a1)
    beq t3, t2, pending_capacity_valid
    li t2, 20
    bne t3, t2, no_pending_request
pending_capacity_valid:
    remu t2, t0, t1
    sw t2, 0(a3)
    remu t2, t0, t3
    li t3, 20
    mul t2, t2, t3
    add t2, t2, a1
    addi t2, t2, 16
    sw t2, 0(a2)
    li a0, 1
    ret
no_pending_request:
    li a0, 0
    ret

legacy_request_index:
    beqz a0, invalid_request_index
    lw t0, 4(a1)
    li t1, 2
    beq t0, t1, request_capacity_valid
    li t1, 20
    beq t0, t1, request_capacity_valid
    sw zero, 0(a2)
invalid_request_index:
    li a0, 6
    ret
request_capacity_valid:
    addi a0, a0, -1
    remu a0, a0, t0
    sw a0, 0(a2)
    li a0, 0
    ret

.macro authorization_callback target, slot
\target:
    li t0, 0x416800
    lw t0, \slot(t0)
    beqz t0, invalid
    jr t0
.endm
authorization_callback legacy_authorization_count_get, 0x204
authorization_callback legacy_authorization_error_set, 0x230
authorization_callback legacy_authorization_count_set, 0x234

authorization_callback legacy_authorization_context_get, 0x198
authorization_callback legacy_authorization_processed_get, 0x208
authorization_callback legacy_authorization_mask_get, 0x1f8

# The OEM owns the c0-byte record report. Never replace it with the donor's
# compact policy-ID report. Only the fully denied context is supported here.
legacy_denied_report_prepare:
    addi sp, sp, -32
    sw ra, 28(sp)
    sw s0, 24(sp)
    sw s1, 20(sp)
    mv s0, a0
    li s1, 0x464104
    sw zero, 0(s1)
    # Both selected OEM getters own this exact c0-byte original-prefix buffer.
    li t0, 0x415624
    bne s0, t0, denied_report_invalid
    call legacy_authorization_context_empty
    beqz a0, denied_report_invalid
    li t0, 0x00c00210
    li t1, 1
    sw t1, 0(t0)
    sw t1, 0(s1)
    li a0, 0
    j denied_report_return
denied_report_invalid:
    li a0, 0x4005
denied_report_return:
    lw s1, 20(sp)
    lw s0, 24(sp)
    lw ra, 28(sp)
    addi sp, sp, 32
    ret

# Match donor 1d0's null-output error and zero-before-lookup convention.
# The preceding check proves an all-zero context, so its two flag bits are zero.
legacy_denied_report_flags:
    beqz a0, denied_report_null_output
    sw zero, 0(a0)
    li t0, 0x464104
    lw t1, 0(t0)
    sw zero, 0(t0)
    beqz t1, denied_report_unavailable
    li a0, 0x59657321
    ret
denied_report_null_output:
    li a0, 0x4002
    ret
denied_report_unavailable:
    li a0, 0x4005
    ret

# Preserve the existing fully denied context boundary for optional queries.
legacy_authorization_context_empty:
    addi sp, sp, -16
    sw ra, 12(sp)
    call legacy_authorization_context_get
    beqz a0, authorization_context_not_empty
    andi t0, a0, 3
    bnez t0, authorization_context_not_empty
    addi t1, a0, 0x29c
authorization_context_word:
    lw t0, 0(a0)
    bnez t0, authorization_context_not_empty
    addi a0, a0, 4
    bne a0, t1, authorization_context_word
    call legacy_authorization_processed_get
    bnez a0, authorization_context_not_empty
    call legacy_authorization_mask_get
    bnez a0, authorization_context_not_empty
    li a0, 1
    j authorization_context_empty_return
authorization_context_not_empty:
    li a0, 0
authorization_context_empty_return:
    lw ra, 12(sp)
    addi sp, sp, 16
    ret

# Neither result supplies a bitmap or a successful authorization result.
legacy_empty_dump_policy:
    beqz a0, denied_report_null_output
    beqz a1, denied_report_null_output
    addi sp, sp, -16
    sw ra, 12(sp)
    call legacy_authorization_context_empty
    beqz a0, empty_dump_policy_unavailable
    li a0, 0x4006
    j empty_dump_policy_return
empty_dump_policy_unavailable:
    li a0, 0x4005
empty_dump_policy_return:
    lw ra, 12(sp)
    addi sp, sp, 16
    ret
# Both donor entries check a0; preserve null before shifting the object pointer.
legacy_context_d4:
    beqz a0, legacy_context_d4_dispatch
    addi a0, a0, 0x6dc
legacy_context_d4_dispatch:
    li t0, 0x4890d4
    lw t0, 0(t0)
    jr t0

legacy_context_b4:
    beqz a0, legacy_context_b4_dispatch
    addi a0, a0, 0x6dc
legacy_context_b4_dispatch:
    li t0, 0x4890b8
    lw t0, 0(t0)
    jr t0

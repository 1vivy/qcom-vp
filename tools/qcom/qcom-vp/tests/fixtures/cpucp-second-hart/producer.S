// SPDX-License-Identifier: BSD-3-Clause-Clear
.section .text
.globl _start
_start:
    // Publish the second-hart boot vector, exactly as the real ROM publishes
    // both 0x17c40000 and 0x17c40004 before any reset. Hart 0 cold-boots
    // immediately from its elaboration-time resetvec and needs no request here.
    mov x2, #0x0004
    movk x2, #0x17c4, lsl #16
    mov w3, #0x0000
    movk w3, #0x0040, lsl #16
    str w3, [x2]
    ldr w4, [x2]
    cmp w4, w3
    b.ne failure
    mov x0, #0x14800000
    mov w1, #1
    str w1, [x0, #4]
    // Hart 0's real control-commit marker.
1:  ldr w1, [x0]
    tbz w1, #0, 1b
    // Only a genuinely released, genuinely executing hart 1 can set this bit.
    // Nothing on the APSS side or hart 0 ever writes it.
2:  ldr w1, [x0]
    tbz w1, #1, 2b
    mov x0, #0x600d0000
    str w1, [x0]
    b done
failure:
    mov x0, #0xbad00000
    str w1, [x0]
done:
3:  b 3b

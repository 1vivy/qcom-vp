// SPDX-License-Identifier: BSD-3-Clause-Clear
.global _start
_start:
    ldr x0, =0x402000
    mov sp, x0
    bl test
1:  b 1b

/* SPDX-License-Identifier: BSD-3-Clause-Clear
 * Synthetic PDP layout. The fixture driver asserts these addresses.
 * The cold table selects its own stack and reset entry.
 * The warm table selects a different stack and a different reset entry,
 * so a reset that loads it proves that the new table was fetched.
 */
SECTIONS
{
    . = 0x20000000;
    /* compile_guest passes -Ttext=0x20000000. This section is the text start. */
    .text : { KEEP(*(.pdp_cold_vectors)) }
    . = 0x20000100;
    .pdp_cold_entry : { KEEP(*(.pdp_cold_entry)) }
    . = 0x20000200;
    .pdp_warm_entry : { KEEP(*(.pdp_warm_entry)) }
    . = 0x20000300;
    .pdp_fault_entry : { KEEP(*(.pdp_fault_entry)) }
    . = 0x20000400;
    .pdp_code : { *(.text*) *(.rodata*) *(.data*) }
    . = 0x20004000;
    .pdp_warm_vectors : { KEEP(*(.pdp_warm_vectors)) }
    /DISCARD/ : { *(.ARM.exidx*) *(.comment) *(.note*) }
}

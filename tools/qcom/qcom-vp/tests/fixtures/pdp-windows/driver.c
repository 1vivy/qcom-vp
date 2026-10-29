// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <stdint.h>

static void write32(uintptr_t address, uint32_t value) {
    *(volatile uint32_t*)address = value;
    __asm__ volatile("dsb sy\n\tisb" ::: "memory");
}
static uint32_t read32(uintptr_t address) { return *(volatile uint32_t*)address; }
static void fail(unsigned code) {
    write32(0x70000000 + code * 4, code);
    for (;;) {}
}
void window_probe(void) {
    const uintptr_t registers = 0x40990000;
    const uintptr_t source = 0x813d0000;
    const uintptr_t target = 0x813e0000;
    write32(source, 0x12345678);
    write32(source + 0xfffc, 0x23456789);
    write32(target, 0x3456789a);
    write32(target + 0xfffc, 0x456789ab);
    if (read32(source) != 0x12345678 || read32(source + 0xfffc) != 0x23456789) fail(1);
    write32(registers + 0x100, 0xffff813d);
    write32(registers + 0x104, 0x813e);
    write32(registers + 0x108, 0x80000000);
    if (read32(source) != 0x3456789a || read32(source + 0xfffc) != 0x456789ab) fail(2);
    write32(source, 0x56789abc);
    if (read32(target) != 0x56789abc) fail(3);
    write32(registers + 0x108, 0);
    if (read32(source) != 0x12345678 || read32(source + 0xfffc) != 0x23456789) fail(4);
    if (read32(target) != 0x56789abc) fail(5);
    // Reprogram a larger lane window after the former alias obtained DMI.
    write32(registers + 0x100, 0xfffc8200);
    write32(registers + 0x104, 0x8204);
    write32(registers + 0x108, 0x80000000);
    write32(0x82040000, 0x6789abcd);
    write32(0x8207fffc, 0x789abcde);
    if (read32(0x82000000) != 0x6789abcd || read32(0x8203fffc) != 0x789abcde) fail(6);
    write32(registers + 0x108, 0);
    write32(0x82000000, 0x89abcdef);
    if (read32(0x82040000) != 0x6789abcd || read32(0x82000000) != 0x89abcdef) fail(7);
    write32(0x600d0000, 0);
    for (;;) {}
}

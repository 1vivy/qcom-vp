// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <stdint.h>

#define REG64(base, offset) (*(volatile uint64_t *)((uintptr_t)(base) + (offset)))
#define REG(base, offset) (*(volatile uint32_t *)((uintptr_t)(base) + (offset)))
#define SMMU 0x15000000U
#define CB (SMMU + 0x87000U)
#define ROOT 0x14800200U
#define L2 0x14801000U
#define L3 0x14802000U
#define PAGE0 0x14808000U
#define PAGE1 0x1480a000U
#define APERTURE UINT64_C(0x1000000000)
#define VALID_PAGE (UINT64_C(3) | (1U << 10) | (1U << 6))
#define MULTI (1U << 31)
#define TF (1U << 1)
#define AFF (1U << 2)
#define PF (1U << 3)
#define ASF (1U << 7)
#define UUT (1U << 8)
#define CR0 ((1U << 21) | (1U << 10) | (1U << 2) | (1U << 1))

extern int probe_read(uint64_t address, uint32_t *value);
extern int probe_write(uint64_t address, uint32_t value);

static void require(int condition, unsigned step) {
    if (!condition) {
        REG(0xbad00000U, step * 4) = step;
        for (;;) {}
    }
}

static void descriptor(uintptr_t base, unsigned index, uint64_t value) {
    *(volatile uint64_t *)(base + index * 8) = value;
    __asm__ volatile("dsb sy" ::: "memory");
    REG(SMMU, 0x64) = 1; // Complete table changes with TLBIALL.
}

static void read_ok(uint64_t va, uint32_t expected, unsigned step) {
    uint32_t value;
    require(!probe_read(APERTURE + va, &value), step);
    require(value == expected, step + 1);
}

static void read_fault(uint64_t va, unsigned step) {
    uint32_t value;
    require(probe_read(APERTURE + va, &value), step);
}

static void irq(unsigned gsiv, int asserted, unsigned step) {
    const uint32_t mask = 1U << (gsiv % 32);
    for (unsigned i = 0; i < 100000; ++i) {
        if (!!(REG(0x17000000U, 0x200 + (gsiv / 32) * 4) & mask) == asserted) return;
    }
    require(0, step);
}

static void context_fault(uint32_t expected, uint64_t va, unsigned step) {
    require(REG(CB, 0x58) == (expected | 0x400U), step);
    require(REG(CB, 0x60) == (uint32_t)va && REG(CB, 0x64) == (uint32_t)(va >> 32), step + 1);
    require(REG(SMMU, 0x141c) == 0x60, step + 2);
    irq(136, 1, step + 3);
    irq(129, 0, step + 4);
    require(REG(SMMU, 0x448) == 0, step + 5);
    require(REG64(CB, 0x60) == va && REG64(CB, 0x70) == va, 280);
    require(REG(CB, 0x70) == (uint32_t)va && REG(CB, 0x74) == (uint32_t)(va >> 32), 281);
    require(REG(CB, 0x6c) == 0, 282);
}

static void clear_context(unsigned step) {
    REG(CB, 0x58) = UINT32_MAX;
    require(REG(CB, 0x58) == 0x400U, step);
    irq(136, 0, step + 1);
}

static void global_fault(uint32_t expected, unsigned step) {
    read_fault(0x4000, step);
    require(REG(SMMU, 0x48) == expected && REG(SMMU, 0x54) == 0x60 &&
            REG(SMMU, 0x40) == 0x4000 && REG(SMMU, 0x44) == 0, step + 1);
    require(REG(CB, 0x58) == 0x400U, step + 2);
    require(REG(SMMU, 0x448) == 0, step + 6); // Non-secure fault state stays separate.
    irq(97, 1, step + 3);
    irq(136, 0, step + 4);
}

static void clear_global(unsigned step) {
    REG(SMMU, 0x48) = UINT32_MAX;
    require(REG(SMMU, 0x48) == 0, step);
    irq(97, 0, step + 1);
}

void test(void) {
    // Configuration-only GPU: use its advertised geometry, never a fake DMA stream.
    const uintptr_t gpu = 0x03da0000U;
    const uint32_t gpu_id0 = REG(gpu, 0x20);
    const uint32_t gpu_id1 = REG(gpu, 0x24);
    require((gpu_id0 & 255) == 128 && gpu_id1 == 0x40000019U, 260);
    const uintptr_t gpu_cb = gpu + ((2U << ((gpu_id1 >> 28) & 7)) * 4096U);
    require(gpu_cb == gpu + 0x20000U, 261);
    const unsigned last = (gpu_id1 & 255) - 1;
    const uintptr_t last_cb = gpu_cb + last * 4096;
    REG(gpu, 0x1000 + last * 4) = 2U << 16;
    REG(gpu, 0x1800 + last * 4) = 1;
    REG(last_cb, 0x20) = ROOT;
    require(REG(gpu, 0x1000 + last * 4) == (2U << 16), 264);
    require(REG(gpu, 0x1800 + last * 4) == 1, 265);
    require(REG(last_cb, 0x20) == ROOT, 266);
    REG(last_cb, 0x7f0) = 0;
    require(REG(last_cb, 0x7f4) == 0, 268);
    uint32_t unused;
    require(probe_read(last_cb + 4096, &unused), 269);
    require(probe_write(gpu + 0x1000 + (last + 1) * 4, 1), 270);
    require(probe_write(gpu + 0x1800 + (last + 1) * 4, 1), 271);
    const uint32_t id0 = REG(SMMU, 0x20);
    require((id0 & (1U << 26)) && ((id0 >> 16) & 255) == 1, 1);
    require((id0 & (1U << 30)) && (id0 & (1U << 29)) && (id0 & (1U << 28)), 2);
    require((REG(SMMU, 0x24) & 255) == 112, 3);
    // Match XBL's IDR1-based diagnostic scan of APPS context 109.
    const uint32_t id1 = REG(SMMU, 0x24);
    const unsigned page_shift = id1 & (1U << 31) ? 16 : 12;
    const uintptr_t diagnostic_cb = SMMU + ((2U << ((id1 >> 28) & 7)) << page_shift) + (109U << 12);
    require(diagnostic_cb + 0x70 == 0x150ed070U, 283);
    require(REG(diagnostic_cb, 0x58) == 0 && REG64(diagnostic_cb, 0x60) == 0, 284);
    require(REG(diagnostic_cb, 0x68) == 0 && REG(diagnostic_cb, 0x6c) == 0, 285);
    require(REG64(diagnostic_cb, 0x70) == 0, 286);
    REG64(diagnostic_cb, 0x60) = UINT64_C(0x123456789);
    REG64(diagnostic_cb, 0x70) = UINT64_MAX; // IPAFAR writes must not change FAR.
    require(REG64(diagnostic_cb, 0x70) == UINT64_C(0x123456789), 287);
    require(REG64(CB, 0x70) == 0, 288); // Banks must not share diagnostic state.
    REG(diagnostic_cb, 0x30) = 1U << 31;
    require(REG(diagnostic_cb, 0x58) == 0x200U, 302);
    REG(SMMU, 0x1800 + 109 * 4) = 1;
    require(REG(diagnostic_cb, 0x58) == 0x400U, 303);
    REG(diagnostic_cb, 0x58) = UINT32_MAX;
    require(REG(diagnostic_cb, 0x58) == 0x400U, 304);
    REG(SMMU, 0x1800 + 109 * 4) = 0;
    REG(diagnostic_cb, 0x30) = 0;
    require(REG(diagnostic_cb, 0x58) == 0, 305);
    // Context seven deliberately leaves IRPTNDX zero, as SMMUv2 consumers do.
    REG(SMMU, 0x101c) = 1U << 16;
    REG(SMMU, 0x181c) = 1;
    REG(SMMU, 0x800) = (1U << 31) | 0x60;
    REG(SMMU, 0xc00) = 7;
    REG(CB, 0x10) = 5;
    REG(CB, 0x30) = 28; // 36-bit input, 512-byte initial level-one table.
    REG(CB, 0x20) = ROOT;
    REG(CB, 0x24) = 0;
    REG(CB, 0) = 1 | (1U << 6) | (1U << 1);
    require(REG(CB, 0x58) == 0x400U, 306);
    irq(136, 0, 307); // Format alone must not assert a fault interrupt.
    descriptor(ROOT, 0, L2 | 3);
    descriptor(ROOT, 4, L2 | 3); // A distinct VA above 32 bits uses the same subtree.
    descriptor(L2, 0, L3 | 3);
    descriptor(L3, 4, PAGE0 | VALID_PAGE);
    descriptor(L3, 5, PAGE1 | VALID_PAGE);
    REG(PAGE0, 0) = 0x10203040;
    REG(PAGE0, 0xffc) = 0x11223344;
    REG(PAGE1, 0) = 0xaabbccdd;
    REG(SMMU, 0x400) = CR0;
    // EL3 accesses below +0x400 reach the secure bank.
    REG(SMMU, 0) = 1U | (1U << 10);
    require(REG(SMMU, 0x400) == CR0 && REG(SMMU, 0) == (1U | (1U << 10)), 4);
    require(REG(SMMU, 4) == ((1U << 16) | (128U << 8) | 112U), 4);
    // The synthetic master executes at EL3, so every probe below is a Secure
    // transaction. Give the Secure state ownership of all groups and contexts.
    REG(SMMU, 4) = 0;
    REG(SMMU, 0) = CR0;
    REG(SMMU, 0x4380) |= 1U << 5;
    require(REG(SMMU, 0x4380) == 1U << 5 && REG(SMMU, 0x4384) == 0, 4);
    read_ok(0x4000, 0x10203040, 5);
    read_ok(UINT64_C(0x100004000), 0x10203040, 7);
    read_ok(0x4ffc, 0x11223344, 9);
    read_ok(0x5000, 0xaabbccdd, 11);
    require(!probe_write(APERTURE + 0x5000, 0x55667788), 13);
    require(REG(PAGE1, 0) == 0x55667788 && REG(PAGE0, 0) == 0x10203040, 14);
    // Ignored root low bits must not force a full-page alignment requirement.
    REG(CB, 0x20) = ROOT | 0x100;
    read_ok(0x4000, 0x10203040, 15);
    REG(CB, 0x20) = ROOT;

    REG(CB, 0x6c) = UINT32_MAX; // The next real fault replaces restored syndrome state.
    descriptor(L3, 4, PAGE0 | 3 | (1U << 6)); // AF clear.
    read_fault(0x4000, 20);
    context_fault(AFF, 0x4000, 21);
    descriptor(L3, 5, UINT64_C(0x000f000000000000)); // Invalid, despite address bits.
    require(probe_write(APERTURE + 0x5000, 1), 27);
    context_fault(AFF | MULTI, 0x4000, 28);
    require(REG(CB, 0x68) == 3 && REG(PAGE1, 0) == 0x55667788, 34);
    REG64(CB, 0x70) = UINT64_MAX;
    require(REG64(CB, 0x60) == 0x4000 && REG64(CB, 0x70) == 0x4000, 289);
    REG(CB, 0) &= ~(1U << 6);
    irq(136, 0, 35);
    REG(CB, 0) |= 1U << 6;
    irq(136, 1, 36);
    REG(CB, 0x58) = AFF;
    require(REG(CB, 0x58) == (MULTI | 0x400U), 37);
    irq(136, 1, 38);
    clear_context(39);
    read_fault(0x5000, 41);
    context_fault(TF, 0x5000, 42);
    clear_context(48);

    descriptor(L3, 4, PAGE0 | VALID_PAGE | (1U << 7)); // Read-only leaf.
    require(probe_write(APERTURE + 0x4000, 0), 50);
    context_fault(PF, 0x4000, 51);
    require(REG(CB, 0x68) == (3 | (1U << 4)) && REG(PAGE0, 0) == 0x10203040, 57);
    clear_context(58);
    descriptor(L3, 4, PAGE0 | 3 | (1U << 10)); // Privileged-only leaf.
    REG(SMMU, 0xc00) = (2U << 24) | 7; // Explicitly force unprivileged.
    read_fault(0x4000, 60);
    context_fault(PF, 0x4000, 61);
    clear_context(67);
    REG(SMMU, 0xc00) = (3U << 24) | 7; // Explicitly force privileged, not a UFS default claim.
    read_ok(0x4000, 0x10203040, 69);
    descriptor(L3, 4, PAGE0 | 3 | (1U << 6));
    read_fault(0x4000, 292);
    context_fault(AFF, 0x4000, 293);
    require(REG(CB, 0x68) == (3 | (1U << 5)), 299);
    clear_context(300);
    REG(SMMU, 0xc00) = 7;
    descriptor(L3, 4, PAGE0 | VALID_PAGE);

    REG(CB, 0x30) = 28 | (1U << 7); // EPD0 is a translation fault, not UUT.
    read_fault(0x4000, 72);
    context_fault(TF, 0x4000, 73);
    require(REG(CB, 0x68) == 0, 290); // EPD0 is a pre-walk level-zero fault.
    clear_context(79);
    REG(CB, 0x30) = 28;
    read_fault(UINT64_C(1) << 36, 81);
    context_fault(TF, UINT64_C(1) << 36, 82);
    require(REG(CB, 0x68) == 0, 291);
    clear_context(88);
    descriptor(L3, 4, (UINT64_C(1) << 40) | VALID_PAGE);
    REG(CB, 0x10) = 0; // 32-bit PA limit.
    read_fault(0x4000, 90);
    context_fault(ASF, 0x4000, 91);
    clear_context(97);
    REG(CB, 0x10) = 5;
    descriptor(L3, 4, PAGE0 | VALID_PAGE);
    REG(CB, 0x30) = 28 | (1U << 14); // Unsupported 64 KiB granule must not bypass.
    read_fault(0x4000, 99);
    context_fault(UUT, 0x4000, 100);
    clear_context(106);
    REG(CB, 0x30) = 28;

    // Exercise both supported T0SZ endpoints and reject the adjacent sizes.
    descriptor(ROOT, 0, L3 | 3);
    REG(CB, 0x30) = 39; // 25-bit input, 128-byte initial level-two table.
    read_ok(0x4000, 0x10203040, 220);
    read_fault(UINT64_C(1) << 25, 222);
    context_fault(TF, UINT64_C(1) << 25, 223);
    clear_context(229);
    descriptor(0x14803000U, 0, 0x14804000U | 3);
    descriptor(0x14804000U, 0, L2 | 3);
    REG(CB, 0x20) = 0x14803000U;
    REG(CB, 0x30) = 16; // 48-bit input, full level-zero table.
    read_ok(0x4000, 0x10203040, 231);
    const unsigned unsupported_t0sz[] = {15, 40};
    for (unsigned i = 0; i != 2; ++i) {
        REG(CB, 0x30) = unsupported_t0sz[i];
        read_fault(0x4000, 233 + i * 9);
        context_fault(UUT, 0x4000, 234 + i * 9);
        clear_context(240 + i * 9);
    }
    descriptor(ROOT, 0, L2 | 3);
    REG(CB, 0x20) = ROOT;
    REG(CB, 0x30) = 28;

    REG(SMMU, 0xc00) = 112; // Unimplemented context index.
    global_fault(1U << 3, 110);
    REG(SMMU, 0xc00) = 2U << 16; // A different class must not overwrite the first fault.
    global_fault(MULTI | (1U << 3), 115);
    REG(SMMU, 0) = CR0 & ~(1U << 2);
    irq(97, 0, 120);
    REG(SMMU, 0) = CR0;
    irq(97, 1, 121);
    REG(SMMU, 0x48) = 1U << 3;
    require(REG(SMMU, 0x48) == MULTI, 122);
    clear_global(123);
    for (unsigned type = 2; type <= 3; ++type) {
        REG(SMMU, 0xc00) = type << 16;
        global_fault(1, 126 + type * 8);
        clear_global(131 + type * 8);
    }
    REG(SMMU, 0xc00) = 7;
    // Secure groups have no stage 2, so every context type except stage 1 faults.
    for (unsigned type = 0; type <= 3; ++type) {
        if (type == 1) continue;
        REG(SMMU, 0x101c) = type << 16;
        global_fault(1, 160 + type * 8);
        clear_global(165 + type * 8);
    }
    REG(SMMU, 0x101c) = 1U << 16;
    REG(SMMU, 0x804) = (1U << 31) | 0x60;
    global_fault(1U << 2, 192);
    clear_global(197);
    REG(SMMU, 0) = CR0 & ~(1U << 21);
    read_ok(PAGE0, 0x10203040, 199); // Architecturally selected multi-match bypass.
    REG(SMMU, 0x804) = 0;
    REG(SMMU, 0x800) = 0;
    REG(SMMU, 0) = CR0;
    global_fault(1U << 1, 202);
    clear_global(207);
    REG(SMMU, 0) = CR0 & ~(1U << 10);
    read_ok(PAGE0, 0x10203040, 209); // Architecturally selected unmatched bypass.
    REG(0x600d0000U, 0) = 1;
    for (;;) {}
}

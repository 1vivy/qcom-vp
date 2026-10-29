// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <stdint.h>

#define CSR(offset) (*(volatile uint32_t *)(0x02230000U + (offset)))
#define INPUT ((volatile uint32_t *)0x02281000U)
#define SOURCE ((const volatile uint32_t *)0x400000U)
#define DIGEST ((volatile uint32_t *)0x401000U)

static void require(int condition, unsigned step) {
    if (!condition) {
        *(volatile uint32_t *)(0xbad00000U + step * 4) = step;
        for (;;) {}
    }
}

static void submit(unsigned mutation, unsigned curve, unsigned dma) {
    CSR(0x60) = 0x100;
    require((CSR(0x60) & 0x70) == 0x10, 1);
    CSR(0) = 8;
    CSR(4) = dma ? 1 : 2;
    CSR(8) = dma ? 0 : 1;
    CSR(12) = 0;
    CSR(0x2c) = dma ? 2 : 1;
    CSR(0x34) = 1;
    CSR(0x38) = curve;
    CSR(0x50) = 0;
    CSR(0x58) = 48;
    for (unsigned i = 0; i != (dma ? 49U : 61U); ++i) INPUT[i] = SOURCE[i];
    if (dma) {
        for (unsigned i = 0; i != 12; ++i) DIGEST[i] = SOURCE[49 + i];
        INPUT[0] = 1;
        INPUT[49] = (uint32_t)(uintptr_t)DIGEST;
        INPUT[50] = 48;
        INPUT[51] = 0;
    }
    if (mutation == 1) {
        for (unsigned i = 1; i != 25; ++i) INPUT[i] = 0;
    } else if (mutation == 2) {
        for (unsigned i = 25; i != 49; ++i) INPUT[i] = 0;
    } else if (mutation == 3) {
        if (dma) DIGEST[0] ^= 1;
        else INPUT[49] ^= 1;
    } else if (mutation == 4) {
        INPUT[48] ^= 1;
    }
    __asm__ volatile("dsb sy" ::: "memory");
    CSR(0x60) = 0x200;
}

void test(void) {
#ifdef UNSUPPORTED_CURVE
    submit(0, 3, 0);
    require(0, 21);
#else
    for (unsigned trial = 0; trial != 12; ++trial) {
        unsigned mutation = trial % 6 == 5 ? 0 : trial % 6;
        submit(mutation, 4, trial >= 6);
        require((CSR(0x60) & 0x70) == 0x40, 2);
        require(CSR(0x128) == 1, 3);
        require(CSR(0x144) == (mutation != 0), 4 + trial);
        CSR(0x130) = 1;
        require(CSR(0x60) == 0 && CSR(0x128) == 0, 20);
    }
    *(volatile uint32_t *)0x600d0000U = 1;
    for (;;) {}
#endif
}

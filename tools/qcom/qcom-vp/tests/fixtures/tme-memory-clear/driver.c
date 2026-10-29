// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <stdint.h>

#define CSR(offset) (*(volatile uint32_t *)(0x02230000U + (offset)))
#define INPUT ((volatile uint32_t *)0x02281000U)
#define BYTES ((volatile unsigned char *)0x400000U)
#ifndef CASE
#define CASE 0
#endif

static void require(int condition, unsigned step) {
    if (!condition) {
        *(volatile uint32_t *)(0xbad00000U + step * 4) = step;
        for (;;) {}
    }
}

static void acquire(void) {
    CSR(0x60) = 0x100;
    require((CSR(0x60) & 0x70) == 0x10, 1);
    CSR(0) = 0x11;
    // The source writes only the selector. These words can retain old values.
    CSR(4) = 0x1357;
    CSR(8) = 0x2468;
    CSR(12) = 0x369c;
}

static void submit(void) {
    __asm__ volatile("dsb sy" ::: "memory");
    CSR(0x60) = 0x200;
    __asm__ volatile("dsb sy" ::: "memory");
}

void test(void) {
    const unsigned lengths[] = {1, 255, 256, 257, 519, 520, 777};
    for (unsigned trial = 0; trial != sizeof(lengths) / sizeof(lengths[0]); ++trial) {
        for (unsigned i = 0; i != 1024; ++i) BYTES[i] = (unsigned char)(1 + i % 255);
        acquire();
        INPUT[0] = 1;
        INPUT[1] = 0x400003;
        INPUT[2] = lengths[trial];
        INPUT[3] = 0x400003;
        submit();
        require((CSR(0x60) & 0x70) == 0x40, 2);
        require(CSR(0x128) == 1 && CSR(0x144) == 0, 3);
        for (unsigned i = 0; i != 1024; ++i) {
            unsigned char expected = i >= 3 && i < 3 + lengths[trial]
                ? 0 : (unsigned char)(1 + i % 255);
            require(BYTES[i] == expected, 4);
        }
        CSR(0x130) = 1;
        require(CSR(0x60) == 0 && CSR(0x128) == 0, 5);
    }
#if CASE != 0
    acquire();
    INPUT[0] = CASE == 1 ? 2 : 1;
    INPUT[1] = CASE == 4 ? 0xfffffff0U : CASE == 5 ? 0x02289000U : 0x400003U;
    INPUT[2] = CASE == 6 ? 0 : 32;
    // CASE 3 must not reuse input word three from the preceding acquisition.
    if (CASE != 3) INPUT[3] = CASE == 2 ? 0x400004U : INPUT[1];
    submit();
    // Rejected framing and denied writes must stop at the submission register.
    require(0, 6);
#else
    *(volatile uint32_t *)0x600d0000U = 1;
#endif
    for (;;) {}
}

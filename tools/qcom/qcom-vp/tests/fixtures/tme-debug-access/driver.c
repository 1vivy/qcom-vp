// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <stdint.h>

#define CSR(offset) (*(volatile uint32_t *)(0x02230000U + (offset)))
#define INPUT ((volatile uint32_t *)0x02281000U)
#define OUTPUT ((volatile uint32_t *)0x02289000U)
#define WORD(address) (*(volatile uint32_t *)(address))

static void require(int condition, unsigned step) {
    if (!condition) {
        WORD(0xbad00000U + step * 4) = step;
        for (;;) {}
    }
}

void test(void) {
    WORD(0x400304U) = 0xc000007f;
    WORD(0x500304U) = 0xc000077f;
    require(WORD(0x500304U) == 0xc000077f, 10);
    WORD(0x510fd8U) = 0x480;
    require(WORD(0x510fd8U) == 0x400, 11);
    WORD(0x510fc4U) = 1;
    require(WORD(0x510fc4U) == 1, 12);
    WORD(0x510fc0U) = 1;
    WORD(0x510fd8U) = 0;
    require(WORD(0x510fd8U) == 0x400, 13);
    require(WORD(0x501018U) == 0, 14);
    WORD(0x402100U) = 0x12345678;
    CSR(0x60) = 0x100;
    CSR(0) = 0x25;
    CSR(4) = 0x80;
    CSR(8) = 0x1357;
    CSR(12) = 0x2468;
    INPUT[0] = 2;
    INPUT[1] = 0x400304;
    INPUT[2] = 0x500304;
    __asm__ volatile("dsb sy" ::: "memory");
    CSR(0x60) = 0x200;
    require((CSR(0x60) & 0x70) == 0x40, 1);
    require(CSR(0x128) == 1 && CSR(0x144) == 0x100, 2);
    require(OUTPUT[0] == 0, 3);
    require(WORD(0x400304U) == 0x80000001, 4);
    require(WORD(0x500304U) == 0x80000001, 5);
    require(WORD(0x402100U) == 0x12345678, 6);
    require(WORD(0x400000U) == 0x30000 && WORD(0x500000U) == 0x3ff0001, 9);
    CSR(0x130) = 1;
    require(CSR(0x60) == 0 && CSR(0x128) == 0, 7);
#ifdef INCOMPLETE_PAYLOAD
    // A new acquisition must not reuse the preceding address list.
    CSR(0x60) = 0x100;
    CSR(0) = 0x25;
    INPUT[0] = 2;
    INPUT[1] = 0x400304;
    CSR(0x60) = 0x200;
    require(0, 8);
#else
    WORD(0x600d0000U) = 1;
#endif
    for (;;) {}
}

// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <stdint.h>

#define CSR(offset) (*(volatile uint32_t *)(0x02230000U + (offset)))
#define INPUT ((volatile uint32_t *)0x02281000U)
#define OUTPUT ((volatile uint32_t *)0x02289000U)
#define COMMAND (ROM_PATCH ? 0x18U : 0x29U)
#define FEATURE (ROM_PATCH ? 0x184U : 0x186U)
#define VIRTUAL_POLICY_DENIED UINT32_MAX

static void require(int condition, unsigned step) {
    if (!condition) {
        *(volatile uint32_t *)(0xbad00000U + step * 4) = step;
        for (;;) {}
    }
}

static void acquire(void) {
    CSR(0x60) = 0x100;
    require((CSR(0x60) & 0x70) == 0x10, 1);
    require(CSR(0x148) == 0 && CSR(0x144) == 0, 2);
}

static void release(void) {
    CSR(0x130) = 1;
    require(CSR(0x60) == 0 && CSR(0x128) == 0, 3);
    require(CSR(0x148) == 0, 4);
}

static void submit(void) {
    __asm__ volatile("dsb sy" ::: "memory");
    CSR(0x60) = 0x200;
}

static void entropy(void) {
    acquire();
    CSR(0) = 0x13;
    CSR(4) = 2;
    CSR(8) = 0;
    CSR(12) = 0;
    submit();
    require(CSR(0x144) == 0x200 && CSR(0x148) == 0, 5);
    require(CSR(0x128) == 1, 6);
    (void)OUTPUT[0];
    (void)OUTPUT[1];
    release();
}

void test(void) {
    entropy();
    for (unsigned trial = 0; trial != 4; ++trial) {
        acquire();
        CSR(0) = COMMAND;
        // These words are not part of either authorization command's envelope.
        CSR(4) = 0xabcdef01;
        CSR(8) = 0x12345678;
        CSR(12) = 0x87654321;
        unsigned size = trial == 0 ? 0x2d0 : (trial == 1 && CASE == 0 ? 4 : 8);
        CSR(0x58) = size;
        INPUT[0] = ((size - 4) << 16) | FEATURE;
        for (unsigned word = 1; word != size / 4; ++word) {
#if CASE == 1
            if (trial == 1 && word == 1) continue;
#endif
#if CASE == 7
            if (trial == 1 && word == 1) {
                volatile uint8_t *bytes = (volatile uint8_t *)&INPUT[word];
                for (unsigned byte = 0; byte != 3; ++byte) bytes[byte] = 0x12;
                continue;
            }
#endif
            INPUT[word] = 0x12345678;
        }
        if (trial == 1) {
#if CASE == 2
            INPUT[0] = (4U << 16) | (ROM_PATCH ? 0x186U : 0x184U);
#elif CASE == 3
            INPUT[0] = (1U << 16) | FEATURE; // Nonzero alignment padding in INPUT[1].
#elif CASE == 4
            INPUT[0] = FEATURE; // Header extent four differs from input size eight.
#elif CASE == 5
            CSR(0) = 0x7f;
#elif CASE == 8
            CSR(0x58) = 4100;
#elif CASE == 9
            CSR(0x58) = 7;
#elif CASE == 10
            CSR(0x58) = 0;
#endif
        }
        if (trial == 3) {
            INPUT[0] = (1U << 16) | FEATURE;
            INPUT[1] = 0x78; // A one-byte token with three zero padding bytes.
        }
        submit();
        require((CSR(0x60) & 0x70) == 0x40, 7);
        require(CSR(0x128) == 1 && CSR(0x144) == 1, 8);
        // The hardware command result is not the firmware callback's reason two.
        require(CSR(0x148) == VIRTUAL_POLICY_DENIED && CSR(0x148) != 2, 9);
        require(CSR(0x148) == VIRTUAL_POLICY_DENIED, 10); // Reads retain the error.
        require(CSR(0x160) == 0 && CSR(0x164) == 0, 11);
        release();
        entropy();
    }
    *(volatile uint32_t *)0x600d0000U = 1;
    for (;;) {}
}

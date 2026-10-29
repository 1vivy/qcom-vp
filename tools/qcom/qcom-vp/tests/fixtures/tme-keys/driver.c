// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <stdint.h>

#define CSR(offset) (*(volatile uint32_t *)(0x02230000U + (offset)))
#define INPUT ((volatile uint32_t *)0x02281000U)
#define OUTPUT ((const volatile uint8_t *)0x02289000U)

// Python hashlib and hmac independently calculate these virtual-profile results.
static const uint8_t original[48] = {
    0xe0,0x1c,0xe6,0x31,0x49,0x49,0xd4,0x20,0x43,0x23,0xca,0xec,
    0xe2,0xdd,0xa8,0x73,0x63,0x8a,0x0c,0x03,0x5a,0x18,0xa0,0x79,
    0x86,0xfd,0xcb,0xa6,0xb7,0x93,0xcd,0x5f,0x71,0xf7,0xe3,0x81,
    0xe3,0x73,0x20,0x0b,0xe3,0xb2,0xb1,0x4e,0x80,0x5b,0xbe,0xa0
};
static const uint8_t context_changed[48] = {
    0x1e,0x3e,0x4c,0xc8,0x64,0x01,0x30,0x72,0x69,0x96,0x4e,0xdf,
    0xaf,0xc6,0x43,0x54,0x99,0x21,0xdf,0x90,0x57,0x07,0x92,0x30,
    0xd1,0xc3,0xc4,0x65,0x5b,0x08,0xf9,0xe7,0xba,0x9e,0x58,0x98,
    0x5a,0xdf,0x14,0x61,0x98,0x19,0xe6,0x92,0xed,0xba,0x6d,0xf1
};
static const uint8_t message_changed[48] = {
    0x35,0xff,0xba,0x6c,0xbc,0xbe,0x1a,0x88,0xef,0xb3,0x3f,0xce,
    0x53,0x21,0x0d,0xbc,0x9e,0x5e,0x25,0x07,0x39,0x41,0x9a,0x60,
    0x7f,0x3b,0x07,0x9f,0x9e,0xd7,0x58,0xf2,0xa7,0xdf,0xb4,0xc8,
    0x67,0x64,0x5c,0x46,0xff,0xeb,0xd8,0x64,0x93,0x45,0x7c,0xae
};

static void require(int condition, unsigned step) {
    if (!condition) {
        *(volatile uint32_t *)(0xbad00000U + step * 4) = step;
        for (;;) {}
    }
}

static void acquire(unsigned command, unsigned owner) {
    CSR(0x60) = 0x100;
    require((CSR(0x60) & 0x70) == 0x10, 1);
    CSR(0) = command;
    CSR(4) = 0;
    CSR(8) = 0;
    CSR(12) = 0;
    CSR(0x2c) = owner;
}

static void complete(unsigned error, unsigned words) {
    __asm__ volatile("dsb sy" ::: "memory");
    CSR(0x60) = 0x200;
    require((CSR(0x60) & 0x70) == 0x40, 2);
    require(CSR(0x144) == (error | (words << 8)), 3);
    require((CSR(0x148) != 0) == error, 4);
}

static void release(void) {
    CSR(0x130) = 1;
    require(CSR(0x60) == 0 && CSR(0x128) == 0, 5);
}

static void derive(unsigned slot, unsigned changed, unsigned owner, unsigned error) {
    acquire(2, owner);
    CSR(8) = 3;
    CSR(0x10) = 9;
    CSR(0x14) = slot;
    CSR(0x18) = 0;
    CSR(0x30) = 2;
    CSR(0x40) = 8;
    CSR(0x44) = 4;
    CSR(0x48) = 1;
    CSR(0x54) = 4;
    INPUT[0] = 0x12345678;
    INPUT[1] = 0xa5a5a5a5;
    INPUT[2] = 0x30;
    INPUT[3] = 0x04030201 ^ changed;
    INPUT[4] = 0x08070605;
    INPUT[5] = 0x74736574;
    complete(error, 0);
    release();
}

static void keyed_hash(unsigned slot, unsigned owner, unsigned message,
                       const uint8_t *expected) {
    acquire(4, owner);
    CSR(4) = 2;
    CSR(8) = 1;
    CSR(12) = 3;
    CSR(0x10) = slot;
    CSR(0x58) = 32;
    CSR(0x5c) = 32;
    INPUT[0] = 2;
    INPUT[1] = 0x400000 + message;
    INPUT[2] = 13;
    INPUT[3] = 0;
    INPUT[4] = 0x40000d + message;
    INPUT[5] = 19;
    INPUT[6] = 0;
    complete(expected == 0, expected ? 12 : 0);
    if (expected) {
        for (unsigned i = 0; i != 48; ++i) require(OUTPUT[i] == expected[i], 16 + i);
    }
    release();
}

static void clear(unsigned slot, unsigned owner, unsigned error) {
    acquire(0xd, owner);
    CSR(0x10) = slot;
    complete(error, 0);
    release();
}

// OEM descriptor 0x443760 supplies this version-four ECC P-384 ECDH policy.
static void generate(unsigned slot, unsigned owner, unsigned credential, unsigned error) {
    acquire(3, owner);
    CSR(0x14) = slot;
    CSR(0x30) = credential;
    INPUT[0] = 0x04204829;
    INPUT[1] = 0x00024040;
    complete(error, 0);
    release();
}

// Donor submit 0x21de92 sends P-384 curve 4 and no input bytes. Command 0x28 sets xCoordOnly.
static void public_key(unsigned slot, unsigned owner, unsigned x_only, unsigned error,
                       uint8_t *full_point) {
    const unsigned bytes = x_only ? 48 : 96;
    acquire(x_only ? 0x28 : 0x0a, owner);
    CSR(0x10) = slot;
    CSR(0x38) = 4;
    CSR(0x58) = 0;
    complete(error, error ? 0 : bytes / 4);
    if (!error) {
        unsigned nonzero = 0;
        for (unsigned i = 0; i != bytes; ++i) {
            nonzero |= OUTPUT[i];
            if (x_only) require(OUTPUT[i] == full_point[i], 81);
            else full_point[i] = OUTPUT[i];
        }
        require(nonzero != 0, 80);
    }
    release();
}

void test(void) {
    uint8_t point[96];
    keyed_hash(0x1e, 2, 0, 0);
    derive(0x1e, 0, 3, 1);
    derive(0x1e, 0, 2, 0);
    keyed_hash(0x1e, 2, 0, original);
    keyed_hash(0x1e, 3, 0, 0);
    clear(0x1e, 3, 1);
    keyed_hash(0x1e, 2, 32, message_changed);
    derive(0x1f, 1, 2, 0);
    keyed_hash(0x1f, 2, 0, context_changed);
    clear(0x1e, 2, 0);
    keyed_hash(0x1e, 2, 0, 0);
    keyed_hash(0x1f, 2, 0, context_changed);
    derive(9, 0, 2, 1);
    derive(0x1e, 0, 2, 0);
    keyed_hash(0x1e, 2, 0, original);
    clear(0x1e, 2, 0);
    clear(0x1f, 2, 0);
    generate(0x19, 1, 3, 1);
    generate(0x19, 1, 2, 0);
    generate(0x19, 1, 2, 1);
    public_key(0x19, 2, 0, 1, point);
    public_key(0x19, 1, 0, 0, point);
    public_key(0x19, 1, 1, 0, point);
    derive(0x1e, 0, 2, 0);
    public_key(0x1e, 2, 1, 1, point);
    clear(0x1e, 2, 0);
    keyed_hash(0x19, 1, 0, 0);
    clear(0x19, 2, 1);
    clear(0x19, 1, 0);
    generate(0x19, 1, 2, 0);
    clear(0x19, 1, 0);
    *(volatile uint32_t *)0x600d0000U = 1;
    for (;;) {}
}

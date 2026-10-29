// SPDX-License-Identifier: BSD-3-Clause-Clear
// RPMB backing-file example. One image runs two boots. A factory device
// programs the key and writes two blocks. A device with a key verifies the
// counter and the media, then rejects a replay, another key, and a changed
// frame without a new counter. The device state selects the branch, so the
// same image runs again with the copied state file.
#include <stdint.h>

#define MMIO(base, offset) (*(volatile uint32_t *)((uintptr_t)(base) + (offset)))
#define HCI 0x01d84000U
#define PHY 0x01d80000U
#define MEM_GDSC 0x0019e000U
#define LOGIC_GDSC 0x00177004U
#define REFERENCE_CLOCK 0x01fd5054U
#define UTRD 0x14800000U
#define COMMAND 0x14801000U
#define RESPONSE (COMMAND + 0x400U)
#define DATA 0x14803000U

static volatile uint8_t *const command = (volatile uint8_t *)COMMAND;
static volatile uint8_t *const response = (volatile uint8_t *)RESPONSE;
#define FRAMES ((volatile uint8_t *)DATA)

static void require(int condition, uint32_t step) {
    if (!condition) {
        MMIO(0xbad00000U, step * 4) = step;
        for (;;) {}
    }
}

static void clear(uintptr_t address, unsigned size) {
    volatile uint8_t *bytes = (volatile uint8_t *)address;
    for (unsigned i = 0; i != size; ++i) bytes[i] = 0;
}

static void be16(volatile uint8_t *bytes, uint16_t value) {
    bytes[0] = value >> 8;
    bytes[1] = value;
}

static void be32(volatile uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) bytes[i] = value >> (24 - 8 * i);
}

static uint16_t read_be16(const volatile uint8_t *bytes) {
    return (uint16_t)(bytes[0] << 8 | bytes[1]);
}

static uint32_t read_be32(const volatile uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
        (uint32_t)bytes[2] << 8 | bytes[3];
}

static uint32_t uic(uint32_t operation, uint32_t attribute, uint32_t value) {
    MMIO(HCI, 0x94) = attribute << 16;
    MMIO(HCI, 0x98) = 0;
    MMIO(HCI, 0x9c) = value;
    MMIO(HCI, 0x90) = operation;
    while (!(MMIO(HCI, 0x20) & 0x400)) {}
    MMIO(HCI, 0x20) = 0x400;
    return MMIO(HCI, 0x98);
}

static void request_length(unsigned direction, uint32_t bytes) {
    clear(UTRD, 32);
    clear(COMMAND, 0x1000);
    MMIO(UTRD, 0) = 1U << 28 | 1U << 24 | direction << 25;
    MMIO(UTRD, 8) = 15;
    MMIO(UTRD, 16) = COMMAND;
    MMIO(UTRD, 24) = 0x01000080;
    MMIO(UTRD, 28) = 0x02000000 | (direction != 0);
    if (direction) {
        // The PRDT entry stores the byte count minus one in its low 18 bits.
        MMIO(COMMAND, 0x800) = DATA;
        MMIO(COMMAND, 0x80c) = bytes - 1;
    }
}

static void request(unsigned direction) {
    request_length(direction, 4096);
}

static uint32_t submit(void) {
    __asm__ volatile("dsb sy" ::: "memory");
    MMIO(HCI, 0x58) = 1;
    while (MMIO(HCI, 0x58) & 1) {}
    __asm__ volatile("dsb sy" ::: "memory");
    require(MMIO(HCI, 0x20) & 1, 1);
    while (!(MMIO(0x17000000U, 0x224) & (1U << 9))) {}
    MMIO(HCI, 0x20) = 1;
    while (MMIO(0x17000000U, 0x224) & (1U << 9)) {}
    return MMIO(UTRD, 8) & 15;
}

static uint32_t query(unsigned opcode, unsigned id, unsigned index,
                      unsigned selector, uint32_t value) {
    request(0);
    command[0] = 0x16;
    command[5] = opcode == 4 || (opcode >= 6 && opcode <= 8) ? 0x81 : 1;
    command[12] = opcode;
    command[13] = id;
    command[14] = index;
    command[15] = selector;
    command[19] = 255;
    be32(command + 20, value);
    return submit();
}

static void power(unsigned base, int enabled, unsigned step) {
    MMIO(base, 0) = enabled ? 0 : 1;
    require(MMIO(base, 4) == (enabled ? 1U << 16 : 1U << 15), step);
    require(MMIO(base, 0) == (enabled ? 1U << 31 : 1), step);
}

static void initialize(void) {
    MMIO(PHY, 0x408) = 1;
    MMIO(PHY, 0x404) = 1;
    MMIO(PHY, 0x5fc) = 0;
    MMIO(PHY, 0x42c) = 0x20;
    MMIO(PHY, 0x408) = 0;
    MMIO(PHY, 0x400) = 1;
    require(MMIO(PHY, 0x5a8) == 1, 4);
    MMIO(HCI, 0x34) = 1;
    require(uic(0x16, 0, 0) == 0, 5);
}

static void start_list(void) {
    MMIO(HCI, 0x50) = UTRD;
    MMIO(HCI, 0x54) = 0;
    MMIO(HCI, 0x60) = 1;
    MMIO(HCI, 0x24) = 1;
}

// Python hmac computes these JESD220 HMAC-SHA256 values with key bytes 1..32.
static const uint8_t mac_write[32] = {0x30,0xe2,0x4e,0xdc,0x48,0x93,0xf0,0xed,0x07,0xd2,0x99,0x61,0x5d,0x17,0xd4,0x73,0x16,0xee,0x2a,0x9e,0xc9,0xc1,0x4a,0xf1,0x32,0x3d,0xe1,0x62,0x99,0x03,0x53,0x8d};
static const uint8_t mac_counter1[32] = {0x52,0xcb,0xbe,0x4d,0x62,0x4d,0xc1,0x83,0x44,0xa9,0x14,0x3e,0xf7,0x39,0x60,0xc9,0xd3,0x81,0xc7,0x49,0xbd,0x91,0x9d,0xea,0x7d,0xa6,0xbd,0x2a,0x37,0x8e,0xaa,0x80};
static const uint8_t mac_write_result[32] = {0x46,0x4d,0x40,0xcd,0xda,0x0a,0x3c,0xbf,0x1a,0xb2,0xf7,0x0f,0x40,0x51,0x45,0x8a,0x81,0xd2,0x51,0x0e,0x82,0xcf,0x7c,0x8e,0x20,0xb9,0x52,0xaf,0xcd,0x82,0x5c,0xd0};
static const uint8_t mac_replay_result[32] = {0x8c,0x53,0x19,0xdb,0x92,0xdb,0x31,0xf3,0x41,0x92,0xaa,0x3e,0xd7,0x3b,0x0c,0x58,0xe2,0xb0,0xd0,0x38,0x6b,0x3d,0x52,0x0d,0xef,0xdb,0x7b,0xb8,0x34,0xc1,0xfc,0xf5};
static const uint8_t mac_tamper_result[32] = {0x48,0xa8,0xb9,0x88,0x42,0x5a,0x46,0xa1,0x25,0xb9,0x5f,0xf7,0x35,0xdc,0xd7,0xb7,0x48,0x5b,0xe4,0x9c,0x0c,0xb7,0x78,0x3b,0x86,0x7a,0x8c,0xe3,0x5c,0xc6,0x2f,0xae};
static const uint8_t mac_forged_result[32] = {0x19,0xa3,0x01,0x68,0x4c,0x01,0x04,0xc9,0x41,0x04,0xea,0x95,0x67,0x7a,0xc8,0xdc,0x54,0xf1,0xbc,0x9e,0xc2,0x1d,0x04,0x4a,0x8b,0xc3,0x2e,0xf5,0xfe,0x14,0x9d,0x94};
static const uint8_t mac_read[32] = {0x8c,0xa6,0x02,0x7c,0x9c,0x1d,0x42,0x11,0x8a,0xaa,0x8e,0x11,0xe4,0x16,0xdf,0x7c,0xb0,0x94,0xd8,0x83,0x5a,0x9b,0xd6,0xbf,0x2a,0xb8,0x9c,0x5f,0x03,0xc0,0xc8,0xa8};
// Signed with bytes 0x21..0x40 instead of the programmed key.
static const uint8_t mac_wrongkey[32] = {0x75,0xa4,0xc9,0x3b,0x74,0xd3,0x7d,0x00,0x20,0xd4,0x67,0xd7,0x6a,0xf0,0x6b,0xbb,0x54,0xc4,0x09,0x87,0x97,0x7d,0x00,0xe1,0xa3,0xc8,0xb5,0x37,0x8e,0x5e,0x53,0x3f};

// Frame fields: MAC 196, data 228, nonce 484, counter 500, address 504,
// block count 506, result 508, and type 510.
static void rpmb_frame(unsigned index, unsigned type, uint32_t counter,
                       unsigned address, unsigned blocks, unsigned nonce) {
    volatile uint8_t *frame = FRAMES + index * 512;
    for (unsigned i = 0; i != 512; ++i) frame[i] = 0;
    if (type == 3) {
        for (unsigned i = 0; i != 256; ++i) frame[228 + i] = index * 16 + i;
    }
    for (unsigned i = 0; nonce && i != 16; ++i) frame[484 + i] = nonce + i;
    be32(frame + 500, counter);
    be16(frame + 504, address);
    be16(frame + 506, blocks);
    be16(frame + 510, type);
}

static void rpmb_mac(unsigned index, const uint8_t *mac) {
    for (unsigned i = 0; i != 32; ++i) FRAMES[index * 512 + 196 + i] = mac[i];
}

static int rpmb_mac_is(unsigned index, const uint8_t *mac) {
    for (unsigned i = 0; i != 32; ++i) {
        if (FRAMES[index * 512 + 196 + i] != (mac ? mac[i] : 0)) return 0;
    }
    return 1;
}

// SECURITY PROTOCOL OUT or IN with protocol EC and specific value 0001.
static unsigned rpmb_security(unsigned out, unsigned frames) {
    request(out ? 1 : 2);
    if (!out) clear(DATA, 4096);
    command[0] = 1;
    command[1] = out ? 0x20 : 0x40;
    command[2] = 0xc4;
    be32(command + 12, frames * 512);
    command[16] = out ? 0xb5 : 0xa2;
    command[17] = 0xec;
    command[19] = 1;
    be32(command + 22, frames * 512);
    require(submit() == 0, 130);
    return response[7];
}

static void rpmb_result(unsigned type, unsigned result, uint32_t counter,
                        const uint8_t *mac, unsigned step) {
    rpmb_frame(0, 5, 0, 0, 0, 0);
    require(rpmb_security(1, 1) == 0, step);
    require(rpmb_security(0, 1) == 0, step + 1);
    require(read_be16(FRAMES + 510) == type << 8 &&
            read_be16(FRAMES + 508) == result &&
            read_be32(FRAMES + 500) == counter && rpmb_mac_is(0, mac), step + 2);
}

static void rpmb_build_write(unsigned address, uint32_t counter,
                             const uint8_t *mac) {
    rpmb_frame(0, 3, counter, address, 2, 0);
    rpmb_frame(1, 3, counter, address, 2, 0);
    rpmb_mac(1, mac);
}

static void rpmb_write(unsigned address, uint32_t counter, const uint8_t *mac) {
    rpmb_build_write(address, counter, mac);
    require(rpmb_security(1, 2) == 0, 131);
}

static void rpmb_counter(uint32_t expected, const uint8_t *mac, unsigned step) {
    rpmb_frame(0, 2, 0, 0, 0, 0x10);
    require(rpmb_security(1, 1) == 0 && rpmb_security(0, 1) == 0, step);
    require(read_be16(FRAMES + 508) == 0 &&
            read_be32(FRAMES + 500) == expected && rpmb_mac_is(0, mac), step + 1);
}

static void rpmb_read_blocks(unsigned address, unsigned blocks, unsigned nonce,
                             unsigned step) {
    rpmb_frame(0, 4, 0, address, blocks, nonce);
    require(rpmb_security(1, 1) == 0 && rpmb_security(0, blocks) == 0, step);
    for (unsigned f = 0; f != blocks; ++f) {
        volatile uint8_t *frame = FRAMES + f * 512;
        for (unsigned i = 0; i != 256; ++i) {
            require(frame[228 + i] == (uint8_t)(f * 16 + i), step + 1);
        }
        require(read_be16(frame + 504) == address &&
                read_be16(frame + 506) == blocks &&
                read_be16(frame + 508) == 0 &&
                read_be16(frame + 510) == 0x400 &&
                frame[484] == nonce, step + 2);
    }
    require(rpmb_mac_is(blocks - 1, mac_read), step + 3);
}

// Factory device: the counter read above reports NO_KEY with a zero MAC.
static void provision(void) {
    require(read_be16(FRAMES + 508) == 7 && read_be16(FRAMES + 510) == 0x200 &&
            rpmb_mac_is(0, 0), 210);
    // A write before the key is programmed returns NO_KEY.
    rpmb_write(3, 0, mac_write);
    rpmb_result(3, 7, 0, 0, 211);
    // The key programs once. A second key returns general failure.
    rpmb_frame(0, 1, 0, 0, 0, 0);
    for (unsigned i = 0; i != 32; ++i) FRAMES[196 + i] = i + 1;
    require(rpmb_security(1, 1) == 0, 212);
    rpmb_result(1, 0, 0, 0, 213);
    rpmb_frame(0, 1, 0, 0, 0, 0);
    require(rpmb_security(1, 1) == 0, 214);
    rpmb_result(1, 1, 0, 0, 215);
    // The authenticated write advances the counter and stores two blocks.
    rpmb_write(3, 0, mac_write);
    rpmb_result(3, 0, 1, mac_write_result, 216);
    rpmb_counter(1, mac_counter1, 218);
    rpmb_read_blocks(3, 2, 0x40, 220);
}

// Restart: the key, the counter, and the media come from the backing file.
static void resume(void) {
    require(read_be16(FRAMES + 508) == 0 && read_be32(FRAMES + 500) == 1 &&
            rpmb_mac_is(0, mac_counter1), 230);
    rpmb_read_blocks(3, 2, 0x40, 232);
    // A replayed counter fails and does not advance the counter.
    rpmb_write(3, 0, mac_write);
    rpmb_result(3, 3, 1, mac_replay_result, 236);
    // A frame signed with another key fails authentication.
    rpmb_write(1023, 1, mac_wrongkey);
    rpmb_result(3, 2, 1, mac_forged_result, 238);
    // A changed media byte breaks the MAC of an otherwise valid frame.
    rpmb_build_write(3, 0, mac_write);
    FRAMES[228] ^= 0xff;
    require(rpmb_security(1, 2) == 0, 240);
    rpmb_result(3, 2, 1, mac_tamper_result, 241);
    // Rejected writes leave the counter and the media unchanged.
    rpmb_counter(1, mac_counter1, 243);
    rpmb_read_blocks(3, 2, 0x40, 245);
}

void test(void) {
    power(MEM_GDSC, 1, 200);
    power(LOGIC_GDSC, 1, 201);
    MMIO(REFERENCE_CLOCK, 0) = 1;
    require(MMIO(REFERENCE_CLOCK, 0) == 1, 202);
    initialize();
    start_list();
    request(0);
    require(submit() == 0 && response[0] == 0x20, 203);
    // The RPMB well-known LUN answers TEST UNIT READY.
    request(0);
    command[0] = 1;
    command[2] = 0xc4;
    require(submit() == 0 && response[7] == 0, 204);
    // The descriptor declares 256 KiB as 1024 blocks of 256 bytes.
    require(query(1, 2, 0xc4, 0, 0) == 0 && response[32 + 3] == 1 &&
            response[32 + 10] == 8 && read_be32(response + 32 + 15) == 1024 &&
            response[32 + 0x13] == 2, 205);
    // The device state selects the phase of this image.
    rpmb_frame(0, 2, 0, 0, 0, 0x10);
    require(rpmb_security(1, 1) == 0 && rpmb_security(0, 1) == 0, 206);
    if (read_be16(FRAMES + 508) == 7) {
        provision();
    } else {
        resume();
    }
    MMIO(0x600d0000U, 0) = 1;
    for (;;) {}
}

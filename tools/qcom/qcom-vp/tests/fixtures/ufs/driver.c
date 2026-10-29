#include <stdint.h>

#define MMIO(base, offset) (*(volatile uint32_t *)((uintptr_t)(base) + (offset)))
#define HCI 0x01d84000U
#define PHY 0x01d80000U
#define MEM_GDSC 0x0019e000U
#define LOGIC_GDSC 0x00177004U
#define BLOCK_RESET 0x00177000U
#define DEVICE_RESET 0x0f1e9000U
#define REFERENCE_CLOCK 0x01fd5054U
#define UTRD 0x14800000U
#define COMMAND 0x14801000U
#define RESPONSE (COMMAND + 0x400U)
#define DATA 0x14803000U

static volatile uint8_t *const command = (volatile uint8_t *)COMMAND;
static volatile uint8_t *const response = (volatile uint8_t *)RESPONSE;

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

static void be32(volatile uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) bytes[i] = value >> (24 - 8 * i);
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

static void device_readiness(unsigned reserved) {
    request(0);
    command[0] = 1;
    command[2] = 0xd0;
    command[17] = reserved;
    require(submit() == 0, 115);
    if (reserved) {
        require(response[7] == 2 && (response[36] & 15) == 5 &&
                response[46] == 0x24, 116);
    } else {
        require(response[6] == 0 && response[7] == 0, 117);
    }
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

static void wait_idle_tick(void) {
    uint64_t frequency, start, now;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    do {
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now - start < frequency / 8);
}

static void writebooster_flush(void) {
    require(query(3, 0x1d, 0, 0, 0) == 0 &&
            read_be32(response + 20) == 9, 130);
    require(query(6, 0x10, 0, 0, 0) == 0, 131);
    wait_idle_tick();
    require(query(3, 0x1d, 0, 0, 0) == 0 &&
            read_be32(response + 20) == 9, 132);
    require(query(7, 0x10, 0, 0, 0) == 0, 133);
    require(uic(0x17, 0, 0) == 0, 134);
    wait_idle_tick();
    require(uic(0x18, 0, 0) == 0, 135);
    require(query(3, 0x1d, 0, 0, 0) == 0 &&
            read_be32(response + 20) == 9, 136);
    require(query(6, 0x10, 0, 0, 0) == 0, 137);
    require(uic(0x17, 0, 0) == 0, 138);
    wait_idle_tick();
    require(uic(0x18, 0, 0) == 0, 139);
    require(query(3, 0x1d, 0, 0, 0) == 0 &&
            read_be32(response + 20) == 10, 140);
}

static void configuration_geometry(void) {
    const unsigned blocks[] = {1, 1, 1, 2, 3, 4};
    uint8_t configuration[0xe6];
    require(query(1, 0, 0, 0, 0) == 0, 118);
    const unsigned base = response[32 + 0x1a];
    const unsigned stride = response[32 + 0x1b];
    require(base == 0x16 && stride == 0x1a && response[32 + 6] == 6, 119);
    require(query(1, 7, 0, 0, 0) == 0, 120);
    const uint64_t allocation_bytes =
        (uint64_t)read_be32(response + 32 + 0x0d) * response[32 + 0x11] * 512;
    const uint64_t raw_sectors =
        (uint64_t)read_be32(response + 32 + 4) << 32 |
        read_be32(response + 32 + 8);
    require(raw_sectors == 12 * 8, 121);
    require(query(1, 1, 0, 0, 0) == 0 &&
            response[32] == sizeof(configuration) && response[33] == 1, 122);
    for (unsigned i = 0; i != sizeof(configuration); ++i) {
        configuration[i] = response[32 + i];
    }
    for (unsigned lun = 0; lun != 6; ++lun) {
        const uint8_t *entry = configuration + base + stride * lun;
        require(entry[0] == 1 && entry[1] == (lun < 3 ? lun : 0), 123);
        const uint64_t allocated = read_be32(entry + 4) * allocation_bytes;
        for (unsigned attempt = 0; attempt != 2; ++attempt) {
            request(2);
            command[0] = 1;
            command[1] = 0x40;
            command[2] = lun;
            be32(command + 12, 8);
            command[16] = 0x25;
            MMIO(COMMAND, 0x80c) = 7;
            require(submit() == 0, 124);
            if (response[7] == 0) break;
            require(attempt == 0 && response[7] == 2 &&
                    (response[36] & 15) == 6, 125);
        }
        const uint32_t count = read_be32((volatile uint8_t *)DATA) + 1;
        const uint32_t block_bytes = read_be32((volatile uint8_t *)DATA + 4);
        require(response[7] == 0 && count == blocks[lun] && block_bytes == 4096, 126);
        require(allocated == (uint64_t)count * block_bytes &&
                entry[9] < 32 && (1U << entry[9]) == block_bytes, 127);
    }
    for (unsigned lun = 6; lun != 8; ++lun) {
        const uint8_t *entry = configuration + base + stride * lun;
        require(entry[0] == 0 && read_be32(entry + 4) == 0, 128);
    }
    require(query(1, 1, 1, 0, 0) == 1 && response[6] == 0xfc, 129);
    require(query(1, 1, 0, 1, 0) == 1 && response[6] == 0xfb, 130);
    request(0);
    command[0] = 0x16;
    command[5] = 0x81;
    command[11] = sizeof(configuration);
    command[12] = 2;
    command[13] = 1;
    command[19] = sizeof(configuration);
    for (unsigned i = 0; i != sizeof(configuration); ++i) {
        command[32 + i] = configuration[i];
    }
    command[35] ^= 1;
    require(submit() == 1 && response[6] == 0xf7, 131);
    require(query(1, 1, 0, 0, 0) == 0, 132);
    for (unsigned i = 0; i != sizeof(configuration); ++i) {
        require(response[32 + i] == configuration[i], 133);
    }
}

static void selection(unsigned value, unsigned expected_response) {
    uint32_t status = query(4, 0, 0, 0, value);
    require(status == (expected_response ? 1U : 0U), 2);
    require(response[6] == expected_response, 3);
}

static void read_blocks(unsigned lun, unsigned blocks, unsigned pattern, unsigned step) {
    request_length(2, blocks * 4096);
    clear(DATA, blocks * 4096);
    command[0] = 1;
    command[1] = 0x40;
    command[2] = lun;
    be32(command + 12, blocks * 4096);
    command[16] = 0x28;
    command[24] = blocks;
    require(submit() == 0 && response[6] == 0 && response[7] == 0, step);
    for (unsigned i = 0; i != blocks * 4096; ++i) {
        require(((volatile uint8_t *)DATA)[i] == pattern, step + 1);
    }
}

static void read_block(unsigned lun, unsigned pattern, unsigned step) {
    read_blocks(lun, 1, pattern, step);
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

static uint32_t local_capability(uint32_t peer_version) {
    require((MMIO(HCI, 0x30) & 15) == 8, 106);
    require(MMIO(HCI, 0x34) == 0 && MMIO(PHY, 0x5a8) == 0, 107);
    require(MMIO(HCI, 0x78) == 0, 108);
    require(uic(1, 0x15a0, 0) == 0 && MMIO(HCI, 0x9c) == peer_version, 112);
    require(uic(1, 0x1586, 0) == 0, 109);
    return MMIO(HCI, 0x9c);
}

// Python hmac computes these JESD220 HMAC-SHA256 values with key bytes 1..32.
static const uint8_t mac_counter[32] = {0x16,0xb8,0xd6,0x0c,0x62,0x28,0x23,0x4b,0x6f,0x61,0xe4,0xbc,0x96,0x0e,0x6d,0xf7,0x31,0x17,0xaf,0xc4,0x6f,0x89,0x22,0x90,0x46,0xf2,0xb1,0x8e,0x02,0xc1,0xdb,0x27};
static const uint8_t mac_write[32] = {0x30,0xe2,0x4e,0xdc,0x48,0x93,0xf0,0xed,0x07,0xd2,0x99,0x61,0x5d,0x17,0xd4,0x73,0x16,0xee,0x2a,0x9e,0xc9,0xc1,0x4a,0xf1,0x32,0x3d,0xe1,0x62,0x99,0x03,0x53,0x8d};
static const uint8_t mac_write_result[32] = {0x46,0x4d,0x40,0xcd,0xda,0x0a,0x3c,0xbf,0x1a,0xb2,0xf7,0x0f,0x40,0x51,0x45,0x8a,0x81,0xd2,0x51,0x0e,0x82,0xcf,0x7c,0x8e,0x20,0xb9,0x52,0xaf,0xcd,0x82,0x5c,0xd0};
static const uint8_t mac_range_write[32] = {0x79,0xa1,0xe7,0x33,0x1f,0x61,0x64,0x30,0x58,0x62,0x23,0x05,0x59,0x3e,0x40,0x3d,0x35,0x79,0xc6,0x69,0xe7,0xcd,0xfb,0x2f,0x84,0x5e,0x8c,0x73,0xa4,0xa6,0xab,0x23};
static const uint8_t mac_read[32] = {0x8c,0xa6,0x02,0x7c,0x9c,0x1d,0x42,0x11,0x8a,0xaa,0x8e,0x11,0xe4,0x16,0xdf,0x7c,0xb0,0x94,0xd8,0x83,0x5a,0x9b,0xd6,0xbf,0x2a,0xb8,0x9c,0x5f,0x03,0xc0,0xc8,0xa8};
static const uint8_t mac_range_result[32] = {0x04,0x19,0x88,0x01,0x9c,0xe6,0x6a,0x54,0x0a,0x08,0xb6,0x41,0x09,0xc4,0x75,0xfb,0x30,0x20,0x34,0x0f,0x8e,0xfc,0x9c,0x33,0xe7,0x23,0x7d,0x90,0x7c,0x85,0x13,0xd4};
static const uint8_t mac_replay_result[32] = {0x8c,0x53,0x19,0xdb,0x92,0xdb,0x31,0xf3,0x41,0x92,0xaa,0x3e,0xd7,0x3b,0x0c,0x58,0xe2,0xb0,0xd0,0x38,0x6b,0x3d,0x52,0x0d,0xef,0xdb,0x7b,0xb8,0x34,0xc1,0xfc,0xf5};
static const uint8_t mac_forged_result[32] = {0x19,0xa3,0x01,0x68,0x4c,0x01,0x04,0xc9,0x41,0x04,0xea,0x95,0x67,0x7a,0xc8,0xdc,0x54,0xf1,0xbc,0x9e,0xc2,0x1d,0x04,0x4a,0x8b,0xc3,0x2e,0xf5,0xfe,0x14,0x9d,0x94};

#define FRAMES ((volatile uint8_t *)DATA)

static void be16(volatile uint8_t *bytes, uint16_t value) {
    bytes[0] = value >> 8;
    bytes[1] = value;
}

static uint16_t read_be16(const volatile uint8_t *bytes) {
    return (uint16_t)(bytes[0] << 8 | bytes[1]);
}

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

static void rpmb_write(unsigned address, uint32_t counter, const uint8_t *mac) {
    rpmb_frame(0, 3, counter, address, 2, 0);
    rpmb_frame(1, 3, counter, address, 2, 0);
    rpmb_mac(1, mac);
    require(rpmb_security(1, 2) == 0, 131);
}

static void rpmb_protocol(void) {
    // The descriptor declares 256 KiB as 1024 blocks of 256 bytes and two 128 KiB units.
    require(query(1, 2, 0xc4, 0, 0) == 0 && response[32 + 3] == 1 &&
            response[32 + 10] == 8 && read_be32(response + 32 + 15) == 1024 &&
            response[32 + 0x13] == 2, 132);
    // An unprogrammed device reports NO_KEY with a zero MAC and no data.
    rpmb_frame(0, 2, 0, 0, 0, 0x10);
    require(rpmb_security(1, 1) == 0 && rpmb_security(0, 1) == 0, 133);
    require(read_be16(FRAMES + 508) == 7 && read_be16(FRAMES + 510) == 0x200 &&
            FRAMES[484] == 0x10 && rpmb_mac_is(0, 0), 134);
    rpmb_write(3, 0, mac_write);
    rpmb_result(3, 7, 0, 0, 135);
    // A read without a pending response and a malformed list are rejected.
    require(rpmb_security(0, 1) == 2, 138);
    rpmb_frame(0, 9, 0, 0, 0, 0);
    require(rpmb_security(1, 1) == 2, 139);
    // The key programs once. A second key is a general failure.
    rpmb_frame(0, 1, 0, 0, 0, 0);
    for (unsigned i = 0; i != 32; ++i) FRAMES[196 + i] = i + 1;
    require(rpmb_security(1, 1) == 0, 140);
    rpmb_result(1, 0, 0, 0, 141);
    rpmb_frame(0, 1, 0, 0, 0, 0);
    require(rpmb_security(1, 1) == 0, 144);
    rpmb_result(1, 1, 0, 0, 145);
    rpmb_frame(0, 2, 0, 0, 0, 0x10);
    require(rpmb_security(1, 1) == 0 && rpmb_security(0, 1) == 0, 148);
    require(read_be16(FRAMES + 508) == 0 && read_be32(FRAMES + 500) == 0 &&
            rpmb_mac_is(0, mac_counter), 149);
    // A valid write advances the counter. A replay and a changed MAC fail.
    rpmb_write(3, 0, mac_write);
    rpmb_result(3, 0, 1, mac_write_result, 150);
    rpmb_write(3, 0, mac_write);
    rpmb_result(3, 3, 1, mac_replay_result, 153);
    rpmb_write(1023, 1, mac_write);
    rpmb_result(3, 2, 1, mac_forged_result, 156);
    rpmb_write(1023, 1, mac_range_write);
    rpmb_result(3, 4, 1, mac_range_result, 159);
    // The read returns the stored blocks with one MAC over both frames.
    rpmb_frame(0, 4, 0, 3, 2, 0x40);
    require(rpmb_security(1, 1) == 0 && rpmb_security(0, 1) == 2, 162);
    rpmb_frame(0, 4, 0, 3, 2, 0x40);
    require(rpmb_security(1, 1) == 0 && rpmb_security(0, 2) == 0, 163);
    for (unsigned f = 0; f != 2; ++f) {
        volatile uint8_t *frame = FRAMES + f * 512;
        for (unsigned i = 0; i != 256; ++i) require(frame[228 + i] == (uint8_t)(f * 16 + i), 164);
        require(read_be16(frame + 504) == 3 && read_be16(frame + 506) == 2 &&
                read_be16(frame + 508) == 0 && read_be16(frame + 510) == 0x400 &&
                frame[484] == 0x40, 165);
    }
    require(rpmb_mac_is(1, mac_read), 166);
}

void test(void) {
    require(MMIO(MEM_GDSC, 0) == 1 && MMIO(MEM_GDSC, 4) == (1U << 15), 40);
    require(MMIO(LOGIC_GDSC, 0) == 1 && MMIO(LOGIC_GDSC, 4) == (1U << 15), 41);
    power(MEM_GDSC, 1, 42);
    require(MMIO(REFERENCE_CLOCK, 0) == 0, 81);
    MMIO(REFERENCE_CLOCK, 0) = 1;
    require(MMIO(REFERENCE_CLOCK, 0) == 1, 82);
    power(LOGIC_GDSC, 1, 43);
    const uint32_t pwm_capability = local_capability(0);
    initialize();
    require(uic(1, 0x15a9, 0) == 0 && MMIO(HCI, 0x9c) == 3, 6);
    require(uic(1, 0x15a0, 0) == 0 && MMIO(HCI, 0x9c) == 3, 99);
    const uint32_t peer_version = MMIO(HCI, 0x9c);
    require(uic(2, 0x15a8, 42) == 0 && uic(4, 0x15a8, 43) == 0, 100);
    require(uic(1, 0x15a8, 0) == 0 && MMIO(HCI, 0x9c) == 42, 101);
    require(uic(3, 0x15a8, 0) == 0 && MMIO(HCI, 0x9c) == 43, 102);
    require(uic(2, 0x15a4, 23) == 0, 7);
    require(uic(1, 0x15a4, 0) == 0 && MMIO(HCI, 0x9c) == 23, 8);
    require(uic(2, 0x1520, 2) != 0, 9);
    require(uic(2, 0x1520, 1) == 0, 10);
    require(uic(2, 0x1560, 1) == 0 && uic(2, 0x1580, 1) == 0, 96);
    require(uic(2, 0x1560, 2) != 0, 97);
    require(uic(1, 0x1560, 0) == 0 && MMIO(HCI, 0x9c) == 1, 98);
    require(uic(2, 0x156a, 2) == 0, 87);
    require(uic(2, 0x1568, 4) == 0 && uic(2, 0x1583, 4) == 0, 88);
    require(uic(2, 0x1568, 5) != 0, 89);
    require(uic(1, 0x1568, 0) == 0 && MMIO(HCI, 0x9c) == 4, 90);
    require(uic(2, 0x1569, 1) == 0 && uic(2, 0x1584, 1) == 0, 91);
    require(uic(2, 0x15b0, 0x1fff) == 0 && uic(2, 0x15b5, 0x7fff) == 0, 92);
    require(uic(2, 0x1571, 0x22) == 0 && (MMIO(HCI, 0x20) & 0x10), 93);
    MMIO(HCI, 0x20) = 0x10;
    require(uic(2, 0x1571, 0x33) != 0 && !(MMIO(HCI, 0x20) & 0x10), 94);
    require(uic(1, 0x1571, 0) == 0 && MMIO(HCI, 0x9c) == 0x22, 95);
    start_list();
    request(0);
    require(submit() == 0 && response[0] == 0x20, 32);
    require(query(6, 1, 0, 0, 0) == 0, 11);
    do {
        require(query(5, 1, 0, 0, 0) == 0, 12);
    } while (read_be32(response + 20) != 0);
    device_readiness(1);
    device_readiness(0);
    // RPMB readiness uses the real link state. It does not authenticate a frame.
    request(0);
    command[0] = 1;
    command[2] = 0xc4;
    require(submit() == 0 && response[7] == 0, 118);
    request(0);
    command[0] = 1;
    command[2] = 0xc4;
    command[17] = 1;
    require(submit() == 0 && response[7] == 2 &&
            (response[36] & 15) == 5 && response[46] == 0x24, 119);
    rpmb_protocol();
    require(query(3, 0, 0, 0, 0) == 0 && read_be32(response + 20) == 1, 13);
    require(query(1, 0, 0, 0, 0) == 0 && response[32 + 8] == 1, 14);
    for (unsigned lun = 0; lun != 3; ++lun) {
        require(query(1, 2, lun, 0, 0) == 0 && response[32 + 4] == lun, 15);
        request(0);
        command[0] = 1;
        command[2] = lun;
        command[16] = 3;
        require(submit() == 0 && response[7] == 2, 16);
    }
    configuration_geometry();
    read_block(0xb0, 0x41, 20);
    selection(2, 0);
    read_block(0xb0, 0x42, 22);
    read_block(1, 0x41, 24);
    read_block(0, 0x40, 26);
    // LUN4 holds three blocks. The read checks 12 KiB of unchanged media data.
    read_blocks(4, 3, 0x44, 170);
    // The APSS initiator is not bulk-enabled. Its control cell uses one 8-byte access.
    volatile uint64_t *control = (volatile uint64_t *)(DATA + 0x3000U);
    *control = 0x1122334455667788ULL;
    require(*control == 0x1122334455667788ULL, 172);
    selection(3, 0xfa);
    require(query(3, 0, 0, 0, 0) == 0 && read_be32(response + 20) == 2, 28);
    require(query(3, 0, 1, 0, 0) == 1 && response[6] == 0xfc, 29);
    require(query(4, 0, 0, 1, 1) == 1 && response[6] == 0xfb, 30);
    require(query(6, 0x0e, 0, 0, 0) == 0, 129);
    request(1);
    for (unsigned i = 0; i != 4096; ++i) ((volatile uint8_t *)DATA)[i] = 0x5a;
    command[0] = 1;
    command[1] = 0x20;
    command[2] = 1;
    be32(command + 12, 4096);
    command[16] = 0x2a;
    command[24] = 1;
    require(submit() == 0 && response[7] == 0, 33);
    writebooster_flush();
    read_block(1, 0x5a, 34);
    read_block(0xb0, 0x42, 36);
    selection(1, 0);
    read_block(0xb0, 0x5a, 38);
    // Each supply must invalidate the physical link and controller state.
    // Completed media writes and device provisioning remain on the device.
    const unsigned supplies[] = {MEM_GDSC, LOGIC_GDSC};
    for (unsigned i = 0; i != 2; ++i) {
        power(supplies[i], 0, 44 + i * 5);
        power(supplies[i], 1, 45 + i * 5);
        require(MMIO(PHY, 0x5a8) == 0, 46 + i * 5);
        require(MMIO(HCI, 0x34) == 0 &&
                MMIO(HCI, 0x60) == 0 && MMIO(HCI, 0x50) == 0 &&
                MMIO(HCI, 0x20) == 0 && MMIO(HCI, 0x24) == 0, 47 + i * 5);
        require(local_capability(peer_version) == pwm_capability, 110);
        initialize();
        start_list();
        read_block(0xb0, 0x5a, 54 + i * 2);
        read_block(2, 0x42, 58 + i * 2);
    }
    MMIO(BLOCK_RESET, 0) = 1;
    require(MMIO(BLOCK_RESET, 0) == 1, 62);
    MMIO(BLOCK_RESET, 0) = 0;
    require(MMIO(BLOCK_RESET, 0) == 0, 63);
    require(MMIO(PHY, 0x5a8) == 0 && MMIO(HCI, 0x34) == 0 &&
            MMIO(HCI, 0x50) == 0 && MMIO(HCI, 0x60) == 0, 64);
    require(MMIO(MEM_GDSC, 4) == (1U << 16) &&
            MMIO(LOGIC_GDSC, 4) == (1U << 16), 65);
    require(local_capability(peer_version) == pwm_capability, 111);
    initialize();
    start_list();
    read_block(0xb0, 0x5a, 66);
    read_block(2, 0x42, 68);
    MMIO(DEVICE_RESET, 0) = 0;
    require(MMIO(DEVICE_RESET, 0) == 0, 70);
    require(MMIO(PHY, 0x5a8) == 1 && MMIO(HCI, 0x34) == 1 &&
            MMIO(HCI, 0x50) == UTRD && MMIO(HCI, 0x60) == 1, 71);
    require((MMIO(HCI, 0x30) & 7) == 0 && uic(0x16, 0, 0) != 0, 72);
    require(uic(3, 0x15a8, 0) != 0, 103);
    require(uic(1, 0x15a0, 0) == 0 && MMIO(HCI, 0x9c) == peer_version, 113);
    MMIO(DEVICE_RESET, 0) = 1;
    require(MMIO(DEVICE_RESET, 0) == 1 && (MMIO(HCI, 0x30) & 7) == 0, 73);
    require(uic(3, 0x15a8, 0) != 0, 104);
    require(uic(1, 0x15a0, 0) == 0 && MMIO(HCI, 0x9c) == peer_version, 114);
    require(uic(0x16, 0, 0) == 0, 74);
    require(uic(1, 0x15a0, 0) == 0 && MMIO(HCI, 0x9c) == 3, 105);
    device_readiness(0);
    for (unsigned lun = 0; lun != 3; ++lun) {
        request(0);
        command[0] = 1;
        command[2] = lun;
        require(submit() == 0 && response[7] == 2 && (response[36] & 15) == 6, 75);
        request(0);
        command[0] = 1;
        command[2] = lun;
        require(submit() == 0 && response[7] == 0, 76);
    }
    read_block(0xb0, 0x5a, 77);
    read_block(2, 0x42, 79);
    MMIO(REFERENCE_CLOCK, 0) = 0;
    require(MMIO(REFERENCE_CLOCK, 0) == 0 && MMIO(PHY, 0x5a8) == 0, 83);
    MMIO(REFERENCE_CLOCK, 0) = 1;
    require(MMIO(PHY, 0x5a8) == 0 && MMIO(HCI, 0x34) == 0, 84);
    initialize();
    start_list();
    read_block(0xb0, 0x5a, 85);
    selection(0, 0);
    request(2);
    command[0] = 1;
    command[1] = 0x40;
    command[2] = 0xb0;
    be32(command + 12, 4096);
    command[16] = 0x28;
    command[24] = 1;
    require(submit() != 0, 31);
    MMIO(0x600d0000U, 0) = 1;
    for (;;) {}
}

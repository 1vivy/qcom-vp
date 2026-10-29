// SPDX-License-Identifier: BSD-3-Clause-Clear
#include <stdint.h>

#ifndef CASE
#define CASE 0
#endif

static uint32_t stage;
static uint32_t read32(uintptr_t address) { return *(volatile uint32_t *)address; }
static void write32(uintptr_t address, uint32_t value) { *(volatile uint32_t *)address = value; }
static void check(int condition) {
    ++stage;
    if (!condition) {
        write32(0xbad00000, stage);
        for (;;) {}
    }
}
static uint32_t pair(unsigned client, unsigned signal) { return (client << 16) | signal; }

void ipcc_probe(void) {
    for (unsigned protocol = 0; protocol != 2; ++protocol) {
        const uintptr_t base = 0x01100000 + protocol * 0x100000;
        const uintptr_t secure = base + 0x1000;
        const uintptr_t apps = base + 0x6000;
        const unsigned clients = 17 + protocol;
        check(read32(secure) == 0x30000);
        check(read32(secure + 4) == pair(1, protocol));
        check(read32(apps + 4) == pair(6, protocol));
        check(read32(secure + 0x48) == 0xffffffff);
        check(read32(apps + 0x48) == 0xffffffff);

        // Delivery identifies the sender, not the destination or physical ID.
        write32(secure + 0x14, pair(6, 2));
        write32(apps + 0xc, pair(1, 2));
        check(read32(secure + 0x48) == pair(6, 2));
        check(read32(secure + 0x48) == pair(6, 2));
        check(read32(apps + 0x48) == 0xffffffff);
        write32(secure + 0x18, pair(6, 2));
        check(read32(secure + 0x48) == 0xffffffff);
        check(read32(secure + 0x600 + 6 * 4) == 4);
        write32(secure + 0x14, pair(6, 2));
        write32(secure + 0x1c, pair(6, 2));
        check(read32(secure + 0x48) == 0xffffffff);

        // Configuration, enable state, and clear operations remain view-local.
        write32(apps + 0x14, pair(1, 7));
        write32(secure + 0xc, pair(6, 7));
        write32(apps + 0xc, pair(1, 2));
        write32(secure + 8, 0x80000001);
        check(read32(secure + 0x48) == 0xffffffff);
        check(read32(apps + 8) == 0);
        check(read32(apps + 0x48) == pair(1, 7));
        write32(secure + 0x38, 1);
        check(read32(apps + 0x48) == pair(1, 7));
        write32(secure + 8, 1);
        write32(apps + 0xc, pair(1, 2));
        check(read32(secure + 0x48) == pair(6, 2));
        check(read32(secure + 0x48) == 0xffffffff);
        check(read32(apps + 0x48) == pair(1, 7));
        write32(apps + 0x1c, pair(1, 7));

        // Loopback uses the same fabric and preserves the local source index.
        write32(secure + 0x14, pair(1, 0));
        write32(secure + 0xc, pair(1, 0));
        check(read32(secure + 0x48) == pair(1, 0));
        check(read32(secure + 0x48) == 0xffffffff);

        // Unattached destinations retain outgoing requests without a response.
        write32(apps + 0x14, pair(13, 0));
        write32(secure + 0x14, pair(13, 0));
        write32(apps + 0xc, pair(13, 0));
        write32(secure + 0xc, pair(13, 0));
        check(read32(apps + 0x48) == 0xffffffff);
        check(read32(secure + 0x48) == 0xffffffff);
        write32(secure + 0x14, pair(clients - 1, 7));
        check(read32(secure + 0x800 + (clients - 1) * 4) == 0x80);
    }

    // Matching client ordinals in another protocol do not receive this send.
    write32(0x01201014, pair(6, 0));
    write32(0x0110600c, pair(1, 0));
    check(read32(0x01201048) == 0xffffffff);
    check(read32(0x01101618) == 1);

    // The real RISC-V guest receives CLIC38, consumes its source-aware ID,
    // and only then publishes a reply. Both address views reach one bank.
    unsigned remaining = 1000000;
    while (!read32(0x400000) && --remaining) {}
    check(remaining != 0);
    check(read32(0x01110000) == 0x30000);
    check(read32(0x01110004) == pair(16, 0));
    check(read32(0x011cf008) == 1);
    // A newer sender's valid pair cannot enter an older receive namespace.
    write32(0x0111100c, pair(6, 5));
    check(read32(0x01106048) == 0xffffffff);
    write32(0x0111100c, pair(16, 5));
    check(read32(0x01110644) == 0x20);
    write32(0x0111001c, pair(17, 5));
    check(read32(0x01110644) == 0);

    write32(0x011cf008, 0x2a);
    write32(0x01106014, pair(16, 4));
    check(read32(0x01106048) == 0xffffffff);
    write32(0x0110600c, pair(16, 3));
    remaining = 1000000;
    while (!read32(0x400004) && --remaining) {}
    check(remaining != 0);
    check(read32(0x400004) == 38);
    remaining = 1000000;
    while (read32(0x01106048) == 0xffffffff && --remaining) {}
    check(remaining != 0);
    check(read32(0x01106048) == pair(16, 4));
    write32(0x0110601c, pair(16, 4));
    check(read32(0x01106048) == 0xffffffff);

#if CASE == 1
    write32(0x0110600c, pair(17, 0));
#elif CASE == 2
    write32(0x0120600c, pair(18, 0));
#elif CASE == 3
    write32(0x0110600c, pair(1, 8));
#elif CASE == 4
    write32(0x01101008, 2);
#endif
    write32(0x600d0000, stage);
    for (;;) {}
}

#include <stdint.h>

static uint32_t read32(uintptr_t address) {
    return *(volatile uint32_t *)address;
}

static void write32(uintptr_t address, uint32_t value) {
    *(volatile uint32_t *)address = value;
    __asm__ volatile("dmb sy" ::: "memory");
}

__attribute__((noreturn)) static void fail(uint32_t stage) {
    // Preserve the stage in AccessMonitor output, which does not include write data.
    write32(0xbad00000 + (uintptr_t)stage * 4, stage);
    for (;;) __asm__ volatile("nop");
}

// A modeled register read runs on the SystemC thread.
// QBox gives that read to the CPU-domain reset thread as one scheduling turn.
// The reset thread applies a reset change that the producer already wrote.
// A shared-memory read stays inside the native CPU and gives no turn.
static void yield_model(void) {
    (void)read32(0x1887c020);
}

// Wait for the reset thread to apply the pending reset changes.
// One turn starts the reset pass.
// Each attached core then suspends for its native reset handshake.
// The remaining turns let the pass continue after each handshake.
// A shared-memory poll keeps the native CPU busy and can miss the whole pass.
// A measured pass needs up to 264 turns during the parallel test run.
// This bound keeps a wide margin over that measurement.
static void settle_model(void) {
    for (unsigned i = 0; i < 1024; ++i)
        yield_model();
}

static void delay(void) {
    // A shared-RAM window that lets the other CPUs run.
    // No clock or status read here can change model state.
    for (unsigned i = 0; i < 32768; ++i)
        (void)read32(0x14800004);
}

static void wait_bits(uintptr_t address, uint32_t mask, uint32_t expected, uint32_t stage) {
    for (unsigned i = 0; i < 1000000; ++i)
        if ((read32(address) & mask) == expected) return;
    fail(stage);
}

static void wait_restart(uint32_t previous, uint32_t stage) {
    settle_model();
    for (unsigned i = 0; i < 1000000; ++i) {
        if ((i & 8191) == 0) yield_model();
        if (read32(0x14800000) > previous) return;
    }
    fail(stage);
}

static void wait_progress(uint32_t boots, uint32_t heartbeat, uint32_t stage) {
    settle_model();
    for (unsigned i = 0; i < 1000000; ++i) {
        if ((i & 8191) == 0) yield_model();
        if (read32(0x14800000) != boots) fail(stage);
        if (read32(0x14800004) != heartbeat) {
            if (read32(0x14800000) != boots) fail(stage);
            return;
        }
    }
    fail(stage);
}

static void check_held(uint32_t stage) {
    settle_model();
    delay();
    const uint32_t boots = read32(0x14800000);
    const uint32_t heartbeat = read32(0x14800004);
    delay();
    if (read32(0x14800000) != boots || read32(0x14800004) != heartbeat) fail(stage);
}
static void check_core_held(unsigned index, uint32_t stage) {
    const uintptr_t counter = 0x14800000 + index * 16;
    settle_model();
    delay();
    const uint32_t boots = read32(counter);
    const uint32_t heartbeat = read32(counter + 4);
    delay();
    if (read32(counter) != boots || read32(counter + 4) != heartbeat) fail(stage);
}

static void wait_core_restart(unsigned index, uint32_t previous, uint32_t stage) {
    settle_model();
    for (unsigned i = 0; i < 1000000; ++i) {
        if ((i & 8191) == 0) yield_model();
        if (read32(0x14800000 + index * 16) > previous) return;
    }
    fail(stage);
}

// The PDP publishes its progress in the shared window above the APSS rows.
// A reset entry writes its own address and stack pointer before the boot count,
// so a visible boot count needs no extra synchronization.
static const uintptr_t pdp_boot = 0x14800800;
static const uintptr_t pdp_entry = 0x14800804;
static const uintptr_t pdp_stack = 0x14800808;
static const uintptr_t pdp_heartbeat = 0x1480080c;
static const uintptr_t pdp_fault = 0x14800810;

static void wait_pdp_boot(uint32_t previous, uint32_t stage) {
    settle_model();
    for (unsigned i = 0; i < 1000000; ++i) {
        if ((i & 8191) == 0) yield_model();
        if (read32(pdp_fault)) fail(stage);
        if (read32(pdp_boot) > previous) return;
    }
    fail(stage);
}

static void wait_pdp_heartbeat(uint32_t previous, uint32_t stage) {
    for (unsigned i = 0; i < 1000000; ++i) {
        if ((i & 8191) == 0) yield_model();
        if (read32(pdp_fault)) fail(stage);
        if (read32(pdp_heartbeat) != previous) return;
    }
    fail(stage);
}

static void check_pdp_held(uint32_t stage) {
    settle_model();
    delay();
    const uint32_t boots = read32(pdp_boot);
    const uint32_t entry = read32(pdp_entry);
    const uint32_t stack = read32(pdp_stack);
    const uint32_t heartbeat = read32(pdp_heartbeat);
    delay();
    if (read32(pdp_boot) != boots || read32(pdp_entry) != entry ||
        read32(pdp_stack) != stack || read32(pdp_heartbeat) != heartbeat)
        fail(stage);
}


static void no_ack(unsigned lane, uint32_t stage) {
    const uintptr_t control = 0x1887c020 + lane * 4;
    const uintptr_t downstream = (lane ? 0x18260000 : 0x18060000) + 8;
    for (unsigned i = 0; i < 256; ++i)
        if ((read32(control) | read32(downstream)) & 2) fail(stage);
}

static void tags(unsigned lane, int bad) {
    const uintptr_t base = lane ? 0x18200000 : 0x18000000;
    const uintptr_t aux = lane ? 0x18240000 : 0x18040000;
    *(volatile uint64_t *)(base + 0x630) = bad ? 7 : lane;
    if (*(volatile uint64_t *)(base + 0x630) != (uint64_t)(bad ? 7 : lane)) fail(199);
    write32(base + 0x30, lane);
    write32(base + 0x38, lane);
    write32(base + 0x40, lane);
    *(volatile uint64_t *)(aux + 0x418) = lane;
    if (read32(aux + 0x418) != lane) fail(200);
    write32(aux + 0x18, lane);
}

static void prepare(unsigned lane) {
    write32(0x1887c0a0 + lane * 4, 0);
    write32(0x18879020 + lane * 4, 0);
    write32(0x188784a0 + lane * 4, 0);
    write32(0x18879000 + lane * 4, 0);
    write32(0x1887c020 + lane * 4, 8);
    delay();
    write32(0x1887c020 + lane * 4, 0);
    delay();
    write32(0x1887c020 + lane * 4, 1);
}

static uint64_t read64(uintptr_t address) {
    return *(volatile uint64_t *)address;
}

static void write64(uintptr_t address, uint64_t value) {
    *(volatile uint64_t *)address = value;
    __asm__ volatile("dmb sy" ::: "memory");
}

static void wait64(uintptr_t address, uint64_t mask, uint64_t expected, uint32_t stage) {
    for (unsigned i = 0; i < 1000000; ++i)
        if ((read64(address) & mask) == expected) return;
    fail(stage);
}
static void sibling_probe(uint64_t control) {
    const uintptr_t base = 0x19880000;
    const uint64_t policy = (UINT64_C(1) << 50) | (UINT64_C(1) << 55) |
        (UINT64_C(3) << 56);
    for (unsigned core = 1; core < 6; ++core) {
        check_core_held(core, 77);
        write64(base + 0x278, (1u << (core + 1)) - 1);
        wait64(base + 0x2b0, 0xff00, ((1u << (core + 1)) - 1) << 8, 78);
        write64(0x19032150 + core * 0x100000, policy);
        check_core_held(core, 79);
        const uint32_t boots = read32(0x14800000);
        const uint32_t previous = read32(0x14800000 + core * 16);
        const uint64_t selected = UINT64_C(1) << (16 + core);
        write64(base + 0x260, control & ~selected);
        wait_core_restart(core, previous, 80);
        if (read32(0x14800008 + core * 16) != 2) fail(103);
        wait_progress(boots, read32(0x14800004), 81);
        write64(base + 0x260, control);
        check_core_held(core, 82);
        wait_progress(boots, read32(0x14800004), 83);
    }
    // Lane-zero changes cannot release either cold lane-one core.
    check_core_held(6, 84);
    check_core_held(7, 85);
    write64(base + 0x278, 1);
    wait64(base + 0x2b0, 0xff00, 0x100, 86);
}


static void outer_probe(void) {
    const uintptr_t base = 0x19880000;
    const uint32_t initial_boots = read32(0x14800000);
    write64(base + 0x398, 0xffffffff);
    write64(base + 0x290, read64(base + 0x290) | 0x101);
    write64(base + 0x328, 0x334100);
    delay();
    wait_progress(initial_boots, read32(0x14800004), 73);
    if (read32(0x14800008) != 0) fail(101);
    write32(0x198f0028, 0x1f004);
    write32(0x198f0038, 0x10004);
    write32(0x198f0040, 0x10004);
    uint64_t control = read64(base + 0x260);
    control &= ~UINT64_C(0x86e0);
    control |= 0xff0001;
    write64(base + 0x260, control);
    check_held(40); // The outer core-reset source must hold the same architectural CPU.
    write64(base + 0x258, 2);
    wait64(base + 0x270, 1, 1, 41);
    control |= 0x100;
    write64(base + 0x260, control);
    write64(0x199a2030, UINT64_C(0x100000002));
    write64(0x198b0008, 0x101);
    // An enabled push cannot complete with an incomplete memory-access policy.
    write64(0x198b0c10, 0x401);
    settle_model();
    if (read64(0x198b0c40) != 0) fail(508);
    write64(0x198b0c10, 1);
    write64(0x198b0c28, UINT64_C(0xc0018003000));
    write64(0x198b0c30, 0x18003000);
    write64(0x198b0c38, 0);
    // Retained configuration alone cannot complete a disabled push.
    settle_model();
    if (read64(0x198b0c40) != 0) fail(509);
    write64(0x198b0c10, 0x401);
    wait64(0x198b0c40, 1, 1, 510);
    if (read64(0x1a8b0c40) != 0) fail(511);
    write64(base + 0x290, 0);
    write64(base + 0x328, 0x334200);
    write64(base + 0x2c0, UINT64_C(0x1102020302020302));
    write64(base + 0x3f8, UINT64_C(0x1102020302020302));
    write64(base + 0x500, UINT64_C(0x33000c0c0c000c));
    write64(base + 0x588, UINT64_C(0x204041041041041));
    write64(base + 0x590, UINT64_C(0x204041041041041));
    write64(base + 0x598, UINT64_C(0x204041041041041));
    write64(base + 0x608, UINT64_C(0x14080a040502));
    write64(base + 0x348, 0x38000000);
    write64(base + 0x350, 0x80000000);
    write64(base + 0x358, UINT64_C(0x880000000));
    write64(base + 0x360, UINT64_C(0x8800000000));
    write64(base + 0x368, 0x14000000);
    write64(base + 0x370, 0x300000);
    write64(base + 0x378, 0);
    write64(base + 0x380, 0);
    write64(base + 0x388, 0);
    write64(base + 0x390, 0x3fffffff);
    write64(base + 0x398, 0xffdfffff);
    write64(base + 0x3a0, UINT64_C(0xfffffffff));
    write64(base + 0x3a8, UINT64_C(0xffffffffff));
    write64(base + 0x3b0, 0x14ffffff);
    write64(base + 0x3b8, 0x3fffff);
    write64(base + 0x3c0, 0);
    write64(base + 0x3c8, 0);
    write64(base + 0x3d0, 0);
    write64(base + 0x310, 1);
    write64(base + 0x10, 0);
    write64(base + 0x258, 0x12);
    write64(base + 0x278, 0x100);
    write64(0x198300f8, UINT64_C(1) << 52);
    // Cold initialization still requires the software clock overrides.
    settle_model();
    if ((read64(base + 0x2b0) & (UINT64_C(31) << 56)) == (UINT64_C(5) << 56)) fail(500);
    write64(0x199a2030, UINT64_C(0x102000003));
    wait64(base + 0x2b0, UINT64_C(31) << 56, UINT64_C(5) << 56, 43);
    write64(base + 0x278, 1);
    wait64(base + 0x2b0, 0xff00, 0x100, 44);
    check_held(45); // Core power ACK is not architectural reset release.
    const uint64_t runtime_policy = (UINT64_C(1) << 50) | (UINT64_C(1) << 55);
    for (unsigned configuration = 0; configuration < 4; ++configuration) {
        const uint64_t policy = runtime_policy | ((uint64_t)configuration << 56);
        write64(0x19032150, policy);
        if (read64(0x19032150) != policy) fail(74);
        check_held(75); // Policy configuration must not release architectural reset.
        wait64(base + 0x2b0, 0xff00, 0x100, 76);
    }
    uint32_t previous = read32(0x14800000);
    control &= ~(UINT64_C(1) << 16);
    write64(base + 0x260, control);
    wait_restart(previous, 46);
    if (read32(0x14800008) != 2) fail(102);
    // QRNG policy changes must not reset an executing core.
    const uint32_t qrng_boots = read32(0x14800000);
    write64(0x198300f8, 0);
    write64(base + 0x310, 0);
    wait_progress(qrng_boots, read32(0x14800004), 201);
    write64(0x198300f8, UINT64_C(1) << 52);
    write64(base + 0x310, 1);
    wait_progress(qrng_boots, read32(0x14800004), 202);
    // QCCE policy must preserve execution and the other lane's state.
    write64(base + 0x338, UINT64_C(1) << 36);
    write64(0x1a880338, UINT64_C(1) << 37);
    write64(base + 0x338, read64(base + 0x338) & ~(UINT64_C(1) << 36));
    if (read64(base + 0x338) != 0 || read64(0x1a880338) != (UINT64_C(1) << 37)) fail(203);
    write64(0x19830100, UINT64_C(1) << 21);
    if (read64(0x1a830100) != 0) fail(204);
    write64(0x198b0008, read64(0x198b0008) | (UINT64_C(1) << 17));
    wait_progress(qrng_boots, read32(0x14800004), 205);
    write32(0x1887c000, 1);
    wait_bits(0x1887c000, 1, 1, 47);
    sibling_probe(control);
    write32(0x19a30000, 6);
    write32(0x19a30030, 1);
    delay();
    if (read32(0x19a30000) & 0x80000000) fail(66); // A zero multiplier cannot lock.
    write32(0x19a30004, 50);
    write32(0x19a3000c, 2);
    wait_bits(0x19a30000, 0x80000000, 0x80000000, 63);
    write32(0x19a30000, 7);
    // NCC mux changes must preserve the running core and its boot count.
    const uint64_t mux_controls[] = {0x2000007, 0x2000027, 0x200002f};
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t boots = read32(0x14800000);
        write64(0x199a2030, mux_controls[i]);
        delay();
        const uint32_t heartbeat = read32(0x14800004);
        wait_progress(boots, heartbeat, 60 + i);
    }
    // The PDP returns clock control to hardware without resetting the cluster.
    const uint64_t software_clock = read64(0x199a2030);
    const uint32_t handover_boots = read32(0x14800000);
    const uint64_t handover_masks[] = {8, 4, 1, UINT64_C(1) << 25};
    for (unsigned i = 0; i < 4; ++i) {
        write64(0x199a2030, read64(0x199a2030) & ~handover_masks[i]);
        wait_progress(handover_boots, read32(0x14800004), 501 + i);
    }
    write64(0x199a2030, software_clock);
    wait_progress(handover_boots, read32(0x14800004), 505);
    // Fail-safe configuration must preserve execution and the other lane.
    const unsigned fail_safe_offsets[] = {0xd8, 0xe0, 0x168, 0x170, 0xfa0, 0xfa8};
    for (unsigned i = 0; i < 6; ++i) {
        const unsigned offset = fail_safe_offsets[i];
        const uint64_t other = read64(0x1a8b0000 + offset);
        const uint64_t value = i < 2 ? ((uint64_t)i + 1) << 36 : i + 1;
        write64(0x198b0000 + offset, value);
        if (read64(0x198b0000 + offset) != value ||
            read64(0x1a8b0000 + offset) != other) fail(506);
    }
    wait_progress(handover_boots, read32(0x14800004), 507);
    // QMX DPM weights are configuration in each lane and do not disturb execution.
    const unsigned qmx_offsets[] = {0xb50, 0x480, 0x4c8, 0x5b8, 0x5e0};
    for (unsigned i = 0; i < 5; ++i) {
        const uint64_t value = UINT64_C(0x0123456700000000) | (qmx_offsets[i] << 4) | i;
        write64(0x19aa0000 + qmx_offsets[i], value);
        if (read64(0x19aa0000 + qmx_offsets[i]) != value ||
            read64(0x1aaa0000 + qmx_offsets[i]) != 0) fail(550);
    }
    write32(0x19aa0b54, 0x89abcdef);
    if (read64(0x19aa0b50) != (UINT64_C(0x89abcdef00000000) | (0xb50u << 4))) fail(551);
    wait_progress(handover_boots, read32(0x14800004), 552);
    // PDP updates runtime power word +0x420 with 32-bit read-modify-write.
    const uint64_t runtime_power = read64(base + 0x420);
    write64(base + 0x420, UINT64_C(0x1234567801000000));
    write32(base + 0x420, read32(base + 0x420) & ~UINT32_C(0x1000000));
    if (read64(base + 0x420) != UINT64_C(0x1234567800000000) ||
        read32(base + 0x424) != 0x12345678) fail(553);
    write64(base + 0x420, runtime_power);
    wait_progress(handover_boots, read32(0x14800004), 554);
    // Enabling runtime DVFS must not repeat the cold initialization gates.
    const uint64_t boot_dvfs = read64(0x198b0008);
    const uint64_t boot_memory = read64(0x198b0c10);
    write64(0x198b0008, boot_dvfs & ~UINT64_C(0x100));
    wait_progress(handover_boots, read32(0x14800004), 512);
    write64(0x198b0008, boot_dvfs & ~UINT64_C(0x101));
    wait_progress(handover_boots, read32(0x14800004), 513);
    write64(0x198b0c10, boot_memory & ~UINT64_C(0x400));
    wait_progress(handover_boots, read32(0x14800004), 514);
    write64(0x198b0c10, (read64(0x198b0c10) & ~UINT64_C(0x3fe)) |
        UINT64_C(0x003e1f0000000260));
    write64(0x198b0008, read64(0x198b0008) | 0x810000);
    wait_progress(handover_boots, read32(0x14800004), 515);
    // A hardware request applies its clock and power profile without a CPU reset.
    write32(0x181cb00c, 0x140a);
    write32(0x181d3014, 0x1e14);
    write32(0x181cba00, (5u << 25) | (100u << 1) | 1);
    write32(0x181d3a00, (5u << 25) | (110u << 1) | 1);
    write64(0x180c06e0, (UINT64_C(3) << 5) | (UINT64_C(5) << 15));
    write64(0x198b0168, 100 | (UINT64_C(0x8000) << 9) | (UINT64_C(1) << 25));
    write64(0x199a2030, software_clock & ~UINT64_C(0x200000d));
    write32(0x19a30030, 0);
    write32(0x198b0000, 0x1e0001);
    const uint64_t applied = (UINT64_C(30) << 45) | (UINT64_C(30) << 40) |
        (UINT64_C(30) << 35) | (UINT64_C(30) << 8) | 30;
    if (read64(0x198b0050) != applied) fail(517);
    if ((read32(0x198b0054) & 0xf9) != (30u << 3)) fail(518);
    if (!(read32(0x19a30000) & 0x80000000)) fail(519);
    if (read64(0x1a8b0050) != 0) fail(520);
    wait_progress(handover_boots, read32(0x14800004), 521);
    write64(base + 0x258, read64(base + 0x258) | 0x20);
    wait_progress(handover_boots, read32(0x14800004), 531);
    // A frequency-only request preserves voltage and memory state at the last extended row.
    write64(0x198b1c20, 71);
    write64(0x198b1cb8, 83);
    write32(0x198b0000, 0x170002);
    if (read64(0x198b1c20) != 71 || read64(0x198b1cb8) != 83) fail(527);
    const uint64_t frequency_only = (applied & ~((UINT64_C(31) << 35) | UINT64_C(0x1f1f))) |
        (UINT64_C(23) << 35) | (UINT64_C(23) << 8) | 23;
    if (read64(0x198b0050) != frequency_only) fail(528);
    wait_progress(handover_boots, read32(0x14800004), 529);
    write32(0x198b0000, 0x1e0001);
    if (read64(0x198b0050) != applied) fail(530);
    // A locked request reports an error without changing the applied state.
    write32(0x198b0000, 0x11e0000);
    if (!(read32(0x198b0054) & 2)) fail(522);
    write32(0x198b0000, 0x11f0002);
    if (read64(0x198b0050) != (applied | (UINT64_C(1) << 33) | (UINT64_C(1) << 24)))
        fail(523);
    write32(0x198b0000, 0x1e0000);
    if (read32(0x198b0054) & 3) fail(524);
    // Configuration stages votes without changing the applied state.
    write32(0x198b0000, 0x41e0000);
    const uint64_t vote_policy = read64(0x198b0c18);
    write64(0x198b0c18, vote_policy & ~UINT64_C(0x80));
    write64(0x180c06a8, (UINT64_C(3) << 5) | (UINT64_C(5) << 15));
    write32(0x198b1d10, (30u << 1) | 1);
    write32(0x198b1d18, (23u << 1) | 1);
    if (read64(0x198b0050) != applied) fail(540);
    write64(0x198b0c18, vote_policy | 0x80);
    if (read64(0x198b0050) != applied) fail(541);
    // State 23 has a lower index but a higher effective frequency than state 30.
    const uint64_t voted = (UINT64_C(23) << 45) | (UINT64_C(23) << 40) |
        (UINT64_C(23) << 35) | (UINT64_C(23) << 8) | 23;
    write32(0x198b0000, 1);
    if (read64(0x198b0050) != voted) fail(542);
    wait_progress(handover_boots, read32(0x14800004), 543);
    write32(0x198b1d18, 0);
    if (read64(0x198b0050) != applied) fail(544);
    write32(0x198b1d10, (23u << 1) | 1);
    write32(0x198b1d14, 0);
    if (read64(0x198b1d10) != ((23u << 1) | 1) ||
        read64(0x198b0050) != voted) fail(545);
    // A locked vote preserves the applied effects and exposes the lock error.
    write32(0x198b0000, 0x1000000);
    write32(0x198b1d10, (30u << 1) | 1);
    if (read64(0x198b0050) != (voted | (UINT64_C(1) << 33) | (UINT64_C(1) << 24)))
        fail(546);
    write32(0x198b0000, 0x4000000);
    write32(0x198b1d10, (30u << 1) | 1);
    if (read64(0x198b0050) != applied) fail(547);
    write32(0x198b1d10, 0);
    if (read64(0x198b0050) != applied) fail(548);
    write64(0x198b0c18, vote_policy);
    write32(0x198b0000, 0x1e0001);
    wait_progress(handover_boots, read32(0x14800004), 549);
    write32(0x19a30030, 1);
    write64(0x199a2030, software_clock & ~UINT64_C(8));
    wait_bits(0x19a30000, 0x80000000, 0x80000000, 526);
    write64(0x199a2030, software_clock);
    wait_progress(handover_boots, read32(0x14800004), 525);
    write64(0x198b0c10, boot_memory);
    write64(0x198b0008, boot_dvfs);
    // Gasket pulses inhibit acknowledgement without resetting the running core.
    const uint32_t gasket_boots = read32(0x14800000);
    for (unsigned lane = 0; lane < 2; ++lane) {
        const uintptr_t gasket = lane ? 0x18260008 : 0x18060008;
        write64(gasket, 5);
        if (read64(gasket) != 5) fail(67);
        delay();
        wait_progress(gasket_boots, read32(0x14800004), 68);
        write64(gasket, 1);
        if (read64(gasket) != 1) fail(69);
        write64(gasket, 0);
        if (read32(gasket) != 0) fail(70);
        write64(gasket, 4);
        wait64(gasket, 7, 6, 71);
        write32(gasket, 0);
        if (read64(gasket) != 0) fail(72);
    }
    // Loss of the selected PLL must hold reset until its lock event restores the clock.
    write32(0x19a30030, 0);
    check_held(64);
    previous = read32(0x14800000);
    write32(0x19a30030, 1);
    wait_restart(previous, 65);
    write64(base + 0x260, control | (UINT64_C(1) << 16));
    check_held(48);
    // A held core does not withdraw the lane publication. The cluster stays up.
    if ((read32(0x1887c000) & 1) != 1) fail(49);
    previous = read32(0x14800000);
    write64(base + 0x260, control);
    wait_restart(previous, 50);
    wait_bits(0x1887c000, 1, 1, 51);
    write64(base + 0x260, control & ~UINT64_C(1));
    // Wait for supply-off completion before measuring the held CPU counters.
    wait64(base + 0x270, 1, 0, 53);
    wait64(base + 0x2b0, (UINT64_C(31) << 56) | 0xff00, 0, 54);
    check_held(52);
    if (read64(0x198b0c40) != 0) fail(516);
    write64(base + 0x260, control);
    write64(base + 0x278, 0x100);
    wait64(base + 0x2b0, UINT64_C(31) << 56, UINT64_C(5) << 56, 55);
    previous = read32(0x14800000);
    write64(base + 0x278, 1);
    wait_restart(previous, 56);
}
static void lane_one_probe(void) {
    const uintptr_t base = 0x1a880000;
    const uint32_t boot_count = read32(0x14800000);
    write32(0x18260008, 4);
    wait_bits(0x18260008, 6, 6, 89);
    const unsigned routes[] = {0x28, 0x38, 0x40};
    for (unsigned i = 0; i < 3; ++i)
        write32(0x1a8f0000 + routes[i], read32(0x198f0000 + routes[i]));
    // The selected runtime programs the second cluster without any timing word.
    // SM8850 run 547 writes only the eighteen range words, the three routing words,
    // +0x8, the bridge word, the lane tag, and the two requests to lane one.
    // Lane one keeps the memory-access configuration that the ROM programmed.
    const unsigned configuration[] = {
        0x348, 0x350, 0x358, 0x360, 0x368, 0x370, 0x378, 0x380, 0x388,
        0x390, 0x398, 0x3a0, 0x3a8, 0x3b0, 0x3b8, 0x3c0, 0x3c8, 0x3d0};
    for (unsigned i = 0; i < sizeof(configuration) / sizeof(configuration[0]); ++i)
        write64(base + configuration[i], read64(0x19880000 + configuration[i]));
    write64(base + 0x260, 0xff0101);
    write64(base + 0x328, 0x334100);
    write64(base + 0x258, 0x12);
    write64(base + 0x10, 1);
    write64(base + 0x310, 1);
    write64(0x1a9a2030, 0x2000003);
    write64(0x1a8b0008, 0x101);
    write64(0x1a8b0c10, 0x401);
    for (unsigned i = 0; i < 3; ++i)
        write64(0x1a8b0c28 + i * 8, read64(0x198b0c28 + i * 8));
    write64(0x1a8300f8, UINT64_C(1) << 52);
    write64(base + 0x278, 0x100);
    wait64(base + 0x2b0, UINT64_C(31) << 56, UINT64_C(5) << 56, 90);
    write64(base + 0x278, 3);
    wait64(base + 0x2b0, 0xff00, 0x300, 91);
    for (unsigned core = 0; core < 2; ++core) {
        const unsigned index = 6 + core;
        check_core_held(index, 92);
        write64(0x1a032150 + core * 0x100000,
            (UINT64_C(1) << 50) | (UINT64_C(1) << 55) | (UINT64_C(1) << 57));
        const uint32_t previous = read32(0x14800000 + index * 16);
        write64(base + 0x260, UINT64_C(0xff0101) & ~(UINT64_C(1) << (16 + core)));
        wait_core_restart(index, previous, 93);
        if (read32(0x14800008 + index * 16) != 1) fail(104);
        write64(base + 0x260, 0xff0101);
        check_core_held(index, 94);
        wait_progress(boot_count, read32(0x14800004), 95);
    }
    // A short lane reset request must assert every consumer before acknowledgement.
    const uint32_t previous6 = read32(0x14800060);
    const uint32_t previous7 = read32(0x14800070);
    write64(base + 0x260, 0xfc0101);
    wait_core_restart(6, previous6, 96);
    wait_core_restart(7, previous7, 97);
    write32(0x1887c024, 8);
    write32(0x1887c024, 0);
    write32(0x1887c024, 1);
    check_core_held(6, 98);
    check_core_held(7, 99);
    wait_progress(boot_count, read32(0x14800004), 100);
}


static void cluster_configuration_probe(void) {
    const uint32_t boots = read32(0x14800000);
    const unsigned registers[] = {0x630, 0x638, 0x640, 0x648, 0x650, 0x658, 0x660, 0x678, 0x680};
    for (unsigned lane = 0; lane < 2; ++lane) {
        const uintptr_t delta = lane * 0x01000000;
        const uintptr_t base = 0x19898000 + delta;
        const uintptr_t table = 0x199e0000 + delta;
        const uintptr_t aux = 0x19aa0000 + delta;
        // Bits 15:8 identify the cluster for PDP. QMX disable bit 54 stays clear.
        if (read64(base + 0x5d0) != (uint64_t)lane << 8 || read32(base + 0x5d4)) fail(105);
        // PDP mailbox setup writes +0x268. The bank retains it for CPUCP snapshots.
        write64(base + 0x268, 1);
        if (read64(base + 0x268) != 1) fail(105);
        for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
            const uint64_t value = (UINT64_C(0x80000001) << 32) | (lane << 8) | i;
            write64(base + registers[i], value);
            if (read64(base + registers[i]) != value) fail(106);
            write32(base + registers[i], 0x12340000 | lane);
            if (read64(base + registers[i]) !=
                ((value & UINT64_C(0xffffffff00000000)) | 0x12340000 | lane)) fail(107);
            write32(base + registers[i] + 4, 0x56780000 | i);
            if (read64(base + registers[i]) !=
                ((UINT64_C(0x56780000) | i) << 32 | 0x12340000 | lane)) fail(108);
        }
        // Both OEM table sizes fit the recovered 64-entry primary capacity.
        for (unsigned i = 0; i < 64; ++i) {
            const uint64_t value = (UINT64_C(0x80000000) << 32) | (lane << 16) | i;
            write64(table + 0x400 + i * 8, value);
            if (read64(table + 0x400 + i * 8) != value) fail(109);
        }
        for (unsigned i = 0; i < 3; ++i) {
            write64(table + 0x1400 + i * 8, UINT64_C(0x10000000) | i);
            write32(table + 0x1404 + i * 8, 0x89abcdef);
            if (read64(table + 0x1400 + i * 8) !=
                (UINT64_C(0x89abcdef10000000) | i)) fail(110);
        }
        // Programmed records do not create low-power entries or residence time.
        for (unsigned i = 0; i < 64; ++i)
            if (read64(table + i * 8) || (i < 3 && read64(table + 0x1000 + i * 8))) fail(554);
        // SM8845's third set has four entries and three bitmaps plus a count.
        for (unsigned i = 0; i < 4; ++i) {
            write64(aux + 0x900 + i * 8, UINT64_C(0x10000000) | i);
            write64(aux + 0xa10 + i * 8, UINT64_C(1) << (48 + i));
            write32(aux + 0xa10 + i * 8, lane + i);
            if (read64(aux + 0x900 + i * 8) != (UINT64_C(0x10000000) | i) ||
                read64(aux + 0xa10 + i * 8) != ((UINT64_C(1) << (48 + i)) | (lane + i)))
                fail(111);
        }
        // The recovered initializer sets enable and clears the routing field.
        write64(base + 0x660, read64(base + 0x660) | 1);
        write64(base + 0x660, read64(base + 0x660) & ~UINT64_C(0x7e));
        if ((read64(base + 0x660) & 0x7f) != 1) fail(112);
    }
    // Configuration alone must not reset the running core or release held cores.
    wait_progress(boots, read32(0x14800004), 113);
    for (unsigned index = 1; index < 8; ++index)
        if (read32(0x14800000 + index * 16)) fail(114);
}

static void runtime_preparation_probe(void) {
    const uintptr_t lane0 = 0x19880000;
    const uintptr_t lane1 = 0x1a880000;
    const unsigned offsets[] = {
        0x2c0, 0x3f8, 0x4e8, 0x288, 0x3f8, 0x288, 0x4e8, 0x4f8, 0x3f0, 0x3f0};
    const uint64_t values[] = {
        UINT64_C(0x2), UINT64_C(0x2), UINT64_C(0x2), UINT64_C(0x1000000000000),
        UINT64_C(0x1000000000002), UINT64_C(0x2000000000000), UINT64_C(0x6),
        UINT64_C(0x0020000000000000), UINT64_C(0x100000000), UINT64_C(0x200000003)};
    const uint32_t boots = read32(0x14800000);
    uint32_t heartbeat = read32(0x14800004);
    check_core_held(1, 115);
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        write64(lane0 + offsets[i], values[i]);
        write64(lane1 + offsets[i], values[i]);
        if (read64(lane0 + offsets[i]) != values[i] ||
            read64(lane1 + offsets[i]) != values[i]) fail(116);
        check_core_held(1, 120 + i);
        check_core_held(6, 130 + i);
        check_core_held(7, 140 + i);
    }
    const uint64_t midr = UINT64_C(0x000f0510);
    if (read64(0x19030d00) != midr ||
        read64(0x19130d00) != midr || read64(0x1a030d00) != midr ||
        read64(0x1a130d00) != midr) fail(118);
    check_core_held(1, 119);
    // Both OEM images clear three words, mask +0x30 to its upper bits, then set
    // bits 1 and 23 of +0x20. These writes retain configuration only.
    const uintptr_t init_bank = 0x17960020;
    write32(init_bank, 0);
    write32(init_bank + 4, 0);
    write32(init_bank + 8, 0);
    write32(init_bank + 16, read32(init_bank + 16) & 0xfffc0000);
    write32(init_bank, read32(init_bank) | 2);
    write32(init_bank, read32(init_bank) | 0x06800000);
    if (read32(init_bank) != 0x06800002 || read32(init_bank + 16) != 0) fail(150);
    check_core_held(1, 151);
    wait_progress(boots, heartbeat, 124);
}

static void runtime_control_probe(void) {
    // The ROM sets control bit 28 after its domain is initialized. Writing the control
    // word earlier transfers reset ownership while the domain is still cold.
    const uintptr_t lanes[] = {0x19880000, 0x1a880000};
    const uint32_t boots = read32(0x14800000);
    for (unsigned lane = 0; lane < 2; ++lane) {
        const uint64_t control = read64(lanes[lane] + 0x260);
        write64(lanes[lane] + 0x260, control | (UINT64_C(1) << 28));
        if (read64(lanes[lane] + 0x260) != (control | (UINT64_C(1) << 28)))
            fail(125 + lane);
    }
    check_core_held(1, 127);
    wait_progress(boots, read32(0x14800004), 128);
}

void domain_probe(void) {
    wait_restart(0, 1);
    // Only the actual boot core runs before CPUCP owns lane-zero reset.
    for (unsigned index = 1; index < 8; ++index) {
        check_core_held(index, 87);
        if (read32(0x14800000 + index * 16)) fail(88);
    }
    runtime_preparation_probe();
    cluster_configuration_probe();
    for (unsigned i = 0; i < 5; ++i) write32(0x17960020 + i * 4, 0);
    tags(0, 1);
    prepare(0);
    write32(0x18060008, 4);
    write32(0x17c020b4, 1);
    wait_bits(0x17c020a4, 3, 2, 2);
    no_ack(0, 3); // No source and no branches.
    check_held(4);

    // Program an actual voted PLL and select it at the domain root.
    write32(0x100010, 31);
    write32(0x100018, 0x02000001);
    write32(0x100000, 4);
    write32(0x152020, 1);
    write32(0x17c02008, 0x100);
    write32(0x17c02004, 3);
    wait_bits(0x17c02004, 1, 0, 5);
    no_ack(0, 6); // A live root without branches must not initialize the domain.
    wait_bits(0x17c020a4, 3, 2, 7);
    write32(0x17c0402c, 1);
    write32(0x17c04030, 1);
    no_ack(0, 8); // The third branch is still missing.
    check_held(9);
    const uint32_t held_boots = read32(0x14800000);
    write32(0x17c04038, 1);
    wait_bits(0x17c020a4, 3, 1, 10);
    wait_restart(held_boots, 11); // Real reset release restarts the consumer at its reset vector.
    no_ack(0, 12); // All clocks exist, but one lane tag is wrong.

    write32(0x18000630, 0);
    wait_bits(0x1887c020, 2, 2, 13);
    write32(0x18060008, 4);
    wait_bits(0x18060008, 6, 6, 14);

    // PLL vote loss must revoke completion and hold the CPU without guest status polling.
    write32(0x152020, 0);
    check_held(15);
    no_ack(0, 16);
    wait_bits(0x17c020a4, 3, 2, 17);
    uint32_t previous = read32(0x14800000);
    write32(0x152020, 1);
    wait_restart(previous, 18); // PLL lock-deadline notification must release the CPU.
    wait_bits(0x1887c020, 2, 2, 19);
    wait_bits(0x18060008, 6, 0, 20); // Reset discarded the downstream request/completion.
    write32(0x18060008, 4);
    wait_bits(0x18060008, 6, 6, 21);

    // A later branch disable must also invalidate a live domain.
    write32(0x17c04038, 0);
    check_held(22);
    no_ack(0, 23);
    previous = read32(0x14800000);
    write32(0x17c04038, 1);
    wait_restart(previous, 24);
    wait_bits(0x1887c020, 2, 2, 25);

    // An explicit reset pulse must reach the architectural CPU, not retained register RAM.
    write32(0x1887c020, 8);
    check_held(26);
    no_ack(0, 27);
    previous = read32(0x14800000);
    write32(0x1887c020, 0);
    write32(0x1887c020, 1);
    wait_restart(previous, 28);
    wait_bits(0x1887c020, 2, 2, 29);
    write32(0x18060008, 4);
    wait_bits(0x18060008, 6, 6, 30);

    // A reset request must survive even when the producer clears it before delivery.
    previous = read32(0x14800000);
    write32(0x1887c020, 8);
    write32(0x1887c020, 0);
    write32(0x1887c020, 1);
    wait_restart(previous, 73);
    wait_bits(0x1887c020, 2, 2, 74);
    wait_bits(0x18060008, 6, 0, 75);
    write32(0x18060008, 4);
    wait_bits(0x18060008, 6, 6, 76);
    write32(0x18060008, 0);
    wait_bits(0x18060008, 2, 0, 31);
    write32(0x1887c020, 0);
    write32(0x17c020b4, 0);
    wait_bits(0x17c020a4, 3, 0, 32);

    // Lane-one domain initialization must keep both cold architectural CPUs held.
    tags(1, 0);
    write32(0x17c04034, 1);
    write32(0x17c0403c, 1);
    write32(0x17c020b4, 2);
    wait_bits(0x17c020a4, 12, 4, 33);
    prepare(1);
    wait_bits(0x1887c024, 2, 2, 34);
    write32(0x18260008, 4);
    wait_bits(0x18260008, 6, 6, 35);
    write32(0x18260008, 0);
    wait_bits(0x18260008, 2, 0, 36);
    write32(0x1887c024, 0);
    write32(0x17c020b4, 0);
    wait_bits(0x17c020a4, 15, 0, 37);
    outer_probe();
    lane_one_probe();
    runtime_control_probe();
    for (unsigned lane = 0; lane < 2; ++lane) {
        const uintptr_t base = lane ? 0x1a850000 : 0x19850000;
        const unsigned count = 6;
        const unsigned offsets[] = {0x08, 0x10, 0x18, 0x20};
        for (unsigned core = 0; core < count; ++core)
            for (unsigned i = 0; i < 4; ++i) {
                const uintptr_t address = base + core * 0x40 + offsets[i];
                const uint64_t value = (UINT64_C(0x81850000) << 32) | address;
                write64(address, value);
                if (read64(address) != value) fail(114);
            }
        const unsigned special_offsets[] = {0x168, 0x178, 0x180, 0x188};
        for (unsigned i = 0; i < 4; ++i) {
            const unsigned offset = special_offsets[i];
            const uint64_t value = (UINT64_C(0x19850000) << 32) | base | offset;
            write64(base + offset, value);
            if (read64(base + offset) != value) fail(115);
        }
    }
    write64(0x19850a10, UINT64_C(0x5a5aa5a5));
    if (read64(0x19850a10) != UINT64_C(0x5a5aa5a5) ||
        read64(0x19850a18) != 0)
        fail(117);
    for (unsigned offset = 0x48; offset <= 0x80; offset += 8) {
        const uint64_t value = UINT64_C(0x1122334455660000) | offset;
        write64(0x198f0000 + offset, value);
        if (read64(0x198f0000 + offset) != value) fail(112);
        if (read64(0x1a8f0000 + offset) != 0) fail(113);
    }
    write64(0x19880420, UINT64_C(0xe100000000000001));
    write64(0x19880570, 2);
    if (read64(0x19880420) != UINT64_C(0xe100000000000001) ||
        read64(0x19880570) != 2)
        fail(118);
    write64(0x19830150, UINT64_C(0x22003ff));
    write64(0x19880578, 0xff);
    write64(0x19880600, 0xe);
    if (read64(0x19830150) != UINT64_C(0x22003ff) ||
        read64(0x19880578) != 0xff || read64(0x19880600) != 0xe)
        fail(119);
    for (unsigned lane = 0; lane < 2; ++lane) {
        const uintptr_t base = 0x18080000 + lane * 0x200000;
        for (unsigned index = 1; index <= (lane ? 4 : 8); ++index) {
            const uint64_t value = UINT64_C(0x8000000100000000) | (lane << 16) | index;
            write64(base + index * 8, value);
            if (read64(base + index * 8) != value) fail(123);
        }
    }
    if (read64(0x18080008) != UINT64_C(0x8000000100000001) ||
        read64(0x18280008) != UINT64_C(0x8000000100010001)) fail(124);
    for (unsigned lane = 0; lane < 2; ++lane) {
        for (unsigned core = 0; core < (lane ? 2 : 6); ++core) {
            const uintptr_t base = 0x19032000 + lane * 0x1000000 + core * 0x100000;
            for (unsigned offset = 0xb8; offset <= 0x130; offset += 8) {
                const uint64_t value = (UINT64_C(0xa5a55a5a) << 32) | (base + offset);
                write64(base + offset, value);
                if (read64(base + offset) != value) fail(127);
            }
            for (unsigned row = 0; row < 3; ++row) {
                for (unsigned word = 8; word <= 0x20; word += 8) {
                    const uintptr_t address = base + 0xe000 + row * 0x40 + word;
                    const uint64_t value = (UINT64_C(0x5a5aa5a5) << 32) | address;
                    write64(address, value);
                    if (read64(address) != value) fail(129);
                }
            }
        }
    }
    if (read64(0x190320b8) != UINT64_C(0xa5a55a5a190320b8) ||
        read64(0x1a0320b8) != UINT64_C(0xa5a55a5a1a0320b8)) fail(128);
    const uint32_t pdp_request_boots = read32(0x14800000);
    for (unsigned lane = 0; lane < 2; ++lane) {
        const unsigned offset = lane * 4;
        if (read32(0x1887c180 + offset) != 0 || read32(0x18878080 + offset) != 0) fail(125);
        write32(0x1887c180 + offset, 0);
        write32(0x18878080 + offset, 0);
        write32(0x18878040 + offset, lane);
        write32(0x18871200 + offset, 1);
        write32(0x18871300 + offset, lane + 1);
    }
    if (read32(0x18878040) != 0 || read32(0x18878044) != 1 ||
        read32(0x18871300) != 1 || read32(0x18871304) != 2) fail(126);
    write64(0x19880258, read64(0x19880258) | 0x40);
    if (!(read64(0x19880258) & 0x40)) fail(120);
    check_core_held(1, 121);
    wait_progress(pdp_request_boots, read32(0x14800004), 122);
    if (read64(0x198f0088) != 0 || read64(0x198f0090) != 0 ||
        read64(0x1a8f0088) != 0 || read64(0x1a8f0090) != 0)
        fail(116);
    // The PDP stays held until the image-complete bit releases it.
    // The released M7 starts from the cold table at 0x20000000.
    wait_pdp_boot(0, 300);
    if (read32(pdp_boot) != 1) fail(301);
    if (read32(pdp_entry) != 0x20000100) fail(302);
    if (read32(pdp_stack) != 0x2001e000) fail(303);
    wait_pdp_heartbeat(read32(pdp_heartbeat), 304);
    // A publication selects the table for the next reset only.
    // The running M7 keeps its boot count, entry, and stack.
    write32(0x198802c8, 0x2000407f);
    if (read32(0x198802c8) != 0x20004000) fail(305);
    settle_model();
    wait_pdp_heartbeat(read32(pdp_heartbeat), 306);
    if (read32(pdp_boot) != 1 || read32(pdp_entry) != 0x20000100 ||
        read32(pdp_stack) != 0x2001e000) fail(307);
    // A cleared image-complete bit holds the same M7.
    write64(0x19880258, read64(0x19880258) & ~UINT64_C(0x40));
    check_pdp_held(308);
    if (read32(pdp_boot) != 1 || read32(pdp_entry) != 0x20000100) fail(309);
    // The new release loads the published warm table in the same M7.
    write64(0x19880258, read64(0x19880258) | 0x40);
    wait_pdp_boot(1, 310);
    if (read32(pdp_boot) != 2) fail(311);
    if (read32(pdp_entry) != 0x20000200) fail(312);
    if (read32(pdp_stack) != 0x2001f000) fail(313);
    // Leave the PDP held so it cannot change the remaining fixture state.
    write64(0x19880258, read64(0x19880258) & ~UINT64_C(0x40));
    settle_model();
    write32(0x600d0000, 57);
    fail(58);
}

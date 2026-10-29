// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "training_context.h"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <string_view>

namespace qcom_vp {
namespace training_pxi_detail {
// Register programs follow the SHRM helpers. Pattern bytes remain guest-owned.
// run_normalized() selects a virtual transfer count, not a recovered silicon range.
template<class Context> class Program {
    Context& c;
public:
    explicit Program(Context& context) : c(context) {}
    bool put(unsigned offset, uint32_t value) { return c.write32(0x34a00000ULL + offset, value); }
    bool masked(unsigned offset, uint32_t keep, uint32_t value = 0) {
        uint32_t old;
        return c.read32(0x34a00000ULL + offset, old) && put(offset, (old & keep) | value);
    }
    bool list(unsigned offset, std::initializer_list<uint32_t> words) {
        for (auto value : words) {
            if (!put(offset, value)) return false;
            offset += 4;
        }
        return true;
    }
    bool patterns() {
        for (unsigned i = 0; i < 32; ++i) {
            uint32_t first, second;
            if (!c.read32(c.table(0x4009c8 + i * 4), first) ||
                !c.read32(c.table(0x400a48 + i * 4), second) ||
                !put(0xe780 + i * 4, first) || !put(0xeb80 + i * 4, first) ||
                !put(0xef80 + i * 4, second) || !put(0xed80 + i * 4, second)) return false;
        }
        return true;
    }
    bool common() {
        if (!put(0x80b0, 0x111) ||
            !masked(0x801c, 0x70132, 1) || !masked(0x801c, 0x70131, 2) ||
            !masked(0x801c, 0x70113) || !masked(0x801c, 0x70123) ||
            !masked(0x801c, 0x70033, 0x100) || !masked(0x801c, 0x60133) ||
            !masked(0x801c, 0x30133) || !masked(0x801c, 0x50133) ||
            !list(0xec80, {8, 1}) || !masked(0x80a0, 0x31101, 0x10)) return false;
        for (unsigned offset : {0x8100U, 0x8104U}) {
            if (!masked(offset, 0x1f006d11, 0x60000) ||
                !masked(offset, 0x1f0f6c11, offset == 0x8104 ? 0x100 : 0) ||
                !masked(offset, 0x1f0f6d01) || !masked(offset, 0x1f0f6d10) ||
                !masked(offset, 0x1e0f6d11, 0x1000000) ||
                !masked(offset, offset == 0x8100 ? 0x1d0f6d11 : 0x1b0f6d11,
                        offset == 0x8100 ? 0x2000000 : 0x4000000)) return false;
        }
        if (!put(0x8110, 4) || !put(0x8114, 4) || !masked(0x8018, 0x9ff90f0f) ||
            !masked(0x8010, 0x3f3f3f3f, 0x80000000) ||
            !masked(0x80a4, 0x0f0f0f00, 3) || !masked(0x80a4, 0x0f000f0f, 0x30000) ||
            !masked(0x80a4, 0x0f0f000f, 0x300) || !masked(0x80a4, 0x000f0f0f, 0x3000000) ||
            !patterns()) return false;
        for (unsigned i = 0; i < 5; ++i)
            if (!put(0xe080 + 4 * i, 0x0c000000 + i * 0x10)) return false;
        if (!put(0xe094, 0x04000000) || !list(0xe180, {0x340, 0, 0x40, 0x80, 0xc0}) ||
            !put(0xe480, 0x04000000)) return false;
        for (unsigned i = 0; i < 13; ++i)
            if (!put(0xe484 + i * 4, 0x0c000000 + i * 0x10)) return false;
        if (!list(0xe580, {0, 0x80, 0x10100, 0x10180, 0x400, 0x480, 0x10500, 0x10580,
                            0x7a0a00, 0x7a0a40, 0x790b00, 0x790b40, 0x7e0800}) ||
            !put(0xe280, 0x04000000)) return false;
        for (unsigned i = 1; i < 10; ++i)
            if (!put(0xe280 + i * 4, 0x0c000000 + i * 0x10 + (i == 8 || i == 9))) return false;
        if (!list(0xe2a8, {0x0c0000a0, 0x0c0000b0, 0x0c1000c1, 0x0c1000d9,
                            0x0c0000e0, 0x0c0000f0, 0x0c000100, 0x0c000110,
                            0x0c000120, 0x0c000130, 0x0c000140, 0x0c000150,
                            0x041000c1, 0x041000d9, 0x0c000089, 0x0c000099}) ||
            !list(0xe2f0, {0x040001d0, 0x0c0001d0, 0x0c0001e0}) ||
            !list(0xe380, {0, 0x4000, 0x4087, 0x4003, 0x4287, 0x4003, 0x4487, 0x4003,
                            0x4687, 0x4003, 0x401c, 0x402c, 0x4006, 0x4001,
                            0x78fe, 0x7afe, 0x7cfe, 0x7efe, 0x78f9, 0x7af9, 0x7cf9, 0x7ef9}) ||
            !list(0xea00, {0x40000000, 0x002000ff, 0})) return false;
        for (unsigned i = 3; i <= 28; ++i) {
            const unsigned pointer = ((i - 1) % 4) + 1;
            if (!put(0xea00 + i * 4, (pointer << 25) | 0x00200000)) return false;
        }
        if (!list(0xea80, {0x0ca00000, 0x0c000000, 0x0c002020, 0x0c004040,
                            0x0c006060, 0x0c008080, 0x0c00a0a0, 0x0c00c0c0, 0x0c00e0e0}) ||
            !list(0xe600, {0x00117000, 0x00107002, 0x00114000, 0x022000ff, 0x00100000, 0})) return false;
        for (unsigned i = 6; i <= 30; ++i) {
            const unsigned pointer = ((i - 6 + 3) % 4) + 2;
            if (!put(0xe600 + i * 4, (pointer << 25) | 0x00200000)) return false;
        }
        return put(0xe67c, 0) && list(0xe680, {0x04000000, 0x0ca00000, 0x0c000000,
            0x0c002020, 0x0c004040, 0x0c006060, 0x0c008080, 0x0c00a0a0, 0x0c00c0c0, 0x0c00e0e0});
    }
    bool payloads(bool write) {
        if (!list(0xe000, {0x00108000, 0x02100002, 0x0a10b000, 0x04107002,
                            0x0410c000, 0x02100000, 0x021010ff}) ||
            !list(0xe040, {0x00108000, 0x06100002, 0x0a10b000, 0x08107002,
                            0x0810c000, 0x06100000, 0x061010ff}) ||
            !list(0xe400, {0x00108000, 0x0010e000, 0x00107002, 0x0010c000, 0x12100000, 0x021010ff}) ||
            !list(0xe440, {0x00108000, 0x0010e000, 0x00107002, 0x0010c000, 0x14100000, 0x041010ff}) ||
            !list(0xe200, {0x00108000, 0x02100002, 0x0010b000, 0x04206000, 0x08206000,
                            0x0c206000, 0x1020b000})) return false;
        if (write) {
            return list(0xe01c, {0x0a102000, 0x02100000, 0x021010ff, 0, 0x00108000,
                                 0x02100002, 0, 0x04100000, 0}) &&
                list(0xe05c, {0x0a102000, 0x06100000, 0x061010ff, 0}) &&
                list(0xe418, {0x00102000, 0x16100000, 0x061010ff, 0}) &&
                list(0xe458, {0x00102000, 0x18100000, 0x081010ff, 0}) &&
                list(0xe21c, {0x181010ff, 0x2c100000, 0x00100000, 0x2e100000, 0x1a1010fb,
                    0x2e100000, 0x24101000, 0x26101000, 0x28101000, 0x2a101000, 0}) &&
                list(0xe26c, {0x38108000, 0x3a100002, 0, 0x3c200000, 0});
        }
        return put(0xe01c, 0) && put(0xe05c, 0) && put(0xe418, 0) && put(0xe458, 0) &&
            list(0xe21c, {0x181010fb, 0x2c100000, 0x1c101000, 0x1e101000, 0x20101000, 0x22101000, 0});
    }
    bool configure(bool write) {
        if (!common() || !payloads(write) || !c.fill(c.local(0x403a9c), 8)) return false;
        // Native PRBS initialization is register state, not a vendor pattern table.
        for (unsigned base : {0xeb00U, 0xe700U}) {
            if (base == 0xe700 && !write) continue;
            if (!put(base, 0x5106d633) || !put(base + 4, 0xe85334e1) ||
                !put(base + 8, 0x93b170e4) || !put(base + 12, 0x26f97cd3)) return false;
        }
        uint8_t frequency;
        if (!c.read8(c.local(0x403a98), frequency) ||
            !put(0x80b0, 0x221) || !list(0xe308, {0x84000000, 0x84000000}) ||
            !put(0x8024, 0x10001000)) return false;
        if (frequency) {
            const auto row = c.local(0x402e20 + frequency * 22);
            uint8_t first, second, p, b6, b13, b15, b17;
            uint32_t a, b, t;
            if (!c.read8(c.local(0x402839), first) || !c.read8(c.local(0x40283a), second) ||
                !c.read8(row + ((first == 1 || second == 1) ? 5 : 4), p) ||
                !c.read8(row + 6, b6) || !c.read8(row + 13, b13) ||
                !c.read8(row + 15, b15) || !c.read8(row + 17, b17) ||
                !c.read32(0x34a04744, a) || !c.read32(0x34a0474c, b) ||
                !c.read32(0x34a04b44, t)) return false;
            const uint32_t latency = (t & 31) - 1 - (a & 255) + ((a >> 8) & 255) + p;
            const uint32_t value = latency | ((uint32_t{b15} + b17) << 16) |
                ((uint32_t{b13} + b17) << 24) | (((uint32_t{b6} - (b & 255)) << 8) + (b & 0xff00));
            if (!masked(0x8010, 0x80000000, value & 0x3f3f3f3f)) return false;
        }
        return timing(write) && put(0xea04, 0x002000ff) && put(0xea08, 0) &&
            list(0xe384, {0x8000, 0x8087, 0x8003, 0x8287, 0x8003, 0x8487, 0x8003,
                0x8687, 0x8003, 0x801c, 0x802c, 0x8006, 0x8001, 0xb8fe, 0xbafe, 0xbcfe,
                0xbefe, 0xb8f9, 0xbaf9, 0xbcf9, 0xbef9}) &&
            c.write32(c.local(0x403a9c), frequency) && c.write8(c.local(0x403aa0), 1) &&
            c.write8(c.local(0x403aa1), 1) && c.write8(c.local(0x403aa2), write ? 2 : 1) &&
            c.write8(c.local(0x403aa3), 0);
    }
    bool timing(bool write) {
        uint8_t frequency, first, second, read_delay, raw_write, kind;
        uint32_t special_id, special_limit;
        if (!c.read8(c.local(0x403a98), frequency) ||
            !c.read8(c.local(0x402839), first) || !c.read8(c.local(0x40283a), second) ||
            !c.read8(c.local(0x402e20 + frequency * 22 + ((first == 1 || second == 1) ? 5 : 4)), read_delay) ||
            !c.read8(c.local(0x402e26 + frequency * 22), raw_write) ||
            !c.read8(c.local(0x4031f6 + frequency), kind) ||
            !c.read32(c.local(0x402c88), special_id) || !c.read32(c.local(0x402c8c), special_limit)) return false;
        const unsigned r = std::min<unsigned>(read_delay, 25);
        const unsigned w = std::min<unsigned>(raw_write, 25);
        const unsigned clocks = std::max<unsigned>(raw_write, 3);
        const unsigned d = kind == 3 ? 2 : 4;
        const unsigned extra = special_id == 0xa012 && special_limit <= 0x1ff ? r : 0;
        const auto q = [](unsigned value) { return ((value - 1) & 63) << 12; };
        if (write) {
            if (!put(0xe224, 0x00100000 | (((clocks - 3) & 63) << 12)) ||
                !put(0xe21c, 0x181000ff + q(w + d)) || !put(0xe22c, 0x1a1000fb + q(d + extra)) ||
                !put(0xe01c, 0x0a100000 | q(clocks)) || !put(0xe05c, 0x0a100000 | q(clocks)) ||
                !put(0xe018, 0x021000ff + q(w + d)) || !put(0xe024, 0x021000ff + q(d)) ||
                !put(0xe058, 0x061000ff + q(w + d)) || !put(0xe064, 0x061000ff + q(d + extra)) ||
                !put(0xe418, 0x00100000 | q(clocks)) || !put(0xe458, 0x00100000 | q(clocks)) ||
                !put(0xe414, 0x021000ff + q(w + d)) || !put(0xe420, 0x061000ff + q(d)) ||
                !put(0xe454, 0x041000ff + q(w + d)) || !put(0xe460, 0x081000ff + q(d + extra))) return false;
            for (unsigned i = 0; i < 4; ++i)
                if (!put(0xe234 + i * 4, (0x24100000 + (i << 25)) | q(d))) return false;
        } else {
            if (!put(0xe21c, 0x181000fb + q(d)) || !put(0xe218, 0x1020b000) ||
                !put(0xe220, 0x2c100000) || !put(0xe018, 0x021000ff + q(d + r)) ||
                !put(0xe058, 0x061000ff + q(d + r)) || !put(0xe410, 0x16100000) ||
                !put(0xe450, 0x18100000) || !put(0xe414, 0x061000ff + q(d + r)) ||
                !put(0xe454, 0x081000ff + q(d + r))) return false;
            for (unsigned i = 0; i < 4; ++i)
                if (!put(0xe224 + i * 4, (0x1c100000 + (i << 25)) | q(d))) return false;
        }
        return put(0xe608, 0x00100000 | (((raw_write + 11) & 63) << 12)) &&
            put(0xe60c, 0x020000ff | (d << 20) | (write ? w << 12 : 0)) &&
            put(0xe610, 0x00100000) && put(0xe614, 0);
    }
    // Virtual policy: repeat guest pattern word zero for 256 four-byte transfers.
    // Each selected x8 lane receives exactly 1KiB at per-lane offset512.
    // Readback uses the same memory socket and accumulates real XOR differences.
    bool run_normalized(bool write_first = true) {
        if (!c.channels || (c.channels & ~15U) || !c.ranks || (c.ranks & ~3U)) return false;
        constexpr std::array<unsigned, 4> bases{0x31a00000, 0x32a00000, 0x31e00000, 0x32e00000};
        constexpr std::array<unsigned, 12> offsets{
            0xe600, 0xe604, 0xe680, 0xea00, 0xea04, 0xea80,
            0xe308, 0xe30c, 0x8024, 0x8028, 0xeb80, 0xed80};
        for (unsigned pair = 0; pair < 4; ++pair) {
            if (!(c.channels & (1U << pair))) continue;
            const uint64_t base = bases[pair];
            std::array<uint32_t, offsets.size()> retained{};
            for (unsigned i = 0; i < offsets.size(); ++i)
                if (!c.read32(base + offsets[i], retained[i])) return false;
            uint32_t pattern0, pattern1;
            if (!c.read32(base + 0xe780, pattern0) || !c.read32(base + 0xef80, pattern1)) return false;
            bool ok = c.write32(base + 0xe600, 0x001000ff) && c.write32(base + 0xe604, 0) &&
                c.write32(base + 0xe680, 0x0c000000) && c.write32(base + 0xea00, 0x401000ff) &&
                c.write32(base + 0xea04, 0) && c.write32(base + 0xea80, 0x0c000000) &&
                c.write32(base + 0x8024, 0) && c.write32(base + 0x8028, 0) &&
                c.write32(base + 0xeb80, pattern0) && c.write32(base + 0xed80, pattern1);
            for (unsigned rank = 0; ok && rank < 2; ++rank) {
                if (!(c.ranks & (1U << rank))) continue;
                uint32_t idle = 0, beats = 0, errors0 = 0, errors1 = 0;
                const uint32_t rrbc = rank ? 0x84000000 : 0x44000000;
                ok = c.write32(base + 0xe308, rrbc) && c.write32(base + 0xe30c, rrbc) &&
                    c.write32(base + 0x8074, 1);
                if (ok && write_first)
                    ok = c.write32(base + 0x8020, 0x1e) &&
                        c.read32(base + 0x802c, idle) && (idle & 31) == 31 &&
                        c.write32(base + 0x8020, 0x1f0000);
                ok = ok &&
                    c.write32(base + 0x8020, 0x1d) &&
                    c.read32(base + 0x802c, idle) && (idle & 31) == 31 &&
                    c.read32(base + 0x8070, beats) && c.read32(base + 0xfce8, errors0) &&
                    c.read32(base + 0xfcec, errors1) && beats == 512 && !(errors0 | errors1);
                if (!ok) {
                    // Native consumer reports error six for a count or comparison failure.
                    c.write32(c.local(0x4029a0), 6);
                    c.write32(c.local(0x4029a4), c.level);
                    c.write32(c.local(0x4029a8), pair);
                    c.write32(c.local(0x4029ac), rank);
                }
            }
            // Cleanup is unconditional. Do not replace retained native programs.
            bool restored = c.write32(base + 0x8020, 0x1f0000);
            for (unsigned i = 0; i < offsets.size(); ++i)
                if (!c.write32(base + offsets[i], retained[i])) restored = false;
            if (!ok || !restored) return false;
        }
        return true;
    }
    bool training_path(bool enable) {
        if (enable && (!masked(0x6044, 1, 0x30) ||
            !masked(0x6038, 0xffffffff, 0xc0000000) ||
            !masked(0x603c, 0xffffffff, 0xc0000000) || !masked(0x6068, 0xd) ||
            !put(0x6020, 1))) return false;
        uint8_t type;
        if (!c.read8(c.local(0x40405a), type)) return false;
        const auto gate = [this](uint64_t address, uint32_t keep, uint32_t value) {
            uint32_t old;
            return c.read32(address, old) && c.write32(address, (old & keep) | value);
        };
        uint32_t discarded;
        const uint64_t global = type == 3 ? 0x34b86c3c : 0x34bc0064;
        const uint32_t keep = type == 3 ? 0x1ff7ff3f : 0x8980000f;
        bool ok = gate(global, keep, type == 3 ? 0x40 : 0x00400000);
        if (ok && type == 3) ok = c.read32(0x34b866d4, discarded);
        constexpr std::array<unsigned, 4> bases{0x31a00000, 0x32a00000, 0x31e00000, 0x32e00000};
        for (unsigned pair = 0; ok && pair < 4; ++pair)
            if (c.channels & (1U << pair))
                ok = gate(uint64_t{bases[pair]} + 0x1c0010, 0x8980000f, 0x00400000);
        if (ok) ok = c.read32(0x310a8048, discarded) &&
            masked(0x8018, 0x9ff98f00, enable ? (type == 3 ? 15 : 7) : 0);
        // Remove transient publication gates even if a preceding access failed.
        for (unsigned pair = 0; pair < 4; ++pair)
            if ((c.channels & (1U << pair)) &&
                !gate(uint64_t{bases[pair]} + 0x1c0010, 0x8980000f, 0)) ok = false;
        if (!c.read32(0x310a8048, discarded)) ok = false;
        if (!gate(global, keep, 0)) ok = false;
        if (type == 3 && !c.read32(0x34b866d4, discarded)) ok = false;
        if (!enable) {
            if (!masked(0x6044, 1)) ok = false;
            if (!masked(0x6038, 0x3fffffff)) ok = false;
            if (!masked(0x603c, 0x3fffffff)) ok = false;
            if (!masked(0x6068, 0xd, 2)) ok = false;
            if (!put(0x6020, 1)) ok = false;
        }
        uint32_t status;
        return c.read32(0x34a06020, status) && !(status & 1) && ok;
    }
    bool post() {
        if (!put(0x80b0, 0x110)) return false;
        for (unsigned offset : {0x801cU, 0x80a0U, 0x8100U, 0x8104U, 0x8110U, 0x8114U, 0x8010U, 0x80a4U})
            if (!put(offset, 0)) return false;
        if (!masked(0x8018, 0x9ff90f0f, 0x8000)) return false;
        for (unsigned i = 0; i < 32; ++i) if (!put(0xea00 + 4 * i, 0)) return false;
        return true;
    }
};
} // namespace training_pxi_detail

// Context masks select four channel pairs and two ranks. The parent performs
// low-frequency analog initialization first. This helper owns digital effects.
// Completion requires successful MMIO, shared-memory traffic, and comparisons.
template<class Context> TrainingResult training_pxi(Context& c, std::string_view name) {
    training_pxi_detail::Program<Context> program(c);
    bool ok;
    if (name == "PXI_RD_Training_Config") ok = program.configure(false);
    else if (name == "PXI_WR_Training_Config") ok = program.configure(true);
    else if (name == "PXI_Post_Training_Config") ok = program.post();
    else if (name == "RD_Low_Freq_Write" || name == "PHY_SED_RD" || name == "PHY_SED_LP5_WR_Print") {
        ok = program.training_path(true);
        if (ok) ok = program.run_normalized(name == "RD_Low_Freq_Write");
        const bool disabled = program.training_path(false);
        ok = ok && disabled;
    } else return TrainingResult::unhandled;
    return ok ? TrainingResult::complete : TrainingResult::failed;
}
} // namespace qcom_vp

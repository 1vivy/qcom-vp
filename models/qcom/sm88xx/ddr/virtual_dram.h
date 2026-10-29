// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cstdio>
#include "training_calibration.h"
#include "training_pxi.h"

namespace qcom_vp {

// Selected XBL ABI with ideal electrical calibration and real memory effects.
// Save and restore preserve the register state selected by the guest.
// XBL still creates the flash header, geometry conversions, regions, and SMEM.
class QcomVirtualDdr : public sc_core::sc_module {
public:
    tlm_utils::simple_initiator_socket<QcomVirtualDdr> memory_socket{"memory_socket"};
    cci::cci_param<uint64_t> shared_base{"shared_base", 0, "Required selected XBL DDR shared object"};
    cci::cci_param<uint64_t> dram_base{"dram_base", 0, "Required interleaved rank zero base"};
    cci::cci_param<uint64_t> dram_lower_size{"dram_lower_size", 0, "Dense DRAM bytes below the optional APSS aperture gap"};
    cci::cci_param<uint64_t> dram_upper_base{"dram_upper_base", 0, "APSS base after the optional DRAM aperture gap"};
    cci::cci_param<unsigned> rank0_mib{"rank0_mib", 512, "MiB per x8 PXI lane, rank zero"};
    cci::cci_param<unsigned> rank1_mib{"rank1_mib", 0, "MiB per x8 PXI lane, rank one"};
    cci::cci_param<unsigned> rows{"rows", 15, "Address row bits for each populated rank"};
    cci::cci_param<unsigned> columns{"columns", 10, "Address column bits"};
    cci::cci_param<unsigned> banks{"banks", 16, "Banks per x16 controller"};
    cci::cci_param<uint32_t> startup_version_major{"startup_version_major", 0x1d, "Selected SHRM ABI version major"};
    cci::cci_param<uint32_t> startup_version_minor{"startup_version_minor", 0, "Selected SHRM ABI version minor"};
    cci::cci_param<uint32_t> startup_version_patch{"startup_version_patch", 0, "Selected SHRM ABI version patch"};
    cci::cci_param<uint32_t> training_data_offset{"training_data_offset", 0, "Relative relocation of SHRM lookup tables"};

    explicit QcomVirtualDdr(sc_core::sc_module_name name) : sc_module(name) {}

    std::uint64_t clock_period_ps() const { return clock_period_ps_; }

    void reset() {
        phase_ = Phase::reset;
        started_ = false;
        dcb_address_ = aop_address_ = 0;
        current_frequency_ = 0;
        clock_period_ps_ = 800;
    }

    // Called only after the guest releases the authenticated SHRM image.
    // Version metadata varies between images with the same modeled shared ABI.
    bool startup() {
        if (started_) return true;
        const auto base = shared_base.get_value();
        if (phase_ == Phase::failed || base < 0x2800 ||
            base > std::numeric_limits<std::uint32_t>::max() - 0x15800 ||
            !range(base, shared_.size()) ||
            !transfer(base, shared_.data(), shared_.size(), false)) return fail();
        put(shared_.data() + 0xc, startup_version_major.get_value(), 4);
        put(shared_.data() + 0x10, startup_version_minor.get_value(), 4);
        put(shared_.data() + 0x14, startup_version_patch.get_value(), 4);
        std::array<unsigned char, 12> bounds{};
        put(bounds.data(), base - 0x800, 4);
        put(bounds.data() + 4, base + 0x15800, 4);
        put(bounds.data() + 8, 0x16000, 4);
        if (!write_shared(0xc, 12) ||
            !transfer(base - 0x2800, bounds.data(), bounds.size(), true)) return fail();
        started_ = true;
        return true;
    }

    // Failure never changes response. Processor reset clears a failed operation.
    bool execute(uint32_t request, uint32_t& response) {
        if (!started_) return false;
        if (phase_ == Phase::failed || phase_ == Phase::exited) return false;
        const uint32_t command = request & 0xffff0000;
        if (command != 0x05030000 && command != 0x09080000 &&
            command != 0x05060000 && command != 0x05070000 &&
            command != 0x09800000 && command != 0x09410000) return false;
        if ((command != 0x09080000 && command != 0x09800000 && (request & 0xffff)) ||
            (command == 0x09080000 && (request & 0xff00))) return false;
        if (!configuration()) return fail();
        bool ok = false;
        switch (command) {
        case 0x05030000:
            ok = configure_sram_retention();
            if (ok && phase_ == Phase::reset) phase_ = Phase::configured;
            break;
        case 0x09080000:
            ok = (phase_ == Phase::reset || phase_ == Phase::configured ||
                  phase_ == Phase::initialized || phase_ == Phase::trained) &&
                 (request & 0xf) < shared_[0x2c] &&
                 ((request >> 4) & 0xf) < shared_[0x2d] &&
                 ((phase_ == Phase::initialized || phase_ == Phase::trained) || initialize()) &&
                 switch_clock(request & 0xff);
            if (ok && phase_ != Phase::trained) phase_ = Phase::initialized;
            break;
        case 0x05060000:
            if (phase_ == Phase::initialized || phase_ == Phase::trained) {
                phase_ = Phase::training;
                ok = train();
                if (ok) phase_ = Phase::trained;
            }
            break;
        case 0x05070000:
            ok = (phase_ == Phase::initialized || phase_ == Phase::trained) && restore();
            break;
        case 0x09800000:
            ok = (phase_ == Phase::initialized || phase_ == Phase::trained) &&
                 training_callback(request & 0xffff);
            break;
        case 0x09410000:
            ok = phase_ == Phase::trained;
            if (ok) phase_ = Phase::exited;
            break;
        }
        if (!ok) return fail();
        response = (request & 0xfcff0000) | 0xf0;
        return true;
    }

    // Virtual policy: eight x8 lanes behind four x16 controllers use 64-byte stripes.
    // This mapping is not a claim about the physical controller's address hash.
    bool pxi_transfer(unsigned channel, unsigned rank, uint64_t byte_offset,
                      unsigned char* data, unsigned length, bool write) {
        if (channel >= 8 || rank >= 2 || !data || !length) return false;
        const uint64_t capacity = uint64_t{rank ? rank1_mib.get_value() : rank0_mib.get_value()} << 20;
        if (!capacity || byte_offset >= capacity || length > capacity - byte_offset) return false;
        const uint64_t rank_offset = rank ? (uint64_t{rank0_mib.get_value()} << 23) : 0;
        const auto base = dram_base.get_value();
        const auto lower = dram_lower_size.get_value();
        const auto upper = dram_upper_base.get_value();
        const auto maximum = std::numeric_limits<uint64_t>::max();
        if ((lower == 0) != (upper == 0)) return false;
        const uint64_t last = byte_offset + length - 1;
        if (last / 64 > (maximum - channel * 64 - 63) / 512) return false;
        const uint64_t last_stripe = (last / 64) * 512 + channel * 64 + last % 64;
        if (last_stripe > maximum - rank_offset) return false;
        const uint64_t last_dense = rank_offset + last_stripe;
        if (lower && last_dense >= lower) {
            if (last_dense - lower > maximum - upper || lower - 1 > maximum - base) return false;
        } else if (last_dense > maximum - base) return false;
        while (length) {
            const uint64_t dense = rank_offset + (byte_offset / 64) * 512 + channel * 64 + byte_offset % 64;
            const unsigned available = 64 - byte_offset % 64;
            unsigned chunk = length < available ? length : available;
            if (lower && dense < lower && chunk > lower - dense) chunk = lower - dense;
            const uint64_t address = lower && dense >= lower ? upper + (dense - lower) : base + dense;
            if (!transfer(address, data, chunk, write)) return false;
            byte_offset += chunk;
            data += chunk;
            length -= chunk;
        }
        return true;
    }

private:
    enum class Phase { reset, configured, initialized, training, trained, exited, failed };
    Phase phase_ = Phase::reset;
    bool started_ = false;
    std::array<unsigned char, 0x480> shared_{};
    std::array<unsigned char, 0xa4> dcb_{};
    std::array<unsigned char, 0x700> aop_{};
    std::array<unsigned char, 0xa000> phy_{};
    std::array<unsigned char, 256> scratch_{};
    uint64_t dcb_address_ = 0, aop_address_ = 0;
    unsigned current_frequency_ = 0;
    std::uint64_t clock_period_ps_ = 800;

    bool fail() { phase_ = Phase::failed; return false; }
    static uint64_t get(const unsigned char* p, unsigned n) {
        uint64_t value = 0;
        for (unsigned i = 0; i < n; ++i) value |= uint64_t{p[i]} << (i * 8);
        return value;
    }
    static void put(unsigned char* p, uint64_t value, unsigned n) {
        for (unsigned i = 0; i < n; ++i) p[i] = static_cast<unsigned char>(value >> (i * 8));
    }
    static bool range(uint64_t address, std::size_t length) {
        return address && length && address <= std::numeric_limits<uint64_t>::max() - length;
    }
    bool transfer(uint64_t address, unsigned char* bytes, unsigned length, bool write) {
        if (!range(address, length)) return false;
        tlm::tlm_generic_payload tx;
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_address(address);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        memory_socket->b_transport(tx, delay);
        if (delay != sc_core::SC_ZERO_TIME) sc_core::wait(delay);
        return tx.is_response_ok();
    }
    bool write_shared(unsigned offset, unsigned length) {
        return transfer(shared_base.get_value() + offset, shared_.data() + offset, length, true);
    }
    bool probe(uint64_t address, unsigned length) {
        if (!range(address, length)) return false;
        while (length) {
            const unsigned count = length < scratch_.size() ? length : scratch_.size();
            if (!transfer(address, scratch_.data(), count, false)) return false;
            address += count;
            length -= count;
        }
        return true;
    }
    static bool disjoint(uint64_t a, std::size_t an, uint64_t b, std::size_t bn) {
        return range(a, an) && range(b, bn) && (a + an <= b || b + bn <= a);
    }
    bool section(unsigned index, unsigned minimum) {
        const unsigned offset = get(dcb_.data() + index, 2);
        const unsigned size = get(dcb_.data() + index + 2, 2);
        return offset >= dcb_.size() && size >= minimum && offset + size <= 0x10000 &&
               range(dcb_address_, offset + size) &&
               disjoint(dcb_address_ + offset, size, shared_base.get_value(), shared_.size()) &&
               disjoint(dcb_address_ + offset, size, aop_address_, aop_.size()) &&
               probe(dcb_address_ + offset, size);
    }
    bool configuration() {
        const unsigned r = rows.get_value(), c = columns.get_value(), b = banks.get_value();
        // XBL tables 0x1492678e/0x149267b6 decode MR8 0x10 as 15/10/16 and 1024 MiB per controller.
        if (r != 15 || c != 10 || b != 16) return false;
        const uint64_t lane_mib = ((uint64_t{1} << (r + c)) * b) >> 20;
        const uint64_t controller_mib = lane_mib * 2;
        if (controller_mib != 1024 || rank0_mib.get_value() != lane_mib ||
            (rank1_mib.get_value() && rank1_mib.get_value() != lane_mib) ||
            (dram_base.get_value() & 0xfffff)) return false;
        const uint64_t total = uint64_t{rank0_mib.get_value() + rank1_mib.get_value()} * 8 << 20;
        if (!range(dram_base.get_value(), total) ||
            !range(shared_base.get_value(), shared_.size()) ||
            !transfer(shared_base.get_value(), shared_.data(), shared_.size(), false)) return false;
        if (get(shared_.data() + 0x1a0, 4)) return false;
        dcb_address_ = get(shared_.data() + 0x468, 8);
        aop_address_ = get(shared_.data() + 0x470, 8);
        // These are distinct guest objects, not aliases of the shared header.
        if (!disjoint(dcb_address_, dcb_.size(), shared_base.get_value(), shared_.size()) ||
            !disjoint(aop_address_, aop_.size(), shared_base.get_value(), shared_.size()) ||
            !disjoint(aop_address_, aop_.size(), dcb_address_, dcb_.size())) return false;
        if ((dcb_address_ & 3) || (aop_address_ & 3) ||
            !transfer(dcb_address_, dcb_.data(), dcb_.size(), false) ||
            !transfer(aop_address_, aop_.data(), aop_.size(), false)) return false;
        // Runtime, training parameters, and post-training restore are guest inputs.
        // All four selected DCBs declare 0xb00 runtime bytes. XBL's later
        // runtime+0xbb4 flags belong to the following section, declared at +0x3c.
        if (!section(0x34, 1) || !section(0x38, 0xb00) ||
            !section(0x3c, 1) || !section(0x40, 1)) return false;
        const unsigned flags = get(dcb_.data() + 0x38, 2) + 0xbb4;
        const unsigned extension = get(dcb_.data() + 0x3c, 2);
        const unsigned extension_size = get(dcb_.data() + 0x3e, 2);
        if (flags < extension || flags + 3 > extension + extension_size) return false;
        const unsigned levels = shared_[0x2c];
        if (!levels || levels > 12 || !shared_[0x2d] || shared_[0x2d] > 16) return false;
        for (unsigned i = 0; i < levels; ++i) {
            const auto* state = aop_.data() + 0x80 + 0x28 * i;
            if (state[0x10] > 1) return false;
        }
        return true;
    }
    bool configure_sram_retention() {
        auto transport = [this](uint64_t address, unsigned char* data, unsigned length, bool write) {
            return transfer(address, data, length, write);
        };
        DdrTrainingContext context(transport, shared_base.get_value() - 0x2800);
        std::array<unsigned char, 5> flags{};
        std::uint8_t code_mask, data_mask;
        std::uint32_t control;
        if (!context.read(context.local(0x403344), flags.data(), flags.size()) ||
            !context.read8(context.local(0x403538), code_mask) ||
            !context.read8(context.local(0x403539), data_mask) ||
            !context.read32(0x310a80bc, control)) return false;
        // Both selected images occupy two 32-KiB retention blocks.
        code_mask |= 3;
        data_mask |= 3;
        const std::uint8_t retention = !flags[2] || flags[4] ? 0xff :
            (flags[0] || flags[1] ? data_mask | 0xe0 : code_mask | 0x20);
        if (!context.write8(context.local(0x403538), code_mask) ||
            !context.write8(context.local(0x403539), data_mask) ||
            !context.write32(0x310a92bc, retention) ||
            !context.write32(0x310a80bc, control & 0x89c0300f)) return false;
        if (!flags[4]) return true;
        if (flags[0] || flags[1]) return context.write8(context.local(0x403348), 0);
        return context.write32(context.local(0x400cb4 + training_data_offset.get_value()), 0x408000) &&
            context.write32(0x31078000, 1);
    }
    bool initialize() {
        // Four x16 controllers each use two x8 PXI lanes.
        // XBL 0x148b66f0 reconstructs four capacities and normalized source bases.
        // ICB 0x1484cb5c consumes four entries. Slots four to seven stay zero.
        const unsigned rank1 = rank1_mib.get_value() ? 1 : 0;
        shared_[0x30] = 0xff; // Virtual manufacturer, not a captured part identity.
        shared_[0x31] = 8;    // LPDDR5. Type 7 takes XBL's unsupported-device path.
        shared_[0x32] = 0;
        for (unsigned rank = 0; rank < 2; ++rank) {
            const bool populated = rank == 0 || rank1;
            const unsigned lane_mib = rank ? rank1_mib.get_value() : rank0_mib.get_value();
            shared_[0x33 + rank] = populated ? rows.get_value() : 0;
            shared_[0x35 + rank] = populated ? columns.get_value() : 0;
            shared_[0x37 + rank] = populated ? banks.get_value() : 0;
            shared_[0x39 + rank] = 0; // JEDEC x16 width code.
            // XBL density table 0x149267b6: width zero, density four gives 1024 MiB.
            if (populated && lane_mib * 2 != 1024) return false;
            shared_[0x3b + rank] = populated ? 4 : 0;
            shared_[0x3d + rank] = populated ? 1 : 0;
            shared_[0x3f + rank] = 0;
            const uint64_t base = rank ? (uint64_t{rank0_mib.get_value()} * 8 << 20) : 0;
            for (unsigned controller = 0; controller < 8; ++controller) {
                const bool active = populated && controller < 4;
                put(shared_.data() + 0x48 + rank * 0x20 + controller * 4, active ? lane_mib * 2 : 0, 4);
                put(shared_.data() + 0x98 + rank * 0x40 + controller * 8, active ? base : 0, 8);
            }
        }
        shared_[0x41] = rank1 ? 3 : 1;
        shared_[0x42] = 0;
        put(aop_.data() + 0x600, 4, 4);
        aop_[0x604] = 8; // Density in Gbit, not the MR8 density code.
        aop_[0x605] = rank1 ? 8 : 0;
        // Do not overwrite the DCB, guest pointers, clock plans, or flash header.
        return write_shared(0x30, 0x13) && write_shared(0x48, 0x40) &&
               write_shared(0x98, 0x80) &&
               transfer(aop_address_ + 0x600, aop_.data() + 0x600, 6, true);
    }
    bool switch_clock(unsigned selection) {
        // SHRM 0x2012e8 stages both plans. 0x202f86 and 0x202ff6 commit them.
        // The virtual clock changes synchronously and preserves trained profiles.
        auto transport = [this](std::uint64_t address, unsigned char* bytes, unsigned length, bool write) {
            return transfer(address, bytes, length, write);
        };
        DdrTrainingContext context(transport, shared_base.get_value() - 0x2800);
        const unsigned level = selection & 15, resource = selection >> 4;
        std::uint32_t period, frequency, resource_frequency, resource_state;
        std::uint8_t slot, ratio;
        if (!context.read32(context.local(0x402cc4 + 20 * level), period) ||
            !context.read32(context.local(0x402cbc + 20 * level), frequency) ||
            !context.read32(context.local(0x402db0 + 12 * resource), resource_frequency) ||
            !context.read32(context.local(0x402dac + 12 * resource), resource_state) ||
            !context.read8(context.local(0x40336e + level), slot) ||
            !context.read8(context.local(0x4031f6 + level), ratio)) return false;
        if (!context.write16(context.local(0x403396), selection) ||
            !context.write8(context.local(0x403c6b), level) ||
            !context.write8(context.local(0x403a99), level) ||
            !context.write32(context.local(0x403a94), period) ||
            !context.write32(context.local(0x403354), frequency) ||
            !context.write8(context.local(0x403389), resource) ||
            !context.write32(context.local(0x403384), resource_frequency) ||
            !context.write8(context.local(0x403a98), level) ||
            !context.write32(context.local(0x403a90), period) ||
            !context.write32(context.local(0x403350), frequency) ||
            !context.write8(context.local(0x403388), resource) ||
            !context.write32(context.local(0x403380), resource_frequency) ||
            !context.write32(context.local(0x40337c), resource_state) ||
            !context.write16(context.local(0x403364), slot) ||
            !context.write32(context.local(0x403a8c), 0) ||
            !context.write8(context.local(0x4033fb), 0) ||
            !context.write32(0x34a00080, period)) return false;
        current_frequency_ = level;
        if (period) clock_period_ps_ = std::uint64_t{period} * (ratio == 3 ? 4 : 2);
        return true;
    }
    bool train() {
        if (current_frequency_ >= shared_[0x2c] ||
            aop_[0x80 + current_frequency_ * 0x28 + 0x10] != 1) return false;
        return physical_snapshot(false);
    }
    bool restore() { return physical_snapshot(true); }
    bool physical_snapshot(bool restore) {
        const uint64_t flash = get(shared_.data() + 0x460, 8);
        if ((flash & 7) ||
            !disjoint(flash, 0xbb78, shared_base.get_value(), shared_.size()) ||
            !disjoint(flash, 0xbb78, aop_address_, aop_.size()) ||
            !disjoint(flash, 0xbb78, dcb_address_, dcb_.size()) ||
            !probe(flash, 0xbb78)) return false;
        std::array<unsigned char, 0x1400> program;
        if (!transfer(shared_base.get_value() + 0x2cb4, program.data(), program.size(), false)) return false;
        unsigned position = 0, words = 0;
        while (position < program.size() && program[position]) {
            if (position + 2 > program.size()) return false;
            const unsigned bases = program[position], offsets = program[position + 1];
            const unsigned length = 2 + 2 * (bases + offsets);
            if (length > program.size() - position || bases * offsets > phy_.size() / 4 - words)
                return false;
            words += bases * offsets;
            position += length;
        }
        if (position == program.size()) return false;
        if (!words) return true;
        if (restore && !transfer(flash + 0x1a08, phy_.data(), words * 4, false)) return false;
        unsigned cursor = 0;
        for (position = 0; program[position];) {
            const unsigned bases = program[position], offsets = program[position + 1];
            const unsigned base_start = position + 2, offset_start = base_start + 2 * bases;
            for (unsigned b = 0; b < bases; ++b) {
                const auto base = static_cast<std::uint32_t>(get(program.data() + base_start + 2 * b, 2)) << 12;
                for (unsigned o = 0; o < offsets; ++o) {
                    const auto offset = static_cast<std::uint32_t>(get(program.data() + offset_start + 2 * o, 2)) << 2;
                    const std::uint32_t address = 0x30000000U | (base + offset);
                    if (!transfer(address, phy_.data() + cursor, 4, restore)) return false;
                    cursor += 4;
                }
            }
            position = offset_start + 2 * offsets;
        }
        return restore || transfer(flash + 0x1a08, phy_.data(), cursor, true);
    }
    bool training_callback(unsigned index) {
        if (index >= 44 || shared_base.get_value() < 0x2800) return false;
        auto transport = [this](std::uint64_t address, unsigned char* bytes, unsigned length, bool write) {
            return transfer(address, bytes, length, write);
        };
        DdrTrainingContext context(transport, shared_base.get_value() - 0x2800);
        context.table_offset = training_data_offset.get_value();
        std::array<unsigned char, 48> pointers;
        const auto shadow = context.local(0x405000);
        if (!context.read(shadow + 0x450, pointers.data(), pointers.size())) return false;
        context.scratch = get(pointers.data(), 8);
        context.parameters = get(pointers.data() + 8, 8);
        context.data = get(pointers.data() + 16, 8);
        context.logs[0] = get(pointers.data() + 24, 8);
        context.logs[1] = get(pointers.data() + 32, 8);
        context.runtime = get(pointers.data() + 40, 8);
        if (!range(context.scratch, 0xbb78) || !range(context.runtime, 44 * 64) ||
            !disjoint(context.logs[0], context.log_capacity, context.logs[1], context.log_capacity) ||
            !disjoint(context.logs[0], context.log_capacity, context.scratch, 0xbb78) ||
            !disjoint(context.logs[1], context.log_capacity, context.scratch, 0xbb78) ||
            !disjoint(context.logs[0], context.log_capacity, context.runtime, 44 * 64) ||
            !disjoint(context.logs[1], context.log_capacity, context.runtime, 44 * 64)) return false;
        std::array<unsigned char, 64> row;
        std::uint8_t ranks, level, fsp;
        if (!context.read(context.runtime + 64 * index, row.data(), row.size()) || row[47] != 1 ||
            !context.read32(context.local(0x402c7c), context.channels) ||
            !context.read8(context.local(0x402841), ranks) ||
            !context.read8(context.local(0x403a98), level) || level >= shared_[0x2c] ||
            !context.read32(context.local(0x404060 + 4 * level), context.profile) ||
            !context.read8(context.local(0x402f2a + 10 * level), fsp) ||
            !context.channels || (context.channels & ~15U) || !ranks || (ranks & ~3U) ||
            context.profile > 10 || fsp > 7) return false;
        context.ranks = ranks;
        context.level = level;
        context.fsp = fsp;
        std::uint32_t period;
        std::uint8_t ratio;
        if (!context.read32(context.local(0x403a90), period) ||
            !context.read8(context.local(0x4031f6 + level), ratio)) return false;
        if (period) clock_period_ps_ = std::uint64_t{period} * (ratio == 3 ? 4 : 2);
        unsigned length = 0;
        while (length < 25 && row[22 + length]) ++length;
        const std::string_view name(reinterpret_cast<const char*>(row.data() + 22), length);
        if (name.empty() || !context.fill(context.logs[0], context.log_capacity) ||
            !context.fill(context.logs[1], context.log_capacity)) return false;
        const bool eye = name == "PHY_SED_RD" || name == "PHY_SED_LP5_WR_Print";
        auto result = eye ? training_pxi(context, name) : TrainingResult::complete;
        if (result == TrainingResult::complete) {
            result = training_calibration(context, name);
            if (result == TrainingResult::unhandled ||
                (result == TrainingResult::complete && name == "RD_Low_Freq_Write"))
                result = training_pxi(context, name);
        }
        if (result != TrainingResult::complete) {
            std::fprintf(stderr, "DDR_TRAINING_CALLBACK_FAILED model=%s index=%u name=%.*s level=%u\n",
                         this->name(), index, static_cast<int>(name.size()), name.data(), context.level);
            return false;
        }
        if (name == "LP5_WR_CPY_DVref_Upr_FSP" || name == "LP5_WR_CPY_MR30_Upr_FSP" ||
            name == "LP5_RD_CPY_MR69_Upr_FSP") {
            std::uint8_t target;
            std::uint32_t frequency, state;
            if (!context.read8(context.local(0x403a99), target) || target >= shared_[0x2c] ||
                aop_[0x80 + target * 0x28 + 0x10] != 1 ||
                !context.read32(context.local(0x403a94), frequency) ||
                !context.read32(context.local(0x403354), state) ||
                !context.read8(context.local(0x4031f6 + target), ratio) ||
                !context.write8(context.local(0x403a98), target) ||
                !context.write32(context.local(0x403a90), frequency) ||
                !context.write32(context.local(0x403350), state) ||
                !context.write32(0x34a00080, frequency)) return false;
            current_frequency_ = target;
            if (frequency) clock_period_ps_ = std::uint64_t{frequency} * (ratio == 3 ? 4 : 2);
        }
        if (!context.write32(shadow + 0x488, context.counts[0]) ||
            !context.write32(shadow + 0x48c, context.counts[1])) return false;
        std::fprintf(stderr,
            "DDR_TRAINING_CALLBACK model=%s index=%u name=%.*s level=%u bytes=%u,%u policy=ideal-electrical-real-memory\n",
            this->name(), index, static_cast<int>(name.size()), name.data(), context.level,
            context.counts[0], context.counts[1]);
        return true;
    }
};

} // namespace qcom_vp

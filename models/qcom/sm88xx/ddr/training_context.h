// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace qcom_vp {

enum class TrainingResult { unhandled, complete, failed };

// Both callback groups use the same guest SRAM and transaction path.
template<class Transfer>
struct DdrTrainingContext {
    Transfer& transfer;
    std::uint64_t sram_base;
    std::uint64_t scratch = 0, parameters = 0, data = 0, runtime = 0;
    std::uint64_t logs[2]{};
    std::uint32_t channels = 0, ranks = 0, level = 0, profile = 0, fsp = 0;
    std::uint32_t table_offset = 0;
    std::uint32_t counts[2]{};
    static constexpr unsigned log_capacity = 0xe00;

    DdrTrainingContext(Transfer& transport, std::uint64_t base)
        : transfer(transport), sram_base(base) {}

    std::uint64_t local(std::uint32_t address) const {
        return sram_base + (address - 0x400000);
    }
    std::uint64_t table(std::uint32_t address) const {
        return local(address + table_offset);
    }
    bool read(std::uint64_t address, unsigned char* bytes, unsigned length) {
        return transfer(address, bytes, length, false);
    }
    bool write(std::uint64_t address, const unsigned char* bytes, unsigned length) {
        return transfer(address, const_cast<unsigned char*>(bytes), length, true);
    }
    bool read8(std::uint64_t address, std::uint8_t& value) {
        return read(address, &value, 1);
    }
    bool read16(std::uint64_t address, std::uint16_t& value) {
        unsigned char bytes[2];
        if (!read(address, bytes, sizeof bytes)) return false;
        value = std::uint16_t{bytes[0]} | (std::uint16_t{bytes[1]} << 8);
        return true;
    }
    bool read32(std::uint64_t address, std::uint32_t& value) {
        unsigned char bytes[4];
        if (!read(address, bytes, sizeof bytes)) return false;
        value = std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8) |
                (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
        return true;
    }
    bool write8(std::uint64_t address, std::uint8_t value) {
        return write(address, &value, 1);
    }
    bool write16(std::uint64_t address, std::uint16_t value) {
        const unsigned char bytes[]{static_cast<unsigned char>(value),
                                    static_cast<unsigned char>(value >> 8)};
        return write(address, bytes, sizeof bytes);
    }
    bool write32(std::uint64_t address, std::uint32_t value) {
        const unsigned char bytes[]{static_cast<unsigned char>(value),
                                    static_cast<unsigned char>(value >> 8),
                                    static_cast<unsigned char>(value >> 16),
                                    static_cast<unsigned char>(value >> 24)};
        return write(address, bytes, sizeof bytes);
    }
    bool update32(std::uint64_t address, std::uint32_t mask, std::uint32_t value) {
        std::uint32_t old;
        return read32(address, old) && write32(address, (old & ~mask) | (value & mask));
    }
    bool fill(std::uint64_t address, unsigned length, unsigned char value = 0) {
        std::array<unsigned char, 256> bytes;
        bytes.fill(value);
        while (length) {
            const unsigned count = length < bytes.size() ? length : bytes.size();
            if (!write(address, bytes.data(), count)) return false;
            address += count;
            length -= count;
        }
        return true;
    }
};

} // namespace qcom_vp

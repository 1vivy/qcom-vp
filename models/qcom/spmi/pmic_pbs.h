// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace qcom_vp {

// Programming transport only. This component does not execute PBS instructions,
// drive triggers, authenticate images, or implement the independent ADC service.
// The PBS driver supplies memory information, configuration access, disable
// access, RAM image load, and RAM image verification.
// Selected SM8845 consumers: 0x1487f3ec (download), 0x1487f1d8 (setup),
// 0x1487f6e0 (disable), 0x1487eedc (readback). Setup uses byte addresses.
// Selected SM8845 geometry decoder 0x1487ec64..0x1487ee48:
// NVM words = base * 2^(multiplier & 3) * 8, RAM byte base = NVM words * 4.
// RAM words = configuration * 32. At revision >= 0x0500, configuration
// includes the high byte only when its low byte exceeds eight.
// Revision 0x0500 uses high byte +a6. Revision 0x0501 uses +a1.
// Example virtual geometry {0x0501, 0, 0xc000}: a0=80, a1=01, a3=a4=00.
// Selected packed driver 0x1486eb24..0x1486eb68 repeats +80 for each
// <=8-byte upload chunk. The RAM pointer advances, not the SPMI offset.
// Integration must route owned offsets here, including unsupported aliases.
// Never use error-based sparse fallback or per-byte FIFO accesses.
// Pre-upload consumers 0x1487f810/0x1487f99c/0x1487f91c observe +20/+27,
// read +28/2 only for state 1, and pulse +c3 bit 2. No autonomous producer
// exists here: both status inputs remain zero (state 0, not started).
// No loaded-version value exists. The control pulse cannot create one.
class QcomPmicPbs {
public:
    // These are explicit virtual geometry, not inferred physical specifications.
    // The reserved aperture below RAM has no readable ROM/OTP backing.
    // RAM capacity must use 128-byte units. The address space is 16-bit.
    struct Config {
        std::uint16_t revision;
        std::uint32_t ram_base_bytes;
        std::uint32_t ram_size_bytes;
    };

    enum class Result {
        ok, unknown_register, unsupported, disabled, wrong_mode,
        invalid_transaction, bounds
    };

    static constexpr bool owns_register(unsigned offset) {
        return offset <= 7 || (offset >= 0x40 && offset <= 0x43) ||
               (offset >= 0x48 && offset <= 0x4f) ||
               (offset >= 0x80 && offset <= 0x8f) ||
               (offset >= 0xa0 && offset <= 0xa6) ||
               offset == 0x20 || (offset >= 0x27 && offset <= 0x29) ||
               offset == 0x9d || offset == 0xc3 || offset == 0xe9 || offset == 0xeb;
    }

    explicit QcomPmicPbs(Config config) : config_(config) {
        // These versions have documented geometry encodings. Other versions
        // require a separate contract, rather than an assumed compatible layout.
        if (config.revision != 0x0301 && config.revision != 0x0303 &&
            config.revision != 0x0500 && config.revision != 0x0501)
            throw std::invalid_argument("Unsupported virtual PBS revision");
        if (!config.ram_size_bytes || config.ram_size_bytes % 128 ||
            config.ram_base_bytes % 32 || config.ram_base_bytes >= 65536 ||
            config.ram_size_bytes > 65536 - config.ram_base_bytes)
            throw std::invalid_argument("Invalid virtual PBS geometry");
        unsigned aperture = config.ram_base_bytes / 32;
        while (aperture > 255 && nvm_multiplier_ < 3 && !(aperture & 1)) {
            aperture >>= 1;
            ++nvm_multiplier_;
        }
        const auto units = config.ram_size_bytes / 128;
        if (aperture > 255 ||
            (config.revision < 0x0500 && units > 255) ||
            (units > 255 && (units & 255) <= 8))
            throw std::invalid_argument("Unrepresentable virtual PBS geometry");
        nvm_base_ = static_cast<std::uint8_t>(aperture);
        ram_.resize(config.ram_size_bytes);
    }

    // Declared virtual SRAM is zero at construction and after cold reset.
    // Warm reset retains guest bytes. Both resets disable the interface.
    void cold_reset() {
        for (auto& byte : ram_) byte = 0;
        warm_reset();
    }

    void warm_reset() {
        enabled_ = false;
        control_ = 0;
        pointer_ = 0;
        pre_upload_control_ = 0;
    }

    // Offsets are relative to the configured PBS peripheral. Preserve the
    // complete SPMI transaction: never call once per byte of a FIFO transfer.
    // All failures leave RAM, pointer, registers and caller output unchanged.
    // FIFO transfers require one complete 4-byte word. Wide windows require
    // complete 8-byte units. Repeated burst transactions advance the pointer.
    Result read(unsigned offset, std::uint8_t* data, std::size_t size) {
        if (!data || !size) return Result::invalid_transaction;
        if (offset == 0x4c || offset == 0x88)
            return transfer(false, offset, data, nullptr, size);
        if (offset > 255 || size > 256 - offset) return Result::invalid_transaction;
        for (std::size_t i = 0; i < size; ++i) {
            std::uint8_t value = 0;
            const auto result = register_value(offset + static_cast<unsigned>(i), value);
            if (result != Result::ok) return result;
        }
        for (std::size_t i = 0; i < size; ++i)
            register_value(offset + static_cast<unsigned>(i), data[i]);
        return Result::ok;
    }

    // Reports whether this transaction uses the RAM transfer port under the
    // current enable and mode. The answer changes no state. A transport uses it
    // to tell a real transfer from an observation burst that reads the port.
    bool transfer_shaped(bool writing, unsigned offset, std::size_t size) const {
        if (!enabled_ || bool(control_ & 0x40) != writing) return false;
        const bool wide = control_ & 0x20;
        const bool read_alias = !writing && offset == 0x4c && control_ == 0xa0;
        const unsigned width = wide && !read_alias ? 8 : 4;
        if (!read_alias &&
            offset != (writing ? (wide ? 0x80U : 0x48U) : (wide ? 0x88U : 0x4cU)))
            return false;
        return !(size % width) && !(width == 4 && size != width) &&
            ((control_ & 0x80) || size == width);
    }

    Result write(unsigned offset, const std::uint8_t* data, std::size_t size) {
        if (!data || !size) return Result::invalid_transaction;
        if (offset == 0x48 || offset == 0x80)
            return transfer(true, offset, nullptr, data, size);
        if (offset > 255 || size > 256 - offset) return Result::invalid_transaction;
        // Interface registers and the independent pre-upload control are
        // writable. Validate the whole transaction before committing any byte.
        for (std::size_t i = 0; i < size; ++i) {
            const auto reg = offset + i;
            if (reg == 0x40) {
                if (data[i] != 0 && data[i] != 0x80) return Result::unsupported;
            } else if (reg == 0x41) {
                if ((data[i] & ~0xe0U) ||
                    ((data[i] & 0x20) &&
                     (config_.revision < 0x0501 || !(data[i] & 0x80))))
                    return Result::unsupported;
            } else if (reg == 0xc3) {
                if (data[i] & ~4U) return Result::unsupported;
            } else if (reg != 0x42 && reg != 0x43) {
                std::uint8_t ignored = 0;
                const auto result = register_value(static_cast<unsigned>(reg), ignored);
                return result == Result::ok ? Result::unsupported : result;
            }
        }
        for (std::size_t i = 0; i < size; ++i) {
            switch (offset + i) {
            case 0x40: enabled_ = data[i] != 0; break;
            case 0x41: control_ = data[i]; break;
            case 0x42: pointer_ = (pointer_ & 0xff00) | data[i]; break;
            case 0x43: pointer_ = (pointer_ & 0xff) | (unsigned(data[i]) << 8); break;
            case 0xc3: pre_upload_control_ = data[i]; break;
            }
        }
        return Result::ok;
    }

private:
    Config config_;
    std::vector<std::uint8_t> ram_; // One construction allocation, none in transport.
    std::uint32_t pointer_ = 0; // Includes terminal 0x10000. Never wraps into ROM.
    std::uint8_t control_ = 0;
    std::uint8_t pre_upload_control_ = 0;
    std::uint8_t nvm_base_ = 0;
    std::uint8_t nvm_multiplier_ = 0;
    bool enabled_ = false;

    Result register_value(unsigned offset, std::uint8_t& value) const {
        const auto units = config_.ram_size_bytes / 128;
        const bool wide = config_.revision > 0x0500;
        if (offset == 0) value = static_cast<std::uint8_t>(config_.revision);
        else if (offset == 1) value = static_cast<std::uint8_t>(config_.revision >> 8);
        // Analog revision 1.0 is explicit virtual policy, not a captured value.
        else if (offset == 2) value = 0;
        else if (offset == 3) value = 1;
        else if (offset == 4) value = 0x16;
        else if (offset == 5) value = 1;
        else if (offset == 6) value = 0; // Discovery dummy, not client status.
        // The virtual core exposes three passive client inputs.
        // The PBS driver initialization reads their count. This is not execution status.
        else if (offset == 7) value = 3;
        else if (offset == 0x20 || offset == 0x27) value = 0;
        else if (offset == 0x40) value = enabled_ ? 0x80 : 0;
        else if (offset == 0x41) value = control_;
        else if (offset == 0x42) value = static_cast<std::uint8_t>(pointer_);
        else if (offset == 0x43) value = static_cast<std::uint8_t>(pointer_ >> 8);
        else if (offset == 0xa0) value = static_cast<std::uint8_t>(units);
        else if (offset == (wide ? 0xa3U : 0xa2U)) value = nvm_base_;
        else if (offset == (wide ? 0xa4U : 0xa3U)) value = nvm_multiplier_;
        else if (config_.revision >= 0x0500 && offset == (wide ? 0xa1U : 0xa6U))
            value = static_cast<std::uint8_t>(units >> 8);
        else if (offset == 0xc3) value = pre_upload_control_;
        else return owns_register(offset) ? Result::unsupported : Result::unknown_register;
        return Result::ok;
    }

    Result transfer(bool writing, unsigned offset, std::uint8_t* output,
                    const std::uint8_t* input, std::size_t size) {
        if (!enabled_) return Result::disabled;
        if (bool(control_ & 0x40) != writing) return Result::wrong_mode;
        const bool wide = control_ & 0x20;
        // Selected RAM-version consumers 0x14880294..0x148802b0 (SM8845)
        // and 0x14880bd4..0x14880bf0 (SM8850) use +4c/4 under control a0.
        const bool read_alias = !writing && offset == 0x4c && control_ == 0xa0;
        const unsigned width = wide && !read_alias ? 8 : 4;
        if (!read_alias &&
            offset != (writing ? (wide ? 0x80U : 0x48U) : (wide ? 0x88U : 0x4cU)))
            return Result::wrong_mode;
        if (size % width || (width == 4 && size != width) ||
            (!(control_ & 0x80) && size != width))
            return Result::invalid_transaction;
        if (pointer_ % width || pointer_ < config_.ram_base_bytes)
            return Result::bounds;
        const auto index = pointer_ - config_.ram_base_bytes;
        if (index > ram_.size() || size > ram_.size() - index) return Result::bounds;
        for (std::size_t i = 0; i < size; ++i) {
            if (writing) ram_[index + i] = input[i];
            else output[i] = ram_[index + i];
        }
        if (control_ & 0x80) pointer_ += static_cast<std::uint32_t>(size);
        return Result::ok;
    }
};

} // namespace qcom_vp

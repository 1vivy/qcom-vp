// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "rsc.h"

namespace qcom_vp {

// This version stores the DCP sequence and shares the RPMh resource backend.
class QcomDisplayRsc : public QcomRpmhRsc {
public:
    explicit QcomDisplayRsc(sc_core::sc_module_name name) : QcomRpmhRsc(name) {}

    // The caller supplies an observed hardware event, not a software read.
    void timestamp_event(unsigned channel, std::uint64_t ticks) {
        if (channel >= timestamp_.size())
            throw std::out_of_range("The display RSC timestamp channel is invalid");
        auto& timestamp = timestamp_[channel];
        if (!(timestamp.enable & 1)) return;
        timestamp.low = static_cast<std::uint32_t>(ticks);
        timestamp.high = static_cast<std::uint32_t>(ticks >> 32) & 0xffffff;
        timestamp.status = 1;
    }

protected:
    bool access(std::uint64_t offset, bool write, std::uint32_t& value) override {
        if (offset == 8) {
            if (write) return false;
            value = static_cast<std::uint32_t>(sequence_.size()) << 16;
            return true;
        }
        // RSC_SEQ_PWR_CTRL_STATUS_DRV0, power-on reset zero. UEFI DisplayDxe
        // RVA 0x321f4 tests bit 12. No sequence runs, so the outputs keep their reset value.
        if (offset == 0x2d0) {
            if (write) return false;
            value = 0;
            return true;
        }
        std::uint32_t* storage = nullptr;
        std::uint32_t mask = 0xffffffff;
        if (offset == 0x38) storage = &match_high_;
        else if (offset == 0x40) storage = &match_low_;
        else if (offset == 0x48) storage = &pdc_start_;
        else if (offset == 0x110) storage = &timestamp_control_;
        else if (offset == 0x11c) storage = &timestamp_clock_;
        else if (offset == 0x220) { storage = &override_; mask = 0x800003ff; }
        else if (offset >= 0x2dc && offset < 0x2ec) {
            storage = &branch_[(offset - 0x2dc) / 4];
            mask = 0x3ff;
        } else if (offset >= 0x370 && offset < 0x370 + sequence_.size() * 4) {
            storage = &sequence_[(offset - 0x370) / 4];
        } else if (offset >= 0x124 && offset < 0x1a4) {
            auto& timestamp = timestamp_[(offset - 0x124) / 0x20];
            switch ((offset - 0x124) % 0x20) {
            case 0: storage = &timestamp.enable; mask = 1; break;
            case 4:
                if (write) return false;
                value = timestamp.low;
                return true;
            case 8:
                if (write) return false;
                value = timestamp.high;
                return true;
            case 12:
                if (write) return false;
                value = timestamp.status;
                return true;
            default: return false;
            }
        }
        if (storage) {
            if (write) {
                if (value & ~mask) return false;
                *storage = value;
            } else value = *storage;
            return true;
        }
        // Do not expose the APSS sequence window through this IP version.
        if (offset == 0xc || offset == 0xd8 || offset >= tcs_offset.get_value())
            return QcomRpmhRsc::access(offset, write, value);
        return false;
    }

private:
    struct Timestamp {
        std::uint32_t enable = 0;
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        std::uint32_t status = 0;
    };
    std::array<std::uint32_t, 64> sequence_{};
    std::array<std::uint32_t, 4> branch_{};
    std::array<Timestamp, 4> timestamp_{};
    std::uint32_t match_high_ = 0;
    std::uint32_t match_low_ = 0;
    std::uint32_t pdc_start_ = 0;
    std::uint32_t timestamp_control_ = 0;
    std::uint32_t timestamp_clock_ = 0;
    std::uint32_t override_ = 0;
};

} // namespace qcom_vp

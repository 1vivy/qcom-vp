// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace qcom_vp {

// The public CPR3 regulator driver defines the 0x440 thread stride and the
// threshold, result, and target words. Selected XBL uses a 0x740 thread stride,
// adds bit 2 at 0x32d0, and initializes byte fields starting at 0x3640.
// Ideal sensors match their programmed RO targets. This device does not drive
// a regulator or a CPRF transaction, external IRQ, temperature source, or fuse.
// `models/qcom/cpr/cprf.h` owns the CPRF words that firmware writes.
class QcomCpr3Controller : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCpr3Controller> target_socket{"target_socket"};
    cci::cci_param<unsigned> sensor_count{"sensor_count", 0, "Physical sensor count, 1 through 256"};
    cci::cci_param<unsigned> thread_count{"thread_count", 0, "Thread count, 1 or 2"};

    SC_HAS_PROCESS(QcomCpr3Controller);
    explicit QcomCpr3Controller(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomCpr3Controller::transport);
        SC_METHOD(automatic_measurement);
        sensitive << timer_;
        dont_initialize();
    }

private:
    static constexpr unsigned aperture = 0x4000;
    static constexpr unsigned stride = 0x740;
    // The selected stride holds a 0x40 header and 28 target modes of 0x40 bytes.
    static constexpr unsigned target_modes = (stride - 0x40) / 0x40;
    struct Thread {
        uint32_t threshold = 0;
        uint32_t mask = 0x8000ffffu;
        uint32_t irq_enable = 0;
        uint32_t irq_status = 0;
        uint32_t mode = 0;
        std::array<uint32_t, 3> result{};
        std::array<std::array<uint32_t, 16>, target_modes> target{};
        bool automatic = false;
        sc_core::sc_time due = sc_core::SC_ZERO_TIME;
    };
    std::array<Thread, 2> threads_{};
    std::array<uint32_t, aperture / 4> config_{};
    std::array<uint32_t, 8> bypass_write_{}, bypass_read_{}, mask_write_{}, mask_read_{};
    std::array<uint8_t, 256> owner_{};
    sc_core::sc_event timer_;
    bool warned_aging_ = false;

    void end_of_elaboration() override {
        if (!sensor_count.get_value() || sensor_count.get_value() > 256 ||
            !thread_count.get_value() || thread_count.get_value() > threads_.size())
            throw std::invalid_argument("CPR3 requires 1..256 sensors and 1..2 threads");
    }
    bool enabled() const { return config_[1] & 1; }
    unsigned banks() const { return (sensor_count.get_value() + 31) / 32; }
    void publish_masks() {
        bypass_read_ = bypass_write_;
        mask_read_ = mask_write_;
    }
    bool available(unsigned sensor, unsigned thread) const {
        const uint32_t bit = 1u << (sensor % 32);
        return owner_[sensor] == thread &&
            !((bypass_read_[sensor / 32] | mask_read_[sensor / 32]) & bit);
    }
    sc_core::sc_time interval() const {
        // Reference clock is 19.2 MHz. A zero interval still takes one cycle.
        const auto cycles = config_[0xc / 4] ? config_[0xc / 4] : 1;
        auto delay = sc_core::sc_time(static_cast<double>(cycles) / 19200000.0, sc_core::SC_SEC);
        return delay == sc_core::SC_ZERO_TIME ? sc_core::sc_get_time_resolution() : delay;
    }
    void arm_timer() {
        timer_.cancel();
        bool found = false;
        sc_core::sc_time earliest;
        for (unsigned t = 0; t < thread_count.get_value(); ++t) {
            if (!threads_[t].automatic) continue;
            if (!found || threads_[t].due < earliest) earliest = threads_[t].due;
            found = true;
        }
        if (found) timer_.notify(earliest > sc_core::sc_time_stamp() ?
            earliest - sc_core::sc_time_stamp() : sc_core::SC_ZERO_TIME);
    }
    void measure(unsigned tid) {
        auto& thread = threads_[tid];
        thread.automatic = false;
        if (!enabled() || (thread.mask & 0x80000000u)) return;
        thread.result = {1, 0, 0};
        // Aging has no target-derived physical transfer function. Keep BUSY
        // asserted instead of inventing an aging delta or PAGE_IS_AGE result.
        if ((config_[1] & 0xc0) == 0xc0 || (config_[1] & 0x100) ||
            (config_[0x6c0 / 4] & 1)) {
            if (!warned_aging_) {
                SC_REPORT_WARNING(name(), "CPR3 aging, HTOL, and 64-RO measurements are unsupported");
                warned_aging_ = true;
            }
            return;
        }
        unsigned first = sensor_count.get_value(), last = first;
        for (unsigned s = 0; s < sensor_count.get_value(); ++s) {
            if (!available(s, tid)) continue;
            if (first == sensor_count.get_value()) first = s;
            last = s;
        }
        if (first == sensor_count.get_value()) return;
        unsigned minq = 0x1000, maxq = 0, minro = 0, maxro = 0;
        bool found = false;
        for (unsigned ro = 0; ro < 16; ++ro) {
            if ((thread.mask | config_[0x704 / 4]) & (1u << ro)) continue;
            const auto target = thread.target[thread.mode][ro];
            const auto gate = (config_[0x700 / 4] & 0x400) ?
                ((target >> 12) & 0x3ff) : (config_[(0xa0 + ro * 4) / 4] & 0x3ff);
            const auto quot = target & 0xfff;
            if (!gate || !quot) continue;
            if (quot < minq) { minq = quot; minro = ro; }
            if (!found || quot > maxq) { maxq = quot; maxro = ro; }
            found = true;
        }
        if (!found) return;
        // Each ideal RO equals its own target, so error and voltage steps are
        // zero even when different ROs have different absolute quotients.
        thread.result[0] = 0;
        thread.result[1] = minq | (maxq << 12) | (minro << 24) | (maxro << 28);
        thread.result[2] = (config_[0x14 / 4] & 0xfff) | (first << 16) | (last << 24);
        thread.irq_status |= 5; // Completed measurement and MID, before IRQ masking.
        if (!(thread.irq_enable & 4)) {
            thread.automatic = true;
            thread.due = sc_core::sc_time_stamp() + interval();
        }
    }
    void automatic_measurement() {
        for (unsigned t = 0; t < thread_count.get_value(); ++t)
            if (threads_[t].automatic && threads_[t].due <= sc_core::sc_time_stamp()) measure(t);
        arm_timer();
    }
    // Retained configuration only. These registers do not imply external
    // voltage, temperature, ACD, BIST, or interrupt-delivery effects.
    bool retained(unsigned offset) const {
        if (offset >= 0xa0 && offset < 0xe0) return true;
        if (offset >= 0x600 && offset <= 0x6c0) return true;
        // CPRDxe writes one step-counter entry for each mode of the voltage plan
        // from 0x32d4. RVA 0xb940 writes entry n+1, and MAX_STEPS bounds the table.
        // Run768 logged every write above 0x3314. Only the GFX plan goes past it, and its
        // last entry is 19 at 0x3320. Ideal sensors take no voltage steps from these limits.
        if (offset >= 0x32b0 && offset <= 0x3320) return true;
        // Selected initializer walks a byte-counted array here. Its field name
        // is not established. Preserve it as opaque config.
        if (offset >= 0x3640 && offset < 0x3a40) return true;
        switch (offset) {
        case 0xc: case 0x10: case 0x14: case 0x70:
        case 0x700: case 0x704: case 0x70c: case 0x710: case 0x714:
        case 0x718: case 0x71c: case 0x798: case 0x79c: case 0x7a0:
        case 0x7a4: case 0x7a8: case 0x7ac: case 0x7b0: case 0x7b4:
        case 0x7b8: case 0x7f0: case 0x7f4: case 0x7f8: case 0x7fc:
        case 0x3100: case 0x3104: return true;
        default: return false;
        }
    }
    bool access(unsigned offset, uint32_t& value, bool write) {
        if (offset == 4) {
            if (!write) { value = config_[1]; return true; }
            const bool was_enabled = enabled();
            config_[1] = value;
            if (enabled()) {
                publish_masks();
                if (!was_enabled) {
                    for (unsigned t = 0; t < thread_count.get_value(); ++t) measure(t);
                }
            } else {
                for (auto& thread : threads_) {
                    thread.automatic = false;
                    thread.result[0] &= ~1u;
                }
            }
            arm_timer();
            return true;
        }
        if (offset == 8 && !write) {
            value = 0;
            for (unsigned t = 0; t < thread_count.get_value(); ++t) value |= threads_[t].result[0] & 1;
            return true;
        }
        for (unsigned bank = 0; bank < banks(); ++bank) {
            if (offset == 0xe0 + 4 * bank || offset == 0x120 + 4 * bank) {
                auto& reg = offset < 0x120 ? bypass_write_[bank] : mask_write_[bank];
                if (write) { reg = value; if (enabled()) publish_masks(); }
                else value = reg;
                return true;
            }
            if (!write && (offset == 0x100 + 4 * bank || offset == 0x140 + 4 * bank)) {
                value = offset < 0x140 ? bypass_read_[bank] : mask_read_[bank];
                return true;
            }
        }
        if (offset >= 0x200 && offset < 0x200 + 4 * sensor_count.get_value()) {
            auto& reg = owner_[(offset - 0x200) / 4];
            if (write) {
                if (value >= thread_count.get_value()) return false;
                reg = static_cast<uint8_t>(value);
            } else value = reg;
            return true;
        }
        for (unsigned tid = 0; tid < thread_count.get_value(); ++tid) {
            const unsigned base = 0x800 + stride * tid;
            if (offset < base || offset >= base + stride) continue;
            auto& thread = threads_[tid];
            const unsigned local = offset - base;
            if (local >= 0x40) {
                auto& reg = thread.target[(local - 0x40) / 0x40][((local - 0x40) % 0x40) / 4];
                if (write) reg = value & 0x3fffff; else value = reg;
                return true;
            }
            switch (local) {
            case 0:
                if (!write) { value = 0; return true; }
                // Both ACK=1 and NACK=0 pulse CONT. Neither changes voltage.
                if (config_[0x10 / 4] & 0x10000000) thread.irq_status = 0;
                measure(tid);
                arm_timer();
                return true;
            case 8:
                if (write) thread.threshold = value & 0x3ffff; else value = thread.threshold;
                return true;
            case 0xc:
                if (write) {
                    const bool disabled = thread.mask & 0x80000000u;
                    thread.mask = value & 0x8000ffffu;
                    if (thread.mask & 0x80000000u) {
                        thread.automatic = false;
                        thread.result[0] &= ~1u;
                    } else if (disabled) measure(tid);
                    arm_timer();
                } else value = thread.mask;
                return true;
            case 0x10: case 0x14: case 0x18:
                if (write) return false;
                value = thread.result[(local - 0x10) / 4];
                return true;
            case 0x1c:
                if (write) {
                    thread.irq_enable = value & 0xc000000f;
                    if (thread.irq_enable & 4) thread.automatic = false;
                    else if (enabled() && !(thread.mask & 0x80000000u) && !(thread.result[0] & 1)) {
                        thread.automatic = true;
                        thread.due = sc_core::sc_time_stamp() + interval();
                    }
                    arm_timer();
                } else value = thread.irq_enable;
                return true;
            case 0x20:
                if (write) thread.irq_status &= ~value; else value = 0;
                return true;
            case 0x24:
                if (write) return false;
                value = thread.irq_status;
                return true;
            case 0x28:
                if (write) thread.mode = value & 0xf; else value = thread.mode;
                return true;
            default: return false;
            }
        }
        if (retained(offset)) {
            if (write) config_[offset / 4] = value; else value = config_[offset / 4];
            return true;
        }
        return false;
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE); return;
        }
        if (tx.get_address() >= aperture) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        }
        auto* p = tx.get_data_ptr();
        uint32_t value = uint32_t{p[0]} | (uint32_t{p[1]} << 8) |
            (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
        const bool ok = (tx.is_write() || tx.is_read()) &&
            access(static_cast<unsigned>(tx.get_address()), value, tx.is_write());
        if (!ok) {
            char message[128];
            std::snprintf(message, sizeof(message), "Unsupported CPR3 %s at +0x%llx (value 0x%08x)",
                tx.is_write() ? "write" : "read", static_cast<unsigned long long>(tx.get_address()), value);
            SC_REPORT_WARNING(name(), message);
        } else if (tx.is_read()) {
            for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        tx.set_response_status(ok ? tlm::TLM_OK_RESPONSE : tlm::TLM_COMMAND_ERROR_RESPONSE);
    }
};
} // namespace qcom_vp

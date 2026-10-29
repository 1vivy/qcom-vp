// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"

namespace qcom_vp {

// XBL 0x14861b14 and 0x14861ca4 use the Zonda ELU/OLE register contract.
class QcomZondaPll : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomZondaPll> target_socket{"target_socket"};
    cci::cci_param<std::uint64_t> reference_hz{
        "reference_hz", 19200000, "Virtual PLL reference frequency"};
    sc_core::sc_event state_changed{"state_changed"};
    sc_core::sc_event lock_changed{"lock_changed"};

    explicit QcomZondaPll(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomZondaPll::transport);
    }

    // The hardware owner supplies the effective output synchronously.
    // The model does not reproduce analog acquisition.
    std::uint64_t main_output_hz() const {
        return hardware_owned_ ? hardware_vco_hz_ : output_hz(1);
    }
    std::uint64_t even_output_hz() const {
        if (hardware_owned_) return hardware_vco_hz_ / hardware_divider_;
        constexpr unsigned divisors[] = {1, 2, 8, 4};
        return output_hz(2) / divisors[(words_[4] >> 12) & 3];
    }
    bool hardware_controlled() const { return hardware_owned_; }
    bool set_hardware_frequency(std::uint64_t vco_hz, unsigned even_divider) {
        if (even_divider != 1 && even_divider != 2 && even_divider != 4 && even_divider != 8)
            return false;
        const auto before = effective_state();
        hardware_owned_ = true;
        hardware_vco_hz_ = vco_hz;
        hardware_divider_ = even_divider;
        announce(before);
        return true;
    }
    void clear_hardware_frequency() {
        if (!hardware_owned_) return;
        const auto before = effective_state();
        hardware_owned_ = false;
        hardware_vco_hz_ = 0;
        hardware_divider_ = 1;
        announce(before);
    }

private:
    struct EffectiveState {
        std::uint64_t main;
        std::uint64_t even;
        bool running;
        bool locked;
    };

    std::array<std::uint32_t, 13> words_{};
    bool running_ = false;
    bool hardware_owned_ = false;
    std::uint64_t hardware_vco_hz_ = 0;
    unsigned hardware_divider_ = 1;
    sc_core::sc_time lock_at_ = sc_core::SC_ZERO_TIME;

    void end_of_elaboration() override {
        if (!reference_hz.get_value() || reference_hz.get_value() > 1000000000)
            throw std::invalid_argument("PLL reference must be in 1..1000000000 Hz");
    }
    bool software_locked() const {
        return running_ && sc_core::sc_time_stamp() >= lock_at_;
    }
    bool software_acquisition_pending() const {
        return running_ && sc_core::sc_time_stamp() < lock_at_;
    }
    bool effective_running() const {
        return hardware_owned_ ? hardware_vco_hz_ != 0 : running_;
    }
    bool effective_locked() const {
        return hardware_owned_ ? hardware_vco_hz_ != 0 : software_locked();
    }
    EffectiveState effective_state() const {
        return {main_output_hz(), even_output_hz(), effective_running(), effective_locked()};
    }
    std::uint64_t output_hz(std::uint32_t gate) const {
        if (!software_locked() || !(words_[0] & 1) || !(words_[3] & gate)) return 0;
        const auto pre = (words_[4] & 0x20000) ? 2u : 1u;
        const auto alpha = (words_[4] & 0x2000000) ? (words_[2] & 0xffff) : 0u;
        return reference_hz.get_value() * ((std::uint64_t{words_[1] & 0xff} << 16) + alpha) /
            (65536 * pre);
    }
    // Hardware control supplies a lock immediately.
    // Software control retains its acquisition deadline and notification.
    void announce(const EffectiveState& before) {
        const auto after = effective_state();
        if (after.main != before.main || after.even != before.even ||
            after.running != before.running || after.locked != before.locked)
            state_changed.notify(sc_core::SC_ZERO_TIME);
        if (after.locked == before.locked) return;
        if (after.locked) {
            lock_changed.cancel();
            lock_changed.notify(sc_core::SC_ZERO_TIME);
        } else if (software_acquisition_pending()) {
            lock_changed.cancel();
            lock_changed.notify(lock_at_ - sc_core::sc_time_stamp());
        } else {
            lock_changed.cancel();
        }
    }
    void update(bool reprogrammed) {
        const bool run = (words_[0] & 6) == 6 && (words_[1] & 0xff) && words_[12] == 1;
        if (run && (!running_ || reprogrammed)) {
            // The acquisition delay is virtual policy, not a measured silicon delay.
            lock_at_ = sc_core::sc_time_stamp() + sc_core::sc_time(5, sc_core::SC_US);
            lock_changed.cancel();
            lock_changed.notify(lock_at_ - sc_core::sc_time_stamp());
        }
        running_ = run;
        if (!run) lock_changed.cancel();
        state_changed.notify(sc_core::SC_ZERO_TIME);
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset > 0x30) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& word = words_[offset / 4];
        if (tx.is_read()) {
            auto value = word;
            if (offset == 0) {
                if (effective_running()) value |= 0x20000000;
                if (effective_locked()) value |= 0xc0000000;
            }
            clock_detail::store(tx, value);
        } else {
            auto value = clock_detail::load(tx);
            if (offset == 0) value &= ~std::uint32_t{0xe0000000};
            if ((offset == 0 && (value & ~7u)) ||
                (offset == 0x30 && value > 1) ||
                (offset == 0x10 && (value & 0x10000000))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            const auto changed = word ^ value;
            word = value;
            const bool reprogrammed = (offset == 4 || offset == 8) ? changed != 0 :
                offset == 0x10 && (changed & 0x2020000);
            update(reprogrammed);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

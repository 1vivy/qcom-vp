// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"

namespace qcom_vp {

// XBL 0x14861580 and 0x14861768 control the AOSS Pongo oscillator.
class QcomPongoPll : public sc_core::sc_module, public PllSource {
public:
    tlm_utils::multi_passthrough_target_socket<QcomPongoPll> target_socket{"target_socket"};
    cci::cci_param<std::uint64_t> reference_hz{"reference_hz", 19200000, "Virtual Pongo reference frequency"};
    cci::cci_param<unsigned> calibration_ratio{"calibration_ratio", 8, "Virtual calibration/reference frequency ratio"};
    sc_core::sc_event state_changed{"state_changed"};

    unsigned bank_count() const override { return 1; }
    std::uint64_t reference_rate_hz() const override { return reference_hz.get_value(); }
    const sc_core::sc_event& clock_changed() const override { return state_changed; }
    const sc_core::sc_event& lock_changed(unsigned index) const override {
        if (index) throw std::out_of_range("Pongo PLL has one bank");
        return lock_event_;
    }
    std::uint64_t main_output_hz(unsigned index) const override {
        return output_hz(index, 1) / (((words_[5] >> 6) & 15) + 1);
    }
    std::uint64_t even_output_hz(unsigned index) const override {
        return output_hz(index, 2) / (((words_[5] >> 10) & 1) + 1);
    }
    std::uint64_t calibration_output_hz(unsigned index) const override { return output_hz(index, 4); }

    explicit QcomPongoPll(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomPongoPll::transport);
    }

private:
    std::array<std::uint32_t, 16> words_{};
    bool running_ = false;
    sc_core::sc_time lock_at_ = sc_core::SC_ZERO_TIME;
    sc_core::sc_time calibration_until_ = sc_core::SC_ZERO_TIME;
    sc_core::sc_event lock_event_;

    void end_of_elaboration() override {
        if (!reference_hz.get_value() || reference_hz.get_value() > 1000000000 ||
            !calibration_ratio.get_value() || calibration_ratio.get_value() > 32)
            throw std::invalid_argument("Pongo reference or calibration ratio exceeds its supported range");
    }
    std::uint64_t output_hz(unsigned index, unsigned gate) const {
        const unsigned hardware_gate = gate == 1 ? 8 : gate == 2 ? 16 : 0;
        if (index || !running_ || sc_core::sc_time_stamp() < lock_at_ ||
            !(words_[0] & (1 | hardware_gate)) || !(words_[5] & gate)) return 0;
        return reference_hz.get_value() *
            (gate == 4 ? calibration_ratio.get_value() : words_[4] & 0xfff);
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset > 0x3c || offset == 8 || offset == 0xc) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& word = words_[offset / 4];
        if (tx.is_read()) {
            auto value = word;
            if (offset == 0 && running_) {
                if (sc_core::sc_time_stamp() >= lock_at_) value |= 0xc0000000;
                if (sc_core::sc_time_stamp() < calibration_until_) value |= 0x200;
            }
            clock_detail::store(tx, value);
        } else {
            auto value = clock_detail::load(tx);
            if (offset == 0) value &= ~std::uint32_t{0xc0000200};
            if ((offset == 0 && (value & ~0x1c9du)) || (offset == 4 && value > 1)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            const auto changed = word ^ value;
            word = value;
            const bool run = (words_[0] & 0x404) == 0x404 &&
                (words_[4] & 0xfff) && words_[1] == 1;
            if (run && (!running_ || (offset == 0x10 && changed))) {
                lock_at_ = sc_core::sc_time_stamp() + sc_core::sc_time(5, sc_core::SC_US);
                lock_event_.cancel();
                lock_event_.notify(lock_at_ - sc_core::sc_time_stamp());
            }
            running_ = run;
            if (!run) {
                calibration_until_ = sc_core::SC_ZERO_TIME;
                lock_event_.cancel();
            } else if (offset == 0 && (changed & value & 0x800)) {
                // Virtual calibration completes after the firmware's one-microsecond request pulse.
                calibration_until_ = sc_core::sc_time_stamp() + sc_core::sc_time(1, sc_core::SC_US);
            }
            state_changed.notify(sc_core::SC_ZERO_TIME);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"

namespace qcom_vp {

// One REFGEN NXGEN block. XBL 0x1484ee78..0x1484eef0 defines this register subset.
class QcomRefgen : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomRefgen> target_socket{"target_socket"};
    cci::cci_param<bool> virtual_power{
        "virtual_power", false, "Declare a continuously powered virtual analog rail"};
    cci::cci_param<std::string> calibration_policy{
        "calibration_policy", "disabled", "Calibration substitute: disabled or bounded-virtual"};
    cci::cci_param<unsigned> calibration_delay_us{
        "calibration_delay_us", 5, "Virtual calibration delay in microseconds, not silicon timing"};

    explicit QcomRefgen(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomRefgen::transport);
    }

    // Cold reset clears the virtual calibration and the retained controls.
    void cold_reset() {
        tune_ = 0;
        ctrl1_ = 0;
        ctrl2_ = 0;
        state_ = State::untuned;
        ready_at_ = sc_core::SC_ZERO_TIME;
    }

private:
    enum class State { untuned, calibrating, ready };
    State state_ = State::untuned;
    std::uint32_t tune_ = 0;
    std::uint32_t ctrl1_ = 0;
    std::uint32_t ctrl2_ = 0;
    bool powered_ = false;
    bool virtual_calibration_ = false;
    sc_core::sc_time calibration_delay_ = sc_core::SC_ZERO_TIME;
    sc_core::sc_time ready_at_ = sc_core::SC_ZERO_TIME;

    void end_of_elaboration() override {
        const auto& policy = calibration_policy.get_value();
        if (policy != "disabled" && policy != "bounded-virtual")
            throw std::invalid_argument("REFGEN calibration policy must be disabled or bounded-virtual");
        if (!calibration_delay_us.get_value() || calibration_delay_us.get_value() > 1000)
            throw std::invalid_argument("REFGEN calibration delay must be in 1..1000 microseconds");
        // The platform declares fixed power and calibration policy before elaboration.
        powered_ = virtual_power.get_value();
        virtual_calibration_ = policy == "bounded-virtual";
        calibration_delay_ = sc_core::sc_time(calibration_delay_us.get_value(), sc_core::SC_US);
    }

    void refresh() {
        if (state_ == State::calibrating && sc_core::sc_time_stamp() >= ready_at_)
            state_ = State::ready;
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset != 0x44 && offset != 0x5c && offset != 0x60 && offset != 0xa8) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        refresh();
        if (tx.is_read()) {
            const auto value = offset == 0x44 ? (state_ == State::ready ? 0x80u : 0u) :
                offset == 0x5c ? ctrl1_ : offset == 0x60 ? ctrl2_ : tune_;
            clock_detail::store(tx, value);
        } else {
            const auto value = clock_detail::load(tx);
            if (offset == 0x44 || (offset == 0xa8 && value != 0xf) ||
                (offset == 0x5c && value != 8) || (offset == 0x60 && value != 3)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (offset == 0xa8) {
                tune_ = value;
                if (state_ == State::untuned && powered_ && virtual_calibration_) {
                    // This transition replaces analog training. It does not simulate it.
                    state_ = State::calibrating;
                    ready_at_ = sc_core::sc_time_stamp() + calibration_delay_;
                }
            } else if (offset == 0x5c) {
                // Retain hardware-pin vote selection. External vote pins are not modeled.
                ctrl1_ = value;
            } else {
                // Retain the input clamp selection. Rail collapse is not modeled.
                ctrl2_ = value;
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

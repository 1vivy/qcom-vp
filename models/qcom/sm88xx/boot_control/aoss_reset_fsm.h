// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <cstdint>

namespace qcom_vp {

// Functional startup advances POR to IDLE without a guessed silicon delay.
class QcomAossResetFsm : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomAossResetFsm> control_socket{"control_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomAossResetFsm> status_socket{"status_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomAossResetFsm> debug_enable_socket{"debug_enable_socket"};

    SC_HAS_PROCESS(QcomAossResetFsm);
    explicit QcomAossResetFsm(sc_core::sc_module_name name) : sc_module(name) {
        control_socket.register_b_transport(this, &QcomAossResetFsm::control);
        status_socket.register_b_transport(this, &QcomAossResetFsm::status);
        debug_enable_socket.register_b_transport(this, &QcomAossResetFsm::debug_enable);
        SC_METHOD(complete_startup);
        sensitive << startup_completion_;
        dont_initialize();
    }

    void cold_reset() {
        startup_completion_.cancel();
        control_ = 0;
        debug_enable_ = 0;
        state_ = State::por;
        reset_delta_ = sc_core::sc_delta_count();
        startup_completion_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    enum class State : std::uint32_t { por = 0, idle = 1 };
    std::uint32_t control_ = 0;
    std::uint32_t debug_enable_ = 0;
    State state_ = State::por;
    std::uint64_t reset_delta_ = 0;
    sc_core::sc_event startup_completion_;

    void start_of_simulation() override { cold_reset(); }

    void complete_startup() {
        // A reset can occur after this method becomes runnable in the same delta.
        if (sc_core::sc_delta_count() == reset_delta_) {
            startup_completion_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        state_ = State::idle;
    }

    static bool valid(tlm::tlm_generic_payload& tx) {
        if (!clock_detail::valid(tx)) return false;
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    void control(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.is_write()) control_ = clock_detail::load(tx);
        else clock_detail::store(tx, control_);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void debug_enable(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.is_write()) debug_enable_ = clock_detail::load(tx);
        else clock_detail::store(tx, debug_enable_);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void status(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        // FIRST_PASS and all other status flags remain zero in the cold profile.
        clock_detail::store(tx, static_cast<std::uint32_t>(state_));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

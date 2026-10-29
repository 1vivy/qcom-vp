// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef QCOM_VP_MPM_PS_HOLD_H
#define QCOM_VP_MPM_PS_HOLD_H

#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// MPM_PS_HOLD drives the PMIC input. The PMIC selects reset versus shutdown.
class MpmPsHold : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<MpmPsHold> target_socket{"target_socket"};
    InitiatorSignalSocket<bool> ps_hold{"ps_hold"};
    cci::cci_param<unsigned> initial_hold{
        "initial_hold", 2, "Required initial PS_HOLD level: 0 or 1"};

    explicit MpmPsHold(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &MpmPsHold::transport);
    }

private:
    bool held_ = false;

    void end_of_elaboration() override {
        if (initial_hold.get_value() > 1) {
            throw std::invalid_argument("MPM_PS_HOLD requires an explicit initial level of 0 or 1");
        }
        held_ = initial_hold.get_value() != 0;
        if (ps_hold.size() != 1) {
            throw std::invalid_argument("MPM_PS_HOLD requires a power consumer");
        }
    }

    void start_of_simulation() override { ps_hold->write(held_); }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* data = tx.get_data_ptr();
        if (tx.is_write()) {
            const auto value = std::uint32_t{data[0]} | (std::uint32_t{data[1]} << 8) |
                (std::uint32_t{data[2]} << 16) | (std::uint32_t{data[3]} << 24);
            if (value & ~std::uint32_t{1}) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            const bool held = value != 0;
            if (held != held_) {
                held_ = held;
                ps_hold->write(held_);
            }
        } else {
            data[0] = held_ ? 1 : 0;
            data[1] = data[2] = data[3] = 0;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp
#endif

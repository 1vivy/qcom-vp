// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstdint>

namespace qcom_vp {

// APSS 0x30725c enables bit 0 at 0x01fd5054 before it starts the PHY.
// The upstream reference is always available by explicit virtual policy.
class QcomTcsrClockReference : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomTcsrClockReference> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<QcomTcsrClockReference> clock_socket{"clock_socket"};

    explicit QcomTcsrClockReference(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomTcsrClockReference::transport);
    }

private:
    bool enabled_ = false;

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        tx.set_dmi_allowed(false);
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* bytes = tx.get_data_ptr();
        if (tx.is_write()) {
            const auto value = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
                (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
            if (value > 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if ((value != 0) != enabled_) {
                tlm::tlm_generic_payload clock;
                clock.set_command(tlm::TLM_WRITE_COMMAND);
                clock.set_address(0);
                clock.set_data_ptr(bytes);
                clock.set_data_length(4);
                clock.set_streaming_width(4);
                clock_socket->b_transport(clock, delay);
                if (!clock.is_response_ok()) {
                    tx.set_response_status(clock.get_response_status());
                    return;
                }
                enabled_ = value != 0;
            }
        } else if (tx.is_read()) {
            bytes[0] = enabled_ ? 1 : 0;
            bytes[1] = bytes[2] = bytes[3] = 0;
        } else {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

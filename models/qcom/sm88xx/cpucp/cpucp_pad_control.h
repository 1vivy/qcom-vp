// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// ROM 0x335988 modifies mask 0x1c0. This model makes no electrical claim.
class QcomCpucpPadControl : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCpucpPadControl> target_socket{"target_socket"};
    cci::cci_param<std::uint64_t> reset_value{
        "reset_value", UINT64_MAX, "Required virtual initial pad word"};

    explicit QcomCpucpPadControl(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomCpucpPadControl::transport);
    }

private:
    std::uint32_t word_ = 0;

    void end_of_elaboration() override {
        if (reset_value.get_value() > UINT32_MAX) {
            throw std::invalid_argument("CPUCP pad control requires a 32-bit initial word");
        }
        word_ = static_cast<std::uint32_t>(reset_value.get_value());
    }

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
            word_ = std::uint32_t{data[0]} | (std::uint32_t{data[1]} << 8) |
                (std::uint32_t{data[2]} << 16) | (std::uint32_t{data[3]} << 24);
        } else {
            for (unsigned i = 0; i != 4; ++i) {
                data[i] = static_cast<unsigned char>(word_ >> (8 * i));
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstdint>
#include <cci_configuration>
#include <stdexcept>

namespace qcom_vp {

// Relative GDSCR/CFG_GDSCR pair. The consumer must apply power before ACK.
// Register evidence: gcc-canoe.c and gdsc.c at fc30e54174d254ff7f33622a9278e4435f6718d2.
class QcomGdsc : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomGdsc> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket_optional<QcomGdsc> power_socket{"power_socket"};
    cci::cci_param<bool> virtual_supply{"virtual_supply", false,
        "Explicit functional supply for a domain without a peripheral consumer"};
    cci::cci_param<std::uint32_t> retained_mask{"retained_mask", 0,
        "Evidenced GDSCR policy fields with no power-state effect"};
    cci::cci_param<std::uint32_t> cfg_retained_mask{"cfg_retained_mask", 0,
        "Evidenced CFG_GDSCR policy fields with no completion effect"};
    // Linux gdsc-regulator.c poll_gdsc_status() reads PWR_ON (bit 31) from the
    // GDS_HW_CTRL register when a domain declares one. Zero means absent.
    cci::cci_param<std::uint32_t> hw_ctrl_offset{"hw_ctrl_offset", 0,
        "Offset of a read-only GDS_HW_CTRL power status word, zero when absent"};

    explicit QcomGdsc(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomGdsc::transport);
    }

private:
    bool powered_ = false;
    uint32_t controls_ = 1;
    uint32_t cfg_controls_ = 0;

    void end_of_elaboration() override {
        if (virtual_supply.get_value() && power_socket.size())
            throw std::invalid_argument("A GDSC requires either a virtual supply or a consumer");
        if (retained_mask.get_value() & 0x80000001u)
            throw std::invalid_argument("Retained GDSCR fields must exclude collapse and power status");
        if (cfg_retained_mask.get_value() & ((1u << 15) | (1u << 16)))
            throw std::invalid_argument("Retained CFG_GDSCR fields must exclude completion status");
        const auto hw_ctrl = hw_ctrl_offset.get_value();
        if (hw_ctrl && (hw_ctrl < 8 || (hw_ctrl & 3)))
            throw std::invalid_argument("GDS_HW_CTRL must be an aligned word after CFG_GDSCR");
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        tx.set_dmi_allowed(false);
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || tx.get_byte_enable_ptr() ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto hw_ctrl = hw_ctrl_offset.get_value();
        if (hw_ctrl && tx.get_address() == hw_ctrl) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            const uint32_t value = powered_ ? 0x80000000u : 0u;
            for (unsigned i = 0; i != 4; ++i) tx.get_data_ptr()[i] = value >> (8 * i);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (tx.get_address() > 4) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* p = tx.get_data_ptr();
        if (tx.is_write()) {
            const uint32_t value = uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
            if (tx.get_address() == 4) {
                constexpr uint32_t completion = (1u << 15) | (1u << 16);
                if (value & ~(cfg_retained_mask.get_value() | completion)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                cfg_controls_ = value & cfg_retained_mask.get_value();
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
            if (value & ~(uint32_t{0x80000001} | retained_mask.get_value())) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            const bool requested = !(value & 1);
            if (requested != powered_) {
                if (!power_socket.size() && !virtual_supply.get_value()) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                if (power_socket.size()) {
                    unsigned char bytes[4] = {static_cast<unsigned char>(requested), 0, 0, 0};
                    tlm::tlm_generic_payload power;
                    power.set_command(tlm::TLM_WRITE_COMMAND);
                    power.set_address(0);
                    power.set_data_ptr(bytes);
                    power.set_data_length(4);
                    power.set_streaming_width(4);
                    power_socket->b_transport(power, delay);
                    if (!power.is_response_ok()) {
                        tx.set_response_status(power.get_response_status());
                        return;
                    }
                }
                powered_ = requested;
            }
            controls_ = value & (1 | retained_mask.get_value());
        } else {
            const uint32_t value = tx.get_address() == 0 ?
                (controls_ | (powered_ ? 0x80000000u : 0u)) :
                (cfg_controls_ | (powered_ ? 1u << 16 : 1u << 15));
            for (unsigned i = 0; i != 4; ++i) p[i] = value >> (8 * i);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

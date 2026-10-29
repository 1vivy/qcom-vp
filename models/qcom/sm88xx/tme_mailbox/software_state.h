// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>

namespace qcom_vp {

// ROM 0x205208 reads a state bank and writes its update alias after it sets one bit.
class TmeSoftwareState : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<TmeSoftwareState> state_socket{"state_socket"};
    tlm_utils::simple_target_socket<TmeSoftwareState> update_socket{"update_socket"};
    tlm_utils::simple_target_socket<TmeSoftwareState> handoff_socket{"handoff_socket"};
    cci::cci_param<uint32_t> low_reset{"low_reset", 0, "Virtual low software state at reset"};
    cci::cci_param<uint32_t> high_reset{"high_reset", 0, "Virtual high software state at reset"};
    cci::cci_param<uint32_t> handoff_reset{"handoff_reset", 0, "Virtual handoff flags at reset"};

    explicit TmeSoftwareState(sc_core::sc_module_name name) : sc_module(name) {
        state_socket.register_b_transport(this, &TmeSoftwareState::read_state);
        update_socket.register_b_transport(this, &TmeSoftwareState::write_state);
        handoff_socket.register_b_transport(this, &TmeSoftwareState::handoff);
    }

private:
    std::array<uint32_t, 2> words_{};
    uint32_t handoff_flags_ = 0;

    void end_of_elaboration() override {
        words_ = {low_reset.get_value(), high_reset.get_value()};
        handoff_flags_ = handoff_reset.get_value();
    }

    static bool valid(tlm::tlm_generic_payload& tx, bool write) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if (tx.get_address() >= 8) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        if (write ? !tx.is_write() : !tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    void read_state(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, false)) return;
        const auto value = words_[tx.get_address() / 4];
        for (unsigned i = 0; i != 4; ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void write_state(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, true)) return;
        const auto* bytes = tx.get_data_ptr();
        const uint32_t value = uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8) |
            (uint32_t{bytes[2]} << 16) | (uint32_t{bytes[3]} << 24);
        auto& word = words_[tx.get_address() / 4];
        // The observed firmware preserves all set bits. Clearing semantics remain unknown.
        if ((value & word) != word) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        word = value;
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void handoff(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, tx.is_write())) return;
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            const auto* bytes = tx.get_data_ptr();
            const uint32_t value = uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8) |
                (uint32_t{bytes[2]} << 16) | (uint32_t{bytes[3]} << 24);
            // TME 0x2005e8 sets bit 1. APSS 0x30de68 sets bit 2.
            if (((value ^ handoff_flags_) & ~6U) || (value & handoff_flags_) != handoff_flags_) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            handoff_flags_ = value;
        } else {
            for (unsigned i = 0; i != 4; ++i)
                tx.get_data_ptr()[i] = static_cast<unsigned char>(handoff_flags_ >> (8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/clic/cpu.h"
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// OEM low-power configuration and explicit virtual retained-power lifecycle.
// External wake levels are inactive unless a real producer is implemented.
class TmeLowPowerControl : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<TmeLowPowerControl> inhibit_socket{"inhibit_socket"};
    tlm_utils::simple_target_socket<TmeLowPowerControl> vector_socket{"vector_socket"};
    tlm_utils::simple_target_socket<TmeLowPowerControl> diagnostic_socket{"diagnostic_socket"};
    tlm_utils::simple_target_socket_optional<TmeLowPowerControl> request_socket{"request_socket"};
    tlm_utils::simple_target_socket_optional<TmeLowPowerControl> control_socket{"control_socket"};
    tlm_utils::simple_target_socket_optional<TmeLowPowerControl> wake_mask_socket{"wake_mask_socket"};
    tlm_utils::simple_target_socket_optional<TmeLowPowerControl> wake_status_socket{"wake_status_socket"};
    cci::cci_param<std::string> external_wake_policy{"external_wake_policy", "",
        "Required external wake policy: inactive-no-external-producers"};
    cci::cci_param<std::string> policy{
        "policy", "", "Required virtual policy: cold-no-external-inhibit"};

    TmeLowPowerControl(sc_core::sc_module_name name, sc_core::sc_object* cpu)
        : sc_module(name), cpu_(dynamic_cast<TmeClicCpu*>(cpu)) {
        if (!cpu_) throw std::invalid_argument("TME power control requires a TmeClicCpu");
        inhibit_socket.register_b_transport(this, &TmeLowPowerControl::inhibit);
        vector_socket.register_b_transport(this, &TmeLowPowerControl::vector);
        diagnostic_socket.register_b_transport(this, &TmeLowPowerControl::diagnostic);
        request_socket.register_b_transport(this, &TmeLowPowerControl::request);
        control_socket.register_b_transport(this, &TmeLowPowerControl::control);
        wake_mask_socket.register_b_transport(this, &TmeLowPowerControl::wake_mask);
        wake_status_socket.register_b_transport(this, &TmeLowPowerControl::wake_status);
    }

private:
    TmeClicCpu* cpu_;
    std::uint32_t request_ = 0;
    std::uint32_t control_ = 0;
    std::array<std::uint32_t, 4> wake_masks_{};
    bool request_seen_ = false;
    std::uint32_t exit_vector_ = 0;
    std::array<std::uint32_t, 8> diagnostics_{};

    void end_of_elaboration() override {
        if (policy.get_value() != "cold-no-external-inhibit")
            throw std::invalid_argument("TME low-power control requires the cold-no-external-inhibit policy");
        if (!cpu_->retained_power.get_value() ||
            external_wake_policy.get_value() != "inactive-no-external-producers")
            throw std::invalid_argument("TME power control requires retained power and an explicit external wake policy");
    }

    void reject(tlm::tlm_generic_payload& tx, tlm::tlm_response_status status) {
        SC_REPORT_WARNING(name(), "Unsupported TME low-power control access");
        tx.set_response_status(status);
    }

    bool valid(tlm::tlm_generic_payload& tx, std::uint64_t size, bool read_only = false) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            reject(tx, tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || (tx.get_address() & 3)) {
            reject(tx, tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if (tx.get_address() >= size) {
            reject(tx, tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        if ((!tx.is_read() && !tx.is_write()) || (read_only && !tx.is_read())) {
            reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    static void transfer(tlm::tlm_generic_payload& tx, std::uint32_t& word) {
        auto* bytes = tx.get_data_ptr();
        if (tx.is_write()) {
            word = std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8) |
                (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
        } else {
            for (unsigned i = 0; i != 4; ++i)
                bytes[i] = static_cast<unsigned char>(word >> (8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void inhibit(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, 4, true)) return;
        // No external debugger or external inhibit producer is connected.
        // Zero permits the OEM initializer. One would bypass it and is not supplied.
        std::uint32_t inactive = 0;
        transfer(tx, inactive);
    }

    void vector(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, 4)) return;
        if (tx.is_write()) {
            const auto* b = tx.get_data_ptr();
            const auto vector = std::uint32_t{b[0]} | (std::uint32_t{b[1]} << 8) |
                (std::uint32_t{b[2]} << 16) | (std::uint32_t{b[3]} << 24);
            if (vector & 1) {
                reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
        }
        transfer(tx, exit_vector_);
        if (tx.is_write()) {
            cpu_->publish_power_vector(exit_vector_);
            if (!exit_vector_) {
                cpu_->arm_retained_power(false);
                request_seen_ = false;
            }
        }
    }

    void diagnostic(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, 0x20)) return;
        transfer(tx, diagnostics_[tx.get_address() / 4]);
    }

    void request(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, 4)) return;
        transfer(tx, request_);
        if (tx.is_write()) {
            request_seen_ = request_ == 0x21;
            cpu_->arm_retained_power(false);
        }
    }

    void control(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, 4)) return;
        auto next = control_;
        transfer(tx, next);
        if (tx.is_write()) {
            const bool armed = request_seen_ && (next & 0xff) == 0x23;
            if (armed && !exit_vector_) {
                reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            control_ = next;
            cpu_->arm_retained_power(armed);
            request_seen_ = false;
        }
    }

    void wake_mask(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, 0x10)) return;
        transfer(tx, wake_masks_[tx.get_address() / 4]);
    }

    void wake_status(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx, 0x10, true)) return;
        // No external wake producer is connected. Do not mirror CLIC pending state.
        std::uint32_t inactive = 0;
        transfer(tx, inactive);
    }
};

} // namespace qcom_vp

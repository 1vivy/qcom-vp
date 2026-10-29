// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <tlm_utils/simple_target_socket.h>

namespace qcom_vp {

// No host endpoint and no inherited USB session. Guest enable votes and retained
// configuration never create a connection or enumeration.
class QcomDisconnectedUsbDebug : public sc_core::sc_module, public ClockSource {
public:
    tlm_utils::multi_passthrough_target_socket<QcomDisconnectedUsbDebug> enable_socket{"enable_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDisconnectedUsbDebug> enumeration_socket{"enumeration_socket"};
    tlm_utils::simple_target_socket_optional<QcomDisconnectedUsbDebug> attach_socket{"attach_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDisconnectedUsbDebug> controller_socket{"controller_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDisconnectedUsbDebug> detect_socket{"detect_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDisconnectedUsbDebug> power_socket{"power_socket"};
    cci::cci_param<bool> disconnected{"disconnected", false, "Require an explicit no-host virtual USB policy"};

    explicit QcomDisconnectedUsbDebug(sc_core::sc_module_name name) : sc_module(name) {
        enable_socket.register_b_transport(this, &QcomDisconnectedUsbDebug::enable);
        enumeration_socket.register_b_transport(this, &QcomDisconnectedUsbDebug::inactive);
        attach_socket.register_b_transport(this, &QcomDisconnectedUsbDebug::attach);
        controller_socket.register_b_transport(this, &QcomDisconnectedUsbDebug::controller);
        detect_socket.register_b_transport(this, &QcomDisconnectedUsbDebug::detect);
        power_socket.register_b_transport(this, &QcomDisconnectedUsbDebug::power);
    }
    void cold_reset() { enabled_ = 0; detect_request_ = 0; powered_ = false; }
    // No virtual USB PHY supplies the PIPE source.
    bool clock_running() override { return false; }
    const sc_core::sc_event& clock_changed() const override { return no_phy_change_; }

private:
    std::uint32_t enabled_ = 0;
    std::uint32_t detect_request_ = 0;
    bool powered_ = false;
    sc_core::sc_event no_phy_change_;
    void end_of_elaboration() override {
        if (!disconnected.get_value())
            throw std::invalid_argument("USB debug requires the explicit disconnected transport policy");
        cold_reset();
    }
    static bool valid(tlm::tlm_generic_payload& tx) {
        if (!clock_detail::valid(tx)) return false;
        if (tx.get_address()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        return true;
    }
    static void read_only(tlm::tlm_generic_payload& tx, std::uint32_t value) {
        if (!valid(tx)) return;
        if (tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        clock_detail::store(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void inactive(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        read_only(tx, 0);
    }
    void attach(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        read_only(tx, 0);
    }
    void enable(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.is_write()) {
            const auto value = clock_detail::load(tx);
            if (value > 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // XBL applies its security and permission predicates before this
            // protected-register vote. Retaining it does not attach a host.
            enabled_ = value;
        } else {
            clock_detail::store(tx, enabled_);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void controller(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        // DCTL POR with RUN_STOP clear: no controller session was inherited.
        // Starting, resetting, or servicing the USB controller is unsupported.
        if (!valid(tx)) return;
        if (!powered_) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        read_only(tx, 0x00f00000);
    }
    void detect(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.is_write()) {
            // Retain the cold setup request, not a physical cable indication.
            // A falling edge requests EUD recovery/reset, which is unsupported.
            if (clock_detail::load(tx) != 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            detect_request_ = 1;
        } else {
            clock_detail::store(tx, detect_request_);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void power(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (!tx.is_write() || clock_detail::load(tx) > 1) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        // Apply virtual register-domain power. This never starts a USB session.
        powered_ = clock_detail::load(tx) != 0;
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

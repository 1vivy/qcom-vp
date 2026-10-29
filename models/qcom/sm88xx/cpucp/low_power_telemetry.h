// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"

namespace qcom_vp {

// No low-power transition source exists in this continuously awake profile.
class QcomLowPowerTelemetry : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomLowPowerTelemetry> target_socket{"target_socket"};
    explicit QcomLowPowerTelemetry(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomLowPowerTelemetry::transport);
    }
private:
    std::uint32_t event_enable_ = 0;
    std::uint32_t duration_control_ = 0;
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset == 0 || offset == 0x100) {
            auto& word = offset ? duration_control_ : event_enable_;
            if (tx.is_write()) word = clock_detail::load(tx);
            else clock_detail::store(tx, word);
        } else if (offset == 8 || offset == 0x10 || offset == 0x18 ||
                   (offset >= 0x108 && offset <= 0x11c)) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // Enable writes cannot manufacture low-power entries or residence time.
            clock_detail::store(tx, 0);
        } else {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <cstdint>

namespace qcom_vp {

// No display engine or panel supplies events to this owner.
class QcomDsiInterrupts : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomDsiInterrupts> target_socket{"target_socket"};

    explicit QcomDsiInterrupts(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomDsiInterrupts::transport);
    }

    void cold_reset() { enabled_ = 0; }

private:
    // The selected HAL clears these event bits with 0x55115505.
    static constexpr std::uint32_t event_mask = 0x55115505;
    static constexpr std::uint32_t enable_mask = event_mask << 1;
    std::uint32_t enabled_ = 0;

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        if (tx.get_address() != 0x110) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            // W1C event bits cannot create pending events. Reserved bits read zero.
            enabled_ = clock_detail::load(tx) & enable_mask;
        } else {
            clock_detail::store(tx, enabled_);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

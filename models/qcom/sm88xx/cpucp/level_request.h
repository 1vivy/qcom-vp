// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"
#include <array>

namespace qcom_vp {

// Functional level requests replace analog settling. See subsystem_evidence.md.
class QcomLevelRequest : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomLevelRequest> target_socket{"target_socket"};
    explicit QcomLevelRequest(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomLevelRequest::transport);
    }
private:
    std::array<std::uint32_t, 6> configuration_{};
    std::uint32_t command_ = 0;
    std::uint32_t level_ = 0;
    bool enabled_ = false;
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        std::uint32_t value = tx.is_write() ? clock_detail::load(tx) : 0;
        if (offset == 0) {
            if (tx.is_write()) {
                if ((configuration_[0] & 0x11) != 0x11) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                if ((value & 0xffff0000u) == 0x100000u) {
                    if (!enabled_) {
                        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                        return;
                    }
                    level_ = value & 0xffff;
                } else if ((value & ~0x80u) == 0x30000u) {
                    enabled_ = value & 0x80;
                } else {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                command_ = value;
            } else value = command_;
        } else if (offset == 0xe0 || offset == 8) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // PDP 0x813233de reads +0x8 as a voltage limit. No source defines the word,
            // so it reads virtual zero. See subsystem_evidence.md "Level request controllers".
            value = offset == 8 ? 0 : level_;
        } else {
            unsigned index;
            if (offset == 4) index = 0;
            else if (offset == 0xc) index = 1;
            else if (offset == 0x20) index = 2;
            else if (offset == 0x2c) index = 3;
            else if (offset == 0xb0) index = 4;
            else if (offset == 0xb4) index = 5;
            else {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            if (tx.is_write()) configuration_[index] = value;
            else value = configuration_[index];
        }
        if (tx.is_read()) clock_detail::store(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

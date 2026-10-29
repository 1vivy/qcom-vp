// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <atomic>
#include <cstdint>

namespace qcom_vp {
// Selected SmemDxe 0x37d4/0x3834: claim with host+1, read owner, release with zero.
class QcomSmemMutex : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomSmemMutex> target_socket{"target_socket"};
    explicit QcomSmemMutex(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomSmemMutex::transport);
    }
    bool claim_remote() {
        uint32_t expected = 0;
        return owner_.compare_exchange_strong(expected, 20); // SMEM host 19 plus one.
    }
    void release_remote() {
        uint32_t expected = 20;
        owner_.compare_exchange_strong(expected, 0);
    }
private:
    std::atomic<uint32_t> owner_{0};
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            tx.get_address()) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE); return;
        }
        auto* p = tx.get_data_ptr();
        if (tx.is_write()) {
            const uint32_t value = uint32_t{p[0]} | (uint32_t{p[1]} << 8) |
                (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
            if (value) {
                uint32_t expected = 0;
                owner_.compare_exchange_strong(expected, value);
            } else owner_.store(0);
        } else if (tx.is_read()) {
            const auto value = owner_.load();
            for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
        } else {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};
} // namespace qcom_vp

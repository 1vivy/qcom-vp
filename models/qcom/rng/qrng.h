// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "host_entropy.h"
#include <systemc>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace qcom_vp {

// APSS ROM 0x3094a4 enables this engine. ROM 0x3094ec consumes its output.
class QcomRng : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomRng> target_socket{"target_socket"};

    explicit QcomRng(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomRng::transport);
    }

private:
    std::uint32_t control_ = 0;
    std::array<unsigned char, 64> entropy_{};
    std::size_t cursor_ = entropy_.size();

    bool active() const { return (control_ & 3) == 2; }

    void refill() {
        fill_host_entropy(entropy_.data(), entropy_.size());
        cursor_ = 0;
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
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
        auto* data = tx.get_data_ptr();
        const auto offset = tx.get_address();
        std::uint32_t value = 0;
        if (tx.is_write()) {
            value = std::uint32_t{data[0]} | (std::uint32_t{data[1]} << 8) |
                (std::uint32_t{data[2]} << 16) | (std::uint32_t{data[3]} << 24);
            if (offset != 0x14 || (value & ~3U)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            const bool was_active = active();
            control_ = value;
            if (!active()) cursor_ = entropy_.size();
            else if (!was_active) refill();
        } else if (offset == 0x14) {
            value = control_;
        } else if (offset == 0x1000 || offset == 0x3000 || offset == 0x5000 || offset == 0xc000) {
            if (!active() || cursor_ == entropy_.size()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            std::memcpy(data, entropy_.data() + cursor_, 4);
            cursor_ += 4;
            if (cursor_ == entropy_.size()) refill();
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        } else if (offset == 0x1004 || offset == 0x3004 || offset == 0x5004 || offset == 0xc004) {
            value = active() && cursor_ != entropy_.size() ? 1 : 0;
        } else if (offset == 0x100 || offset == 0x120 || offset == 0x124 || offset == 0x208) {
            // Host entropy acquisition is synchronous. A host failure stops the run.
            value = 0;
        } else {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            for (unsigned i = 0; i != 4; ++i) {
                data[i] = static_cast<unsigned char>(value >> (8 * i));
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

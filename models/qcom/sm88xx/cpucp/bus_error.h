// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <array>
#include <cstdint>

namespace qcom_vp {

// Rocket BusErrorUnit register contract. This functional board has no cache
// ECC or TileLink error producer. Unmodeled bus accesses remain fatal elsewhere.
class QcomCpucpBusError : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCpucpBusError> target_socket{"target_socket"};
    InitiatorSignalSocket<bool> local_irq{"local_irq"};
    InitiatorSignalSocket<bool> platform_irq{"platform_irq"};
    explicit QcomCpucpBusError(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomCpucpBusError::transport);
        registers_[2] = 0xee;
    }
private:
    // Cause, captured address, enable, platform mask, accrued, local mask.
    std::array<uint64_t, 6> registers_{};
    void update() {
        for (int port = 0; port < local_irq.size(); ++port)
            local_irq[port]->write((registers_[4] & registers_[5]) != 0);
        for (int port = 0; port < platform_irq.size(); ++port)
            platform_irq[port]->write((registers_[4] & registers_[3]) != 0);
    }
    void start_of_simulation() override { update(); }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const auto width = tx.get_data_length();
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return;
        }
        if (!tx.get_data_ptr() || (width != 4 && width != 8) ||
            tx.get_streaming_width() < width || (tx.get_address() & (width - 1))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE); return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
        }
        const auto offset = tx.get_address();
        if (offset >= 0x30) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        const unsigned index = offset / 8;
        const unsigned shift = (offset & 4) ? 32 : 0;
        if (tx.is_write()) {
            uint64_t value = 0;
            for (unsigned i = 0; i < width; ++i) value |= uint64_t{tx.get_data_ptr()[i]} << (8 * i);
            const uint64_t mask = width == 8 ? UINT64_MAX : uint64_t{0xffffffff} << shift;
            auto& word = registers_[index];
            word = (word & ~mask) | ((value << shift) & mask);
            if (index >= 2) word &= 0xee; // Unsupported event positions are hardwired zero.
            if (index == 0) word &= 7;
            update();
        } else {
            const auto value = registers_[index] >> shift;
            for (unsigned i = 0; i < width; ++i)
                tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

class MailboxDoorbellSink {
public:
    virtual ~MailboxDoorbellSink() = default;
    virtual bool mailbox_doorbell(unsigned endpoint, unsigned channel, uint32_t payload,
        sc_core::sc_time& delay) = 0;
};

namespace mailbox_detail {
// Only aligned, little-endian 32-bit transactions have defined register side effects.
inline bool access(tlm::tlm_generic_payload& tx) {
    tx.set_dmi_allowed(false);
    if (tx.get_byte_enable_ptr()) {
        tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return false;
    }
    if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
        (tx.get_address() & 3)) {
        tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE); return false;
    }
    if (!tx.is_read() && !tx.is_write()) {
        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return false;
    }
    return true;
}
inline uint32_t word(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
        (uint32_t(p[3]) << 24);
}
inline void result(tlm::tlm_generic_payload& tx, bool ok, uint32_t value) {
    if (ok && tx.is_read())
        for (unsigned i = 0; i < 4; ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
    tx.set_response_status(ok ? tlm::TLM_OK_RESPONSE : tlm::TLM_COMMAND_ERROR_RESPONSE);
}
} // namespace mailbox_detail

// TX aperture. Low command words are retained but have no undocumented side effect.
class QcomMailbox : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomMailbox> target_socket{"target_socket"};
    explicit QcomMailbox(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomMailbox::transport);
    }
    bool attach(MailboxDoorbellSink& sink, unsigned endpoint) {
        if (running_ || sink_) return false;
        sink_ = &sink; endpoint_ = endpoint; return true;
    }
private:
    std::array<uint32_t, 128> command_{};
    MailboxDoorbellSink* sink_ = nullptr;
    unsigned endpoint_ = 0;
    bool running_ = false;
    void start_of_simulation() override { running_ = true; }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!mailbox_detail::access(tx)) return;
        const auto address = tx.get_address();
        uint32_t value = mailbox_detail::word(tx.get_data_ptr());
        bool ok = false;
        if (address == 0 && tx.is_read()) {
            value = 0; ok = true; // Explicit virtual IDR. Selected UEFI never reads it.
        } else if (address >= 0x100 && address < 0x300) {
            const auto index = static_cast<unsigned>((address - 0x100) / 4);
            if (tx.is_read()) { value = command_[index]; ok = true; }
            else if (!(index & 1)) { command_[index] = value; ok = true; }
            else {
                // An absent endpoint retains outgoing data but cannot produce completion.
                ok = !sink_ || sink_->mailbox_doorbell(endpoint_, index / 2, value, delay);
                if (ok) command_[index] = value;
            }
        }
        mailbox_detail::result(tx, ok, value);
    }
};

// One physical owner for RX commands, status, W1C, enable state, and the OR-level IRQ.
class QcomMailboxRxBank : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomMailboxRxBank> target_socket{"target_socket"};
    InitiatorSignalSocket<bool> irq{"irq"};
    explicit QcomMailboxRxBank(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomMailboxRxBank::transport);
    }
    bool claim(unsigned channel, MailboxDoorbellSink& owner) {
        if (running_ || channel >= owners_.size() || owners_[channel]) return false;
        owners_[channel] = &owner; return true;
    }
    bool available(unsigned channel, const MailboxDoorbellSink& owner) const {
        return channel < owners_.size() && owners_[channel] == &owner &&
            !(pending_ & (uint64_t{1} << channel));
    }
    bool complete(unsigned channel, const MailboxDoorbellSink& owner, uint32_t payload) {
        if (!available(channel, owner)) return false;
        command_[2 * channel + 1] = payload;
        pending_ |= uint64_t{1} << channel;
        update_irq();
        return true;
    }
private:
    std::array<uint32_t, 128> command_{};
    std::array<MailboxDoorbellSink*, 64> owners_{};
    std::array<uint32_t, 2> map_{};
    uint64_t pending_ = 0, enabled_ = 0;
    bool running_ = false;
    void end_of_elaboration() override {
        if (irq.size() != 1) throw std::invalid_argument("Mailbox RX requires one IRQ consumer");
    }
    void start_of_simulation() override { running_ = true; update_irq(); }
    void update_irq() { irq->write((pending_ & enabled_) != 0); }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!mailbox_detail::access(tx)) return;
        const auto address = tx.get_address();
        uint32_t value = mailbox_detail::word(tx.get_data_ptr());
        bool ok = false;
        if (address == 0 && tx.is_read()) { value = 0; ok = true; }
        else if (address >= 0x100 && address < 0x300 && tx.is_read()) {
            value = command_[(address - 0x100) / 4]; ok = true;
        } else if (address == 0x4000 || address == 0x4004) {
            // MailboxDxe publishes the routing map. Retain the guest word and
            // give it no routing effect, because no channel mapping is recovered.
            auto& word = map_[address == 0x4004 ? 1 : 0];
            if (tx.is_read()) value = word;
            else word = value;
            ok = true;
        } else if (address == 0x4400 || address == 0x4404) {
            if (tx.is_read()) { value = static_cast<uint32_t>(pending_ >> (32 * (address == 0x4404))); ok = true; }
        } else if (address == 0x4800 || address == 0x4804) {
            if (tx.is_write()) {
                pending_ &= ~(uint64_t(value) << (32 * (address == 0x4804)));
                update_irq(); ok = true;
            }
        } else if (address == 0x4c00 || address == 0x4c04) {
            const unsigned shift = address == 0x4c04 ? 32 : 0;
            if (tx.is_read()) value = static_cast<uint32_t>(enabled_ >> shift);
            else {
                enabled_ = (enabled_ & ~(uint64_t{0xffffffff} << shift)) | (uint64_t(value) << shift);
                update_irq();
            }
            ok = true;
        }
        mailbox_detail::result(tx, ok, value);
    }
};
} // namespace qcom_vp

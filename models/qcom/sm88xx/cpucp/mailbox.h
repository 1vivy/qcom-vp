// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// CPUCP hardware register contract. No SCMI headers, responses, or FREE words
// are constructed here. An optional peer is another retained hardware bank.
class QcomCpucpMailbox : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCpucpMailbox> rx_socket{"rx_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpucpMailbox> tx_socket{"tx_socket"};
    tlm_utils::simple_target_socket_optional<QcomCpucpMailbox> auxiliary_tx_socket{"auxiliary_tx_socket"};
    InitiatorSignalSocket<bool> irq{"irq"};
    cci::cci_param<std::string> peer_path{"peer_path", "",
        "Optional virtual same-channel hardware peer. Empty means no remote endpoint"};
    cci::cci_param<std::string> auxiliary_peer_path{"auxiliary_peer_path", "",
        "Optional hardware peer for the second PDP transmitter"};

    explicit QcomCpucpMailbox(sc_core::sc_module_name name) : sc_module(name) {
        rx_socket.register_b_transport(this, &QcomCpucpMailbox::receive_transport);
        tx_socket.register_b_transport(this, &QcomCpucpMailbox::transmit_transport);
        auxiliary_tx_socket.register_b_transport(this, &QcomCpucpMailbox::auxiliary_transport);
    }

private:
    // Explicit virtual geometry: 64 channels and one mapped interrupt group.
    static constexpr uint32_t id = 1u | (64u << 1) | (1u << 8);
    std::array<uint64_t, 64> rx_command_{};
    std::array<uint64_t, 64> tx_command_{};
    std::array<uint64_t, 64> auxiliary_command_{};
    uint64_t pending_ = 0, mapped_ = 0, enabled_ = 0;
    QcomCpucpMailbox* peer_ = nullptr;
    QcomCpucpMailbox* auxiliary_peer_ = nullptr;

    void end_of_elaboration() override {
        if (irq.size() != 1) throw std::invalid_argument("CPUCP mailbox requires one IRQ consumer");
        peer_ = resolve_peer(peer_path.get_value());
        auxiliary_peer_ = resolve_peer(auxiliary_peer_path.get_value());
    }
    QcomCpucpMailbox* resolve_peer(const std::string& path) {
        if (path.empty()) return nullptr;
        auto* peer = dynamic_cast<QcomCpucpMailbox*>(sc_core::sc_find_object(path.c_str()));
        if (!peer && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            peer = dynamic_cast<QcomCpucpMailbox*>(sc_core::sc_find_object(relative.c_str()));
        }
        if (!peer || peer == this)
            throw std::invalid_argument("CPUCP mailbox requires a distinct hardware peer");
        return peer;
    }
    void start_of_simulation() override { update(); }
    void update() { irq->write((pending_ & mapped_ & enabled_) != 0); }
    void arrive(unsigned channel, uint64_t command) {
        rx_command_[channel] = command;
        pending_ |= uint64_t{1} << channel;
        update();
    }
    static bool valid(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        const unsigned width = tx.get_data_length();
        if (tx.get_byte_enable_ptr()) tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        else if (!tx.get_data_ptr() || (width != 4 && width != 8) ||
                 tx.get_streaming_width() < width || (tx.get_address() & (width - 1)))
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        else if (!tx.is_read() && !tx.is_write())
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        else return true;
        return false;
    }
    static uint64_t load(const tlm::tlm_generic_payload& tx) {
        uint64_t value = 0;
        for (unsigned i = 0; i < tx.get_data_length(); ++i)
            value |= uint64_t{tx.get_data_ptr()[i]} << (8 * i);
        return value;
    }
    static void result(tlm::tlm_generic_payload& tx, uint64_t value) {
        if (tx.is_read()) for (unsigned i = 0; i < tx.get_data_length(); ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    static void merge(uint64_t& word, uint64_t value, unsigned shift, unsigned width) {
        const uint64_t mask = width == 8 ? UINT64_MAX : uint64_t{0xffffffff} << shift;
        word = (word & ~mask) | ((value << shift) & mask);
    }
    void transmit_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        transmit(tx, tx_command_, peer_);
    }
    void auxiliary_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        transmit(tx, auxiliary_command_, auxiliary_peer_);
    }
    void transmit(tlm::tlm_generic_payload& tx, std::array<uint64_t, 64>& commands,
                  QcomCpucpMailbox* peer) {
        if (!valid(tx)) return;
        const auto address = tx.get_address();
        if (address == 0 && tx.is_read()) { result(tx, id); return; }
        if (address < 0x100 || address >= 0x300) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        }
        const unsigned channel = (address - 0x100) / 8;
        const unsigned shift = (address & 4) ? 32 : 0;
        if (tx.is_write()) {
            if (!peer && (shift || tx.get_data_length() == 8)) {
                SC_REPORT_WARNING(name(), "CPUCP mailbox TX has no declared remote peer");
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            merge(commands[channel], load(tx), shift, tx.get_data_length());
            // Firmware publishes low first and rings the doorbell with high.
            if (shift || tx.get_data_length() == 8)
                peer->arrive(channel, commands[channel]);
        }
        result(tx, commands[channel] >> shift);
    }
    void receive_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto address = tx.get_address();
        const unsigned shift = (address & 4) ? 32 : 0;
        uint64_t value = load(tx);
        if (address == 0 && tx.is_read()) value = id;
        else if (address >= 0x100 && address < 0x300) {
            // PDP 0x20006384..0x2000639c clears its own RX command words during setup.
            // Receiver writes change stored data only. They never create or clear pending.
            auto& command = rx_command_[(address - 0x100) / 8];
            if (tx.is_write()) merge(command, value, shift, tx.get_data_length());
            value = command >> shift;
        }
        else if (address == 0x4000 || address == 0x4004) {
            if (tx.is_write()) merge(mapped_, value, shift, tx.get_data_length());
            value = mapped_ >> shift;
        } else if ((address == 0x4400 || address == 0x4404) && tx.is_read())
            value = (pending_ & mapped_) >> shift;
        else if ((address == 0x4800 || address == 0x4804) && tx.is_write()) {
            pending_ &= ~(value << shift);
        } else if (address == 0x4c00 || address == 0x4c04) {
            if (tx.is_write()) merge(enabled_, value, shift, tx.get_data_length());
            value = enabled_ >> shift;
        } else { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        update();
        result(tx, value);
    }
};

} // namespace qcom_vp

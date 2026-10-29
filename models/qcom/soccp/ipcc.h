// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace qcom_vp {

class IpccRemotePeer {
public:
    virtual bool consume_doorbell() = 0;
    virtual ~IpccRemotePeer() = default;
};

// One local-client bank. Platform data supplies protocol and BSP index geometry.
// Selected IPCCDxe: 0x2648, 0x26d0, 0x28cc, 0x29b4, 0x2ab8, 0x2c18.
class QcomIpccLocal : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomIpccLocal> target_socket{"target_socket"};
    InitiatorSignalSocket<bool> irq{"irq"};
    cci::cci_param<unsigned> protocol{"protocol", 0, "BSP protocol number"};
    cci::cci_param<unsigned> local_index{"local_index", 0, "BSP local client index"};
    cci::cci_param<unsigned> client_count{"client_count", 0, "BSP client count, at most 32"};
    cci::cci_param<unsigned> fabric_client_count{"fabric_client_count", 0,
        "Owner directory capacity. Zero uses the owner's local client count."};
    cci::cci_param<unsigned> signal_count{"signal_count", 0, "Signals per client, at most 32"};
    cci::cci_param<unsigned> peer_index{"peer_index", 32, "Registered remote index, 32 means absent"};
    cci::cci_param<std::string> peer_path{"peer_path", "", "Optional functional remote module path"};
    cci::cci_param<std::string> fabric_path{"fabric_path", "", "Protocol fabric owner. Empty selects this view."};
    cci::cci_param<std::string> remote_peer_path{"remote_peer_path", "",
        "Optional functional remote that consumes this fabric's doorbells"};
    cci::cci_param<unsigned> remote_peer_index{"remote_peer_index", 32,
        "Remote client index of that peer, 32 means absent"};
    cci::cci_param<unsigned> remote_signal{"remote_signal", 0,
        "Signal used for the doorbell and its reply"};

    explicit QcomIpccLocal(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomIpccLocal::transport);
    }

    void receive(unsigned client, unsigned signal) {
        if (client != peer_index.get_value() || !peer_ || signal != 0)
            throw std::logic_error("IPCC signal from an unregistered remote");
        pending_[client] |= 1u << signal;
        update_irq();
    }

    void attach_remote(unsigned remote, unsigned local, unsigned signal, IpccRemotePeer& peer) {
        if (fabric_ != this || remote >= capacity() || local >= capacity() ||
            signal >= signal_count.get_value() || views_[remote] || !views_[local] ||
            remote == peer_index.get_value() || remotes_[remote].peer)
            throw std::invalid_argument("IPCC remote requires an unused client and an existing local view");
        remotes_[remote] = {&peer, views_[local], signal};
    }

    void notify_remote(unsigned remote) {
        if (remote >= capacity() || !remotes_[remote].peer)
            throw std::logic_error("IPCC notification requires a registered remote");
        auto& endpoint = remotes_[remote];
        endpoint.local->pending_[remote] |= 1u << endpoint.signal;
        endpoint.local->update_irq();
    }

    bool has_remote(unsigned remote) const {
        return remote < remotes_.size() && remotes_[remote].peer;
    }

private:
    std::array<uint32_t, 32> enabled_{};
    std::array<uint32_t, 32> pending_{};
    std::array<uint32_t, 32> outgoing_{};
    uint32_t config_ = 0;
    IpccRemotePeer* peer_ = nullptr;
    // One local view owns the protocol's endpoint directory. Every send, including
    // loopback, uses this directory and supplies the sender's local BSP index.
    std::array<QcomIpccLocal*, 32> views_{};
    QcomIpccLocal* fabric_ = this;
    struct Remote {
        IpccRemotePeer* peer = nullptr;
        QcomIpccLocal* local = nullptr;
        unsigned signal = 0;
    };
    std::array<Remote, 32> remotes_{};

    void end_of_elaboration() override {
        if (!client_count.get_value() || client_count.get_value() > 32 ||
            !signal_count.get_value() || signal_count.get_value() > 32 ||
            local_index.get_value() >= client_count.get_value() || irq.size() != 1)
            throw std::invalid_argument("IPCC requires valid geometry and one IRQ consumer");
        const auto& path = peer_path.get_value();
        if (!path.empty()) {
            peer_ = dynamic_cast<IpccRemotePeer*>(sc_core::sc_find_object(path.c_str()));
            if (!peer_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                peer_ = dynamic_cast<IpccRemotePeer*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!peer_ || peer_index.get_value() >= client_count.get_value() ||
                peer_index.get_value() == local_index.get_value())
                throw std::invalid_argument("IPCC requires a valid remote peer");
        } else if (peer_index.get_value() != 32) {
            throw std::invalid_argument("IPCC remote index requires a peer path");
        }
        const auto& fabric_path_value = fabric_path.get_value();
        if (fabric_path_value.empty() &&
            (capacity() < client_count.get_value() || capacity() > views_.size()))
            throw std::invalid_argument("IPCC fabric capacity must contain its local namespace");
        if (!fabric_path_value.empty()) {
            fabric_ = dynamic_cast<QcomIpccLocal*>(sc_core::sc_find_object(fabric_path_value.c_str()));
            if (!fabric_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + fabric_path_value;
                fabric_ = dynamic_cast<QcomIpccLocal*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!fabric_ || fabric_ == this || !fabric_->fabric_path.get_value().empty() ||
                fabric_->protocol.get_value() != protocol.get_value() ||
                fabric_client_count.get_value() != 0 ||
                client_count.get_value() > fabric_->capacity() ||
                fabric_->signal_count.get_value() != signal_count.get_value() || peer_)
                throw std::invalid_argument("IPCC local views require one matching protocol fabric");
        }
        auto& slot = fabric_->views_[local_index.get_value()];
        if (slot) throw std::invalid_argument("IPCC protocol has duplicate local clients");
        slot = this;
        const auto& remote_path = remote_peer_path.get_value();
        if (remote_path.empty()) {
            if (remote_peer_index.get_value() != 32)
                throw std::invalid_argument("IPCC remote index requires a peer path");
            return;
        }
        auto* object = sc_core::sc_find_object(remote_path.c_str());
        if (!object && get_parent_object())
            object = sc_core::sc_find_object(
                (std::string(get_parent_object()->name()) + "." + remote_path).c_str());
        auto* remote = dynamic_cast<IpccRemotePeer*>(object);
        if (!remote || remote_peer_index.get_value() >= capacity())
            throw std::invalid_argument("IPCC remote peer is missing or out of range");
        attach_remote(remote_peer_index.get_value(), local_index.get_value(),
            remote_signal.get_value(), *remote);
    }
    void start_of_simulation() override {
        if (fabric_ == this && peer_ && views_[peer_index.get_value()])
            throw std::invalid_argument("IPCC remote client also has a local view");
        update_irq();
    }
    unsigned capacity() const {
        const auto count = fabric_client_count.get_value();
        return count ? count : client_count.get_value();
    }
    bool send(QcomIpccLocal& source, unsigned destination, unsigned signal) {
        if (auto* receiver = views_[destination]) {
            if (source.local_index.get_value() >= receiver->client_count.get_value()) {
                source.outgoing_[destination] |= 1u << signal;
                return true;
            }
            receiver->pending_[source.local_index.get_value()] |= 1u << signal;
            receiver->update_irq();
            return true;
        }
        auto& remote = remotes_[destination];
        if (remote.peer && remote.local == &source && remote.signal == signal)
            return remote.peer->consume_doorbell();
        source.outgoing_[destination] |= 1u << signal;
        // The SOCCP service owns the original AP view's queue, not another
        // client's queue. A secure sender cannot manufacture an AP response.
        if (&source == this && destination == peer_index.get_value() && signal == 0 && peer_) {
            if (!peer_->consume_doorbell()) return false;
            source.outgoing_[destination] &= ~(1u << signal);
        }
        return true;
    }
    void update_irq() {
        bool level = false;
        if (!(config_ & 0x80000000u)) {
            for (unsigned i = 0; i < client_count.get_value(); ++i)
                level = level || (pending_[i] & enabled_[i]);
        }
        irq->write(level);
    }
    bool pair(uint32_t value, unsigned& client, unsigned& signal) const {
        client = value >> 16;
        signal = value & 0xffff;
        return client < client_count.get_value() && signal < signal_count.get_value();
    }
    bool read(uint64_t offset, uint32_t& value) {
        switch (offset) {
        case 0: value = 0x00030000; return true; // Explicit virtual v3 identity.
        case 4: value = (local_index.get_value() << 16) | protocol.get_value(); return true;
        case 8: value = config_; return true;
        case 0x48:
            value = 0xffffffff;
            if (config_ & 0x80000000u) return true;
            for (unsigned c = 0; c < client_count.get_value(); ++c) {
                const auto bits = pending_[c] & enabled_[c];
                if (!bits) continue;
                unsigned s = 0;
                while (!(bits & (1u << s))) ++s;
                value = (c << 16) | s;
                if (config_ & 1) {
                    pending_[c] &= ~(1u << s);
                    update_irq();
                }
                break;
            }
            return true;
        default:
            if (offset >= 0x600 && offset < 0x600 + 4 * client_count.get_value()) {
                value = pending_[(offset - 0x600) / 4]; return true;
            }
            if (offset >= 0x800 && offset < 0x800 + 4 * client_count.get_value()) {
                value = enabled_[(offset - 0x800) / 4]; return true;
            }
            return false;
        }
    }
    bool write(uint64_t offset, uint32_t value) {
        unsigned client = 0, signal = 0;
        switch (offset) {
        case 8:
            if (value & ~0x80000001u) return false;
            config_ = value; break;
        case 0x38:
            if (value & ~1u) return false;
            if (value) pending_.fill(0);
            break;
        case 0x44:
            if (value & ~0xffffu) return false;
            value = ((value >> 8) << 16) | (value & 0xff);
            [[fallthrough]];
        case 0xc:
            if ((config_ & 0x80000000u) || !pair(value, client, signal)) return false;
            if (!fabric_->send(*this, client, signal)) return false;
            break;
        case 0x14: case 0x18: case 0x1c:
            if (!pair(value, client, signal)) return false;
            if (offset == 0x14) enabled_[client] |= 1u << signal;
            else if (offset == 0x18) enabled_[client] &= ~(1u << signal);
            else pending_[client] &= ~(1u << signal);
            break;
        default: {
            const uint32_t mask = signal_count.get_value() == 32 ? 0xffffffffu :
                (1u << signal_count.get_value()) - 1;
            if (value & ~mask) return false;
            if (offset >= 0x200 && offset < 0x200 + 4 * client_count.get_value())
                enabled_[(offset - 0x200) / 4] |= value;
            else if (offset >= 0x400 && offset < 0x400 + 4 * client_count.get_value())
                enabled_[(offset - 0x400) / 4] &= ~value;
            else return false;
        }
        }
        update_irq();
        return true;
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE); return;
        }
        auto* p = tx.get_data_ptr();
        uint32_t value = uint32_t{p[0]} | (uint32_t{p[1]} << 8) |
            (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
        const bool ok = tx.is_write() ? write(tx.get_address(), value) :
            tx.is_read() && read(tx.get_address(), value);
        if (ok && tx.is_read())
            for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(ok ? tlm::TLM_OK_RESPONSE : tlm::TLM_COMMAND_ERROR_RESPONSE);
    }
};
} // namespace qcom_vp

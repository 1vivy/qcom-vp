// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <iostream>

namespace qcom_vp {

// OEM TMEC config 0x447564 selects this split register layout.
// Priorities use the functional CLIC policy: larger values win, then larger IDs.
class TmeClic : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket_tagged_optional<TmeClic> pending_socket{"pending_socket"};
    tlm_utils::simple_target_socket_tagged_optional<TmeClic> enable_socket{"enable_socket"};
    tlm_utils::simple_target_socket_tagged_optional<TmeClic> priority_socket{"priority_socket"};
    tlm_utils::simple_target_socket_tagged_optional<TmeClic> threshold_socket{"threshold_socket"};
    tlm_utils::simple_target_socket_tagged_optional<TmeClic> source_socket{"source_socket"};
    // Virtual APSS QMP notification input. The guest clears its latch through
    // the existing TMEC W1C bank, not through a host mailbox peer.
    tlm_utils::simple_target_socket_tagged_optional<TmeClic> qmp_notify_socket{"qmp_notify_socket"};
    sc_core::sc_vector<TargetSignalSocket<bool>> source{"source", 65};
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq{"irq", 65};
    // Outgoing peer notifications. Command bit zero drives peer_notify_0.
    // Command bit one drives peer_notify_1. These outputs never feed the
    // local receive latch.
    sc_core::sc_vector<InitiatorSignalSocket<bool>> peer_notify{"peer_notify", 2};
    cci::cci_param<unsigned> priority_reset{"priority_reset", 0,
        "Explicit board reset priority, not a captured silicon value"};

    SC_HAS_PROCESS(TmeClic);
    explicit TmeClic(sc_core::sc_module_name name) : sc_module(name) {
        pending_socket.register_b_transport(this, &TmeClic::transport, 0);
        enable_socket.register_b_transport(this, &TmeClic::transport, 1);
        priority_socket.register_b_transport(this, &TmeClic::transport, 2);
        threshold_socket.register_b_transport(this, &TmeClic::transport, 3);
        source_socket.register_b_transport(this, &TmeClic::transport, 4);
        qmp_notify_socket.register_b_transport(this, &TmeClic::transport, 5);
        SC_METHOD(clear_peer_notifications);
        sensitive << peer_clear_;
        dont_initialize();
        for (unsigned i = 0; i < source.size(); ++i) {
            source[i].register_value_changed_cb([this, i](const bool& value) {
                level_[i] = value;
                update();
            });
        }
    }

private:
    std::array<bool, 65> level_{};
    std::array<uint8_t, 65> enabled_{};
    std::array<uint8_t, 65> priority_{};
    uint8_t threshold_ = 0;
    uint64_t software_ = 0;
    uint64_t source_enable_ = 0;
    int winner_ = -1;
    unsigned peer_active_ = 0;
    std::array<uint64_t, 2> peer_delta_{};
    sc_core::sc_event peer_clear_;
    static constexpr uint64_t external_mask = (uint64_t{1} << 44) - 1;

    void end_of_elaboration() override {
        if (priority_reset.get_value() > 255)
            throw std::invalid_argument("CLIC reset priority must fit in one byte");
        priority_.fill(priority_reset.get_value());
        // Every selected ID needs an architectural consumer, including ID 64.
        for (auto& output : irq) {
            if (!output.size())
                throw std::invalid_argument("CLIC requires all 65 CPU interrupt bindings");
        }
    }
    void start_of_simulation() override { update(); }

    // Each bound output pulses high for one delta, then returns low.
    void notify_peer(unsigned command) {
        unsigned unrouted = 0;
        for (unsigned i = 0; i < peer_notify.size(); ++i) {
            if (!(command & (1U << i))) continue;
            if (!peer_notify[i].size()) {
                unrouted |= 1U << i;
                continue;
            }
            peer_active_ |= 1U << i;
            peer_delta_[i] = sc_core::sc_delta_count();
            for (int p = 0; p < peer_notify[i].size(); ++p)
                peer_notify[i][p]->write(true);
        }
        if (unrouted)
            std::cerr << "TME_PEER_NOTIFICATION model=" << name()
                      << " command=" << command << " unrouted=" << unrouted << '\n';
        if (peer_active_) peer_clear_.notify(sc_core::SC_ZERO_TIME);
    }
    void clear_peer_notifications() {
        for (unsigned i = 0; i < peer_notify.size(); ++i) {
            if (!(peer_active_ & (1U << i))) continue;
            if (peer_delta_[i] == sc_core::sc_delta_count()) continue;
            for (int p = 0; p < peer_notify[i].size(); ++p)
                peer_notify[i][p]->write(false);
            peer_active_ &= ~(1U << i);
        }
        if (peer_active_) peer_clear_.notify(sc_core::SC_ZERO_TIME);
    }
    uint64_t raw() const {
        uint64_t bits = software_;
        for (unsigned i = 21; i < level_.size(); ++i)
            if (level_[i]) bits |= uint64_t{1} << (i - 21);
        return bits;
    }
    void update() {
        int next = -1;
        const auto external = raw() & source_enable_;
        for (unsigned i = 0; i < enabled_.size(); ++i) {
            const bool active = i < 21 ? level_[i] :
                (external & (uint64_t{1} << (i - 21))) != 0;
            if (active && enabled_[i] && priority_[i] > threshold_ &&
                (next < 0 || priority_[i] >= priority_[next])) next = i;
        }
        if (next == winner_) return;
        if (winner_ >= 0)
            for (int p = 0; p < irq[winner_].size(); ++p) irq[winner_][p]->write(false);
        winner_ = next;
        if (winner_ >= 0)
            for (int p = 0; p < irq[winner_].size(); ++p) irq[winner_][p]->write(true);
    }
    static uint32_t load(const unsigned char* data) {
        return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
            (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
    }
    static void store(unsigned char* data, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) data[i] = value >> (8 * i);
    }
    void transport(int bank, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        const unsigned width = bank == 5 || (bank < 2 && tx.get_data_length() == 4) ||
            (bank == 4 && tx.get_address() != 0) ? 4 : 1;
        if (!tx.get_data_ptr() || tx.get_data_length() != width ||
            tx.get_streaming_width() < width || (tx.get_address() & (width - 1))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto offset = tx.get_address();
        if (bank == 5) {
            if (offset != 0) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            // QSEE writes one. Zero has no effect. Reject unsupported command
            // bits and reads rather than invent a status or response register.
            if (!tx.is_write() || load(tx.get_data_ptr()) > 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (load(tx.get_data_ptr()))
                software_ |= uint64_t{1} << (28 - 21);
            update();
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (bank < 4) {
            const unsigned size = bank == 3 ? 1U : 65U;
            if (offset >= size || width > size - offset) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            if (bank == 0) {
                if (tx.is_write()) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                const auto external = offset + width > 21 ? raw() & source_enable_ : 0;
                for (unsigned i = 0; i < width; ++i) {
                    const auto id = offset + i;
                    tx.get_data_ptr()[i] = id < 21 ? level_[id] :
                        (external & (uint64_t{1} << (id - 21))) != 0;
                }
            } else {
                const bool writing = tx.is_write();
                if (writing && bank == 1) {
                    for (unsigned i = 0; i < width; ++i) {
                        if (tx.get_data_ptr()[i] > 1) {
                            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                            return;
                        }
                    }
                }
                for (unsigned i = 0; i < width; ++i) {
                    auto& reg = bank == 1 ? enabled_[offset + i] :
                        bank == 2 ? priority_[offset + i] : threshold_;
                    if (writing) reg = tx.get_data_ptr()[i];
                    else tx.get_data_ptr()[i] = reg;
                }
                if (writing) update();
            }
        } else {
            if (offset == 0) {
                if (!tx.is_write() || (tx.get_data_ptr()[0] & ~3U)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                // TMEC pending_ids maps values 1 and 2 to peer signals 28 and 33.
                // These are outgoing notifications, not local pending sources.
                const unsigned command = tx.get_data_ptr()[0];
                if (command) notify_peer(command);
            } else if (offset == 8 || offset == 12) {
                if (!tx.is_write()) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                // W1C cannot suppress a source whose input remains asserted.
                software_ &= ~(uint64_t(load(tx.get_data_ptr())) << ((offset - 8) * 8));
            } else if (offset == 16 || offset == 20) {
                if (!tx.is_read()) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                store(tx.get_data_ptr(), raw() >> ((offset - 16) * 8));
            } else if (offset == 24 || offset == 28) {
                const unsigned shift = (offset - 24) * 8;
                if (tx.is_write()) {
                    const uint64_t value = uint64_t(load(tx.get_data_ptr())) << shift;
                    if (value & ~external_mask) {
                        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                        return;
                    }
                    source_enable_ = (source_enable_ & ~(uint64_t{0xffffffff} << shift)) | value;
                } else store(tx.get_data_ptr(), source_enable_ >> shift);
            } else {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            update();
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

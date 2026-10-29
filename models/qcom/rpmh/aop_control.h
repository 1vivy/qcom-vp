// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"

namespace qcom_vp {

// Relative registers for the AOP transfer controller. Requests do not authorize images.
class QcomAopControl : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomAopControl> target_socket{"target_socket"};

    explicit QcomAopControl(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomAopControl::transport);
    }
    void cold_reset() {
        words_.fill(0);
        status_ = 0;
    }

private:
    static constexpr std::array<std::uint32_t, 14> offsets_{
        0x10, 0x18, 0x1c, 0x40, 0x60, 0x64, 0x68, 0x6c, 0x70, 0x74,
        0x400, 0x410, 0x404, 0x414};
    std::array<std::uint32_t, offsets_.size()> words_{};
    std::uint32_t status_ = 0;

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset == 0x408) {
            if (tx.is_write()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            clock_detail::store(tx, status_);
        } else {
            unsigned index = 0;
            while (index < offsets_.size() && offsets_[index] != offset) ++index;
            if (index == offsets_.size()) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            if (tx.is_write()) {
                const auto value = clock_detail::load(tx);
                words_[index] = value;
                if (value & 1) {
                    if (offset == 0x404) status_ = 0x80000000u;
                    else if (offset == 0x414 && (status_ & 0x80000000u)) status_ = 1;
                }
            } else {
                clock_detail::store(tx, words_[index]);
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// The functional acknowledgment follows the complete retained request word.
class QcomRequestAck : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomRequestAck> target_socket{"target_socket"};

    explicit QcomRequestAck(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomRequestAck::transport);
    }
    void cold_reset() { request_ = 0; }

private:
    std::uint32_t request_ = 0;
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset != 0 && offset != 4) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            if (offset == 4) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            request_ = clock_detail::load(tx);
        } else {
            clock_detail::store(tx, request_);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// Functional Q-channel policy: no unsolicited activity and no denied requests.
class QcomHaltChannel : public sc_core::sc_module, public ClockSource {
public:
    tlm_utils::multi_passthrough_target_socket<QcomHaltChannel> target_socket{"target_socket"};
    cci::cci_param<std::uint32_t> initial_request{"initial_request", 1, "Initial retained request word"};
    cci::cci_param<std::string> source_path{"source_path", "", "Required upstream ClockSource"};
    sc_core::sc_event state_changed{"state_changed"};

    SC_HAS_PROCESS(QcomHaltChannel);
    explicit QcomHaltChannel(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomHaltChannel::transport);
        SC_THREAD(change_thread);
    }
    bool clock_running() override { return (request_ & 1) && source_->clock_running(); }
    const sc_core::sc_event& clock_changed() const override { return state_changed; }
    void cold_reset() {
        request_ = initial_request.get_value();
        update_event_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    ClockSource* source_ = nullptr;
    std::uint32_t request_ = 1;
    sc_core::sc_event update_event_;

    void end_of_elaboration() override {
        const auto& path = source_path.get_value();
        if (!path.empty()) {
            source_ = dynamic_cast<ClockSource*>(sc_core::sc_find_object(path.c_str()));
            if (!source_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                source_ = dynamic_cast<ClockSource*>(sc_core::sc_find_object(relative.c_str()));
            }
        }
        if (!source_) throw std::invalid_argument("A halt channel requires an upstream ClockSource");
        cold_reset();
    }
    void change_thread() {
        const sc_core::sc_event_or_list events = update_event_ | source_->clock_changed();
        for (;;) {
            state_changed.notify(sc_core::SC_ZERO_TIME);
            sc_core::wait(events);
        }
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset > 0xc) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            if (offset != 0xc) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            const auto value = clock_detail::load(tx);
            if (value != request_) {
                request_ = value;
                update_event_.notify(sc_core::SC_ZERO_TIME);
            }
        } else {
            clock_detail::store(tx, offset == 0 || offset == 0xc ? request_ : 0);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "llcc.h"
#include "qcom/apss/execution_group.h"
#include <tlm_utils/simple_initiator_socket.h>
#include <algorithm>
#include <atomic>
#include <memory>

namespace qcom_vp {

class DareMemoryPort : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<DareMemoryPort> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<DareMemoryPort> initiator_socket{"initiator_socket"};
    cci::cci_param<uint64_t> physical_base{"physical_base", 0, "Physical memory base"};
    cci::cci_param<uint64_t> memory_size{"memory_size", 0, "Physical memory size"};

    DareMemoryPort(sc_core::sc_module_name name, sc_core::sc_object* controller, sc_core::sc_object* owner)
        : sc_module(name), controller_(dynamic_cast<QcomLlcc*>(controller)), views_(owner) {
        if (!controller_) throw std::invalid_argument("DARE memory requires an LLCC controller");
        target_socket.register_b_transport(this, &DareMemoryPort::transport);
        target_socket.register_transport_dbg(this, &DareMemoryPort::debug);
        target_socket.register_get_direct_mem_ptr(this, &DareMemoryPort::dmi);
        initiator_socket.register_invalidate_direct_mem_ptr(this, &DareMemoryPort::invalidate);
    }

private:
    QcomLlcc* controller_;
    NativeExecutionViews views_;
    uint64_t base_ = 0;
    uint64_t size_ = 0;
    uint64_t last_ = 0;
    std::shared_ptr<std::atomic<bool>> stopped_ = std::make_shared<std::atomic<bool>>(false);

    void end_of_elaboration() override {
        base_ = physical_base.get_value();
        size_ = memory_size.get_value();
        if (!size_ || size_ - 1 > UINT64_MAX - base_)
            throw std::invalid_argument("DARE memory requires a valid physical range");
        last_ = base_ + size_ - 1;
        controller_->attach_dare_memory(base_, size_, [this](uint64_t first, uint64_t last) { revoke(first, last); });
    }

    void start_of_simulation() override { views_.start(); }
    void end_of_simulation() override { stopped_->store(true); }

    bool in_range(uint64_t address, uint64_t length) const {
        return length && address < size_ && length <= size_ - address;
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        const auto address = tx.get_address();
        const auto length = tx.get_data_length();
        tx.set_dmi_allowed(false);
        if (!in_range(address, length)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if ((!tx.is_read() && !tx.is_write()) || !tx.get_data_ptr()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (tx.get_byte_enable_ptr() && !tx.get_byte_enable_length()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (tx.get_streaming_width() < length) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!controller_->dare_permits(base_ + address, length)) {
            std::fprintf(stderr, "DARE_ACCESS_DENIED model=%s address=0x%llx length=%u operation=%s\n",
                name(), static_cast<unsigned long long>(base_ + address), length, tx.is_write() ? "write" : "read");
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        initiator_socket->b_transport(tx, delay);
        if (tx.is_response_ok() && tx.is_write()) observe_write(tx, base_ + address);
        if (tx.is_dmi_allowed()) {
            auto first = base_;
            auto last = last_;
            if (!controller_->dare_constrain_dmi(base_ + address, first, last)) tx.set_dmi_allowed(false);
        }
    }

    void observe_write(const tlm::tlm_generic_payload& tx, uint64_t address) {
        const auto* enables = tx.get_byte_enable_ptr();
        const auto length = tx.get_data_length();
        if (!enables) {
            controller_->dare_observe_write(address, length);
            return;
        }
        const auto period = tx.get_byte_enable_length();
        for (unsigned i = 0; i < length;) {
            if (enables[i % period] != TLM_BYTE_ENABLED) { ++i; continue; }
            const auto first = i++;
            while (i < length && enables[i % period] == TLM_BYTE_ENABLED) ++i;
            controller_->dare_observe_write(address + first, i - first);
        }
    }

    unsigned debug(int id, tlm::tlm_generic_payload& tx) {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        transport(id, tx, delay);
        return tx.is_response_ok() ? tx.get_data_length() : 0;
    }

    bool dmi(int, tlm::tlm_generic_payload& tx, tlm::tlm_dmi& region) {
        const auto address = tx.get_address();
        if (!in_range(address, 1) || !controller_->dare_permits(base_ + address, 1)) return false;
        auto first = base_;
        auto last = last_;
        if (!controller_->dare_constrain_dmi(base_ + address, first, last) ||
            !initiator_socket->get_direct_mem_ptr(tx, region)) return false;
        const uint64_t original = region.get_start_address();
        first = std::max(first - base_, original);
        last = std::min<uint64_t>(last - base_, region.get_end_address());
        if (first > address || last < address) return false;
        region.set_dmi_ptr(region.get_dmi_ptr() + (first - original));
        region.set_start_address(first);
        region.set_end_address(last);
        return true;
    }

    void invalidate(sc_dt::uint64 first, sc_dt::uint64 last) {
        if (first >= size_ || first > last) return;
        last = std::min<uint64_t>(last, size_ - 1);
        for (unsigned i = 0; i < target_socket.size(); ++i)
            target_socket[i]->invalidate_direct_mem_ptr(first, last);
    }

    void revoke(uint64_t first, uint64_t last) {
        if (last < base_ || first > last_ || sc_core::sc_get_status() != sc_core::SC_RUNNING) return;
        first = std::max(first, base_) - base_;
        last = std::min(last, last_) - base_;
        auto stopped = stopped_;
        // Revoke every native execution view, including sibling-only mappings.
        views_.invalidate(base_ + first, last - first + 1, [this, stopped, first, last] {
            if (stopped->load()) return;
            invalidate(first, last);
        });
    }
};

} // namespace qcom_vp

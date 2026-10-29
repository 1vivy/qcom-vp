// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"
#include <array>

namespace qcom_vp {

// Four independent vote words. Platform data supplies the supported fields.
class QcomClockVoteBank : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomClockVoteBank> target_socket{"target_socket"};
    cci::cci_param<std::uint32_t> mask0{"mask0", 0, "Supported fields at offset 0"};
    cci::cci_param<std::uint32_t> mask1{"mask1", 0, "Supported fields at offset 8"};
    cci::cci_param<std::uint32_t> mask2{"mask2", 0, "Supported fields at offset 16"};
    cci::cci_param<std::uint32_t> mask3{"mask3", 0, "Supported fields at offset 24"};
    sc_core::sc_event state_changed{"state_changed"};

    explicit QcomClockVoteBank(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomClockVoteBank::transport);
    }
    bool supports(std::uint32_t offset, std::uint32_t mask) const {
        return offset <= 24 && !(offset % 8) && mask && !(mask & ~supported(offset / 8));
    }
    bool voted(std::uint32_t offset, std::uint32_t mask) const {
        return supports(offset, mask) && (words_[offset / 8] & mask);
    }
    void cold_reset() {
        words_.fill(0);
        state_changed.notify(sc_core::SC_ZERO_TIME);
    }

private:
    std::array<std::uint32_t, 4> words_{};
    std::uint32_t supported(unsigned index) const {
        switch (index) {
        case 0: return mask0.get_value();
        case 1: return mask1.get_value();
        case 2: return mask2.get_value();
        default: return mask3.get_value();
        }
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset > 24 || offset % 8) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const auto index = static_cast<unsigned>(offset / 8);
        if (tx.is_write()) {
            const auto value = clock_detail::load(tx);
            if (value & ~supported(index)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            words_[index] = value;
            state_changed.notify(sc_core::SC_ZERO_TIME);
        } else {
            clock_detail::store(tx, words_[index]);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// Retained fields for a block without a modeled reset, power, or timing consumer.
// This control has no completion bit and does not create a peripheral.
class QcomRetainedBootControl : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomRetainedBootControl> target_socket{"target_socket"};
    cci::cci_param<std::uint32_t> writable_mask{"writable_mask", 0, "Required retained control fields"};
    cci::cci_param<std::uint32_t> reset_value{"reset_value", 0, "Explicit virtual initial control state"};
    // A block without hardware reports its idle status from a retained enable
    // field. The status bit says that the field is clear, which is the branch
    // CLK_OFF sense of a clock control. It adds no clock, delay, or completion.
    cci::cci_param<std::uint32_t> status_source_mask{
        "status_source_mask", 0, "Enable fields that the status bit reports as clear"};
    cci::cci_param<unsigned> status_bit{
        "status_bit", 0, "Read-only status bit driven by status_source_mask"};
    explicit QcomRetainedBootControl(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomRetainedBootControl::transport);
    }
    void cold_reset() { value_ = reset_value.get_value(); }

private:
    std::uint32_t value_ = 0;
    void end_of_elaboration() override {
        if (reset_value.get_value() & ~writable_mask.get_value())
            throw std::invalid_argument("Retained control reset value must use declared writable fields");
        if (status_source_mask.get_value() &&
            (status_bit.get_value() >= 32 ||
             (writable_mask.get_value() & (std::uint32_t{1} << status_bit.get_value()))))
            throw std::invalid_argument("Retained control status bit must sit outside the writable fields");
        cold_reset();
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        if (tx.get_address()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            auto value = clock_detail::load(tx);
            // A read-modify-write returns the status bit with the control word.
            // Accept it and regenerate it on read; never retain it.
            if (status_source_mask.get_value())
                value &= ~(std::uint32_t{1} << status_bit.get_value());
            if (value & ~writable_mask.get_value()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            value_ = value;
        } else {
            auto value = value_;
            if (status_source_mask.get_value() && !(value_ & status_source_mask.get_value()))
                value |= std::uint32_t{1} << status_bit.get_value();
            clock_detail::store(tx, value);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

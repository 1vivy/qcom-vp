// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_votes.h"
#include <array>

namespace qcom_vp {

// APSS ROM 0x313420 enables the branch. 0x3131a0 checks its status nibble.
// Four fixed vote slots keep every independent owner separate. Any vote keeps
// the branch running. Every request still needs a running source.
class QcomClockBranch : public sc_core::sc_module, public ClockSource {
public:
    static constexpr unsigned vote_slot_count = 4;
    tlm_utils::multi_passthrough_target_socket<QcomClockBranch> target_socket{"target_socket"};
    cci::cci_param<std::string> source_path{"source_path", "", "Required upstream ClockSource"};
    cci::cci_param<bool> hardware_control{"hardware_control", false, "Support HW_CTL bit 1"};
    cci::cci_param<bool> continuous_demand{"continuous_demand", false, "Explicit virtual demand while HW_CTL is set"};
    cci::cci_param<std::string> vote_path_0{"vote_path_0", "", "Optional vote bank for slot 0"};
    cci::cci_param<std::uint32_t> vote_offset_0{"vote_offset_0", 0, "Relative vote word offset for slot 0"};
    cci::cci_param<std::uint32_t> vote_mask_0{"vote_mask_0", 0, "Independent enable votes for slot 0"};
    cci::cci_param<std::string> vote_path_1{"vote_path_1", "", "Optional vote bank for slot 1"};
    cci::cci_param<std::uint32_t> vote_offset_1{"vote_offset_1", 0, "Relative vote word offset for slot 1"};
    cci::cci_param<std::uint32_t> vote_mask_1{"vote_mask_1", 0, "Independent enable votes for slot 1"};
    cci::cci_param<std::string> vote_path_2{"vote_path_2", "", "Optional vote bank for slot 2"};
    cci::cci_param<std::uint32_t> vote_offset_2{"vote_offset_2", 0, "Relative vote word offset for slot 2"};
    cci::cci_param<std::uint32_t> vote_mask_2{"vote_mask_2", 0, "Independent enable votes for slot 2"};
    cci::cci_param<std::string> vote_path_3{"vote_path_3", "", "Optional vote bank for slot 3"};
    cci::cci_param<std::uint32_t> vote_offset_3{"vote_offset_3", 0, "Relative vote word offset for slot 3"};
    cci::cci_param<std::uint32_t> vote_mask_3{"vote_mask_3", 0, "Independent enable votes for slot 3"};
    cci::cci_param<std::uint32_t> retained_mask{"retained_mask", 0, "Explicit retained policy fields with no readiness effect"};
    cci::cci_param<bool> status_on_high{"status_on_high", false,
        "Bit 31 reports CLK_ON for ACGCR instead of CBCR CLK_OFF"};
    sc_core::sc_event state_changed{"state_changed"};

    SC_HAS_PROCESS(QcomClockBranch);
    explicit QcomClockBranch(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomClockBranch::transport);
        SC_THREAD(change_thread);
    }

    bool clock_running() override {
        bool voted = false;
        for (const auto& vote : votes_) {
            if (vote.bank && vote.bank->voted(vote.offset, vote.mask)) {
                voted = true;
                break;
            }
        }
        return ((controls_ & 1) || voted || ((controls_ & 2) && continuous_demand.get_value())) &&
            source_->clock_running();
    }
    const sc_core::sc_event& clock_changed() const override { return state_changed; }
    void cold_reset() {
        controls_ = 0;
        update_event_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    struct Vote {
        QcomClockVoteBank* bank = nullptr;
        std::uint32_t offset = 0;
        std::uint32_t mask = 0;
    };
    struct VoteFields {
        std::string path;
        std::uint32_t offset;
        std::uint32_t mask;
    };
    ClockSource* source_ = nullptr;
    std::array<Vote, vote_slot_count> votes_{};
    std::uint32_t controls_ = 0;
    sc_core::sc_event update_event_;

    VoteFields vote_fields(unsigned slot) const {
        if (slot == 0) return {vote_path_0.get_value(), vote_offset_0.get_value(), vote_mask_0.get_value()};
        if (slot == 1) return {vote_path_1.get_value(), vote_offset_1.get_value(), vote_mask_1.get_value()};
        if (slot == 2) return {vote_path_2.get_value(), vote_offset_2.get_value(), vote_mask_2.get_value()};
        return {vote_path_3.get_value(), vote_offset_3.get_value(), vote_mask_3.get_value()};
    }
    QcomClockVoteBank* resolve_vote_bank(const std::string& path) const {
        auto* bank = dynamic_cast<QcomClockVoteBank*>(sc_core::sc_find_object(path.c_str()));
        if (!bank && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            bank = dynamic_cast<QcomClockVoteBank*>(sc_core::sc_find_object(relative.c_str()));
        }
        return bank;
    }
    void resolve_votes() {
        bool closed = false;
        for (unsigned slot = 0; slot < vote_slot_count; ++slot) {
            const auto fields = vote_fields(slot);
            if (fields.path.empty()) {
                if (fields.offset || fields.mask)
                    throw std::invalid_argument("A branch vote field requires a vote bank");
                closed = true;
                continue;
            }
            if (closed)
                throw std::invalid_argument("Branch vote slots must be contiguous from slot zero");
            auto* bank = resolve_vote_bank(fields.path);
            if (!bank || !bank->supports(fields.offset, fields.mask))
                throw std::invalid_argument("A branch requires a supported independent vote field");
            for (unsigned previous = 0; previous < slot; ++previous) {
                const auto& other = votes_[previous];
                if (other.bank == bank && other.offset == fields.offset &&
                    (other.mask & fields.mask))
                    throw std::invalid_argument("A branch vote field has a duplicate owner");
            }
            votes_[slot] = {bank, fields.offset, fields.mask};
        }
    }

    void end_of_elaboration() override {
        if (retained_mask.get_value() & 0x80000003u)
            throw std::invalid_argument("Retained branch fields must exclude gates and CLK_OFF");
        const auto& path = source_path.get_value();
        if (!path.empty()) {
            source_ = dynamic_cast<ClockSource*>(sc_core::sc_find_object(path.c_str()));
            if (!source_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                source_ = dynamic_cast<ClockSource*>(sc_core::sc_find_object(relative.c_str()));
            }
        }
        if (!source_) throw std::invalid_argument("A branch requires an upstream ClockSource");
        resolve_votes();
    }

    void change_thread() {
        sc_core::sc_event_or_list events = update_event_ | source_->clock_changed();
        for (unsigned slot = 0; slot < vote_slot_count; ++slot) {
            auto* bank = votes_[slot].bank;
            if (!bank) continue;
            bool subscribed = false;
            for (unsigned previous = 0; previous < slot; ++previous) {
                if (votes_[previous].bank == bank) {
                    subscribed = true;
                    break;
                }
            }
            if (!subscribed) events |= bank->state_changed;
        }
        for (;;) {
            state_changed.notify(sc_core::SC_ZERO_TIME);
            sc_core::wait(events);
        }
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        if (tx.get_address()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        constexpr std::uint32_t clock_off = 1u << 31;
        if (tx.is_write()) {
            const auto value = clock_detail::load(tx);
            const std::uint32_t writable = (hardware_control.get_value() ? 3u : 1u) |
                retained_mask.get_value();
            if (value & ~(clock_off | writable)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            controls_ = value & writable;
            update_event_.notify(sc_core::SC_ZERO_TIME);
        } else {
            const bool status = clock_running() == status_on_high.get_value();
            clock_detail::store(tx, controls_ | (status ? clock_off : 0u));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

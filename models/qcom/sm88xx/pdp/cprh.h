// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <tlm_utils/simple_target_socket.h>
#include "qcom/sm88xx/pdp/cprh_target.h"
#include <array>
#include <cstdint>

namespace qcom_vp {

// CPUCP and PDP share each lane's program and configuration words.
// The model abstracts internal sequencer execution. See models/qcom/sm88xx/pdp/evidence.md.
// A power state applies the programmed row to both configured target owners.
// Undeclared status registers fail instead of reporting artificial completion.
class PdpCprh : public sc_core::sc_module {
public:
    static constexpr unsigned power_state_count = 32;
    tlm_utils::simple_target_socket_optional<PdpCprh> pdp_lane0{"pdp_lane0"};
    tlm_utils::simple_target_socket_optional<PdpCprh> pdp_lane1{"pdp_lane1"};
    tlm_utils::simple_target_socket_optional<PdpCprh> cpucp_lane0{"cpucp_lane0"};
    tlm_utils::simple_target_socket_optional<PdpCprh> cpucp_lane1{"cpucp_lane1"};
    cci::cci_param<std::string> lane0_target0_path{"lane0_target0_path", "",
        "Required corner owner for lane zero rail zero"};
    cci::cci_param<std::string> lane0_target1_path{"lane0_target1_path", "",
        "Required corner owner for lane zero rail one"};
    cci::cci_param<std::string> lane1_target0_path{"lane1_target0_path", "",
        "Required corner owner for lane one rail zero"};
    cci::cci_param<std::string> lane1_target1_path{"lane1_target1_path", "",
        "Required corner owner for lane one rail one"};
    explicit PdpCprh(sc_core::sc_module_name name) : sc_module(name) {
        pdp_lane0.register_b_transport(this, &PdpCprh::pdp_lane0_transport);
        pdp_lane1.register_b_transport(this, &PdpCprh::pdp_lane1_transport);
        cpucp_lane0.register_b_transport(this, &PdpCprh::cpucp_lane0_transport);
        cpucp_lane1.register_b_transport(this, &PdpCprh::cpucp_lane1_transport);
    }

    // The state row supplies one corner index for each rail. Rails stay generic:
    // target zero takes the first field pair and target one takes the second.
    bool can_apply_power_state(unsigned lane, unsigned state) const;
    bool apply_power_state(unsigned lane, unsigned state);
    bool power_state_ready(unsigned lane, unsigned state) const;

private:
    struct Lane {
        std::array<std::uint32_t, (0x328 - 0x198) / 4> sequence{};
        std::array<std::uint32_t, 2> initialization{};
        std::array<std::uint32_t, 2> control{};
        std::array<std::uint64_t, 10> rows_0480{};
        std::array<std::uint64_t, 6> rows_05b8{};
        std::array<std::uint64_t, 30> lut{};
        std::array<std::uint64_t, 2> lut_aux{};
        std::array<std::uint64_t, 32> bank_07c0{};
        std::array<std::uint64_t, 32> bank_0950{};
        std::array<std::uint64_t, 2> rows_0b50{};
        std::array<bool, power_state_count> power_rows{};
    };
    std::array<Lane, 2> lanes_{};
    std::array<std::array<CprhTarget*, 2>, 2> target_owners_{};

    static unsigned power_row(std::uint64_t offset) {
        if (offset >= 0x5f0 && offset <= 0x6d8) return static_cast<unsigned>((offset - 0x5f0) / 8);
        if (offset == 0x6e0) return 30;
        if (offset == 0x6e8) return 31;
        return power_state_count;
    }
    static std::uint64_t power_row_word(const Lane& lane, unsigned row) {
        return row < lane.lut.size() ? lane.lut[row] : lane.lut_aux[row - lane.lut.size()];
    }

    using Corners = std::array<unsigned, 2>;
    bool state_programmed(unsigned lane, unsigned state) const {
        return lane < lanes_.size() && state < power_state_count && lanes_[lane].power_rows[state];
    }
    // The CPR fields select target corners. NSI fields describe separate link states.
    Corners state_corners(unsigned lane, unsigned state) const {
        const auto word = power_row_word(lanes_[lane], state);
        const unsigned first = static_cast<unsigned>((word >> 5) & 0x1f);
        const unsigned second = static_cast<unsigned>((word >> 15) & 0x1f);
        return {{first, second}};
    }
    const std::string& target_path(unsigned lane, unsigned target) const {
        if (lane == 0) return target == 0 ? lane0_target0_path.get_value() : lane0_target1_path.get_value();
        return target == 0 ? lane1_target0_path.get_value() : lane1_target1_path.get_value();
    }
    CprhTarget* resolve_target(unsigned lane, unsigned target) const {
        const auto& path = target_path(lane, target);
        if (path.empty()) return nullptr;
        auto* object = sc_core::sc_find_object(path.c_str());
        if (!object && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            object = sc_core::sc_find_object(relative.c_str());
        }
        return dynamic_cast<CprhTarget*>(object);
    }
    void end_of_elaboration() override {
        for (unsigned lane = 0; lane < target_owners_.size(); ++lane)
            for (unsigned target = 0; target < target_owners_[lane].size(); ++target)
                target_owners_[lane][target] = resolve_target(lane, target);
    }
    // Each rail needs its own effect owner. A lane must not share an owner with
    // another lane, because one owner keeps one effective corner.
    bool lane_targets(unsigned lane, CprhTarget*& first, CprhTarget*& second) const {
        first = target_owners_[lane][0];
        second = target_owners_[lane][1];
        if (!first || !second || first == second) return false;
        for (unsigned other = 0; other < lanes_.size(); ++other) {
            if (other == lane) continue;
            for (unsigned target = 0; target < 2; ++target) {
                auto* peer = target_owners_[other][target];
                if (peer && (peer == first || peer == second)) return false;
            }
        }
        return true;
    }

    static bool sequence_word(unsigned offset) {
        return (offset >= 0x198 && offset <= 0x268 && (offset - 0x198) % 8 == 0) ||
            (offset >= 0x1b4 && offset <= 0x324 && (offset - 0x1b4) % 8 == 0);
    }
    static std::uint64_t* wide_word(Lane& lane, unsigned offset) {
        if (offset >= 0x480 && offset <= 0x4c8) return &lane.rows_0480[(offset - 0x480) / 8];
        if (offset >= 0x5b8 && offset <= 0x5e0) return &lane.rows_05b8[(offset - 0x5b8) / 8];
        if (offset >= 0x5f0 && offset <= 0x6d8) return &lane.lut[(offset - 0x5f0) / 8];
        if (offset == 0x6e0 || offset == 0x6e8) return &lane.lut_aux[(offset - 0x6e0) / 8];
        if (offset >= 0x7c0 && offset <= 0x8b8) return &lane.bank_07c0[(offset - 0x7c0) / 8];
        if (offset >= 0x950 && offset <= 0xa48) return &lane.bank_0950[(offset - 0x950) / 8];
        if (offset == 0xb50 || offset == 0xb58) return &lane.rows_0b50[(offset - 0xb50) / 8];
        return nullptr;
    }
    static std::uint64_t load(const tlm::tlm_generic_payload& tx) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < tx.get_data_length(); ++i)
            value |= std::uint64_t{tx.get_data_ptr()[i]} << (8 * i);
        return value;
    }
    static void store(tlm::tlm_generic_payload& tx, std::uint64_t value) {
        for (unsigned i = 0; i < tx.get_data_length(); ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
    }
    void transport(tlm::tlm_generic_payload& tx, unsigned index) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        const auto size = tx.get_data_length();
        if (!tx.get_data_ptr() || (size != 4 && size != 8) ||
            tx.get_streaming_width() < size || (tx.get_address() & (size - 1))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        if (tx.get_address() >= 0x1000) return;
        const auto offset = static_cast<unsigned>(tx.get_address());
        auto& lane = lanes_[index];
        if (offset >= 0x330 && offset <= 0x344) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // Functional power requests finish synchronously.
            // No internal instruction executes between guest accesses.
            store(tx, 0);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (sequence_word(offset)) {
            // Validate both words before an eight-byte write changes either word.
            if (size == 8 && !sequence_word(offset + 4)) return;
            const auto first = (offset - 0x198) / 4;
            if (tx.is_write()) {
                const auto value = load(tx);
                lane.sequence[first] = static_cast<std::uint32_t>(value);
                if (size == 8) lane.sequence[first + 1] = static_cast<std::uint32_t>(value >> 32);
            } else {
                std::uint64_t value = lane.sequence[first];
                if (size == 8) value |= std::uint64_t{lane.sequence[first + 1]} << 32;
                store(tx, value);
            }
        } else if (offset == 0x328 || offset == 0x32c || offset == 0x508 || offset == 0x50c) {
            if (size != 4) {
                tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
                return;
            }
            auto& word = offset < 0x500 ? lane.initialization[(offset - 0x328) / 4] :
                lane.control[(offset - 0x508) / 4];
            if (tx.is_write()) word = static_cast<std::uint32_t>(load(tx));
            else store(tx, word);
        } else {
            auto* word = wide_word(lane, offset & ~7u);
            if (!word) return;
            const auto shift = (offset & 4) * 8;
            if (tx.is_write()) {
                const auto value = load(tx);
                if (size == 8) *word = value;
                else {
                    const auto mask = std::uint64_t{0xffffffff} << shift;
                    *word = (*word & ~mask) | (value << shift);
                }
                const auto row = power_row(offset & ~7u);
                if (row < power_state_count) lane.power_rows[row] = true;
            } else store(tx, *word >> shift);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void pdp_lane0_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) { transport(tx, 0); }
    void pdp_lane1_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) { transport(tx, 1); }
    void cpucp_lane0_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) { transport(tx, 0); }
    void cpucp_lane1_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) { transport(tx, 1); }
};

inline bool PdpCprh::can_apply_power_state(unsigned lane, unsigned state) const {
    CprhTarget* first = nullptr;
    CprhTarget* second = nullptr;
    if (!state_programmed(lane, state) || !lane_targets(lane, first, second)) return false;
    const auto corners = state_corners(lane, state);
    return first->can_apply_corner(corners[0]) && second->can_apply_corner(corners[1]);
}

inline bool PdpCprh::apply_power_state(unsigned lane, unsigned state) {
    CprhTarget* first = nullptr;
    CprhTarget* second = nullptr;
    if (!state_programmed(lane, state) || !lane_targets(lane, first, second)) return false;
    const auto corners = state_corners(lane, state);
    // Both owners must accept their corner before either owner changes state.
    if (!first->can_apply_corner(corners[0]) || !second->can_apply_corner(corners[1]))
        return false;
    return first->apply_corner(corners[0]) && second->apply_corner(corners[1]);
}

inline bool PdpCprh::power_state_ready(unsigned lane, unsigned state) const {
    CprhTarget* first = nullptr;
    CprhTarget* second = nullptr;
    if (!state_programmed(lane, state) || !lane_targets(lane, first, second)) return false;
    const auto corners = state_corners(lane, state);
    return first->corner_ready(corners[0]) && second->corner_ready(corners[1]);
}

} // namespace qcom_vp

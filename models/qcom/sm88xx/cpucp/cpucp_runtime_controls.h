// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "cpu_domain_core.h"

namespace qcom_vp {

// Functional configuration-apply policy for the recovered runtime initializer.
// These names describe virtual effects, not recovered silicon register names.
struct CpucpRuntimeControls {
    std::array<std::uint32_t, 2> event_masks{};
    std::array<std::uint32_t, 5> policy{};
    std::array<std::uint64_t, 2> lane_policy{};
    std::array<std::uint32_t, 6> clock_config{};
    unsigned event_writes = 0;
    unsigned clock_writes = 0;

    bool configured(unsigned lanes) const {
        if (event_writes != 3 || clock_writes != 0x3f) return false;
        for (unsigned i = 0; i < 3; ++i)
            if (!(policy[i] & 0x4000)) return false;
        for (unsigned lane = 0; lane < 2; ++lane)
            if ((lanes & (1u << lane)) &&
                (!(policy[3 + lane] & 0x4000) || (lane_policy[lane] & 3) != 2)) return false;
        return (clock_config[0] & 1) && (clock_config[3] & 1) &&
            (clock_config[4] & 1) && (clock_config[5] & 2);
    }
    std::uint32_t status(unsigned lanes, bool source_running,
                         std::uint32_t first_request, std::uint32_t second_request) const {
        if (!source_running || !configured(lanes)) return 0;
        // Requests apply already retained configuration. There is no firmware ACK,
        // CPU reset, fabricated interrupt, or arbitrary acquisition timeout.
        return ((first_request & 0x1000) ? 1u << 18 : 0u) |
            ((second_request & 0x1000) ? 1u << 19 : 0u);
    }
    void event_transport(tlm::tlm_generic_payload& tx, std::uint32_t status_value) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset == 0x10) {
            if (!tx.is_read()) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
            clock_detail::store(tx, status_value);
        } else if (offset == 0 || offset == 4) {
            const unsigned index = static_cast<unsigned>(offset / 4);
            if (tx.is_write()) {
                const auto value = clock_detail::load(tx);
                event_masks[index] = value;
                event_writes |= 1u << index;
            } else clock_detail::store(tx, event_masks[index]);
        } else { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void policy_transport(tlm::tlm_generic_payload& tx) {
        const auto offset = tx.get_address();
        if (offset >= 0x20 && offset < 0x30) {
            const bool wide = tx.get_data_length() == 8;
            if (!(wide ? CpuDomainCoreState::valid64(tx) : clock_detail::valid(tx))) return;
            auto& value = lane_policy[(offset - 0x20) / 8];
            const unsigned shift = (offset & 4) * 8;
            if (tx.is_write()) {
                if (wide) value = CpuDomainCoreState::load64(tx);
                else {
                    const auto mask = UINT64_C(0xffffffff) << shift;
                    value = (value & ~mask) | (std::uint64_t{clock_detail::load(tx)} << shift);
                }
            } else if (wide) CpuDomainCoreState::store64(tx, value);
            else clock_detail::store(tx, static_cast<std::uint32_t>(value >> shift));
        } else {
            if (!clock_detail::valid(tx)) return;
            unsigned index;
            if (offset <= 8) index = static_cast<unsigned>(offset / 4);
            else if (offset == 0x10 || offset == 0x14) index = 3 + (offset - 0x10) / 4;
            else { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
            if (tx.is_write()) {
                const auto value = clock_detail::load(tx);
                policy[index] = value;
            } else clock_detail::store(tx, policy[index]);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void clock_transport(tlm::tlm_generic_payload& tx) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        unsigned index;
        if (offset <= 0x10) index = static_cast<unsigned>(offset / 4);
        else if (offset == 0x30) index = 5;
        else { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (tx.is_write()) {
            const auto value = clock_detail::load(tx);
            clock_config[index] = value;
            clock_writes |= 1u << index;
        } else clock_detail::store(tx, clock_config[index]);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <array>
#include <cstdint>
#include <iostream>

namespace qcom_vp {

// IPA secure configuration and GSI register state. See models/qcom/ipa/evidence.md.
class QcomIpaV5 : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomIpaV5> cfg_socket{"cfg_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomIpaV5> gsi_socket{"gsi_socket"};
    cci::cci_param<unsigned> user_info_ptr{"user_info_ptr", 0x3fb,
        "Virtual GSI user-info pointer in eight-byte units"};

    explicit QcomIpaV5(sc_core::sc_module_name name) : sc_module(name) {
        cfg_socket.register_b_transport(this, &QcomIpaV5::cfg_transport);
        gsi_socket.register_b_transport(this, &QcomIpaV5::gsi_transport);
    }

    void cold_reset() {
        controls_.fill(0);
        endpoint_.fill(0);
        resource0_.fill(0);
        resource1_.fill(0);
        resource2_.fill(0);
        channel_map_.fill(0);
        iram_ptrs_.fill(0);
        gsi_controls_.fill(0);
        canary_ = 0;
    }

private:
    std::array<std::uint32_t, 4> controls_{};
    std::array<std::uint32_t, 36> endpoint_{}, resource0_{}, resource1_{};
    std::array<std::uint32_t, 32> resource2_{};
    std::array<std::uint32_t, 36> channel_map_{};
    std::array<std::uint32_t, 19> iram_ptrs_{};
    std::array<std::uint32_t, 2> gsi_controls_{};
    std::uint32_t canary_ = 0;
    static constexpr std::array<unsigned, 19> iram_ptr_offsets{
        0x400, 0x418, 0x424, 0x428, 0x404, 0x42c, 0x44c, 0x430, 0x434,
        0x438, 0x420, 0x444, 0x43c, 0x41c, 0x448, 0x440, 0x408, 0x414, 0x470};

    void end_of_elaboration() override {
        if (!user_info_ptr.get_value() || user_info_ptr.get_value() > 0x3fb)
            throw std::invalid_argument("IPA user-info pointer must fit the declared shared RAM");
        cold_reset();
    }

    template<std::size_t N>
    static std::uint32_t* bank(std::array<std::uint32_t, N>& words,
                               std::uint64_t offset, unsigned base) {
        if (offset < base || offset - base >= N * 4) return nullptr;
        return &words[(offset - base) / 4];
    }

    std::uint32_t* cfg_word(std::uint64_t offset) {
        if (auto* word = bank(controls_, offset, 0x82c4)) return word;
        if (auto* word = bank(endpoint_, offset, 0x8800)) return word;
        if (auto* word = bank(resource0_, offset, 0x9000)) return word;
        if (auto* word = bank(resource1_, offset, 0x9400)) return word;
        return bank(resource2_, offset, 0x9800);
    }

    std::uint32_t* gsi_word(std::uint64_t offset) {
        if (auto* word = bank(gsi_controls_, offset, 0x18)) return word;
        for (unsigned i = 0; i < iram_ptr_offsets.size(); ++i)
            if (offset == iram_ptr_offsets[i]) return &iram_ptrs_[i];
        // QSEE declares 26 channels in EE0, nine in EE1, and one in EE2.
        if (offset >= 0x9000 && offset <= 0x9064) return &channel_map_[(offset - 0x9000) / 4];
        if (offset >= 0x9400 && offset <= 0x9420) return &channel_map_[26 + (offset - 0x9400) / 4];
        if (offset == 0x9800) return &channel_map_[35];
        if (offset == 0x1ffc + std::uint64_t{user_info_ptr.get_value()} * 8) return &canary_;
        return nullptr;
    }

    void transport(tlm::tlm_generic_payload& tx, bool cfg) {
        const auto offset = tx.get_address();
        if (!clock_detail::valid(tx)) {
            log_unknown(tx, cfg);
            return;
        }
        if (!cfg && offset == 0x25c && tx.is_read()) {
            clock_detail::store(tx, user_info_ptr.get_value());
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        auto* word = cfg ? (offset < 0x2000 ? cfg_word(offset + 0x8000) : nullptr) : gsi_word(offset);
        if (!word) {
            log_unknown(tx, cfg);
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) *word = clock_detail::load(tx);
        else clock_detail::store(tx, *word);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void log_unknown(const tlm::tlm_generic_payload& tx, bool cfg) const {
        std::cerr << "IPA_UNMODELED name=" << name() << " region=" << (cfg ? "cfg" : "gsi")
                  << " offset=0x" << std::hex << tx.get_address() << std::dec
                  << " size=" << tx.get_data_length() << " write=" << tx.is_write() << '\n';
    }
    void cfg_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { transport(tx, true); }
    void gsi_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { transport(tx, false); }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"
#include <algorithm>

namespace qcom_vp {

// CPUSS limits manager. The PDP limit driver programs the retained words.
// The CPUCP thermal module copies the sample words through ACEHR.
// Offsets and instruction addresses: models/qcom/sm88xx/cpucp/subsystem_evidence.md.
class QcomCpussLlm : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCpussLlm> target_socket{"target_socket"};
    cci::cci_param<unsigned> first_sensor{"first_sensor", 5, "First implemented sample word index"};
    cci::cci_param<unsigned> core_count{"core_count", 6, "Number of implemented core sample pairs"};
    cci::cci_param<unsigned> temperature_decic{"temperature_decic", 250, "Explicit virtual temperature in tenths of a degree"};
    cci::cci_param<bool> sample_valid{"sample_valid", true, "Virtual sensor sample validity"};
    explicit QcomCpussLlm(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomCpussLlm::transport);
    }
    // Retained words return to the declared virtual cold value.
    void cold_reset() { words_.fill(0); }

private:
    // Words that the selected PDP tables and limit paths access.
    // The four initialization tables write offsets 0x380..0x2a60.
    // The limit paths read and write offsets 0x0c4, 0x0b90, 0x2890, and 0x2984.
    // Two banks hold sixteen trip-point words at 0x1110 and 0x2390.
    static constexpr std::array<std::uint32_t, 178> offsets_{
        0x0c4, 0x380, 0x390, 0x394, 0x398, 0x39c, 0x3a0, 0x3a4,
        0x3a8, 0x3ac, 0x3b0, 0x3b4, 0x3b8, 0x3bc, 0x3c0, 0x3c4,
        0x3c8, 0x3cc, 0x3d0, 0x410, 0x430, 0x450, 0x800, 0x880, 0x890,
        0x894, 0x898, 0x89c, 0x900, 0x910, 0x914, 0x980, 0x990,
        0x994, 0xa80, 0xa90, 0xa94, 0xa98, 0xa9c, 0xaa0, 0xaa4,
        0xb10, 0xb14, 0xb18, 0xb1c, 0xb90, 0x1000, 0x1018, 0x101c,
        0x1024, 0x1098, 0x109c, 0x1110, 0x1114, 0x1118, 0x111c, 0x1120,
        0x1124, 0x1128, 0x112c, 0x1130, 0x1134, 0x1138, 0x113c, 0x1140,
        0x1144, 0x1148, 0x114c, 0x142c, 0x1680, 0x1698, 0x169c, 0x16a0,
        0x16b0, 0x1a80, 0x1a90, 0x1a94, 0x1a98, 0x1a9c, 0x1aa0, 0x1aa4,
        0x1aa8, 0x1aac, 0x1b10, 0x1b14, 0x1b18, 0x1b1c, 0x1b90, 0x1b94,
        0x1b98, 0x1b9c, 0x1ba0, 0x1ba4, 0x1ba8, 0x1bac, 0x1bb0, 0x1bb4,
        0x1bb8, 0x1bbc, 0x1bc0, 0x1bc4, 0x1bc8, 0x1bcc, 0x1bd4, 0x1bd8,
        0x1bdc, 0x1be0, 0x1be4, 0x1be8, 0x1bec, 0x1bf0, 0x1bf4, 0x1bf8,
        0x1bfc, 0x1c00, 0x1c04, 0x1c08, 0x1c0c, 0x1c14, 0x1c18, 0x1c1c,
        0x1c20, 0x1c24, 0x1c28, 0x1c2c, 0x1c30, 0x1c34, 0x1c38, 0x1c3c,
        0x1c40, 0x1c44, 0x1c48, 0x1c4c, 0x1c54, 0x1c58, 0x1c5c, 0x1c60,
        0x1c64, 0x1c68, 0x1c6c, 0x1c70, 0x1c74, 0x1c78, 0x1c7c, 0x1c80,
        0x1c84, 0x1c88, 0x1c8c, 0x2390, 0x2394, 0x2398, 0x239c, 0x23a0,
        0x23a4, 0x23a8, 0x23ac, 0x23b0, 0x23b4, 0x23b8, 0x23bc, 0x23c0,
        0x23c4, 0x23c8, 0x23cc, 0x2410, 0x2414, 0x2490, 0x2494, 0x2514,
        0x2794, 0x2810, 0x2814, 0x2890, 0x2980, 0x2984, 0x2a10, 0x2a40, 0x2a60,
    };
    // The package aggregate shares the sample encoding.
    static constexpr std::uint32_t aggregate = 0x560;
    static constexpr std::uint64_t aperture_size = 0x8000;
    std::array<std::uint32_t, offsets_.size()> words_{};

    void end_of_elaboration() override {
        if (!core_count.get_value() || core_count.get_value() > 8 || first_sensor.get_value() > 31 ||
            first_sensor.get_value() + 2 * core_count.get_value() > 32 || temperature_decic.get_value() > 0xffff)
            throw std::invalid_argument("The CPUSS LLM sample configuration is invalid");
        for (std::size_t index = 0; index < offsets_.size(); ++index) {
            const auto offset = offsets_[index];
            if ((offset & 3) || offset >= aperture_size || (index && offset <= offsets_[index - 1]) ||
                sample_word(offset))
                throw std::invalid_argument("The CPUSS LLM retained offset table is invalid");
        }
        cold_reset();
    }
    // The sample bank and the aggregate are read-only virtual measurements.
    bool sample_word(std::uint64_t offset) const {
        const auto first = 0x4e0 + 4 * first_sensor.get_value();
        return offset == aggregate || (offset >= first && offset < first + 8 * core_count.get_value() &&
            !((offset - first) & 3));
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (sample_word(offset)) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            clock_detail::store(tx, (sample_valid.get_value() ? 0x10000u : 0u) | temperature_decic.get_value());
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset >= aperture_size) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const auto entry = std::lower_bound(offsets_.begin(), offsets_.end(),
                                           static_cast<std::uint32_t>(offset));
        if (entry == offsets_.end() || *entry != offset) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& word = words_[static_cast<std::size_t>(entry - offsets_.begin())];
        if (tx.is_read()) clock_detail::store(tx, word);
        else word = clock_detail::load(tx);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

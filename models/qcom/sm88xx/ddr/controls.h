// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <array>
#include <sstream>

namespace qcom_vp {

// Ideal synchronous programming latch. No analog training executes here.
// Platform data declares exactly sixteen configuration words and two controls.
class QcomDdrFrontendProgram : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomDdrFrontendProgram> target_socket{"target_socket"};
    cci::cci_param<std::string> configuration_offsets{
        "configuration_offsets", "", "Sixteen distinct relative word offsets, comma-separated"};
    cci::cci_param<std::uint32_t> apply_offset{"apply_offset", 0, "Relative apply control offset"};
    cci::cci_param<std::uint32_t> retained_offset{"retained_offset", 0, "Relative retained control offset"};
    cci::cci_param<std::string> profile_offsets{
        "profile_offsets", "", "Two optional read-only profile offsets, comma-separated"};
    cci::cci_param<std::string> profile_values{
        "profile_values", "", "Two optional read-only profile values, comma-separated"};
    explicit QcomDdrFrontendProgram(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomDdrFrontendProgram::transport);
    }
    void cold_reset() {
        values_ = {};
        written_ = {};
        retained_ = 0;
    }

private:
    std::array<std::uint32_t, 16> offsets_{};
    std::array<std::uint32_t, 16> values_{};
    std::array<bool, 16> written_{};
    std::array<std::uint32_t, 2> profile_offsets_{};
    std::array<std::uint32_t, 2> profile_values_{};
    bool has_profile_ = false;
    std::uint32_t retained_ = 0;
    void end_of_elaboration() override {
        const auto apply = apply_offset.get_value();
        const auto retained = retained_offset.get_value();
        if ((apply & 3) || (retained & 3) || apply == retained)
            throw std::invalid_argument("DDR frontend controls must be distinct aligned words");
        std::istringstream input(configuration_offsets.get_value());
        std::string token;
        std::size_t count = 0;
        while (std::getline(input, token, ',')) {
            std::size_t used = 0;
            const auto offset = std::stoull(token, &used, 0);
            if (used != token.size() || offset > 0xffffffffULL || (offset & 3) ||
                offset == apply || offset == retained || count == offsets_.size())
                throw std::invalid_argument("Invalid DDR frontend configuration offset");
            for (std::size_t i = 0; i < count; ++i)
                if (offsets_[i] == offset)
                    throw std::invalid_argument("Duplicate DDR frontend configuration offset");
            offsets_[count++] = static_cast<std::uint32_t>(offset);
        }
        if (count != offsets_.size())
            throw std::invalid_argument("DDR frontend requires sixteen configuration words");
        const auto parse_profile = [](const std::string& text, std::array<std::uint32_t, 2>& output) {
            std::istringstream input(text);
            std::string token;
            std::size_t count = 0;
            while (std::getline(input, token, ',')) {
                std::size_t used = 0;
                const auto value = std::stoull(token, &used, 0);
                if (used != token.size() || value > 0xffffffffULL || count == output.size())
                    throw std::invalid_argument("Invalid DDR frontend profile");
                output[count++] = static_cast<std::uint32_t>(value);
            }
            return count;
        };
        const auto profile_offset_count = parse_profile(profile_offsets.get_value(), profile_offsets_);
        const auto profile_value_count = parse_profile(profile_values.get_value(), profile_values_);
        if (profile_offset_count != profile_value_count ||
            (profile_offset_count != 0 && profile_offset_count != profile_offsets_.size()))
            throw std::invalid_argument("DDR frontend profile requires two offset and value pairs");
        has_profile_ = profile_offset_count != 0;
        for (std::size_t i = 0; i < profile_offset_count; ++i) {
            if ((profile_offsets_[i] & 3) || profile_offsets_[i] == apply ||
                profile_offsets_[i] == retained || profile_offsets_[i] == profile_offsets_[1 - i])
                throw std::invalid_argument("Invalid DDR frontend profile offset");
            for (const auto offset : offsets_)
                if (profile_offsets_[i] == offset)
                    throw std::invalid_argument("Duplicate DDR frontend profile offset");
        }
        cold_reset();
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto address = tx.get_address();
        if (address == apply_offset.get_value()) {
            if (tx.is_write()) {
                if (clock_detail::load(tx) != 1) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                for (const auto written : written_) {
                    if (!written) {
                        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                        return;
                    }
                }
                // Accepted configuration completes synchronously. Keep every word.
            } else {
                clock_detail::store(tx, 0);
            }
        } else if (address == retained_offset.get_value()) {
            if (tx.is_write()) retained_ = clock_detail::load(tx);
            else clock_detail::store(tx, retained_);
        } else {
            for (std::size_t i = 0; has_profile_ && i < profile_offsets_.size(); ++i) {
                if (address != profile_offsets_[i]) continue;
                if (tx.is_write()) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                clock_detail::store(tx, profile_values_[i]);
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
            std::size_t i = 0;
            while (i < offsets_.size() && address != offsets_[i]) ++i;
            if (i == offsets_.size()) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            if (tx.is_write()) {
                values_[i] = clock_detail::load(tx);
                written_[i] = true;
            } else {
                clock_detail::store(tx, values_[i]);
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

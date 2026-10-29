// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"
#include <array>
#include <sstream>

namespace qcom_vp {

// Configuration storage does not execute instructions or report completion.
class QcomSequenceConfiguration : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomSequenceConfiguration> target_socket{"target_socket"};
    cci::cci_param<std::string> indices{"indices", "", "Implemented configuration record indices"};
    explicit QcomSequenceConfiguration(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomSequenceConfiguration::transport);
    }
private:
    std::array<std::array<std::uint32_t, 6>, 95> records_{};
    std::array<bool, 95> implemented_{};
    void end_of_elaboration() override {
        std::istringstream input(indices.get_value());
        unsigned index;
        unsigned count = 0;
        while (input >> index) {
            if (index >= implemented_.size() || implemented_[index])
                throw std::invalid_argument("The sequencer record index is invalid");
            implemented_[index] = true;
            ++count;
        }
        if (!count || !input.eof())
            throw std::invalid_argument("The sequencer requires valid record indices");
    }
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        std::uint32_t* word = nullptr;
        if (offset / 32 < records_.size() && (offset % 32) < 24 && implemented_[offset / 32]) {
            word = &records_[offset / 32][(offset % 32) / 4];
        }
        if (!word) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) *word = clock_detail::load(tx);
        else clock_detail::store(tx, *word);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

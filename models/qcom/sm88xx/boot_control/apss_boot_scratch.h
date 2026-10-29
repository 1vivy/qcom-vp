// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef QCOM_VP_APSS_BOOT_SCRATCH_H
#define QCOM_VP_APSS_BOOT_SCRATCH_H

#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// Software-owned scratch state, including the canary. No automatic publication.
class QcomApssBootScratch : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomApssBootScratch> target_socket{"target_socket"};
    cci::cci_param<std::uint64_t> canary_reset{
        "canary_reset", 0x100000000ULL,
        "Required board canary scratch initial word, not a measured hardware reset value"};

    explicit QcomApssBootScratch(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomApssBootScratch::transport);
    }

private:
    std::array<std::uint32_t, 16> words_{};

    void end_of_elaboration() override {
        const auto initial = canary_reset.get_value();
        if (initial > 0xffffffffULL) {
            throw std::invalid_argument("APSS scratch requires an explicit 32-bit canary reset profile");
        }
        words_[0x38 / 4] = static_cast<std::uint32_t>(initial);
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() >= words_.size() * sizeof(std::uint32_t)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* data = tx.get_data_ptr();
        auto& word = words_[tx.get_address() / sizeof(std::uint32_t)];
        if (tx.is_write()) {
            word = std::uint32_t{data[0]} | (std::uint32_t{data[1]} << 8) |
                (std::uint32_t{data[2]} << 16) | (std::uint32_t{data[3]} << 24);
        } else if (tx.is_read()) {
            for (unsigned i = 0; i != 4; ++i) {
                data[i] = static_cast<unsigned char>(word >> (8 * i));
            }
        } else {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp
#endif

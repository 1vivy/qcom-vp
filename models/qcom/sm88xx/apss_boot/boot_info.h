// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef QCOM_VP_APSS_BOOT_INFO_H
#define QCOM_VP_APSS_BOOT_INFO_H

#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace qcom_vp {

// qcom,tcsr-boot-info-v1. See boot_info_evidence.md for the register contract.
class QcomTcsrBootInfo : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomTcsrBootInfo> target_socket{"target_socket"};
    cci::cci_param<unsigned> failed_attempts{
        "failed_attempts", 32, "Required virtual reset count of failed boot attempts (0..31)"};
    cci::cci_param<unsigned> max_attempts{
        "max_attempts", 32, "Required virtual reset maximum boot attempts (0..31)"};
    cci::cci_param<unsigned> use_set_b{
        "use_set_b", 2, "Required virtual reset image set selection (0=A, 1=B)"};
    cci::cci_param<unsigned> recovery_enabled{
        "recovery_enabled", 2, "Required virtual reset early firmware failure recovery enable (0..1)"};

    explicit QcomTcsrBootInfo(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomTcsrBootInfo::transport);
    }

private:
    std::uint32_t value_ = 0;

    void end_of_elaboration() override {
        const auto failed = failed_attempts.get_value();
        const auto maximum = max_attempts.get_value();
        const auto set_b = use_set_b.get_value();
        const auto recovery = recovery_enabled.get_value();
        if (failed > 31 || maximum > 31 || set_b > 1 || recovery > 1) {
            throw std::invalid_argument("TCSR boot info requires explicit valid virtual reset fields");
        }
        value_ = (failed << 1) | (maximum << 6) | (set_b << 31) | recovery;
        char policy[192];
        std::snprintf(policy, sizeof(policy),
            "Virtual TCSR boot info: failed=%u, maximum=%u, set=%c, recovery=%u. "
            "Spare bits reset to zero. This is not captured silicon state.",
            failed, maximum, set_b ? 'B' : 'A', recovery);
        SC_REPORT_INFO(name(), policy);
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* data = tx.get_data_ptr();
        if (tx.is_read()) {
            for (unsigned i = 0; i != 4; ++i) {
                data[i] = static_cast<unsigned char>(value_ >> (8 * i));
            }
        } else if (tx.is_write()) {
            // HWIO exposes full-word writes, including the documented spare field.
            value_ = 0;
            for (unsigned i = 0; i != 4; ++i) {
                value_ |= static_cast<std::uint32_t>(data[i]) << (8 * i);
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

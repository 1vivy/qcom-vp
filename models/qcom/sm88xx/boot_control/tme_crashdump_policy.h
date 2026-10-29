// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// XBL 0x1487ad78 and CPH2771 0x148794bc read the crash-dump option vector.
// Evidence and dependency boundaries: tme_crashdump_evidence.md.
class TmeCrashdumpPolicy : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<TmeCrashdumpPolicy> target_socket{"target_socket"};
    cci::cci_param<std::string> policy{
        "policy", "", "Required virtual crash-dump policy: disabled"};

    explicit TmeCrashdumpPolicy(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &TmeCrashdumpPolicy::transport);
    }

private:
    void end_of_elaboration() override {
        if (policy.get_value() != "disabled")
            throw std::invalid_argument("TME crash-dump policy must explicitly disable all dump options");
    }

    void reject(tlm::tlm_generic_payload& tx, tlm::tlm_response_status status) {
        tx.set_response_status(status);
        SC_REPORT_WARNING(name(), "Unsupported TME crash-dump policy access");
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            reject(tx, tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || (tx.get_address() & 3)) {
            reject(tx, tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() != 0) {
            reject(tx, tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        // No authorized dump-policy producer exists in this virtual epoch.
        // Zero denies mini, non-secure, and unencrypted full dumps alike.
        for (unsigned i = 0; i != 4; ++i) tx.get_data_ptr()[i] = 0;
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

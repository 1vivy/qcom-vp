// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef QCOM_VP_APSS_BOOT_IDENTITY_H
#define QCOM_VP_APSS_BOOT_IDENTITY_H

#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace qcom_vp {

// The selected CPUCP images read this block at 0x17651000: hardware version at
// +0x00, CPU presence at +0x0c, boot selector at +0x18, LLC disable at +0x20,
// and boot frequency at +0x24.
// PBL 0x30d78c reads bits 23:16 of the boot frequency word. The caller at 0x302e48 accepts 0..3.
// The source-only block and register names remain descriptive labels.
// The values are explicit board choices. No SM8850 measurement is established.
class QcomApssBootIdentity : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomApssBootIdentity> target_socket{"target_socket"};
    cci::cci_param<std::uint64_t> hardware_version{
        "hardware_version", UINT64_C(0x100000000),
        "Required virtual APSS_HW_VERSION word (major 31:28, minor 27:16, step 15:0)"};
    cci::cci_param<unsigned> profile_selector{
        "profile_selector", 256,
        "Required virtual APSS boot selector (0..3), not a measured silicon identity"};
    cci::cci_param<unsigned> variant_selector{
        "variant_selector", 256,
        "Required virtual boot variant selector (0..2)"};
    cci::cci_param<unsigned> cpu_present_mask{
        "cpu_present_mask", 0x1000000,
        "Required virtual CPU presence bits, one byte per cluster"};
    cci::cci_param<unsigned> llc_group_disable_mask{
        "llc_group_disable_mask", 0x10000,
        "Required virtual LLC group disable bits, four bits per cluster"};
    explicit QcomApssBootIdentity(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomApssBootIdentity::transport);
    }

private:
    std::uint32_t version_ = 0;
    std::uint32_t identity_ = 0;
    std::uint32_t variant_ = 0;
    std::uint32_t cpu_present_ = 0;
    std::uint32_t llc_disabled_ = 0;

    void end_of_elaboration() override {
        const auto selector = profile_selector.get_value();
        const auto variant = variant_selector.get_value();
        const auto present = cpu_present_mask.get_value();
        const auto disabled = llc_group_disable_mask.get_value();
        if (present > 0xffffff || disabled > 0xffff) {
            throw std::invalid_argument("APSS identity requires explicit 24-bit CPU and 16-bit LLC masks");
        }
        if (hardware_version.get_value() > 0xffffffff) {
            throw std::invalid_argument("APSS identity requires an explicit 32-bit hardware version");
        }
        version_ = static_cast<std::uint32_t>(hardware_version.get_value());
        if (variant > 2) {
            throw std::invalid_argument("Boot identity requires an explicit virtual variant selector in 0..2");
        }
        if (selector > 3) {
            throw std::invalid_argument("APSS identity requires an explicit virtual profile selector in 0..3");
        }
        identity_ = static_cast<std::uint32_t>(selector) << 16;
        variant_ = static_cast<std::uint32_t>(variant) << 16;
        cpu_present_ = present;
        llc_disabled_ = disabled;
        char policy[224];
        std::snprintf(policy, sizeof(policy),
            "Virtual boot identity: version=0x%08x, profile=%u, variant=%u in bits 23:16. "
            "CPU present=0x%06x, LLC group disable=0x%04x. "
            "Other bits are zero. These values do not describe captured silicon.",
            version_, selector, variant, present, disabled);
        SC_REPORT_INFO(name(), policy);
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
        std::uint32_t value;
        switch (tx.get_address()) {
        case 0x00: value = version_; break;
        case 0x0c: value = cpu_present_; break;
        case 0x18: value = variant_; break;
        case 0x20: value = llc_disabled_; break;
        case 0x24: value = identity_; break;
        default:
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        // CPUCP 0x335668 reads the variant. Its caller at 0x3344ec accepts 0..2.
        auto* data = tx.get_data_ptr();
        for (unsigned i = 0; i != 4; ++i) {
            data[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp
#endif

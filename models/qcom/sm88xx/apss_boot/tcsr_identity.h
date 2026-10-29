// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// APSS 0x30d86c reads EMULATION_TYPE. TME 0x20cb5a/0x20cb80 read version words.
// CPH2771 XBL 0x14826094 reads the hardware and emulation words together.
class QcomTcsrIdentity : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomTcsrIdentity> target_socket{"target_socket"};
    cci::cci_param<uint64_t> hardware_version{"hardware_version", UINT64_MAX, "Required virtual hardware version"};
    cci::cci_param<unsigned> emulation_type{"emulation_type", 256, "Required four-bit emulation type"};
    cci::cci_param<uint64_t> sw_once_hardware_version{"sw_once_hardware_version", UINT64_MAX,
        "Required virtual software-once hardware version read value"};

    explicit QcomTcsrIdentity(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomTcsrIdentity::transport);
    }

private:
    void end_of_elaboration() override {
        if (hardware_version.get_value() > UINT32_MAX || emulation_type.get_value() > 15 ||
            sw_once_hardware_version.get_value() > UINT32_MAX)
            throw std::invalid_argument("TCSR identity requires explicit version and emulation values");
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const auto width = tx.get_data_length();
        if (!tx.get_data_ptr() || (width != 4 && width != 8) ||
            tx.get_streaming_width() < width || tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto offset = tx.get_address();
        if ((offset & (width - 1)) || offset > 12 - width) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        uint64_t value = offset == 0 ? hardware_version.get_value() :
            offset == 4 ? emulation_type.get_value() : sw_once_hardware_version.get_value();
        if (width == 8) value |= uint64_t{emulation_type.get_value()} << 32;
        auto* bytes = tx.get_data_ptr();
        for (unsigned i = 0; i != width; ++i) bytes[i] = value >> (8 * i);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

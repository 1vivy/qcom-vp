// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "sequencer_auth.h"
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <iostream>
#include <string>

namespace qcom_vp {

// The virtual status bridge observes the real response ring. It creates no mailbox response.
// APSS 0x30db84 starts the authenticated sequencer. APSS 0x30dbf8 requires result three.
class TmeBootStatus : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<TmeBootStatus> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<TmeBootStatus> mailbox_socket{"mailbox_socket"};
    tlm_utils::simple_initiator_socket<TmeBootStatus> handoff_socket{"handoff_socket"};
    tlm_utils::simple_initiator_socket_optional<TmeBootStatus> program_socket{"program_socket"};
    tlm_utils::simple_initiator_socket_optional<TmeBootStatus> input_socket{"input_socket"};
    cci::cci_param<std::string> trusted_root{"trusted_root", "", "Provisioned P-521 root file"};
    cci::cci_param<std::string> entitlement_sha256{
        "entitlement_sha256", "", "SHA-256 of the approved signed entitlement"};
    cci::cci_param<unsigned> hardware_version{"hardware_version", 0, "Provisioned hardware version"};
    cci::cci_param<unsigned> interface_version{
        "interface_version", 256, "Required virtual channel interface version (0..255)"};

    explicit TmeBootStatus(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &TmeBootStatus::transport);
    }

private:
    SequencerAuthentication authentication_;
    uint32_t boot_result_ = 0;
    void end_of_elaboration() override {
        if (interface_version.get_value() > 255)
            throw std::invalid_argument("TME boot status requires an explicit interface version");
        authentication_.configure(trusted_root.get_value(), entitlement_sha256.get_value());
    }

    static uint32_t word(const unsigned char* bytes) {
        return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
            (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
    }

    template<typename Socket>
    static tlm::tlm_response_status read_block(Socket& socket, unsigned char* bytes,
                                               unsigned length, sc_core::sc_time& delay) {
        tlm::tlm_generic_payload read;
        read.set_command(tlm::TLM_READ_COMMAND);
        read.set_address(0);
        read.set_data_ptr(bytes);
        read.set_data_length(length);
        read.set_streaming_width(length);
        socket->b_transport(read, delay);
        return read.get_response_status();
    }

    tlm::tlm_response_status ring_state(uint32_t& value, sc_core::sc_time& delay) {
        std::array<unsigned char, 16> header{};
        const auto status = read_block(mailbox_socket, header.data(), header.size(), delay);
        if (status != tlm::TLM_OK_RESPONSE) return status;
        const auto signature = word(header.data());
        const auto capacity = word(header.data() + 4);
        const auto produced = word(header.data() + 8);
        const auto consumed = word(header.data() + 12);
        value = 0;
        // Only a complete, bounded firmware ring makes its service available.
        if (capacity == 2 && uint32_t(produced - consumed) <= capacity) {
            if (signature == 0x4c425054) value = 5;
            else if (signature == 0x57464d54) value = 4;
        }
        return tlm::TLM_OK_RESPONSE;
    }

    void start(sc_core::sc_time& delay) {
        uint32_t state = 0;
        const char* failure = nullptr;
        if (ring_state(state, delay) != tlm::TLM_OK_RESPONSE || state != 5)
            failure = "service-unavailable";
        else if (!program_socket.size() || !input_socket.size())
            failure = "missing-image-memory";
        else {
            std::array<unsigned char, 4096> program{}, input{};
            if (read_block(program_socket, program.data(), program.size(), delay) != tlm::TLM_OK_RESPONSE ||
                read_block(input_socket, input.data(), input.size(), delay) != tlm::TLM_OK_RESPONSE)
                failure = "image-memory-read";
            else failure = authentication_.authenticate(program, input, hardware_version.get_value());
        }
        boot_result_ = failure ? 1 : 3;
        if (failure) std::cerr << "TME_SEQUENCER_AUTH_FAILED reason=" << failure << '\n';
        else std::cerr << "TME_SEQUENCER_AUTHENTICATED\n";
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        tx.set_dmi_allowed(false);
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            if (tx.get_address() != 0 || word(tx.get_data_ptr()) != 1 || boot_result_) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            start(delay);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        uint32_t value;
        if (tx.get_address() == 0x4c) {
            value = interface_version.get_value() << 16;
        } else if (tx.get_address() == 0x18) {
            value = boot_result_;
        } else if (tx.get_address() == 0x2c) {
            const auto status = ring_state(value, delay);
            if (status != tlm::TLM_OK_RESPONSE) {
                tx.set_response_status(status);
                return;
            }
            if (boot_result_ == 1) {
                value = 1;
            } else if (boot_result_ == 3) {
                if (value == 4) {
                    std::array<unsigned char, 4> flags{};
                    const auto handoff_status = read_block(handoff_socket, flags.data(), flags.size(), delay);
                    if (handoff_status != tlm::TLM_OK_RESPONSE) {
                        tx.set_response_status(handoff_status);
                        return;
                    }
                    // APSS 0x30de60 acknowledges the runtime before 0x30de50 waits for state four.
                    value = (word(flags.data()) & 6U) == 6U ? 4 : 3;
                } else {
                    value = 3;
                }
            } else if (value == 4) {
                value = 0;
            }
        } else {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* bytes = tx.get_data_ptr();
        for (unsigned i = 0; i != 4; ++i) bytes[i] = value >> (8 * i);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/tme_mailbox/tme_sequencer.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Dma : sc_core::sc_module {
    tlm_utils::simple_target_socket<Dma> target_socket{"target_socket"};
    std::array<unsigned char, 64> bytes{};

    explicit Dma(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &Dma::transport);
        for (unsigned i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(i % 32);
        bytes[37] ^= 1;
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        const auto* attrs = tx.get_extension<QemuMemTxAttrsExtension>();
        const auto address = tx.get_address();
        const auto length = tx.get_data_length();
        if (!attrs || attrs->attrs.unspecified || !attrs->attrs.secure || !tx.is_read() ||
            !tx.get_data_ptr() || address < 0x400000 || length > bytes.size() ||
            address - 0x400000 > bytes.size() - length) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        std::memcpy(tx.get_data_ptr(), bytes.data() + (address - 0x400000), length);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

struct Consumer : sc_core::sc_module {
    qcom_vp::TmeSequencer sequencer{"sequencer"};
    Dma dma{"dma"};
    tlm_utils::simple_initiator_socket<Consumer> registers{"registers"};
    tlm_utils::simple_initiator_socket<Consumer> policy{"policy"};
    tlm_utils::simple_initiator_socket<Consumer> input{"input"};
    tlm_utils::simple_initiator_socket<Consumer> output{"output"};

    explicit Consumer(sc_core::sc_module_name name, const std::string& root,
                      bool substitute = true) : sc_module(name) {
        registers.bind(sequencer.target_socket);
        policy.bind(sequencer.policy_socket);
        input.bind(sequencer.input_socket);
        output.bind(sequencer.output_socket);
        sequencer.dma_socket.bind(dma.target_socket);
        sequencer.policy_selection.set_value(0);
        sequencer.virtual_key_substitute.set_value(substitute);
        sequencer.virtual_root_hex.set_value(root);
    }

    std::uint32_t access(tlm_utils::simple_initiator_socket<Consumer>& bus,
                         unsigned address, bool writing, std::uint32_t value = 0) {
        unsigned char bytes[4]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(address);
        tx.set_command(writing ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(4);
        tx.set_streaming_width(4);
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        auto delay = sc_core::SC_ZERO_TIME;
        bus->b_transport(tx, delay);
        assert(tx.is_response_ok());
        if (writing) return 0;
        std::uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t(bytes[i]) << (8 * i);
        return result;
    }

    void write_input(const unsigned char* bytes, unsigned size) {
        tlm::tlm_generic_payload tx;
        tx.set_address(0);
        tx.set_command(tlm::TLM_WRITE_COMMAND);
        tx.set_data_ptr(const_cast<unsigned char*>(bytes));
        tx.set_data_length(size);
        tx.set_streaming_width(size);
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        auto delay = sc_core::SC_ZERO_TIME;
        input->b_transport(tx, delay);
        assert(tx.is_response_ok());
    }

    void acquire() {
        access(registers, 0x60, true, 0x100);
        assert((access(registers, 0x60, false) & 0x70) == 0x10);
    }

    void release() { access(registers, 0x130, true, 1); }

    void start() {
        access(registers, 0x60, true, 0x200);
        assert((access(registers, 0x60, false) & 0x70) == 0x40);
        assert(access(registers, 0x148, false) == 0);
        release();
    }

    void derive() {
        acquire();
        access(registers, 0x00, true, 2);
        access(registers, 0x04, true, 0);
        access(registers, 0x08, true, 3);
        access(registers, 0x0c, true, 0);
        access(registers, 0x10, true, 9);
        access(registers, 0x14, true, 0x1e);
        access(registers, 0x18, true, 0);
        access(registers, 0x30, true, 2);
        access(registers, 0x40, true, 8);
        access(registers, 0x44, true, 4);
        access(registers, 0x48, true, 1);
        access(registers, 0x54, true, 4);
        access(registers, 0x2c, true, 2);
        const std::uint32_t payload[] = {
            0x12345678, 0xa5a5a5a5, 0x30, 0x04030201,
            0x08070605, 0x74736574};
        unsigned char bytes[sizeof(payload)]{};
        for (unsigned i = 0; i < 6; ++i)
            for (unsigned n = 0; n < 4; ++n)
                bytes[4 * i + n] = static_cast<unsigned char>(payload[i] >> (8 * n));
        write_input(bytes, sizeof(bytes));
        start();
    }

    std::array<unsigned char, 48> keyed_hash() {
        acquire();
        access(registers, 0x00, true, 4);
        access(registers, 0x04, true, 2);
        access(registers, 0x08, true, 1);
        access(registers, 0x0c, true, 3);
        access(registers, 0x10, true, 0x1e);
        access(registers, 0x2c, true, 2);
        access(registers, 0x58, true, 32);
        access(registers, 0x5c, true, 32);
        const std::uint32_t descriptors[] = {
            2, 0x400000, 13, 0, 0x40000d, 19, 0};
        unsigned char bytes[sizeof(descriptors)]{};
        for (unsigned i = 0; i < 7; ++i)
            for (unsigned n = 0; n < 4; ++n)
                bytes[4 * i + n] = static_cast<unsigned char>(descriptors[i] >> (8 * n));
        write_input(bytes, sizeof(bytes));
        start();
        std::array<unsigned char, 48> result{};
        tlm::tlm_generic_payload tx;
        tx.set_address(0);
        tx.set_command(tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(result.data());
        tx.set_data_length(result.size());
        tx.set_streaming_width(result.size());
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        auto delay = sc_core::SC_ZERO_TIME;
        output->b_transport(tx, delay);
        assert(tx.is_response_ok());
        return result;
    }
};

std::string repeated_hex(const char* pair, unsigned count) {
    std::string result;
    result.reserve(2 * count);
    for (unsigned i = 0; i < count; ++i) result += pair;
    return result;
}

} // namespace

int sc_main(int argc, char** argv) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    if (argc > 1 && std::string(argv[1]) == "--root-profile") {
        const auto root_a = repeated_hex("12", 48);
        const auto root_b = repeated_hex("34", 48);
        Consumer first{"first", root_a};
        Consumer repeat{"repeat", root_a};
        Consumer different{"different", root_b};
        sc_core::sc_start(sc_core::SC_ZERO_TIME);
        first.derive();
        repeat.derive();
        different.derive();
        const auto a = first.keyed_hash();
        const auto same = repeat.keyed_hash();
        const auto b = different.keyed_hash();
        assert(a == same && a != b);
        std::cout << "TME_ROOT_PROFILE_PASS\n";
        return 0;
    }
    if (argc > 1 && (std::string(argv[1]) == "--invalid-length" ||
                     std::string(argv[1]) == "--invalid-hex" ||
                     std::string(argv[1]) == "--disabled")) {
        const std::string bad = std::string(argv[1]) == "--invalid-length" ?
            std::string(95, '1') : std::string(95, '1') +
                (std::string(argv[1]) == "--invalid-hex" ? 'g' : '1');
        Consumer invalid{"invalid", bad, std::string(argv[1]) != "--disabled"};
        try {
            sc_core::sc_start(sc_core::SC_ZERO_TIME);
        } catch (const std::invalid_argument& error) {
            const std::string message = error.what();
            assert(message.find("virtual_root_hex") != std::string::npos);
            std::cout << "TME_ROOT_INVALID_PASS\n";
            return 0;
        }
        return 1;
    }
    return 2;
}

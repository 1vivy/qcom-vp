// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/gpce/qce_v6.h"
#include "qcom/sm88xx/tme_mailbox/tme_sequencer.h"
#include <tlm_utils/simple_initiator_socket.h>
#include <cci/utils/broker.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

struct Consumer : sc_core::sc_module {
    qcom_vp::TmeSequencer sequencer{"sequencer"};
    qcom_vp::QceV6 gpce{"gpce"};
    // The GPCE stream endpoint is unused here. It only satisfies elaboration.
    struct NullStream : sc_core::sc_module {
        tlm_utils::simple_initiator_socket<NullStream> initiator{"initiator"};
        explicit NullStream(sc_core::sc_module_name name) : sc_module(name) {}
    } null_stream{"null_stream"};
    sc_core::sc_signal<bool> stream_ready{"stream_ready"};
    tlm_utils::simple_initiator_socket<Consumer> cpu{"cpu"};
    tlm_utils::simple_initiator_socket<Consumer> policy{"policy"};
    tlm_utils::simple_initiator_socket<Consumer> input{"input"};
    tlm_utils::simple_initiator_socket<Consumer> output{"output"};
    tlm_utils::simple_initiator_socket<Consumer> registers{"registers"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        cpu.bind(sequencer.target_socket);
        policy.bind(sequencer.policy_socket);
        input.bind(sequencer.input_socket);
        output.bind(sequencer.output_socket);
        registers.bind(gpce.target_socket);
        null_stream.initiator.bind(gpce.stream_socket);
        gpce.stream_ready.bind(stream_ready);
        // The sequencer resolves this sibling name at end of elaboration.
        sequencer.policy_selection.set_value(0);
        sequencer.virtual_key_substitute.set_value(true);
        sequencer.transport_key_sink.set_value("gpce");
    }

    std::uint32_t access(tlm_utils::simple_initiator_socket<Consumer>& bus,
                         unsigned address, bool writing, std::uint32_t value = 0) {
        unsigned char bytes[4]{};
        for (unsigned i = 0; i != 4; ++i) bytes[i] = value >> (8 * i);
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
        for (unsigned i = 0; i != 4; ++i) result |= std::uint32_t(bytes[i]) << (8 * i);
        return result;
    }

    std::uint32_t read(unsigned address) { return access(cpu, address, false); }
    void write(unsigned address, std::uint32_t value) { access(cpu, address, true, value); }

    void read_blob(unsigned char* blob) {
        tlm::tlm_generic_payload tx;
        tx.set_address(12);
        tx.set_command(tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(blob);
        tx.set_data_length(92);
        tx.set_streaming_width(92);
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        auto delay = sc_core::SC_ZERO_TIME;
        output->b_transport(tx, delay);
        assert(tx.is_response_ok());
    }

    void acquire() {
        write(0x60, 0x100);
        assert((read(0x60) & 0x70) == 0x10);
    }

    void start() { write(0x60, 0x200); }

    void release() { write(0x130, 1); }

    void derive(unsigned slot, unsigned requester) {
        acquire();
        write(0x00, 2);
        write(0x04, 0);
        write(0x08, 3);
        write(0x0c, 0);
        write(0x10, 9);
        write(0x14, slot);
        write(0x18, 0);
        write(0x30, 2);
        write(0x40, 8);
        write(0x44, 4);
        write(0x48, 1);
        write(0x54, 4);
        write(0x2c, requester);
        static const std::uint32_t payload[6] = {
            0x12345678, 0xa5a5a5a5, 0x30, 0x04030201, 0x08070605, 0x74736574};
        for (unsigned i = 0; i != 6; ++i) {
            tlm::tlm_generic_payload tx;
            const auto value = payload[i];
            unsigned char bytes[4]{};
            for (unsigned n = 0; n != 4; ++n) bytes[n] = value >> (8 * n);
            tx.set_address(i * 4);
            tx.set_command(tlm::TLM_WRITE_COMMAND);
            tx.set_data_ptr(bytes);
            tx.set_data_length(4);
            tx.set_streaming_width(4);
            auto delay = sc_core::SC_ZERO_TIME;
            input->b_transport(tx, delay);
            assert(tx.is_response_ok());
        }
        start();
        assert((read(0x60) & 0x70) == 0x40);
        assert(read(0x144) == 0);
        assert(read(0x148) == 0);
        release();
    }

    // Returns the command error word. Zero means the wrap completed.
    std::uint32_t wrap(unsigned source, unsigned wrapping, unsigned requester) {
        acquire();
        write(0x00, 0x0b);
        write(0x04, 0);
        write(0x08, 0);
        write(0x0c, 0);
        write(0x10, source);
        write(0x1c, wrapping);
        write(0x2c, requester);
        start();
        assert((read(0x60) & 0x70) == 0x40);
        const auto error = read(0x148);
        assert((read(0x144) & 1) == (error != 0));
        release();
        return error;
    }

    std::uint32_t broadcast(unsigned requester) {
        acquire();
        write(0x00, 0x1c);
        write(0x04, 0);
        write(0x08, 0);
        write(0x0c, 0);
        write(0x2c, requester);
        start();
        assert((read(0x60) & 0x70) == 0x40);
        const auto error = read(0x148);
        release();
        return error;
    }

    std::uint32_t read_register(unsigned address) {
        return access(registers, address, false);
    }

    void write_register(unsigned address, std::uint32_t value) {
        access(registers, address, true, value);
    }

    // Mirrors the selected 15703b74 sequence over the GPCE FIFO registers.
    std::uint32_t fifo_transaction(std::uint32_t command,
                                   const unsigned char* blob, unsigned words) {
        write_register(0x2000, read_register(0x2000) | 2u);
        write_register(0x2000, read_register(0x2000) & ~2u);
        write_register(0x2010, read_register(0x2010));
        write_register(0x2000, (read_register(0x2000) & ~1u) | 1u);
        assert((read_register(0x2000) & 2u) == 0);
        for (unsigned i = 0; i <= words; ++i) {
            std::uint32_t value = 0;
            if (i == 0) {
                value = command;
            } else {
                for (unsigned n = 0; n != 4; ++n)
                    value |= std::uint32_t(blob[(i - 1) * 4 + n]) << (8 * n);
            }
            assert(read_register(0x2004) & 0x7c000);
            write_register(0x201c, value);
        }
        assert(read_register(0x2004) & 0x3e00);
        assert(read_register(0x2008) & 2u);
        std::uint32_t status[2]{};
        for (unsigned i = 0; i != 2; ++i) {
            assert(read_register(0x2004) & 0x3e00);
            status[i] = read_register(0x205c);
        }
        write_register(0x2008, read_register(0x2008) | 2u);
        assert((read_register(0x2008) & 2u) == 0);
        return status[1];
    }
};

std::uint32_t word(const unsigned char* bytes) {
    return std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8) |
        (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
}

} // namespace

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer top{"top"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // A valid source key is not sufficient without the wrapping key.
    top.derive(0x1e, 2);
    assert(top.wrap(0x1e, 0x15, 2) != 0);

    // The TME broadcast is the only producer of the wrapping key.
    top.write_register(0x101c, 0x13e);
    assert(top.broadcast(2) == 0);
    assert(top.read_register(0x1020) & 0x100);

    assert(top.wrap(0x1e, 0x15, 2) == 0);
    unsigned char blob[120]{};
    top.read_blob(blob);
    assert(word(blob) == 0x314b5651);
    assert(word(blob + 4) == 48);

    // The firmware consumer transmits 96 bytes: the core plus four clear bytes.
    assert(top.gpce.import_wrapped_key(blob, 92, 8, 0x3e));
    assert(top.gpce.imported_key_present(8));
    assert(!top.gpce.imported_key_present(9));

    unsigned char padded[100]{};
    std::memcpy(padded, blob, 92);
    assert(top.gpce.import_wrapped_key(padded, 96, 9, 0x3e));
    assert(top.gpce.imported_key_present(9));

    // A changed ciphertext byte must not install another destination key.
    padded[20] ^= 1;
    assert(!top.gpce.import_wrapped_key(padded, 96, 10, 0x3e));
    assert(!top.gpce.imported_key_present(10));

    // Nonzero padding, an unsupported destination, and a foreign auxiliary
    // selector must all fail without installation.
    std::memcpy(padded, blob, 92);
    padded[96] = 1;
    assert(!top.gpce.import_wrapped_key(padded, 100, 10, 0x3e));
    assert(!top.gpce.import_wrapped_key(blob, 92, 64, 0x3e));
    assert(!top.gpce.import_wrapped_key(blob, 92, 8, 0x3d));

    // A key that belongs to another execution environment cannot be exported.
    // Derivation remains limited to owner two, so the foreign requester is the
    // only reachable ownership conflict here.
    assert(top.wrap(0x1e, 0x15, 3) != 0);
    // The recovered wrapping-key slot is the transport key only.
    assert(top.wrap(0x1e, 0x14, 2) != 0);

    // The FIFO transaction installs the wrapped blob end to end.
    const std::uint32_t fifo_command = 0xc8000004u | (10u << 5) | (0x3eu << 13);
    assert(top.fifo_transaction(fifo_command, blob, 24) == 0);
    assert(top.gpce.imported_key_present(10));

    // A changed blob reports a nonzero FIFO status and installs nothing.
    unsigned char tampered[96]{};
    std::memcpy(tampered, blob, 92);
    tampered[30] ^= 1;
    assert(top.fifo_transaction(0xc8000004u | (11u << 5) | (0x3eu << 13), tampered, 24) != 0);
    assert(!top.gpce.imported_key_present(11));

    // A malformed command word also fails instead of faking completion.
    assert(top.fifo_transaction(0, blob, 24) != 0);
    assert(!top.gpce.imported_key_present(12));

    std::cout << "TME_KEY_WRAP_PASS\n";
    return 0;
}

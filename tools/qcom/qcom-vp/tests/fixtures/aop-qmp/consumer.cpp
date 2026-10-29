// SPDX-License-Identifier: BSD-3-Clause-Clear
// Drives the AOP QMP endpoint as the selected GlinkDxe transport would.
#include "qcom/aop/qmp.h"
#include "qcom/soccp/ipcc.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> qmp_bus{"qmp_bus"};
    tlm_utils::simple_initiator_socket<Consumer> ipcc_bus{"ipcc_bus"};
    qcom_vp::QcomIpccLocal ipcc{"ipcc"};
    qcom_vp::QcomAopQmp qmp{"qmp"};
    sc_core::sc_signal<bool> irq{"irq"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        ipcc.protocol.set_value(0);
        ipcc.local_index.set_value(6);
        ipcc.client_count.set_value(18);
        ipcc.signal_count.set_value(8);
        ipcc.fabric_client_count.set_value(18);
        ipcc.remote_peer_path.set_value("qmp");
        ipcc.remote_peer_index.set_value(0);
        ipcc.remote_signal.set_value(0);
        ipcc.irq.bind(irq);
        qmp.ipcc_path.set_value("ipcc");
        qmp.ipcc_client.set_value(0);
        qmp_bus.bind(qmp.target_socket);
        ipcc_bus.bind(ipcc.target_socket);
    }

    tlm::tlm_response_status access(tlm_utils::simple_initiator_socket<Consumer>& bus,
        unsigned offset, bool write, std::uint32_t& value, unsigned length = 4) {
        unsigned char bytes[8]{};
        for (unsigned i = 0; i < length && i < 8; ++i)
            bytes[i] = static_cast<unsigned char>(value >> (8 * i));
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        bus->b_transport(tx, delay);
        if (!tx.is_response_ok()) return tx.get_response_status();
        if (!write) {
            value = 0;
            for (unsigned i = 0; i < length && i < 8; ++i)
                value |= std::uint32_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }
    std::uint32_t ram(unsigned offset) {
        std::uint32_t value = 0;
        assert(access(qmp_bus, offset, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void ram_set(unsigned offset, std::uint32_t value) {
        assert(access(qmp_bus, offset, true, value) == tlm::TLM_OK_RESPONSE);
    }
    tlm::tlm_response_status ram_try(unsigned offset, std::uint32_t value) {
        return access(qmp_bus, offset, true, value);
    }
    std::uint32_t ipcc_read(unsigned offset) {
        std::uint32_t value = 0;
        assert(access(ipcc_bus, offset, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void ipcc_write(unsigned offset, std::uint32_t value) {
        assert(access(ipcc_bus, offset, true, value) == tlm::TLM_OK_RESPONSE);
    }
    // Selected xport_qmp.c:1013, 1125, 388, 448, 640, 767 use one interrupt
    // for every doorbell: EFI_IPCC_C_AOP and QMP_IPCC_IRQ, both zero.
    void doorbell() { ipcc_write(0xc, 0); }
    std::uint32_t pending() { return ipcc_read(0x600); }
    std::uint32_t status() { return ipcc_read(0x48); }
    void settle() { sc_core::sc_start(sc_core::SC_ZERO_TIME); }

    // UEFI writes one fixed buffer and leaves bytes after the terminator undefined.
    void payload(const std::string& text, unsigned length) {
        for (unsigned i = 0; i < length; i += 4) {
            std::uint32_t word = 0;
            for (unsigned b = 0; b < 4; ++b) {
                const unsigned index = i + b;
                unsigned char byte = 0xa5;
                if (index < text.size()) byte = static_cast<unsigned char>(text[index]);
                else if (index == text.size()) byte = 0;
                word |= std::uint32_t{byte} << (8 * b);
            }
            ram_set(0x44 + i, word);
        }
    }
};

} // namespace

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer consumer{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // The descriptor that the AOP publishes before the APSS starts.
    assert(consumer.ram(0x00) == 0x4d41494c);
    assert(consumer.ram(0x04) == 1);
    assert(consumer.ram(0x08) == 0);
    assert(consumer.ram(0x0c) == 0x0000ffff);  // AOP link up
    assert(consumer.ram(0x14) == 0xffff0000);  // AOP channel down
    assert(consumer.ram(0x1c) == 0x180);       // AOP mailbox size
    assert(consumer.ram(0x20) == 0x200);       // AOP mailbox offset
    assert(consumer.ram(0x24) == 0xffff0000);  // APSS link down
    assert(consumer.ram(0x2c) == 0xffff0000);  // APSS channel down
    assert(consumer.ram(0x28) == 0 && consumer.ram(0x30) == 0);
    assert(consumer.ram(0x34) == 0x180);       // APSS mailbox size
    assert(consumer.ram(0x38) == 0x40);        // APSS mailbox offset

    // AOP-owned words, reserved words, and wide accesses stay rejected.
    assert(consumer.ram_try(0x0c, 0x1234) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.ram_try(0x1c, 0x1234) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.ram_try(0x34, 0x1234) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.ram_try(0x200, 0x10) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.ram_try(0x40, 0x180) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    {
        std::uint32_t value = 0;
        assert(consumer.access(consumer.qmp_bus, 0x400, false, value) ==
            tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(consumer.access(consumer.qmp_bus, 0x3c, false, value) ==
            tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(consumer.access(consumer.qmp_bus, 0x44, false, value, 8) ==
            tlm::TLM_BURST_ERROR_RESPONSE);
    }

    // The APSS interrupt handler enables the AOP signal and reads the status.
    consumer.ipcc_write(0x14, 0);  // enable client 0, signal 0
    consumer.ipcc_write(0x8, 1);   // self-clearing status read
    assert(consumer.ipcc_read(0x600) == 0);

    // Selected xport_qmp.c:276-284: ack the AOP state, publish the host state.
    consumer.ram_set(0x10, consumer.ram(0x0c));
    consumer.ram_set(0x18, consumer.ram(0x14));
    consumer.ram_set(0x24, 0x0000ffff);
    consumer.ram_set(0x2c, 0xffff0000);
    consumer.ram_set(0x40, 0);
    consumer.doorbell();
    consumer.settle();
    assert(consumer.ram(0x28) == 0x0000ffff);  // the AOP acked the host link
    assert(consumer.ram(0x30) == 0xffff0000);  // the AOP acked the host channel
    assert(consumer.ram(0x14) == 0xffff0000);
    assert(consumer.pending() == 1);
    assert(consumer.irq.read());
    assert(consumer.status() == 0x00000000);   // client 0, signal 0
    consumer.settle();
    assert(!consumer.irq.read());

    // Selected xport_qmp.c:384-388: open aop_apps and ring the doorbell.
    consumer.ram_set(0x2c, 0x0000ffff);
    consumer.doorbell();
    consumer.settle();
    assert(consumer.ram(0x30) == 0x0000ffff);
    assert(consumer.ram(0x14) == 0x0000ffff);  // the AOP opened its channel
    assert(consumer.status() == 0x00000000);
    consumer.settle();
    assert(!consumer.irq.read());

    // One accepted clock vote. ClockQDSS.c:59-76 sends the whole 64 bytes.
    consumer.payload("{class: clock, res: qdss, val: 1}", 64);
    consumer.ram_set(0x40, 64);
    consumer.doorbell();
    consumer.settle();
    assert(consumer.ram(0x40) == 0);  // the length clears after the effect
    assert(consumer.qmp.clock_vote("qdss") == 1);
    assert(consumer.qmp.accepted_messages() == 1);
    assert(consumer.status() == 0x00000000);
    consumer.settle();
    assert(!consumer.irq.read());

    // One accepted PASR request. PASRLib.c:154-265 sends the whole 100 bytes.
    const std::string pasr =
        "{class: ddr, event: pasr, addr_hi: 0x80000000, addr_lo: 0x00000000, refresh: off}";
    assert(pasr.size() == 81);
    consumer.payload(pasr, 100);
    consumer.ram_set(0x40, 100);
    consumer.doorbell();
    consumer.settle();
    assert(consumer.ram(0x40) == 0);
    assert(consumer.qmp.pasr_requests() == 1);
    assert(consumer.qmp.accepted_messages() == 2);
    consumer.status();
    consumer.settle();
    assert(!consumer.irq.read());

    // An unknown message is held, logged, and not acknowledged.
    consumer.payload("{class: foo, res: bar, val: 3}", 64);
    consumer.ram_set(0x40, 64);
    consumer.doorbell();
    consumer.settle();
    assert(consumer.ram(0x40) == 64);
    assert(consumer.qmp.rejected_messages() == 1);
    assert(consumer.qmp.accepted_messages() == 2);
    assert(!consumer.irq.read());
    assert(consumer.status() == 0xffffffff);
    assert(consumer.pending() == 0);

    // A message without a terminator is rejected for the same reason.
    for (unsigned offset = 0x44; offset < 0x44 + 64; offset += 4)
        consumer.ram_set(offset, 0x41414141);
    consumer.ram_set(0x40, 4);
    consumer.doorbell();
    consumer.settle();
    assert(consumer.ram(0x40) == 4);
    assert(consumer.qmp.rejected_messages() == 2);
    assert(!consumer.irq.read());

    // The host abandons the held message and sends a valid one.
    consumer.ram_set(0x40, 0);
    consumer.doorbell();
    consumer.settle();
    assert(!consumer.irq.read());
    consumer.payload("{class: clock, res: qdss_ao, val: 0}", 64);
    consumer.ram_set(0x40, 64);
    consumer.doorbell();
    consumer.settle();
    assert(consumer.ram(0x40) == 0);
    assert(consumer.qmp.clock_vote("qdss_ao") == 0);
    assert(consumer.qmp.accepted_messages() == 3);

    // A cold reset republishes the AOP state and forgets the votes.
    consumer.qmp.cold_reset();
    assert(consumer.ram(0x24) == 0xffff0000);
    assert(consumer.ram(0x28) == 0);
    assert(consumer.ram(0x2c) == 0xffff0000);
    assert(consumer.ram(0x30) == 0);
    assert(consumer.qmp.clock_vote("qdss") == -1);
    assert(consumer.qmp.accepted_messages() == 0);

    std::cout << "AOP_QMP_HANDSHAKE_PASS\n";
    return 0;
}

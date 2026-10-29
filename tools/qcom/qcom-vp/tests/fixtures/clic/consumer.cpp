// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/clic/clic.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

// This fixture observes the CLIC's own outputs. It adds no peer firmware and
// no receive path. The observer records one high delta per bound output pulse.
struct PulseObserver : sc_core::sc_module {
    sc_core::sc_in<bool> in{"in"};
    unsigned rises = 0;
    bool high = false;

    SC_HAS_PROCESS(PulseObserver);
    explicit PulseObserver(sc_core::sc_module_name name) : sc_module(name) {
        SC_METHOD(sample);
        sensitive << in;
        dont_initialize();
    }
    void sample() {
        if (in.read()) {
            if (!high) ++rises;
            high = true;
        } else {
            high = false;
        }
    }
    void clear() {
        rises = 0;
        high = false;
    }
};

struct Consumer : sc_core::sc_module {
    qcom_vp::TmeClic clic{"clic"};
    // This instance keeps both peer outputs unbound to check the log path.
    qcom_vp::TmeClic unconnected{"unconnected"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> irq{"irq", 65};
    sc_core::sc_vector<sc_core::sc_signal<bool>> irq_unconnected{"irq_unconnected", 65};
    sc_core::sc_signal<bool> peer0{"peer0"};
    sc_core::sc_signal<bool> peer1{"peer1"};
    PulseObserver pulse0{"pulse0"};
    PulseObserver pulse1{"pulse1"};
    tlm_utils::simple_initiator_socket<Consumer> command{"command"};
    tlm_utils::simple_initiator_socket<Consumer> pending{"pending"};
    tlm_utils::simple_initiator_socket<Consumer> enable{"enable"};
    tlm_utils::simple_initiator_socket<Consumer> qmp{"qmp"};
    tlm_utils::simple_initiator_socket<Consumer> unconnected_command{"unconnected_command"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        clic.priority_reset = 1;
        command.bind(clic.source_socket);
        pending.bind(clic.pending_socket);
        enable.bind(clic.enable_socket);
        qmp.bind(clic.qmp_notify_socket);
        unconnected_command.bind(unconnected.source_socket);
        for (unsigned i = 0; i < irq.size(); ++i) {
            clic.irq[i].bind(irq[i]);
            unconnected.irq[i].bind(irq_unconnected[i]);
        }
        clic.peer_notify[0].bind(peer0);
        clic.peer_notify[1].bind(peer1);
        pulse0.in(peer0);
        pulse1.in(peer1);
    }

    std::uint64_t access(tlm_utils::simple_initiator_socket<Consumer>& bus,
                         std::uint64_t offset, bool write, std::uint64_t value = 0,
                         unsigned length = 4) {
        unsigned char bytes[8]{};
        for (unsigned i = 0; i < length; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        bus->b_transport(tx, delay);
        assert(tx.is_response_ok());
        std::uint64_t result = 0;
        for (unsigned i = 0; i < length; ++i) result |= std::uint64_t{bytes[i]} << (8 * i);
        return result;
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer consumer{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // Enable both software sources, so a wrong local latch would also alert.
    consumer.access(consumer.enable, 28, true, 1, 1);
    consumer.access(consumer.enable, 33, true, 1, 1);
    consumer.access(consumer.command, 0x18, true, 0x1080);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(!consumer.irq[28].read() && !consumer.irq[33].read());

    // A zero command raises no peer event.
    consumer.access(consumer.command, 0, true, 0, 1);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(consumer.pulse0.rises == 0 && consumer.pulse1.rises == 0);

    // Command bit zero drives only the first peer output, high for one delta.
    consumer.access(consumer.command, 0, true, 1, 1);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(consumer.pulse0.rises == 1 && !consumer.pulse0.high);
    assert(consumer.pulse1.rises == 0 && !consumer.pulse1.high);
    assert(!consumer.peer0.read() && !consumer.peer1.read());
    // No local receive state and no local CPU interrupt result.
    assert(consumer.access(consumer.pending, 28, false, 0, 1) == 0);
    assert(consumer.access(consumer.pending, 32, false, 0, 4) == 0);
    assert(consumer.access(consumer.command, 0x10, false) == 0);
    assert(!consumer.irq[28].read() && !consumer.irq[33].read());

    // Command bit one drives only the second peer output.
    consumer.pulse0.clear();
    consumer.pulse1.clear();
    consumer.access(consumer.command, 0, true, 2, 1);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(consumer.pulse0.rises == 0 && !consumer.pulse0.high);
    assert(consumer.pulse1.rises == 1 && !consumer.pulse1.high);

    // Both command bits raise both peer outputs.
    consumer.pulse0.clear();
    consumer.pulse1.clear();
    consumer.access(consumer.command, 0, true, 3, 1);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(consumer.pulse0.rises == 1 && consumer.pulse1.rises == 1);
    assert(!consumer.peer0.read() && !consumer.peer1.read());
    assert(consumer.access(consumer.pending, 28, false, 0, 1) == 0);
    assert(consumer.access(consumer.command, 0x10, false) == 0);
    assert(!consumer.irq[28].read() && !consumer.irq[33].read());

    // The APSS QMP write stays the receive input. Repeated writes coalesce.
    consumer.access(consumer.qmp, 0, true, 1);
    consumer.access(consumer.qmp, 0, true, 1);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(consumer.access(consumer.command, 0x10, false) == 0x80);
    assert(consumer.access(consumer.pending, 28, false, 0, 1) == 1);
    assert(consumer.irq[28].read());
    // The guest W1C clears the notification. Outgoing outputs stay idle.
    consumer.access(consumer.command, 8, true, 0x80);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(consumer.access(consumer.command, 0x10, false) == 0);
    assert(consumer.access(consumer.pending, 28, false, 0, 1) == 0);
    assert(!consumer.irq[28].read());
    assert(consumer.pulse0.rises == 1 && consumer.pulse1.rises == 1);

    // An unbound peer output logs the positive notification.
    std::ostringstream captured;
    auto* previous = std::cerr.rdbuf(captured.rdbuf());
    consumer.access(consumer.unconnected_command, 0, true, 3, 1);
    std::cerr.rdbuf(previous);
    const std::string message = captured.str();
    assert(message.find("TME_PEER_NOTIFICATION") != std::string::npos);
    assert(message.find("unrouted=3") != std::string::npos);

    std::cout << "CLIC_PEER_NOTIFICATION_PASS\n";
    return 0;
}

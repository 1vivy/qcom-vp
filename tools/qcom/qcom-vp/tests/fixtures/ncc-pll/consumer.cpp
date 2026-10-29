// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/cpucp/zonda_pll.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace {
constexpr std::uint32_t running_bit = 0x20000000;
constexpr std::uint32_t locked_bits = 0xc0000000;

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> bus{"bus"};
    qcom_vp::QcomZondaPll pll{"pll"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        bus.bind(pll.target_socket);
    }

    std::uint32_t access(unsigned offset, bool write, std::uint32_t value = 0) {
        unsigned char bytes[4]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(4);
        tx.set_streaming_width(4);
        auto delay = sc_core::SC_ZERO_TIME;
        bus->b_transport(tx, delay);
        assert(tx.is_response_ok());
        value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{bytes[i]} << (8 * i);
        return value;
    }
    void write(unsigned offset, std::uint32_t value) { access(offset, true, value); }
    std::uint32_t read(unsigned offset) { return access(offset, false); }
};

// Counts the effective-state notifications the model already publishes.
struct Monitor : sc_core::sc_module {
    SC_HAS_PROCESS(Monitor);
    unsigned state_events = 0;
    unsigned lock_events = 0;

    Monitor(sc_core::sc_module_name name, qcom_vp::QcomZondaPll& pll) : sc_module(name) {
        SC_METHOD(count_state);
        sensitive << pll.state_changed;
        dont_initialize();
        SC_METHOD(count_lock);
        sensitive << pll.lock_changed;
        dont_initialize();
    }
    void count_state() { ++state_events; }
    void count_lock() { ++lock_events; }
};
} // namespace

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer consumer{"consumer"};
    Monitor monitor{"monitor", consumer.pll};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    constexpr std::uint64_t reference_hz = 19200000;

    // Software control keeps its original acquisition deadline.
    assert(consumer.pll.main_output_hz() == 0);
    assert(!consumer.pll.hardware_controlled());
    consumer.write(0x30, 1);
    consumer.write(0x04, 96);
    consumer.write(0x0c, 3);
    consumer.write(0x00, 7);
    assert(consumer.pll.main_output_hz() == 0);
    assert(consumer.read(0) == (running_bit | 7));
    sc_core::sc_start(5, sc_core::SC_US);
    assert(consumer.pll.main_output_hz() == reference_hz * 96);
    assert(consumer.pll.even_output_hz() == reference_hz * 96);
    assert(consumer.read(0) == (running_bit | locked_bits | 7));
    sc_core::sc_start(1, sc_core::SC_NS);
    monitor.state_events = 0;
    monitor.lock_events = 0;

    // A takeover supplies the effective output and leaves software words intact.
    assert(consumer.pll.set_hardware_frequency(2400000000, 2));
    assert(consumer.pll.hardware_controlled());
    assert(consumer.pll.main_output_hz() == 2400000000);
    assert(consumer.pll.even_output_hz() == 1200000000);
    assert(consumer.read(0) == (running_bit | locked_bits | 7));
    assert(consumer.read(0x04) == 96);
    assert(consumer.read(0x0c) == 3);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(monitor.state_events == 1);
    assert(monitor.lock_events == 0);
    monitor.state_events = 0;

    // Stopping the software PLL cannot revoke the hardware output.
    consumer.write(0x00, 0);
    assert(consumer.pll.main_output_hz() == 2400000000);
    assert(consumer.pll.even_output_hz() == 1200000000);
    assert(consumer.read(0) == (running_bit | locked_bits));
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(monitor.state_events == 1);
    assert(monitor.lock_events == 0);
    monitor.state_events = 0;

    // Later software configuration is retained, not applied.
    consumer.write(0x04, 100);
    assert(consumer.read(0x04) == 100);
    assert(consumer.pll.main_output_hz() == 2400000000);
    sc_core::sc_start(1, sc_core::SC_NS);
    monitor.state_events = 0;
    monitor.lock_events = 0;

    // An invalid divider changes nothing and notifies nothing.
    assert(!consumer.pll.set_hardware_frequency(1800000000, 3));
    assert(!consumer.pll.set_hardware_frequency(0, 0));
    assert(consumer.pll.main_output_hz() == 2400000000);
    assert(consumer.pll.even_output_hz() == 1200000000);
    assert(consumer.pll.hardware_controlled());
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(monitor.state_events == 0);
    assert(monitor.lock_events == 0);

    // A zero rate disables the hardware output while ownership remains.
    assert(consumer.pll.set_hardware_frequency(0, 4));
    assert(consumer.pll.main_output_hz() == 0);
    assert(consumer.pll.even_output_hz() == 0);
    assert(consumer.read(0) == 0);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(monitor.state_events == 1);
    assert(monitor.lock_events == 0);
    monitor.state_events = 0;
    assert(consumer.pll.set_hardware_frequency(0, 8));
    assert(!consumer.pll.set_hardware_frequency(0, 16));
    assert(consumer.pll.hardware_controlled());
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(monitor.state_events == 0);
    assert(monitor.lock_events == 0);

    // Clearing ownership exposes only the real software state.
    consumer.pll.clear_hardware_frequency();
    assert(!consumer.pll.hardware_controlled());
    assert(consumer.pll.main_output_hz() == 0);
    assert(consumer.pll.even_output_hz() == 0);
    assert(consumer.read(0) == 0);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(monitor.state_events == 0);
    assert(monitor.lock_events == 0);
    consumer.pll.clear_hardware_frequency();

    // Retained software configuration reacquires through the normal deadline.
    consumer.write(0x00, 7);
    assert(consumer.pll.main_output_hz() == 0);
    assert(consumer.read(0) == (running_bit | 7));
    sc_core::sc_start(5, sc_core::SC_US);
    assert(consumer.pll.main_output_hz() == reference_hz * 100);
    assert(consumer.read(0) == (running_bit | locked_bits | 7));
    monitor.state_events = 0;
    monitor.lock_events = 0;

    // Clearing ownership while software is live restores its output.
    assert(consumer.pll.set_hardware_frequency(1600000000, 4));
    assert(consumer.pll.main_output_hz() == 1600000000);
    assert(consumer.pll.even_output_hz() == 400000000);
    consumer.pll.clear_hardware_frequency();
    assert(consumer.pll.main_output_hz() == reference_hz * 100);
    assert(consumer.read(0) == (running_bit | locked_bits | 7));
    sc_core::sc_start(1, sc_core::SC_NS);
    monitor.state_events = 0;
    monitor.lock_events = 0;

    // A pending software acquisition survives a takeover and its return.
    consumer.write(0x04, 120);
    assert(consumer.pll.main_output_hz() == 0);
    sc_core::sc_start(1, sc_core::SC_NS);
    monitor.state_events = 0;
    monitor.lock_events = 0;
    assert(consumer.pll.set_hardware_frequency(1500000000, 1));
    assert(consumer.pll.main_output_hz() == 1500000000);
    assert(consumer.pll.even_output_hz() == 1500000000);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(monitor.state_events == 1);
    assert(monitor.lock_events == 1);
    monitor.state_events = 0;
    monitor.lock_events = 0;
    consumer.pll.clear_hardware_frequency();
    assert(consumer.pll.main_output_hz() == 0);
    assert(consumer.read(0) == (running_bit | 7));
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(monitor.state_events == 1);
    assert(monitor.lock_events == 0);
    monitor.state_events = 0;
    sc_core::sc_start(5, sc_core::SC_US);
    assert(consumer.pll.main_output_hz() == reference_hz * 120);
    assert(consumer.read(0) == (running_bit | locked_bits | 7));
    assert(monitor.lock_events == 1);

    // A rejected divider never takes ownership.
    assert(!consumer.pll.set_hardware_frequency(2400000000, 6));
    assert(!consumer.pll.hardware_controlled());
    assert(consumer.pll.main_output_hz() == reference_hz * 120);
    std::cout << "ZONDA_PLL_HARDWARE_OWNERSHIP_PASS\n";
    return 0;
}

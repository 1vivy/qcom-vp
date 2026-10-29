// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/cpucp/amu.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> bus{"bus"};
    tlm_utils::simple_initiator_socket<Consumer> pll_bus{"pll_bus"};
    qcom_vp::QcomZondaPll pll{"pll"};
    qcom_vp::QcomCoreAmu amu{"amu"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        bus.bind(amu.target_socket);
        pll_bus.bind(pll.target_socket);
    }

    tlm::tlm_response_status access(unsigned offset, bool write, std::uint64_t& value,
                                    unsigned length = 4) {
        unsigned char bytes[8]{};
        for (unsigned i = 0; i < 8; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        bus->b_transport(tx, delay);
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < length; ++i) value |= std::uint64_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }
    std::uint64_t read(unsigned offset, unsigned length = 4) {
        std::uint64_t value = 0;
        assert(access(offset, false, value, length) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void write(unsigned offset, std::uint64_t value, unsigned length = 4) {
        assert(access(offset, true, value, length) == tlm::TLM_OK_RESPONSE);
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer c{"consumer"};
    c.amu.pll_path.set_value("consumer.pll");
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // Catalog constants: four architected group-0 events and eight auxiliary counters.
    assert(c.read(0x400) == 0x11 && c.read(0x404) == 0x4004);
    assert(c.read(0x408) == 0x08 && c.read(0x40c) == 0x4005);
    assert(c.read(0xce0) == 0x804 && c.read(0xe00) == 0x11003f0b);
    assert(c.read(0xfbc) == 0x47700a66 && c.read(0xff0) == 0x0d);

    // Disabled counters keep their value while time passes.
    c.pll.set_hardware_frequency(1000000000, 1);
    sc_core::sc_start(1, sc_core::SC_MS);
    assert(c.read(0x00, 8) == 0 && c.read(0x08, 8) == 0);

    // Counter 0 follows the core clock. Counter 1 follows the constant reference.
    c.write(0xc00, 3);
    assert(c.read(0xc00) == 3 && c.read(0xc20) == 3);
    sc_core::sc_start(1, sc_core::SC_MS);
    assert(c.read(0x00, 8) == 1000000 && c.read(0x08, 8) == 19200);
    c.pll.set_hardware_frequency(2000000000, 1);
    sc_core::sc_start(1, sc_core::SC_MS);
    assert(c.read(0x00, 8) == 3000000 && c.read(0x08, 8) == 38400);

    // A cleared enable freezes its counter. The other counter continues.
    c.write(0xc20, 1);
    sc_core::sc_start(1, sc_core::SC_MS);
    assert(c.read(0x00, 8) == 3000000 && c.read(0x08, 8) == 57600);

    // No source supplies retired instructions, stalls, or auxiliary events.
    std::uint64_t value = 4;
    assert(c.access(0xc00, true, value) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    value = 8;
    assert(c.access(0xc00, true, value) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    value = 1;
    assert(c.access(0xc04, true, value) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    c.write(0xc24, 0xff);
    assert(c.read(0xc04) == 0 && c.read(0x100, 8) == 0);
    assert(c.read(0xc00) == 2);

    // Counters accept 64-bit values and 32-bit halves. Auxiliary types are retained.
    c.write(0x00, 0x1122334455667788ull, 8);
    assert(c.read(0x04) == 0x11223344 && c.read(0x00) == 0x55667788);
    c.write(0x10c, 7);
    assert(c.read(0x108, 8) == 0x0000000700000000ull);
    c.write(0x484, 0x1234);
    assert(c.read(0x484) == 0x1234);

    // Fixed registers reject writes. Holes and wrong widths fail.
    value = 0;
    assert(c.access(0x400, true, value) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(c.access(0x20, false, value, 8) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.access(0x500, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.access(0xc00, false, value, 8) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.access(0x02, false, value, 4) == tlm::TLM_BURST_ERROR_RESPONSE);

    // A cold reset clears counters, enables, and auxiliary types.
    c.amu.cold_reset();
    assert(c.read(0xc00) == 0 && c.read(0x00, 8) == 0 && c.read(0x484) == 0);
    std::cout << "CORE_AMU_COUNTING_PASS" << std::endl;
    return 0;
}

// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/pdp/cprh.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>

// The PDP programs lane 0 and lane 1 through the APSS router. The CPUCP programs
// the same lane registers through its own router. Both master faces reach one
// lane state, and the two lanes stay independent.
struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> pdp0{"pdp0"}, pdp1{"pdp1"},
        cpucp0{"cpucp0"}, cpucp1{"cpucp1"};
    qcom_vp::PdpCprh cprh{"cprh"};
    qcom_vp::CprhTarget target0{"target0"}, target1{"target1"},
        target2{"target2"}, target3{"target3"};
    tlm_utils::simple_initiator_socket<Consumer> table0{"table0"}, table1{"table1"},
        table2{"table2"}, table3{"table3"};
    tlm_utils::simple_initiator_socket<Consumer> control0{"control0"},
        control1{"control1"}, control2{"control2"}, control3{"control3"};
    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        pdp0.bind(cprh.pdp_lane0);
        pdp1.bind(cprh.pdp_lane1);
        cpucp0.bind(cprh.cpucp_lane0);
        cpucp1.bind(cprh.cpucp_lane1);
        table0.bind(target0.table_socket);
        table1.bind(target1.table_socket);
        table2.bind(target2.table_socket);
        table3.bind(target3.table_socket);
        control0.bind(target0.target_socket);
        control1.bind(target1.target_socket);
        control2.bind(target2.target_socket);
        control3.bind(target3.target_socket);
    }
    // The table socket decodes rows from 0x3000 and the control word from 0x3a00.
    tlm::tlm_response_status target_access(unsigned index, unsigned offset, bool write,
                                           std::uint32_t& value) {
        unsigned char bytes[4]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(4);
        tx.set_streaming_width(4);
        auto delay = sc_core::SC_ZERO_TIME;
        switch (index) {
        case 0: table0->b_transport(tx, delay); break;
        case 1: table1->b_transport(tx, delay); break;
        case 2: table2->b_transport(tx, delay); break;
        default: table3->b_transport(tx, delay); break;
        }
        if (tx.is_response_ok()) value = qcom_vp::clock_detail::load(tx);
        return tx.get_response_status();
    }
    std::uint32_t program_row(unsigned index, unsigned row, std::uint32_t word) {
        assert(target_access(index, row * 4, true, word) == tlm::TLM_OK_RESPONSE);
        std::uint32_t readback = 0;
        assert(target_access(index, row * 4, false, readback) == tlm::TLM_OK_RESPONSE);
        return readback;
    }
    std::uint32_t program_control(unsigned index, std::uint32_t word) {
        assert(target_access(index, 0xa00, true, word) == tlm::TLM_OK_RESPONSE);
        std::uint32_t readback = 0;
        assert(target_access(index, 0xa00, false, readback) == tlm::TLM_OK_RESPONSE);
        return readback;
    }
    // master false selects the PDP, true selects the CPUCP.
    tlm::tlm_response_status access(bool master, bool lane, unsigned offset,
                                    bool write, std::uint64_t& value,
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
        if (master) {
            if (lane) cpucp1->b_transport(tx, delay);
            else cpucp0->b_transport(tx, delay);
        } else {
            if (lane) pdp1->b_transport(tx, delay);
            else pdp0->b_transport(tx, delay);
        }
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < length; ++i)
                value |= std::uint64_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }
    tlm::tlm_response_status write(bool master, bool lane, unsigned offset,
                                   std::uint64_t value, unsigned length = 4) {
        return access(master, lane, offset, true, value, length);
    }
    void put(bool master, bool lane, unsigned offset, std::uint64_t value,
             unsigned length = 4) {
        assert(write(master, lane, offset, value, length) == tlm::TLM_OK_RESPONSE);
    }
    std::uint64_t get(bool master, bool lane, unsigned offset, unsigned length = 4) {
        std::uint64_t value = 0;
        assert(access(master, lane, offset, false, value, length) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    tlm::tlm_response_status read_status(bool master, bool lane, unsigned offset,
                                         unsigned length = 4) {
        std::uint64_t value = 0;
        return access(master, lane, offset, false, value, length);
    }
};

static void expect(tlm::tlm_response_status actual, tlm::tlm_response_status wanted) {
    assert(actual == wanted);
}

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer c{"consumer"};
    c.cprh.lane0_target0_path.set_value("target0");
    c.cprh.lane0_target1_path.set_value("target1");
    c.cprh.lane1_target0_path.set_value("target2");
    c.cprh.lane1_target1_path.set_value("target3");
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // Reset state answers reads. The block never mirrors an access with a zero word.
    expect(c.read_status(false, 0, 0x100), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    expect(c.read_status(false, 0, 0x700), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    expect(c.read_status(false, 1, 0x8c0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    expect(c.read_status(false, 0, 0x19c, 8), tlm::TLM_BURST_ERROR_RESPONSE);
    // A diagnostic write cannot manufacture a busy sequencer or instruction.
    expect(c.write(false, 0, 0x338, 1), tlm::TLM_COMMAND_ERROR_RESPONSE);
    c.put(false, 0, 0x508, 0x1c);
    c.put(false, 0, 0x50c, 0x80000000u);
    assert(c.get(true, 0, 0x508) == 0x1c);
    assert(c.get(true, 0, 0x50c) == 0x80000000u);
    sc_core::sc_start(sc_core::sc_time(20, sc_core::SC_US));
    assert(c.get(false, 0, 0x330, 8) == 0);
    assert(c.get(false, 0, 0x338, 8) == 0);
    assert(c.get(false, 0, 0x340, 8) == 0);

    // Both masters share the slots, while the lanes stay independent.
    c.put(false, 0, 0x198, 0x1234);
    assert(c.get(true, 0, 0x198) == 0x1234);
    c.put(true, 1, 0x324, 0x5678);
    assert(c.get(false, 1, 0x324) == 0x5678);
    assert(c.get(true, 1, 0x198) == 0);
    assert(c.get(false, 0, 0x324) == 0);
    // An eight-byte transfer across an undeclared slot changes neither word.
    expect(c.write(true, 0, 0x198, 0xaabbccddeeff0011ull, 8),
           tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.get(false, 0, 0x198) == 0x1234);
    expect(c.read_status(false, 0, 0x19c), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    c.put(true, 0, 0x1d0, 0xaabbccddeeff0011ull, 8);
    assert(c.get(false, 0, 0x1d0) == 0xeeff0011);
    assert(c.get(false, 0, 0x1d4) == 0xaabbccdd);

    // Doubleword registers take a native write and either 32-bit half afterwards.
    c.put(false, 0, 0x5e0, 0x1122334455667788ull, 8);
    assert(c.get(true, 0, 0x5e0, 8) == 0x1122334455667788ull);
    c.put(true, 0, 0x5e4, 0xdeadbeefu);
    assert(c.get(false, 0, 0x5e0, 8) == 0xdeadbeef55667788ull);
    assert(c.get(false, 1, 0x5e0, 8) == 0);

    // Independent LUT endpoints and the last configuration bank retain values.
    c.put(false, 0, 0x5f0, 0x3030100ull, 8);
    c.put(false, 0, 0x6d8, 5, 8);
    c.put(false, 0, 0xb58, 7, 8);
    assert(c.get(true, 0, 0x5f0, 8) == 0x3030100ull);
    assert(c.get(true, 0, 0x6d8, 8) == 5);
    assert(c.get(true, 0, 0xb58, 8) == 7);
    // The lane loop increments its cursor before it accesses the second bank.
    c.put(false, 0, 0x950, 0x12, 8);
    c.put(true, 0, 0xa48, 0x34, 8);
    assert(c.get(true, 0, 0x950, 8) == 0x12);
    assert(c.get(false, 0, 0xa48, 8) == 0x34);
    expect(c.read_status(true, 0, 0x948, 8), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    expect(c.read_status(true, 0, 0xa50, 8), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    // A power profile needs programmed corners in both targets.
    c.put(false, 0, 0x5f0, 0x28060ull, 8);
    assert(!c.cprh.can_apply_power_state(0, 0));
    assert(!c.cprh.apply_power_state(0, 0));
    assert(!c.cprh.power_state_ready(0, 0));
    assert(!c.cprh.can_apply_power_state(2, 0));
    assert(!c.cprh.can_apply_power_state(0, 32));

    for (unsigned index = 0; index < 4; ++index) {
        assert(c.program_row(index, 3, 0x2a10) == 0x2a10);
        assert(c.program_row(index, 5, 0x3f06) == 0x3f06);
        assert(c.program_row(index, 7, 0x0c05) == 0x0c05);
        assert(c.program_row(index, 9, 0x0d04) == 0x0d04);
        assert(c.program_control(index, 0x04000081) == 0x04000081);
    }

    // Lane 0 applies corner 3 and corner 5 and keeps the lane-1 owners idle.
    assert(c.cprh.can_apply_power_state(0, 0));
    assert(c.cprh.apply_power_state(0, 0));
    assert(c.cprh.power_state_ready(0, 0));
    assert(c.target0.effective_corner() == 3);
    assert(c.target1.effective_corner() == 5);
    assert(c.target0.effective_voltage_mv() == 212);
    assert(!c.target2.has_effective_corner());
    assert(!c.target3.has_effective_corner());
    assert(!c.cprh.power_state_ready(1, 0));
    assert(!c.cprh.apply_power_state(1, 0));

    // Lane 1 has its own row and its own owners.
    c.put(true, 1, 0x5f0, 0x480e0ull, 8);
    assert(c.cprh.apply_power_state(1, 0));
    assert(c.cprh.power_state_ready(1, 0));
    assert(c.target2.effective_corner() == 7);
    assert(c.target3.effective_corner() == 9);
    assert(c.target0.effective_corner() == 3);
    assert(c.target1.effective_corner() == 5);

    // The second target cannot take corner 31, so lane 0 state 1 changes nothing.
    c.put(false, 0, 0x5f8, 0xf8060ull, 8);
    assert(!c.cprh.can_apply_power_state(0, 1));
    assert(!c.cprh.apply_power_state(0, 1));
    assert(c.target0.effective_corner() == 3);
    assert(c.target1.effective_corner() == 5);
    assert(c.get(false, 0, 0x5f8, 8) == 0xf8060ull);

    // NSI states can exceed the programmed corner range, including with array overrides.
    c.put(false, 0, 0x600,
        (3ull << 20) | (31ull << 10) | 31 | (7ull << 5) | (9ull << 15), 8);
    assert(c.cprh.apply_power_state(0, 2));
    assert(c.target0.effective_corner() == 7);
    assert(c.target1.effective_corner() == 9);
    assert(c.target0.effective_voltage_mv() == 152);
    assert(c.cprh.power_state_ready(0, 2));
    assert(!c.cprh.power_state_ready(0, 0));

    // A new control word makes the applied corner stale until a new apply.
    assert(c.program_control(1, 0x06000081) == 0x06000081);
    assert(!c.cprh.power_state_ready(0, 2));
    assert(c.cprh.apply_power_state(0, 2));
    assert(c.cprh.power_state_ready(0, 2));
    assert(c.target1.effective_voltage_mv() == 231);

    // Gaps between the declared banks stay errors.
    expect(c.read_status(true, 0, 0x4d0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    expect(c.read_status(true, 0, 0x6f0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    expect(c.read_status(true, 0, 0x8c0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    expect(c.read_status(true, 0, 0xb60), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    std::cout << "PDP_CPRH_CONFIGURATION_PASS" << std::endl;
    return 0;
}

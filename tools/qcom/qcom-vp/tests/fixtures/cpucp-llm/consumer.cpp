// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/cpucp/llm.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>

// Lane zero declares six core pairs from sample index five.
// Lane one declares two core pairs from sample index seven.
struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> bus0{"bus0"}, bus1{"bus1"};
    qcom_vp::QcomCpussLlm lane0{"lane0"}, lane1{"lane1"};
    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        lane1.first_sensor = 7;
        lane1.core_count = 2;
        bus0.bind(lane0.target_socket);
        bus1.bind(lane1.target_socket);
    }
    tlm::tlm_response_status access(bool lane, unsigned offset, bool write,
                                    std::uint32_t& value, unsigned length = 4) {
        unsigned char bytes[4]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        if (lane) bus1->b_transport(tx, delay);
        else bus0->b_transport(tx, delay);
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }
    void write(bool lane, unsigned offset, std::uint32_t value) {
        assert(access(lane, offset, true, value) == tlm::TLM_OK_RESPONSE);
    }
    std::uint32_t read(bool lane, unsigned offset) {
        std::uint32_t value = 0;
        assert(access(lane, offset, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer c{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    // The PDP initialization tables write these words, and later paths re-read them.
    const unsigned configuration[]{
        0x380, 0x410, 0x450, 0x800, 0x880, 0x890, 0x894, 0x898, 0x89c, 0x900, 0x980,
        0xb10, 0xb18, 0x1000, 0x1098, 0x1118, 0x111c, 0x1124, 0x1b90, 0x1c8c,
        0x2390, 0x23a0, 0x23cc, 0x2410, 0x2490, 0x2514, 0x2794, 0x2814, 0x2980,
        0x2a10, 0x2a40, 0x2a60};
    for (unsigned i = 0; i < sizeof(configuration) / sizeof(configuration[0]); ++i) {
        assert(c.read(0, configuration[i]) == 0);
        c.write(0, configuration[i], 0x10000u + i);
        assert(c.read(0, configuration[i]) == 0x10000u + i);
    }
    // The trip-point bank holds sixteen words, and the bitmap at 0x1000 selects them.
    for (unsigned index = 0; index < 16; ++index) {
        c.write(0, 0x1110 + 4 * index, 0x10000u + index);
        assert(c.read(0, 0x1110 + 4 * index) == 0x10000u + index);
    }
    // The limit path updates the low half of the offset word at 0x2a60.
    c.write(0, 0x2a60, 0x00010002);
    c.write(0, 0x2a60, (c.read(0, 0x2a60) & 0xffff0000u) | 0x0003);
    assert(c.read(0, 0x2a60) == 0x00010003);
    // Both lanes report the declared 25 degrees with validity bit 16.
    for (unsigned index = 5; index < 17; ++index)
        assert(c.read(0, 0x4e0 + 4 * index) == 0x100fa);
    assert(c.read(0, 0x560) == 0x100fa);
    for (unsigned index = 7; index < 11; ++index)
        assert(c.read(1, 0x4e0 + 4 * index) == 0x100fa);
    assert(c.read(1, 0x560) == 0x100fa);
    // A sample word is a virtual measurement, and lane one has no word at 0x4f4.
    std::uint32_t value = 0;
    assert(c.access(0, 0x4f4, true, value) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(c.access(1, 0x4f4, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    // Unlisted offsets stay errors, including the gaps inside a written group.
    for (const unsigned offset : {0x1c10u, 0x1d0u, 0x4f0u, 0x564u, 0x1150u, 0x8000u}) {
        assert(c.access(0, offset, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(c.access(0, offset, true, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    assert(c.access(0, 0x380, false, value, 2) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.access(0, 0x382, false, value) == tlm::TLM_BURST_ERROR_RESPONSE);
    // Cold reset clears one instance and keeps the other lane and the samples.
    c.write(1, 0x380, 0x12345678);
    c.lane0.cold_reset();
    assert(c.read(0, 0x380) == 0);
    assert(c.read(1, 0x380) == 0x12345678);
    assert(c.read(0, 0x4f4) == 0x100fa);
    std::cout << "CPUSS_LLM_CONFIGURATION_PASS\n";
    return 0;
}

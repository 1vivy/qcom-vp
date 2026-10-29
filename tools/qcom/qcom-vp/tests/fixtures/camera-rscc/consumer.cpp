// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/rpmh/rscc.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <iostream>

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> bus{"bus"};
    qcom_vp::QcomTitanRscc rscc{"rscc"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        bus.bind(rscc.target_socket);
    }

    bool access(unsigned offset, bool write, std::uint32_t& value) {
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
        if (!tx.is_response_ok()) return false;
        value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{bytes[i]} << (8 * i);
        return true;
    }
    void write(unsigned offset, std::uint32_t value) { assert(access(offset, true, value)); }
    std::uint32_t read(unsigned offset) {
        std::uint32_t value = 0;
        assert(access(offset, false, value));
        return value;
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer consumer{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // RscDxe rsc_init reads the configuration words in this order.
    const auto solver = consumer.read(0x4);
    assert((solver & 0x071f1f1f) == solver);
    const auto config = consumer.read(0x8);
    assert(((config >> 24) & 0x7) == 3);
    assert(((config >> 16) & 0xff) >= 16);
    assert(((config >> 8) & 0xf) == 2);
    assert((config & 0x1f) == 0x14);
    const auto parent = consumer.read(0xc);
    assert(((parent >> 27) & 0x1f) == 0xc);
    assert(((parent >> 6) & 0x3f) == 4);

    // rsc_init reads the identification fields after the parameters.
    const auto id = consumer.read(0x0);
    assert(((id >> 16) & 0xff) == 3);
    assert(((id >> 8) & 0xff) == 8);
    assert((id & 0xff) == 0);

    // rsc_set_low_power_mode writes the branch table and the override address.
    assert(consumer.read(0x420) == 0);
    consumer.write(0x424, 0x8);
    consumer.write(0x428, 0x18);
    consumer.write(0x42c, 0x1c);
    consumer.write(0x430, 0x2e);
    consumer.write(0x434, 0x36);
    consumer.write(0x438, 0x46);
    consumer.write(0x43c, 0x4a);
    consumer.write(0x440, 0x5c);
    assert(consumer.read(0x424) == 0x8);
    assert(consumer.read(0x440) == 0x5c);
    // The valid bit is set first, then the start address.
    consumer.write(0x220, 0x80000000);
    assert(consumer.read(0x220) == 0x80000000);
    consumer.write(0x220, consumer.read(0x220));
    assert(consumer.read(0x220) == 0x80000000);

    // The 32-bit camera sequence uses 24 retained words in the CHN_TCS memory.
    for (std::uint32_t word = 0; word < 24; ++word)
        consumer.write(0x4b8 + 4 * word, 0xa5a50000u | word);
    for (std::uint32_t word = 0; word < 24; ++word) {
        assert(consumer.read(0x4b8 + 4 * word) == (0xa5a50000u | word));
        consumer.write(0x4b8 + 4 * word, consumer.read(0x4b8 + 4 * word));
    }
    assert(consumer.read(0x4b8) == 0xa5a50000u);
    assert(consumer.read(0x4b8 + 4 * 23) == 0xa5a50017u);

    // RscDxe 0x38f0 selects the PDC timer owner with a masked write of bits 1:0.
    consumer.write(0x1c, (consumer.read(0x1c) & ~3u) | 2);
    assert(consumer.read(0x1c) == 2);

    std::uint32_t value = 1;
    // Read-only parameters do not accept a write.
    assert(!consumer.access(0x0, true, value));
    assert(!consumer.access(0x8, true, value));
    // No hidden TCS exists, so its trigger bit fails.
    value = 0x10002;
    assert(!consumer.access(0x1c, true, value));
    value = 1;
    // Fields outside the register masks fail.
    value = 0x400;
    assert(!consumer.access(0x424, true, value));
    value = 0xc0000000;
    assert(!consumer.access(0x220, true, value));
    // The out-of-model range fails.
    // No sequencer exists, so busy, the program counter, and the trigger fail.
    assert(!consumer.access(0x224, false, value));
    assert(!consumer.access(0x228, false, value));
    assert(!consumer.access(0x280, true, value));
    assert(!consumer.access(0x4b4, false, value));
    assert(!consumer.access(0x518, false, value));
    assert(!consumer.access(0x2dc, false, value));
    assert(!consumer.access(0x370, false, value));
    std::cout << "CAMERA_RSCC_SEQUENCE_UPLOAD_PASS\n";
    return 0;
}

// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/rpmh/pcie_rscc.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <iostream>

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> bus{"bus"};
    qcom_vp::QcomPcieRscc rscc{"rscc"};

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

    // RscDxe reads the parameters first, then the identification fields.
    assert(consumer.read(0x4) == 0x00010100);
    const auto config = consumer.read(0x8);
    assert(((config >> 24) & 0x7) == 1);
    assert(((config >> 16) & 0xff) == 0x10);
    assert(((config >> 8) & 0xf) == 2);
    assert((config & 0x1f) == 0x14);
    const auto parent = consumer.read(0xc);
    assert(((parent >> 27) & 0x1f) == 1);
    assert((parent & 0x3f) == 1);
    const auto id = consumer.read(0x0);
    assert(((id >> 16) & 0xff) == 3);
    assert(((id >> 8) & 0xff) == 8);
    assert((id & 0xff) == 0);

    // The copy routine limits the sequence to NUM_SEQ_CMD_WORDS and uses slots
    // 0x370 to 0x3ac. Each store follows a masked-write read of the same slot.
    const std::uint32_t sequence[5] = {0xe5a0e3a3, 0x81e1a183, 0xe2a2e0a5,
                                       0x84e4a482, 0x00000020};
    for (std::uint32_t slot = 0; slot < 5; ++slot)
        consumer.write(0x370 + 4 * slot, sequence[slot]);
    for (std::uint32_t slot = 0; slot < 5; ++slot) {
        assert(consumer.read(0x370 + 4 * slot) == sequence[slot]);
        consumer.write(0x370 + 4 * slot, consumer.read(0x370 + 4 * slot));
    }
    assert(consumer.read(0x370) == 0xe5a0e3a3);
    assert(consumer.read(0x380) == 0x00000020);
    assert(consumer.read(0x384) == 0);
    assert(consumer.read(0x3ac) == 0);

    // The driver sets the PDC timer owner with a masked write of bits 1:0.
    consumer.write(0x1c, (consumer.read(0x1c) & ~3u) | 2);
    assert(consumer.read(0x1c) == 2);

    // The hidden-TCS command data words come from rsc_hal_set_hidden_tcs_init.
    consumer.write(0x38, 0x80ffffff);
    consumer.write(0x40, 0xffffffff);
    consumer.write(0x48, 0x80000021);
    assert(consumer.read(0x38) == 0x80ffffff);
    assert(consumer.read(0x40) == 0xffffffff);
    assert(consumer.read(0x48) == 0x80000021);

    // The valid bit is set first, then the start address.
    consumer.write(0x220, 0x80000000);
    assert(consumer.read(0x220) == 0x80000000);
    consumer.write(0x220, consumer.read(0x220));
    assert(consumer.read(0x220) == 0x80000000);

    std::uint32_t value = 1;
    // Read-only parameters do not accept a write.
    assert(!consumer.access(0x0, true, value));
    assert(!consumer.access(0x8, true, value));
    // The hidden-TCS trigger bit is outside the owner field.
    value = 0x10002;
    assert(!consumer.access(0x1c, true, value));
    // Reserved bits and fields outside the masks fail.
    value = 0x01000000;
    assert(!consumer.access(0x38, true, value));
    value = 0x40000000;
    assert(!consumer.access(0x220, true, value));
    // The selected mode writes no branch address, so the branch table has no model.
    assert(!consumer.access(0x2dc, false, value));
    // No sequencer, program counter, timestamps, or force wake exist here.
    assert(!consumer.access(0x224, false, value));
    assert(!consumer.access(0x228, false, value));
    assert(!consumer.access(0x280, true, value));
    assert(!consumer.access(0x120, false, value));
    // The out-of-model range fails.
    assert(!consumer.access(0x36c, false, value));
    assert(!consumer.access(0x3b0, false, value));
    assert(!consumer.access(0x34, false, value));
    std::cout << "PCIE_RSCC_CONFIGURATION_PASS\n";
    return 0;
}

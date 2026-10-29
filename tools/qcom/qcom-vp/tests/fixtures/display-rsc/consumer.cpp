// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/rpmh/display_rsc.h"
#include "qcom/rpmh/display_pdc.h"
#include "qcom/rpmh/cesta.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <iostream>

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> display_bus{"display_bus"};
    tlm_utils::simple_initiator_socket<Consumer> owner_bus{"owner_bus"};
    tlm_utils::simple_initiator_socket<Consumer> pdc_bus{"pdc_bus"};
    tlm_utils::simple_initiator_socket<Consumer> pdc_commands{"pdc_commands"};
    tlm_utils::simple_initiator_socket<Consumer> crmb{"crmb"};
    tlm_utils::simple_initiator_socket<Consumer> crmb_pt{"crmb_pt"};
    tlm_utils::simple_initiator_socket<Consumer> crmc{"crmc"};
    tlm_utils::simple_initiator_socket<Consumer> crmv{"crmv"};
    tlm_utils::simple_initiator_socket<Consumer> common{"common"};
    qcom_vp::QcomDisplayPdc pdc{"pdc"};
    qcom_vp::QcomCesta cesta{"cesta"};
    qcom_vp::QcomRpmhRsc owner{"owner"};
    qcom_vp::QcomDisplayRsc display{"display"};
    sc_core::sc_signal<bool> irq{"irq"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        owner.banks.set_value("0 3 1");
        owner.resources.set_value("0x30000");
        display.banks.set_value("0 3 1");
        display.commands.set_value(12);
        display.tcs_offset.set_value(0x520);
        display.tcs_stride.set_value(0x150);
        display.requester_base.set_value(1);
        display.resource_owner_path.set_value("owner");
        display_bus.bind(display.target_socket);
        owner_bus.bind(owner.target_socket);
        display.irq_0.bind(irq);
        pdc.command_table.set_value("0 0 0x30000 4;1 2 0x30010 7");
        pdc_bus.bind(pdc.target_socket);
        pdc_commands.bind(pdc.command_socket);
        // The selected DCP program registers, as platforms/qcom/sm88xx/dcp-runtime.lua declares them.
        cesta.crmb_registers.set_value("0 0x68:12");
        cesta.crmb_pt_registers.set_value("4:5:0x14 8:5:0x14 0x10:5:0x14");
        cesta.crmc_registers.set_value("0xc 0x28 0xdc 0x268 0x4ec:27 0x56c:98");
        cesta.crmv_registers.set_value("0:5:0x40 0x38:5:0x40");
        cesta.common_registers.set_value("0xc");
        crmb.bind(cesta.crmb_socket);
        crmb_pt.bind(cesta.crmb_pt_socket);
        crmc.bind(cesta.crmc_socket);
        crmv.bind(cesta.crmv_socket);
        common.bind(cesta.common_socket);
    }

    bool access(unsigned offset, bool write, std::uint32_t& value) {
        return access(display_bus, offset, write, value);
    }
    bool access(tlm_utils::simple_initiator_socket<Consumer>& bus,
                unsigned offset, bool write, std::uint32_t& value) {
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
    assert(((consumer.read(8) >> 16) & 0xff) >= 11);
    consumer.write(0x124, 1);
    consumer.write(0x370, 0);
    assert(consumer.read(0x130) == 0);
    consumer.write(0x220, 0x80000000);
    assert(consumer.read(0x130) == 0);
    std::uint32_t value = 0;
    assert(!consumer.access(0x4bc, false, value));
    assert(!consumer.access(0x4510, false, value));
    consumer.write(0x520, 1);
    consumer.write(0x554, 0x10108);
    consumer.write(0x558, 0x30000);
    consumer.write(0x55c, 7);
    consumer.write(0x54c, 1);
    consumer.write(0x544, 0x10000);
    assert(consumer.read(0x524) == 0);
    assert(!consumer.owner.resource_state(0x30000, value));
    consumer.write(0x544, 0x1010000);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    assert(consumer.irq.read());
    assert(consumer.owner.resource_state(0x30000, value) && value == 7);
    assert(consumer.read(0x560) == 0x10101);
    assert(consumer.read(0x130) == 0);
    consumer.write(0x528, 1);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    assert(!consumer.irq.read());
    consumer.display.timestamp_event(0, 0x123456789abcdef0ULL);
    assert(consumer.read(0x130) == 1);
    assert(consumer.read(0x128) == 0x9abcdef0);
    assert(consumer.read(0x12c) == 0x345678);
    consumer.display.timestamp_event(1, 1);
    assert(consumer.read(0x150) == 0);
    auto read = [&](auto& bus, unsigned offset) {
        std::uint32_t result = 0;
        assert(consumer.access(bus, offset, false, result));
        return result;
    };
    auto write = [&](auto& bus, unsigned offset, std::uint32_t data) {
        assert(consumer.access(bus, offset, true, data));
    };
    assert(read(consumer.pdc_bus, 0x1004) == 0x260);
    assert(read(consumer.pdc_bus, 0x5000) == 1);
    assert(read(consumer.pdc_bus, 0x50c8) == 4);
    assert(read(consumer.pdc_commands, 0) == 0x30000);
    assert(read(consumer.pdc_commands, 4) == 4);
    assert(read(consumer.pdc_commands, 0xe8) == 0x30010);
    write(consumer.pdc_bus, 0x11c, 0xc);
    for (unsigned slot = 0; slot < 4; ++slot)
        for (unsigned member = 0; member < 12; member += 4)
            assert(read(consumer.pdc_bus, 0x1200 + 0x14 * slot + member) == 0);
    value = 1;
    assert(!consumer.access(consumer.pdc_bus, 0x1004, true, value));
    assert(!consumer.access(consumer.pdc_commands, 0, true, value));
    assert(!consumer.access(consumer.pdc_bus, 0x160, false, value));
    write(consumer.crmc, 0x56c, 0x12345678);
    write(consumer.crmc, 0x4ec, 0x10);
    write(consumer.crmc, 0xdc, 1);
    assert(read(consumer.crmc, 0xdc) == 1);
    assert(read(consumer.crmc, 0x28) == 0);
    assert(read(consumer.crmc, 0x268) == 0);
    assert(!consumer.access(consumer.crmc, 0x558, false, value));
    assert(!consumer.access(consumer.crmb, 4, false, value));
    assert(!consumer.access(consumer.crmb_pt, 0, false, value));
    assert(!consumer.access(consumer.crmv, 4, false, value));
    assert(!consumer.access(consumer.common, 0, false, value));
    std::cout << "DISPLAY_RSC_CAUSAL_COMPLETION_PASS\n";
    return 0;
}

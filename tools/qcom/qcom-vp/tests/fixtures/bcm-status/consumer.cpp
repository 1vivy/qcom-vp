// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/rpmh/bcm_status.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <iostream>

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> rsc_bus{"rsc_bus"};
    tlm_utils::simple_initiator_socket<Consumer> status_bus{"status_bus"};
    qcom_vp::QcomRpmhRsc owner{"owner"};
    qcom_vp::QcomBcmStatus status{"status"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        owner.banks.set_value("0 3 1;2 3 1");
        owner.resources.set_value("0x50000,0x50004,0x50010");
        status.resource_owner_path.set_value("owner");
        status.resource_vcds.set_value("0x50000 0;0x50004 0;0x50010 3");
        rsc_bus.bind(owner.target_socket);
        status_bus.bind(status.target_socket);
    }

    std::uint32_t access(tlm_utils::simple_initiator_socket<Consumer>& bus,
                         unsigned offset, bool write, std::uint32_t value = 0) {
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

    void submit(unsigned drv, std::uint32_t resource, std::uint32_t vote) {
        const auto base = drv * 0x10000;
        access(rsc_bus, base + 0xd24, true, 0x10000);
        access(rsc_bus, base + 0xd34, true, 0x10108);
        access(rsc_bus, base + 0xd38, true, resource);
        access(rsc_bus, base + 0xd3c, true, vote);
        access(rsc_bus, base + 0xd2c, true, 1);
        access(rsc_bus, base + 0xd24, true, 0x1010000);
    }
    std::uint32_t vcd(unsigned id) { return access(status_bus, 0x800 + 4 * id, false); }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer consumer{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    assert(consumer.vcd(0) == 0);
    assert(consumer.vcd(3) == 0);
    consumer.submit(0, 0x50000, 0x20000003);
    assert(consumer.vcd(0) == 1);
    assert(consumer.vcd(3) == 0);
    std::uint32_t value = 0;
    assert(!consumer.owner.resource_state(0x50000, value));
    consumer.submit(0, 0x50004, 0x60000005);
    assert(consumer.vcd(0) == 0);
    assert(consumer.owner.resource_state(0x50000, value) && value == 0x20000003);
    consumer.submit(2, 0x50004, 0x20000009);
    assert(consumer.vcd(0) == 1);
    consumer.submit(0, 0x50000, 0x60000008);
    assert(consumer.vcd(0) == 1);
    consumer.submit(2, 0x50010, 0x60000004);
    assert(consumer.vcd(0) == 0);
    assert(consumer.vcd(3) == 0);
    assert(consumer.owner.resource_state(0x50004, value) && value == 0x20000009);
    std::cout << "BCM_STAGED_VOTE_COMMIT_PASS\n";
    return 0;
}

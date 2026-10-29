// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/dcp/dsi_interrupts.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <iostream>

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> bus0{"bus0"};
    tlm_utils::simple_initiator_socket<Consumer> bus1{"bus1"};
    qcom_vp::QcomDsiInterrupts dsi0{"dsi0"};
    qcom_vp::QcomDsiInterrupts dsi1{"dsi1"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        bus0.bind(dsi0.target_socket);
        bus1.bind(dsi1.target_socket);
    }

    tlm::tlm_response_status access(unsigned controller, unsigned offset, bool write,
                                  std::uint32_t& value, unsigned length = 4,
                                  bool byte_enable = false) {
        unsigned char bytes[4]{};
        unsigned char enables[4]{0xff, 0xff, 0xff, 0xff};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        if (byte_enable) {
            tx.set_byte_enable_ptr(enables);
            tx.set_byte_enable_length(4);
        }
        auto delay = sc_core::SC_ZERO_TIME;
        if (controller) bus1->b_transport(tx, delay);
        else bus0->b_transport(tx, delay);
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }
    void write(unsigned controller, std::uint32_t value) {
        assert(access(controller, 0x110, true, value) == tlm::TLM_OK_RESPONSE);
    }
    std::uint32_t read(unsigned controller) {
        std::uint32_t value = 0;
        assert(access(controller, 0x110, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer consumer{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    assert(consumer.read(0) == 0 && consumer.read(1) == 0);
    consumer.write(0, 0);
    consumer.write(0, 0x55115505);
    assert(consumer.read(0) == 0);
    consumer.write(0, 0xffffffff);
    assert(consumer.read(0) == 0xaa22aa0a);
    assert(consumer.read(1) == 0);
    consumer.write(0, 0xaa22aa0b);
    assert(consumer.read(0) == 0xaa22aa0a);
    consumer.write(1, 2);
    assert(consumer.read(1) == 2);
    consumer.dsi0.cold_reset();
    assert(consumer.read(0) == 0 && consumer.read(1) == 2);
    std::uint32_t value = 0;
    assert(consumer.access(0, 0x114, true, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(consumer.access(0, 0x10c, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(consumer.access(0, 0x111, true, value) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(consumer.access(0, 0x110, true, value, 1) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(consumer.access(1, 0x110, true, value, 4, true) == tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
    assert(consumer.read(1) == 2);
    std::cout << "DSI_INTERRUPTS_NO_SYNTHETIC_EVENT_PASS\n";
    return 0;
}

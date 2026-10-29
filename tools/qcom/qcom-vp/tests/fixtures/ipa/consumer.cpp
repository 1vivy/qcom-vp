// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/ipa/ipa.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <iostream>

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> cfg{"cfg"}, gsi{"gsi"};
    qcom_vp::QcomIpaV5 ipa{"ipa"};
    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        cfg.bind(ipa.cfg_socket);
        gsi.bind(ipa.gsi_socket);
    }
    tlm::tlm_response_status access(bool config, unsigned offset, bool write,
                                  std::uint32_t& value, unsigned length = 4) {
        unsigned char bytes[4]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(config ? offset - 0x8000 : offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        if (config) cfg->b_transport(tx, delay);
        else gsi->b_transport(tx, delay);
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }
    void write(bool cfg, unsigned offset, std::uint32_t value) {
        assert(access(cfg, offset, true, value) == tlm::TLM_OK_RESPONSE);
    }
    std::uint32_t read(bool cfg, unsigned offset) {
        std::uint32_t value = 0;
        assert(access(cfg, offset, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer c{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    for (const auto base : {0x8800u, 0x9000u, 0x9400u, 0x9800u}) {
        const unsigned count = base == 0x9800 ? 32 : 36;
        for (unsigned i = 0; i < count; ++i) {
            assert(c.read(true, base + 4 * i) == 0);
            c.write(true, base + 4 * i, 0xffffffff - i);
            assert(c.read(true, base + 4 * i) == 0xffffffff - i);
        }
    }
    for (unsigned i = 0; i < 4; ++i) {
        c.write(true, 0x82c4 + 4 * i, 0x30000303 + i);
        assert(c.read(true, 0x82c4 + 4 * i) == 0x30000303 + i);
    }
    for (unsigned ee = 0; ee < 3; ++ee) {
        const unsigned count = ee == 0 ? 26 : ee == 1 ? 9 : 1;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned offset = 0x9000 + (ee << 10) + (i << 2);
            c.write(false, offset, 100 * ee + i);
            assert(c.read(false, offset) == 100 * ee + i);
        }
    }
    const unsigned iram_ptrs[]{0x400,0x418,0x424,0x428,0x404,0x42c,0x44c,0x430,0x434,
                         0x438,0x420,0x444,0x43c,0x41c,0x448,0x440,0x408,0x414,0x470};
    for (unsigned i = 0; i < 19; ++i) {
        c.write(false, iram_ptrs[i], i + 1);
        assert(c.read(false, iram_ptrs[i]) == i + 1);
    }
    c.write(false, 0x18, 0x11223344);
    c.write(false, 0x1c, 0x55667788);
    const auto pointer = c.read(false, 0x25c) & 0xffff;
    assert(pointer == 0x3fb);
    const auto canary = 0x1ffc + (pointer << 3);
    assert(canary == 0x3fd4);
    c.write(false, canary, 0xdeadbeef);
    assert(c.read(false, canary) == 0xdeadbeef);
    std::uint32_t value = 0;
    for (const unsigned offset : {0x20u, 0x25cu, 0x40cu, 0x9068u, 0x9424u, 0x9804u, canary - 4})
        assert(c.access(false, offset, true, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.access(true, 0x8890, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.access(true, 0x8801, false, value) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.access(false, 0x18, false, value, 1) == tlm::TLM_BURST_ERROR_RESPONSE);
    c.ipa.cold_reset();
    assert(c.read(true, 0x8800) == 0 && c.read(false, 0x9000) == 0);
    assert(c.read(false, canary) == 0 && c.read(false, 0x18) == 0);
    assert(c.read(false, 0x25c) == pointer);
    std::cout << "IPA_SECURE_CONFIGURATION_PASS\n";
    return 0;
}

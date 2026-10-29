// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/cpucp/clock_gating_overrides.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>

// The NSI control-plane layout: CGC at +0, RIF_CGC at +8, CGC2 at +0x20.
// A 32-bit clock-controller override follows at +0x30.
struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> bus{"bus"};
    qcom_vp::QcomClockGatingOverrides bank{"bank"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) { bus.bind(bank.target_socket); }

    tlm::tlm_response_status access(unsigned offset, bool write, std::uint64_t& value,
                                    unsigned length = 8) {
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
    std::uint64_t read(unsigned offset, unsigned length = 8) {
        std::uint64_t value = 0;
        assert(access(offset, false, value, length) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void write(unsigned offset, std::uint64_t value, unsigned length = 8) {
        assert(access(offset, true, value, length) == tlm::TLM_OK_RESPONSE);
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer c{"consumer"};
    c.bank.words.set_value("0:ffffffffffffffff:ffffffffffffffff 8:1:1 "
                           "20:ffffffffffffffff:ffffffffffffffff 30:60000000:ffffffff:4");
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // Declared reset values come from the catalog.
    assert(c.read(0x00) == ~0ull);
    assert(c.read(0x08) == 1);
    assert(c.read(0x20) == ~0ull);
    // CPUCP 0x1d808d08 clears the overrides with 64-bit read-modify-write.
    c.write(0x00, 0);
    c.write(0x20, c.read(0x20) & (~0ull << 40));
    c.write(0x08, c.read(0x08) & ~1ull);
    assert(c.read(0x00) == 0 && c.read(0x08) == 0);
    assert(c.read(0x20) == 0xffffff0000000000ull);
    // A 32-bit half merges into the doubleword.
    c.write(0x24, 0x12345678, 4);
    assert(c.read(0x20) == 0x1234567800000000ull);
    assert(c.read(0x24, 4) == 0x12345678 && c.read(0x20, 4) == 0);
    // Only the override bit of RIF_CGC is writable. A rejected write keeps the word.
    std::uint64_t value = 2;
    assert(c.access(0x08, true, value) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(c.read(0x08) == 0);
    // Undeclared offsets, odd widths, and misalignment fail.
    for (const unsigned offset : {0x10u, 0x18u}) {
        assert(c.access(offset, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(c.access(offset, true, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    assert(c.access(0x04, false, value) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.access(0x00, false, value, 2) == tlm::TLM_BURST_ERROR_RESPONSE);
    // A 32-bit word answers only 32-bit accesses at its own offset.
    assert(c.read(0x30, 4) == 0x60000000);
    c.write(0x30, c.read(0x30, 4) & 0x9fffffffu, 4);
    assert(c.read(0x30, 4) == 0);
    assert(c.access(0x30, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.access(0x34, false, value, 4) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    // A cold reset restores the declared values.
    c.bank.cold_reset();
    assert(c.read(0x00) == ~0ull && c.read(0x08) == 1 && c.read(0x30, 4) == 0x60000000);
    std::cout << "CLOCK_GATING_OVERRIDES_PASS" << std::endl;
    return 0;
}

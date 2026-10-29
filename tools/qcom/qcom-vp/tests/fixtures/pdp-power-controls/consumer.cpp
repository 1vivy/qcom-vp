// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/pdp/power_controls.h"
#include "qcom/sm88xx/pdp/acd.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>

// Two lanes share the image and differ only in the system address. Every lane
// therefore gets its own instance, and the fixture keeps the words apart.
struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> dpm_bus0{"dpm_bus0"}, dpm_bus1{"dpm_bus1"};
    tlm_utils::simple_initiator_socket<Consumer> limiter_bus0{"limiter_bus0"};
    tlm_utils::simple_initiator_socket<Consumer> limiter_bus1{"limiter_bus1"};
    tlm_utils::simple_initiator_socket<Consumer> core_bus{"core_bus"};
    tlm_utils::simple_initiator_socket<Consumer> acd_bus{"acd_bus"};
    qcom_vp::PdpDpm dpm0{"dpm0"}, dpm1{"dpm1"};
    qcom_vp::PdpLimiters limiter0{"limiter0"}, limiter1{"limiter1"};
    qcom_vp::PdpCoreDpm core_dpm{"core_dpm"};
    qcom_vp::PdpAcd acd{"acd"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        dpm_bus0.bind(dpm0.target_socket);
        dpm_bus1.bind(dpm1.target_socket);
        limiter_bus0.bind(limiter0.target_socket);
        limiter_bus1.bind(limiter1.target_socket);
        core_bus.bind(core_dpm.target_socket);
        acd_bus.bind(acd.target_socket);
    }

    tlm::tlm_response_status access(bool lane, unsigned offset, bool write,
                                    std::uint64_t& value, unsigned length = 8) {
        unsigned char bytes[8]{};
        for (unsigned i = 0; i < 8; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        if (lane) dpm_bus1->b_transport(tx, delay);
        else dpm_bus0->b_transport(tx, delay);
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }

    tlm::tlm_response_status acd_access(unsigned offset, bool write, std::uint32_t& value,
                                        unsigned length = 4) {
        unsigned char bytes[8]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        acd_bus->b_transport(tx, delay);
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }
    void acd_write(unsigned offset, std::uint32_t value) {
        assert(acd_access(offset, true, value) == tlm::TLM_OK_RESPONSE);
    }
    std::uint32_t acd_read(unsigned offset) {
        std::uint32_t value = 0;
        assert(acd_access(offset, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }

    tlm::tlm_response_status core_access(unsigned offset, bool write, std::uint64_t& value,
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
        core_bus->b_transport(tx, delay);
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }

    tlm::tlm_response_status limiter_access(bool lane, unsigned offset, bool write,
                                            std::uint64_t& value, unsigned length = 8) {
        unsigned char bytes[8]{};
        for (unsigned i = 0; i < 8; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        if (lane) limiter_bus1->b_transport(tx, delay);
        else limiter_bus0->b_transport(tx, delay);
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }

    void write(bool lane, unsigned offset, std::uint64_t value) {
        assert(access(lane, offset, true, value) == tlm::TLM_OK_RESPONSE);
    }
    std::uint64_t read(bool lane, unsigned offset) {
        std::uint64_t value = 0;
        assert(access(lane, offset, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void limiter_write(bool lane, unsigned offset, std::uint64_t value) {
        assert(limiter_access(lane, offset, true, value) == tlm::TLM_OK_RESPONSE);
    }
    std::uint64_t limiter_read(bool lane, unsigned offset) {
        std::uint64_t value = 0;
        assert(limiter_access(lane, offset, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer c{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // The DPM starts empty. The DVFS bundle then writes the constant qword.
    assert(c.read(0, 0x80) == 0);
    c.write(0, 0x80, 0x10000);
    assert(c.read(0, 0x80) == 0x10000);
    // The init path copies the K_DPM_CFG pairs into the two low words.
    c.write(0, 0x00, 0x1122334455667788ull);
    c.write(0, 0x30, 0xdeadbeefcafef00dull);
    assert(c.read(0, 0x00) == 0x1122334455667788ull);
    assert(c.read(0, 0x30) == 0xdeadbeefcafef00dull);
    // Lane one has its own instance and its own words.
    assert(c.read(1, 0x00) == 0);
    assert(c.read(1, 0x80) == 0);
    // Only the three recovered words answer, and only as 64-bit accesses.
    std::uint64_t value = 0;
    for (const unsigned offset : {0x08u, 0x38u, 0x88u, 0x100u}) {
        assert(c.access(0, offset, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(c.access(0, offset, true, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    assert(c.access(0, 0x80, false, value, 4) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.access(0, 0x84, false, value) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.access(0, 0x82, false, value) == tlm::TLM_BURST_ERROR_RESPONSE);
    // A cold reset clears one lane and keeps the other lane.
    c.dpm0.cold_reset();
    assert(c.read(0, 0x80) == 0);
    assert(c.read(0, 0x00) == 0);
    c.write(1, 0x30, 7);
    assert(c.read(1, 0x30) == 7);
    // The cluster weights record fills six further words without changing the others.
    for (const unsigned offset : {0x18u, 0x20u, 0x28u, 0xa8u, 0xb0u, 0xb8u})
        c.write(0, offset, 0x5a00000000000000ull | offset);
    for (const unsigned offset : {0x18u, 0x20u, 0x28u, 0xa8u, 0xb0u, 0xb8u}) {
        assert(c.read(0, offset) == (0x5a00000000000000ull | offset));
        assert(c.read(1, offset) == 0);
    }
    assert(c.read(0, 0x30) == 0);
    // A core bank holds 33 weights: 0x10..0x98 and 0x100..0x170.
    for (const unsigned offset : {0x10u, 0x98u, 0x100u, 0x170u}) {
        std::uint64_t word = 0xc0de000000000000ull | offset;
        assert(c.core_access(offset, true, word) == tlm::TLM_OK_RESPONSE);
        word = 0;
        assert(c.core_access(offset, false, word) == tlm::TLM_OK_RESPONSE);
        assert(word == (0xc0de000000000000ull | offset));
    }
    for (const unsigned offset : {0x00u, 0x08u, 0xa0u, 0xf8u, 0x178u}) {
        assert(c.core_access(offset, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(c.core_access(offset, true, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    assert(c.core_access(0x10, false, value, 4) == tlm::TLM_BURST_ERROR_RESPONSE);
    c.core_dpm.cold_reset();
    value = 1;
    assert(c.core_access(0x10, false, value) == tlm::TLM_OK_RESPONSE && value == 0);

    // The limiter init writes the three words of the window with id 2.
    assert(c.limiter_read(0, 0x40) == 0);
    c.limiter_write(0, 0x40, 0x0000000100020003ull);
    c.limiter_write(0, 0x48, 0x0000000400050006ull);
    c.limiter_write(0, 0x50, 0x0000000000000000ull);
    assert(c.limiter_read(0, 0x40) == 0x0000000100020003ull);
    assert(c.limiter_read(0, 0x48) == 0x0000000400050006ull);
    // The start path sets bit 3 of the control word and the stop path clears
    // bits 2:0. The model keeps the guest value and adds no completion.
    c.limiter_write(0, 0x50, c.limiter_read(0, 0x50) | 0x8ull);
    assert(c.limiter_read(0, 0x50) == 0x8);
    c.limiter_write(0, 0x50, 0x1f);
    c.limiter_write(0, 0x50, c.limiter_read(0, 0x50) & ~0x7ull);
    assert(c.limiter_read(0, 0x50) == 0x18);
    // Each window keeps its own words.
    assert(c.limiter_read(0, 0x60) == 0);
    c.limiter_write(0, 0x60, 0x99);
    assert(c.limiter_read(0, 0x60) == 0x99);
    assert(c.limiter_read(0, 0x40) == 0x0000000100020003ull);
    // The last declared window answers at the third word. The gap at 0x180 holds
    // no window, and the table ends after the window at 0x1c0.
    assert(c.limiter_read(0, 0x1a0) == 0);
    c.limiter_write(0, 0x1a0, 0x5a);
    assert(c.limiter_read(0, 0x1a0) == 0x5a);
    assert(c.limiter_read(0, 0x1c0 + 0x10) == 0);
    for (const unsigned offset : {0x58u, 0x180u, 0x190u, 0x1d8u, 0x1e0u, 0x3e0u}) {
        assert(c.limiter_access(0, offset, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(c.limiter_access(0, offset, true, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    assert(c.limiter_access(0, 0x40, false, value, 4) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.limiter_access(0, 0x44, false, value) == tlm::TLM_BURST_ERROR_RESPONSE);
    // Lane one has its own limiter bank.
    assert(c.limiter_read(1, 0x40) == 0);
    c.limiter_write(1, 0x40, 0x42);
    assert(c.limiter_read(1, 0x40) == 0x42);
    assert(c.limiter_read(0, 0x40) == 0x0000000100020003ull);
    c.limiter0.cold_reset();
    assert(c.limiter_read(0, 0x40) == 0);
    assert(c.limiter_read(1, 0x40) == 0x42);

    // The ACD bank starts at the catalog reset values.
    assert(c.acd_read(0x000) == 0x10030000);
    assert(c.acd_read(0x044) == 1);
    assert(c.acd_read(0x030) == 0x02c3da78);
    assert(c.acd_read(0x110) == 0);
    // PDP 0x8132120e programs ACDSSCR and ACDCR. Configuration starts nothing.
    c.acd_write(0x014, 0x00123456);
    c.acd_write(0x028, 0x00000a51);
    c.acd_write(0x004, 0x00280001);
    assert(c.acd_read(0x004) == 0x00280001 && c.acd_read(0x028) == 0x00000a51);
    assert(c.acd_read(0x044) == 1 && c.acd.calibrations() == 0);
    // PDP 0x813211c0 pulses UPDATEAUTOCAL. The rising edge completes one calibration.
    c.acd_write(0x004, 0x01280001);
    assert((c.acd_read(0x044) & 2) && c.acd_read(0x110) == 0x00123456);
    c.acd_write(0x004, 0x00280001);
    assert((c.acd_read(0x044) & 2) && c.acd.calibrations() == 1);
    // A held bit is not an edge. The next edge clears and sets CALIB_DONE again.
    c.acd_write(0x014, 0x00000777);
    c.acd_write(0x004, 0x01280001);
    c.acd_write(0x004, 0x01280001);
    assert(c.acd.calibrations() == 2 && c.acd_read(0x110) == 0x00000777);
    // ACD_MISC_STATUS keeps bits 1:0 as status and retains the upper bits.
    c.acd_write(0x044, 0xfffffffc);
    assert(c.acd_read(0x044) == 0xffffffff);
    c.acd_write(0x044, 0);
    assert(c.acd_read(0x044) == 3);
    // Read-only registers reject writes. Holes and other widths fail.
    std::uint32_t word = 0;
    for (const unsigned offset : {0x000u, 0x01cu, 0x110u, 0x0e0u})
        assert(c.acd_access(offset, true, word) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    for (const unsigned offset : {0x05cu, 0x094u, 0x0a4u, 0x0b0u, 0x130u, 0x7fcu})
        assert(c.acd_access(offset, false, word) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.acd_access(0x004, false, word, 8) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.acd_access(0x006, false, word, 2) == tlm::TLM_BURST_ERROR_RESPONSE);
    // A readout selection without a recovered source fails.
    assert(c.acd_read(0x098) == 0x10030000);
    c.acd_write(0x048, 5);
    assert(c.acd_access(0x098, false, word) == tlm::TLM_GENERIC_ERROR_RESPONSE);
    c.acd.cold_reset();
    assert(c.acd_read(0x044) == 1 && c.acd_read(0x004) == 0 && c.acd.calibrations() == 0);

    std::cout << "PDP_POWER_CONTROLS_BOUNDARY_PASS\n";
    return 0;
}

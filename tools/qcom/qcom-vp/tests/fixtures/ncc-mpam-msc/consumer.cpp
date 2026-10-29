// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/cpucp/mpam_msc.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>

struct Frame : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Frame> bus{"bus"};
    qcom_vp::QcomNccMpamMsc msc{"msc"};

    explicit Frame(sc_core::sc_module_name name) : sc_module(name) { bus.bind(msc.target_socket); }

    tlm::tlm_response_status access(unsigned offset, bool write, std::uint64_t& value,
                                    unsigned length = 4) {
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
    std::uint64_t read(unsigned offset, unsigned length = 4) {
        std::uint64_t value = 0;
        assert(access(offset, false, value, length) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void write(unsigned offset, std::uint64_t value) {
        assert(access(offset, true, value) == tlm::TLM_OK_RESPONSE);
    }
    tlm::tlm_response_status attempt(unsigned offset, bool write, std::uint64_t value = 0,
                                     unsigned length = 4) {
        return access(offset, write, value, length);
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Frame s{"secure"};
    Frame ns{"nonsecure"};
    s.msc.secure.set_value(true);
    s.msc.monitors.set_value(4);
    s.msc.filter_partid_bits.set_value(4);
    ns.msc.monitors.set_value(16);
    ns.msc.filter_partid_bits.set_value(6);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // Catalog identity. Only the secure frame has MPAMF_SIDR. NUM_MON follows the frame.
    assert(s.read(0x0, 8) == 0x807b01003full && ns.read(0x0) == 0x7b01003f && ns.read(0x4) == 0x80);
    assert(s.read(0x8) == 0x0001000f && ns.attempt(0x8, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(s.read(0x30) == 0xc && s.read(0x88) == 0xc0000004 && ns.read(0x88) == 0xc0000010);
    assert(s.read(0x90) == 0xd00b0004 && ns.read(0x90) == 0xd00b0010);
    assert(s.attempt(0x30, true, 0) == tlm::TLM_COMMAND_ERROR_RESPONSE);

    // Every partition starts at the catalog reset values.
    for (unsigned partition : {0u, 63u}) {
        ns.write(0x100, partition);
        assert(ns.read(0x108) == 0xf && ns.read(0x400) == 0x30000);
        assert(ns.read(0x1000) == 0xfff && ns.read(0x3000) == 1);
    }

    // PART_SEL selects separate storage. The query path reads back the selected partition.
    ns.write(0x100, 5);
    ns.write(0x1000, 0x0f0);
    ns.write(0x108, 3);
    ns.write(0x400, 0x10000);
    ns.write(0x3000, 7);
    ns.write(0x100, 6);
    assert(ns.read(0x1000) == 0xfff && ns.read(0x108) == 0xf && ns.read(0x3000) == 1);
    ns.write(0x100, 5);
    assert(ns.read(0x1000) == 0x0f0 && ns.read(0x108) == 3);
    assert(ns.read(0x400) == 0x10000 && ns.read(0x3000) == 7);
    // The secure frame is a separate configuration space.
    s.write(0x100, 5);
    assert(s.read(0x1000) == 0xfff);

    // A selector beyond the frame capacity fails and keeps the previous selection.
    assert(ns.attempt(0x100, true, 64) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(ns.read(0x100) == 5);
    assert(s.attempt(0x800, true, 4) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    ns.write(0x800, 15);

    // Read-only fields keep their value. CPUCP writes CSU_CTL without the TYPE field.
    s.write(0x800, 2);
    s.write(0x818, 0xf0030000);
    assert(s.read(0x818) == 0xf0030043);
    s.write(0x810, 0xffffffff);
    assert(s.read(0x810) == 0x1000f);
    ns.write(0x810, 0xffffffff);
    assert(ns.read(0x810) == 0x1003f);
    s.write(0x800, 1);
    assert(s.read(0x810) == 0 && s.read(0x818) == 0x43 && s.read(0x828) == 0x42);
    s.write(0x800, 2);
    assert(s.read(0x810) == 0x1000f);

    // No virtual cache or memory path counts partition usage.
    for (unsigned offset : {0x840u, 0x848u, 0x860u, 0x868u})
        assert(s.attempt(offset, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(s.attempt(0x880, false, 0, 8) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(s.attempt(0x808, true, 1) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    s.write(0x808, 0);
    assert(s.attempt(0x2000, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(s.attempt(0x102, false) == tlm::TLM_BURST_ERROR_RESPONSE);
    std::cout << "NCC_MPAM_MSC_PARTITION_PASS" << std::endl;
    return 0;
}

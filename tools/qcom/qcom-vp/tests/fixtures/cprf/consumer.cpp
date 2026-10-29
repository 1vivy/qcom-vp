// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/cpr/cprf.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <cstdint>
#include <iostream>

// UEFI CPRDxe programs each CPRF row from the selected binary:
//   0xc1b0 writes VOLTAGE_LIMITS_n, 0xc1b4 writes SAFE_VOLTAGE_n,
//   0xc22c sets CTRL_INTERACTION_EN, 0xc474 writes VRM_SETTINGS, and
//   0xc49c sets ENABLE. The block keeps these words and creates no voltage.
// The row of CPRF m starts at 0x1000 + 0x1000*m.
struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> port{"port"};
    qcom_vp::QcomRpmhCprf cprf{"cprf"};
    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        port.bind(cprf.target_socket);
    }
    tlm::tlm_response_status access(unsigned offset, bool write, std::uint32_t& value,
                                    unsigned length = 4) {
        unsigned char bytes[8]{};
        for (unsigned i = 0; i < length; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        port->b_transport(tx, delay);
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < length; ++i) value |= std::uint32_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }
    void put(unsigned offset, std::uint32_t value) {
        assert(access(offset, true, value) == tlm::TLM_OK_RESPONSE);
    }
    std::uint32_t get(unsigned offset) {
        std::uint32_t value = 0;
        assert(access(offset, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void reject_write(unsigned offset, std::uint32_t value) {
        assert(access(offset, true, value) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    }
    void reject_read(unsigned offset) {
        std::uint32_t value = 0;
        assert(access(offset, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer c{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // HW_VERSION reports its reset value and rejects writes.
    assert(c.get(0x0000) == 0x100b0000);
    c.reject_write(0x0000, 0);
    // The row registers of CPRF 5 start at their reset values.
    assert(c.get(0x6004) == 0x801d);
    assert(c.get(0x6008) == 0x1100);
    assert(c.get(0x6118) == 0);
    assert(c.get(0x6010) == 0 && c.get(0x6090) == 0);

    // The CPRDxe write of run768: GFX row 5, entry 1, 488 mV ceiling, 432 mV floor.
    const std::uint32_t limits = (0x1e8u << 16) | 0x1b0u;
    assert(limits == 0x1e801b0);
    c.put(0x6010, limits);
    c.put(0x6090, 0x1e8);
    assert(c.get(0x6010) == limits);
    assert(c.get(0x6090) == 0x1e8);
    // Each row and each entry keeps its own word.
    c.put(0x5010, 0x1a401a0);
    c.put(0x6014, 0x1f01c8);
    assert(c.get(0x5010) == 0x1a401a0);
    assert(c.get(0x6014) == 0x1f01c8);
    assert(c.get(0x6010) == limits);
    assert(c.get(0x5014) == 0);
    // The selected plan of the GFX rail reaches entry 19, beyond the
    // 16 entries that the older register layout declares.
    c.put(0x6058, 0x12c00f0);
    c.put(0x60d8, 0x12c);
    assert(c.get(0x6058) == 0x12c00f0);
    assert(c.get(0x60d8) == 0x12c);
    // Entry 20 has no declared field.
    c.reject_read(0x605c);
    c.reject_read(0x60dc);

    // A field outside VMAX and VMIN, and bit 29, cannot enter a limit word.
    c.reject_write(0x6010, 0x20000000);
    c.reject_write(0x6010, 0xffffffff);
    c.reject_write(0x6090, 0x2000);
    assert(c.get(0x6010) == limits);
    assert(c.get(0x6090) == 0x1e8);

    // The firmware read-modify-write keeps every CTRL field and sets bit 2.
    // Its mask is 0x1ffff, so bit 16 also survives the merge.
    std::uint32_t ctrl = c.get(0x6004);
    ctrl = ((ctrl & 0x1ffffu) & ~0x4u) | 0x4u;
    c.put(0x6004, ctrl);
    assert((c.get(0x6004) & 0x4) == 0x4);
    assert((c.get(0x6004) & 0x8018) == (0x801d & 0x8018));
    c.put(0x6004, 0x1ffff);
    assert(c.get(0x6004) == 0x1ffff);
    // Bit 17 has no defined field.
    c.reject_write(0x6004, 0x20000);
    assert(c.get(0x6004) == 0x1ffff);

    // The VRM resource word keeps the resource id and the address offset, as
    // cpr_hal_set_vrm_settings builds them (cpr_hal.c:754-757).
    const std::uint32_t vrm = 0x1234u << 13 | 0x56u;
    c.put(0x6008, vrm);
    assert(c.get(0x6008) == vrm);
    c.reject_write(0x6008, 0x20000000);
    assert(c.get(0x6008) == vrm);

    // ENABLE has one writable bit.
    c.put(0x6118, 1);
    assert(c.get(0x6118) == 1);
    c.reject_write(0x6118, 2);
    assert(c.get(0x6118) == 1);
    assert(c.get(0x5118) == 0);

    // No consumer reads a status word, so the block gives none. CPRF_RESET,
    // CTRL of a missing field, the interrupt status, and the idle status fail.
    c.reject_read(0x1000);
    c.reject_read(0x1220);
    std::uint32_t zero = 0;
    assert(c.access(0x1220, true, zero) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    c.reject_read(0x008c);
    c.reject_read(0x0008);
    // Row 13 is outside the catalog count of 12 rows.
    c.reject_read(0xd004);
    // Only whole words of four bytes are supported.
    std::uint32_t wide = 0;
    assert(c.access(0x6010, false, wide, 8) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.access(0x6012, false, wide) == tlm::TLM_BURST_ERROR_RESPONSE);

    // Cold reset returns the retained configuration to the catalog values.
    c.cprf.cold_reset();
    assert(c.get(0x6010) == 0);
    assert(c.get(0x6058) == 0);
    assert(c.get(0x6004) == 0x801d);
    assert(c.get(0x6008) == 0x1100);
    assert(c.get(0x6118) == 0);

    std::cout << "CPRF_CONFIGURATION_PASS\n";
    return 0;
}

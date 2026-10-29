// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/pdp/cprh_target.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cassert>
#include <iostream>

struct Consumer : sc_core::sc_module {
    enum Port { Control, Peer, Table, OtherControl, OtherTable };
    tlm_utils::simple_initiator_socket<Consumer> control{"control"}, peer{"peer"}, table{"table"},
        other_control{"other_control"}, other_table{"other_table"};
    qcom_vp::CprhTarget target{"target"}, other{"other"};
    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        control.bind(target.target_socket);
        peer.bind(target.target_socket);
        table.bind(target.table_socket);
        other_control.bind(other.target_socket);
        other_table.bind(other.table_socket);
    }
    tlm::tlm_response_status access(Port port, unsigned offset, bool write,
                                  std::uint32_t& value, unsigned length = 4) {
        unsigned char bytes[8]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        auto delay = sc_core::SC_ZERO_TIME;
        switch (port) {
        case Control: control->b_transport(tx, delay); break;
        case Peer: peer->b_transport(tx, delay); break;
        case Table: table->b_transport(tx, delay); break;
        case OtherControl: other_control->b_transport(tx, delay); break;
        case OtherTable: other_table->b_transport(tx, delay); break;
        }
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) value = qcom_vp::clock_detail::load(tx);
        return tx.get_response_status();
    }
    tlm::tlm_response_status put(Port port, unsigned offset, std::uint32_t value,
                               unsigned length = 4) {
        return access(port, offset, true, value, length);
    }
    std::uint32_t get(Port port, unsigned offset) {
        std::uint32_t value = 0;
        assert(access(port, offset, false, value) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void reject_read(Port port, unsigned offset) {
        std::uint32_t value = 0;
        assert(access(port, offset, false, value) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer c{"consumer"};
    c.target.reference_clock_hz.set_value(19200000);
    c.target.oscillator_clock_hz.set_value(1920000000);
    c.target.ownership_word_count.set_value(2);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    using P = Consumer;

    // Loop control alone cannot measure without a supply and selected sensors.
    assert(c.put(P::Control, 4, 0x2814) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Peer, 4, 0x2815) == tlm::TLM_OK_RESPONSE);
    assert(c.get(P::Control, 4) == 0x2815);

    // Corner application needs a programmed row and an enabled control word.
    assert(!c.target.has_effective_corner());
    assert(!c.target.can_apply_corner(3));
    assert(!c.target.apply_corner(3));
    assert(c.put(P::Table, 0xa00, 0x04000081) == tlm::TLM_OK_RESPONSE);
    assert(!c.target.can_apply_corner(3));
    assert(c.put(P::Control, 4, 0x2814) == tlm::TLM_OK_RESPONSE);

    // Row 3 holds initial step 0x10 and floor 0x2a. The floor wins.
    assert(c.put(P::Table, 0xc, 0x2a10) == tlm::TLM_OK_RESPONSE);
    assert(c.target.can_apply_corner(3));
    assert(c.target.apply_corner(3));
    assert(c.target.corner_ready(3));
    assert(c.target.effective_corner() == 3);
    assert(c.target.effective_steps() == 0x2a);
    // The multiplier is two and the base count is 0x40: (0x40 + 0x2a) * 2.
    assert(c.target.effective_voltage_mv() == 212);
    assert(c.get(P::Table, 0xc) == 0x2a10);
    assert(c.get(P::Table, 0xa00) == 0x04000081);
    assert(c.get(P::Table, 0xa04) == (3u | (212u << 6)));
    assert(c.put(P::Table, 0xa04, 0xffffffffu) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(c.put(P::Control, 4, 0x2815) == tlm::TLM_OK_RESPONSE);
    assert(c.get(P::Table, 0xa04) == (3u | (212u << 6) | (1u << 29)));
    assert(c.put(P::Control, 4, 0x2814) == tlm::TLM_OK_RESPONSE);

    // An unprogrammed row cannot replace the effective corner.
    assert(!c.target.apply_corner(4));
    assert(c.target.effective_corner() == 3);
    assert(c.get(P::Table, 0xa04) == (3u | (212u << 6)));
    // A changed row makes the applied corner stale until a new application.
    assert(c.put(P::Table, 0xc, 0x0510) == tlm::TLM_OK_RESPONSE);
    assert(!c.target.corner_ready(3));
    assert(c.get(P::Table, 0xa04) == (3u | (212u << 6)));
    assert(c.target.apply_corner(3));
    assert(c.target.effective_steps() == 0x10);
    assert(c.target.effective_voltage_mv() == 0xa0);
    assert(c.get(P::Table, 0xa04) == (3u | (160u << 6) | (3u << 18)));

    // A disabled control word stops new requests and readiness.
    assert(c.put(P::Table, 0xa00, 0x04000080) == tlm::TLM_OK_RESPONSE);
    assert(!c.target.can_apply_corner(3));
    assert(!c.target.apply_corner(3));
    assert(!c.target.corner_ready(3));
    assert(c.target.effective_corner() == 3);
    // A zero multiplier cannot produce a voltage.
    assert(c.put(P::Table, 0xa00, 0x81) == tlm::TLM_OK_RESPONSE);
    assert(!c.target.can_apply_corner(3));

    // The adjacent target keeps its own corner and its own control word.
    assert(!c.other.has_effective_corner());
    assert(c.put(P::OtherTable, 0xa00, 0x06000081) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::OtherTable, 0x14, 0x3f06) == tlm::TLM_OK_RESPONSE);
    assert(c.other.apply_corner(5));
    assert(c.other.effective_corner() == 5);
    assert(c.other.effective_steps() == 0x3f);
    assert(c.target.has_effective_corner());

    // The firmware selects the minimum quotient for each shared oscillator.
    assert(c.put(P::Control, 0x840, 1200) == tlm::TLM_OK_RESPONSE);
    for (const auto requested : {900u, 1100u}) {
        if (requested < c.get(P::Peer, 0x840))
            assert(c.put(P::Peer, 0x840, requested) == tlm::TLM_OK_RESPONSE);
    }
    assert(c.get(P::Control, 0x840) == 900);
    assert(c.get(P::OtherControl, 0x840) == 0);

    // The adjacent thread and table rows must not alias the selected thread.
    assert(c.put(P::Control, 0x87c, 4095) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0x880, 1) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.put(P::Control, 0xc80, 1) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.put(P::Control, 0x87c, 1, 8) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.get(P::Control, 0x87c) == 4095);
    assert(c.put(P::Table, 0x9c, 0x40322d1e) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Table, 0xa0, 1) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(c.put(P::Table, 0x9c, 1, 8) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(c.get(P::Table, 0x9c) == 0x40322d1e);
    assert(c.get(P::OtherTable, 0x9c) == 0);

    // Configuration and elapsed time must not manufacture status.
    sc_core::sc_start(sc_core::sc_time(1, sc_core::SC_MS));
    c.reject_read(P::Control, 0x2600);
    c.reject_read(P::Table, 0x1000);
    assert(c.get(P::Control, 0x824) == 0);
    assert(c.put(P::Control, 0x824, 1) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(c.put(P::Table, 0xc94, 0xfc0000ff) == tlm::TLM_OK_RESPONSE);
    assert(c.get(P::Table, 0xc94) == 0xfc000000);

    // Manual sampling uses actual configured gates and the declared sensor clock.
    assert(c.put(P::Control, 4, 0) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Table, 0xa00, 0) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0x20, 0x80000600) == tlm::TLM_OK_RESPONSE);
    assert(c.get(P::Control, 0x824) == 0);
    assert(c.put(P::Control, 0x200, 1) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(c.put(P::Control, 0x200, 0) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0x204, 0) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0x120, ~3u) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0xa0, 19) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0xa4, 23) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0x81c, 4) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 4, 1) == tlm::TLM_OK_RESPONSE);
    const auto sample = 2000u | (2400u << 12) | (1u << 28);
    assert(c.get(P::Control, 8) == 0);
    assert(c.get(P::Control, 0x814) == sample);
    assert(c.get(P::Control, 0x818) == (1u << 24));
    assert(c.get(P::Control, 0x824) == 5);
    assert(c.put(P::Control, 0x820, 1) == tlm::TLM_OK_RESPONSE);
    assert(c.get(P::Control, 0x824) == 4);

    // A mode write does not replace the sample before the continuation request.
    assert(c.put(P::Control, 0x120, ~(1u << 15)) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 4, 0xc1) == tlm::TLM_OK_RESPONSE);
    assert(c.get(P::Control, 8) == 0);
    assert(c.put(P::Control, 0x800, 0) == tlm::TLM_OK_RESPONSE);
    assert(c.get(P::Control, 8) == 2);
    assert(c.get(P::Control, 0x818) == ((15u << 16) | (15u << 24)));
    c.reject_read(P::Control, 0x208);
    assert(c.get(P::Control, 0x814) == sample);
    assert(c.get(P::Control, 0x824) == 5);
    assert(c.put(P::Control, 0x814, 0) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(c.target.effective_voltage_mv() == 160);

    // Missing selected sensors cannot produce a completed measurement.
    assert(c.put(P::Control, 0x120, 0xffffffffu) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0x800, 0) == tlm::TLM_OK_RESPONSE);
    assert(c.get(P::Control, 8) == 1);
    assert(c.get(P::Control, 0x824) == 0);
    assert(c.put(P::Control, 4, 0) == tlm::TLM_OK_RESPONSE);
    assert(c.get(P::Control, 8) == 0);

    // Automatic continuation follows its interval and stops when the loop stops.
    assert(c.put(P::Control, 0x120, ~3u) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0xc, 384) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0x81c, 0) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 4, 1) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0x820, 0xf) == tlm::TLM_OK_RESPONSE);
    sc_core::sc_start(sc_core::sc_time(10, sc_core::SC_US));
    assert(c.get(P::Control, 0x824) == 0);
    sc_core::sc_start(sc_core::sc_time(11, sc_core::SC_US));
    assert(c.get(P::Control, 0x824) == 5);
    assert(c.put(P::Control, 4, 0) == tlm::TLM_OK_RESPONSE);
    assert(c.put(P::Control, 0x820, 0xf) == tlm::TLM_OK_RESPONSE);
    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_US));
    assert(c.get(P::Control, 0x824) == 0);
    std::cout << "CPRH_TARGET_BOUNDARY_PASS" << std::endl;
    return 0;
}

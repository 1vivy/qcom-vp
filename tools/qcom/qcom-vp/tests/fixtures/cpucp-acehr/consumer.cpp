// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/sm88xx/cpucp/acehr.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <iostream>

// Fixture-only physical targets supply bytes; ACEHR completes after real DMA.
struct DmaMemory : sc_core::sc_module {
    tlm_utils::multi_passthrough_target_socket<DmaMemory> target_socket{"target_socket"};
    std::array<unsigned char, 0x100> gic{};
    std::array<unsigned char, 0x8000> sram{};
    bool deny_read = false;

    explicit DmaMemory(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &DmaMemory::transport);
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        const auto* attrs = tx.get_extension<QemuMemTxAttrsExtension>();
        if (!attrs || attrs->attrs.unspecified || !attrs->attrs.secure ||
            attrs->attrs.user || attrs->attrs.requester_id != 0) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto address = tx.get_address();
        const unsigned length = tx.get_data_length();
        unsigned char* bytes = nullptr;
        if (address >= 0x17090c00 && address + length <= 0x17090d00)
            bytes = gic.data() + (address - 0x17090c00);
        else if (address >= 0x1d880000 && address + length <= 0x1d888000)
            bytes = sram.data() + (address - 0x1d880000);
        if (!bytes || !tx.get_data_ptr() || (deny_read && tx.is_read())) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        for (unsigned i = 0; i < length; ++i) {
            if (tx.is_write()) bytes[i] = tx.get_data_ptr()[i];
            else tx.get_data_ptr()[i] = bytes[i];
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Consumer> bus0{"bus0"};
    tlm_utils::simple_initiator_socket<Consumer> bus1{"bus1"};
    qcom_vp::QcomCpucpAcehr acehr0{"acehr0"};
    qcom_vp::QcomCpucpAcehr acehr1{"acehr1"};
    DmaMemory memory{"memory"};
    sc_core::sc_signal<bool> irq0{"irq0"};
    sc_core::sc_signal<bool> irq1{"irq1"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        acehr0.dma_beat_hz = 20000000;
        acehr1.dma_beat_hz = 20000000;
        bus0.bind(acehr0.target_socket);
        bus1.bind(acehr1.target_socket);
        acehr0.irq.bind(irq0);
        acehr1.irq.bind(irq1);
        acehr0.dma_socket.bind(memory.target_socket);
        acehr1.dma_socket.bind(memory.target_socket);
    }

    tlm::tlm_response_status access(unsigned instance, std::uint64_t offset,
                                    bool write, std::uint64_t& value,
                                    unsigned length = 4, bool byte_enable = false) {
        unsigned char bytes[8]{};
        unsigned char enables[8]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        for (unsigned i = 0; i < length; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_address(offset);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        if (byte_enable) {
            tx.set_byte_enable_ptr(enables);
            tx.set_byte_enable_length(length);
        }
        auto delay = sc_core::SC_ZERO_TIME;
        if (instance) bus1->b_transport(tx, delay);
        else bus0->b_transport(tx, delay);
        assert(!tx.is_dmi_allowed());
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < length; ++i)
                value |= std::uint64_t{bytes[i]} << (8 * i);
        }
        return tx.get_response_status();
    }
    std::uint64_t read(unsigned instance, std::uint64_t offset, unsigned length = 4) {
        std::uint64_t value = 0;
        assert(access(instance, offset, false, value, length) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void write(unsigned instance, std::uint64_t offset, std::uint64_t value,
               unsigned length = 4) {
        assert(access(instance, offset, true, value, length) == tlm::TLM_OK_RESPONSE);
    }
    tlm::tlm_response_status status(unsigned instance, std::uint64_t offset, bool write,
                                    unsigned length = 4, bool byte_enable = false) {
        std::uint64_t value = 0;
        return access(instance, offset, write, value, length, byte_enable);
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer consumer{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    // Cold state: only retained register stores and descriptor bytes exist.
    assert(consumer.read(0, 0x21c) == 0);
    assert(consumer.read(1, 0x21c) == 0);
    assert(consumer.read(0, 0x560, 8) == 0);
    assert(consumer.read(0, 0x570, 8) == 0);
    assert(consumer.read(0, 0x590) == 0);
    for (unsigned i = 0; i < 6; ++i) assert(consumer.read(0, 0x594 + 4 * i) == 0);
    for (unsigned word = 0; word < 4; ++word) assert(consumer.read(0, 0x204 + 4 * word) == 0);
    for (unsigned channel = 0; channel < 64; ++channel)
        assert(consumer.read(0, 0x004 + 4 * channel) == 0);
    assert(consumer.read(0, 0x4000, 8) == 0);
    assert(!consumer.irq0.read() && !consumer.irq1.read());

    // The recovered initializer writes 0xf only, on each instance.
    consumer.write(0, 0x21c, 0xf);
    consumer.write(1, 0x21c, 0xf);
    assert(consumer.read(0, 0x21c) == 0xf);
    assert(consumer.read(1, 0x21c) == 0xf);
    assert(!consumer.irq0.read() && !consumer.irq1.read());

    // Configuration bits retain values without issuing a channel start.
    for (unsigned channel : {0u, 1u, 31u, 32u, 63u}) {
        const auto offset = 0x004 + 4 * channel;
        consumer.write(0, offset, 0xa5a50000u | (channel << 1));
        assert(consumer.read(0, offset) == (0xa5a50000u | (channel << 1)));
        assert(consumer.read(1, offset) == 0);
    }
    consumer.write(0, 0x204, 0x80000001u);
    consumer.write(0, 0x208, 0x00000002u);
    consumer.write(0, 0x20c, 0x00000004u);
    consumer.write(0, 0x210, 0x80000008u);
    assert(consumer.read(0, 0x204) == 0x80000001u);
    assert(consumer.read(0, 0x208) == 0x00000002u);
    assert(consumer.read(0, 0x20c) == 0x00000004u);
    assert(consumer.read(0, 0x210) == 0x80000008u);
    assert(consumer.read(1, 0x204) == 0 && consumer.read(1, 0x210) == 0);

    // The trigger release word and the service word are command captures.
    consumer.write(0, 0x588, 0x0123456789abcdefULL, 8);
    consumer.write(0, 0x5ac, 0x40010002u);
    assert(consumer.read(0, 0x588, 8) == 0x0123456789abcdefULL);
    assert(consumer.read(0, 0x5ac) == 0x40010002u);
    assert(consumer.read(1, 0x588, 8) == 0 && consumer.read(1, 0x5ac) == 0);

    // Descriptor slots are retained at the recovered ring base and bound.
    consumer.write(0, 0x4000 + 16 * 7, 0x0000000000000001ULL, 8);
    consumer.write(0, 0x4000 + 16 * 7 + 8, 0xffffffffffffffffULL, 8);
    consumer.write(0, 0x4000 + 16 * 0x3ff, 0x1122334455667788ULL, 8);
    assert(consumer.read(0, 0x4000 + 16 * 7, 8) == 0x1);
    assert(consumer.read(0, 0x4000 + 16 * 7 + 8, 8) == 0xffffffffffffffffULL);
    assert(consumer.read(0, 0x4000 + 16 * 0x3ff, 8) == 0x1122334455667788ULL);
    assert(consumer.read(1, 0x4000 + 16 * 7, 8) == 0);

    // Engine-owned state rejects writes and reads as no produced event.
    assert(consumer.status(0, 0x560, true, 8) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.status(0, 0x570, true, 8) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.status(0, 0x590, true, 4) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.status(0, 0x594, true, 4) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.status(0, 0x5a8, true, 4) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.read(0, 0x560, 8) == 0);
    assert(consumer.read(0, 0x570, 8) == 0);
    assert(consumer.read(0, 0x590) == 0);
    assert(consumer.read(0, 0x594) == 0);

    // A misaligned 64-bit access to a 32-bit register fails alignment first.
    assert(consumer.status(0, 0x21c, true, 8) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(consumer.read(0, 0x21c) == 0xf);
    // Wrong width at an aligned offset is a command error, not a store.
    assert(consumer.status(0, 0x560, false, 4) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.status(0, 0x588, true, 4) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.read(0, 0x588, 8) == 0x0123456789abcdefULL);

    // A doorbell publishes queue bounds but cannot complete a descriptor.
    const auto kick = qcom_vp::QcomCpucpAcehr::kick_offset + 4 * 5;
    consumer.write(0, kick, 0x00070007);
    assert(consumer.read(0, kick) == 0x00070007);
    assert(consumer.read(0, 0x004 + 4 * 5) == 0);
    assert(consumer.read(0, 0x204) == 0x80000001u);
    assert(consumer.read(0, 0x560, 8) == 0);
    assert(consumer.read(0, 0x570, 8) == 0);
    assert(consumer.read(0, 0x21c) == 0xf);
    assert(!consumer.irq0.read() && !consumer.irq1.read());

    // Bounds and malformed transfers stay explicit.
    assert(consumer.status(0, 0x8000, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(consumer.status(0, 0x3f00, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(consumer.status(0, 0x1000, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
    assert(consumer.status(0, kick + 2, false) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(consumer.status(0, 0x21c, false, 1) == tlm::TLM_BURST_ERROR_RESPONSE);
    assert(consumer.status(0, 0x21c, false, 4, true) ==
           tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
    assert(consumer.status(0, 0x4000 + 3, false, 8) == tlm::TLM_BURST_ERROR_RESPONSE);

    // Cold reset clears retained state on one instance only.
    consumer.acehr0.cold_reset();
    assert(consumer.read(0, 0x21c) == 0);
    assert(consumer.read(0, 0x204) == 0);
    assert(consumer.read(0, 0x004) == 0);
    assert(consumer.read(0, 0x588, 8) == 0);
    assert(consumer.read(0, 0x4000 + 16 * 7, 8) == 0);
    assert(consumer.read(0, 0x4000 + 16 * 0x3ff, 8) == 0);
    assert(consumer.read(1, 0x21c) == 0xf);
    assert(consumer.read(1, 0x4000, 8) == 0);

    // The captured OEM descriptor is a 4-beat, 4-byte GICR-to-SRAM transfer.
    const auto encode = [](std::uint32_t source, std::uint32_t destination,
                           unsigned beats, unsigned exponent, unsigned mode) {
        return (std::uint64_t{source} << 19) |
               (std::uint64_t{destination & 31} << 59) |
               (std::uint64_t{2} << 17) |
               (std::uint64_t{exponent} << 15) |
               (std::uint64_t{mode} << 12) | (beats - 1);
    };
    const auto publish = [&](unsigned instance, unsigned channel, unsigned slot,
                             std::uint64_t first, std::uint64_t second) {
        consumer.write(instance, 0x4000 + 16 * slot, first, 8);
        consumer.write(instance, 0x4000 + 16 * slot + 8, second, 8);
        consumer.write(instance, 0x3e00 + 4 * channel, slot | (slot << 16));
    };
    for (unsigned i = 0; i < 16; ++i) consumer.memory.gic[i] = i + 1;
    consumer.write(0, 0x21c, 0xf);
    const auto snapshot = encode(0x17090c00, 0x1d880000, 4, 2, 0);
    assert(snapshot == 0x0000b84860050003ULL);
    publish(0, 0, 0, snapshot, 0x0000000000ec4000ULL);
    for (unsigned i = 0; i < 16; ++i) assert(consumer.memory.sram[i] == 0);
    assert(consumer.read(0, 0x560, 8) == 0);
    assert(!consumer.irq0.read());
    auto delay = sc_core::SC_ZERO_TIME;
    assert(consumer.acehr0.trigger_external(0, delay));
    assert(consumer.read(0, 0x570, 8) == 0);
    sc_core::sc_start(2, sc_core::SC_US);
    for (unsigned i = 0; i < 16; ++i)
        assert(consumer.memory.sram[i] == consumer.memory.gic[i]);
    assert(consumer.read(0, 0x560, 8) == 1);
    assert(consumer.read(0, 0x570, 8) == 1);
    consumer.write(0, 0x004, 0); // Firmware clears bit 0 after consuming completion.
    assert(consumer.read(0, 0x560, 8) == 0);
    assert(consumer.read(0, 0x570, 8) == 0);

    // The selected reverse descriptor restores those same GICR bytes.
    publish(0, 0, 1, encode(0x1d880000, 0x17090c00, 4, 2, 0),
            std::uint64_t{0x17090c00} >> 5);
    consumer.memory.gic.fill(0);
    assert(consumer.acehr0.trigger_external(0, delay));
    sc_core::sc_start(2, sc_core::SC_US);
    for (unsigned i = 0; i < 16; ++i)
        assert(consumer.memory.gic[i] == i + 1);
    assert(consumer.read(0, 0x560, 8) == 1);
    assert(consumer.read(0, 0x570, 8) == 1);
    consumer.write(0, 0x004, 0);
    assert(consumer.read(0, 0x560, 8) == 0);

    // Request and interrupt-mask writes cannot start a transfer.
    publish(0, 0, 2, encode(0x17090c00, 0x1d880100, 4, 2, 0),
            std::uint64_t{0x1d880100} >> 5);
    consumer.write(0, 0x20c, 1);
    assert(consumer.memory.sram[0x100] == 0);
    consumer.write(0, 0x204, 1);
    assert(consumer.memory.sram[0x100] == 0);
    consumer.write(0, 0x004, 1);
    sc_core::sc_start(2, sc_core::SC_US);
    for (unsigned i = 0; i < 16; ++i)
        assert(consumer.memory.sram[0x100 + i] == consumer.memory.gic[i]);
    assert(consumer.read(0, 0x560, 8) == 1);
    assert(consumer.irq0.read());
    assert(consumer.read(0, 0x570, 8) == 1);
    consumer.write(0, 0x588, 1, 8);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    assert(!consumer.irq0.read());
    assert(consumer.read(0, 0x570, 8) == 0);
    assert(consumer.read(0, 0x560, 8) == 1);
    consumer.write(0, 0x004, 0);
    assert(consumer.read(0, 0x560, 8) == 0);

    // The synchronous OEM path starts a channel through its own command word.
    publish(0, 1, 2, encode(0x17090c00, 0x1d880180, 4, 2, 0),
            std::uint64_t{0x1d880180} >> 5);
    consumer.write(0, 0x008, 1);
    assert(consumer.read(0, 0x570, 8) == 0);
    sc_core::sc_start(2, sc_core::SC_US);
    for (unsigned i = 0; i < 16; ++i)
        assert(consumer.memory.sram[0x180 + i] == consumer.memory.gic[i]);
    assert(consumer.read(0, 0x560, 8) == 2);
    consumer.write(0, 0x008, 0);
    assert(consumer.read(0, 0x560, 8) == 0);

    // Unsupported operations and failed source reads do not publish success.
    publish(0, 0, 3, encode(0x17090c00, 0x1d880200, 4, 2, 1),
            std::uint64_t{0x1d880200} >> 5);
    std::uint64_t request = 1;
    assert(consumer.access(0, 0x004, true, request) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.memory.sram[0x200] == 0);
    assert(consumer.read(0, 0x560, 8) == 0);
    consumer.write(0, 0x204, 0);
    publish(0, 0, 4, encode(0x17090c00, 0x1d880400, 4, 2, 0),
            std::uint64_t{0x1d880400} >> 5);
    consumer.memory.deny_read = true;
    request = 1;
    assert(consumer.access(0, 0x004, true, request) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    consumer.memory.deny_read = false;
    assert(consumer.memory.sram[0x400] == 0);
    assert(consumer.read(0, 0x560, 8) == 0);
    consumer.write(0, 0x204, 0);
    // SM8850 run324 starts this shape for the PDP image load: the staged bytes
    // are readable but the 0x19930000 destination has no owner.
    publish(0, 9, 11, encode(0x1d880000, 0x19930000, 4, 2, 0),
            std::uint64_t{0x19930000} >> 5);
    request = 1;
    assert(consumer.access(0, 0x004 + 4 * 9, true, request) ==
           tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.read(0, 0x004 + 4 * 9) == 0);
    assert(consumer.read(0, 0x560, 8) == 0);
    assert(consumer.read(0, 0x570, 8) == 0);
    assert(!consumer.irq0.read());

    // Upper-half channel routing and independent instance state.
    publish(0, 32, 5, encode(0x17090c00, 0x1d880300, 4, 2, 0),
            std::uint64_t{0x1d880300} >> 5);
    consumer.write(0, 0x210, 1);
    consumer.write(0, 0x208, 1);
    consumer.write(0, 0x084, 1);
    sc_core::sc_start(2, sc_core::SC_US);
    assert(consumer.memory.sram[0x300] == 1);
    assert(consumer.read(0, 0x560, 8) == (std::uint64_t{1} << 32));
    assert(consumer.read(0, 0x570, 8) == (std::uint64_t{1} << 32));
    consumer.write(0, 0x004 + 4 * 32, 0);
    assert(consumer.read(0, 0x560, 8) == 0);
    publish(1, 0, 0, encode(0x17090c00, 0x1d880500, 4, 2, 0),
            std::uint64_t{0x1d880500} >> 5);
    consumer.write(1, 0x20c, 1);
    consumer.write(1, 0x204, 1);
    consumer.write(1, 0x004, 1);
    sc_core::sc_start(2, sc_core::SC_US);
    assert(consumer.memory.sram[0x500] == 1);
    assert(consumer.read(1, 0x560, 8) == 1);
    assert(consumer.read(0, 0x560, 8) == 0);
    consumer.write(1, 0x004, 0);
    assert(consumer.read(1, 0x560, 8) == 0);

    // Polling transfers complete while their service interrupt is masked.
    publish(1, 1, 1, encode(0x17090c00, 0x1d880540, 4, 2, 0),
            std::uint64_t{0x1d880540} >> 5);
    consumer.write(1, 0x204, 2);
    assert(consumer.read(1, 0x204) == 2);
    assert(consumer.memory.sram[0x540] == 0);
    assert(consumer.read(1, 0x560, 8) == 0);
    consumer.write(1, 0x008, 1);
    sc_core::sc_start(2, sc_core::SC_US);
    assert(consumer.memory.sram[0x540] == 1);
    assert(!consumer.irq1.read());
    consumer.write(1, 0x20c, 3);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    assert(consumer.irq1.read());
    assert(consumer.read(1, 0x560, 8) == 2);
    consumer.write(1, 0x008, 0);
    assert(consumer.read(1, 0x560, 8) == 0);

    // Packed low/high indices are an inclusive chain, not one selected slot.
    publish(0, 1, 6, encode(0x17090c00, 0x1d880600, 4, 2, 0),
            std::uint64_t{0x1d880600} >> 5);
    publish(0, 1, 7, encode(0x17090c00, 0x1d880610, 4, 2, 0),
            std::uint64_t{0x1d880610} >> 5);
    consumer.write(0, 0x3e00 + 4, 6 | (7 << 16));
    consumer.write(0, 0x20c, 3);
    consumer.write(0, 0x204, 2);
    consumer.write(0, 0x008, 1);
    sc_core::sc_start(2, sc_core::SC_US);
    assert(consumer.memory.sram[0x600] == 1);
    assert(consumer.memory.sram[0x610] == 1);
    assert(consumer.read(0, 0x560, 8) == 2);
    publish(0, 0, 9, encode(0x17090c00, 0x1d880720, 4, 2, 0),
            std::uint64_t{0x1d880720} >> 5);
    assert(consumer.acehr0.trigger_external(0, delay));
    sc_core::sc_start(2, sc_core::SC_US);
    assert(consumer.read(0, 0x560, 8) == 3);
    consumer.write(0, 0x008, 0);
    assert(consumer.read(0, 0x560, 8) == 1);
    consumer.write(0, 0x004, 0);
    assert(consumer.read(0, 0x560, 8) == 0);

    std::uint64_t invalid_bounds = 7 | (6 << 16);
    assert(consumer.access(0, 0x3e00 + 4, true, invalid_bounds) ==
           tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.read(0, 0x3e00 + 4) == (6 | (7 << 16)));
    consumer.write(0, 0x4000 + 16 * 8,
                   encode(0x17090c00, 0x1d880700, 4, 2, 0), 8);
    consumer.write(0, 0x3e00 + 4, 8 | (8 << 16));
    request = 1;
    assert(consumer.access(0, 0x008, true, request) == tlm::TLM_COMMAND_ERROR_RESPONSE);
    assert(consumer.memory.sram[0x700] == 0);
    assert(consumer.read(0, 0x560, 8) == 0);
    consumer.write(0, 0x204, 0);

    // A long request cannot publish service before its bus work elapses.
    for (unsigned i = 0; i < 0x4000; ++i) {
        const auto value = static_cast<unsigned char>(i * 17 + (i >> 8));
        consumer.memory.sram[i] = value;
        consumer.memory.sram[0x4000 + i] = ~value;
    }
    publish(0, 0, 10, encode(0x1d880000, 0x1d884000, 2048, 3, 0),
            std::uint64_t{0x1d884000} >> 5);
    consumer.write(0, 0x20c, 1);
    consumer.write(0, 0x004, 1);
    sc_core::sc_start(100, sc_core::SC_US);
    assert(consumer.read(0, 0x570, 8) == 0);
    assert(!consumer.irq0.read());
    sc_core::sc_start(200, sc_core::SC_US);
    for (unsigned i = 0; i < 0x4000; ++i)
        assert(consumer.memory.sram[0x4000 + i] == consumer.memory.sram[i]);
    assert(consumer.read(0, 0x570, 8) == 1);
    assert(consumer.irq0.read());
    consumer.write(0, 0x004, 0);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    assert(!consumer.irq0.read());

    std::cout << "CPUCP_ACEHR_REQUEST_BOUNDARY_PASS\n";
    return 0;
}

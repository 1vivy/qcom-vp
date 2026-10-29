// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/smmu/smmu.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

// Each port has its own physical egress. Neither base is an SMMU register
// address, and neither line is a production RAM address.
constexpr std::uint64_t secure_base = 0x700000000;
constexpr std::uint64_t nonsecure_base = 0x780000000;
// Both producers use the current firmware CRYPTO StreamID from the selected
// IORT: 0xc2. The Secure mapping is InputBase 0x81010000 with bit 31 set.
constexpr std::uint64_t va_base = 0x82740000;
constexpr unsigned data_offset = 0x30000;

struct Ram : sc_core::sc_module {
    tlm_utils::simple_target_socket<Ram> target_socket{"target_socket"};
    std::uint64_t base;
    std::array<unsigned char, 0x40000> bytes{};
    unsigned accesses = 0, secure_accesses = 0, nonsecure_accesses = 0;
    unsigned unspecified = 0, errors = 0;
    unsigned walk_secure = 0, walk_nonsecure = 0;
    unsigned data_secure = 0, data_nonsecure = 0;

    Ram(sc_core::sc_module_name name, std::uint64_t base_address)
        : sc_module(name), base(base_address) {
        target_socket.register_b_transport(this, &Ram::transport);
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        const auto* attrs = tx.get_extension<QemuMemTxAttrsExtension>();
        const auto address = tx.get_address();
        const auto length = tx.get_data_length();
        if (!attrs || attrs->attrs.unspecified) {
            ++unspecified;
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if ((!tx.is_read() && !tx.is_write()) || !tx.get_data_ptr() || !length ||
            address < base || length > bytes.size() ||
            address - base > bytes.size() - length) {
            ++errors;
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        ++accesses;
        if (attrs->attrs.secure) ++secure_accesses;
        else ++nonsecure_accesses;
        const auto offset = static_cast<unsigned>(address - base);
        if (offset < data_offset) {
            if (attrs->attrs.secure) ++walk_secure;
            else ++walk_nonsecure;
        } else {
            if (attrs->attrs.secure) ++data_secure;
            else ++data_nonsecure;
        }
        auto* memory = bytes.data() + offset;
        if (tx.is_read()) std::memcpy(tx.get_data_ptr(), memory, length);
        else std::memcpy(memory, tx.get_data_ptr(), length);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void word(unsigned offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = value >> (8 * i);
    }
    void qword(unsigned offset, std::uint64_t value) {
        word(offset, static_cast<std::uint32_t>(value));
        word(offset + 4, static_cast<std::uint32_t>(value >> 32));
    }
};

struct Consumer : sc_core::sc_module {
    qcom_vp::ArmSmmuV2 smmu{"smmu"};
    qcom_vp::ArmSmmuStreamPort secure_port{"secure_port", &smmu};
    // Same StreamID, different input security and different physical egress.
    qcom_vp::ArmSmmuStreamPort nonsecure_port{"nonsecure_port", &smmu};
    Ram secure_ram{"secure_ram", secure_base};
    Ram nonsecure_ram{"nonsecure_ram", nonsecure_base};
    tlm_utils::simple_initiator_socket<Consumer> mmio{"mmio"};
    tlm_utils::simple_initiator_socket<Consumer> secure_bus{"secure_bus"};
    tlm_utils::simple_initiator_socket<Consumer> nonsecure_bus{"nonsecure_bus"};

    explicit Consumer(sc_core::sc_module_name name) : sc_module(name) {
        secure_port.stream_id = 0xc2;
        nonsecure_port.stream_id = 0xc2;
        mmio.bind(smmu.target_socket);
        secure_bus.bind(secure_port.target_socket);
        nonsecure_bus.bind(nonsecure_port.target_socket);
        secure_port.initiator_socket.bind(secure_ram.target_socket);
        nonsecure_port.initiator_socket.bind(nonsecure_ram.target_socket);
    }

    using Bus = tlm_utils::simple_initiator_socket<Consumer>;

    tlm::tlm_response_status access(Bus& bus, std::uint64_t address, bool write,
                                    std::uint32_t& value, bool secure) {
        unsigned char bytes[4]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        QemuMemTxAttrsExtension attrs;
        attrs.attrs = {};
        attrs.attrs.unspecified = false;
        attrs.attrs.secure = secure;
        tlm::tlm_generic_payload tx;
        tx.set_address(address);
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(4);
        tx.set_streaming_width(4);
        tx.set_extension(&attrs);
        auto delay = sc_core::SC_ZERO_TIME;
        bus->b_transport(tx, delay);
        tx.clear_extension<QemuMemTxAttrsExtension>();
        if (tx.is_response_ok()) {
            value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes[i]) << (8 * i);
        }
        return tx.get_response_status();
    }
    void write_mmio(unsigned offset, std::uint32_t value, bool secure = true) {
        std::uint32_t held = value;
        assert(access(mmio, offset, true, held, secure) == tlm::TLM_OK_RESPONSE);
    }
    std::uint32_t read_mmio(unsigned offset, bool secure = true) {
        std::uint32_t value = 0;
        assert(access(mmio, offset, false, value, secure) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    void write64_mmio(unsigned offset, std::uint64_t value, bool secure = true) {
        write_mmio(offset, static_cast<std::uint32_t>(value), secure);
        write_mmio(offset + 4, static_cast<std::uint32_t>(value >> 32), secure);
    }
    std::uint32_t read_ram(Bus& bus, std::uint64_t address, bool secure) {
        std::uint32_t value = 0;
        assert(access(bus, address, false, value, secure) == tlm::TLM_OK_RESPONSE);
        return value;
    }
    static std::uint32_t load(const Ram& ram, unsigned offset) {
        return std::uint32_t(ram.bytes[offset]) |
            (std::uint32_t(ram.bytes[offset + 1]) << 8) |
            (std::uint32_t(ram.bytes[offset + 2]) << 16) |
            (std::uint32_t(ram.bytes[offset + 3]) << 24);
    }

    // 36-bit input, 4 KiB granule, TTBR0. Level-1 index 2 covers
    // 0x80000000..0x83ffffff, level-2 index 0x13 covers 0x82600000..,
    // and one level-3 entry maps the page used below.
    static void build_tables(Ram& ram) {
        ram.qword(0x8000 + 2 * 8, ram.base + 0x9000 | 3);
        ram.qword(0x9000 + 0x13 * 8, ram.base + 0xa000 | 3);
        ram.qword(0xa000 + 0x140 * 8,
                  ram.base + data_offset | 3 | (1u << 10) | (1u << 6));
    }

    // Retained APPS SMMU state from native run393 (CPH2749 16.0.5.700,
    // tz.img afdfaf27...). The capture places SMR127 and context 111 in the
    // Secure state. Table addresses are fixture-owned, not captured addresses.
    void configure() {
        build_tables(secure_ram);
        build_tables(nonsecure_ram);
        secure_ram.word(data_offset, 0x11223344);
        nonsecure_ram.word(data_offset, 0x55667788);
        write_mmio(0x0000, 0x20200626);   // sCR0
        write_mmio(0x0004, 0x0000796f);   // sCR1: 121 NS SMRs, 111 NS CBs
        write_mmio(0x0008, 0x00000000);   // sCR2
        write_mmio(0x0400, 0x00200226);   // NS CR0 alias
        write_mmio(0x09fc, 0x800000c2);   // SMR127, Secure group, SID C2
        write_mmio(0x0dfc, 0x0308006f);   // S2CR127: type 0, NSCFG Secure, CB111
        write_mmio(0x11bc, 0x0001f000);   // CBAR111: stage 1
        write_mmio(0x19bc, 0x00000001);   // CBA2R111: AArch64 tables
        constexpr unsigned secure_cb = 0x80000 + 111 * 0x1000;
        write_mmio(secure_cb + 0x00, 0x200500e1);       // captured SCTLR
        write_mmio(secure_cb + 0x10, 0x40038001);       // captured TCR2
        write64_mmio(secure_cb + 0x20, secure_base + 0x8000);
        write_mmio(secure_cb + 0x30, 0x00802d1c);       // captured TCR

        write_mmio(0x0800, 0x800000c2, false);          // SMR0, Non-secure group
        write_mmio(0x0c00, 0x03000000, false);          // S2CR0: type 0, CB0
        write_mmio(0x1000, 0x0001f000, false);          // CBAR0: stage 1
        write_mmio(0x1800, 0x00000001, false);          // CBA2R0: AArch64 tables
        constexpr unsigned ns_cb = 0x80000;
        write_mmio(ns_cb + 0x00, 0x00000001, false);    // SCTLR.M
        write_mmio(ns_cb + 0x10, 0x40038001, false);    // same TCR2 format
        write64_mmio(ns_cb + 0x20, nonsecure_base + 0x8000, false);
        write_mmio(ns_cb + 0x30, 0x00802d1c, false);
    }

    void run() {
        // Non-secure configuration sees only the sCR1 allocation. The Secure
        // view retains the implemented geometry.
        assert((read_mmio(0x20, false) & 0xff) == 121);
        assert((read_mmio(0x24, false) & 0xff) == 111);
        assert(((read_mmio(0x20, false) >> 16) & 0xff) == 0);
        assert((read_mmio(0x20, true) & 0xff) == 128);
        assert((read_mmio(0x24, true) & 0xff) == 112);
        assert(((read_mmio(0x20, true) >> 16) & 0xff) == 1);
        assert(read_mmio(0x04, false) == 0);
        assert(read_mmio(0x04, true) == 0x0000796f);
        assert(read_mmio(0x09fc, false) == 0);
        assert(read_mmio(0x0dfc, false) == 0);
        assert(read_mmio(0x11bc, false) == 0);
        assert(read_mmio(0x19bc, false) == 0);
        assert(read_mmio(0xef058, false) == 0);
        write_mmio(0x09fc, 0, false);
        write_mmio(0x11bc, 0, false);
        assert(read_mmio(0x09fc, true) == 0x800000c2);
        assert(read_mmio(0x11bc, true) == 0x0001f000);
        assert(read_mmio(0xef010, true) == 0x40038001);
        assert(read_mmio(0xef030, true) == 0x00802d1c);

        // A Secure transaction matches SMR127 and reaches CB111. Its walk and
        // its data are Secure, and it reaches only the Secure port's egress.
        const auto ns_before = nonsecure_ram.accesses;
        assert(read_ram(secure_bus, va_base, true) == 0x11223344);
        assert(secure_ram.walk_secure > 0);
        assert(secure_ram.walk_nonsecure == 0);
        assert(secure_ram.data_secure > 0);
        assert(nonsecure_ram.accesses == ns_before);
        const auto secure_walks = secure_ram.walk_secure;
        assert(read_ram(secure_bus, va_base, true) == 0x11223344);
        assert(secure_ram.walk_secure == secure_walks);

        // Table changes remain stale until a synchronous TLBI write.
        secure_ram.qword(0xa000 + 0x140 * 8,
                         secure_base + data_offset + 0x1000 | 3 | (1u << 10) | (1u << 6));
        secure_ram.word(data_offset + 0x1000, 0xdeadbeef);
        assert(read_ram(secure_bus, va_base, true) == 0x11223344);
        write_mmio(0x64, 1);
        assert(read_ram(secure_bus, va_base, true) == 0xdeadbeef);

        // A context-bank write also invalidates the cached page.
        secure_ram.qword(0xa000 + 0x140 * 8,
                         secure_base + data_offset | 3 | (1u << 10) | (1u << 6) | (1u << 7));
        assert(read_ram(secure_bus, va_base, true) == 0xdeadbeef);
        write_mmio(0xef030, 0x00802d1c);
        assert(read_ram(secure_bus, va_base, true) == 0x11223344);

        // A cached read does not authorize a write. The fault registers match
        // the result after a TLBI forces another walk.
        std::uint32_t permission_data = 0x12345678;
        assert(access(secure_bus, va_base, true, permission_data, true) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        const auto permission_fsr = read_mmio(0xef058, true);
        const auto permission_far = read_mmio(0xef060, true);
        const auto permission_syn = read_mmio(0xef068, true);
        assert((permission_fsr & 0xff) == (1U << 3));
        write_mmio(0xef058, 0xffffffff, true);
        write_mmio(0x64, 1);
        assert(access(secure_bus, va_base, true, permission_data, true) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(read_mmio(0xef058, true) == permission_fsr);
        assert(read_mmio(0xef060, true) == permission_far);
        assert(read_mmio(0xef068, true) == permission_syn);
        write_mmio(0xef058, 0xffffffff, true);

        // The same StreamID from the Non-secure state matches only SMR0 and
        // reaches CB0. It cannot observe or use the Secure entry.
        const auto secure_before = secure_ram.accesses;
        assert(read_ram(nonsecure_bus, va_base, false) == 0x55667788);
        assert(nonsecure_ram.walk_nonsecure > 0);
        assert(nonsecure_ram.walk_secure == 0);
        assert(nonsecure_ram.data_nonsecure > 0);
        assert(nonsecure_ram.data_secure == 0);
        assert(secure_ram.accesses == secure_before);

        std::uint32_t value = 0xa5a5a5a5;
        std::uint32_t held = value;
        assert(access(nonsecure_bus, va_base + 4, true, held, false) == tlm::TLM_OK_RESPONSE);
        assert(load(nonsecure_ram, data_offset + 4) == value);

        // A Non-secure entry that names the Secure bank faults and borrows
        // nothing. The Non-secure fault bank records UUT.
        write_mmio(0x0c00, 0x0300006f, false);
        const auto secure_seen = secure_ram.accesses;
        const auto ns_seen = nonsecure_ram.accesses;
        std::uint32_t ignored = 0;
        assert(access(nonsecure_bus, va_base, false, ignored, false) ==
               tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(secure_ram.accesses == secure_seen);
        assert(nonsecure_ram.accesses == ns_seen);
        assert(read_mmio(0x448, false) == (1U << 3));
        write_mmio(0x448, 0xffffffff, false);
        assert(read_mmio(0x448, false) == 0);
        write_mmio(0x0c00, 0x03000000, false);

        // The captured TCR2 carries NSCFG1=1. TTBR1 is still absent: a VA
        // outside the T0SZ range takes a translation fault, not a walk.
        assert(access(secure_bus, std::uint64_t{1} << 36, false, ignored, true) ==
               tlm::TLM_ADDRESS_ERROR_RESPONSE);
        const auto fsr = read_mmio(0xef058, true);
        assert((fsr & 0xff) == (1U << 1));
        assert((fsr & 0x600) == 0x400);
        write_mmio(0xef058, 0xffffffff, true);
        assert((read_mmio(0xef058, true) & 0x3ff) == 0);

        // S2CR.NSCFG=3 makes a Secure group map Non-secure memory. Both the
        // walk and the downstream data become Non-secure.
        write_mmio(0x0dfc, 0x030c006f);
        const auto walk_ns_before = secure_ram.walk_nonsecure;
        const auto data_ns_before = secure_ram.data_nonsecure;
        assert(read_ram(secure_bus, va_base, true) == 0x11223344);
        assert(secure_ram.walk_nonsecure > walk_ns_before);
        assert(secure_ram.data_nonsecure > data_ns_before);
        write_mmio(0x0dfc, 0x0308006f);

        // SCTLR.NSCFG applies when translation is disabled. Identity traffic
        // uses the captured context format and never turns a Non-secure
        // request Secure.
        write_mmio(0xef000, 0x200500e0);
        const auto disabled_secure_before = secure_ram.data_secure;
        assert(read_ram(secure_bus, secure_base + data_offset, true) == 0x11223344);
        assert(secure_ram.data_secure > disabled_secure_before);
        write_mmio(0xef000, 0x300500e0);
        const auto disabled_ns_before = secure_ram.data_nonsecure;
        assert(read_ram(secure_bus, secure_base + data_offset, true) == 0x11223344);
        assert(secure_ram.data_nonsecure > disabled_ns_before);
        write_mmio(0xef000, 0x200500e1);

        // Leaf NS=1 in a Secure walk selects a Non-secure downstream access.
        const auto leaf = secure_base + data_offset | 3 | (1U << 10) | (1U << 6);
        secure_ram.qword(0xa000 + 0x140 * 8, leaf | (1U << 5));
        secure_ram.qword(0x8000 + 2 * 8, secure_base + 0x9000 | 3);
        write_mmio(0x64, 1);
        const auto walk_secure_before = secure_ram.walk_secure;
        const auto leaf_ns_before = secure_ram.data_nonsecure;
        assert(read_ram(secure_bus, va_base, true) == 0x11223344);
        assert(secure_ram.walk_secure > walk_secure_before);
        assert(secure_ram.data_nonsecure > leaf_ns_before);

        // NSTable=1 sends the next-level walks to Non-secure memory. The
        // Security state never returns to Secure after that point.
        secure_ram.qword(0x8000 + 2 * 8,
                         secure_base + 0x9000 | 3 | (std::uint64_t{1} << 63));
        write_mmio(0x64, 1);
        const auto walk_nonsecure_before = secure_ram.walk_nonsecure;
        assert(read_ram(secure_bus, va_base, true) == 0x11223344);
        assert(secure_ram.walk_nonsecure >= walk_nonsecure_before + 2);
        secure_ram.qword(0xa000 + 0x140 * 8, leaf);
        secure_ram.qword(0x8000 + 2 * 8, secure_base + 0x9000 | 3);

        // Run674 QHEE stage 2: CBAR106 type 0 with VMID 3, and a 36-bit IPA from
        // level one. Stage 2 maps the stream's IPA page read-only to a second page,
        // and it maps the stage 1 tables and data with an identity 2 MiB block.
        constexpr unsigned ns_cb = 0x80000;
        constexpr unsigned s2_cb = 0x80000 + 106 * 0x1000;
        const std::uint64_t s2_root = nonsecure_base + 0x10000;
        nonsecure_ram.qword(0x10000 + 2 * 8, nonsecure_base + 0x11000 | 3);
        nonsecure_ram.qword(0x11000 + 0x13 * 8, nonsecure_base + 0x12000 | 3);
        nonsecure_ram.qword(0x12000 + 0x140 * 8,
                            nonsecure_base + data_offset + 0x1000 | 3 | (1U << 10) | (1U << 6));
        nonsecure_ram.qword(0x10000 + 0x1e * 8, nonsecure_base + 0x13000 | 3);
        nonsecure_ram.qword(0x13000, nonsecure_base | 1 | (1U << 10) | (3U << 6));
        nonsecure_ram.word(data_offset + 0x1000, 0x99aabbcc);
        write_mmio(0x1000 + 106 * 4, 0x00000003, false);
        write_mmio(0x1800 + 106 * 4, 0x00000001, false);
        write64_mmio(s2_cb + 0x20, s2_root, false);
        write_mmio(s2_cb + 0x30, 0x0001355c, false);
        write_mmio(s2_cb + 0x00, 0x00cf00e1, false);

        // A stage 2 bank translates the input address as its IPA.
        write_mmio(0x0c00, 0x0300006a, false);
        assert(read_ram(nonsecure_bus, va_base, false) == 0x99aabbcc);

        // A stage-2-only translation is reused until TLBI. A table change stays
        // stale, and the cached entry keeps the read-only stage 2 permission.
        const auto s2_walks = nonsecure_ram.walk_nonsecure;
        assert(read_ram(nonsecure_bus, va_base, false) == 0x99aabbcc);
        assert(nonsecure_ram.walk_nonsecure == s2_walks);
        nonsecure_ram.qword(0x12000 + 0x140 * 8,
                            nonsecure_base + data_offset + 0x2000 | 3 | (1U << 10) | (1U << 6));
        nonsecure_ram.word(data_offset + 0x2000, 0x76543210);
        assert(read_ram(nonsecure_bus, va_base, false) == 0x99aabbcc);
        write_mmio(0x64, 1, false);
        assert(read_ram(nonsecure_bus, va_base, false) == 0x76543210);
        nonsecure_ram.qword(0x12000 + 0x140 * 8,
                            nonsecure_base + data_offset + 0x1000 | 3 | (1U << 10) | (1U << 6));
        write_mmio(0x64, 1, false);
        assert(read_ram(nonsecure_bus, va_base, false) == 0x99aabbcc);
        std::uint32_t s2_denied = 0;
        assert(access(nonsecure_bus, va_base, true, s2_denied, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        const auto s2_fsr = read_mmio(s2_cb + 0x58, false);
        const auto s2_far = read_mmio(s2_cb + 0x60, false);
        const auto s2_syn = read_mmio(s2_cb + 0x68, false);
        assert((s2_fsr & 0xff) == (1U << 3));
        write_mmio(s2_cb + 0x58, 0xffffffff, false);
        write_mmio(0x64, 1, false);
        assert(access(nonsecure_bus, va_base, true, s2_denied, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert(read_mmio(s2_cb + 0x58, false) == s2_fsr);
        assert(read_mmio(s2_cb + 0x60, false) == s2_far);
        assert(read_mmio(s2_cb + 0x68, false) == s2_syn);
        write_mmio(s2_cb + 0x58, 0xffffffff, false);

        // A nested bank with stage 1 disabled sends the input address to stage 2.
        write_mmio(0x0c00, 0x03000000, false);
        write_mmio(0x1000, 0x00036a03, false);
        write_mmio(ns_cb + 0x00, 0x0a9f00e0, false);
        assert(read_ram(nonsecure_bus, va_base, false) == 0x99aabbcc);
        // This nested bank also bypasses stage 1, so its outcome is cached.
        const auto bypass_walks = nonsecure_ram.walk_nonsecure;
        assert(read_ram(nonsecure_bus, va_base, false) == 0x99aabbcc);
        assert(nonsecure_ram.walk_nonsecure == bypass_walks);

        // S2AP denies the write. The stage 2 bank records the fault and the IPA.
        std::uint32_t denied = 0;
        assert(access(nonsecure_bus, va_base, true, denied, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert((read_mmio(s2_cb + 0x58, false) & 0xff) == (1U << 3));
        assert(read_mmio(s2_cb + 0x60, false) == va_base);
        write_mmio(s2_cb + 0x58, 0xffffffff, false);
        assert(load(nonsecure_ram, data_offset + 0x1000) == 0x99aabbcc);

        // With stage 1 enabled, stage 2 translates each table fetch and the output IPA.
        write_mmio(ns_cb + 0x00, 0x00000001, false);
        assert(read_ram(nonsecure_bus, va_base, false) == 0x55667788);
        nonsecure_ram.qword(0x10000 + 0x1e * 8, 0);
        write_mmio(0x64, 1, false);
        assert(access(nonsecure_bus, va_base, false, denied, false) == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        assert((read_mmio(s2_cb + 0x58, false) & 0xff) == (1U << 1));
        assert(read_mmio(s2_cb + 0x60, false) == static_cast<std::uint32_t>(nonsecure_base + 0x8000 + 2 * 8));
        write_mmio(s2_cb + 0x58, 0xffffffff, false);

        // Every translated access carries a specified security attribute. A
        // Non-secure origin never produced a Secure access or table walk.
        assert(secure_ram.unspecified == 0);
        assert(nonsecure_ram.unspecified == 0);
        assert(nonsecure_ram.walk_secure == 0);
        assert(nonsecure_ram.data_secure == 0);
        assert(secure_ram.errors == 0);
        assert(nonsecure_ram.errors == 0);
        assert(secure_ram.accesses > 0);
        assert(nonsecure_ram.accesses > 0);
    }
};

} // namespace

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    Consumer c{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    c.configure();
    c.run();
    std::cout << "SMMU_STREAM_SECURITY_PASS\n";
    return 0;
}

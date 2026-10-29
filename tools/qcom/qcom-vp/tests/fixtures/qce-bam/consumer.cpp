// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/bam/bam.h"
#include "qcom/gpce/qce_v6.h"
#include "qcom/smmu/smmu.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {

// Physical RAM is a separate array. No descriptor, ring or digest address below
// is a physical address, so an identity-mapped path cannot reach this array.
constexpr std::uint64_t ram_base = 0x780000000;
// The synthetic IOVA exceeds 4 GiB. Real page tables translate it to
// ram_base + 0x10000 .. + 0x2ffff.
constexpr std::uint64_t va_base = 0x782740000;
constexpr std::uint64_t table_l1 = ram_base + 0x8000;
constexpr std::uint64_t table_l2 = ram_base + 0x9000;
constexpr std::uint64_t table_l3 = ram_base + 0xa000;
constexpr unsigned mapped_pages = 32;

// Physical offsets of the fixture buffers inside RAM.
constexpr unsigned ring0 = 0x10000, ring1 = 0x10100;
constexpr unsigned command_list = 0x13000;
constexpr unsigned input_a = 0x14000, input_b = 0x14100, input_c = 0x14200,
                   input_d = 0x14300, input_e = 0x14400, input_f = 0x14500;
constexpr unsigned digest_a = 0x15000, digest_b = 0x15100, digest_c = 0x15200,
                   digest_d = 0x15300;
constexpr unsigned nocmd_ring = 0x30000, nocmd_list = 0x31000;

struct Ram : sc_core::sc_module {
    tlm_utils::multi_passthrough_target_socket<Ram> target_socket{"target_socket"};
    std::array<unsigned char, 0x40000> bytes{};
    bool saw_ring = false, saw_payload = false, saw_digest = false;
    unsigned nonsecure = 0, address_errors = 0, accesses = 0;
    std::uint64_t last_address = 0;

    explicit Ram(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &Ram::transport);
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        const auto* attrs = tx.get_extension<QemuMemTxAttrsExtension>();
        if (!attrs || attrs->attrs.unspecified || !attrs->attrs.secure) {
            ++nonsecure;
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto address = tx.get_address();
        const auto size = tx.get_data_length();
        if (address < ram_base || size > bytes.size() || address - ram_base > bytes.size() - size ||
            !tx.get_data_ptr() || (!tx.is_read() && !tx.is_write())) {
            ++address_errors;
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const auto offset = static_cast<unsigned>(address - ram_base);
        last_address = address;
        ++accesses;
        if (offset == ring0) saw_ring = true;
        if (offset >= input_a && offset < input_b) saw_payload = true;
        if (offset == digest_a) saw_digest = true;
        auto* memory = bytes.data() + offset;
        if (tx.is_read()) std::memcpy(tx.get_data_ptr(), memory, size);
        else std::memcpy(memory, tx.get_data_ptr(), size);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void word(unsigned offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = value >> (8 * i);
    }
    void qword(unsigned offset, std::uint64_t value) {
        word(offset, static_cast<std::uint32_t>(value));
        word(offset + 4, static_cast<std::uint32_t>(value >> 32));
    }
    void descriptor(unsigned ring, unsigned offset, std::uint64_t address, unsigned size,
                    unsigned flags) {
        word(ring + offset, static_cast<std::uint32_t>(address));
        word(ring + offset + 4, (size & 0xffff) |
             (static_cast<std::uint32_t>((address >> 32) & 0xff) << 16) | (flags << 24));
    }
    void digest(unsigned offset, const char* expected, unsigned length) const {
        const auto nibble = [](char c) { return unsigned(c <= '9' ? c - '0' : c - 'a' + 10); };
        for (unsigned i = 0; i < length; ++i)
            assert(bytes[offset + i] == (nibble(expected[i * 2]) << 4 | nibble(expected[i * 2 + 1])));
    }
};

// Opcode 4 command targets are absolute physical register addresses. This
// router never translates and never accepts an address from the IOVA window.
struct CommandTarget : sc_core::sc_module {
    tlm_utils::multi_passthrough_target_socket<CommandTarget> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<CommandTarget> qce{"qce"};
    tlm_utils::simple_initiator_socket<CommandTarget> bam{"bam"};
    unsigned writes = 0, nonsecure = 0;
    bool saw_goproc = false, saw_self_reset = false;

    explicit CommandTarget(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &CommandTarget::transport);
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        const auto* attrs = tx.get_extension<QemuMemTxAttrsExtension>();
        if (!attrs || attrs->attrs.unspecified || !attrs->attrs.secure) {
            ++nonsecure;
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto address = tx.get_address();
        if (address >= 0x01dc4000 && address < 0x01de4000) {
            ++writes;
            if (address == 0x01dc4000) saw_self_reset = true;
            tx.set_address(address - 0x01dc4000);
            bam->b_transport(tx, delay);
            tx.set_address(address);
            return;
        }
        if (address >= 0x01df0000 && address < 0x01e00000) {
            ++writes;
            if (address == 0x01dfa120) saw_goproc = true;
            tx.set_address(address - 0x01df0000);
            qce->b_transport(tx, delay);
            tx.set_address(address);
            return;
        }
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

// RAM and register bus for the unbound-command test. It would forward an opcode 4
// write to QCE if the BAM wrongly used dma_socket, so success is visible there.
struct NoCmdBus : sc_core::sc_module {
    tlm_utils::simple_target_socket<NoCmdBus> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<NoCmdBus> qce{"qce"};
    std::array<unsigned char, 0x40000> bytes{};
    unsigned register_writes = 0, nonsecure = 0;

    explicit NoCmdBus(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &NoCmdBus::transport);
    }
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        const auto* attrs = tx.get_extension<QemuMemTxAttrsExtension>();
        if (!attrs || attrs->attrs.unspecified || !attrs->attrs.secure) {
            ++nonsecure;
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto address = tx.get_address();
        if (address >= 0x01df0000 && address < 0x01e00000) {
            ++register_writes;
            tx.set_address(address - 0x01df0000);
            qce->b_transport(tx, delay);
            tx.set_address(address);
            return;
        }
        const auto size = tx.get_data_length();
        if (address < ram_base || size > bytes.size() || address - ram_base > bytes.size() - size ||
            !tx.get_data_ptr() || (!tx.is_read() && !tx.is_write())) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* memory = bytes.data() + (address - ram_base);
        if (tx.is_read()) std::memcpy(tx.get_data_ptr(), memory, size);
        else std::memcpy(memory, tx.get_data_ptr(), size);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void word(unsigned offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = value >> (8 * i);
    }
    void descriptor(unsigned ring, std::uint64_t address, unsigned size, unsigned flags) {
        word(ring, static_cast<std::uint32_t>(address));
        word(ring + 4, (size & 0xffff) |
             (static_cast<std::uint32_t>((address >> 32) & 0xff) << 16) | (flags << 24));
    }
};

} // namespace

struct Consumer : sc_core::sc_module {
    qcom_vp::QcomBamV2 bam{"bam"};
    qcom_vp::QceV6 qce{"qce"};
    Ram ram{"ram"};
    CommandTarget commands{"commands"};
    qcom_vp::ArmSmmuV2 smmu{"smmu"};
    qcom_vp::ArmSmmuStreamPort port{"port", &smmu};
    // Second BAM: its command socket stays unbound, so an opcode 4 element must
    // fail the pipe instead of borrowing the RAM path.
    qcom_vp::QcomBamV2 bam_nocmd{"bam_nocmd"};
    NoCmdBus probe{"probe"};
    tlm_utils::simple_initiator_socket<Consumer> bam_bus{"bam_bus"};
    tlm_utils::simple_initiator_socket<Consumer> nocmd_bus{"nocmd_bus"};
    tlm_utils::simple_initiator_socket<Consumer> qce_bus{"qce_bus"};
    tlm_utils::simple_initiator_socket<Consumer> smmu_bus{"smmu_bus"};
    sc_core::sc_signal<bool> irq{"irq"};

    using Bus = tlm_utils::simple_initiator_socket<Consumer>;

    explicit Consumer(sc_core::sc_module_name name, const std::string& key = {})
        : sc_module(name) {
        if (!key.empty()) qce.oem_hw_key_hex.set_value(key);
        bam.num_pipes = 2;
        bam.num_ees = 1;
        bam.revision = 0x20;
        bam.command_base = 0x01dc0000;
        bam.address_bits = 36;
        bam_nocmd.num_pipes = 1;
        bam_nocmd.num_ees = 1;
        bam_nocmd.revision = 0x20;
        bam_nocmd.command_base = 0x01dfa000;
        bam_nocmd.address_bits = 36;
        port.stream_id = 0xc2;
        bam_bus.bind(bam.target_socket);
        nocmd_bus.bind(bam_nocmd.target_socket);
        qce_bus.bind(qce.target_socket);
        smmu_bus.bind(smmu.target_socket);
        bam.dma_socket.bind(port.target_socket);
        port.initiator_socket.bind(ram.target_socket);
        bam.command_socket.bind(commands.target_socket);
        commands.qce.bind(qce.target_socket);
        commands.bam.bind(bam.target_socket);
        bam_nocmd.dma_socket.bind(probe.target_socket);
        probe.qce.bind(qce.target_socket);
        bam.stream_socket.bind(qce.stream_socket);
        qce.stream_ready.bind(bam.stream_ready);
        bam.irq[0].bind(irq);
    }

    std::array<unsigned char, 16> oem_root_probe() {
        configure();
        write_bam(0, 2);
        write_bam(0x3004, 3);
        pipe(0, 0, 24, false);
        pipe(1, 0x100, 16, true);
        for (unsigned i = 0; i < 16; ++i)
            ram.bytes[input_e + i] = static_cast<unsigned char>(0x10 + i);
        cmac(16, true);
        write_qce(0xc000, 7);
        ram.descriptor(ring1, 0, va_base + 0x5300, 128, 0xc0);
        write_bam(0xd818, 8);
        ram.descriptor(ring0, 0, va_base + 0x4400, 16, 0xc0);
        write_bam(0xc818, 8);
        sc_core::sc_start(1, sc_core::SC_NS);
        std::array<unsigned char, 16> result{};
        std::copy_n(ram.bytes.data() + digest_d, result.size(), result.data());
        return result;
    }

    std::uint32_t access(Bus& bus, unsigned address, bool writing,
                         std::uint32_t value = 0, bool secure = true) {
        unsigned char bytes[4]{};
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
        QemuMemTxAttrsExtension attrs;
        attrs.attrs = {};
        attrs.attrs.unspecified = false;
        attrs.attrs.secure = secure;
        tlm::tlm_generic_payload tx;
        tx.set_address(address);
        tx.set_command(writing ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_data_ptr(bytes);
        tx.set_data_length(4);
        tx.set_streaming_width(4);
        tx.set_extension(&attrs);
        auto delay = sc_core::SC_ZERO_TIME;
        bus->b_transport(tx, delay);
        tx.clear_extension<QemuMemTxAttrsExtension>();
        assert(tx.is_response_ok());
        value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(bytes[i]) << (8 * i);
        return value;
    }
    void write(Bus& bus, unsigned address, std::uint32_t value, bool secure = true) {
        access(bus, address, true, value, secure);
    }
    void write_bam(unsigned address, std::uint32_t value, bool secure = true) {
        write(bam_bus, address, value, secure);
    }
    std::uint32_t read_bam(unsigned address) { return access(bam_bus, address, false); }
    std::uint32_t read_bus(Bus& bus, unsigned address) { return access(bus, address, false); }
    std::uint32_t read_smmu(unsigned address) { return access(smmu_bus, address, false); }
    void write_smmu(unsigned address, std::uint32_t value) {
        write(smmu_bus, address, value);
    }
    void write_qce(unsigned address, std::uint32_t value) { write(qce_bus, address, value); }
    std::uint32_t read_qce(unsigned address) { return access(qce_bus, address, false); }

    // The page tables map only the 32 pages that this fixture uses.
    void build_tables() {
        ram.qword(0x8000 + ((va_base >> 30) & 0x3f) * 8, table_l2 | 3);
        ram.qword(0x9000 + ((va_base >> 21) & 0x1ff) * 8, table_l3 | 3);
        for (unsigned i = 0; i < mapped_pages; ++i)
            ram.qword(0xa000 + ((va_base >> 12) & 0x1ff) * 8 + i * 8,
                      (ram_base + 0x10000 + i * 0x1000) | 3 | (1u << 10));
    }

    // Secure firmware state from the retained APPS capture. The secure bank owns
    // SMR127 and context 111. The captured TCR2 is stored without reinterpretation.
    void configure() {
        build_tables();
        write(smmu_bus, 0x0000, 0x20200626);      // secure CR0
        write(smmu_bus, 0x0004, 0x0000796f);      // secure CR1: 121 NS SMRs, 111 NS CBs
        write(smmu_bus, 0x0008, 0x00000000);      // secure CR2
        write(smmu_bus, 0x0400, 0x00200226);      // NS CR0 alias
        write(smmu_bus, 0x09fc, 0x800000c2);      // SMR127 matches SID C2, secure group
        write(smmu_bus, 0x0dfc, 0x0308006f);      // S2CR127: stage 1, context 111
        write(smmu_bus, 0x11bc, 0x0001f000);      // CBAR111: stage 1
        write(smmu_bus, 0x19bc, 0x00000001);      // CBA2R111: AArch64 tables
        constexpr unsigned cb = 0xef000;
        write(smmu_bus, cb + 0x00, 0x200500e1);   // captured SCTLR: M set
        write(smmu_bus, cb + 0x10, 0x40038001);   // captured TCR2: 36-bit PASize, NSCFG0 clear
        write(smmu_bus, cb + 0x20, static_cast<std::uint32_t>(table_l1));
        write(smmu_bus, cb + 0x24, static_cast<std::uint32_t>(table_l1 >> 32));
        write(smmu_bus, cb + 0x30, 0x00802d1c);   // TCR: T0SZ 28, 4 KiB, TTBR0
    }

    void pipe_on(Bus& bus, unsigned index, std::uint64_t ring_address, unsigned size,
                 bool output) {
        const auto base = 0xc000 + index * 0x1000;
        write(bus, base + 4, 1);
        write(bus, base + 4, 0);
        write(bus, base + 0x81c, static_cast<std::uint32_t>(ring_address));
        if ((access(bus, 0x1008, false) >> 14) & 3)
            write(bus, base + 0x914, static_cast<std::uint32_t>(ring_address >> 32));
        write(bus, base + 0x820, size);
        write(bus, base + 0x828, 1);
        write(bus, base + 0x18, 0xa1);
        write(bus, base, 0x22 | (output ? 8 : 0));
    }
    void pipe(unsigned index, unsigned offset, unsigned size, bool output) {
        pipe_on(bam_bus, index, va_base + offset, size, output);
    }
    static std::uint32_t reverse(std::uint32_t value) {
        return (value >> 24) | ((value >> 8) & 0xff00) |
               ((value << 8) & 0xff0000) | (value << 24);
    }
    // AUTH_SIZE zero selects SHA-1, one selects SHA-256, and two selects SHA-384.
    void hash(unsigned size, unsigned length, bool first, bool last) {
        write_qce(0xa400, 0xe020e);
        write_qce(0xa200, 0);
        write_qce(0xa110, length);
        write_qce(0xa304, length);
        write_qce(0xa308, 0);
        write_qce(0xa300, 1 | (size << 9) |
                  (first ? 0x20000 : 0) | (last ? 0x10000 : 0));
        if (!first) return;
        const std::array<std::uint32_t, 8> iv256{
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        const std::array<std::uint32_t, 5> iv1{
            0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
        // QSEE 156c6c84 writes each 64-bit SHA-384 word high half first.
        const std::array<std::uint32_t, 16> iv384{
            0xcbbb9d5d, 0xc1059ed8, 0x629a292a, 0x367cd507,
            0x9159015a, 0x3070dd17, 0x152fecd8, 0xf70e5939,
            0x67332667, 0xffc00b31, 0x8eb44a87, 0x68581511,
            0xdb0c2e0d, 0x64f98fa7, 0x47b5481d, 0xbefa4fa4};
        const std::array<std::uint32_t, 16> iv512{
            0x6a09e667, 0xf3bcc908, 0xbb67ae85, 0x84caa73b,
            0x3c6ef372, 0xfe94f82b, 0xa54ff53a, 0x5f1d36f1,
            0x510e527f, 0xade682d1, 0x9b05688c, 0x2b3e6c1f,
            0x1f83d9ab, 0xfb41bd6b, 0x5be0cd19, 0x137e2179};
        for (unsigned i = 0; i < 16; ++i) {
            const auto word = size == 3 ? iv512[i] : size == 2 ? iv384[i] :
                size == 1 ? (i < iv256.size() ? iv256[i] : 0) :
                            (i < iv1.size() ? iv1[i] : 0);
            write_qce(0xa310 + i * 4, reverse(word));
        }
        for (unsigned i = 0; i < 4; ++i) write_qce(0xa390 + i * 4, 0);
    }
    // QSEE RPMB key derivation: AES-256 CMAC, USE_HW_KEY, 16-byte MAC, AUTH_FIRST.
    void cmac(unsigned length, bool last, bool hw_key = true) {
        write_qce(0xa400, 0xe020e);
        write_qce(0xa200, 0);
        write_qce(0xa110, length);
        write_qce(0xa304, length);
        write_qce(0xa308, 0);
        write_qce(0xa300, (0x65e52 & ~(hw_key ? 0u : 0x40000u)) | (last ? 0x10000 : 0));
        for (unsigned i = 0; i < 4; ++i) write_qce(0xa390 + i * 4, 0);
    }
};

int sc_main(int argc, char** argv) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    if (argc > 1 && std::string(argv[1]) == "--root-profile") {
        const std::string root_a(64, '1');
        const std::string root_b(64, '2');
        Consumer first{"first", root_a};
        Consumer repeat{"repeat", root_a};
        Consumer different{"different", root_b};
        sc_core::sc_start(sc_core::SC_ZERO_TIME);
        const auto a = first.oem_root_probe();
        const auto same = repeat.oem_root_probe();
        const auto b = different.oem_root_probe();
        assert(a == same && a != b);
        std::cout << "QCE_ROOT_PROFILE_PASS\n";
        return 0;
    }
    if (argc > 1 && (std::string(argv[1]) == "--invalid-length" ||
                     std::string(argv[1]) == "--invalid-hex")) {
        const auto bad = std::string(argv[1]) == "--invalid-length" ?
            std::string(63, '1') : std::string(63, '1') + 'g';
        Consumer invalid{"invalid", bad};
        try {
            sc_core::sc_start(sc_core::SC_ZERO_TIME);
        } catch (const std::invalid_argument& error) {
            assert(std::string(error.what()).find("oem_hw_key_hex") != std::string::npos);
            std::cout << "QCE_ROOT_INVALID_PASS\n";
            return 0;
        }
        return 1;
    }

    Consumer c{"consumer"};
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    c.configure();
    assert(c.read_smmu(0x09fc) == 0x800000c2);
    assert(c.read_smmu(0x0dfc) == 0x0308006f);
    assert(c.read_smmu(0x0ef010) == 0x40038001);
    c.write_bam(0, 2);
    c.write_bam(0x3004, 3);
    c.pipe(0, 0, 24, false);
    c.pipe(1, 0x100, 16, true);

    // SHA-384 uses the SHA-512 family engine and 128-byte blocks. A saved
    // nonfinal state must continue to the correct digest.
    assert((c.read_qce(0xa108) & 0x30) == 0x30);
    std::memset(c.ram.bytes.data() + input_e, 'a', 128);
    std::memcpy(c.ram.bytes.data() + input_f, "bbb", 3);
    c.hash(2, 128, true, false);
    c.write_qce(0xa120, 3);
    c.ram.descriptor(ring0, 0, va_base + 0x4400, 128, 0xc0);
    c.write_bam(0xc818, 8);
    sc_core::sc_start(1, sc_core::SC_NS);
    std::array<std::uint32_t, 16> sha384_iv{};
    std::array<std::uint32_t, 4> sha384_count{};
    for (unsigned i = 0; i < sha384_iv.size(); ++i) sha384_iv[i] = c.read_qce(0xa310 + i * 4);
    for (unsigned i = 0; i < sha384_count.size(); ++i) sha384_count[i] = c.read_qce(0xa390 + i * 4);
    c.hash(2, 3, false, true);
    for (unsigned i = 0; i < sha384_iv.size(); ++i) c.write_qce(0xa310 + i * 4, sha384_iv[i]);
    for (unsigned i = 0; i < sha384_count.size(); ++i) c.write_qce(0xa390 + i * 4, sha384_count[i]);
    c.write_qce(0xa120, 7);
    c.ram.descriptor(ring1, 0, va_base + 0x5300, 128, 0xc0);
    c.write_bam(0xd818, 8);
    c.ram.descriptor(ring0, 8, va_base + 0x4500, 3, 0xc0);
    c.write_bam(0xc818, 16);
    sc_core::sc_start(1, sc_core::SC_NS);
    c.ram.digest(digest_d, "44eecf34326eb468db40dbb5d12f8fbf9888eeb909a552a6"
                           "c20069ed57f8483414edea0fb1f4f71ba01fed73ba28ce2a", 48);
    // SHA-512 uses AUTH_SIZE three and returns all 16 state words.
    std::memcpy(c.ram.bytes.data() + input_f, "abc", 3);
    c.hash(3, 3, true, true);
    c.write_qce(0xa120, 7);
    c.ram.descriptor(ring1, 8, va_base + 0x5300, 128, 0xc0);
    c.write_bam(0xd818, 0);
    c.ram.descriptor(ring0, 16, va_base + 0x4500, 3, 0xc0);
    c.write_bam(0xc818, 0);
    sc_core::sc_start(1, sc_core::SC_NS);
    c.ram.digest(digest_d, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                           "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", 64);
    // This default-profile path derives the virtual OEM key from its public label.
    // The separate root-profile mode checks explicit roots through the same CMAC path.
    assert(c.read_qce(0xa108) & 1);
    for (unsigned i = 0; i < 16; ++i) c.ram.bytes[input_e + i] = static_cast<unsigned char>(0x10 + i);
    // A nonfinal block publishes the chaining value AES-256(K, M).
    c.cmac(16, false);
    c.write_qce(0xc000, 7);
    c.ram.descriptor(ring1, 0, va_base + 0x5300, 128, 0xc0);
    c.write_bam(0xd818, 8);
    c.ram.descriptor(ring0, 0, va_base + 0x4400, 16, 0xc0);
    c.write_bam(0xc818, 8);
    sc_core::sc_start(1, sc_core::SC_NS);
    c.ram.digest(digest_d, "f42e3c1f0e674543577473e4fe06026f", 16);
    // A final partial block takes the padded K2 path.
    c.cmac(3, true);
    c.write_qce(0xc000, 7);
    c.ram.descriptor(ring1, 8, va_base + 0x5300, 128, 0xc0);
    c.write_bam(0xd818, 0);
    c.ram.descriptor(ring0, 8, va_base + 0x4500, 3, 0xc0);
    c.write_bam(0xc818, 16);
    sc_core::sc_start(1, sc_core::SC_NS);
    c.ram.digest(digest_d, "248faa1f4b1736391c3bff79459321df", 16);
    // A final complete block takes the K1 path.
    c.cmac(16, true);
    c.write_qce(0xc000, 7);
    c.ram.descriptor(ring1, 0, va_base + 0x5300, 128, 0xc0);
    c.write_bam(0xd818, 8);
    c.ram.descriptor(ring0, 16, va_base + 0x4400, 16, 0xc0);
    c.write_bam(0xc818, 0);
    sc_core::sc_start(1, sc_core::SC_NS);
    c.ram.digest(digest_d, "2e8876a7ae576fb3d6dc83aeb779281f", 16);
    // The plain GO register has no OEM key, and a software AES key is not implemented.
    assert(!(c.read_qce(0xa104) & 0x80000000u));
    c.cmac(16, true);
    c.write_qce(0xa120, 7);
    assert((c.read_qce(0xa100) & 1) && (c.read_qce(0xa104) & 0x80000000u));
    c.write_qce(0xa100, 0);
    c.write_qce(0xa104, 0);
    c.cmac(16, true, false);
    c.write_qce(0xc000, 7);
    assert((c.read_qce(0xa100) & 1) && (c.read_qce(0xa104) & 0x80000000u));
    c.write_qce(0xa100, 0);
    c.write_qce(0xa104, 0);
    // Restore the cold pipe state for the scenarios below.
    c.write_bam(0xc014, 0xff);
    c.write_bam(0xd014, 0xff);
    c.pipe(0, 0, 24, false);
    c.pipe(1, 0x100, 16, true);
    c.ram.saw_ring = c.ram.saw_payload = c.ram.saw_digest = false;
    sc_core::sc_start(1, sc_core::SC_NS);
    std::memcpy(c.ram.bytes.data() + input_a, "abc", 3);
    std::memset(c.ram.bytes.data() + digest_a, 0xa5, 128);
    // Pre-GO: the output descriptor cannot complete and must not touch RAM.
    c.ram.descriptor(ring1, 0, va_base + 0x5000, 128, 0xc0);
    c.write_bam(0xd818, 8);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(c.read_bam(0xd800) == 0);
    for (unsigned i = 0; i < 128; ++i) assert(c.ram.bytes[digest_a + i] == 0xa5);
    assert(!c.ram.saw_digest);

    // The command descriptor starts the real engine before the data descriptor.
    // QSEE programs AUTH_KEY and AUTH_INFO_NONCE through physical command
    // elements before AUTH_SEG_CFG and GO. The key words stay hidden, so the
    // digest must stay that of the unkeyed message.
    c.hash(1, 3, true, true);
    std::array<std::uint32_t, 16> key_words{};
    for (unsigned i = 0; i < key_words.size(); ++i)
        key_words[i] = 0x1000u + i * 0x01010101u;
    const std::array<std::uint32_t, 4> nonce_words{
        0x11111111, 0x22222222, 0x33333333, 0x44444444};
    unsigned element = 0;
    const auto command = [&](std::uint32_t offset, std::uint32_t value) {
        c.ram.word(command_list + element * 8, 0x04000000u | offset);
        c.ram.word(command_list + element * 8 + 4, value);
        ++element;
    };
    // AUTH_KEY0..15 at QCE offset 3040, physical 01dfd040.
    for (unsigned i = 0; i < key_words.size(); ++i)
        command(0x03d040 + i * 4, key_words[i]);
    // AUTH_INFO_NONCE0..3 at QCE offset 350, physical 01dfa350.
    for (unsigned i = 0; i < nonce_words.size(); ++i)
        command(0x03a350 + i * 4, nonce_words[i]);
    // AUTH_SEG_CFG follows the key and nonce words, as in the QSEE list.
    command(0x03a300, 1u | 0x200u | 0x20000u | 0x10000u);
    command(0x03a120, 7);
    c.ram.descriptor(ring0, 0, va_base + 0x3000, element * 8, 8);
    c.ram.descriptor(ring0, 8, va_base + 0x4000, 3, 0xc0);
    c.write_bam(0xc818, 16);
    sc_core::sc_start(1, sc_core::SC_NS);
    c.ram.digest(digest_a, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32);
    // Key words never read back. The nonce words retain their programmed value.
    for (unsigned i = 0; i < key_words.size(); ++i)
        assert(c.read_qce(0xd040 + i * 4) == 0);
    for (unsigned i = 0; i < nonce_words.size(); ++i)
        assert(c.read_qce(0xa350 + i * 4) == nonce_words[i]);
    assert((c.read_bam(0xc800) & 0xffff) == 16);
    assert((c.read_bam(0xd800) & 0xffff) == 8);
    assert(c.read_qce(0xa100) & 2);
    assert(c.irq.read());
    // Translation, not identity: the ring and payload arrive at their physical pages.
    assert(c.ram.saw_ring);
    assert(c.ram.saw_payload);
    assert(c.ram.saw_digest);
    assert(c.ram.nonsecure == 0);
    assert(c.ram.address_errors == 0);
    assert(c.commands.writes > 0);
    assert(c.commands.nonsecure == 0);
    assert(c.commands.saw_goproc);
    c.write_bam(0xc014, 0xff);
    c.write_bam(0xd014, 0xff);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(!c.irq.read());

    // A nonfinal block must survive another hash through its saved IV and count.
    std::memset(c.ram.bytes.data() + input_b, 'a', 64);
    c.hash(0, 64, true, false);
    c.write_qce(0xa120, 3);
    c.ram.descriptor(ring0, 16, va_base + 0x4100, 64, 0xc0);
    c.write_bam(0xc818, 0);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert((c.read_bam(0xc800) & 0xffff) == 0);
    std::array<std::uint32_t, 16> saved_iv{};
    std::array<std::uint32_t, 4> saved_count{};
    for (unsigned i = 0; i < saved_iv.size(); ++i) saved_iv[i] = c.read_qce(0xa310 + i * 4);
    for (unsigned i = 0; i < saved_count.size(); ++i) saved_count[i] = c.read_qce(0xa390 + i * 4);

    std::memcpy(c.ram.bytes.data() + input_c, "xyz", 3);
    c.hash(1, 3, true, true);
    c.write_qce(0xa120, 7);
    c.ram.descriptor(ring1, 8, va_base + 0x5100, 128, 0xc0);
    c.write_bam(0xd818, 0);
    c.ram.descriptor(ring0, 0, va_base + 0x4200, 3, 0xc0);
    c.write_bam(0xc818, 8);
    sc_core::sc_start(1, sc_core::SC_NS);
    c.ram.digest(digest_b, "3608bca1e44ea6c4d268eb6db02260269892c0b42b86bbf1e77a6fa16c3c9282", 32);

    std::memcpy(c.ram.bytes.data() + input_d, "bbb", 3);
    c.hash(0, 3, false, true);
    for (unsigned i = 0; i < saved_iv.size(); ++i) c.write_qce(0xa310 + i * 4, saved_iv[i]);
    for (unsigned i = 0; i < saved_count.size(); ++i) c.write_qce(0xa390 + i * 4, saved_count[i]);
    c.write_qce(0xa120, 7);
    c.ram.descriptor(ring1, 0, va_base + 0x5200, 128, 0xc0);
    c.write_bam(0xd818, 8);
    c.ram.descriptor(ring0, 8, va_base + 0x4300, 3, 0xc0);
    c.write_bam(0xc818, 16);
    sc_core::sc_start(1, sc_core::SC_NS);
    c.ram.digest(digest_c, "ffeceea41e39ec070be9be367748d417dda81bb7", 20);

    // A reset within a command list cancels the remaining register writes. The
    // reset target is physical and must not travel through the SMMU.
    c.write_qce(0xa110, 17);
    c.ram.word(command_list, 0x04004000);
    c.ram.word(command_list + 4, 1);
    c.ram.word(command_list + 8, 0x0403a110);
    c.ram.word(command_list + 12, 123);
    c.ram.descriptor(ring0, 16, va_base + 0x3000, 16, 8);
    c.write_bam(0xc818, 0);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(c.read_qce(0xa110) == 17);
    assert((c.read_bam(0) & 2) == 0);
    assert(c.read_bam(0xc800) == 0);
    assert(c.commands.saw_self_reset);

    // An unbound command socket fails the pipe and never reports completion. The
    // probe DMA bus accepts the descriptor and would forward a register write, so
    // a fallback to dma_socket would complete the descriptor and change QCE.
    c.write(c.nocmd_bus, 0, 2);
    c.pipe_on(c.nocmd_bus, 0, ram_base + nocmd_ring, 16, false);
    c.write_qce(0xa110, 99);
    c.probe.word(0x31000, 0x0403a110);
    c.probe.word(0x31004, 5);
    c.probe.descriptor(0x30000, ram_base + nocmd_list, 8, 8);
    c.write(c.nocmd_bus, 0xc818, 8);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert(c.read_qce(0xa110) == 99);
    assert(c.read_bus(c.nocmd_bus, 0xc800) == 0);
    assert(c.read_bus(c.nocmd_bus, 0xc010) == 0x10);
    assert(c.probe.register_writes == 0);
    assert(c.probe.nonsecure == 0);
    assert(c.ram.nonsecure == 0);

    // A nonsecure submission cannot borrow the secure DMA identity. SMR120 is an
    // NS-owned entry that names SID C2 and selects Secure context 111, so the
    // stream group ownership rule must fault before any physical access.
    c.write_smmu(0x09e0, 0x800000c2);   // SMR120, NS-owned group
    c.write_smmu(0x0de0, 0x0308006f);   // S2CR120 selects Secure context 111
    c.write_bam(0, 2);
    c.pipe(0, 0, 24, false);
    c.hash(1, 3, true, true);
    c.write_qce(0xa120, 3);
    c.ram.descriptor(ring0, 0, va_base + 0x4000, 3, 0xc0);
    c.write_bam(0xc014, 0xff);
    c.write_bam(0xc818, 8, false);
    sc_core::sc_start(1, sc_core::SC_NS);
    assert((c.read_bam(0xc800) & 0xffff) == 0);
    assert(c.read_bam(0xc010) & 0x80);
    assert(!(c.read_qce(0xa100) & 2));
    assert(c.read_smmu(0x448) & 0x8);
    assert(c.ram.nonsecure == 0);
    assert(c.ram.address_errors == 0);
    assert(c.commands.nonsecure == 0);
    std::cout << "QCE_BAM_HASH_PASS\n";
    return 0;
}

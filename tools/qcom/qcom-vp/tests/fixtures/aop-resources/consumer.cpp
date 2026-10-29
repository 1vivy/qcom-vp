// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "qcom/rpmh/aop_resource_service.h"
#include <cci/utils/broker.h>
#include <tlm_utils/simple_target_socket.h>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

using Bytes = std::vector<unsigned char>;
static void put(Bytes& bytes, unsigned offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = value >> (i * 8);
}
static uint32_t get(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static std::string digest(const Bytes& bytes) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(bytes.data(), bytes.size(), hash);
    std::ostringstream out;
    for (auto byte : hash) out << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
    return out.str();
}
static std::string span(uint32_t address, const Bytes& bytes) {
    std::ostringstream out;
    out << std::hex << address << ' ' << bytes.size() << ' ' << digest(bytes);
    return out.str();
}

struct Handoff : sc_core::sc_module, qcom_vp::ImageHandoffSource {
    explicit Handoff(sc_core::sc_module_name name) : sc_module(name) {}
    bool subscribe(uint32_t image, qcom_vp::ImageHandoffSink&) override { return image == 0x21; }
};

struct Consumer : sc_core::sc_module {
    tlm_utils::simple_target_socket<Consumer> memory{"memory"};
    Handoff handoff{"handoff"};
    qcom_vp::QcomAopResourceService service{"service"};
    std::map<uint32_t, Bytes> blocks;
    std::array<uint32_t, 0x4d4 + 0x200> ppids{};
    std::array<uint32_t, 256> vrm{};
    std::map<unsigned, std::array<unsigned char, 8>> revisions;
    std::array<uint32_t, 4096> commands{};
    unsigned failing_apid = 4096, writes = 0, observer_reads = 0;

    explicit Consumer(sc_core::sc_module_name name, unsigned test_case = 0) : sc_module(name) {
        memory.register_b_transport(this, &Consumer::transport);
        service.memory_socket.bind(memory);
        service.observer_path.set_value("handoff");
        service.startup_policy.set_value("synchronous-virtual");
        blocks[0x0c3f0000] = Bytes(0x400);
        blocks[0x81c60000] = Bytes(0x2000);
        Bytes config(0x4000), strings(0x40);
        std::memcpy(strings.data(), "L1D_E0\0L2D_E0\0L3D_E0\0SHADOW\0", 28);
        const std::array<uint32_t, 12> properties{
            0x408e957d, 0xe7b5f30e, 0x25ddbc21,
            0xc03be6b8, 0x0990fd58, 0xa283a84e,
            0x64dd6e23, 0xc3e60850, 0x018e477f,
            0xe61047ad, 0x2fbb5c4d, 0x84a8095b};
        for (unsigned i = 0; i < properties.size(); ++i) {
            put(config, 0x1cc + i * 8, properties[i]);
            put(config, 0x1d0 + i * 8, 0x81c80300 + i * 0x70);
        }
        put(config, 0x1cc + 12 * 8, 0xf00bdd05);
        put(config, 0x1d0 + 12 * 8, 0x81c83000);
        put(config, 0x1cc + 13 * 8, 0x308f356e);
        put(config, 0x1d0 + 13 * 8, 0x81c83100);
        put(config, 0x300 + 0x70 + 3 * 4, 3);
        put(config, 0x300 + 4 * 0x70 + 3 * 4, 0x81c81000);
        put(config, 0x300 + 7 * 0x70 + 3 * 4, 0x81c81200);
        put(config, 0x300 + 10 * 0x70 + 3 * 4, 0x81c81400);
        for (unsigned j = 0; j < 3; ++j) {
            put(config, 0x1000 + (j + 1) * 8, 0x81c84000 + j * 7);
            put(config, 0x1400 + j * 0x24, j == 1 ? 0 : 1);
        }
        // SID two belongs to SID one's identity. It is not another chip slot.
        put(config, 0x300 + 0x70 + 2 * 4, 1);
        put(config, 0x300 + 4 * 0x70 + 2 * 4, 0x81c81600);
        put(config, 0x300 + 7 * 0x70 + 2 * 4, 0x81c81700);
        put(config, 0x300 + 10 * 0x70 + 2 * 4, 0x81c81800);
        put(config, 0x1608, 0x81c84015);
        put(config, 0x1800, 1);
        blocks[0x81c80000] = config;
        blocks[0x81c84000] = strings;
        std::ostringstream tables;
        for (unsigned i = 0; i < 3; ++i) {
            Bytes records(0x18), auxiliary(8, 0xa0 + i);
            std::memcpy(records.data(), i == 0 ? "arc.one" : i == 1 ? "arc.two" : "bcm.one", 7);
            put(records, 0x10, i == 2 ? 0x50000 : 0x30000 + i * 0x10);
            const uint32_t address = 0x81000000 + i * 0x100;
            blocks[address] = records;
            blocks[address + 0x40] = auxiliary;
            tables << std::hex << (i == 2 ? 5 : 3) << " 1000 1 " << address << ' ' << digest(records)
                   << ' ' << address + 0x40 << " 8 " << digest(auxiliary) << ';';
        }
        tables << "4 100 4000 81c80000 " << digest(config) << " 81c84000 40 " << digest(strings);
        service.tables.set_value(tables.str());
        Bytes pointers(0xbc), masks(0x2e0), layouts(0xa2);
        for (unsigned i = 0; i < 47; ++i) put(pointers, i * 4, 0x82001000);
        masks[0xc1 / 8] |= 1u << (0xc1 % 8);
        masks[0xc3 / 8] |= 1u << (0xc3 % 8);
        layouts[1] = 2;
        layouts[0x3f] = 1;
        layouts[0x71] = 3;
        blocks[0x82000000] = pointers;
        blocks[0x82001000] = masks;
        blocks[0x82002000] = layouts;
        service.discovery.set_value(span(0x82000000, pointers) + ';' + span(0x82001000, masks) + ';' +
                                    span(0x82002000, layouts));
        ppids[0] = 0x001;
        ppids[1] = 0x101;
        ppids[2] = 0x201;
        ppids[3] = 0x301;
        ppids[4] = 0x3c1;
        ppids[5] = 0x3c3;
        revisions[0x001] = {0, 0, 0, 2, 0x51, 0x5c, 1, 0};
        revisions[0x101] = {0, 0, 0, 2, 0x51, 0x5d, 2, 0};
        revisions[0x201] = {0, 0, 0, 2, 0x51, 0x5d, 1, 0};
        revisions[0x301] = {0, 0, 0, 2, 0x51, 0x61, 2, 0};
        revisions[0x3c1] = {0, 3, 0, 0, 4, 0x30, 0, 0};
        revisions[0x3c3] = revisions[0x3c1];
        vrm[3] = (3u << 16) | (0xc1u << 8);
        vrm[12] = vrm[3];
        vrm[7] = (3u << 16) | (0xc3u << 8);
        if (test_case == 1) blocks[0x81c80000][0] ^= 1;
        if (test_case == 2) ppids[5] = 0;
        if (test_case == 3) failing_apid = 5;
        if (test_case == 4) blocks[0x82001000][0] ^= 1;
        if (test_case == 5 || test_case == 6) {
            ppids[6] = 0x1c1;
            revisions[0x1c1] = revisions[0x3c1];
            if (test_case == 5) failing_apid = 6;
        }
        if (test_case == 7) {
            ppids[6] = 0x3c4;
            revisions[0x3c4] = revisions[0x3c1];
            blocks[0x82001000][0xc4 / 8] |= 1u << (0xc4 % 8);
            service.discovery.set_value(span(0x82000000, pointers) + ';' +
                span(0x82001000, blocks[0x82001000]) + ';' + span(0x82002000, layouts));
        }
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        const auto address = tx.get_address();
        auto* bytes = tx.get_data_ptr();
        const unsigned size = tx.get_data_length();
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
        auto block = blocks.upper_bound(address);
        if (block != blocks.begin()) {
            --block;
            if (address >= block->first && address - block->first <= block->second.size() &&
                size <= block->second.size() - (address - block->first)) {
                auto* data = block->second.data() + address - block->first;
                if (tx.is_write()) { std::memcpy(data, bytes, size); ++writes; }
                else std::memcpy(bytes, data, size);
                return;
            }
        }
        if (size != 4) { tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE); return; }
        uint32_t value = 0;
        if (address >= 0x0c403000 && address < 0x0c405800 && tx.is_read()) {
            const unsigned apid = (address - 0x0c403000) / 4;
            if (apid < 0x4d4) value = ppids[apid];
            else if (apid >= 0x800) value = ppids[0x4d4 + apid - 0x800];
            else { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        } else if (address >= 0x0c402300 && address < 0x0c402700 && tx.is_read()) {
            value = vrm[(address - 0x0c402300) / 4];
        } else if (address >= 0x0c4c0000 && address < 0x0c500000) {
            const unsigned apid = (address - 0x0c4c0000) / 0x20;
            const unsigned reg = (address - 0x0c4c0000) % 0x20;
            if (tx.is_write() && !reg) {
                assert((get(bytes) >> 27) == 1);
                commands[apid] = get(bytes);
                return;
            }
            ++observer_reads;
            if (reg == 8) value = apid == failing_apid ? 3 : 1;
            else if (reg == 0x18 || reg == 0x1c) {
                const unsigned index = apid < 0x800 ? apid : 0x4d4 + apid - 0x800;
                const unsigned ppid = ppids[index] & 0x1fff;
                const unsigned key = ((apid / 0x800) << 16) | ppid;
                const unsigned offset = ((commands[apid] >> 4) & 255) + reg - 0x18;
                for (unsigned i = 0; i < 4; ++i) {
                    const auto found = revisions.find(key);
                    if (found != revisions.end() && offset + i < found->second.size())
                        value |= uint32_t(found->second[offset + i]) << (i * 8);
                }
            } else { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
        } else { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (i * 8);
    }

    bool publish() {
        auto delay = sc_core::SC_ZERO_TIME;
        return service.on_image_handoff({1, 1, 0x21}, delay);
    }
    void check() {
        const auto& header = blocks.at(0x0c3f0000);
        const auto& database = blocks.at(0x81c60000);
        assert(get(header.data() + 0xc) == 0x81c60000);
        assert(get(header.data() + 0x40) == 0xa0c00c1e);
        assert(get(database.data()) == 1 && get(database.data() + 4) == 0x0c0330db);
        for (unsigned i = 0; i < 3; ++i) {
            const auto* copied = database.data() + 0x90 + i * 0x20;
            assert(!std::memcmp(copied, blocks.at(0x81000000 + i * 0x100).data(), 0x18));
            assert(!std::memcmp(copied + 0x18, blocks.at(0x81000040 + i * 0x100).data(), 8));
        }
        assert(database[0x38] == 4 && database[0x3e] == 2);
        assert(database[0x40] == 0 && database[0x41] == 1);
        const auto* records = database.data() + 0xf0;
        assert(!std::memcmp(records, "L1D_E0", 6));
        assert(!std::memcmp(records + 0x18, "L3D_E0", 6));
        assert(get(records + 0x10) == 0x40000);
        assert(get(records + 0x28) == 0x40100);
        const auto* auxiliary = records + 0x30;
        assert(auxiliary[0] == 1 && auxiliary[1] == 12);
        assert(auxiliary[4] == 1 && auxiliary[5] == 7);
        assert(observer_reads);
    }
};

int sc_main(int, char**) {
    cci_utils::broker broker{"broker"};
    cci::cci_register_broker(broker);
    std::vector<std::unique_ptr<Consumer>> cases;
    for (unsigned i = 0; i < 8; ++i)
        cases.emplace_back(new Consumer(("case_" + std::to_string(i)).c_str(), i));
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    assert(cases[0]->publish());
    cases[0]->check();
    const auto writes = cases[0]->writes;
    assert(cases[0]->publish() && cases[0]->writes == writes);
    assert(cases[5]->publish());
    cases[5]->check();
    assert(cases[5]->commands[6] == 0x08000006);
    for (unsigned i = 1; i < cases.size(); ++i) {
        if (i == 5) continue;
        assert(!cases[i]->publish());
        const auto& header = cases[i]->blocks.at(0x0c3f0000);
        assert(!get(header.data() + 0xc) && !get(header.data() + 0x40));
        assert(!get(cases[i]->blocks.at(0x81c60000).data()));
    }
    std::cout << "AOP_DISCOVERED_VRM_PUBLICATION_PASS\n";
    return 0;
}

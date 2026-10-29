// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/tme_mailbox/image_handoff.h"
#include "resource_directory.h"
#include <cci_configuration>
#include <tlm_utils/simple_initiator_socket.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <algorithm>

namespace qcom_vp {

// Synchronous virtual replacement for the AOP resource startup task, not an AOP CPU.
// Only the shared observer can admit this service. No authentication result is changed.
class QcomAopResourceService : public sc_core::sc_module, public ImageHandoffSink,
                               public RpmhResourceDirectory {
public:
    tlm_utils::simple_initiator_socket<QcomAopResourceService> memory_socket{"memory_socket"};
    cci::cci_param<std::string> observer_path{"observer_path", "", "Authenticated image handoff source"};
    cci::cci_param<std::string> startup_policy{"startup_policy", "", "Explicit synchronous-virtual startup policy"};
    // Rows: slave version count records_address records_sha256 auxiliary_address auxiliary_size auxiliary_sha256.
    // Slave four uses count for the configuration size and auxiliary fields for its string source.
    cci::cci_param<std::string> tables{"tables", "", "Selected guest-loaded source spans, semicolon-separated rows"};
    cci::cci_param<std::string> discovery{"discovery", "", "Guest source rows: address size SHA-256 for masks and layouts"};

    explicit QcomAopResourceService(sc_core::sc_module_name name) : sc_module(name) {}

    // Only a completed publication defines VRM resources. Each record holds 0x40000 | id << 8.
    bool published_vrm(std::uint32_t address) const override {
        if (state_ != State::published || (address & 3) || (address & 0xff) > 0xc) return false;
        const auto base = address & ~std::uint32_t{0xff};
        for (unsigned i = 0; i < table_count_; ++i) {
            if (selected_[i].slave != 4) continue;
            for (unsigned record = 0; record < selected_[i].count; ++record)
                if (get32(vrm_records_.data() + record * 0x18 + 0x10) == base) return true;
        }
        return false;
    }
    bool on_image_handoff(const ImageHandoffEvent& event, sc_core::sc_time& delay) override {
        if (event.image_id != 0x21 || !event.sequence) return false;
        // One cold construction owns one admission. Neither duplicates nor a new generation
        // imply a warm reset, and failed publication is not retried with stale image bytes.
        if (state_ != State::dormant) return state_ == State::published &&
            event.generation == admission_.generation && event.sequence == admission_.sequence;
        admission_ = event;
        state_ = State::failed;
        if (!configured_) return false;
        try {
            database_.fill(0);
            unsigned cursor = 0;
            for (unsigned i = 0; i < table_count_; ++i) {
                auto& t = selected_[i];
                if (t.slave == 4 && !derive_vrm(t, i, delay)) {
                    std::cerr << "AOP_VRM_DERIVATION_FAILED stage=" << failure_ << '\n';
                    return false;
                }
                const unsigned records = t.count * 0x18u;
                const unsigned start = (cursor + 7u) & ~7u;
                const unsigned extent = (records + t.aux_size + 7u) & ~7u;
                if (start > capacity - data_offset || extent > capacity - data_offset - start)
                    return false;
                auto* record_data = database_.data() + data_offset + start;
                auto* aux_data = record_data + records;
                if (t.slave == 4) {
                    std::copy_n(vrm_records_.data(), records, record_data);
                    std::copy_n(vrm_aux_.data(), t.aux_size, aux_data);
                } else if (!read_checked(t.records, record_data, records, t.record_hash, i * 2, delay) ||
                    !read_checked(t.auxiliary, aux_data, t.aux_size, t.aux_hash, i * 2 + 1, delay)) return false;
                auto* directory = database_.data() + i * 0x10;
                put16(directory + 8, t.slave);
                put16(directory + 0xa, start);
                put16(directory + 0xc, start + records);
                put16(directory + 0xe, t.count);
                put16(directory + 0x10, t.version);
                cursor = start + extent;
            }
            put32(database_.data(), 1);
            put32(database_.data() + 4, 0x0c0330db);
            state_ = State::initialized;
            // All source validation precedes output writes. Pointer and cookie remain null
            // while one complete database write also clears all unused bytes.
            std::array<unsigned char, 0x400> header{};
            if (!word(header_base + 0x40, 0, delay) || !word(header_base + 0xc, 0, delay) ||
                !transfer(true, header_base, header.data(), header.size(), delay) ||
                !transfer(true, database_base, database_.data(), database_.size(), delay) ||
                !word(header_base, 0xf0040, delay) ||
                !word(header_base + 0x10, data_offset + cursor, delay) ||
                !word(header_base + 0xc, database_base, delay) ||
                !word(header_base + 0x40, 0xa0c00c1e, delay)) {
                invalidate(delay);
                state_ = State::failed;
                return false;
            }
            state_ = State::published;
            return true;
        } catch (const std::exception& error) {
            std::cerr << "AOP_RESOURCE_PUBLICATION_FAILED reason=" << error.what() << '\n';
            invalidate(delay);
            state_ = State::failed;
            return false;
        } catch (...) {
            invalidate(delay);
            state_ = State::failed;
            return false;
        }
    }

private:
    static constexpr unsigned capacity = 0x2000, data_offset = 0x90;
    static constexpr uint64_t header_base = 0x0c3f0000, database_base = 0x81c60000;
    enum class State { dormant, initialized, published, failed };
    State state_ = State::dormant;
    ImageHandoffEvent admission_{};
    struct Table {
        uint16_t slave = 0, version = 0, count = 0;
        uint64_t records = 0, auxiliary = 0;
        unsigned aux_size = 0, source_size = 0;
        std::array<unsigned char, SHA256_DIGEST_LENGTH> record_hash{}, aux_hash{};
    };
    std::array<Table, 8> selected_{};
    struct DiscoverySource {
        uint64_t address = 0;
        std::array<unsigned char, SHA256_DIGEST_LENGTH> hash{};
    };
    std::array<DiscoverySource, 3> discovery_sources_{};
    std::array<unsigned char, 0xbc> mask_pointers_{};
    std::array<unsigned char, 0x2e0> masks_{};
    std::array<unsigned char, 0xa2> layouts_{};
    struct Rail {
        unsigned char sid = 0, peripheral = 0;
    };
    std::array<std::array<std::array<Rail, 30>, 3>, 28> discovered_rails_{};
    std::array<bool, 28> discovered_chips_{};
    std::array<uint16_t, 2 * 16 * 256> ppid_apids_{};
    struct Digest {
        EVP_MD_CTX* context = nullptr;
        ~Digest() { EVP_MD_CTX_free(context); }
    };
    // Allocate and initialize all digest contexts at elaboration, never per admission.
    std::array<Digest, 16> digests_{};
    std::array<unsigned char, capacity> database_{};
    std::array<unsigned char, 0x4600> config_{};
    std::array<unsigned char, 130 * 0x18> vrm_records_{};
    std::array<unsigned char, 130 * 4> vrm_aux_{};
    std::array<uint32_t, 0x4d4 + 0x200> spmi_ppids_{};
    std::array<uint32_t, 0x100> spmi_vrm_{};
    unsigned config_data_size_ = 0, config_string_size_ = 0;
    unsigned table_count_ = 0;
    bool configured_ = false;
    const char* failure_ = "none";
    bool fail(const char* stage) {
        failure_ = stage;
        return false;
    }

    static void put16(unsigned char* p, unsigned v) { p[0] = v; p[1] = v >> 8; }
    static void put32(unsigned char* p, uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
    }
    static bool hash(const std::string& text, std::array<unsigned char, SHA256_DIGEST_LENGTH>& out) {
        if (text.size() != out.size() * 2) return false;
        const auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        for (unsigned i = 0; i < out.size(); ++i) {
            const int hi = digit(text[i * 2]), lo = digit(text[i * 2 + 1]);
            if (hi < 0 || lo < 0) return false;
            out[i] = static_cast<unsigned char>((hi << 4) | lo);
        }
        return true;
    }
    static uint32_t get32(const unsigned char* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
            (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }
    const unsigned char* source(uint32_t address, unsigned size) const {
        const uint32_t offset = address - 0x81c80000;
        if (address < 0x81c80000 ||
            !((offset <= config_data_size_ && size <= config_data_size_ - offset) ||
              (offset >= 0x4000 && offset - 0x4000 <= config_string_size_ &&
               size <= config_string_size_ - (offset - 0x4000))))
            throw std::out_of_range("AOP configuration pointer exceeds its source");
        return config_.data() + address - 0x81c80000;
    }
    uint32_t source_word(uint32_t address) const { return get32(source(address, 4)); }
    uint32_t property(uint32_t crc) const {
        for (unsigned i = 0; i < 20; ++i) {
            const auto row = 0x81c801cc + i * 8;
            if (source_word(row) == crc) return source_word(row + 4);
        }
        throw std::invalid_argument("AOP configuration property is absent");
    }
    uint64_t resource_name(uint32_t address) const {
        uint64_t name = 0;
        if (!address) return name;
        for (unsigned i = 0; i < 9; ++i) {
            const auto byte = *source(address + i, 1);
            if (!byte) return name;
            if (i == 8) throw std::invalid_argument("AOP resource name exceeds eight bytes");
            name |= uint64_t(byte) << (i * 8);
        }
        return name;
    }
    bool guest_word(uint32_t address, uint32_t& value, sc_core::sc_time& delay) {
        std::array<unsigned char, 4> bytes{};
        if (!transfer(false, address, bytes.data(), bytes.size(), delay)) return false;
        value = get32(bytes.data());
        return true;
    }
    unsigned peripheral_apid(unsigned bus, unsigned sid, unsigned peripheral) const {
        if (bus > 1 || sid > 15 || !peripheral || peripheral > 255) return 0xffff;
        return ppid_apids_[(bus * 16 + sid) * 256 + peripheral];
    }
    bool read_pmic(unsigned bus, unsigned sid, unsigned peripheral, unsigned offset,
                   unsigned count, std::array<unsigned char, 8>& bytes, sc_core::sc_time& delay) {
        const unsigned apid = peripheral_apid(bus, sid, peripheral);
        if (apid == 0xffff || !count || count > bytes.size() || offset + count > 256) return false;
        const uint32_t observer = 0x0c4c0000 + apid * 0x20;
        uint32_t status, low, high = 0;
        if (!word(observer, 0x08000000 | (offset << 4) | (count - 1), delay) ||
            !guest_word(observer + 8, status, delay) || status != 1 ||
            !guest_word(observer + 0x18, low, delay) ||
            (count > 4 && !guest_word(observer + 0x1c, high, delay))) return false;
        put32(bytes.data(), low);
        put32(bytes.data() + 4, high);
        return true;
    }
    bool discover_rails(const std::array<uint32_t, 3>& counts, sc_core::sc_time& delay) {
        const std::array<unsigned, 3> layout_offsets{0x70, 0x3e, 0};
        std::array<unsigned, 3> strides{};
        for (unsigned kind = 0; kind < strides.size(); ++kind) {
            const auto* layout = layouts_.data() + layout_offsets[kind];
            const unsigned stride = unsigned(layout[0]) | (unsigned(layout[1]) << 8);
            if (!stride || (stride & 255)) return fail("rail-layout-stride");
            strides[kind] = stride >> 8;
        }
        for (unsigned bus = 0; bus < 2; ++bus) {
            for (unsigned sid = 0, slot = 0; sid < 14 && slot < 14;) {
                std::array<unsigned char, 8> identity{};
                const bool present = read_pmic(bus, sid, 1, 2, 5, identity, delay) &&
                    identity[2] == 0x51 && identity[3];
                const unsigned span = present ? (identity[4] ? identity[4] : 2) : 1;
                if (span > 14 - sid || span > 14 - slot) return fail("pmic-slave-count");
                const unsigned selected = bus * 14 + slot;
                discovered_chips_[selected] = present;
                if (present) {
                    const unsigned model = identity[3];
                    const uint64_t pointer = model >= 0x35 && model <= 0x63 ?
                        get32(mask_pointers_.data() + (model - 0x35) * 4) :
                        discovery_sources_[1].address + masks_.size() - 32;
                    const uint64_t start = pointer - discovery_sources_[1].address;
                    if (pointer < discovery_sources_[1].address || start > masks_.size() ||
                        span * 32 > masks_.size() - start) return fail("pmic-discovery-mask");
                    std::array<unsigned, 3> first{}, first_sid{};
                    for (unsigned bank = 0; bank < span; ++bank) {
                        const auto* mask = masks_.data() + start + bank * 32;
                        for (unsigned peripheral = 1; peripheral < 256; ++peripheral) {
                            if (!(mask[peripheral / 8] & (1u << (peripheral % 8))) ||
                                peripheral_apid(bus, sid + bank, peripheral) == 0xffff) continue;
                            std::array<unsigned char, 8> revision{};
                            if (!read_pmic(bus, sid + bank, peripheral, 0, 7, revision, delay)) {
                                std::cerr << "AOP_PMIC_REVISION_READ_FAILED bus=" << bus << " sid="
                                          << (sid + bank) << " peripheral=0x" << std::hex << peripheral
                                          << std::dec << '\n';
                                // Native discovery ignores read status after clearing its revision buffer.
                                // SM8845 0x81c1ff48..0x81c1ff84 / SM8850 0x81c20384..0x81c203c0.
                                continue;
                            }
                            if (!revision[4] || revision[5] == revision[6]) continue;
                            unsigned kind;
                            if (revision[4] == 3 || revision[4] == 0x1c || revision[4] == 0x22) kind = 0;
                            else if (revision[4] == 4 || revision[4] == 0x21) kind = 1;
                            else if (revision[4] == 0x29) kind = 2;
                            else continue;
                            if (!first[kind]) { first[kind] = peripheral; first_sid[kind] = sid + bank; }
                            if (peripheral < first[kind]) return fail("rail-discovery-order");
                            const unsigned index = (peripheral - first[kind]) / strides[kind];
                            const unsigned count = source_word(counts[kind] + selected * 4);
                            if (index >= count || index >= discovered_rails_[selected][kind].size())
                                return fail("rail-discovery-count");
                            discovered_rails_[selected][kind][index] = Rail{
                                static_cast<unsigned char>(first_sid[kind]), static_cast<unsigned char>(peripheral)};
                        }
                    }
                }
                sid += span;
                slot += span;
            }
        }
        return discovered_chips_[0] || fail("pmic-zero-absent");
    }
    bool peripheral_channel(unsigned bus, unsigned sid, unsigned peripheral, unsigned& channel) const {
        if (peripheral_apid(bus, sid, peripheral) == 0xffff) return false;
        const unsigned ppid = (sid << 8) | peripheral;
        channel = 0x100;
        for (unsigned i = 0; i < spmi_vrm_.size(); ++i) {
            const auto mapping = spmi_vrm_[i];
            if (((mapping >> 21) & 3) == bus && ((mapping >> 8) & 0x1fff) == ppid)
                channel = i;
        }
        return true;
    }
    bool derive_vrm(Table& t, unsigned digest, sc_core::sc_time& delay) {
        if (!read_checked(t.records, config_.data(), t.source_size, t.record_hash, digest * 2, delay))
            return fail("configuration-data");
        if (!read_checked(t.auxiliary, config_.data() + 0x4000, t.aux_size, t.aux_hash, digest * 2 + 1, delay))
            return fail("configuration-strings");
        config_data_size_ = t.source_size;
        config_string_size_ = t.aux_size;
        if (!read_checked(discovery_sources_[0].address, mask_pointers_.data(), mask_pointers_.size(),
                          discovery_sources_[0].hash, 8, delay)) return fail("discovery-mask-pointers");
        if (!read_checked(discovery_sources_[1].address, masks_.data(), masks_.size(),
                          discovery_sources_[1].hash, 9, delay)) return fail("discovery-masks");
        if (!read_checked(discovery_sources_[2].address, layouts_.data(), layouts_.size(),
                          discovery_sources_[2].hash, 10, delay)) return fail("discovery-layouts");
        ppid_apids_.fill(0xffff);
        for (unsigned i = 0; i < spmi_ppids_.size(); ++i) {
            const unsigned apid = i < 0x4d4 ? i : 0x800 + i - 0x4d4;
            if (!guest_word(0x0c403000 + apid * 4, spmi_ppids_[i], delay)) return fail("spmi-ppid-table");
            const auto ppid = spmi_ppids_[i] & 0x1fff;
            if (!(ppid & 0x1000) && (ppid & 255)) {
                const unsigned index = ((apid / 0x800) * 16 * 256) + ppid;
                if (ppid_apids_[index] == 0xffff) ppid_apids_[index] = apid;
            }
        }
        for (unsigned i = 0; i < spmi_vrm_.size(); ++i)
            if (!guest_word(0x0c402300 + i * 4, spmi_vrm_[i], delay)) return fail("spmi-vrm-map");
        struct Group {
            uint64_t name = 0;
            unsigned id = 0, size = 0, start = 0, used = 0;
        };
        std::array<Group, 8> groups{};
        unsigned group_count = 0, next = 0, next_enable = 0x60, records = 0;
        const std::array<uint32_t, 3> counts{property(0x408e957d), property(0xe7b5f30e), property(0x25ddbc21)};
        const std::array<uint32_t, 3> names{property(0xc03be6b8), property(0x0990fd58), property(0xa283a84e)};
        const std::array<uint32_t, 3> deps{property(0x64dd6e23), property(0xc3e60850), property(0x018e477f)};
        const std::array<uint32_t, 3> rails{property(0xe61047ad), property(0x2fbb5c4d), property(0x84a8095b)};
        const auto vector_entry = [&](uint32_t vector, unsigned slot) {
            return source_word(vector + slot * 4);
        };
        if (!discover_rails(counts, delay)) return false;
        for (unsigned slot = 0; slot < 28; ++slot) {
            for (unsigned kind = 0; kind < 3; ++kind) {
                if (!discovered_chips_[slot] || !discovered_rails_[slot][kind][0].peripheral) continue;
                const unsigned count = vector_entry(counts[kind], slot);
                const uint32_t rs = vector_entry(names[kind], slot), ds = vector_entry(deps[kind], slot);
                if (count > 30) return fail("rail-count");
                if (!rs || !ds) continue;
                for (unsigned j = 1; j <= count; ++j) {
                    if (!source_word(ds + j * 16 + 4)) continue;
                    if (group_count == groups.size()) return fail("parent-capacity");
                    auto& group = groups[group_count++];
                    group.name = resource_name(source_word(rs + j * 8));
                    group.id = next++;
                    group.size = source_word(ds + j * 16 + 8);
                    if (!group.name || !group.size || group.size > 30 ||
                        source_word(ds + j * 16 + 0xc)) return fail("parent-record");
                }
            }
        }
        const auto allocate = [&](uint64_t name, uint64_t parent, unsigned type) -> unsigned {
            if (type == 1) return next_enable++;
            if (type) throw std::invalid_argument("Unsupported selected AOP resource type");
            for (unsigned i = 0; i < group_count; ++i) {
                auto& group = groups[i];
                if (group.name == name) return group.id;
                if (parent && group.name == parent) {
                    if (group.used == group.size) throw std::out_of_range("AOP parent resource is full");
                    if (!group.used) { group.start = next; next += group.size; }
                    return group.start + group.used++;
                }
            }
            if (parent) throw std::invalid_argument("AOP parent resource is absent");
            return next++;
        };
        const auto append = [&](uint64_t name, unsigned id, unsigned type, unsigned channel) {
            if (!name || records == vrm_aux_.size() / 4 || id > 0x7f) return false;
            auto* row = vrm_records_.data() + records * 0x18;
            for (unsigned i = 0; i < 8; ++i) row[i] = name >> (i * 8);
            put32(row + 0x10, 0x40000 | (id << 8));
            put16(row + 0x14, 4);
            put16(row + 0x16, records * 4);
            vrm_aux_[records * 4] = type == 1 ? 2 : 1;
            vrm_aux_[records * 4 + 1] = channel;
            ++records;
            return true;
        };
        for (unsigned slot = 0; slot < 28; ++slot) {
            for (unsigned kind = 0; kind < 3; ++kind) {
                if (!discovered_chips_[slot] || !discovered_rails_[slot][kind][0].peripheral) continue;
                const unsigned count = vector_entry(counts[kind], slot);
                const uint32_t rs = vector_entry(names[kind], slot), ds = vector_entry(deps[kind], slot);
                const uint32_t config = vector_entry(rails[kind], slot);
                if (!rs || !ds || !config) continue;
                for (unsigned j = 0; j < count; ++j) {
                    const uint32_t flags = source_word(config + j * 0x24);
                    if (!(flags & 1)) continue;
                    const uint64_t name = resource_name(source_word(rs + (j + 1) * 8));
                    const uint64_t alias = resource_name(source_word(rs + (j + 1) * 8 + 4));
                    const uint32_t parent_pair = source_word(ds + (j + 1) * 16);
                    const uint64_t parent = parent_pair ? resource_name(source_word(parent_pair)) : 0;
                    const unsigned type = (flags >> 1) & 15, id = allocate(name, parent, type);
                    unsigned channel;
                    const auto hardware = discovered_rails_[slot][kind][j];
                    if (!hardware.peripheral) return fail("rail-discovery-hole");
                    const unsigned sid = hardware.sid, peripheral = hardware.peripheral;
                    const unsigned controller_sid = ((slot / 14) << 4) | sid;
                    if (!peripheral_channel(controller_sid >> 4, controller_sid & 15, peripheral, channel)) {
                        std::cerr << "AOP_VRM_PERIPHERAL_ABSENT bus=" << (controller_sid >> 4)
                                  << " sid=" << (controller_sid & 15) << " peripheral=0x" << std::hex
                                  << peripheral << std::dec << " slot=" << slot << " kind=" << kind
                                  << " index=" << j << '\n';
                        return fail("rail-peripheral");
                    }
                    if (kind == 0 && !peripheral_channel(controller_sid >> 4, controller_sid & 15,
                            peripheral + 1, channel)) return fail("rail-secondary-peripheral");
                    if (!append(name, id, type, channel) ||
                        (alias && !append(alias, id, type, channel))) return fail("rail-record");
                }
            }
        }
        const uint32_t targets = property(0xf00bdd05);
        const unsigned target_count = source_word(property(0x308f356e));
        if (target_count > 30) return fail("target-count");
        for (unsigned j = 0; j < target_count; ++j) {
            const uint32_t address = targets + j * 0x28, flags = source_word(address + 0x14);
            if (!(flags & 1)) continue;
            const uint64_t name = resource_name(source_word(address));
            const unsigned type = ((flags >> 21) & 7) == 1 ? 1 : 0, id = allocate(name, 0, type);
            const unsigned pmic = source_word(address + 4), sid = (flags >> 1) & 15;
            const unsigned peripheral = (flags >> 5) & 0xffff;
            unsigned channel;
            if (!peripheral_channel(pmic, sid, peripheral >> 8, channel)) return fail("target-peripheral");
            if (!append(name, id, type, channel)) return fail("target-record");
        }
        t.count = records;
        t.aux_size = records * 4;
        return true;
    }
    bool configure() {
        if (startup_policy.get_value() != "synchronous-virtual") return false;
        std::istringstream rows(tables.get_value());
        std::string row;
        while (std::getline(rows, row, ';')) {
            if (table_count_ == selected_.size()) return false;
            std::istringstream fields(row);
            uint64_t slave, version, count, records, auxiliary, aux_size;
            std::string records_hash, aux_hash, extra;
            if (!(fields >> std::hex >> slave >> version >> count >> records >> records_hash >>
                  auxiliary >> aux_size >> aux_hash) || (fields >> extra)) return false;
            if (!slave || slave > UINT16_MAX || version > UINT16_MAX || !count ||
                (slave == 4 ? count > 0x4000 : count > (capacity - data_offset) / 0x18) ||
                !aux_size || aux_size > (slave == 4 ? 0x600 : capacity) ||
                !records || !auxiliary || records > UINT64_MAX - count * 0x18 ||
                auxiliary > UINT64_MAX - aux_size) return false;
            auto& t = selected_[table_count_++];
            t.slave = slave; t.version = version;
            if (slave == 4) {
                if (records != 0x81c80000 || auxiliary != 0x81c84000 || version != 0x100) return false;
                t.source_size = count;
            } else t.count = count;
            t.records = records; t.auxiliary = auxiliary; t.aux_size = aux_size;
            if (!hash(records_hash, t.record_hash) || !hash(aux_hash, t.aux_hash)) return false;
        }
        if (table_count_ != 4 || selected_[0].slave != 3 || selected_[1].slave != 3 ||
            selected_[2].slave != 5 || selected_[3].slave != 4) return false;
        std::istringstream inputs(discovery.get_value());
        const std::array<unsigned, 3> sizes{mask_pointers_.size(), masks_.size(), layouts_.size()};
        for (unsigned i = 0; i < discovery_sources_.size(); ++i) {
            if (!std::getline(inputs, row, ';')) return false;
            std::istringstream fields(row);
            uint64_t address, size;
            std::string digest, extra;
            if (!(fields >> std::hex >> address >> size >> digest) || (fields >> extra) ||
                !address || size != sizes[i] || address > UINT64_MAX - size ||
                !hash(digest, discovery_sources_[i].hash)) return false;
            discovery_sources_[i].address = address;
        }
        return !std::getline(inputs, row, ';');
    }
    void end_of_elaboration() override {
        configured_ = configure();
        if (!configured_)
            throw std::invalid_argument("AOP resource service requires valid selected tables and synchronous-virtual policy");
        if (configured_) {
            for (unsigned i = 0; i < table_count_ * 2 + discovery_sources_.size(); ++i) {
                auto& digest = digests_[i];
                digest.context = EVP_MD_CTX_new();
                if (!digest.context || EVP_DigestInit_ex(digest.context, EVP_sha256(), nullptr) != 1)
                    throw std::runtime_error("AOP resource service could not initialize SHA-256 contexts");
            }
        }
        auto* source = dynamic_cast<ImageHandoffSource*>(sc_core::sc_find_object(observer_path.get_value().c_str()));
        if (!source && get_parent_object()) {
            const auto path = std::string(get_parent_object()->name()) + "." + observer_path.get_value();
            source = dynamic_cast<ImageHandoffSource*>(sc_core::sc_find_object(path.c_str()));
        }
        if (!source || !source->subscribe(0x21, *this))
            throw std::invalid_argument("AOP resource service requires its authenticated handoff observer");
    }
    bool transfer(bool write, uint64_t address, unsigned char* data, unsigned size, sc_core::sc_time& delay) {
        tlm::tlm_generic_payload tx;
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_address(address); tx.set_data_ptr(data); tx.set_data_length(size);
        tx.set_streaming_width(size); tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        memory_socket->b_transport(tx, delay);
        return tx.is_response_ok();
    }
    bool read_checked(uint64_t address, unsigned char* data, unsigned size,
                      const std::array<unsigned char, SHA256_DIGEST_LENGTH>& expected,
                      unsigned index, sc_core::sc_time& delay) {
        std::array<unsigned char, SHA256_DIGEST_LENGTH> actual{};
        unsigned length = 0;
        return transfer(false, address, data, size, delay) &&
            EVP_DigestUpdate(digests_[index].context, data, size) == 1 &&
            EVP_DigestFinal_ex(digests_[index].context, actual.data(), &length) == 1 &&
            length == actual.size() && actual == expected;
    }
    bool word(uint64_t address, uint32_t value, sc_core::sc_time& delay) {
        std::array<unsigned char, 4> bytes{};
        put32(bytes.data(), value);
        return transfer(true, address, bytes.data(), bytes.size(), delay);
    }
    void invalidate(sc_core::sc_time& delay) noexcept {
        // A broken memory backend cannot guarantee rollback. Never report publication success.
        try { word(header_base + 0x40, 0, delay); } catch (...) {}
        try { word(header_base + 0xc, 0, delay); } catch (...) {}
        try { word(header_base + 0x10, 0, delay); } catch (...) {}
    }
};

} // namespace qcom_vp

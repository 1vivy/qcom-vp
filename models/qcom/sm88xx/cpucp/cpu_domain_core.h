// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"

namespace qcom_vp {

// Relative-address functional core sequencer. The enclosing domain owns CPU reset delivery.
struct CpuDomainCoreState {
    enum class Phase : std::uint64_t { off = 0, supply = 1, reset_hold = 2,
        configuring = 3, initializing = 4, online = 5 };
    std::array<std::uint32_t, 3> routes{};
    std::array<std::uint64_t, 8> route_policy{};
    std::array<std::uint64_t, 2> route_status{};
    std::array<std::uint64_t, 45> words{};
    std::array<std::uint64_t, 8> window_config{};
    std::array<std::uint64_t, 3> boot_memory_access_config{};
    std::array<std::array<std::uint64_t, 17>, 32> dvfs_state_config{};
    std::uint64_t dvfs_control = 0x1f0000;
    std::uint64_t effective_frequency_hz = 0;
    unsigned dvfs_request = 0;
    unsigned dvfs_target = 31;
    std::array<std::uint64_t, 6> dvfs_core_votes{};
    unsigned effective_power_state = 0;
    unsigned effective_memory_state = 0;
    std::array<std::array<std::uint64_t, 3>, 8> memory_access_regions{};
    // +0xc20 is NCC_ACD_MISC_CFG. PDP 0x8130b866 sets ACDEN after the ACD calibration.
    // +0xc78 is NCC_DVMR_REGION_CFG. PDP 0x81321704 programs its region fields.
    static constexpr std::array<std::uint64_t, 11> dvfs_policy_offsets{
        0x10, 0x18, 0x20, 0x28, 0x30, 0x718, 0xc18, 0xc20, 0xc78, 0xc98, 0xf18};
    std::array<std::uint64_t, dvfs_policy_offsets.size()> dvfs_policy{};
    std::array<std::uint64_t, 8> core_policy{};
    // Selected TZ writes a 64-bit payload to +0x8 and a 64-bit command word to +0x0.
    // It never reads these words. The block identity stays open.
    std::array<std::array<std::uint64_t, 2>, 8> release_words{};
    std::array<std::array<std::uint64_t, 16>, 8> core_runtime_policy{};
    // CPUCP 0x1d80cea0 and PDP 0x2000b2c2 interpret bit 32 as transaction busy.
    // No transaction exists in the cold state.
    std::uint64_t dvfs_status = 0;
    std::array<std::array<std::uint64_t, 12>, 8> core_runtime_config{};
    // Runtime cluster configuration has no completion or reset side effect.
    // Zero QMX disable bit 54 is virtual board policy, not a silicon POR claim.
    // Index 10 retains PDP-written word +0x268. Index 0 is read-only QMX policy.
    std::array<std::uint64_t, 11> cluster_config{};
    // PDP 0x20000730 reads its cluster index from +0x5d0 bits 15:8.
    unsigned cluster_index = 0;
    std::uint32_t pdp_reset_vector = 0x20000000;
    std::array<std::uint64_t, 64> cluster_table{};
    std::array<std::uint64_t, 28> runtime_config{};
    std::uint64_t runtime_aux_control = 0;
    std::uint64_t runtime_aux_status = 0;
    std::array<std::uint64_t, 3> cluster_secondary{};
    std::array<std::uint64_t, 4> cluster_aux_table{};
    std::array<std::uint64_t, 4> cluster_aux_config{};
    std::array<std::array<std::uint64_t, 2>, 24> auxiliary_state_config{};
    std::uint64_t cluster_aux_policy = 0;
    // PDP 0x20004ba0 and 0x20004c34 program the QMX DPM weights.
    // No firmware path reads them back, so they retain configuration only.
    // Index 0 is +0xb50, 1..10 are +0x480..+0x4c8, 11..16 are +0x5b8..+0x5e0.
    std::array<std::uint64_t, 17> qmx_dpm_config{};
    std::uint64_t bridge = 0;
    std::uint64_t ncc_lock_timeout = 0;
    std::uint64_t dvfs_config = 0;
    std::uint64_t memory_access_config = 0;
    std::uint64_t fabric = 0;
    std::array<std::uint64_t, 12> fabric_policy{};
    std::uint32_t range_writes = 0;
    std::uint32_t boot_memory_access_writes = 0;
    std::uint32_t route_writes = 0;
    unsigned core_mask = 0xff;
    bool bridge_written = false;
    bool managed = false;
    bool supply_good = false;
    bool cluster_initialized = false;
    bool boot_memory_access_done = false;
    bool commit = false;
    unsigned powered_cores = 0;
    Phase phase = Phase::off;
    inline static constexpr std::array<std::uint64_t, 45> offsets{
        0x10, 0x258, 0x260, 0x278, 0x290, 0x310, 0x328,
        0x2c0, 0x3f8, 0x500, 0x588, 0x590, 0x598, 0x608,
        0x348, 0x350, 0x358, 0x360, 0x368, 0x370, 0x378, 0x380, 0x388,
        0x390, 0x398, 0x3a0, 0x3a8, 0x3b0, 0x3b8, 0x3c0, 0x3c8, 0x3d0,
        0x270, 0x2b0, 0x288, 0x3f0, 0x4e8, 0x4f8, 0x338, 0x410, 0x4f0,
        0x420, 0x570, 0x578, 0x600};

    CpuDomainCoreState() {
        // Virtual cold gates. The ROM clears each inhibit and asserts all eight reset bits.
        words[2] = 0xff86e0;
    }
    void refresh(bool domain_ready, unsigned lane) {
        const auto control = words[2];
        // NCC_BOOT_CFG1 bits 10 and 9 (OK2ENTERCL5, OK2ENTERCL5LITE) only permit cluster
        // collapse. TZ 0x1573eb64 sets them and 0x1573ec64 clears them. They do not remove supply.
        supply_good = domain_ready && (control & 1) && !(control & 0x80e0) && (words[1] & 2);
        // Virtual SRAM applies the boot read-assist policy synchronously.
        // The power gate, complete policy, and BOOTMEMACCEN qualify the push.
        boot_memory_access_done = supply_good && (control & 0x100) &&
            boot_memory_access_writes == 7 && (memory_access_config & 0x400);
        if (!supply_good) {
            cluster_initialized = false;
            powered_cores = 0;
            phase = Phase::off;
            return;
        }
        phase = Phase::supply;
        if (!(control & 0x100)) { cluster_initialized = false; powered_cores = 0; return; }
        phase = Phase::reset_hold;
        // The ROM disables DVFS while it applies the boot memory-access policy.
        // The PDP later enables DVFS without repeating cluster initialization.
        const bool sequence_ready = cluster_initialized ||
            ((dvfs_config & 0x101) == 0x101 && (memory_access_config & 1) &&
             boot_memory_access_done);
        // Software clock overrides qualify cold initialization only.
        // The PDP later clears them when hardware takes clock ownership.
        const bool bridge_ready = cluster_initialized || !bridge_written ||
            (bridge & 0x2000003) == 0x2000003;
        const bool configured = range_writes == 0x3ffff && route_writes == 7 &&
            sequence_ready && bridge_ready && (words[0] & 0xfff) == lane &&
            (words[1] & 0x10);
        if (!configured) { cluster_initialized = false; powered_cores = 0; return; }
        phase = Phase::configuring;
        if (words[3] & 0x100) {
            phase = Phase::initializing;
            // Functional phase completion follows the modeled supply/configuration effects.
            // Encoding 5 is the first completion phase accepted by the ROM, not an analog measurement.
            cluster_initialized = true;
        }
        if (!cluster_initialized) { powered_cores = 0; return; }
        phase = Phase::online;
        powered_cores = static_cast<unsigned>(words[3]) & core_mask;
    }
    bool core_reset(unsigned core) const {
        return !cluster_initialized || !(powered_cores & (1u << core)) ||
            (words[2] & (UINT64_C(1) << (16 + core)));
    }
    static bool valid64(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        else if (!tx.get_data_ptr() || tx.get_data_length() != 8 || tx.get_streaming_width() < 8 ||
                 (tx.get_address() & 7)) tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        else if (!tx.is_read() && !tx.is_write()) tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        else return true;
        return false;
    }
    static std::uint64_t load64(const tlm::tlm_generic_payload& tx) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{tx.get_data_ptr()[i]} << (i * 8);
        return value;
    }
    static void store64(tlm::tlm_generic_payload& tx, std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (i * 8));
    }
    void power_transport(tlm::tlm_generic_payload& tx, bool domain_ready, unsigned lane,
                         std::uint64_t boot_status) {
        const auto offset = tx.get_address();
        if (offset == 0x2c8 || offset == 0x2cc) {
            const bool wide = tx.get_data_length() == 8;
            if (wide ? !valid64(tx) : !clock_detail::valid(tx)) return;
            if (tx.is_write()) {
                const auto value = wide ? load64(tx) : std::uint64_t{clock_detail::load(tx)};
                if ((value >> 32) || (offset == 0x2cc && value)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                if (offset == 0x2c8)
                    pdp_reset_vector = static_cast<std::uint32_t>(value) & ~UINT32_C(0x7f);
            } else if (wide) {
                store64(tx, pdp_reset_vector);
            } else {
                clock_detail::store(tx, offset == 0x2cc ? 0 : pdp_reset_vector);
            }
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        // PDP 0x20000978 and 0x20001cf8 update +0x420 with 32-bit accesses.
        // A half merges into the doubleword, then follows the doubleword checks.
        const bool wide = tx.get_data_length() == 8;
        if (wide ? !valid64(tx) : !clock_detail::valid(tx)) return;
        refresh(domain_ready, lane);
        const auto word_offset = offset & ~UINT64_C(7);
        const unsigned shift = static_cast<unsigned>(offset & 4) * 8;
        unsigned index = 0;
        while (index < offsets.size() && offsets[index] != word_offset) ++index;
        // Only the ROM-observed control/configuration words form this interface.
        if (index >= offsets.size()) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        const auto reply = [&](std::uint64_t value) {
            if (wide) store64(tx, value);
            else clock_detail::store(tx, static_cast<std::uint32_t>(value >> shift));
        };
        if (index == 32 || index == 33) {
            if (!tx.is_read()) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
            // NCC_BOOT_STS1 comes from the domain, which observes the attached CPUs.
            // NCC_PWR_STS1: CAGPWRSTATE [60:56], CORSPRINITDONE [15:8], CORDOMISON [7:0].
            // A functionally powered core reports both its domain and its initialization.
            reply(index == 32 ? boot_status :
                (static_cast<std::uint64_t>(phase) << 56) | (std::uint64_t{powered_cores} << 8) |
                powered_cores);
        } else if (tx.is_read()) reply(words[index]);
        else {
            const auto mask = UINT64_C(0xffffffff) << shift;
            const auto value = wide ? load64(tx) :
                (words[index] & ~mask) | (std::uint64_t{clock_detail::load(tx)} << shift);
            if ((index == 0 && (value & ~UINT64_C(0xfff))) ||
                (index == 1 && (value & ~UINT64_C(0x72))) ||
                (index == 2 && (value & ~UINT64_C(0x10ff87e1))) ||
                (index == 3 && (value & ~std::uint64_t{0x100u | core_mask}))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
            }
            words[index] = value;
            if (index >= 14 && index <= 31) range_writes |= 1u << (index - 14);
            // Only the control word transfers ownership of core reset and supply.
            // APSS writes scratch/configuration words before its own reset request.
            if (index == 2) managed = true;
            refresh(domain_ready, lane);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void window_transport(tlm::tlm_generic_payload& tx, unsigned count) {
        if (!valid64(tx)) return;
        const auto offset = tx.get_address();
        if (offset < 8 || (offset - 8) / 8 >= count || count > window_config.size()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& word = window_config[(offset - 8) / 8];
        if (tx.is_write()) word = load64(tx);
        else store64(tx, word);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void route_transport(tlm::tlm_generic_payload& tx) {
        const auto offset = tx.get_address();
        constexpr std::array<std::uint64_t, 8> policy_offsets{
            0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80};
        const auto policy = std::find(policy_offsets.begin(), policy_offsets.end(), offset);
        if (policy != policy_offsets.end()) {
            if (!valid64(tx)) return;
            auto& value = route_policy[policy - policy_offsets.begin()];
            if (tx.is_write()) value = load64(tx);
            else store64(tx, value);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset == 0x88 || offset == 0x90) {
            if (!valid64(tx)) return;
            if (tx.is_write()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            store64(tx, route_status[(offset - 0x88) / 8]);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (tx.get_data_length() == 4 ? !clock_detail::valid(tx) : !valid64(tx)) return;
        unsigned index;
        if (offset == 0x28) index = 0;
        else if (offset == 0x38) index = 1;
        else if (offset == 0x40) index = 2;
        else { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (tx.is_write()) {
            const auto value = tx.get_data_length() == 8 ? load64(tx) : clock_detail::load(tx);
            if (value & ~UINT64_C(0xfffff)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
            }
            routes[index] = static_cast<std::uint32_t>(value);
            route_writes |= 1u << index;
        } else if (tx.get_data_length() == 8) store64(tx, routes[index]);
        else clock_detail::store(tx, routes[index]);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void bridge_transport(tlm::tlm_generic_payload& tx) {
        if (!valid64(tx)) return;
        const auto offset = tx.get_address();
        if (offset != 0x10 && offset != 0x30) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& word = offset == 0x10 ? ncc_lock_timeout : bridge;
        if (tx.is_write()) {
            const auto value = load64(tx);
            const auto mask = offset == 0x10 ? UINT64_C(0xfff0000ffff) :
                UINT64_C(0x10200002f);
            if (value & ~mask) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
            word = value;
            if (offset == 0x30) bridge_written = true;
        } else store64(tx, word);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    bool per_core_dvfs() const { return dvfs_policy[6] & 0x80; }
    unsigned selected_dvfs_state() const {
        if (!per_core_dvfs()) return static_cast<unsigned>((dvfs_control >> 16) & 31);
        unsigned selected = dvfs_state_config.size();
        std::uint64_t best = 0;
        for (unsigned core = 0; core < dvfs_core_votes.size(); ++core) {
            const auto vote = dvfs_core_votes[core];
            if (!(core_mask & (1u << core)) || !(vote & 1)) continue;
            const auto state = static_cast<unsigned>((vote >> 1) & 31);
            const auto word = dvfs_state_config[state][1];
            const auto scaled = ((word & 0x1ff) << 16) | ((word >> 9) & 0xffff);
            // All requests share one reference clock. Compare effective rates,
            // not state indices, because reserved states need not be ordered.
            const auto rank = ((word >> 25) & 3) ? scaled : scaled * 2;
            if (selected == dvfs_state_config.size() || rank > best) {
                selected = state;
                best = rank;
            }
        }
        return selected;
    }
    void request_dvfs(unsigned request, bool vote_update = false) {
        if (dvfs_control & (UINT64_C(1) << 24)) {
            dvfs_status |= UINT64_C(1) << 33;
            if (request) dvfs_status |= UINT64_C(1) << 24;
            return;
        }
        dvfs_status &= ~(UINT64_C(1) << 33);
        if (!request) return;
        const auto target = selected_dvfs_state();
        // With no remaining request, retain the last applied state.
        // The separate power controller still owns supply removal.
        if (vote_update && target == dvfs_state_config.size()) return;
        dvfs_target = target;
        dvfs_request = request;
        dvfs_status |= UINT64_C(1) << 32;
    }
    unsigned requested_dvfs_state() const { return dvfs_target; }
    unsigned requested_dvfs_effects() const {
        return (dvfs_request & 1) ? 0xe : dvfs_request & 0xe;
    }
    void finish_dvfs(bool success, bool voltage_error = false) {
        if (success) {
            const auto state = std::uint64_t{requested_dvfs_state()};
            const auto effects = requested_dvfs_effects();
            if (effects & 2)
                dvfs_status = (dvfs_status & ~(UINT64_C(31) << 35)) | (state << 35);
            if (effects & 4) {
                effective_power_state = static_cast<unsigned>(state);
                dvfs_status = (dvfs_status & ~(UINT64_C(31) << 40)) | (state << 40);
            }
            if (effects & 8) {
                // Virtual SRAM retains data at each supported memory-access state.
                effective_memory_state = static_cast<unsigned>(state);
                dvfs_status = (dvfs_status & ~(UINT64_C(31) << 45)) | (state << 45);
            }
            dvfs_status = (dvfs_status & ~UINT64_C(0x1f1f)) | state | (state << 8);
        } else if (voltage_error) {
            dvfs_status |= UINT64_C(1) << 34;
        }
        dvfs_request = 0;
        dvfs_status &= ~(UINT64_C(1) << 32);
    }
    std::uint64_t* dvfs_state_word(std::uint64_t offset) {
        // PDP 0x8130b7be..0x8130b81a and 0x8130b51c write the last four columns of each state.
        constexpr std::array<std::uint64_t, 17> bases{
            0x58, 0xe8, 0xf20, 0x9d0, 0xa60, 0xaf0, 0xb80,
            0x4d8, 0x568, 0x1430, 0x14c0, 0x8b0, 0x940, 0x790, 0x820, 0xfb0, 0x1040};
        for (unsigned column = 0; column < bases.size(); ++column) {
            if (offset < bases[column] || offset >= bases[column] + 0x90) continue;
            const auto row = static_cast<unsigned>((offset - bases[column]) / 8);
            return &dvfs_state_config[row < 16 ? row : row + 14][column];
        }
        if (offset >= 0x1550 && offset < 0x1d10) {
            constexpr std::array<std::uint64_t, 17> columns{
                0, 8, 0xa0, 0x80, 0x88, 0x90, 0x98,
                0x40, 0x48, 0xe8, 0xf0, 0x70, 0x78, 0x60, 0x68, 0xa8, 0xb0};
            const auto row = static_cast<unsigned>((offset - 0x1550) / 0xf8) + 16;
            const auto position = (offset - 0x1550) % 0xf8;
            const auto column = std::find(columns.begin(), columns.end(), position);
            if (column != columns.end())
                return &dvfs_state_config[row][column - columns.begin()];
        }
        return nullptr;
    }
    void sequence_transport(tlm::tlm_generic_payload& tx) {
        const bool wide = tx.get_data_length() == 8;
        if (wide ? !valid64(tx) : !clock_detail::valid(tx)) return;
        const auto offset = tx.get_address() & ~UINT64_C(7);
        const auto shift = static_cast<unsigned>((tx.get_address() & 4) * 8);
        const auto mask = wide ? UINT64_MAX : UINT64_C(0xffffffff) << shift;
        const auto data = tx.is_write() ?
            (wide ? load64(tx) : std::uint64_t{clock_detail::load(tx)} << shift) : 0;
        const auto reply = [&](std::uint64_t value) {
            if (wide) store64(tx, value);
            else clock_detail::store(tx, static_cast<std::uint32_t>(value >> shift));
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
        };
        if (offset >= 0x1d10 && offset < 0x1d40) {
            auto& vote = dvfs_core_votes[(offset - 0x1d10) / 8];
            if (tx.is_read()) { reply(vote); return; }
            const auto merged = (vote & ~mask) | data;
            if ((merged & ~UINT64_C(0x3f)) || dvfs_request) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            vote = merged;
            if ((mask & 0x3f) && per_core_dvfs() && !(dvfs_config & 1))
                request_dvfs(1, true);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset == 0x788) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // The virtual platform has no external DVFS limit or tracking request.
            // Completed software requests do not leave an arbitration request active.
            reply(0);
            return;
        }
        if (offset == 0x50 || offset == 0xc40) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            reply(offset == 0x50 ? dvfs_status : boot_memory_access_done);
            return;
        }
        if (!offset) {
            if (tx.is_read()) {
                reply(dvfs_control | (std::uint64_t{dvfs_request} << 28));
                return;
            }
            constexpr auto writable = UINT64_C(0x10f9f000f);
            const auto merged = (dvfs_control & ~mask) | data;
            const auto request = static_cast<unsigned>(data & 15);
            if ((merged & ~writable) || dvfs_request || (request && (dvfs_config & 1))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            dvfs_control = merged & ~UINT64_C(15);
            if (data & (UINT64_C(1) << 25)) dvfs_status &= ~(UINT64_C(1) << 34);
            if (data & (UINT64_C(1) << 26)) dvfs_status &= ~(UINT64_C(1) << 24);
            request_dvfs(request);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        std::uint64_t* value = nullptr;
        if (offset == 8) value = &dvfs_config;
        else if (offset == 0xc10) value = &memory_access_config;
        else if (offset == 0xc28 || offset == 0xc30 || offset == 0xc38)
            value = &boot_memory_access_config[(offset - 0xc28) / 8];
        else if (offset >= 0x728 && offset < 0x788)
            value = &memory_access_regions[((offset - 0x728) % 0x20) / 8][(offset - 0x728) / 0x20];
        else if (offset >= 0xc80 && offset < 0xc98)
            value = &memory_access_regions[4][(offset - 0xc80) / 8];
        else if (offset >= 0xd00 && offset < 0xd48)
            value = &memory_access_regions[5 + (offset - 0xd00) / 0x18][((offset - 0xd00) % 0x18) / 8];
        else {
            value = dvfs_state_word(offset);
            if (!value) {
                const auto policy = std::find(dvfs_policy_offsets.begin(), dvfs_policy_offsets.end(), offset);
                if (policy != dvfs_policy_offsets.end()) value = &dvfs_policy[policy - dvfs_policy_offsets.begin()];
            }
        }
        if (!value) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (tx.is_write()) {
            const auto merged = (*value & ~mask) | data;
            if ((offset == 8 && (merged & ~UINT64_C(0x3e1f3e1f00830107))) ||
                (offset == 0xc10 && (merged & ~UINT64_C(0x003e1f00000007ff)))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
            }
            *value = merged;
            if (offset >= 0xc28 && offset <= 0xc38) {
                boot_memory_access_writes |= 1u << ((offset - 0xc28) / 8);
                boot_memory_access_done = false;
            } else if (offset == 0xc10) {
                boot_memory_access_done = false;
            }
        } else {
            reply(*value);
            return;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void fabric_transport(tlm::tlm_generic_payload& tx) {
        if (!valid64(tx)) return;
        const auto offset = tx.get_address();
        std::uint64_t* word = nullptr;
        if (offset == 0xf8) word = &fabric;
        else {
            // QCCE callbacks retain microarchitecture policy without completion effects.
            static constexpr std::array<std::uint64_t, 12> registers{
                0x80, 0x88, 0x90, 0x100, 0x108, 0x118, 0x168, 0xa0, 0xa8, 0xc8,
                0x150, 0x320};
            for (unsigned i = 0; i < registers.size(); ++i)
                if (offset == registers[i]) word = &fabric_policy[i];
        }
        if (!word) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (tx.is_write()) {
            const auto value = load64(tx);
            if (offset == 0xf8 && (value & ~((UINT64_C(1) << 52) | (UINT64_C(15) << 40)))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
            }
            *word = value;
        } else store64(tx, *word);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void runtime_transport(tlm::tlm_generic_payload& tx, unsigned core_count,
                           bool special_record) {
        if (!valid64(tx)) return;
        if (special_record && (tx.get_address() == 0xa10 || tx.get_address() == 0xa18)) {
            auto& value = tx.get_address() == 0xa10 ? runtime_aux_control : runtime_aux_status;
            if (tx.is_write() && tx.get_address() == 0xa18) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (tx.is_write()) value = load64(tx);
            else store64(tx, value);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        const auto core = tx.get_address() / 0x40;
        const auto offset = tx.get_address() % 0x40;
        constexpr std::array<std::uint64_t, 4> registers{0x08, 0x10, 0x18, 0x20};
        const auto word = std::find(registers.begin(), registers.end(), offset);
        std::size_t index;
        if (core < core_count && word != registers.end())
            index = core * registers.size() + (word - registers.begin());
        else {
            constexpr std::array<std::uint64_t, 4> special_offsets{
                0x168, 0x178, 0x180, 0x188};
            const auto special =
                std::find(special_offsets.begin(), special_offsets.end(), tx.get_address());
            if (!special_record || special == special_offsets.end()) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            index = 24 + (special - special_offsets.begin());
        }
        auto& value = runtime_config[index];
        if (tx.is_write()) value = load64(tx);
        else store64(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    enum class ClusterBank { config, table, aux };
    void cluster_transport(tlm::tlm_generic_payload& tx, ClusterBank bank) {
        // Both word halves and a native doubleword address the same retained state.
        const bool wide = tx.get_data_length() == 8;
        if (!(wide ? valid64(tx) : clock_detail::valid(tx))) return;
        const auto offset = tx.get_address() & ~UINT64_C(7);
        std::uint64_t* word = nullptr;
        if (bank == ClusterBank::config) {
            static constexpr std::array<std::uint64_t, 11> registers{
                0x5d0, 0x630, 0x638, 0x640, 0x648, 0x650, 0x658, 0x660, 0x678, 0x680, 0x268};
            for (unsigned i = 0; i < registers.size(); ++i)
                if (offset == registers[i]) word = &cluster_config[i];
        } else if (bank == ClusterBank::table) {
            if (offset >= 0x400 && offset < 0x600)
                word = &cluster_table[(offset - 0x400) / 8];
            else if (offset >= 0x1400 && offset < 0x1418)
                word = &cluster_secondary[(offset - 0x1400) / 8];
            else if (offset < 0x200 || (offset >= 0x1000 && offset < 0x1018)) {
                // Counters for the primary and secondary records: low-power entries and residence time.
                // This continuously awake profile has no low-power transition source.
                if (!tx.is_read()) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
                if (wide) store64(tx, 0);
                else clock_detail::store(tx, 0);
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
        } else {
            if (offset >= 0x900 && offset < 0x920)
                word = &cluster_aux_table[(offset - 0x900) / 8];
            else if (offset >= 0xa10 && offset < 0xa30)
                word = &cluster_aux_config[(offset - 0xa10) / 8];
            else if (offset == 0x10) word = &cluster_aux_policy;
            else if (offset >= 0x130 && offset < 0x1b0)
                word = &auxiliary_state_config[(offset - 0x130) / 8][0];
            else if (offset >= 0x1c0 && offset < 0x240)
                word = &auxiliary_state_config[(offset - 0x1c0) / 8][1];
            else if (offset >= 0xc18 && offset < 0xc98)
                word = &auxiliary_state_config[16 + ((offset - 0xc18) % 0x40) / 8][(offset - 0xc18) / 0x40];
            else if (offset == 0xb50) word = &qmx_dpm_config[0];
            else if (offset >= 0x480 && offset < 0x4d0)
                word = &qmx_dpm_config[1 + (offset - 0x480) / 8];
            else if (offset >= 0x5b8 && offset < 0x5e8)
                word = &qmx_dpm_config[11 + (offset - 0x5b8) / 8];
        }
        if (!word) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (bank == ClusterBank::config && offset == 0x5d0 && tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
        }
        const unsigned shift = (tx.get_address() & 4) * 8;
        if (tx.is_write()) {
            if (wide) *word = load64(tx);
            else {
                const auto mask = UINT64_C(0xffffffff) << shift;
                *word = (*word & ~mask) | (std::uint64_t{clock_detail::load(tx)} << shift);
            }
        } else {
            auto value = *word;
            if (bank == ClusterBank::config && offset == 0x5d0)
                value |= std::uint64_t{cluster_index} << 8;
            if (wide) store64(tx, value);
            else clock_detail::store(tx, static_cast<std::uint32_t>(value >> shift));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void feature_transport(tlm::tlm_generic_payload& tx, std::uint32_t identity) {
        if (!valid64(tx)) return;
        const auto address = tx.get_address();
        const unsigned core = static_cast<unsigned>((address >> 20) & 0xf);
        if ((address >> 24) || core >= 8 || !(core_mask & (1u << core)) ||
            (address & 0xfffff) != 0xd00) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        }
        if (!tx.is_read()) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
        // Match the pinned QEMU max TCG MIDR, not a Qualcomm part identity.
        store64(tx, identity);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void core_transport(tlm::tlm_generic_payload& tx) {
        if (!valid64(tx)) return;
        const auto address = tx.get_address();
        const unsigned core = static_cast<unsigned>(address >> 20);
        if (core >= 8 || !(core_mask & (1u << core))) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        }
        const auto offset = address & 0xfffff;
        if (offset >= 0xe008 && offset <= 0xe0a0) {
            const auto row = (offset - 0xe000) >> 6;
            const auto word = offset & 0x3f;
            if (word < 8 || word > 0x20) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            auto& value = core_runtime_config[core][row * 4 + word / 8 - 1];
            if (tx.is_write()) value = load64(tx);
            else store64(tx, value);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset >= 0xb8 && offset <= 0x130) {
            auto& word = core_runtime_policy[core][(offset - 0xb8) / 8];
            if (tx.is_write()) word = load64(tx);
            else store64(tx, word);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset != 0x150) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (tx.is_write()) {
            const auto value = load64(tx);
            // The ROM writes bit 56. The selected runtime producer additionally
            // writes bits 50 and 55, with configuration at bits 56 and 57.
            // These fields configure the core. Power ACK and reset release remain
            // controlled by +0x278 and +0x260, not by policy publication.
            constexpr auto mask = (UINT64_C(1) << 50) | (UINT64_C(1) << 55) |
                (UINT64_C(3) << 56);
            if (value & ~mask) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
            core_policy[core] = value;
        } else store64(tx, core_policy[core]);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    // Local address is core*0x100000 plus the pair offset.
    // This transport retains the words. The enclosing domain applies the start
    // request after the transaction, so power and reset stay lane-owned.
    void release_transport(tlm::tlm_generic_payload& tx) {
        if (!valid64(tx)) return;
        const auto address = tx.get_address();
        const unsigned core = static_cast<unsigned>(address >> 20);
        const auto offset = address & 0xfffff;
        if (core >= 8 || !(core_mask & (1u << core)) || (offset != 0 && offset != 8)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& word = release_words[core][offset / 8];
        if (tx.is_write()) {
            const auto value = load64(tx);
            // The selected TZ producer writes command zero or one to +0x0.
            // The payload at +0x8 stays an unconstrained 64-bit value.
            if (offset == 0 && value > 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            word = value;
        } else store64(tx, word);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

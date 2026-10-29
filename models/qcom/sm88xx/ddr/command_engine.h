// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include "virtual_dram.h"
#include <array>
#include <tlm_utils/simple_initiator_socket.h>

namespace qcom_vp {

// Native descriptor execution with ideal electrical timing. Geometry comes from
// the backend topology, not from the guest's shared geometry header.
class QcomDdrCommandEngine : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomDdrCommandEngine> broadcast_socket{"broadcast_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDdrCommandEngine> and_socket{"and_socket"};
    tlm_utils::simple_initiator_socket<QcomDdrCommandEngine> fallback_socket{"fallback_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDdrCommandEngine> pair0_socket{"pair0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDdrCommandEngine> pair1_socket{"pair1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDdrCommandEngine> pair2_socket{"pair2_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDdrCommandEngine> pair3_socket{"pair3_socket"};
    cci::cci_param<std::string> backend_path{"backend_path", "", "Required QcomVirtualDdr object path"};

    explicit QcomDdrCommandEngine(sc_core::sc_module_name name) : sc_module(name) {
        broadcast_socket.register_b_transport(this, &QcomDdrCommandEngine::broadcast_transport);
        and_socket.register_b_transport(this, &QcomDdrCommandEngine::and_transport);
        pair0_socket.register_b_transport(this, &QcomDdrCommandEngine::pair0_transport);
        pair1_socket.register_b_transport(this, &QcomDdrCommandEngine::pair1_transport);
        pair2_socket.register_b_transport(this, &QcomDdrCommandEngine::pair2_transport);
        pair3_socket.register_b_transport(this, &QcomDdrCommandEngine::pair3_transport);
    }

    void cold_reset() {
        for (auto& pair : pairs_) {
            pair.selection = pair.trigger = pair.ready = 0;
            pair.results.fill(0);
            for (auto& descriptor : pair.descriptors) descriptor = {};
            for (unsigned rank = 0; rank < 2; ++rank) {
                pair.mr[rank].fill(0);
                if (populated_[rank]) {
                    pair.mr[rank][5] = 0xff;
                    pair.mr[rank][6] = 1;
                    pair.mr[rank][8] = 0x10; // XBL 0x1492678e: density four, x16, 15/10/16.
                }
            }
        }
    }

    void apply_rank_policy(unsigned pair, std::uint32_t ranks) {
        pairs_[pair].ready = (pairs_[pair].ready & ~0x300U) | ((ranks & 3U) << 8);
    }

private:
    static constexpr std::uint64_t first_offset = 0x5700;
    static constexpr std::uint64_t bank_extent = 0x10000;
    // Identity MRR returns one byte. Oscillator MRR returns one byte per
    // x8 lane. Absent MRR returns all ones. Absent MRW changes nothing.
    static constexpr std::uint32_t absent_result = 0xffffffff;
    struct Descriptor { std::uint32_t command = 0, companion = 0; };
    struct Pair {
        std::array<Descriptor, 32> descriptors{};
        std::array<std::array<std::uint8_t, 256>, 2> mr{};
        std::array<std::uint32_t, 2> results{};
        std::uint32_t selection = 0, trigger = 0, ready = 0;
    };
    std::array<Pair, 4> pairs_{};
    std::array<bool, 2> populated_{};
    QcomVirtualDdr* backend_ = nullptr;

    void end_of_elaboration() override {
        const auto& path = backend_path.get_value();
        auto* backend = dynamic_cast<QcomVirtualDdr*>(sc_core::sc_find_object(path.c_str()));
        if (!backend && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            backend = dynamic_cast<QcomVirtualDdr*>(sc_core::sc_find_object(relative.c_str()));
        }
        if (path.empty() || !backend)
            throw std::invalid_argument("DDR command engine requires a QcomVirtualDdr at backend_path");
        if (backend->rows.get_value() != 15 || backend->columns.get_value() != 10 ||
            backend->banks.get_value() != 16 || backend->rank0_mib.get_value() != 512 ||
            (backend->rank1_mib.get_value() != 0 && backend->rank1_mib.get_value() != 512))
            throw std::invalid_argument("DDR command engine requires 15/10/16 geometry and 512 MiB per populated x8 lane");
        populated_[0] = true;
        populated_[1] = backend->rank1_mib.get_value() != 0;
        backend_ = backend;
        cold_reset();
    }

    static unsigned oscillator_cycles(std::uint8_t duration) {
        // Native MR37 duration table: 128, 256, 512, 1008, 2048, 4096, 8192 CK.
        if (duration < 0x40) return unsigned(duration) * 16;
        if (!(duration & 0x3f)) return 1024U << (duration >> 6);
        return 0;
    }

    unsigned oscillator_count(std::uint8_t duration) const {
        // The ideal physical oscillator period is 400 ps, independent of CK.
        const auto count = (oscillator_cycles(duration) * backend_->clock_period_ps() + 400) / 800;
        return count <= 0xffff ? static_cast<unsigned>(count) : 0;
    }

    bool valid_batch(const Pair& pair) const {
        if (!pair.selection) return false;
        std::array<std::uint8_t, 2> duration{pair.mr[0][37], pair.mr[1][37]};
        for (unsigned slot = 0; slot < pair.descriptors.size(); ++slot) {
            if (!(pair.selection & (std::uint32_t{1} << slot))) continue;
            const auto command = pair.descriptors[slot].command;
            if (command & 0x0000cc00U) return false;
            switch (command & 0x3ff) {
            // These observed operations have ideal physical timing. Their
            // physical names and companion timing fields remain unestablished.
            case 0x60: case 0xa4: case 0x00: case 0xa0: case 0x64:
                break;
            // Only the native zero-payload electrical forms are established.
            case 0x50: case 0x58: case 0x8c:
                if (!(command & 0x3000) || (command >> 16)) return false;
                break;
            case 0x77:
                // The observed flagged MRW form always writes MR16 with zero.
                if ((command >> 16) != 16) return false;
                [[fallthrough]];
            case 0x70: case 0x74:
                if (!(command & 0x3000)) return false;
                if ((command & 0x3ff) == 0x74 && ((command >> 16) & 0xff) == 37)
                    for (unsigned rank = 0; rank < 2; ++rank)
                        if (command & (std::uint32_t{1} << (12 + rank)))
                            duration[rank] = static_cast<std::uint8_t>(command >> 24);
                break;
            case 0x94:
                if (!(command & 0x3000) || (command >> 16)) return false;
                for (unsigned rank = 0; rank < 2; ++rank)
                    if (populated_[rank] && (command & (std::uint32_t{1} << (12 + rank))) &&
                        !oscillator_count(duration[rank])) return false;
                break;
            default:
                return false;
            }
        }
        return true;
    }

    void execute(Pair& pair) {
        // +0x5800 is a bitmask, not a descriptor count. In particular, 0xb0f7
        // selects slots 0,1,2,4,5,6,7,12,13,15. The virtual reset policy makes
        // an unwritten slot zero (op00, rank mask zero), which has no MR effect.
        // Native code can skip the conditional slot-15 MR25 writer. A previously
        // programmed slot remains intact until another write or cold reset.
        for (unsigned slot = 0; slot < pair.descriptors.size(); ++slot) {
            if (!(pair.selection & (std::uint32_t{1} << slot))) continue;
            const auto command = pair.descriptors[slot].command;
            const auto operation = command & 0x3ff;
            const auto mr = (command >> 16) & 0xff;
            for (unsigned rank = 0; rank < 2; ++rank) {
                if (!(command & (std::uint32_t{1} << (12 + rank)))) continue;
                if ((operation == 0x74 || operation == 0x77) && populated_[rank])
                    pair.mr[rank][mr] = static_cast<std::uint8_t>(command >> 24);
                else if (operation == 0x94 && populated_[rank]) {
                    const auto count = oscillator_count(pair.mr[rank][37]);
                    pair.mr[rank][35] = static_cast<std::uint8_t>(count);
                    pair.mr[rank][36] = static_cast<std::uint8_t>(count >> 8);
                } else if (operation == 0x50)
                    pair.ready |= std::uint32_t{1} << (8 + rank);
                else if (operation == 0x70) {
                    const auto lanes = (mr == 35 || mr == 36) ? 0x101U : 1U;
                    pair.results[rank] = populated_[rank] ? pair.mr[rank][mr] * lanes : absent_result;
                }
            }
        }
        // Publish every MR effect before acknowledging synchronous completion.
        pair.trigger = 0;
    }

    bool read_word(const Pair& pair, std::uint64_t offset, std::uint32_t& value) const {
        if (offset >= first_offset && offset < 0x5800) {
            const auto& descriptor = pair.descriptors[(offset - first_offset) / 8];
            value = (offset & 4) ? descriptor.companion : descriptor.command;
        } else {
            switch (offset) {
            case 0x708: value = pair.ready; break;
            case 0x5c0c: value = (pair.ready & 0x300U) << 8; break;
            case 0x5800: value = pair.selection; break;
            case 0x5804: value = pair.trigger; break;
            case 0x5a38: value = pair.results[0]; break;
            case 0x5a3c: value = pair.results[1]; break;
            case 0x5c94: value = (populated_[0] ? 0U : 1U) | (populated_[1] ? 0U : 0x10U); break;
            default: return false;
            }
        }
        return true;
    }

    void transport(unsigned first_pair, unsigned pair_count, std::uint64_t offset,
                   tlm::tlm_generic_payload& tx) {
        if (!clock_detail::valid(tx)) return;
        std::uint32_t previous = 0;
        if (!read_word(pairs_[first_pair], offset, previous)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            // Broadcast reads select pair zero under the virtual board policy.
            // Pair ports retain independent state and return their own values.
            clock_detail::store(tx, previous);
        } else {
            const auto value = clock_detail::load(tx);
            if (offset == 0x708 || offset == 0x5c0c || offset == 0x5a38 || offset == 0x5a3c || offset == 0x5c94 ||
                (offset == 0x5804 && value != 1)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (offset == 0x5804) {
                // Validate all selected banks before changing any MR state.
                // A rejected trigger stays asserted and cannot report success.
                bool valid = true;
                for (unsigned p = first_pair; p < first_pair + pair_count; ++p) {
                    pairs_[p].trigger = 1;
                    if (!valid_batch(pairs_[p])) valid = false;
                }
                if (!valid) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
            }
            for (unsigned p = first_pair; p < first_pair + pair_count; ++p) {
                auto& pair = pairs_[p];
                if (offset < 0x5800) {
                    auto& descriptor = pair.descriptors[(offset - first_offset) / 8];
                    if (offset & 4) descriptor.companion = value;
                    else descriptor.command = value;
                } else if (offset == 0x5800) pair.selection = value;
                else execute(pair);
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    static bool command_word(std::uint64_t offset) {
        return offset == 0x708 || (offset >= first_offset && offset < 0x5800) || offset == 0x5800 ||
            offset == 0x5804 || offset == 0x5a38 || offset == 0x5a3c || offset == 0x5c0c || offset == 0x5c94;
    }
    void broadcast_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        const auto offset = tx.get_address();
        if (command_word(offset))
            transport(0, 4, offset, tx);
        else
            fallback_socket->b_transport(tx, delay);
    }
    void pair_transport(unsigned pair, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        const auto offset = tx.get_address();
        if (offset >= bank_extent) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (command_word(offset)) {
            transport(pair, 1, offset, tx);
        } else {
            tx.set_address(((pair + 1) << 16) | offset);
            fallback_socket->b_transport(tx, delay);
            tx.set_address(offset);
        }
    }
    void and_transport(int id, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!clock_detail::valid(tx)) return;
        if (tx.is_write()) {
            broadcast_transport(id, tx, delay);
            return;
        }
        std::uint32_t value = ~0U;
        const auto initial_delay = delay;
        for (unsigned pair = 0; pair < pairs_.size(); ++pair) {
            auto pair_delay = initial_delay;
            pair_transport(pair, tx, pair_delay);
            delay = std::max(delay, pair_delay);
            if (!tx.is_response_ok()) return;
            value &= clock_detail::load(tx);
        }
        clock_detail::store(tx, value);
    }
    void pair0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) { pair_transport(0, tx, delay); }
    void pair1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) { pair_transport(1, tx, delay); }
    void pair2_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) { pair_transport(2, tx, delay); }
    void pair3_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) { pair_transport(3, tx, delay); }
};

} // namespace qcom_vp

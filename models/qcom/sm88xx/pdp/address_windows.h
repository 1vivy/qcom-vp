// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <algorithm>
#include <array>
#include <cstdint>

namespace qcom_vp {

// The PDP programs eight windows before it accesses its lane-specific DDR data.
class PdpAddressWindows : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<PdpAddressWindows> registers{"registers"};
    tlm_utils::simple_target_socket<PdpAddressWindows> memory{"memory"};
    tlm_utils::simple_initiator_socket<PdpAddressWindows> output{"output"};

    explicit PdpAddressWindows(sc_core::sc_module_name name) : sc_module(name) {
        registers.register_b_transport(this, &PdpAddressWindows::register_transport);
        memory.register_b_transport(this, &PdpAddressWindows::transport);
        memory.register_transport_dbg(this, &PdpAddressWindows::debug);
        memory.register_get_direct_mem_ptr(this, &PdpAddressWindows::dmi);
        output.register_invalidate_direct_mem_ptr(this, &PdpAddressWindows::invalidate);
    }

private:
    struct Window {
        std::array<std::uint32_t, 3> words{};
        bool enabled() const { return words[2] & 0x80000000u; }
        std::uint64_t first() const { return std::uint64_t{words[0] & 0xffffu} << 16; }
        std::uint64_t last() const {
            return first() | (std::uint64_t{(~words[0] >> 16) & 0xffffu} << 16) | 0xffffu;
        }
        std::uint64_t target() const {
            return (std::uint64_t{words[2] & 0xfu} << 48) | (std::uint64_t{words[1]} << 16);
        }
        bool valid() const {
            const auto mask = words[0] >> 16;
            const auto holes = (~mask) & 0xffffu;
            return !(holes & (holes + 1)) && !(words[0] & holes) &&
                !(words[1] & holes) && !(words[2] & 0x7ffffff0u);
        }
    };
    std::array<Window, 8> windows_{};
    struct Mapping { std::uint64_t first, last, target; };

    Mapping mapping(std::uint64_t address) const {
        Mapping result{0, 0xffffffffu, 0};
        for (const auto& window : windows_) {
            if (!window.enabled()) continue;
            if (address >= window.first() && address <= window.last()) {
                result = {window.first(), window.last(), window.target()};
                break;
            }
        }
        // Bound DMI to one translation, including identity gaps between windows.
        const auto translated = result.target + address - result.first;
        for (const auto& window : windows_) {
            if (!window.enabled()) continue;
            if (window.first() > address) result.last = std::min(result.last, window.first() - 1);
            if (window.last() < address) result.first = std::max(result.first, window.last() + 1);
        }
        result.target = translated - (address - result.first);
        return result;
    }

    void register_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto offset = tx.get_address();
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{tx.get_data_ptr()[i]} << (8 * i);
        if (offset == 0 && tx.is_read()) {
            value = windows_.size();
        } else if (offset >= 0x100 && (offset - 0x100) / 0x400 < windows_.size() &&
                   (offset - 0x100) % 0x400 < 12) {
            auto& window = windows_[(offset - 0x100) / 0x400];
            const auto index = ((offset - 0x100) % 0x400) / 4;
            if (tx.is_write()) {
                auto next = window;
                next.words[index] = value;
                if (next.enabled() && !next.valid()) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                memory->invalidate_direct_mem_ptr(0, 0xffffffffu);
                window = next;
            } else value = window.words[index];
        } else {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            for (unsigned i = 0; i < 4; ++i) tx.get_data_ptr()[i] = value >> (8 * i);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    bool prepare(tlm::tlm_generic_payload& tx, Mapping& map) const {
        const auto address = tx.get_address();
        const auto size = tx.get_data_length();
        if ((!tx.is_read() && !tx.is_write()) || !tx.get_data_ptr() || !size) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        if (tx.get_streaming_width() < size || address > 0xffffffffu) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        map = mapping(address);
        if (size - 1 > map.last - address) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        return true;
    }
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        Mapping map{};
        tx.set_dmi_allowed(false);
        if (!prepare(tx, map)) return;
        const auto address = tx.get_address();
        tx.set_address(map.target + address - map.first);
        output->b_transport(tx, delay);
        tx.set_address(address);
    }
    unsigned debug(tlm::tlm_generic_payload& tx) {
        Mapping map{};
        if (!prepare(tx, map)) return 0;
        const auto address = tx.get_address();
        tx.set_address(map.target + address - map.first);
        const auto count = output->transport_dbg(tx);
        tx.set_address(address);
        return count;
    }
    bool dmi(tlm::tlm_generic_payload& tx, tlm::tlm_dmi& region) {
        const auto address = tx.get_address();
        if (address > 0xffffffffu) return false;
        const auto map = mapping(address);
        const auto translated = map.target + address - map.first;
        tx.set_address(translated);
        const bool granted = output->get_direct_mem_ptr(tx, region);
        tx.set_address(address);
        if (!granted || translated < region.get_start_address() || translated > region.get_end_address())
            return false;
        const auto first = std::max<std::uint64_t>(map.target, region.get_start_address());
        const auto last = std::min<std::uint64_t>(map.target + map.last - map.first, region.get_end_address());
        region.set_dmi_ptr(region.get_dmi_ptr() + first - region.get_start_address());
        region.set_start_address(map.first + first - map.target);
        region.set_end_address(map.first + last - map.target);
        return true;
    }
    void invalidate(sc_dt::uint64, sc_dt::uint64) {
        // One downstream region can have several programmed aliases.
        memory->invalidate_direct_mem_ptr(0, 0xffffffffu);
    }
};

} // namespace qcom_vp

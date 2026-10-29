// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef QCOM_VP_UFS_VIRTUAL_PHY_H
#define QCOM_VP_UFS_VIRTUAL_PHY_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <tlm>
#include "xbl_phy_registers.h"

// This opt-in link substitutes digital state for analog calibration.
// Register identities come from the PBL and selected XBL QMP v4 tables.
// No vendor calibration values enter this model.
class UfsVirtualPhy {
public:
    void set_host_reset(bool asserted) {
        host_reset = asserted;
        if (asserted) {
            started = false;
            programmed.fill(false);
        }
    }
    void set_source_clocks(bool enabled) {
        source_clocks = enabled;
        if (!enabled) started = false;
    }
    bool ready() const { return !host_reset && reference && source_clocks && supplied && started && reset_released && powered && pll_enabled; }
    bool reference_running() const { return reference; }
    // One line for diagnostics: each readiness input in ready() order.
    std::string describe() const {
        return std::string("host_reset=") + (host_reset ? "1" : "0") + " reference=" + (reference ? "1" : "0") +
            " source_clocks=" + (source_clocks ? "1" : "0") + " supplied=" + (supplied ? "1" : "0") +
            " started=" + (started ? "1" : "0") + " reset_released=" + (reset_released ? "1" : "0") +
            " powered=" + (powered ? "1" : "0") + " pll_enabled=" + (pll_enabled ? "1" : "0");
    }
    void set_reference(bool enabled) {
        reference = enabled;
        if (!enabled) started = false;
    }

    void set_supply(bool enabled) {
        if (!enabled) {
            values.fill(0);
            programmed.fill(false);
            reset = 0;
            reset_seen = reset_released = started = powered = pll_enabled = false;
            host_reset = false;
        }
        supplied = enabled;
    }

    bool removes_readiness(const tlm::tlm_generic_payload& tx) const {
        if (!ready() || !tx.is_write()) return false;
        const auto value = tx.get_data_ptr()[0];
        switch (tx.get_address()) {
        case 0x408: return value & 1;
        case 0x400: case 0x404: return !(value & 1);
        case 0x42c: return !(value & 0x20);
        default: return false;
        }
    }

    void transport(tlm::tlm_generic_payload& tx) {
        if (!supplied) return fail(tx);
        const auto offset = tx.get_address();
        auto* bytes = tx.get_data_ptr();
        uint32_t value = 0;
        if (tx.is_write()) {
            value = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
                    (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
        }
        if (offset == 0x408) {
            if (tx.is_write()) {
                if (value & ~uint32_t{1}) return fail(tx);
                if (value & 1) {
                    reset_seen = true;
                    reset_released = false;
                    started = false;
                    programmed.fill(false);
                } else {
                    reset_released = reset_seen;
                }
                reset = value;
            } else value = reset;
        } else if (offset == 0x400) {
            if (tx.is_write()) {
                if (value & ~uint32_t{1}) return fail(tx);
                if ((value & 1) && !(reference && reset_released && powered && pll_enabled)) return fail(tx);
                if ((value & 1) && (host_reset || !source_clocks)) return fail(tx);
                started = value & 1;
            } else value = started ? 1 : 0;
        } else if (offset == 0x5a8) {
            if (tx.is_write()) return fail(tx);
            value = ready() ? 1 : 0;
        } else {
            const auto found = std::lower_bound(offsets.begin(), offsets.end(), offset);
            const bool pbl_register = found != offsets.end() && *found == offset;
            const bool xbl_register =
                std::binary_search(ufs_xbl_phy_offsets.begin(), ufs_xbl_phy_offsets.end(), offset);
            if (!pbl_register && !xbl_register) return fail(tx);
            const auto index = static_cast<std::size_t>(offset / 4);
            if (tx.is_write()) {
                if (value > 0xff) return fail(tx);
                if (started && offset != 0x404 && offset != 0x42c) return fail(tx);
                values[index] = value;
                programmed[index] = true;
                if (offset == 0x404) {
                    powered = value & 1;
                    if (!powered) started = false;
                }
                if (offset == 0x42c) {
                    pll_enabled = value & 0x20;
                    if (!pll_enabled) started = false;
                }
            } else {
                // PLL control has an explicit virtual reset value of zero.
                if (!programmed[index] && offset != 0x42c) return fail(tx);
                value = values[index];
            }
        }
        if (tx.is_read()) {
            for (unsigned i = 0; i != 4; ++i) bytes[i] = value >> (8 * i);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

private:
    static void fail(tlm::tlm_generic_payload& tx) {
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    inline static constexpr std::array<uint16_t, 75> offsets{{
        0x3c, 0x70, 0x74, 0x78, 0x80, 0x84, 0x88, 0x8c, 0x9c,
        0xa0, 0xa4, 0xa8, 0xac, 0xbc, 0xdc, 0xf4, 0xfc, 0x100,
        0x110, 0x118, 0x120, 0x124, 0x13c, 0x144, 0x148, 0x15c,
        0x160, 0x174, 0x404, 0x414, 0x418, 0x420, 0x42c, 0x430,
        0x448, 0x474, 0x4bc, 0x4dc, 0x4ec, 0x57c, 0x588, 0x58c,
        0x594, 0x654, 0x100c, 0x1030, 0x1034, 0x1050, 0x107c,
        0x1080, 0x1084, 0x1088, 0x1100, 0x1108, 0x1258, 0x12b4,
        0x12bc, 0x1378, 0x13b4, 0x13d8, 0x13dc, 0x13e0, 0x13e4,
        0x1418, 0x141c, 0x1420, 0x1424, 0x1428, 0x142c, 0x1430,
        0x1434, 0x1438, 0x1578, 0x157c, 0x1580
    }};
    std::array<uint8_t, 0x2000 / 4> values{};
    std::array<bool, 0x2000 / 4> programmed{};
    bool host_reset = false;
    bool source_clocks = true;
    uint32_t reset = 0;
    bool reset_seen = false;
    bool reset_released = false;
    bool started = false;
    bool powered = false;
    bool pll_enabled = false;
    bool supplied = false;
    bool reference = false;
};
#endif

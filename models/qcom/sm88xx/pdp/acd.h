// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <systemc>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <iostream>

namespace qcom_vp {

// NCC adaptive clock distribution (ACD) bank. The PDP instance table places lane n
// at local 0x409d0000 and system 0x199d0000 plus n << 24. The second instance at
// 0x19aa2000 uses the same layout. See models/qcom/sm88xx/pdp/acd_evidence.md.
//
// Configuration registers retain 32-bit writes. Status and counter registers keep
// their initial values, because the model has no clock-path measurement.
//
// The table below is an unverified initial state. The selected firmware establishes
// only the offsets that it reads and writes. No permitted source establishes the
// other offsets or any reset value.
// A rising UPDATEAUTOCAL edge in ACDCR runs one functional calibration. It captures
// the configured ACDDVMRC delay code and sets ACD_MISC_STATUS.CALIB_DONE.
// Undeclared offsets fail. A write to a read-only register fails.
class PdpAcd : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<PdpAcd> target_socket{"target_socket"};

    explicit PdpAcd(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &PdpAcd::transport);
        cold_reset();
    }

    void cold_reset() {
        for (unsigned index = 0; index < registers.size(); ++index) words_[index] = registers[index].reset;
        calibrations_ = 0;
    }

    std::uint32_t calibrations() const { return calibrations_; }

private:
    struct Register {
        std::uint32_t offset;
        bool writable;
        std::uint32_t reset;
    };
    // Offset, access, and initial value.
    static constexpr std::array<Register, 58> registers{{
        {0x000, false, 0x10030000}, {0x004, true, 0}, {0x008, true, 0}, {0x00c, true, 0},
        {0x010, false, 0}, {0x014, true, 0}, {0x018, true, 0}, {0x01c, false, 0},
        {0x020, true, 0}, {0x024, false, 0}, {0x028, true, 0}, {0x02c, false, 0},
        {0x030, true, 0x02c3da78}, {0x034, true, 0}, {0x038, true, 0x037d2002}, {0x03c, true, 0},
        {0x040, true, 0}, {0x044, true, 1}, {0x048, true, 0}, {0x04c, true, 0},
        {0x050, true, 0}, {0x054, true, 0}, {0x058, false, 0}, {0x098, false, 0x10030000},
        {0x09c, true, 0}, {0x0a0, false, 0}, {0x0ac, true, 0}, {0x0b4, false, 0},
        {0x0b8, false, 0}, {0x0bc, false, 0}, {0x0c0, false, 0}, {0x0c4, false, 0},
        {0x0c8, false, 0}, {0x0cc, true, 0}, {0x0d0, true, 0}, {0x0d4, false, 0},
        {0x0d8, true, 0}, {0x0dc, true, 0}, {0x0e0, false, 0x10000000}, {0x0e4, true, 0},
        {0x0e8, false, 0}, {0x0ec, true, 0}, {0x0f0, false, 0}, {0x0f4, false, 0},
        {0x0f8, true, 0}, {0x0fc, true, 0}, {0x100, true, 0}, {0x104, true, 0},
        {0x108, true, 0}, {0x10c, true, 0}, {0x110, false, 0}, {0x114, true, 0x0007f800},
        {0x118, false, 0}, {0x11c, false, 0}, {0x120, false, 0}, {0x124, true, 0},
        {0x128, true, 0}, {0x12c, true, 0}}};
    static constexpr std::uint32_t acdcr = 0x004;
    static constexpr std::uint32_t acddvmrc = 0x014;
    static constexpr std::uint32_t misc_status = 0x044;
    static constexpr std::uint32_t readout_cfg = 0x048;
    static constexpr std::uint32_t readout = 0x098;
    static constexpr std::uint32_t rdvm_readout_cfg = 0x0dc;
    static constexpr std::uint32_t rdvm_readout = 0x0e0;
    static constexpr std::uint32_t captured_code = 0x110;
    static constexpr std::uint32_t update_autocal = 1u << 24;
    static constexpr std::uint32_t calib_done = 1u << 1;
    // ACD_MISC_STATUS bits 1:0 are read-only status. The upper bits retain writes.
    static constexpr std::uint32_t misc_status_mask = 3;

    std::array<std::uint32_t, registers.size()> words_{};
    std::uint32_t calibrations_ = 0;

    static int find(std::uint64_t offset) {
        for (unsigned index = 0; index < registers.size(); ++index)
            if (registers[index].offset == offset) return static_cast<int>(index);
        return -1;
    }
    std::uint32_t& word(std::uint32_t offset) { return words_[static_cast<unsigned>(find(offset))]; }

    void calibrate() {
        // The functional calibration confirms the configured delay code. It does not
        // measure a clock path. See "Calibration" in models/qcom/sm88xx/pdp/acd_evidence.md.
        word(captured_code) = word(acddvmrc);
        word(misc_status) |= calib_done;
        ++calibrations_;
        std::cerr << "PDP_ACD_CALIBRATION model=" << name() << " acdcr=0x" << std::hex << word(acdcr)
                  << " code=0x" << word(captured_code) << std::dec << '\n';
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        const int index = find(offset);
        if (index < 0) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        auto& value = words_[static_cast<unsigned>(index)];
        if (tx.is_read()) {
            // A readout selection other than zero has no recovered data source.
            if ((offset == readout && word(readout_cfg) & 0x7f) ||
                (offset == rdvm_readout && word(rdvm_readout_cfg) & 0x1f)) {
                std::cerr << "PDP_ACD_READOUT_UNSUPPORTED model=" << name() << " offset=0x" << std::hex
                          << offset << " select=0x"
                          << (offset == readout ? word(readout_cfg) & 0x7f : word(rdvm_readout_cfg) & 0x1f)
                          << std::dec << '\n';
                tx.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
                return;
            }
            clock_detail::store(tx, value);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (!registers[static_cast<unsigned>(index)].writable) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto written = clock_detail::load(tx);
        if (offset == misc_status) {
            value = (value & misc_status_mask) | (written & ~misc_status_mask);
        } else if (offset == acdcr) {
            const bool start = (written & update_autocal) && !(value & update_autocal);
            value = written;
            if (start) {
                word(misc_status) &= ~calib_done;
                calibrate();
            }
        } else {
            value = written;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

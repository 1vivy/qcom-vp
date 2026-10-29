// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <cstdio>

namespace qcom_vp {

// RPMH CPRF block of the AOSS. UEFI CPRDxe programs a rail's voltage limits,
// its safe voltage, the VRM resource, and the enable bit here, and it sets one
// CTRL field through a read-modify-write.
//
// The model keeps the written configuration and applies no other effect.
// In silicon the CPRF block adjusts a rail voltage inside the programmed
// limits and reacts to CPR controller recommendations and ARC levels. This
// platform has ideal CPR sensors and no PMIC regulator, so no consumer can
// observe a voltage. The model therefore produces no voltage change, no
// completion, and no interrupt. Registers that no consumer reads stay
// unmodeled and keep their address error.
//
// Evidence: models/qcom/cpr/evidence.md.
class QcomRpmhCprf : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomRpmhCprf> target_socket{"target_socket"};

    explicit QcomRpmhCprf(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomRpmhCprf::transport);
        cold_reset();
    }

    void cold_reset() {
        for (auto& ctrl : ctrl_) ctrl = ctrl_reset;
        for (auto& settings : vrm_settings_) settings = vrm_reset;
        for (auto& enable : enable_) enable = 0;
        for (auto& row : voltage_limits_) row.fill(0);
        for (auto& row : safe_voltage_) row.fill(0);
    }

private:
    // The selected CPRDxe asserts on a CPRF id above 11, so the block decodes
    // twelve rows of 0x1000 bytes.
    static constexpr unsigned cprf_count = 12;
    static constexpr unsigned stride = 0x1000;
    static constexpr unsigned aperture = 0x10000;
    // CPRDxe writes one entry for each mode of the voltage plan, and the GFX
    // plan reaches 19. The model keeps 20 entries. See models/qcom/cpr/evidence.md.
    static constexpr unsigned limit_entries = 20;
    // Initial words from a private layout. No permitted source establishes the
    // silicon reset words. See "Functional policy" in models/qcom/cpr/evidence.md.
    static constexpr uint32_t ctrl_reset = 0x0000801du;
    static constexpr uint32_t vrm_reset = 0x00001100u;
    static constexpr uint32_t hw_version = 0x100b0000u;

    std::array<uint32_t, cprf_count> ctrl_{};
    std::array<uint32_t, cprf_count> vrm_settings_{};
    std::array<uint32_t, cprf_count> enable_{};
    std::array<std::array<uint32_t, limit_entries>, cprf_count> voltage_limits_{};
    std::array<std::array<uint32_t, limit_entries>, cprf_count> safe_voltage_{};

    struct Decoded {
        bool present = false;
        uint32_t* storage = nullptr;  // null when the register is read-only
        uint32_t value = 0;           // constant read value for a read-only register
        uint32_t mask = 0;            // writable field
    };

    // offsets within a CPRFm row. The row block of CPRF m starts at
    // 0x1000 + 0x1000*m, so global offset 0x1004 + 0x1000*m is local 0x4.
    static constexpr unsigned ctrl_offset = 0x4;
    static constexpr unsigned vrm_offset = 0x8;
    static constexpr unsigned limits_offset = 0xc;
    static constexpr unsigned safe_offset = 0x8c;
    static constexpr unsigned enable_offset = 0x118;

    Decoded decode(unsigned offset) {
        if (offset == 0) return Decoded{true, nullptr, hw_version, 0};
        const unsigned block = offset / stride;
        if (block == 0 || block > cprf_count) return Decoded{};
        const unsigned row = block - 1;
        const unsigned local = offset % stride;
        switch (local) {
        case ctrl_offset:
            return Decoded{true, &ctrl_[row], 0, 0x1ffffu};
        case vrm_offset:
            return Decoded{true, &vrm_settings_[row], 0, 0x1fffffffu};
        case enable_offset:
            return Decoded{true, &enable_[row], 0, 0x1u};
        default: break;
        }
        if (local >= limits_offset && local < limits_offset + 4 * limit_entries &&
            !((local - limits_offset) & 3))
            return Decoded{true, &voltage_limits_[row][(local - limits_offset) / 4], 0,
                0x1fff1fffu};
        if (local >= safe_offset && local < safe_offset + 4 * limit_entries &&
            !((local - safe_offset) & 3))
            return Decoded{true, &safe_voltage_[row][(local - safe_offset) / 4], 0, 0x1fffu};
        return Decoded{};
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3) || tx.get_address() >= aperture) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const unsigned offset = static_cast<unsigned>(tx.get_address());
        auto* p = tx.get_data_ptr();
        uint32_t value = tx.is_write() ?
            uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) |
                (uint32_t{p[3]} << 24) : 0;
        const Decoded decoded = decode(offset);
        tlm::tlm_response_status status = tlm::TLM_OK_RESPONSE;
        if (!decoded.present) {
            // An unimplemented offset of the block.
            status = tlm::TLM_ADDRESS_ERROR_RESPONSE;
        } else if (tx.is_write() && !decoded.storage) {
            status = tlm::TLM_COMMAND_ERROR_RESPONSE;
        } else if (tx.is_write() && (value & ~decoded.mask)) {
            status = tlm::TLM_COMMAND_ERROR_RESPONSE;
        } else if (tx.is_write()) {
            *decoded.storage = value;
        } else {
            value = decoded.storage ? *decoded.storage : decoded.value;
            for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        if (status != tlm::TLM_OK_RESPONSE) {
            char message[144];
            std::snprintf(message, sizeof(message), "Unsupported CPRF %s at +0x%x (value 0x%08x)",
                tx.is_write() ? "write" : "read", offset, value);
            SC_REPORT_WARNING(name(), message);
        }
        tx.set_response_status(status);
    }
};

} // namespace qcom_vp

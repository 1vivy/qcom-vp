// SPDX-License-Identifier: BSD-3-Clause
// Register definitions and HAL semantics derived from Qualcomm TF-A, commit
// f7787c3627e200933f02b93b1c25b8eb5bcf797d, drivers/qti/accesscontrol/vmidmt/.
// Copyright (c) 2026, Qualcomm Technologies, Inc. and/or its subsidiaries.
// See evidence.md for attribution, license, source coordinates, and limitations.
#pragma once

#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace qcom_vp {

// Configuration-plane model only. No downstream master or protection grant API.
class VmidMapper : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<VmidMapper> target_socket{"target_socket"};
    cci::cci_param<unsigned> entry_count{"entry_count", 0, "Required stream context count (1..128)"};
    cci::cci_param<unsigned> stream_id_bits{"stream_id_bits", 0, "Required stream ID width (1..15)"};
    cci::cci_param<unsigned> ssd_index_bits{"ssd_index_bits", std::numeric_limits<unsigned>::max(),
        "Required SSD index width (0..7)"};
    cci::cci_param<unsigned> input_address_bits{"input_address_bits", 0,
        "Required input address width (32,36,40,42,44,48)"};
    cci::cci_param<unsigned> vmid_count{"vmid_count", 0, "Required VMID count (1..32)"};
    cci::cci_param<bool> stream_match_support{"stream_match_support", false,
        "Platform must declare whether stream matching is available"};

    explicit VmidMapper(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &VmidMapper::transport);
    }

private:
    // These masks describe implemented fields, not the broad HWIO RMSK values.
    // Bit 8 in CR0 is retained because both selected XBLs write default 0x111.
    static constexpr uint32_t secure_control_mask = 0x3fff0df5;
    static constexpr uint32_t nonsecure_control_mask = 0x0fff0df5;
    static constexpr uint32_t auxiliary_mask = 0x70000013;
    static constexpr uint32_t context_attribute_mask = 0x30fcfbff;
    static constexpr uint32_t fault_mask = 0xc00000a6;

    struct ControlBank {
        uint32_t control = 1; // Virtual cold policy: client disabled, no domains granted.
        uint32_t bypass_vmid = 0;
        uint32_t auxiliary = 0;
        uint32_t clock_control = 0;
        uint32_t fault = 0;
    };
    struct StreamContext {
        uint32_t match = 0;
        uint32_t attributes = 0;
        uint32_t auxiliary = 0;
    };
    ControlBank secure_;
    ControlBank nonsecure_;
    std::array<StreamContext, 128> contexts_{};
    std::array<uint32_t, 4> security_state_{};
    uint32_t partition_ = 0;
    uint32_t permissions_ = 0;
    uint32_t id0_ = 0;
    uint32_t id1_ = 0;
    uint32_t id2_ = 0;
    uint32_t id5_ = 0;
    uint32_t stream_mask_ = 0;
    uint32_t permission_mask_ = 0;
    unsigned count_ = 0;
    unsigned ssd_words_ = 0;
    bool matching_ = false;

    void end_of_elaboration() override {
        count_ = entry_count.get_value();
        const auto sid_bits = stream_id_bits.get_value();
        const auto ssd_bits = ssd_index_bits.get_value();
        const auto vmids = vmid_count.get_value();
        if (!count_ || count_ > contexts_.size() || !sid_bits || sid_bits > 15 ||
            ssd_bits > 7 || !vmids || vmids > 32) {
            throw std::invalid_argument("VMID mapper requires explicit, supported capability geometry");
        }
        switch (input_address_bits.get_value()) {
        case 32: id2_ = 0; break;
        case 36: id2_ = 1; break;
        case 40: id2_ = 2; break;
        case 42: id2_ = 3; break;
        case 44: id2_ = 4; break;
        case 48: id2_ = 5; break;
        default: throw std::invalid_argument("VMID mapper input address width is not supported");
        }
        matching_ = stream_match_support.get_value();
        stream_mask_ = (uint32_t{1} << sid_bits) - 1;
        permission_mask_ = vmids == 32 ? UINT32_MAX : (uint32_t{1} << vmids) - 1;
        ssd_words_ = ssd_bits < 5 ? 1 : (1U << (ssd_bits - 5));
        id0_ = (matching_ ? 0x08000000U : 0U) | (sid_bits << 9) | count_;
        id1_ = ssd_bits << 8;
        id5_ = vmids;
    }

    // Encoding 1 is absent from the HAL NSCFG, allocation, and transient enums.
    static bool valid_control(uint32_t value, bool secure) {
        return !(value & ~(secure ? secure_control_mask : nonsecure_control_mask)) &&
            (!secure || ((value >> 28) & 3) != 1) && ((value >> 6) & 3) != 1 &&
            ((value >> 26) & 3) != 1 && ((value >> 24) & 3) != 1 &&
            ((value >> 16) & 15) <= 7;
    }

    bool access_bank(ControlBank& bank, uint64_t offset, bool secure, bool write, uint32_t& value) {
        switch (offset) {
        case 0:
            if (write) {
                if (!valid_control(value, secure) || (!matching_ && (value & 0x00200000))) return false;
                bank.control = value;
            } else value = bank.control;
            return true;
        case 8:
            // HWIO CR2 RMSK disagrees with BPVMID. Selected XBL reads low eight bits.
            if (write) {
                if (value >= id5_) return false;
                bank.bypass_vmid = value;
            } else value = bank.bypass_vmid;
            return true;
        case 0x10:
            if (write) bank.auxiliary = value & auxiliary_mask;
            else value = bank.auxiliary;
            return true;
        case 0x48:
            if (write) return false; // No established status-write semantics.
            value = bank.fault & fault_mask;
            return true;
        case 0x4c:
            // Both HALs explicitly clear the sticky status through RESTORE=0.
            if (write) {
                if (value) return false; // Fault injection/restore is not implemented.
                bank.fault = 0;
            } else value = bank.fault & fault_mask;
            return true;
        case 0x40: case 0x44: case 0x50: case 0x54: case 0x58:
            if (write) return false;
            value = 0; // No attached master can produce a data-plane fault.
            return true;
        case 0x90:
            if (write) {
                if (value & ~1U) return false;
                bank.clock_control = value;
            } else value = bank.clock_control;
            return true;
        default: return false;
        }
    }

    bool access(uint64_t offset, bool write, uint32_t& value) {
        switch (offset) {
        case 0x20: case 0x24: case 0x28: case 0x34:
            if (write) return false;
            value = offset == 0x20 ? id0_ : offset == 0x24 ? id1_ : offset == 0x28 ? id2_ : id5_;
            return true;
        case 4:
            if (write) {
                if ((value & ~0x0100ff00U) || ((value >> 8) & 255) > count_) return false;
                partition_ = value;
            } else value = partition_;
            return true;
        case 0x9c:
            if (write) permissions_ = value & permission_mask_;
            else value = permissions_;
            return true;
        default: break;
        }
        if (offset >= 0x80 && offset < 0x90) {
            const auto index = (offset - 0x80) / 4;
            if (index >= ssd_words_) return false;
            // Ordinary retained bits. The HAL itself implements clear-only config_ssdt
            // with a read/AND/write, but config_ctx also sets individual NS bits.
            if (write) security_state_[index] = value;
            else value = security_state_[index];
            return true;
        }
        if (offset >= 0x800 && offset < 0xc00) {
            const auto index = (offset - 0x800) / 4;
            if (!matching_ || index >= count_) return false;
            const auto mask = 0x80000000U | stream_mask_ | (stream_mask_ << 16);
            if (write) contexts_[index].match = value & mask;
            else value = contexts_[index].match;
            return true;
        }
        if (offset >= 0xc00 && offset < 0xe00) {
            const auto index = (offset - 0xc00) / 4;
            if (index >= count_) return false;
            if (write) {
                // The init path writes TYPE=1. Configuration preserves TYPE with
                // OUTMI's 0x30fcfbff mask. No other nonzero TYPE is established.
                const auto masked = value & (context_attribute_mask | 0x00030000U);
                if (((masked >> 16) & 3) > 1 || ((masked >> 18) & 3) == 1 ||
                    ((masked >> 22) & 3) == 1 || ((masked >> 20) & 3) == 1 ||
                    ((masked >> 28) & 3) == 1 || ((masked >> 12) & 15) > 7 ||
                    (masked & 255) >= id5_) return false;
                contexts_[index].attributes = masked;
            } else value = contexts_[index].attributes;
            return true;
        }
        if (offset >= 0xe00 && offset < 0x1000) {
            const auto index = (offset - 0xe00) / 4;
            if (index >= count_) return false;
            if (write) contexts_[index].auxiliary = value & auxiliary_mask;
            else value = contexts_[index].auxiliary;
            return true;
        }
        if (offset < 0x400) return access_bank(secure_, offset, true, write, value);
        if (offset < 0x800) return access_bank(nonsecure_, offset - 0x400, false, write, value);
        return false;
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
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
        const auto* data = tx.get_data_ptr();
        uint32_t value = 0;
        if (tx.is_write()) {
            value = uint32_t{data[0]} | (uint32_t{data[1]} << 8) |
                (uint32_t{data[2]} << 16) | (uint32_t{data[3]} << 24);
        }
        if (!access(tx.get_address(), tx.is_write(), value)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            for (unsigned i = 0; i < 4; ++i) tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (i * 8));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

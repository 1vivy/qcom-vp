// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// One security frame of an NCC last-level-cache MPAM memory-system component.
// The partition and monitor words use the Arm MPAM memory-map offsets, such as
// MPAMCFG_PART_SEL 0x0100, MPAMCFG_CMAX 0x0108, MPAMCFG_PRI 0x0400, and
// MSMON_CFG_MON_SEL 0x0800. The CPUCP consumers are in
// models/qcom/sm88xx/cpucp/mpam_evidence.md.
//
// The identification words and the initial partition and monitor values are
// model-supplied. No firmware instruction reads an identification word, and no
// permitted source establishes these values.
//
// The frame retains partition and monitor configuration. PART_SEL selects the partition
// for CMAX, PRI, CPBM, and the implementation SCID word. MON_SEL selects the monitor for
// the CSU and MBWU filter and control words. No virtual cache or memory path reports
// partition occupancy or bandwidth, so every counter register and capture event fails.
class QcomNccMpamMsc : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomNccMpamMsc> target_socket{"target_socket"};
    cci::cci_param<bool> secure{"secure", false, "Secure frame with MPAMF_SIDR"};
    cci::cci_param<unsigned> monitors{"monitors", 0, "CSU and MBWU monitors in this frame"};
    cci::cci_param<unsigned> filter_partid_bits{"filter_partid_bits", 0, "Monitor filter PARTID width"};

    static constexpr unsigned partitions = 64; // MPAMF_IDR.PARTID_MAX is 0x3f.

    explicit QcomNccMpamMsc(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomNccMpamMsc::transport);
    }

private:
    struct Partition {
        std::uint32_t cmax = 0xf;
        std::uint32_t pri = 0x30000;
        std::uint32_t cpbm = 0xfff;
        std::uint32_t scid = 1;
    };
    struct Monitor {
        std::uint32_t csu_filter = 0;
        std::uint32_t csu_control = 0x43;
        std::uint32_t mbwu_filter = 0;
        std::uint32_t mbwu_control = 0x42;
    };
    std::array<Partition, partitions> partition_{};
    std::array<Monitor, 16> monitor_{};
    std::uint32_t partition_select_ = 0;
    std::uint32_t monitor_select_ = 0;
    std::uint32_t error_control_ = 0;
    std::uint32_t error_status_ = 0;

    void before_end_of_elaboration() override {
        const auto count = monitors.get_value();
        if (count != 4 && count != 16) throw std::invalid_argument("The MPAM frame requires 4 or 16 monitors");
        const auto bits = filter_partid_bits.get_value();
        if (bits != 4 && bits != 6) throw std::invalid_argument("The MPAM filter PARTID width must be 4 or 6");
        secure.lock();
        monitors.lock();
        filter_partid_bits.lock();
    }

    // Identification words. CSUMON_IDR and MBWUMON_IDR report the frame's monitor count.
    bool identity(std::uint64_t offset, std::uint32_t& value) const {
        switch (offset) {
        case 0x00: value = 0x7b01003f; return true; // MPAMF_IDR[31:0]
        case 0x04: value = 0x80; return true;       // MPAMF_IDR[63:32]
        case 0x08:
            if (!secure.get_value()) return false;
            value = 0x0001000f; // MPAMF_SIDR
            return true;
        case 0x18: value = 0x00130070; return true; // MPAMF_IIDR
        case 0x20: value = 0x11; return true;       // MPAMF_AIDR
        case 0x28: value = 1; return true;          // MPAMF_IMPL_IDR
        case 0x30: value = 0xc; return true;        // MPAMF_CPOR_IDR: 12 portions
        case 0x38: value = 4; return true;          // MPAMF_CCAP_IDR: four CMAX bits
        case 0x40: value = 0; return true;          // MPAMF_MBW_IDR
        case 0x48: value = 0x00230000; return true; // MPAMF_PRI_IDR
        case 0x50: value = 0; return true;          // MPAMF_PARTID_NRW_IDR
        case 0x80: value = 0xc0030000; return true; // MPAMF_MSMON_IDR
        case 0x88: value = 0xc0000000 | monitors.get_value(); return true; // MPAMF_CSUMON_IDR
        case 0x90: value = 0xd00b0000 | monitors.get_value(); return true; // MPAMF_MBWUMON_IDR
        default: return false;
        }
    }

    // Returns the storage and its writable mask, or nullptr for an undeclared word.
    std::uint32_t* word(std::uint64_t offset, std::uint32_t& writable) {
        const std::uint32_t filter = (1u << 16) | ((1u << filter_partid_bits.get_value()) - 1);
        auto& part = partition_[partition_select_];
        auto& mon = monitor_[monitor_select_];
        switch (offset) {
        case 0x0f0: writable = 1; return &error_control_;
        case 0x0f8: writable = 0x8f0100ff; return &error_status_;
        case 0x100: writable = 0xff; return &partition_select_;
        case 0x108: writable = 0xf; return &part.cmax;
        case 0x400: writable = 0x30000; return &part.pri;
        case 0x800: writable = monitors.get_value() - 1; return &monitor_select_;
        case 0x810: writable = filter; return &mon.csu_filter;
        case 0x818: writable = 0xf0030000; return &mon.csu_control;
        case 0x820: writable = 0xc0000000 | filter; return &mon.mbwu_filter;
        case 0x828: writable = 0xfd0b8000; return &mon.mbwu_control;
        case 0x1000: writable = 0xfff; return &part.cpbm;
        case 0x3000: writable = 0x3f; return &part.scid;
        default: return nullptr;
        }
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const unsigned length = tx.get_data_length();
        const auto offset = tx.get_address();
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || (length != 4 && length != 8) || tx.get_streaming_width() < length ||
            (offset & (length - 1))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        unsigned char* data = tx.get_data_ptr();
        // MPAMF_IDR is the only 64-bit register outside the unsupported counters.
        if (length == 8) {
            if (offset != 0 || tx.is_write()) {
                tx.set_response_status(offset == 0 ? tlm::TLM_COMMAND_ERROR_RESPONSE
                                                   : tlm::TLM_BURST_ERROR_RESPONSE);
                return;
            }
            std::uint32_t low = 0, high = 0;
            identity(0, low);
            identity(4, high);
            const std::uint64_t value = (std::uint64_t{high} << 32) | low;
            for (unsigned i = 0; i < 8; ++i) data[i] = static_cast<unsigned char>(value >> (8 * i));
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        std::uint32_t input = 0;
        for (unsigned i = 0; i < 4; ++i) input |= std::uint32_t{data[i]} << (8 * i);
        std::uint32_t output = 0;
        tlm::tlm_response_status status = tlm::TLM_OK_RESPONSE;
        std::uint32_t writable = 0;
        if (identity(offset, output)) {
            if (tx.is_write()) status = tlm::TLM_COMMAND_ERROR_RESPONSE;
        } else if (auto* storage = word(offset, writable)) {
            if (tx.is_read()) {
                output = *storage;
            } else if ((offset == 0x100 && input >= partitions) ||
                       (offset == 0x800 && input >= monitors.get_value())) {
                // A selector beyond PARTID_MAX or NUM_MON has no configuration storage.
                status = tlm::TLM_COMMAND_ERROR_RESPONSE;
            } else {
                // Read-only and reserved fields ignore written values.
                *storage = (*storage & ~writable) | (input & writable);
            }
        } else if (offset == 0x808) {
            // MSMON_CAPT_EVNT reads zero. A capture needs the unsupported counter values.
            if (tx.is_write() && input) status = tlm::TLM_COMMAND_ERROR_RESPONSE;
        } else {
            status = tlm::TLM_ADDRESS_ERROR_RESPONSE;
        }
        if (status == tlm::TLM_OK_RESPONSE && tx.is_read())
            for (unsigned i = 0; i < 4; ++i) data[i] = static_cast<unsigned char>(output >> (8 * i));
        tx.set_response_status(status);
    }
};

} // namespace qcom_vp

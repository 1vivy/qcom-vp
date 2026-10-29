// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "zonda_pll.h"
#include <sysc/kernel/sc_spawn.h>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>

namespace qcom_vp {

// Memory-mapped view of one core's Arm AMUv1 activity monitors.
// The counter and type offsets follow the Arm AMUv1 activity-monitor page.
// The CPUCP consumer copies group 0 (+0x000, four counters) and group 1
// (+0x100, eight counters) into CPUCP SRAM. See models/qcom/sm88xx/cpucp/amu_evidence.md.
//
// No selected firmware instruction reads an enable word, a control word, or an
// identification word. Their offsets come from a private layout, and their
// values are model-supplied.
//
// Counter 0 counts core cycles at the lane PLL output. Counter 1 counts at the
// constant reference rate. Both count only while their enable bit is set. No
// source supplies retired instructions, memory stalls, or the auxiliary events,
// so enabling counter 2, counter 3, or any group-1 counter fails.
class QcomCoreAmu : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomCoreAmu> target_socket{"target_socket"};
    cci::cci_param<std::string> pll_path{"pll_path", "", "Lane PLL that clocks this core"};
    cci::cci_param<std::uint64_t> reference_hz{
        "reference_hz", 19200000, "Constant-frequency counter rate"};

    explicit QcomCoreAmu(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomCoreAmu::transport);
    }

    void cold_reset() {
        fold();
        counters_.fill(0);
        group1_types_.fill(0);
        enabled0_ = 0;
        control_ = 0;
    }

private:
    QcomZondaPll* pll_ = nullptr;
    std::array<std::uint64_t, 12> counters_{};
    std::array<std::uint32_t, 8> group1_types_{};
    std::uint32_t enabled0_ = 0;
    std::uint32_t control_ = 0;
    std::uint64_t core_hz_ = 0;
    sc_core::sc_time since_ = sc_core::SC_ZERO_TIME;
    // Fractional cycles carried across folds, in units of hz * picoseconds.
    std::array<unsigned __int128, 2> remainder_{};

    void before_end_of_elaboration() override {
        auto* object = sc_core::sc_find_object(pll_path.get_value().c_str());
        if (!object && get_parent_object())
            object = sc_core::sc_find_object(
                (std::string(get_parent_object()->name()) + "." + pll_path.get_value()).c_str());
        pll_ = dynamic_cast<QcomZondaPll*>(object);
        if (!pll_) throw std::invalid_argument("The AMU requires its lane PLL: " + pll_path.get_value());
        if (!reference_hz.get_value()) throw std::invalid_argument("The AMU reference rate must be nonzero");
        // A PLL change closes the counting interval at the previous core rate.
        sc_core::sc_spawn_options options;
        options.spawn_method();
        options.dont_initialize();
        options.set_sensitivity(&pll_->state_changed);
        sc_core::sc_spawn([this] { fold(); }, sc_core::sc_gen_unique_name("clock_changed"), &options);
    }
    void start_of_simulation() override { core_hz_ = pll_->main_output_hz(); }

    void advance(unsigned index, std::uint64_t hz, std::uint64_t picoseconds) {
        const unsigned __int128 total = static_cast<unsigned __int128>(hz) * picoseconds + remainder_[index];
        constexpr unsigned __int128 scale = 1000000000000ULL;
        counters_[index] += static_cast<std::uint64_t>(total / scale);
        remainder_[index] = total % scale;
    }
    // Add the counts since the last fold at the rates that applied in that interval.
    void fold() {
        const auto now = sc_core::sc_time_stamp();
        const auto picoseconds = static_cast<std::uint64_t>((now - since_).to_seconds() * 1e12 + 0.5);
        if (enabled0_ & 1) advance(0, core_hz_, picoseconds);
        if (enabled0_ & 2) advance(1, reference_hz.get_value(), picoseconds);
        since_ = now;
        if (pll_) core_hz_ = pll_->main_output_hz();
    }

    static std::uint32_t constant(std::uint64_t offset, bool& found) {
        found = true;
        switch (offset) {
        case 0x400: return 0x11;
        case 0x404: return 0x4004;
        case 0x408: return 0x08;
        case 0x40c: return 0x4005;
        case 0xce0: return 0x804;
        case 0xe00: return 0x11003f0b;
        case 0xe08: case 0xfa8: case 0xfac: return 0;
        case 0xfbc: return 0x47700a66;
        case 0xfcc: return 0x16;
        case 0xfd0: case 0xfe0: case 0xfe4: case 0xfe8: case 0xfec: return 0;
        case 0xff0: return 0x0d;
        case 0xff4: return 0x90;
        case 0xff8: return 0x05;
        case 0xffc: return 0xb1;
        default: found = false; return 0;
        }
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const unsigned length = tx.get_data_length();
        const auto address = tx.get_address();
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || (length != 4 && length != 8) || tx.get_streaming_width() < length ||
            (address & (length - 1))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        unsigned char* data = tx.get_data_ptr();
        std::uint64_t input = 0;
        for (unsigned i = 0; i < length; ++i) input |= std::uint64_t{data[i]} << (8 * i);
        std::uint64_t output = 0;
        const auto reply = [&](tlm::tlm_response_status status) {
            if (status == tlm::TLM_OK_RESPONSE && tx.is_read())
                for (unsigned i = 0; i < length; ++i) data[i] = static_cast<unsigned char>(output >> (8 * i));
            tx.set_response_status(status);
        };

        // Event counters accept 64-bit words and aligned 32-bit halves.
        const bool group0 = address < 0x20;
        const bool group1 = address >= 0x100 && address < 0x140;
        if (group0 || group1) {
            fold();
            const unsigned index = group0 ? static_cast<unsigned>(address / 8)
                                          : 4 + static_cast<unsigned>((address - 0x100) / 8);
            const unsigned shift = static_cast<unsigned>(address & 4) * 8;
            const auto lane = length == 8 ? ~UINT64_C(0) : UINT64_C(0xffffffff) << shift;
            if (tx.is_read()) output = counters_[index] >> shift;
            else counters_[index] = (counters_[index] & ~lane) | ((input << shift) & lane);
            return reply(tlm::TLM_OK_RESPONSE);
        }
        // All other registers are 32 bits wide. An undeclared offset fails first.
        bool fixed_found = false;
        const auto fixed = constant(address, fixed_found);
        const bool declared = fixed_found || (address >= 0x480 && address < 0x4a0) || address == 0xc00 ||
                              address == 0xc20 || address == 0xc04 || address == 0xc24 || address == 0xe04;
        if (!declared) return reply(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        if (length != 4) return reply(tlm::TLM_BURST_ERROR_RESPONSE);
        const auto value = static_cast<std::uint32_t>(input);
        if (address >= 0x480 && address < 0x4a0) {
            auto& type = group1_types_[(address - 0x480) / 4];
            if (tx.is_read()) output = type;
            else type = value;
            return reply(tlm::TLM_OK_RESPONSE);
        }
        switch (address) {
        case 0xc00: // AMCNTENSET0_EL0
        case 0xc20: // AMCNTENCLR0_EL0
            if (tx.is_read()) {
                output = enabled0_;
                return reply(tlm::TLM_OK_RESPONSE);
            }
            if (value & ~UINT32_C(0xf)) return reply(tlm::TLM_COMMAND_ERROR_RESPONSE);
            if (address == 0xc00 && (value & 0xc)) {
                // No source supplies retired instructions or memory stalls.
                return reply(tlm::TLM_COMMAND_ERROR_RESPONSE);
            }
            fold();
            if (address == 0xc00) enabled0_ |= value;
            else enabled0_ &= ~value;
            return reply(tlm::TLM_OK_RESPONSE);
        case 0xc04: // AMCNTENSET1_EL0
        case 0xc24: // AMCNTENCLR1_EL0
            if (tx.is_read()) return reply(tlm::TLM_OK_RESPONSE);
            // No source supplies the auxiliary events. Clearing disabled counters has no effect.
            return reply(address == 0xc04 && value ? tlm::TLM_COMMAND_ERROR_RESPONSE : tlm::TLM_OK_RESPONSE);
        case 0xe04: // AMCR_EL0
            if (tx.is_read()) output = control_;
            else control_ = value;
            return reply(tlm::TLM_OK_RESPONSE);
        default:
            break;
        }
        if (tx.is_write()) return reply(tlm::TLM_COMMAND_ERROR_RESPONSE);
        output = fixed;
        return reply(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <algorithm>

namespace qcom_vp {

// This version has one target thread and separate configuration tables.
// The level request block at +0x2600 has its own owner.
// Register evidence: models/qcom/sm88xx/pdp/evidence.md.
// Corner application reads the programmed rows and the voltage control word.
// Measurements use explicit ideal sensor clocks, not analog simulation.
class CprhTarget : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<CprhTarget> target_socket{"target_socket"};
    tlm_utils::multi_passthrough_target_socket<CprhTarget> table_socket{"table_socket"};
    cci::cci_param<std::uint32_t> reference_clock_hz{
        "reference_clock_hz", 0, "Reference clock for virtual sensor measurements"};
    cci::cci_param<std::uint32_t> oscillator_clock_hz{
        "oscillator_clock_hz", 0, "Nominal virtual oscillator clock without aging drift"};
    cci::cci_param<unsigned> ownership_word_count{
        "ownership_word_count", 0, "Number of ordinary sensor ownership words"};

    SC_HAS_PROCESS(CprhTarget);
    explicit CprhTarget(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &CprhTarget::control_transport);
        table_socket.register_b_transport(this, &CprhTarget::table_transport);
        SC_METHOD(measure);
        sensitive << measurement_timer_;
        dont_initialize();
    }
    void end_of_elaboration() override {
        if (ownership_word_count.get_value() > 32)
            throw std::invalid_argument("CPRH supports at most 32 ownership words");
    }

    // The functional voltage uses the larger initial or floor step count.
    // The remaining corner fields retain their programmed values.
    bool can_apply_corner(unsigned corner) const {
        return corner < levels_.size() && programmed_[corner] &&
            control_programmed_ && (voltage_control_ & 1) &&
            ((voltage_control_ >> 25) & 0xf);
    }
    bool apply_corner(unsigned corner) {
        if (!can_apply_corner(corner)) return false;
        previous_corner_ = applied_corner_;
        applied_corner_ = corner;
        applied_level_ = levels_[corner];
        applied_control_ = voltage_control_;
        effective_steps_ = std::max(applied_level_ & 0xffu, (applied_level_ >> 8) & 0xffu);
        effective_voltage_mv_ = (((applied_control_ >> 1) & 0x3ffu) + effective_steps_) *
            ((applied_control_ >> 25) & 0xfu);
        applied_ = true;
        return true;
    }
    bool corner_ready(unsigned corner) const {
        return can_apply_corner(corner) && applied_ && applied_corner_ == corner &&
            applied_level_ == levels_[corner] && applied_control_ == voltage_control_;
    }
    bool has_effective_corner() const { return applied_; }
    unsigned effective_corner() const { return applied_corner_; }
    unsigned effective_steps() const { return effective_steps_; }
    unsigned effective_voltage_mv() const { return effective_voltage_mv_; }

private:
    static constexpr std::uint32_t config_offsets_[]{
        0x004, 0x00c, 0x010, 0x014, 0x020, 0x0e0, 0x120,
        0x700, 0x704, 0x70c, 0x77c, 0x7a4, 0x7a8, 0x7f8, 0x808, 0x80c, 0x81c,
        0x2000, 0x2004, 0x3a00, 0x3a2c, 0x3b20, 0x3b24,
        0x3b50, 0x3b54, 0x3b58, 0x3c84, 0x3c8c, 0x3c94,
    };
    std::array<std::uint32_t, std::size(config_offsets_)> configuration_{};
    std::array<std::uint32_t, 16> gates_{}, selectors_{}, targets_{};
    // The firmware uses a 0xa0-byte row bank for each target thread.
    std::array<std::uint32_t, 40> periods_{}, levels_{};
    std::array<bool, 40> programmed_{};
    bool control_programmed_ = false, applied_ = false, loop_enabled_ = false;
    std::uint32_t voltage_control_ = 0, applied_level_ = 0, applied_control_ = 0;
    unsigned applied_corner_ = 0, effective_steps_ = 0, effective_voltage_mv_ = 0;
    unsigned previous_corner_ = 0;
    std::array<std::uint32_t, 3> result_{};
    std::uint32_t measurement_status_ = 0, irq_status_ = 0;
    sc_core::sc_event measurement_timer_;

    std::uint32_t* word(std::uint64_t offset) {
        if (offset >= 0xa0 && offset <= 0xdc) return &gates_[(offset - 0xa0) / 4];
        if (offset >= 0x714 && offset <= 0x750) return &selectors_[(offset - 0x714) / 4];
        if (offset >= 0x840 && offset <= 0x87c) return &targets_[(offset - 0x840) / 4];
        if (offset >= 0x2008 && offset <= 0x20a4) return &periods_[(offset - 0x2008) / 4];
        if (offset >= 0x3000 && offset <= 0x309c) return &levels_[(offset - 0x3000) / 4];
        const auto entry = std::lower_bound(std::begin(config_offsets_), std::end(config_offsets_), offset);
        if (entry == std::end(config_offsets_) || *entry != offset) return nullptr;
        return &configuration_[entry - std::begin(config_offsets_)];
    }
    void schedule_measurement() {
        measurement_timer_.cancel();
        if (!loop_enabled_ || (*word(0x80c) & 0x80000000u) ||
            (*word(0x81c) & 4) || !reference_clock_hz.get_value()) return;
        const auto cycles = std::max(*word(0xc), 1u);
        auto delay = sc_core::sc_time(
            double(cycles) / reference_clock_hz.get_value(), sc_core::SC_SEC);
        if (delay == sc_core::SC_ZERO_TIME) delay = sc_core::sc_get_time_resolution();
        measurement_timer_.notify(delay);
    }
    void measure() {
        measurement_timer_.cancel();
        if (!loop_enabled_) return;
        measurement_status_ = 1;
        result_[0] = 1;
        irq_status_ &= ~0xfu;
        if (!applied_ || !reference_clock_hz.get_value() ||
            !oscillator_clock_hz.get_value() || (*word(0x80c) & 0x80000000u)) return;
        const auto excluded = *word(0xe0) | *word(0x120);
        unsigned first = 32, last = 32;
        // Reset routing selects thread zero, including the separate aging sensor.
        for (unsigned sensor = 0; sensor < 32; ++sensor) {
            if (excluded & (1u << sensor)) continue;
            if (first == 32) first = sensor;
            last = sensor;
        }
        if (first == 32) return;
        const auto ro_mask = *word(0x704) | *word(0x80c);
        unsigned minq = 0x1000, maxq = 0, minro = 0, maxro = 0;
        bool found = false;
        for (unsigned ro = 0; ro < gates_.size(); ++ro) {
            if (ro_mask & (1u << ro)) continue;
            const auto gate = (*word(0x700) & 0x400) ?
                (targets_[ro] >> 12) & 0x3ffu : gates_[ro] & 0x3ffu;
            if (!gate) continue;
            // A regulated ideal sensor matches its target at the applied voltage.
            // Manual measurements use the declared clock without aging drift.
            const auto quotient = (voltage_control_ & 1) ? targets_[ro] & 0xfffu :
                std::uint64_t{gate + 1} * oscillator_clock_hz.get_value() /
                    reference_clock_hz.get_value();
            if (!quotient || quotient > 0xfff) return;
            if (quotient < minq) { minq = quotient; minro = ro; }
            if (!found || quotient >= maxq) { maxq = quotient; maxro = ro; }
            found = true;
        }
        if (!found) return;
        result_[0] = 0;
        result_[1] = minq | (maxq << 12) | (minro << 24) | (maxro << 28);
        result_[2] = (*word(0x14) & 0xfffu) | (first << 16) | (last << 24);
        measurement_status_ = (*word(4) & 0xc0) == 0xc0 ? 2 : 0;
        irq_status_ |= 5;
        schedule_measurement();
    }
    void transport(tlm::tlm_generic_payload& tx, std::uint64_t base, std::uint64_t size) {
        if (!clock_detail::valid(tx)) return;
        if (tx.get_address() >= size) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const auto offset = base + tx.get_address();
        if (offset >= 0x200 && offset < 0x200 + 4 * ownership_word_count.get_value()) {
            if (tx.is_write()) {
                if (clock_detail::load(tx)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
            } else clock_detail::store(tx, 0);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset == 0x800 || offset == 0x820) {
            if (tx.is_write()) {
                const auto command = clock_detail::load(tx);
                if (offset == 0x820) irq_status_ &= ~command;
                else {
                    if (*word(0x10) & 0x10000000u) irq_status_ = 0;
                    measure();
                }
            } else clock_detail::store(tx, 0);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset == 8 || (offset >= 0x810 && offset <= 0x818)) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            clock_detail::store(tx, offset == 8 ? measurement_status_ : result_[(offset - 0x810) / 4]);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset == 0x824 || offset == 0x3a04) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            std::uint32_t status = offset == 0x824 ? irq_status_ : 0;
            if (offset == 0x3a04 && applied_) {
                status = applied_corner_ | ((effective_voltage_mv_ & 0xfffu) << 6) |
                    (previous_corner_ << 18) | (std::uint32_t{loop_enabled_} << 29);
            }
            clock_detail::store(tx, status);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        auto* value = word(offset);
        if (!value) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            clock_detail::store(tx, *value);
        } else {
            const auto requested = clock_detail::load(tx);
            const bool thread_was_disabled = offset == 0x80c && (*value & 0x80000000u);
            // The override register shares its word with read-only timeout flags.
            *value = offset == 0x3c94 ? requested & 0xfc000000u : requested;
            if (offset == 4) {
                const bool was_enabled = loop_enabled_;
                loop_enabled_ = requested & 1;
                if (!loop_enabled_) {
                    measurement_timer_.cancel();
                    measurement_status_ &= ~1u;
                    result_[0] &= ~1u;
                } else if (!was_enabled) measure();
            }
            if (offset == 0x81c) schedule_measurement();
            if (offset == 0x80c && (requested & 0x80000000u)) {
                measurement_timer_.cancel();
                measurement_status_ &= ~1u;
                result_[0] &= ~1u;
            } else if (thread_was_disabled && loop_enabled_) {
                measure();
            }
            if (offset >= 0x3000 && offset <= 0x309c)
                programmed_[(offset - 0x3000) / 4] = true;
            if (offset == 0x3a00) {
                voltage_control_ = requested;
                control_programmed_ = true;
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void control_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        transport(tx, 0, 0x2600);
    }
    void table_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        transport(tx, 0x3000, 0x1000);
    }
};

} // namespace qcom_vp

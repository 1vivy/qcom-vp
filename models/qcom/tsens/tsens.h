// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {
// TSENS v2 SROT/TM pair. All socket addresses are relative to their own window.
class QcomTsensV2 : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomTsensV2> srot_socket{"srot_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomTsensV2> tm_socket{"tm_socket"};
    cci::cci_param<unsigned> sensor_count{"sensor_count", 0, "Implemented channels, 1..16"};
    cci::cci_param<unsigned> conversion_us{"conversion_us", 30, "Virtual conversion time per enabled channel"};
    cci::cci_param<int> temperature_decic{"temperature_decic", 250, "Virtual environment, not a measured temperature"};
    cci::cci_param<unsigned> sleep_clock_hz{"sleep_clock_hz", 32768, "Virtual watchdog sleep-clock frequency"};
    explicit QcomTsensV2(sc_core::sc_module_name name) : sc_module(name) {
        srot_socket.register_b_transport(this, &QcomTsensV2::srot_transport);
        tm_socket.register_b_transport(this, &QcomTsensV2::tm_transport);
        cold_reset();
    }
    void cold_reset() {
        srot_ = {};
        tm_ = {};
        calibrated_ = 0;
        pending_ = false;
        sample_valid_ = false;
        sample_temperature_ = 0;
        zero_c_ = false;
        next_sample_at_ = sc_core::SC_ZERO_TIME;
        // Explicit virtual cold-reset policy: disabled controller, masked interrupts.
        tm_[0x10 / 4] = 0xffffffff;
        tm_[0x1c / 4] = 0xc000ffff;
    }
private:
    std::array<std::uint32_t, 0x154 / 4> srot_{};
    std::array<std::uint32_t, 0xec / 4> tm_{};
    std::uint32_t calibrated_ = 0;
    bool pending_ = false;
    bool sample_valid_ = false;
    int sample_temperature_ = 0;
    bool zero_c_ = false;
    sc_core::sc_time next_sample_at_ = sc_core::SC_ZERO_TIME;
    void end_of_elaboration() override {
        if (!sensor_count.get_value() || sensor_count.get_value() > 16 || !conversion_us.get_value() ||
            !sleep_clock_hz.get_value() ||
            temperature_decic.get_value() < -2048 || temperature_decic.get_value() > 2047)
            throw std::invalid_argument("Invalid TSENS channel count, conversion time, or temperature");
    }
    std::uint32_t channels() const { return (1u << sensor_count.get_value()) - 1; }
    std::uint32_t enabled() const { return (srot_[1] >> 3) & channels(); }
    bool converting() const {
        // Internal oscillator only. Reset holds the converter, not its configuration.
        return (srot_[1] & 7) == 1 && !(srot_[1] & ((1u << 19) | (1u << 20))) &&
            (srot_[1] & (1u << 21)) && enabled() &&
            (enabled() & calibrated_) == enabled();
    }
    sc_core::sc_time period() const {
        // AUTO_ADJUST_PERIOD_EN selects MAIN_MEASURE_PERIOD in this explicitly
        // active environment. There is no sleep-state producer in either platform.
        const unsigned code = srot_[2] & 0xff;
        if (!code || code == 0xff) return sc_core::SC_ZERO_TIME;
        return sc_core::sc_time((code >> 6) * 250000.0 + (code & 63) * 1950.0,
                                sc_core::SC_US);
    }
    sc_core::sc_time conversion_time() const {
        unsigned count = 0;
        for (auto bits = enabled(); bits; bits >>= 1) count += bits & 1;
        return sc_core::sc_time(double(count) * conversion_us.get_value(), sc_core::SC_US);
    }
    bool watchdog_budget_valid() const {
        const auto control = srot_[0x140 / 4];
        if (!(control & 1) || !(srot_[1] & 1) || (srot_[1] & 2)) return true;
        const unsigned ticks = (control >> 2) & 0xffff;
        if (!ticks || (control & ((1u << 19) | 2))) return false;
        // Bounded no-expiry substitute: each admitted scan must finish before
        // the watchdog deadline. Unknown timeout/reset behavior is not invented.
        return converting() && (srot_[2] & 0xff) != 0xff &&
            conversion_time() + period() <
                sc_core::sc_time(double(ticks) / sleep_clock_hz.get_value(), sc_core::SC_SEC);
    }
    void restart() {
        pending_ = converting();
        sample_valid_ = false;
        next_sample_at_ = sc_core::sc_time_stamp() + conversion_time();
    }
    void update_sample() {
        if (!pending_ || !converting() || sc_core::sc_time_stamp() < next_sample_at_) return;
        sample_valid_ = true;
        sample_temperature_ = temperature_decic.get_value();
        if ((srot_[2] & 0xff) == 0xff) {
            pending_ = false; // One shot retains its completed sample until reconfigured.
        } else {
            const auto cadence = conversion_time() + period();
            const auto elapsed = sc_core::sc_time_stamp() - next_sample_at_;
            next_sample_at_ += cadence * (static_cast<double>(elapsed.value() / cadence.value()) + 1);
        }
    }
    bool ready() const { return sample_valid_ && converting(); }
    void update_zero_c() {
        const auto control = srot_[0x18 / 4];
        if (!ready() || !(control & 1) || !((control >> 1) & enabled())) return;
        const auto thresholds = srot_[0x1c / 4];
        if (sample_temperature_ < signed12(thresholds >> 12)) zero_c_ = true;
        else if (sample_temperature_ > signed12(thresholds)) zero_c_ = false;
    }
    static bool valid(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        else if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
                 (tx.get_address() & 3)) tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        else if (!tx.is_read() && !tx.is_write()) tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        else return true;
        return false;
    }
    static std::uint32_t load(const tlm::tlm_generic_payload& tx) {
        const auto* p = tx.get_data_ptr();
        return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
            (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
    }
    static void store(tlm::tlm_generic_payload& tx, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
    }
    bool sensor_register(std::uint64_t off, unsigned base) const {
        return off >= base && off < base + 4 * sensor_count.get_value();
    }
    static int signed12(std::uint32_t value) {
        const int v = value & 0xfff;
        return v & 0x800 ? v - 4096 : v;
    }
    std::uint32_t sample(unsigned sensor) const {
        if (!ready() || !(enabled() & (1u << sensor))) return 0;
        const int t = sample_temperature_;
        const auto limits = srot_[(0x20 + 4 * sensor) / 4];
        const auto thresholds = tm_[(0x20 + 4 * sensor) / 4];
        std::uint32_t result = (1u << 21) | (static_cast<std::uint32_t>(t) & 0xfff);
        if (!(limits & (1u << 24)) && t < signed12(limits)) result |= 1u << 16;
        if (t < signed12(thresholds)) result |= 1u << 17;
        if (t > signed12(thresholds >> 12)) result |= 1u << 18;
        if (t > signed12(tm_[(0x60 + 4 * sensor) / 4])) result |= 1u << 19;
        if (!(limits & (1u << 25)) && t > signed12(limits >> 12)) result |= 1u << 20;
        return result;
    }
    void srot_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto off = tx.get_address();
        const bool sensor = sensor_register(off, 0x20) || sensor_register(off, 0x60) || sensor_register(off, 0xa0);
        const bool scalar = off == 4 || off == 8 || off == 0x18 || off == 0x1c ||
            off == 0xe0 || off == 0xe4 || off == 0xe8 || off == 0xec ||
            (off >= 0xf0 && off <= 0x12c) || off == 0x130 || off == 0x140 || off == 0x150;
        if (off == 0 && tx.is_read()) store(tx, 0x20000000);
        else if (!sensor && !scalar) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        else if (tx.is_read()) store(tx, srot_[off / 4]);
        else {
            const auto value = load(tx);
            // External ADC clocks have no producer in this bounded model.
            if ((off == 4 && ((value & 4u) ||
                             (((value >> 3) & 0xffff) & ~channels()))) ||
                (off == 0x140 && (value & ~0xfffffu)) ||
                (off == 0xec && (value & 0xffff)) ||
                (off >= 0xf0 && off <= 0x12c && value) ||
                (off == 0x130 && (value & 1)) ||
                (sensor_register(off, 0x60) && (value & 0xfe000000)) ||
                (sensor_register(off, 0xa0) && (value & ~0xfu))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
            }
            const auto previous = srot_[off / 4];
            const auto previous_calibrated = calibrated_;
            srot_[off / 4] = value;
            if (sensor_register(off, 0x60)) {
                const auto bit = 1u << ((off - 0x60) / 4);
                if ((value >> 10) & 0x1fff) calibrated_ |= bit;
                else calibrated_ &= ~bit;
            }
            if (!watchdog_budget_valid()) {
                srot_[off / 4] = previous;
                calibrated_ = previous_calibrated;
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
            }
            if (previous != value &&
                (off == 4 || off == 8 || sensor_register(off, 0x60) || sensor_register(off, 0xa0)))
                restart();
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void tm_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        update_sample();
        update_zero_c();
        const auto off = tx.get_address();
        if (tx.is_read() && sensor_register(off, 0xa0)) store(tx, sample((off - 0xa0) / 4));
        else if (tx.is_read() && off == 0xe4) store(tx, ready() ? 1 : 0);
        else if (tx.is_read() && off == 0xe0) store(tx, zero_c_ ? 1 : 0);
        // All admitted armed-watchdog states satisfy the no-expiry budget.
        else if (tx.is_read() && off == 0x13c) store(tx, 0);
        else if (tx.is_read() && (off == 8 || off == 0x14)) {
            std::uint32_t status = 0;
            for (unsigned i = 0; i < sensor_count.get_value(); ++i) {
                const auto s = sample(i);
                if (off == 8) {
                    if (s & (1u << 17)) status |= 1u << i;
                    if (s & (1u << 18)) status |= 1u << (i + 16);
                } else if (s & (1u << 19)) status |= 1u << i;
            }
            store(tx, status);
        } else if (off == 0x18) {
            if (tx.is_read()) store(tx, tm_[off / 4]);
            else tm_[off / 4] = load(tx);
        } else if (off == 4 || off == 0x10 || off == 0x1c || off == 0xe8 ||
                   sensor_register(off, 0x20) || sensor_register(off, 0x60)) {
            if (tx.is_read()) store(tx, tm_[off / 4]);
            else {
                const auto value = load(tx);
                // No interrupt sink is connected in this bounded boot group.
                if (off == 4 && value) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
                tm_[off / 4] = value;
            }
        } else { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// A separate four-byte profile word. It must not expose adjacent security fuses.
class QcomTsensCalibrationProfile : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomTsensCalibrationProfile> target_socket{"target_socket"};
    explicit QcomTsensCalibrationProfile(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomTsensCalibrationProfile::transport);
    }
private:
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        else if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4)
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        else if (tx.get_address()) tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        else if (!tx.is_read()) tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        else {
            for (unsigned i = 0; i < 4; ++i) tx.get_data_ptr()[i] = 0;
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
        }
    }
};
} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cstdint>

namespace qcom_vp {

// Availability only. Implementations own their state and change notification.
class ClockSource {
public:
    virtual ~ClockSource() = default;
    virtual bool clock_running() = 0;
    virtual const sc_core::sc_event& clock_changed() const = 0;
};

// A bank exposes independent PLL outputs and their acquisition events.
class PllSource {
public:
    virtual ~PllSource() = default;
    virtual unsigned bank_count() const = 0;
    virtual std::uint64_t reference_rate_hz() const = 0;
    virtual std::uint64_t main_output_hz(unsigned index) const = 0;
    virtual std::uint64_t even_output_hz(unsigned index) const = 0;
    virtual std::uint64_t calibration_output_hz(unsigned index) const = 0;
    virtual const sc_core::sc_event& clock_changed() const = 0;
    virtual const sc_core::sc_event& lock_changed(unsigned index) const = 0;
};

} // namespace qcom_vp

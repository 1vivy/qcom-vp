// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once

namespace qcom_vp {

// A CPU that reports whether it waits in WFI. A power controller reads this
// state only while the CPU has power, so a halt in reset does not count.
class CpuIdleState {
public:
    virtual bool waiting_for_interrupt() const = 0;

protected:
    ~CpuIdleState() = default;
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <tlm>

namespace qcom_vp {

// Only the arbiter's observer lane supplies this transaction marker.
// The marker lives on the caller stack, so it must not free itself.
struct PmicObserverExtension final : tlm::tlm_extension<PmicObserverExtension> {
    tlm::tlm_extension_base* clone() const override {
        return new PmicObserverExtension(*this);
    }
    void copy_from(const tlm::tlm_extension_base& other) override {
        *this = static_cast<const PmicObserverExtension&>(other);
    }
    void free() override {}
};

} // namespace qcom_vp

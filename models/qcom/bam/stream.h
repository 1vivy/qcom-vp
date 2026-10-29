// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <tlm>
#include <cstdint>

namespace qcom_vp {

// The payload address selects a BAM pipe. The command selects stream direction.
// The endpoint reports consumed bytes. Zero bytes means backpressure, not completion.
struct BamStreamExtension final : tlm::tlm_extension<BamStreamExtension> {
    std::uint32_t transferred = 0;
    bool end_of_transfer = false;

    tlm::tlm_extension_base* clone() const override {
        return new BamStreamExtension(*this);
    }
    void copy_from(const tlm::tlm_extension_base& other) override {
        *this = static_cast<const BamStreamExtension&>(other);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <cstdint>

namespace qcom_vp {

// Command DB resources that exist only after a publication. The RSC owner asks
// the directory whether a VRM register belongs to a published resource.
class RpmhResourceDirectory {
public:
    // A VRM resource has four 32-bit vote registers at +0x0, +0x4, +0x8, and +0xc.
    virtual bool published_vrm(std::uint32_t address) const = 0;

protected:
    ~RpmhResourceDirectory() = default;
};

} // namespace qcom_vp

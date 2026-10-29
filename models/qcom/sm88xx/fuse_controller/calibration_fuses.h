// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace qcom_vp {

// The selected CPH2749 and CPH2771 XBL CPR descriptors define the first group.
// The SM8850 PDP image defines two more groups of calibration reads.
// Without a private page, zero declares virtual revision/plan zero and zero
// calibration corrections. Those values are not measurements from a part.
// Security fuses and PRNG health thresholds (CALIBRATION2/3) do not belong to
// this policy.
class QcomCalibrationFuses {
public:
    // The page is the raw QFPROM range 0x221c8000..0x221c8fff.
    static constexpr std::size_t page_size = 4096;
    using Page = std::array<std::uint8_t, page_size>;

    // Offset relative to the existing fuse controller's 0x...8000 aperture.
    // The caller must retain its ordinary transaction validation and errors.
    // A null page selects the declared-zero policy. A page supplies the raw
    // word at the same offset.
    static bool read_raw_word(std::uint64_t offset, const Page* page,
                              std::uint32_t& value) {
        if ((offset & 3) != 0) return false;
        // CALIBRATION4..31: voltage, quotient, offset, aging, and CPR revision.
        // The second image also uses four additional descriptor words.
        // The PDP image reads 0x9e0..0xa20 for VPP increments and revisions.
        // It reads 0xb0c..0xb28 for IDDQ codes, VPP codes, and the flex ID.
        if ((offset >= 0x970 && offset <= 0x9dc) || offset == 0xa24 ||
            (offset >= 0xa60 && offset <= 0xa6c) ||
            (offset >= 0x9e0 && offset <= 0xa20) ||
            (offset >= 0xb0c && offset <= 0xb28)) {
            if (page) {
                const auto* bytes = page->data() + offset;
                value = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
                    (std::uint32_t(bytes[2]) << 16) |
                    (std::uint32_t(bytes[3]) << 24);
            } else {
                value = 0;
            }
            return true;
        }
        return false;
    }

};

} // namespace qcom_vp

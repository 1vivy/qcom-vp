// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <cerrno>
#include <cstddef>
#include <system_error>
#include <sys/random.h>

namespace qcom_vp {

inline void fill_host_entropy(unsigned char* output, std::size_t length) {
    std::size_t offset = 0;
    while (offset != length) {
        const auto count = getrandom(output + offset, length - offset, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            throw std::system_error(count < 0 ? errno : EIO, std::generic_category(),
                                    "Cannot obtain host entropy");
        }
        offset += static_cast<std::size_t>(count);
    }
}

} // namespace qcom_vp

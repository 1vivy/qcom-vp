// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <cstddef>

namespace qcom_vp {

// Public tme_hwkm_master_defs.h declares the retained transport-key entry as
// 528 bits. The split between key material and entry metadata is not recovered.
inline constexpr std::size_t transport_key_bytes = 66;

// Hardware sideband from the TME key master to one HWKM slave.
// The receiver keeps the material and has no read path for it.
class TransportKeySink {
public:
    virtual ~TransportKeySink() = default;
    enum class Delivery {
        rejected,   // The slave cannot accept a key in its current state.
        pending,    // The slave retained the broadcast for its next receive window.
        installed,  // The key is in the slave key store and its status bit is set.
    };
    virtual Delivery transport_key_receive(const unsigned char* material,
        std::size_t size) = 0;
};

} // namespace qcom_vp

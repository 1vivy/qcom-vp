// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cstdint>

namespace qcom_vp {

// The observer emits this event after the guest consumes a successful finalization.
struct ImageHandoffEvent {
    std::uint32_t generation;
    std::uint32_t sequence;
    std::uint32_t image_id;
};

class ImageHandoffSink {
public:
    virtual ~ImageHandoffSink() = default;
    virtual bool on_image_handoff(const ImageHandoffEvent& event, sc_core::sc_time& delay) = 0;
};

class ImageHandoffSource {
public:
    virtual ~ImageHandoffSource() = default;
    virtual bool subscribe(std::uint32_t image_id, ImageHandoffSink& sink) = 0;
};

} // namespace qcom_vp

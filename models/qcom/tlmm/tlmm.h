// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <vector>

namespace qcom_vp {

// TLMM v1 GPIO words, reconstructed from the native HAL and public register facts.
// No external input, eGPIO provider, interrupt source, or security interface exists.
// Register facts: Linux drivers/pinctrl/qcom/pinctrl-kaanapali.c (PINGROUP).
// https://github.com/torvalds/linux/blob/master/drivers/pinctrl/qcom/pinctrl-kaanapali.c
// Selected SM8850 HAL: 0x1489e82c..0x1489ec14. SM8845: 0x1489d0f4..0x1489d4dc.
// Their xbl_config pinctrl@f100000 nodes set stride 0x1000 and GPIO counts 217/205.
// Both native tile scanners use ID_STATUS bit 0. The configured tile count is one.
// Native publication writes LP_CFG (+0x14) for each GPIO. LPM reset clears +0x18.
// The model starts ordinary controls at zero. Sleep programming does not trigger sleep.
// Auxiliary SPMI pads retain the previous helper's mask. They are not GPIOs.
class QcomTlmm : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomTlmm> target_socket{"target_socket"};
    cci::cci_param<unsigned> gpio_count{"gpio_count", 0, "Contiguous GPIOs in the single tile"};
    cci::cci_param<unsigned> pad_first{"pad_first", 226, "First auxiliary configuration-only pad"};
    cci::cci_param<unsigned> pad_count{"pad_count", 2, "Number of auxiliary configuration-only pads"};

    explicit QcomTlmm(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomTlmm::transport);
    }
    void cold_reset() {
        for (auto& pin : pins_) pin = {};
        for (auto& pad : pads_) pad = 0;
    }

private:
    static constexpr std::uint64_t stride = 0x1000;
    struct Pin {
        std::uint32_t configuration = 0;
        std::uint32_t output = 0;
        std::uint32_t interrupt_configuration = 0;
        std::uint32_t sleep_configuration = 0;
        std::uint32_t sleep_interrupt = 0;
    };
    std::vector<Pin> pins_;
    std::vector<std::uint32_t> pads_;

    void end_of_elaboration() override {
        if (!gpio_count.get_value() || gpio_count.get_value() > 1024 ||
            pad_count.get_value() > 1024 || pad_first.get_value() < gpio_count.get_value() ||
            pad_first.get_value() > 1024 - pad_count.get_value())
            throw std::invalid_argument("TLMM requires bounded, non-overlapping GPIO and auxiliary pad ranges");
        pins_.resize(gpio_count.get_value());
        pads_.resize(pad_count.get_value());
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto index = tx.get_address() / stride;
        const auto offset = tx.get_address() % stride;
        std::uint32_t value = 0;
        const auto requested = tx.is_write() ? clock_detail::load(tx) : 0;
        if (index >= pins_.size()) {
            if (index < pad_first.get_value() || index - pad_first.get_value() >= pads_.size() || offset != 0) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            auto& pad = pads_[index - pad_first.get_value()];
            if (tx.is_write()) pad = requested & 0x1fff;
            value = pad;
        } else {
            auto& pin = pins_[index];
            switch (offset) {
            case 0x00:
                // Pull, mux, drive, output enable, eGPIO enable, and strong pull.
                // eGPIO PRESENT (bit 11) is immutable zero: no provider is connected.
                if (tx.is_write()) pin.configuration = requested & 0x33ff;
                value = pin.configuration;
                break;
            case 0x04:
                if (tx.is_write()) pin.output = requested & 2;
                value = pin.output;
                // GPIO output drives its own digital input only in GPIO output mode.
                // Inputs and peripheral muxes remain disconnected low. Pulls are retained only.
                if ((pin.configuration & 0x123c) == 0x200) value |= pin.output >> 1;
                break;
            case 0x08:
                // No wakeup producer: WAKEUP_PRESENT (bit 6) remains zero.
                if (tx.is_write()) pin.interrupt_configuration = requested & 0x79f;
                value = pin.interrupt_configuration;
                break;
            case 0x0c:
                // Status is clear. Acknowledge writes cannot manufacture an interrupt.
                break;
            case 0x10:
                if (tx.is_write()) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                value = 1; // This GPIO exists in the single tile. This is not an access grant.
                break;
            case 0x14:
                if (tx.is_write()) pin.sleep_configuration = requested & 0xfff;
                value = pin.sleep_configuration;
                break;
            case 0x18:
                if (tx.is_write()) pin.sleep_interrupt = requested & 3;
                value = pin.sleep_interrupt;
                break;
            default:
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
        }
        if (tx.is_read()) clock_detail::store(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

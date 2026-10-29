// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <array>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// One APSS PDC owner for the QSEE boot contracts. It holds the BCM controls,
// the capture configuration, and the ownership bitmap. No capture producer,
// wake producer, interrupt producer, or protection backend is attached.
// A configuration write therefore makes no timestamp, event, interrupt, or
// permission. It also grants no access.
class QcomApssPdc : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomApssPdc> target_socket{"target_socket"};
    cci::cci_param<unsigned> capture_slots{"capture_slots", 0,
        "Advertised capture slots in 0x1004 bits 15:12. Zero declares no producer."};

    explicit QcomApssPdc(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomApssPdc::transport);
    }

private:
    // The OEM writes five complete control words. The physical reserved-bit
    // masks are not established, so the model keeps each word unchanged.
    std::array<std::uint32_t, 5> bcm_{};
    // The selected tables hold 180 or 192 records. Six words cover both.
    std::array<std::uint32_t, 6> ownership_{};
    std::uint32_t owner_ = 0;
    std::array<std::uint32_t, 15> capture_enable_{};
    // Each domain has a comparison word and a handoff word. Four domains fit
    // in a 0x40000 aperture.
    std::array<std::array<std::uint32_t, 2>, 4> comparison_{};
    std::uint32_t event_selection_ = 0;

    static constexpr std::uint64_t bcm_base = 0x5504;
    static constexpr std::uint64_t bitmap_base = 0x4600;
    static constexpr std::uint64_t enable_base = 0x4f00;
    static constexpr std::uint64_t capture_base = 0x1200;
    // A capture slot holds five words, so the next slot starts 0x14 bytes later.
    static constexpr std::uint64_t slot_stride = 0x14;

    void before_end_of_elaboration() override {
        if (capture_slots.get_value() > capture_enable_.size())
            throw std::invalid_argument("APSS PDC capture_slots must be at most 15");
    }

    static void retained(tlm::tlm_generic_payload& tx, std::uint32_t& state) {
        if (tx.is_write()) state = clock_detail::load(tx);
        else clock_detail::store(tx, state);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // A capture register must not report a valid bit or a timestamp on its own.
    static void read_zero(tlm::tlm_generic_payload& tx) {
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        clock_detail::store(tx, 0);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const std::uint64_t address = tx.get_address();

        // The slot count is a capability. A write cannot add an event source.
        if (address == 0x1004) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            clock_detail::store(tx, capture_slots.get_value() << 12);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (address >= bcm_base && address <= bcm_base + 4 * (bcm_.size() - 1)) {
            retained(tx, bcm_[(address - bcm_base) / 4]);
            return;
        }
        if (address >= bitmap_base && address <= bitmap_base + 4 * (ownership_.size() - 1)) {
            // The firmware selects the bit and compares the owner word itself.
            retained(tx, ownership_[(address - bitmap_base) / 4]);
            return;
        }
        if (address == 0x4640) {
            retained(tx, owner_);
            return;
        }
        if (address == 0x48) {
            retained(tx, event_selection_);
            return;
        }
        {
            const std::uint64_t domain = address >> 16;
            const std::uint64_t offset = address & 0xffff;
            if (domain < comparison_.size() && (offset == 0x38 || offset == 0x40)) {
                // The firmware sets bit 31 itself on the upper write.
                retained(tx, comparison_[domain][offset == 0x40 ? 0 : 1]);
                return;
            }
        }
        if (address >= enable_base && address < enable_base + slot_stride * capture_enable_.size()) {
            const std::uint64_t relative = address - enable_base;
            if (relative % slot_stride == 0) {
                retained(tx, capture_enable_[relative / slot_stride]);
                return;
            }
        }
        if (address >= capture_base && address < capture_base + slot_stride * capture_enable_.size()) {
            const std::uint64_t relative = address - capture_base;
            const std::uint64_t within = relative % slot_stride;
            if (within == 0 || within == 4 || within == 8) {
                read_zero(tx);
                return;
            }
        }
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

} // namespace qcom_vp

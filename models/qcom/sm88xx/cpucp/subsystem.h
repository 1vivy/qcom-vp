// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <array>
#include <cstdint>

namespace qcom_vp {

// Virtual PLICv2 topology: 256 sources, two hart contexts, 16 priorities.
// The firmware derives these compact register ranges from the two ID words.
class QcomCpucpSubsystem : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCpucpSubsystem> control_socket{"control_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpucpSubsystem> interrupt_socket{"interrupt_socket"};
    tlm_utils::simple_target_socket_optional<QcomCpucpSubsystem> secure_notify_socket{"secure_notify_socket"};
    sc_core::sc_vector<TargetSignalSocket<bool>> source{"source", 256};
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq{"irq", 2};
    // Virtual policy connects the first subsystem commit to secondary release.
    InitiatorSignalSocket<bool> hart1_release{"hart1_release"};

    explicit QcomCpucpSubsystem(sc_core::sc_module_name name) : sc_module(name) {
        control_socket.register_b_transport(this, &QcomCpucpSubsystem::control);
        interrupt_socket.register_b_transport(this, &QcomCpucpSubsystem::interrupt);
        secure_notify_socket.register_b_transport(this, &QcomCpucpSubsystem::secure_notify);
        for (unsigned i = 0; i < source.size(); ++i) {
            source[i].register_value_changed_cb([this, i](const bool& level) {
                // Signal sockets invoke callbacks even for unchanged writes.
                // The gateway stays closed until the claimed source completes.
                const bool rising = level && !level_[i];
                level_[i] = level;
                if (rising && !in_service_[i])
                    pending_[i / 32] |= uint32_t{1} << (i % 32);
                update();
            });
        }
    }

private:
    static constexpr unsigned trigger_base = 8, priority_base = 0x28;
    static constexpr unsigned enable_base = 0xa8, threshold_base = 0xe8, claim_base = 0xf0;
    std::array<uint32_t, 8> pending_{};
    // One selects edge-triggered input. Zero selects level-triggered input.
    std::array<uint32_t, 8> trigger_{};
    std::array<uint32_t, 32> priority_{};
    std::array<std::array<uint32_t, 8>, 2> enable_{};
    std::array<uint32_t, 2> threshold_{};
    std::array<std::array<unsigned, 256>, 2> claimed_{};
    std::array<unsigned, 2> claim_depth_{};
    std::array<uint32_t, 2> context_init_{};
    std::array<uint32_t, 2> hart_init_{};
    std::array<bool, 256> level_{};
    std::array<bool, 256> in_service_{};
    uint32_t control_ = 1; // Explicit virtual cold inhibit, not a silicon reset value.
    uint32_t configuration_ = 0;
    bool hart1_released_ = false;

    static bool valid(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        else if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
                 tx.get_streaming_width() < 4 || (tx.get_address() & 3))
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        else if (!tx.is_read() && !tx.is_write())
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        else return true;
        return false;
    }
    static uint32_t load(const tlm::tlm_generic_payload& tx) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t{tx.get_data_ptr()[i]} << (8 * i);
        return value;
    }
    static void result(tlm::tlm_generic_payload& tx, uint32_t value) {
        if (tx.is_read()) for (unsigned i = 0; i < 4; ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    bool ready() const { return (control_ & 0x80000003u) == 0x80000002u; }
    unsigned winner(unsigned hart) const {
        unsigned selected = 0;
        unsigned priority = threshold_[hart];
        if (!ready()) return 0;
        for (unsigned i = 0; i < 256; ++i) {
            const auto bit = uint32_t{1} << (i % 32);
            const unsigned candidate = (priority_[i / 8] >> (4 * (i % 8))) & 15;
            if ((pending_[i / 32] & enable_[hart][i / 32] & bit) &&
                !in_service_[i] && candidate > priority) {
                priority = candidate;
                selected = i + 1; // Firmware uses source index zero, claim ID one.
            }
        }
        return selected;
    }
    void update() {
        for (unsigned hart = 0; hart < irq.size(); ++hart)
            for (int port = 0; port < irq[hart].size(); ++port)
                irq[hart][port]->write(winner(hart) != 0);
    }
    void start_of_simulation() override { update(); }
    void secure_notify(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        }
        const uint32_t value = load(tx);
        if (!tx.is_write() || (value & ~uint32_t{0x10000000})) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
        }
        // Declared virtual APSS notification latch, not a silicon wire claim.
        // Pre-claim rings coalesce. A ring during service queues the next claim.
        // Only guest execution consumes the request and publishes SCMI completion.
        if (value) {
            pending_[117 / 32] |= uint32_t{1} << (117 % 32);
            update();
        }
        result(tx, value);
    }
    void control(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto offset = tx.get_address();
        uint32_t value = load(tx);
        if (offset == 0x104) {
            if (tx.is_write()) {
                if (value & ~0x80000003u) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
                }
                // The virtual commit requires the guest to remove the inhibit first.
                control_ = value;
                if (control_ & 1) control_ &= ~0x80000002u;
                if (!(control_ & 2)) control_ &= ~0x80000000u;
                // This connection does not publish a firmware acknowledgement.
                if (!hart1_released_ && ready()) {
                    hart1_released_ = true;
                    for (int port = 0; port < hart1_release.size(); ++port)
                        hart1_release[port]->write(true);
                }
                update();
            }
            value = control_;
        } else if (offset == 0x118 && tx.is_read()) {
            // Bit 18 is a sleep-path discriminator. This board starts awake and
            // does not implement a CPUCP power-collapse transition.
            value = 0;
        } else {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        }
        result(tx, value);
    }
    void interrupt(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto offset = tx.get_address();
        uint32_t value = load(tx);
        uint32_t* word = nullptr;
        if (offset < 8 && tx.is_read()) value = offset ? 16 : (2u << 16) | 256;
        else if (offset >= trigger_base && offset < priority_base) {
            const unsigned index = (offset - trigger_base) / 4;
            // The firmware programs pulse sources here before it enables them.
            // These writes configure the gateway. They do not request interrupts.
            if (tx.is_write()) trigger_[index] = value;
            value = trigger_[index];
        } else if (offset >= priority_base && offset < enable_base)
            word = &priority_[(offset - priority_base) / 4];
        else if (offset >= enable_base && offset < threshold_base) {
            const unsigned index = (offset - enable_base) / 4;
            word = &enable_[index / 8][index % 8];
        } else if (offset >= threshold_base && offset < claim_base) {
            word = &threshold_[(offset - threshold_base) / 4];
            value &= 15;
        } else if (offset >= claim_base && offset < 0xf8) {
            const unsigned hart = (offset - claim_base) / 4;
            if (tx.is_read()) {
                value = winner(hart);
                if (value) {
                    claimed_[hart][claim_depth_[hart]++] = value;
                    in_service_[value - 1] = true;
                    pending_[(value - 1) / 32] &= ~(uint32_t{1} << ((value - 1) % 32));
                }
            } else {
                // This PLICv2 firmware completes its context by writing zero,
                // unlike the standard PLIC write-back-of-source-ID interface.
                if (value) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
                if (claim_depth_[hart]) {
                    const unsigned source = claimed_[hart][--claim_depth_[hart]] - 1;
                    in_service_[source] = false;
                    const auto bit = uint32_t{1} << (source % 32);
                    if (level_[source] && !(trigger_[source / 32] & bit))
                        pending_[source / 32] |= bit;
                }
            }
        } else if (offset == 0xf8 || offset == 0xfc)
            word = &context_init_[(offset - 0xf8) / 4];
        else if (offset == 0x100 || offset == 0x104)
            word = &hart_init_[(offset - 0x100) / 4];
        else if (offset == 0x200) word = &configuration_;
        else { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (word) {
            if (tx.is_write()) *word = value;
            value = *word;
        }
        update();
        result(tx, value);
    }
};

} // namespace qcom_vp

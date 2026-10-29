// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <ports/initiator-signal-socket.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace qcom_vp {

// CPUCP 0x33552a..0x335566 initializes the counter and both physical frames.
class SystemTimer : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket_tagged_optional<SystemTimer> control_socket{"control_socket"};
    tlm_utils::simple_target_socket_tagged_optional<SystemTimer> config_socket{"config_socket"};
    tlm_utils::multi_passthrough_target_socket<SystemTimer> frame0_socket{"frame0_socket"};
    tlm_utils::multi_passthrough_target_socket<SystemTimer> frame1_socket{"frame1_socket"};
    tlm_utils::simple_target_socket_tagged_optional<SystemTimer> frame2_socket{"frame2_socket"};
    tlm_utils::simple_target_socket_tagged_optional<SystemTimer> frame3_socket{"frame3_socket"};
    tlm_utils::simple_target_socket_tagged_optional<SystemTimer> frame4_socket{"frame4_socket"};
    tlm_utils::simple_target_socket_tagged_optional<SystemTimer> frame5_socket{"frame5_socket"};
    tlm_utils::simple_target_socket_tagged_optional<SystemTimer> frame6_socket{"frame6_socket"};
    tlm_utils::simple_target_socket_tagged_optional<SystemTimer> frame0_el0_socket{"frame0_el0_socket"};
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq{"irq", 7};
    cci::cci_param<unsigned> clock_hz{"clock_hz", 0, "Required counter frequency in Hz"};
    cci::cci_param<uint64_t> initial_count{"initial_count", 0, "Initial counter value"};
    cci::cci_param<bool> enabled_at_reset{"enabled_at_reset", false, "Initial counter enable state"};
    cci::cci_param<unsigned> frame_access_reset{"frame_access_reset", 0, "Initial frame access bits"};
    cci::cci_param<unsigned> frame0_el0_access_reset{"frame0_el0_access_reset", 0,
        "Initial CNTEL0ACR for frame 0 (architectural reset is UNKNOWN)"};

    SC_HAS_PROCESS(SystemTimer);
    explicit SystemTimer(sc_core::sc_module_name name) : sc_module(name) {
        control_socket.register_b_transport(this, &SystemTimer::control, 0);
        config_socket.register_b_transport(this, &SystemTimer::configuration, 0);
        frame0_socket.register_b_transport(this, &SystemTimer::frame0);
        frame1_socket.register_b_transport(this, &SystemTimer::frame1);
        frame2_socket.register_b_transport(this, &SystemTimer::frame2, 2);
        frame3_socket.register_b_transport(this, &SystemTimer::additional_frame, 3);
        frame4_socket.register_b_transport(this, &SystemTimer::additional_frame, 4);
        frame5_socket.register_b_transport(this, &SystemTimer::additional_frame, 5);
        frame6_socket.register_b_transport(this, &SystemTimer::additional_frame, 6);
        frame0_el0_socket.register_b_transport(this, &SystemTimer::frame0_el0, 0);
        SC_METHOD(update_irq);
        sensitive << event_;
        dont_initialize();
    }

private:
    struct Frame {
        uint64_t compare = 0;
        uint64_t virtual_offset = 0;
        uint32_t control = 0;
        uint32_t access = 0;
    };
    std::array<Frame, 7> frames_{};
    sc_core::sc_event event_;
    uint64_t epoch_ = 0;
    uint64_t base_count_ = 0;
    uint64_t resolution_ = 0;
    unsigned clock_ = 0;
    uint32_t frequency_register_ = 0;
    uint32_t nonsecure_access_ = 0;
    uint32_t frame0_el0_access_ = 0;
    bool enabled_ = false;

    void end_of_elaboration() override {
        clock_ = clock_hz.get_value();
        if (!clock_ || frame_access_reset.get_value() > 0x3f ||
            (frame0_el0_access_reset.get_value() & ~0x303U)) {
            throw std::invalid_argument("The timer requires a frequency and valid frame access bits");
        }
        frequency_register_ = clock_;
        frame0_el0_access_ = frame0_el0_access_reset.get_value();
        base_count_ = initial_count.get_value();
        enabled_ = enabled_at_reset.get_value();
        resolution_ = sc_core::sc_time(1, sc_core::SC_SEC).value();
        for (auto& frame : frames_) frame.access = frame_access_reset.get_value();
    }

    void start_of_simulation() override { update_irq(); }

    uint64_t count(const sc_core::sc_time& delay) const {
        const uint64_t time = (sc_core::sc_time_stamp() + delay).value();
        const uint64_t elapsed = enabled_ && time >= epoch_ ? time - epoch_ : 0;
        return base_count_ + static_cast<uint64_t>(
            (static_cast<unsigned __int128>(elapsed) * clock_) / resolution_);
    }

    static bool expired(uint64_t now, uint64_t compare) {
        return now - compare < (uint64_t{1} << 63);
    }

    void update_irq() {
        const auto now = count(sc_core::SC_ZERO_TIME);
        uint64_t delta = std::numeric_limits<uint64_t>::max();
        for (unsigned i = 0; i < frames_.size(); ++i) {
            const auto& frame = frames_[i];
            const bool active = (frame.control & 3) == 1;
            const bool condition = expired(now, frame.compare);
            for (int port = 0; port < irq[i].size(); ++port) irq[i][port]->write(active && condition);
            if (active) {
                delta = std::min(delta, condition ?
                    (uint64_t{1} << 63) - (now - frame.compare) : frame.compare - now);
            }
        }
        event_.cancel();
        if (!enabled_ || delta == std::numeric_limits<uint64_t>::max()) return;
        const auto time = sc_core::sc_time_stamp().value();
        const auto elapsed = time >= epoch_ ? time - epoch_ : 0;
        const auto ticks = (static_cast<unsigned __int128>(elapsed) * clock_) / resolution_;
        const auto deadline = epoch_ + ((ticks + delta) * resolution_ + clock_ - 1) / clock_;
        if (deadline <= sc_core::sc_max_time().value()) {
            event_.notify(sc_core::sc_time::from_value(static_cast<uint64_t>(deadline) - time));
        }
    }

    static uint32_t load(const unsigned char* data) {
        return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
            (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
    }
    static void store(unsigned char* data, uint64_t value, unsigned length = 4) {
        for (unsigned i = 0; i != length; ++i) data[i] = value >> (8 * i);
    }
    static bool valid(tlm::tlm_generic_payload& tx, bool wide_counter = false) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        const auto length = tx.get_data_length();
        if (!tx.get_data_ptr() ||
            (length != 4 && !(wide_counter && length == 8 && tx.is_read())) ||
            tx.get_streaming_width() < length || (tx.get_address() & (length - 1))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if ((!tx.is_read() && !tx.is_write()) || !tx.get_extension<QemuMemTxAttrsExtension>()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        return true;
    }
    static bool nonsecure(tlm::tlm_generic_payload& tx) {
        const auto& attrs = tx.get_extension<QemuMemTxAttrsExtension>()->attrs;
        return !attrs.unspecified && !attrs.secure;
    }

    void control(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!valid(tx)) return;
        if (tx.get_address() != 0 || nonsecure(tx)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            const auto value = load(tx.get_data_ptr());
            if (value & ~1U) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            base_count_ = count(delay);
            epoch_ = (sc_core::sc_time_stamp() + delay).value();
            enabled_ = value & 1;
            update_irq();
            if (delay != sc_core::SC_ZERO_TIME) event_.notify(delay);
        } else {
            store(tx.get_data_ptr(), enabled_);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void configuration(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset >= 0x80 && offset < 0x80 + frames_.size() * 8) {
            const auto index = (offset - 0x80) / 8;
            if (nonsecure(tx) && !(nonsecure_access_ & (1U << index))) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            auto& value = frames_[index].virtual_offset;
            const unsigned shift = (offset & 4) ? 32 : 0;
            if (tx.is_write()) {
                value = (value & ~(uint64_t{0xffffffff} << shift)) |
                    (uint64_t{load(tx.get_data_ptr())} << shift);
            } else store(tx.get_data_ptr(), static_cast<uint32_t>(value >> shift));
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        uint32_t* word = nullptr;
        if (offset == 0) word = &frequency_register_;
        else if (offset == 4) word = &nonsecure_access_;
        else if (offset >= 0x40 && offset < 0x40 + frames_.size() * 4)
            word = &frames_[(offset - 0x40) / 4].access;
        if (!word || (nonsecure(tx) && (offset < 0x40 ||
            !(nonsecure_access_ & (1U << ((offset - 0x40) / 4)))))) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            const auto value = load(tx.get_data_ptr());
            *word = offset == 0 ? value : value & (offset == 4 ? 0x7fU : 0x3fU);
        } else store(tx.get_data_ptr(), *word);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void frame0(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) { frame(0, tx, delay); }
    void frame1(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) { frame(1, tx, delay); }
    void frame2(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) { frame(2, tx, delay); }
    void additional_frame(int index, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        frame(static_cast<unsigned>(index), tx, delay);
    }
    void frame0_el0(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        frame(0, tx, delay, true);
    }

    void frame(unsigned index, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay,
               bool second_view = false) {
        const auto offset = tx.get_address();
        if (!valid(tx, offset == 0 || offset == 8)) return;
        auto& state = frames_[index];
        uint32_t permission;
        switch (offset) {
        case 0: case 4: permission = 1; break;
        case 0x10: permission = 4; break;
        case 0x14: permission = 0; break;
        case 8: case 0xc: permission = 2; break;
        case 0x18: case 0x1c: permission = 8; break;
        // Virtual compare and interrupt state remain unavailable.
        case 0x30: case 0x34: case 0x38: case 0x3c: permission = 0; break;
        case 0x20: case 0x24: case 0x28: case 0x2c: permission = 32; break;
        default:
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (nonsecure(tx) && !(nonsecure_access_ & (1U << index))) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (offset == 0x14 && index == 0 && !second_view) {
            if (tx.is_write()) frame0_el0_access_ = load(tx.get_data_ptr()) & 0x303U;
            else store(tx.get_data_ptr(), frame0_el0_access_);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        uint32_t second_permission = 0;
        if (permission == 1) second_permission = 1;
        else if (permission == 2) second_permission = 2;
        else if (permission == 4) second_permission = 3;
        else if (permission == 32) second_permission = 0x200;
        if (second_view && !(frame0_el0_access_ & second_permission)) permission = 0;
        if (!(state.access & permission)) {
            if (tx.is_read()) store(tx.get_data_ptr(), 0, tx.get_data_length());
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        const auto now = count(delay);
        if (tx.is_write()) {
            const auto value = load(tx.get_data_ptr());
            switch (offset) {
            case 0x20: state.compare = (state.compare & 0xffffffff00000000ULL) | value; break;
            case 0x24: state.compare = (state.compare & 0xffffffffULL) | (uint64_t(value) << 32); break;
            case 0x28:
                state.compare = now + uint64_t(value) - ((value & 0x80000000U) ? (uint64_t{1} << 32) : 0);
                break;
            case 0x2c:
                if ((value & 3) == 1 && irq[index].size() == 0) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                state.control = value & 3;
                break;
            default:
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // Publish masking before the CPU can EOI and reactivate a level IRQ.
            // A delta-delayed update lets the QEMU thread observe the old level.
            update_irq();
            if (delay != sc_core::SC_ZERO_TIME) event_.notify(delay);
        } else {
            uint64_t value = 0;
            switch (offset) {
            case 0: value = now; break;
            case 4: value = now >> 32; break;
            case 8: value = now - state.virtual_offset; break;
            case 0xc: value = (now - state.virtual_offset) >> 32; break;
            case 0x18: value = state.virtual_offset; break;
            case 0x1c: value = state.virtual_offset >> 32; break;
            case 0x10: value = frequency_register_; break;
            case 0x20: value = state.compare; break;
            case 0x24: value = state.compare >> 32; break;
            case 0x28: value = state.compare - now; break;
            case 0x2c: value = state.control | ((state.control & 1) && expired(now, state.compare) ? 4U : 0U); break;
            }
            store(tx.get_data_ptr(), value, tx.get_data_length());
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

}

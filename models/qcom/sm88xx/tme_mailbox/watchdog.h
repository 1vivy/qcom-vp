// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <ports/initiator-signal-socket.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace qcom_vp {

// OEM 0x429560..0x4295d4 accesses the counter, deadlines, enable, and pet registers.
class TmeWatchdog : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<TmeWatchdog> target_socket{"target_socket"};
    InitiatorSignalSocket<bool> bark{"bark"};
    InitiatorSignalSocket<bool> bite{"bite"};
    cci::cci_param<unsigned> clock_hz{"clock_hz", 0, "Required watchdog frequency in Hz"};

    SC_HAS_PROCESS(TmeWatchdog);
    explicit TmeWatchdog(sc_core::sc_module_name name) : TmeWatchdog(name, false) {}

protected:
    TmeWatchdog(sc_core::sc_module_name name, bool packed_status)
        : sc_module(name), packed_status_(packed_status) {
        target_socket.register_b_transport(this, &TmeWatchdog::transport);
        SC_METHOD(update);
        sensitive << event_;
        dont_initialize();
    }
    virtual bool permits(const QemuMemTxAttrsExtension&) const { return true; }
    // The OS bank maps WDT_BARK_TIME to 0x10 and WDT_BITE_TIME to 0x14.
    // The 0x17908000 secure bank reverses the pair.
    virtual bool deadlines_swapped() const { return false; }
private:
    static constexpr uint32_t counter_mask = 0xfffff;
    const bool packed_status_;
    uint32_t control_ = 0;
    uint32_t bark_time_ = counter_mask;
    uint32_t bite_time_ = counter_mask;
    uint32_t chicken_bits_ = 0;
    uint64_t epoch_ = 0;
    uint64_t base_count_ = 0;
    uint64_t resolution_ = 0;
    unsigned clock_ = 0;
    sc_core::sc_event event_;

    void end_of_elaboration() override {
        clock_ = clock_hz.get_value();
        if (!clock_) throw std::invalid_argument("The watchdog requires a frequency");
        resolution_ = sc_core::sc_time(1, sc_core::SC_SEC).value();
    }
    void start_of_simulation() override { update(); }

    uint32_t count(const sc_core::sc_time& delay) const {
        const auto now = (sc_core::sc_time_stamp() + delay).value();
        const auto elapsed = (control_ & 1) && now >= epoch_ ? now - epoch_ : 0;
        const auto ticks = base_count_ +
            (static_cast<unsigned __int128>(elapsed) * clock_) / resolution_;
        return static_cast<uint32_t>(std::min<unsigned __int128>(ticks, counter_mask));
    }
    uint32_t status(const sc_core::sc_time& delay) const {
        const auto ticks = count(delay);
        return packed_status_ ?
            (ticks << 1) | (((control_ & 1) && ticks >= bark_time_) ? 1u : 0u) : ticks;
    }

    void update() {
        const auto ticks = count(sc_core::SC_ZERO_TIME);
        const bool enabled = control_ & 1;
        const bool barking = enabled && ticks >= bark_time_;
        const bool biting = enabled && ticks >= bite_time_;
        for (int port = 0; port < bark.size(); ++port) bark[port]->write(barking);
        for (int port = 0; port < bite.size(); ++port) bite[port]->write(biting);
        event_.cancel();
        if (biting && !bite.size()) {
            std::cerr << "WATCHDOG_BITE model=" << name() << " ticks=" << ticks
                      << " bite=" << bite_time_ << " enable=" << (control_ & 1) << '\n';
            sc_core::sc_stop();
            return;
        }
        if (!enabled) return;
        uint32_t next = counter_mask + 1;
        if (!barking) next = bark_time_;
        if (!biting) next = std::min(next, bite_time_);
        if (next > counter_mask) return;
        const auto delta = static_cast<unsigned __int128>(next - base_count_) * resolution_;
        const auto deadline = epoch_ + (delta + clock_ - 1) / clock_;
        const auto now = sc_core::sc_time_stamp().value();
        if (deadline >= now && deadline <= sc_core::sc_max_time().value())
            event_.notify(sc_core::sc_time::from_value(static_cast<uint64_t>(deadline) - now));
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if ((!tx.is_read() && !tx.is_write()) || !tx.get_extension<QemuMemTxAttrsExtension>()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (!permits(*tx.get_extension<QemuMemTxAttrsExtension>())) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto offset = tx.get_address();
        if (offset > 0x10 && !(packed_status_ && offset == 0x20)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* data = tx.get_data_ptr();
        // QHEE writes 0xf once to page offset 0x24 at image offset 0x46df0 and
        // never reads it back. The source-only register name and the write-only
        // contract remain unestablished. The model reserves the four written
        // bits and rejects reads without claiming a hardware access type.
        if (offset == 0x20 && tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            const uint32_t value = offset == 0 ? 0 : offset == 4 ? control_ :
                offset == 8 ? status(delay) :
                ((offset == 0xc) != deadlines_swapped() ? bark_time_ : bite_time_);
            for (unsigned i = 0; i < 4; ++i) data[i] = value >> (8 * i);
        } else {
            const uint32_t value = uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
                (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
            if ((offset == 0 && (value & ~1U)) || (offset == 4 && (value & ~7U)) ||
                offset == 8 || (offset == 0x20 && (value & ~0xfU)) ||
                (offset >= 0xc && offset <= 0x10 && (value & ~counter_mask))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            const auto now = (sc_core::sc_time_stamp() + delay).value();
            if (offset == 0 && (value & 1)) {
                // Name the pet writer and its time so a deadline is attributable.
                std::cerr << "WATCHDOG_PET model=" << name() << " at="
                          << (sc_core::sc_time_stamp() + delay).to_seconds() << '\n';
                base_count_ = 0;
                epoch_ = now;
            } else if (offset == 4) {
                // Log the gating word so a boot boundary names the owner and value.
                const auto* attrs = tx.get_extension<QemuMemTxAttrsExtension>();
                std::cerr << "WATCHDOG_CONTROL model=" << name() << " value=" << value
                          << " secure=" << (attrs ? attrs->attrs.secure : 0) << '\n';
                base_count_ = count(delay);
                epoch_ = now;
                control_ = value;
            } else if (offset == 0xc || offset == 0x10) {
                const bool bark = (offset == 0xc) != deadlines_swapped();
                std::cerr << (bark ? "WATCHDOG_BARK" : "WATCHDOG_BITE_TIME")
                          << " model=" << name() << " value=" << value << '\n';
                if (bark) bark_time_ = value;
                else bite_time_ = value;
            }
            // Page offset 0x24: four override fields in bits 3:0.
            // The source-only field names remain unestablished.
            // QHEE writes 0xf once, with no read or poll.
            // Retain configuration only: electrical override effects are not modeled.
            else if (offset == 0x20) {
                chicken_bits_ = value;
                std::cerr << "WATCHDOG_CHICKEN_BITS model=" << name()
                          << " value=" << chicken_bits_ << " policy=retained-override-config\n";
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
            // Publish the IRQ level before the CPU can return from its handler.
            update();
            if (delay != sc_core::SC_ZERO_TIME) event_.notify(delay);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// KPSS status packs COUNT in bits 20:1 and EXPIRED_STATUS in bit zero.
// The board maps the socket at WDT_RST, four bytes after the register base.
class ApssWatchdog final : public TmeWatchdog {
public:
    tlm_utils::multi_passthrough_target_socket<ApssWatchdog> security_socket{"security_socket"};
    cci::cci_param<bool> nonsecure_reset{
        "nonsecure_reset", false, "Permit nonsecure timer access at cold reset"};
    // The public Linux qcom-wdt driver places WDT_BARK_TIME at 0x10 and
    // WDT_BITE_TIME at 0x14 for the KPSS layout, and the device-tree node at
    // 0x17600000 uses that layout.
    // The socket starts at WDT_RST, four bytes
    // after the register base, so offset 0xc is the bark and offset 0x10 is
    // the bite for both banks. QHEE writes 192000 to the bark and a larger
    // value to the bite, which matches the six-second bark of the debug guide.
    // No profile reverses the pair.
    cci::cci_param<bool> secure_bank{
        "secure_bank", false, "Map bite to 0xc and bark to 0x10, for a bank with reversed deadlines"};
    explicit ApssWatchdog(sc_core::sc_module_name name) : TmeWatchdog(name, true) {
        security_socket.register_b_transport(this, &ApssWatchdog::security_transport);
    }

private:
    bool nonsecure_ = false;

    void before_end_of_elaboration() override {
        nonsecure_ = nonsecure_reset.get_value();
    }
    bool deadlines_swapped() const override { return secure_bank.get_value(); }
    bool permits(const QemuMemTxAttrsExtension& attributes) const override {
        return nonsecure_ || (!attributes.attrs.unspecified && attributes.attrs.secure);
    }
    void security_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const auto* attributes = tx.get_extension<QemuMemTxAttrsExtension>();
        if (!attributes || (!tx.is_read() && !tx.is_write()) ||
            (tx.is_write() && (attributes->attrs.unspecified || !attributes->attrs.secure))) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        auto* data = tx.get_data_ptr();
        if (tx.is_read()) {
            data[0] = nonsecure_;
            data[1] = data[2] = data[3] = 0;
        } else {
            const uint32_t value = uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
                (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
            nonsecure_ = value & 1;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

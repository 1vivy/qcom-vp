// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once

#include "virtual_dram.h"
#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// APSS controls and shared processor reset use separate board apertures.
// Selected XBL: clock gating 0x148adb08, mode control 0x148adbf4,
// command submission and completion 0x148adc1c. No SHRM CPU timing is modeled.
class QcomVirtualShrm : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomVirtualShrm> target_socket{"target_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomVirtualShrm> processor_control_socket{"processor_control_socket"};
    cci::cci_param<std::string> backend_path{
        "backend_path", "", "Required QcomVirtualDdr object path"};
    cci::cci_param<std::string> startup_policy{
        "startup_policy", "guest-release", "Virtual startup policy: guest-release"};
    cci::cci_param<std::string> completion_policy{
        "completion_policy", "synchronous-virtual", "Complete only after backend effects"};

    explicit QcomVirtualShrm(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomVirtualShrm::transport);
        target_socket.register_transport_dbg(this, &QcomVirtualShrm::debug);
        target_socket.register_get_direct_mem_ptr(this, &QcomVirtualShrm::dmi);
        processor_control_socket.register_b_transport(this, &QcomVirtualShrm::processor_control);
    }

    // Reset preserves guest memory but clears processor and command state.
    // Reset cannot cancel a backend call that is still executing.
    bool reset() {
        if (busy_) return false;
        ready_ = false;
        request_staged_ = false;
        request_ = trigger_ = response_ = mode_ = route_ = 0;
        processor_control_ = 2;
        clock_gating_ = 0;
        if (backend_) backend_->reset();
        return true;
    }

private:
    QcomVirtualDdr* backend_ = nullptr;
    uint32_t request_ = 0;
    uint32_t trigger_ = 0;
    uint32_t response_ = 0;
    uint32_t mode_ = 0;
    uint32_t route_ = 0;
    uint32_t processor_control_ = 2;
    uint32_t clock_gating_ = 0;
    bool ready_ = false;
    bool busy_ = false;
    bool request_staged_ = false;

    struct BusyGuard {
        bool& busy;
        ~BusyGuard() { busy = false; }
    };

    void end_of_elaboration() override {
        if (startup_policy.get_value() != "guest-release" ||
            completion_policy.get_value() != "synchronous-virtual") {
            throw std::invalid_argument("Unsupported virtual SHRM policy");
        }
        const auto& path = backend_path.get_value();
        if (!path.empty()) {
            backend_ = dynamic_cast<QcomVirtualDdr*>(sc_core::sc_find_object(path.c_str()));
            if (!backend_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                backend_ = dynamic_cast<QcomVirtualDdr*>(sc_core::sc_find_object(relative.c_str()));
            }
        }
        if (!backend_) throw std::invalid_argument("Virtual SHRM requires a DDR backend");
    }

    static void fail(tlm::tlm_generic_payload& tx) {
        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    }

    bool write_word(uint64_t offset, uint32_t value) {
        if (busy_) return false;
        switch (offset) {
        case 0x04:
            if (value > 1) return false;
            clock_gating_ = value;
            return true;
        case 0x10:
            if (value > 1 || trigger_) return false;
            mode_ = value;
            return true;
        case 0x60:
            if (value > 1 || trigger_) return false;
            route_ = value;
            return true;
        case 0x68:
            if (!ready_ || trigger_) return false;
            request_ = value;
            response_ = 0;
            request_staged_ = true;
            return true;
        case 0x64:
            if (!ready_ || value > 1) return false;
            if (!value) {
                trigger_ = 0;
                return true;
            }
            if (trigger_ || !request_staged_ || mode_ != 0 || route_ != 1) return false;
            request_staged_ = false;
            response_ = 0;
            busy_ = true;
            {
                // execute() can perform nested TLM accesses. Preserve the busy
                // guard through those accesses and clear it on exceptions.
                BusyGuard guard{busy_};
                uint32_t result = 0;
                if (!backend_->execute(request_, result) ||
                    result != ((request_ & 0xfcff0000u) | 0xf0u)) {
                    std::fprintf(stderr, "SHRM_REQUEST_FAILED model=%s request=0x%08x\n", name(), request_);
                    return false;
                }
                response_ = result;
                trigger_ = 1;
            }
            return true;
        default:
            return false;
        }
    }

    bool read_word(uint64_t offset, uint32_t& value) const {
        switch (offset) {
        case 0x04: value = clock_gating_; return true;
        case 0x08: value = ready_ ? 0x10 : 0; return true;
        case 0x10: value = mode_; return true;
        case 0x60: value = route_; return true;
        case 0x64: value = busy_ ? 1 : trigger_; return true;
        case 0x68: value = request_; return true;
        case 0x6c: value = response_; return true;
        default: return false;
        }
    }

    void processor_control(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if ((!tx.is_read() && !tx.is_write()) || !tx.get_data_ptr()) return fail(tx);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (tx.get_data_length() != 4 || tx.get_streaming_width() < 4) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* bytes = tx.get_data_ptr();
        if (tx.is_write()) {
            uint32_t value = 0;
            for (unsigned i = 0; i != 4; ++i) value |= uint32_t{bytes[i]} << (8 * i);
            if (busy_) return fail(tx);
            if (value & 3) {
                if (!reset()) return fail(tx);
            } else if (!ready_) {
                busy_ = true;
                BusyGuard guard{busy_};
                if (!backend_->startup()) return fail(tx);
                ready_ = true;
            }
            processor_control_ = value;
        } else {
            for (unsigned i = 0; i != 4; ++i)
                bytes[i] = static_cast<unsigned char>(processor_control_ >> (8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if ((!tx.is_read() && !tx.is_write()) || !tx.get_data_ptr()) return fail(tx);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (tx.get_data_length() != 4 || tx.get_streaming_width() < 4) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() & 3) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* bytes = tx.get_data_ptr();
        uint32_t value = 0;
        if (tx.is_write()) {
            for (unsigned i = 0; i != 4; ++i) value |= uint32_t{bytes[i]} << (8 * i);
            if (!write_word(tx.get_address(), value)) return fail(tx);
        } else {
            if (!read_word(tx.get_address(), value)) return fail(tx);
            for (unsigned i = 0; i != 4; ++i) bytes[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    unsigned debug(tlm::tlm_generic_payload& tx) {
        // Debug access must not start firmware work or change controller state.
        if (!tx.is_read()) {
            fail(tx);
            return 0;
        }
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        transport(tx, delay);
        return tx.is_response_ok() ? 4 : 0;
    }

    bool dmi(tlm::tlm_generic_payload&, tlm::tlm_dmi&) { return false; }
};

} // namespace qcom_vp

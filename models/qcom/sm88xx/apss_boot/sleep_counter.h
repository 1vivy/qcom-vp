// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// XBL 0x14827fbc reads MPM_SLEEP_TIMETICK_COUNT_VAL before it initializes the stack.
class SleepCounter : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<SleepCounter> target_socket{"target_socket"};
    cci::cci_param<unsigned> clock_hz{"clock_hz", 0, "Required sleep clock frequency in Hz"};

    explicit SleepCounter(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &SleepCounter::transport);
    }

private:
    unsigned frequency_ = 0;
    uint64_t resolution_ = 0;

    void end_of_elaboration() override {
        frequency_ = clock_hz.get_value();
        if (!frequency_) throw std::invalid_argument("The sleep counter requires a clock frequency");
        resolution_ = sc_core::sc_time(1, sc_core::SC_SEC).value();
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        tx.set_dmi_allowed(false);
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto time = (sc_core::sc_time_stamp() + delay).value();
        const uint32_t count = (static_cast<unsigned __int128>(time) * frequency_) / resolution_;
        for (unsigned i = 0; i != 4; ++i) tx.get_data_ptr()[i] = count >> (8 * i);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {
namespace pdp_detail {
inline void read_word(tlm::tlm_generic_payload& tx, std::uint64_t value) {
    tx.set_dmi_allowed(false);
    const auto size = tx.get_data_length();
    if (tx.get_byte_enable_ptr()) {
        tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        return;
    }
    if (!tx.get_data_ptr() || (size != 4 && size != 8) ||
        tx.get_streaming_width() < size || (tx.get_address() & (size - 1))) {
        tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }
    if (!tx.is_read()) {
        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }
    value >>= (tx.get_address() & 4) * 8;
    for (unsigned i = 0; i < size; ++i)
        tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (i * 8));
    tx.set_response_status(tlm::TLM_OK_RESPONSE);
}
} // namespace pdp_detail

// Firmware reads a free-running counter at +0x10 and its frequency at +0x38.
class NccTimer : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<NccTimer> target_socket{"target_socket"};
    cci::cci_param<std::uint64_t> frequency_hz{
        "frequency_hz", 0, "Required virtual NCC counter frequency"};
    explicit NccTimer(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &NccTimer::transport);
    }
private:
    std::uint64_t frequency_ = 0;
    std::uint64_t ticks_per_second_ = 0;
    void end_of_elaboration() override {
        frequency_ = frequency_hz.get_value();
        if (!frequency_) throw std::invalid_argument("The NCC timer requires a frequency");
        ticks_per_second_ = sc_core::sc_time(1, sc_core::SC_SEC).value();
    }
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        const auto offset = tx.get_address() & ~std::uint64_t{7};
        if (offset == 0x38) pdp_detail::read_word(tx, frequency_);
        else if (offset == 0x10) {
            const auto ticks = sc_core::sc_time_stamp().value();
            const auto count = static_cast<std::uint64_t>(
                static_cast<unsigned __int128>(ticks) * frequency_ / ticks_per_second_);
            pdp_detail::read_word(tx, count);
        } else {
            tx.set_dmi_allowed(false);
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        }
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace qcom_vp {

// Arm BSA generic watchdog: refresh frame WRR, control frame WCS, WOR, and WCV.
// WS0 is the bark interrupt. A second timeout asserts WS1, which stops the simulation.
class BsaWatchdog : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<BsaWatchdog> refresh_socket{"refresh_socket"};
    tlm_utils::simple_target_socket<BsaWatchdog> control_socket{"control_socket"};
    InitiatorSignalSocket<bool> ws0{"ws0"};
    cci::cci_param<std::uint64_t> clock_hz{"clock_hz", 0, "Required system counter frequency"};

    SC_HAS_PROCESS(BsaWatchdog);
    explicit BsaWatchdog(sc_core::sc_module_name name) : sc_module(name) {
        refresh_socket.register_b_transport(this, &BsaWatchdog::refresh_transport);
        control_socket.register_b_transport(this, &BsaWatchdog::control_transport);
        SC_METHOD(expire);
        sensitive << event_;
        dont_initialize();
    }

private:
    static constexpr std::uint64_t offset_mask = (std::uint64_t{1} << 48) - 1;
    std::uint64_t clock_ = 0;
    std::uint64_t offset_ = 0;
    std::uint64_t compare_ = 0;
    std::uint32_t status_ = 0;
    std::uint32_t auxiliary_ = 0;
    bool auxiliary_logged_ = false;
    sc_core::sc_event event_;

    void end_of_elaboration() override {
        clock_ = clock_hz.get_value();
        if (!clock_) throw std::invalid_argument("The BSA watchdog requires a counter frequency");
    }
    void start_of_simulation() override { drive(); }
    std::uint64_t now() const {
        const auto ticks = sc_core::sc_time_stamp().value();
        return static_cast<std::uint64_t>(static_cast<unsigned __int128>(ticks) * clock_ /
                                          sc_core::sc_time(1, sc_core::SC_SEC).value());
    }
    void drive() {
        for (int port = 0; port < ws0.size(); ++port) ws0[port]->write((status_ & 2) != 0);
    }
    void schedule() {
        event_.cancel();
        if (!(status_ & 1)) return;
        const auto current = now();
        const auto remaining = compare_ > current ? compare_ - current : 0;
        const auto seconds = static_cast<double>(remaining) / static_cast<double>(clock_);
        event_.notify(sc_core::sc_time(seconds, sc_core::SC_SEC));
    }
    // A refresh, a WCS write, and a WOR write restart the first timeout.
    void refresh() {
        status_ &= 1;
        compare_ = now() + offset_;
        drive();
        schedule();
    }
    void expire() {
        if (!(status_ & 1)) return;
        if (now() < compare_) { schedule(); return; }
        if (!(status_ & 2)) {
            status_ |= 2;
            compare_ = now() + offset_;
            std::fprintf(stderr, "WATCHDOG_BARK model=%s source=ws0\n", name());
            drive();
            schedule();
            return;
        }
        status_ |= 4;
        std::fprintf(stderr, "WATCHDOG_BITE model=%s source=ws1\n", name());
        sc_core::sc_stop();
    }
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
    static std::uint32_t load(const tlm::tlm_generic_payload& tx) {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{tx.get_data_ptr()[i]} << (8 * i);
        return value;
    }
    static void store(tlm::tlm_generic_payload& tx, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void refresh_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.get_address() != 0) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (tx.is_write()) refresh();
        store(tx, 0);
    }
    void control_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto offset = tx.get_address();
        const auto value = load(tx);
        std::uint32_t result = 0;
        switch (offset) {
        case 0x0:
            if (tx.is_write()) { status_ = (status_ & ~1u) | (value & 1); refresh(); }
            result = status_;
            break;
        case 0x8:
            if (tx.is_write()) { offset_ = (offset_ & ~std::uint64_t{0xffffffff}) | value; refresh(); }
            result = static_cast<std::uint32_t>(offset_);
            break;
        case 0xc:
            if (tx.is_write()) {
                offset_ = ((std::uint64_t{value & 0xffff} << 32) | (offset_ & 0xffffffff)) & offset_mask;
                refresh();
            }
            result = static_cast<std::uint32_t>(offset_ >> 32);
            break;
        case 0x10: case 0x14: {
            const unsigned shift = offset == 0x14 ? 32 : 0;
            if (tx.is_write()) {
                const auto mask = std::uint64_t{0xffffffff} << shift;
                compare_ = (compare_ & ~mask) | (std::uint64_t{value} << shift);
                schedule();
            }
            result = static_cast<std::uint32_t>(compare_ >> shift);
            break;
        }
        case 0x100:
            // PDP 0x8132ac7e writes one before it enables WCS. No source names this word.
            if (tx.is_write()) {
                auxiliary_ = value;
                if (!auxiliary_logged_) {
                    auxiliary_logged_ = true;
                    std::fprintf(stderr, "BSA_WATCHDOG_RETAINED_WORD model=%s offset=0x100 value=0x%x\n",
                                 name(), value);
                }
            }
            result = auxiliary_;
            break;
        default:
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        store(tx, result);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_roots.h"

namespace qcom_vp {

// CPUCP runtime table selection shares the existing RCG source and output state.
class QcomRuntimeClockRoot : public QcomClockRoot {
public:
    tlm_utils::simple_target_socket<QcomRuntimeClockRoot> runtime_socket{"runtime_socket"};
    explicit QcomRuntimeClockRoot(sc_core::sc_module_name name) : QcomClockRoot(name) {
        runtime_socket.register_b_transport(this, &QcomRuntimeClockRoot::runtime_transport);
    }
    void cold_reset() override {
        levels_ = {};
        programmed_ = 0;
        control_ = 0;
        selector_ = 0;
        request_ = false;
        QcomClockRoot::cold_reset();
    }
private:
    std::array<std::uint32_t, 4> levels_{};
    std::uint32_t programmed_ = 0;
    std::uint32_t control_ = 0;
    std::uint32_t selector_ = 0;
    bool request_ = false;
    void runtime_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        refresh();
        const auto offset = tx.get_address();
        auto value = tx.is_write() ? clock_detail::load(tx) : 0;
        if (offset == 0) {
            if (tx.is_write()) control_ = value;
            else value = control_;
        } else if (offset >= 8 && offset < 0x18) {
            const unsigned level = (offset - 8) / 4;
            if (tx.is_write()) {
                if (value & ~0x71fu) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                levels_[level] = value;
                programmed_ |= 1u << level;
            } else value = levels_[level];
        } else if (offset == 0x20) {
            if (tx.is_write()) selector_ = value;
            else value = selector_;
        } else if (offset == 0x24) {
            if (tx.is_write()) {
                request_ = value & 1;
                const unsigned level = selector_ & 15;
                if (request_ && level < 4 && (programmed_ & (1u << level)) && (control_ & 1)) {
                    request_configuration({levels_[level], 0, 0, 0});
                }
            } else {
                const unsigned level = selector_ & 15;
                value = request_;
                if (request_ && level < 4 && (programmed_ & (1u << level)) &&
                    (control_ & 1) && !root_.pending && frequency(root_.active)) value |= 0x80000000u;
            }
        } else {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) clock_detail::store(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

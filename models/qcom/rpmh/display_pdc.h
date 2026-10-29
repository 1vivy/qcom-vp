// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <algorithm>
#include <iomanip>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// The display DRV retains IRQ controls. Its configuration does not create events.
class QcomDisplayPdc : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomDisplayPdc> target_socket{"target_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomDisplayPdc> command_socket{"command_socket"};
    cci::cci_param<std::string> command_table{"command_table", "",
        "Rows contain the TCS index, command index, resource address, and initial value"};

    explicit QcomDisplayPdc(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomDisplayPdc::transport);
        command_socket.register_b_transport(this, &QcomDisplayPdc::transport_command);
    }

private:
    static constexpr unsigned pin_count = 20;
    static constexpr unsigned max_tcs = 15;
    static constexpr unsigned max_commands = 7;
    struct Command {
        std::uint32_t address = 0;
        std::uint32_t value = 0;
        bool present = false;
    };
    std::array<std::uint32_t, pin_count> irq_configuration_{};
    std::array<std::array<Command, max_commands>, max_tcs> commands_{};
    std::array<std::uint32_t, 2> match_{};
    std::uint32_t start_ = 0;
    unsigned tcs_count_ = 0;
    unsigned command_count_ = 0;

    void end_of_elaboration() override {
        std::istringstream input(command_table.get_value());
        std::string row;
        while (std::getline(input, row, ';')) {
            std::istringstream fields(row);
            unsigned tcs = 0, slot = 0;
            std::uint32_t address = 0, value = 0;
            std::string extra;
            if (!(fields >> tcs >> slot >> std::setbase(0) >> address >> value) ||
                (fields >> extra) || tcs >= max_tcs || slot >= max_commands ||
                (address & ~0x7fffcu) || value > 0x1fff || commands_[tcs][slot].present)
                throw std::invalid_argument("The display PDC command table is invalid");
            commands_[tcs][slot] = {address, value, true};
            tcs_count_ = std::max(tcs_count_, tcs + 1);
            command_count_ = std::max(command_count_, slot + 1);
        }
    }

    static void retained(tlm::tlm_generic_payload& tx, std::uint32_t& value) {
        if (tx.is_write()) value = clock_detail::load(tx);
        else clock_detail::store(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    static void read_only(tlm::tlm_generic_payload& tx, std::uint32_t value) {
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        clock_detail::store(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset >= 0x110 && offset < 0x110 + 4 * pin_count) {
            retained(tx, irq_configuration_[(offset - 0x110) / 4]);
            return;
        }
        if (offset == 0x38 || offset == 0x40) {
            retained(tx, match_[offset == 0x40 ? 0 : 1]);
            return;
        }
        if (offset == 0x48) { retained(tx, start_); return; }
        if (offset == 0x1004) {
            // There is no capture producer. TCS fields describe the explicit table.
            read_only(tx, (tcs_count_ << 8) | (command_count_ << 5));
            return;
        }
        if (offset >= 0x1200 && offset < 0x1250) {
            const auto member = (offset - 0x1200) % 0x14;
            if (member == 0 || member == 4 || member == 8) {
                read_only(tx, 0);
                return;
            }
        }
        if (offset >= 0x5000) {
            const auto relative = offset - 0x5000;
            const auto tcs = relative / 0xc8;
            if (tcs < tcs_count_ && relative % 0xc8 == 0) {
                std::uint32_t enabled = 0;
                for (unsigned slot = 0; slot < command_count_; ++slot)
                    if (commands_[tcs][slot].present) enabled |= 1u << slot;
                read_only(tx, enabled);
                return;
            }
        }
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }

    void transport_command(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        const auto tcs = offset / 0xc8;
        const auto relative = offset % 0xc8;
        const auto slot = relative / 0x10;
        const auto member = relative % 0x10;
        if (tcs < tcs_count_ && slot < command_count_ && (member == 0 || member == 4)) {
            const auto& command = commands_[tcs][slot];
            read_only(tx, member == 0 ? command.address : command.value);
            return;
        }
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace qcom_vp {

// One CESTA instance with its five CRM banks. The banks retain the guest program.
// Writes do not execute hardware instructions. Platform data lists the registers that
// the selected firmware programs in each bank. Other offsets fail.
class QcomCesta : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCesta> crmb_socket{"crmb_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCesta> crmb_pt_socket{"crmb_pt_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCesta> crmc_socket{"crmc_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCesta> crmv_socket{"crmv_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCesta> common_socket{"common_socket"};
    // Each entry is offset[:count[:stride]]. The stride defaults to four bytes.
    cci::cci_param<std::string> crmb_registers{"crmb_registers", "", "CRMB register offsets"};
    cci::cci_param<std::string> crmb_pt_registers{"crmb_pt_registers", "", "CRMB_PT register offsets"};
    cci::cci_param<std::string> crmc_registers{"crmc_registers", "", "CRMC register offsets"};
    cci::cci_param<std::string> crmv_registers{"crmv_registers", "", "CRMV register offsets"};
    cci::cci_param<std::string> common_registers{"common_registers", "", "CRM common register offsets"};

    explicit QcomCesta(sc_core::sc_module_name name) : sc_module(name) {
        crmb_socket.register_b_transport(this, &QcomCesta::transport_crmb);
        crmb_pt_socket.register_b_transport(this, &QcomCesta::transport_crmb_pt);
        crmc_socket.register_b_transport(this, &QcomCesta::transport_crmc);
        crmv_socket.register_b_transport(this, &QcomCesta::transport_crmv);
        common_socket.register_b_transport(this, &QcomCesta::transport_common);
    }

private:
    struct Bank {
        std::vector<std::uint32_t> words;
        std::vector<bool> declared;
        explicit Bank(std::size_t bytes) : words(bytes / 4), declared(bytes / 4) {}
    };
    Bank crmb_{0x400}, crmb_pt_{0x400}, crmc_{0x2000}, crmv_{0x600}, common_{0x200};

    static void declare(Bank& bank, const std::string& list, const char* label) {
        std::istringstream entries(list);
        std::string entry;
        while (entries >> entry) {
            std::uint64_t field[3]{0, 1, 4};
            std::istringstream parts(entry);
            std::string part;
            unsigned index = 0;
            while (std::getline(parts, part, ':')) {
                if (index == 3) throw std::invalid_argument(std::string("Malformed CESTA entry in ") + label);
                field[index++] = std::stoull(part, nullptr, 0);
            }
            if (!index || !field[1] || (field[0] & 3) || (field[2] & 3) || !field[2])
                throw std::invalid_argument(std::string("Malformed CESTA entry in ") + label);
            for (std::uint64_t i = 0; i < field[1]; ++i) {
                const auto offset = field[0] + i * field[2];
                if (offset / 4 >= bank.declared.size())
                    throw std::invalid_argument(std::string("CESTA register outside ") + label);
                bank.declared[offset / 4] = true;
            }
        }
    }
    void end_of_elaboration() override {
        declare(crmb_, crmb_registers.get_value(), "CRMB");
        declare(crmb_pt_, crmb_pt_registers.get_value(), "CRMB_PT");
        declare(crmc_, crmc_registers.get_value(), "CRMC");
        declare(crmv_, crmv_registers.get_value(), "CRMV");
        declare(common_, common_registers.get_value(), "common");
    }
    static void access(tlm::tlm_generic_payload& tx, Bank& bank) {
        if (!clock_detail::valid(tx)) return;
        const auto index = tx.get_address() / 4;
        if (index >= bank.declared.size() || !bank.declared[index]) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) bank.words[index] = clock_detail::load(tx);
        else clock_detail::store(tx, bank.words[index]);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void transport_crmb(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { access(tx, crmb_); }
    void transport_crmb_pt(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { access(tx, crmb_pt_); }
    void transport_crmc(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { access(tx, crmc_); }
    void transport_crmv(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { access(tx, crmv_); }
    void transport_common(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { access(tx, common_); }
};

} // namespace qcom_vp

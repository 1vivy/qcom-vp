// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include "rsc.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// Version 2.0 selects the BCM encoding used by the shared RPMh backend.
class QcomBcmStatus : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomBcmStatus> target_socket{"target_socket"};
    cci::cci_param<std::string> resource_owner_path{"resource_owner_path", "",
        "Path to the shared RPMh resource owner"};
    cci::cci_param<std::string> resource_vcds{"resource_vcds", "",
        "Rows contain a BCM resource address and its command-database VCD"};

    explicit QcomBcmStatus(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomBcmStatus::transport);
    }

private:
    QcomRpmhRsc* owner_ = nullptr;
    std::array<std::uint32_t, 16> resource_mask_{};

    void end_of_elaboration() override {
        const auto& path = resource_owner_path.get_value();
        owner_ = dynamic_cast<QcomRpmhRsc*>(sc_core::sc_find_object(path.c_str()));
        if (!owner_ && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            owner_ = dynamic_cast<QcomRpmhRsc*>(sc_core::sc_find_object(relative.c_str()));
        }
        if (!owner_) throw std::invalid_argument("The BCM status view requires a resource owner");
        std::istringstream input(resource_vcds.get_value());
        std::string row;
        std::uint32_t assigned = 0;
        while (std::getline(input, row, ';')) {
            std::istringstream fields(row);
            std::uint32_t address = 0;
            unsigned vcd = 0;
            std::string extra;
            if (!(fields >> std::setbase(0) >> address >> vcd) || (fields >> extra) ||
                address < 0x50000 || address > 0x5007c || (address & 3) || vcd >= resource_mask_.size())
                throw std::invalid_argument("The BCM VCD map is invalid");
            const auto bit = 1u << ((address - 0x50000) / 4);
            if (assigned & bit) throw std::invalid_argument("The BCM resource has two VCD mappings");
            assigned |= bit;
            resource_mask_[vcd] |= bit;
        }
        if (!assigned) throw std::invalid_argument("The BCM status view requires a VCD map");
    }

    void start_of_simulation() override {
        for (const auto mask : resource_mask_) {
            for (unsigned slot = 0; slot < 32; ++slot) {
                if (!(mask & (1u << slot))) continue;
                bool pending = false;
                if (!owner_->resource_pending(0x50000 + 4 * slot, pending))
                    throw std::invalid_argument("The BCM VCD map names an unsupported resource");
            }
        }
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        const auto vcd = offset >= 0x800 ? (offset - 0x800) / 4 : resource_mask_.size();
        const bool status = vcd < resource_mask_.size() && resource_mask_[vcd];
        if (offset != 0x1908 && !status) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        std::uint32_t value = 0x00020000;
        if (status) {
            value = 0;
            const auto mask = resource_mask_[vcd];
            for (unsigned slot = 0; slot < 32; ++slot) {
                if (!(mask & (1u << slot))) continue;
                bool pending = false;
                if (!owner_->resource_pending(0x50000 + 4 * slot, pending)) {
                    tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                    return;
                }
                value |= pending ? 1u : 0u;
            }
        }
        clock_detail::store(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

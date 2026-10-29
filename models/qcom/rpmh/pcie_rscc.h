// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <array>
#include <cstdint>

namespace qcom_vp {

// PCIe RSCC. The block keeps the words that the selected UEFI RscDxe writes.
// It has no sequencer and no hidden-TCS execution.
//
// The driver reads the identification words and the configuration words.
// It copies the five-word PCIe sequence into the simple sequence memory.
// It sets the PDC timer owner. It writes the three hidden-TCS command data words.
// It sets the override start address. It writes no branch address.
// It starts no sequence and polls no status.
// Register names and offsets: PCIE_RSCC and RSCC.
// The decode of the selected binary: models/qcom/rpmh/evidence.md.
class QcomPcieRscc : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomPcieRscc> target_socket{"target_socket"};

    explicit QcomPcieRscc(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomPcieRscc::transport);
    }

private:
    // RSC_ID_DRVd_POR and RSC_PARAM_SOLVER_CONFIG_DRVd_POR.
    // Both words are virtual for the selected target.
    static constexpr std::uint32_t id_reset = 0x00030800;
    static constexpr std::uint32_t solver_config_reset = 0x00010100;
    // RSC_PARAM_RSC_CONFIG_DRVd_POR value 0x01100214: NUM_DRV is one,
    // NUM_SEQ_CMD_WORDS is 0x10, NUM_TS_EVENTS is two, and the delay width is 0x14.
    static constexpr std::uint32_t num_seq_cmd_words = 0x10;
    static constexpr std::uint32_t rsc_config_reset =
        (1u << 24) | (num_seq_cmd_words << 16) | 0x214;
    // RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRVd_POR.
    static constexpr std::uint32_t parent_child_reset = 0x08000001;
    // RSC_SEQ_MEM_m_DRV0 holds m in 0..15.
    static constexpr std::uint32_t sequence_base = 0x370;
    // RSC_SEQ_OVERRIDE_START_ADDR_DRV0: VALID and ADDR.
    static constexpr std::uint32_t override_mask = 0x800003ff;
    // HIDDEN_TCS_CTRL_DRV0 holds the PDC timer owner in bits 1:0.
    static constexpr std::uint32_t owner_mask = 0x3;

    // The PCIe branch list holds only address zero, so the driver writes no branch register.
    std::array<std::uint32_t, num_seq_cmd_words> sequence_{};
    std::array<std::uint32_t, 3> hidden_data_{};
    std::uint32_t owner_ = 0;
    std::uint32_t override_ = 0;

    static void read_only(tlm::tlm_generic_payload& tx, std::uint32_t value) {
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        clock_detail::store(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    static void retained(tlm::tlm_generic_payload& tx, std::uint32_t& cell,
                         std::uint32_t mask) {
        if (tx.is_write()) {
            const auto value = clock_detail::load(tx);
            if (value & ~mask) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            cell = value;
        } else {
            clock_detail::store(tx, cell);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = static_cast<std::uint32_t>(tx.get_address());
        if (offset == 0x0) return read_only(tx, id_reset);
        if (offset == 0x4) return read_only(tx, solver_config_reset);
        if (offset == 0x8) return read_only(tx, rsc_config_reset);
        if (offset == 0xc) return read_only(tx, parent_child_reset);
        if (offset == 0x1c) return retained(tx, owner_, owner_mask);
        if (offset == 0x38) return retained(tx, hidden_data_[0], 0x80ffffff);
        if (offset == 0x40) return retained(tx, hidden_data_[1], 0xffffffff);
        if (offset == 0x48) return retained(tx, hidden_data_[2], 0x8000ffff);
        if (offset == 0x220) return retained(tx, override_, override_mask);
        if (offset >= sequence_base && offset < sequence_base + 4 * num_seq_cmd_words &&
            !(offset & 3))
            return retained(tx, sequence_[(offset - sequence_base) / 4], 0xffffffff);
        // Branches, force wake, busy, the program counter, timestamps, and triggers
        // have no model. The selected driver writes none of them.
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

} // namespace qcom_vp

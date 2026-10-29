// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <array>
#include <cstdint>

namespace qcom_vp {

// TITAN camera RSCC. The block keeps the values that the selected UEFI RscDxe
// reads and writes. It does not start the command sequencer.
//
// The driver reads the identification words. It reads the configuration words.
// It copies the camera command sequence into the CHN_TCS sequence memory.
// It writes the branch table. It sets the override start address.
// It writes no override trigger and it polls no busy bit.
// An offset without a model fails.
//
// Register names and offsets: TITAN_RSCC.
// The decode of the selected binary: models/qcom/rpmh/evidence.md.
class QcomTitanRscc : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomTitanRscc> target_socket{"target_socket"};

    explicit QcomTitanRscc(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomTitanRscc::transport);
    }

private:
    // TITAN_RSCC_RSC_ID_DRVd: MAJOR_VER, MINOR_VER and STEP_VER.
    // The power-on value is virtual for SM8850.
    static constexpr std::uint32_t id_reset = 0x00030800;
    // TITAN_RSCC_RSC_PARAM_SOLVER_CONFIG_DRVd power-on value.
    static constexpr std::uint32_t solver_config_reset = 0x00010100;
    // TITAN_RSCC_RSC_PARAM_RSC_CONFIG_DRVd power-on value.
    // NUM_DRV is three, NUM_SEQ_CMD_WORDS is 0x10, and DELAY_CNTR_BITWIDTH is 0x14.
    static constexpr std::uint32_t rsc_config_reset = 0x03100214;
    // TITAN_RSCC_RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRVd power-on value.
    static constexpr std::uint32_t parent_child_reset = 0x60004104;
    // TITAN_RSCC_RSC_SEQ_CFG_BR_ADDR_i_CHN_TCS_DRV0 holds i in 0..15.
    static constexpr std::uint32_t branch_base = 0x420;
    static constexpr std::uint32_t branch_count = 16;
    // TITAN_RSCC_SEQ_MEM_CHN_TCS_m_DRV0 holds m in 0..23.
    static constexpr std::uint32_t sequence_base = 0x4b8;
    static constexpr std::uint32_t sequence_words = 24;
    // TITAN_RSCC_RSC_SEQ_OVERRIDE_START_ADDR_DRV0: VALID and ADDR.
    static constexpr std::uint32_t override_mask = 0x800003ff;
    // TITAN_RSCC_RSC_SEQ_CFG_BR_ADDR_i_CHN_TCS_DRV0 ADDR field.
    static constexpr std::uint32_t branch_mask = 0x3ff;
    // TITAN_RSCC_HIDDEN_TCS_CTRL_DRV0: HIDDEN_TCS_PDC_TIMER_DRV_OWNER bits 1:0.
    // RscDxe 0x38f0 changes only the owner. The model has no hidden TCS, so a
    // HIDDEN_TCS_TRIGGER (bit 16) write fails.
    static constexpr std::uint32_t hidden_tcs_owner_mask = 0x3;

    std::array<std::uint32_t, branch_count> branch_{};
    std::array<std::uint32_t, sequence_words> sequence_{};
    std::uint32_t override_ = 0;
    std::uint32_t hidden_tcs_control_ = 0;

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
        if (offset == 0x1c) return retained(tx, hidden_tcs_control_, hidden_tcs_owner_mask);
        if (offset == 0x220) return retained(tx, override_, override_mask);
        // The model runs no sequencer, because RscDxe writes no start request.
        // Busy (0x224), the program counter (0x228), and the trigger (0x280) fail.
        if (offset >= branch_base && offset < branch_base + 4 * branch_count &&
            !(offset & 3))
            return retained(tx, branch_[(offset - branch_base) / 4], branch_mask);
        if (offset >= sequence_base && offset < sequence_base + 4 * sequence_words &&
            !(offset & 3))
            return retained(tx, sequence_[(offset - sequence_base) / 4], 0xffffffff);
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

} // namespace qcom_vp

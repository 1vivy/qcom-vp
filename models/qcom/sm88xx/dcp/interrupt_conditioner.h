// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <array>
#include <cstdint>
#include <iostream>

namespace qcom_vp {

// This owner observes interrupt sources. It does not gate architectural delivery.
// The wake interpretation is a functional policy inferred from the PM callers.
class DcpInterruptConditioner : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<DcpInterruptConditioner> mem{"mem"};
    tlm_utils::simple_target_socket_optional<DcpInterruptConditioner> auxiliary{"auxiliary"};
    sc_core::sc_vector<TargetSignalSocket<bool>> plic_source{"plic_source", 52};
    sc_core::sc_vector<TargetSignalSocket<bool>> local_source{"local_source", 16};
    sc_core::sc_vector<InitiatorSignalSocket<bool>> plic_wake{"plic_wake", 52};
    sc_core::sc_vector<InitiatorSignalSocket<bool>> local_wake{"local_wake", 16};
    // Neither FDT specifies silicon reset types. Zero selects the functional
    // all-level policy, not a pending event or a measured silicon reset value.
    cci::cci_param<uint64_t> plic_edge_reset{"plic_edge_reset", 0,
        "Board edge types: bit zero is PLIC source one; default all-level policy"};
    cci::cci_param<uint32_t> local_edge_reset{"local_edge_reset", 0,
        "Board edge types: bits zero through four are CPU IRQ21 through IRQ25"};

    explicit DcpInterruptConditioner(sc_core::sc_module_name name) : sc_module(name) {
        mem.register_b_transport(this, &DcpInterruptConditioner::transport);
        auxiliary.register_b_transport(this, &DcpInterruptConditioner::transport);
        for (unsigned i = 0; i < plic_source.size(); ++i)
            plic_source[i].register_value_changed_cb([this, i](const bool& value) {
                const uint64_t bit = uint64_t{1} << i;
                if (value && !(plic_level_ & bit)) plic_latched_ |= bit & plic_type_;
                plic_level_ = (plic_level_ & ~bit) | (value ? bit : 0);
                update();
            });
        for (unsigned i = 0; i < local_source.size(); ++i)
            local_source[i].register_value_changed_cb([this, i](const bool& value) {
                const uint32_t bit = uint32_t{1} << i;
                if (value && !(local_level_ & bit)) local_latched_ |= bit & local_edges();
                local_level_ = (local_level_ & ~bit) | (value ? bit : 0);
                update();
            });
    }

private:
    static constexpr uint64_t plic_bits = (uint64_t{1} << 52) - 1;
    // SM8850 0xa00eadb6/0xa00eb020 and SM8845 0xa41171ca read +0x280 into PM log records.
    // No decoded consumer gives it semantics, so it keeps the retained exploratory policy.
    static constexpr unsigned bank_bytes = 0x284;
    static constexpr unsigned bank_words = bank_bytes / 4;
    std::array<uint32_t, bank_words> configuration_{};
    std::array<bool, bank_words> warned_{};
    uint64_t plic_level_ = 0, plic_latched_ = 0, plic_type_ = 0, plic_mask_ = 0;
    uint32_t local_level_ = 0, local_latched_ = 0, local_type_ = 0, local_mask_ = 0;
    uint32_t auxiliary_configuration_ = 0;
    bool auxiliary_warned_ = false;

    void end_of_elaboration() override {
        plic_type_ = plic_edge_reset.get_value() & plic_bits;
        local_type_ = local_edge_reset.get_value() & 0x1f;
        // Source sockets can receive values during binding. Preserve those levels,
        // but do not turn a configuration change into a synthetic rising edge.
        plic_latched_ = 0;
        local_latched_ = 0;
    }
    void start_of_simulation() override { update(); }
    uint32_t local_edges() const { return local_type_ << 5; }
    uint64_t plic_pending() const {
        return ((plic_level_ & ~plic_type_) | plic_latched_) & plic_bits;
    }
    uint32_t local_pending() const {
        return ((local_level_ & ~local_edges()) | local_latched_) & 0xffff;
    }
    void update() {
        const uint64_t plic_active = plic_pending() & plic_mask_;
        const uint32_t local_active = local_pending() & local_mask_;
        for (unsigned i = 0; i < plic_wake.size(); ++i)
            for (int p = 0; p < plic_wake[i].size(); ++p)
                plic_wake[i][p]->write((plic_active & (uint64_t{1} << i)) != 0);
        for (unsigned i = 0; i < local_wake.size(); ++i)
            for (int p = 0; p < local_wake[i].size(); ++p)
                local_wake[i][p]->write((local_active & (uint32_t{1} << i)) != 0);
    }
    static bool known(unsigned offset) {
        switch (offset) {
        case 0: case 4: case 0x80: case 0x84: case 0x100: case 0x104:
        case 0x180: case 0x184: case 0x200: case 0x210: case 0x218: return true;
        default: return false;
        }
    }
    uint32_t read_word(unsigned offset) const {
        switch (offset) {
        case 0: return uint32_t(plic_mask_);
        case 4: return uint32_t(plic_mask_ >> 32);
        case 0x80: return uint32_t(plic_pending());
        case 0x84: return uint32_t(plic_pending() >> 32);
        case 0x100: return uint32_t(plic_type_);
        case 0x104: return uint32_t(plic_type_ >> 32);
        case 0x180: case 0x184: case 0x218: return 0;
        case 0x200: return local_mask_;
        case 0x210: return local_type_;
        default: return configuration_[offset / 4];
        }
    }
    void write_word(unsigned offset, uint32_t value) {
        switch (offset) {
        case 0: plic_mask_ = (plic_mask_ & 0xffffffff00000000ULL) | value; break;
        case 4: plic_mask_ = (plic_mask_ & 0xffffffffULL) | (uint64_t{value} << 32); break;
        case 0x80: case 0x84: break; // Source-owned pending status.
        case 0x100:
            plic_type_ = (plic_type_ & 0xffffffff00000000ULL) | value;
            plic_latched_ &= plic_type_;
            break;
        case 0x104:
            plic_type_ = (plic_type_ & 0xffffffffULL) | (uint64_t{value} << 32);
            plic_latched_ &= plic_type_;
            break;
        case 0x180: plic_latched_ &= ~uint64_t{value}; break;
        case 0x184: plic_latched_ &= ~(uint64_t{value} << 32); break;
        case 0x200: local_mask_ = value & 0xffff; break;
        case 0x210:
            local_type_ = value & 0x1f;
            local_latched_ &= local_edges();
            break;
        case 0x218: local_latched_ &= ~((value & 0x1f) << 5); break;
        default: configuration_[offset / 4] = value; break;
        }
        plic_type_ &= plic_bits;
        plic_mask_ &= plic_bits;
        update();
    }
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const uint64_t address = tx.get_address();
        const bool auxiliary_access = address == 0xe11104;
        if (!auxiliary_access && (address < 0xe10000 || address >= 0xe10000 + bank_bytes)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || (address & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const unsigned offset = auxiliary_access ? 0 : unsigned(address - 0xe10000);
        bool& warned = auxiliary_access ? auxiliary_warned_ : warned_[offset / 4];
        if ((auxiliary_access || !known(offset)) && !warned) {
            std::cerr << name() << ": unexplained DCP configuration at 0x" << std::hex
                      << address << std::dec << ". Configuration retained without completion effect\n";
            warned = true;
        }
        auto* data = tx.get_data_ptr();
        if (tx.is_write()) {
            uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= uint32_t(data[i]) << (8 * i);
            if (auxiliary_access) auxiliary_configuration_ = value;
            else write_word(offset, value);
        } else {
            const uint32_t value = auxiliary_access ? auxiliary_configuration_ : read_word(offset);
            for (unsigned i = 0; i < 4; ++i) data[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <gs_memory.h>
#include "qcom/apss/execution_group.h"
#include <tlm-extensions/mem-tx-attrs.h>
#include <algorithm>

namespace qcom_vp {

// Native siblings share executable RAM. External writers remain observable.
class SharedExecutableMemory : public gs::gs_memory<> {
public:
    SharedExecutableMemory(sc_core::sc_module_name name, sc_core::sc_object* owner)
        : gs::gs_memory<>(name), views_(owner) {}

private:
    bool relative_addresses_ = true;
    NativeExecutionViews views_;

    void start_of_simulation() override {
        views_.start();
        if (!size() || base() > UINT64_MAX - (size() - 1)) {
            throw std::invalid_argument("Executable memory has an invalid address range");
        }
        auto broker = cci::cci_get_broker();
        gs::cci_get<bool>(broker, std::string(name()) + ".target_socket.relative_addresses",
                          relative_addresses_);
    }

    void end_of_simulation() override {
        views_.finish_writes();
    }

    bool is_owner(tlm::tlm_generic_payload& tx) const {
        return views_.contains(tx);
    }

    bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& tx, tlm::tlm_dmi& dmi) override {
        return is_owner(tx) && gs::gs_memory<>::get_direct_mem_ptr(id, tx, dmi);
    }

    void b_transport(int id, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) override {
        const bool owner = is_owner(tx);
        const uint64_t address = tx.get_address();
        const uint64_t length = tx.get_data_length();
        const uint64_t offset = relative_addresses_ ? address : address - base();
        if ((owner && !views_.has_participant()) || !tx.is_write() || !length || p_rom || !p_dmi ||
            sc_core::sc_get_status() != sc_core::SC_RUNNING ||
            (!relative_addresses_ && address < base()) || offset > size() || length > size() - offset ||
            tx.get_streaming_width() < length) {
            gs::gs_memory<>::b_transport(id, tx, delay);
            if (!owner) tx.set_dmi_allowed(false);
            return;
        }

        qemu::MemoryRegionOps::MemTxAttrs attrs;
        if (const auto* extension = tx.get_extension<QemuMemTxAttrsExtension>())
            attrs = extension->attrs;
        auto* enables = tx.get_byte_enable_ptr();
        const unsigned enable_length = tx.get_byte_enable_length();
        unsigned position = 0;
        while (position < length) {
            if (enables && enables[position % enable_length] != TLM_BYTE_ENABLED) {
                ++position;
                continue;
            }
            tlm::tlm_generic_payload probe;
            probe.set_address(address + position);
            tlm::tlm_dmi dmi;
            const bool mapped = gs::gs_memory<>::get_direct_mem_ptr(id, probe, dmi);
            probe.clear_extension<gs::ShmemIDExtension>();
            if (!mapped) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                tx.set_dmi_allowed(false);
                return;
            }
            unsigned count = std::min<uint64_t>(length - position,
                                                dmi.get_end_address() - probe.get_address() + 1);
            if (enables) {
                unsigned enabled = 1;
                while (enabled < count &&
                       enables[(position + enabled) % enable_length] == TLM_BYTE_ENABLED)
                    ++enabled;
                count = enabled;
            }
            // Each instance needs its own native RAM write for synchronous TB invalidation.
            const auto result = views_.write(dmi, probe.get_address(),
                                             tx.get_data_ptr() + position, count, attrs);
            if (result != qemu::MemoryRegionOps::MemTxOK) {
                tx.set_response_status(result == qemu::MemoryRegionOps::MemTxDecodeError
                                           ? tlm::TLM_ADDRESS_ERROR_RESPONSE
                                           : tlm::TLM_GENERIC_ERROR_RESPONSE);
                tx.set_dmi_allowed(false);
                return;
            }
            position += count;
        }
        delay += p_latency;
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
        tx.set_dmi_allowed(owner);
    }
};

}

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace qcom_vp {

// Immediate setup only. No fault producer, IRQ, clear-bit, or permission API.
// See evidence.md for the bounded retained-word substitute contract.
class QcomFaultMonitor : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomFaultMonitor> target_socket{"target_socket"};
    cci::cci_param<std::string> configuration_offsets{"configuration_offsets", "",
        "Explicit retained setup words, relative to this aperture"};
    explicit QcomFaultMonitor(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomFaultMonitor::transport);
    }
private:
    struct Word { std::uint32_t offset; std::uint32_t value = 0; };
    std::vector<Word> words_;
    void end_of_elaboration() override {
        std::istringstream input(configuration_offsets.get_value());
        std::string token;
        while (std::getline(input, token, ',')) {
            std::size_t used = 0;
            const auto offset = std::stoull(token, &used, 0);
            if (used != token.size() || offset > UINT32_MAX || (offset & 3))
                throw std::invalid_argument("Invalid fault monitor setup offset");
            words_.push_back({static_cast<std::uint32_t>(offset), 0});
        }
        if (words_.empty()) throw std::invalid_argument("Fault monitor requires explicit setup words");
        std::sort(words_.begin(), words_.end(), [](const Word& a, const Word& b) {
            return a.offset < b.offset;
        });
        for (std::size_t i = 1; i < words_.size(); ++i)
            if (words_[i - 1].offset == words_[i].offset)
                throw std::invalid_argument("Duplicate fault monitor setup offset");
    }
    void reject(tlm::tlm_generic_payload& tx, tlm::tlm_response_status status) {
        std::fprintf(stderr, "%s: rejected fault setup %s offset=0x%llx size=%u\n", name(),
            tx.is_write() ? "write" : tx.is_read() ? "read" : "command",
            static_cast<unsigned long long>(tx.get_address()), tx.get_data_length());
        tx.set_response_status(status);
    }
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            reject(tx, tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || (tx.get_address() & 3)) {
            reject(tx, tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto it = std::lower_bound(words_.begin(), words_.end(), tx.get_address(),
            [](const Word& word, std::uint64_t address) { return word.offset < address; });
        if (it == words_.end() || it->offset != tx.get_address()) {
            reject(tx, tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* bytes = tx.get_data_ptr();
        if (tx.is_write()) {
            it->value = std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8) |
                (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
        } else {
            for (unsigned i = 0; i < 4; ++i)
                bytes[i] = static_cast<unsigned char>(it->value >> (8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};
} // namespace qcom_vp

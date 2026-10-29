// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace qcom_vp {

// Clock-gating override words of one block. The CPUCP post-ROM sequence
// clears the override fields so that hardware gates its own clocks. Clock
// gating has no firmware-visible effect in this platform, so the owner retains
// the words and creates no clock, power, or completion effect.
// Evidence: models/qcom/sm88xx/cpucp/subsystem_evidence.md, "Clock-gating overrides".
class QcomClockGatingOverrides : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomClockGatingOverrides> target_socket{"target_socket"};
    // Space-separated offset:reset:mask[:width] entries in hexadecimal, relative
    // to the socket. Width is 4 or 8 bytes. A doubleword also accepts aligned
    // 32-bit halves. The default width is 8.
    cci::cci_param<std::string> words{"words", "", "Declared offset:reset:mask[:width] words"};

    explicit QcomClockGatingOverrides(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomClockGatingOverrides::transport);
    }

    void cold_reset() {
        for (auto& word : words_) word.value = word.reset;
    }

private:
    struct Word {
        std::uint64_t offset = 0;
        std::uint64_t reset = 0;
        std::uint64_t mask = 0;
        unsigned width = 8;
        std::uint64_t value = 0;
    };
    std::vector<Word> words_;

    void end_of_elaboration() override {
        std::istringstream list(words.get_value());
        std::string entry;
        while (list >> entry) {
            Word word;
            std::vector<std::uint64_t> fields;
            std::istringstream parts(entry);
            std::string part;
            while (std::getline(parts, part, ':')) {
                std::size_t used = 0;
                try {
                    fields.push_back(std::stoull(part, &used, 16));
                } catch (const std::exception&) {
                    used = 0;
                }
                if (part.empty() || used != part.size())
                    throw std::invalid_argument("Invalid clock-gating word: " + entry);
            }
            if (fields.size() == 4) word.width = static_cast<unsigned>(fields[3]);
            if (fields.size() < 3 || fields.size() > 4 || (word.width != 4 && word.width != 8))
                throw std::invalid_argument("Invalid clock-gating word: " + entry);
            word.offset = fields[0];
            word.reset = fields[1];
            word.mask = fields[2];
            const auto limit = word.width == 8 ? ~UINT64_C(0) : UINT64_C(0xffffffff);
            if ((word.offset & (word.width - 1)) || (word.mask & ~limit) || (word.reset & ~word.mask))
                throw std::invalid_argument("Invalid clock-gating word: " + entry);
            for (const auto& other : words_)
                if (word.offset < other.offset + other.width && other.offset < word.offset + word.width)
                    throw std::invalid_argument("Overlapping clock-gating word: " + entry);
            words_.push_back(word);
        }
        if (words_.empty()) throw std::invalid_argument("Clock-gating overrides require declared words");
        cold_reset();
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const unsigned length = tx.get_data_length();
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || (length != 4 && length != 8) || tx.get_streaming_width() < length ||
            (tx.get_address() & (length - 1))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        Word* word = nullptr;
        for (auto& candidate : words_)
            if (tx.get_address() >= candidate.offset &&
                tx.get_address() + length <= candidate.offset + candidate.width)
                word = &candidate;
        if (!word) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const unsigned shift = static_cast<unsigned>(tx.get_address() - word->offset) * 8;
        const auto lane = length == 8 ? ~UINT64_C(0) : UINT64_C(0xffffffff) << shift;
        unsigned char* data = tx.get_data_ptr();
        if (tx.is_read()) {
            const auto value = word->value >> shift;
            for (unsigned i = 0; i < length; ++i) data[i] = static_cast<unsigned char>(value >> (8 * i));
        } else {
            std::uint64_t input = 0;
            for (unsigned i = 0; i < length; ++i) input |= std::uint64_t{data[i]} << (8 * i);
            const auto value = (word->value & ~lane) | ((input << shift) & lane);
            if (value & ~word->mask) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            word->value = value;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <vector>
#include <functional>
#include <limits>
#include <mutex>
#include <tlm_utils/simple_initiator_socket.h>

namespace qcom_vp {

// Memory remains coherent without private cache lines. Slice and power requests
// change controller state before completion. No image authorization occurs here.
class QcomLlcc : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomLlcc> target_socket{"target_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomLlcc> status_socket{"status_socket"};
    tlm_utils::simple_initiator_socket_optional<QcomLlcc> memory_socket{"memory_socket"};
    cci::cci_param<std::uint32_t> opaque_encryption_key_slots{
        "opaque_encryption_key_slots", 0, "Declared implicit encryption-only key domains"};
    cci::cci_param<unsigned> configuration_banks{"configuration_banks", 4, "Number of LLCC banks"};
    cci::cci_param<std::uint32_t> retained_extent{"retained_extent", 0x200000, "LLCC register aperture size"};
    cci::cci_param<std::string> configuration_offsets{"configuration_offsets", "", "Declared LLCC configuration words"};

    explicit QcomLlcc(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomLlcc::or_transport);
        status_socket.register_b_transport(this, &QcomLlcc::and_transport);
    }
    void attach_dare_memory(std::uint64_t base, std::uint64_t size,
                            std::function<void(std::uint64_t, std::uint64_t)> revoke) {
        std::lock_guard<std::recursive_mutex> lock(dare_mutex_);
        if (!size || base > UINT64_MAX - (size - 1))
            throw std::invalid_argument("Invalid LLCC memory interval");
        memories_.push_back({base, base + size - 1, std::move(revoke)});
    }

    bool dare_permits(std::uint64_t address, std::uint64_t length) const {
        std::lock_guard<std::recursive_mutex> lock(dare_mutex_);
        if (!length || address > UINT64_MAX - (length - 1)) return false;
        const auto last = address + length - 1;
        while (true) {
            if (!permits_at(address)) return false;
            const auto end = policy_end(address, last);
            if (end == last) return true;
            address = end + 1;
        }
    }

    void dare_observe_write(std::uint64_t address, std::uint64_t length) {
        std::lock_guard<std::recursive_mutex> lock(dare_mutex_);
        if (!length || address > UINT64_MAX - (length - 1)) return;
        const auto last = address + length - 1;
        for (auto& bank : contexts_)
            for (auto& context : bank)
                if (context.valid && context.aux_size &&
                    overlaps(address, last, context.aux, context.aux + context.aux_size - 1))
                    invalidate(context);
    }

    bool dare_constrain_dmi(std::uint64_t address, std::uint64_t& first,
                            std::uint64_t& last) const {
        std::lock_guard<std::recursive_mutex> lock(dare_mutex_);
        if (first > address || last < address || metadata_at(address) || !permits_at(address))
            return false;
        for_each_boundary([&](std::uint64_t boundary) {
            if (boundary <= address) first = std::max(first, boundary);
            else last = std::min(last, boundary - 1);
        });
        return true;
    }

    void cold_reset() {
        std::lock_guard<std::recursive_mutex> lock(dare_mutex_);
        ++configuration_generation_;
        for (auto& bank : contexts_)
            for (auto& context : bank) {
                invalidate(context);
                context = {};
            }
        for (auto& bank : key_generations_) bank.fill(0);
        for (auto& bank : key_written_) bank.fill(false);
        revoke_all();
        std::fill(values_.begin(), values_.end(), 0);
        ++values_version_;
        for (auto& bank : key_receipts_) bank.fill(0);
        for (unsigned bank = 0; bank < configuration_banks.get_value(); ++bank) {
            for (unsigned slice = 0; slice < slice_count; ++slice)
                value(bank, slice * 0x1000 + 4) = 2;
            value(bank, 0x64000) = 0x06000000;
            value(bank, 0x6400c) = configuration_banks.get_value() << 28;
        }
    }

private:
    static constexpr unsigned extent = 0x200000;
    static constexpr unsigned word_count = extent / 4;
    static constexpr unsigned slice_count = 64;
    std::vector<std::uint32_t> values_;
    std::vector<bool> declared_;
    static constexpr unsigned key_groups = 4;
    static constexpr unsigned keys_per_group = 4;
    static constexpr std::uint8_t complete_key = 0x3f;
    std::array<std::array<std::uint8_t, key_groups * keys_per_group>, 8> key_receipts_{};
    struct Memory {
        std::uint64_t first, last;
        std::function<void(std::uint64_t, std::uint64_t)> revoke;
    };
    struct Context {
        std::uint64_t first = 0, last = 0, aux = 0, aux_size = 0, generation = 0;
        std::uint32_t policy = 0, slot = 0;
        bool valid = false, requested = false;
    };
    mutable std::recursive_mutex dare_mutex_;
    std::vector<Memory> memories_;
    std::array<std::array<Context, 32>, 8> contexts_{};
    std::array<std::array<std::uint64_t, 16>, 8> key_generations_{};
    std::array<std::array<bool, 16>, 8> key_written_{};
    std::uint64_t configuration_generation_ = 0;
    // Decoded rows depend only on values_. Every values_ write bumps values_version_.
    // Callers hold dare_mutex_, so the mutable cache needs no other lock.
    std::uint64_t values_version_ = 0;
    mutable std::vector<Context> decoded_;
    mutable std::vector<std::uint64_t> decoded_version_;

    std::uint32_t word(unsigned bank, unsigned offset) const {
        // TME 0x44216c consumes bit seven after the row operation completes.
        if (offset >= 0x103500 && offset < 0x103580)
            return contexts_[bank][(offset - 0x103500) / 4].valid ? 0x80 : 0;
        return values_[static_cast<std::size_t>(bank) * word_count + offset / 4];
    }
    static bool overlaps(std::uint64_t a, std::uint64_t b,
                         std::uint64_t c, std::uint64_t d) {
        return a <= d && c <= b;
    }
    void revoke_range(std::uint64_t first, std::uint64_t last) {
        for (const auto& memory : memories_)
            if (memory.revoke && overlaps(first, last, memory.first, memory.last))
                memory.revoke(std::max(first, memory.first), std::min(last, memory.last));
    }
    void revoke_all() {
        for (const auto& memory : memories_)
            if (memory.revoke) memory.revoke(memory.first, memory.last);
    }
    void invalidate(Context& context) {
        if (context.valid) {
            context.valid = false;
            revoke_range(context.first, context.last);
        }
    }
    bool mapped(std::uint64_t first, std::uint64_t last) const {
        if (first > last) return false;
        while (true) {
            bool found = false;
            auto end = first;
            for (const auto& memory : memories_)
                if (memory.first <= first && memory.last >= first) {
                    found = true;
                    end = std::max(end, memory.last);
                }
            if (!found) return false;
            if (end >= last) return true;
            first = end + 1;
        }
    }
    Context configured(unsigned bank, unsigned row) const {
        const std::size_t index = static_cast<std::size_t>(bank) * 32 + row;
        if (decoded_.size() <= index) {
            decoded_.resize(static_cast<std::size_t>(configuration_banks.get_value()) * 32);
            decoded_version_.assign(decoded_.size(), UINT64_MAX);
        }
        if (decoded_version_[index] != values_version_) {
            decoded_[index] = decode(bank, row);
            decoded_version_[index] = values_version_;
        }
        return decoded_[index];
    }
    Context decode(unsigned bank, unsigned row) const {
        const unsigned base = 0x120000 + row * 0x1000;
        Context result;
        const auto wide = [&](unsigned offset) {
            return std::uint64_t{word(bank, base + offset)} |
                (std::uint64_t{word(bank, base + offset + 4)} << 32);
        };
        result.first = wide(0);
        result.last = wide(8) | 0xffff;
        result.policy = word(bank, base + 0x40);
        result.slot = word(bank, base + 0x44);
        result.aux = wide((result.policy & 0x20) ? 0x18 : 0x10);
        if (result.first <= result.last && result.last - result.first != UINT64_MAX) {
            const auto size = result.last - result.first + 1;
            if ((result.policy & 3) == 2)
                result.aux_size = (size / 8 + 0xffff) & 0x3fff0000ULL;
            else if ((result.policy & 3) == 3)
                result.aux_size = (size / 4 + size / 64 + size / 512 +
                                   size / 4096 + 0x107ff) & 0x7fff0000ULL;
        }
        return result;
    }
    bool enabled(unsigned bank, unsigned row) const {
        return (word(bank, 0x100088) & 1) && (word(bank, 0x103350) & (1U << row));
    }
    bool metadata_at(std::uint64_t address) const {
        for (unsigned bank = 0; bank < configuration_banks.get_value(); ++bank) {
            const auto rows = word(bank, 0x103350);
            for (unsigned row = 0; row < 32; ++row) {
                // Every DMA beat reaches this test. Skip disabled rows before decoding them.
                if (!(rows & (1U << row))) continue;
                const auto config = configured(bank, row);
                if (config.aux_size && address >= config.aux && address - config.aux < config.aux_size)
                    return true;
            }
        }
        return false;
    }
    bool permits_at(std::uint64_t address) const {
        if (metadata_at(address)) return true;
        for (unsigned bank = 0; bank < configuration_banks.get_value(); ++bank) {
            int selected = -1;
            Context config;
            for (unsigned row = 0; row < 32; ++row) {
                if (!enabled(bank, row)) continue;
                const auto candidate = configured(bank, row);
                if (candidate.first > address || candidate.last < address) continue;
                if (selected < 0 || (!(config.policy & 0x10000) && (candidate.policy & 0x10000))) {
                    selected = row;
                    config = candidate;
                }
            }
            if (selected < 0) continue;
            if (config.policy & 0x30) return false;
            const auto& context = contexts_[bank][selected];
            if (context.valid) {
                if (address < context.first || address > context.last) return false;
                if ((context.policy & 3) && (context.slot >= 16 ||
                    context.generation != key_generations_[bank][context.slot] ||
                    key_receipts_[bank][context.slot] != complete_key)) return false;
                continue;
            }
            if (context.requested) return false;
            if (!(config.policy & 3)) continue;
            if ((config.policy & 3) == 1 && config.slot < 16 &&
                !key_written_[bank][config.slot] &&
                (opaque_encryption_key_slots.get_value() & (1U << config.slot))) continue;
            return false;
        }
        return true;
    }
    template <typename F> void for_each_boundary(F visit) const {
        for (unsigned bank = 0; bank < configuration_banks.get_value(); ++bank)
            for (unsigned row = 0; row < 32; ++row) {
                if (!(word(bank, 0x103350) & (1U << row))) continue;
                const auto config = configured(bank, row);
                visit(config.first);
                if (config.last != UINT64_MAX) visit(config.last + 1);
                if (config.aux_size && config.aux <= UINT64_MAX - config.aux_size) {
                    visit(config.aux);
                    visit(config.aux + config.aux_size);
                }
                const auto& context = contexts_[bank][row];
                if (context.valid) {
                    visit(context.first);
                    if (context.last != UINT64_MAX) visit(context.last + 1);
                }
            }
    }
    std::uint64_t policy_end(std::uint64_t address, std::uint64_t last) const {
        for_each_boundary([&](std::uint64_t boundary) {
            if (boundary > address) last = std::min(last, boundary - 1);
        });
        return last;
    }
    bool zero_auxiliary(const Context& context) {
        if (!context.aux_size) return true;
        if (!memory_socket.size()) return false;
        std::array<unsigned char, 4096> zeros{};
        for (std::uint64_t done = 0; done < context.aux_size;) {
            const auto count = static_cast<unsigned>(std::min<std::uint64_t>(
                zeros.size(), context.aux_size - done));
            tlm::tlm_generic_payload tx;
            tx.set_command(tlm::TLM_WRITE_COMMAND);
            tx.set_address(context.aux + done);
            tx.set_data_ptr(zeros.data());
            tx.set_data_length(count);
            tx.set_streaming_width(count);
            tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            memory_socket->b_transport(tx, delay);
            if (tx.is_response_error() || tx.get_response_status() != tlm::TLM_OK_RESPONSE)
                return false;
            done += count;
        }
        return true;
    }
    bool prepare(unsigned bank, unsigned row, bool array, Context& context) const {
        context = configured(bank, row);
        context.requested = true;
        const auto base = 0x120000 + row * 0x1000;
        const auto mode = context.policy & 3;
        const bool submitted = !array && mode >= 2;
        if (context.policy & 0x30) {
            std::fprintf(stderr, "UNMODELED_LLCC_DARE_FEATURE model=%s bank=%u row=%u mte=%u ecc=%u\n",
                name(), bank, row, (context.policy >> 5) & 1U, (context.policy >> 4) & 1U);
            return false;
        }
        if (!memory_socket.size() || !(word(bank, 0x103350) & (1U << row)) ||
            context.first > context.last ||
            (context.first & 0xffff) || !mapped(context.first, context.last)) return false;
        if (!array && word(bank, base + 0x60) != (mode >= 2 ? 3U : 1U)) return false;
        // TME 0x441e6c starts command one from configured bounds without a submitted range.
        if (submitted) {
            const auto start = std::uint64_t{word(bank, base + 0x30)} |
                (std::uint64_t{word(bank, base + 0x34)} << 32);
            const auto end = (std::uint64_t{word(bank, base + 0x38)} |
                (std::uint64_t{word(bank, base + 0x3c)} << 32)) | 0xffff;
            if (start > end || start < context.first || end > context.last || (start & 0xffff))
                return false;
        }
        if (mode) {
            if (context.slot >= 16 || key_receipts_[bank][context.slot] != complete_key)
                return false;
            context.generation = key_generations_[bank][context.slot];
        }
        if (mode >= 2) {
            if (!context.aux_size || (context.aux & 0xffff) ||
                context.aux > UINT64_MAX - (context.aux_size - 1)) return false;
            const auto last = context.aux + context.aux_size - 1;
            if (!mapped(context.aux, last) ||
                overlaps(context.first, context.last, context.aux, last)) return false;
            for (unsigned other_row = 0; other_row < 32; ++other_row) {
                if (other_row == row || !(word(bank, 0x103350) & (1U << other_row))) continue;
                const auto other = configured(bank, other_row);
                if ((other.policy & 0x10000) != (context.policy & 0x10000)) continue;
                if (overlaps(context.aux, last, other.first, other.last)) return false;
                if (other.aux_size && other.aux <= UINT64_MAX - (other.aux_size - 1) &&
                    overlaps(context.aux, last, other.aux, other.aux + other.aux_size - 1))
                    return false;
            }
        }
        if (submitted) {
            context.first = std::uint64_t{word(bank, base + 0x30)} |
                (std::uint64_t{word(bank, base + 0x34)} << 32);
            context.last = (std::uint64_t{word(bank, base + 0x38)} |
                (std::uint64_t{word(bank, base + 0x3c)} << 32)) | 0xffff;
        }
        return true;
    }
    void initialize(unsigned first, unsigned end, unsigned row, bool array,
                    const std::array<bool, 8>& starts) {
        std::array<Context, 8> staged{};
        std::array<bool, 8> success{};
        std::array<std::pair<std::uint64_t, std::uint64_t>, 8> cleared{};
        unsigned cleared_count = 0;
        const auto generation = configuration_generation_;
        for (unsigned bank = first; bank < end; ++bank) {
            if (!starts[bank]) continue;
            auto& context = contexts_[bank][row];
            invalidate(context);
            context.requested = true;
            const auto config = configured(bank, row);
            revoke_range(config.first, config.last);
        }
        for (unsigned bank = first; bank < end; ++bank) {
            if (!starts[bank]) continue;
            auto& context = staged[bank];
            success[bank] = prepare(bank, row, array, context);
            if (!success[bank] || !context.aux_size) continue;
            const auto interval = std::make_pair(context.aux, context.aux_size);
            if (std::find(cleared.begin(), cleared.begin() + cleared_count, interval) == cleared.begin() + cleared_count) {
                success[bank] = zero_auxiliary(context);
                if (success[bank]) cleared[cleared_count++] = interval;
            }
        }
        unsigned completed = 0, failed = 0;
        for (unsigned bank = first; bank < end; ++bank) {
            if (!starts[bank]) continue;
            success[bank] = success[bank] && generation == configuration_generation_;
            const auto base = 0x120000 + row * 0x1000;
            if (success[bank]) {
                staged[bank].valid = true;
                contexts_[bank][row] = staged[bank];
            }
            value(bank, base + 0x70) = success[bank] ? 1 : 0;
            value(bank, base + 0x74) = success[bank] ? 3 : 0x600;
            value(bank, base + 0x78) = success[bank] ? 0 : 1;
            value(bank, base + 0x98) = success[bank] ? 0 : 1;
            value(bank, 0x103014 + row * 8) = success[bank] ? 0x10 : 0;
            (success[bank] ? completed : failed) |= 1U << bank;
        }
        std::fprintf(stderr,
            "LLCC_DARE_EFFECT model=%s row=%u completed=0x%x failed=0x%x policy=data-preserving-canonical-zero-auxiliary\n",
            name(), row, completed, failed);
    }
    void clear_row_completion(unsigned bank, unsigned row) {
        const auto base = 0x120000 + row * 0x1000;
        value(bank, base + 0x70) = 0;
        value(bank, base + 0x74) = 0;
        value(bank, base + 0x78) = 0;
        value(bank, base + 0x98) = 0;
    }

    std::uint32_t& value(unsigned bank, unsigned offset) {
        ++values_version_;
        return values_[static_cast<std::size_t>(bank) * word_count + offset / 4];
    }
    void end_of_elaboration() override {
        const auto banks = configuration_banks.get_value();
        if (!banks || banks > 8 || retained_extent.get_value() != extent)
            throw std::invalid_argument("LLCC requires one to eight banks and a 2 MiB aperture");
        values_.resize(static_cast<std::size_t>(banks) * word_count);
        declared_.resize(word_count);
        std::istringstream input(configuration_offsets.get_value());
        std::string token;
        while (std::getline(input, token, ',')) {
            std::size_t used = 0;
            const auto offset = std::stoull(token, &used, 0);
            if (used != token.size() || offset >= extent || (offset & 3) || declared_[offset / 4])
                throw std::invalid_argument("Invalid LLCC configuration offset");
            declared_[offset / 4] = true;
        }
        cold_reset();
    }
    static bool security_word(unsigned offset) {
        return (offset >= 0x60000 && offset < 0x64000) ||
               (offset >= 0xb0000 && offset < 0xc0000);
    }
    static constexpr unsigned translator_lock = 0x640f8;
    static bool translator_word(unsigned offset) {
        // Six original regions, then four additional regions. Region zero
        // has no base register. Retain complete words, including high fields.
        return (offset >= 0x65000 && offset <= 0x65058) ||
               (offset >= 0x65100 && offset <= 0x6513c);
    }
    bool translator_write_permitted(unsigned bank, unsigned offset, std::uint32_t data) const {
        if (offset == translator_lock)
            return data <= 1 && (!(word(bank, translator_lock) & 1) || data == 1);
        return !translator_word(offset) || !(word(bank, translator_lock) & 1);
    }
    static bool slice_word(unsigned offset, unsigned field) {
        return offset < slice_count * 0x1000 && (offset & 0xfff) == field;
    }
    static bool row_word(unsigned offset, unsigned field) {
        return offset >= 0x120000 && offset < 0x140000 && (offset & 0xfff) == field;
    }
    static bool array_word(unsigned offset, unsigned field) {
        return offset >= 0x103010 && offset < 0x103110 && (offset & 7) == field;
    }
    static bool key_slot(unsigned offset) {
        return offset >= 0x110000 && offset < 0x114000 && (offset & 0xfff) < 0x400;
    }
    static unsigned key_word_mask(unsigned offset) {
        if (!key_slot(offset)) return 0;
        switch (offset & 0xff) {
        case 0x00: return 1;
        case 0x04: return 2;
        case 0x20: return 4;
        case 0x24: return 8;
        case 0x28: return 16;
        case 0x2c: return 32;
        default: return 0;
        }
    }
    static bool key_marker(unsigned offset) {
        return key_slot(offset) && (offset & 0xff) == 0x80;
    }
    static bool observation(unsigned offset) {
        if (slice_word(offset, 4) || row_word(offset, 0x70) || row_word(offset, 0x74) ||
            row_word(offset, 0x78) || row_word(offset, 0x98) || array_word(offset, 4)) return true;
        if (key_marker(offset) || (offset >= 0x103500 && offset < 0x103580)) return true;
        switch (offset) {
        case 0x64000: case 0x6400c:
        case 0x7c038: case 0x7c03c: case 0x7c114:
        case 0xa6024: case 0xa6058: case 0xa606c:
        case 0x45240: case 0x452a0: case 0x43408:
            return true;
        default: return false;
        }
    }
    static bool request_word(unsigned offset) {
        return slice_word(offset, 0) || slice_word(offset, 8) ||
            row_word(offset, 0x30) || row_word(offset, 0x34) ||
            row_word(offset, 0x38) || row_word(offset, 0x3c) ||
            row_word(offset, 0x64) || array_word(offset, 0) ||
            offset == 0x7c028 || offset == 0xa6018 || offset == 0x45214 || offset == 0x43908;
    }
    void publish_mask(unsigned bank, std::uint32_t mask, unsigned low, unsigned high) {
        std::uint64_t states = 0;
        for (unsigned bit = 0; bit < 32; ++bit)
            states |= std::uint64_t{(mask >> bit) & 1U} << (2 * bit + 1);
        value(bank, low) = static_cast<std::uint32_t>(states);
        value(bank, high) = static_cast<std::uint32_t>(states >> 32);
    }
    void write_word(unsigned bank, unsigned offset, std::uint32_t data) {
        const auto previous = value(bank, offset);
        value(bank, offset) = data;
        if (const auto mask = key_word_mask(offset)) {
            const auto slot = ((offset - 0x110000) / 0x1000) * keys_per_group +
                ((offset & 0xfff) / 0x100);
            auto& receipt = key_receipts_[bank][slot];
            // A write to a published slot begins replacement. Every word must
            // arrive again before the replacement becomes usable.
            if (receipt == complete_key) receipt = 0;
            key_written_[bank][slot] = true;
            ++key_generations_[bank][slot];
            receipt |= mask;
            value(bank, (offset & ~0xffU) + 0x80) = receipt == complete_key ? 0xf03 : 0;
        }
        if (slice_word(offset, 0)) {
            if ((data & 1) && !(previous & 1)) {
                const unsigned state = (data & 6) == 2 ? 1 : 2;
                value(bank, offset + 4) = state | 0x10;
            }
        } else if (slice_word(offset, 8)) {
            if (data & 1) value(bank, offset - 4) &= ~0x10U;
            value(bank, offset) = 0;
        } else if (offset == 0x7c028 && (data & 2)) {
            const auto mask = value(bank, 0x7c020) & ~value(bank, 0x7c024);
            publish_mask(bank, mask, 0x7c038, 0x7c03c);
            value(bank, 0x7c114) = 0;
        } else if (offset == 0xa6018 && (data & 2)) {
            const auto mask = value(bank, 0xa6050) & ~value(bank, 0xa6010);
            publish_mask(bank, mask, 0xa6024, 0xa6058);
            value(bank, 0xa606c) = 0;
        } else if (offset == 0x45214 && (data & 0x01000000) && !(previous & 0x01000000)) {
            // All memory writes already reach the common backing. There are no
            // private dirty lines to drain under the coherent virtual policy.
            value(bank, offset + 0x2c) = 1;
        } else if (offset == 0x43908 && (data & 1)) {
            value(bank, 0x43408) |= 0x10000000;
        }
    }
    void transport(bool and_read, tlm::tlm_generic_payload& tx) {
        std::lock_guard<std::recursive_mutex> lock(dare_mutex_);
        const bool low_byte_write = tx.is_write() && tx.get_address() % extent == 0x100088 &&
            tx.get_data_ptr() && tx.get_data_length() == 1 && tx.get_streaming_width() >= 1 &&
            !tx.get_byte_enable_ptr();
        tx.set_dmi_allowed(false);
        if (!low_byte_write && !clock_detail::valid(tx)) return;
        const auto address = tx.get_address();
        const auto selector = address / extent;
        const auto offset = static_cast<unsigned>(address % extent);
        const auto banks = configuration_banks.get_value();
        if (selector > banks) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (security_word(offset)) {
            std::fprintf(stderr, "UNMODELED_LLCC_SECURITY_ACCESS model=%s offset=0x%06x operation=%s\n",
                name(), offset, tx.is_write() ? "write" : "read");
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const unsigned first = selector ? selector - 1 : 0;
        const unsigned end = selector ? first + 1 : banks;
        std::uint32_t data = tx.is_write() ?
            (low_byte_write ? tx.get_data_ptr()[0] : clock_detail::load(tx)) : (and_read ? ~0U : 0U);
        if (!tx.is_write()) {
            for (unsigned bank = first; bank < end; ++bank)
                data = and_read ? data & word(bank, offset) : data | word(bank, offset);
        }
        if (!declared_[offset / 4] && !observation(offset) && !request_word(offset) &&
            !key_word_mask(offset) && !translator_word(offset) && offset != translator_lock)
            std::fprintf(stderr,
                "UNMODELED_LLCC_CONFIGURATION_SUBSTITUTE model=%s selector=%llu offset=0x%06x operation=%s value=0x%08x policy=retained-cold-zero\n",
                name(), static_cast<unsigned long long>(selector), offset,
                tx.is_write() ? "write" : "read", data);
        if (tx.is_write()) {
            if (observation(offset) || (slice_word(offset, 0) && (data & 1) &&
                (data & 6) != 2 && (data & 6) != 4)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // Preflight the complete selection before changing any bank.
            // This functional lock policy grants no requester authorization.
            if (offset == translator_lock || translator_word(offset)) {
                for (unsigned bank = first; bank < end; ++bank)
                    if (!translator_write_permitted(bank, offset, data)) {
                        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                        return;
                    }
            }
            ++configuration_generation_;
            std::array<std::uint32_t, 8> previous{};
            std::array<bool, 8> starts{};
            for (unsigned bank = first; bank < end; ++bank) {
                previous[bank] = value(bank, offset);
                starts[bank] = (data & 1) && !(previous[bank] & 1);
            }
            for (unsigned bank = first; bank < end; ++bank)
                write_word(bank, offset, low_byte_write ? (previous[bank] & ~0xffU) | data : data);
            bool revoke_policy = false;
            for (unsigned bank = first; bank < end; ++bank) {
                if (key_word_mask(offset)) {
                    const auto slot = ((offset - 0x110000) / 0x1000) * keys_per_group +
                        ((offset & 0xfff) / 0x100);
                    for (auto& context : contexts_[bank])
                        if (context.valid && context.slot == slot && (context.policy & 3))
                            invalidate(context);
                    revoke_policy = true;
                }
                if (previous[bank] != data && offset >= 0x120000 && offset < 0x140000) {
                    const auto field = offset & 0xfff;
                    if (field <= 0x1c || field == 0x40 || field == 0x44 ||
                        field == 0x4c || field == 0x90 || field == 0x9c || field == 0xa0) {
                        invalidate(contexts_[bank][(offset - 0x120000) / 0x1000]);
                        revoke_policy = true;
                    }
                }
                if (offset == 0x103350 && previous[bank] != data) {
                    for (unsigned row = 0; row < 32; ++row)
                        if ((previous[bank] ^ data) & (1U << row))
                            invalidate(contexts_[bank][row]);
                    revoke_policy = true;
                }
                if (offset == 0x100088 && ((previous[bank] ^ data) & 1))
                    revoke_policy = true;
                if (row_word(offset, 0x64) && (data & 2) && !(previous[bank] & 2) &&
                    value(bank, offset + 8) == 1) {
                    clear_row_completion(bank, (offset - 0x120000) / 0x1000);
                    value(bank, offset + 8) = 0;
                }
                if (offset == 0x103338) {
                    const auto rising = data & ~previous[bank];
                    for (unsigned row = 0; row < 32; ++row)
                        if (rising & (1U << row)) {
                            value(bank, 0x103014 + row * 8) = 0;
                            clear_row_completion(bank, row);
                        }
                }
            }
            if (revoke_policy) revoke_all();
            if (row_word(offset, 0x64) || array_word(offset, 0)) {
                const bool array = array_word(offset, 0);
                const unsigned row = array ? (offset - 0x103010) / 8 :
                    (offset - 0x120000) / 0x1000;
                bool any_start = false;
                for (unsigned bank = first; bank < end; ++bank) any_start |= starts[bank];
                if (any_start) initialize(first, end, row, array, starts);
            }
        } else clock_detail::store(tx, data);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void or_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { transport(false, tx); }
    void and_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { transport(true, tx); }
};

} // namespace qcom_vp

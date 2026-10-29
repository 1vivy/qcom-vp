// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_source.h"
#include <systemc>
#include <sysc/kernel/sc_spawn.h>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// ROM-visible Taycan EKO T PLLs. These are clock sources, not a message transport.
class QcomGccPll : public sc_core::sc_module, public PllSource {
public:
    tlm_utils::multi_passthrough_target_socket<QcomGccPll> target_socket{"target_socket"};
    tlm_utils::simple_target_socket_tagged_optional<QcomGccPll> vote_socket{"vote_socket"};
    cci::cci_param<std::uint64_t> reference_hz{
        "reference_hz", 19200000, "Virtual always-on PLL reference frequency"};
    cci::cci_param<unsigned> pll_count{"pll_count", 1, "Number of PLL banks at a 4096-byte stride"};
    cci::cci_param<unsigned> vote_count{"vote_count", 1, "Number of master vote registers at a 20480-byte stride"};
    cci::cci_param<std::uint32_t> coarse_calibration_mask{
        "coarse_calibration_mask", 0, "PLL banks with virtual coarse calibration"};
    cci::cci_param<unsigned> virtual_coarse_calibration_code{
        "virtual_coarse_calibration_code", 0, "Declared five-bit virtual analog calibration code"};
    cci::cci_param<std::string> external_vote_path{
        "external_vote_path", "", "Optional shared GCC vote owner for a single PLL"};
    cci::cci_param<std::uint32_t> external_vote_mask{
        "external_vote_mask", 0, "Vote bit consumed from the shared owner"};

    sc_core::sc_event state_changed{"state_changed"};
    const sc_core::sc_event& lock_changed(unsigned index) const override { return lock_events_.at(index); }
    const sc_core::sc_event& clock_changed() const override { return state_changed; }
    unsigned bank_count() const override { return pll_count.get_value(); }
    std::uint64_t reference_rate_hz() const override { return reference_hz.get_value(); }
    std::uint64_t calibration_output_hz(unsigned) const override { return 0; }
    explicit QcomGccPll(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomGccPll::pll_transport);
        vote_socket.register_b_transport(this, &QcomGccPll::vote_transport, 0);
    }

    // Cold reset belongs to this clock block. A CPU-only reset must not call it.
    void cold_reset() {
        plls_ = {};
        const auto old_votes = votes_;
        votes_ = 0;
        vote_words_ = {};
        for (auto& event : lock_events_) event.cancel();
        state_changed.notify(sc_core::SC_ZERO_TIME);
        if (old_votes) votes_changed_.notify(sc_core::SC_ZERO_TIME);
    }

    // Main output frequency, including the programmed fractional multiplier.
    // Consumers must not use an unlocked or disabled output as a clock source.
    std::uint64_t main_output_hz(unsigned index) const override {
        return output_hz(index, 1);
    }

    // The even output has its own gate. It does not depend on the main gate.
    std::uint64_t even_output_hz(unsigned index) const override {
        if (index >= pll_count.get_value()) return 0;
        const auto divisor = ((plls_.at(index).words[6] >> 10) & 15) + 1;
        return output_hz(index, 2) / divisor;
    }

private:
    std::uint64_t output_hz(unsigned index, std::uint32_t gate) const {
        if (index >= pll_count.get_value()) return 0;
        const auto& pll = plls_.at(index);
        const auto mode = pll.words[0];
        const auto user = pll.words[6];
        const bool voted = (user & 0x02000000) && vote_asserted(index);
        if (!locked(pll) || !(user & gate) || (!(mode & 1) && !(mode & 8) && !voted)) return 0;
        const auto l = pll.words[4] & 0xffff;
        const auto alpha = pll.words[5] & 0xffff;
        const auto pre = ((user >> 22) & 7) + 1;
        return reference_hz.get_value() * ((std::uint64_t{l} << 16) + alpha) / (65536 * pre);
    }

    struct Pll {
        std::array<std::uint32_t, 14> words{};
        bool running = false;
        sc_core::sc_time lock_at = sc_core::SC_ZERO_TIME;
    };
    std::array<Pll, 12> plls_{};
    std::uint32_t votes_ = 0;
    std::array<std::uint32_t, 4> vote_words_{};
    std::array<sc_core::sc_event, 12> lock_events_;
    sc_core::sc_event votes_changed_;
    const QcomGccPll* external_vote_source_ = nullptr;
    bool external_vote_level_ = false;

    bool vote_asserted(unsigned index) const {
        return external_vote_source_
            ? (external_vote_source_->votes_ & external_vote_mask.get_value()) != 0
            : (votes_ & (1u << index)) != 0;
    }

    void refresh_external_vote() {
        const bool level = vote_asserted(0);
        if (external_vote_level_ == level) return;
        external_vote_level_ = level;
        update(0);
    }

    void end_of_elaboration() override {
        if (!pll_count.get_value() || pll_count.get_value() > plls_.size())
            throw std::invalid_argument("PLL bank count must be in 1..12");
        if (!vote_count.get_value() || vote_count.get_value() > vote_words_.size())
            throw std::invalid_argument("PLL vote register count must be in 1..4");
        if ((coarse_calibration_mask.get_value() & ~((1u << pll_count.get_value()) - 1)) ||
            virtual_coarse_calibration_code.get_value() > 31)
            throw std::invalid_argument("PLL coarse calibration requires existing banks and a five-bit code");
        // Keep the integer frequency calculation bounded.
        if (!reference_hz.get_value() || reference_hz.get_value() > 1000000000) {
            throw std::invalid_argument("GCC PLL reference must be in 1..1000000000 Hz");
        }
        const auto& path = external_vote_path.get_value();
        const auto mask = external_vote_mask.get_value();
        if (path.empty()) {
            if (mask) throw std::invalid_argument("An external PLL vote mask requires a shared owner");
            return;
        }
        external_vote_source_ = dynamic_cast<QcomGccPll*>(sc_core::sc_find_object(path.c_str()));
        if (!external_vote_source_ && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            external_vote_source_ = dynamic_cast<QcomGccPll*>(sc_core::sc_find_object(relative.c_str()));
        }
        if (!external_vote_source_ || external_vote_source_ == this || pll_count.get_value() != 1 ||
            !mask || (mask & (mask - 1)) || vote_socket.get_base_port().size() ||
            !external_vote_source_->external_vote_path.get_value().empty() ||
            !external_vote_source_->vote_socket.get_base_port().size())
            throw std::invalid_argument("An external PLL requires one vote bit from a separate register owner");
        sc_core::sc_spawn_options options;
        options.spawn_method();
        options.set_sensitivity(&external_vote_source_->votes_changed_);
        sc_core::sc_spawn([this] { refresh_external_vote(); },
            sc_core::sc_gen_unique_name("external_vote"), &options);
    }

    static bool locked(const Pll& pll) {
        return pll.running && sc_core::sc_time_stamp() >= pll.lock_at;
    }

    void update(unsigned index, bool reprogrammed = false) {
        auto& pll = plls_[index];
        const auto user = pll.words[6];
        const bool vote = vote_asserted(index);
        const bool run = (pll.words[0] & 4) && (pll.words[4] & 0xffff) &&
            (pll.words[1] == 1 || ((user & 0x02000000) && vote));
        if (run && (!pll.running || reprogrammed)) {
            // Functional acquisition delay, not a claim about silicon timing.
            pll.lock_at = sc_core::sc_time_stamp() + sc_core::sc_time(5, sc_core::SC_US);
            lock_events_[index].cancel();
            lock_events_[index].notify(pll.lock_at - sc_core::sc_time_stamp());
        }
        pll.running = run;
        if (!run) lock_events_[index].cancel();
        state_changed.notify(sc_core::SC_ZERO_TIME);
    }

    static bool valid(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        } else if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
                   tx.get_streaming_width() < 4 || (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        } else if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        } else {
            return true;
        }
        return false;
    }

    static std::uint32_t load(const tlm::tlm_generic_payload& tx) {
        const auto* p = tx.get_data_ptr();
        return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
            (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
    }

    static void store(tlm::tlm_generic_payload& tx, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        }
    }

    void pll_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (external_vote_source_) refresh_external_vote();
        const auto index = tx.get_address() >> 12;
        const auto offset = tx.get_address() & 0xfff;
        if (index >= pll_count.get_value() ||
            (offset != 0 && offset != 4 && offset != 0xc && (offset < 0x10 || offset > 0x34))) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& pll = plls_[index];
        if (offset == 0xc) {
            if (!(coarse_calibration_mask.get_value() & (1u << index))) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            } else if (!tx.is_read() || !pll.running || !(pll.words[11] & 0x100)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            } else {
                // Virtual coarse calibration completes before fine lock. Clock outputs still require lock.
                store(tx, virtual_coarse_calibration_code.get_value() << 13);
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
            }
            return;
        }
        auto& word = pll.words[offset / 4];
        if (tx.is_read()) {
            auto value = word;
            if (offset == 0) {
                // Accepted programming is synchronous. Latch ACK does not require oscillator lock.
                // Linux Lucid5LPE uses the same bit-14 input and bit-13 acknowledgment.
                if (word & 0x4000) value |= 0x2000;
                if (pll.running) value |= 0x20000000;
                if (locked(pll)) value |= 0xc0000000;
            }
            store(tx, value);
        } else {
            auto value = load(tx);
            if (offset == 0) {
                // Lock status and latch ACK are hardware-owned.
                value &= ~std::uint32_t{0xe0002000};
                if (value & ~std::uint32_t{0x407f}) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
            } else if (offset == 4 && value != 0 && value != 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // ROM selects alpha format. Do not silently misinterpret M/N format.
            if (offset == 0x18 && (value & 0x10000000)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            const auto changed_bits = word ^ value;
            word = value;
            // Output gates and postdividers do not restart oscillator acquisition.
            const bool reprogrammed = (offset == 0x10 || offset == 0x14) ? changed_bits != 0 :
                offset == 0x18 && (changed_bits & 0x11c00000);
            update(index, reprogrammed);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void vote_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto master = tx.get_address() / 0x5000;
        if (tx.get_address() % 0x5000 || master >= vote_count.get_value()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            store(tx, vote_words_[master]);
        } else {
            vote_words_[master] = load(tx);
            std::uint32_t value = 0;
            for (const auto word : vote_words_) value |= word;
            const auto changed = votes_ ^ value;
            votes_ = value;
            for (unsigned index = 0; index < pll_count.get_value(); ++index)
                if (changed & (1u << index)) update(index);
            if (changed) votes_changed_.notify(sc_core::SC_ZERO_TIME);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

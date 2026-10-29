// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "clock_source.h"
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <array>
#include <stdexcept>
#include <string>
#include <sstream>

namespace qcom_vp {
namespace clock_detail {
inline bool valid(tlm::tlm_generic_payload& tx) {
    tx.set_dmi_allowed(false);
    if (tx.get_byte_enable_ptr()) tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
    else if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
             tx.get_streaming_width() < 4 || (tx.get_address() & 3))
        tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
    else if (!tx.is_read() && !tx.is_write()) tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    else return true;
    return false;
}
inline std::uint32_t load(const tlm::tlm_generic_payload& tx) {
    const auto* p = tx.get_data_ptr();
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
        (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}
inline void store(tlm::tlm_generic_payload& tx, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
}
inline PllSource* resolve_plls(const sc_core::sc_module& consumer, const std::string& path) {
    auto* plls = dynamic_cast<PllSource*>(sc_core::sc_find_object(path.c_str()));
    if (!plls && consumer.get_parent_object()) {
        const auto relative = std::string(consumer.get_parent_object()->name()) + "." + path;
        plls = dynamic_cast<PllSource*>(sc_core::sc_find_object(relative.c_str()));
    }
    if (!plls) throw std::invalid_argument("Clock consumer requires a PLL source at pll_path");
    return plls;
}
} // namespace clock_detail

// One relative-address RCG. Platform data supplies source wiring and enable policy.
class QcomClockRoot : public sc_core::sc_module, public ClockSource {
public:
    tlm_utils::multi_passthrough_target_socket<QcomClockRoot> target_socket{"target_socket"};
    cci::cci_param<std::string> pll_path{"pll_path", "gcc_plls", "Shared PLL path, absolute or relative to parent"};
    cci::cci_param<unsigned> crystal_selector{"crystal_selector", 0, "Crystal mux selector, 8 disables"};
    cci::cci_param<unsigned> primary_selector{"primary_selector", 8, "Primary main output selector, 8 disables"};
    cci::cci_param<unsigned> primary_even_selector{"primary_even_selector", 8, "Primary even output selector, 8 disables"};
    cci::cci_param<unsigned> secondary_selector{"secondary_selector", 8, "Secondary main output selector, 8 disables"};
    cci::cci_param<unsigned> calibration_selector{"calibration_selector", 8, "Primary calibration output selector, 8 disables"};
    cci::cci_param<unsigned> primary_index{"primary_index", 0, "Primary PLL bank index"};
    cci::cci_param<unsigned> secondary_index{"secondary_index", 4, "Secondary PLL bank index"};
    cci::cci_param<std::string> main_sources{"main_sources", "", "Additional main sources: selector index records separated by semicolons"};
    cci::cci_param<bool> require_root_enable{"require_root_enable", true, "Require ROOT_EN instead of continuous downstream demand"};
    cci::cci_param<unsigned> mnd_width{"mnd_width", 8, "M/N/D field width, 8 or 16 bits"};
    sc_core::sc_event state_changed{"state_changed"};

    SC_HAS_PROCESS(QcomClockRoot);
    explicit QcomClockRoot(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomClockRoot::transport);
        SC_THREAD(change_thread);
    }
    virtual void cold_reset() {
        root_ = {};
        acquisition_event_.cancel();
        update_event_.notify(sc_core::SC_ZERO_TIME);
    }
    // Zero means disabled or source unavailable. An RCG keeps its active
    // configuration until a pending update acquires the new source.
    std::uint64_t output_hz() {
        refresh();
        if (require_root_enable.get_value() && !(root_.command & 2)) return 0;
        return configuration_available() ? frequency(root_.active) : 0;
    }
    bool clock_running() override { return output_hz() != 0; }
    const sc_core::sc_event& clock_changed() const override { return state_changed; }

protected:
    struct Root {
        std::uint32_t command = 0;
        std::array<std::uint32_t, 4> shadow{};
        std::array<std::uint32_t, 4> requested{};
        std::array<std::uint32_t, 4> active{};
        bool pending = false;
        std::uint64_t acquiring_hz = 0;
        sc_core::sc_time ready_at = sc_core::SC_ZERO_TIME;
    } root_;
    PllSource* plls_ = nullptr;
    std::array<unsigned, 8> main_source_indices_{};
    sc_core::sc_event update_event_;
    sc_core::sc_event acquisition_event_;

    virtual bool configuration_available() const { return true; }
    virtual std::array<std::uint32_t, 4> command_configuration() const { return root_.shadow; }
    virtual void configuration_completed() {}
    virtual void extended_transport(tlm::tlm_generic_payload& tx) {
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    void request_configuration(const std::array<std::uint32_t, 4>& cfg) {
        root_.requested = cfg;
        root_.pending = true;
        root_.acquiring_hz = 0;
        refresh();
        update_event_.notify(sc_core::SC_ZERO_TIME);
    }
    void change_thread() {
        sc_core::sc_event_or_list wake;
        wake |= update_event_;
        wake |= acquisition_event_;
        wake |= plls_->clock_changed();
        if (primary_index.get_value() < plls_->bank_count())
            wake |= plls_->lock_changed(primary_index.get_value());
        if (secondary_index.get_value() < plls_->bank_count() &&
            secondary_index.get_value() != primary_index.get_value())
            wake |= plls_->lock_changed(secondary_index.get_value());
        for (const auto index : main_source_indices_)
            if (index) wake |= plls_->lock_changed(index - 1);
        for (;;) {
            refresh();
            state_changed.notify(sc_core::SC_ZERO_TIME);
            sc_core::wait(wake);
        }
    }

    void end_of_elaboration() override {
        if (mnd_width.get_value() != 8 && mnd_width.get_value() != 16)
            throw std::invalid_argument("RCG M/N/D width must be 8 or 16 bits");
        const std::array<unsigned, 5> selectors{crystal_selector.get_value(),
            primary_selector.get_value(), primary_even_selector.get_value(),
            secondary_selector.get_value(), calibration_selector.get_value()};
        unsigned used = 0;
        for (const auto selector : selectors) {
            if (selector > 8 || (selector < 8 && (used & (1u << selector))))
                throw std::invalid_argument("RCG selectors must be unique in 0..7, or disabled with 8");
            if (selector < 8) used |= 1u << selector;
        }
        plls_ = clock_detail::resolve_plls(*this, pll_path.get_value());
        if (((primary_selector.get_value() < 8 || primary_even_selector.get_value() < 8 ||
              calibration_selector.get_value() < 8) && primary_index.get_value() >= plls_->bank_count()) ||
            (secondary_selector.get_value() < 8 && secondary_index.get_value() >= plls_->bank_count()))
            throw std::invalid_argument("Clock root source index exceeds the configured PLL bank");
        std::istringstream records(main_sources.get_value());
        std::string record;
        while (std::getline(records, record, ';')) {
            std::istringstream fields(record);
            unsigned selector, index;
            if (!(fields >> selector >> index) || !(fields >> std::ws).eof() ||
                selector >= 8 || index >= plls_->bank_count() || (used & (1u << selector)))
                throw std::invalid_argument("Additional RCG sources require unique selectors and valid PLL indices");
            used |= 1u << selector;
            main_source_indices_[selector] = index + 1;
        }
    }
    std::uint64_t frequency(const std::array<std::uint32_t, 4>& cfg) const {
        if (!plls_) return 0;
        const unsigned selector = (cfg[0] >> 8) & 7;
        std::uint64_t hz = 0;
        if (selector == crystal_selector.get_value()) hz = plls_->reference_rate_hz();
        else if (selector == primary_selector.get_value()) hz = plls_->main_output_hz(primary_index.get_value());
        else if (selector == primary_even_selector.get_value()) hz = plls_->even_output_hz(primary_index.get_value());
        else if (selector == secondary_selector.get_value()) hz = plls_->main_output_hz(secondary_index.get_value());
        else if (selector == calibration_selector.get_value()) hz = plls_->calibration_output_hz(primary_index.get_value());
        else if (main_source_indices_[selector]) hz = plls_->main_output_hz(main_source_indices_[selector] - 1);
        if (!hz) return 0;
        const auto hid = cfg[0] & 31;
        if (hid) hz = hz * 2 / (hid + 1);
        const auto mode = (cfg[0] >> 12) & 3;
        if (mode == 2) {
            // XBL stores full-word complements. Only the implemented field bits apply.
            const std::uint32_t mask = (1u << mnd_width.get_value()) - 1;
            const std::uint64_t m = cfg[1] & mask;
            const std::uint64_t n = (~cfg[2] & mask) + m;
            if (!m || n <= m || n > mask || (~cfg[3] & mask) != n) return 0;
            hz = hz * m / n;
        } else if (mode != 0) return 0;
        return hz;
    }
    void refresh() {
        if (!root_.pending) return;
        const auto hz = configuration_available() ? frequency(root_.requested) : 0;
        if (!hz) {
            root_.acquiring_hz = 0;
            acquisition_event_.cancel();
            return;
        }
        if (root_.acquiring_hz != hz) {
            root_.acquiring_hz = hz;
            // Functional acquisition policy, not measured hardware latency.
            root_.ready_at = sc_core::sc_time_stamp() + sc_core::sc_time(1, sc_core::SC_US);
            acquisition_event_.cancel();
            acquisition_event_.notify(root_.ready_at - sc_core::sc_time_stamp());
        }
        if (sc_core::sc_time_stamp() >= root_.ready_at) {
            root_.active = root_.requested;
            root_.pending = false;
            root_.command &= ~std::uint32_t{1};
            configuration_completed();
            acquisition_event_.cancel();
        }
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset >= 0x14) {
            extended_transport(tx);
            return;
        }
        refresh();
        if (offset == 0) {
            if (tx.is_read()) {
                auto value = root_.command;
                if (!output_hz()) value |= 0x80000000;
                clock_detail::store(tx, value);
            } else {
                const auto value = clock_detail::load(tx) & ~std::uint32_t{0x80000000};
                if ((value & ~std::uint32_t{3}) || (root_.pending && !(value & 1))) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                root_.command = value;
                if (value & 1) {
                    request_configuration(command_configuration());
                }
            }
        } else {
            auto& word = root_.shadow[offset / 4 - 1];
            if (tx.is_read()) clock_detail::store(tx, word);
            else word = clock_detail::load(tx);
        }
        if (tx.is_write()) update_event_.notify(sc_core::SC_ZERO_TIME);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// Serial-engine DFS revision. The conventional RCG interface remains unchanged.
class QcomDfsClockRoot : public QcomClockRoot {
public:
    explicit QcomDfsClockRoot(sc_core::sc_module_name name) : QcomClockRoot(name) {}
    void cold_reset() override {
        control_ = 0;
        current_level_ = 0;
        requested_level_ = 0;
        hardware_level_ = 0;
        levels_ = {};
        programmed_ = {};
        QcomClockRoot::cold_reset();
    }
    // Hardware consumers can request a level without writing read-only status bits.
    void request_level(unsigned level) {
        hardware_level_ = level;
        if ((control_ & 1) && !(control_ & 0x400)) select_level();
    }
private:
    std::array<std::array<std::uint32_t, 4>, 8> levels_{};
    std::array<bool, 8> programmed_{};
    std::uint32_t control_ = 0;
    unsigned current_level_ = 0;
    unsigned requested_level_ = 0;
    unsigned hardware_level_ = 0;

    std::array<std::uint32_t, 4> command_configuration() const override {
        if (!(control_ & 1)) return root_.shadow;
        return requested_level_ < levels_.size() ?
            levels_[requested_level_] : std::array<std::uint32_t, 4>{};
    }
    bool configuration_available() const override {
        return !(control_ & 1) ||
            (requested_level_ < levels_.size() && programmed_[requested_level_]);
    }
    void configuration_completed() override {
        if (control_ & 1) current_level_ = requested_level_;
    }
    void select_level() {
        requested_level_ = (control_ & 0x400) ? (control_ >> 11) & 15 : hardware_level_;
        request_configuration(requested_level_ < levels_.size() ?
            levels_[requested_level_] : std::array<std::uint32_t, 4>{});
    }
    void extended_transport(tlm::tlm_generic_payload& tx) override {
        refresh();
        const auto offset = tx.get_address();
        if (offset == 0x14) {
            if (tx.is_read()) {
                clock_detail::store(tx, control_ | (current_level_ << 1) |
                    ((control_ & 1) && root_.pending ? 0x200 : 0));
            } else {
                // Current level, FSM state, and update status are hardware-owned.
                const auto value = clock_detail::load(tx);
                if (value & ~std::uint32_t{0x7fff}) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                const bool was_enabled = control_ & 1;
                control_ = value & 0x7c21;
                if (control_ & 1) select_level();
                else if (was_enabled) request_configuration(root_.shadow);
            }
        } else {
            unsigned field;
            unsigned level;
            if (offset >= 0x1c && offset < 0x3c) field = 0, level = (offset - 0x1c) / 4;
            else if (offset >= 0x5c && offset < 0x7c) field = 1, level = (offset - 0x5c) / 4;
            else if (offset >= 0x9c && offset < 0xbc) field = 2, level = (offset - 0x9c) / 4;
            else if (offset >= 0xdc && offset < 0xfc) field = 3, level = (offset - 0xdc) / 4;
            else {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            auto& word = levels_[level][field];
            if (tx.is_read()) clock_detail::store(tx, word);
            else {
                word = clock_detail::load(tx);
                if (!field) programmed_[level] = true;
                if ((control_ & 1) && requested_level_ == level) select_level();
            }
        }
        if (tx.is_write()) update_event_.notify(sc_core::SC_ZERO_TIME);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// Explicit virtual reference. This is not evidence of a silicon clock parent.
class VirtualAlwaysOnClock : public sc_core::sc_module, public ClockSource {
public:
    explicit VirtualAlwaysOnClock(sc_core::sc_module_name name) : sc_module(name) {}
    bool clock_running() override { return true; }
    const sc_core::sc_event& clock_changed() const override { return unchanged_; }
private:
    sc_core::sc_event unchanged_;
};

// Two-bit symbol mux: PHY selector 0, crystal selector 2 (gcc_parent_map_9..11).
class QcomSymbolClockMux : public sc_core::sc_module, public ClockSource {
public:
    tlm_utils::multi_passthrough_target_socket<QcomSymbolClockMux> target_socket{"target_socket"};
    cci::cci_param<std::string> phy_path{"phy_path", "", "Required PHY ClockSource"};
    cci::cci_param<std::string> crystal_path{"crystal_path", "", "Required crystal ClockSource"};
    cci::cci_param<unsigned> reset_selector{"reset_selector", 0, "Explicit virtual reset selector: PHY 0 or crystal 2"};
    sc_core::sc_event state_changed{"state_changed"};

    SC_HAS_PROCESS(QcomSymbolClockMux);
    explicit QcomSymbolClockMux(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomSymbolClockMux::transport);
        SC_THREAD(change_thread);
    }
    bool clock_running() override { return (selector_ == 0 ? phy_ : crystal_)->clock_running(); }
    const sc_core::sc_event& clock_changed() const override { return state_changed; }
    void cold_reset() {
        selector_ = reset_selector.get_value();
        update_event_.notify(sc_core::SC_ZERO_TIME);
    }
private:
    ClockSource* phy_ = nullptr;
    ClockSource* crystal_ = nullptr;
    unsigned selector_ = 0;
    sc_core::sc_event update_event_;

    ClockSource* resolve(const std::string& path) {
        ClockSource* source = nullptr;
        if (!path.empty()) {
            source = dynamic_cast<ClockSource*>(sc_core::sc_find_object(path.c_str()));
            if (!source && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                source = dynamic_cast<ClockSource*>(sc_core::sc_find_object(relative.c_str()));
            }
        }
        if (!source) throw std::invalid_argument("Symbol mux requires PHY and crystal ClockSource paths");
        return source;
    }
    void end_of_elaboration() override {
        if (reset_selector.get_value() != 0 && reset_selector.get_value() != 2)
            throw std::invalid_argument("Symbol mux reset selector must be PHY 0 or crystal 2");
        phy_ = resolve(phy_path.get_value());
        crystal_ = resolve(crystal_path.get_value());
        selector_ = reset_selector.get_value();
    }
    void change_thread() {
        for (;;) {
            state_changed.notify(sc_core::SC_ZERO_TIME);
            sc_core::wait(update_event_ | phy_->clock_changed() | crystal_->clock_changed());
        }
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        if (tx.get_address()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) clock_detail::store(tx, selector_);
        else {
            const auto value = clock_detail::load(tx);
            if (value != 0 && value != 2) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            selector_ = value;
            update_event_.notify(sc_core::SC_ZERO_TIME);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// Integer divider, encoded as divisor minus one in the platform-selected field.
class QcomClockDivider : public sc_core::sc_module, public ClockSource {
public:
    tlm_utils::multi_passthrough_target_socket<QcomClockDivider> target_socket{"target_socket"};
    cci::cci_param<std::string> pll_path{"pll_path", "gcc_plls", "Shared PLL path, absolute or relative to parent"};
    cci::cci_param<unsigned> source_index{"source_index", 0, "Upstream PLL main output index"};
    cci::cci_param<unsigned> field_shift{"field_shift", 0, "Divider field start bit"};
    cci::cci_param<unsigned> field_width{"field_width", 4, "Divider field width"};
    cci::cci_param<bool> reference_source{"reference_source", false, "Select the declared reference instead of a PLL output"};
    sc_core::sc_event state_changed{"state_changed"};
    SC_HAS_PROCESS(QcomClockDivider);
    explicit QcomClockDivider(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomClockDivider::transport);
        SC_THREAD(change_thread);
    }
    void cold_reset() {
        divider_ = 0;
        update_event_.notify(sc_core::SC_ZERO_TIME);
    }
    std::uint64_t output_hz() const {
        if (!plls_) return 0;
        const auto hz = reference_source.get_value() ? plls_->reference_rate_hz() :
            plls_->main_output_hz(source_index.get_value());
        return hz / (((divider_ >> field_shift.get_value()) & field_mask_) + 1);
    }
    bool clock_running() override { return output_hz() != 0; }
    const sc_core::sc_event& clock_changed() const override { return state_changed; }
private:
    std::uint32_t divider_ = 0;
    PllSource* plls_ = nullptr;
    std::uint32_t field_mask_ = 15;
    sc_core::sc_event update_event_;
    void change_thread() {
        for (;;) {
            state_changed.notify(sc_core::SC_ZERO_TIME);
            sc_core::wait(update_event_ | plls_->clock_changed());
        }
    }
    void end_of_elaboration() override {
        if (!field_width.get_value() || field_width.get_value() > 16 ||
            field_shift.get_value() > 32 - field_width.get_value())
            throw std::invalid_argument("Divider field must fit in one word and contain 1..16 bits");
        field_mask_ = (1u << field_width.get_value()) - 1;
        plls_ = clock_detail::resolve_plls(*this, pll_path.get_value());
        if (source_index.get_value() >= plls_->bank_count())
            throw std::invalid_argument("Divider PLL source index exceeds the configured bank");
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) clock_detail::store(tx, divider_);
        else {
            const auto value = clock_detail::load(tx);
            if (value & ~(field_mask_ << field_shift.get_value())) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            divider_ = value;
            update_event_.notify(sc_core::SC_ZERO_TIME);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// Five independent one-bit branch enables. No downstream status is fabricated.
class QcomClockBranchEnables : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomClockBranchEnables> target_socket{"target_socket"};
    sc_core::sc_event state_changed{"state_changed"};
    explicit QcomClockBranchEnables(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomClockBranchEnables::transport);
    }
    void cold_reset() {
        branches_ = {};
        state_changed.notify(sc_core::SC_ZERO_TIME);
    }
    bool branch_enabled(unsigned index) const { return branches_.at(index) & 1; }
private:
    std::array<std::uint32_t, 5> branches_{};
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset >= 0x14) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& branch = branches_[offset / 4];
        if (tx.is_read()) clock_detail::store(tx, branch);
        else {
            const auto value = clock_detail::load(tx);
            if (value > 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            branch = value;
            state_changed.notify(sc_core::SC_ZERO_TIME);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

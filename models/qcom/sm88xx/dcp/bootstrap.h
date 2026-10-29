// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "cpu.h"
#include "clock_source.h"
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <initializer_list>

namespace qcom_vp {

// DCP configuration drives the native CPU through completed reset transitions.
// Absolute APSS addresses preserve one state across the six narrow bank mappings.
class QcomDcpBootstrap : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<QcomDcpBootstrap> power_socket{"power_socket"};
    tlm_utils::simple_target_socket<QcomDcpBootstrap> branch_socket{"branch_socket"};
    tlm_utils::simple_target_socket<QcomDcpBootstrap> clock_socket{"clock_socket"};
    tlm_utils::simple_target_socket<QcomDcpBootstrap> boot_socket{"boot_socket"};
    tlm_utils::simple_target_socket<QcomDcpBootstrap> dtb_socket{"dtb_socket"};
    tlm_utils::simple_target_socket<QcomDcpBootstrap> config_socket{"config_socket"};
    // Remapper window one translates DCP 0x40000000..0x9fffffff in 128 MiB entries.
    tlm_utils::simple_target_socket_optional<QcomDcpBootstrap> remap_socket{"remap_socket"};
    tlm_utils::simple_initiator_socket_optional<QcomDcpBootstrap> soc_socket{"soc_socket"};
    InitiatorSignalSocket<bool> reset{"reset"};
    // Each pulse reports one executed DCP WFI.
    TargetSignalSocket<bool> wfi{"wfi"};
    // Optional clock parents. Fixtures without them keep the core-root proxy.
    cci::cci_param<std::string> display_pll_path{"display_pll_path", "",
        "Display PLL bank whose output one feeds core-root selector 4"};
    cci::cci_param<std::string> ahb_root_path{"ahb_root_path", "", "Display AHB root at DISP+0x378"};
    cci::cci_param<std::string> vsync_root_path{"vsync_root_path", "", "Display vsync root at DISP+0x17c"};
    cci::cci_param<std::string> timer_root_path{"timer_root_path", "", "Display timer root at DISP+0x6044"};
    SC_HAS_PROCESS(QcomDcpBootstrap);
    QcomDcpBootstrap(sc_core::sc_module_name name, sc_core::sc_object* cpu)
        : sc_module(name), cpu_(dynamic_cast<DcpCpu*>(cpu)) {
        if (!cpu_) throw std::invalid_argument("DCP bootstrap requires a DcpCpu");
        words_[0][0] = 1; // Collapsed power domain.
        words_[2][0x30 / 4] = 0x80000000; // Processor halted until guest release.
        power_socket.register_b_transport(this, &QcomDcpBootstrap::transport);
        branch_socket.register_b_transport(this, &QcomDcpBootstrap::transport);
        clock_socket.register_b_transport(this, &QcomDcpBootstrap::transport);
        boot_socket.register_b_transport(this, &QcomDcpBootstrap::transport);
        dtb_socket.register_b_transport(this, &QcomDcpBootstrap::transport);
        config_socket.register_b_transport(this, &QcomDcpBootstrap::transport);
        remap_socket.register_b_transport(this, &QcomDcpBootstrap::remap);
        wfi.register_value_changed_cb([this](const bool& entered) { if (entered) idle(); });
        SC_THREAD(reset_thread);
    }

private:
    static constexpr std::array<uint64_t, 6> bases{
        0x09bab000, 0x09bae000, 0x09bb4000, 0x09c70000, 0x09ca0000, 0x09ca2000};
    std::array<std::array<uint32_t, 0x1000 / 4>, 6> words_{};
    // Core page roots: DCP 0x10, debug APB 0x44, debug QTS 0x60, and debug ATB 0x7c.
    // Each root has CMD, CFG, M, N, and D words. Entry zero gates DCP release.
    static constexpr std::array<unsigned, 4> roots{0x10, 0x44, 0x60, 0x7c};
    std::array<bool, 4> root_ok_{};
    PllSource* display_plls_ = nullptr;
    ClockSource* ahb_root_ = nullptr;
    ClockSource* vsync_root_ = nullptr;
    ClockSource* timer_root_ = nullptr;
    DcpCpu* cpu_;
    bool requested_release_ = false;
    bool released_ = false;
    std::uint32_t requested_entry_ = 0;
    std::uint64_t control_generation_ = 0;
    // Hardware control removed display-domain power after an armed DCP WFI.
    bool hw_collapsed_ = false;
    sc_core::sc_event reset_event_;
    static constexpr uint64_t remap_base = 0x40000000;
    static constexpr uint64_t remap_size = 0x60000000;
    static constexpr uint64_t remap_granule = 0x8000000;

    // The DCP tree places window one's entries at 0x1ea2080. XBL programs them at 0x09ca2080.
    // Bit zero enables an entry, and bits 31:27 select the system target.
    // A disabled entry passes the address unchanged. The tree declares DDR at DCP 0x80000000,
    // and XBL enables only entries zero through six. Entries 8 through 11 must reach DDR.
    void remap(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        tx.set_dmi_allowed(false);
        const uint64_t address = tx.get_address();
        const uint64_t length = tx.get_data_length();
        if (address < remap_base || address - remap_base >= remap_size) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const uint64_t offset = (address - remap_base) % remap_granule;
        if (!length || length > remap_granule - offset) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        const auto index = unsigned((address - remap_base) / remap_granule);
        const uint32_t entry = words_[5][0x80 / 4 + index];
        const uint64_t target = (entry & 1) ? (entry & 0xf8000000u) + offset : address;
        tx.set_address(target);
        soc_socket->b_transport(tx, delay);
        tx.set_address(address);
        tx.set_dmi_allowed(false);
    }

    template <class Source> Source* resolve(const std::string& path) {
        if (path.empty()) return nullptr;
        auto* source = dynamic_cast<Source*>(sc_core::sc_find_object(path.c_str()));
        if (!source && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            source = dynamic_cast<Source*>(sc_core::sc_find_object(relative.c_str()));
        }
        if (!source) throw std::invalid_argument("DCP bootstrap clock parent is missing: " + path);
        return source;
    }

    void end_of_elaboration() override {
        if (reset.size() != 1)
            throw std::invalid_argument("DCP bootstrap requires one CPU reset consumer");
        display_plls_ = resolve<PllSource>(display_pll_path.get_value());
        ahb_root_ = resolve<ClockSource>(ahb_root_path.get_value());
        vsync_root_ = resolve<ClockSource>(vsync_root_path.get_value());
        timer_root_ = resolve<ClockSource>(timer_root_path.get_value());
    }

    void reset_thread() {
        std::uint64_t delivered_generation = 0;
        // Assertion waits for the cold-held CPU's native reset completion.
        reset->write(true);
        for (;;) {
            if (delivered_generation == control_generation_) {
                sc_core::wait(reset_event_);
                continue;
            }
            const auto generation = control_generation_;
            reset->write(true);
            released_ = false;
            if (generation != control_generation_) continue;
            if (requested_release_) {
                cpu_->prepare_reset_vector(requested_entry_);
                // Configuration can change while native reset work suspends this thread.
                if (generation != control_generation_) continue;
                reset->write(false);
                released_ = true;
            }
            delivered_generation = generation;
        }
    }

    bool clocks_enabled() const {
        for (unsigned offset : {4u, 8u, 0x2cu, 0x30u, 0x40u})
            if (!(words_[2][offset / 4] & 1)) return false;
        return (words_[1][8 / 4] & 1) != 0;
    }
    // GDSCR bit 0 is SW_COLLAPSE and bit 1 is HW_CONTROL. The DCP sets HW_CONTROL
    // itself: SM8850 0xa00d8dba and SM8845 0xa4105e82 OR bit 1 into DCP 0x103ab000.
    // Under hardware control, an armed DCP idle entry collapses the MDSS core until
    // software takes control again.
    bool gdsc_on() const {
        const auto gdscr = words_[0][0];
        return !(gdscr & 1) && !((gdscr & 2) && hw_collapsed_);
    }
    // XBL clears SW_COLLAPSE before it releases the DCP. The DCP clock groups use the
    // separate a000 and 4000 reset groups, so a hardware MDSS collapse keeps them running.
    bool dcp_supply() const { return !(words_[0][0] & 1); }
    // SM8850 0xa00eaba6..0xa00eabe0 and SM8845 0xa4116d50..0xa4116d8a set core-clock
    // bit 14 (memory retention) and bit 1 (hardware gating) immediately before deep WFI.
    // This functional policy replaces the opaque CESTA, RSC, and PDC sleep handshake.
    // It keeps that handshake's firmware-visible result: the MDSS core collapses only
    // after the DCP reaches WFI with retention and hardware gating armed.
    void idle() {
        const auto gdscr = words_[0][0];
        const auto control = words_[2][0x30 / 4];
        if (!released_ || !gdsc_on() || !(gdscr & 2) || (control & 0x4002) != 0x4002) return;
        hw_collapsed_ = true;
        std::cerr << "DCP_DISPLAY_DOMAIN_COLLAPSE source=armed-wfi gdscr=0x" << std::hex << gdscr
                  << " control=0x" << control << std::dec << '\n';
        effects();
    }
    void effects() {
        auto& power = words_[0][0];
        power = (power & 0x7fffffff) | (gdsc_on() ? 0x80000000u : 0u);
        // CFG bits 10:8 select XO 0, GPLL0 1, or display PLL1 main 4. A zero divider
        // field stays pending, as the selected XBL write sequence requires.
        for (unsigned i = 0; i < roots.size(); ++i) {
            auto& command = words_[2][roots[i] / 4];
            const auto config = words_[2][roots[i] / 4 + 1];
            if ((command & 1) && dcp_supply() && (config & 0x1f) && source_live((config >> 8) & 7)) {
                command &= ~1u;
                root_ok_[i] = true;
            }
        }
        auto& control = words_[2][0x30 / 4];
        const auto entry = words_[3][0xc / 4];
        const bool release = dcp_supply() && root_ok_[0] && clocks_enabled() &&
            entry != 0 && (control & 0xd) == 1;
        if (release != requested_release_ || entry != requested_entry_) {
            requested_release_ = release;
            requested_entry_ = entry;
            ++control_generation_;
            reset_event_.notify(sc_core::SC_ZERO_TIME);
        }
        // Report completed delivery, not eligibility or a queued native reset job.
        control = (control & 0x7fffffff) | (released_ ? 0u : 0x80000000u);
    }
    bool source_live(unsigned selector) const {
        if (selector == 0) return true; // XO reference.
        if (selector == 1) return true; // GPLL0, which XBL selects before release.
        if (selector == 4)
            return display_plls_ && display_plls_->bank_count() > 1 && display_plls_->main_output_hz(1);
        return false;
    }
    bool root_running(unsigned index) const {
        const auto config = words_[2][roots[index] / 4 + 1];
        return root_ok_[index] && dcp_supply() && source_live((config >> 8) & 7);
    }
    bool external_running(ClockSource* source) const {
        return source ? source->clock_running() : root_running(0);
    }
    // Parents from models/qcom/sm88xx/dcp/clocks-evidence.md. Minus one marks a non-branch word.
    int branch_parent_running(uint64_t address) const {
        switch (address) {
        case 0x09bae004: case 0x09bae00c: case 0x09bb4004: return external_running(ahb_root_);
        case 0x09bae008: return external_running(vsync_root_);
        case 0x09bb4008: return external_running(timer_root_);
        case 0x09bb400c: return 1; // Sleep reference record without a register.
        case 0x09bb4028: case 0x09bb402c: case 0x09bb4040: return root_running(0);
        case 0x09bb405c: return root_running(1);
        case 0x09bb4078: return root_running(2);
        case 0x09bb4094: return root_running(3);
        default: return -1;
        }
    }
    static bool known(uint64_t address) {
        if (address == 0x09bab000 || address == 0x09bab004 || address == 0x09bae000 ||
            address == 0x09bae004 || address == 0x09bae008 || address == 0x09bae00c ||
            address == 0x09c7000c || address == 0x09c70010 || address == 0x09ca0048) return true;
        if (address >= 0x09ca2000 && address <= 0x09ca22ff) return true;
        // DCP core page: BCR, branches, and the CMD/CFG/M/N/D groups of four roots.
        if (address >= 0x09bb4000 && address <= 0x09bb4094) {
            const auto offset = unsigned(address - 0x09bb4000);
            for (const auto root : roots)
                if (offset >= root && offset < root + 0x14) return true;
            switch (offset) {
            case 0x00: case 0x04: case 0x08: case 0x0c: case 0x28: case 0x2c: case 0x30:
            case 0x40: case 0x5c: case 0x78: case 0x94: return true;
            default: return false;
            }
        }
        return false;
    }
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const auto address = tx.get_address();
        unsigned bank = 0;
        while (bank < bases.size() && !(address >= bases[bank] && address < bases[bank] + 0x1000)) ++bank;
        if (bank == bases.size()) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            tx.get_byte_enable_ptr() || (address & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE); return;
        }
        const auto index = unsigned((address - bases[bank]) / 4);
        if (!known(address)) {
            std::cerr << name() << ": exploratory DCP configuration storage at 0x" << std::hex
                      << address << std::dec << ". No inferred completion effect\n";
        }
        auto* data = tx.get_data_ptr();
        if (tx.is_write()) {
            uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= uint32_t(data[i]) << (8 * i);
            if (address == 0x09bab000 || address == 0x09bb4030 ||
                address == 0x09bb4040 || address == 0x09bae004)
                value = (value & 0x7fffffff) | (words_[bank][index] & 0x80000000);
            if (address == 0x09bab004)
                value &= ~((1u << 15) | (1u << 16));
            words_[bank][index] = value;
            // Removing power invalidates the root-clock completion, not guest configuration.
            if (address == 0x09bab000 && (value & 1)) root_ok_ = {};
            // Software control ends a hardware collapse. SW_COLLAPSE still decides power.
            if (address == 0x09bab000 && !(value & 2)) hw_collapsed_ = false;
            effects();
        } else if (tx.is_read()) {
            effects();
            auto value = words_[bank][index];
            if (address == 0x09bab004)
                value = (value & ~((1u << 15) | (1u << 16))) |
                    (gdsc_on() ? 1u << 16 : 1u << 15);
            const int parent = branch_parent_running(address);
            if (parent >= 0 && address != 0x09bb4030) {
                const bool running = (value & 1) && dcp_supply() && parent;
                value = (value & 0x7fffffff) | (running ? 0u : 0x80000000u);
            }
            for (unsigned i = 0; i < roots.size(); ++i)
                if (address == 0x09bb4000 + roots[i])
                    value = (value & 0x7fffffff) | (root_running(i) ? 0u : 0x80000000u);
            for (unsigned i = 0; i < 4; ++i) data[i] = static_cast<unsigned char>(value >> (8 * i));
        } else { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

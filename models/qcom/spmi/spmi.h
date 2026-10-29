// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once

#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <sstream>
#include <vector>
#include <stdexcept>
#include <string>
#include <tlm-extensions/mem-tx-attrs.h>
#include "pmic_access.h"

namespace qcom_vp {

// Register offsets describe one arbiter version. Platform data supplies its base.
class QcomSpmiArbiter : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomSpmiArbiter> target_socket{"target_socket"};
    tlm_utils::simple_target_socket_optional<QcomSpmiArbiter> dcp_socket{"dcp_socket"};
    tlm_utils::simple_initiator_socket<QcomSpmiArbiter> pmic_socket{"pmic_socket"};
    cci::cci_param<unsigned> executing_owner{"executing_owner", 0, "APSS arbiter master index in 0..12"};
    cci::cci_param<unsigned> secure_write_owner_mask{
        "secure_write_owner_mask", 0, "Explicit foreign write owners available to a secure requester"};
    cci::cci_param<unsigned> secure_observer_ee{
        "secure_observer_ee", UINT32_MAX, "Additional observer EE window available to a secure requester"};
    cci::cci_param<unsigned> initial_bus_enable{
        "initial_bus_enable", 0, "Explicit inherited virtual enable bits for bus zero and bus one"};
    cci::cci_param<std::string> completion_policy{
        "completion_policy", "disabled", "disabled or synchronous-virtual; no physical wire timing"};
    cci::cci_param<std::string> ibus_policy{
        "ibus_policy", "disabled", "The IBUS producer policy is disabled or disconnected"};
    cci::cci_param<std::string> initial_routes{
        "initial_routes", "", "Explicit virtual routes: bus apid sid peripheral owner irq_owner, separated by semicolons"};

    explicit QcomSpmiArbiter(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomSpmiArbiter::transport);
        dcp_socket.register_b_transport(this, &QcomSpmiArbiter::dcp_transport);
    }

    void cold_reset() {
        ppids_.fill(0);
        owners_.fill(0);
        vrm_.fill(0);
        reset_owners_.fill(0);
        ibus_enable_ = 0;
        ibus_config_c_ = 0;
        ibus_config_10_ = 0;
        ibus_slots_.fill(0);
        ibus_slot_controls_.fill(0);
        for (auto& bus : arbiter_priorities_) bus.fill(0);
        pvc_global_.fill(0);
        vrm_control_.fill(0);
        port_global_.fill(0);
        vrm_enable_ = 0;
        for (auto& bus : pvc_) bus.fill(0);
        for (auto& bus : pvc_commands_) {
            for (auto& port : bus) port.fill(0);
        }
        for (auto& bus : port_config_) bus.fill(0);
        for (auto& bus : port_enable_) bus.fill(0);
        for (auto& bus : port_priority_) bus.fill(0);
        for (auto& bus : vioctl_positive_) bus.fill(0);
        for (auto& bus : vioctl_self_clear_) bus.fill(0);
        for (auto& bus : vioctl_negative_) bus.fill(0);
        for (auto& lane : write_) lane = Lane{};
        for (auto& lane : observe_) lane = Lane{};
        for (auto& lane : secure_observe_) lane = Lane{};
        for (auto& lane : dcp_observe_) lane = Lane{};
        for (auto& bus : controls_) bus.fill(0);
        for (auto& bus : timing_) bus.fill(0);
        global_.fill(0);
        for (unsigned bus = 0; bus < 2; ++bus)
            controls_[bus][6] = (initial_bus_enable.get_value() >> bus) & 1;
        for (const auto& route : routes_) {
            ppids_[route.apid] = route.ppid;
            owners_[route.apid] = route.owner;
        }
    }

private:
    struct Lane {
        std::uint32_t command = 0;
        std::uint32_t config = 0;
        std::uint32_t status = 1;
        std::array<unsigned char, 8> wdata{};
        std::array<unsigned char, 8> rdata{};
        unsigned written = 0;
        bool readable = false;
        unsigned irq_enable = 0;
    };
    // The gap in bus one's APID space remains inaccessible.
    std::array<std::uint32_t, 4096> ppids_{};
    std::array<std::uint32_t, 4096> owners_{};
    std::array<std::uint32_t, 256> vrm_{};
    std::array<std::uint32_t, 2> pvc_global_{};
    std::array<std::uint32_t, 2> vrm_control_{};
    std::array<std::uint32_t, 2> port_global_{};
    std::uint32_t vrm_enable_ = 0;
    std::array<std::array<std::uint32_t, 6>, 2> pvc_{};
    std::array<std::array<std::array<std::uint32_t, 16>, 6>, 2> pvc_commands_{};
    std::array<std::array<std::uint32_t, 10>, 2> port_config_{};
    std::array<std::array<std::uint32_t, 10>, 2> port_enable_{};
    std::array<std::array<std::uint32_t, 10>, 2> port_priority_{};
    // Both selected VIOCTL tables require port indices through 12.
    std::array<std::array<std::uint32_t, 13>, 2> vioctl_positive_{};
    std::array<std::array<std::uint32_t, 13>, 2> vioctl_self_clear_{};
    std::array<std::array<std::uint32_t, 13>, 2> vioctl_negative_{};
    std::array<std::array<std::uint32_t, 9>, 2> arbiter_priorities_{};
    std::array<std::uint32_t, 512> reset_owners_{};
    std::array<Lane, 4096> write_{};
    std::array<Lane, 4096> observe_{};
    std::array<Lane, 4096> secure_observe_{};
    std::array<Lane, 4096> dcp_observe_{};
    std::array<std::array<std::uint32_t, 13>, 2> controls_{};
    std::array<std::array<std::uint32_t, 31>, 2> timing_{};
    std::array<std::uint32_t, 3> global_{};
    unsigned owner_ = 0;
    bool functional_ = false;
    bool ibus_disconnected_ = false;
    std::uint32_t ibus_enable_ = 0;
    std::uint32_t ibus_config_c_ = 0;
    std::uint32_t ibus_config_10_ = 0;
    std::array<std::uint32_t, 16> ibus_slots_{};
    std::array<std::uint32_t, 16> ibus_slot_controls_{};
    struct Route {
        unsigned apid;
        std::uint32_t ppid;
        unsigned owner;
    };
    std::vector<Route> routes_;

    // XBL 0x1486f958..0x1486fba8. These are retained digital controls only.
    // XBL 0x1487b4f4 and 0x14879bf0 set bits 22 and 21 before controller initialization.
    inline static constexpr std::array<std::uint32_t, 13> control_offsets_{
        0x25000, 0x25010, 0x25014, 0x25018, 0x25048,
        0x27800, 0x27804, 0x27808, 0x25028, 0x2502c, 0x27814, 0x2781c, 0x27820};
    inline static constexpr std::array<std::uint32_t, 13> control_masks_{
        1, 1, 3, 0x27f, 1, 3, 0x1c600bab, 1, 0, 0, 1, UINT32_MAX, UINT32_MAX};

    // Descriptor +0x48, not a scaled window number:
    // SM8850 0x148f3b18/0x148f3b70, SM8845 0x148f1520/0x148f1578.
    // MGPI edge configuration is passive: no external edge producer is connected.
    inline static constexpr std::array<unsigned, 2> port_bases_{0xb000, 0x16000};

    static bool valid_apid(unsigned apid) {
        return apid < 2048 || (apid >= 2048 && apid < 2560);
    }
    static std::uint32_t load(const unsigned char* p) {
        return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
            (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
    }
    static void store(unsigned char* p, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (i * 8));
    }
    void end_of_elaboration() override {
        owner_ = executing_owner.get_value();
        const auto& policy = completion_policy.get_value();
        const auto& ibus_policy = this->ibus_policy.get_value();
        if (ibus_policy != "disabled" && ibus_policy != "disconnected")
            throw std::invalid_argument("Invalid IBUS producer policy");
        ibus_disconnected_ = ibus_policy == "disconnected";
        if (owner_ > 12 || (secure_write_owner_mask.get_value() & ~0x1fffu) ||
            initial_bus_enable.get_value() > 3 ||
            (policy != "disabled" && policy != "synchronous-virtual") || pmic_socket.size() != 1)
            throw std::invalid_argument("Invalid SPMI policy or missing PMIC target");
        functional_ = policy == "synchronous-virtual";
        std::istringstream input(initial_routes.get_value());
        std::string row;
        std::array<bool, 4096> assigned{};
        while (std::getline(input, row, ';')) {
            if (row.find_first_not_of(" \t\n\r") == std::string::npos) continue;
            std::istringstream fields(row);
            std::array<unsigned, 6> v{};
            for (auto& value : v) {
                std::string token;
                if (!(fields >> token)) throw std::invalid_argument("Incomplete virtual SPMI route");
                std::size_t used = 0;
                const auto number = std::stoul(token, &used, 0);
                if (used != token.size() || number > 0xffffffffUL)
                    throw std::invalid_argument("Invalid virtual SPMI route number");
                value = static_cast<unsigned>(number);
            }
            std::string extra;
            if ((fields >> extra) || v[0] > 1 || v[1] >= (v[0] ? 512u : 2048u) ||
                v[2] > 15 || !v[3] || v[3] > 255 || v[4] > 12 || v[5] > 1)
                throw std::invalid_argument("Invalid virtual SPMI route");
            const unsigned apid = v[0] * 2048 + v[1];
            if (assigned[apid]) throw std::invalid_argument("Duplicate virtual SPMI APID");
            assigned[apid] = true;
            routes_.push_back(Route{apid, (v[5] << 31) | (v[2] << 8) | v[3], v[4]});
        }
        cold_reset();
    }

    void command(Lane& lane, unsigned apid, bool observer, bool observation,
                 std::uint32_t value,
                 tlm::tlm_generic_payload& mmio, sc_core::sc_time& delay,
                 unsigned requester, bool allow_secure_grant) {
        const unsigned opcode = value >> 27;
        const unsigned count = (value & 7) + 1;
        const unsigned offset = (value >> 4) & 255;
        const auto ppid = ppids_[apid];
        const unsigned sid = (ppid >> 8) & 31;
        const unsigned peripheral = ppid & 255;
        if (!functional_ || (value & ~0x0c000ff7u) || opcode > 1 ||
            (observer && opcode != 1) || !peripheral || sid > 15 || offset + count > 256 ||
            (!opcode && (lane.written & ((1u << count) - 1)) != ((1u << count) - 1))) {
            mmio.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        lane.command = value;
        lane.status = 0;
        lane.readable = false;
        lane.written = 0;
        const unsigned bus = apid / 2048;
        const auto* attributes = mmio.get_extension<QemuMemTxAttrsExtension>();
        const bool secure = allow_secure_grant && attributes &&
            !attributes->attrs.unspecified && attributes->attrs.secure;
        const bool permitted_owner = owners_[apid] == requester ||
            (secure && (secure_write_owner_mask.get_value() & (1u << owners_[apid])));
        if (!(controls_[bus][6] & 1) || (!observer && !permitted_owner)) {
            lane.status = 5; // DONE | DENIED, without a peripheral side effect.
            return;
        }
        std::array<unsigned char, 8> data = lane.wdata;
        tlm::tlm_generic_payload request;
        request.set_address((bus << 20) | (sid << 16) | (peripheral << 8) | offset);
        request.set_command(opcode ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND);
        request.set_data_ptr(data.data());
        request.set_data_length(count);
        request.set_streaming_width(count);
        request.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        // The dump framework reads through the secure observer window. Other
        // observer reads are driver transactions, so they carry no marker.
        // The PMIC backend decides which unowned bytes an observation covers.
        PmicObserverExtension marker;
        if (observation) request.set_extension(&marker);
        // A command snapshots WDATA. The backend validates the whole byte burst.
        pmic_socket->b_transport(request, delay);
        if (request.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE) {
            lane.status = 3; // DONE | FAILURE: explicitly absent SID only.
            return;
        }
        if (request.is_response_error() || request.get_response_status() != tlm::TLM_OK_RESPONSE) {
            lane.status = 3;
            mmio.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        lane.status = 1;
        if (opcode) {
            lane.rdata.fill(0);
            for (unsigned i = 0; i < count; ++i) lane.rdata[i] = data[i];
            lane.readable = true;
        }
    }

    bool table(std::uint64_t a, std::uint32_t*& word, std::uint32_t& mask) {
        // IBUS v1 configuration, XBL 0x1488d3c8..0x1488d52c.
        // These words select alert slots and their consumers, not SPMI commands.
        // The disconnected producer never requests a capture or asserts an alert.
        if (ibus_disconnected_) {
            if (a == 0x69000) {
                word = &ibus_enable_;
                mask = 3;
                return true;
            }
            if (a == 0x6900c) {
                word = &ibus_config_c_;
                mask = UINT32_MAX;
                return true;
            }
            if (a == 0x69010) {
                word = &ibus_config_10_;
                mask = 0x3f;
                return true;
            }
            if (a >= 0x69100 && a < 0x69140) {
                word = &ibus_slots_[(a - 0x69100) / 4];
                mask = UINT32_MAX;
                return true;
            }
            if (a >= 0x69200 && a < 0x69240) {
                word = &ibus_slot_controls_[(a - 0x69200) / 4];
                mask = 0x3f;
                return true;
            }
        }
        if (a >= 0x3000 && a < 0x7000) {
            const auto apid = static_cast<unsigned>((a - 0x3000) / 4);
            if (!valid_apid(apid)) return false;
            word = &ppids_[apid];
            mask = 0x80001fff;
            return true;
        }
        if (a == 0x2100) {
            word = &vrm_enable_;
            mask = 1;
            return true;
        }
        // XBL 0x1486f2e0..0x1486f358 uses one shared table, with bus bits 22:21.
        if (a >= 0x2300 && a < 0x2700) {
            word = &vrm_[(a - 0x2300) / 4];
            mask = 0x7fff00;
            return true;
        }
        for (unsigned bus = 0; bus < 2; ++bus) {
            if (a == 0x300 + bus * 0x100) {
                word = &pvc_global_[bus];
                mask = 1;
                return true;
            }
            if (a == 0x2110 + bus * 0x40) {
                word = &vrm_control_[bus];
                mask = 3;
                return true;
            }
            for (unsigned port = 0; port < (bus ? 2u : 6u); ++port) {
                if (a == 0x308 + bus * 0x100 + port * 4) {
                    word = &pvc_[bus][port];
                    mask = 3;
                    return true;
                }
                const auto base = 0x900 + bus * 0x400 + port * 0x40;
                if (a >= base && a < base + 16 * 4) {
                    word = &pvc_commands_[bus][port][(a - base) / 4];
                    mask = 0xfffff;
                    return true;
                }
            }
            // Native MGPI helper 0x1486f404 (SM8845 0x1486db04).
            const unsigned port_base = port_bases_[bus];
            const unsigned ports = bus ? 2 : 10;
            for (unsigned port = 0; port < ports; ++port) {
                if (a == port_base + port * 8) {
                    word = &port_config_[bus][port];
                    mask = 0x0fff0fff;
                    return true;
                }
                if (a == port_base + port * 8 + 4) {
                    word = &port_enable_[bus][port];
                    mask = 3;
                    return true;
                }
                if (a == port_base + 0x1004 + port * 0x1000) {
                    word = &port_priority_[bus][port];
                    mask = 7;
                    return true;
                }
            }
            // XBL 0x1486f5f0 configures VIOCTL, not MGPI ports or arbiter priorities.
            for (unsigned port = 0; port < vioctl_positive_[bus].size(); ++port) {
                if (a == 0x27a00 + bus * 0x11000 + port * 4) {
                    word = &vioctl_positive_[bus][port];
                    mask = 0xff8fffff;
                    return true;
                }
                if (a == 0x27a80 + bus * 0x11000 + port * 4) {
                    word = &vioctl_self_clear_[bus][port];
                    mask = 1;
                    return true;
                }
                if (a == 0x27b80 + bus * 0x11000 + port * 4) {
                    word = &vioctl_negative_[bus][port];
                    mask = 0xff8fffff;
                    return true;
                }
            }
            const unsigned priority_count = bus ? 5 : 9;
            for (unsigned priority = 0; priority < priority_count; ++priority) {
                if (a == 0x100 + bus * 0x50 + priority * 4) {
                    word = &arbiter_priorities_[bus][priority];
                    mask = 15;
                    return true;
                }
            }
        }
        // XBL 0x1486f8c0..0x1486f8d0 uses the global APID during bus-one reset.
        // Retain this distinct bank. No unproved alias to local owners is made.
        if (a >= 0x3d000 && a < 0x3d800) {
            word = &reset_owners_[(a - 0x3d000) / 4];
            mask = 15;
            return true;
        }
        for (unsigned bus = 0; bus < 2; ++bus) {
            const auto base = 0x2a000 + bus * 0x11000;
            const unsigned count = bus ? 512 : 2048;
            if (a >= base && a < base + count * 4) {
                word = &owners_[bus * 2048 + (a - base) / 4];
                mask = 15;
                return true;
            }
            if (a == port_bases_[bus] + 0x100) {
                word = &port_global_[bus];
                mask = 1;
                return true;
            }
            // XBL 0x148bdba0 and 0x148bc0c8 program both complete timing banks.
            const auto timing_base = 0x25100 + bus * 0x11000;
            if (a >= timing_base && a < timing_base + timing_[bus].size() * 4) {
                word = &timing_[bus][(a - timing_base) / 4];
                mask = UINT32_MAX;
                return true;
            }
            for (unsigned i = 0; i < control_offsets_.size(); ++i) {
                if (a == control_offsets_[i] + bus * 0x11000) {
                    word = &controls_[bus][i];
                    mask = control_masks_[i];
                    return true;
                }
            }
        }
        constexpr std::array<unsigned, 3> offsets{0x3c, 0x44, 0x48};
        for (unsigned i = 0; i < offsets.size(); ++i) {
            if (a == offsets[i]) {
                word = &global_[i];
                mask = 1;
                return true;
            }
        }
        return false;
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        access(tx, delay, owner_, false);
    }

    void dcp_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        access(tx, delay, 11, true);
    }

    void access(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay,
                unsigned requester, bool dcp) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        const auto a = tx.get_address();
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 || (a & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
        const auto value = load(tx.get_data_ptr());
        // DCP a00ef9e4 / a411bbd2 reads counts and PIC group counts per bus.
        // Bus one starts after all 2048 bus-zero APIDs in the shared map.
        if (a == 0 || a == 4 || a == 8) {
            if (tx.is_read()) {
                const std::uint32_t result = a == 0 ? 0x80000000u :
                    a == 4 ? (64u << 23) | 2048u : (16u << 23) | 512u;
                store(tx.get_data_ptr(), result);
            } else tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (a == 0x69004 && ibus_disconnected_) {
            if (tx.is_read()) store(tx.get_data_ptr(), 0x100); // IBUS major version 1.
            else tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        std::uint32_t* word = nullptr;
        std::uint32_t mask = 0;
        if (table(a, word, mask)) {
            if (tx.is_read()) store(tx.get_data_ptr(), *word);
            else if (dcp || !mask || (value & ~mask) || (mask == 15 && value > 12))
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            else *word = value;
            return;
        }
        for (unsigned bus = 0; bus < 2; ++bus) {
            if (a == 0x28000 + bus * 0x11000 && tx.is_read()) {
                store(tx.get_data_ptr(), 0); // No injected protocol errors.
                return;
            }
        }
        // XBL 0x1487506c reads PIC groups. No PMIC IRQ input is connected.
        if (a >= 0x4c0000 && a < 0x4e0000 && tx.is_read()) {
            const unsigned bus = static_cast<unsigned>((a - 0x4c0000) / 0x10000);
            const unsigned bank = static_cast<unsigned>((a - 0x4c0000) % 0x10000);
            if (bank / 0x1000 < 13 && bank % 0x1000 < (bus ? 16u : 64u) * 4) {
                store(tx.get_data_ptr(), 0);
                return;
            }
        }
        unsigned apid = 4096, reg = 0;
        bool observer = false, secure_window = false;
        if (a >= 0x500000 && a < 0x700000) {
            apid = static_cast<unsigned>((a - 0x500000) / 0x200);
            reg = static_cast<unsigned>((a - 0x500000) % 0x200);
        } else if (a >= 0xc0000 && a < 0x4c0000) {
            const auto bank = a - 0xc0000;
            const auto ee = static_cast<unsigned>(bank / 0x40000);
            const auto* attributes = tx.get_extension<QemuMemTxAttrsExtension>();
            // QSEE uses its own observer window. Its secure attributes select that window.
            secure_window = !dcp && ee != requester && ee == secure_observer_ee.get_value() &&
                attributes && !attributes->attrs.unspecified && attributes->attrs.secure;
            if (ee != requester && !secure_window) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            apid = static_cast<unsigned>((bank % 0x40000) / 0x20);
            reg = static_cast<unsigned>(bank % 0x20);
            observer = true;
        }
        if (!valid_apid(apid)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& lane = !observer ? write_[apid] : dcp ? dcp_observe_[apid] :
            secure_window ? secure_observe_[apid] : observe_[apid];
        if (tx.is_read()) {
            std::uint32_t result = 0;
            if (reg == 0) result = lane.command;
            else if (reg == 4) result = lane.config;
            else if (reg == 8) result = lane.status;
            else if (!observer && reg == 0x100) result = lane.irq_enable;
            else if (!observer && (reg == 0x104 || reg == 0x108)) result = 0;
            else if (!observer && (reg == 0x10 || reg == 0x14)) result = load(lane.wdata.data() + reg - 0x10);
            else if ((reg == 0x18 || reg == 0x1c) && lane.readable)
                result = load(lane.rdata.data() + reg - 0x18);
            else {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            store(tx.get_data_ptr(), result);
        } else if (reg == 0)
            command(lane, apid, observer, secure_window, value, tx, delay, requester, !dcp);
        else if (reg == 4 && !(value & ~1u)) lane.config = value;
        else if (!observer && (reg == 0x10 || reg == 0x14)) {
            const unsigned start = reg - 0x10;
            store(lane.wdata.data() + start, value);
            lane.written |= 15u << start;
        } else if (!observer && reg == 0x100 && !(value & ~1u)) lane.irq_enable = value;
        else if (!observer && reg == 0x108 && !(value & ~255u)) {
            // W1C on deasserted inputs leaves all inputs deasserted.
        } else tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    }
};

} // namespace qcom_vp

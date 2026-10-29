// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "cpu_domain_core.h"
#include "qcom/apss/idle_state.h"
#include "zonda_pll.h"
#include "qcom/sm88xx/pdp/cprh.h"
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstdint>
#include <iostream>

namespace qcom_vp {

// Two domain lanes. All addresses are local to their separately mapped sockets.
// Register names below describe virtual functional policy, not undocumented RTL names.
class QcomCpuDomainPower : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> init_socket{"init_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> clock_socket{"clock_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> control_socket{"control_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> tags0_socket{"tags0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> tags1_socket{"tags1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> aux0_socket{"aux0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> aux1_socket{"aux1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> downstream0_socket{"downstream0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> downstream1_socket{"downstream1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> route0_socket{"route0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> route1_socket{"route1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> runtime0_socket{"runtime0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> runtime1_socket{"runtime1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> window0_socket{"window0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> window1_socket{"window1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> startup_socket{"startup_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> power0_socket{"power0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> power1_socket{"power1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> bridge0_socket{"bridge0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> bridge1_socket{"bridge1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> sequence0_socket{"sequence0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> sequence1_socket{"sequence1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> fabric0_socket{"fabric0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> fabric1_socket{"fabric1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> feature0_socket{"feature0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> feature1_socket{"feature1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> core0_socket{"core0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> core1_socket{"core1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> cluster0_socket{"cluster0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> cluster1_socket{"cluster1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> table0_socket{"table0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> table1_socket{"table1_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> cluster_aux0_socket{"cluster_aux0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> cluster_aux1_socket{"cluster_aux1_socket"};
    // Relative per-lane pair at 0x19050000 + core*0x100000 + lane*0x1000000.
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> release0_socket{"release0_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpuDomainPower> release1_socket{"release1_socket"};
    InitiatorSignalSocket<bool> pdp_reset0{"pdp_reset0"};
    InitiatorSignalSocket<bool> pdp_reset1{"pdp_reset1"};
    InitiatorSignalSocket<std::uint32_t> pdp_vector0{"pdp_vector0"};
    InitiatorSignalSocket<std::uint32_t> pdp_vector1{"pdp_vector1"};
    // Optional APSS view of the PDP CPU boot status block. See
    // models/qcom/sm88xx/cpucp/cpu_domain_core_evidence.md "APSS CPU boot status block".
    tlm_utils::simple_initiator_socket_optional<QcomCpuDomainPower> boot_status_socket{
        "boot_status_socket"};
#define QCOM_DOMAIN_CORE_OUTPUT(lane, core) \
    InitiatorSignalSocket<bool> reset_l##lane##_c##core{"reset_l" #lane "_c" #core}; \
    InitiatorSignalSocket<std::uint64_t> vector_l##lane##_c##core{"vector_l" #lane "_c" #core}; \
    cci::cci_param<std::string> vector_l##lane##_c##core##_target{ \
        "vector_l" #lane "_c" #core "_target", "", "Optional reset-vector input path"};
    QCOM_DOMAIN_CORE_OUTPUT(0, 0)
    QCOM_DOMAIN_CORE_OUTPUT(0, 1)
    QCOM_DOMAIN_CORE_OUTPUT(0, 2)
    QCOM_DOMAIN_CORE_OUTPUT(0, 3)
    QCOM_DOMAIN_CORE_OUTPUT(0, 4)
    QCOM_DOMAIN_CORE_OUTPUT(0, 5)
    QCOM_DOMAIN_CORE_OUTPUT(0, 6)
    QCOM_DOMAIN_CORE_OUTPUT(0, 7)
    QCOM_DOMAIN_CORE_OUTPUT(1, 0)
    QCOM_DOMAIN_CORE_OUTPUT(1, 1)
    QCOM_DOMAIN_CORE_OUTPUT(1, 2)
    QCOM_DOMAIN_CORE_OUTPUT(1, 3)
    QCOM_DOMAIN_CORE_OUTPUT(1, 4)
    QCOM_DOMAIN_CORE_OUTPUT(1, 5)
    QCOM_DOMAIN_CORE_OUTPUT(1, 6)
    QCOM_DOMAIN_CORE_OUTPUT(1, 7)
#undef QCOM_DOMAIN_CORE_OUTPUT
    cci::cci_param<std::string> root_path{"root_path", "cpucp_clock_root_3", "Shared domain programming clock root"};
    cci::cci_param<std::string> branches_path{"branches_path", "cpucp_clock_branches", "Shared five branch enables"};
    cci::cci_param<std::string> ncc_pll0_path{"ncc_pll0_path", "", "Optional lane zero NCC PLL"};
    cci::cci_param<std::string> ncc_pll1_path{"ncc_pll1_path", "", "Optional lane one NCC PLL"};
    cci::cci_param<std::string> cprh_path{"cprh_path", "", "Optional functional CPRH power controller"};
    cci::cci_param<unsigned> lane0_attached_mask{"lane0_attached_mask", 1, "Lane zero cores with reset consumers"};
    cci::cci_param<unsigned> lane1_attached_mask{"lane1_attached_mask", 0, "Lane one cores with reset consumers"};
    cci::cci_param<unsigned> boot_lane{"boot_lane", 0, "Board boot CPU lane"};
    cci::cci_param<unsigned> boot_core{"boot_core", 0, "Board boot CPU core"};
    cci::cci_param<unsigned> initially_powered_mask{"initially_powered_mask", 1, "Explicit board boot power policy, not silicon POR"};
    cci::cci_param<unsigned> lane0_core_mask{"lane0_core_mask", 0xff, "Implemented core bits in lane 0"};
    cci::cci_param<unsigned> lane1_core_mask{"lane1_core_mask", 0xff, "Implemented core bits in lane 1"};
    // The selected TZ polls 0x81f5a000 + lane*0x40000 for the PDP header and a
    // per-core bitmap. The PDP firmware normally publishes it.
    cci::cci_param<bool> boot_status_enable{"boot_status_enable", false,
        "Publish the APSS CPU boot status block through an optional memory socket"};
    cci::cci_param<std::uint64_t> boot_status_base{"boot_status_base", 0x81f5a000,
        "APSS CPU boot status block base address"};

    SC_HAS_PROCESS(QcomCpuDomainPower);
    explicit QcomCpuDomainPower(sc_core::sc_module_name name) : sc_module(name) {
        init_socket.register_b_transport(this, &QcomCpuDomainPower::init_transport);
        clock_socket.register_b_transport(this, &QcomCpuDomainPower::clock_transport);
        control_socket.register_b_transport(this, &QcomCpuDomainPower::control_transport);
        tags0_socket.register_b_transport(this, &QcomCpuDomainPower::tags0_transport);
        tags1_socket.register_b_transport(this, &QcomCpuDomainPower::tags1_transport);
        aux0_socket.register_b_transport(this, &QcomCpuDomainPower::aux0_transport);
        aux1_socket.register_b_transport(this, &QcomCpuDomainPower::aux1_transport);
        downstream0_socket.register_b_transport(this, &QcomCpuDomainPower::downstream0_transport);
        downstream1_socket.register_b_transport(this, &QcomCpuDomainPower::downstream1_transport);
        route0_socket.register_b_transport(this, &QcomCpuDomainPower::route0_transport);
        route1_socket.register_b_transport(this, &QcomCpuDomainPower::route1_transport);
        runtime0_socket.register_b_transport(this, &QcomCpuDomainPower::runtime0_transport);
        runtime1_socket.register_b_transport(this, &QcomCpuDomainPower::runtime1_transport);
        window0_socket.register_b_transport(this, &QcomCpuDomainPower::window0_transport);
        window1_socket.register_b_transport(this, &QcomCpuDomainPower::window1_transport);
        startup_socket.register_b_transport(this, &QcomCpuDomainPower::startup_transport);
        power0_socket.register_b_transport(this, &QcomCpuDomainPower::power0_transport);
        power1_socket.register_b_transport(this, &QcomCpuDomainPower::power1_transport);
        bridge0_socket.register_b_transport(this, &QcomCpuDomainPower::bridge0_transport);
        bridge1_socket.register_b_transport(this, &QcomCpuDomainPower::bridge1_transport);
        sequence0_socket.register_b_transport(this, &QcomCpuDomainPower::sequence0_transport);
        sequence1_socket.register_b_transport(this, &QcomCpuDomainPower::sequence1_transport);
        fabric0_socket.register_b_transport(this, &QcomCpuDomainPower::fabric0_transport);
        fabric1_socket.register_b_transport(this, &QcomCpuDomainPower::fabric1_transport);
        core0_socket.register_b_transport(this, &QcomCpuDomainPower::core0_transport);
        core1_socket.register_b_transport(this, &QcomCpuDomainPower::core1_transport);
        cluster0_socket.register_b_transport(this, &QcomCpuDomainPower::cluster0_transport);
        cluster1_socket.register_b_transport(this, &QcomCpuDomainPower::cluster1_transport);
        table0_socket.register_b_transport(this, &QcomCpuDomainPower::table0_transport);
        table1_socket.register_b_transport(this, &QcomCpuDomainPower::table1_transport);
        feature0_socket.register_b_transport(this, &QcomCpuDomainPower::feature0_transport);
        feature1_socket.register_b_transport(this, &QcomCpuDomainPower::feature1_transport);
        cluster_aux0_socket.register_b_transport(this, &QcomCpuDomainPower::cluster_aux0_transport);
        cluster_aux1_socket.register_b_transport(this, &QcomCpuDomainPower::cluster_aux1_transport);
        release0_socket.register_b_transport(this, &QcomCpuDomainPower::release0_transport);
        release1_socket.register_b_transport(this, &QcomCpuDomainPower::release1_transport);
        SC_THREAD(reset_thread);
    }

    bool powered(unsigned lane) const { return lanes_.at(lane).powered; }
    bool isolated(unsigned lane) const {
        const auto& state = lanes_.at(lane);
        return (state.isolation_a & 1) || (state.isolation_b & 5);
    }
    bool reset_asserted(unsigned lane, unsigned core) const { return applied_reset_.at(lane).at(core); }

private:
    struct Lane {
        std::array<std::uint32_t, 6> tags{};
        std::uint32_t tag_writes = 0;
        std::uint32_t isolation_a = 1;
        std::uint32_t isolation_b = 5;
        std::uint32_t control = 0;
        std::uint32_t downstream = 0;
        bool supply_released = false;
        bool reset_gate_released = false;
        bool powered = false;
        bool supply_ack = false;
        bool reset_seen = false;
        std::uint64_t reset_requested = 0;
        std::uint64_t reset_delivered = 0;
        std::uint64_t vector_generation = 0;
        bool initialized = false;
        bool domain_complete = false;
    };
    std::array<Lane, 2> lanes_{};
    std::array<CpuDomainCoreState, 2> cores_{};
    std::array<std::uint32_t, 3> interrupt_configuration_{};
    std::array<std::array<std::uint32_t, 3>, 2> startup_configuration_{};
    std::array<std::array<bool, 8>, 2> applied_reset_{};
    std::array<std::array<std::uint64_t, 8>, 2> sampled_vector_{};
    std::array<std::array<bool, 8>, 2> release_pending_{};
    // A start request for a core in C4 holds it in reset for one pass, then releases it.
    std::array<std::array<bool, 8>, 2> power_cycle_{};
    std::array<std::uint32_t, 2> boot_status_online_{};
    bool boot_status_published_ = false;
    bool boot_status_force_ = false;
    std::array<std::array<InitiatorSignalSocket<bool>*, 8>, 2> resets_{{
        {{&reset_l0_c0, &reset_l0_c1, &reset_l0_c2, &reset_l0_c3,
          &reset_l0_c4, &reset_l0_c5, &reset_l0_c6, &reset_l0_c7}},
        {{&reset_l1_c0, &reset_l1_c1, &reset_l1_c2, &reset_l1_c3,
          &reset_l1_c4, &reset_l1_c5, &reset_l1_c6, &reset_l1_c7}}}};
    std::array<std::array<InitiatorSignalSocket<std::uint64_t>*, 8>, 2> vectors_{{
        {{&vector_l0_c0, &vector_l0_c1, &vector_l0_c2, &vector_l0_c3,
          &vector_l0_c4, &vector_l0_c5, &vector_l0_c6, &vector_l0_c7}},
        {{&vector_l1_c0, &vector_l1_c1, &vector_l1_c2, &vector_l1_c3,
          &vector_l1_c4, &vector_l1_c5, &vector_l1_c6, &vector_l1_c7}}}};
    std::array<std::array<cci::cci_param<std::string>*, 8>, 2> vector_targets_{{
        {{&vector_l0_c0_target, &vector_l0_c1_target, &vector_l0_c2_target, &vector_l0_c3_target,
          &vector_l0_c4_target, &vector_l0_c5_target, &vector_l0_c6_target, &vector_l0_c7_target}},
        {{&vector_l1_c0_target, &vector_l1_c1_target, &vector_l1_c2_target, &vector_l1_c3_target,
          &vector_l1_c4_target, &vector_l1_c5_target, &vector_l1_c6_target, &vector_l1_c7_target}}}};
    unsigned attached_mask(unsigned lane) const {
        return lane == 0 ? lane0_attached_mask.get_value() : lane1_attached_mask.get_value();
    }
    std::uint32_t init_writes_ = 0;
    std::array<std::uint32_t, 5> init_config_{};
    std::uint32_t requests_ = 0;
    QcomClockRoot* root_ = nullptr;
    QcomClockBranchEnables* branches_ = nullptr;
    std::array<QcomZondaPll*, 2> ncc_plls_{};
    PdpCprh* cprh_ = nullptr;
    std::array<std::array<const CpuIdleState*, 8>, 2> idle_{};
    sc_core::sc_event reset_event_;

    template<class T> T* resolve(const std::string& path) {
        auto* object = dynamic_cast<T*>(sc_core::sc_find_object(path.c_str()));
        if (!object && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            object = dynamic_cast<T*>(sc_core::sc_find_object(relative.c_str()));
        }
        if (!object) throw std::invalid_argument("The CPU domain cannot resolve the configured object: " + path);
        return object;
    }
    void before_end_of_elaboration() override {
        // QBox's named binder supports bool and uint32_t signals, not uint64_t.
        for (unsigned lane = 0; lane < 2; ++lane) {
            for (unsigned core = 0; core < 8; ++core) {
                const auto& path = vector_targets_[lane][core]->get_value();
                if (path.empty()) continue;
                auto* target = resolve<TargetSignalSocket<std::uint64_t>>(path);
                vectors_[lane][core]->bind(*target);
                // The vector input belongs to the CPU. A fixture target without idle state reports C0.
                idle_[lane][core] = dynamic_cast<const CpuIdleState*>(target->get_parent_object());
            }
        }
    }
    // NCC_BOOT_STS1. See cpu_domain_core_evidence.md "Core C-state report".
    std::uint64_t boot_status1(unsigned lane) const {
        const auto& core = cores_[lane];
        // NCC_PWR_CFG1 (+0x290): OK2ENTERC4 [15:8] and OK2ENTERC3 [7:0].
        const auto enables = core.words[4];
        // YLDCOREVLD keeps its reset value. NCCRESETDONE is the supply acknowledgement.
        std::uint64_t value = (UINT64_C(0xff) << 56) | std::uint64_t{lanes_[lane].supply_ack};
        for (unsigned index = 0; index < 8; ++index) {
            const bool attached = attached_mask(lane) & (1u << index);
            std::uint64_t state = 4; // No power.
            if (!core.core_reset(index) && !(attached && applied_reset_[lane][index])) {
                const auto* cpu = idle_[lane][index];
                if (!cpu || !cpu->waiting_for_interrupt()) state = 0;
                else if (enables & (UINT64_C(1) << (8 + index))) state = 4;
                else if (enables & (UINT64_C(1) << index)) state = 3;
                else state = 2;
            }
            value |= state << (4 + 4 * index);
        }
        return value;
    }
    void end_of_elaboration() override {
        root_ = resolve<QcomClockRoot>(root_path.get_value());
        branches_ = resolve<QcomClockBranchEnables>(branches_path.get_value());
        if (!cprh_path.get_value().empty()) cprh_ = resolve<PdpCprh>(cprh_path.get_value());
        if (initially_powered_mask.get_value() & ~3u)
            throw std::invalid_argument("CPU domain power mask must fit two lanes");
        if (boot_lane.get_value() >= 2 || boot_core.get_value() >= 8)
            throw std::invalid_argument("CPU domain boot CPU is outside the topology");
        for (unsigned lane = 0; lane < 2; ++lane) {
            const auto& pll_path = lane == 0 ? ncc_pll0_path.get_value() : ncc_pll1_path.get_value();
            if (!pll_path.empty()) ncc_plls_[lane] = resolve<QcomZondaPll>(pll_path);
            const auto& pdp_reset = lane ? pdp_reset1 : pdp_reset0;
            const auto& pdp_vector = lane ? pdp_vector1 : pdp_vector0;
            if (bool(pdp_reset.size()) != bool(pdp_vector.size()))
                throw std::invalid_argument("Bind the PDP reset and vector outputs together");
            auto& state = lanes_[lane];
            const unsigned mask = lane == 0 ? lane0_core_mask.get_value() : lane1_core_mask.get_value();
            const unsigned attached = attached_mask(lane);
            if (!mask || (mask & ~0xffu) || (attached & ~mask))
                throw std::invalid_argument("CPU domain attached masks must describe implemented cores");
            if (lane == boot_lane.get_value() && !(mask & (1u << boot_core.get_value())))
                throw std::invalid_argument("CPU domain boot CPU must be implemented");
            for (unsigned core = 0; core < 8; ++core) {
                const bool present = attached & (1u << core);
                if (resets_[lane][core]->size() != (present ? 1u : 0u) ||
                    vectors_[lane][core]->size() > (present ? 1u : 0u))
                    throw std::invalid_argument("CPU domain bindings must match attached core masks");
            }
            cores_[lane].core_mask = mask;
            cores_[lane].cluster_index = lane;
            state.powered = initially_powered_mask.get_value() & (1u << lane);
            // Cold lanes retain core reset ownership through domain initialization.
            cores_[lane].managed = !state.powered;
            if (state.powered) {
                state.isolation_a = 0;
                state.isolation_b = 0;
            }
        }
        if (boot_status_enable.get_value()) {
            if (!boot_status_socket.size())
                throw std::invalid_argument("The CPU domain boot status requires one memory socket");
            if (boot_status_base.get_value() & 0x3f)
                throw std::invalid_argument("The CPU domain boot status base must be 64-byte aligned");
        }
    }
    bool clock_available(unsigned lane) {
        return root_->output_hz() && branches_->branch_enabled(0) &&
            branches_->branch_enabled(1 + lane) && branches_->branch_enabled(3 + lane);
    }
    bool ncc_clock_available(unsigned lane) const {
        const auto& core = cores_[lane];
        const auto control = core.bridge;
        const auto* pll = ncc_plls_[lane];
        if (!(control & 0x2000000) && pll && pll->hardware_controlled())
            return core.effective_frequency_hz && pll->main_output_hz();
        // The AUX path supplies the clock before the first hardware request.
        if ((control & 0xc) != 0xc) return true;
        return pll && ((control & 0x20) ? pll->even_output_hz() : pll->main_output_hz());
    }
    bool tags_match(unsigned lane) const {
        const auto& state = lanes_[lane];
        if (state.tag_writes != 0x3f) return false;
        for (const auto tag : state.tags) if ((tag & 0xfff) != lane) return false;
        return true;
    }
    bool prepared(unsigned lane) {
        const auto& state = lanes_[lane];
        return init_writes_ == 0x1f && tags_match(lane) && state.supply_released &&
            state.reset_gate_released && !isolated(lane) && clock_available(lane);
    }
    bool must_reset(unsigned lane) {
        const auto& state = lanes_[lane];
        // Before CPUCP takes ownership, preserve the board's running boot domain.
        return !state.powered || isolated(lane) || (state.control & 8) ||
            state.reset_requested != state.reset_delivered ||
            (state.supply_released && (!state.reset_gate_released || !clock_available(lane)));
    }
    // A core in WFI with OK2ENTERC4 armed has lost power. See "Core C-state report"
    // in cpu_domain_core_evidence.md.
    bool collapsed(unsigned lane, unsigned core) const {
        const auto* cpu = idle_[lane][core];
        return cpu && !cores_[lane].core_reset(core) && cpu->waiting_for_interrupt() &&
            (cores_[lane].words[4] & (UINT64_C(1) << (8 + core)));
    }
    // The selected TZ writes the release pair while a core is held, then polls the
    // PDP boot status block until that core reports online. Treat the pair as a
    // functional start request: publish any explicit entry, release the core, and
    // record its online state. The physical block identity remains open.
    // A start request for a collapsed core restarts it from reset.
    void apply_release_request(unsigned lane, unsigned core) {
        auto& state = lanes_[lane];
        auto& model = cores_[lane];
        if (!model.cluster_initialized) return;
        release_pending_[lane][core] = false;
        if (!applied_reset_[lane][core]) {
            // A running core ignores the request. A collapsed core restarts from reset.
            if (!collapsed(lane, core)) return;
            std::cerr << "APSS_CORE_POWER_CYCLE lane=" << lane << " core=" << core << '\n';
            power_cycle_[lane][core] = true;
            release_pending_[lane][core] = true;
            return;
        }
        power_cycle_[lane][core] = false;
        const auto payload = model.release_words[core][1];
        if (payload != UINT64_MAX && payload != model.words[6]) {
            model.words[6] = payload;
            ++state.vector_generation;
        }
        model.words[3] |= UINT64_C(1) << core;
        model.words[2] &= ~(UINT64_C(1) << (16 + core));
        model.managed = true;
    }
    void status_write(std::uint64_t address, std::uint32_t value, unsigned length) {
        std::array<unsigned char, 4> bytes{};
        for (unsigned index = 0; index < length; ++index)
            bytes[index] = static_cast<unsigned char>(value >> (8 * index));
        tlm::tlm_generic_payload tx;
        tx.set_command(tlm::TLM_WRITE_COMMAND);
        tx.set_address(address);
        tx.set_data_ptr(bytes.data());
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        tx.set_byte_enable_ptr(nullptr);
        tx.set_dmi_allowed(false);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        boot_status_socket->b_transport(tx, delay);
        if (tx.is_response_error())
            std::cerr << "APSS_BOOT_STATUS_WRITE_FAILED address=0x" << std::hex << address
                      << " status=" << tx.get_response_string() << std::dec << '\n';
    }
    // PDP firmware 0x81321b80 initializes this block. QSEE 0x1573e630..0x1573e69c
    // polls the header word at +0x30 and bits 16..23 of the word at +0x0.
    void publish_boot_status(bool force_header) {
        if (!boot_status_enable.get_value() || !boot_status_socket.size()) return;
        const bool header = force_header || !boot_status_published_;
        constexpr std::uint64_t stride = 0x40000;
        for (unsigned lane = 0; lane < 2; ++lane) {
            unsigned online = 0;
            for (unsigned core = 0; core < 8; ++core) {
                if ((attached_mask(lane) & (1u << core)) && !cores_[lane].core_reset(core))
                    online |= 1u << core;
            }
            const auto address = boot_status_base.get_value() + lane * stride;
            if (header) {
                status_write(address + 0x30, UINT32_C(0xabcd9876), 4);
                status_write(address + 0x34, 2, 2);
            }
            const auto bitmap = static_cast<std::uint32_t>(online) << 16;
            if (header || bitmap != boot_status_online_[lane]) {
                status_write(address, bitmap, 4);
                boot_status_online_[lane] = bitmap;
            }
        }
        boot_status_published_ = true;
    }
    bool init_ack(unsigned lane) {
        const auto& state = lanes_[lane];
        return state.powered && state.reset_seen && state.domain_complete &&
            !must_reset(lane) && prepared(lane);
    }
    bool combined_reset(unsigned lane, unsigned core) {
        cores_[lane].refresh(init_ack(lane) && lanes_[lane].initialized, lane);
        const bool boot = lane == boot_lane.get_value() && core == boot_core.get_value();
        return must_reset(lane) || (!cores_[lane].managed && !boot) || power_cycle_[lane][core] ||
            (cores_[lane].managed && (cores_[lane].core_reset(core) || !ncc_clock_available(lane)));
    }
    bool pdp_reset_required(unsigned lane) {
        return must_reset(lane) || !cores_[lane].cluster_initialized ||
            !ncc_clock_available(lane) || !(cores_[lane].words[1] & 0x40);
    }
    static void fail(tlm::tlm_generic_payload& tx, tlm::tlm_response_status status) {
        // The platform AccessMonitor records the original address, PC, and response.
        tx.set_response_status(status);
    }
    void init_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset >= 0x14) { fail(tx, tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (tx.is_write()) {
            const auto value = clock_detail::load(tx);
            init_config_[offset / 4] = value;
            // Retained runtime configuration does not replace the ROM's five clears.
            if (!value) init_writes_ |= 1u << (offset / 4);
            reset_event_.notify(sc_core::SC_ZERO_TIME);
        } else clock_detail::store(tx, init_config_[offset / 4]);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void clock_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset == 0 && tx.is_read()) {
            std::uint32_t status = 0;
            for (unsigned lane = 0; lane < 2; ++lane) {
                if (requests_ & (1u << lane))
                    status |= (clock_available(lane) ? 1u : 2u) << (lane * 2);
            }
            clock_detail::store(tx, status);
        } else if (offset == 0x10) {
            if (tx.is_read()) clock_detail::store(tx, requests_);
            else {
                const auto value = clock_detail::load(tx);
                if (value & ~3u) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
                requests_ = value;
            }
        } else { fail(tx, tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void startup_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset != 0 && offset != 4 && offset != 0x100 && offset != 0x104) {
            fail(tx, tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& word = startup_configuration_[(offset >> 2) & 1][offset >= 0x100];
        if (tx.is_write()) {
            const auto value = clock_detail::load(tx);
            const auto mask = offset >= 0x100 ? 3u : 1u;
            if (value & ~mask) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
            word = value;
        } else clock_detail::store(tx, word);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void control_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset == 0x7a0 || offset == 0x424 || offset == 0x400) {
            auto& word = interrupt_configuration_[offset == 0x400 ? 2 : offset == 0x424];
            if (tx.is_write()) word = clock_detail::load(tx);
            else clock_detail::store(tx, word);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        // PDP 0x813210a4 returns bit 1 of this word to its handler 0x81307bf4 (request 0x12, type 2).
        // Both values complete the request. No firmware writes the word, so it reads zero.
        if (offset == 0x304) {
            if (!tx.is_read()) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
            clock_detail::store(tx, 0);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset >= 0x1100 && offset <= 0x11f0 && !(offset & 15)) {
            const unsigned status_lane = static_cast<unsigned>((offset - 0x1100) >> 7);
            const unsigned core = static_cast<unsigned>((offset - 0x1100) >> 4) & 7;
            if (!tx.is_read() || !(cores_[status_lane].core_mask & (1u << core))) {
                fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return;
            }
            cores_[status_lane].refresh(init_ack(status_lane) && lanes_[status_lane].initialized, status_lane);
            clock_detail::store(tx, (cores_[status_lane].powered_cores >> core) & 1);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        const unsigned lane = (offset >> 2) & 1;
        const auto reg = offset & ~std::uint64_t{4};
        auto& state = lanes_[lane];
        std::uint32_t value = tx.is_write() ? clock_detail::load(tx) : 0;
        if (reg == 0x4180 || reg == 0x80) {
            if (tx.is_write() && value) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
        } else if (reg == 0x40) {
            auto& word = startup_configuration_[lane][2];
            if (tx.is_write()) {
                if (value & ~1u) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
                word = value;
            } else value = word;
        } else if (reg == 0x40a0 || reg == 0x4a0) {
            if (tx.is_write()) {
                if (value) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
                if (reg == 0x40a0) state.supply_released = true;
                else state.reset_gate_released = true;
            }
        } else if (reg == 0x1020 || reg == 0x1000) {
            auto& word = reg == 0x1020 ? state.isolation_a : state.isolation_b;
            const auto mask = reg == 0x1020 ? 3u : 15u;
            if (tx.is_write()) {
                if (value & ~mask) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
                word = value;
            } else value = word;
        } else if (reg == 0x4000) {
            if (tx.is_write()) {
                if (value & ~1u) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
                cores_[lane].commit = value & 1;
            } else {
                // CPUCP 0x1d8099ba reads bit 0 to decide whether the lane is already up.
                // The cluster state controls the result. A held core does not clear it.
                value = cores_[lane].commit && cores_[lane].cluster_initialized &&
                    !must_reset(lane) && ncc_clock_available(lane);
            }
        } else if (reg == 0x4020) {
            if (tx.is_write()) {
                if (value & ~11u) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
                const bool reset = value & 8;
                state.control = value & 9;
                if (reset) {
                    ++state.reset_requested;
                    state.reset_seen = false;
                    state.domain_complete = false;
                    state.initialized = false;
                    state.downstream = 0;
                }
            } else value = state.control | (((state.control & 1) && init_ack(lane)) ? 2u : 0u);
        } else { fail(tx, tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (tx.is_read()) clock_detail::store(tx, value);
        if (tx.is_write()) reset_event_.notify(sc_core::SC_ZERO_TIME);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void tag_transport(unsigned lane, bool aux, tlm::tlm_generic_payload& tx) {
        const bool wide = tx.get_data_length() == 8;
        if (!(wide ? CpuDomainCoreState::valid64(tx) : clock_detail::valid(tx))) return;
        const auto offset = tx.get_address();
        unsigned index;
        if (aux && offset == 0x418) index = 4;
        else if (aux && offset == 0x18) index = 5;
        else if (!aux && offset == 0x630) index = 0;
        else if (!aux && offset == 0x30) index = 1;
        else if (!aux && offset == 0x38) index = 2;
        else if (!aux && offset == 0x40) index = 3;
        else { fail(tx, tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        auto& state = lanes_[lane];
        if (tx.is_write()) {
            const auto value = wide ? CpuDomainCoreState::load64(tx) : clock_detail::load(tx);
            if (value & ~UINT64_C(0xfffff)) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
            state.tags[index] = static_cast<std::uint32_t>(value);
            state.tag_writes |= 1u << index;
            reset_event_.notify(sc_core::SC_ZERO_TIME);
        } else if (wide) CpuDomainCoreState::store64(tx, state.tags[index]);
        else clock_detail::store(tx, state.tags[index]);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void downstream_transport(unsigned lane, tlm::tlm_generic_payload& tx) {
        const bool wide = tx.get_data_length() == 8;
        if (!(wide ? CpuDomainCoreState::valid64(tx) : clock_detail::valid(tx))) return;
        if (tx.get_address() != 8) { fail(tx, tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        auto& state = lanes_[lane];
        if (tx.is_write()) {
            const auto value = wide ? CpuDomainCoreState::load64(tx) : clock_detail::load(tx);
            if (value & ~UINT64_C(7)) { fail(tx, tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
            state.downstream = static_cast<std::uint32_t>(value) & 5;
            if (state.downstream == 4 && init_ack(lane)) state.initialized = true;
            reset_event_.notify(sc_core::SC_ZERO_TIME);
        } else {
            if (state.downstream == 4 && init_ack(lane)) state.initialized = true;
            const auto ack = state.initialized && init_ack(lane) && state.downstream == 4;
            const auto value = state.downstream | (ack ? 2u : 0u);
            if (wide) CpuDomainCoreState::store64(tx, value);
            else clock_detail::store(tx, value);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void tags0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { tag_transport(0, false, tx); }
    void tags1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { tag_transport(1, false, tx); }
    void aux0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { tag_transport(0, true, tx); }
    void aux1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { tag_transport(1, true, tx); }
    void downstream0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { downstream_transport(0, tx); }
    void downstream1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { downstream_transport(1, tx); }
    enum class CoreBank { route, runtime, window, power, bridge, sequence, fabric, core, release };
    void refresh_supply_ack(unsigned lane) {
        auto& state = lanes_[lane];
        cores_[lane].refresh(init_ack(lane) && state.initialized, lane);
        if (cores_[lane].supply_good) {
            state.supply_ack = true;
            return;
        }
        // Supply-off completion requires every attached CPU to stop execution.
        if (state.reset_requested != state.reset_delivered) return;
        for (unsigned core = 0; core < 8; ++core) {
            if ((attached_mask(lane) & (1u << core)) && !applied_reset_[lane][core]) return;
        }
        state.supply_ack = false;
    }
    bool apply_dvfs(unsigned lane) {
        auto& core = cores_[lane];
        const auto effects = core.requested_dvfs_effects();
        const auto target = core.requested_dvfs_state();
        const auto reject = [&](const char* cause, bool voltage = false) {
            core.finish_dvfs(false, voltage);
            std::cerr << "NCC_DVFS_ERROR model=" << name() << " lane=" << lane
                      << " state=" << target << " cause=" << cause << '\n';
            return false;
        };
        if (target >= core.dvfs_state_config.size()) return reject("no-active-vote");
        core.refresh(init_ack(lane) && lanes_[lane].initialized, lane);
        if (!core.supply_good) return reject("power-unavailable", true);
        auto* pll = ncc_plls_[lane];
        std::uint64_t frequency = 0;
        unsigned divider = 1;
        if (effects & 2) {
            if (!pll || (core.bridge & 0x2000000)) return reject("clock-owner-unavailable");
            const auto word = core.dvfs_state_config[target][1];
            const auto multiplier = word & 0x1ff;
            const auto fraction = (word >> 9) & 0xffff;
            const auto reference = pll->reference_hz.get_value();
            const auto scaled = (multiplier << 16) | fraction;
            if (!multiplier || !reference || reference > UINT64_MAX / scaled)
                return reject("invalid-frequency");
            frequency = reference * scaled / 65536;
            // CPUCP 0x1d80d0ac treats each nonzero selector as a divide-by-two output.
            divider = ((word >> 25) & 3) ? 2 : 1;
        }
        if ((effects & 4) && (!cprh_ || !cprh_->apply_power_state(lane, target)))
            return reject("invalid-power-profile", true);
        if (effects & 2) {
            pll->set_hardware_frequency(frequency, divider);
            core.effective_frequency_hz = frequency / divider;
        }
        core.finish_dvfs(true);
        return true;
    }
    void outer_transport(unsigned lane, CoreBank bank, tlm::tlm_generic_payload& tx) {
        auto& core = cores_[lane];
        const bool ready = init_ack(lane) && lanes_[lane].initialized;
        switch (bank) {
        case CoreBank::route: core.route_transport(tx); break;
        case CoreBank::runtime: core.runtime_transport(tx, 6, true); break;
        case CoreBank::window: core.window_transport(tx, lane ? 4 : 8); break;
        case CoreBank::power:
            // NCC_BOOT_STS1 reflects the current supply and core power state.
            core.refresh(ready, lane);
            refresh_supply_ack(lane);
            core.power_transport(tx, ready, lane, boot_status1(lane));
            break;
        case CoreBank::bridge: core.bridge_transport(tx); break;
        case CoreBank::sequence: core.sequence_transport(tx); break;
        case CoreBank::fabric: core.fabric_transport(tx); break;
        case CoreBank::core: core.core_transport(tx); break;
        case CoreBank::release: core.release_transport(tx); break;
        }
        if (tx.is_response_ok() && tx.is_write() && bank == CoreBank::sequence &&
            core.dvfs_request && !apply_dvfs(lane)) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (tx.is_response_ok() && tx.is_write() && bank == CoreBank::bridge &&
            (core.bridge & 0x2000000) && ncc_plls_[lane])
            ncc_plls_[lane]->clear_hardware_frequency();
        if (tx.is_response_ok() && bank == CoreBank::power) {
            refresh_supply_ack(lane);
        }
        if (tx.is_write() && tx.is_response_ok()) {
            if (bank == CoreBank::power && tx.get_address() == 0x328)
                ++lanes_[lane].vector_generation;
            // The release wrapper applies its own start request after this transport.
            if (bank != CoreBank::release) reset_event_.notify(sc_core::SC_ZERO_TIME);
        }
    }
    void route0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(0, CoreBank::route, tx); }
    void route1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(1, CoreBank::route, tx); }
    void runtime0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(0, CoreBank::runtime, tx); }
    void runtime1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(1, CoreBank::runtime, tx); }
    void window0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(0, CoreBank::window, tx); }
    void window1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(1, CoreBank::window, tx); }
    void power0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(0, CoreBank::power, tx); }
    void power1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(1, CoreBank::power, tx); }
    void bridge0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(0, CoreBank::bridge, tx); }
    void bridge1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(1, CoreBank::bridge, tx); }
    void sequence0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(0, CoreBank::sequence, tx); }
    void sequence1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(1, CoreBank::sequence, tx); }
    void fabric0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(0, CoreBank::fabric, tx); }
    void fabric1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(1, CoreBank::fabric, tx); }
    void core0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(0, CoreBank::core, tx); }
    void core1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) { outer_transport(1, CoreBank::core, tx); }
    void feature0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        cores_[0].feature_transport(tx, UINT32_C(0x000f0510));
    }
    void feature1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        cores_[1].feature_transport(tx, UINT32_C(0x000f0510));
    }
    void cluster0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        cores_[0].cluster_transport(tx, CpuDomainCoreState::ClusterBank::config);
    }
    void cluster1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        cores_[1].cluster_transport(tx, CpuDomainCoreState::ClusterBank::config);
    }
    void table0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        cores_[0].cluster_transport(tx, CpuDomainCoreState::ClusterBank::table);
    }
    void table1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        cores_[1].cluster_transport(tx, CpuDomainCoreState::ClusterBank::table);
    }
    void cluster_aux0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        cores_[0].cluster_transport(tx, CpuDomainCoreState::ClusterBank::aux);
    }
    void cluster_aux1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        cores_[1].cluster_transport(tx, CpuDomainCoreState::ClusterBank::aux);
    }
    void release0_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        release_transport(0, tx);
    }
    void release1_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        release_transport(1, tx);
    }
    void release_transport(unsigned lane, tlm::tlm_generic_payload& tx) {
        outer_transport(lane, CoreBank::release, tx);
        if (!tx.is_response_ok() || !tx.is_write()) return;
        const auto core = static_cast<unsigned>(tx.get_address() >> 20);
        if ((tx.get_address() & 0xfffff) || !(cores_[lane].core_mask & (1u << core))) return;
        // The command word arrives after the payload at +0x8.
        release_pending_[lane][core] = true;
        boot_status_force_ = true;
        reset_event_.notify(sc_core::SC_ZERO_TIME);
    }
    void reset_thread() {
        std::array<bool, 2> pdp_held{true, true};
        std::array<std::uint32_t, 2> pdp_vectors{};
        sc_core::sc_event_or_list wake;
        wake |= reset_event_;
        wake |= root_->state_changed;
        wake |= branches_->state_changed;
        for (const auto* pll : ncc_plls_) {
            if (pll) {
                wake |= pll->state_changed;
                wake |= pll->lock_changed;
            }
        }
        bool first = true;
        for (;;) {
            for (unsigned lane = 0; lane < 2; ++lane) {
                auto& state = lanes_[lane];
                if ((state.control & 1) && prepared(lane)) state.powered = true;
                const auto reset_requested = state.reset_requested;
                const bool domain_reset = must_reset(lane);
                if (domain_reset) {
                    state.domain_complete = false;
                    state.initialized = false;
                    state.downstream = 0;
                }
                const bool reset_pending = reset_requested != state.reset_delivered;
                bool delivered = true;
                for (unsigned core = 0; core < 8; ++core) {
                    if (!(attached_mask(lane) & (1u << core))) continue;
                    if (release_pending_[lane][core]) apply_release_request(lane, core);
                    auto& applied = applied_reset_[lane][core];
                    bool asserted = combined_reset(lane, core);
                    if (first || (!applied && asserted)) {
                        (*resets_[lane][core])->write(asserted);
                        applied = asserted;
                    }
                    // A vector publication cannot reset a running CPU. Sample it only
                    // while held, including publications made after the original assertion.
                    if (applied && vectors_[lane][core]->size()) {
                        while (sampled_vector_[lane][core] != state.vector_generation) {
                            const auto generation = state.vector_generation;
                            (*vectors_[lane][core])->write(cores_[lane].words[6]);
                            sampled_vector_[lane][core] = generation;
                        }
                    }
                    // Prepare while held. The callback can suspend, so check the
                    // vector generation and all reset gates again before release.
                    while (applied && !combined_reset(lane, core)) {
                        if (vectors_[lane][core]->size()) {
                            while (sampled_vector_[lane][core] != state.vector_generation) {
                                const auto generation = state.vector_generation;
                                (*vectors_[lane][core])->write(cores_[lane].words[6]);
                                sampled_vector_[lane][core] = generation;
                            }
                        }
                        const auto generation = state.vector_generation;
                        (*resets_[lane][core])->write(true);
                        if (generation != state.vector_generation) continue;
                        if (combined_reset(lane, core)) break;
                        // The native release callback must not suspend.
                        (*resets_[lane][core])->write(false);
                        applied = false;
                    }
                    if (!applied) delivered = false;
                }
                // Acknowledge only the captured request after every attached core
                // receives assertion. Never acknowledge a request made by a callback.
                if (domain_reset && delivered && reset_pending) {
                    state.reset_delivered = reset_requested;
                    state.reset_seen = state.reset_requested == reset_requested;
                }
                // Require both the sampled and current gates before completion.
                if (!domain_reset && !must_reset(lane) && (state.control & 1) &&
                    state.reset_seen && prepared(lane))
                    state.domain_complete = true;
                if (state.downstream == 4 && init_ack(lane)) state.initialized = true;
                refresh_supply_ack(lane);
                auto& pdp_reset = lane ? pdp_reset1 : pdp_reset0;
                auto& pdp_vector = lane ? pdp_vector1 : pdp_vector0;
                if (pdp_vector.size() && (first || pdp_vectors[lane] != cores_[lane].pdp_reset_vector)) {
                    pdp_vectors[lane] = cores_[lane].pdp_reset_vector;
                    pdp_vector->write(pdp_vectors[lane]);
                }
                if (pdp_reset.size()) {
                    // CPUCP publishes bit six after the authenticated image copy.
                    // The functional NCC sequencer releases the real PDP CPU.
                    if (first || (!pdp_held[lane] && pdp_reset_required(lane))) {
                        if (!first) {
                            const auto& s = lanes_[lane];
                            std::cerr << "PDP_RESET_ASSERTED lane=" << lane
                                      << " powered=" << s.powered << " isolated=" << isolated(lane)
                                      << " control=0x" << std::hex << s.control << std::dec
                                      << " reset_pending=" << (s.reset_requested != s.reset_delivered)
                                      << " supply_released=" << s.supply_released
                                      << " reset_gate=" << s.reset_gate_released
                                      << " clock=" << clock_available(lane)
                                      << " cluster_initialized=" << cores_[lane].cluster_initialized
                                      << " supply_good=" << cores_[lane].supply_good
                                      << " ncc_clock=" << ncc_clock_available(lane)
                                      << " run_request=" << ((cores_[lane].words[1] & 0x40) != 0) << '\n';
                        }
                        pdp_reset->write(true);
                        pdp_held[lane] = true;
                    }
                    if (pdp_held[lane] && !pdp_reset_required(lane)) {
                        pdp_reset->write(true);
                        if (!pdp_reset_required(lane) &&
                            pdp_vectors[lane] == cores_[lane].pdp_reset_vector) {
                            pdp_reset->write(false);
                            pdp_held[lane] = false;
                        }
                    }
                }
            }
            // The PDP header appears after the CPUCP initializes a lane. Refresh it and
            // the online bitmap whenever a release request arrives.
            if (boot_status_force_ || boot_status_published_ ||
                lanes_[0].initialized || lanes_[1].initialized)
                publish_boot_status(boot_status_force_);
            boot_status_force_ = false;
            first = false;
            // QEMU reset callbacks can suspend this thread. Recheck changes before waiting.
            bool pending = false;
            for (unsigned lane = 0; lane < 2; ++lane) {
                for (unsigned core = 0; core < 8; ++core) {
                    if (!(attached_mask(lane) & (1u << core))) continue;
                    pending |= combined_reset(lane, core) != applied_reset_[lane][core];
                    pending |= release_pending_[lane][core];
                    pending |= applied_reset_[lane][core] && vectors_[lane][core]->size() &&
                        sampled_vector_[lane][core] != lanes_[lane].vector_generation;
                }
                pending |= lanes_[lane].reset_requested != lanes_[lane].reset_delivered;
                pending |= (lanes_[lane].control & 1) && !lanes_[lane].powered && prepared(lane);
                const auto& pdp_reset = lane ? pdp_reset1 : pdp_reset0;
                if (pdp_reset.size())
                    pending |= pdp_reset_required(lane) != pdp_held[lane] ||
                        pdp_vectors[lane] != cores_[lane].pdp_reset_vector;
            }
            if (pending) continue;
            sc_core::wait(wake);
        }
    }
};

} // namespace qcom_vp

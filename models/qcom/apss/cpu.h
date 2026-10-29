// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <cortex-a710.h>
#include "execution_group.h"
#include "idle_state.h"
#include <atomic>

namespace qcom_vp {

// Architectural substitute, not an Oryon identity or performance model.
// Keep QBox's ARM interrupt, power, reset, and exclusive-access plumbing.
class ApssCpu : public cpu_arm_cortexA710, public CpuIdleState {
public:
    cci::cci_param<bool> initial_reset{
        "initial_reset", false, "Hold the CPU until its reset controller releases it"};
    cci::cci_param<unsigned> group_index{"group_index", 0, "Ordered native APSS CPU index"};
    TargetSignalSocket<uint64_t> reset_vector{"reset_vector"};
    ApssCpu(sc_core::sc_module_name name, sc_core::sc_object* object, sc_core::sc_object* group)
        : cpu_arm_cortexA710(name, object), group_(dynamic_cast<ApssExecutionGroup*>(group)) {
        if (!group_) throw std::invalid_argument("APSS CPU requires an execution group");
        group_->attach(*this, group_index.get_value());
        set_qom_type("max-arm-cpu");
        reset_vector.register_value_changed_cb([this](const uint64_t& vector) {
            if (m_resetting == none)
                throw std::logic_error("APSS reset-vector publication requires asserted reset");
            pending_vector_ = vector;
            vector_pending_ = true;
        });
        reset.register_value_changed_cb([this](const bool& asserted) {
            if (asserted) {
                const bool held = m_resetting != none;
                start_reset_assertion();
                wait_for_reset_assertion();
                // A repeated assertion prepares the vector before the controller's final gate check.
                if (held && vector_pending_) apply_reset_vector();
                return;
            }
            // Release must not suspend after the controller checks the gates.
            if (m_resetting == start_reset || vector_pending_ ||
                completed_pause_generation_.load(std::memory_order_acquire) != pause_generation_)
                throw std::logic_error("APSS reset release requires completed assertion and vector preparation");
            QemuCpu::reset_cb(false);
            if (initial_hold_) {
                m_inst.get().lock_iothread();
                initial_hold_ = false;
                m_started = true;
                m_cpu.set_soft_stopped(false);
                m_time_sync->on_arm_deadline();
                m_cpu.kick();
                m_inst.get().unlock_iothread();
            }
        });
    }
    ~ApssCpu() override { end_of_simulation(); }

    // QEMU sets the architectural halted flag at WFI and clears it at wake.
    bool waiting_for_interrupt() const override { return m_cpu.get_halted(); }

    void end_of_simulation() override {
        // A cold-held CPU has a native thread but no running execution clock.
        // Enable QBox's shutdown path without releasing architectural reset.
        if (initial_hold_) {
            m_started = true;
            initial_hold_ = false;
        }
        cpu_arm_cortexA710::end_of_simulation();
    }

    void before_end_of_elaboration() override {
        cpu_arm_cortexA710::before_end_of_elaboration();
        m_cpu.set_end_of_loop_callback([this] {
            // QEMU calls this with the BQL held, after queued native work.
            // A reset job publishes its generation only after reset(true) returns.
            const auto generation = native_reset_generation_.load(std::memory_order_acquire);
            if (generation != completed_pause_generation_.load(std::memory_order_relaxed) &&
                !m_cpu.can_run()) {
                completed_pause_generation_.store(generation, std::memory_order_release);
                m_start_reset_done_ev.async_notify();
            }
            QemuCpu::end_of_loop_cb();
        });
        qemu::CpuAarch64 cpu(m_cpu);
        cpu.set_prop_bool("x-qcom-mpam", true);
        // QHEE 0x48314..0x48428 reads AMCFGR_EL0, AMCGCR_EL0, ID_AA64MMFR2_EL1,
        // ID_AA64DFR0_EL1, and TRCIDR2/4/5 before it enables any interrupt. The
        // kernel __cpu_setup also writes AMUSERENR_EL0. The profile reports
        // identification words, retains TRFCR_EL2 writes, and implements the
        // AMUv1 activity monitors. It adds no trace unit. See models/qcom/apss/evidence.md.
        cpu.set_prop_bool("x-qcom-ids", true);
        // QHEE switches over ICH_LR0..LR7 at image offsets 0x91fa4..0x92054
        // and never reads ICH_VTR_EL2, so it uses eight list registers. QEMU
        // otherwise defines four. This model raises no virtual interrupt.
        cpu.set_prop_int("x-qcom-gic-num-lrs", 8);
        // QHEE saves PMEVCNTR0..11_EL0 and PMEVTYPER0..11_EL0 at image offsets
        // 0x6a900..0x6a9cc and restores them at 0x6a7c0..0x6a8f0. It never reads
        // PMCR_EL0.N, so it needs twelve event counters. QEMU otherwise defines six.
        cpu.set_prop_int("x-qcom-pmu-num-counters", 12);
        // Declare EL1 SIMD access at reset. Guest writes still control all later access.
        cpu.set_prop_int("reset-fpen", 1);
        if (p_cntfrq_hz.is_default_value()) {
            cpu.set_prop_int("cntfrq", 19200000);
        }
        if (group_) {
            static constexpr unsigned affinities[] = {0x00000, 0x00100, 0x00200, 0x00300,
                                                     0x00400, 0x00500, 0x10000, 0x10100};
            if (p_start_powered_off.get_value() || p_psci_conduit.get_value() != "disabled" ||
                !p_has_el3.get_value() || p_mp_affinity.get_value() != affinities[group_index.get_value()])
                throw std::invalid_argument("Invalid native APSS execution-group topology or PSCI configuration");
        }
        if (initial_reset.get_value() &&
            (!m_inst.is_tcg_enabled() || m_coroutines ||
             m_inst.get_tcg_mode() != QemuInstance::TCG_MULTI))
            throw std::invalid_argument("Initial APSS reset requires multithreaded TCG");
    }

    void end_of_elaboration() override {
        if (group_) group_->realize();
        cpu_arm_cortexA710::end_of_elaboration();
    }

    void start_of_simulation() override {
        if (!initial_reset.get_value()) {
            cpu_arm_cortexA710::start_of_simulation();
            return;
        }
        // Keep QBox's startup hold until the hardware reset callback completes.
        // A debugger resume must not release this hold.
        m_quantum_ns = static_cast<std::int64_t>(
            tlm_utils::tlm_quantumkeeper::get_global_quantum().to_seconds() * 1e9);
        QemuDevice::start_of_simulation();
        initial_hold_ = true;
        m_cpu.set_vcpu_dirty(true);
        queue_reset_assertion();
        m_inst.get().lock_iothread();
        m_cpu.kick();
        m_inst.get().unlock_iothread();
    }

private:
    bool initial_hold_ = false;
    ApssExecutionGroup* group_ = nullptr;
    std::uint64_t pending_vector_ = 0;
    bool vector_pending_ = false;
    std::uint64_t pause_generation_ = 0;
    std::atomic<std::uint64_t> native_reset_generation_{0};
    std::atomic<std::uint64_t> completed_pause_generation_{0};
    void queue_reset_assertion(bool publish_vector = false, std::uint64_t vector = 0) {
        m_resetting = start_reset;
        const auto generation = ++pause_generation_;
        m_cpu.async_safe_run(make_tracked_async_job([this, generation, publish_vector, vector] {
            if (publish_vector) m_dev.set_prop_uint("rvbar", vector);
            m_cpu.reset(true);
            native_reset_generation_.store(generation, std::memory_order_release);
        }));
    }
    void start_reset_assertion() {
        if (m_finished) return;
        if (m_resetting == none) queue_reset_assertion();
        m_time_sync->on_kick_notify();
    }
    void wait_for_reset_assertion() {
        while (completed_pause_generation_.load(std::memory_order_acquire) != pause_generation_) {
            kick_reset_job();
            sc_core::wait(m_start_reset_done_ev);
        }
        // Keep QBox's reset state on the SystemC thread.
        if (m_resetting == start_reset) m_resetting = hold_reset;
    }
    // Prepare only for an eligible release. Publications do not restart held CPUs.
    void apply_reset_vector() {
        const auto vector = pending_vector_;
        vector_pending_ = false;
        queue_reset_assertion(true, vector);
        wait_for_reset_assertion();
    }
    void kick_reset_job() {
        m_inst.get().lock_iothread();
        m_cpu.kick();
        m_inst.get().unlock_iothread();
    }
};

} // namespace qcom_vp

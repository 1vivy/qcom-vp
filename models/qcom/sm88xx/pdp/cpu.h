// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <cortex-m7.h>
#include "qcom/apss/execution_group.h"
#include <ports/target-signal-socket.h>
#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// Execute the loaded PDP image with the M7 architectural model.
// The reset controller owns release. The image supplies both SP and PC.
class PdpCpu : public cpu_arm_cortexM7, public NativeCpuContainer {
public:
    cci::cci_param<std::uint64_t> reference_clock_hz{
        "reference_clock_hz", 19200000, "SysTick reference clock frequency"};
    TargetSignalSocket<std::uint32_t> reset_vector{"reset_vector"};
    PdpCpu(sc_core::sc_module_name name, sc_core::sc_object* instance)
        : cpu_arm_cortexM7(name, instance) {
        reset_vector.register_value_changed_cb([this](const std::uint32_t& vector) {
            pending_vector_ = vector;
        });
        reset.register_value_changed_cb([this](const bool& asserted) {
            if (m_finished) return;
            if (asserted) {
                wait_for_assertion();
                // Refresh the guest vectors while the controller still holds reset.
                queue_assertion();
                m_time_sync->on_kick_notify();
                wait_for_assertion();
                return;
            }
            // The controller must prepare reset, then recheck its release gates.
            // This callback must not suspend after that final gate check.
            if (m_resetting == none) return;
            if (m_resetting != hold_reset ||
                completed_generation_.load(std::memory_order_acquire) != generation_)
                throw std::logic_error("PDP release requires completed reset preparation");
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
    ~PdpCpu() override { end_of_simulation(); }
    qemu::Cpu native_cpu() override { return m_cpu; }

    void before_end_of_elaboration() override {
        cpu_arm_cortexM7::before_end_of_elaboration();
        pending_vector_ = static_cast<std::uint32_t>(p_init_nsvtor.get_value());
        queued_vector_ = pending_vector_;
        if (!m_inst.is_tcg_enabled() || m_coroutines ||
            m_inst.get_tcg_mode() != QemuInstance::TCG_MULTI ||
            p_start_powered_off.get_value())
            throw std::invalid_argument("PDP reset requires multithreaded TCG without power control");
        auto container = get_qemu_dev();
        reference_clock_ = m_inst.get().clock_new(container.get_qemu_obj(), "PDP_REFCLK");
        m_inst.get().clock_set_hz(reference_clock_, reference_clock_hz.get_value());
        m_inst.get().qdev_connect_clock_in(container.get_qemu_obj(), "refclk", reference_clock_);
    }

    void start_of_simulation() override {
        m_quantum_ns = static_cast<std::int64_t>(
            tlm_utils::tlm_quantumkeeper::get_global_quantum().to_seconds() * 1e9);
        QemuDevice::start_of_simulation();
        initial_hold_ = true;
        m_cpu.set_vcpu_dirty(true);
        queue_assertion();
        kick();
    }

    void end_of_simulation() override {
        if (initial_hold_) {
            m_started = true;
            initial_hold_ = false;
        }
        cpu_arm_cortexM7::end_of_simulation();
    }

private:
    qemu::Clock reference_clock_;
    bool initial_hold_ = false;
    std::uint64_t generation_ = 0;
    std::atomic<std::uint64_t> completed_generation_{0};
    std::uint32_t pending_vector_ = 0;
    std::uint32_t queued_vector_ = 0;

    void queue_assertion() {
        m_resetting = start_reset;
        const auto generation = ++generation_;
        const auto vector = pending_vector_;
        const bool publish_vector = vector != queued_vector_;
        queued_vector_ = vector;
        m_cpu.async_safe_run(make_tracked_async_job([this, generation, vector, publish_vector] {
            if (publish_vector) m_cpu.set_prop_uint("init-nsvtor", vector);
            m_cpu.reset(true);
            m_cpu.invalidate_code(0, std::uint64_t{1} << 32);
            // ARM reset queues its vector fetch. This FIFO barrier follows it.
            // A stopped M-profile CPU need not call another end-of-loop callback.
            m_cpu.async_safe_run(make_tracked_async_job([this, generation] {
                completed_generation_.store(generation, std::memory_order_release);
                m_start_reset_done_ev.async_notify();
            }));
        }));
    }
    void wait_for_assertion() {
        while (completed_generation_.load(std::memory_order_acquire) != generation_) {
            kick();
            sc_core::wait(m_start_reset_done_ev);
        }
        if (m_resetting == start_reset) m_resetting = hold_reset;
    }
    void kick() {
        m_inst.get().lock_iothread();
        m_cpu.kick();
        m_inst.get().unlock_iothread();
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <riscv32.h>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace qcom_vp {

// Execute the guest-loaded DCP image only after its reset controller releases it.
class DcpCpu : public cpu_riscv32 {
public:
    cci::cci_param<bool> cache_operations{"cache_operations", true,
        "Enable the explicit coherent SiFive cache-operation policy"};
    // Patch 0016 pulses this output each time the hart executes WFI.
    QemuInitiatorSignalSocket wfi{"wfi"};
    DcpCpu(sc_core::sc_module_name name, sc_core::sc_object* object)
        : cpu_riscv32(name, object, 0) {
        // Start from the bare RV32I model, not QEMU's expanding default ISA.
        set_qom_type("rv32i-riscv-cpu");
        // QEMU assigns architecture ID 42 to this model. It is not an E66 identity.
        if (p_marchid.is_default_value()) p_marchid.set_value(42);
        reset.register_value_changed_cb([this](const bool& asserted) {
            if (m_finished) return;
            if (asserted) {
                if (m_resetting == none) queue_reset_assertion();
                m_time_sync->on_kick_notify();
                wait_for_reset_assertion();
                return;
            }
            if (m_resetting == start_reset ||
                completed_pause_generation_.load(std::memory_order_acquire) != pause_generation_)
                throw std::logic_error("DCP reset release requires completed assertion");
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
    ~DcpCpu() override { end_of_simulation(); }

    // Call from the reset controller's SC_THREAD, with architectural reset held.
    void prepare_reset_vector(std::uint32_t vector) {
        if (m_resetting != hold_reset ||
            completed_pause_generation_.load(std::memory_order_acquire) != pause_generation_)
            throw std::logic_error("DCP vector publication requires completed reset assertion");
        // The property setter dispatches to the QEMU I/O thread under its lock.
        // Reset consumes this property. No current-PC write or parking ROM is used.
        m_dev.set_prop_uint("resetvec", vector);
        queue_reset_assertion();
        wait_for_reset_assertion();
    }

    void before_end_of_elaboration() override {
        cpu_riscv32::before_end_of_elaboration();
        if (!m_inst.is_tcg_enabled() || m_coroutines ||
            m_inst.get_tcg_mode() != QemuInstance::TCG_MULTI)
            throw std::invalid_argument("DCP cold reset requires multithreaded TCG without coroutines");
        // The selected firmware requires RV32IMAC, Zicsr, and Zifencei.
        for (const auto* extension : {"i", "m", "a", "c", "zicsr", "zifencei", "zicntr"})
            m_dev.set_prop_bool(extension, true);
        m_dev.set_prop_bool("x-sifive-cache-operations", cache_operations.get_value());
        m_dev.set_prop_bool("x-qcom-wfi-output", true);
        m_cpu.set_end_of_loop_callback([this] {
            // QEMU holds the BQL here, after queued native reset work completes.
            const auto generation = native_reset_generation_.load(std::memory_order_acquire);
            if (generation != completed_pause_generation_.load(std::memory_order_relaxed) &&
                !m_cpu.can_run()) {
                completed_pause_generation_.store(generation, std::memory_order_release);
                m_start_reset_done_ev.async_notify();
            }
            QemuCpu::end_of_loop_cb();
        });
    }

    void end_of_elaboration() override {
        cpu_riscv32::end_of_elaboration();
        wfi.init_named(m_dev, "qcom.wfi", 0);
    }

    void start_of_simulation() override {
        // Keep QBox's startup hold without starting an execution clock.
        m_quantum_ns = static_cast<std::int64_t>(
            tlm_utils::tlm_quantumkeeper::get_global_quantum().to_seconds() * 1e9);
        QemuDevice::start_of_simulation();
        initial_hold_ = true;
        m_cpu.set_vcpu_dirty(true);
        queue_reset_assertion();
        kick_reset_job();
    }

    void end_of_simulation() override {
        // Enable QBox shutdown while retaining architectural reset on a cold CPU.
        if (initial_hold_) {
            m_started = true;
            initial_hold_ = false;
        }
        cpu_riscv32::end_of_simulation();
    }

private:
    bool initial_hold_ = false;
    std::uint64_t pause_generation_ = 0;
    std::atomic<std::uint64_t> native_reset_generation_{0};
    std::atomic<std::uint64_t> completed_pause_generation_{0};

    void queue_reset_assertion() {
        m_resetting = start_reset;
        const auto generation = ++pause_generation_;
        m_cpu.async_safe_run(make_tracked_async_job([this, generation] {
            m_cpu.reset(true);
            // APSS can publish a new image through its own DMI view while reset is held.
            // That write cannot invalidate this instance's translations, so discard them here.
            m_cpu.invalidate_code(0, std::uint64_t{1} << 32);
            native_reset_generation_.store(generation, std::memory_order_release);
        }));
    }
    void wait_for_reset_assertion() {
        while (completed_pause_generation_.load(std::memory_order_acquire) != pause_generation_) {
            kick_reset_job();
            sc_core::wait(m_start_reset_done_ev);
        }
        if (m_resetting == start_reset) m_resetting = hold_reset;
    }
    void kick_reset_job() {
        m_inst.get().lock_iothread();
        m_cpu.kick();
        m_inst.get().unlock_iothread();
    }
};

} // namespace qcom_vp

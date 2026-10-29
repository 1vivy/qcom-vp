// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <riscv32.h>
#include <cci_configuration>
#include <stdexcept>

namespace qcom_vp {

// Keep QBox unchanged. The pinned libqemu patch owns the mode-2 trap path.
class TmeClicCpu : public QemuCpuRiscv32 {
public:
    sc_core::sc_vector<QemuTargetSignalSocket> clic_irq{"clic_irq", 65};
    cci::cci_param<bool> retained_power{"retained_power", false,
        "Enable the explicit virtual retained-power WFI lifecycle"};

    void publish_power_vector(uint32_t vector) {
        if (!retained_power.get_value())
            throw std::logic_error("TME retained-power lifecycle is not enabled");
        get_qemu_dev().set_prop_uint("x-qcom-power-vector", vector);
    }
    void arm_retained_power(bool armed) {
        if (!retained_power.get_value())
            throw std::logic_error("TME retained-power lifecycle is not enabled");
        get_qemu_dev().set_prop_bool("x-qcom-power-armed", armed);
    }

    TmeClicCpu(sc_core::sc_module_name name, sc_core::sc_object* object, uint64_t hartid)
        : QemuCpuRiscv32(name, *dynamic_cast<QemuInstance*>(object), "rv32", hartid) {
        for (auto& irq : clic_irq) m_external_ev |= irq->default_event();
    }

    void before_end_of_elaboration() override {
        QemuCpuRiscv32::before_end_of_elaboration();
        qemu::CpuRiscv32 cpu(get_qemu_dev());
        cpu.set_prop_bool("x-qcom-clic", true);
        cpu.set_prop_bool("x-qcom-retained-power", retained_power.get_value());
    }

    void end_of_elaboration() override {
        QemuCpuRiscv32::end_of_elaboration();
        for (unsigned i = 0; i < clic_irq.size(); ++i)
            clic_irq[i].init_named(m_dev, "qcom.clic", i);
    }
};

} // namespace qcom_vp

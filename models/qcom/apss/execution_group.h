// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <cpu.h>
#include <tlm-extensions/qemu-cpu-hint.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>
#include <utility>

namespace qcom_vp {

// An M-profile QEMU device is an armv7m container, not the CPU object.
// Such a CPU model exposes its architectural CPU for code and hint views.
class NativeCpuContainer {
public:
    virtual qemu::Cpu native_cpu() = 0;

protected:
    ~NativeCpuContainer() = default;
};

inline qemu::Cpu native_cpu(QemuCpu& cpu) {
    if (auto* container = dynamic_cast<NativeCpuContainer*>(&cpu)) return container->native_cpu();
    return qemu::Cpu(cpu.get_qemu_dev());
}

// One native instance supplies RAM identity, CPU indices, and exclusive accesses.
class ApssExecutionGroup : public sc_core::sc_module {
public:
    static constexpr unsigned maximum_cpu_count = 8;
    cci::cci_param<unsigned> cpu_count{"cpu_count", maximum_cpu_count, "Ordered APSS CPU count"};
    explicit ApssExecutionGroup(sc_core::sc_module_name name, sc_core::sc_object* object)
        : sc_module(name), instance_(dynamic_cast<QemuInstance*>(object)), cpus_(cpu_count.get_value(), nullptr) {
        if (!instance_) throw std::invalid_argument("APSS execution group requires a QemuInstance");
        if (cpus_.empty() || cpus_.size() > maximum_cpu_count)
            throw std::invalid_argument("Invalid APSS execution-group CPU count");
    }

    void attach(QemuCpu& cpu, unsigned index) {
        if (index >= cpus_.size() || cpus_[index] || &cpu.get_qemu_inst() != instance_)
            throw std::invalid_argument("Invalid APSS execution-group member");
        cpus_[index] = &cpu;
    }

    void realize() {
        if (realized_) return;
        for (auto* cpu : cpus_)
            if (!cpu) throw std::invalid_argument("APSS execution group has missing CPUs");
        for (unsigned i = 0; i < cpus_.size(); ++i) {
            cpus_[i]->realize();
            if (qemu::Cpu(cpus_[i]->get_qemu_dev()).get_index() != i)
                throw std::invalid_argument("Unexpected native APSS CPU index");
        }
        realized_ = true;
    }

    const std::vector<QemuCpu*>& cpus() const { return cpus_; }
    QemuInstance& instance() const { return *instance_; }

private:
    QemuInstance* instance_;
    std::vector<QemuCpu*> cpus_;
    bool realized_ = false;
    void end_of_elaboration() override { realize(); }
};

// Executable-memory consumers can select a group or an independent native CPU.
// This does not alter the initiating CPU hint or the transaction attributes.
class NativeExecutionViews {
public:
    cci::cci_param<std::string> execution_participant{
        "execution_participant", "", "Space-separated independent native CPU paths for executable memory"};
    cci::cci_param<std::string> execution_sibling{
        "execution_sibling", "", "Additional CPU path in the owner's native instance"};
    explicit NativeExecutionViews(sc_core::sc_object* object)
        : group_(dynamic_cast<ApssExecutionGroup*>(object)), cpu_(dynamic_cast<QemuCpu*>(object)) {
        if (!group_ && !cpu_) throw std::invalid_argument("Executable memory requires native execution views");
    }

    void start() {
        views_.reserve(group_ ? group_->cpus().size() : 1);
        if (group_) {
            for (auto* cpu : group_->cpus()) views_.emplace_back(native_cpu(*cpu));
        } else {
            views_.emplace_back(native_cpu(*cpu_));
        }
        const auto& sibling_path = execution_sibling.get_value();
        if (!sibling_path.empty()) {
            auto* object = sc_core::sc_find_object(sibling_path.c_str());
            auto* owner = group_ ? static_cast<sc_core::sc_object*>(group_) : cpu_;
            if (!object && owner->get_parent_object()) {
                const auto relative = std::string(owner->get_parent_object()->name()) + "." + sibling_path;
                object = sc_core::sc_find_object(relative.c_str());
            }
            auto* sibling = dynamic_cast<QemuCpu*>(object);
            if (!sibling || &sibling->get_qemu_inst() !=
                    &(group_ ? group_->instance() : cpu_->get_qemu_inst()))
                throw std::invalid_argument("Executable memory requires a sibling in the same native instance");
            views_.emplace_back(native_cpu(*sibling));
        }
        std::istringstream paths(execution_participant.get_value());
        for (std::string path; paths >> path;) {
            auto* object = sc_core::sc_find_object(path.c_str());
            auto* owner = group_ ? static_cast<sc_core::sc_object*>(group_) : cpu_;
            if (!object && owner->get_parent_object()) {
                const auto relative = std::string(owner->get_parent_object()->name()) + "." + path;
                object = sc_core::sc_find_object(relative.c_str());
            }
            auto* participant = dynamic_cast<QemuCpu*>(object);
            if (!participant ||
                &participant->get_qemu_inst() == &(group_ ? group_->instance() : cpu_->get_qemu_inst()))
                throw std::invalid_argument("Executable memory requires an independent native participant");
            for (const auto& existing : participants_)
                if (&existing.cpu->get_qemu_inst() == &participant->get_qemu_inst())
                    throw std::invalid_argument("Executable memory participants require distinct native instances");
            participants_.push_back({participant, {}});
            views_.emplace_back(native_cpu(*participant));
        }
    }

    bool contains(const tlm::tlm_generic_payload& tx) const {
        const auto* hint = tx.get_extension<QemuCpuHintTlmExtension>();
        if (!hint) return false;
        for (const auto& cpu : views_)
            if (hint->get_cpu().get_qemu_obj() == cpu.get_qemu_obj()) return true;
        return false;
    }
    // Each instance has a distinct RAM identity for the same host bytes.
    // Use each native RAM write path to invalidate its translated code.
    qemu::MemoryRegionOps::MemTxResult write(
        const tlm::tlm_dmi& dmi, uint64_t address, const unsigned char* data,
        unsigned length, qemu::MemoryRegionOps::MemTxAttrs attrs) {
        auto result = write_instance(group_ ? group_->instance() : cpu_->get_qemu_inst(),
                                     writes_, dmi, address, data, length, attrs);
        for (auto& participant : participants_) {
            if (result != qemu::MemoryRegionOps::MemTxOK) break;
            result = write_instance(participant.cpu->get_qemu_inst(), participant.writes,
                                    dmi, address, data, length, attrs);
        }
        return result;
    }

private:
    struct WriteView {
        QemuInstanceDmiManager::DmiRegionAlias::Ptr alias;
        std::shared_ptr<qemu::AddressSpace> space;
    };
    using Writes = std::map<uintptr_t, WriteView>;

    qemu::MemoryRegionOps::MemTxResult write_instance(
        QemuInstance& owner, Writes& writes, const tlm::tlm_dmi& dmi,
        uint64_t address, const unsigned char* data, unsigned length,
        qemu::MemoryRegionOps::MemTxAttrs attrs) {
        auto& instance = owner.get();
        const bool locked = instance.is_iothread_locked();
        if (!locked) instance.lock_iothread();
        struct Unlock {
            qemu::LibQemu& instance;
            bool locked;
            ~Unlock() { if (!locked) instance.unlock_iothread(); }
        } unlock{instance, locked};
        const auto key = reinterpret_cast<uintptr_t>(dmi.get_dmi_ptr());
        auto it = writes.find(key);
        if (it == writes.end()) {
            WriteView view;
            view.alias = owner.get_dmi_manager().get_new_region_alias(dmi);
            view.space = instance.address_space_new();
            view.space->init(view.alias->get_alias_mr(), "external-executable-write");
            it = writes.emplace(key, std::move(view)).first;
        } else {
            // A permission-limited CPU DMI grant can replace this backing region
            // with a shorter region at the same host pointer. Restore its extent.
            // The CPU aliases retain their own permission-limited bounds.
            owner.get_dmi_manager().get_region(dmi);
        }
        return it->second.space->write(address - dmi.get_start_address(), data, length, attrs);
    }

public:
    bool has_participant() const { return !participants_.empty(); }

    void finish_writes() {
        finish_instance(group_ ? group_->instance() : cpu_->get_qemu_inst(), writes_);
        for (auto& participant : participants_)
            finish_instance(participant.cpu->get_qemu_inst(), participant.writes);
    }


    // Each address space can contain mappings that no other CPU established.
    // The final safe job revokes upstream DMI only after all views lose code.
    void invalidate(uint64_t address, uint64_t length, std::function<void()> complete) {
        auto operation = std::make_shared<Invalidation>(views_.size(), std::move(complete));
        for (auto cpu : views_) {
            auto& instance = cpu.get_inst();
            const bool locked = instance.is_iothread_locked();
            if (!locked) instance.lock_iothread();
            cpu.async_safe_run([cpu, address, length, operation]() mutable {
                cpu.invalidate_code(address, length);
                if (operation->remaining.fetch_sub(1) == 1) operation->complete();
            });
            cpu.kick();
            if (!locked) instance.unlock_iothread();
        }
    }

private:
    struct Invalidation {
        std::atomic<unsigned> remaining;
        std::function<void()> complete;
        Invalidation(unsigned count, std::function<void()> callback)
            : remaining(count), complete(std::move(callback)) {}
    };
    void finish_instance(QemuInstance& owner, Writes& writes) {
        if (writes.empty()) return;
        auto& instance = owner.get();
        const bool locked = instance.is_iothread_locked();
        if (!locked) instance.lock_iothread();
        writes.clear();
        if (!locked) instance.unlock_iothread();
    }
    ApssExecutionGroup* group_;
    QemuCpu* cpu_;
    std::vector<qemu::Cpu> views_;
    Writes writes_;
    struct Participant {
        QemuCpu* cpu;
        Writes writes;
    };
    std::vector<Participant> participants_;
};

} // namespace qcom_vp

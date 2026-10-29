// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <arm_gicv3_its.h>
#include <cstdint>
#include <memory>
#include <string>

namespace qcom_vp {

// QHEE requires GICv4.1 capabilities that QEMU does not advertise.
// It reads GITS_TYPER (64-bit, frame offset 8) and checks these fields:
//   bit 0  Physical
//   bit 1  Virtual
//   bit 39 VSGI  (direct virtual SGI injection)
//   bit 40 VMAPP (GICv4.1 VMAPP command layout)
//   bit 41 SVPET (shared vPE table, low bit of the two-bit field at 42:41)
// QHEE panics at hyp/platform/gicv3/src/gicv3_its.c:648 when bit 39 or bit 40 is zero.
// ITS initialization rejects SVPET zero with "No ITS had a shared vPE table".
// SVPET one compares only affinity level three, which matches the zero GITS_MPIDR
// and every APSS affinity in this platform.
// The native ITS implements the VMAPP command with the GICv4.0 layout and no
// direct virtual SGI injection. This window publishes the capability bits and
// reports the GIC-700 product identity and forwards other accesses to the native device.
// docs/qcom/sm88xx/stubs.md records the substitute.
//
// The same window supplies the GITS_CTLR Quiescent transition.
// Arm defines Quiescent as read-only: it reads zero while Enabled is one and
// reads one while the ITS is disabled and idle.
// QEMU sets Quiescent only in its reset callback and its CTLR write changes
// Enabled alone, so the native register keeps bit 31 set.
// QHEE enables the ITS with 0x80000001 and waits for bit 31 to clear.
// Without this transition, QHEE polls forever after it writes Enabled.
// The window ignores written bit 31 and derives the read value from Enabled.
class ApssIts : public arm_gicv3_its {
public:
    ApssIts(sc_core::sc_module_name name, sc_core::sc_object* object, sc_core::sc_object* gic)
        : arm_gicv3_its(name, object, gic) {
    }

    void end_of_elaboration() override {
        arm_gicv3_its::end_of_elaboration();
        publish_virtual_capabilities();
    }

private:
    static constexpr std::uint64_t control_size = 0x10000;
    static constexpr std::uint64_t translation_size = 0x10000;
    static constexpr std::uint64_t window_size = control_size + translation_size;
    // GITS_CTLR occupies bytes 0 through 3. Enabled is bit 0 and Quiescent is bit 31.
    static constexpr std::uint64_t ctlr_first = 0;
    static constexpr unsigned ctlr_enabled_bit = 0;
    static constexpr unsigned ctlr_quiescent_bit = 31;
    // GITS_TYPER starts at byte 8. VSGI is bit 39 and VMAPP is bit 40.
    static constexpr std::uint64_t typer_first = 8;
    static constexpr unsigned vsgi_byte = 4;
    static constexpr unsigned vmapp_byte = 5;

    qemu::MemoryRegion native_;
    std::shared_ptr<qemu::AddressSpace> native_as_;
    qemu::MemoryRegion window_;
    qemu::MemoryRegionOpsPtr ops_;

    void publish_virtual_capabilities() {
        auto& instance = get_qemu_inst().get();
        native_ = qemu::SysBusDevice(get_qemu_dev()).mmio_get_region(0);
        native_as_ = instance.address_space_new();
        native_as_->init(native_, "apss-its-native");
        const std::string stem = std::string(name()) + ".capabilities";

        ops_ = instance.memory_region_ops_new();
        ops_->set_max_access_size(8);
        ops_->set_read_callback(
            [this](std::uint64_t offset, std::uint64_t* data, unsigned size,
                   qemu::MemoryRegionOps::MemTxAttrs attrs) {
                const auto result = native_as_->read(offset, data, size, attrs);
                if (result != qemu::MemoryRegionOps::MemTxOK)
                    return result;
                if (offset == ctlr_first && size >= 4) {
                    const bool enabled = (*data >> ctlr_enabled_bit) & 1;
                    const auto quiescent = std::uint64_t{1} << ctlr_quiescent_bit;
                    *data = enabled ? (*data & ~quiescent) : (*data | quiescent);
                }
                // GITS_IIDR is at offset 4. Its fourth byte holds ProductID.
                // The GIC-700 product identity is 4 (TF-A GIC_PRODUCT_ID_GIC700).
                constexpr std::uint64_t iidr_product_byte = 4 + 3;
                constexpr std::uint64_t gic700_product = 4;
                for (unsigned k = 0; k != size; ++k) {
                    if (offset + k != iidr_product_byte)
                        continue;
                    *data &= ~(std::uint64_t{0xff} << (8 * k));
                    *data |= gic700_product << (8 * k);
                }
                const auto typer_last = typer_first + 7;
                if (offset > typer_last || offset + size <= typer_first)
                    return result;
                // Byte k of the returned value is the byte at offset + k.
                for (unsigned k = 0; k != size; ++k) {
                    const auto byte = offset + k;
                    if (byte == typer_first + vsgi_byte)
                        *data |= std::uint64_t{1} << (8 * k + 7);
                    // VMAPP is bit 40 and SVPET[0] is bit 41.
                    if (byte == typer_first + vmapp_byte)
                        *data |= std::uint64_t{3} << (8 * k);
                }
                return result;
            });
        ops_->set_write_callback(
            [this](std::uint64_t offset, std::uint64_t data, unsigned size,
                   qemu::MemoryRegionOps::MemTxAttrs attrs) {
                // Quiescent is read-only. Do not let the written bit reach the native device.
                if (offset == ctlr_first && size >= 4)
                    data &= ~(std::uint64_t{1} << ctlr_quiescent_bit);
                return native_as_->write(offset, &data, size, attrs);
            });
        window_ = instance.object_new_unparented<qemu::MemoryRegion>();
        window_.init_io(get_qemu_dev(), (stem + ".window").c_str(), window_size, ops_);
        mem.init_with_mr(window_);
    }
};

} // namespace qcom_vp

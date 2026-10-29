// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "execution_group.h"
#include <arm_gicv3.h>
#include <array>
#include <tlm_utils/multi_passthrough_target_socket.h>

namespace qcom_vp {

// One native region preserves SGIs, security, and the final-only Last bit.
class ApssGic : public arm_gicv3 {
public:
    sc_core::sc_vector<QemuTargetSocket<>> redist_slot;
    tlm_utils::multi_passthrough_target_socket<ApssGic> fault_socket{"fault_socket"};

    ApssGic(sc_core::sc_module_name name, sc_core::sc_object* object, sc_core::sc_object* group)
        : arm_gicv3(name, object), redist_slot("redist_slot"),
          group_(dynamic_cast<ApssExecutionGroup*>(group)) {
        if (!group_ || &group_->instance() != &get_qemu_inst())
            throw std::invalid_argument("APSS GIC requires the CPU execution group instance");
        redist_slot.init(group_->cpu_count.get_value(),
            [this](const char* n, size_t) { return new QemuTargetSocket<>(n, get_qemu_inst()); });
        fault_socket.register_b_transport(this, &ApssGic::fault_transport);
    }

private:
    // GICv4 with LPIs has GICR_TYPER.VLPIS set, so the native frame is 0x40000.
    // QEMU selects that size from VLPIS in gicv3_redist_size().
    // The Kaanapali DT also declares one 0x40000-byte stride per redistributor.
    static constexpr std::uint64_t redistributor_frame = 0x40000;
    ApssExecutionGroup* group_;
    std::array<qemu::MemoryRegion, ApssExecutionGroup::maximum_cpu_count> aliases_;
    qemu::MemoryRegion redist_native_;
    std::shared_ptr<qemu::AddressSpace> redist_as_;
    std::array<qemu::MemoryRegionOpsPtr, ApssExecutionGroup::maximum_cpu_count> redist_ops_;
    std::array<std::uint32_t, 2> fault_route_{};
    std::array<std::uint64_t, 2> fault_control_{};
    qemu::MemoryRegion dist_native_;
    qemu::MemoryRegion dist_window_;
    qemu::MemoryRegionOpsPtr dist_ops_;

    // GIC-700 reports No1N=0. QHEE requires it at gicv3.c:830.
    // This capability override is an explicit policy, not native QEMU
    // 1-of-N routing support. See docs/qcom/sm88xx/stubs.md and apss/evidence.md.
    // The window also reports the GIC-700 product identity.
    // All other distributor reads and writes retain native behavior.
    void publish_distributor_typer() {
        auto& instance = get_qemu_inst().get();
        dist_native_ = qemu::SysBusDevice(get_qemu_dev()).mmio_get_region(0);
        const std::string stem = std::string(name()) + ".distributor";

        dist_ops_ = instance.memory_region_ops_new();
        dist_ops_->set_max_access_size(8);
        dist_ops_->set_read_callback(
            [this](std::uint64_t offset, std::uint64_t* data, unsigned size,
                   qemu::MemoryRegionOps::MemTxAttrs attrs) {
                const auto result = dist_native_.dispatch_read(offset, data, size, attrs);
                if (result != qemu::MemoryRegionOps::MemTxOK)
                    return result;
                // TYPER bit 25 is bit one of its fourth byte.
                constexpr std::uint64_t typer_low_byte = 7;
                if (offset <= typer_low_byte && typer_low_byte < offset + size)
                    *data &= ~(std::uint64_t{1} << ((typer_low_byte - offset) * 8 + 1));
                // GICD_IIDR is at offset 8. Its fourth byte holds ProductID.
                // TF-A defines GIC_PRODUCT_ID_GIC700 as 4 and this SoC uses a GIC-700 class controller.
                // QHEE at image offset 0x46c7c requires ProductID two or four.
                constexpr std::uint64_t iidr_product_byte = 8 + 3;
                constexpr std::uint64_t gic700_product = 4;
                for (unsigned k = 0; k != size; ++k) {
                    if (offset + k != iidr_product_byte)
                        continue;
                    *data &= ~(std::uint64_t{0xff} << (8 * k));
                    *data |= gic700_product << (8 * k);
                }
                return result;
            });
        dist_ops_->set_write_callback(
            [this](std::uint64_t offset, std::uint64_t data, unsigned size,
                   qemu::MemoryRegionOps::MemTxAttrs attrs) {
                return dist_native_.dispatch_write(offset, data, size, attrs);
            });
        dist_window_ = instance.object_new_unparented<qemu::MemoryRegion>();
        dist_window_.init_io(get_qemu_dev(), (stem + ".window").c_str(), 0x10000, dist_ops_);
        dist_iface.init_with_mr(dist_window_);
    }

    // QSEE configures fault routing without producing a hardware fault.
    void fault_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const auto size = tx.get_data_length();
        const auto address = tx.get_address();
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || (size != 4 && size != 8) ||
            tx.get_streaming_width() < size || (address & (size - 1))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const bool route = address == 0xe800 || address == 0xe808;
        const bool control = (address & ~std::uint64_t{7}) == 0x48 ||
                             (address & ~std::uint64_t{7}) == 0x88;
        if ((!route && !control) || (route && size != 4)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const unsigned index = route ? (address == 0xe808) : (address >= 0x88);
        const unsigned shift = control ? (address & 4) * 8 : 0;
        auto value = route ? std::uint64_t{fault_route_[index]} : fault_control_[index];
        auto* data = tx.get_data_ptr();
        if (tx.is_write()) {
            const auto mask = size == 8 ? ~std::uint64_t{0} : std::uint64_t{0xffffffff} << shift;
            value &= ~mask;
            for (unsigned i = 0; i != size; ++i)
                value |= std::uint64_t{data[i]} << (shift + 8 * i);
            if (route) fault_route_[index] = static_cast<std::uint32_t>(value);
            else fault_control_[index] = value;
        } else {
            for (unsigned i = 0; i != size; ++i)
                data[i] = static_cast<unsigned char>(value >> (shift + 8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void before_end_of_elaboration() override {
        const auto count = group_->cpu_count.get_value();
        if (p_num_cpu.get_value() != count || p_first_cpu_index.get_value() != 0 ||
            p_redist_region.get_value() != std::vector<unsigned>({count}) ||
            p_revision.get_value() != 4 || !p_has_security_extensions.get_value() || !p_has_lpi.get_value() ||
            !get_qemu_inst().is_tcg_enabled())
            throw std::invalid_argument("APSS GIC requires the execution group in one secure redistributor region");
        arm_gicv3::before_end_of_elaboration();
    }

    void end_of_elaboration() override {
        group_->realize();
        arm_gicv3::end_of_elaboration();
        publish_distributor_typer();
        auto bank = qemu::SysBusDevice(get_qemu_dev()).mmio_get_region(1);
        auto& instance = get_qemu_inst().get();
        redist_native_ = bank;
        redist_as_ = instance.address_space_new();
        redist_as_->init(redist_native_, "apss-gic-redistributors");
        for (unsigned i = 0; i < redist_slot.size(); ++i) {
            const auto slot = std::uint64_t{i} * redistributor_frame;
            auto ops = instance.memory_region_ops_new();
            ops->set_max_access_size(8);
            ops->set_read_callback(
                [this, slot](std::uint64_t offset, std::uint64_t* data, unsigned size,
                             qemu::MemoryRegionOps::MemTxAttrs attrs) {
                    const auto result = redist_as_->read(slot + offset, data, size, attrs);
                    if (result != qemu::MemoryRegionOps::MemTxOK)
                        return result;
                    // GICR_TYPER is the 64-bit word at frame offset 8.
                    // QHEE FUN_00148100 tests mask 0x04000084 at image offsets 0x4826c..0x48284:
                    // Dirty bit 2, RVPEID bit 7, and VSGI bit 26 must all be one.
                    // Native QEMU sets none of these GICv4.1 fields at revision four.
                    // This capability advertisement is explicit policy, not extra hardware.
                    // See docs/qcom/sm88xx/stubs.md and models/qcom/apss/evidence.md.
                    for (unsigned k = 0; k != size; ++k) {
                        const auto byte = offset + k;
                        if (byte == 8)
                            *data |= std::uint64_t{0x84} << (8 * k);
                        else if (byte == 11)
                            *data |= std::uint64_t{0x04} << (8 * k);
                    }
                    return result;
                });
            ops->set_write_callback(
                [this, slot](std::uint64_t offset, std::uint64_t data, unsigned size,
                             qemu::MemoryRegionOps::MemTxAttrs attrs) {
                    return redist_as_->write(slot + offset, &data, size, attrs);
                });
            redist_ops_[i] = ops;
            aliases_[i] = instance.object_new_unparented<qemu::MemoryRegion>();
            const auto alias_name = std::string(name()) + ".redist-slot-" + std::to_string(i);
            aliases_[i].init_io(get_qemu_dev(), alias_name.c_str(), redistributor_frame, ops);
            redist_slot[i].init_with_mr(aliases_[i]);
        }
    }
};

} // namespace qcom_vp

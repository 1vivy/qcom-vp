// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "cpucp_runtime_controls.h"
#include <systemc>
#include <cci_configuration>
#include <cpu.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// The virtual policy separates ROM vector publication from the APSS reset request.
class QcomCpucpBootControl : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCpucpBootControl> target_socket{"target_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpucpBootControl> reset_socket{"reset_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomCpucpBootControl> setup_socket{"setup_socket"};
    tlm_utils::simple_target_socket_optional<QcomCpucpBootControl> runtime_socket{"runtime_socket"};
    tlm_utils::simple_target_socket_optional<QcomCpucpBootControl> runtime_policy_socket{"runtime_policy_socket"};
    tlm_utils::simple_target_socket_optional<QcomCpucpBootControl> runtime_clock_socket{"runtime_clock_socket"};
    cci::cci_param<std::string> runtime_clock_path{
        "runtime_clock_path", "", "Existing live clock for the optional runtime configuration transition"};
    cci::cci_param<unsigned> runtime_lane_mask{
        "runtime_lane_mask", 3, "Implemented lanes in the optional runtime configuration group"};
    InitiatorSignalSocket<bool> reset{"reset"};
    cci::cci_param<unsigned> hart_index{"hart_index", 0, "Boot vector index for the connected CPU: 0 or 1"};
    cci::cci_param<bool> sifive_cease{
        "sifive_cease", false, "Enable the observed SiFive CEASE instruction on the connected CPU"};
    cci::cci_param<bool> legacy_rnmi{
        "legacy_rnmi", false, "Enable the observed legacy RNMI CSR window on the connected CPU"};
    // Virtual policy releases the cold-held secondary through the subsystem commit.
    cci::cci_param<std::string> hart1_path{
        "hart1_path", "", "Optional second CPU path for a dual-hart CPUCP release"};
    InitiatorSignalSocket<bool> reset1{"reset1"};
    TargetSignalSocket<bool> hart1_release{"hart1_release"};

    enum class RegisterLayout { v1, v2 };

    SC_HAS_PROCESS(QcomCpucpBootControl);
    QcomCpucpBootControl(sc_core::sc_module_name name, sc_core::sc_object* cpu,
                        RegisterLayout layout = RegisterLayout::v1)
        : sc_module(name), cpu_(dynamic_cast<QemuCpu*>(cpu)), layout_(layout) {
        if (!cpu_) throw std::invalid_argument("CPUCP boot control requires a QemuCpu");
        target_socket.register_b_transport(this, &QcomCpucpBootControl::boot_transport);
        reset_socket.register_b_transport(this, &QcomCpucpBootControl::reset_transport);
        SC_THREAD(reset_thread);
        SC_THREAD(hart1_reset_thread);
        setup_socket.register_b_transport(this, &QcomCpucpBootControl::setup_transport);
        runtime_socket.register_b_transport(this, &QcomCpucpBootControl::runtime_transport);
        runtime_policy_socket.register_b_transport(this, &QcomCpucpBootControl::runtime_policy_transport);
        runtime_clock_socket.register_b_transport(this, &QcomCpucpBootControl::runtime_clock_transport);
        hart1_release.register_value_changed_cb([this](const bool& level) {
            if (level) hart1_release_event_.notify(sc_core::SC_ZERO_TIME);
        });
    }

private:
    QemuCpu* cpu_;
    QemuCpu* cpu1_ = nullptr;
    const RegisterLayout layout_;
    std::array<std::uint32_t, 2> vectors_{};
    std::array<std::array<std::uint32_t, 2>, 2> recovery_vectors_{};
    std::array<bool, 2> written_{};
    std::uint32_t select_ = 0;
    std::uint32_t request_ = 0;
    std::uint32_t pending_vector_ = 0;
    // Bit 8 remains a V2 reset pulse field. Optional runtime bit 12 applies configuration.
    std::array<std::uint32_t, 8> setup_{};
    CpucpRuntimeControls runtime_;
    ClockSource* runtime_source_ = nullptr;
    bool pending_ = false;
    sc_core::sc_event reset_event_;
    sc_core::sc_event hart1_release_event_;

    void before_end_of_elaboration() override {
        const auto& hart1 = hart1_path.get_value();
        if (!hart1.empty()) {
            cpu1_ = dynamic_cast<QemuCpu*>(sc_core::sc_find_object(hart1.c_str()));
            if (!cpu1_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + hart1;
                cpu1_ = dynamic_cast<QemuCpu*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!cpu1_) throw std::invalid_argument("CPUCP dual-hart release requires a real second QemuCpu");
            cpu1_->instantiate();
            cpu1_->get_qemu_dev().set_prop_bool("start-powered-off", true);
        }
        if (sifive_cease.get_value()) {
            cpu_->instantiate();
            cpu_->get_qemu_dev().set_prop_bool("x-sifive-cease", true);
            if (cpu1_) {
                cpu1_->instantiate();
                cpu1_->get_qemu_dev().set_prop_bool("x-sifive-cease", true);
            }
        }
        if (legacy_rnmi.get_value()) {
            cpu_->instantiate();
            cpu_->get_qemu_dev().set_prop_bool("x-legacy-rnmi", true);
            if (cpu1_) {
                cpu1_->instantiate();
                cpu1_->get_qemu_dev().set_prop_bool("x-legacy-rnmi", true);
            }
        }
    }

    void end_of_elaboration() override {
        if (hart_index.get_value() > 1) {
            throw std::invalid_argument("CPUCP boot control requires hart index 0 or 1");
        }
        if (reset.size() != 1) {
            throw std::invalid_argument("CPUCP boot control requires one CPU reset consumer");
        }
        if (reset1.size() != (cpu1_ ? 1u : 0u)) {
            throw std::invalid_argument("CPUCP dual-hart release requires exactly one second CPU reset consumer");
        }
        const auto& path = runtime_clock_path.get_value();
        if (!path.empty()) {
            runtime_source_ = dynamic_cast<ClockSource*>(sc_core::sc_find_object(path.c_str()));
            if (!runtime_source_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                runtime_source_ = dynamic_cast<ClockSource*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!runtime_source_ || !runtime_lane_mask.get_value() || (runtime_lane_mask.get_value() & ~3u))
                throw std::invalid_argument("CPUCP runtime configuration requires a clock and implemented lane mask");
        }
    }

    void start_of_simulation() override {
        reset->write(false);
    }

    static bool valid(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        } else if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
                   (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        } else if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        } else {
            return true;
        }
        return false;
    }

    static std::uint32_t read_data(const tlm::tlm_generic_payload& tx) {
        const auto* data = tx.get_data_ptr();
        return std::uint32_t{data[0]} | (std::uint32_t{data[1]} << 8) |
            (std::uint32_t{data[2]} << 16) | (std::uint32_t{data[3]} << 24);
    }

    static void write_data(tlm::tlm_generic_payload& tx, std::uint32_t value) {
        for (unsigned i = 0; i != 4; ++i) {
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        }
    }

    void boot_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset == 0x20 || offset == 0x24 || offset == 0x40 || offset == 0x44) {
            auto& word = recovery_vectors_[offset >= 0x40][(offset & 4) / 4];
            if (tx.is_read()) write_data(tx, word);
            else {
                const auto value = read_data(tx);
                if (value & 1) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                // Guest publication does not synthesize an NMI or reset.
                // The IRQ/exception vector-bank distinction remains unproven.
                word = value;
            }
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (offset != 0 && offset != 4 && offset != 0x60) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            write_data(tx, offset == 0x60 ? select_ : vectors_[offset / 4]);
        } else {
            const auto value = read_data(tx);
            if (pending_ || (offset == 0x60 && (value & ~std::uint32_t{1})) ||
                (offset != 0x60 && (value & 1))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (offset == 0x60) {
                select_ = value;
            } else {
                vectors_[offset / 4] = value;
                written_[offset / 4] = true;
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void setup_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset == 0x94) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            write_data(tx, 0);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        unsigned index;
        if (offset == 0) index = 0;
        else if (offset == 0x1c) index = 1;
        else if (offset == 0x8c) index = 2;
        else if ((layout_ == RegisterLayout::v2 || runtime_source_) && offset == 0x14) index = 3;
        else if ((layout_ == RegisterLayout::v2 || runtime_source_) && offset == 0x18) index = 4;
        else if (offset == 0x10) index = 5;
        else if (offset == 0x78) index = 6;
        else if (offset == 0x80) index = 7;
        else {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            const auto value = read_data(tx);
            const auto mask = (layout_ == RegisterLayout::v2 ? 0x100u : 0u) |
                (runtime_source_ ? 0x1000u : 0u);
            if ((index == 3 || index == 4) && (value & ~mask)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            setup_[index] = value;
        } else {
            write_data(tx, setup_[index]);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void runtime_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!runtime_source_) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
        std::uint32_t status = 0;
        if (tx.is_read() && tx.get_address() == 0x10) {
            const bool running = runtime_source_->clock_running();
            status = runtime_.status(runtime_lane_mask.get_value(), running, setup_[3], setup_[4]);
        }
        runtime_.event_transport(tx, status);
    }
    void runtime_policy_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!runtime_source_) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
        runtime_.policy_transport(tx);
    }
    void runtime_clock_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!runtime_source_) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
        runtime_.clock_transport(tx);
    }

    void reset_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            write_data(tx, request_);
        } else {
            const auto value = read_data(tx);
            const auto index = hart_index.get_value();
            if ((value & ~std::uint32_t{1}) || pending_ ||
                ((value & 1) && (!(select_ & 1) || !written_[index]))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            request_ = value;
            if (value & 1) {
                pending_vector_ = vectors_[index];
                pending_ = true;
                reset_event_.notify(sc_core::SC_ZERO_TIME);
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void reset_thread() {
        for (;;) {
            sc_core::wait(reset_event_);
            // QEMU permits resetvec writes after realization. This does not change the current PC.
            // The property setter dispatches to the QEMU I/O thread and acquires its lock.
            cpu_->get_qemu_dev().set_prop_uint("resetvec", pending_vector_);
            // QemuCpu requires an SC_THREAD. Deassertion waits for its safe reset job.
            reset->write(true);
            reset->write(false);
            pending_ = false;
        }
    }

    void hart1_reset_thread() {
        for (;;) {
            sc_core::wait(hart1_release_event_);
            if (!cpu1_) continue;
            // The guest must publish the vector before it requests release.
            if (!written_[1]) continue;
            cpu1_->get_qemu_dev().set_prop_uint("resetvec", vectors_[1]);
            cpu1_->get_qemu_dev().set_prop_bool("start-powered-off", false);
            reset1->write(true);
            reset1->write(false);
        }
    }
};

class QcomCpucpBootControlV2 final : public QcomCpucpBootControl {
public:
    QcomCpucpBootControlV2(sc_core::sc_module_name name, sc_core::sc_object* cpu)
        : QcomCpucpBootControl(name, cpu, RegisterLayout::v2) {}
};

} // namespace qcom_vp

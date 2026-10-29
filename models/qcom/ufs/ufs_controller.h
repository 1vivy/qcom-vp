// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef QCOM_VP_UFS_CONTROLLER_H
#define QCOM_VP_UFS_CONTROLLER_H

#include <ufs.h>
#include <ports/initiator.h>
#include <async_event.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cci_configuration>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include "virtual_phy.h"
#include "clock_source.h"

// The dedicated QEMU instance owns only this controller and its logical units.
class UfsController : public ufs, public QemuInitiatorIface, public qcom_vp::ClockSource {
public:
    SC_HAS_PROCESS(UfsController);
    tlm_utils::simple_target_socket<UfsController, DEFAULT_TLM_BUSWIDTH> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<UfsController, DEFAULT_TLM_BUSWIDTH> dma_socket{"dma_socket"};
    tlm_utils::simple_target_socket_optional<UfsController, DEFAULT_TLM_BUSWIDTH> phy_socket{"phy_socket"};
    tlm_utils::simple_target_socket<UfsController, DEFAULT_TLM_BUSWIDTH> mem_power_socket{"mem_power_socket"};
    tlm_utils::simple_target_socket<UfsController, DEFAULT_TLM_BUSWIDTH> logic_power_socket{"logic_power_socket"};
    tlm_utils::simple_target_socket<UfsController, DEFAULT_TLM_BUSWIDTH> reset_socket{"reset_socket"};
    tlm_utils::simple_target_socket<UfsController, DEFAULT_TLM_BUSWIDTH> device_reset_socket{"device_reset_socket"};
    tlm_utils::simple_target_socket<UfsController, DEFAULT_TLM_BUSWIDTH> reference_clock_socket{"reference_clock_socket"};
    cci::cci_param<bool> virtual_phy{"virtual_phy", false, "Use the explicit virtual physical link"};
    cci::cci_param<bool> boot_enable{"boot_enable", false, "OEM boot capability"};
    cci::cci_param<uint32_t> boot_lun_en{"boot_lun_en", 255, "Initial OEM boot LU ID"};
    cci::cci_param<uint32_t> rpmb_size_kib{
        "rpmb_size_kib", 0, "Declared RPMB region 0 size in KiB, zero disables RPMB"};
    cci::cci_param<std::string> rpmb_state_file{
        "rpmb_state_file", "", "Persistent RPMB state path, empty selects volatile host storage"};
    cci::cci_param<uint32_t> virtual_hw_revision{
        "virtual_hw_revision", 0x30010000, "Explicit virtual host revision, not a silicon measurement"};
    cci::cci_param<std::string> phy_clock_paths[6]{
        {"phy_axi_clock", "", "Live AXI branch"},
        {"phy_ahb_clock", "", "Live AHB branch"},
        {"phy_ice_clock", "", "Live ICE branch"},
        {"phy_unipro_clock", "", "Live UniPro branch"},
        {"phy_aux_clock", "", "Live PHY auxiliary branch"},
        {"phy_aggre_clock", "", "Live aggregate AXI branch"}};

    UfsController(sc_core::sc_module_name name, sc_core::sc_object* instance)
        : UfsController(name, checked_instance(instance)) {}

    UfsController(sc_core::sc_module_name name, QemuInstance& instance)
        : ufs(name, instance), qemu_dma("qemu_dma", *this, instance) {
        // Select wide dispatches before init_global creates the region operations.
        // Other initiator sockets keep their eight-byte access limit.
        qemu_dma.enable_bulk_access();
        target_socket.register_b_transport(this, &UfsController::transport);
        target_socket.register_transport_dbg(this, &UfsController::debug);
        registers.bind(q_socket);
        qemu_dma.bind(dma_target);
        dma_target.register_b_transport(this, &UfsController::dma_transport);
        dma_target.register_transport_dbg(this, &UfsController::dma_debug);
        dma_target.register_get_direct_mem_ptr(this, &UfsController::no_dma_dmi);
        phy_socket.register_b_transport(this, &UfsController::phy_transport);
        phy_socket.register_transport_dbg(this, &UfsController::phy_debug);
        mem_power_socket.register_b_transport(this, &UfsController::mem_power_transport);
        logic_power_socket.register_b_transport(this, &UfsController::logic_power_transport);
        reset_socket.register_b_transport(this, &UfsController::reset_transport);
        device_reset_socket.register_b_transport(this, &UfsController::device_reset_transport);
        reference_clock_socket.register_b_transport(this, &UfsController::reference_clock_transport);
        SC_METHOD(deliver_irq);
        sensitive << irq_changed;
        dont_initialize();
        SC_THREAD(source_clock_thread);
    }

    void before_end_of_elaboration() override {
        if (virtual_hw_revision.get_value() != 0x30010000)
            throw std::invalid_argument("Unsupported virtual UFS host revision");
        if (p_mcq.get_value()) {
            throw std::invalid_argument("UfsController does not expose MCQ registers");
        }
        if (boot_lun_en.get_value() > 2 ||
            (!boot_enable.get_value() && boot_lun_en.get_value() != 0)) {
            throw std::invalid_argument("UfsController requires a valid boot selection");
        }
        ufs::before_end_of_elaboration();
        m_dev.set_prop_bool("boot-enable", boot_enable.get_value());
        m_dev.set_prop_uint("boot-lun-en", boot_lun_en.get_value());
        m_dev.set_prop_uint("rpmb-size-kib", rpmb_size_kib.get_value());
        if (!rpmb_state_file.get_value().empty())
            m_dev.set_prop_str("rpmb-state-file", rpmb_state_file.get_value().c_str());
        qemu_dma.init_global(m_dev);
    }
    void end_of_elaboration() override {
        ufs::end_of_elaboration();
        for (unsigned i = 0; i != phy_sources.size(); ++i) {
            const auto& path = phy_clock_paths[i].get_value();
            if (path.empty()) continue;
            phy_sources[i] = dynamic_cast<qcom_vp::ClockSource*>(sc_core::sc_find_object(path.c_str()));
            if (!phy_sources[i] && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                phy_sources[i] = dynamic_cast<qcom_vp::ClockSource*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!phy_sources[i] || phy_sources[i] == this)
                throw std::invalid_argument("A UFS source clock is absent or cyclic");
        }
        const auto connected = std::count_if(phy_sources.begin(), phy_sources.end(),
            [](qcom_vp::ClockSource* source) { return source != nullptr; });
        if (connected && connected != 6)
            throw std::invalid_argument("UFS source clocks must be all connected or all absent");
        if (connected) {
            phy.set_source_clocks(false);
            source_clocks_available = false;
        }
        device_reset_gpio = qemu::Gpio(m_dev.get_gpio_in_named("device-reset-n", 0));
        // GpioProxy commits its previous level only after its callback returns.
        // The default cross-instance socket unlocks UFS during that callback.
        // A concurrent acknowledgement can then lose the falling edge.
        // Capture without unlocking or waiting, then forward on SystemC.
        irq_capture = m_inst.get().gpio_new();
        irq_capture.set_event_callback([this](bool level) {
            {
                std::lock_guard<std::mutex> lock(irq_mutex);
                irq_levels.push_back(level);
            }
            irq_changed.async_notify();
        });
        qemu::SysBusDevice(m_dev).connect_gpio_out(0, irq_capture);
    }

    bool clock_running() override { return source_clocks_running() && phy.ready(); }
    const sc_core::sc_event& clock_changed() const override { return phy_changed; }


    void end_of_simulation() override {
        // Wait for any capture callback before disabling it. The GPIO member
        // retains its proxy until controller teardown, after simulation stops.
        const bool locked = m_inst.get().is_iothread_locked();
        if (!locked) m_inst.get().lock_iothread();
        irq_capture.set_event_callback(nullptr);
        if (!locked) m_inst.get().unlock_iothread();
        qemu_dma.end_of_simulation();
    }

    void initiator_customize_tlm_payload(TlmPayload&) override {}
    void initiator_tidy_tlm_payload(TlmPayload&) override {}
    sc_core::sc_time initiator_get_local_time() override { return sc_core::SC_ZERO_TIME; }
    void initiator_set_local_time(const sc_core::sc_time&) override {}
    void initiator_async_run(qemu::Cpu::AsyncJobFn) override {
        // DMA cannot install DMI aliases through dma_target.
        SC_REPORT_FATAL("UfsController", "Unexpected DMA alias invalidation");
    }

private:
    QemuInitiatorSocket<> qemu_dma;
    tlm_utils::simple_target_socket<UfsController, DEFAULT_TLM_BUSWIDTH> dma_target{"dma_target"};
    tlm_utils::simple_initiator_socket<UfsController, DEFAULT_TLM_BUSWIDTH> registers{"registers"};
    // SYS1CLK_1US is a software clock-count setting, not a completion register.
    // No SM8850 reset value is known. Reads require a prior guest write.
    uint32_t sys1clk_1us = 0;
    bool sys1clk_programmed = false;
    UfsVirtualPhy phy;
    uint32_t cfg0 = 0;
    uint32_t cfg1 = 0;
    bool cfg1_programmed = false;
    uint32_t debug_spare = 0;
    uint32_t uic_arg1 = 0;
    bool memory_powered = false;
    bool logic_powered = false;
    bool reset_asserted = false;
    bool device_reset_high = true;
    qemu::Gpio device_reset_gpio;
    sc_core::sc_event phy_changed;
    std::array<qcom_vp::ClockSource*, 6> phy_sources{};
    bool source_clocks_available = true;

    bool source_clocks_running() const {
        return std::all_of(phy_sources.begin(), phy_sources.end(),
            [](qcom_vp::ClockSource* source) { return !source || source->clock_running(); });
    }

    void source_clock_thread() {
        sc_core::sc_event_or_list events;
        bool connected = false;
        for (auto* source : phy_sources) {
            if (!source) continue;
            events |= source->clock_changed();
            connected = true;
        }
        if (!connected) return;
        for (;;) {
            synchronize_source_clocks();
            sc_core::wait(events);
        }
    }

    void synchronize_source_clocks() {
        const bool running = source_clocks_running();
        if (running == source_clocks_available) return;
        if (!running && phy.ready()) {
            auto delay = sc_core::SC_ZERO_TIME;
            if (!idle(delay)) {
                SC_REPORT_FATAL("UfsController", "UFS source clock loss during an active transfer");
                return;
            }
            quiesce(delay);
        }
        unsigned stopped = 0;
        for (unsigned i = 0; i != phy_sources.size(); ++i)
            if (phy_sources[i] && !phy_sources[i]->clock_running()) stopped |= 1u << i;
        std::cerr << "UFS_SOURCE_CLOCKS model=" << name() << " running=" << running << " stopped_mask=0x" << std::hex
                  << stopped << std::dec << " link_started=" << phy.ready() << " at=" << sc_core::sc_time_stamp() << '\n';
        phy.set_source_clocks(running);
        source_clocks_available = running;
        phy_changed.notify(sc_core::SC_ZERO_TIME);
    }
    qemu::Gpio irq_capture;
    gs::async_event irq_changed{false};
    std::mutex irq_mutex;
    std::deque<bool> irq_levels;

    void deliver_irq() {
        for (;;) {
            bool level;
            {
                std::lock_guard<std::mutex> lock(irq_mutex);
                if (irq_levels.empty()) return;
                level = irq_levels.front();
                irq_levels.pop_front();
            }
            // Do not hold the queue mutex while entering the APSS instance.
            if (irq_out.get_interface()) irq_out->write(level);
        }
    }

    bool powered() const { return memory_powered && logic_powered; }
    bool operational() const { return powered() && !reset_asserted; }

    // These operations use QEMU's implemented register paths, not system_reset().
    bool qemu_word(uint64_t address, uint32_t& value, bool write,
                   sc_core::sc_time& delay) {
        unsigned char bytes[4];
        for (unsigned i = 0; i != 4; ++i) bytes[i] = value >> (8 * i);
        tlm::tlm_generic_payload tx;
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_address(address);
        tx.set_data_ptr(bytes);
        tx.set_data_length(4);
        tx.set_streaming_width(4);
        registers->b_transport(tx, delay);
        if (!tx.is_response_ok()) return false;
        if (!write) value = load_word(tx);
        return true;
    }

    bool idle(sc_core::sc_time& delay) {
        uint32_t doorbell = 0;
        return qemu_word(0x58, doorbell, false, delay) && doorbell == 0;
    }

    void quiesce(sc_core::sc_time& delay) {
        // No active requests may cross this boundary. HCE clear drops HCS.
        // Clear IRQ state and the list configuration through real MMIO writes.
        constexpr uint64_t offsets[] = {0x24, 0x34, 0x60, 0x50, 0x54,
                                        0x94, 0x98, 0x9c, 0x20};
        for (const auto offset : offsets) {
            uint32_t value = offset == 0x20 ? 0xffffffffu : 0;
            if (!qemu_word(offset, value, true, delay))
                SC_REPORT_FATAL("UfsController", "QEMU rejected quiescent power collapse");
        }
        sys1clk_1us = cfg0 = debug_spare = uic_arg1 = 0;
        sys1clk_programmed = false;
    }

    void power_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay,
                         bool& supply) {
        if (!valid_shape(tx)) return;
        if (!tx.is_write() || tx.get_address() != 0 || load_word(tx) > 1) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const bool enabled = load_word(tx) != 0;
        if (enabled != supply) {
            if (!enabled) {
                if (!idle(delay)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                quiesce(delay);
                cfg1 = 0;
                cfg1_programmed = false;
            }
            supply = enabled;
            phy.set_supply(operational());
            phy_changed.notify(sc_core::SC_ZERO_TIME);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void mem_power_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        power_transport(tx, delay, memory_powered);
    }
    void logic_power_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        power_transport(tx, delay, logic_powered);
    }
    // GCC_UFS_PHY_BCR holds the controller and virtual PHY in reset.
    void reset_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!valid_shape(tx)) return;
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            const auto value = load_word(tx);
            if (value > 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (value && !reset_asserted) {
                if (!idle(delay)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                quiesce(delay);
                cfg1 = 0;
                cfg1_programmed = false;
            }
            reset_asserted = value != 0;
            phy.set_supply(operational());
            phy_changed.notify(sc_core::SC_ZERO_TIME);
        } else {
            auto* bytes = tx.get_data_ptr();
            bytes[0] = reset_asserted ? 1 : 0;
            bytes[1] = bytes[2] = bytes[3] = 0;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void device_reset_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!valid_shape(tx)) return;
        if (tx.get_address()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            const auto value = load_word(tx);
            if (value > 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (!value && device_reset_high && !idle(delay)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if ((value != 0) != device_reset_high) {
                device_reset_gpio.set(value != 0);
                device_reset_high = value != 0;
            }
        } else {
            auto* bytes = tx.get_data_ptr();
            bytes[0] = device_reset_high ? 1 : 0;
            bytes[1] = bytes[2] = bytes[3] = 0;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void reference_clock_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!valid_shape(tx)) return;
        if (!tx.is_write() || tx.get_address() || load_word(tx) > 1) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const bool enabled = load_word(tx) != 0;
        if (enabled != phy.reference_running()) {
            if (!enabled) {
                if (!idle(delay)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                quiesce(delay);
            }
            std::cerr << "UFS_REFERENCE_CLOCK model=" << name() << " running=" << enabled
                      << " link_started=" << phy.ready() << '\n';
            phy.set_reference(enabled);
            phy_changed.notify(sc_core::SC_ZERO_TIME);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }




    bool available(tlm::tlm_generic_payload& tx) {
        synchronize_source_clocks();
        if (!operational()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        // QEMU does not itself enforce the physical link on a doorbell.
        if (tx.is_write() && tx.get_address() == 0x58) {
            auto delay = sc_core::SC_ZERO_TIME;
            uint32_t hcs = 0, hce = 0, run = 0;
            if (!device_reference_running() || !phy.ready() || !qemu_word(0x30, hcs, false, delay) ||
                !qemu_word(0x34, hce, false, delay) ||
                !qemu_word(0x60, run, false, delay) ||
                !device_reset_high || !(hcs & 1) || !(hce & 1) || !(run & 1)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return false;
            }
        }
        return true;
    }

    static uint32_t load_word(const tlm::tlm_generic_payload& tx) {
        const auto* bytes = tx.get_data_ptr();
        return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
               (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
    }

    bool virtual_configuration(tlm::tlm_generic_payload& tx) {
        if (tx.get_address() != 0xd8 && tx.get_address() != 0x284c) return false;
        if (!virtual_phy.get_value()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return true;
        }
        auto& state = tx.get_address() == 0xd8 ? cfg0 : debug_spare;
        // DEBUG_SPARE_CFG is 32 spare bits. UEFI stores the UFS version there for later software.
        const uint32_t mask = tx.get_address() == 0xd8 ? 0x21 : 0xffffffff;
        if (tx.is_write()) {
            const auto value = load_word(tx);
            if (value & ~mask) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return true;
            }
            state = value;
        } else {
            for (unsigned i = 0; i != 4; ++i) tx.get_data_ptr()[i] = state >> (8 * i);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    bool device_reference_running() const {
        return !cfg1_programmed || (cfg1 & (1u << 26));
    }

    bool host_configuration(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        const auto offset = tx.get_address();
        if (offset != 0xdc && offset != 0xe4) return false;
        if (!virtual_phy.get_value() || !virtual_hw_revision.get_value()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return true;
        }
        if (offset == 0xe4) {
            if (!tx.is_read()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return true;
            }
            const auto value = virtual_hw_revision.get_value();
            for (unsigned i = 0; i != 4; ++i) tx.get_data_ptr()[i] = value >> (8 * i);
        } else if (tx.is_read()) {
            if (!cfg1_programmed) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return true;
            }
            for (unsigned i = 0; i != 4; ++i) tx.get_data_ptr()[i] = cfg1 >> (8 * i);
        } else {
            const auto value = load_word(tx);
            constexpr uint32_t policy = 0x1800052c;
            constexpr uint32_t controls = (1u << 26) | 3u;
            // The selected HAL holds WARM_DME_RESET high throughout enable.
            // Independent changes to that policy are not an implemented command.
            if ((value & ~controls) != policy ||
                (!cfg1_programmed && !(value & 2))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return true;
            }
            const bool asserts_reset = (value & 2) && (!cfg1_programmed || !(cfg1 & 2));
            const bool loses_reference = cfg1_programmed && (cfg1 & (1u << 26)) &&
                !(value & (1u << 26));
            if (asserts_reset || loses_reference) {
                if (!idle(delay)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return true;
                }
                quiesce(delay);
            }
            if ((value & 2) && (!cfg1_programmed || !(cfg1 & 2)))
                phy.set_host_reset(true);
            else if (!(value & 2) && (cfg1 & 2))
                phy.set_host_reset(false);
            cfg1 = value;
            cfg1_programmed = true;
            phy_changed.notify(sc_core::SC_ZERO_TIME);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    bool supported_uic(uint32_t command) const {
        if (!virtual_phy.get_value() || !source_clocks_available) return false;
        // Local capability reads precede PHY setup and HCE in the selected XBL.
        // Remote and link operations still require the physical connection.
        if (command != 0x01 && (!phy.ready() || !device_reference_running())) return false;
        switch (command) {
        case 0x16: // DME_LINK_STARTUP requires the modeled physical transition.
            return virtual_phy.get_value() && phy.ready();
        case 0x17: case 0x18: // Upstream models HIBER_ENTER and HIBER_EXIT.
            return true;
        case 0x01: case 0x03: // DME_GET and DME_PEER_GET.
            if (uic_arg1 & 0xffff)
                return command == 0x03 && uic_arg1 == 0x009f0004;
            switch (uic_arg1 >> 16) {
            case 0x0002: case 0x0022: case 0x0023:
                return command == 0x03;
            case 0xd002: case 0x9007:
            case 0x0041: case 0x15a0: case 0x15a7:
            case 0x15d3: case 0x15d4: case 0x155e: case 0x155f:
                return command == 0x01;
            case 0x15aa: case 0x15a8:
                return true;
            case 0x15a9: case 0x15a4: case 0x1571:
            case 0x1568: case 0x1583: case 0x1569: case 0x1584: case 0x156a:
            case 0x15b0: case 0x15b1: case 0x15b2: case 0x15b3: case 0x15b4: case 0x15b5:
                return command == 0x01;
            default: break;
            }
            switch (uic_arg1 >> 16) {
            case 0x1520: case 0x1540: case 0x1561: case 0x1581:
            case 0x1560: case 0x1580:
            case 0x1587: case 0x1586:
                return true;
            default: return false;
            }
        case 0x02:
            if (uic_arg1 & 0xffff) return false;
            switch (uic_arg1 >> 16) {
            case 0xd002: case 0x9007:
            case 0x0037: // Selected per-lane TX equalizer setting, lane zero only.
            case 0x15a7: case 0x15a8: case 0x15d3: case 0x15d4:
            case 0x155e: case 0x155f:
            case 0x1571: case 0x15a4: case 0x1520: case 0x1540: case 0x1560: case 0x1580:
            case 0x1568: case 0x1583: case 0x1569: case 0x1584: case 0x156a:
            case 0x15b0: case 0x15b1: case 0x15b2: case 0x15b3: case 0x15b4: case 0x15b5:
                return true;
            default: return false;
            }
        case 0x04: // The selected timing cohort updates peer PA_TActivate.
            return uic_arg1 == 0x15a80000;
        default: return false;
        }
    }

    bool vendor_transport(tlm::tlm_generic_payload& tx) {
        if (tx.get_address() != 0xc0) return false;
        auto* bytes = tx.get_data_ptr();
        if (tx.is_write()) {
            const uint32_t value = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
                                   (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
            if (value & ~uint32_t{0x7ff}) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return true;
            }
            sys1clk_1us = value;
            sys1clk_programmed = true;
        } else {
            if (!sys1clk_programmed) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return true;
            }
            for (unsigned i = 0; i != 4; ++i) bytes[i] = sys1clk_1us >> (8 * i);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    static QemuInstance& checked_instance(sc_core::sc_object* object) {
        auto* instance = dynamic_cast<QemuInstance*>(object);
        if (!instance) throw std::invalid_argument("UfsController requires a QemuInstance");
        return *instance;
    }

    static bool readable(uint64_t offset) {
        switch (offset) {
        case 0x00: case 0x08: case 0x10: case 0x14: case 0x18:
        case 0x20: case 0x24: case 0x30: case 0x34:
        case 0x38: case 0x3c: case 0x40: case 0x44: case 0x48:
        case 0x50: case 0x54: case 0x58: case 0x5c: case 0x60: case 0x64:
        case 0x70: case 0x74: case 0x78:
        case 0x90: case 0x94: case 0x98: case 0x9c:
        case 0xc0:
        case 0xd8: case 0xdc: case 0xe4: case 0x284c:
            return true;
        default: return false;
        }
    }

    static bool writable(uint64_t offset) {
        switch (offset) {
        case 0x20: case 0x24: case 0x34:
        case 0x50: case 0x54: case 0x58: case 0x60: case 0x64:
        case 0x70: case 0x74: case 0x90: case 0x94: case 0x98: case 0x9c:
        case 0xc0:
        case 0xd8: case 0xdc: case 0x284c:
            return true;
        default: return false;
        }
    }

    static bool valid_shape(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            tx.get_byte_enable_ptr() || !tx.get_data_ptr() || (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if ((!tx.is_read() && !tx.is_write())) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    static bool valid(tlm::tlm_generic_payload& tx) {
        if (!valid_shape(tx)) return false;
        if (!(tx.is_read() ? readable(tx.get_address()) : writable(tx.get_address()))) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    void local_uic_status(tlm::tlm_generic_payload& tx) const {
        if (!virtual_phy.get_value() || !tx.is_read() || tx.get_address() != 0x30 ||
            !tx.is_response_ok()) return;
        // The powered local command engine is independent of the UTP enable.
        // Reset and supply checks already rejected inaccessible transactions.
        const uint32_t value = (load_word(tx) & ~uint32_t{8}) |
                               (source_clocks_available ? 8u : 0u);
        for (unsigned i = 0; i != 4; ++i) tx.get_data_ptr()[i] = value >> (8 * i);
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!valid(tx) || !available(tx)) return;
        if (tx.is_write() && tx.get_address() == 0x90 && !supported_uic(load_word(tx))) {
            std::cerr << "UFS_UIC_UNSUPPORTED model=" << name() << std::hex << " command=0x"
                      << load_word(tx) << " arg1=0x" << uic_arg1 << std::dec
                      << " source_clocks=" << source_clocks_available << ' ' << phy.describe() << '\n';
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (!host_configuration(tx, delay) && !virtual_configuration(tx) && !vendor_transport(tx))
            registers->b_transport(tx, delay);
        local_uic_status(tx);
        if (tx.is_write() && tx.get_address() == 0x94 && tx.is_response_ok()) uic_arg1 = load_word(tx);
        tx.set_dmi_allowed(false);
    }
    unsigned debug(tlm::tlm_generic_payload& tx) {
        if (!valid(tx) || !available(tx)) return 0;
        if (tx.is_write() && tx.get_address() == 0x90 && !supported_uic(load_word(tx))) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return 0;
        }
        auto delay = sc_core::SC_ZERO_TIME;
        const auto count = host_configuration(tx, delay) || virtual_configuration(tx) || vendor_transport(tx) ?
            (tx.is_response_ok() ? 4u : 0u) : registers->transport_dbg(tx);
        local_uic_status(tx);
        if (tx.is_write() && tx.get_address() == 0x94 && tx.is_response_ok()) uic_arg1 = load_word(tx);
        return count;
    }
    void dma_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        synchronize_source_clocks();
        if (!operational() || !phy.ready() || !device_reference_running()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        // Forward the original attributes. Do not infer a security state from a CPU.
        dma_socket->b_transport(tx, delay);
        tx.set_dmi_allowed(false);
    }
    unsigned dma_debug(tlm::tlm_generic_payload& tx) {
        synchronize_source_clocks();
        if (!operational() || !phy.ready() || !device_reference_running()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return 0;
        }
        return dma_socket->transport_dbg(tx);
    }
    bool no_dma_dmi(tlm::tlm_generic_payload&, tlm::tlm_dmi&) { return false; }

    void phy_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!valid_shape(tx)) return;
        synchronize_source_clocks();
        if (!virtual_phy.get_value()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const bool loses_link = phy.removes_readiness(tx);
        if (loses_link && !idle(delay)) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        phy.transport(tx);
        if (loses_link && tx.is_response_ok()) quiesce(delay);
        if (tx.is_write() && tx.is_response_ok()) phy_changed.notify(sc_core::SC_ZERO_TIME);
    }
    unsigned phy_debug(tlm::tlm_generic_payload& tx) {
        auto delay = sc_core::SC_ZERO_TIME;
        phy_transport(tx, delay);
        return tx.is_response_ok() ? 4 : 0;
    }
};
#endif

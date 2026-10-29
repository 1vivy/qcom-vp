// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// GENI programming state shared by protocol-neutral SEs and the FIFO UART.
// SM8850 XBL 0x148a0054 downloads real guest RAM and selects DMA/GSI mode.
// Selected QSEE 0x15715610..0x157158f4 also programs +0x200c, so the retained
// configuration window includes it. The sequence does not poll that word.
// This model does not execute SE microcode or implement SPI/GPI transfers.
class GeniSe : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<GeniSe> se_socket{"se_socket"};
    InitiatorSignalSocket<bool> irq{"irq"};
    cci::cci_param<unsigned> fifo_depth{"fifo_depth", 16, "Virtual 32-bit FIFO depth in words (9..255)"};
    cci::cci_param<std::string> tx_log{"tx_log", "", "Required binary output file for guest TX bytes"};

    explicit GeniSe(sc_core::sc_module_name name) : GeniSe(name, false) {}

protected:
    GeniSe(sc_core::sc_module_name name, bool uart) : sc_module(name), uart_(uart) {
        se_socket.register_b_transport(this, &GeniSe::se_transport);
    }

private:
    const bool uart_;
    std::array<uint32_t, 0x3000 / 4> regs_{};
    std::array<unsigned char, 1020 * 4> ram_{};
    std::ofstream output_;
    uint32_t remaining_ = 0;
    uint32_t transmitted_ = 0;
    uint32_t m_irq_ = 0;
    uint32_t s_irq_ = 0;
    bool m_active_ = false;
    bool s_active_ = false;

    void end_of_elaboration() override {
        if (fifo_depth.get_value() <= 8 || fifo_depth.get_value() > 255)
            throw std::invalid_argument("GENI SE requires FIFO depth 9..255");
        if (!uart_) return;
        if (tx_log.get_value().empty())
            throw std::invalid_argument("GENI UART requires a TX log path");
        output_.exceptions(std::ios::failbit | std::ios::badbit);
        try {
            output_.open(tx_log.get_value(), std::ios::out | std::ios::binary | std::ios::trunc);
        } catch (const std::ios_base::failure& error) {
            throw std::runtime_error("GENI UART cannot open TX log " + tx_log.get_value() + ": " + error.what());
        }
    }
    void start_of_simulation() override { update_irq(); }
    void end_of_simulation() override {
        if (!uart_) return;
        try {
            output_.flush();
            output_.close();
        } catch (const std::ios_base::failure& error) {
            throw std::runtime_error("GENI UART cannot close TX log " + tx_log.get_value() + ": " + error.what());
        }
    }
    static uint32_t load(const unsigned char* data) {
        return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
            (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
    }
    static void store(unsigned char* data, uint32_t value) {
        for (unsigned i = 0; i != 4; ++i) data[i] = value >> (8 * i);
    }
    static bool configuration_register(uint64_t offset) {
        if ((offset >= 0x100 && offset <= 0x148) || (offset >= 0x200 && offset <= 0x2f0)) return true;
        switch (offset) {
        case 0: case 4: case 0x10: case 0x14: case 0x20: case 0x24:
        case 0x28: case 0x2c: case 0x3c: case 0x48: case 0x4c: case 0x7c: case 0x80:
        case 0x614: case 0x644: case 0x808: case 0x80c: case 0x810: case 0x814:
        case 0x900: case 0xc48: case 0xd48: case 0xe14: case 0xe18: case 0xe1c:
        case 0xe30: case 0xe38: case 0x1000: case 0x1004: case 0x2000: case 0x2004: case 0x2008:
        case 0x200c:
            return true;
        default: return false;
        }
    }
    static bool valid(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        const auto offset = tx.get_address();
        const auto size = tx.get_data_length();
        if (offset >= 0x3000u) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        const bool ram = offset >= 0x1010 && offset < 0x2000;
        const bool ram_width = size == 1 || size == 2 || size == 4 || size == 8 || size == 16;
        if (!tx.get_data_ptr() || tx.get_streaming_width() < size ||
            (ram ? (!ram_width || offset % size || size > 0x2000 - offset) : (size != 4 || offset % 4))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        return true;
    }
    uint32_t m_status() const {
        return m_irq_ | ((m_active_ && regs_[0x80c / 4]) ? 0x40000000u : 0) |
            ((s_irq_ & regs_[0x644 / 4]) ? 0x80000000u : 0);
    }
    void update_irq() {
        const auto enabled = regs_[0xe1c / 4];
        const bool pending = ((enabled & 4) && (m_status() & regs_[0x614 / 4])) ||
            ((enabled & 8) && (s_irq_ & regs_[0x644 / 4])) ||
            ((enabled & 2) && (regs_[0xc40 / 4] & regs_[0xc48 / 4])) ||
            ((enabled & 1) && (regs_[0xd40 / 4] & regs_[0xd48 / 4]));
        // Publish the level before returning from MMIO. Delaying this update
        // lets the CPU EOI an interrupt before its mask/W1C reaches the GIC.
        for (int port = 0; port < irq.size(); ++port) irq[port]->write(pending);
    }
    bool read_register(uint64_t offset, uint32_t& value) const {
        switch (offset) {
        case 0x600: case 0x630: value = regs_[offset / 4]; return true;
        // Linux polls S_GENI_CMD_ABORT clear in SE_GENI_S_CMD_CTRL_REG after an
        // RX abort. Each abort and cancel request completes before the write
        // returns, so the request bits already self-cleared in the hardware.
        case 0x604: case 0x634: value = 0; return true;
        case 0x40: value = unsigned(m_active_) | (s_active_ ? 0x1000 : 0); return true;
        case 0x610: value = m_status(); return true;
        case 0x640: value = s_irq_; return true;
        case 0xc40: case 0xd40: value = regs_[offset / 4]; return true;
        case 0x910: value = transmitted_; return true;
        case 0x800: case 0x804: case 0x908: case 0x624: case 0x628: case 0xe2c:
            value = 0; return true;
        case 0xe24: case 0xe28: value = (32u << 24) | (fifo_depth.get_value() << 16); return true;
        case 0x60: offset = 0x2000; break;
        case 0x64: offset = 0x2008; break;
        case 0x68: offset = 0x1000; break;
        case 0x6c: offset = 0x1004; break;
        case 0xe20: offset = 0x2004; break;
        default: break;
        }
        if (!configuration_register(offset)) return false;
        value = regs_[offset / 4];
        return true;
    }
    bool command(bool secondary, uint32_t value) {
        if (!uart_ || value != 0x08000000 ||
            ((regs_[(secondary ? 0x1004 : 0x1000) / 4] >> 8) & 0xff) != 2 ||
            !(regs_[0x2000 / 4] & 1) || (regs_[0x2008 / 4] & 1) ||
            (regs_[0x258 / 4] & 1) || regs_[0xe18 / 4]) return false;
        if (secondary) {
            if (s_active_) return false;
            s_active_ = true;
        } else {
            if (m_active_ || regs_[0x268 / 4] != 8 || regs_[0x260 / 4] != 0x4380e ||
                regs_[0x264 / 4] != 0xc3e0e) return false;
            remaining_ = regs_[0x270 / 4];
            transmitted_ = 0;
            m_active_ = remaining_ != 0;
            if (!m_active_) m_irq_ |= 1;
        }
        regs_[(secondary ? 0x630 : 0x600) / 4] = value;
        return true;
    }
    // Linux writes one control request per access: ABORT is bit 1 and CANCEL is
    // bit 2. It then polls the matching CMD_ABORT_EN or CMD_CANCEL_EN status bit
    // in the M or S IRQ status register. Both requests finish before the write
    // returns, clear the active command, and latch that one status bit.
    bool control(bool secondary, uint32_t value) {
        if (!uart_) return false;
        if (value != 0 && value != 1 && value != 2 && value != 4) return false;
        if (!value) return true;
        const auto event = value == 2 ? 0x20u : value == 4 ? 0x10u : 0;
        if (secondary) { s_active_ = false; s_irq_ |= event; }
        else { m_active_ = false; remaining_ = 0; m_irq_ |= event; }
        return true;
    }
    bool write_register(uint64_t offset, uint32_t value) {
        switch (offset) {
        case 0x600: case 0x630: return command(offset == 0x630, value);
        case 0x604: case 0x634: return control(offset == 0x634, value);
        case 0x618: m_irq_ &= ~value; return true;
        case 0x648: s_irq_ &= ~value; return true;
        // Linux geni_se_irq_clear clears both DMA banks before FIFO setup.
        // DMA requests remain unsupported, so neither bank has an event source.
        case 0xc44: case 0xd44: regs_[(offset - 4) / 4] &= ~value; return true;
        case 0x61c: case 0x620: case 0x64c: case 0x650:
        case 0xc4c: case 0xc50: case 0xd4c: case 0xd50: {
            const bool set = (offset & 0xf) == 0xc;
            const auto target = offset - (offset < 0x700 ? (set ? 8 : 12) : (set ? 4 : 8));
            if (set) regs_[target / 4] |= value;
            else regs_[target / 4] &= ~value;
            return true;
        }
        default: break;
        }
        if (offset >= 0x700 && offset < 0x780) {
            if (!m_active_) return false;
            unsigned char bytes[4];
            store(bytes, value);
            const auto count = std::min(4u, remaining_);
            try {
                output_.write(reinterpret_cast<const char*>(bytes), count);
                output_.flush();
            } catch (const std::ios_base::failure& error) {
                throw std::runtime_error("GENI UART TX log failure " + tx_log.get_value() + ": " + error.what());
            }
            remaining_ -= count;
            transmitted_ += count;
            if (!remaining_) { m_active_ = false; m_irq_ |= 1; }
            return true;
        }
        // Mode selection is configuration, not a transfer or a completion.
        if (!configuration_register(offset)) return false;
        regs_[offset / 4] = value;
        return true;
    }
    void se_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        const auto offset = tx.get_address();
        if (offset >= 0x1010 && offset < 0x2000) {
            auto* ram = ram_.data() + offset - 0x1010;
            if (tx.is_write()) std::memcpy(ram, tx.get_data_ptr(), tx.get_data_length());
            else std::memcpy(tx.get_data_ptr(), ram, tx.get_data_length());
        } else {
            uint32_t value = 0;
            if (tx.is_write()) {
                if (!write_register(offset, load(tx.get_data_ptr()))) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                update_irq();
            } else {
                if (!read_register(offset, value)) {
                    tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                    return;
                }
                store(tx.get_data_ptr(), value);
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

class GeniUart : public GeniSe {
public:
    explicit GeniUart(sc_core::sc_module_name name) : GeniSe(name, true) {}
};

// One instance owns each physical wrapper, including the console wrapper.
// XBL 0x1489f320 reads +4 and updates +0x120, +0x118, and +0x21c.
class GeniCommon : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<GeniCommon> target_socket{"target_socket"};
    // The phone kernel reports HW Ver 0x40030000 for SM8850 QUP wrappers.
    // See gobbl-lab record 20261026T003332Z-phone-qup-hw-version.
    cci::cci_param<unsigned> hw_version{"hw_version", 0x40030000,
        "GENI wrapper revision reported by the SM8850 phone kernel"};

    explicit GeniCommon(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &GeniCommon::transport);
    }

private:
    std::array<uint32_t, 0x1000 / 4> regs_{};

    static bool writable(uint64_t offset) {
        switch (offset) {
        case 0x100: case 0x104: case 0x108: case 0x114: case 0x118:
        case 0x120: case 0x128: case 0x12c: case 0x134: case 0x21c: return true;
        default: return false;
        }
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const auto offset = tx.get_address();
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || offset % 4) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (!writable(offset) && !(tx.is_read() && offset == 4)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* data = tx.get_data_ptr();
        if (tx.is_read()) {
            const auto value = offset == 4 ? hw_version.get_value() : regs_[offset / 4];
            for (unsigned i = 0; i != 4; ++i) data[i] = value >> (8 * i);
        } else {
            regs_[offset / 4] = uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
                (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

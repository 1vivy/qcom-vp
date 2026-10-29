// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <array>
#include <bitset>
#include <cstdint>
#include <stdexcept>

namespace qcom_vp {

// qcom,gsi-v1: OEM bootstrap controls only. All socket addresses are relative.
// Evidence: XBL loaders SM8850 0x148a0554 and SM8845 0x1489ee28. Selected TZ
// loaders in IMAGES/tz.mbn: SM8850 0x157234f0 and SM8845 0x15717c54.
// The selected QSI is the segment at file offset 0x1000 in IMAGES/qupv3fw.elf.
// Its virtual address is 0x82461000. SM8850 has 0xa49 instruction words and ten
// configuration words, total 0x52b0 bytes. SM8845 has 0xa46 and ten, total
// 0x5298 bytes. The instruction copy starts at header +0x68. Each configuration
// entry holds a 32-bit offset and a 32-bit value.
// XBL writes ten config words, the first instruction word at +8, RAM at
// +0x4c000, +0xb000 bit0, +0=5, +0xf400=0, +0xf018=0x61, then +0xf018=0x81
// until +0xf400 is nonzero.
// TZ clears bit1 at +0x60, writes the ten config words, writes the first
// instruction word at +8, copies RAM to +0x4c000, sets +0xb000 bit0, writes
// +0 as 1 or 5, writes 0 at +0xf400, and waits for +0xf400 after writing 0x81
// at +0xf018. After +0xf400 is nonzero, TZ writes bit1 then bit0 at +0xb008.
// The +0 value is 5 for the selected wrappers. Their version word 0x40030000
// does not select the 1 branch.
// Idealization: the final mailbox request completes initialization synchronously
// after the real download and ordered controls. Microcode is retained, not
// interpreted or authenticated here. This is not a GPI transfer engine.
// Public later-channel protocol: https://github.com/torvalds/linux/blob/master/drivers/dma/qcom/gpi.c
// GPII contexts, ring commands, doorbells, and all unknown controls are rejected.
class GsiController : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<GsiController> target_socket{"target_socket"};
    tlm_utils::multi_passthrough_target_socket<GsiController> firmware_socket{"firmware_socket"};
    cci::cci_param<unsigned> firmware_size{"firmware_size", 0,
        "Required downloaded firmware extent in bytes, aligned to eight, maximum 0x6000"};

    explicit GsiController(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &GsiController::control_transport);
        firmware_socket.register_b_transport(this, &GsiController::firmware_transport);
    }

    // Platform cold reset removes all guest state. Mailbox clear below retains
    // the running image and permits another initialization request.
    void cold_reset() {
        config_.fill(0);
        config_written_.reset();
        ram_.fill(0);
        downloaded_.reset();
        downloaded_bytes_ = 0;
        first_word_ = control_ = clock_ = mailbox_ = ready_ = 0;
        prestart_ = postready_ = 0;
        first_written_ = started_ = cleared_ = false;
        postready_bit1_ = postready_done_ = false;
    }

private:
    static constexpr unsigned capacity = 0x6000;
    static constexpr std::array<unsigned, 10> config_offsets{
        0x400, 0x418, 0x424, 0x428, 0x404, 0x42c, 0x44c, 0x420, 0x430, 0x43c};
    std::array<std::uint32_t, 10> config_{};
    std::bitset<10> config_written_;
    std::array<unsigned char, capacity> ram_{};
    std::bitset<capacity> downloaded_;
    unsigned downloaded_bytes_ = 0;
    std::uint32_t first_word_ = 0, control_ = 0, clock_ = 0, mailbox_ = 0, ready_ = 0;
    std::uint32_t prestart_ = 0, postready_ = 0;
    bool first_written_ = false, started_ = false, cleared_ = false;
    bool postready_bit1_ = false, postready_done_ = false;

    void end_of_elaboration() override {
        if (!firmware_size.get_value() || firmware_size.get_value() > capacity ||
            (firmware_size.get_value() & 7))
            throw std::invalid_argument("GSI requires firmware_size in 8..0x6000 bytes, aligned to eight");
    }

    static std::uint32_t load_word(const unsigned char* p) {
        return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
            (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
    }

    bool image_complete() const {
        return config_written_.all() && first_written_ &&
            downloaded_bytes_ == firmware_size.get_value() && first_word_ == load_word(ram_.data());
    }

    bool access(unsigned offset, std::uint32_t& value, bool write) {
        for (unsigned i = 0; i < config_offsets.size(); ++i) {
            if (offset != config_offsets[i]) continue;
            if (write) {
                if (started_) return false;
                config_[i] = value;
                config_written_.set(i);
            } else value = config_[i];
            return true;
        }
        switch (offset) {
        case 0:
            if (!write) value = control_;
            else {
                if (value != 5 || !(clock_ & 1) || !image_complete()) return false;
                control_ = value;
                started_ = true;
                ready_ = 0;
                postready_ = 0;
                cleared_ = false;
                postready_bit1_ = postready_done_ = false;
            }
            return true;
        case 0x60:
            if (!write) value = prestart_;
            else {
                // Selected TZ clears bit1 before configuration.
                if (started_ || (value & 2)) return false;
                prestart_ = value;
            }
            return true;
        case 8:
            if (!write) value = first_word_;
            else {
                if (started_) return false;
                first_word_ = value;
                first_written_ = true;
            }
            return true;
        case 0xb000:
            if (!write) value = clock_;
            else {
                // Only bit0 is established by the selected read-modify-write.
                if (value & ~1U) return false;
                if (!(value & 1) && started_) return false;
                clock_ = value;
            }
            return true;
        case 0xb008:
            if (!write) value = postready_;
            else {
                // Selected TZ sets bit1 then bit0 after the mailbox reports ready.
                if (!started_ || mailbox_ != 0x81 || !ready_) return false;
                if (!postready_bit1_) {
                    if ((value & 3) != 2) return false;
                    postready_ = value;
                    postready_bit1_ = true;
                } else {
                    if (postready_done_ || (value & 3) != 3) return false;
                    postready_ = value;
                    postready_done_ = true;
                }
            }
            return true;
        case 0xf400:
            if (!write) value = ready_;
            else {
                if (value != 0) return false;
                ready_ = 0;
                cleared_ = started_;
                postready_ = 0;
                postready_bit1_ = postready_done_ = false;
            }
            return true;
        case 0xf018:
            if (!write) value = mailbox_;
            else {
                if (!started_ || !cleared_) return false;
                if (value == 0x61) {
                    // XBL arms the mailbox with 0x61 before its 0x81 request.
                    if (ready_ || mailbox_ == 0x61) return false;
                    ready_ = 0;
                } else if (value == 0x81) {
                    // Selected TZ writes 0x81 without the 0x61 arm step, and
                    // repeats 0x81 while +0xf400 reads zero.
                    ready_ = 1;
                } else return false;
                mailbox_ = value;
            }
            return true;
        default: return false;
        }
    }

    static bool valid_transaction(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        if (!tx.get_data_ptr() || !tx.get_data_length() ||
            tx.get_streaming_width() < tx.get_data_length()) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    void control_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid_transaction(tx)) return;
        if (tx.get_data_length() != 4 || (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() >= 0x10000) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto* p = tx.get_data_ptr();
        auto value = load_word(p);
        const bool ok = access(static_cast<unsigned>(tx.get_address()), value, tx.is_write());
        if (ok && tx.is_read())
            for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(ok ? tlm::TLM_OK_RESPONSE : tlm::TLM_COMMAND_ERROR_RESPONSE);
    }

    void firmware_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid_transaction(tx)) return;
        const auto offset = tx.get_address();
        const auto length = tx.get_data_length();
        if (offset >= capacity || length > capacity - offset) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write() && started_) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        auto* p = tx.get_data_ptr();
        for (unsigned i = 0; i < length; ++i) {
            const auto index = static_cast<unsigned>(offset) + i;
            if (tx.is_read()) p[i] = ram_[index];
            else {
                ram_[index] = p[i];
                if (index < firmware_size.get_value() && !downloaded_.test(index)) {
                    downloaded_.set(index);
                    ++downloaded_bytes_;
                }
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};
} // namespace qcom_vp

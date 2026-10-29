// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <ports/initiator-signal-socket.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace qcom_vp {

// ACEHR request-controller register block and descriptor store.
// The selected CPUCP images configure two instances whose register block and
// descriptor store form one contiguous 32 KiB aperture:
//
//   base + 0x0000..0x3fff  control and trigger registers
//   base + 0x4000..0x7fff  1024 retained 16-byte descriptor slots
//
// Queue and request words publish work. A rising channel start bit executes it.
// Interrupt masks do not gate DMA. Completion requires all bus effects.
class QcomCpucpAcehr : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomCpucpAcehr> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket_optional<QcomCpucpAcehr> dma_socket{"dma_socket"};
    InitiatorSignalSocket<bool> irq{"irq"};
    cci::cci_param<uint64_t> dma_beat_hz{
        "dma_beat_hz", 19200000, "Virtual DMA bus frequency"};

    static constexpr uint64_t control_bytes = 0x4000;
    static constexpr uint64_t ring_bytes = 0x4000;
    static constexpr uint64_t aperture_bytes = control_bytes + ring_bytes;
    static constexpr unsigned channel_count = 64;
    static constexpr unsigned descriptor_bytes = 16;
    static constexpr unsigned descriptor_slots = ring_bytes / descriptor_bytes;
    static constexpr uint64_t kick_offset = 0x3e00;
    SC_HAS_PROCESS(QcomCpucpAcehr);

    explicit QcomCpucpAcehr(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomCpucpAcehr::transport);
        SC_METHOD(publish_completions);
        sensitive << completion_event_;
        dont_initialize();
    }
    // External hardware may trigger a configured channel. No event source is
    // synthesized by this model; callers must supply the real event.
    bool trigger_external(unsigned channel, sc_core::sc_time& delay) {
        if (channel >= channel_count) return false;
        return execute(channel, delay);
    }

    // Cold state. The selected initializer writes only the configuration word.
    void cold_reset() {
        channel_state_.fill(0);
        queue_index_.fill(0);
        queue_valid_.fill(false);
        trigger_.fill(0);
        config_ = 0;
        completion_ = 0;
        engine_state_ = 0;
        pending_completion_ = 0;
        completion_event_.cancel();
        release_ = 0;
        wake_ = 0;
        error_capture_.fill(0);
        service_ = 0;
        descriptor_.fill(0);
        descriptor_written_.fill(0);
        drive_irq();
    }

private:
    // base+0x004+4*n: per-channel trigger word, 64 entries.
    std::array<uint32_t, static_cast<std::size_t>(channel_count)> channel_state_{};
    // +0x204/+0x208 retain request bits; +0x20c/+0x210 mask service interrupts.
    std::array<uint32_t, 4> trigger_{};
    // base+0x21c: controller configuration. The initializer writes 0xf.
    uint32_t config_{};
    // base+0x560: engine completion bitmap. Only an executed request may set it.
    uint64_t completion_{};
    // base+0x570: service-pending bitmap from completed transfers.
    uint64_t engine_state_{};
    // base+0x588: trigger release value written by the service path.
    uint64_t release_{};
    // base+0x590: wake/validity bitmap read by the error path.
    uint32_t wake_{};
    // base+0x594..+0x5a8: captured write/read response and translation addresses.
    std::array<uint32_t, 6> error_capture_{};
    // base+0x5ac: service word written by the error path.
    uint32_t service_{};
    // base+0x4000..+0x7fff: descriptor store.
    std::array<uint8_t, static_cast<std::size_t>(ring_bytes)> descriptor_{};
    std::array<uint16_t, descriptor_slots> descriptor_written_{};
    std::array<uint32_t, channel_count> queue_index_{};
    std::array<bool, channel_count> queue_valid_{};
    uint64_t pending_completion_ = 0;
    std::array<sc_core::sc_time, channel_count> completion_deadline_{};
    sc_core::sc_event completion_event_;
    void publish_completions() {
        const auto now = sc_core::sc_time_stamp();
        auto next = sc_core::sc_max_time();
        for (unsigned channel = 0; channel < channel_count; ++channel) {
            const auto bit = uint64_t{1} << channel;
            if (!(pending_completion_ & bit)) continue;
            if (completion_deadline_[channel] <= now) {
                pending_completion_ &= ~bit;
                engine_state_ |= bit;
            } else next = std::min(next, completion_deadline_[channel]);
        }
        if (next != sc_core::sc_max_time()) completion_event_.notify(next - now);
        drive_irq();
    }

    void end_of_elaboration() override {
        if (irq.size() > 1)
            throw std::invalid_argument("ACEHR requires at most one IRQ consumer");
        if (!dma_beat_hz.get_value() || dma_beat_hz.get_value() > 1000000000)
            throw std::invalid_argument("DMA bus frequency must be in 1..1000000000 Hz");
        dma_beat_hz.lock();
    }
    void start_of_simulation() override { drive_irq(); }
    void drive_irq() {
        if (!irq.size()) return;
        const uint64_t mask = (uint64_t{trigger_[3]} << 32) | trigger_[2];
        irq->write((engine_state_ & mask) != 0);
    }

    uint64_t descriptor_word(unsigned slot, unsigned word) const {
        uint64_t value = 0;
        const unsigned offset = slot * descriptor_bytes + word * 8;
        for (unsigned i = 0; i < 8; ++i)
            value |= uint64_t{descriptor_[offset + i]} << (8 * i);
        return value;
    }
    bool dma_access(tlm::tlm_generic_payload& tx, tlm::tlm_command command,
                    uint64_t address, sc_core::sc_time& delay) {
        tx.set_command(command);
        tx.set_address(address);
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        dma_socket->b_transport(tx, delay);
        if (tx.is_response_ok()) return true;
        std::fprintf(stderr, "ACEHR_DMA_ACCESS_FAILED model=%s address=0x%llx width=%u operation=%s status=%d\n",
                     name(), static_cast<unsigned long long>(address), tx.get_data_length(),
                     tx.is_read() ? "read" : "write", static_cast<int>(tx.get_response_status()));
        return false;
    }
    bool execute(unsigned channel, sc_core::sc_time& delay) {
        if (!queue_valid_[channel] || !dma_socket.size()) {
            SC_REPORT_WARNING(name(), "ACEHR trigger has no published descriptor or DMA bus");
            return false;
        }
        const unsigned first = queue_index_[channel] & 0xffff;
        const unsigned last = queue_index_[channel] >> 16;
        if (first > last || last >= descriptor_slots) {
            SC_REPORT_WARNING(name(), "ACEHR queue bounds exceed the declared descriptor ring");
            return false;
        }
        QemuMemTxAttrsExtension attributes;
        attributes.attrs.secure = true;
        attributes.attrs.unspecified = false;
        attributes.attrs.user = false;
        attributes.attrs.requester_id = 0;
        unsigned char data[8]{};
        tlm::tlm_generic_payload tx;
        tx.set_extension(&attributes);
        tx.set_data_ptr(data);
        tx.set_dmi_allowed(false);
        uint64_t dma_beats = 0;
        for (unsigned slot = first; slot <= last; ++slot) {
            if (descriptor_written_[slot] != UINT16_MAX) {
                tx.clear_extension(&attributes);
                SC_REPORT_WARNING(name(), "ACEHR queue references an incomplete descriptor");
                return false;
            }
            const uint64_t command = descriptor_word(slot, 0);
            const uint64_t destination_word = descriptor_word(slot, 1);
            const unsigned mode = (command >> 12) & 7;
            const unsigned width = 1u << ((command >> 15) & 3);
            const unsigned count = (command & 0xfff) + 1;
            const uint64_t source = (command >> 19) & UINT64_C(0xffffffff);
            const uint64_t destination = ((command >> 59) & 0x1f) |
                                         ((destination_word & 0x7ffffff) << 5);
            const uint64_t bytes = uint64_t{count} * width;
            if (mode != 0 || (destination_word >> 27) || ((command >> 51) & 0xff) ||
                bytes > 0x8000 || ((source | destination) & (width - 1)) ||
                source + bytes > UINT64_C(0x100000000) ||
                destination + bytes > UINT64_C(0x100000000)) {
                tx.clear_extension(&attributes);
                SC_REPORT_WARNING(name(), "ACEHR descriptor mode, address, or transfer geometry is unsupported");
                return false;
            }
            tx.set_data_length(width);
            tx.set_streaming_width(width);
            for (unsigned i = 0; i < count; ++i) {
                if (!dma_access(tx, tlm::TLM_READ_COMMAND, source + uint64_t{i} * width, delay) ||
                    !dma_access(tx, tlm::TLM_WRITE_COMMAND, destination + uint64_t{i} * width, delay)) {
                    tx.clear_extension(&attributes);
                    // Report the complete published range so that one failure names its transfer set.
                    for (unsigned listed = first; listed <= last; ++listed) {
                        const uint64_t listed_command = descriptor_word(listed, 0);
                        const uint64_t listed_destination = descriptor_word(listed, 1);
                        std::fprintf(stderr,
                                     "ACEHR_DMA_FAILED_DESCRIPTOR model=%s channel=%u slot=%u source=0x%llx "
                                     "destination=0x%llx width=%u count=%u\n",
                                     name(), channel, listed,
                                     static_cast<unsigned long long>((listed_command >> 19) & UINT64_C(0xffffffff)),
                                     static_cast<unsigned long long>(((listed_command >> 59) & 0x1f) |
                                                                     ((listed_destination & 0x7ffffff) << 5)),
                                     1u << ((listed_command >> 15) & 3),
                                     static_cast<unsigned>((listed_command & 0xfff) + 1));
                    }
                    SC_REPORT_WARNING(name(), "ACEHR DMA read or write failed; completion was not published");
                    return false;
                }
            }
            dma_beats += uint64_t{count} * 2;
        }
        tx.clear_extension(&attributes);
        // Data completion is visible before the asynchronous service interrupt.
        completion_ |= uint64_t{1} << channel;
        pending_completion_ |= uint64_t{1} << channel;
        const auto service_delay = delay +
            sc_core::sc_time(static_cast<double>(dma_beats) / dma_beat_hz.get_value(), sc_core::SC_SEC) +
            sc_core::sc_time(1, sc_core::SC_US);
        completion_deadline_[channel] = sc_core::sc_time_stamp() + service_delay;
        completion_event_.notify(service_delay);
        return true;
    }

    static bool valid(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        const unsigned width = tx.get_data_length();
        if (tx.get_byte_enable_ptr()) tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        else if (!tx.get_data_ptr() || (width != 4 && width != 8) ||
                 tx.get_streaming_width() < width || (tx.get_address() & (width - 1)))
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        else if (!tx.is_read() && !tx.is_write())
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        else return true;
        return false;
    }
    static uint64_t load(const tlm::tlm_generic_payload& tx) {
        uint64_t value = 0;
        for (unsigned i = 0; i < tx.get_data_length(); ++i)
            value |= uint64_t{tx.get_data_ptr()[i]} << (8 * i);
        return value;
    }
    static void result(tlm::tlm_generic_payload& tx, uint64_t value) {
        for (unsigned i = 0; i < tx.get_data_length(); ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void reject(tlm::tlm_generic_payload& tx, tlm::tlm_response_status status,
                const char* message) {
        SC_REPORT_WARNING(name(), message);
        tx.set_response_status(status);
    }
    static bool width_is(tlm::tlm_generic_payload& tx, unsigned width) {
        return tx.get_data_length() == width;
    }
    void rw32(tlm::tlm_generic_payload& tx, uint32_t& word) {
        if (!width_is(tx, 4)) {
            reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE,
                   "ACEHR register supports 32-bit accesses only");
            return;
        }
        if (tx.is_write()) word = static_cast<uint32_t>(load(tx));
        result(tx, word);
    }
    void ro32(tlm::tlm_generic_payload& tx, uint32_t value) {
        if (!width_is(tx, 4)) {
            reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE,
                   "ACEHR register supports 32-bit accesses only");
            return;
        }
        if (tx.is_write()) {
            reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE,
                   "ACEHR engine-owned status is read-only");
            return;
        }
        result(tx, value);
    }
    void rw64(tlm::tlm_generic_payload& tx, uint64_t& word) {
        if (!width_is(tx, 8)) {
            reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE,
                   "ACEHR register supports 64-bit accesses only");
            return;
        }
        if (tx.is_write()) word = load(tx);
        result(tx, word);
    }
    void ro64(tlm::tlm_generic_payload& tx, uint64_t value) {
        if (!width_is(tx, 8)) {
            reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE,
                   "ACEHR register supports 64-bit accesses only");
            return;
        }
        if (tx.is_write()) {
            reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE,
                   "ACEHR engine-owned status is read-only");
            return;
        }
        result(tx, value);
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!valid(tx)) return;
        const auto address = tx.get_address();
        if (address >= aperture_bytes) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (address >= control_bytes) {
            // Descriptor SRAM retains guest stores; a real trigger reads slots.
            const auto offset = static_cast<std::size_t>(address - control_bytes);
            if (tx.is_write()) {
                const unsigned slot = static_cast<unsigned>(offset / descriptor_bytes);
                if (offset % descriptor_bytes == 0) descriptor_written_[slot] = 0;
                for (unsigned i = 0; i < tx.get_data_length(); ++i) {
                    descriptor_[offset + i] = tx.get_data_ptr()[i];
                    descriptor_written_[slot] |= uint16_t{1} << ((offset + i) % descriptor_bytes);
                }
            } else {
                for (unsigned i = 0; i < tx.get_data_length(); ++i)
                    tx.get_data_ptr()[i] = descriptor_[offset + i];
            }
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (address >= kick_offset && address < kick_offset + 4 * channel_count) {
            if (!width_is(tx, 4)) {
                reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE,
                       "ACEHR queue index supports 32-bit accesses only");
                return;
            }
            const unsigned channel = static_cast<unsigned>((address - kick_offset) / 4);
            if (tx.is_write()) {
                const auto value = static_cast<uint32_t>(load(tx));
                const unsigned first = value & 0xffff;
                const unsigned last = value >> 16;
                if (first > last || last >= descriptor_slots) {
                    reject(tx, tlm::TLM_COMMAND_ERROR_RESPONSE,
                           "ACEHR queue bounds exceed the declared descriptor ring");
                    return;
                }
                queue_index_[channel] = value;
                queue_valid_[channel] = true;
            }
            result(tx, queue_index_[channel]);
            return;
        }
        if (address >= 0x004 && address < 0x004 + 4 * channel_count) {
            const unsigned channel = static_cast<unsigned>((address - 0x004) / 4);
            const auto previous = channel_state_[channel];
            rw32(tx, channel_state_[channel]);
            if (tx.is_write() && tx.is_response_ok()) {
                if (channel_state_[channel] & 1) {
                    if (!(previous & 1) && !execute(channel, delay)) {
                        channel_state_[channel] = previous;
                        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    }
                } else {
                    const uint64_t bit = uint64_t{1} << channel;
                    completion_ &= ~bit;
                    engine_state_ &= ~bit;
                    pending_completion_ &= ~bit;
                    drive_irq();
                }
            }
            return;
        }
        switch (address) {
        case 0x204:
        case 0x208:
        case 0x20c:
        case 0x210: {
            const unsigned index = static_cast<unsigned>((address - 0x204) / 4);
            rw32(tx, trigger_[index]);
            if (!tx.is_response_ok() || !tx.is_write()) return;
            drive_irq();
            return;
        }
        case 0x21c: rw32(tx, config_); return;
        case 0x560: ro64(tx, completion_); return;
        case 0x570: ro64(tx, engine_state_); return;
        case 0x588:
            rw64(tx, release_);
            if (tx.is_write() && tx.is_response_ok()) {
                engine_state_ &= ~release_;
                pending_completion_ &= ~release_;
                drive_irq();
            }
            return;
        case 0x590: ro32(tx, wake_); return;
        case 0x594: ro32(tx, error_capture_[0]); return;
        case 0x598: ro32(tx, error_capture_[1]); return;
        case 0x59c: ro32(tx, error_capture_[2]); return;
        case 0x5a0: ro32(tx, error_capture_[3]); return;
        case 0x5a4: ro32(tx, error_capture_[4]); return;
        case 0x5a8: ro32(tx, error_capture_[5]); return;
        case 0x5ac: rw32(tx, service_); return;
        default: break;
        }
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include "stream.h"
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace qcom_vp {

// Register file and descriptor engine of one BAM with the Linux qcom,bam-v2.0.0
// layout. See models/qcom/bam/evidence.md.
//
// A pipe consumes the 8-byte descriptors that software writes to its ring. Each
// accepted descriptor moves real bytes: a data descriptor transfers through
// stream_socket and RAM through dma_socket, and a command descriptor carries an
// array of command elements whose opcode 4 entries write registers named by
// command_base through command_socket. The command path is physical and bypasses
// any translation in front of dma_socket. Completion, pipe status, sideband state
// and the per-EE interrupt outputs follow only from an accepted descriptor. A ring
// that cannot be read, an unbound endpoint, a failed transfer and an unknown
// command opcode latch a pipe error and never report completion.
class QcomBamV2 : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomBamV2> target_socket{"target_socket"};
    // Optional descriptor, data and command payload bus. This socket carries only
    // ring and RAM traffic. A descriptor that needs memory while it is unbound
    // latches a visible pipe error.
    tlm_utils::simple_initiator_socket_optional<QcomBamV2> dma_socket{"dma_socket"};
    // Optional physical register bus for opcode 4 command elements. It never
    // receives ring or payload traffic, and command writes never use
    // dma_socket. An unbound command socket fails the pipe without completion.
    tlm_utils::simple_initiator_socket_optional<QcomBamV2> command_socket{"command_socket"};
    // Optional pipe endpoint. The payload address is the pipe index. TLM_WRITE
    // moves RAM bytes to the endpoint and TLM_READ moves endpoint bytes to RAM.
    // The endpoint reports the accepted byte count in BamStreamExtension.
    tlm_utils::simple_initiator_socket_optional<QcomBamV2> stream_socket{"stream_socket"};
    // Endpoint capacity input. Each false-to-true change retries every
    // backpressured pipe. It never gates a descriptor that made progress.
    TargetSignalSocket<bool> stream_ready{"stream_ready"};
    // Pipe interrupt outputs, one per execution environment. Bind irq_0..irq_7.
    // An output is a level that follows the enabled pending pipe status.
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq{"irq", 8};
    cci::cci_param<unsigned> num_pipes{"num_pipes", 0, "BAM_NUM_PIPES pipe count (1..32)"};
    cci::cci_param<unsigned> num_ees{"num_ees", 0, "BAM_REVISION execution environment count (1..8)"};
    cci::cci_param<unsigned> revision{"revision", 0, "Virtual BAM_REVISION revision field (1..255)"};
    cci::cci_param<unsigned> address_bits{
        "address_bits", 32, "Descriptor address width: 32 or 36 bits"};
    cci::cci_param<std::uint64_t> command_base{
        "command_base", 0, "Absolute base for opcode 4 command writes; 0 fails a command visibly"};

    SC_HAS_PROCESS(QcomBamV2);
    explicit QcomBamV2(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomBamV2::transport);
        stream_ready.register_value_changed_cb([this](const bool& ready) {
            if (ready) retry_event_.notify(sc_core::SC_ZERO_TIME);
        });
        SC_METHOD(retry_pending);
        sensitive << retry_event_;
        dont_initialize();
    }

    void cold_reset() {
        ctrl_ = 0;
        reset_state();
    }

private:
    static constexpr std::uint32_t ee_base = 0x3000, pipe_base = 0xc000, page = 0x1000;
    static constexpr std::uint32_t bam_sw_rst = 1u << 0, bam_en = 1u << 1;
    static constexpr std::uint32_t pipe_en = 1u << 1, pipe_rst = 1u << 0, pipe_stop = 1u << 0;
    // P_DIRECTION: set means the endpoint is the source, so data moves to RAM.
    static constexpr std::uint32_t pipe_direction = 1u << 3;
    // Descriptor flag byte at word 1 bits 31:24. Bit 0 selects 64 KiB size units,
    // as the QSEE descriptor writer sets at 0x157bfe04. The other bits keep the
    // SPS positions inside that byte.
    static constexpr std::uint8_t desc_size_unit = 0x01, desc_unlock = 0x02, desc_lock = 0x04,
                                  desc_cmd = 0x08, desc_nwd = 0x10, desc_eob = 0x20,
                                  desc_eot = 0x40, desc_int = 0x80;
    static constexpr std::uint32_t size_unit_bytes = 0x10000;
    static constexpr unsigned descriptor_bytes = 8, command_element_bytes = 8;
    // Pipe interrupt status bits.
    static constexpr std::uint32_t pipe_irq_processed = 0x1, pipe_irq_out_of_desc = 0x8,
                                  pipe_irq_error = 0x10, pipe_irq_transfer_end = 0x20,
                                  pipe_irq_hresp_error = 0x80;
    // BAM interrupt status bits.
    static constexpr std::uint32_t global_irq_hresp = 0x2, global_irq_error = 0x4,
                                  global_irq_empty = 0x8, global_irq_timer = 0x10;
    static constexpr std::uint32_t bam_irq_bit = 0x80000000u;
    // Sideband fields. The model has no data FIFO, so the byte counts stay zero.
    // The control field reports real stream backpressure, and the toggle follows
    // completed end-of-transfer packets.
    static constexpr std::uint32_t sideband_updated = 0x1000000, sideband_toggle = 0x100000,
                                  sideband_ctrl_backpressure = 0x20000;
    static constexpr std::uint32_t command_write = 4;
    static constexpr unsigned scratch_bytes = 4096;

    struct Pipe {
        std::uint32_t ctrl = 0, halt = 0, irq_en = 0;
        // Retained words at +0x81c..0x83c, which hold FIFO addresses, sizes and thresholds.
        std::array<std::uint32_t, 9> config{};
        std::uint32_t trust = 0;
        // +0x914/+0x924/+0x934 high address parts of the descriptor ring, data
        // FIFO and event destination.
        std::uint32_t desc_msb = 0, data_msb = 0, event_msb = 0;
        // +0x800 consumer byte offset, and the producer offset from +0x818.
        std::uint32_t consumer = 0, peer = 0;
        std::uint32_t irq_status = 0;
        std::uint32_t bytes_consumed = 0;
        // Real sideband state: whether the last stream attempt was backpressured,
        // the toggle of completed end-of-transfer packets, and one event flag.
        bool backpressured = false;
        bool packet_toggle = false;
        bool sideband_event = false;
        bool failed = false;
        bool chunk = false;
        bool chunk_command = false;
        std::uint64_t chunk_address = 0;
        std::uint32_t chunk_size = 0, chunk_done = 0;
        std::uint8_t chunk_flags = 0;
        std::uint32_t chunk_ring = 0;
        bool chunk_to_ram = false;
        // Attributes of the access that programmed the pipe, and the copy that an
        // accepted descriptor keeps for its whole transfer.
        struct Attributes {
            bool present = false;
            qemu::MemoryRegionOps::MemTxAttrs attrs{};
        } attributes, chunk_attributes;
    };

    std::uint32_t ctrl_ = 0, desc_threshold_ = 0, irq_en_ = 0, config_bits_ = 0;
    std::uint32_t trust_ = 0, irq_mask_ = 0, global_status_ = 0;
    std::array<std::uint32_t, 8> ee_irq_mask_{};
    std::array<bool, 8> irq_level_{};
    std::array<Pipe, 32> pipes_{};
    std::array<unsigned char, scratch_bytes> scratch_{};
    sc_core::sc_event retry_event_;
    // A reset invalidates accepted work. The global counter covers BAM_SW_RST and
    // the per-pipe counter covers P_RST, which is also reachable from a command
    // element that a running descriptor executes.
    std::uint64_t generation_ = 0;
    std::array<std::uint64_t, 32> pipe_generation_{};
    bool processing_ = false;

    struct Cursor {
        std::uint64_t global = 0, pipe = 0;
    };

    bool stale(unsigned index, const Cursor& cursor) const {
        return generation_ != cursor.global || pipe_generation_[index] != cursor.pipe;
    }

    void end_of_elaboration() override {
        if (!num_pipes.get_value() || num_pipes.get_value() > pipes_.size() ||
            !num_ees.get_value() || num_ees.get_value() > ee_irq_mask_.size() ||
            !revision.get_value() || revision.get_value() > 0xff)
            throw std::invalid_argument("BAM pipe, execution environment, or revision count is invalid");
        if (address_bits.get_value() != 32 && address_bits.get_value() != 36)
            throw std::invalid_argument("BAM address width must be 32 or 36 bits");
        if ((command_base.get_value() & 3) || command_base.get_value() > UINT64_C(0xffffffff))
            throw std::invalid_argument("BAM command base must be a 4-byte aligned 32-bit address");
        cold_reset();
    }

    // BAM_SW_RST returns every register except BAM_CTRL to its virtual reset value.
    void reset_state() {
        desc_threshold_ = irq_en_ = config_bits_ = trust_ = irq_mask_ = global_status_ = 0;
        ee_irq_mask_.fill(0);
        pipes_.fill(Pipe{});
        ++generation_;
        retry_event_.cancel();
        publish_irqs();
    }

    static std::uint32_t ring_bytes(const Pipe& p) { return p.config[1] & 0xffff; }

    std::uint64_t ring_address(const Pipe& p) const {
        std::uint64_t base = p.config[0];
        base |= static_cast<std::uint64_t>(p.desc_msb) << 32;
        return base;
    }

    void capture(Pipe& p, tlm::tlm_generic_payload& tx) {
        const auto* attributes = tx.get_extension<QemuMemTxAttrsExtension>();
        p.attributes.present = attributes != nullptr;
        if (attributes) p.attributes.attrs = attributes->attrs;
    }

    std::uint32_t asserted_pipes() const {
        std::uint32_t mask = 0;
        for (unsigned p = 0; p < num_pipes.get_value() && p < 31 && p < pipes_.size(); ++p)
            if (pipes_[p].irq_status & pipes_[p].irq_en) mask |= 1u << p;
        return mask;
    }

    // BAM_IRQ is one global source and does not depend on a pipe.
    std::uint32_t global_source() const {
        return (global_status_ & irq_en_) ? bam_irq_bit : 0;
    }

    std::uint32_t irq_sources() const {
        return asserted_pipes() | global_source();
    }

    void publish_irqs() {
        for (unsigned ee = 0; ee < num_ees.get_value() && ee < irq.size(); ++ee) {
            const bool level = ee_sources(ee) != 0;
            if (level == irq_level_[ee]) continue;
            irq_level_[ee] = level;
            for (int p = 0; p < irq[ee].size(); ++p) irq[ee][p]->write(level);
        }
    }

    // The peripheral FIFO byte counts stay zero, because the stream is unbuffered.
    static std::uint32_t sideband_value(const Pipe& p) {
        return (p.backpressured ? sideband_ctrl_backpressure : 0) |
               (p.packet_toggle ? sideband_toggle : 0) |
               (p.sideband_event ? sideband_updated : 0);
    }

    void set_backpressure(unsigned index, bool backpressured) {
        auto& p = pipes_[index];
        if (p.backpressured == backpressured) return;
        p.backpressured = backpressured;
        p.sideband_event = true;
    }

    void fail(unsigned index, const char* message, bool bus_error = false) {
        auto& p = pipes_[index];
        p.chunk = false;
        p.irq_status |= bus_error ? (pipe_irq_hresp_error | pipe_irq_error) : pipe_irq_error;
        global_status_ |= bus_error ? (global_irq_hresp | global_irq_error) : global_irq_error;
        if (!p.failed) {
            p.failed = true;
            std::cerr << "BAM_ERROR name=" << name() << " pipe=" << index
                      << " ring=0x" << std::hex << ring_address(p)
                      << " consumer=0x" << p.consumer << std::dec
                      << " error=" << message << '\n';
        }
        publish_irqs();
    }

    std::uint32_t pipe_attributes(unsigned ee) const {
        // BAM_PIPE_ATTR_EE reports BAM_EN and the pipes that P_TRUST assigns to this EE.
        std::uint32_t value = (ctrl_ & bam_en) ? 0x80000000u : 0;
        for (unsigned p = 0; p < num_pipes.get_value() && p < 31; ++p)
            if ((pipes_[p].trust & 7) == ee) value |= 1u << p;
        return value;
    }

    std::uint32_t ee_sources(unsigned ee) const {
        // Pipe bits need the EE mask. Bit 31 needs the global source and the
        // EE mask bit 31.
        return (asserted_pipes() & ee_irq_mask_[ee]) |
               (global_source() & (ee_irq_mask_[ee] & bam_irq_bit));
    }

    bool global(std::uint32_t offset, bool write, std::uint32_t& value) {
        std::uint32_t* reg = nullptr;
        switch (offset) {
        case 0x0000:
            if (write) {
                ctrl_ = value;
                if (value & bam_sw_rst) reset_state();
            } else {
                value = ctrl_;
            }
            return true;
        case 0x0008: reg = &desc_threshold_; break;
        case 0x001c: reg = &irq_en_; break;
        case 0x007c: reg = &config_bits_; break;
        case 0x2000: reg = &trust_; break;
        case 0x3014: reg = &irq_mask_; break;
        case 0x0014:
            // BAM_IRQ_STTS holds only sources that a real transfer or bus error set.
            if (write) return false;
            value = global_status_;
            return true;
        case 0x0018:
            // BAM_IRQ_CLR is write-one-to-clear.
            if (write) {
                global_status_ &= ~value;
                publish_irqs();
            } else {
                value = 0;
            }
            return true;
        case 0x3010:
            if (write) return false;
            value = irq_sources();
            return true;
        case 0x3018:
            if (write) return false;
            value = asserted_pipes();
            return true;
        case 0x1000:
            if (write) return false;
            value = revision.get_value() | (num_ees.get_value() << 8);
            return true;
        case 0x1008:
            if (write) return false;
            value = num_pipes.get_value() | (address_bits.get_value() == 36 ? 1u << 14 : 0);
            return true;
        default: return false;
        }
        if (write) *reg = value; else value = *reg;
        return true;
    }

    bool trust(std::uint32_t offset, bool write, std::uint32_t& value) {
        const auto pipe = (offset - 0x2020) / 4;
        if (offset < 0x2020 || pipe >= num_pipes.get_value()) return false;
        if (write) pipes_[pipe].trust = value; else value = pipes_[pipe].trust;
        return true;
    }

    bool execution_environment(std::uint32_t offset, bool write, std::uint32_t& value) {
        const auto ee = (offset - ee_base) / page;
        const auto reg = (offset - ee_base) % page;
        if (offset < ee_base || ee >= num_ees.get_value()) return false;
        switch (reg) {
        case 0x0:
            if (write) return false;
            value = ee_sources(ee);
            return true;
        case 0x4:
            if (write) {
                ee_irq_mask_[ee] = value;
                publish_irqs();
            } else {
                value = ee_irq_mask_[ee];
            }
            return true;
        case 0x8:
            if (write) return false;
            value = asserted_pipes();
            return true;
        case 0xc:
            if (write) return false;
            value = pipe_attributes(ee);
            return true;
        default: return false;
        }
    }

    bool pipe(std::uint32_t offset, bool write, std::uint32_t& value,
              tlm::tlm_generic_payload& tx, unsigned& index_out) {
        const auto index = (offset - pipe_base) / page;
        const auto reg = (offset - pipe_base) % page;
        if (offset < pipe_base || index >= num_pipes.get_value()) return false;
        auto& p = pipes_[index];
        index_out = index;
        switch (reg) {
        case 0x000:
            if (write) { p.ctrl = value; capture(p, tx); } else value = p.ctrl;
            return true;
        case 0x004:
            if (write) {
                capture(p, tx);
                if (value & pipe_rst) {
                    const auto trust = p.trust;
                    p = Pipe{};
                    p.trust = trust;
                    ++pipe_generation_[index];
                    capture(p, tx);
                    publish_irqs();
                }
            } else {
                value = 0;
            }
            return true;
        case 0x008:
            if (write) { p.halt = value; capture(p, tx); } else value = p.halt;
            return true;
        case 0x010:
            // P_IRQ_STTS reports only status that an accepted descriptor set.
            if (write) return false;
            value = p.irq_status;
            return true;
        case 0x014:
            // P_IRQ_CLR is write-one-to-clear.
            if (write) {
                p.irq_status &= ~value;
                publish_irqs();
            } else {
                value = 0;
            }
            return true;
        case 0x018:
            if (write) { p.irq_en = value; publish_irqs(); } else value = p.irq_en;
            return true;
        case 0x024:
        case 0x028:
            // P_PRDCR_SDBND and P_CNSMR_SDBND. The peripheral FIFO byte count is
            // zero because the model has no data FIFO. The control field reports
            // stream backpressure and the toggle follows completed EOT packets.
            // The event flag is state, so a read does not clear it.
            if (write) return false;
            value = sideband_value(p);
            return true;
        case 0x800:
            // P_SW_OFSTS reports the byte offset being processed and the
            // progress within the accepted descriptor. Hardware keeps it read-only.
            if (write) return false;
            value = (p.consumer & 0xffff) | ((p.chunk ? p.chunk_done & 0xffff : 0) << 16);
            return true;
        case 0x818:
            // P_EVNT_REG holds the producer byte offset in the low half and the
            // consumed byte count in the high half.
            if (write) {
                p.peer = value & 0xffff;
                capture(p, tx);
            }
            value = ((p.bytes_consumed & 0xffff) << 16) | (p.peer & 0xffff);
            return true;
        case 0x914:
            if (write) { p.desc_msb = value; capture(p, tx); } else value = p.desc_msb;
            return true;
        case 0x924:
            if (write) { p.data_msb = value; capture(p, tx); } else value = p.data_msb;
            return true;
        case 0x934:
            if (write) { p.event_msb = value; capture(p, tx); } else value = p.event_msb;
            return true;
        default:
            if (reg < 0x81c || reg > 0x83c) return false;
            if (write) { p.config[(reg - 0x81c) / 4] = value; capture(p, tx); }
            else value = p.config[(reg - 0x81c) / 4];
            return true;
        }
    }

    using BusSocket = tlm_utils::simple_initiator_socket_optional<QcomBamV2>;

    // The transfer repeats the captured attributes exactly and attaches the
    // extension only when the initiating access carried one. The caller selects
    // the RAM bus or the physical command bus. There is no fallback between them.
    bool dma(BusSocket& bus, const char* path, const Pipe::Attributes& attributes, bool write,
             std::uint64_t address, unsigned char* bytes, unsigned length,
             sc_core::sc_time& delay) {
        if (!bus.size()) return false;
        QemuMemTxAttrsExtension holder;
        tlm::tlm_generic_payload tx;
        if (attributes.present) {
            holder.attrs = attributes.attrs;
            tx.set_extension(&holder);
        }
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_address(address);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        tx.set_dmi_allowed(false);
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        bus->b_transport(tx, delay);
        if (!tx.is_response_ok())
            std::cerr << "BAM_DMA_ERROR name=" << name() << " path=" << path
                      << " address=0x" << std::hex << address << std::dec
                      << " bytes=" << length << " write=" << write
                      << " response=" << tx.get_response_string() << '\n';
        if (attributes.present) tx.clear_extension(&holder);
        return tx.is_response_ok();
    }

    // Returns the accepted byte count. A zero count is backpressure, not an error.
    // The captured attributes travel with the stream request, so the endpoint can
    // apply its own ownership rule.
    unsigned stream_access(unsigned index, const Pipe::Attributes& attributes, bool write,
                           unsigned char* bytes, unsigned length, bool end_of_transfer,
                           sc_core::sc_time& delay, bool& ok) {
        BamStreamExtension extension;
        extension.end_of_transfer = end_of_transfer;
        QemuMemTxAttrsExtension holder;
        tlm::tlm_generic_payload tx;
        tx.set_extension(&extension);
        if (attributes.present) {
            holder.attrs = attributes.attrs;
            tx.set_extension(&holder);
        }
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_address(index);
        tx.set_data_ptr(bytes);
        tx.set_data_length(length);
        tx.set_streaming_width(length);
        tx.set_dmi_allowed(false);
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        stream_socket->b_transport(tx, delay);
        ok = tx.is_response_ok();
        const auto transferred = extension.transferred;
        if (attributes.present) tx.clear_extension(&holder);
        tx.clear_extension(&extension);
        return transferred;
    }

    bool validate_ring(unsigned index, std::uint64_t base, std::uint32_t ring) {
        auto& p = pipes_[index];
        if (!ring || (ring & 7)) {
            fail(index, "descriptor ring size is zero or not a multiple of eight bytes");
            return false;
        }
        if (!base) {
            fail(index, "descriptor ring base is not programmed");
            return false;
        }
        if (base >> address_bits.get_value()) {
            fail(index, "descriptor ring address exceeds the configured address width");
            return false;
        }
        if (p.peer >= ring || (p.peer & 7)) {
            fail(index, "producer descriptor offset is outside the ring");
            return false;
        }
        if (p.consumer >= ring || (p.consumer & 7)) {
            fail(index, "consumer descriptor offset is outside the ring");
            return false;
        }
        return true;
    }

    bool fetch(unsigned index, std::uint64_t base, std::uint32_t ring,
               sc_core::sc_time& delay, const Cursor& cursor) {
        auto& p = pipes_[index];
        // The accepted descriptor keeps its own attributes and geometry, so a
        // later register write cannot change it.
        const Pipe::Attributes attributes = p.attributes;
        std::uint32_t words[2]{};
        if (!dma(dma_socket, "dma", attributes, false, base + p.consumer,
                 reinterpret_cast<unsigned char*>(words), sizeof(words), delay)) {
            if (stale(index, cursor)) return false;
            fail(index, dma_socket.size() ? "descriptor read failed" : "descriptor read has no DMA bus",
                 dma_socket.size() != 0);
            return false;
        }
        if (stale(index, cursor)) return false;
        const std::uint32_t low = words[0];
        const std::uint16_t size_field = static_cast<std::uint16_t>(words[1]);
        const std::uint32_t address_high = (words[1] >> 16) & 0xff;
        const std::uint8_t flags = static_cast<std::uint8_t>(words[1] >> 24);
        if (address_high >> (address_bits.get_value() - 32)) {
            fail(index, "descriptor address exceeds the configured address width");
            return false;
        }
        const std::uint64_t address = std::uint64_t{low} | (std::uint64_t{address_high} << 32);
        // Flags bit 0 makes the size field count 64 KiB units. The QSEE writer
        // sets that bit at 0x157bfe04 after moving the unit count into the size field.
        const std::uint64_t bytes = (flags & desc_size_unit)
            ? std::uint64_t{size_field} * size_unit_bytes : std::uint64_t{size_field};
        if ((flags & desc_cmd) && (bytes % command_element_bytes)) {
            fail(index, "command payload length is not a multiple of eight bytes");
            return false;
        }
        if ((flags & desc_cmd) && !command_base.get_value()) {
            fail(index, "command payload needs a configured command_base");
            return false;
        }
        p.chunk = true;
        p.chunk_command = (flags & desc_cmd) != 0;
        p.chunk_flags = flags;
        p.chunk_done = 0;
        p.chunk_address = address;
        p.chunk_size = static_cast<std::uint32_t>(bytes);
        p.chunk_ring = ring;
        p.chunk_to_ram = (p.ctrl & pipe_direction) != 0;
        p.chunk_attributes = attributes;
        if (!p.chunk_size) {
            // A zero-length descriptor moves no bytes and issues no stream access.
            complete(index);
            return true;
        }
        return true;
    }

    // Moves or completes one step of the accepted descriptor. Returns false when
    // the pipe stalled, failed or a reset cancelled the work, so no trigger can
    // spin with zero progress.
    bool step(unsigned index, sc_core::sc_time& delay, const Cursor& cursor) {
        auto& p = pipes_[index];
        const std::uint32_t remaining = p.chunk_size - p.chunk_done;
        if (!remaining) {
            if (stale(index, cursor)) return false;
            complete(index);
            return true;
        }
        if (!dma_socket.size()) {
            fail(index, "descriptor needs memory but dma_socket is unbound");
            return false;
        }
        const Pipe::Attributes attributes = p.chunk_attributes;
        if (p.chunk_command) {
            const unsigned length = remaining > scratch_.size() ? unsigned(scratch_.size()) : remaining;
            if (!dma(dma_socket, "dma", attributes, false, p.chunk_address + p.chunk_done,
                     scratch_.data(), length, delay)) {
                if (stale(index, cursor)) return false;
                fail(index, "command payload read failed", true);
                return false;
            }
            if (stale(index, cursor)) return false;
            for (unsigned offset = 0; offset + command_element_bytes <= length;
                 offset += command_element_bytes) {
                const unsigned char* element = scratch_.data() + offset;
                const std::uint32_t first = std::uint32_t{element[0]} |
                    (std::uint32_t{element[1]} << 8) | (std::uint32_t{element[2]} << 16) |
                    (std::uint32_t{element[3]} << 24);
                const std::uint32_t second = std::uint32_t{element[4]} |
                    (std::uint32_t{element[5]} << 8) | (std::uint32_t{element[6]} << 16) |
                    (std::uint32_t{element[7]} << 24);
                const std::uint32_t opcode = first >> 24;
                const std::uint32_t register_offset = first & 0x00ffffff;
                if (opcode != command_write) {
                    fail(index, "command element holds an unsupported opcode");
                    return false;
                }
                const std::uint64_t target = command_base.get_value() + register_offset;
                if (target > UINT64_C(0xffffffff)) {
                    fail(index, "command element address exceeds the 32-bit register space");
                    return false;
                }
                unsigned char value[4] = {
                    static_cast<unsigned char>(second),
                    static_cast<unsigned char>(second >> 8),
                    static_cast<unsigned char>(second >> 16),
                    static_cast<unsigned char>(second >> 24)};
                if (!command_socket.size()) {
                    fail(index, "command register write has no command bus");
                    return false;
                }
                if (!dma(command_socket, "command", attributes, true, target, value, 4, delay)) {
                    if (stale(index, cursor)) return false;
                    fail(index, "command register write failed", true);
                    return false;
                }
                // A command element can reset this BAM or its pipe. The elements
                // that follow belong to cancelled work and must not run.
                if (stale(index, cursor)) return false;
                p.chunk_done += command_element_bytes;
            }
            if (p.chunk_done == p.chunk_size) complete(index);
            return true;
        }
        if (!stream_socket.size()) {
            fail(index, "descriptor needs a stream endpoint but stream_socket is unbound");
            return false;
        }
        const bool to_ram = p.chunk_to_ram;
        const unsigned length = remaining > scratch_.size() ? unsigned(scratch_.size()) : remaining;
        const bool final_chunk = length == remaining;
        if (to_ram) {
            bool ok = false;
            const unsigned accepted =
                stream_access(index, attributes, false, scratch_.data(), length,
                             final_chunk && (p.chunk_flags & desc_eot), delay, ok);
            if (stale(index, cursor)) return false;
            if (!ok) {
                fail(index, "stream read failed");
                return false;
            }
            if (!accepted) {
                set_backpressure(index, true);
                return false;
            }
            if (accepted > length) {
                fail(index, "stream read reported more bytes than requested");
                return false;
            }
            if (!dma(dma_socket, "dma", attributes, true, p.chunk_address + p.chunk_done,
                     scratch_.data(), accepted, delay)) {
                if (stale(index, cursor)) return false;
                fail(index, "RAM write of stream data failed", true);
                return false;
            }
            if (stale(index, cursor)) return false;
            set_backpressure(index, false);
            p.chunk_done += accepted;
            p.bytes_consumed += accepted;
        } else {
            if (!dma(dma_socket, "dma", attributes, false, p.chunk_address + p.chunk_done,
                     scratch_.data(), length, delay)) {
                if (stale(index, cursor)) return false;
                fail(index, "RAM read of descriptor data failed", true);
                return false;
            }
            if (stale(index, cursor)) return false;
            bool ok = false;
            const unsigned accepted =
                stream_access(index, attributes, true, scratch_.data(), length,
                             final_chunk && (p.chunk_flags & desc_eot), delay, ok);
            if (stale(index, cursor)) return false;
            if (!ok) {
                fail(index, "stream write failed");
                return false;
            }
            if (!accepted) {
                set_backpressure(index, true);
                return false;
            }
            if (accepted > length) {
                fail(index, "stream write reported more bytes than offered");
                return false;
            }
            set_backpressure(index, false);
            p.chunk_done += accepted;
            p.bytes_consumed += accepted;
        }
        if (p.chunk_done == p.chunk_size) complete(index);
        return true;
    }

    void complete(unsigned index) {
        auto& p = pipes_[index];
        const std::uint8_t flags = p.chunk_flags;
        const std::uint32_t ring = p.chunk_ring;
        if (ring) p.consumer = (p.consumer + descriptor_bytes) % ring;
        if (flags & desc_int) p.irq_status |= pipe_irq_processed;
        if (flags & desc_eot) {
            p.irq_status |= pipe_irq_transfer_end;
            p.packet_toggle = !p.packet_toggle;
            p.sideband_event = true;
        }
        p.chunk = false;
        p.chunk_command = false;
        p.chunk_size = p.chunk_done = 0;
        p.chunk_flags = 0;
        publish_irqs();
    }

    void retry_pending() {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        process_all(delay);
    }

    void process_all(sc_core::sc_time& delay) {
        for (unsigned i = 0; i < num_pipes.get_value() && i < pipes_.size(); ++i)
            process_pipe(i, delay);
    }

    void process_pipe(unsigned index, sc_core::sc_time& delay) {
        if (processing_) return;
        processing_ = true;
        auto& p = pipes_[index];
        const Cursor cursor{generation_, pipe_generation_[index]};
        const std::uint32_t consumer = p.consumer, done = p.chunk_done;
        const bool chunk = p.chunk;
        const bool cancelled = process_work(index, delay, cursor);
        // One trigger stays bounded. When it made progress and work remains, the
        // next delta continues the ring, so a long ring never needs a new doorbell.
        // Cancelled work reports neither progress nor failure for the new state.
        const bool progressed = !cancelled &&
            (p.consumer != consumer || p.chunk_done != done || p.chunk != chunk);
        const bool pending = !cancelled && !p.failed && (p.chunk || p.peer != p.consumer);
        processing_ = false;
        if (progressed && pending) retry_event_.notify(sc_core::SC_ZERO_TIME);
    }

    // Returns true when a reset cancelled the work, so the caller must not report
    // progress or failure for it.
    bool process_work(unsigned index, sc_core::sc_time& delay, const Cursor& cursor) {
        auto& p = pipes_[index];
        if (p.failed) return false;
        if (!(ctrl_ & bam_en) || !(p.ctrl & pipe_en) || (p.halt & pipe_stop)) return false;
        if (!p.chunk && p.peer == p.consumer) return false;
        if (!validate_ring(index, ring_address(p), ring_bytes(p))) return stale(index, cursor);
        const unsigned limit = ring_bytes(p) / descriptor_bytes;
        for (unsigned count = 0; count < limit; ++count) {
            if (p.failed) return false;
            if (!p.chunk) {
                if (p.consumer == p.peer) return false;
                // Each new descriptor takes the geometry that the registers hold
                // now. A program change while work is active is visible here.
                const std::uint64_t base = ring_address(p);
                const std::uint32_t ring = ring_bytes(p);
                if (!validate_ring(index, base, ring)) return stale(index, cursor);
                if (!fetch(index, base, ring, delay, cursor)) return stale(index, cursor);
                // A zero-length descriptor completed in fetch; take the next one.
                if (!p.chunk) continue;
            }
            if (!step(index, delay, cursor)) return stale(index, cursor);
        }
        return false;
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!clock_detail::valid(tx)) return;
        const auto address = tx.get_address();
        if (address > 0xffffffffu) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const auto offset = static_cast<std::uint32_t>(address);
        const bool write = tx.is_write();
        std::uint32_t value = write ? clock_detail::load(tx) : 0;
        bool handled = false;
        unsigned index = static_cast<unsigned>(-1);
        if (offset >= pipe_base) handled = pipe(offset, write, value, tx, index);
        else if (offset >= ee_base) handled = global(offset, write, value) ||
                                              execution_environment(offset, write, value);
        else if (offset >= 0x2000) handled = global(offset, write, value) || trust(offset, write, value);
        else handled = global(offset, write, value);
        if (!handled) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (!write) clock_detail::store(tx, value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
        if (!write) return;
        if (index != static_cast<unsigned>(-1)) {
            process_pipe(index, delay);
        } else if (offset == 0x0000 && (value & bam_en) && !(value & bam_sw_rst)) {
            process_all(delay);
        }
    }
};

} // namespace qcom_vp

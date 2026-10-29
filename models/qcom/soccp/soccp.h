// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "ipcc.h"
#include "service.h"
#include "mutex.h"
#include "clock_source.h"
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <algorithm>
#include <array>
#include <optional>
#include <string>

namespace qcom_vp {

// Functional remote endpoint. It does not execute the physical SOCCP image.
// XBL still loads and authenticates that image before its real release operation.
class QcomSoccp : public sc_core::sc_module, public IpccRemotePeer {
public:
    tlm_utils::multi_passthrough_target_socket<QcomSoccp> boot_socket{"boot_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomSoccp> config_socket{"config_socket"};
    tlm_utils::multi_passthrough_target_socket<QcomSoccp> dtb_socket{"dtb_socket"};
    tlm_utils::simple_initiator_socket<QcomSoccp> memory_socket{"memory_socket"};
    cci::cci_param<uint64_t> smem_base{"smem_base", 0, "Guest-owned SMEM region base"};
    cci::cci_param<uint32_t> smem_size{"smem_size", 0, "Guest-owned SMEM region size"};
    cci::cci_param<std::string> ipcc_path{"ipcc_path", "", "Protocol-zero local IPCC module"};
    cci::cci_param<std::string> clock_path{"clock_path", "", "Required release clock branch"};
    cci::cci_param<std::string> mutex_path{"mutex_path", "", "Shared guest/remote SMEM allocation mutex"};
    cci::cci_param<uint32_t> voltage_mv{"voltage_mv", 0, "Explicit virtual pack voltage, not a measurement"};
    cci::cci_param<uint32_t> design_mwh{"design_mwh", 0, "Explicit virtual pack design energy"};
    cci::cci_param<uint32_t> full_mwh{"full_mwh", 0, "Explicit virtual pack full energy"};
    cci::cci_param<uint32_t> remaining_mwh{"remaining_mwh", 0, "Explicit virtual pack remaining energy"};
    cci::cci_param<int32_t> temperature_decic{"temperature_decic", 0, "Explicit virtual pack temperature"};

    enum class RegisterLayout { v1, v2 };

    explicit QcomSoccp(sc_core::sc_module_name name, RegisterLayout layout = RegisterLayout::v1)
        : sc_module(name), layout_(layout) {
        boot_socket.register_b_transport(this, &QcomSoccp::boot_transport);
        config_socket.register_b_transport(this, &QcomSoccp::config_transport);
        dtb_socket.register_b_transport(this, &QcomSoccp::dtb_transport);
    }

    bool consume_doorbell() override {
        if (!released_ || faulted_ || servicing_) return false;
        if (link_down_) return true;
        servicing_ = true;
        try {
            connect_heap();
            if (!ap_fifo_) { servicing_ = false; return true; }
            if (!version_sent_) {
                emit(0, 1, 1);
                version_sent_ = true;
            }
            if (!flush()) { servicing_ = false; return true; }
            if (!link_down_) drain();
            servicing_ = false;
            return true;
        } catch (const std::exception& error) {
            servicing_ = false;
            faulted_ = true;
            SC_REPORT_ERROR("SOCCP", error.what());
            return false;
        }
    }

private:
    static constexpr unsigned fifo_size = 0x4000;
    static constexpr unsigned mtu = 0x1000;
    // The extended layout adds one remap and one mode word. Transport and services are shared.
    static constexpr std::array<uint32_t, 15> config_offsets{
        0, 4, 8, 0xc, 0x20, 0x28, 0x80, 0x84, 0x88, 0x8c, 0xa8, 0x200, 0x218,
        0x90, 0x10};
    const RegisterLayout layout_;
    unsigned config_count() const { return layout_ == RegisterLayout::v2 ? 15 : 13; }
    std::array<uint32_t, config_offsets.size()> config_{};
    uint32_t config_written_ = 0;
    uint32_t entry_ = 0, dtb_ = 0;
    bool entry_written_ = false, dtb_written_ = false, released_ = false;
    bool faulted_ = false, servicing_ = false;
    bool version_sent_ = false, negotiated_ = false, link_down_ = false;
    QcomIpccLocal* ipcc_ = nullptr;
    ClockSource* clock_ = nullptr;
    QcomSmemMutex* mutex_ = nullptr;
    std::optional<soccp_detail::Service> service_;
    uint64_t partition_ = 0, descriptor_ = 0, ap_fifo_ = 0, peer_fifo_ = 0;
    uint32_t partition_size_ = 0, rx_pos_ = 0, tx_pos_ = 0;
    std::array<unsigned char, fifo_size> output_{};
    unsigned output_size_ = 0;
    std::array<unsigned char, 8204> response_{};
    std::array<unsigned char, mtu> fragment_{};

    struct Intent {
        uint32_t id = 0, size = 0;
        bool present = false, in_flight = false;
    };
    struct ReceiveIntent {
        std::array<unsigned char, mtu> data{};
        unsigned size = 0, expected = 0;
        bool arriving = false;
    };
    struct Channel {
        uint16_t ap = 0;
        bool present = false, acknowledged = false, closing = false;
        uint32_t signals = 0;
        std::array<Intent, 32> ap_intents{};
        std::array<ReceiveIntent, 6> receive{};
    };
    std::array<Channel, 3> channels_{};
    static constexpr std::array<const char*, 3> channel_names{
        "PMIC_RTR_SOCCP_APPS", "PMIC_LOGS_SOCCP_APPS", "glink_ssr"};

    template<class T> T* resolve(const std::string& path) {
        auto* result = dynamic_cast<T*>(sc_core::sc_find_object(path.c_str()));
        if (!result && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            result = dynamic_cast<T*>(sc_core::sc_find_object(relative.c_str()));
        }
        return result;
    }
    void end_of_elaboration() override {
        if (!smem_base.get_value() || smem_size.get_value() < 0x2000 ||
            (smem_size.get_value() & 0xfff) ||
            smem_base.get_value() > UINT64_MAX - smem_size.get_value())
            throw std::invalid_argument("SOCCP requires a valid guest SMEM region");
        ipcc_ = resolve<QcomIpccLocal>(ipcc_path.get_value());
        clock_ = resolve<ClockSource>(clock_path.get_value());
        mutex_ = resolve<QcomSmemMutex>(mutex_path.get_value());
        if (!ipcc_ || !clock_ || !mutex_ || ipcc_->protocol.get_value() != 0 ||
            ipcc_->peer_path.get_value().empty())
            throw std::invalid_argument("SOCCP requires its IPCC remote, heap mutex, and release clock");
        service_.emplace(soccp_detail::BatteryPolicy{voltage_mv.get_value(), design_mwh.get_value(),
            full_mwh.get_value(), remaining_mwh.get_value(), temperature_decic.get_value()});
    }
    static void require(bool condition, const char* message) { soccp_detail::require(condition, message); }
    static unsigned aligned(unsigned size) { return (size + 7u) & ~7u; }
    static uint32_t u32(const unsigned char* p) { return soccp_detail::u32(p); }
    static uint16_t u16(const unsigned char* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
    static void put32(unsigned char* p, uint32_t v) { soccp_detail::put32(p, v); }
    static void put16(unsigned char* p, uint16_t v) {
        p[0] = static_cast<unsigned char>(v); p[1] = static_cast<unsigned char>(v >> 8);
    }
    void memory(bool write, uint64_t address, unsigned char* data, unsigned size) {
        require(address >= smem_base.get_value() && size <= smem_size.get_value() &&
            address - smem_base.get_value() <= smem_size.get_value() - size,
            "SOCCP memory access exceeds the guest SMEM region");
        tlm::tlm_generic_payload tx;
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_address(address); tx.set_data_ptr(data); tx.set_data_length(size);
        tx.set_streaming_width(size); tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        memory_socket->b_transport(tx, delay);
        require(tx.is_response_ok(), "SOCCP shared-memory transaction failed");
    }
    uint32_t read32(uint64_t address) {
        std::array<unsigned char, 4> bytes{};
        memory(false, address, bytes.data(), bytes.size()); return u32(bytes.data());
    }
    void write32(uint64_t address, uint32_t value) {
        std::array<unsigned char, 4> bytes{};
        put32(bytes.data(), value); memory(true, address, bytes.data(), bytes.size());
    }
    void discover_partition() {
        const auto base = smem_base.get_value();
        const auto toc = base + smem_size.get_value() - 0x1000;
        std::array<unsigned char, 32> header{};
        memory(false, toc, header.data(), header.size());
        require(u32(header.data()) == 0x434f5424 && u32(header.data() + 4) == 1,
            "SOCCP doorbell before guest SMEM TOC initialization");
        const auto count = u32(header.data() + 8);
        require(count && count <= 60, "Invalid guest SMEM TOC length");
        std::array<unsigned char, 48> entry{};
        for (unsigned i = 0; i < count; ++i) {
            memory(false, toc + 32 + i * 48, entry.data(), entry.size());
            if (u16(entry.data() + 12) != 0 || u16(entry.data() + 14) != 19) continue;
            require(!partition_, "Duplicate APSS/SOCCP SMEM partition");
            const auto offset = u32(entry.data());
            const auto size = u32(entry.data() + 4);
            require(u32(entry.data() + 8) == 3 && size >= 0x1000 && offset >= 0x1000 &&
                !(offset & 0xfff) && !(size & 0xfff) && u32(entry.data() + 16) == 32 &&
                offset < smem_size.get_value() - 0x1000 &&
                size <= smem_size.get_value() - 0x1000 - offset,
                "Invalid guest SOCCP partition geometry");
            // This consumer uses an uncached heap with no exclusion regions.
            for (unsigned j = 32; j < 48; j += 4)
                require(!u32(entry.data() + j), "Unsupported SOCCP partition exclusion");
            partition_ = base + offset; partition_size_ = size;
        }
        require(partition_ != 0, "Guest did not initialize an APSS/SOCCP partition");
    }
    // Scan only the guest's published uncached allocation chain. Never create its root or partition.
    uint64_t find_item(unsigned item, unsigned size, uint32_t& cursor, uint32_t& cached) {
        std::array<unsigned char, 32> header{};
        memory(false, partition_, header.data(), header.size());
        require(u32(header.data()) == 0x54525024 && u16(header.data() + 4) == 0 &&
            u16(header.data() + 6) == 19 && u32(header.data() + 8) == partition_size_,
            "Invalid guest SOCCP SMEM partition header");
        cursor = u32(header.data() + 12); cached = u32(header.data() + 16);
        require(cursor >= 32 && !(cursor & 7) && cached >= cursor && cached <= partition_size_,
            "Invalid guest SOCCP heap cursors");
        uint64_t found = 0;
        std::array<unsigned char, 16> allocation{};
        for (unsigned offset = 32; offset < cursor;) {
            require(cursor - offset >= 16, "Truncated guest SMEM allocation header");
            memory(false, partition_ + offset, allocation.data(), allocation.size());
            const auto extent = u32(allocation.data() + 4);
            const auto padding = u16(allocation.data() + 8);
            require(u16(allocation.data()) == 0xa5a5 && !(extent & 7) && padding <= extent &&
                extent <= cursor - offset - 16 && u16(allocation.data() + 10) == 0,
                "Invalid guest SMEM uncached allocation");
            if (u16(allocation.data() + 2) == item) {
                require(!found && extent - padding == size, "Invalid or duplicate guest GLINK item");
                found = partition_ + offset + 16;
            }
            offset += 16 + extent;
        }
        return found;
    }
    uint64_t allocate_item(unsigned item, unsigned size) {
        uint32_t cursor = 0, cached = 0;
        if (auto address = find_item(item, size, cursor, cached)) return address;
        require(aligned(size) <= cached - cursor && cached - cursor - aligned(size) >= 16,
            "Guest SOCCP heap cannot hold the remote GLINK item");
        // Only the remote descriptor/FIFO allocation is added. The guest owns its global allocator state.
        std::array<unsigned char, 16> header{};
        put16(header.data(), 0xa5a5); put16(header.data() + 2, static_cast<uint16_t>(item));
        put32(header.data() + 4, aligned(size));
        put16(header.data() + 8, static_cast<uint16_t>(aligned(size) - size));
        const auto address = partition_ + cursor + 16;
        // The caller holds the same mutex as selected SmemDxe. Publish the cursor last.
        memory(true, partition_ + cursor, header.data(), header.size());
        std::array<unsigned char, 256> zeros{};
        for (unsigned offset = 0; offset < aligned(size); offset += zeros.size()) {
            const auto n = std::min<unsigned>(zeros.size(), aligned(size) - offset);
            memory(true, address + offset, zeros.data(), n);
        }
        write32(partition_ + 12, cursor + 16 + aligned(size));
        return address;
    }
    void connect_heap() {
        if (descriptor_ && peer_fifo_ && ap_fifo_) return;
        require(mutex_->claim_remote(), "SOCCP allocation while the guest owns the heap mutex");
        struct Unlock {
            QcomSmemMutex* mutex;
            ~Unlock() { mutex->release_remote(); }
        } unlock{mutex_};
        const bool newly_published = !peer_fifo_;
        if (!partition_) discover_partition();
        if (!descriptor_) {
            descriptor_ = allocate_item(478, 32);
            // Existing AP-owned indices are not reset by this remote.
            require(read32(descriptor_) == 0 && read32(descriptor_ + 12) == 0,
                "SOCCP cannot adopt an active remote descriptor");
        }
        if (!peer_fifo_) peer_fifo_ = allocate_item(480, fifo_size);
        if (!ap_fifo_) {
            uint32_t cursor = 0, cached = 0;
            ap_fifo_ = find_item(479, fifo_size, cursor, cached);
        }
        // Publication itself changes link availability, even before an AP command exists.
        if (newly_published) ipcc_->receive(ipcc_->peer_index.get_value(), 0);
    }
    static unsigned distance(unsigned from, unsigned to) { return (to + fifo_size - from) % fifo_size; }
    uint32_t checked_index(uint64_t address) {
        const auto value = read32(address);
        require(value < fifo_size && !(value & 7), "Unaligned or out-of-range GLINK index");
        return value;
    }
    void ring(bool write, uint64_t base, uint32_t& position, unsigned char* bytes, unsigned size) {
        require(size <= fifo_size && !(size & 7), "Invalid GLINK ring transfer");
        const auto first = std::min<unsigned>(size, fifo_size - position);
        if (first) memory(write, base + position, bytes, first);
        if (first < size) memory(write, base, bytes + first, size - first);
        position = (position + size) % fifo_size;
    }
    void take(unsigned char* bytes, unsigned size) {
        const auto producer = checked_index(descriptor_ + 4);
        require(size <= distance(rx_pos_, producer), "Incomplete published GLINK command");
        ring(false, ap_fifo_, rx_pos_, bytes, size);
    }
    uint64_t take_word() {
        std::array<unsigned char, 8> bytes{}; take(bytes.data(), bytes.size());
        return uint64_t{u32(bytes.data())} | (uint64_t{u32(bytes.data() + 4)} << 32);
    }
    void append(const unsigned char* bytes, unsigned size) {
        require(aligned(size) <= output_.size() - output_size_, "SOCCP command exceeds its output bound");
        std::memcpy(output_.data() + output_size_, bytes, size);
        std::memset(output_.data() + output_size_ + size, 0, aligned(size) - size);
        output_size_ += aligned(size);
    }
    void emit(unsigned command, unsigned parameter, uint32_t argument) {
        std::array<unsigned char, 8> word{};
        put32(word.data(), command | (parameter << 16)); put32(word.data() + 4, argument);
        append(word.data(), word.size());
    }
    bool flush() {
        if (!output_size_) return true;
        const auto read = checked_index(descriptor_ + 8);
        const auto free = fifo_size - distance(read, tx_pos_);
        // Reserve the mandatory gap and one READ_REQ. Never overwrite an AP-owned response intent.
        if (free < output_size_ + 16) {
            if (free >= 16) {
                std::array<unsigned char, 8> request{}; put32(request.data(), 13);
                ring(true, peer_fifo_, tx_pos_, request.data(), request.size());
                write32(descriptor_ + 12, tx_pos_);
                ipcc_->receive(ipcc_->peer_index.get_value(), 0);
            }
            return false;
        }
        ring(true, peer_fifo_, tx_pos_, output_.data(), output_size_);
        write32(descriptor_ + 12, tx_pos_);
        output_size_ = 0;
        ipcc_->receive(ipcc_->peer_index.get_value(), 0);
        return true;
    }
    unsigned ap_channel(unsigned cid) const {
        for (unsigned i = 0; i < channels_.size(); ++i)
            if (channels_[i].present && channels_[i].ap == cid) return i;
        throw std::runtime_error("GLINK command references an unknown AP channel");
    }
    unsigned local_channel(unsigned cid) const {
        require(cid && cid <= channels_.size() && channels_[cid - 1].present,
            "GLINK command references an unknown remote channel");
        return cid - 1;
    }
    void open(unsigned ap, unsigned length) {
        require(ap && length && length <= 32, "Invalid GLINK OPEN name length or channel");
        std::array<unsigned char, 32> name{}; take(name.data(), aligned(length));
        require(!name[length - 1] && std::strlen(reinterpret_cast<const char*>(name.data())) == length - 1,
            "Invalid GLINK OPEN name");
        unsigned index = channels_.size();
        for (unsigned i = 0; i < channels_.size(); ++i) {
            require(!channels_[i].present || channels_[i].ap != ap, "Duplicate GLINK AP channel");
            if (std::strcmp(reinterpret_cast<const char*>(name.data()), channel_names[i]) == 0) index = i;
        }
        require(index < channels_.size() && !channels_[index].present, "Unsupported or duplicate GLINK channel");
        auto& channel = channels_[index]; channel.present = true; channel.ap = ap;
        emit(2, index + 1, length | (100u << 16)); append(name.data(), length);
        emit(5, index + 1, channel.receive.size());
        for (unsigned j = 0; j < channel.receive.size(); ++j) {
            std::array<unsigned char, 8> intent{};
            put32(intent.data(), mtu); put32(intent.data() + 4, index * 6 + j + 1);
            append(intent.data(), intent.size());
        }
        emit(4, ap, 0);
    }
    Intent& intent(Channel& channel, uint32_t id) {
        for (auto& entry : channel.ap_intents) if (entry.present && entry.id == id) return entry;
        throw std::runtime_error("GLINK references an unowned AP receive intent");
    }
    void send_packet(unsigned index, unsigned size) {
        auto& channel = channels_[index];
        Intent* chosen = nullptr;
        for (auto& entry : channel.ap_intents)
            if (entry.present && !entry.in_flight && entry.size >= size &&
                (!chosen || entry.size < chosen->size)) chosen = &entry;
        require(chosen, "AP did not publish a sufficient response intent");
        chosen->in_flight = true;
        for (unsigned offset = 0; offset < size; offset += mtu) {
            const auto fragment = std::min<unsigned>(mtu, size - offset);
            emit(offset ? 12 : 9, index + 1, chosen->id);
            std::array<unsigned char, 8> lengths{};
            put32(lengths.data(), fragment); put32(lengths.data() + 4, size - offset - fragment);
            append(lengths.data(), lengths.size()); append(response_.data() + offset, fragment);
        }
    }
    void receive_packet(unsigned index, uint32_t id, bool continuation) {
        auto& channel = channels_[index];
        require(channel.acknowledged && !channel.closing && id > index * 6 && id <= index * 6 + 6,
            "GLINK TX references an unavailable remote intent");
        auto& packet = channel.receive[id - index * 6 - 1];
        const auto lengths = take_word();
        const uint32_t fragment = lengths, remaining = lengths >> 32;
        require(fragment <= mtu && remaining <= mtu && fragment + remaining <= mtu &&
            fragment != 0, "GLINK packet exceeds its remote intent");
        if (!continuation) {
            require(!packet.arriving, "GLINK TX restarts an arriving intent");
            packet.expected = fragment + remaining; packet.size = 0; packet.arriving = true;
        } else require(packet.arriving && packet.size + fragment + remaining == packet.expected,
            "Inconsistent GLINK continuation length");
        require(packet.size + fragment <= packet.data.size(), "GLINK fragment overflows its intent");
        take(fragment_.data(), aligned(fragment));
        std::memcpy(packet.data.data() + packet.size, fragment_.data(), fragment); packet.size += fragment;
        if (remaining) return;
        require(packet.size == packet.expected, "Incomplete GLINK service message");
        if (index == 2) {
            require(packet.size == 48 && u32(packet.data.data()) == 0 && u32(packet.data.data() + 4) == 0 &&
                u32(packet.data.data() + 12) == 5 && std::memcmp(packet.data.data() + 16, "apss", 5) == 0,
                "Unsupported GLINK SSR cleanup");
            // Sequence@8 belongs to the request. This protocol resets the edge instead of replying.
            channels_ = {}; rx_pos_ = tx_pos_ = 0;
            write32(descriptor_, 0); write32(descriptor_ + 12, 0);
            output_size_ = 0; link_down_ = true; return;
        }
        const auto response_size = service_->execute(index == 1, packet.data.data(), packet.size, response_.data());
        packet.arriving = false; packet.size = packet.expected = 0;
        emit(14, index + 1, id);
        send_packet(index, response_size);
    }
    void command(uint64_t word) {
        const unsigned opcode = word & 0xffff, parameter = (word >> 16) & 0xffff;
        const uint32_t argument = word >> 32;
        if (opcode == 0 || opcode == 1) {
            require(parameter == 1 && (argument & 1), "Unsupported GLINK version negotiation");
            if (opcode == 0) emit(1, 1, 1);
            else negotiated_ = argument == 1;
            return;
        }
        require(negotiated_, "GLINK command before version negotiation");
        if (opcode == 2) { open(parameter, argument & 0xffff); return; }
        if (opcode == 4) {
            auto& c = channels_[local_channel(parameter)];
            require(!c.acknowledged && !c.closing, "Duplicate GLINK OPEN ACK");
            c.acknowledged = true; return;
        }
        if (opcode == 11) {
            auto& c = channels_[local_channel(parameter)]; require(c.closing, "Unexpected GLINK CLOSE ACK");
            c = {}; return;
        }
        if (opcode == 13) { require(parameter == 0 && argument == 0, "Invalid GLINK READ_REQ"); return; }
        const auto index = ap_channel(parameter); auto& c = channels_[index];
        if (opcode == 3) {
            require(c.acknowledged && !c.closing, "GLINK CLOSE outside a connected channel");
            c.closing = true; c.acknowledged = false;
            emit(11, parameter, 0); emit(3, index + 1, 0);
        } else if (opcode == 5) {
            require(argument && argument <= c.ap_intents.size(), "GLINK intent count exceeds the model bound");
            for (unsigned j = 0; j < argument; ++j) {
                const auto descriptor = take_word();
                const uint32_t size = descriptor, id = descriptor >> 32;
                require(size && size <= 0x10000, "Invalid GLINK AP intent size");
                Intent* vacant = nullptr;
                for (auto& entry : c.ap_intents) {
                    require(!entry.present || entry.id != id, "Duplicate GLINK AP intent");
                    if (!entry.present && !vacant) vacant = &entry;
                }
                require(vacant, "GLINK AP intent table exhausted"); *vacant = {id, size, true, false};
            }
        } else if (opcode == 6 || opcode == 14) {
            auto& entry = intent(c, argument); require(entry.in_flight, "GLINK response intent released twice");
            if (opcode == 14) entry.in_flight = false;
            else entry = {};
        } else if (opcode == 7) {
            // Six reusable remote intents already exist. This bounded peer refuses additional intents.
            emit(8, index + 1, 0);
        } else if (opcode == 9 || opcode == 12) receive_packet(index, argument, opcode == 12);
        else if (opcode == 15) c.signals = argument;
        else require(false, "Unsupported or unnegotiated GLINK command");
    }
    void drain() {
        for (unsigned commands = 0; commands < fifo_size / 8; ++commands) {
            if (rx_pos_ == checked_index(descriptor_ + 4)) return;
            command(take_word());
            if (link_down_) return;
            // Consumption and response publication are separate. The AP owns D+4 and D+8.
            write32(descriptor_, rx_pos_);
            if (!flush()) return;
        }
        require(false, "GLINK drain exceeded the FIFO command bound");
    }
    static bool valid(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        else if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        else if (!tx.is_read() && !tx.is_write()) tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        else return true;
        return false;
    }
    static uint32_t value(const tlm::tlm_generic_payload& tx) { return u32(tx.get_data_ptr()); }
    static void reply(tlm::tlm_generic_payload& tx, uint32_t data) {
        if (tx.is_read()) put32(tx.get_data_ptr(), data);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    static void reject(tlm::tlm_generic_payload& tx) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); }
    void config_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        for (unsigned i = 0; i < config_count(); ++i) {
            if (tx.get_address() != config_offsets[i]) continue;
            if (tx.is_write()) {
                if (released_ || faulted_) return reject(tx);
                const auto v = value(tx);
                // XBL changes the low-nibble mode with a read/modify/write.
                // Retain the other bits as ordinary configuration, not a readiness predicate.
                config_[i] = v; config_written_ |= 1u << i;
            }
            return reply(tx, config_[i]);
        }
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
    void dtb_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.get_address()) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (tx.is_write()) {
            if (released_ || faulted_ || !value(tx) || (value(tx) & 3)) return reject(tx);
            dtb_ = value(tx); dtb_written_ = true;
        }
        reply(tx, dtb_);
    }
    void boot_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.get_address() != 0 && tx.get_address() != 4) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        }
        if (!tx.is_write()) return reply(tx, tx.get_address() ? (released_ ? 0 : 1) : entry_);
        if (!tx.get_address()) {
            if (released_ || faulted_ || !value(tx) || (value(tx) & 1)) return reject(tx);
            entry_ = value(tx); entry_written_ = true;
        } else {
            if (value(tx) || faulted_ || !entry_written_ || !dtb_written_ ||
                config_written_ != (1u << config_count()) - 1 || !clock_->clock_running())
                return reject(tx);
            // Release changes remote lifecycle only. It does not publish SMEM or answer an EFI call.
            released_ = true;
        }
        reply(tx, 0);
    }
};

class QcomSoccpV2 final : public QcomSoccp {
public:
    explicit QcomSoccpV2(sc_core::sc_module_name name) : QcomSoccp(name, RegisterLayout::v2) {}
};
} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include "resource_directory.h"

namespace qcom_vp {

// The RSC retains requester votes and completes supported active batches. See evidence.md.
class QcomRpmhRsc : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomRpmhRsc> target_socket{"target_socket"};
    InitiatorSignalSocket<bool> irq_0{"irq_0"}, irq_1{"irq_1"}, irq_2{"irq_2"}, irq_3{"irq_3"};
    cci::cci_param<std::string> banks{"banks", "", "Rows contain the DRV index, TCS count, and active count"};
    cci::cci_param<unsigned> requester_base{"requester_base", 0, "First requester index for this controller"};
    cci::cci_param<std::string> resource_owner_path{"resource_owner_path", "", "Path to the shared resource owner"};
    cci::cci_param<unsigned> sequencer_words{"sequencer_words", 0, "Instruction capacity in 32-bit words"};
    cci::cci_param<std::string> resources{"resources", "", "Comma-separated supported resource addresses"};
    cci::cci_param<unsigned> commands{"commands", 16, "Commands per TCS, from 1 through 16"};
    cci::cci_param<unsigned> tcs_offset{"tcs_offset", 0xd00, "Offset of the common TCS registers in each DRV"};
    cci::cci_param<unsigned> tcs_stride{"tcs_stride", 0x2a0, "Byte distance between TCS register groups"};
    cci::cci_param<std::string> directory_path{"directory_path", "",
        "Optional Command DB directory that publishes VRM resources"};

    explicit QcomRpmhRsc(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomRpmhRsc::transport);
    }

    // This state describes virtual votes, not measured voltages or clock frequencies.
    bool resource_state(std::uint32_t address, std::uint32_t& value) const {
        if (owner_ != this) return owner_->resource_state(address, value);
        for (unsigned i = 0; i != resource_count_; ++i) {
            if (resource_[i].address == address && resource_[i].valid) {
                value = resource_[i].value;
                return true;
            }
        }
        return false;
    }

    bool resource_pending(std::uint32_t address, bool& pending) const {
        if (owner_ != this) return owner_->resource_pending(address, pending);
        const int index = resource_index(address);
        if (index < 0) return false;
        pending = false;
        for (const auto& vote : resource_[index].vote) pending |= vote.staged;
        return true;
    }

    std::uint64_t submissions() const { return submissions_; }

private:
    static constexpr unsigned command_count = 16; // Storage capacity per TCS.
    static constexpr unsigned max_banks = 4;
    // Requester indices are shared by every controller that votes through one owner.
    static constexpr unsigned max_requesters = 8;
    static constexpr unsigned max_tcs = 6;
    struct Command {
        std::uint32_t message = 0;
        std::uint32_t address = 0;
        std::uint32_t data = 0;
        std::uint32_t status = 0;
    };
    struct Tcs {
        std::uint32_t wait = 0;
        std::uint32_t control = 0;
        std::uint32_t enable = 0;
        std::array<Command, command_count> command{};
    };
    struct Bank {
        unsigned count = 0;
        unsigned active = 0;
        unsigned requester = 0;
        std::uint32_t irq_enable = 0;
        std::uint32_t irq_status = 0;
        std::uint32_t configuration = 0;
        std::array<Tcs, max_tcs> tcs{};
    };
    struct Vote {
        std::uint32_t value = 0;
        std::uint32_t pending = 0;
        bool present = false;
        bool staged = false;
    };
    struct Resource {
        std::uint32_t address = 0;
        std::uint32_t value = 0;
        bool valid = false;
        std::array<Vote, max_requesters> vote{};
    };
    std::array<Bank, max_banks> bank_{};
    std::array<Resource, 128> resource_{};
    const RpmhResourceDirectory* directory_ = nullptr;
    std::array<QcomRpmhRsc*, max_requesters> requester_{};
    QcomRpmhRsc* owner_ = this;
    unsigned resource_count_ = 0;
    unsigned bank_span_ = 0;
    std::uint32_t configuration_ = 0;
    unsigned commands_ = command_count;
    unsigned tcs_offset_ = 0xd00;
    unsigned tcs_stride_ = 0x2a0;
    std::array<std::uint32_t, 128> sequence_{};
    std::array<std::uint32_t, 4> branch_{};
    std::array<std::uint32_t, 2> timestamp_{};
    std::uint32_t timer_owner_ = 0;
    std::uint64_t submissions_ = 0;

    void end_of_elaboration() override {
        commands_ = commands.get_value();
        if (!commands_ || commands_ > command_count)
            throw std::invalid_argument("The RSC command count is invalid");
        configuration_ = commands_ << 27;
        tcs_offset_ = tcs_offset.get_value();
        tcs_stride_ = tcs_stride.get_value();
        if (tcs_offset_ < 0x10 || (tcs_offset_ & 3) || (tcs_stride_ & 3) ||
            tcs_stride_ < 0x14 + commands_ * 0x18 ||
            tcs_offset_ + 0x20 + max_tcs * tcs_stride_ > 0x10000)
            throw std::invalid_argument("The RSC TCS layout is invalid");
        if (sequencer_words.get_value() > sequence_.size())
            throw std::invalid_argument("The RSC sequence exceeds its storage capacity");
        if (!resource_owner_path.get_value().empty()) {
            const auto& path = resource_owner_path.get_value();
            owner_ = dynamic_cast<QcomRpmhRsc*>(sc_core::sc_find_object(path.c_str()));
            if (!owner_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                owner_ = dynamic_cast<QcomRpmhRsc*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!owner_ || owner_ == this || !owner_->resource_owner_path.get_value().empty() ||
                !resources.get_value().empty())
                throw std::invalid_argument("The RSC requires one direct resource owner");
        }
        std::istringstream rows(banks.get_value());
        std::string row;
        while (std::getline(rows, row, ';')) {
            std::istringstream fields(row);
            unsigned id = 0, count = 0, active = 0;
            std::string extra;
            if (!(fields >> id >> count >> active) || id >= max_banks || !count || count > max_tcs ||
                !active || active > count || bank_[id].count)
                throw std::invalid_argument("The RSC bank geometry is invalid");
            // A fourth field names the requester. Otherwise the requester follows requester_base.
            unsigned requester = requester_base.get_value() + id;
            if (fields >> extra) {
                std::size_t used = 0;
                try {
                    requester = static_cast<unsigned>(std::stoul(extra, &used, 0));
                } catch (const std::exception&) {
                    used = 0;
                }
                if (used != extra.size() || (fields >> extra))
                    throw std::invalid_argument("The RSC bank geometry is invalid");
            }
            if (requester >= max_requesters) throw std::invalid_argument("The RSC requester index is invalid");
            if (owner_->requester_[requester])
                throw std::invalid_argument("Two RSC banks use the same requester index");
            owner_->requester_[requester] = this;
            bank_[id].requester = requester;
            bank_[id].count = count;
            bank_[id].active = active;
            bank_span_ = std::max(bank_span_, id + 1);
            configuration_ |= count << (id * 6);
        }
        if (!bank_span_) throw std::invalid_argument("The RSC requires a bank");
        if (owner_ != this) {
            if (!directory_path.get_value().empty())
                throw std::invalid_argument("Only the RSC resource owner takes a directory");
            return;
        }
        if (!directory_path.get_value().empty()) {
            const auto& path = directory_path.get_value();
            directory_ = dynamic_cast<const RpmhResourceDirectory*>(sc_core::sc_find_object(path.c_str()));
            if (!directory_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                directory_ = dynamic_cast<const RpmhResourceDirectory*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!directory_) throw std::invalid_argument("The RSC directory is not a Command DB directory");
        }
        std::istringstream input(resources.get_value());
        std::string item;
        while (std::getline(input, item, ',')) {
            std::size_t used = 0;
            const auto address = std::stoull(item, &used, 0);
            const bool arc = address >= 0x30000 && address <= 0x300e0 && !(address & 0xf);
            const bool bcm = address >= 0x50000 && address <= 0x5007c && !(address & 3);
            if (used != item.size() || (!arc && !bcm) || resource_count_ == resource_.size())
                throw std::invalid_argument("The RSC resource has no supported vote format");
            if (resource_index(static_cast<std::uint32_t>(address)) >= 0)
                throw std::invalid_argument("The RSC resource address is duplicated");
            resource_[resource_count_++].address = static_cast<std::uint32_t>(address);
        }
        if (!resource_count_) throw std::invalid_argument("The RSC requires an explicit resource list");
    }

    int resource_index(std::uint32_t address) const {
        for (unsigned i = 0; i != resource_count_; ++i) {
            if (resource_[i].address == address) return static_cast<int>(i);
        }
        return -1;
    }

    // A published VRM register gets its resource entry at the first vote.
    int vote_resource(std::uint32_t address) {
        const int index = resource_index(address);
        if (index >= 0 || !directory_ || address < 0x40000 || address >= 0x50000 ||
            !directory_->published_vrm(address))
            return index;
        if (resource_count_ == resource_.size()) {
            std::cerr << "RPMH_RESOURCE_CAPACITY_EXCEEDED model=" << name() << std::hex
                      << " address=0x" << address << std::dec << '\n';
            return -1;
        }
        std::cerr << "RPMH_VRM_RESOURCE model=" << name() << std::hex << " address=0x" << address
                  << std::dec << '\n';
        resource_[resource_count_].address = address;
        return static_cast<int>(resource_count_++);
    }

    void update_irq(unsigned id) {
        auto* output = id == 0 ? &irq_0 : id == 1 ? &irq_1 : id == 2 ? &irq_2 : &irq_3;
        const bool level = (bank_[id].irq_status & bank_[id].irq_enable) != 0;
        for (int port = 0; port < output->size(); ++port) (*output)[port]->write(level);
    }

    static void aggregate(Resource& resource) {
        resource.value = 0;
        resource.valid = false;
        if (resource.address < 0x50000) {
            for (const auto& vote : resource.vote) {
                if (!vote.present) continue;
                resource.valid = true;
                resource.value = std::max(resource.value, vote.value);
            }
            return;
        }
        // The virtual BCM uses independent maxima for X and Y. It does not model physical bandwidth.
        std::uint32_t x = 0, y = 0, valid = 0;
        for (const auto& vote : resource.vote) {
            resource.valid |= vote.present;
            if (!vote.present || !(vote.value & (1u << 29))) continue;
            valid = 1u << 29;
            x = std::max(x, (vote.value >> 14) & 0x3fff);
            y = std::max(y, vote.value & 0x3fff);
        }
        resource.value = valid | (x << 14) | y;
    }

    void apply_vote(unsigned index, unsigned requester, std::uint32_t value) {
        auto& resource = resource_[index];
        auto& vote = resource.vote[requester];
        if (resource.address < 0x50000) {
            vote.value = value;
            vote.present = true;
            aggregate(resource);
            return;
        }
        vote.pending = value & ~(1u << 30);
        vote.staged = true;
        if (!(value & (1u << 30))) return;
        // A BCM commit publishes only this requester's pending group.
        for (unsigned i = 0; i != resource_count_; ++i) {
            auto& pending = resource_[i].vote[requester];
            if (!pending.staged) continue;
            pending.value = pending.pending;
            pending.present = true;
            pending.staged = false;
            aggregate(resource_[i]);
        }
    }

    bool submit(unsigned id, unsigned index) {
        auto& bank = bank_[id];
        auto& tcs = bank.tcs[index];
        // Sleep and wake storage requires a separate lifecycle event.
        if (index >= bank.active || !tcs.enable) {
            std::cerr << "RPMH_TCS_REJECTED model=" << name() << " drv=" << id << " tcs=" << index
                      << " reason=" << (index >= bank.active ? "not-active" : "no-command") << '\n';
            return false;
        }
        std::array<unsigned, command_count> destination{};
        for (unsigned slot = 0; slot != command_count; ++slot) {
            if (!(tcs.enable & (1u << slot))) continue;
            const auto& cmd = tcs.command[slot];
            const int resource = owner_->vote_resource(cmd.address);
            // Eight-byte write request, with optional completion request. Reads need a real backend.
            if ((cmd.message != 0x10008 && cmd.message != 0x10108) || resource < 0) {
                std::cerr << "RPMH_TCS_REJECTED model=" << name() << " drv=" << id << " tcs=" << index
                          << " slot=" << slot << std::hex << " message=0x" << cmd.message
                          << " address=0x" << cmd.address << " data=0x" << cmd.data << std::dec
                          << " reason=" << (resource < 0 ? "unknown-resource" : "unsupported-message") << '\n';
                return false;
            }
            destination[slot] = static_cast<unsigned>(resource);
        }
        // Validate the entire batch before publishing any vote or completion.
        for (auto& cmd : tcs.command) cmd.status = 0;
        for (unsigned slot = 0; slot != command_count; ++slot) {
            if (!(tcs.enable & (1u << slot))) continue;
            auto& cmd = tcs.command[slot];
            owner_->apply_vote(destination[slot], bank.requester, cmd.data);
            cmd.status = 0x10101;
        }
        bank.irq_status |= 1u << index;
        update_irq(id);
        ++submissions_;
        return true;
    }

protected:
    virtual bool access(std::uint64_t offset, bool write, std::uint32_t& value) {
        const auto id = offset / 0x10000;
        offset %= 0x10000;
        if (id >= max_banks || !bank_[id].count) return false;
        auto& bank = bank_[id];
        if (offset == 0xc) {
            if (write) return false;
            value = configuration_;
            return true;
        }
        if (id == 0 && sequencer_words.get_value()) {
            if (offset == 0 || offset == 4 || offset == 8) {
                if (write) return false;
                // The virtual engine has instruction storage but no autonomous solver.
                value = offset == 8 ? (bank_span_ << 24) | (sequencer_words.get_value() << 16) : 0;
                return true;
            }
            std::uint32_t* storage = nullptr;
            std::uint32_t mask = 0xffffffff;
            if (offset == 0x1c) { storage = &timer_owner_; mask = 3; }
            else if (offset >= 0x4bc && offset <= 0x4c8) {
                storage = &branch_[(offset - 0x4bc) / 4];
                mask = 0x3ff;
            } else if (offset >= 0x550 && offset < 0x550 + sequencer_words.get_value() * 4) {
                storage = &sequence_[(offset - 0x550) / 4];
            } else if (offset == 0x4510 || offset == 0x4514) {
                storage = &timestamp_[(offset - 0x4510) / 4];
            }
            if (storage) {
                if (write) {
                    if (value & ~mask) return false;
                    *storage = value;
                } else value = *storage;
                return true;
            }
        }
        if (offset == 0xd8) {
            if (write) bank.configuration = value;
            else value = bank.configuration;
            return true;
        }
        if (offset == tcs_offset_) {
            if (write) {
                bank.irq_enable = value & ((1u << bank.count) - 1);
                update_irq(id);
            } else value = bank.irq_enable;
            return true;
        }
        if (offset == tcs_offset_ + 4) {
            if (write) return false;
            value = bank.irq_status;
            return true;
        }
        if (offset == tcs_offset_ + 8) {
            if (!write) return false;
            bank.irq_status &= ~value;
            update_irq(id);
            return true;
        }
        if (offset < tcs_offset_ + 0x20) return false;
        const auto index = (offset - tcs_offset_ - 0x20) / tcs_stride_;
        const auto field = (offset - tcs_offset_ - 0x20) % tcs_stride_;
        if (index >= bank.count) return false;
        auto& tcs = bank.tcs[index];
        if (field == 8) {
            if (write) return false;
            // Idle is not AOP readiness. This synchronous transport has no outstanding batch.
            value = 1;
            return true;
        }
        std::uint32_t* storage = nullptr;
        std::uint32_t mask = 0xffffffff;
        if (field == 0) { storage = &tcs.wait; mask = 0xffff; }
        else if (field == 4) { storage = &tcs.control; mask = 0x1010000; }
        else if (field == 12) { storage = &tcs.enable; mask = (1u << commands_) - 1; }
        else if (field >= 0x14) {
            const auto slot = (field - 0x14) / 0x18;
            const auto member = (field - 0x14) % 0x18;
            if (slot >= commands_) return false;
            auto& cmd = tcs.command[slot];
            if (member == 0) { storage = &cmd.message; mask = 0x1010f; }
            else if (member == 4) { storage = &cmd.address; mask = 0x7ffff; }
            else if (member == 8) storage = &cmd.data;
            else if (member == 12 && !write) storage = &cmd.status;
            // No invented response data for write commands.
        }
        if (!storage) return false;
        if (!write) { value = *storage; return true; }
        if (value & ~mask) return false;
        if (field == 4 && (value & 0x1000000) && !(tcs.control & 0x1000000)) {
            if (!(value & 0x10000) || !submit(id, static_cast<unsigned>(index))) return false;
        }
        *storage = value;
        return true;
    }

private:
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        auto* data = tx.get_data_ptr();
        std::uint32_t value = 0;
        if (tx.is_write()) {
            for (unsigned i = 0; i != 4; ++i) value |= std::uint32_t{data[i]} << (8 * i);
        }
        if (!access(tx.get_address(), tx.is_write(), value)) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) {
            for (unsigned i = 0; i != 4; ++i) data[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

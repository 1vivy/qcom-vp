// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace qcom_vp {

// This opt-in software adapter preserves live callbacks across two ROM interfaces.
class TmeRuntimeAbiBridge : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<TmeRuntimeAbiBridge> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<TmeRuntimeAbiBridge> initiator_socket{"initiator_socket"};
    sc_core::sc_port<sc_core::sc_signal_inout_if<bool>, 0, sc_core::SC_ZERO_OR_MORE_BOUND>
        runtime_active{"runtime_active"};
    cci::cci_param<uint64_t> legacy_base{"legacy_base", 0, "Required legacy table address"};
    cci::cci_param<uint64_t> runtime_base{"runtime_base", 0, "Required runtime table address"};
    cci::cci_param<uint64_t> enable_address{"enable_address", 0, "Required adapter activation address"};
    cci::cci_param<uint64_t> legacy_channels{"legacy_channels", 0, "Legacy channel array address"};
    cci::cci_param<uint64_t> runtime_channels{"runtime_channels", 0, "Runtime channel array address"};
    cci::cci_param<std::string> mapping_path{"mapping_path", "", "Required file of hexadecimal slot pairs"};

    explicit TmeRuntimeAbiBridge(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &TmeRuntimeAbiBridge::transport);
        target_socket.register_transport_dbg(this, &TmeRuntimeAbiBridge::debug);
        target_socket.register_get_direct_mem_ptr(this, &TmeRuntimeAbiBridge::dmi);
        initiator_socket.register_invalidate_direct_mem_ptr(this, &TmeRuntimeAbiBridge::invalidate);
    }

private:
    static constexpr unsigned legacy_size = 0xc00;
    static constexpr unsigned runtime_size = 0xc08;
    static constexpr unsigned channel_count = 3;
    static constexpr unsigned legacy_channel_stride = 0xa8;
    static constexpr unsigned runtime_channel_stride = 0x218;
    static constexpr unsigned legacy_channel_size = channel_count * legacy_channel_stride;
    static constexpr unsigned runtime_channel_size = channel_count * runtime_channel_stride;
    static constexpr uint16_t missing = 0xffff;
    static constexpr uint16_t legacy_owned = 0xfffc;
    std::array<uint16_t, legacy_size / 4> slots_{};
    uint64_t legacy_ = 0;
    uint64_t runtime_ = 0;
    uint64_t control_ = 0;
    uint64_t legacy_channels_ = 0;
    uint64_t runtime_channels_ = 0;
    bool enabled_ = false;

    void end_of_elaboration() override {
        legacy_ = legacy_base.get_value();
        runtime_ = runtime_base.get_value();
        control_ = enable_address.get_value();
        legacy_channels_ = legacy_channels.get_value();
        runtime_channels_ = runtime_channels.get_value();
        if (!legacy_ || !runtime_ || !control_ || (!legacy_channels_ != !runtime_channels_)) {
            throw std::invalid_argument("The runtime ABI addresses are invalid");
        }
        const std::array<std::pair<uint64_t, uint64_t>, 5> ranges{{
            {legacy_, legacy_size}, {runtime_, runtime_size}, {control_, 12},
            {legacy_channels_, legacy_channel_size}, {runtime_channels_, runtime_channel_size}}};
        for (unsigned i = 0; i < ranges.size(); ++i) {
            const auto [begin, size] = ranges[i];
            if (!begin) continue;
            if ((begin & 3) || begin > UINT32_MAX - size) {
                throw std::invalid_argument("The runtime ABI addresses are invalid");
            }
            for (unsigned j = 0; j < i; ++j) {
                if (ranges[j].first && begin < ranges[j].first + ranges[j].second &&
                    ranges[j].first < begin + size) {
                    throw std::invalid_argument("The runtime ABI addresses overlap");
                }
            }
        }
        slots_.fill(missing);
        std::ifstream input(mapping_path.get_value());
        if (!input) throw std::invalid_argument("The runtime ABI mapping file cannot be opened");
        unsigned old_slot = 0, count = 0;
        std::string destination;
        while (input >> std::hex >> old_slot) {
            if (!(input >> destination)) {
                throw std::invalid_argument("The runtime ABI mapping contains an invalid slot");
            }
            unsigned new_slot = legacy_owned;
            if (destination != "legacy") {
                std::string_view number(destination);
                if (number.size() >= 2 && number[0] == '0' && (number[1] == 'x' || number[1] == 'X'))
                    number.remove_prefix(2);
                const auto parsed = std::from_chars(number.data(), number.data() + number.size(), new_slot, 16);
                if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() ||
                    new_slot >= runtime_size) {
                    throw std::invalid_argument("The runtime ABI mapping contains an invalid slot");
                }
            }
            if (((old_slot | new_slot) & 3) || old_slot >= legacy_size || slots_[old_slot / 4] != missing) {
                throw std::invalid_argument("The runtime ABI mapping contains an invalid slot");
            }
            slots_[old_slot / 4] = static_cast<uint16_t>(new_slot);
            ++count;
        }
        if (!input.eof() || !count) throw std::invalid_argument("The runtime ABI mapping is incomplete");
    }

    bool legacy_address(uint64_t address) const {
        return address >= legacy_ && address - legacy_ < legacy_size;
    }

    bool channel_address(uint64_t address) const {
        return legacy_channels_ && address >= legacy_channels_ &&
            address - legacy_channels_ < legacy_channel_size;
    }

    bool migrate_channels(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!legacy_channels_) return true;
        const auto address = tx.get_address();
        auto* const data = tx.get_data_ptr();
        const auto length = tx.get_data_length();
        const auto width = tx.get_streaming_width();
        const auto command = tx.get_command();
        std::array<unsigned char, runtime_channel_stride> channel{};
        for (unsigned i = 0; i < channel_count; ++i) {
            tx.set_read();
            tx.set_address(legacy_channels_ + i * legacy_channel_stride);
            tx.set_data_ptr(channel.data());
            tx.set_data_length(legacy_channel_stride);
            tx.set_streaming_width(legacy_channel_stride);
            initiator_socket->b_transport(tx, delay);
            if (!tx.is_response_ok()) break;
            tx.set_write();
            tx.set_address(runtime_channels_ + i * runtime_channel_stride);
            tx.set_data_length(runtime_channel_stride);
            tx.set_streaming_width(runtime_channel_stride);
            initiator_socket->b_transport(tx, delay);
            if (!tx.is_response_ok()) break;
        }
        tx.set_address(address);
        tx.set_data_ptr(data);
        tx.set_data_length(length);
        tx.set_streaming_width(width);
        tx.set_command(command);
        tx.set_dmi_allowed(false);
        return tx.is_response_ok();
    }

    bool prepare(tlm::tlm_generic_payload& tx) const {
        const auto address = tx.get_address();
        const auto length = tx.get_data_length();
        if ((!tx.is_read() && !tx.is_write()) || !tx.get_data_ptr() || !length ||
            address > UINT64_MAX - (length - 1)) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        if (tx.get_streaming_width() < length) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if (enabled_) {
            const auto first = std::max<uint64_t>(address, legacy_);
            const auto last = std::min<uint64_t>(address + length - 1, legacy_ + legacy_size - 1);
            if (first <= last) {
                for (auto slot = (first - legacy_) / 4; slot <= (last - legacy_) / 4; ++slot) {
                    if (slots_[slot] == missing || (slot == 1 && tx.is_write())) {
                        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                        return false;
                    }
                }
            }
        }
        return true;
    }

    unsigned transfer(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay, bool debug_access) {
        const auto address = tx.get_address();
        const auto length = tx.get_data_length();
        auto* const data = tx.get_data_ptr();
        const auto width = tx.get_streaming_width();
        if (!prepare(tx)) return 0;
        if (address < control_ + 12 && address + length > control_) {
            tx.set_dmi_allowed(false);
            if ((address & 3) || length != 4 || debug_access ||
                (tx.is_write() && address != control_)) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return 0;
            }
            if (tx.is_write()) {
                if (data[0] != 1 || data[1] || data[2] || data[3]) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return 0;
                }
                if (!enabled_ && !migrate_channels(tx, delay)) return 0;
                enabled_ = true;
                for (int i = 0; i < runtime_active.size(); ++i) runtime_active[i]->write(true);
            } else {
                const auto value = address == control_ ? uint64_t{enabled_} :
                    address == control_ + 4 ? legacy_channels_ : runtime_channels_;
                for (unsigned i = 0; i < 4; ++i) {
                    data[i] = static_cast<unsigned char>(value >> (8 * i));
                }
            }
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return length;
        }
        const bool translated = enabled_ &&
            ((address < legacy_ + legacy_size && address + length > legacy_) ||
             (legacy_channels_ && address < legacy_channels_ + legacy_channel_size &&
              address + length > legacy_channels_));
        if (!translated) {
            if (debug_access) return initiator_socket->transport_dbg(tx);
            initiator_socket->b_transport(tx, delay);
            return tx.is_response_ok() ? length : 0;
        }
        unsigned completed = 0;
        while (completed < length) {
            const auto current = address + completed;
            const auto chunk = std::min<unsigned>(length - completed, 4 - (current & 3));
            if (legacy_address(current) && (current - legacy_) / 4 == 1) {
                // A legacy caller must use legacy indices after it reads the table address.
                for (unsigned i = 0; i < chunk; ++i) {
                    data[completed + i] = static_cast<uint8_t>(legacy_ >> (8 * ((current + i) & 3)));
                }
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
            } else {
                auto mapped = current;
                if (legacy_address(current)) {
                    const auto slot = slots_[(current - legacy_) / 4];
                    if (slot != legacy_owned) mapped = runtime_ + slot + (current & 3);
                } else if (channel_address(current)) {
                    const auto offset = current - legacy_channels_;
                    mapped = runtime_channels_ + (offset / legacy_channel_stride) * runtime_channel_stride +
                        offset % legacy_channel_stride;
                }
                tx.set_address(mapped);
                tx.set_data_ptr(data + completed);
                tx.set_data_length(chunk);
                tx.set_streaming_width(chunk);
                if (debug_access) {
                    if (initiator_socket->transport_dbg(tx) != chunk) break;
                } else {
                    initiator_socket->b_transport(tx, delay);
                    if (!tx.is_response_ok()) break;
                }
            }
            completed += chunk;
        }
        tx.set_address(address);
        tx.set_data_ptr(data);
        tx.set_data_length(length);
        tx.set_streaming_width(width);
        tx.set_dmi_allowed(false);
        return completed;
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        transfer(tx, delay, false);
    }

    unsigned debug(tlm::tlm_generic_payload& tx) {
        auto delay = sc_core::SC_ZERO_TIME;
        return transfer(tx, delay, true);
    }

    bool dmi(tlm::tlm_generic_payload& tx, tlm::tlm_dmi& region) {
        const auto address = tx.get_address();
        const std::array<std::pair<uint64_t, uint64_t>, 3> excluded{{
            {legacy_ & ~uint64_t{0xfff}, (legacy_ + legacy_size + 0xfff) & ~uint64_t{0xfff}},
            {control_ & ~uint64_t{0xfff}, (control_ + 0x1000) & ~uint64_t{0xfff}},
            {legacy_channels_ & ~uint64_t{0xfff}, legacy_channels_ ?
                (legacy_channels_ + legacy_channel_size + 0xfff) & ~uint64_t{0xfff} : 0}}};
        for (const auto& range : excluded) {
            if (address >= range.first && address < range.second) return false;
        }
        if (!initiator_socket->get_direct_mem_ptr(tx, region)) return false;
        for (const auto& range : excluded) {
            if (address < range.first && region.get_end_address() >= range.first) {
                region.set_end_address(range.first - 1);
            } else if (address >= range.second && region.get_start_address() < range.second) {
                region.set_dmi_ptr(region.get_dmi_ptr() + range.second - region.get_start_address());
                region.set_start_address(range.second);
            }
        }
        return true;
    }

    void invalidate(sc_dt::uint64 begin, sc_dt::uint64 end) {
        target_socket->invalidate_direct_mem_ptr(begin, end);
    }
};

}

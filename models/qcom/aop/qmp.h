// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/soccp/ipcc.h"
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// AOP-side QMP endpoint. The descriptor and mailbox words follow the selected
// GlinkDxe transport. See models/qcom/aop/evidence.md for the RVAs and the Linux
// descriptor cross-check. The AOP firmware is not executed. This model
// publishes the descriptor that the AOP firmware would publish.
class QcomAopQmp : public sc_core::sc_module, public IpccRemotePeer {
public:
    tlm_utils::multi_passthrough_target_socket<QcomAopQmp> target_socket{"target_socket"};
    cci::cci_param<std::string> ipcc_path{"ipcc_path", "",
        "Required APSS IPCC fabric path for the notify interrupt"};
    cci::cci_param<unsigned> ipcc_client{"ipcc_client", 0,
        "AOP IPCC client index, at most 31"};

    // Selected GlinkDxe constants.
    static constexpr std::uint32_t magic = 0x4d41494c;
    static constexpr std::uint32_t version = 1;
    static constexpr std::uint32_t features = 0;
    static constexpr std::uint32_t state_up = 0x0000ffff;
    static constexpr std::uint32_t state_down = 0xffff0000;

    // Descriptor layout. The AOP owns the endpoint at 0x0c. The APSS owns the
    // endpoint at 0x24, which the transport calls its local endpoint.
    enum : std::uint32_t {
        desc_magic = 0x00, desc_version = 0x04, desc_features = 0x08,
        ucore_link_state = 0x0c, ucore_link_state_ack = 0x10,
        ucore_ch_state = 0x14, ucore_ch_state_ack = 0x18,
        ucore_mbox_size = 0x1c, ucore_mbox_offset = 0x20,
        mcore_link_state = 0x24, mcore_link_state_ack = 0x28,
        mcore_ch_state = 0x2c, mcore_ch_state_ack = 0x30,
        mcore_mbox_size = 0x34, mcore_mbox_offset = 0x38,
        apss_mailbox = 0x40, apss_mailbox_size = 0x180,
        aop_mailbox = 0x200, aop_mailbox_size = 0x180,
        region_size = 0x400,
    };

    explicit QcomAopQmp(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomAopQmp::transport);
        publish();
    }

    void cold_reset() { publish(); }

    // Consumer-visible records of the declared effects. The platform has no
    // AOP clock controller and no DDR refresh model. See docs/qcom/sm88xx/stubs.md.
    int clock_vote(const std::string& resource) const {
        const auto found = clock_votes_.find(resource);
        return found == clock_votes_.end() ? -1 : found->second;
    }
    unsigned accepted_messages() const { return accepted_; }
    unsigned rejected_messages() const { return rejected_; }
    unsigned pasr_requests() const { return pasr_requests_; }

    bool consume_doorbell() override {
        if (!fabric_) return false;
        bool notify = false;
        // Ack the APSS link state. The AOP link state is up after AOP boot.
        if (word(mcore_link_state) != word(mcore_link_state_ack)) {
            word(mcore_link_state_ack) = word(mcore_link_state);
            notify = true;
        }
        // Ack the APSS channel state. The AOP opens aop_apps with the client.
        if (word(mcore_ch_state) != word(mcore_ch_state_ack)) {
            word(mcore_ch_state_ack) = word(mcore_ch_state);
            word(ucore_ch_state) = word(mcore_ch_state) == state_up ? state_up : state_down;
            notify = true;
        }
        const std::uint32_t length = word(apss_mailbox);
        if (length) {
            std::string reason;
            if (word(ucore_ch_state) != state_up || word(mcore_ch_state) != state_up) {
                reason = "channel not connected";
            } else if (length > apss_mailbox_size - 4) {
                reason = "length exceeds the mailbox";
            } else if (std::string text; read_text(length, text)) {
                if (apply(text)) {
                    // The length clears only after the declared effect.
                    word(apss_mailbox) = 0;
                    notify = true;
                } else {
                    reason = "unknown message";
                }
            } else {
                reason = "message has no terminator";
            }
            if (!reason.empty()) {
                ++rejected_;
                std::cerr << "AOP_QMP_REJECT reason=" << reason << " length=" << length
                          << " text=" << escaped(length) << '\n';
            }
        }
        if (notify) fabric_->notify_remote(ipcc_client.get_value());
        return true;
    }

private:
    std::array<std::uint32_t, region_size / 4> words_{};
    std::uint32_t& word(std::uint32_t offset) { return words_[offset / 4]; }
    std::uint32_t word(std::uint32_t offset) const { return words_[offset / 4]; }
    std::map<std::string, int> clock_votes_;
    unsigned accepted_ = 0, rejected_ = 0, pasr_requests_ = 0;
    QcomIpccLocal* fabric_ = nullptr;

    static std::uint32_t load(const tlm::tlm_generic_payload& tx) {
        const auto* p = tx.get_data_ptr();
        return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) |
            (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
    }
    static void store(tlm::tlm_generic_payload& tx, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
    }

    void publish() {
        words_.fill(0);
        clock_votes_.clear();
        accepted_ = rejected_ = pasr_requests_ = 0;
        word(desc_magic) = magic;
        word(desc_version) = version;
        word(desc_features) = features;
        word(ucore_link_state) = state_up;
        word(ucore_ch_state) = state_down;
        word(mcore_link_state) = state_down;
        word(mcore_ch_state) = state_down;
        word(ucore_mbox_size) = aop_mailbox_size;
        word(ucore_mbox_offset) = aop_mailbox;
        word(mcore_mbox_size) = apss_mailbox_size;
        word(mcore_mbox_offset) = apss_mailbox;
    }

    void end_of_elaboration() override {
        if (ipcc_client.get_value() > 31)
            throw std::invalid_argument("QMP requires a valid AOP IPCC client index");
        const auto& path = ipcc_path.get_value();
        if (path.empty())
            throw std::invalid_argument("QMP requires the APSS IPCC fabric path");
        fabric_ = dynamic_cast<QcomIpccLocal*>(resolve(path));
        if (!fabric_)
            throw std::invalid_argument("QMP requires an IPCC fabric at ipcc_path");
    }
    void start_of_simulation() override {
        if (!fabric_->has_remote(ipcc_client.get_value()))
            throw std::invalid_argument("QMP IPCC client has no attached doorbell target");
    }

    sc_core::sc_object* resolve(const std::string& path) {
        if (auto* object = sc_core::sc_find_object(path.c_str())) return object;
        if (!get_parent_object()) return nullptr;
        const std::string relative = std::string(get_parent_object()->name()) + "." + path;
        return sc_core::sc_find_object(relative.c_str());
    }

    bool readable_word(std::uint32_t offset) const {
        return offset <= mcore_mbox_offset ||
            (offset >= apss_mailbox && offset < apss_mailbox + apss_mailbox_size) ||
            (offset >= aop_mailbox && offset < aop_mailbox + aop_mailbox_size);
    }

    bool write_word(std::uint32_t offset, std::uint32_t value) {
        // The APSS writes only the ACK fields of the remote endpoint
        // and its own state fields. Everything else belongs to the AOP.
        switch (offset) {
        case ucore_link_state_ack:
        case ucore_ch_state_ack:
        case mcore_link_state:
        case mcore_ch_state:
            word(offset) = value;
            return true;
        case apss_mailbox:
            if (value > apss_mailbox_size - 4) return false;
            word(offset) = value;
            return true;
        case aop_mailbox:
            if (value != 0) return false;
            word(offset) = 0;
            return true;
        default:
            if (offset <= apss_mailbox || offset >= apss_mailbox + apss_mailbox_size ||
                (offset & 3)) return false;
            word(offset) = value;
            return true;
        }
    }

    std::string escaped(std::uint32_t length) const {
        std::string out;
        for (std::uint32_t i = 0; i < length && i < 96; ++i) {
            const auto byte = static_cast<unsigned char>(
                word(apss_mailbox + 4 + i) >> (8 * ((apss_mailbox + 4 + i) % 4)));
            out.push_back(byte >= 32 && byte < 127 ? static_cast<char>(byte) : '.');
        }
        return out;
    }

    // UEFI sends a fixed buffer and leaves bytes after the terminator undefined.
    // Only the bytes before the first zero byte belong to the message.
    bool read_text(std::uint32_t length, std::string& text) const {
        text.clear();
        for (std::uint32_t i = 0; i < length; ++i) {
            const std::uint32_t address = apss_mailbox + 4 + i;
            const auto byte =
                static_cast<unsigned char>(word(address) >> (8 * (address % 4)));
            if (byte == 0) return true;
            if (byte < 32 || byte >= 127) return false;
            text.push_back(static_cast<char>(byte));
        }
        return false;
    }

    bool parse_clock(const std::string& text, std::string& resource, int& value) const {
        static const std::string prefix = "{class: clock, res: ";
        static const std::string middle = ", val: ";
        if (text.size() <= prefix.size() || text.compare(0, prefix.size(), prefix) != 0)
            return false;
        const auto middle_at = text.find(middle, prefix.size());
        if (middle_at == std::string::npos || text.back() != '}') return false;
        resource = text.substr(prefix.size(), middle_at - prefix.size());
        const std::string number = text.substr(middle_at + middle.size(),
            text.size() - middle_at - middle.size() - 1);
        if (number.empty() || number.size() > 11) return false;
        char* end = nullptr;
        const long parsed = std::strtol(number.c_str(), &end, 10);
        if (!end || *end != '\0') return false;
        value = static_cast<int>(parsed);
        return true;
    }

    bool parse_pasr(const std::string& text, std::uint32_t& high, std::uint32_t& low) const {
        static const std::string prefix = "{class: ddr, event: pasr, addr_hi: 0x";
        static const std::string middle = ", addr_lo: 0x";
        static const std::string suffix = ", refresh: off}";
        if (text.size() != prefix.size() + 8 + middle.size() + 8 + suffix.size())
            return false;
        if (text.compare(0, prefix.size(), prefix) != 0) return false;
        if (text.compare(text.size() - suffix.size(), suffix.size(), suffix) != 0)
            return false;
        const std::string high_text = text.substr(prefix.size(), 8);
        const std::string rest = text.substr(prefix.size() + 8, middle.size());
        if (rest != middle) return false;
        const std::string low_text = text.substr(prefix.size() + 8 + middle.size(), 8);
        high = static_cast<std::uint32_t>(std::strtoul(high_text.c_str(), nullptr, 16));
        low = static_cast<std::uint32_t>(std::strtoul(low_text.c_str(), nullptr, 16));
        return true;
    }

    bool apply(const std::string& text) {
        std::string resource;
        int value = 0;
        if (parse_clock(text, resource, value)) {
            // qdss and qdss_ao are the two clock resources.
            if (resource != "qdss" && resource != "qdss_ao") return false;
            clock_votes_[resource] = value;
            ++accepted_;
            std::cerr << "AOP_QMP_CLOCK_VOTE res=" << resource << " val=" << value << '\n';
            return true;
        }
        std::uint32_t high = 0, low = 0;
        if (parse_pasr(text, high, low)) {
            ++pasr_requests_;
            ++accepted_;
            std::cerr << "AOP_QMP_PASR addr_hi=0x" << std::hex << high << " addr_lo=0x"
                      << low << std::dec << " refresh=off\n";
            return true;
        }
        return false;
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE); return;
        }
        if (tx.get_address() >= region_size) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
        }
        const auto offset = static_cast<std::uint32_t>(tx.get_address());
        if (tx.is_read()) {
            if (!readable_word(offset)) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
            }
            store(tx, word(offset));
        } else if (tx.is_write()) {
            if (!write_word(offset, load(tx))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
            }
        } else {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

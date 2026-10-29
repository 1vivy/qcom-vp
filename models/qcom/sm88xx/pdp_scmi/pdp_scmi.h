// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "service.h"
#include "qcom/qcom_mailbox/mailbox.h"
#include "qcom/sm88xx/tme_mailbox/image_handoff.h"
#include <tlm_utils/simple_initiator_socket.h>
#include <string>
#include <iostream>

namespace qcom_vp {

// Synchronous virtual service. The genuine firmware observer owns all authentication decisions.
class QcomPdpScmi : public sc_core::sc_module, public ImageHandoffSink,
                    public MailboxDoorbellSink, public PerformanceResource {
public:
    tlm_utils::simple_initiator_socket<QcomPdpScmi> memory_socket{"memory_socket"};
    cci::cci_param<std::string> observer_path{"observer_path", "", "Authenticated image observer"};
    cci::cci_param<std::string> tx0_path{"tx0_path", "", "PDP0 TX mailbox object"};
    cci::cci_param<std::string> tx1_path{"tx1_path", "", "PDP1 TX mailbox object"};
    cci::cci_param<std::string> rx_path{"rx_path", "", "Single shared PDP RX bank"};
    cci::cci_param<uint64_t> slot0{"slot0", 0x81f08100, "Selected PDP0 request slot"};
    cci::cci_param<uint64_t> slot1{"slot1", 0x81f08180, "Selected PDP1 request slot"};

    explicit QcomPdpScmi(sc_core::sc_module_name name) : sc_module(name) {}

    bool on_image_handoff(const ImageHandoffEvent& event, sc_core::sc_time& delay) override {
        if (event.image_id != 0x91 || !event.generation || !event.sequence || faulted_) return false;
        // The observer contract admits once per cold module lifetime. It has no warm-reset API.
        if (admitted_) return event.generation == admission_.generation && event.sequence == admission_.sequence;
        if (!rx_ || servicing_) return false;
        servicing_ = true;
        const bool ok = write_word(endpoints_[0].slot + 4, 1, delay) &&
            write_word(endpoints_[1].slot + 4, 1, delay);
        servicing_ = false;
        if (!ok) { faulted_ = true; return false; }
        admission_ = event;
        admitted_ = true;
        std::cerr << "PDP_VIRTUAL_AVAILABLE name=" << name() << " image=145 generation="
                  << event.generation << " sequence=" << event.sequence << '\n';
        return true;
    }

    bool performance_state(unsigned domain, PerformanceResourceState& state) const override {
        const auto* current = service_.state(domain);
        if (!admitted_ || faulted_ || !current) return false;
        state = *current;
        return true;
    }

    bool mailbox_doorbell(unsigned endpoint, unsigned channel, uint32_t payload,
        sc_core::sc_time& delay) override {
        if (!admitted_ || faulted_ || servicing_ || endpoint >= endpoints_.size() || channel != 0 ||
            payload != doorbell || !rx_->available(endpoint, *this)) return false;
        servicing_ = true;
        const bool ok = respond(endpoints_[endpoint], endpoint, delay);
        servicing_ = false;
        return ok;
    }

private:
    static constexpr uint32_t doorbell = 0x494d4353;
    struct Endpoint { uint64_t slot = 0; };
    std::array<Endpoint, 2> endpoints_{};
    QcomMailboxRxBank* rx_ = nullptr;
    ImageHandoffEvent admission_{};
    pdp_scmi_detail::Service service_;
    bool admitted_ = false, faulted_ = false, servicing_ = false;

    template<class T> T* resolve(const std::string& path) {
        if (path.empty()) return nullptr;
        auto* object = dynamic_cast<T*>(sc_core::sc_find_object(path.c_str()));
        if (!object && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            object = dynamic_cast<T*>(sc_core::sc_find_object(relative.c_str()));
        }
        return object;
    }
    void end_of_elaboration() override {
        // This compatible covers only the two selected UEFI slots, never CPUCP's incoming slot.
        if (slot0.get_value() != 0x81f08100 || slot1.get_value() != 0x81f08180)
            throw std::invalid_argument("PDP requires the selected disjoint UEFI request slots");
        endpoints_[0].slot = slot0.get_value(); endpoints_[1].slot = slot1.get_value();
        auto* observer = resolve<ImageHandoffSource>(observer_path.get_value());
        auto* tx0 = resolve<QcomMailbox>(tx0_path.get_value());
        auto* tx1 = resolve<QcomMailbox>(tx1_path.get_value());
        rx_ = resolve<QcomMailboxRxBank>(rx_path.get_value());
        if (!observer || !tx0 || !tx1 || tx0 == tx1 || !rx_ ||
            !tx0->attach(*this, 0) || !tx1->attach(*this, 1) ||
            !rx_->claim(0, *this) || !rx_->claim(1, *this) || !observer->subscribe(0x91, *this))
            throw std::invalid_argument("PDP requires unique TX blocks, one RX bank, and image admission");
    }
    bool memory(bool write, uint64_t address, unsigned char* bytes, unsigned size, sc_core::sc_time& delay) {
        bool within = false;
        for (const auto& endpoint : endpoints_)
            within = within || (address >= endpoint.slot && size <= 0x80 &&
                address - endpoint.slot <= 0x80 - size);
        if (!within || !size) return false;
        tlm::tlm_generic_payload tx;
        tx.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        tx.set_address(address); tx.set_data_ptr(bytes); tx.set_data_length(size);
        tx.set_streaming_width(size); tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        memory_socket->b_transport(tx, delay);
        if (tx.is_response_ok()) return true;
        faulted_ = true; // Do not retry a partly published transaction as a new request.
        return false;
    }
    bool write_word(uint64_t address, uint32_t value, sc_core::sc_time& delay) {
        std::array<unsigned char, 4> bytes{};
        pdp_scmi_detail::put(bytes.data(), value);
        return memory(true, address, bytes.data(), bytes.size(), delay);
    }
    bool respond(const Endpoint& endpoint, unsigned channel, sc_core::sc_time& delay) {
        std::array<unsigned char, 128> request{}, current{};
        if (!memory(false, endpoint.slot, request.data(), 28, delay)) return false;
        const auto status = pdp_scmi_detail::word(request.data() + 4);
        const auto flags = pdp_scmi_detail::word(request.data() + 16);
        const auto length = pdp_scmi_detail::word(request.data() + 20);
        const auto header = pdp_scmi_detail::word(request.data() + 24);
        // FREE, error, unknown status/flag bits, reserved header bits, and non-command types fail closed.
        if (status != 0 || (flags & ~1u) || length < 4 || length > 104 || (length & 3) ||
            (header & 0xf0000300u)) return false;
        const unsigned payload_size = length - 4;
        if (payload_size && !memory(false, endpoint.slot + 28, request.data() + 28, payload_size, delay))
            return false;
        auto reply = service_.request((header >> 10) & 0xff, header & 0xff,
            request.data() + 28, payload_size);
        // Reject any ownership, header, length, flags, or payload change before publication.
        if (!memory(false, endpoint.slot, current.data(), 28 + payload_size, delay) ||
            !std::equal(request.begin(), request.begin() + 28 + payload_size, current.begin())) return false;
        if (!memory(true, endpoint.slot + 28, reply.bytes.data(), reply.size, delay) ||
            !write_word(endpoint.slot + 20, reply.size + 4, delay) ||
            !write_word(endpoint.slot + 24, header, delay)) return false;
        // A downstream target may yield. Do not complete if another writer changed the publication.
        if (!memory(false, endpoint.slot, current.data(), 28 + reply.size, delay) ||
            !std::equal(request.begin(), request.begin() + 20, current.begin()) ||
            pdp_scmi_detail::word(current.data() + 20) != reply.size + 4 ||
            pdp_scmi_detail::word(current.data() + 24) != header ||
            !std::equal(reply.bytes.begin(), reply.bytes.begin() + reply.size, current.begin() + 28))
            return false;
        // Retain the effect before FREE. Roll back if the ownership publication fails.
        const auto previous = *service_.state(reply.domain);
        service_.commit(reply);
        if (!write_word(endpoint.slot + 4, 1, delay)) {
            if (reply.changes_state) { reply.state = previous; service_.commit(reply); }
            return false;
        }
        // Responses precede FREE. FREE precedes command publication and pending/IRQ assertion.
        if (!rx_->complete(channel, *this, doorbell)) { faulted_ = true; return false; }
        return true;
    }
};
} // namespace qcom_vp

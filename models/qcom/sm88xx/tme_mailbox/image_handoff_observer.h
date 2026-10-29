// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "image_handoff.h"
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace qcom_vp {

// Passive OEM mailbox observation. Only the original firmware writes authentication results.
class AuthenticatedImageHandoff : public sc_core::sc_module, public ImageHandoffSource {
public:
    tlm_utils::multi_passthrough_target_socket<AuthenticatedImageHandoff> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<AuthenticatedImageHandoff> backing_socket{"backing_socket"};
    tlm_utils::simple_initiator_socket<AuthenticatedImageHandoff> memory_socket{"memory_socket"};
    cci::cci_param<uint64_t> current_image_context_address{
        "current_image_context_address", 0, "TME current-image word address; zero disables admission"};

    explicit AuthenticatedImageHandoff(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &AuthenticatedImageHandoff::transport);
        target_socket.register_transport_dbg(this, &AuthenticatedImageHandoff::debug);
        target_socket.register_get_direct_mem_ptr(this, &AuthenticatedImageHandoff::dmi);
    }

    // Subscription is an elaboration-time capability. Services cannot manufacture admission.
    bool subscribe(uint32_t image_id, ImageHandoffSink& sink) override {
        if (running_ || !image_id) return false;
        for (auto& subscriber : subscribers_) {
            if (subscriber.sink == &sink && subscriber.image == image_id) return true;
        }
        for (auto& subscriber : subscribers_) {
            if (!subscriber.sink) {
                subscriber = {&sink, image_id, false};
                return true;
            }
        }
        return false;
    }

private:
    static constexpr uint32_t aperture = 0x3800;
    static constexpr uint32_t request_header = 0x2800;
    static constexpr uint32_t stride = 0x14;
    static constexpr uint32_t generation = 1; // A module lifetime is one cold generation.
    struct Subscriber {
        ImageHandoffSink* sink = nullptr;
        uint32_t image = 0;
        bool admitted = false;
    };
    struct Header {
        uint32_t signature = 0, capacity = 0, produced = 0, released = 0;
    };
    struct Pending {
        std::array<unsigned char, stride> request{};
        uint32_t sequence = 0, opcode = 0, image = 0, epoch = 0;
        uint32_t request_capacity = 0, response_capacity = 0, response_fresh = 0;
        bool eligible = false, segment_complete = false;
    };
    std::array<Subscriber, 16> subscribers_{};
    std::array<Pending, 20> pending_{};
    std::array<uint32_t, 20> request_fresh_{};
    uint64_t context_ = 0;
    uint32_t last_sequence_ = 0, epoch_ = 0, image_ = 0;
    bool metadata_ = false, segments_ = false, running_ = false, poisoned_ = false;

    void end_of_elaboration() override {
        context_ = current_image_context_address.get_value();
        if (context_ && ((context_ & 3) || context_ > UINT32_MAX - 3 ||
            (context_ >= 0x02000000 && context_ < 0x02000000 + aperture)))
            throw std::invalid_argument("Image context must be an aligned non-mailbox TME word");
    }
    void start_of_simulation() override { running_ = true; }

    static uint32_t word(const unsigned char* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
            (uint32_t(p[3]) << 24);
    }
    template<class Socket>
    static bool read_bytes(Socket& socket, uint64_t address, unsigned char* bytes, unsigned size) {
        tlm::tlm_generic_payload tx;
        tx.set_command(tlm::TLM_READ_COMMAND);
        tx.set_address(address);
        tx.set_data_ptr(bytes);
        tx.set_data_length(size);
        tx.set_streaming_width(size);
        auto delay = sc_core::SC_ZERO_TIME;
        socket->b_transport(tx, delay);
        return tx.is_response_ok();
    }
    bool header(uint32_t address, Header& h) {
        std::array<unsigned char, 16> bytes{};
        if (!read_bytes(backing_socket, address, bytes.data(), bytes.size())) return false;
        h = {word(bytes.data()), word(bytes.data() + 4), word(bytes.data() + 8),
            word(bytes.data() + 12)};
        return true;
    }
    bool headers(Header& request, Header& response) {
        return header(request_header, request) && header(0, response) &&
            request.signature == 0x43535841 && response.signature == 0x57464d54 &&
            (request.capacity == 2 || request.capacity == 20) && response.capacity == 2 &&
            request.produced >= request.released && response.produced >= response.released &&
            request.produced - request.released <= request.capacity &&
            response.produced - response.released <= response.capacity;
    }
    bool context(uint32_t& id) {
        std::array<unsigned char, 4> bytes{};
        if (!read_bytes(memory_socket, context_, bytes.data(), bytes.size())) return false;
        id = word(bytes.data());
        return true;
    }
    // Selected OEM wrappers stage these inputs in the existing shared TME RAM alias.
    // Other payload address domains are unsupported, not speculative translations.
    bool payload(uint32_t address, uint32_t length, unsigned char* bytes, unsigned size) {
        if (address >= 0x20400000 && address < 0x2045c000) address -= 0x20000000;
        if (address < 0x400000 || address >= 0x45c000 || length > 0x45c000 - address ||
            size > length) return false;
        return read_bytes(memory_socket, address, bytes, size);
    }
    void discard_lineage() {
        epoch_ = image_ = 0;
        metadata_ = segments_ = false;
    }
    void discard_pending() {
        discard_lineage();
        pending_.fill(Pending{});
        request_fresh_.fill(0);
    }
    static bool authentication(uint32_t opcode) {
        return opcode == 6 || opcode == 7 || opcode == 0x10 || opcode == 0x11 || opcode == 0x12;
    }
    static uint32_t written_mask(uint64_t address, unsigned length, uint32_t base, unsigned size) {
        // The caller bounds the transaction to the aperture. Fields contain at most twenty bytes.
        const auto end = address + length;
        if (address >= base + size || end <= base) return 0;
        const auto first = address > base ? unsigned(address - base) : 0;
        const auto last = end < base + size ? unsigned(end - base) : size;
        return ((uint32_t{1} << (last - first)) - 1) << first;
    }

    void publish() {
        Header request, response;
        if (!headers(request, response)) { discard_pending(); return; }
        if (!request.produced && !last_sequence_) { discard_pending(); return; }
        // Never reinterpret a counter rollback, wrap, or repeated publication as a cold reset.
        if (!request.produced || request.produced <= last_sequence_) {
            poisoned_ = true;
            discard_pending();
            return;
        }
        const bool consecutive = request.produced == last_sequence_ + 1;
        last_sequence_ = request.produced;
        const auto index = (request.produced - 1) % request.capacity;
        const auto fresh = request_fresh_[index];
        request_fresh_[index] = 0;
        if (!consecutive || pending_[index].sequence || fresh != 0xfffff ||
            request.produced == request.released) {
            discard_pending();
            return;
        }
        Pending p;
        if (!read_bytes(backing_socket, request_header + 16 + index * stride,
                        p.request.data(), p.request.size())) { discard_pending(); return; }
        p.sequence = word(p.request.data());
        p.opcode = word(p.request.data() + 4);
        p.request_capacity = request.capacity;
        p.response_capacity = response.capacity;
        if (p.sequence != request.produced || word(p.request.data() + 16) != 0) {
            discard_pending();
            return;
        }
        const auto address = word(p.request.data() + 8);
        const auto length = word(p.request.data() + 12);
        std::array<unsigned char, 8> input{};
        uint32_t current = 0;
        if (p.opcode == 6) {
            discard_lineage(); // Even failed or overlapping metadata invalidates the previous image.
            epoch_ = p.sequence;
            if (length >= 0xa4 && payload(address, length, input.data(), 4)) {
                p.image = word(input.data());
                image_ = p.image;
                p.eligible = p.image != 0;
            }
        } else if (p.opcode == 7) {
            p.eligible = metadata_ && length >= 0x1c &&
                payload(address, length, input.data(), input.size()) &&
                context(current) && current == image_ && word(input.data()) == image_;
            const auto operation = word(input.data() + 4);
            p.eligible = p.eligible && operation >= 1 && operation <= 4;
            p.segment_complete = operation == 3;
            p.image = image_;
        } else if (p.opcode == 0x10) {
            // This handler checks all required segments and clears the authentication context.
            p.eligible = metadata_ && length >= 0x59 &&
                payload(address, length, input.data(), 4) && context(current) &&
                current == image_ && word(input.data()) == image_;
            p.image = current;
        } else if (p.opcode == 0x11) {
            p.eligible = metadata_ && length >= 0x10 &&
                payload(address, length, input.data(), input.size()) &&
                context(current) && current == image_ && word(input.data()) == image_;
            p.image = image_;
            p.segment_complete = true;
        } else if (p.opcode == 0x12) {
            // First capture: the handler clears this context before its response publication.
            p.eligible = metadata_ && segments_ && length == 1 &&
                payload(address, length, input.data(), 1) && input[0] == 1 &&
                context(current) && current == image_;
            p.image = current;
        }
        p.epoch = epoch_;
        if (authentication(p.opcode)) {
            for (const auto& other : pending_)
                if (other.sequence && authentication(other.opcode)) p.eligible = false;
            if (!p.eligible) discard_lineage();
        }
        pending_[index] = p;
    }

    void admit(const Pending& p) {
        const ImageHandoffEvent event{generation, p.sequence, p.image};
        bool emitted = false;
        for (auto& subscriber : subscribers_) {
            if (!subscriber.sink || subscriber.image != p.image || subscriber.admitted) continue;
            subscriber.admitted = true; // Callback failure cannot retry or change authentication.
            emitted = true;
            auto delay = sc_core::SC_ZERO_TIME;
            bool success = false;
            try { success = subscriber.sink->on_image_handoff(event, delay); }
            catch (const std::exception& error) {
                std::cerr << "IMAGE_HANDOFF_VIRTUAL_SERVICE_EXCEPTION name=" << name()
                          << " reason=" << error.what() << '\n';
            } catch (...) {
                std::cerr << "IMAGE_HANDOFF_VIRTUAL_SERVICE_EXCEPTION name=" << name() << '\n';
            }
            if (!success)
                std::cerr << "IMAGE_HANDOFF_VIRTUAL_SERVICE_FAILED generation=" << generation
                          << " sequence=" << p.sequence << " image=" << p.image << '\n';
        }
        if (emitted)
            std::cerr << "IMAGE_HANDOFF_ADMITTED generation=" << generation
                      << " sequence=" << p.sequence << " image=" << p.image << '\n';
    }

    void release(unsigned index) {
        const auto p = pending_[index];
        pending_[index] = Pending{};
        request_fresh_[index] = 0;
        if (!p.sequence || !authentication(p.opcode)) return;
        Header request, response;
        std::array<unsigned char, stride> slot{};
        std::array<unsigned char, 16> completion{};
        bool accepted = p.eligible && p.epoch && p.epoch == epoch_ && p.response_fresh == 0xffff &&
            headers(request, response) && request.capacity == p.request_capacity &&
            response.capacity == p.response_capacity && request.produced >= p.sequence &&
            request.produced - p.sequence < request.capacity &&
            read_bytes(backing_socket, request_header + 16 + index * stride, slot.data(), slot.size()) &&
            word(slot.data() + 16) == 1;
        for (unsigned i = 0; accepted && i < 16; ++i) accepted = slot[i] == p.request[i];
        accepted = accepted && read_bytes(backing_socket,
            16 + ((p.sequence - 1) % p.response_capacity) * stride,
            completion.data(), completion.size()) && word(completion.data()) == p.sequence &&
            word(completion.data() + 4) == p.opcode && word(completion.data() + 8) == 2 &&
            word(completion.data() + 12) == 0;
        uint32_t current = 0;
        if (accepted && p.opcode != 0x10 && p.opcode != 0x12)
            accepted = context(current) && current == p.image;
        if (!accepted) { discard_lineage(); return; }
        if (p.opcode == 6) metadata_ = true;
        else if (p.opcode == 7 || p.opcode == 0x11) segments_ |= p.segment_complete;
        else {
            // Last release: only the consumer's matching release store permits notification.
            discard_lineage();
            admit(p);
        }
    }

    void observe(const tlm::tlm_generic_payload& tx) {
        const auto address = tx.get_address();
        const auto length = tx.get_data_length();
        if (!length || !tx.get_data_ptr() || tx.get_byte_enable_ptr() ||
            tx.get_streaming_width() < length || address >= aperture || length > aperture - address) {
            discard_pending();
            return;
        }
        // Header identity writes invalidate lineage, but do not erase admissions or sequence history.
        if (written_mask(address, length, 0, 8) || written_mask(address, length, request_header, 8))
            discard_pending();
        for (unsigned i = 0; i < pending_.size(); ++i) {
            const auto mask = written_mask(address, length, request_header + 16 + i * stride, stride);
            request_fresh_[i] |= mask;
            auto& p = pending_[i];
            if (!p.sequence) continue;
            if (mask & 0xffff) {
                if (authentication(p.opcode)) discard_lineage();
                p = Pending{};
                continue;
            }
            p.response_fresh |= written_mask(address, length,
                16 + ((p.sequence - 1) % p.response_capacity) * stride, 16);
            if (mask & 0xf0000) {
                if ((mask & 0xf0000) == 0xf0000) release(i);
                else {
                    if (authentication(p.opcode)) discard_lineage();
                    p = Pending{};
                }
            }
        }
        const auto produced_mask = written_mask(address, length, request_header + 8, 4);
        if (produced_mask) {
            if (produced_mask == 15) publish();
            else discard_pending();
        }
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        backing_socket->b_transport(tx, delay);
        tx.set_dmi_allowed(false);
        if (!context_ || poisoned_ || !tx.is_write()) return;
        if (!tx.is_response_ok()) { discard_pending(); return; }
        try { observe(tx); }
        catch (const std::exception& error) {
            discard_pending();
            std::cerr << "IMAGE_HANDOFF_OBSERVATION_FAILED name=" << name()
                      << " reason=" << error.what() << '\n';
        } catch (...) {
            discard_pending();
            std::cerr << "IMAGE_HANDOFF_OBSERVATION_FAILED name=" << name() << '\n';
        }
    }
    unsigned debug(int, tlm::tlm_generic_payload& tx) {
        const auto transferred = backing_socket->transport_dbg(tx);
        tx.set_dmi_allowed(false);
        // Debug writes are not genuine CPU traffic. They can only revoke pending evidence.
        if (tx.is_write()) discard_pending();
        return transferred;
    }
    bool dmi(int, tlm::tlm_generic_payload& tx, tlm::tlm_dmi&) {
        tx.set_dmi_allowed(false);
        return false;
    }
};

} // namespace qcom_vp

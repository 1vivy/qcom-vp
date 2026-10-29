// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace qcom_vp {

// Virtual performance-resource selection, not a physical CPU or architectural timer clock.
struct PerformanceResourceState {
    uint32_t level = 0;
    uint32_t minimum = 0;
    uint32_t maximum = 1;
    uint64_t effective_hz = 1000000000;
};

class PerformanceResource {
public:
    virtual ~PerformanceResource() = default;
    // False means unavailable or an invalid domain. The output remains unchanged.
    virtual bool performance_state(unsigned domain, PerformanceResourceState& state) const = 0;
};

namespace pdp_scmi_detail {
inline uint32_t word(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
        (uint32_t(p[3]) << 24);
}
inline void put(unsigned char* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
}

// State changes are staged until the endpoint validates correlation and publishes the response.
struct Reply {
    std::array<unsigned char, 100> bytes{};
    unsigned size = 4;
    unsigned domain = 0;
    bool changes_state = false;
    PerformanceResourceState state{};
};

class Service {
public:
    const PerformanceResourceState* state(unsigned domain) const {
        return domain < domains_.size() ? &domains_[domain] : nullptr;
    }
    void commit(const Reply& reply) {
        if (reply.changes_state) domains_[reply.domain] = reply.state;
    }
    Reply request(unsigned protocol, unsigned message, const unsigned char* input, unsigned size) const {
        Reply reply;
        if (protocol == 0x10) base(message, input, size, reply);
        else if (protocol == 0x13) performance(message, input, size, reply);
        else status(reply, -1);
        return reply;
    }

private:
    std::array<PerformanceResourceState, 2> domains_{};
    static void status(Reply& r, int32_t value) {
        put(r.bytes.data(), static_cast<uint32_t>(value));
        if (value) { r.size = 4; r.changes_state = false; }
    }
    static bool length(Reply& r, unsigned actual, unsigned expected) {
        if (actual == expected) return true;
        status(r, -2); // INVALID_PARAMETERS
        return false;
    }
    static void value(Reply& r, unsigned offset, uint32_t v) {
        put(r.bytes.data() + 4 + offset, v);
        r.size = std::max(r.size, 8 + offset);
    }
    static void text(Reply& r, unsigned offset, const char* s) {
        // All identities are fixed virtual strings shorter than the 16-byte field.
        std::memcpy(r.bytes.data() + 4 + offset, s, std::strlen(s));
        r.size = 20 + offset;
    }
    static bool base_message(unsigned message) { return message <= 6; }
    static bool performance_message(unsigned message) { return message <= 8; }
    static void base(unsigned message, const unsigned char* input, unsigned size, Reply& r) {
        if (!base_message(message)) { status(r, -1); return; }
        if (!length(r, size, (message == 2 || message == 6) ? 4 : 0)) return;
        switch (message) {
        case 0: value(r, 0, 0x20000); break;
        case 1: value(r, 0, 0x101); break; // One protocol, one agent. Base is implicit.
        case 2:
            if (!base_message(word(input))) status(r, -1);
            else value(r, 0, 0);
            break;
        case 3: text(r, 0, "qcom-vp-virtual"); break;
        case 4: text(r, 0, "virtual-pdp"); break;
        case 5: value(r, 0, 1); break;
        case 6:
            if (word(input) > 1) { status(r, -2); break; }
            value(r, 0, word(input) == 0 ? 1 : 0);
            if (word(input) == 0) value(r, 4, 0x13); // Positive page and zero padding.
            break;
        }
    }
    void performance(unsigned message, const unsigned char* input, unsigned size, Reply& r) const {
        // Notifications and fastchannels deliberately have no successful path.
        if (!performance_message(message)) { status(r, -1); return; }
        constexpr std::array<unsigned, 9> lengths{0, 0, 4, 4, 8, 12, 4, 8, 4};
        if (!length(r, size, lengths[message])) return;
        if (message == 0) { value(r, 0, 0x40000); return; }
        if (message == 1) {
            value(r, 0, 2); value(r, 4, 0); value(r, 8, 0); value(r, 12, 0);
            return;
        }
        if (message == 2) {
            if (!performance_message(word(input))) status(r, -1);
            else value(r, 0, 0);
            return;
        }
        const auto domain = word(input);
        if (domain >= domains_.size()) { status(r, -4); return; } // NOT_FOUND
        r.domain = domain;
        r.state = domains_[domain];
        switch (message) {
        case 3:
            value(r, 0, 0xc0000000); // SET_LIMITS and SET_PERF only.
            value(r, 4, 0); value(r, 8, 1000000); value(r, 12, 0);
            text(r, 16, domain ? "virtual-cpu1" : "virtual-cpu0");
            break;
        case 4: {
            const auto start = word(input + 4);
            if (start >= 2) { status(r, -5); break; }
            value(r, 0, 1 | ((1 - start) << 16));
            value(r, 4, start); value(r, 8, 0); value(r, 12, 30);
            value(r, 16, (start + 1) * 1000000); value(r, 20, start);
            break;
        }
        case 5: {
            const auto maximum = word(input + 4), minimum = word(input + 8);
            if (minimum > maximum || maximum > 1) { status(r, -5); break; }
            r.state.minimum = minimum; r.state.maximum = maximum;
            r.state.level = std::clamp(r.state.level, minimum, maximum);
            r.state.effective_hz = (uint64_t(r.state.level) + 1) * 1000000000;
            r.changes_state = true;
            break;
        }
        case 6: value(r, 0, r.state.maximum); value(r, 4, r.state.minimum); break;
        case 7: {
            const auto level = word(input + 4);
            if (level < r.state.minimum || level > r.state.maximum) { status(r, -5); break; }
            r.state.level = level;
            r.state.effective_hz = (uint64_t(level) + 1) * 1000000000;
            r.changes_state = true;
            break;
        }
        case 8: value(r, 0, r.state.level); break;
        }
    }
};
} // namespace pdp_scmi_detail
} // namespace qcom_vp

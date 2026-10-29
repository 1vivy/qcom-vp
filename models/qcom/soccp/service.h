// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace qcom_vp {
namespace soccp_detail {
inline uint32_t u32(const unsigned char* p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) |
        (uint32_t{p[3]} << 24);
}
inline void put32(unsigned char* p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}
inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// No physical readings. The board must choose every nonzero battery stimulus.
struct BatteryPolicy {
    uint32_t voltage_mv;
    uint32_t design_mwh;
    uint32_t full_mwh;
    uint32_t remaining_mwh;
    int32_t temperature_decic;
};

// Selected PmicGlinkDxe wire group, not an EFI protocol replacement.
// The transport owns correlation through channel and intent IDs. This header has no sequence field.
class Service {
public:
    explicit Service(BatteryPolicy policy) : policy_(policy) {
        require(policy.full_mwh && policy.design_mwh && policy.voltage_mv &&
            policy.remaining_mwh <= policy.full_mwh && policy.voltage_mv <= 0xffffffffu / 1000,
            "Invalid virtual SOCCP battery policy");
    }
    unsigned execute(bool logs, const unsigned char* request, unsigned size,
                     unsigned char* response) {
        require(size >= 12 && u32(request + 4) == 1, "Invalid SOCCP request header");
        const auto owner = u32(request);
        const auto opcode = u32(request + 8);
        unsigned result = 0;
        const auto begin_response = [&](unsigned bytes) {
            result = bytes;
            std::memset(response, 0, bytes);
        };
        if (logs) {
            if (owner == 0x800a && opcode == 0x19) {
                require(size == 32 && u32(request + 24) <= 5, "Invalid SOCCP log filter");
                log_categories_ = uint64_t{u32(request + 16)} | (uint64_t{u32(request + 20)} << 32);
                log_level_ = u32(request + 24);
                begin_response(16);
            } else if (owner == 0x800a && (opcode == 0x18 || opcode == 0x23)) {
                require(size == 16 && u32(request + 12) && u32(request + 12) <= 8192,
                    "Invalid SOCCP log size");
                // This virtual remote has no firmware log producer. Its log is empty.
                begin_response(8204);
            } else if (owner == 0x800d && (opcode == 0x36 || opcode == 0x39)) {
                require(size == 16 && u32(request + 12) < 2, "Invalid SOCCP QBG dump request");
                // No QBG context exists. dump_size@12 is zero, not a successful physical capture.
                begin_response(opcode == 0x36 ? 0x9a0 : 0xe0);
            } else require(false, "Unsupported SOCCP log command");
        } else if (owner == 0x8011) {
            require(size >= 24 && u32(request + 12) == 0, "Invalid SOCCP OEM battery index");
            if (opcode == 0x90) {
                require(size == 24 && u32(request + 16) == 0x30100 && u32(request + 20) == 4,
                    "Unsupported SOCCP OEM GET");
                // Selected ChargerDxe uses exchange completion, not these opaque four data bytes.
                begin_response(1040);
            } else if (opcode == 0x91) {
                require(size == 1048 && u32(request + 16) == 0x180 && u32(request + 20) == 4,
                    "Unsupported SOCCP OEM SET");
                const auto stage = u32(request + 24) & 15;
                require(stage >= 1 && stage <= 3, "Unsupported SOCCP host software stage");
                host_stage_ = stage;
                begin_response(16);
            } else require(false, "Unsupported SOCCP OEM opcode");
        } else {
            require(owner == 0x800a, "Unsupported SOCCP service owner");
            if (opcode == 0 || opcode == 1 || opcode == 9) {
                require(size == 16 && u32(request + 12) == 0, "Invalid SOCCP battery query");
                if (opcode == 0) {
                    begin_response(16);
                    put32(response + 12, 1);
                } else if (opcode == 1) {
                    begin_response(48);
                    // Idle pack. No source, current, charging, discharging, or critical flag is asserted.
                    put32(response + 16, policy_.remaining_mwh);
                    put32(response + 24, policy_.voltage_mv);
                    put32(response + 36, static_cast<uint32_t>(policy_.temperature_decic));
                } else {
                    begin_response(736);
                    put32(response + 12, 1); // mWh
                    put32(response + 16, policy_.design_mwh);
                    put32(response + 20, policy_.full_mwh);
                    put32(response + 24, 2); // Explicit virtual LiP pack policy.
                    put32(response + 28, policy_.voltage_mv);
                    put32(response + 64, 1);
                    put32(response + 68, 1);
                    std::memcpy(response + 80, "VIRTUAL-PACK", 13);
                    std::memcpy(response + 208, "0", 2);
                    std::memcpy(response + 336, "LiP", 4);
                    std::memcpy(response + 592, "LIP", 4);
                    put32(response + 732, 1);
                }
            } else if (opcode == 0x30) {
                require(size == 24 && u32(request + 12) == 0 && u32(request + 16) == 6,
                    "Unsupported SOCCP power property");
                begin_response(24);
                put32(response + 12, 6);
                put32(response + 16, policy_.voltage_mv * 1000);
            } else if (opcode == 0x300) {
                require(size == 24 && u32(request + 12) == 0 && u32(request + 16) == 0x7b &&
                    u32(request + 20) <= 1, "Unsupported SOCCP OEM power policy");
                pd_enabled_ = u32(request + 20) != 0;
                begin_response(24);
                put32(response + 12, 0x7b);
                put32(response + 16, pd_enabled_ ? 1 : 0);
            } else require(false, "Unsupported SOCCP battery command");
        }
        put32(response, owner);
        put32(response + 4, 1);
        put32(response + 8, opcode);
        return result;
    }
private:
    BatteryPolicy policy_;
    uint64_t log_categories_ = 0;
    uint32_t log_level_ = 0;
    uint32_t host_stage_ = 0;
    bool pd_enabled_ = false;
};
} // namespace soccp_detail
} // namespace qcom_vp

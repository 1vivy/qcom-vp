// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <cstdint>

namespace qcom_vp {
namespace pdp_power_detail {

// Both firmware blocks use only 64-bit LDRD and STRD. Any other width fails.
inline bool prepare(tlm::tlm_generic_payload& tx) {
    tx.set_dmi_allowed(false);
    if (tx.get_byte_enable_ptr()) {
        tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        return false;
    }
    const auto size = tx.get_data_length();
    if (!tx.get_data_ptr() || size != 8 || tx.get_streaming_width() < size ||
        (tx.get_address() & 7)) {
        tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return false;
    }
    if (!tx.is_read() && !tx.is_write()) {
        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return false;
    }
    return true;
}

inline std::uint64_t load(const tlm::tlm_generic_payload& tx) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= std::uint64_t{tx.get_data_ptr()[i]} << (8 * i);
    return value;
}

inline void store(tlm::tlm_generic_payload& tx, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
}

} // namespace pdp_power_detail

// Retained 64-bit DPM configuration words at recovered offsets. Every recovered
// producer uses STRD and no PDP path reads these words back for a decision.
// The model therefore retains configuration without power metering, limits,
// or completion. Undeclared offsets fail.
template <std::size_t N>
class PdpDpmWords : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<PdpDpmWords> target_socket{"target_socket"};

    PdpDpmWords(sc_core::sc_module_name name, const std::array<std::uint64_t, N>& offsets)
        : sc_module(name), offsets_(offsets) {
        target_socket.register_b_transport(this, &PdpDpmWords::transport);
    }

    void cold_reset() { words_.fill(0); }

private:
    std::array<std::uint64_t, N> offsets_;
    std::array<std::uint64_t, N> words_{};

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!pdp_power_detail::prepare(tx)) return;
        const auto offset = tx.get_address();
        for (unsigned index = 0; index < offsets_.size(); ++index) {
            if (offset != offsets_[index]) continue;
            if (tx.is_write()) words_[index] = pdp_power_detail::load(tx);
            else pdp_power_detail::store(tx, words_[index]);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

// NCC cluster DPM bank. The PDP reaches it at local 0x408c0000, which is
// system 0x198c0000 plus lane << 24. Device configuration 0x2000d250 holds the
// base. The recovered producers write these 64-bit words:
//
//   0x00  the K_DPM_CFG pair blob[2], blob[3]      PC 0x81326eba
//   0x30  the K_DPM_CFG pair blob[0], blob[1]      PC 0x81326eb0
//   0x80  the constant 0x0000000000010000          PC 0x2000b63c
//   0x18, 0x20, 0xa8, 0x28, 0xb0, 0xb8
//         the K_DPM_CLUSTER_WEIGHTS record          PC 0x81326f10..0x81326f38
class PdpDpm : public PdpDpmWords<9> {
public:
    explicit PdpDpm(sc_core::sc_module_name name)
        : PdpDpmWords(name, {0x00, 0x18, 0x20, 0x28, 0x30, 0x80, 0xa8, 0xb0, 0xb8}) {}
};

// NCC per-core DPM bank. The PDP reaches core n at local 0x400b0000 + (n << 20),
// from the table at 0x2000d268. The system address is
// 0x190b0000 + (n << 20) + (lane << 24).
// Function 0x81326c40 copies the K_DPM_CORE_WEIGHTS record into 33 words:
// 0x10..0x98 and 0x100..0x170, each with an eight-byte stride.
inline constexpr std::array<std::uint64_t, 33> pdp_core_dpm_offsets() {
    std::array<std::uint64_t, 33> offsets{};
    for (unsigned i = 0; i < 18; ++i) offsets[i] = 0x10 + 8 * i;
    for (unsigned i = 0; i < 15; ++i) offsets[18 + i] = 0x100 + 8 * i;
    return offsets;
}

class PdpCoreDpm : public PdpDpmWords<33> {
public:
    explicit PdpCoreDpm(sc_core::sc_module_name name)
        : PdpDpmWords(name, pdp_core_dpm_offsets()) {}
};

// NCC limiter bank. The PDP reaches it at local 0x40a00000, which is system
// 0x19a00000 plus lane << 24. The device configuration at link 0x8132d410 holds
// the base 0x40a00000. The static window table at link 0x813d0518 declares
// fourteen windows of 0x20 bytes at these offsets:
//
//   0x00, 0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0,
//   0x100, 0x120, 0x140, 0x160, 0x1a0, 0x1c0
//
// The offsets 0x180 to 0x19f hold no window. Each window holds three 64-bit words:
//
//   +0x00  limit word, read by the limit query      PC 0x81326fc4
//   +0x08  limit word, read by the limit query      PC 0x81326fd4
//   +0x10  control word: the init writes it         PC 0x813270e6
//          the stop path clears bits 2:0            PC 0x8132721a
//          both paths then set bit 3                PC 0x8132722e
//
// The recovered initializer programs window ids 2 to 6, at offsets 0x40 to 0xc0.
// The model retains configuration without physical clock or rail limiting.
// Undeclared offsets fail. No completion bit comes from a retained word.
class PdpLimiters : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<PdpLimiters> target_socket{"target_socket"};

    explicit PdpLimiters(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &PdpLimiters::transport);
    }

    void cold_reset() { for (auto& window : words_) window.fill(0); }

private:
    std::array<std::array<std::uint64_t, 3>, 14> words_{};

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!pdp_power_detail::prepare(tx)) return;
        const auto offset = tx.get_address();
        const auto base = offset & ~std::uint64_t{0x1f};
        const auto local = offset & 0x1f;
        if (base > 0x1c0 || base == 0x180 || local >= 0x18) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const auto index = base / 0x20 - (base >= 0x1a0 ? 1 : 0);
        auto& word = words_[index][local / 8];
        if (tx.is_write()) word = pdp_power_detail::load(tx);
        else pdp_power_detail::store(tx, word);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

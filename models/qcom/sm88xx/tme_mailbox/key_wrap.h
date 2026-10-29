// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "sequencer_hash.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace qcom_vp {

// Virtual authenticated wrap format for sequencer command 0x0b.
//
// The selected firmware exports 100 bytes for most modes and 92 bytes for the
// converted mode-two path. The GPCE consumer transmits 96 bytes. This layout
// keeps the complete authenticated core inside the first 92 bytes:
//
//   00..03  magic "QVK1"
//   04..07  key material length, 48
//   08..0f  nonce
//   10..3f  AES-256-CTR ciphertext of the source key
//   40..5b  truncated HMAC-SHA-384 tag
//
// Bytes 92..99 are zero padding for the longer copy path.
// The silicon cipher, field order, tag position, and policy binding remain
// unrecovered. This layout is a declared virtual contract, not a vendor format.
inline constexpr std::size_t wrapped_key_core_bytes = 92;
inline constexpr std::size_t wrapped_key_max_bytes = 100;
inline constexpr std::size_t wrapped_key_material_bytes = 48;
inline constexpr std::size_t wrapped_key_tag_bytes = 28;
inline constexpr std::size_t wrapped_key_header_bytes = 16;
inline constexpr std::uint32_t wrapped_key_magic = 0x314b5651U;

namespace key_wrap_detail {

struct Derivation {
    std::array<unsigned char, 32> encryption{};
    std::array<unsigned char, 48> authentication{};
};

inline void put32(unsigned char* bytes, std::uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
        bytes[i] = static_cast<unsigned char>(value >> (8 * i));
}

inline std::uint32_t get32(const unsigned char* bytes) {
    return std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8) |
        (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
}

inline void derive(const unsigned char* transport_key, std::size_t size, Derivation& out) {
    static constexpr unsigned char encryption_label[] =
        "qcom-vp transport key wrap encryption v1";
    static constexpr unsigned char authentication_label[] =
        "qcom-vp transport key wrap authentication v1";
    std::array<unsigned char, 48> digest{};
    SequencerHmacSha384 prf;
    prf.initialize(transport_key, size);
    prf.update(encryption_label, sizeof(encryption_label) - 1);
    prf.finalize(digest.data());
    std::memcpy(out.encryption.data(), digest.data(), out.encryption.size());
    prf.initialize(transport_key, size);
    prf.update(authentication_label, sizeof(authentication_label) - 1);
    prf.finalize(digest.data());
    std::memcpy(out.authentication.data(), digest.data(), out.authentication.size());
    OPENSSL_cleanse(digest.data(), digest.size());
}

inline bool crypt(const Derivation& keys, const unsigned char* nonce,
                  const unsigned char* input, unsigned char* output, std::size_t size) {
    std::array<unsigned char, 16> counter{};
    std::memcpy(counter.data(), nonce, 8);
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (!context) return false;
    bool ok = EVP_EncryptInit_ex(context, EVP_aes_256_ctr(), nullptr,
        keys.encryption.data(), counter.data()) == 1;
    int produced = 0;
    if (ok)
        ok = EVP_EncryptUpdate(context, output, &produced, input,
            static_cast<int>(size)) == 1 &&
            produced == static_cast<int>(size);
    if (ok) {
        int tail = 0;
        ok = EVP_EncryptFinal_ex(context, output + size, &tail) == 1 && tail == 0;
    }
    EVP_CIPHER_CTX_free(context);
    OPENSSL_cleanse(counter.data(), counter.size());
    return ok;
}

inline void authenticate(const Derivation& keys, const unsigned char* bytes,
                         std::size_t size, unsigned char* tag) {
    std::array<unsigned char, 48> digest{};
    SequencerHmacSha384 mac;
    mac.initialize(keys.authentication.data(), keys.authentication.size());
    mac.update(bytes, size);
    mac.finalize(digest.data());
    std::memcpy(tag, digest.data(), wrapped_key_tag_bytes);
    OPENSSL_cleanse(digest.data(), digest.size());
}

inline bool trailing_bytes_zero(const unsigned char* blob, std::size_t size) {
    for (std::size_t i = wrapped_key_core_bytes; i != size; ++i) {
        if (blob[i]) return false;
    }
    return true;
}

} // namespace key_wrap_detail

// The producer keeps both derived keys only for this call.
inline bool wrap_key_material(const unsigned char* transport_key, std::size_t transport_size,
                              const unsigned char* material, std::size_t material_size,
                              const unsigned char nonce[8],
                              unsigned char blob[wrapped_key_core_bytes]) {
    if (!transport_key || !material || !nonce ||
        material_size != wrapped_key_material_bytes) return false;
    key_wrap_detail::Derivation keys;
    key_wrap_detail::derive(transport_key, transport_size, keys);
    std::memset(blob, 0, wrapped_key_core_bytes);
    key_wrap_detail::put32(blob, wrapped_key_magic);
    key_wrap_detail::put32(blob + 4, static_cast<std::uint32_t>(material_size));
    std::memcpy(blob + 8, nonce, 8);
    bool ok = key_wrap_detail::crypt(keys, nonce, material, blob + 16, material_size);
    if (ok) key_wrap_detail::authenticate(keys, blob,
        wrapped_key_header_bytes + material_size, blob + wrapped_key_header_bytes + material_size);
    OPENSSL_cleanse(&keys, sizeof(keys));
    return ok;
}

// The consumer installs the material only after the tag and length checks pass.
inline bool unwrap_key_material(const unsigned char* transport_key, std::size_t transport_size,
                                const unsigned char* blob, std::size_t blob_size,
                                unsigned char material[wrapped_key_material_bytes]) {
    if (!transport_key || !blob || blob_size < wrapped_key_core_bytes ||
        blob_size > wrapped_key_max_bytes ||
        !key_wrap_detail::trailing_bytes_zero(blob, blob_size)) return false;
    if (key_wrap_detail::get32(blob) != wrapped_key_magic ||
        key_wrap_detail::get32(blob + 4) != wrapped_key_material_bytes) return false;
    key_wrap_detail::Derivation keys;
    key_wrap_detail::derive(transport_key, transport_size, keys);
    std::array<unsigned char, wrapped_key_tag_bytes> expected{};
    key_wrap_detail::authenticate(keys, blob,
        wrapped_key_header_bytes + wrapped_key_material_bytes, expected.data());
    const bool authentic = CRYPTO_memcmp(expected.data(),
        blob + wrapped_key_header_bytes + wrapped_key_material_bytes,
        expected.size()) == 0;
    bool ok = authentic && key_wrap_detail::crypt(keys, blob + 8,
        blob + wrapped_key_header_bytes, material, wrapped_key_material_bytes);
    OPENSSL_cleanse(&keys, sizeof(keys));
    OPENSSL_cleanse(expected.data(), expected.size());
    if (!authentic || !ok) OPENSSL_cleanse(material, wrapped_key_material_bytes);
    return ok;
}

} // namespace qcom_vp

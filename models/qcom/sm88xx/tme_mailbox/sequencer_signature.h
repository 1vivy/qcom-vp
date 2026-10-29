// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <array>
#include <cstring>
#include <memory>

namespace qcom_vp {

// ROM 0x20f5cc submits big-endian P-384 coordinates, signature integers, and a SHA-384 digest.
inline bool verify_sequencer_p384(const unsigned char* coordinates,
                                  const unsigned char* signature,
                                  const unsigned char* digest) {
    std::array<unsigned char, 97> point{};
    point[0] = 4;
    std::memcpy(point.data() + 1, coordinates, 96);
    char group[] = "secp384r1";
    OSSL_PARAM parameters[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group, 0),
        OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size()),
        OSSL_PARAM_construct_end()
    };
    using Context = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
    Context importer(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY* raw = nullptr;
    if (!importer || EVP_PKEY_fromdata_init(importer.get()) <= 0 ||
        EVP_PKEY_fromdata(importer.get(), &raw, EVP_PKEY_PUBLIC_KEY, parameters) <= 0) return false;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw, EVP_PKEY_free);
    Context verifier(EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
    if (!verifier || EVP_PKEY_public_check(verifier.get()) != 1) return false;

    std::array<unsigned char, 104> der{};
    std::size_t used = 2;
    for (unsigned part = 0; part != 2; ++part) {
        const auto* value = signature + 48 * part;
        std::size_t count = 48;
        while (count > 1 && !*value) { ++value; --count; }
        const bool prefix = (*value & 0x80) != 0;
        der[used++] = 2;
        der[used++] = static_cast<unsigned char>(count + prefix);
        if (prefix) der[used++] = 0;
        std::memcpy(der.data() + used, value, count);
        used += count;
    }
    der[0] = 0x30;
    der[1] = static_cast<unsigned char>(used - 2);
    return EVP_PKEY_verify_init(verifier.get()) == 1 &&
        EVP_PKEY_CTX_set_signature_md(verifier.get(), EVP_sha384()) == 1 &&
        EVP_PKEY_verify(verifier.get(), der.data(), used, digest, 48) == 1;
}

// The sequencer exchanges P-384 coordinates as big-endian X followed by Y.
inline bool derive_sequencer_p384_public(const unsigned char* scalar, unsigned char* coordinates) {
    std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)> group(
        EC_GROUP_new_by_curve_name(NID_secp384r1), EC_GROUP_free);
    std::unique_ptr<BIGNUM, decltype(&BN_clear_free)> secret(
        BN_bin2bn(scalar, 48, nullptr), BN_clear_free);
    if (!group || !secret) return false;
    std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)> point(
        EC_POINT_new(group.get()), EC_POINT_free);
    std::array<unsigned char, 97> encoded{};
    if (!point || EC_POINT_mul(group.get(), point.get(), secret.get(), nullptr, nullptr, nullptr) != 1 ||
        EC_POINT_point2oct(group.get(), point.get(), POINT_CONVERSION_UNCOMPRESSED,
                           encoded.data(), encoded.size(), nullptr) != encoded.size()) return false;
    std::memcpy(coordinates, encoded.data() + 1, 96);
    return true;
}

} // namespace qcom_vp

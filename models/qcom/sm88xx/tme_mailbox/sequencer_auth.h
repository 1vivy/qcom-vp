// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// The virtual profile pins an issuer-signed entitlement until all chip constraints have defined semantics.
// The model verifies both signatures and the program hash. It does not trust an embedded key alone.
class SequencerAuthentication {
    struct Tlv {
        const unsigned char* header = nullptr;
        const unsigned char* data = nullptr;
        size_t length = 0;
        size_t extent = 0;
    };
    class Fields {
        const unsigned char* cursor_;
        const unsigned char* end_;
    public:
        Fields(const unsigned char* data, size_t length) : cursor_(data), end_(data + length) {}
        explicit Fields(const Tlv& value) : Fields(value.data, value.length) {}
        bool take(unsigned tag, Tlv& value) {
            if (size_t(end_ - cursor_) < 4 || half(cursor_) != tag) return false;
            const size_t length = half(cursor_ + 2);
            const size_t extent = 4 + ((length + 3) & ~size_t(3));
            if (extent > size_t(end_ - cursor_)) return false;
            for (size_t i = 4 + length; i < extent; ++i)
                if (cursor_[i]) return false;
            value = {cursor_, cursor_ + 4, length, extent};
            cursor_ += extent;
            return true;
        }
        bool number(unsigned tag, uint32_t expected, size_t length = 4) {
            Tlv value;
            if (!take(tag, value) || value.length != length || length > 4) return false;
            uint32_t actual = 0;
            for (size_t i = 0; i < length; ++i) actual |= uint32_t(value.data[i]) << (8 * i);
            return actual == expected;
        }
        bool complete() const { return cursor_ == end_; }
    private:
        static unsigned half(const unsigned char* p) { return unsigned(p[0]) | (unsigned(p[1]) << 8); }
    };
    using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    Key root_{nullptr, EVP_PKEY_free};
    std::array<unsigned char, 32> entitlement_hash_{};

    static Key public_key(const unsigned char* encoded, size_t length) {
        if (length != 136 || encoded[0] || encoded[1] || encoded[68] || encoded[69])
            return {nullptr, EVP_PKEY_free};
        std::array<unsigned char, 133> point{};
        point[0] = 4;
        std::memcpy(point.data() + 1, encoded + 2, 66);
        std::memcpy(point.data() + 67, encoded + 70, 66);
        char group[] = "secp521r1";
        OSSL_PARAM parameters[] = {
            OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group, 0),
            OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size()),
            OSSL_PARAM_construct_end()
        };
        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
            EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), EVP_PKEY_CTX_free);
        EVP_PKEY* raw = nullptr;
        if (!context || EVP_PKEY_fromdata_init(context.get()) <= 0 ||
            EVP_PKEY_fromdata(context.get(), &raw, EVP_PKEY_PUBLIC_KEY, parameters) <= 0)
            return {nullptr, EVP_PKEY_free};
        Key key(raw, EVP_PKEY_free);
        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> check(
            EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
        if (!check || EVP_PKEY_public_check(check.get()) != 1) return {nullptr, EVP_PKEY_free};
        return key;
    }

    static bool verify(EVP_PKEY* key, const Tlv& signature,
                       const unsigned char* message, size_t length) {
        if (signature.length != 136 || signature.data[0] || signature.data[1] ||
            signature.data[68] || signature.data[69]) return false;
        std::array<unsigned char, 141> der{};
        std::array<unsigned char, 138> integers{};
        size_t used = 0;
        for (unsigned part = 0; part != 2; ++part) {
            auto* value = signature.data + 68 * part;
            size_t count = 68;
            while (count > 1 && !*value) { ++value; --count; }
            const bool prefix = (*value & 0x80) != 0;
            integers[used++] = 2;
            integers[used++] = static_cast<unsigned char>(count + prefix);
            if (prefix) integers[used++] = 0;
            std::memcpy(integers.data() + used, value, count);
            used += count;
        }
        size_t header = 0;
        der[header++] = 0x30;
        if (used >= 128) der[header++] = 0x81;
        der[header++] = static_cast<unsigned char>(used);
        std::memcpy(der.data() + header, integers.data(), used);
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        return context && EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha512(), nullptr, key) == 1 &&
            EVP_DigestVerify(context.get(), der.data(), header + used, message, length) == 1;
    }

    static bool digest(const unsigned char* data, size_t length, const EVP_MD* algorithm,
                       const unsigned char* expected, unsigned expected_length) {
        std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
        unsigned size = 0;
        return EVP_Digest(data, length, hash.data(), &size, algorithm, nullptr) == 1 &&
            size == expected_length && std::memcmp(hash.data(), expected, size) == 0;
    }

public:
    void configure(const std::string& path, const std::string& entitlement) {
        if (path.empty() && entitlement.empty()) return;
        if (path.empty() || entitlement.size() != 64)
            throw std::invalid_argument("The sequencer requires a root and an entitlement hash");
        for (size_t i = 0; i != entitlement_hash_.size(); ++i) {
            unsigned byte = 0;
            for (size_t j = 0; j != 2; ++j) {
                const auto c = entitlement[2 * i + j];
                const int digit = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (digit < 0) throw std::invalid_argument("The entitlement hash requires hexadecimal digits");
                byte = (byte << 4) | unsigned(digit);
            }
            entitlement_hash_[i] = static_cast<unsigned char>(byte);
        }
        std::array<unsigned char, 136> encoded{};
        std::ifstream file(path, std::ios::binary);
        if (!file.read(reinterpret_cast<char*>(encoded.data()), encoded.size()) ||
            file.peek() != std::char_traits<char>::eof())
            throw std::invalid_argument("The sequencer root requires 136 bytes");
        root_ = public_key(encoded.data(), encoded.size());
        if (!root_) throw std::invalid_argument("The sequencer root is not a valid P-521 public key");
    }

    const char* authenticate(const std::array<unsigned char, 4096>& program,
                             const std::array<unsigned char, 4096>& input,
                             uint32_t hardware_version) const {
        if (!root_) return "missing-trust";
        if (!hardware_version) return "missing-hardware-version";
        Tlv envelope, command, signature, certificate;
        Fields buffer(input.data(), input.size());
        if (!buffer.take(0x0182, envelope)) return "invalid-envelope";
        Fields outer(envelope);
        if (!outer.number(0x0001, 2, 1) || !outer.take(0x8291, command) ||
            !outer.take(0x81ff, signature) || !outer.take(0x8080, certificate) || !outer.complete())
            return "invalid-command";
        Tlv chip, hash;
        Fields fields(command);
        if (!fields.number(0x8301, 1) || !fields.number(0x8351, 0x37) || !fields.number(0x8352, 1) ||
            !fields.take(0x8380, chip) || !fields.take(0x8384, hash) || !fields.complete())
            return "unsupported-command";
        Fields chip_fields(chip);
        if (!chip_fields.number(0x8401, (hardware_version & 0xffff0000U) | 0xffffU) || !chip_fields.complete())
            return "wrong-hardware-version";
        Tlv expected_hash;
        Fields hash_fields(hash);
        if (!hash_fields.number(0x8440, 5) || !hash_fields.take(0x8441, expected_hash) ||
            expected_hash.length != 64 || !hash_fields.complete()) return "unsupported-program-hash";
        if (!digest(program.data(), program.size(), EVP_sha512(), expected_hash.data, 64))
            return "program-hash-mismatch";

        Tlv bundle, certificate_signature;
        Fields certificate_fields(certificate);
        if (!certificate_fields.number(0x8101, 1) || !certificate_fields.number(0x8102, 1) ||
            !certificate_fields.number(0x8103, 2) || !certificate_fields.take(0x8180, bundle) ||
            !certificate_fields.take(0x81ff, certificate_signature) || !certificate_fields.complete())
            return "unsupported-certificate";
        Tlv entitlement;
        Fields bundle_fields(bundle);
        if (!bundle_fields.take(0x8281, entitlement) || !bundle_fields.complete())
            return "unsupported-entitlement";
        if (!verify(root_.get(), certificate_signature, entitlement.header, entitlement.extent))
            return "invalid-certificate-signature";
        if (!digest(entitlement.header, entitlement.extent, EVP_sha256(), entitlement_hash_.data(), 32))
            return "unapproved-entitlement";

        Tlv constraints, software, key;
        Fields entitlement_fields(entitlement);
        if (!entitlement_fields.number(0x8300, 1) || !entitlement_fields.take(0x8380, constraints) ||
            !entitlement_fields.take(0x8381, software) || !entitlement_fields.take(0x8390, key) ||
            !entitlement_fields.complete()) return "unsupported-entitlement-fields";
        Fields software_fields(software);
        if (!software_fields.number(0x8422, 0x37) || !software_fields.number(0x8423, 4) ||
            !software_fields.complete()) return "wrong-software-entitlement";
        Tlv encoded_key;
        Fields key_fields(key);
        if (!key_fields.number(0x8410, 3) || !key_fields.take(0x8411, encoded_key) || !key_fields.complete())
            return "unsupported-signing-key";
        auto subject = public_key(encoded_key.data, encoded_key.length);
        if (!subject || !verify(subject.get(), signature, input.data(), size_t(signature.header - input.data())))
            return "invalid-command-signature";
        return nullptr;
    }
};

} // namespace qcom_vp

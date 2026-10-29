// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <openssl/sha.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace qcom_vp {

// EVP cannot import or export SHA-512 chaining words. This ABI requires them.
// Keep the deprecated OpenSSL context API inside this bounded adapter.
class SequencerSha384 {
public:
    void initialize(const unsigned char* state, std::uint32_t previous_bytes) {
        SHA384_Init(&context_);
        if (state) {
            for (unsigned i = 0; i != 8; ++i) {
                std::uint64_t word = 0;
                for (unsigned j = 0; j != 8; ++j) word = (word << 8) | state[i * 8 + j];
                context_.h[i] = word;
            }
            context_.Nl = std::uint64_t{previous_bytes} * 8;
            context_.Nh = 0;
        }
    }

    void update(const unsigned char* bytes, std::size_t length) {
        SHA384_Update(&context_, bytes, length);
    }

    void state(unsigned char* bytes) const {
        for (unsigned i = 0; i != 8; ++i) {
            for (unsigned j = 0; j != 8; ++j) {
                bytes[i * 8 + j] = static_cast<unsigned char>(context_.h[i] >> (56 - j * 8));
            }
        }
    }

    void finalize(unsigned char* bytes) { SHA384_Final(bytes, &context_); }

private:
    SHA512_CTX context_{};
};

class SequencerHmacSha384 {
public:
    void initialize(const unsigned char* key, std::size_t length) {
        pad_.fill(0);
        if (length > pad_.size()) {
            SequencerSha384 hash;
            hash.initialize(nullptr, 0);
            hash.update(key, length);
            hash.finalize(pad_.data());
        } else {
            for (std::size_t i = 0; i != length; ++i) pad_[i] = key[i];
        }
        for (auto& byte : pad_) byte ^= 0x36;
        inner_.initialize(nullptr, 0);
        inner_.update(pad_.data(), pad_.size());
    }

    void update(const unsigned char* bytes, std::size_t length) {
        inner_.update(bytes, length);
    }

    void finalize(unsigned char* bytes) {
        std::array<unsigned char, 48> digest{};
        inner_.finalize(digest.data());
        for (auto& byte : pad_) byte ^= 0x36 ^ 0x5c;
        SequencerSha384 outer;
        outer.initialize(nullptr, 0);
        outer.update(pad_.data(), pad_.size());
        outer.update(digest.data(), digest.size());
        outer.finalize(bytes);
    }

private:
    SequencerSha384 inner_;
    std::array<unsigned char, 128> pad_{};
};

} // namespace qcom_vp

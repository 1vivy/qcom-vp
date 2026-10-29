// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include "qcom/bam/stream.h"
#include "qcom/sm88xx/tme_mailbox/transport_key.h"
#include "qcom/sm88xx/tme_mailbox/key_wrap.h"

namespace qcom_vp {

// QCE 6.0 functional model. It keeps the GPCE startup controls and implements
// SHA-1, SHA-256, SHA-384, SHA-512, and AES CMAC with a supplied AUTH_KEY or
// the OEM hardware key.
//
// MMIO offsets are relative to 01df0000. The QCE register block starts at
// offset a000. The key bank is at d000 and holds 32 write-only words. Key
// reads return zero, and only AES CMAC reads the programmed AUTH_KEY words.
// The BAM stream carries input on pipe 2n and the result dump on
// pipe 2n+1, where n is CONFIG.PIPE_SET_SELECT.
// The TPKEY receive path keeps the TME transport key and sets 01df1020 bit8.
// An imported blob is unwrapped only after its tag and destination checks.
// It does not authenticate an image.
class QceV6 : public sc_core::sc_module, public TransportKeySink {
public:
    tlm_utils::multi_passthrough_target_socket<QceV6> target_socket{"target_socket"};
    tlm_utils::multi_passthrough_target_socket<QceV6> stream_socket{"stream_socket"};
    InitiatorSignalSocket<bool> stream_ready{"stream_ready"};

    cci::cci_param<unsigned> version{"version", 0x06000000,
        "Crypto core version register"};
    cci::cci_param<unsigned> first_slot_count{"first_slot_count", 8, "Virtual first slot-bank count"};
    cci::cci_param<unsigned> total_slot_count{"total_slot_count", 16, "Virtual total slot count"};
    cci::cci_param<unsigned> primary_destination{"primary_destination", 8, "Virtual primary GPCE destination selector"};
    cci::cci_param<unsigned> secondary_destination{"secondary_destination", 9, "Virtual secondary GPCE destination selector"};
    cci::cci_param<unsigned> selector_base0{"selector_base0", 0, "Virtual selector base from CRYPTO +0xb00"};
    cci::cci_param<unsigned> selector_base1{"selector_base1", 2, "Virtual selector base from CRYPTO +0xb08"};
    cci::cci_param<unsigned> selector_base2{"selector_base2", 4, "Virtual selector base from CRYPTO +0xb10"};
    cci::cci_param<std::string> oem_hw_key_hex{
        "oem_hw_key_hex", "", "Optional exact 32-byte virtual OEM hardware key in hexadecimal"};

    // The TME key master calls this sideband after a successful command 1c.
    // The transport key stays in this object and has no MMIO or host read path.
    Delivery transport_key_receive(const unsigned char* material,
        std::size_t size) override {
        if (!material || size != transport_key_bytes) {
            std::cerr << "QCE_V6_TPKEY_REJECTED model=" << name()
                      << " reason=extent\n";
            return Delivery::rejected;
        }
        std::memcpy(pending_transport_key_.data(), material, size);
        transport_key_pending_ = true;
        if (transport_key_control_ & transport_key_receive_enable)
            accept_pending_transport_key();
        std::cerr << "QCE_V6_TPKEY_BROADCAST model=" << name()
                  << " bits=" << transport_key_bytes * 8
                  << " receive=" << ((transport_key_control_ & transport_key_receive_enable) ?
                      "enabled" : "disabled")
                  << '\n';
        return transport_key_ready_ ? Delivery::installed : Delivery::pending;
    }

    // The transport-key import path calls this method with the FIFO blob.
    // A destination key becomes present only after the tag and length checks.
    bool import_wrapped_key(const unsigned char* blob, std::size_t size,
                            unsigned destination, unsigned auxiliary) {
        if (!transport_key_ready_) {
            std::cerr << "QCE_V6_IMPORT_REJECTED model=" << name()
                      << " reason=no-transport-key\n";
            return false;
        }
        if (auxiliary != import_auxiliary_selector ||
            destination >= total_slot_count.get_value() ||
            destination >= imported_keys_.size()) {
            std::cerr << "QCE_V6_IMPORT_REJECTED model=" << name()
                      << " reason=destination destination=" << destination
                      << " auxiliary=0x" << std::hex << auxiliary << std::dec << '\n';
            return false;
        }
        std::array<unsigned char, wrapped_key_material_bytes> material{};
        if (!unwrap_key_material(transport_key_.data(), transport_key_.size(),
                blob, size, material.data())) {
            std::cerr << "QCE_V6_IMPORT_REJECTED model=" << name()
                      << " reason=authentication bytes=" << size << '\n';
            return false;
        }
        std::memcpy(imported_keys_[destination].data(), material.data(), material.size());
        imported_valid_[destination] = true;
        OPENSSL_cleanse(material.data(), material.size());
        std::cerr << "QCE_V6_KEY_IMPORTED model=" << name()
                  << " destination=" << destination
                  << " bytes=" << wrapped_key_material_bytes << '\n';
        return true;
    }

    // This view reports presence only. It never returns key material.
    bool imported_key_present(unsigned destination) const {
        return destination < imported_valid_.size() && imported_valid_[destination];
    }

    SC_HAS_PROCESS(QceV6);

    explicit QceV6(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QceV6::transport);
        stream_socket.register_b_transport(this, &QceV6::stream_transport);
        SC_THREAD(ready_worker);
    }

protected:
    void end_of_elaboration() override {
        if (first_slot_count.get_value() > 63 || total_slot_count.get_value() > 127 ||
            first_slot_count.get_value() > total_slot_count.get_value() ||
            primary_destination.get_value() >= total_slot_count.get_value() ||
            secondary_destination.get_value() >= total_slot_count.get_value() ||
            primary_destination.get_value() > 31 || secondary_destination.get_value() > 31 ||
            selector_base0.get_value() > 255 || selector_base1.get_value() > 255 ||
            selector_base2.get_value() > 255) {
            SC_REPORT_FATAL("QCE_V6", "Invalid virtual GPCE geometry");
        }
        if (!aes_ctx_)
            SC_REPORT_FATAL("QCE_V6", "Cannot allocate the AES context");
        const auto& configured_key = oem_hw_key_hex.get_value();
        if (configured_key.empty()) {
            (void)SHA256(reinterpret_cast<const unsigned char*>(oem_hw_key_label),
                sizeof(oem_hw_key_label) - 1, oem_hw_key_.data());
        } else if (!decode_hex_key(configured_key, oem_hw_key_.data(), oem_hw_key_.size())) {
            throw std::invalid_argument(
                "QCE oem_hw_key_hex must contain exactly 64 hexadecimal characters");
        }
        std::cerr << "QCE_V6_POLICY model=" << name()
                  << " version=0x" << std::hex << version.get_value() << std::dec
                  << " bist=completed-cold first_slots=" << first_slot_count.get_value()
                  << " total_slots=" << total_slot_count.get_value()
                  << " primary_destination=" << primary_destination.get_value()
                  << " secondary_destination=" << secondary_destination.get_value()
                  << " sha=sha1-sha256-sha384-sha512 aes=cmac128-cmac256-cmac256-oem-hw-key engines=0x31"
                     " keys=retained-hidden tpkey=cold-absent ciphers=unsupported\n";
    }

    void start_of_simulation() override {
        if (stream_ready.get_interface())
            stream_ready->write(false);
        ready_level_ = false;
    }

private:
    static constexpr std::uint32_t bank_base = 0xa000;
    static constexpr std::uint32_t reg_version = 0x000;
    static constexpr std::uint32_t reg_status = 0x100;
    static constexpr std::uint32_t reg_status2 = 0x104;
    static constexpr std::uint32_t reg_engines_avail = 0x108;
    static constexpr std::uint32_t reg_engines_avail_ext = 0x140;
    static constexpr std::uint32_t reg_fifo_sizes = 0x10c;
    static constexpr std::uint32_t reg_seg_size = 0x110;
    static constexpr std::uint32_t reg_goproc = 0x120;
    static constexpr std::uint32_t reg_encr_cfg = 0x200;
    static constexpr std::uint32_t reg_encr_size = 0x204;
    static constexpr std::uint32_t reg_encr_start = 0x208;
    static constexpr std::uint32_t reg_cntr_iv = 0x20c;
    static constexpr std::uint32_t reg_cntr_mask = 0x21c;
    static constexpr std::uint32_t reg_ccm_cnt = 0x220;
    static constexpr std::uint32_t reg_xts_du = 0x230;
    static constexpr std::uint32_t reg_cntr_mask2 = 0x234;
    static constexpr std::uint32_t reg_cntr_mask1 = 0x238;
    static constexpr std::uint32_t reg_cntr_mask0 = 0x23c;
    static constexpr std::uint32_t reg_auth_cfg = 0x300;
    static constexpr std::uint32_t reg_auth_size = 0x304;
    static constexpr std::uint32_t reg_auth_start = 0x308;
    static constexpr std::uint32_t reg_auth_iv = 0x310;
    static constexpr std::uint32_t reg_auth_nonce = 0x350;
    static constexpr std::uint32_t reg_auth_count = 0x390;
    static constexpr std::uint32_t reg_auth_mac = 0x3a0;
    static constexpr std::uint32_t reg_config = 0x400;
    static constexpr std::uint32_t reg_key = 0x3000;
    static constexpr std::uint32_t reg_auth_key = 0x3040;
    static constexpr std::uint32_t reg_key_last = 0x307c;
    // The key bank at physical 01dfd000 reaches this model at offset d000.
    // It holds the QCE key-register offsets 3000..307c.
    static constexpr std::uint32_t key_bank_first = 0xd000;
    static constexpr std::uint32_t key_bank_last =
        key_bank_first + (reg_key_last - reg_key);
    // QSEE 156d69d0 issues a BAM command write of zero to QCE offset 5000.
    // The word is a retained scratch target. It has no lock or completion effect.
    static constexpr std::uint32_t bam_scratch = 0xf000;

    static constexpr std::uint32_t status_sw_err = 1u << 0;
    static constexpr std::uint32_t status_done = 1u << 1;
    static constexpr std::uint32_t status_auth_busy = 1u << 8;
    static constexpr std::uint32_t status_cfg_change_err = 1u << 17;
    // QSEE polls STATUS2 bit zero for idle before it reads a result. See the
    // 156d65d0 loop in the selected image. The top error bit is bit 31.
    static constexpr std::uint32_t status2_busy = 1u << 0;
    static constexpr std::uint32_t status2_error = 1u << 31;
    static constexpr std::uint32_t config_le = 1u << 9;
    static constexpr std::uint32_t config_pipe_set_shift = 5;
    static constexpr std::uint32_t config_pipe_set_mask = 0xf;
    static constexpr std::uint32_t go_bit = 1u << 0;
    static constexpr std::uint32_t go_clear_context = 1u << 1;
    static constexpr std::uint32_t go_result_dump = 1u << 2;
    // QSEE 156d22a0 tests engine bits against 01dfa108 and 01dfa140.
    // The first word uses mask fc07ffff and bit 4 is CRYPTO_SHA_SEL.
    // Bit 4 selects the SHA-1/SHA-256 engine. Bit 5 selects the SHA-512 family engine.
    // QSEE 156d239c tests bit 5 for its SHA-384 request. This profile implements
    // SHA-384 and SHA-512 behind that bit.
    // QSEE 156d2448 tests bit 0 (AES) before its RPMB key derivation. This profile
    // implements the AES CMAC request with a supplied or OEM hardware key.
    static constexpr std::uint32_t engines_available = (1u << 4) | (1u << 5) | (1u << 0);
    // QSEE 156d22a0 also tests the 21 low bits of 01dfa140 against a second
    // engine set. No selected source names those bits and this profile
    // implements no engine behind them. The model reports zero.
    static constexpr std::uint32_t engines_avail_ext_absent = 0;
    // QSEE 15703ac8 reads these words at 01df0000..01df000c and copies them
    // into the info record for id 0x100. The declared identity values identify
    // the modelled IP; this review found no independent record of them.
    // They are not fuse values or keys.
    static constexpr std::uint32_t reg_km_ipcat_version = 0x0000;      // KM_IPCAT_VERSION
    static constexpr std::uint32_t reg_km_key_policy_version = 0x0004; // KM_KEY_POLICY_VERSION
    static constexpr std::uint32_t reg_km_shared_status = 0x0008;      // KM_SHARED_STATUS
    static constexpr std::uint32_t reg_km_keytable_size = 0x000c;      // KM_KEYTABLE_SIZE
    static constexpr std::uint32_t km_ipcat_version = 0x02020000;
    static constexpr std::uint32_t km_key_policy_version = 4;
    static constexpr std::uint32_t km_shared_busy = 1u << 0;
    static constexpr std::uint32_t km_keytable_slots_mask = 0xff;
    // KM_STATUS at 01df1004. 15703ac8 reports bit 6 or bit 8 as a self-test
    // error and copies no other field.
    static constexpr std::uint32_t reg_km_status = 0x1004;
    static constexpr std::uint32_t km_status_bist_done = 1u << 9;
    static constexpr std::uint32_t km_status_bist_error = 1u << 8;
    static constexpr std::uint32_t km_status_crypto_bist_done = 1u << 7;
    static constexpr std::uint32_t km_status_crypto_bist_error = 1u << 6;
    static constexpr std::uint32_t km_status_boot_list1_done = 1u << 2;
    static constexpr std::uint32_t km_status_boot_list0_done = 1u << 1;
    static constexpr std::uint32_t km_status_kt_clear_done = 1u << 0;
    // QSEE 157039e0 clears CRC_CHECK_EN at 01df1000 before the first
    // key-table use. The model retains that write.
    // The source-only reset value 7 remains unestablished.
    static constexpr std::uint32_t km_ctl_field_mask = 0x1f;
    static constexpr std::uint32_t km_ctl_crc_check_en = 1u << 0;
    // 15703a28 sets bit8 of 01df101c and replaces the low byte with 0x3e.
    // 15703a60 polls bit8 of 01df1020. Public ice.c names the control word
    // TPKEY_RECEIVE_CTL and uses the same bit for the same purpose.
    static constexpr std::uint32_t transport_key_receive_enable = 1u << 8;
    static constexpr std::uint32_t transport_key_control_mask = 0x1ff;
    // 15703b74 drives the GPCE FIFO at 01df2000. The request is one command
    // word and 96 blob bytes. The response is two words.
    static constexpr unsigned fifo_input_words = 25;
    static constexpr unsigned fifo_output_words = 2;
    static constexpr std::uint32_t fifo_input_space = 0x7c000;
    static constexpr std::uint32_t fifo_output_ready = 0x3e00;
    static constexpr std::uint32_t fifo_done_bit = 1u << 1;
    static constexpr std::uint32_t fifo_command_base = 0xc8000004u;
    static constexpr std::uint32_t fifo_field_mask = 0xff;
    static constexpr unsigned fifo_destination_shift = 5;
    static constexpr unsigned fifo_auxiliary_shift = 13;
    static constexpr std::uint32_t auth_alg_mask = 0x7;
    static constexpr std::uint32_t auth_alg_sha = 1;
    static constexpr std::uint32_t auth_mode_shift = 6;
    static constexpr std::uint32_t auth_mode_mask = 0x7;
    static constexpr std::uint32_t auth_mode_hash = 0;
    static constexpr std::uint32_t auth_size_shift = 9;
    static constexpr std::uint32_t auth_size_mask = 0x1f;
    static constexpr std::uint32_t auth_last_bit = 1u << 16;
    static constexpr std::uint32_t auth_nonce_words_shift = 20;
    static constexpr std::uint32_t auth_nonce_words_mask = 0x7;
    static constexpr std::uint32_t auth_comp_exp_mac = 1u << 24;
    static constexpr std::uint32_t auth_f9_direction = 1u << 23;
    static constexpr std::uint32_t auth_hw_key = 1u << 18;
    static constexpr std::uint32_t auth_pipe_key = 1u << 19;
    static constexpr std::uint32_t result_dump_size = 128;
    static constexpr std::uint32_t sha256_block_size = 64;
    static constexpr std::uint32_t sha512_block_size = 128;
    // QSEE 156d7cf8 places the context algorithm size at AUTH_SIZE. Its gate calls
    // store zero for SHA-1, one for SHA-256, two for SHA-384 (156c6bdc), and three
    // for SHA-512 (156c6f7c).
    static constexpr std::uint32_t auth_size_sha1 = 0;
    static constexpr std::uint32_t auth_size_sha256 = 1;
    static constexpr std::uint32_t auth_size_sha384 = 2;
    static constexpr std::uint32_t auth_size_sha512 = 3;
    // Run727 QSEE RPMB key derivation programs AUTH_SEG_CFG 0x65e52 and writes
    // GOPROC_OEM_KEY. Linux regs-v5.h decodes AES, CMAC, AES-256, and USE_HW_KEY.
    static constexpr std::uint32_t reg_goproc_oem_key = 0x2000;
    static constexpr std::uint32_t auth_alg_aes = 2;
    static constexpr std::uint32_t auth_mode_cmac = 1;
    static constexpr std::uint32_t auth_key_size_shift = 3;
    static constexpr std::uint32_t auth_key_size_mask = 0x7;
    static constexpr std::uint32_t auth_key_aes128 = 0;
    static constexpr std::uint32_t auth_key_aes256 = 2;
    static constexpr std::uint32_t auth_first_bit = 1u << 17;
    static constexpr std::uint32_t auth_size_aes_mac = 15;
    static constexpr std::uint32_t aes_block_size = 16;
    // Silicon reads its OEM hardware key from secure fuses.
    // The empty virtual profile uses this public label instead.
    // A configured key is a declared input. The model does not infer its origin.
    static constexpr char oem_hw_key_label[] = "qcom-vp virtual GPCE OEM hardware key v1";
    static int hex_nibble(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }
    static bool decode_hex_key(const std::string& encoded, unsigned char* output,
                               std::size_t size) {
        if (encoded.size() != size * 2) return false;
        for (std::size_t i = 0; i < size; ++i) {
            const int high = hex_nibble(encoded[2 * i]);
            const int low = hex_nibble(encoded[2 * i + 1]);
            if (high < 0 || low < 0) return false;
            output[i] = static_cast<unsigned char>((high << 4) | low);
        }
        return true;
    }

    // aes_cmac is keyed AES CMAC. The others are plain hashes.
    enum class HashAlg { none, sha1, sha256, sha384, sha512, aes_cmac };
    static bool sha512_family(HashAlg alg) {
        return alg == HashAlg::sha384 || alg == HashAlg::sha512;
    }
    enum class PrivateWord { busy_clear = 0, pipe_control = 1, auth_control = 2, hash_control = 3 };

    // Startup controls retained from the earlier GPCE owner.
    std::array<std::uint32_t, 63> slot_controls_{};
    std::array<std::uint32_t, 10> access_controls_{};
    std::array<std::uint32_t, 4> masks_{};
    std::array<std::uint32_t, 4> private_words_{};
    std::uint32_t control_ = 0;
    std::uint32_t transport_key_control_ = 0;
    std::uint32_t fifo_configuration_ = 0;
    // 15703b74 transaction state. The FIFO holds one request at a time.
    std::uint32_t fifo_control_ = 0;
    std::uint32_t fifo_ack_ = 0;
    std::array<std::uint32_t, fifo_input_words> fifo_input_{};
    std::array<std::uint32_t, fifo_output_words> fifo_output_{};
    unsigned fifo_input_count_ = 0;
    unsigned fifo_output_count_ = 0;
    unsigned fifo_output_read_ = 0;
    bool fifo_done_ = false;
    // Hardware handshake state for the TME transport-key broadcast.
    std::array<unsigned char, transport_key_bytes> transport_key_{};
    std::array<unsigned char, transport_key_bytes> pending_transport_key_{};
    bool transport_key_pending_ = false;
    bool transport_key_ready_ = false;
    unsigned transport_key_dks_ = 0;
    // Set by the post-reset KM_CTL write that clears CRC_CHECK_EN. It is the
    // only self-test step this model runs.
    bool km_boot_init_ = false;
    // Normal and special destination consumers supply auxiliary selector 3e.
    static constexpr unsigned import_auxiliary_selector = 0x3e;
    std::array<std::array<unsigned char, wrapped_key_material_bytes>, 64> imported_keys_{};
    std::array<bool, 64> imported_valid_{};

    // QCE register state.
    std::uint32_t status_latched_ = 0;
    std::uint32_t status2_latched_ = 0;
    std::uint32_t config_ = 0;
    bool config_written_ = false;
    std::uint32_t seg_size_ = 0;
    std::uint32_t auth_cfg_ = 0;
    std::uint32_t auth_size_ = 0;
    std::uint32_t auth_start_ = 0;
    std::uint32_t encr_cfg_ = 0;
    std::uint32_t encr_size_ = 0;
    std::uint32_t encr_start_ = 0;
    std::array<std::uint32_t, 4> cntr_iv_{};
    std::uint32_t cntr_mask_ = 0;
    std::array<std::uint32_t, 4> ccm_counters_{};
    std::uint32_t xts_du_ = 0;
    std::array<std::uint32_t, 4> cntr_masks_{};
    // Key writes use the CONFIG byte lanes. Only AES CMAC reads these words.
    std::array<std::uint32_t, 32> key_words_{};
    std::array<std::uint32_t, 4> nonce_words_{};
    // GOPROC_OEM_KEY selects this key for the following request only.
    bool oem_key_go_ = false;
    std::array<unsigned char, 32> oem_hw_key_{};

    // AUTH_IV words hold the mathematical SHA state words. Under CONFIG
    // little-endian mode each AUTH_IV MMIO word is byte swapped. The byte
    // counter words are numeric control values.
    std::array<std::uint32_t, 16> iv_words_{};
    std::array<std::uint32_t, 4> count_words_{};

    // Operation state. The OpenSSL context holds the running hash and the
    // model commits AUTH_IV and the byte counts only after every requested
    // byte arrived.
    HashAlg op_alg_ = HashAlg::none;
    bool busy_ = false;
    bool op_final_ = false;
    std::uint64_t op_total_count_ = 0;
    std::uint32_t op_seg_size_ = 0;
    std::uint32_t op_received_ = 0;
    SHA_CTX sha1_ctx_{};
    SHA256_CTX sha256_ctx_{};
    SHA512_CTX sha512_ctx_{};
    // AES CMAC state. The pending block stays unprocessed until more data
    // arrives, because only the last block of a final request takes a subkey.
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> aes_ctx_{
        EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free};
    std::array<unsigned char, 16> cmac_state_{};
    std::array<unsigned char, 16> cmac_pending_{};
    unsigned cmac_pending_length_ = 0;
    std::uint32_t active_pipe_set_ = 0;
    std::uint32_t bam_scratch_ = 0;

    // Result dump. The buffer is served on the output BAM pipe.
    bool dump_requested_ = false;
    bool dump_pending_ = false;
    std::uint32_t dump_offset_ = 0;
    std::array<std::uint8_t, result_dump_size> dump_{};

    // Stream readiness events are raised outside the current transport call.
    // This prevents a re-entrant BAM callback during a stream transaction.
    sc_core::sc_event ready_event_;
    bool ready_dirty_ = false;
    bool ready_level_ = false;

    static std::uint32_t bswap32(std::uint32_t value) {
        return ((value & 0xffu) << 24) | ((value & 0xff00u) << 8) |
               ((value >> 8) & 0xff00u) | (value >> 24);
    }

    static std::uint32_t load_be32(const std::uint8_t* p) {
        return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
               (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
    }

    static void store_be32(std::uint8_t* p, std::uint32_t value) {
        p[0] = std::uint8_t(value >> 24);
        p[1] = std::uint8_t(value >> 16);
        p[2] = std::uint8_t(value >> 8);
        p[3] = std::uint8_t(value);
    }

    static bool is_private_offset(std::uint32_t offset) {
        return offset == 0x11c || offset == 0x124 || offset == 0x128 || offset == 0x13c;
    }

    static unsigned private_index(std::uint32_t offset) {
        switch (offset) {
        case 0x11c: return static_cast<unsigned>(PrivateWord::busy_clear);
        case 0x124: return static_cast<unsigned>(PrivateWord::pipe_control);
        case 0x128: return static_cast<unsigned>(PrivateWord::auth_control);
        default: return static_cast<unsigned>(PrivateWord::hash_control);
        }
    }

    std::uint32_t mmio_field_word(std::uint32_t word) const {
        return (config_ & config_le) ? bswap32(word) : word;
    }

    std::uint32_t decode_field_word(std::uint32_t value) const {
        return (config_ & config_le) ? bswap32(value) : value;
    }

    std::uint64_t programmed_count() const {
        return (std::uint64_t{count_words_[0]} << 32) | count_words_[1];
    }

    unsigned config_pipe_set() const {
        return (config_ >> config_pipe_set_shift) & config_pipe_set_mask;
    }

    bool pipe_policy_known() const {
        return config_written_ || busy_ || dump_pending_;
    }

    unsigned current_pipe_set() const {
        return (busy_ || dump_pending_) ? active_pipe_set_ : config_pipe_set();
    }

    // A broadcast that arrives with the receive window closed stays pending.
    // Opening the window accepts it and sets the 01df1020 bit8 status.
    void accept_pending_transport_key() {
        if (!transport_key_pending_) return;
        std::memcpy(transport_key_.data(), pending_transport_key_.data(),
            pending_transport_key_.size());
        OPENSSL_cleanse(pending_transport_key_.data(), pending_transport_key_.size());
        transport_key_pending_ = false;
        transport_key_ready_ = true;
        // TPKEY_RECEIVE_STATUS.TPKEY_DKS reports the slot the receive window names.
        transport_key_dks_ = transport_key_control_ & 0xff;
        std::cerr << "QCE_V6_TPKEY_ACCEPTED model=" << name()
                  << " bits=" << transport_key_bytes * 8 << " state=retained\n";
    }

    // Work that the engine accepted and did not finish. A bank command is in
    // flight between its first word and its response. A delivered transport key
    // waits until the receive window opens.
    bool km_busy() const {
        const bool command_in_flight = !fifo_done_ && fifo_input_count_ != 0 &&
            fifo_input_count_ < fifo_input_.size();
        return transport_key_pending_ || command_in_flight;
    }

    bool key_table_empty() const {
        for (bool valid : imported_valid_)
            if (valid) return false;
        return true;
    }

    // KM_STATUS reports state that this model reached. BIST_DONE and
    // CRYPTO_LIB_BIST_DONE follow the post-reset KM_CTL write. The model runs
    // no boot command list and runs no self-test that can fail, so the error
    // bits and the boot-list bits stay zero.
    std::uint32_t km_status() const {
        if (!km_boot_init_) return 0;
        std::uint32_t value = km_status_bist_done | km_status_crypto_bist_done;
        if (key_table_empty()) value |= km_status_kt_clear_done;
        return value;
    }

    // The selected firmware starts a request when it sets control bit0.
    void fifo_start() {
        fifo_input_.fill(0);
        fifo_input_count_ = 0;
        fifo_output_.fill(0);
        fifo_output_count_ = 0;
        fifo_output_read_ = 0;
        fifo_done_ = false;
    }

    void fifo_push(std::uint32_t word) {
        if (fifo_input_count_ >= fifo_input_.size()) return;
        fifo_input_[fifo_input_count_++] = word;
        if (fifo_input_count_ == fifo_input_.size()) fifo_execute();
    }

    // A complete request installs one destination key or reports a failure.
    // The status word is real: a rejected blob never reports zero.
    void fifo_execute() {
        const auto command = fifo_input_[0];
        const std::uint32_t field = (fifo_field_mask << fifo_destination_shift) |
            (fifo_field_mask << fifo_auxiliary_shift);
        bool installed = false;
        if ((command & ~field) == fifo_command_base) {
            const unsigned destination =
                (command >> fifo_destination_shift) & fifo_field_mask;
            const unsigned auxiliary =
                (command >> fifo_auxiliary_shift) & fifo_field_mask;
            std::array<unsigned char, 4 * (fifo_input_words - 1)> blob{};
            for (unsigned i = 0; i != blob.size() / 4; ++i) {
                for (unsigned n = 0; n != 4; ++n)
                    blob[i * 4 + n] =
                        static_cast<unsigned char>(fifo_input_[1 + i] >> (8 * n));
            }
            installed = import_wrapped_key(blob.data(), blob.size(),
                destination, auxiliary);
        } else {
            std::cerr << "QCE_V6_FIFO_REJECTED model=" << name()
                      << " reason=command word=0x" << std::hex << command
                      << std::dec << '\n';
        }
        fifo_output_[0] = 0;
        fifo_output_[1] = installed ? 0 : 1;
        fifo_output_count_ = fifo_output_.size();
        fifo_output_read_ = 0;
        fifo_done_ = true;
        std::cerr << "QCE_V6_FIFO_COMPLETE model=" << name()
                  << " status=" << fifo_output_[1] << '\n';
    }

    bool can_accept_input() const {
        return busy_ && op_received_ < op_seg_size_;
    }

    bool can_serve_output() const {
        return dump_pending_ && dump_offset_ < dump_.size();
    }

    void schedule_ready() {
        if (!ready_dirty_) {
            ready_dirty_ = true;
            ready_event_.notify(sc_core::SC_ZERO_TIME);
        }
    }

    void ready_worker() {
        while (true) {
            wait(ready_event_);
            ready_dirty_ = false;
            if (!stream_ready.get_interface()) continue;
            if (can_accept_input() || can_serve_output()) {
                // A same-value write is a deliberate wake event for a stalled pipe.
                stream_ready->write(true);
                ready_level_ = true;
            } else if (ready_level_) {
                stream_ready->write(false);
                ready_level_ = false;
            }
        }
    }

    // Startup register read and write handling.
    bool read(std::uint64_t address, std::uint32_t& value) {
        switch (address) {
        case 0xa180: value = first_slot_count.get_value() << 24; return true;
        case 0xa184: value = total_slot_count.get_value() << 23; return true;
        case 0xab00: value = selector_base0.get_value(); return true;
        case 0xab08: value = selector_base1.get_value(); return true;
        case 0xab10: value = selector_base2.get_value(); return true;
        case 0xab14: value = primary_destination.get_value(); return true;
        case 0xab18: value = secondary_destination.get_value(); return true;
        // 15703ac8 copies the shared identity words and the TZ status word.
        case reg_km_ipcat_version: value = km_ipcat_version; return true;
        case reg_km_key_policy_version: value = km_key_policy_version; return true;
        case reg_km_shared_status: value = km_busy() ? km_shared_busy : 0; return true;
        case reg_km_keytable_size:
            value = total_slot_count.get_value() & km_keytable_slots_mask;
            return true;
        case reg_km_status: value = km_status(); return true;
        case 0x1000: value = control_; return true;
        case 0x101c: value = transport_key_control_; return true;
        // 15703a60 waits for bit8. Only a delivered broadcast sets it.
        // TPKEY_DKS reports the slot that the receive window named.
        case 0x1020:
            value = transport_key_ready_ ?
                (transport_key_receive_enable | transport_key_dks_) : 0;
            return true;
        case bam_scratch: value = bam_scratch_; return true;
        case 0x2000: value = fifo_control_; return true;
        case 0x2004: {
            // Only the nonzero conditions the selected code tests are encoded.
            const bool input = fifo_input_count_ < fifo_input_.size() && !fifo_done_;
            const bool output = fifo_output_read_ < fifo_output_count_;
            value = (input ? fifo_input_space : 0) | (output ? fifo_output_ready : 0);
            return true;
        }
        case 0x2008:
            value = fifo_configuration_ | (fifo_done_ ? fifo_done_bit : 0);
            return true;
        case 0x2010: value = fifo_ack_; return true;
        case 0x201c:
            value = fifo_input_count_ ? fifo_input_[fifo_input_count_ - 1] : 0;
            return true;
        case 0x205c:
            value = fifo_output_read_ < fifo_output_count_ ?
                fifo_output_[fifo_output_read_++] : 0;
            return true;
        default: break;
        }
        if (address >= 0xf100 && address <= 0xf124 && !(address & 3)) {
            value = access_controls_[(address - 0xf100) / 4];
            return true;
        }
        if (address >= 0x5000 && address <= 0x500c && !(address & 3)) {
            value = masks_[(address - 0x5000) / 4];
            return true;
        }
        for (unsigned n = 0; n < first_slot_count.get_value(); ++n) {
            const std::uint64_t slot_address = (n <= 15 ? 0xe004 : 0xe704) + std::uint64_t(n) * 16;
            if (address == slot_address) {
                value = slot_controls_[n];
                return true;
            }
        }
        if (address >= bank_base && address < bank_base + 0x400 + 0x10)
            return read_qce(static_cast<std::uint32_t>(address - bank_base), value);
        if (address >= key_bank_first && address <= key_bank_last)
            return read_qce(reg_key + static_cast<std::uint32_t>(address - key_bank_first), value);

        return false;
    }

    bool read_qce(std::uint32_t offset, std::uint32_t& value) const {
        switch (offset) {
        case reg_version: value = version.get_value(); return true;
        case reg_status: value = status_latched_ | (busy_ ? status_auth_busy : 0); return true;
        case reg_status2: value = (busy_ ? status2_busy : 0) | status2_latched_; return true;
        case reg_engines_avail: value = engines_available; return true;
        case reg_engines_avail_ext:
            // QSEE 156d22a0 checks a second engine bitmap here with mask
            // 1fffff. This profile implements none of those engines.
            value = engines_avail_ext_absent;
            std::cerr << "QCE_V6_EXT_CAPABILITIES model=" << name()
                      << " value=0x" << std::hex << engines_avail_ext_absent
                      << std::dec << '\n';
            return true;
        case reg_fifo_sizes: value = 0; return true;
        case reg_seg_size: value = seg_size_; return true;
        case reg_goproc: value = 0; return true;
        case reg_encr_cfg: value = encr_cfg_; return true;
        case reg_encr_size: value = encr_size_; return true;
        case reg_encr_start: value = encr_start_; return true;
        case reg_cntr_mask: value = cntr_mask_; return true;
        case reg_xts_du: value = xts_du_; return true;
        case reg_cntr_mask2: value = cntr_masks_[0]; return true;
        case reg_cntr_mask1: value = cntr_masks_[1]; return true;
        case reg_cntr_mask0: value = cntr_masks_[2]; return true;
        case reg_auth_cfg: value = auth_cfg_; return true;
        case reg_auth_size: value = auth_size_; return true;
        case reg_auth_start: value = auth_start_; return true;
        case reg_config: value = config_; return true;
        default: break;
        }
        if (is_private_offset(offset)) {
            value = private_words_[private_index(offset)];
            return true;
        }
        if (offset >= reg_cntr_iv && offset <= reg_cntr_iv + 0xc && !(offset & 3)) {
            value = cntr_iv_[(offset - reg_cntr_iv) / 4];
            return true;
        }
        if (offset >= reg_ccm_cnt && offset <= reg_ccm_cnt + 0xc && !(offset & 3)) {
            value = ccm_counters_[(offset - reg_ccm_cnt) / 4];
            return true;
        }
        if (offset >= reg_auth_iv && offset <= reg_auth_iv + 0x3c && !(offset & 3)) {
            value = mmio_field_word(iv_words_[(offset - reg_auth_iv) / 4]);
            return true;
        }
        if (offset >= reg_auth_count && offset <= reg_auth_count + 0xc && !(offset & 3)) {
            // The byte counter is a numeric control value. Unlike AUTH_IV it is
            // not part of the little-endian data byte lanes.
            value = count_words_[(offset - reg_auth_count) / 4];
            return true;
        }
        if (offset >= reg_auth_nonce && offset <= reg_auth_nonce + 0xc && !(offset & 3)) {
            value = nonce_words_[(offset - reg_auth_nonce) / 4];
            return true;
        }
        // Zero key readback is an explicit model limitation, not a silicon claim.
        // Never expose retained key material through MMIO.
        if (offset >= reg_key && offset <= reg_key_last && !(offset & 3)) {
            value = 0;
            return true;
        }
        // Expected-MAC comparison is not implemented. Reads return zero.
        if (offset >= reg_auth_mac && offset <= reg_auth_mac + 0x1c && !(offset & 3)) {
            value = 0;
            return true;
        }
        return false;
    }

    bool write(std::uint64_t address, std::uint32_t value) {
        switch (address) {
        case 0x1000:
            if (value & ~km_ctl_field_mask) return false;
            control_ = value;
            // 157039e0 clears CRC_CHECK_EN after reset. That write completes the
            // only self-test step that this model runs.
            if (!(value & km_ctl_crc_check_en)) km_boot_init_ = true;
            return true;
        case 0x101c:
            if (value & ~transport_key_control_mask) return false;
            transport_key_control_ = value;
            if (transport_key_control_ & transport_key_receive_enable)
                accept_pending_transport_key();
            return true;
        case 0x2000:
            if (value & ~3u) return false;
            fifo_control_ = value;
            if (value & 1u) fifo_start();
            return true;
        case 0x2008:
            if (value & ~(fifo_done_bit | 8u)) return false;
            fifo_configuration_ = value & 8u;
            if (value & fifo_done_bit) fifo_done_ = false;
            return true;
        case 0x2010: fifo_ack_ = value; return true;
        case 0x201c:
            if (fifo_input_count_ >= fifo_input_.size()) return false;
            fifo_push(value);
            return true;
        case bam_scratch: bam_scratch_ = value; return true;
        default: break;
        }
        if (address >= 0xf100 && address <= 0xf124 && !(address & 3)) {
            if (value & ~0x3fu) return false;
            access_controls_[(address - 0xf100) / 4] = value;
            return true;
        }
        if (address >= 0x5000 && address <= 0x500c && !(address & 3)) {
            masks_[(address - 0x5000) / 4] = value;
            return true;
        }
        for (unsigned n = 0; n < first_slot_count.get_value(); ++n) {
            const std::uint64_t slot_address = (n <= 15 ? 0xe004 : 0xe704) + std::uint64_t(n) * 16;
            if (address == slot_address) {
                if (value != 1) return false;
                slot_controls_[n] = value;
                return true;
            }
        }
        if (address >= bank_base && address < bank_base + 0x400 + 0x10)
            return write_qce(static_cast<std::uint32_t>(address - bank_base), value);
        if (address >= key_bank_first && address <= key_bank_last)
            return write_qce(reg_key + static_cast<std::uint32_t>(address - key_bank_first), value);
        // GOPROC_OEM_KEY starts a request with the OEM hardware key. GOPROC_QC_KEY
        // at 0xb000 has no selected consumer and stays unmapped.
        if (address == bank_base + reg_goproc_oem_key) {
            oem_key_go_ = true;
            const bool accepted = go(value);
            oem_key_go_ = false;
            return accepted;
        }
        return false;
    }

    bool write_qce(std::uint32_t offset, std::uint32_t value) {
        if (offset == reg_status) {
            status_latched_ = 0;
            return true;
        }
        if (offset == reg_status2) {
            // QSEE writes zero to STATUS2 before each request. This clears the
            // latched error bits and does not change the busy bit.
            if (value != 0) return false;
            status2_latched_ = 0;
            return true;
        }
        if (offset == reg_version || offset == reg_engines_avail || offset == reg_fifo_sizes)
            return false;
        // Expected-MAC comparison is not implemented.
        if (offset >= reg_auth_mac && offset <= reg_auth_mac + 0x1c && !(offset & 3)) {
            return false;
        }
        // A configuration write during an active operation must not corrupt it.
        // QCE reports CFG_CHNG_ERR for this case.
        if (busy_) {
            status_latched_ |= status_cfg_change_err;
            std::cerr << "QCE_V6_BUSY_WRITE model=" << name()
                      << " offset=0x" << std::hex << offset << std::dec << '\n';
            return true;
        }
        switch (offset) {
        case reg_seg_size:
            seg_size_ = value;
            return true;
        case reg_goproc:
            return go(value);
        case reg_encr_cfg:
            encr_cfg_ = value;
            return true;
        case reg_encr_size:
            encr_size_ = value;
            return true;
        case reg_encr_start:
            encr_start_ = value;
            return true;
        case reg_cntr_mask:
            cntr_mask_ = value;
            return true;
        case reg_xts_du:
            xts_du_ = value;
            return true;
        case reg_cntr_mask2:
            cntr_masks_[0] = value;
            return true;
        case reg_cntr_mask1:
            cntr_masks_[1] = value;
            return true;
        case reg_cntr_mask0:
            cntr_masks_[2] = value;
            return true;
        case reg_auth_cfg:
            auth_cfg_ = value;
            return true;
        case reg_auth_size:
            auth_size_ = value;
            return true;
        case reg_auth_start:
            auth_start_ = value;
            return true;
        case reg_config:
            config_ = value;
            config_written_ = true;
            return true;
        default: break;
        }
        if (is_private_offset(offset)) {
            // Only the evidenced zero write is accepted for these private words.
            if (value != 0) return false;
            private_words_[private_index(offset)] = 0;
            return true;
        }
        if (offset >= reg_cntr_iv && offset <= reg_cntr_iv + 0xc && !(offset & 3)) {
            cntr_iv_[(offset - reg_cntr_iv) / 4] = value;
            return true;
        }
        if (offset >= reg_ccm_cnt && offset <= reg_ccm_cnt + 0xc && !(offset & 3)) {
            ccm_counters_[(offset - reg_ccm_cnt) / 4] = value;
            return true;
        }
        if (offset >= reg_auth_iv && offset <= reg_auth_iv + 0x3c && !(offset & 3)) {
            iv_words_[(offset - reg_auth_iv) / 4] = decode_field_word(value);
            return true;
        }
        if (offset >= reg_auth_count && offset <= reg_auth_count + 0xc && !(offset & 3)) {
            count_words_[(offset - reg_auth_count) / 4] = value;
            return true;
        }
        if (offset >= reg_auth_nonce && offset <= reg_auth_nonce + 0xc && !(offset & 3)) {
            nonce_words_[(offset - reg_auth_nonce) / 4] = value;
            return true;
        }
        if (offset >= reg_key && offset <= reg_key_last && !(offset & 3)) {
            // AUTH_KEY takes the same write-time CONFIG byte lanes as AUTH_IV.
            // Linux programs big-endian key groups with bit9 clear and sets bit9
            // only before GO. QSEE stores its little-endian caller words with
            // bit9 set. The decode therefore uses the CONFIG value at the write.
            key_words_[(offset - reg_key) / 4] = decode_field_word(value);
            return true;
        }
        return false;
    }

    bool go(std::uint32_t value) {
        if (value & ~(go_bit | go_clear_context | go_result_dump)) {
            std::cerr << "QCE_V6_GO_UNSUPPORTED model=" << name()
                      << " value=0x" << std::hex << value << std::dec << '\n';
            return false;
        }
        if (!(value & go_bit)) {
            std::cerr << "QCE_V6_GO_UNSUPPORTED model=" << name()
                      << " reason=missing-go value=0x" << std::hex << value << std::dec << '\n';
            return false;
        }
        if (busy_) {
            status_latched_ |= status_cfg_change_err;
            std::cerr << "QCE_V6_BUSY_WRITE model=" << name() << " offset=0x120\n";
            return true;
        }
        if (dump_pending_) {
            // Do not overwrite a result dump that the consumer did not read.
            status_latched_ |= status_sw_err | status_done;
            status2_latched_ |= status2_error;
            std::cerr << "QCE_V6_OP_REJECTED model=" << name()
                      << " reason=unread-result-dump\n";
            return true;
        }
        start_operation((value & go_result_dump) != 0);
        return true;
    }

    void reject_operation(const char* reason) {
        busy_ = false;
        (void)EVP_CIPHER_CTX_reset(aes_ctx_.get());
        status_latched_ |= status_sw_err | status_done;
        status2_latched_ |= status2_error;
        dump_requested_ = false;
        dump_pending_ = false;
        std::cerr << "QCE_V6_OP_REJECTED model=" << name()
                  << " reason=" << reason << " auth_cfg=0x" << std::hex << auth_cfg_
                  << std::dec << '\n';
        schedule_ready();
    }

    void start_operation(bool result_dump) {
        const std::uint32_t alg = auth_cfg_ & auth_alg_mask;
        const std::uint32_t mode = (auth_cfg_ >> auth_mode_shift) & auth_mode_mask;
        const std::uint32_t auth_size = (auth_cfg_ >> auth_size_shift) & auth_size_mask;
        const std::uint32_t nonce_words = (auth_cfg_ >> auth_nonce_words_shift) & auth_nonce_words_mask;
        const bool cmac = alg == auth_alg_aes;
        if (cmac) {
            if (const char* reason = cmac_request_error(mode, auth_size, nonce_words)) {
                reject_operation(reason);
                return;
            }
        } else if (oem_key_go_ || alg != auth_alg_sha || mode != auth_mode_hash || nonce_words != 0 ||
            (auth_cfg_ & (auth_comp_exp_mac | auth_f9_direction | auth_hw_key | auth_pipe_key)) ||
            auth_size > auth_size_sha512) {
            // AUTH_POS and AUTH_KEY_SIZE have no effect on a plain hash.
            // QSEE may set AUTH_POS for requests that never enable encryption.
            reject_operation("unsupported-auth-mode");
            return;
        }
        if (auth_start_ != 0 || seg_size_ != auth_size_) {
            reject_operation("unsupported-segment");
            return;
        }
        if (encr_cfg_ != 0 || encr_size_ != 0 || encr_start_ != 0) {
            reject_operation("unsupported-cipher");
            return;
        }
        if (count_words_[2] != 0 || count_words_[3] != 0) {
            reject_operation("unsupported-128bit-count");
            return;
        }
        const std::uint64_t prior = programmed_count();
        if (prior > UINT64_MAX - std::uint64_t{seg_size_}) {
            reject_operation("length-overflow");
            return;
        }
        const std::uint64_t total = prior + std::uint64_t{seg_size_};
        const std::uint32_t block = cmac ? aes_block_size
            : auth_size >= auth_size_sha384 ? sha512_block_size : sha256_block_size;
        if (prior % block != 0) {
            reject_operation("unaligned-count");
            return;
        }
        if (total > UINT64_MAX / 8) {
            reject_operation("length-overflow");
            return;
        }
        const bool final = (auth_cfg_ & auth_last_bit) != 0;
        if (!final && total % block != 0) {
            reject_operation("partial-nonfinal");
            return;
        }
        op_alg_ = cmac ? HashAlg::aes_cmac
            : auth_size == auth_size_sha512 ? HashAlg::sha512
            : auth_size == auth_size_sha384 ? HashAlg::sha384
            : auth_size == auth_size_sha256 ? HashAlg::sha256 : HashAlg::sha1;
        op_final_ = final;
        // AUTH_FIRST marks a new message. The model always uses the programmed
        // IV and byte count, so a caller-provided initial IV is preserved.
        op_total_count_ = total;
        op_seg_size_ = seg_size_;
        op_received_ = 0;
        // Inject the programmed IV and byte count into the OpenSSL context.
        // Nl and Nh hold the bit count, so the hardware byte count stays
        // separate in op_total_count_.
        const std::uint64_t prior_bits = prior * 8;
        if (cmac) {
            if (!cmac_begin()) {
                reject_operation("aes-unavailable");
                return;
            }
        } else if (sha512_family(op_alg_)) {
            // Each 64-bit state word occupies two AUTH_IV words, high half first.
            if (op_alg_ == HashAlg::sha384) (void)SHA384_Init(&sha512_ctx_);
            else (void)SHA512_Init(&sha512_ctx_);
            for (unsigned i = 0; i < 8; ++i)
                sha512_ctx_.h[i] = (std::uint64_t{iv_words_[2 * i]} << 32) | iv_words_[2 * i + 1];
            sha512_ctx_.Nl = prior_bits;
            sha512_ctx_.Nh = 0;
            sha512_ctx_.num = 0;
        } else if (op_alg_ == HashAlg::sha256) {
            (void)SHA256_Init(&sha256_ctx_);
            for (unsigned i = 0; i < 8; ++i) sha256_ctx_.h[i] = iv_words_[i];
            sha256_ctx_.Nl = std::uint32_t(prior_bits);
            sha256_ctx_.Nh = std::uint32_t(prior_bits >> 32);
            sha256_ctx_.num = 0;
        } else {
            (void)SHA1_Init(&sha1_ctx_);
            sha1_ctx_.h0 = iv_words_[0];
            sha1_ctx_.h1 = iv_words_[1];
            sha1_ctx_.h2 = iv_words_[2];
            sha1_ctx_.h3 = iv_words_[3];
            sha1_ctx_.h4 = iv_words_[4];
            sha1_ctx_.Nl = std::uint32_t(prior_bits);
            sha1_ctx_.Nh = std::uint32_t(prior_bits >> 32);
            sha1_ctx_.num = 0;
        }
        dump_requested_ = result_dump;
        dump_pending_ = false;
        dump_offset_ = 0;
        active_pipe_set_ = config_pipe_set();
        status_latched_ &= ~status_done;
        busy_ = true;
        if (op_seg_size_ == 0)
            finish_operation();
        else
            schedule_ready();
    }

    // Supplied AUTH_KEY supports AES-128/256. The evidenced OEM path uses
    // AES-256 with USE_HW_KEY through GOPROC_OEM_KEY. Both return 16-byte MACs.
    const char* cmac_request_error(std::uint32_t mode, std::uint32_t auth_size,
                                   std::uint32_t nonce_words) const {
        if (mode != auth_mode_cmac || nonce_words != 0 ||
            (auth_cfg_ & (auth_comp_exp_mac | auth_f9_direction | auth_pipe_key)))
            return "unsupported-aes-mode";
        const auto key_size = (auth_cfg_ >> auth_key_size_shift) & auth_key_size_mask;
        if (key_size != auth_key_aes128 && key_size != auth_key_aes256)
            return "unsupported-aes-key-size";
        if (auth_cfg_ & auth_hw_key) {
            if (!oem_key_go_) return "unsupported-aes-key-source";
            if (key_size != auth_key_aes256) return "unsupported-aes-key-size";
        } else if (oem_key_go_) {
            return "unsupported-aes-key-source";
        }
        if (auth_size != auth_size_aes_mac) return "unsupported-aes-mac-size";
        return nullptr;
    }

    // AUTH_FIRST starts the CMAC chain at zero. A continuation loads the
    // chaining value from AUTH_IV0..3 in the same byte order as a SHA digest.
    bool cmac_begin() {
        if (auth_cfg_ & auth_first_bit) {
            cmac_state_.fill(0);
        } else {
            for (unsigned i = 0; i < 4; ++i) store_be32(cmac_state_.data() + 4 * i, iv_words_[i]);
        }
        cmac_pending_length_ = 0;
        // GO captures the key before the stream supplies data.
        // Register writes already decode their CONFIG byte lanes.
        bool ok = false;
        if (auth_cfg_ & auth_hw_key) {
            ok = EVP_EncryptInit_ex(aes_ctx_.get(), EVP_aes_256_ecb(), nullptr,
                     oem_hw_key_.data(), nullptr) == 1;
        } else {
            std::array<unsigned char, 32> supplied_key{};
            const bool aes256 =
                ((auth_cfg_ >> auth_key_size_shift) & auth_key_size_mask) == auth_key_aes256;
            const unsigned words = aes256 ? 8 : 4;
            for (unsigned i = 0; i < words; ++i)
                store_be32(supplied_key.data() + 4 * i,
                    key_words_[(reg_auth_key - reg_key) / 4 + i]);
            ok = EVP_EncryptInit_ex(aes_ctx_.get(),
                     aes256 ? EVP_aes_256_ecb() : EVP_aes_128_ecb(), nullptr,
                     supplied_key.data(), nullptr) == 1;
            OPENSSL_cleanse(supplied_key.data(), supplied_key.size());
        }
        return ok && EVP_CIPHER_CTX_set_padding(aes_ctx_.get(), 0) == 1;
    }

    bool aes_encrypt(const unsigned char* input, unsigned char* output) {
        int length = 0;
        return EVP_EncryptUpdate(aes_ctx_.get(), output, &length, input, aes_block_size) == 1 &&
            length == int(aes_block_size);
    }

    bool cmac_chain(const unsigned char* block) {
        std::array<unsigned char, 16> input{};
        for (unsigned i = 0; i < aes_block_size; ++i) input[i] = cmac_state_[i] ^ block[i];
        return aes_encrypt(input.data(), cmac_state_.data());
    }

    // The newest block stays pending, because a final request applies a subkey to it.
    bool cmac_update(const unsigned char* data, std::uint32_t length) {
        while (length) {
            if (cmac_pending_length_ == aes_block_size) {
                if (!cmac_chain(cmac_pending_.data())) return false;
                cmac_pending_length_ = 0;
            }
            const std::uint32_t count =
                std::min<std::uint32_t>(aes_block_size - cmac_pending_length_, length);
            std::memcpy(cmac_pending_.data() + cmac_pending_length_, data, count);
            cmac_pending_length_ += count;
            data += count;
            length -= count;
        }
        return true;
    }

    static void cmac_double(std::array<unsigned char, 16>& value) {
        const bool carry = value[0] & 0x80;
        for (unsigned i = 0; i < 15; ++i)
            value[i] = static_cast<unsigned char>((value[i] << 1) | (value[i + 1] >> 7));
        value[15] = static_cast<unsigned char>((value[15] << 1) ^ (carry ? 0x87 : 0));
    }

    // A nonfinal request chains every block and publishes the CBC-MAC state.
    // A final request applies the RFC 4493 subkey to its last block.
    bool cmac_finish() {
        bool ok = true;
        if (!op_final_) {
            if (cmac_pending_length_ == aes_block_size)
                ok = cmac_chain(cmac_pending_.data());
        } else if (cmac_pending_length_ == 0 && op_total_count_ != 0) {
            reject_operation("cmac-final-without-block");
            return false;
        } else {
            std::array<unsigned char, 16> subkey{};
            ok = aes_encrypt(subkey.data(), subkey.data());
            cmac_double(subkey);
            if (cmac_pending_length_ < aes_block_size) {
                cmac_double(subkey);
                cmac_pending_[cmac_pending_length_] = 0x80;
                std::memset(cmac_pending_.data() + cmac_pending_length_ + 1, 0,
                    aes_block_size - cmac_pending_length_ - 1);
            }
            for (unsigned i = 0; i < aes_block_size; ++i) cmac_pending_[i] ^= subkey[i];
            ok = ok && cmac_chain(cmac_pending_.data());
            OPENSSL_cleanse(subkey.data(), subkey.size());
        }
        OPENSSL_cleanse(cmac_pending_.data(), cmac_pending_.size());
        (void)EVP_CIPHER_CTX_reset(aes_ctx_.get());
        if (!ok) {
            reject_operation("aes-unavailable");
            return false;
        }
        for (unsigned i = 0; i < 4; ++i) iv_words_[i] = load_be32(cmac_state_.data() + 4 * i);
        OPENSSL_cleanse(cmac_state_.data(), cmac_state_.size());
        return true;
    }

    void finish_operation() {
        if (op_alg_ == HashAlg::aes_cmac) {
            if (!cmac_finish()) return;
        } else if (op_final_) {
            std::array<std::uint8_t, SHA512_DIGEST_LENGTH> digest{};
            unsigned words = 0;
            if (op_alg_ == HashAlg::sha512) {
                (void)SHA512_Final(digest.data(), &sha512_ctx_);
                words = 16;
            } else if (op_alg_ == HashAlg::sha384) {
                (void)SHA384_Final(digest.data(), &sha512_ctx_);
                words = 12;
            } else if (op_alg_ == HashAlg::sha256) {
                (void)SHA256_Final(digest.data(), &sha256_ctx_);
                words = 8;
            } else {
                (void)SHA1_Final(digest.data(), &sha1_ctx_);
                words = 5;
            }
            for (unsigned i = 0; i < words; ++i)
                iv_words_[i] = load_be32(digest.data() + 4 * i);
        } else if (sha512_family(op_alg_)) {
            if (sha512_ctx_.num != 0) {
                reject_operation("partial-final-block");
                return;
            }
            for (unsigned i = 0; i < 8; ++i) {
                iv_words_[2 * i] = std::uint32_t(sha512_ctx_.h[i] >> 32);
                iv_words_[2 * i + 1] = std::uint32_t(sha512_ctx_.h[i]);
            }
        } else if (op_alg_ == HashAlg::sha256) {
            if (sha256_ctx_.num != 0) {
                reject_operation("partial-final-block");
                return;
            }
            for (unsigned i = 0; i < 8; ++i) iv_words_[i] = sha256_ctx_.h[i];
        } else {
            if (sha1_ctx_.num != 0) {
                reject_operation("partial-final-block");
                return;
            }
            iv_words_[0] = sha1_ctx_.h0;
            iv_words_[1] = sha1_ctx_.h1;
            iv_words_[2] = sha1_ctx_.h2;
            iv_words_[3] = sha1_ctx_.h3;
            iv_words_[4] = sha1_ctx_.h4;
        }
        count_words_[0] = std::uint32_t(op_total_count_ >> 32);
        count_words_[1] = std::uint32_t(op_total_count_);
        count_words_[2] = 0;
        count_words_[3] = 0;
        status_latched_ |= status_done;
        busy_ = false;
        if (dump_requested_) {
            build_dump();
            dump_pending_ = true;
            dump_offset_ = 0;
        } else {
            dump_pending_ = false;
        }
        schedule_ready();
    }

    void build_dump() {
        dump_.fill(0);
        for (unsigned i = 0; i < 16; ++i) store_be32(dump_.data() + 4 * i, iv_words_[i]);
        for (unsigned i = 0; i < 4; ++i) store_be32(dump_.data() + 64 + 4 * i, count_words_[i]);
        // The encryption counter IV stays zero because no cipher ran.
        store_be32(dump_.data() + 96, status_latched_ | status_done);
        store_be32(dump_.data() + 100, 0);
    }

    void stream_error(const char* reason, tlm::tlm_generic_payload& tx) {
        std::cerr << "QCE_V6_STREAM_ERROR model=" << name()
                  << " reason=" << reason << " pipe=" << tx.get_address() << '\n';
        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    }

    void stream_transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        auto* extension = tx.get_extension<BamStreamExtension>();
        if (!extension) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        extension->transferred = 0;
        if (tx.get_byte_enable_ptr() ||
            (!tx.get_data_ptr() && tx.get_data_length() != 0) ||
            (!tx.is_read() && !tx.is_write())) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const std::uint64_t pipe = tx.get_address();
        if (pipe > 0xffffffffull) {
            stream_error("pipe-range", tx);
            return;
        }
        if (!pipe_policy_known()) {
            // No CONFIG write has selected a pipe set yet. Stall the transport.
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        const unsigned input_pipe = current_pipe_set() * 2;
        const unsigned output_pipe = input_pipe + 1;
        const auto length = tx.get_data_length();
        if (pipe == input_pipe && tx.is_write()) {
            if (!busy_ || length == 0) {
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
            const std::uint32_t remaining = op_seg_size_ - op_received_;
            if (length > remaining) {
                reject_operation("stream-overrun");
                stream_error("overrun", tx);
                return;
            }
            const auto* data = tx.get_data_ptr();
            if (op_alg_ == HashAlg::aes_cmac) {
                if (!cmac_update(data, length)) {
                    reject_operation("aes-unavailable");
                    stream_error("aes", tx);
                    return;
                }
            } else if (op_alg_ == HashAlg::sha384)
                (void)SHA384_Update(&sha512_ctx_, data, length);
            else if (op_alg_ == HashAlg::sha512)
                (void)SHA512_Update(&sha512_ctx_, data, length);
            else if (op_alg_ == HashAlg::sha256)
                (void)SHA256_Update(&sha256_ctx_, data, length);
            else
                (void)SHA1_Update(&sha1_ctx_, data, length);
            op_received_ += length;
            extension->transferred = length;
            if (op_received_ == op_seg_size_) finish_operation();
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (pipe == output_pipe && tx.is_read()) {
            if (!dump_pending_ || dump_offset_ >= dump_.size()) {
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
            const std::uint32_t available = result_dump_size - dump_offset_;
            const std::uint32_t count = length < available ? length : available;
            auto* data = tx.get_data_ptr();
            for (std::uint32_t i = 0; i < count; ++i)
                data[i] = dump_[dump_offset_ + i];
            dump_offset_ += count;
            extension->transferred = count;
            if (dump_offset_ == dump_.size()) {
                dump_pending_ = false;
                schedule_ready();
            }
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (pipe == input_pipe || pipe == output_pipe) {
            stream_error("direction", tx);
            return;
        }
        stream_error("wrong-pipe", tx);
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 ||
            tx.get_streaming_width() < 4 || tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        auto* bytes = tx.get_data_ptr();
        std::uint32_t value = 0;
        bool handled = false;
        if (tx.is_read()) {
            handled = read(tx.get_address(), value);
            if (handled) {
                for (unsigned n = 0; n < 4; ++n) bytes[n] = std::uint8_t(value >> (8 * n));
            }
        } else {
            for (unsigned n = 0; n < 4; ++n) value |= std::uint32_t(bytes[n]) << (8 * n);
            handled = write(tx.get_address(), value);
        }
        if (!handled) {
            std::cerr << "QCE_V6_UNSUPPORTED model=" << name()
                      << " offset=0x" << std::hex << tx.get_address()
                      << " value=0x" << value << std::dec
                      << " operation=" << (tx.is_write() ? "write" : "read") << '\n';
        }
        tx.set_response_status(handled ? tlm::TLM_OK_RESPONSE : tlm::TLM_COMMAND_ERROR_RESPONSE);
    }
};

} // namespace qcom_vp

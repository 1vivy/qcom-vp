// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/rng/host_entropy.h"
#include "sequencer_hash.h"
#include "sequencer_signature.h"
#include "transport_key.h"
#include "key_wrap.h"
#include <openssl/crypto.h>
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <ports/initiator-signal-socket.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// ROM 0x20ef00 acquires the engine. ROM 0x2116e0 requests random words.
class TmeSequencer : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<TmeSequencer> target_socket{"target_socket"};
    tlm_utils::simple_target_socket<TmeSequencer> policy_socket{"policy_socket"};
    tlm_utils::simple_target_socket<TmeSequencer> input_socket{"input_socket"};
    tlm_utils::simple_target_socket<TmeSequencer> output_socket{"output_socket"};
    tlm_utils::simple_target_socket_optional<TmeSequencer> load_socket{"load_socket"};
    tlm_utils::simple_target_socket_optional<TmeSequencer> ram_status_socket{"ram_status_socket"};
    tlm_utils::simple_target_socket_optional<TmeSequencer> report_control_socket{"report_control_socket"};
    tlm_utils::simple_target_socket_optional<TmeSequencer> grant_status_socket{"grant_status_socket"};
    tlm_utils::simple_initiator_socket_optional<TmeSequencer> dma_socket{"dma_socket"};
    InitiatorSignalSocket<bool> irq{"irq"};
    cci::cci_param<unsigned> policy_selection{
        "policy_selection", 2, "Required virtual sequencer ownership policy"};
    cci::cci_param<uint64_t> debug_access_value{
        "debug_access_value", UINT64_MAX, "Required virtual debug-access policy for command 0x25"};
    cci::cci_param<bool> control_state_substitute{
        "control_state_substitute", false, "Enable the explicit runtime control-state substitute"};
    cci::cci_param<bool> deny_debug_authorization{
        "deny_debug_authorization", false, "Deny all requests for debug privileges"};
    cci::cci_param<bool> deny_rom_patch_authorization{
        "deny_rom_patch_authorization", false, "Deny all requests for ROM patch authorization"};
    cci::cci_param<bool> virtual_key_substitute{
        "virtual_key_substitute", false, "Enable the explicit virtual managed-key profile"};
    cci::cci_param<std::string> virtual_root_hex{
        "virtual_root_hex", "", "Optional exact 48-byte virtual managed-key root in hexadecimal"};
    cci::cci_param<std::string> transport_key_sink{
        "transport_key_sink", "", "HWKM slave object that receives the transport-key broadcast"};

    SC_HAS_PROCESS(TmeSequencer);
    explicit TmeSequencer(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &TmeSequencer::transport);
        policy_socket.register_b_transport(this, &TmeSequencer::policy_transport);
        input_socket.register_b_transport(this, &TmeSequencer::input_transport);
        output_socket.register_b_transport(this, &TmeSequencer::output_transport);
        load_socket.register_b_transport(this, &TmeSequencer::load_transport);
        ram_status_socket.register_b_transport(this, &TmeSequencer::ram_status);
        report_control_socket.register_b_transport(this, &TmeSequencer::report_control);
        grant_status_socket.register_b_transport(this, &TmeSequencer::grant_status);
        SC_METHOD(update_irq);
        sensitive << irq_event_;
        dont_initialize();
    }

private:
    enum class Phase : std::uint32_t { Idle = 0, Acquired = 0x10, Complete = 0x40 };
    // Virtual policy failure, not a recovered vendor error enumeration.
    static constexpr std::uint32_t virtual_policy_denied = 0xffffffffU;
    Phase phase_ = Phase::Idle;
    std::uint32_t policy_ = 0;
    std::uint32_t owner_ = 0;
    std::array<std::uint32_t, 4> command_{};
    std::uint32_t operation_control_ = 0;
    std::uint32_t input_bytes_ = 0;
    std::uint32_t total_bytes_ = 0;
    std::uint32_t signature_control_ = 0;
    std::uint32_t curve_id_ = 0;
    std::uint32_t signature_options_ = 0;
    // Additional indexed metadata from donor 0x21fca2 / table 0x2326f0.
    // This retains registers only. It does not authorize or implement a key command.
    std::array<std::uint32_t, 20> indexed_metadata_{};
    std::array<unsigned char, 4096> input_{};
    std::array<unsigned char, 4096> output_{};
    std::array<bool, 4096> input_written_{};
    std::uint32_t output_words_ = 0;
    bool completed_ = false;
    bool operation_error_ = false;
    std::uint32_t command_error_ = 0;
    std::uint32_t irq_enable_ = 0;
    std::uint32_t control_mode_ = 0;
    bool boot_transition_completed_ = false;
    std::uint32_t report_control_ = 0;
    sc_core::sc_event irq_event_;
    // HMAC keys come from the virtual derivation profile. ECDH keys come from GENERATE_KEY.
    enum class KeyKind : std::uint8_t { hmac_sha384, ecdh_p384 };
    struct ManagedKey {
        std::array<unsigned char, 48> material{};
        std::uint32_t owner = 0;
        KeyKind kind = KeyKind::hmac_sha384;
        bool valid = false;
    };
    std::array<ManagedKey, 64> keys_{};
    std::array<unsigned char, 48> virtual_root_{};
    // Public TME_KID_TP is a retained 528-bit entry with the KWK_TRANSPORT profile.
    std::array<unsigned char, transport_key_bytes> transport_key_{};
    bool transport_key_retained_ = false;
    TransportKeySink* transport_key_sink_ = nullptr;

    template <class T> T* resolve_module(const std::string& path) {
        if (path.empty()) return nullptr;
        auto* object = dynamic_cast<T*>(sc_core::sc_find_object(path.c_str()));
        if (!object && get_parent_object()) {
            const auto relative = std::string(get_parent_object()->name()) + "." + path;
            object = dynamic_cast<T*>(sc_core::sc_find_object(relative.c_str()));
        }
        return object;
    }

    void end_of_elaboration() override {
        if (policy_selection.get_value() > 1) {
            throw std::invalid_argument("The sequencer requires ownership policy zero or one");
        }
        policy_ = policy_selection.get_value();
        const auto& configured_root = virtual_root_hex.get_value();
        if (!configured_root.empty() && !virtual_key_substitute.get_value()) {
            throw std::invalid_argument(
                "virtual_root_hex requires virtual_key_substitute=true");
        }
        if (virtual_key_substitute.get_value()) {
            if (configured_root.empty()) {
                // This public identity defines a virtual device, not a physical device secret.
                static constexpr unsigned char identity[] = "qcom-vp virtual TME managed-key profile v1";
                SequencerSha384 hash;
                hash.initialize(nullptr, 0);
                hash.update(identity, sizeof(identity) - 1);
                hash.finalize(virtual_root_.data());
            } else if (!decode_root_hex(configured_root, virtual_root_.data(),
                                        virtual_root_.size())) {
                throw std::invalid_argument(
                    "virtual_root_hex must contain exactly 96 hexadecimal characters");
            }
            std::cerr << "SEQUENCER_KEY_SUBSTITUTE root_slot=9 owner=2"
                      << " policy=managed-hmac root="
                      << (configured_root.empty() ? "public-virtual-identity" : "explicit-profile")
                      << '\n';
        }
        if (!transport_key_sink.get_value().empty()) {
            transport_key_sink_ =
                resolve_module<TransportKeySink>(transport_key_sink.get_value());
            if (!transport_key_sink_) {
                throw std::invalid_argument(
                    "The sequencer transport-key sink is not a HWKM slave");
            }
        }
    }

    void start_of_simulation() override { update_irq(); }

    void update_irq() {
        for (int port = 0; port < irq.size(); ++port)
            irq[port]->write(completed_ && (irq_enable_ & 1));
    }

    static std::uint32_t load_word(const unsigned char* data) {
        return std::uint32_t{data[0]} | (std::uint32_t{data[1]} << 8) |
            (std::uint32_t{data[2]} << 16) | (std::uint32_t{data[3]} << 24);
    }
    static int hex_nibble(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }
    static bool decode_root_hex(const std::string& encoded, unsigned char* output,
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

    static void store_word(unsigned char* data, std::uint32_t value) {
        for (unsigned i = 0; i != 4; ++i) {
            data[i] = static_cast<unsigned char>(value >> (8 * i));
        }
    }

    static bool validate(tlm::tlm_generic_payload& tx, bool word) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        if (!tx.get_data_ptr() || !tx.get_data_length() ||
            tx.get_streaming_width() < tx.get_data_length() ||
            (word && (tx.get_data_length() != 4 || (tx.get_address() & 3)))) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    void ram_status(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!validate(tx, true)) return;
        if (tx.get_address() != 0 || !tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        // The virtual payload arrays are initialized before the engine becomes available.
        // The complete ROM checks this zero-ready word at 0x20f744 and 0x20f760.
        store_word(tx.get_data_ptr(), 0);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void report_control(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!validate(tx, true)) return;
        if (tx.get_address() != 0 || !control_state_substitute.get_value()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            const auto value = load_word(tx.get_data_ptr());
            if (value > 1) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // Virtual retention only. This does not authorize debug or images.
            if (value != report_control_) {
                std::clog << name() << ": virtual report control retained " << value << '\n';
            }
            report_control_ = value;
        } else {
            store_word(tx.get_data_ptr(), report_control_);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void grant_status(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!validate(tx, true)) return;
        if (tx.get_address() != 0 && tx.get_address() != 4) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        // These summary views expose the explicit all-denied engine policy.
        // No publication or positive authorization path is implemented here.
        if (!tx.is_read() || !deny_debug_authorization.get_value() ||
            !deny_rom_patch_authorization.get_value()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        store_word(tx.get_data_ptr(), 0);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    std::uint32_t key_metadata(unsigned offset) const {
        return indexed_metadata_[(offset - 0x10) / 4];
    }

    bool key_error() {
        operation_error_ = true;
        command_error_ = virtual_policy_denied;
        std::memset(output_.data(), 0, 48);
        output_words_ = 0;
        return true;
    }

    bool derive_key() {
        if (!virtual_key_substitute.get_value() || command_[1] != 0 ||
            command_[2] != 3 || command_[3] != 0 || key_metadata(0x48) != 1) {
            std::cerr << "SEQUENCER_KEY_DERIVE_UNSUPPORTED binding="
                      << key_metadata(0x48) << " arguments="
                      << command_[1] << ',' << command_[2] << ','
                      << command_[3] << '\n';
            return false;
        }
        const auto context_length = key_metadata(0x40);
        const auto label_length = key_metadata(0x44);
        if ((label_length & 3) || context_length > input_.size() - 12 ||
            label_length > input_.size() - 12 - context_length) return false;
        const auto extent = 12 + context_length + label_length;
        for (std::size_t i = 0; i != extent; ++i) {
            if (!input_written_[i]) return false;
        }
        const auto source = key_metadata(0x10);
        const auto destination = key_metadata(0x14);
        const auto policy = key_metadata(0x30);
        // The two fused root sources are CUS and CRBK. The virtual profile
        // permits root derivation only for owner two.
        if ((source != 8 && source != 9) || key_metadata(0x18) != 0 ||
            operation_control_ != 2 || policy != 2 ||
            destination < 0x1c || destination >= keys_.size()) {
            const char* reason = (source != 8 && source != 9) ? "source"
                : key_metadata(0x18) != 0 ? "second"
                : operation_control_ != 2 ? "requester"
                : policy != 2 ? "policy"
                : "destination";
            std::cerr << "SEQUENCER_KEY_DERIVE_REJECTED reason=" << reason
                      << " source=" << source
                      << " destination=" << destination
                      << " policy=0x" << std::hex << policy << std::dec
                      << " second=" << key_metadata(0x18)
                      << " binding=" << key_metadata(0x48)
                      << " owner=" << operation_control_ << '\n';
            return key_error();
        }
        auto& key = keys_[destination];
        if (key.valid && (key.owner != operation_control_ ||
                          key.kind != KeyKind::hmac_sha384)) return key_error();

        // Virtual SP800-108 counter derivation binds the supplied policy and context.
        // Its binding layout is a substitute, not a recovered silicon serialization.
        SequencerHmacSha384 kdf;
        kdf.initialize(virtual_root_.data(), virtual_root_.size());
        static constexpr unsigned char counter[] = {0, 0, 0, 1};
        static constexpr unsigned char separator = 0;
        static constexpr unsigned char bits[] = {0, 0, 1, 0x80};
        std::array<unsigned char, 16> binding{};
        store_word(binding.data(), key_metadata(0x54));
        store_word(binding.data() + 4, operation_control_);
        store_word(binding.data() + 8, key_metadata(0x30));
        store_word(binding.data() + 12, source);
        kdf.update(counter, sizeof(counter));
        kdf.update(input_.data() + 12 + context_length, label_length);
        kdf.update(&separator, 1);
        kdf.update(binding.data(), binding.size());
        kdf.update(input_.data(), 12 + context_length);
        kdf.update(bits, sizeof(bits));
        kdf.finalize(key.material.data());
        key.owner = operation_control_;
        key.kind = KeyKind::hmac_sha384;
        key.valid = true;
        for (unsigned i = 0; i != 12; ++i) output_[i] = 0;
        output_words_ = 0;
        std::cerr << "SEQUENCER_KEY_DERIVED slot=" << destination
                  << " owner=" << key.owner << " bytes=" << key.material.size()
                  << " substitute=virtual-policy-binding\n";
        return true;
    }

    bool keyed_hash(sc_core::sc_time& delay) {
        if (!virtual_key_substitute.get_value() || command_[1] != 2 ||
            command_[2] != 1 || command_[3] != 3 || total_bytes_ != input_bytes_)
            return false;
        const auto slot = key_metadata(0x10);
        if (slot >= keys_.size() || !keys_[slot].valid ||
            keys_[slot].owner != operation_control_ ||
            keys_[slot].kind != KeyKind::hmac_sha384) return key_error();
        for (unsigned i = 0; i != 4; ++i) {
            if (!input_written_[i]) return false;
        }
        const auto count = load_word(input_.data());
        if (count > 8 || !dma_socket.size()) return false;
        std::uint64_t sum = 0;
        for (unsigned i = 0; i != count; ++i) {
            const auto offset = 4 + 12 * i;
            for (unsigned j = 0; j != 12; ++j) {
                if (!input_written_[offset + j]) return false;
            }
            const auto address = load_word(input_.data() + offset);
            const auto length = load_word(input_.data() + offset + 4);
            if (load_word(input_.data() + offset + 8) != 0 ||
                std::uint64_t{address} + length > (std::uint64_t{1} << 32)) return false;
            sum += length;
        }
        if (sum != input_bytes_) return false;
        SequencerHmacSha384 hash;
        hash.initialize(keys_[slot].material.data(), keys_[slot].material.size());
        std::array<unsigned char, 256> buffer{};
        for (unsigned i = 0; i != count; ++i) {
            const auto offset = 4 + 12 * i;
            std::uint64_t address = load_word(input_.data() + offset);
            auto remaining = load_word(input_.data() + offset + 4);
            while (remaining) {
                const unsigned length = remaining < buffer.size() ? remaining : buffer.size();
                if (!read_dma(address, buffer.data(), length, delay)) return false;
                hash.update(buffer.data(), length);
                address += length;
                remaining -= length;
            }
        }
        hash.finalize(output_.data());
        output_words_ = 12;
        return true;
    }

    bool clear_key() {
        if (!virtual_key_substitute.get_value() || command_[1] != 0 ||
            command_[2] != 0 || command_[3] != 0) return false;
        const auto slot = key_metadata(0x10);
        if (slot >= keys_.size() || !keys_[slot].valid ||
            keys_[slot].owner != operation_control_) return key_error();
        auto& key = keys_[slot];
        OPENSSL_cleanse(key.material.data(), key.material.size());
        key.owner = 0;
        key.valid = false;
        for (unsigned i = 0; i != 12; ++i) output_[i] = 0;
        output_words_ = 0;
        return true;
    }

    // The OEM QBEC descriptor at 0x443760 supplies this exact version-four policy.
    // The public layout decodes it as ECC P-384, ECDH, hardware key, and TME firmware use.
    static constexpr std::uint32_t qbec_ecdh_policy_low = 0x04204829;
    static constexpr std::uint32_t qbec_ecdh_policy_high = 0x00024040;

    // Donor 0x21e58e writes the slot at 0x14, the requester at 0x2c, and the credential at 0x30.
    // The policy words occupy input bytes 0..7. Completion 0x21e6ac copies no output words.
    bool generate_key() {
        if (!virtual_key_substitute.get_value() || command_[1] != 0 ||
            command_[2] != 0 || command_[3] != 0 ||
            !std::all_of(input_written_.begin(), input_written_.begin() + 8,
                         [](bool written) { return written; })) return false;
        const auto low = load_word(input_.data());
        const auto high = load_word(input_.data() + 4);
        const auto slot = key_metadata(0x14);
        const auto credential = key_metadata(0x30);
        if (low != qbec_ecdh_policy_low || high != qbec_ecdh_policy_high) {
            std::cerr << "SEQUENCER_GENERATE_KEY_UNSUPPORTED low=0x" << std::hex << low
                      << " high=0x" << high << std::dec << '\n';
            return false;
        }
        // Credential slots zero, one, and two are the public throttling choices.
        if (slot >= keys_.size() || keys_[slot].valid || credential > 2) {
            std::cerr << "SEQUENCER_GENERATE_KEY_REJECTED slot=" << slot
                      << " credential=" << credential
                      << " occupied=" << (slot < keys_.size() && keys_[slot].valid) << '\n';
            return key_error();
        }
        // Rejection sampling gives a uniform private scalar in [1, n - 1].
        static constexpr std::array<unsigned char, 48> p384_order{
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xc7, 0x63, 0x4d, 0x81, 0xf4, 0x37, 0x2d, 0xdf, 0x58, 0x1a, 0x0d, 0xb2,
            0x48, 0xb0, 0xa7, 0x7a, 0xec, 0xec, 0x19, 0x6a, 0xcc, 0xc5, 0x29, 0x73};
        auto& key = keys_[slot];
        do {
            fill_host_entropy(key.material.data(), key.material.size());
        } while (std::all_of(key.material.begin(), key.material.end(),
                             [](unsigned char byte) { return byte == 0; }) ||
                 !std::lexicographical_compare(key.material.begin(), key.material.end(),
                                               p384_order.begin(), p384_order.end()));
        key.owner = operation_control_;
        key.kind = KeyKind::ecdh_p384;
        key.valid = true;
        output_words_ = 0;
        std::cerr << "SEQUENCER_KEY_GENERATED slot=" << slot << " owner=" << key.owner
                  << " credential=" << credential << " type=ecdh-p384 bytes="
                  << key.material.size() << '\n';
        return true;
    }

    // Donor submit 0x21de92 sends GET_ECC_PK operation 0x17 for curve selector 4 as
    // command 0xa, or as command 0x28 when its xCoordOnly byte is set. It writes the key
    // slot at 0x10, the requester at 0x2c, the curve at 0x38, and zero input bytes at 0x58.
    // Donor 0x2a0 maps the QBEC policy to curve 4. Donor 0x2b0 maps curve 4 to 384 bits.
    static constexpr std::uint32_t p384_curve = 4;

    bool public_key(bool x_only) {
        if (!virtual_key_substitute.get_value() || command_[1] != 0 ||
            command_[2] != 0 || command_[3] != 0) return false;
        if (curve_id_ != p384_curve || input_bytes_ != 0) {
            std::cerr << "SEQUENCER_PUBLIC_KEY_UNSUPPORTED curve=0x" << std::hex
                      << curve_id_ << std::dec << " input_bytes=" << input_bytes_ << '\n';
            return false;
        }
        const auto slot = key_metadata(0x10);
        if (slot >= keys_.size() || !keys_[slot].valid ||
            keys_[slot].owner != operation_control_ ||
            keys_[slot].kind != KeyKind::ecdh_p384) return key_error();
        // Completion 0x21df70 copies 48 bytes for X only, otherwise 96 bytes.
        std::array<unsigned char, 96> coordinates{};
        if (!derive_sequencer_p384_public(keys_[slot].material.data(), coordinates.data())) {
            return false;
        }
        const std::size_t bytes = x_only ? 48 : 96;
        std::memcpy(output_.data(), coordinates.data(), bytes);
        output_words_ = static_cast<std::uint32_t>(bytes / 4);
        std::cerr << "SEQUENCER_PUBLIC_KEY slot=" << slot << " owner=" << operation_control_
                  << " type=ecdh-p384 bytes=" << bytes << '\n';
        return true;
    }

    bool hash(sc_core::sc_time& delay) {
        const auto phase = command_[3];
        const bool one_shot = command_[2] == 0;
        const bool final = one_shot || phase == 5;
        if (command_[1] != 2 || command_[2] > 1 ||
            (phase != 3 && phase != 4 && phase != 5) || operation_control_ != 1 ||
            (one_shot && phase != 3) || total_bytes_ < input_bytes_ ||
            (phase == 3 && total_bytes_ != input_bytes_) ||
            (!final && input_bytes_ % 128 != 0) ||
            (phase != 3 && (total_bytes_ - input_bytes_) % 128 != 0)) return false;
        const std::size_t descriptors = phase == 3 ? 4 : 68;
        for (std::size_t i = 0; i != descriptors; ++i) {
            if (!input_written_[i]) return false;
        }
        const auto count = load_word(input_.data());
        if (count > 8 || (input_bytes_ != 0 && count == 0) ||
            !dma_socket.size()) return false;
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i != count; ++i) {
            const auto offset = descriptors + i * 12;
            for (std::size_t j = 0; j != 12; ++j) {
                if (!input_written_[offset + j]) return false;
            }
            const auto address = load_word(input_.data() + offset);
            const auto length = load_word(input_.data() + offset + 4);
            if (load_word(input_.data() + offset + 8) != 0 ||
                std::uint64_t{address} + length > (std::uint64_t{1} << 32)) return false;
            sum += length;
        }
        if (sum != input_bytes_) return false;
        SequencerSha384 hash;
        hash.initialize(phase == 3 ? nullptr : input_.data() + 4,
                        total_bytes_ - input_bytes_);
        std::array<unsigned char, 256> buffer{};
        for (std::size_t i = 0; i != count; ++i) {
            const auto offset = descriptors + i * 12;
            std::uint64_t address = load_word(input_.data() + offset);
            auto remaining = load_word(input_.data() + offset + 4);
            while (remaining) {
                const auto length = remaining < buffer.size() ? remaining : buffer.size();
                if (!read_dma(address, buffer.data(), length, delay)) return false;
                hash.update(buffer.data(), length);
                address += length;
                remaining -= length;
            }
        }
        if (final) hash.finalize(output_.data());
        else hash.state(output_.data());
        output_words_ = final ? 12 : 16;
        return true;
    }

    bool read_dma(std::uint64_t address, unsigned char* bytes, unsigned length,
                  sc_core::sc_time& delay) {
        QemuMemTxAttrsExtension attributes;
        attributes.attrs.secure = true;
        attributes.attrs.unspecified = false;
        tlm::tlm_generic_payload read;
        read.set_extension(&attributes);
        read.set_command(tlm::TLM_READ_COMMAND);
        read.set_address(address);
        read.set_data_ptr(bytes);
        read.set_data_length(length);
        read.set_streaming_width(length);
        read.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        dma_socket->b_transport(read, delay);
        read.clear_extension(&attributes);
        if (read.is_response_error()) {
            std::cerr << "SEQUENCER_DMA_FAILURE address=0x" << std::hex << address
                      << " length=0x" << length << std::dec
                      << " response=" << read.get_response_string() << '\n';
            return false;
        }
        return true;
    }

    bool signature(sc_core::sc_time& delay) {
        if (command_[3] != 0 || signature_control_ != 1 ||
            curve_id_ != 4 || signature_options_ != 0 || input_bytes_ != 48) return false;
        const bool inline_digest = command_[1] == 2 && command_[2] == 1 &&
                                   operation_control_ == 1;
        const bool dma_digest = command_[1] == 1 && command_[2] == 0 &&
                                operation_control_ == 2;
        if (!inline_digest && !dma_digest) return false;
        const std::size_t frame_size = inline_digest ? 244 : 208;
        for (std::size_t i = 0; i != frame_size; ++i) {
            if (!input_written_[i]) return false;
        }
        if (load_word(input_.data()) != (dma_digest ? 1U : 0U)) return false;
        const auto* digest = input_.data() + 196;
        std::array<unsigned char, 48> transferred_digest;
        if (dma_digest) {
            const auto address = load_word(digest);
            if (!dma_socket.size() || load_word(digest + 4) != transferred_digest.size() ||
                load_word(digest + 8) != 0 ||
                std::uint64_t{address} + transferred_digest.size() > (std::uint64_t{1} << 32))
                return false;
            if (!read_dma(address, transferred_digest.data(), transferred_digest.size(), delay))
                return false;
            digest = transferred_digest.data();
        }
        operation_error_ = !verify_sequencer_p384(input_.data() + 4, input_.data() + 100, digest);
        std::cerr << "SEQUENCER_SIGNATURE_VERIFY digest=" << (dma_digest ? "dma" : "inline")
                  << " owner=" << operation_control_
                  << " result=" << (operation_error_ ? "fail" : "pass") << '\n';
        output_words_ = 0;
        return true;
    }

    bool deny_authorization_request(std::uint32_t feature) {
        if (input_bytes_ < 4 || input_bytes_ > input_.size() || (input_bytes_ & 3))
            return false;
        for (std::size_t i = 0; i != input_bytes_; ++i) {
            if (!input_written_[i]) return false;
        }
        const auto header = load_word(input_.data());
        const auto length = header >> 16;
        const auto extent = (length + 7) & ~std::uint32_t{3};
        if ((header & 0xffff) != feature || extent != input_bytes_) return false;
        for (std::size_t i = length + 4; i != extent; ++i) {
            if (input_[i]) return false;
        }
        operation_error_ = true;
        command_error_ = virtual_policy_denied;
        output_words_ = 0;
        std::cerr << "SEQUENCER_AUTHORIZATION_DENIED command=0x" << std::hex << command_[0]
                  << std::dec << " policy=deny-all\n";
        return true;
    }

    bool set_debug_access(sc_core::sc_time& delay) {
        const auto policy = debug_access_value.get_value();
        if ((policy & ~uint64_t{0xc000007f}) || !dma_socket.size()) return false;
        for (unsigned i = 0; i < 4; ++i) {
            if (!input_written_[i]) return false;
        }
        const auto count = load_word(input_.data());
        if (!count || count > input_.size() / 4 - 1) return false;
        for (unsigned i = 4; i < (count + 1) * 4; ++i) {
            if (!input_written_[i]) return false;
        }
        for (unsigned i = 0; i < count; ++i) {
            if ((load_word(input_.data() + (i + 1) * 4) & 0xfff) != 0x304) return false;
        }

        // OEM 0x423f98 supplies DBGAR addresses, not values.
        // The virtual profile supplies the policy instead of unverified physical fuse rules.
        std::array<unsigned char, 4> bytes{};
        QemuMemTxAttrsExtension attributes;
        attributes.attrs.secure = true;
        attributes.attrs.unspecified = false;
        tlm::tlm_generic_payload access;
        access.set_extension(&attributes);
        access.set_data_ptr(bytes.data());
        access.set_data_length(bytes.size());
        access.set_streaming_width(bytes.size());
        store_word(output_.data(), 0);
        for (unsigned i = 0; i < count; ++i) {
            const auto address = load_word(input_.data() + (i + 1) * 4);
            access.set_address(address);
            store_word(bytes.data(), static_cast<uint32_t>(policy));
            for (const auto operation : {tlm::TLM_WRITE_COMMAND, tlm::TLM_READ_COMMAND}) {
                access.set_command(operation);
                access.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
                dma_socket->b_transport(access, delay);
                if (access.is_response_error()) {
                    access.clear_extension(&attributes);
                    std::cerr << "SEQUENCER_DMA_FAILURE address=0x" << std::hex << address
                              << std::dec << " response=" << access.get_response_string() << '\n';
                    return false;
                }
            }
            if (load_word(bytes.data()) != policy) {
                operation_error_ = true;
                store_word(output_.data(), address);
                break;
            }
        }
        access.clear_extension(&attributes);
        output_words_ = 1;
        return true;
    }

    bool clear_memory(sc_core::sc_time& delay) {
        // ROM 0x211a32 and donor 0x2205e8 submit {1, address, length, address}.
        // The constructor's short path calls byte fill with value zero.
        for (unsigned i = 0; i != 16; ++i) {
            if (!input_written_[i]) return false;
        }
        const auto start = load_word(input_.data() + 4);
        auto remaining = load_word(input_.data() + 8);
        if (load_word(input_.data()) != 1 || !remaining ||
            load_word(input_.data() + 12) != start || !dma_socket.size() ||
            std::uint64_t{start} + remaining > (std::uint64_t{1} << 32)) return false;

        std::array<unsigned char, 256> zeros{};
        QemuMemTxAttrsExtension attributes;
        attributes.attrs.secure = true;
        attributes.attrs.unspecified = false;
        tlm::tlm_generic_payload access;
        access.set_extension(&attributes);
        access.set_command(tlm::TLM_WRITE_COMMAND);
        access.set_data_ptr(zeros.data());
        std::uint64_t address = start;
        while (remaining) {
            const unsigned length = remaining < zeros.size() ? remaining : zeros.size();
            access.set_address(address);
            access.set_data_length(length);
            access.set_streaming_width(length);
            access.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            dma_socket->b_transport(access, delay);
            if (access.get_response_status() != tlm::TLM_OK_RESPONSE) {
                access.clear_extension(&attributes);
                std::cerr << "SEQUENCER_DMA_FAILURE address=0x" << std::hex << address
                          << " length=0x" << length << std::dec
                          << " response=" << access.get_response_string() << '\n';
                return false;
            }
            address += length;
            remaining -= length;
        }
        access.clear_extension(&attributes);
        output_words_ = 0;
        return true;
    }

    bool retain_transport_key() {
        if (transport_key_retained_) return true;
        // The silicon derivation inputs and KDF are not recovered. This public
        // identity stands for the fused chip-unique seed.
        static constexpr unsigned char identity[] =
            "qcom-vp virtual TME chip-unique seed substitute v1";
        static constexpr unsigned char label[] = "qcom-vp transport key broadcast v1";
        std::array<unsigned char, 48> seed{};
        SequencerSha384 digest;
        digest.initialize(nullptr, 0);
        digest.update(identity, sizeof(identity) - 1);
        digest.finalize(seed.data());
        // This expansion is a virtual counter mode. It is not a recovered silicon KDF.
        for (unsigned block = 0; block != 2; ++block) {
            SequencerHmacSha384 prf;
            prf.initialize(seed.data(), seed.size());
            prf.update(label, sizeof(label) - 1);
            const unsigned char counter[4] = {
                0, 0, 0, static_cast<unsigned char>(block + 1)};
            prf.update(counter, sizeof(counter));
            std::array<unsigned char, 48> chunk{};
            prf.finalize(chunk.data());
            const auto offset = block * chunk.size();
            const auto count = std::min(chunk.size(), transport_key_bytes - offset);
            std::memcpy(transport_key_.data() + offset, chunk.data(), count);
            OPENSSL_cleanse(chunk.data(), chunk.size());
        }
        OPENSSL_cleanse(seed.data(), seed.size());
        transport_key_retained_ = true;
        std::cerr << "SEQUENCER_TPKEY_RETAINED bits=" << transport_key_bytes * 8
                  << " profile=kwk-transport substitute=virtual-chip-unique-seed\n";
        return true;
    }

    bool broadcast_transport_key() {
        // Both original bodies submit zero at 04, 08, and 0c.
        if (!virtual_key_substitute.get_value() || command_[1] || command_[2] ||
            command_[3]) return false;
        if (!transport_key_sink_) {
            std::cerr << "SEQUENCER_TPKEY_BROADCAST_UNAVAILABLE ee="
                      << operation_control_
                      << " reason=no-hwkm-slave\n";
            return false;
        }
        if (!retain_transport_key()) return false;
        const auto delivery = transport_key_sink_->transport_key_receive(
            transport_key_.data(), transport_key_.size());
        if (delivery == TransportKeySink::Delivery::rejected) {
            std::cerr << "SEQUENCER_TPKEY_BROADCAST_REJECTED ee="
                      << operation_control_ << " reason=slave-state\n";
            return false;
        }
        output_words_ = 0;
        std::cerr << "SEQUENCER_TPKEY_BROADCAST ee=" << operation_control_
                  << " bits=" << transport_key_bytes * 8
                  << " delivery="
                  << (delivery == TransportKeySink::Delivery::installed ?
                      "installed" : "pending")
                  << '\n';
        return true;
    }

    bool export_wrapped_key() {
        // Donor 0x21ed64 writes zero at 04, 08, and 0c for command b.
        if (!virtual_key_substitute.get_value() || command_[1] || command_[2] ||
            command_[3]) return false;
        const auto source = key_metadata(0x10);
        const auto wrapping = key_metadata(0x1c);
        // The only recovered wrapping-key slot is the transport key at 0x15.
        if (wrapping != 0x15) return key_error();
        if (!transport_key_retained_) {
            // The broadcast is the only producer of the wrapping key.
            std::cerr << "SEQUENCER_KEY_WRAP_UNAVAILABLE reason=no-transport-key\n";
            return key_error();
        }
        if (source >= keys_.size() || !keys_[source].valid) return key_error();
        // Policy word 0x00024040 permits no wrapping, so only derived keys can leave.
        if (keys_[source].owner != operation_control_ ||
            keys_[source].kind != KeyKind::hmac_sha384) return key_error();
        std::array<unsigned char, 8> nonce{};
        fill_host_entropy(nonce.data(), nonce.size());
        // Clear the status words, the blob, and the longer copy path padding.
        std::memset(output_.data(), 0, 12 + wrapped_key_max_bytes);
        if (!wrap_key_material(transport_key_.data(), transport_key_.size(),
                keys_[source].material.data(), keys_[source].material.size(),
                nonce.data(), output_.data() + 12)) {
            OPENSSL_cleanse(nonce.data(), nonce.size());
            return false;
        }
        OPENSSL_cleanse(nonce.data(), nonce.size());
        output_words_ = 3 + wrapped_key_core_bytes / 4;
        std::cerr << "SEQUENCER_KEY_WRAPPED source=" << source
                  << " wrapping=0x" << std::hex << wrapping << std::dec
                  << " owner=" << operation_control_
                  << " bytes=" << wrapped_key_core_bytes
                  << " substitute=virtual-authenticated-format\n";
        return true;
    }

    bool execute(sc_core::sc_time& delay) {
        if (command_[0] == 1 && hash(delay)) {
            // Only the supported SHA-384 ABI can publish a hash completion.
        } else if (command_[0] == 2 && derive_key()) {
            // The virtual key remains internal until a permitted keyed operation uses it.
        } else if (command_[0] == 4 && keyed_hash(delay)) {
            // The guest receives the HMAC over its actual DMA input.
        } else if (command_[0] == 0xd && clear_key()) {
            // Cleanup erases the material before the engine publishes completion.
        } else if (command_[0] == 3 && generate_key()) {
            // The private scalar remains internal. No output words follow completion.
        } else if ((command_[0] == 0x0a || command_[0] == 0x28) &&
                   public_key(command_[0] == 0x28)) {
            // The output holds only public coordinates. The private scalar stays internal.
        } else if (command_[0] == 8 && signature(delay)) {
            // ROM 0x20f216 reads the signature result from status bit zero.
        } else if (command_[0] == 0x11 && clear_memory(delay)) {
            // Completion only notifies. It does not return an output snapshot.
        } else if (command_[0] == 0x29 && deny_debug_authorization.get_value() &&
                   deny_authorization_request(0x186)) {
            // Deny debug privileges without evaluating the token.
        } else if (command_[0] == 0x18 && deny_rom_patch_authorization.get_value() &&
                   deny_authorization_request(0x184)) {
            // Deny ROM patch authorization without evaluating or applying a patch.
        } else if (command_[0] == 0x25 && set_debug_access(delay)) {
            // The payload contains a count and addresses. Other command words can retain old values.
        } else if (command_[0] == 0x1d && control_state_substitute.get_value() &&
                   command_[1] == 4 && (command_[2] == 1 ||
                       (command_[2] == 2 && boot_transition_completed_)) && command_[3] == 0) {
            // The private profile retains control state without physical voltage effects.
            const auto previous = control_mode_;
            control_mode_ = command_[2];
            output_words_ = 0;
            std::cerr << "SEQUENCER_CONTROL_SUBSTITUTE selector=4 previous=" << previous
                      << " mode=" << control_mode_ << '\n';
        } else if (command_[0] == 0x21 && control_state_substitute.get_value() &&
                   control_mode_ == 1 && command_[1] == 0 && command_[2] == 0 && command_[3] == 0) {
            boot_transition_completed_ = true;
            output_words_ = 0;
            std::cerr << "SEQUENCER_BOOT_TRANSITION_SUBSTITUTE completed=1\n";
        } else if (command_[0] == 0x13 && command_[1] <= output_.size() / 4 &&
                   command_[2] == 0 && command_[3] == 0) {
            fill_host_entropy(output_.data(), command_[1] * 4);
            output_words_ = command_[1];
        } else if (command_[0] == 0x0b && export_wrapped_key()) {
            // The completion copies 92 or 100 bytes after three status words.
        } else if (command_[0] == 0x1c && broadcast_transport_key()) {
            // The GPCE receive window completes the handshake and sets 01df1020 bit8.
        } else {
            std::cerr << "UNMODELED_SEQUENCER_COMMAND command=0x" << std::hex << command_[0]
                      << " argument0=0x" << command_[1] << " argument1=0x" << command_[2]
                      << " argument2=0x" << command_[3] << std::dec
                      << " owner=" << owner_ << " control=" << operation_control_
                      << " input_bytes=" << input_bytes_ << " total_bytes=" << total_bytes_
                      << " written=" << std::count(input_written_.begin(), input_written_.end(), true)
                      << '\n';
            return false;
        }
        completed_ = true;
        phase_ = Phase::Complete;
        irq_event_.notify(sc_core::SC_ZERO_TIME);
        return true;
    }

    bool write_control(std::uint32_t value, sc_core::sc_time& delay) {
        if (value & ~0x373U) return false;
        if (value & 0x100) {
            if (value & 0x200) return false;
            if (phase_ == Phase::Idle) {
                owner_ = policy_ == 1 ? 2 : 1;
                phase_ = Phase::Acquired;
                input_written_.fill(false);
                output_words_ = 0;
                operation_error_ = false;
                command_error_ = 0;
            }
            return true;
        }
        if (value & 0x200) {
            return phase_ == Phase::Acquired && execute(delay);
        }
        return true;
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if (!validate(tx, true)) return;
        const auto offset = tx.get_address();
        auto value = tx.is_write() ? load_word(tx.get_data_ptr()) : 0U;
        if (offset < 0x10) {
            if (tx.is_write()) {
                if (phase_ != Phase::Acquired) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                command_[offset / 4] = value;
            } else {
                value = command_[offset / 4];
            }
        } else if (offset == 0x10 || offset == 0x14 || offset == 0x18 ||
                   offset == 0x1c || offset == 0x2c || offset == 0x30 || offset == 0x34 ||
                   offset == 0x38 || offset == 0x40 || offset == 0x44 ||
                   offset == 0x48 || offset == 0x4c || offset == 0x50 ||
                   offset == 0x54 || offset == 0x58 || offset == 0x5c) {
            // Indexed key fields: a=source, 10=destination, 11=mix,
            // f=requesting EE, 12=credential. Command 0xd sets a and f.
            // Index 19 selects the wrapping-key slot at 1c for command b.
            // Command 3 and import 0xe supply policy words in input RAM.
            auto& word = offset == 0x2c ? operation_control_ :
                offset == 0x34 ? signature_control_ : offset == 0x38 ? curve_id_ :
                offset == 0x50 ? signature_options_ : offset == 0x58 ? input_bytes_ :
                offset == 0x5c ? total_bytes_ : indexed_metadata_[(offset - 0x10) / 4];
            if (tx.is_write()) {
                if (phase_ != Phase::Acquired) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                word = value;
            } else {
                value = word;
            }
        } else if (offset == 0x60) {
            if (tx.is_write() && !write_control(value, delay)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            value = static_cast<std::uint32_t>(phase_) | owner_;
        } else if (offset == 0x12c) {
            if (tx.is_write()) {
                if (value & ~1U) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                irq_enable_ = value;
                irq_event_.notify(sc_core::SC_ZERO_TIME);
            } else {
                value = irq_enable_;
            }
        } else if (offset == 0x130 && tx.is_write() && (value & ~1U) == 0) {
            if (value & 1) {
                completed_ = false;
                phase_ = Phase::Idle;
                owner_ = 0;
                operation_error_ = false;
                command_error_ = 0;
                irq_event_.notify(sc_core::SC_ZERO_TIME);
            }
        } else if (tx.is_read() && offset == 0x128) {
            value = completed_ ? 1 : 0;
        } else if (tx.is_read() && offset == 0x144) {
            value = (output_words_ << 8) | (operation_error_ ? 1U : 0U);
        } else if (tx.is_read() && offset == 0x148) {
            value = command_error_;
        } else if (tx.is_read() && (offset == 0x160 || offset == 0x164)) {
            value = 0;
        } else {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_read()) store_word(tx.get_data_ptr(), value);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void policy_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!validate(tx, true)) return;
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            const auto value = load_word(tx.get_data_ptr());
            if (value > 1 || phase_ != Phase::Idle) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            policy_ = value;
        } else {
            store_word(tx.get_data_ptr(), policy_);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void payload_transport(tlm::tlm_generic_payload& tx, bool input) {
        if (!validate(tx, false)) return;
        auto& bytes = input ? input_ : output_;
        const auto offset = tx.get_address();
        const auto length = tx.get_data_length();
        if (offset >= bytes.size() || length > bytes.size() - offset) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            if (!input || phase_ != Phase::Acquired) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            std::memcpy(bytes.data() + offset, tx.get_data_ptr(), length);
            for (std::size_t i = offset; i != offset + length; ++i) input_written_[i] = true;
        } else {
            std::memcpy(tx.get_data_ptr(), bytes.data() + offset, length);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void load_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!validate(tx, false)) return;
        const auto offset = tx.get_address();
        const auto length = tx.get_data_length();
        if (offset >= input_.size() || length > input_.size() - offset) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (phase_ != Phase::Idle) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        // The protected loader view shares actual input RAM without acquiring the engine.
        // It cannot modify an acquired command or produce a completion.
        if (tx.is_write()) std::memcpy(input_.data() + offset, tx.get_data_ptr(), length);
        else std::memcpy(tx.get_data_ptr(), input_.data() + offset, length);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }


    void input_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        payload_transport(tx, true);
    }

    void output_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        payload_transport(tx, false);
    }
};

} // namespace qcom_vp

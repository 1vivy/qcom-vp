// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "calibration_fuses.h"
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <openssl/evp.h>

namespace qcom_vp {

// Evidence: TME ROM PC 0x20d06e reads the word at offset 0x73c.
// The model uses that word as the lifecycle value. Its source-only register
// name and its encodings remain descriptive labels.
class FuseController : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<FuseController> target_socket{"target_socket"};
    tlm_utils::simple_target_socket_optional<FuseController> tlmm_identity_socket{"tlmm_identity_socket"};
    tlm_utils::simple_target_socket_optional<FuseController> corrected_policy_socket{"corrected_policy_socket"};
    tlm_utils::simple_target_socket_optional<FuseController> pte_identity_socket{"pte_identity_socket"};
    tlm_utils::simple_target_socket_optional<FuseController> rom_error_socket{"rom_error_socket"};
    cci::cci_param<uint32_t> lifecycle{"lifecycle", 0, "The board lifecycle fuse value"};
    cci::cci_param<uint64_t> jtag_id{"jtag_id", UINT64_MAX, "Required virtual JTAG identity"};
    cci::cci_param<uint64_t> oem_hw_id{"oem_hw_id", UINT64_MAX, "Required virtual OEM hardware identity"};
    cci::cci_param<uint64_t> oem_product_id{"oem_product_id", UINT64_MAX, "Required virtual OEM product identity"};
    cci::cci_param<uint64_t> chip_id{"chip_id", UINT64_MAX, "Required virtual chip identity"};
    cci::cci_param<uint64_t> serial_number{"serial_number", UINT64_MAX, "Required virtual serial number"};
    cci::cci_param<uint64_t> pte_row4_msb{
        "pte_row4_msb", UINT64_MAX, "Required virtual PTE_ROW4_MSB manufacturing word"};
    cci::cci_param<uint64_t> tme_debug_vector0{
        "tme_debug_vector0", UINT64_MAX, "Required TME consolidated debug vector zero"};
    cci::cci_param<uint64_t> tme_debug_vector1{
        "tme_debug_vector1", UINT64_MAX, "Required TME consolidated debug vector one"};
    cci::cci_param<std::string> oem_pk_hash_sha384{
        "oem_pk_hash_sha384", "", "SHA-384 of the provisioned OEM root certificate"};
    cci::cci_param<uint64_t> feature_config9{"feature_config9", UINT64_MAX, "Required FEATURE_CONFIG9 fuse value"};
    cci::cci_param<uint64_t> feature_config11{"feature_config11", UINT64_MAX, "Required FEATURE_CONFIG11 fuse value"};
    cci::cci_param<uint64_t> feature_config18{"feature_config18", UINT64_MAX, "Required FEATURE_CONFIG18 fuse value"};
    cci::cci_param<uint64_t> feature_config19{"feature_config19", UINT64_MAX, "Required FEATURE_CONFIG19 fuse value"};
    cci::cci_param<uint64_t> qc_config0{"qc_config0", UINT64_MAX, "Required QC_CONFIG0 fuse value"};
    cci::cci_param<uint64_t> qc_config1{"qc_config1", UINT64_MAX, "Required QC_CONFIG1 fuse value"};
    cci::cci_param<uint64_t> qc_config4{"qc_config4", UINT64_MAX, "Required QC_CONFIG4 fuse value"};
    cci::cci_param<uint64_t> qc_config5{"qc_config5", UINT64_MAX, "Required QC_CONFIG5 fuse value"};
    cci::cci_param<uint64_t> oem_config0{"oem_config0", UINT64_MAX, "Required OEM_CONFIG0 fuse value"};
    cci::cci_param<uint64_t> oem_config2{"oem_config2", UINT64_MAX, "Required OEM_CONFIG2 fuse value"};
    cci::cci_param<uint64_t> oem_config5{"oem_config5", UINT64_MAX, "Required OEM_CONFIG5 rollback policy"};
    cci::cci_param<uint64_t> oem_security_policy_lsb{
        "oem_security_policy_lsb", UINT64_MAX, "Required OEM security policy word"};
    cci::cci_param<uint64_t> qc_config6{"qc_config6", UINT64_MAX, "Required QC_CONFIG6 fuse value"};
    cci::cci_param<uint64_t> qc_config7{"qc_config7", UINT64_MAX, "Required QC_CONFIG7 fuse value"};
    cci::cci_param<uint64_t> antirollback0{"antirollback0", UINT64_MAX, "Required ANTIROLLBACK0 fuse value"};
    cci::cci_param<uint64_t> antirollback17{"antirollback17", UINT64_MAX, "Required ANTIROLLBACK17 fuse value"};
    cci::cci_param<uint64_t> antirollback18{"antirollback18", UINT64_MAX, "Required ANTIROLLBACK18 fuse value"};
    cci::cci_param<uint64_t> antirollback22{"antirollback22", UINT64_MAX, "Required ANTIROLLBACK22 fuse value"};
    cci::cci_param<uint64_t> antirollback23{"antirollback23", UINT64_MAX, "Required ANTIROLLBACK23 fuse value"};
    cci::cci_param<uint64_t> patch_version{"patch_version", UINT64_MAX, "Required ROM patch version"};
    cci::cci_param<uint64_t> boot_config{"boot_config", UINT64_MAX, "Required boot configuration fuse value"};
    cci::cci_param<uint64_t> secure_boot{"secure_boot", UINT64_MAX, "Required secure boot policy fuse value"};
    cci::cci_param<uint64_t> captured_boot_gpio{
        "captured_boot_gpio", UINT64_MAX, "Required captured boot GPIO value"};
    cci::cci_param<uint64_t> test_image_vector1{
        "test_image_vector1", UINT64_MAX, "Required initial test image vector one"};
    cci::cci_param<uint64_t> fec_error_status{
        "fec_error_status", UINT64_MAX, "Required fuse error status"};
    cci::cci_param<uint64_t> rom_error_status{
        "rom_error_status", UINT64_MAX, "Required virtual QFPROM ROM error status"};
    cci::cci_param<bool> secondary_hw_key_programmed{
        "secondary_hw_key_programmed", false,
        "Declare the factory-provisioned secondary hardware key"};
    cci::cci_param<uint64_t> oem_config6{
        "oem_config6", UINT64_MAX, "Required OEM_CONFIG6 fuse value"};
    cci::cci_param<uint64_t> mrc0{
        "mrc0", UINT64_MAX, "Required MRC_0 root-certificate control value"};
    cci::cci_param<uint64_t> qfprom_write_permissions_lsw{
        "qfprom_write_permissions_lsw", UINT64_MAX, "Required QFPROM write-permission low word"};
    cci::cci_param<uint64_t> qfprom_write_permissions_msw{
        "qfprom_write_permissions_msw", UINT64_MAX, "Required QFPROM write-permission high word"};
    cci::cci_param<uint64_t> qfprom_read_permissions_lsw{
        "qfprom_read_permissions_lsw", UINT64_MAX, "Required QFPROM read-permission low word"};
    cci::cci_param<uint64_t> qfprom_read_permissions_msw{
        "qfprom_read_permissions_msw", UINT64_MAX, "Required QFPROM read-permission high word"};
    cci::cci_param<uint64_t> fuse_redundancy_enable0{
        "fuse_redundancy_enable0", UINT64_MAX, "Required QFPROM redundancy-enable low word"};
    cci::cci_param<uint64_t> fuse_redundancy_enable1{
        "fuse_redundancy_enable1", UINT64_MAX, "Required QFPROM redundancy-enable high word"};
    cci::cci_param<std::string> calibration_fuses{
        "calibration_fuses", "",
        "Private raw QFPROM calibration page for the listed words"};

    explicit FuseController(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &FuseController::transport);
        target_socket.register_transport_dbg(this, &FuseController::debug);
        tlmm_identity_socket.register_b_transport(this, &FuseController::tlmm_identity);
        corrected_policy_socket.register_b_transport(this, &FuseController::corrected_policy);
        pte_identity_socket.register_b_transport(this, &FuseController::pte_identity);
        rom_error_socket.register_b_transport(this, &FuseController::rom_error);
    }

private:
    uint32_t boot_gpio_ = 0;
    uint32_t test_image_vector1_ = 0;
    std::array<unsigned char, 48> oem_pk_hash_{};
    bool oem_pk_hash_present_ = false;
    QcomCalibrationFuses::Page calibration_page_{};
    bool calibration_page_present_ = false;
    // Local range five offset 0x58 is the self-test status word. The source-only
    // name of bit zero is a sense-done flag. The virtual self-test completes
    // without an error.
    uint32_t fuse_sense_done_ = 0;

    static std::string sha256_hex(const unsigned char* data, size_t size) {
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned length = 0;
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (!context)
            throw std::runtime_error("The calibration page hash has no context");
        const bool ok =
            EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
            EVP_DigestUpdate(context, data, size) == 1 &&
            EVP_DigestFinal_ex(context, digest.data(), &length) == 1;
        EVP_MD_CTX_free(context);
        if (!ok || length != 32)
            throw std::runtime_error("The calibration page hash failed");
        std::string hex(2 * length, '0');
        for (unsigned i = 0; i != length; ++i) {
            const auto byte = unsigned(digest[i]);
            const auto high = byte >> 4;
            const auto low = byte & 0xf;
            hex[2 * i] = char(high < 10 ? '0' + high : 'a' + high - 10);
            hex[2 * i + 1] = char(low < 10 ? '0' + low : 'a' + low - 10);
        }
        return hex;
    }

    // The page is the raw QFPROM range 0x221c8000..0x221c8fff. The manifest
    // supplies its identity. A malformed page stops elaboration.
    void load_calibration_fuses() {
        const auto& path = calibration_fuses.get_value();
        if (path.empty()) {
            std::cout << "FUSE_CALIBRATION_SOURCE model=qcom,sm8850-fuse-controller"
                         " source=declared-zero\n" << std::flush;
            return;
        }
        std::ifstream file(path, std::ios::binary);
        std::array<char, QcomCalibrationFuses::page_size + 1> bytes{};
        if (!file)
            throw std::invalid_argument("The calibration fuse page cannot be opened");
        file.read(bytes.data(), std::streamsize(bytes.size()));
        if (file.gcount() != std::streamsize(QcomCalibrationFuses::page_size))
            throw std::invalid_argument("The calibration fuse page must contain 4096 bytes");
        std::copy(bytes.begin(), bytes.begin() + QcomCalibrationFuses::page_size,
                  calibration_page_.begin());
        calibration_page_present_ = true;
        std::cout << "FUSE_CALIBRATION_SOURCE model=qcom,sm8850-fuse-controller"
                     " source=private-page sha256="
                  << sha256_hex(calibration_page_.data(), calibration_page_.size())
                  << std::endl;
    }

    void end_of_elaboration() override {
        if (captured_boot_gpio.get_value() > 0x1fff || qc_config1.get_value() > UINT32_MAX ||
            feature_config9.get_value() > UINT32_MAX ||
            feature_config11.get_value() > UINT32_MAX ||
            feature_config18.get_value() > UINT32_MAX ||
            feature_config19.get_value() > UINT32_MAX ||
            jtag_id.get_value() > UINT32_MAX ||
            oem_hw_id.get_value() > UINT16_MAX || oem_product_id.get_value() > UINT16_MAX ||
            chip_id.get_value() > UINT32_MAX || serial_number.get_value() > UINT32_MAX ||
            pte_row4_msb.get_value() > UINT32_MAX ||
            tme_debug_vector0.get_value() > UINT32_MAX ||
            tme_debug_vector1.get_value() > UINT32_MAX ||
            antirollback0.get_value() > UINT32_MAX || antirollback17.get_value() > UINT32_MAX ||
            antirollback18.get_value() > UINT32_MAX ||
            antirollback22.get_value() > UINT32_MAX || antirollback23.get_value() > UINT32_MAX ||
            oem_config2.get_value() > UINT32_MAX ||
            oem_config5.get_value() > UINT32_MAX ||
            oem_security_policy_lsb.get_value() > UINT32_MAX ||
            oem_config0.get_value() > UINT32_MAX || qc_config6.get_value() > UINT32_MAX ||
            qc_config7.get_value() > UINT32_MAX ||
            qc_config0.get_value() > UINT32_MAX || qc_config5.get_value() > UINT32_MAX ||
            qc_config4.get_value() > UINT32_MAX || boot_config.get_value() > 0x1ff ||
            secure_boot.get_value() > 0x1ff || patch_version.get_value() > UINT32_MAX ||
            test_image_vector1.get_value() > UINT32_MAX ||
            fec_error_status.get_value() > UINT32_MAX ||
            rom_error_status.get_value() > UINT32_MAX ||
            oem_config6.get_value() > UINT32_MAX || mrc0.get_value() > UINT32_MAX ||
            qfprom_write_permissions_lsw.get_value() > UINT32_MAX ||
            qfprom_write_permissions_msw.get_value() > UINT32_MAX ||
            qfprom_read_permissions_lsw.get_value() > UINT32_MAX ||
            qfprom_read_permissions_msw.get_value() > UINT32_MAX ||
            fuse_redundancy_enable0.get_value() > UINT32_MAX ||
            fuse_redundancy_enable1.get_value() > UINT32_MAX) {
            throw std::invalid_argument("The fuse profile requires valid configuration, GPIO, and error status values");
        }
        boot_gpio_ = captured_boot_gpio.get_value();
        test_image_vector1_ = test_image_vector1.get_value();
        load_calibration_fuses();
        const auto& encoded = oem_pk_hash_sha384.get_value();
        if (encoded.empty()) return;
        if (encoded.size() != 96)
            throw std::invalid_argument("The OEM root hash requires 96 hexadecimal characters");
        for (size_t i = 0; i != oem_pk_hash_.size(); ++i) {
            unsigned byte = 0;
            for (size_t j = 0; j != 2; ++j) {
                const auto c = encoded[2 * i + j];
                const int digit = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (digit < 0)
                    throw std::invalid_argument("The OEM root hash requires hexadecimal digits");
                byte = (byte << 4) | unsigned(digit);
            }
            oem_pk_hash_[i] = static_cast<unsigned char>(byte);
        }
        oem_pk_hash_present_ = true;
    }
    // XBL 0x1485eba8 and 0x1485ebbc read the split TLMM view of the JTAG identity.
    void tlmm_identity(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const uint32_t identity = jtag_id.get_value();
        uint32_t value;
        if (tx.get_address() == 0) value = (identity >> 12) & 0xffff;
        else if (tx.get_address() == 4) value = identity >> 28;
        else {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        for (unsigned i = 0; i != 4; ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // CPUCP reads corrected PTE_ROW4_MSB and retains bit eight for its policy.
    // The selected firmware, not the model, consumes that bit.
    void pte_identity(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const uint32_t value = pte_row4_msb.get_value();
        for (unsigned i = 0; i != 4; ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void rom_error(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (tx.get_address() != 0) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const uint32_t value = rom_error_status.get_value();
        for (unsigned i = 0; i != 4; ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        std::cerr << "QFPROM_ROM_ERROR_STATUS model=" << name()
                  << " value=0x" << std::hex << value << std::dec << '\n';
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // Selected OEM row five: raw lookup key 0x021c0090, corrected row 0x02302090.
    void corrected_policy(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        const auto length = tx.get_data_length();
        const auto offset = tx.get_address();
        if ((length != 4 && length != 8) || tx.get_streaming_width() < length ||
            !tx.get_data_ptr() || (offset & (length - 1)) != 0) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (offset >= 0x18 || length > 0x18 - offset) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        // Virtual aggregate-error policy, not a recovered vendor bitmap layout.
        // Any injected FEC error marks every row unavailable in both status
        // views. The real OEM descriptor mask then selects its failure path.
        for (unsigned i = 0; i != length; ++i) {
            const auto byte = offset + i;
            tx.get_data_ptr()[i] = byte < 8
                ? static_cast<unsigned char>(oem_security_policy_lsb.get_value() >> (byte * 8))
                : (fec_error_status.get_value() == 0 ? 0 : 0xff);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }


    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        access(tx);
    }

    unsigned debug(int, tlm::tlm_generic_payload& tx) {
        access(tx);
        return tx.is_response_ok() ? tx.get_data_length() : 0;
    }

    void access(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        const auto length = tx.get_data_length();
        if ((length != 4 && length != 8) || tx.get_streaming_width() < length ||
            !tx.get_data_ptr() || (tx.get_address() & (length - 1)) != 0 ||
            (tx.is_write() && length != 4)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write() && (tx.get_address() == 0x1004 || tx.get_address() == 0x100c)) {
            const auto* data = tx.get_data_ptr();
            const uint32_t value = uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
                (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
            const bool gpio = tx.get_address() == 0x1004;
            if (gpio && (value & ~0x1fffU)) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            (gpio ? boot_gpio_ : test_image_vector1_) = value;
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (tx.is_write() && tx.get_address() == 0x1054) {
            // Local range five offset 0x54 is the word that the OEM descriptor
            // table selects. The source-only mask 0x1 and the START meaning of
            // bit zero remain unestablished; the model keeps this bounded step.
            // The virtual fuse array has no injected sense error, so the
            // self-test completes at the write. START reads zero afterward and
            // the status word reports a completed sense. The source-only mask
            // defines bit zero only, so the model stores no other bit.
            // No fuse value, policy, or authentication input changes.
            const auto* data = tx.get_data_ptr();
            const uint32_t value = uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
                (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
            if (value & 1U) fuse_sense_done_ = 1;
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (!tx.is_read()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        uint32_t words[2];
        for (unsigned i = 0; i != length / 4; ++i) {
            const auto status = read_word(tx.get_address() + 4 * i, words[i]);
            if (status != tlm::TLM_OK_RESPONSE) {
                tx.set_response_status(status);
                return;
            }
        }
        for (unsigned i = 0; i != length; ++i)
            tx.get_data_ptr()[i] = static_cast<unsigned char>(words[i / 4] >> ((i % 4) * 8));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    tlm::tlm_response_status read_word(uint64_t address, uint32_t& value) const {
        if (address == 0x10) {
            // This 32-bit word is the fuse self-test status word. The SM8850 QSEE
            // secure-state check reads bit 28 after the fuse self-test and
            // reports the Sec HW key as programmed when the bit is set.
            // The explicit profile parameter declares that condition.
            value = secondary_hw_key_programmed.get_value() ? 0x10000000U : 0;
        } else if (address == 0x40) {
            // QSEE 0x15696058 checks the retail fuse-protection bits.
            value = qfprom_write_permissions_lsw.get_value();
        } else if (address == 0x44) {
            value = qfprom_write_permissions_msw.get_value();
        } else if (address == 0x48) {
            value = qfprom_read_permissions_lsw.get_value();
        } else if (address == 0x4c) {
            value = qfprom_read_permissions_msw.get_value();
        } else if (address == 0x50) {
            value = fuse_redundancy_enable0.get_value();
        } else if (address == 0x54) {
            value = fuse_redundancy_enable1.get_value();
        } else if (address == 0x73c) {
            value = lifecycle.get_value();
        } else if (address == 0x124) {
            value = feature_config9.get_value();
        } else if (address == 0x12c) {
            value = feature_config11.get_value();
        } else if (address == 0x148) {
            value = feature_config18.get_value();
        } else if (address == 0x14c) {
            value = feature_config19.get_value();
        } else if (address == 0x200) {
            value = qc_config0.get_value();
        } else if (address == 0x204 && qc_config1.get_value() <= UINT32_MAX) {
            value = qc_config1.get_value();
        } else if (address == 0x210) {
            value = qc_config4.get_value();
        } else if (address == 0x214) {
            value = qc_config5.get_value();
        } else if (address == 0x218) {
            value = qc_config6.get_value();
        } else if (address == 0x21c) {
            value = qc_config7.get_value();
        } else if (address == 0x300) {
            value = oem_config0.get_value();
        } else if (address == 0x308) {
            value = oem_config2.get_value();
        } else if (address == 0x314) {
            value = oem_config5.get_value();
        } else if (address == 0x318) {
            value = oem_config6.get_value();
        } else if (address == 0x400) {
            value = antirollback0.get_value();
        } else if (address == 0x444) {
            value = antirollback17.get_value();
        } else if (address == 0x448) {
            value = antirollback18.get_value();
        } else if (address == 0x458) {
            value = antirollback22.get_value();
        } else if (address == 0x45c) {
            value = antirollback23.get_value();
        } else if (address >= 0x500 && address <= 0x548 &&
                   (address & 3) == 0) {
            if (!oem_pk_hash_present_) {
                return tlm::TLM_COMMAND_ERROR_RESPONSE;
            }
            const auto index = unsigned((address - 0x500) / 4);
            const auto first = (index / 2) * 7 + (index & 1) * 4;
            const auto count = index == 18 ? 1U : (index & 1) ? 3U : 4U;
            value = 0;
            // ROM 0x20bfec copies bytes in order and skips each odd register's upper byte.
            // The 128 spare hash bits are virtual zero, not a captured fuse value.
            for (unsigned i = 0; i < count && first + i < oem_pk_hash_.size(); ++i)
                value |= uint32_t(oem_pk_hash_[first + i]) << (8 * i);
        } else if (address == 0x600) {
            value = boot_config.get_value();
        } else if (address == 0x604) {
            value = secure_boot.get_value();
        } else if (address == 0x610) {
            value = serial_number.get_value();
        } else if (address == 0x614) {
            value = chip_id.get_value();
        } else if (address == 0x630) {
            value = mrc0.get_value();
        } else if (address == 0x700) {
            value = oem_hw_id.get_value();
        } else if (address == 0x704) {
            value = oem_product_id.get_value();
        } else if (address == 0x71c) {
            value = (oem_security_policy_lsb.get_value() >> 1) & 1;
        } else if (address == 0x724) {
            value = (oem_security_policy_lsb.get_value() >> 4) & 1;
        } else if (address == 0x728) {
            value = (oem_security_policy_lsb.get_value() >> 5) & 1;
        } else if (address == 0x72c) {
            value = (oem_security_policy_lsb.get_value() >> 11) & 1;
        } else if (address == 0x744) {
            value = jtag_id.get_value();
        } else if (address == 0x780) {
            value = tme_debug_vector0.get_value();
        } else if (address == 0x784) {
            value = tme_debug_vector1.get_value();
        } else if (address == 0xcf0) {
            value = oem_security_policy_lsb.get_value();
        } else if (address == 0xcf8) {
            value = patch_version.get_value();
        } else if (address == 0x1004) {
            value = boot_gpio_;
        } else if (address == 0x100c) {
            value = test_image_vector1_;
        } else if (address == 0x1034) {
            value = fec_error_status.get_value();
        } else if (address == 0x1054) {
            // Self-test control word. The source-only mask is 0x1 and the
            // virtual self-test completes at the write, so it reads clear.
            value = 0;
        } else if (address == 0x1058) {
            // Self-test status word. The source-only name of bit zero is a
            // sense-done flag. The word accepts reads only.
            value = fuse_sense_done_;
        } else if (!QcomCalibrationFuses::read_raw_word(
                       address, calibration_page_present_ ? &calibration_page_ : nullptr,
                       value)) {
            return tlm::TLM_ADDRESS_ERROR_RESPONSE;
        }
        return tlm::TLM_OK_RESPONSE;
    }
};

}

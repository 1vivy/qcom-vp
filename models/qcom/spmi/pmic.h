// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once

#include <systemc>
#include <cci_configuration>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <array>
#include <bitset>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <map>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "pmic_pbs.h"
#include "pmic_access.h"

namespace qcom_vp {

// Sparse virtual state. No register value is inferred from an SPMI route.
class QcomVirtualPmic : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomVirtualPmic> target_socket{"target_socket"};
    cci::cci_param<std::string> identities{
        "identities", "", "Virtual primary identities: bus sid model major minor span variant, separated by semicolons"};
    cci::cci_param<std::string> registers{
        "registers", "", "Explicit virtual bytes: bus sid address reset writable_mask, separated by semicolons"};
    cci::cci_param<std::string> pon_peripherals{
        "pon_peripherals", "", "Virtual cold PON control groups: bus sid peripheral, separated by semicolons"};
    cci::cci_param<std::string> pon_hlos_peripherals{
        "pon_hlos_peripherals", "", "PON GEN3 HLOS groups without key inputs: bus sid peripheral"};
    cci::cci_param<std::string> sdam_peripherals{
        "sdam_peripherals", "", "Retained 128-byte SDAM groups: bus sid first_peripheral count"};
    cci::cci_param<std::string> absent_peripherals{
        "absent_peripherals", "", "Explicit absent discovery revisions: bus sid peripheral"};
    cci::cci_param<std::string> regulator_peripherals{
        "regulator_peripherals", "", "Synchronous virtual rails: bus sid peripheral type (3, 4, or 41)"};
    cci::cci_param<std::string> pbs_peripherals{
        "pbs_peripherals", "", "PBS memory groups: bus sid peripheral revision ram_base_bytes ram_size_bytes"};
    cci::cci_param<std::string> gpio_peripherals{
        "gpio_peripherals", "", "Disconnected digital GPIO banks: bus sid first_peripheral count"};
    cci::cci_param<std::string> clock_peripherals{
        "clock_peripherals", "", "Clock configuration groups: bus sid peripheral subtype (1, 8, or 15)"};
    cci::cci_param<std::string> rtc_status_peripherals{
        "rtc_status_peripherals", "", "Cold RTC loss-of-power status groups: bus sid peripheral"};
    cci::cci_param<std::string> misc_peripherals{
        "misc_peripherals", "", "Identified non-functional peripherals: bus sid peripheral type subtype"};
    cci::cci_param<std::string> disconnected_irq_peripherals{
        "disconnected_irq_peripherals", "", "Interrupt groups without event producers: bus sid peripheral"};
    cci::cci_param<std::string> protected_controls{
        "protected_controls", "", "One-shot protected controls: bus sid address writable_mask"};
    cci::cci_param<bool> exploratory_configuration{
        "exploratory_configuration", false, "Log and retain unknown ordinary configuration bytes"};
    cci::cci_param<bool> virtual_absent_discovery{
        "virtual_absent_discovery", false, "Log seven-byte absence for undeclared peripherals without exploratory writes"};
    cci::cci_param<std::string> dump_peripherals{
        "dump_peripherals", "", "Devcfg dump groups with observer read fallback: bus sid peripheral"};
    cci::cci_param<bool> scan_block_reads{
        "scan_block_reads", false, "Answer unmarked eight-byte block bursts; a byte without a value reads zero"};
    cci::cci_param<bool> adc_mailbox{
        "adc_mailbox", false, "Enable the bounded virtual battery ADC service in bus0 SID0 SDAM90"};
    cci::cci_param<unsigned> battery_millivolts{
        "battery_millivolts", 8800, "Declared two-cell battery source, not a measurement"};
    cci::cci_param<unsigned> battery_id_ohms{
        "battery_id_ohms", 180000, "Declared battery ID resistance against a 100000-ohm pull-up"};

    explicit QcomVirtualPmic(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomVirtualPmic::transport);
    }

    void cold_reset() {
        for (auto& entry : bytes_) entry.second.value = entry.second.reset;
        for (auto& entry : pbs_) entry.second.cold_reset();
        for (auto& peripheral : locks_) peripheral.fill(0);
        for (auto& byte : exploratory_) byte = 0;
        exploratory_written_.reset();
    }

    // A virtual warm reset retains SDAM and terminal peripheral locks.
    void warm_reset() {
        for (const auto base : pon_)
            if (value(base + 0xda) & 4)
                bytes_.at(base + 0x8c).value = bytes_.at(base + 0x8c).reset;
        for (auto& entry : bytes_)
            if (!entry.second.retained) entry.second.value = entry.second.reset;
        for (auto& entry : pbs_) entry.second.warm_reset();
        for (auto& byte : exploratory_) byte = 0;
        exploratory_written_.reset();
    }

private:
    enum class ByteKind : std::uint8_t {
        plain, irq_latched_clear, irq_enable_set, irq_enable_clear,
        security_key, protected_control
    };
    struct Byte {
        std::uint8_t reset;
        std::uint8_t value;
        std::uint8_t mask;
        bool retained;
        ByteKind kind;
    };
    std::array<bool, 32> present_{};
    std::array<unsigned char, 32> sid_bank_{};
    std::set<std::uint32_t> pon_;
    std::map<std::uint32_t, Byte> bytes_;
    // D1/D2/D3: open (0), reserved (1), locked open (2), locked closed (3).
    std::array<std::array<std::uint8_t, 3>, 8192> locks_{};
    std::map<std::uint32_t, unsigned> regulators_;
    std::map<std::uint32_t, QcomPmicPbs> pbs_;
    std::set<std::uint32_t> gpios_;
    std::vector<std::uint8_t> exploratory_;
    std::bitset<8192> declared_peripherals_;
    // Devcfg-selected peripheral groups that a QSEE dump observes.
    std::set<unsigned> dump_observed_;
    std::bitset<8192> reported_absences_;
    std::bitset<8192> exploratory_written_;

    static std::uint32_t key(unsigned bus, unsigned sid, unsigned address) {
        return (bus << 20) | (sid << 16) | address;
    }

    template <std::size_t N, typename F>
    static void rows(const std::string& text, F consume) {
        std::istringstream input(text);
        std::string row;
        while (std::getline(input, row, ';')) {
            if (row.find_first_not_of(" \t\n\r") == std::string::npos) continue;
            std::istringstream fields(row);
            std::array<unsigned, N> values{};
            for (auto& value : values) {
                std::string token;
                if (!(fields >> token)) throw std::invalid_argument("Incomplete virtual PMIC row");
                std::size_t used = 0;
                const auto number = std::stoul(token, &used, 0);
                if (used != token.size() || number > 0xffffffffUL)
                    throw std::invalid_argument("Invalid virtual PMIC number");
                value = static_cast<unsigned>(number);
            }
            std::string extra;
            if (fields >> extra) throw std::invalid_argument("Extra virtual PMIC field");
            consume(values);
        }
    }

    void add(unsigned bus, unsigned sid, unsigned address, unsigned value, unsigned mask,
             bool retained = false, ByteKind kind = ByteKind::plain) {
        if (value > 255 || mask > 255 ||
            !bytes_.emplace(key(bus, sid, address), Byte{
                static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value),
                static_cast<std::uint8_t>(mask), retained, kind}).second)
            throw std::invalid_argument("Duplicate or invalid virtual PMIC byte: bus " +
                std::to_string(bus) + " sid " + std::to_string(sid) + " address " +
                std::to_string(address));
    }

    void require_peripheral(unsigned bus, unsigned sid, unsigned peripheral) const {
        if (bus > 1 || sid > 15 || peripheral < 2 || peripheral > 255 ||
            !present_[bus * 16 + sid])
            throw std::invalid_argument("Virtual peripheral requires a populated SID");
    }

    // The dump reads peripheral offset four to decide presence. This model
    // answers that byte from the byte map or from the PBS interface.
    bool presence_byte(unsigned bus, unsigned sid, unsigned peripheral) {
        const auto address = key(bus, sid, (peripheral << 8) + 4);
        const auto found = bytes_.find(address);
        if (found != bytes_.end()) return found->second.reset != 0;
        const auto pbs = pbs_.find(address & ~255U);
        if (pbs == pbs_.end() || !QcomPmicPbs::owns_register(4)) return false;
        std::uint8_t value = 0;
        return pbs->second.read(4, &value, 1) == QcomPmicPbs::Result::ok && value != 0;
    }

    void revision(unsigned bus, unsigned sid, unsigned peripheral,
                  unsigned type, unsigned subtype,
                  unsigned digital = 0x0100, unsigned analog = 0x0100) {
        require_peripheral(bus, sid, peripheral);
        // DCP a00f0a8a / a411cc78 reads the subtype and dummy byte together.
        // Byte seven is the discovery dummy. UEFI run523 reads it in
        // eight-byte bursts at a regulator group.
        const std::array<unsigned, 8> values{
            digital & 255, digital >> 8, analog & 255, analog >> 8, type, subtype, 0, 0};
        for (unsigned i = 0; i < values.size(); ++i)
            add(bus, sid, (peripheral << 8) + i, values[i], 0);
    }
    // No virtual input latches an interrupt. GPIO reads resolve the pin level.
    void disconnected_irq(unsigned bus, unsigned sid, unsigned base) {
        for (unsigned offset : {0x10, 0x18, 0x19})
            add(bus, sid, base + offset, 0, 0);
        for (unsigned offset : {0x11, 0x12, 0x13, 0x1a, 0x1b})
            add(bus, sid, base + offset, 0, 0xff);
        // QSEE reads the whole status block as one burst (run502), and the
        // secondary block tail at 0x18..0x1f (run504). No header assigns those
        // tail bytes a role in this policy, and no event producer exists.
        // They read cold zero and reject writes, so no warm-boot state leaks.
        for (unsigned offset : {0x17, 0x1c, 0x1d, 0x1e, 0x1f})
            add(bus, sid, base + offset, 0, 0);
        add(bus, sid, base + 0x14, 0, 0xff, false, ByteKind::irq_latched_clear);
        add(bus, sid, base + 0x15, 0, 0xff, false, ByteKind::irq_enable_set);
        add(bus, sid, base + 0x16, 0, 0xff, false, ByteKind::irq_enable_clear);
    }


    void end_of_elaboration() override {
        if (exploratory_configuration.get_value()) exploratory_.resize(0x200000);
        // XBL 0x148344a8..0x148345c4 reads 0102..0105, 0100..0106, and 0160.
        // Spans and all identity values are explicit board policy, not captures.
        rows<7>(identities.get_value(), [this](const auto& v) {
            const auto bus = v[0], sid = v[1], span = v[5];
            if (bus > 1 || sid > 13 || !span || span > 14 - sid || !v[2] ||
                v[2] > 255 || v[3] > 255 || v[4] > 255 || v[6] > 255)
                throw std::invalid_argument("Invalid virtual PMIC identity");
            for (unsigned bank = 0; bank < span; ++bank) {
                const auto s = sid + bank;
                if (present_[bus * 16 + s])
                    throw std::invalid_argument("Overlapping virtual PMIC SID spans");
                present_[bus * 16 + s] = true;
                sid_bank_[bus * 16 + s] = bank;
                add(bus, s, 0x100, 0, 0);
                add(bus, s, 0x101, 0, 0);
                add(bus, s, 0x102, v[4], 0);
                add(bus, s, 0x103, v[3], 0);
                add(bus, s, 0x104, 0x51, 0);
                add(bus, s, 0x105, v[2], 0);
                add(bus, s, 0x106, span - bank, 0);
                // QSEE reads this block in eight-byte bursts (runtime474, 486,
                // 489, 491): 0x100, then 0x108, then 0x110. The live-phone
                // regmap read of bus 0, SID 0 records 0x107..0x14f as zero
                // (record 20261026T150037Z). The bytes stay read-only.
                for (unsigned ecid = 0x107; ecid <= 0x14f; ++ecid)
                    add(bus, s, ecid, 0, 0);
                // Guest PSI version scratch, not silicon identity or OTP.
                // SM8845 0x148fbde4..0x148fbf28 and SM8850
                // 0x14900890..0x149009a4 program these before post-init logs.
                add(bus, s, 0x150, 0, 0xff);
                add(bus, s, 0x151, 0, 0xff);
                // The live-phone regmap records this tail for bus 0, SID 0
                // (record 20261026T150037Z): bytes 0x152..0x1ef read zero,
                // except 0x161, which reads 0x61. Byte 0x154 keeps its declared
                // OTP identifier row, and byte 0x160 keeps the identity variant
                // owner. QSEE reads the tail in eight-byte bursts from 0x150
                // through 0x1e8 (runtime494..run499). UEFI run522 reads the same
                // bursts on bus 0, SID 1. No source gives a per-SID value, so
                // every populated SID declares this tail as read-only zero, and
                // only bus 0, SID 0 carries the recorded 0x161 value.
                for (unsigned tail = 0x152; tail <= 0x1ef; ++tail) {
                    if (tail == 0x154 || tail == 0x160) continue;
                    add(bus, s, tail, bus == 0 && s == 0 && tail == 0x161 ? 0x61 : 0, 0);
                }
                add(bus, s, 0x160, v[6], 0);
            }
        });
        // XBL 0x14833050 and 0x148330a0 require PON type 1, subtype 8.
        // Digital 1.0 and analog 1.0 satisfy its bounds at 0x14833108..0x14833130.
        rows<3>(pon_peripherals.get_value(), [this](const auto& v) {
            if (v[0] > 1 || v[1] > 15 || !v[2] || v[2] > 255 ||
                !present_[v[0] * 16 + v[1]])
                throw std::invalid_argument("Virtual PON group requires a populated SID");
            const unsigned base = v[2] << 8;
            // Seven revision bytes plus the dummy byte at offset seven.
            // QSEE reads all eight bytes as one burst (run534, bus0 SID5).
            constexpr std::array<unsigned, 8> revision{0, 1, 0, 1, 1, 8, 0, 0};
            for (unsigned i = 0; i < revision.size(); ++i)
                add(v[0], v[1], base + i, revision[i], 0);
            // QSEE reads the next fields as eight-byte bursts (run501..run505).
            // This policy has no event producer, so the status bytes read cold
            // zero. The live-phone record 20261026T161459Z also reads zero at
            // 0x20..0x3f after a warm boot, so no warm state is copied.
            for (unsigned offset = 8; offset < 0x10; ++offset)
                add(v[0], v[1], base + offset, 0, 0);
            for (unsigned offset = 0x20; offset < 0x40; ++offset)
                add(v[0], v[1], base + offset, 0, 0);
            // QSEE reads this burst next (run506). Byte 0xd1 reads 0x02 on the
            // warm phone, so it is warm state and not a cold value to copy.
            for (unsigned offset = 0xd0; offset < 0xd8; ++offset)
                add(v[0], v[1], base + offset, 0, 0);
            // Cold software-spare and reset controls. These are not power status.
            // Consumers: XBL 0x1483318c, 0x148331b0, 0x148399d4, 0x1483b0f0.
            add(v[0], v[1], base + 0x60, 0, 0x80);
            // The virtual reset policy clears the initialization spare on warm reset.
            // The guest must still complete initialization and write the spare.
            pon_.insert(key(v[0], v[1], base));
            add(v[0], v[1], base + 0x8c, 0, 0x30, true);
            add(v[0], v[1], base + 0x8d, 0, 1);
            add(v[0], v[1], base + 0xda, 4, 4);
            // Post-PSI reset requests: SM8850 1483b6fc, SM8845 14839bac.
            // Three S1/S2 groups occupy offsets 40..4b.
            // These controls retain requests. They do not cause a virtual reset.
            for (unsigned offset : {0x40, 0x44, 0x48})
                add(v[0], v[1], base + offset, 0, 0x0f);
            for (unsigned offset : {0x41, 0x45, 0x49})
                add(v[0], v[1], base + offset, 0, 7);
            for (unsigned offset : {0x42, 0x46, 0x4a})
                add(v[0], v[1], base + offset, 0, 0x0f);
            for (unsigned offset : {0x43, 0x47, 0x4b, 0x53})
                add(v[0], v[1], base + offset, 0, 0x80);
            add(v[0], v[1], base + 0x52, 0, 0xff);
            add(v[0], v[1], base + 0x74, 0, 3);
            add(v[0], v[1], base + 0x75, 0, 0x0f);
            // UVLO/OVLO requests, not voltage comparators or fault producers.
            add(v[0], v[1], base + 0x84, 0, 0xff);
            add(v[0], v[1], base + 0x86, 0, 0xff);
            // Manufacturing test option is cold-clear, not a test result.
            // SM8850 14835610 and SM8845 1483372c read/clear this bit.
            add(v[0], v[1], base + 0x8f, 0, 1);
            disconnected_irq(v[0], v[1], base);
        });
        // Gen3 PON has two register sets: HLOS at 0x1300 and PBS at 0x0800
        // (pmk8280/pmk8550 device trees). Linux pm8941-pwrkey.c names subtype 9
        // PON_SUBTYPE_GEN3_HLOS. UEFI ButtonsDxe reads its real-time status, and
        // SPMIDxe clears its interrupt enables at ExitBootServices (run869).
        // No virtual key exists, so the interrupt block has no event producer.
        rows<3>(pon_hlos_peripherals.get_value(), [this](const auto& v) {
            const unsigned base = v[2] << 8;
            revision(v[0], v[1], v[2], 1, 9);
            for (unsigned offset = 8; offset < 0x10; ++offset)
                add(v[0], v[1], base + offset, 0, 0);
            disconnected_irq(v[0], v[1], base);
        });
        // XBL 0x14883468 accepts SDAM type 2e/subtype 1, digital <= 1.0.
        // Its table at 0x148f3df0 specifies stride 100 and size byte 44.
        rows<4>(sdam_peripherals.get_value(), [this](const auto& v) {
            require_peripheral(v[0], v[1], v[2]);
            if (!v[3] || v[3] > 256 - v[2])
                throw std::invalid_argument("Invalid virtual SDAM range");
            for (unsigned p = v[2]; p < v[2] + v[3]; ++p) {
                revision(v[0], v[1], p, 0x2e, 1);
                for (unsigned offset = 0x40; offset < 0xc0; ++offset) {
                    // Empty history starts at the first byte of bank 74.
                    const bool pointer = v[0] == 0 && v[1] == 0 && p == 0x74 &&
                                         offset == 0x46;
                    const bool size = offset == 0x44;
                    add(v[0], v[1], (p << 8) + offset,
                        size ? 4 : pointer ? 0x4b : 0, size ? 0 : 0xff, !size);
                }
            }
        });
        // Seven equal discovery bytes denote absence, including the dummy byte.
        // XBL 0x14835fd8..0x1483601c rejects type zero; 0x14884c6c skips it.
        rows<3>(absent_peripherals.get_value(), [this](const auto& v) {
            require_peripheral(v[0], v[1], v[2]);
            for (unsigned i = 0; i < 7; ++i)
                add(v[0], v[1], (v[2] << 8) + i, 0, 0);
        });
        rows<4>(gpio_peripherals.get_value(), [this](const auto& v) {
            require_peripheral(v[0], v[1], v[2]);
            if (!v[3] || v[3] > 256 - v[2])
                throw std::invalid_argument("Invalid virtual GPIO bank");
            for (unsigned p = v[2]; p < v[2] + v[3]; ++p) {
                // The selected VIN decoder accepts all three inputs for subtype19.
                // SM8845 1487c004..1487c05c preserves its actual selection checks.
                // DCP requires <3.2. XBL and both UEFI drivers accept <=3.2.
                revision(v[0], v[1], p, 16, 19, 0x0301);
                const auto base = p << 8;
                gpios_.insert(key(v[0], v[1], base));
                add(v[0], v[1], base + 8, 0, 0);
                // Virtual GPIO banks start enabled. This is permissive cold policy,
                // not a physical reset claim. Guest EN_CTL writes can disable pins.
                // SM8850 button setup 14897e48 sets mode, VIN and pull only.
                for (unsigned offset = 0x40; offset <= 0x46; ++offset)
                    add(v[0], v[1], base + offset, offset == 0x46 ? 0x80 : 0, 0xff);
                disconnected_irq(v[0], v[1], base);
            }
        });
        rows<4>(clock_peripherals.get_value(), [this](const auto& v) {
            if (v[3] != 1 && v[3] != 8 && v[3] != 15)
                throw std::invalid_argument("Unsupported virtual clock subtype");
            // BB 8.2 admits separate P/N drive requests. CALRC3 4.1 and
            // analog 0.1 satisfy the selected driver bounds.
            revision(v[0], v[1], v[2], 6, v[3],
                     v[3] == 15 ? 0x0401 : v[3] == 8 ? 0x0802 : 0x0100, 1);
            const auto base = v[2] << 8;
            for (unsigned offset : {0x44, 0x46, 0x48, 0x4a, 0x4d, 0x50, 0x69})
                add(v[0], v[1], base + offset, 0, 0xff);
            // No frequency, calibration result, or oscillator-ready bit exists.
        });
        rows<3>(rtc_status_peripherals.get_value(), [this](const auto& v) {
            // This bounded service exposes only the loss-of-power interface.
            // It does not implement or claim a running RTC counter.
            revision(v[0], v[1], v[2], 7, 1, 0x0501);
            add(v[0], v[1], (v[2] << 8) + 8, 0, 0);
            add(v[0], v[1], (v[2] << 8) + 9, 0, 1);
        });
        rows<5>(misc_peripherals.get_value(), [this](const auto& v) {
            require_peripheral(v[0], v[1], v[2]);
            if (v[3] > 255 || v[4] > 255)
                throw std::invalid_argument("Invalid identified peripheral revision");
            // Type 20 is MISC. Its subtype set has no 0x6b entry, so this
            // service reports the observed identity and supplies no function
            // for the peripheral.
            // The live-phone record 20261026T174054Z reads zero at every other
            // revision byte of bus 0, SID 1, peripherals 0x40 and 0x41.
            revision(v[0], v[1], v[2], v[3], v[4], 0, 0);
            // The same record reads zero at 0x08..0x1f. UEFI run524 reads the
            // eight-byte burst at 0x08, so that span is declared read-only zero.
            for (unsigned offset = 8; offset < 0x20; ++offset)
                add(v[0], v[1], (v[2] << 8) + offset, 0, 0);
        });
        rows<4>(regulator_peripherals.get_value(), [this](const auto& v) {
            const auto type = v[3];
            if (type != 3 && type != 4 && type != 0x29)
                throw std::invalid_argument("Unsupported virtual regulator type");
            // BOB subtype 7 is the config peripheral. Record 20261026T180725Z
            // reads subtype 7 at bus 0, SID 1, peripherals 0xe4 and 0xe6.
            revision(v[0], v[1], v[2], type, type == 3 ? 0xa : type == 4 ? 0x30 : 7);
            const auto base = v[2] << 8;
            regulators_.emplace(key(v[0], v[1], base), type);
            // Rails start disabled, with no voltage request and no READY state.
            // ULS thresholds and phase controls retain the guest's requests.
            // ULS occupies 39 and PHASE_CTL occupies 98.
            for (unsigned offset : {0x39, 0x3a, 0x40, 0x41, 0x42, 0x43, 0x45,
                                    0x46, 0x47, 0x48, 0x4a, 0x50, 0x51, 0x60,
                                    0x88, 0x89, 0x8b, 0x98, 0x99, 0xa0})
                add(v[0], v[1], base + offset, 0,
                    offset == 0x46 ? 0x80 : 0xff);
            for (unsigned offset : {8, 9, 0xb})
                add(v[0], v[1], base + offset, 0, 0);
            // STATUS3 bit0 declares LDO retention-control capability, not READY.
            // Selected post-driver op6 reads this bit before its control write.
            add(v[0], v[1], base + 0xa, type == 4 ? 1 : 0, 0);
            // XBL SMPS driver 0x14882e34 reads the unified voltage limit.
            // A unified rail uses two bytes there. OEM records 0x148fb0ac and
            // 0x148f9f60 give 0xe468 and 0xe669 as writable BOB controls.
            // Run532 writes 0xe468.
            const unsigned limit = type == 0x29 ? 112 : 3600;
            const unsigned limit_mask = type == 0x29 ? 0xff : 0;
            add(v[0], v[1], base + 0x68, limit & 255, limit_mask);
            add(v[0], v[1], base + 0x69, limit >> 8, limit_mask);
        });
        rows<6>(pbs_peripherals.get_value(), [this](const auto& v) {
            require_peripheral(v[0], v[1], v[2]);
            if (v[3] > 0xffff || !pbs_.try_emplace(key(v[0], v[1], v[2] << 8),
                    QcomPmicPbs::Config{static_cast<std::uint16_t>(v[3]), v[4], v[5]}).second)
                throw std::invalid_argument("Duplicate or invalid virtual PBS group");
        });
        // These PON aliases occupy offsets 10..1b.
        // This policy has no input producers. Configuration cannot create events.
        rows<3>(disconnected_irq_peripherals.get_value(), [this](const auto& v) {
            require_peripheral(v[0], v[1], v[2]);
            const auto base = v[2] << 8;
            disconnected_irq(v[0], v[1], base);
        });
        // Each declared target consumes its own peripheral's a5 unlock.
        // The selected PSI programs unlock SID7:2442 through SID7:24d0.
        rows<4>(protected_controls.get_value(), [this](const auto& v) {
            require_peripheral(v[0], v[1], v[2] >> 8);
            if (v[2] > 0xffff || (v[2] & 255) == 0xd0 || !v[3] || v[3] > 255)
                throw std::invalid_argument("Invalid protected PMIC control");
            add(v[0], v[1], v[2], 0, v[3], false, ByteKind::protected_control);
            add(v[0], v[1], (v[2] & ~255U) + 0xd0, 0, 0xff, false, ByteKind::security_key);
        });
        if (adc_mailbox.get_value()) {
            if (battery_millivolts.get_value() > 11250 ||
                battery_id_ohms.get_value() > 0x7fffffffU)
                throw std::invalid_argument("Virtual ADC stimulus is out of range");
            // The optional service requires an explicitly declared SDAM bank.
            for (unsigned offset : {0x45, 0x46, 0x5c, 0x5d}) {
                const auto found = bytes_.find(key(0, 0, 0x9000 + offset));
                if (found == bytes_.end() || !found->second.retained)
                    throw std::invalid_argument("Virtual ADC requires SDAM90 storage");
                found->second.mask = 0;
                found->second.retained = false;
            }
            add(0, 0, 0x90e5, 0, 1);
        }
        rows<5>(registers.get_value(), [this](const auto& v) {
            if (v[0] > 1 || v[1] > 15 || v[2] > 0xffff ||
                !present_[v[0] * 16 + v[1]])
                throw std::invalid_argument("Virtual PMIC byte requires a populated SID");
            const auto found = bytes_.find(key(v[0], v[1], v[2]));
            if (found != bytes_.end() && found->second.retained &&
                v[3] == found->second.reset && v[4] <= 255 &&
                !(v[4] & ~found->second.mask)) {
                // Raw board controls can restrict an SDAM byte's write mask.
                // They cannot replace its cold contents or widen permissions.
                found->second.mask = static_cast<std::uint8_t>(v[4]);
            } else {
                add(v[0], v[1], v[2], v[3], v[4]);
            }
        });
        for (const auto& entry : bytes_) {
            declared_peripherals_.set(entry.first >> 8);
            if (pbs_.count(entry.first & ~255U) &&
                QcomPmicPbs::owns_register(entry.first & 255))
                throw std::invalid_argument("Sparse PMIC byte overlaps the PBS interface");
        }
        for (const auto& entry : pbs_) declared_peripherals_.set(entry.first >> 8);
        // The devcfg bitmap is a runtime blob, so the platform either lists the
        // groups or selects every group that this model treats as present.
        // A group takes part only when its presence byte at offset four reads
        // nonzero, which is the same probe that QSEE uses before a dump.
        const auto dump_policy = dump_peripherals.get_value();
        if (dump_policy == "present") {
            for (unsigned bus = 0; bus < 2; ++bus) {
                for (unsigned sid = 0; sid < 14; ++sid) {
                    if (!present_[bus * 16 + sid]) continue;
                    for (unsigned peripheral = 2; peripheral < 256; ++peripheral)
                        if (presence_byte(bus, sid, peripheral))
                            dump_observed_.insert((bus * 16 + sid) * 256 + peripheral);
                }
            }
        } else rows<3>(dump_policy, [this](const auto& v) {
            if (v[0] > 1 || v[1] > 15 || v[2] < 2 || v[2] > 255)
                throw std::invalid_argument("Invalid virtual dump peripheral");
            if (!present_[v[0] * 16 + v[1]] || !presence_byte(v[0], v[1], v[2])) return;
            dump_observed_.insert((v[0] * 16 + v[1]) * 256 + v[2]);
        });
    }

    std::uint8_t value(std::uint32_t address) const {
        return bytes_.at(address).value;
    }

    // Validate services before the transport commits any byte of a burst.
    bool validate_services(std::uint32_t address, unsigned count,
                           const unsigned char* data) const {
        const auto base = address & ~0xffU;
        const auto rail = regulators_.find(base);
        if (rail != regulators_.end()) {
            const auto proposed = [this, address, count, data](std::uint32_t a) {
                return a >= address && a - address < count ? data[a - address] : value(a);
            };
            const auto voltage_offset = rail->second == 0x29 ? 0x42U : 0x40U;
            const unsigned voltage = proposed(base + voltage_offset) |
                                     (proposed(base + voltage_offset + 1) << 8);
            if (voltage > (rail->second == 0x29 ? 3584U : 3600U) ||
                ((proposed(base + 0x46) & 0x80) && !voltage))
                return false;
        }
        if (adc_mailbox.get_value() && address <= 0x90e5 && 0x90e5 - address < count) {
            if (count != 1 || data[0] != 1) return false;
            const unsigned command = value(0x9050);
            if (!command) return value(0x904b) == 1;
            if (command != 0x80 || value(0x904f) != 0x27 || value(0x9051) != 1 ||
                value(0x9046) || value(0x9053) != 0x80 || value(0x9056))
                return false;
            const unsigned channel = value(0x9054);
            return (channel == 0x8f && value(0x9052) == 0x24 && !value(0x9055)) ||
                   ((channel == 0x45 || channel == 0x4d) &&
                    value(0x9052) == 0x14 && value(0x9055) == 1);
        }
        return true;
    }

    void complete_services(std::uint32_t address, unsigned count) {
        if (!adc_mailbox.get_value() || address > 0x90e5 || 0x90e5 - address >= count)
            return;
        bytes_.at(0x90e5).value = 0; // Synchronous request consumption, not PBS execution.
        if (!value(0x9050)) {
            bytes_.at(0x9046).value = 0;
            bytes_.at(0x904b).value = 0;
            return;
        }
        const auto channel = value(0x9054);
        const std::uint64_t resistance = battery_id_ohms.get_value();
        const auto code = channel == 0x8f ?
            (std::uint64_t(battery_millivolts.get_value()) * 1000 * 28900 + 5625000) /
                11250000 :
            channel == 0x4d ? 32767 : 65535 * resistance / (resistance + 100000);
        bytes_.at(0x905c).value = static_cast<std::uint8_t>(code & 255);
        bytes_.at(0x905d).value = static_cast<std::uint8_t>(code >> 8);
        bytes_.at(0x9046).value = 1;
    }

    std::uint8_t read_byte(std::uint32_t address, const Byte& byte) const {
        if (byte.kind == ByteKind::irq_enable_clear)
            return value((address & ~255U) + 0x15);
        if (adc_mailbox.get_value() && address == 0x9045)
            return 1; // Declared synchronous service is idle, even with a retained EOC.
        const auto rail = regulators_.find(address & ~0xffU);
        if (rail != regulators_.end() && (address & 255) == 8)
            return value((address & ~0xffU) + 0x46) & 0x80;
        if (gpios_.count(address & ~0xffU) &&
            ((address & 255) == 8 || (address & 255) == 0x10)) {
            const auto base = address & ~0xffU;
            const auto mode = value(base + 0x40) & 3;
            const bool output = mode == 1;
            const bool enabled = value(base + 0x46) & 0x80;
            const auto source = value(base + 0x44);
            const bool software = (source & 0x0f) == 0;
            // An open input follows its programmed pull. Encodings 0..3 pull up,
            // 4 pulls down, and 5 has no pull (the deterministic low policy).
            // XBL 1489821c reads IRQ_RT_STS, then inverts this active-low key.
            const bool high = enabled && (output && software ?
                (source & 0x80) != 0 : mode == 0 && (value(base + 0x42) & 7) < 4);
            if ((address & 255) == 0x10) return high;
            return (output && enabled && software ? 0x80 : 0) | high;
        }
        return byte.value;
    }

    void write_byte(std::uint32_t address, Byte& byte, std::uint8_t data) {
        switch (byte.kind) {
        case ByteKind::irq_latched_clear:
            break; // No connected input can set the latch.
        case ByteKind::irq_enable_set:
            byte.value |= data;
            break;
        case ByteKind::irq_enable_clear:
            bytes_.at((address & ~255U) + 0x15).value &= static_cast<std::uint8_t>(~data);
            break;
        case ByteKind::security_key:
            byte.value = data == 0xa5;
            break;
        case ByteKind::protected_control:
            bytes_.at((address & ~255U) + 0xd0).value = 0;
            byte.value = data;
            break;
        case ByteKind::plain:
            byte.value = data;
            break;
        }
    }

    static void report_dump_read(unsigned bus, unsigned sid, unsigned address) {
        std::cerr << "PMIC_DUMP_READ_UNDECLARED bus=" << bus << " sid=" << sid
                  << " address=0x" << std::hex << (address & 0xffff) << std::dec << '\n';
    }

    static void report_scan_read(unsigned bus, unsigned sid, unsigned address) {
        std::cerr << "PMIC_SCAN_READ_UNDECLARED bus=" << bus << " sid=" << sid
                  << " address=0x" << std::hex << (address & 0xffff) << std::dec
                  << " policy=declared-zero\n";
    }

    void report_rejection(const tlm::tlm_generic_payload& tx,
                          const char* reason, unsigned detail = 0) {
        const auto address = tx.get_address();
        std::cerr << "UNMODELED_PMIC bus=" << (address >> 20)
                  << " sid=" << ((address >> 16) & 15)
                  << " address=0x" << std::hex << (address & 0xffff) << std::dec
                  << " width=" << tx.get_data_length()
                  << " operation=" << (tx.is_read() ? "read" : "write")
                  << " reason=" << reason << " detail=" << detail << '\n';
    }

    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return;
        }
        const auto a = tx.get_address();
        const unsigned count = tx.get_data_length();
        if (!tx.get_data_ptr() || !count || count > 8 || tx.get_streaming_width() < count ||
            a > 0x1fffff || (a & 0xff) + count > 256) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        const auto bus = static_cast<unsigned>(a >> 20);
        const auto sid = static_cast<unsigned>((a >> 16) & 15);
        if (sid < 14) {
            transfer(tx, true);
            return;
        }
        if (!tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            report_rejection(tx, "broadcast-read");
            return;
        }
        // The primary and secondary SID globals select groups 14/15.
        // Identity spans define the virtual primary and secondary members.
        tlm::tlm_generic_payload request;
        request.set_command(tlm::TLM_WRITE_COMMAND);
        request.set_data_ptr(tx.get_data_ptr());
        request.set_data_length(count);
        request.set_streaming_width(count);
        bool populated = false;
        for (const bool commit : {false, true}) {
            for (unsigned member = 0; member < 14; ++member) {
                if (!present_[bus * 16 + member] || sid_bank_[bus * 16 + member] != sid - 14)
                    continue;
                populated = true;
                request.set_address(key(bus, member, a & 0xffff));
                transfer(request, commit);
                if (request.is_response_error()) {
                    tx.set_response_status(request.get_response_status());
                    return;
                }
            }
        }
        tx.set_response_status(populated ? tlm::TLM_OK_RESPONSE : tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }

    void transfer(tlm::tlm_generic_payload& tx, bool commit) {
        const auto a = tx.get_address();
        const unsigned count = tx.get_data_length();
        const auto bus = static_cast<unsigned>(a >> 20);
        const auto sid = static_cast<unsigned>((a >> 16) & 15);
        if (!present_[bus * 16 + sid]) {
            // Only an absent SID becomes an in-band transport NACK.
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const auto peripheral = static_cast<unsigned>(a) >> 8;
        if (virtual_absent_discovery.get_value() && tx.is_read() && !(a & 255) && count == 7 &&
            !declared_peripherals_.test(peripheral) && !exploratory_written_.test(peripheral)) {
            for (unsigned i = 0; i < count; ++i) tx.get_data_ptr()[i] = 0;
            if (!reported_absences_.test(peripheral)) {
                reported_absences_.set(peripheral);
                std::cerr << "UNMODELED_PMIC_ABSENT bus=" << bus << " sid=" << sid
                          << " peripheral=0x" << std::hex << (peripheral & 255)
                          << std::dec << " policy=virtual-absent\n";
            }
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        auto& locks = locks_[static_cast<unsigned>(a) >> 8];
        const unsigned offset = static_cast<unsigned>(a) & 255;
        if (offset >= 0xd1 && offset + count <= 0xd4) {
            for (unsigned i = 0; tx.is_write() && i < count; ++i) {
                const auto next = tx.get_data_ptr()[i];
                if (next > 3 || next == 1) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    report_rejection(tx, "peripheral-lock-transition", i);
                    return;
                }
            }
            if (commit) {
                for (unsigned i = 0; i < count; ++i) {
                    auto& state = locks[offset - 0xd1 + i];
                    if (tx.is_read()) tx.get_data_ptr()[i] = state;
                    else if (!(state & 2)) state = tx.get_data_ptr()[i];
                }
            }
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        // Closed latches protect modeled secure controls, not ordinary configuration.
        // Native PSI continues ordinary writes after it programs a closed latch.
        const bool locked = tx.is_write() &&
                            (locks[0] == 3 || locks[1] == 3 || locks[2] == 3);
        // A dump observes data. It cannot grant a write or a control decision.
        const bool observing = tx.is_read() && tx.get_extension<PmicObserverExtension>() &&
            dump_observed_.count((bus * 16 + sid) * 256 + peripheral);
        const auto pbs = pbs_.find(static_cast<std::uint32_t>(a) & ~255U);
        const unsigned burst_offset = static_cast<unsigned>(a) & 255;
        // QSEE reads each present peripheral block in eight-byte bursts from
        // offset 0x08 to the end of the block on an unmarked path. The reader
        // stores the bytes and makes no decision from them. A byte without an
        // owner reads cold zero and reports one line, like the devcfg dump path.
        const bool scanning = tx.is_read() && scan_block_reads.get_value() &&
            count == 8 && burst_offset >= 0x08 &&
            peripheral >= 2 && !observing &&
            declared_peripherals_.test(peripheral);
        if (pbs != pbs_.end()) {
            if (!commit) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                report_rejection(tx, "broadcast-pbs");
                return;
            }
            bool owned = false;
            for (unsigned i = 0; i < count; ++i)
                owned |= QcomPmicPbs::owns_register(burst_offset + i);
            if (owned && observing) {
                // The dump reads eight bytes at once. Resolve each interface byte
                // alone, and report the bytes that no register answers.
                const auto offset = burst_offset;
                for (unsigned i = 0; i < count; ++i) {
                    std::uint8_t value = 0;
                    const auto reg = offset + i;
                    if (QcomPmicPbs::owns_register(reg) &&
                        pbs->second.read(reg, &value, 1) == QcomPmicPbs::Result::ok) {
                        tx.get_data_ptr()[i] = value;
                        continue;
                    }
                    tx.get_data_ptr()[i] = 0;
                    report_dump_read(bus, sid, static_cast<unsigned>(a) + i);
                }
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
            // The scan burst reaches interface bytes and a RAM port that no
            // transfer serves in this state. Answer each byte from the register
            // view, and never move the transfer pointer. An upload readback
            // therefore keeps the real RAM contents.
            if (scanning && !pbs->second.transfer_shaped(false, burst_offset, count)) {
                for (unsigned i = 0; i < count; ++i) {
                    const unsigned reg = burst_offset + i;
                    const auto address = static_cast<std::uint32_t>(a) + i;
                    const auto declared = bytes_.find(address);
                    std::uint8_t value = 0;
                    if (declared != bytes_.end())
                        tx.get_data_ptr()[i] = read_byte(address, declared->second);
                    else if (reg != 0x4c && reg != 0x88 && QcomPmicPbs::owns_register(reg) &&
                             pbs->second.read(reg, &value, 1) == QcomPmicPbs::Result::ok)
                        tx.get_data_ptr()[i] = value;
                    else {
                        tx.get_data_ptr()[i] = 0;
                        report_scan_read(bus, sid, static_cast<unsigned>(a) + i);
                    }
                }
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
                return;
            }
            if (owned) {
                const auto offset = burst_offset;
                if (locked && (offset == 0x48 || offset == 0x80)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    report_rejection(tx, "locked-pbs-memory");
                    return;
                }
                const auto result = tx.is_read() ?
                    pbs->second.read(offset, tx.get_data_ptr(), count) :
                    pbs->second.write(offset, tx.get_data_ptr(), count);
                tx.set_response_status(result == QcomPmicPbs::Result::ok ?
                    tlm::TLM_OK_RESPONSE : tlm::TLM_COMMAND_ERROR_RESPONSE);
                if (result != QcomPmicPbs::Result::ok)
                    report_rejection(tx, "pbs-interface", static_cast<unsigned>(result));
                return;
            }
        }
        std::array<Byte*, 8> selected{};
        std::array<bool, 8> observed{};
        std::array<bool, 8> scanned{};
        bool substituted = false;
        for (unsigned i = 0; i < count; ++i) {
            const auto found = bytes_.find(static_cast<std::uint32_t>(a + i));
            const unsigned address = static_cast<unsigned>(a + i);
            const unsigned offset = address & 255;
            if (found == bytes_.end() && (observing || scanning)) {
                // The dump reads the whole peripheral. A byte without an
                // owner returns its cold value and reports one line per byte.
                observed[i] = observing;
                scanned[i] = scanning;
                continue;
            }
            if (found == bytes_.end() && !exploratory_.empty() &&
                ((address >> 8) & 255) >= 2 && offset >= 0x40 && offset < 0xd0 &&
                pbs == pbs_.end()) {
                substituted = true;
                continue;
            }
            if (found == bytes_.end() ||
                (tx.is_read() && found->second.kind == ByteKind::security_key) ||
                (tx.is_write() &&
                 (!found->second.mask || ((found->second.value ^ tx.get_data_ptr()[i]) &
                                          ~found->second.mask) ||
                  (found->second.kind == ByteKind::protected_control &&
                   (locked || !value((static_cast<std::uint32_t>(a + i) & ~255U) + 0xd0)))))) {
                // Validate the complete burst before changing any byte.
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                report_rejection(tx, found == bytes_.end() ? "unknown-byte" : "access-policy", i);
                return;
            }
            selected[i] = &found->second;
        }
        if (tx.is_write() && !validate_services(static_cast<std::uint32_t>(a), count,
                                                tx.get_data_ptr())) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            report_rejection(tx, "service-contract");
            return;
        }
        if (!commit) {
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (substituted)
            std::cerr << "UNMODELED_PMIC_SUBSTITUTE bus=" << bus << " sid=" << sid
                      << " address=0x" << std::hex << (a & 0xffff) << std::dec
                      << " width=" << count << " operation=" << (tx.is_read() ? "read" : "write")
                      << " policy=retained-configuration reset=0\n";
        if (substituted && tx.is_write()) exploratory_written_.set(peripheral);
        for (unsigned i = 0; i < count; ++i) {
            if (observed[i]) report_dump_read(bus, sid, static_cast<unsigned>(a) + i);
            if (scanned[i]) report_scan_read(bus, sid, static_cast<unsigned>(a) + i);
        }
        for (unsigned i = 0; i < count; ++i) {
            if (!selected[i]) {
                if (tx.is_read())
                    tx.get_data_ptr()[i] = (observed[i] || scanned[i]) ?
                        0 : exploratory_[a + i];
                else exploratory_[a + i] = tx.get_data_ptr()[i];
            } else if (tx.is_read()) {
                tx.get_data_ptr()[i] = read_byte(static_cast<std::uint32_t>(a + i), *selected[i]);
            } else {
                write_byte(static_cast<std::uint32_t>(a + i), *selected[i], tx.get_data_ptr()[i]);
            }
        }
        if (tx.is_write()) complete_services(static_cast<std::uint32_t>(a), count);
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

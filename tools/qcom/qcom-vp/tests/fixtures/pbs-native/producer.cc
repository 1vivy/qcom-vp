// SPDX-License-Identifier: BSD-3-Clause-Clear
// Native fixture: compile with -std=c++17 and the repository models include path.
#include "qcom/spmi/pmic_pbs.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>

using Pbs = qcom_vp::QcomPmicPbs;
using Result = Pbs::Result;

static void setup(Pbs& pbs, std::uint8_t mode, unsigned address) {
    const std::array<std::uint8_t, 4> registers{
        0x80, mode, static_cast<std::uint8_t>(address),
        static_cast<std::uint8_t>(address >> 8)};
    assert(pbs.write(0x40, registers.data(), registers.size()) == Result::ok);
}

static unsigned pointer(Pbs& pbs) {
    std::array<std::uint8_t, 2> value{};
    assert(pbs.read(0x42, value.data(), value.size()) == Result::ok);
    return value[0] | (unsigned(value[1]) << 8);
}

int main() {
    // Synthetic bytes only. No vendor sequences or image information.
    Pbs pbs({0x0501, 0x100, 0xc000});
    std::array<std::uint8_t, 7> discovery{};
    const std::array<std::uint8_t, 7> expected_discovery{1, 5, 0, 1, 0x16, 1, 0};
    assert(pbs.read(0, discovery.data(), discovery.size()) == Result::ok);
    assert(discovery == expected_discovery);
    std::uint8_t dummy_write = 1;
    assert(pbs.write(6, &dummy_write, 1) == Result::unsupported);
    std::uint8_t alias = 0xcc;
    assert(pbs.read(0x49, &alias, 1) == Result::unsupported);
    assert(pbs.write(0x89, &alias, 1) == Result::unsupported);
    assert(alias == 0xcc);
    std::uint8_t pre_upload_active = 0xff, pre_upload_done = 0xff;
    assert(pbs.read(0x20, &pre_upload_active, 1) == Result::ok);
    assert(pbs.read(0x27, &pre_upload_done, 1) == Result::ok);
    // Selected native classification: (0,0) is state0, not completed state1.
    assert(pre_upload_active == 0 && pre_upload_done == 0);
    std::array<std::uint8_t, 2> loaded_version{0xaa, 0x55};
    const auto no_version = loaded_version;
    assert(pbs.read(0x28, loaded_version.data(), 2) == Result::unsupported);
    assert(loaded_version == no_version);
    assert(Pbs::owns_register(0x20) && Pbs::owns_register(0x27));
    assert(Pbs::owns_register(0x28) && Pbs::owns_register(0x29));
    assert(Pbs::owns_register(0xc3));
    const std::array<std::uint8_t, 8> first{0x12, 0xf0, 0x37, 0x91, 0, 0xaa, 0x55, 0xfe};
    const std::array<std::uint8_t, 8> second{7, 6, 5, 4, 3, 2, 1, 0};
    std::array<std::uint8_t, 16> output{};
    output.fill(0xcc);
    const auto untouched = output;
    assert(pbs.write(0x80, first.data(), first.size()) == Result::disabled);
    setup(pbs, 0xa0, 0x100);
    assert(pbs.read(0x88, output.data(), 8) == Result::ok);
    for (unsigned i = 0; i < 8; ++i) assert(output[i] == 0);
    assert(output[8] == untouched[8] && pointer(pbs) == 0x108);
    assert(pbs.write(0x80, first.data(), first.size()) == Result::wrong_mode);

    setup(pbs, 0xe0, 0x100);
    assert(pbs.write(0x80, first.data(), first.size()) == Result::ok);
    assert(pointer(pbs) == 0x108);
    // The selected packed opcode repeats offset80 for each eight-byte chunk.
    assert(pbs.write(0x80, second.data(), second.size()) == Result::ok);
    assert(pointer(pbs) == 0x110);
    // The independent pre-upload pulse neither changes guest SRAM nor starts
    // an autonomous producer. It cannot manufacture a loaded-version value.
    std::uint8_t pulse = 4, pulse_value = 0;
    assert(pbs.write(0xc3, &pulse, 1) == Result::ok);
    assert(pbs.read(0xc3, &pulse_value, 1) == Result::ok && pulse_value == 4);
    pulse = 5;
    assert(pbs.write(0xc3, &pulse, 1) == Result::unsupported);
    assert(pbs.read(0xc3, &pulse_value, 1) == Result::ok && pulse_value == 4);
    const std::array<std::uint8_t, 2> invalid_pulse_burst{};
    assert(pbs.write(0xc3, invalid_pulse_burst.data(), 2) == Result::unknown_register);
    assert(pbs.read(0xc3, &pulse_value, 1) == Result::ok && pulse_value == 4);
    assert(pointer(pbs) == 0x110);
    assert(pbs.read(0x41, &pulse_value, 1) == Result::ok && pulse_value == 0xe0);
    assert(pbs.read(0x40, &pulse_value, 1) == Result::ok && pulse_value == 0x80);
    pulse = 0;
    assert(pbs.write(0xc3, &pulse, 1) == Result::ok);
    assert(pbs.read(0xc3, &pulse_value, 1) == Result::ok && pulse_value == 0);
    assert(pbs.read(0x20, &pre_upload_active, 1) == Result::ok);
    assert(pbs.read(0x27, &pre_upload_done, 1) == Result::ok);
    assert(pre_upload_active == 0 && pre_upload_done == 0);
    assert(pbs.read(0x28, loaded_version.data(), 2) == Result::unsupported);
    assert(loaded_version == no_version);
    const std::array<std::uint8_t, 4> disable{};
    assert(pbs.write(0x40, disable.data(), disable.size()) == Result::ok);
    assert(pbs.read(0x88, output.data(), 8) == Result::disabled);
    setup(pbs, 0xa0, 0x100);
    assert(pbs.read(0x88, output.data(), output.size()) == Result::ok);
    for (unsigned i = 0; i < 8; ++i) {
        assert(output[i] == first[i]);
        assert(output[i + 8] == second[i]);
    }

    // Guest-written data and untouched SRAM share one coherent address space.
    setup(pbs, 0xa0, 0x108);
    assert(pbs.read(0x88, output.data(), output.size()) == Result::ok);
    for (unsigned i = 0; i < 8; ++i) {
        assert(output[i] == second[i]);
        assert(output[i + 8] == 0);
    }
    setup(pbs, 0xe0, 0xc0f8);
    assert(pbs.write(0x80, output.data(), output.size()) == Result::bounds);
    assert(pointer(pbs) == 0xc0f8);
    setup(pbs, 0xa0, 0xc0f8);
    assert(pbs.read(0x88, output.data(), 8) == Result::ok);
    for (unsigned i = 0; i < 8; ++i) assert(output[i] == 0);
    assert(pbs.write(0x80, first.data(), 8) == Result::wrong_mode);
    setup(pbs, 0xe0, 0x100);
    assert(pbs.write(0x80, first.data(), 4) == Result::invalid_transaction);
    assert(pbs.write(0x48, first.data(), 4) == Result::wrong_mode);
    setup(pbs, 0xa0, 0xf8);
    const auto before_bounds = output;
    assert(pbs.read(0x88, output.data(), 8) == Result::bounds);
    assert(output == before_bounds && pointer(pbs) == 0xf8);

    // Metadata describes declared virtual geometry only.
    std::uint8_t value = 0;
    assert(pbs.read(0xa0, &value, 1) == Result::ok && value == 0x80);
    assert(pbs.read(0xa1, &value, 1) == Result::ok && value == 1);
    assert(pbs.read(0xa3, &value, 1) == Result::ok && value == 8);
    assert(pbs.read(0xa4, &value, 1) == Result::ok && value == 0);
    assert(pbs.read(0x9d, &value, 1) == Result::unsupported);
    assert(pbs.read(0xf1, &value, 1) == Result::unknown_register);
    const std::array<std::uint8_t, 2> invalid_setup{0, 1};
    assert(pbs.write(0x40, invalid_setup.data(), 2) == Result::unsupported);
    assert(pbs.read(0x40, &value, 1) == Result::ok && value == 0x80);

    // Native four-byte FIFO reads remain valid under revision0501 modea0.
    // Each read uses four-byte alignment and advances by exactly four bytes.
    setup(pbs, 0xa0, 0x100);
    assert(pbs.read(0x4c, output.data(), 4) == Result::ok);
    assert(pointer(pbs) == 0x104);
    assert(pbs.read(0x4c, output.data() + 4, 4) == Result::ok);
    assert(pointer(pbs) == 0x108);
    for (unsigned i = 0; i < 8; ++i) assert(output[i] == first[i]);
    setup(pbs, 0xa0, 0x104);
    assert(pbs.read(0x88, output.data(), 8) == Result::bounds);
    assert(pointer(pbs) == 0x104);
    setup(pbs, 0xa0, 0xc0fc);
    assert(pbs.read(0x4c, output.data(), 4) == Result::ok);
    assert(pointer(pbs) == 0xc100);
    for (unsigned i = 0; i < 4; ++i) assert(output[i] == 0);
    const auto before_end = output;
    assert(pbs.read(0x4c, output.data(), 4) == Result::bounds);
    assert(output == before_end && pointer(pbs) == 0xc100);

    pulse = 4;
    assert(pbs.write(0xc3, &pulse, 1) == Result::ok);
    pbs.warm_reset();
    assert(pbs.read(0xc3, &pulse_value, 1) == Result::ok && pulse_value == 0);
    assert(pbs.read(0x88, output.data(), 8) == Result::disabled);
    setup(pbs, 0xa0, 0x100);
    assert(pbs.read(0x88, output.data(), 8) == Result::ok);
    for (unsigned i = 0; i < 8; ++i) assert(output[i] == first[i]);
    pbs.cold_reset();
    assert(pbs.read(0x88, output.data(), 8) == Result::disabled);
    setup(pbs, 0xa0, 0x100);
    assert(pbs.read(0x88, output.data(), 8) == Result::ok);
    for (unsigned i = 0; i < 8; ++i) assert(output[i] == 0);

    // Legacy FIFO lifecycle and single-word mode do not use wide aliases.
    Pbs legacy({0x0301, 0, 128});
    setup(legacy, 0xc0, 0);
    assert(legacy.write(0x48, first.data(), 4) == Result::ok);
    assert(legacy.write(0x48, first.data() + 4, 4) == Result::ok);
    assert(pointer(legacy) == 8);
    setup(legacy, 0x80, 0);
    assert(legacy.read(0x4c, output.data(), 4) == Result::ok);
    assert(legacy.read(0x4c, output.data() + 4, 4) == Result::ok);
    for (unsigned i = 0; i < 8; ++i) assert(output[i] == first[i]);
    setup(legacy, 0, 0);
    assert(legacy.read(0x4c, output.data(), 4) == Result::ok);
    assert(pointer(legacy) == 0);
    value = 0xe0;
    assert(legacy.write(0x41, &value, 1) == Result::unsupported);

    bool rejected = false;
    try { Pbs invalid({0x0501, 0, 65536}); }
    catch (const std::invalid_argument&) { rejected = true; }
    // The decoder uses only the low byte when it is <=8.
    // Do not advertise a capacity that its consumer would decode as zero.
    assert(rejected);

    // Largest selected upload: 0x2fc8 four-byte words. Exercise the actual
    // eight-byte transport chunks, not a direct backing-store shortcut.
    Pbs large({0x0501, 0, 0xc000});
    constexpr unsigned upload_bytes = 0x2fc8 * 4;
    std::array<std::uint8_t, 8> chunk{};
    setup(large, 0xe0, 0);
    for (unsigned address = 0; address < upload_bytes; address += 8) {
        for (unsigned i = 0; i < 8; ++i)
            chunk[i] = static_cast<std::uint8_t>((address + i) ^ ((address + i) >> 8));
        assert(large.write(0x80, chunk.data(), chunk.size()) == Result::ok);
    }
    assert(pointer(large) == upload_bytes);
    assert(large.write(0x40, disable.data(), disable.size()) == Result::ok);
    setup(large, 0xa0, 0);
    for (unsigned address = 0; address < upload_bytes; address += 8) {
        assert(large.read(0x88, chunk.data(), chunk.size()) == Result::ok);
        for (unsigned i = 0; i < 8; ++i)
            assert(chunk[i] == static_cast<std::uint8_t>(
                (address + i) ^ ((address + i) >> 8)));
    }
    assert(pointer(large) == upload_bytes);
    // Declared capacity is real SRAM, not an image-version special case.
    setup(large, 0xa0, 0xbffc);
    assert(large.read(0x4c, chunk.data(), 4) == Result::ok);
    for (unsigned i = 0; i < 4; ++i) assert(chunk[i] == 0);
}

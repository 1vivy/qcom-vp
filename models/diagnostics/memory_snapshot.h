// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_initiator_socket.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace qcom_vp {

// Capture one bounded guest range after simulation stops.
class MemorySnapshot final : public sc_core::sc_module {
public:
    tlm_utils::simple_initiator_socket<MemorySnapshot> initiator_socket{"initiator_socket"};
    cci::cci_param<std::uint64_t> address{"address", 0, "First guest address"};
    cci::cci_param<std::uint64_t> size{"size", 0, "Snapshot size in bytes"};
    cci::cci_param<std::string> outfile{"outfile", "", "Snapshot output path"};
    cci::cci_param<std::uint64_t> capture_time_ms{
        "capture_time_ms", 0, "Optional simulation time for an early snapshot"};

    SC_HAS_PROCESS(MemorySnapshot);
    explicit MemorySnapshot(sc_core::sc_module_name name) : sc_module(name) {
        SC_METHOD(capture);
        sensitive << capture_event_;
        dont_initialize();
    }

private:
    static constexpr std::size_t chunk_size = 4096;
    sc_core::sc_event capture_event_;
    bool saved_ = false;

    void end_of_elaboration() override {
        const auto bytes = size.get_value();
        if (!bytes || bytes > 1024 * 1024 || outfile.get_value().empty())
            throw std::invalid_argument("Memory snapshot requires a path and one to 1048576 bytes");
        if (address.get_value() > UINT64_MAX - bytes)
            throw std::invalid_argument("Memory snapshot range overflows");
    }
    void start_of_simulation() override {
        if (capture_time_ms.get_value())
            capture_event_.notify(sc_core::sc_time(capture_time_ms.get_value(), sc_core::SC_MS));
    }

    void end_of_simulation() override { capture(); }
    void capture() {
        if (saved_) return;
        std::ofstream output(outfile.get_value(), std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "MEMORY_SNAPSHOT_FAILED model=" << name() << " reason=open\n";
            return;
        }
        std::array<unsigned char, chunk_size> bytes{};
        std::uint64_t offset = 0;
        while (offset < size.get_value()) {
            const auto count = static_cast<unsigned>(
                std::min<std::uint64_t>(bytes.size(), size.get_value() - offset));
            tlm::tlm_generic_payload tx;
            tx.set_command(tlm::TLM_READ_COMMAND);
            tx.set_address(address.get_value() + offset);
            tx.set_data_ptr(bytes.data());
            tx.set_data_length(count);
            tx.set_streaming_width(count);
            if (initiator_socket->transport_dbg(tx) != count) {
                std::cerr << "MEMORY_SNAPSHOT_FAILED model=" << name()
                          << " reason=read address=0x" << std::hex
                          << address.get_value() + offset << std::dec << '\n';
                return;
            }
            output.write(reinterpret_cast<const char*>(bytes.data()), count);
            if (!output) {
                std::cerr << "MEMORY_SNAPSHOT_FAILED model=" << name() << " reason=write\n";
                return;
            }
            offset += count;
        }
        output.close();
        if (!output) {
            std::cerr << "MEMORY_SNAPSHOT_FAILED model=" << name() << " reason=close\n";
            return;
        }
        std::cerr << "MEMORY_SNAPSHOT_SAVED model=" << name() << " address=0x" << std::hex
                  << address.get_value() << std::dec << " bytes=" << size.get_value() << '\n';
        saved_ = true;
    }
};

} // namespace qcom_vp

// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include "qcom/sm88xx/cpucp/clock_roots.h"
#include "virtual_dram.h"
#include "command_engine.h"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>

namespace qcom_vp {

// Functional programming device with an explicit retained configuration policy.
class QcomDdrPhyProgram : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<QcomDdrPhyProgram> target_socket{"target_socket"};
    cci::cci_param<std::string> configuration_offsets{"configuration_offsets", "", "Accepted relative configuration words"};
    cci::cci_param<std::string> required_offsets{"required_offsets", "", "Configuration words required before commands"};
    cci::cci_param<std::string> pll_path{"pll_path", "", "Optional authoritative PLL source path"};
    cci::cci_param<std::string> command_offsets{"command_offsets", "", "Relative synchronous command words"};
    cci::cci_param<std::string> read_only_offsets{"read_only_offsets", "", "Relative idle-zero observation words"};
    cci::cci_param<std::string> source_paths{"source_paths", "", "Required live programming sources, comma-separated"};
    cci::cci_param<std::uint32_t> completion_mask{"completion_mask", 0, "Synchronous accepted-command acknowledgment"};
    cci::cci_param<bool> ordered_commands{"ordered_commands", false, "Require each previous command to complete first"};
    cci::cci_param<unsigned> configuration_banks{"configuration_banks", 1, "Number of independent configuration banks"};
    cci::cci_param<std::uint32_t> retained_extent{"retained_extent", 0, "Logged retained configuration extent in bytes"};
    cci::cci_param<bool> pxi_frontend{"pxi_frontend", false, "Execute the functional PXI packet frontend"};
    cci::cci_param<std::string> pxi_backend_path{"pxi_backend_path", "", "Shared virtual DRAM owner for PXI traffic"};
    cci::cci_param<std::string> command_engine_path{"command_engine_path", "", "Shared command engine for LLC runtime policies"};
    cci::cci_param<bool> mc_calibration{"mc_calibration", false, "Execute ideal analog calibration after publication"};

    explicit QcomDdrPhyProgram(sc_core::sc_module_name name) : sc_module(name) {
        target_socket.register_b_transport(this, &QcomDdrPhyProgram::transport);
    }
    void cold_reset() {
        std::fill(values_.begin(), values_.end(), Value{});
        std::fill(completed_.begin(), completed_.end(), false);
        std::fill(pxi_.begin(), pxi_.end(), PxiState{});
        std::fill(runtime_.begin(), runtime_.end(), RuntimeState{});
        std::fill(calibration_.begin(), calibration_.end(), CalibrationState{});
    }
    bool programming_ready() {
        for (unsigned bank = 0; bank < configuration_banks.get_value(); ++bank)
            if (!configured(bank)) return false;
        return std::all_of(completed_.begin(), completed_.end(), [](bool value) { return value; });
    }

private:
    struct Word { std::uint32_t offset; bool declared; };
    struct Value { std::uint32_t value = 0; bool written = false; };
    std::vector<Word> words_;
    std::vector<Value> values_;
    std::vector<std::uint32_t> required_, commands_, read_only_;
    std::vector<bool> completed_;
    struct RuntimeState { bool publication_ready = false, published = false; };
    std::vector<RuntimeState> runtime_;
    struct CalibrationState {
        bool pending = false, complete = false, dedicated_status = false;
        std::uint32_t measurement = 0;
    };
    std::vector<CalibrationState> calibration_;
    QcomDdrCommandEngine* command_engine_ = nullptr;
    struct PxiState {
        std::uint32_t request = 0;
        std::uint32_t idle = 0x1f;
        std::array<std::uint32_t, 2> errors{};
        std::uint32_t beats = 0;
    };
    std::vector<PxiState> pxi_;
    std::vector<QcomDdrPhyProgram*> sources_;
    PllSource* plls_ = nullptr;
    QcomVirtualDdr* pxi_backend_ = nullptr;

    static std::vector<std::uint32_t> parse(const std::string& text) {
        std::vector<std::uint32_t> result;
        std::istringstream input(text);
        std::string token;
        while (std::getline(input, token, ',')) {
            std::size_t used = 0;
            const auto value = std::stoull(token, &used, 0);
            if (used != token.size() || value > 0xffff || (value & 3) ||
                std::find(result.begin(), result.end(), value) != result.end())
                throw std::invalid_argument("Invalid DDR PHY word list");
            result.push_back(static_cast<std::uint32_t>(value));
        }
        return result;
    }
    auto word(std::uint64_t offset) {
        return std::lower_bound(words_.begin(), words_.end(), offset,
            [](const Word& entry, std::uint64_t address) { return entry.offset < address; });
    }
    bool locally_configured(unsigned bank) {
        for (const auto offset : required_) {
            const auto entry = word(offset);
            if (!values_[bank * words_.size() + (entry - words_.begin())].written) return false;
        }
        return true;
    }
    bool configured(unsigned bank) {
        if (!locally_configured(bank)) return false;
        // The reference handshake uses the live input, not an unproven PLL output.
        if (plls_ && !plls_->reference_rate_hz()) return false;
        for (auto* source : sources_) if (!source->programming_ready()) return false;
        return true;
    }
    void end_of_elaboration() override {
        const auto banks = configuration_banks.get_value();
        if (banks != 1 && banks != 4)
            throw std::invalid_argument("DDR PHY requires one or four configuration banks");
        const auto offsets = parse(configuration_offsets.get_value());
        const auto extent = retained_extent.get_value();
        if ((extent & 3) || extent > 0x10000)
            throw std::invalid_argument("Invalid DDR retained configuration extent");
        if (extent) {
            words_.reserve(extent / 4);
            for (std::uint32_t offset = 0; offset < extent; offset += 4)
                words_.push_back({offset, false});
            for (const auto offset : offsets) {
                if (offset >= extent)
                    throw std::invalid_argument("DDR configuration word exceeds retained extent");
                words_[offset / 4].declared = true;
            }
        } else {
            words_.reserve(offsets.size());
            for (const auto offset : offsets) words_.push_back({offset, true});
            std::sort(words_.begin(), words_.end(), [](const Word& a, const Word& b) { return a.offset < b.offset; });
        }
        values_.resize(banks * words_.size());
        if (pxi_frontend.get_value() && extent != 0x10000)
            throw std::invalid_argument("DDR PXI requires the complete retained LLC aperture");
        pxi_.resize(pxi_frontend.get_value() ? banks : 0);
        if (mc_calibration.get_value()) {
            if (banks != 4 || extent != 0x10000)
                throw std::invalid_argument("DDR MC calibration requires four complete banks");
            calibration_.resize(banks);
        }
        required_ = parse(required_offsets.get_value());
        if (!pll_path.get_value().empty())
            plls_ = clock_detail::resolve_plls(*this, pll_path.get_value());
        commands_ = parse(command_offsets.get_value());
        read_only_ = parse(read_only_offsets.get_value());
        completed_.resize(banks * commands_.size());
        for (const auto offset : required_) {
            const auto entry = word(offset);
            if (entry == words_.end() || entry->offset != offset || !entry->declared)
                throw std::invalid_argument("DDR PHY prerequisite must be a configuration word");
        }
        for (const auto offset : commands_) {
            const auto entry = word(offset);
            if ((entry != words_.end() && entry->offset == offset && entry->declared) ||
                std::find(read_only_.begin(), read_only_.end(), offset) != read_only_.end())
                throw std::invalid_argument("DDR PHY command overlaps another word");
        }
        for (const auto offset : read_only_) {
            const auto entry = word(offset);
            if (entry != words_.end() && entry->offset == offset && entry->declared)
                throw std::invalid_argument("DDR PHY observation overlaps configuration");
        }
        if (completion_mask.get_value() != 0 && completion_mask.get_value() != 0x10)
            throw std::invalid_argument("Unsupported DDR PHY acknowledgment field");
        std::istringstream input(source_paths.get_value());
        std::string path;
        while (std::getline(input, path, ',')) {
            auto* source = dynamic_cast<QcomDdrPhyProgram*>(sc_core::sc_find_object(path.c_str()));
            if (!source && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                source = dynamic_cast<QcomDdrPhyProgram*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!source || source == this)
                throw std::invalid_argument("DDR PHY requires a distinct live programming source");
            sources_.push_back(source);
        }
        if (pxi_frontend.get_value()) {
            const auto path = pxi_backend_path.get_value();
            pxi_backend_ = dynamic_cast<QcomVirtualDdr*>(sc_core::sc_find_object(path.c_str()));
            if (!pxi_backend_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                pxi_backend_ = dynamic_cast<QcomVirtualDdr*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!pxi_backend_) throw std::invalid_argument("DDR PXI requires the shared DRAM backend");
        }
        if (!command_engine_path.get_value().empty()) {
            const auto& path = command_engine_path.get_value();
            command_engine_ = dynamic_cast<QcomDdrCommandEngine*>(sc_core::sc_find_object(path.c_str()));
            if (!command_engine_ && get_parent_object()) {
                const auto relative = std::string(get_parent_object()->name()) + "." + path;
                command_engine_ = dynamic_cast<QcomDdrCommandEngine*>(sc_core::sc_find_object(relative.c_str()));
            }
            if (!command_engine_ || banks != 4 || extent != 0x10000 ||
                std::find(commands_.begin(), commands_.end(), 0x5020) == commands_.end() ||
                std::find(commands_.begin(), commands_.end(), 0x6020) == commands_.end())
                throw std::invalid_argument("DDR runtime policy requires four LLC banks and a command engine");
            runtime_.resize(banks);
        }
        cold_reset();
    }
    std::uint32_t pxi_word(unsigned bank, unsigned offset) const {
        return values_[bank * words_.size() + offset / 4].value;
    }
    // Stable virtual PRBS polynomial. Both transmit and expected streams use guest seeds.
    static std::uint32_t pxi_prbs(std::uint32_t& seed) {
        const auto value = seed;
        for (unsigned bit = 0; bit < 32; ++bit)
            seed = (seed >> 1) ^ ((0U - (seed & 1)) & 0x80200003U);
        return value;
    }
    bool pxi_data_ready(unsigned bank, bool write, std::uint64_t byte_offset, unsigned rank) const {
        const unsigned base = write ? 0xe600 : 0xea00;
        const unsigned start = write ? pxi_word(bank, 0x8024) & 0xff : pxi_word(bank, 0x8028) & 0xff;
        if (start >= 32) return false;
        std::uint64_t bytes = 0;
        for (unsigned index = start; index < 32; ++index) {
            const auto payload = pxi_word(bank, base + index * 4);
            if (!payload) {
                const std::uint64_t capacity = std::uint64_t{rank ? pxi_backend_->rank1_mib.get_value() :
                    pxi_backend_->rank0_mib.get_value()} << 20;
                return capacity && byte_offset < capacity && bytes <= capacity - byte_offset;
            }
            const unsigned count = (payload >> 20) & 0x1f;
            const unsigned pointer = (payload >> 25) & 0x1f;
            if (pointer + count > 32) return false;
            for (unsigned task_index = 0; task_index < count; ++task_index) {
                const auto task = pxi_word(bank, base + 0x80 + (pointer + task_index) * 4);
                if (!(task & 0x04000000) || !(task & 0x18000000)) continue;
                if (task & 0x01000000) return false;
                for (unsigned lane = 0; lane < 2; ++lane) {
                    const unsigned selection = (task >> (20 + lane * 2)) & 3;
                    const unsigned data_pointer = (task >> (4 + lane * 8)) & 0xff;
                    if (selection != 0 && selection != 2) return false;
                    if (selection == 0 && data_pointer >= 32) return false;
                }
                bytes += ((payload & 0x3ff) + 1) * 4;
            }
        }
        return false;
    }
    bool pxi_data(unsigned bank, bool write, std::uint64_t byte_offset, unsigned rank) {
        auto& state = pxi_[bank];
        const unsigned base = write ? 0xe600 : 0xea00;
        const unsigned start = write ? pxi_word(bank, 0x8024) & 0xff : pxi_word(bank, 0x8028) & 0xff;
        if (start >= 32) return false;
        std::array<std::uint32_t, 2> seeds{
            pxi_word(bank, write ? 0xe700 : 0xeb00),
            pxi_word(bank, write ? 0xe704 : 0xeb04)};
        unsigned emitted = 0;
        for (unsigned payload_index = start; payload_index < 32; ++payload_index) {
            const auto payload = pxi_word(bank, base + payload_index * 4);
            if (!payload) return true;
            if (!write && (payload & 0x40000000)) state.errors = {};
            const unsigned count = (payload >> 20) & 0x1f;
            const unsigned task_pointer = (payload >> 25) & 0x1f;
            if (task_pointer + count > 32) return false;
            for (unsigned repeat = 0; repeat <= (payload & 0x3ff); ++repeat) {
                for (unsigned task_index = 0; task_index < count; ++task_index) {
                    const auto task = pxi_word(bank, base + 0x80 + (task_pointer + task_index) * 4);
                    // Invalid and non-transmitting tasks only consume virtual clocks.
                    if (!(task & 0x04000000) || !(task & 0x18000000)) continue;
                    if (task & 0x01000000) return false; // SPG has a distinct unproven stream ABI.
                    for (unsigned lane = 0; lane < 2; ++lane) {
                        const unsigned selection = (task >> (20 + lane * 2)) & 3;
                        const unsigned pointer = (task >> (4 + lane * 8)) & 0xff;
                        std::uint32_t expected;
                        if (selection == 0) {
                            if (pointer >= 32) return false;
                            const unsigned table = write ? (lane ? 0xef80 : 0xe780) : (lane ? 0xed80 : 0xeb80);
                            expected = pxi_word(bank, table + pointer * 4);
                        } else if (selection == 2) expected = pxi_prbs(seeds[lane]);
                        else return false;
                        unsigned char bytes[4];
                        for (unsigned i = 0; i < 4; ++i) bytes[i] = expected >> (i * 8);
                        if (!pxi_backend_->pxi_transfer(bank * 2 + lane, rank, byte_offset + emitted * 4,
                                                       bytes, sizeof bytes, write)) return false;
                        if (!write) {
                            std::uint32_t observed = 0;
                            for (unsigned i = 0; i < 4; ++i) observed |= std::uint32_t{bytes[i]} << (i * 8);
                            state.errors[lane] |= observed ^ expected;
                        }
                    }
                    ++emitted;
                    if (!write) state.beats += 2;
                }
            }
        }
        return false; // A selected payload list must terminate inside its table.
    }
    bool pxi_execute(unsigned bank) {
        auto& state = pxi_[bank];
        const auto go = state.request & 0x1f;
        if (!go) return true;
        if (!configured(bank) || pxi_backend_->columns.get_value() > 16) return false;
        state.idle &= ~go;
        if (!(go & 3)) {
            // CA-only calibration commands have no DQ stream to transfer.
            state.idle = 0x1f;
            return true;
        }
        // The native helper selects RRBC seeds at CA GPR 2/3 for the two rank streams.
        const auto rrbc = pxi_word(bank, 0xe308);
        const unsigned chip_select = rrbc >> 30;
        const unsigned raw_select = (pxi_word(bank, 0xe384) >> 14) & 3;
        const unsigned select = chip_select ? chip_select : raw_select;
        if (select != 1 && select != 2) return false;
        const unsigned rank = select - 1;
        const auto row = rrbc & 0x1ffff;
        const auto column = (rrbc >> 17) & 0x3ff;
        const auto dram_bank = (rrbc >> 27) & 7;
        const std::uint64_t byte_offset =
            ((std::uint64_t{row} * pxi_backend_->banks.get_value() + dram_bank) *
             (std::uint64_t{1} << pxi_backend_->columns.get_value()) + column);
        if (((go & 2) && !pxi_data_ready(bank, true, byte_offset, rank)) ||
            ((go & 1) && !pxi_data_ready(bank, false, byte_offset, rank))) return false;
        if ((go & 2) && !pxi_data(bank, true, byte_offset, rank)) return false;
        if ((go & 1) && !pxi_data(bank, false, byte_offset, rank)) return false;
        state.idle = 0x1f; // No engine remains active after synchronous packet execution.
        return true;
    }
    bool pxi_transport(tlm::tlm_generic_payload& tx, std::uint32_t offset, unsigned first, unsigned end) {
        if (!pxi_frontend.get_value()) return false;
        if (offset == 0x802c || offset == 0x8070 || offset == 0xfce8 || offset == 0xfcec) {
            if (tx.is_write()) tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            else {
                const auto& state = pxi_[first];
                const auto value = offset == 0x802c ? state.idle :
                    offset == 0x8070 ? state.beats :
                    state.errors[offset == 0xfcec ? 1 : 0];
                clock_detail::store(tx, value);
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
            }
            return true;
        }
        if (!tx.is_write()) return false;
        const auto value = clock_detail::load(tx);
        if (offset == 0x8020) {
            for (unsigned bank = first; bank < end; ++bank) {
                auto& state = pxi_[bank];
                state.request = value;
                state.idle |= (value >> 16) & 0x1f;
                if (!pxi_execute(bank)) {
                    std::fprintf(stderr,
                        "UNMODELED_DDR_PXI_PACKET model=%s bank=%u request=0x%08x policy=no-completion\n",
                        name(), bank, value);
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return true;
                }
            }
        }
        for (unsigned bank = first; bank < end; ++bank) {
            auto& state = pxi_[bank];
            if (offset == 0x8074 && (value & 1)) state.beats = 0;
        }
        return false; // The same transaction retains the guest's control word below.
    }
    bool calibration_transport(tlm::tlm_generic_payload& tx, unsigned offset, unsigned first, unsigned end) {
        if (!mc_calibration.get_value()) return false;
        if (offset == 0x8f98 || offset == 0x70b4 || offset == 0x8fa0 ||
            offset == 0x8fa4 || offset == 0x6edc || offset == 0x6538) {
            if (tx.is_write()) tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            else {
                const auto& state = calibration_[first];
                std::uint32_t mask, result;
                switch (offset) {
                case 0x6538:
                    mask = 0x11f1f;
                    result = state.measurement;
                    break;
                case 0x8f98:
                    mask = 1U << 9;
                    result = state.complete && state.dedicated_status ? mask : 0;
                    break;
                case 0x70b4:
                    mask = 1U << 18;
                    result = state.complete && !state.dedicated_status ? mask : 0;
                    break;
                case 0x8fa0:
                    mask = 0x1f000000;
                    result = state.complete ? 0x10000000 : 0;
                    break;
                case 0x8fa4:
                    mask = 0x1f1f;
                    result = state.complete ? 0x1010 : 0;
                    break;
                default:
                    mask = 3;
                    result = state.pending ? 2 : 0;
                    break;
                }
                clock_detail::store(tx, (pxi_word(first, offset) & ~mask) | result);
                tx.set_response_status(tlm::TLM_OK_RESPONSE);
            }
            return true;
        }
        if (!tx.is_write()) return false;
        const auto value = clock_detail::load(tx);
        for (unsigned bank = first; bank < end; ++bank) {
            auto& state = calibration_[bank];
            if (offset == 0x8e7c) {
                if ((value & 1) && !(pxi_word(bank, offset) & 1)) {
                    state.pending = true;
                    state.complete = false;
                    state.dedicated_status = false;
                } else if (!(value & 1)) state.pending = false;
            }
            if (offset == 0x6ecc && (value & 0x20000) && !(pxi_word(bank, offset) & 0x20000) &&
                state.pending && (pxi_word(bank, 0x8e7c) & 0x1001) == 0x1001 &&
                ((pxi_word(bank, 0x8e98) | pxi_word(bank, 0x8ea4)) & 2) && configured(bank)) {
                state.pending = false;
                state.complete = true;
                state.dedicated_status = (pxi_word(bank, 0x8e74) & 0x02000000) != 0;
                std::fprintf(stderr,
                    "DDR_PHY_ANALOG_CALIBRATION model=%s bank=%u result=16 policy=ideal-five-bit-midpoint\n",
                    name(), bank);
            }
            if (offset == 0x64b4 && (value & 4) && !(pxi_word(bank, offset) & 4)) {
                state.measurement = 0;
                if ((pxi_word(bank, 0x6cb8) & 3) == 3 &&
                    ((pxi_word(bank, 0x64ac) >> 24) & 0xf) && configured(bank)) {
                    state.measurement = 0x10000 | ((value & 1) ? 0 : 16) | ((value & 2) ? 0x1000 : 0);
                    std::fprintf(stderr,
                        "DDR_PHY_MEASUREMENT model=%s bank=%u result=0x%08x policy=ideal-five-bit-midpoint\n",
                        name(), bank, state.measurement);
                }
            }
        }
        return false;
    }
    void transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!clock_detail::valid(tx)) return;
        const auto address = tx.get_address();
        const auto selector = address >> 16;
        const auto banks = configuration_banks.get_value();
        if (selector > banks) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        const auto offset = address & 0xffff;
        const unsigned first = selector ? selector - 1 : 0;
        const unsigned end = !selector && tx.is_write() ? banks : first + 1;
        if (command_engine_ && offset == 0x6604) {
            if (tx.is_write()) {
                if (clock_detail::load(tx) != 1) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                for (unsigned bank = first; bank < end; ++bank)
                    if (!runtime_[bank].publication_ready || !configured(bank)) {
                        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                        return;
                    }
                for (unsigned bank = first; bank < end; ++bank) runtime_[bank].published = true;
            } else clock_detail::store(tx, runtime_[first].published ? 1 : 0);
            tx.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (pxi_transport(tx, offset, first, end)) return;
        if (calibration_transport(tx, offset, first, end)) return;
        const auto command = std::find(commands_.begin(), commands_.end(), offset);
        if (command != commands_.end()) {
            const auto index = static_cast<std::size_t>(command - commands_.begin());
            if (tx.is_write()) {
                const auto value = clock_detail::load(tx);
                const auto mask = completion_mask.get_value();
                if (value & ~(mask | 1U)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                for (unsigned bank = first; bank < end; ++bank) {
                    const auto completion = bank * commands_.size() + index;
                    if (value & 1) {
                        const bool accepted = mask ? locally_configured(bank) : configured(bank);
                        if (!accepted || (ordered_commands.get_value() && index && !completed_[completion - 1])) {
                            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                            return;
                        }
                    } else if (mask == 0 || (value & mask) != (completed_[completion] ? mask : 0)) {
                        tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                        return;
                    }
                }
                if (value & 1) {
                    for (unsigned bank = first; bank < end; ++bank) {
                        if (command_engine_ && offset == 0x5020) {
                            const auto& policy = values_[bank * words_.size() + 0x5328 / 4];
                            if (policy.written) command_engine_->apply_rank_policy(bank, policy.value);
                        }
                        if (command_engine_ && offset == 0x6020)
                            runtime_[bank].publication_ready = true;
                        completed_[bank * commands_.size() + index] = true;
                    }
                }
            } else {
                const bool complete = completed_[first * commands_.size() + index] && configured(first);
                clock_detail::store(tx, complete ? completion_mask.get_value() : 0);
            }
        } else if (std::find(read_only_.begin(), read_only_.end(), offset) != read_only_.end()) {
            if (tx.is_write()) { tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
            clock_detail::store(tx, 0);
        } else {
            const auto entry = word(offset);
            if (entry == words_.end() || entry->offset != offset) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            const auto index = static_cast<std::size_t>(entry - words_.begin());
            const auto value = tx.is_write() ? clock_detail::load(tx) : values_[first * words_.size() + index].value;
            if (!entry->declared)
                std::fprintf(stderr, "UNMODELED_DDR_CONFIGURATION_SUBSTITUTE model=%s selector=%llu offset=0x%04llx operation=%s value=0x%08x policy=retained-cold-zero\n",
                    name(), static_cast<unsigned long long>(selector), static_cast<unsigned long long>(offset),
                    tx.is_write() ? "write" : "read", value);
            if (tx.is_write()) {
                for (unsigned bank = first; bank < end; ++bank) {
                    values_[bank * words_.size() + index] = {value, true};
                }
            } else clock_detail::store(tx, value);
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace qcom_vp

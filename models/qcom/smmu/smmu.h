// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <ports/initiator-signal-socket.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace qcom_vp {

// Bounded SMMUv2 stage-1 path with the two Security states of the architecture.
// Firmware owns all stream, security and table state. Each producer enters through
// one ArmSmmuStreamPort. The port supplies its own StreamID and physical bus, so a
// single register owner serves independent producers without aliasing their egress.
class ArmSmmuV2 : public sc_core::sc_module {
public:
    cci::cci_param<unsigned> context_count{"context_count", 112, "Active context banks (1..112)"};
    cci::cci_param<unsigned> global_span{"global_span", 0x80000, "Global half size (0x20000 or 0x80000)"};
    tlm_utils::simple_target_socket<ArmSmmuV2> target_socket{"target_socket"};
    InitiatorSignalSocket<bool> global_irq{"global_irq"};
    sc_core::sc_vector<InitiatorSignalSocket<bool>> context_irq{"context_irq", context_count.get_value()};

    explicit ArmSmmuV2(sc_core::sc_module_name name) : sc_module(name) {
        if (!context_count.get_value() || context_count.get_value() > contexts_.size() ||
            (global_span.get_value() != 0x20000 && global_span.get_value() != 0x80000) ||
            context_count.get_value() * 4096 > global_span.get_value())
            throw std::invalid_argument("SMMU context count does not fit the supported geometry");
        target_socket.register_b_transport(this, &ArmSmmuV2::mmio);
        // Virtual reset, not an inherited firmware register snapshot.
        s2cr_.fill(2U << 16);
        cbar_.fill(2U << 16);
        // Virtual sCR1 policy: every stream match group, context bank and context
        // interrupt is allocated to the Non-secure state. Secure software must move
        // the boundary before a Secure transaction can match anything.
        secure_.cr1 = (1U << 16) | (static_cast<uint32_t>(smr_.size()) << 8) |
            context_count.get_value();
    }

    // One physical producer transfer. The stream port supplies its StreamID, the
    // transaction carries the input security attribute, and bus is that port's
    // physical egress. No state that belongs to the register owner moves here.
    void stream_transfer(unsigned sid, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay,
                         tlm::tlm_fw_transport_if<>& bus) {
        tx.set_dmi_allowed(false);
        const auto address = tx.get_address();
        const auto length = tx.get_data_length();
        auto* const data = tx.get_data_ptr();
        const auto width = tx.get_streaming_width();
        if (sid >= 65536 || (!tx.is_read() && !tx.is_write()) || !data || !length ||
            tx.get_byte_enable_ptr() || width < length || address >= limit ||
            length > limit - address) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
        }
        // An absent or unspecified attribute is never Secure. Only an explicit
        // Secure attribute can select the Secure register bank or entries.
        const auto* source_attrs = tx.get_extension<QemuMemTxAttrsExtension>();
        const bool in_secure = source_attrs && !source_attrs->attrs.unspecified &&
            source_attrs->attrs.secure;
        // Validate every page before a store. No transfer-sized state is allocated.
        // Recheck each translation when forwarding. Physical bus failures can leave partial DMA.
        for (unsigned pass = 0; pass < (tx.is_write() ? 2U : 1U); ++pass) {
            unsigned consumed = 0;
            while (consumed < length) {
                const auto va = address + consumed;
                const unsigned count = std::min(length - consumed, unsigned(4096 - (va & 4095)));
                Translation translated{};
                if (!translate(sid, in_secure, tx, va, delay, bus, translated)) {
                    tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
                }
                if (tx.is_write() && pass == 0) { consumed += count; continue; }
                // Preserve every other upstream attribute. Only the security state
                // changes, and the caller regains its extension on every return.
                auto* const original = tx.get_extension<QemuMemTxAttrsExtension>();
                QemuMemTxAttrsExtension holder;
                if (original) holder.attrs = original->attrs;
                holder.attrs.secure = translated.secure;
                holder.attrs.unspecified = false;
                tx.set_extension(&holder);
                tx.set_address(translated.pa); tx.set_data_ptr(data + consumed);
                tx.set_data_length(count); tx.set_streaming_width(count);
                tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
                bus.b_transport(tx, delay);
                if (original) tx.set_extension(original);
                else tx.clear_extension<QemuMemTxAttrsExtension>();
                tx.set_address(address); tx.set_data_ptr(data);
                tx.set_data_length(length); tx.set_streaming_width(width); tx.set_dmi_allowed(false);
                if (!tx.is_response_ok()) return;
                consumed += count;
            }
        }
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

private:
    static constexpr uint64_t pa_mask = 0x0000fffffffff000ULL;
    static constexpr uint64_t limit = uint64_t{1} << 48;
    struct Context { std::array<uint32_t, 32> word{}; };
    struct TlbEntry {
        bool valid = false;
        bool secure = false, input_secure = false, write = false, privileged = false, stage2 = false;
        unsigned sid = 0, cb = 0, stream = 0, s2cb = 0;
        uint64_t va_page = 0, pa_page = 0;
        const tlm::tlm_fw_transport_if<>* bus = nullptr;
    };
    std::array<TlbEntry, 256> tlb_{};
    void invalidate_tlb() { for (auto& entry : tlb_) entry.valid = false; }
    static std::size_t tlb_slot(unsigned sid, unsigned cb, unsigned stream, uint64_t page,
                                bool secure, bool stage2, bool write, bool privileged) {
        uint64_t hash = page ^ (uint64_t(sid) << 17) ^ (uint64_t(cb) << 9) ^ (uint64_t(stream) << 1);
        hash ^= uint64_t(secure) << 63 | uint64_t(stage2) << 62 |
            uint64_t(write) << 61 | uint64_t(privileged) << 60;
        hash ^= hash >> 33; hash *= 0xff51afd7ed558ccdULL;
        hash ^= hash >> 33; hash *= 0xc4ceb9fe1a85ec53ULL;
        hash ^= hash >> 33;
        return static_cast<std::size_t>(hash) & 255;
    }
    std::array<Context, 112> contexts_{};
    std::array<uint32_t, 128> smr_{}, s2cr_{};
    std::array<uint32_t, 112> cbar_{}, cba2r_{};
    std::array<uint32_t, 112> cbfrsynra_{};
    // One bank per Security state. sCR0/sCR1/sCR2/sACR and the Secure fault group
    // exist only in the Secure state. NSCFG is meaningful only in sCR0.
    struct GlobalBank {
        uint32_t cr0 = 1, cr1 = 0, cr2 = 0, acr = 0, gfsr = 0;
        std::array<uint32_t, 2> gfar{};
        std::array<uint32_t, 3> gfsynr{};
    };
    GlobalBank secure_, nonsecure_;
    // QSEE sets SSD bits at global +0x4000 (SM8850 0x157272ac, SM8845 0x1571ba18).
    // The table retains guest policy with a zero virtual reset. Translation does not consult it.
    std::array<uint32_t, 1024> ssd_{};
    // QSEE writes TZ_QTI_Config hardware settings into implementation-defined page 2.
    // The words retain guest configuration with a zero virtual reset. They have no model effect.
    std::array<uint32_t, 1024> impl_page2_{};

    // sCR1.NSNUMSMRGO [15:8] and sCR1.NSNUMCBO [7:0] allocate resources from the
    // top of each table to Secure software (IHI 0062D.b 2.3.9, 3.7, 9.6).
    unsigned ns_stream_groups() const {
        return std::min<unsigned>(static_cast<unsigned>(smr_.size()),
                                  (secure_.cr1 >> 8) & 0xffU);
    }
    unsigned ns_contexts() const {
        return std::min<unsigned>(context_count.get_value(), secure_.cr1 & 0xffU);
    }
    unsigned implemented_contexts() const { return context_count.get_value(); }

    void start_of_simulation() override { update_irqs(); }
    void update_irqs() {
        const bool pending = (nonsecure_.gfsr && (nonsecure_.cr0 & (1U << 2))) ||
            (secure_.gfsr && (secure_.cr0 & (1U << 2)));
        for (int p = 0; p < global_irq.size(); ++p) global_irq[p]->write(pending);
        for (unsigned i = 0; i < implemented_contexts(); ++i) {
            const auto& w = contexts_[i].word;
            const bool context_pending = w[0x58 / 4] && (w[0] & (1U << 6));
            // SMMUv2 dedicated context pins do not use CBAR.IRPTNDX.
            for (int p = 0; p < context_irq[i].size(); ++p) context_irq[i][p]->write(context_pending);
        }
    }
    static bool cb_register(unsigned r) {
        switch (r) {
        case 0: case 4: case 8: case 0x10: case 0x20: case 0x24:
        case 0x28: case 0x2c: case 0x30: case 0x34: case 0x38: case 0x3c:
        case 0x50: case 0x54: case 0x58: case 0x60: case 0x64: case 0x68: case 0x6c:
        case 0x70: case 0x74:
            return true;
        default: return false;
        }
    }
    static bool cb_sync(unsigned r) {
        switch (r) {
        case 0x600: case 0x604: case 0x610: case 0x618:
        case 0x620: case 0x624: case 0x7f0: case 0x7f4: return true;
        default: return false;
        }
    }
    bool secure_bank(unsigned offset, uint32_t& value, bool write) {
        uint32_t* v = nullptr;
        switch (offset) {
        case 0: v = &secure_.cr0; break;
        case 4: v = &secure_.cr1; break;
        case 8: v = &secure_.cr2; break;
        case 0x10: v = &secure_.acr; break;
        case 0x40: case 0x44: v = &secure_.gfar[(offset - 0x40) / 4]; break;
        case 0x48: v = &secure_.gfsr; break;
        case 0x50: case 0x54: case 0x58: v = &secure_.gfsynr[(offset - 0x50) / 4]; break;
        default: return false;
        }
        if (!write) value = *v;
        else if (offset == 0x48) *v &= ~value;
        else *v = value;
        return true;
    }
    bool reg(unsigned offset, uint32_t& value, bool write, bool secure) {
        if (offset < 0x400) {
            if (secure && secure_bank(offset, value, write)) return true;
            // sCR1 and sCR2 are secure-only. Non-secure accesses read zero and ignore writes.
            if (!secure && (offset == 4 || offset == 8)) {
                if (!write) value = 0;
                return true;
            }
        }
        // The non-secure global alias reaches the non-secure bank for both security states.
        if (offset >= 0x400 && offset < 0x800) offset -= 0x400;
        const unsigned cbs = implemented_contexts();
        const unsigned ns_cbs = ns_contexts();
        const unsigned ns_groups = ns_stream_groups();
        const auto cb_base = global_span.get_value();
        if (offset >= cb_base && offset < cb_base + cbs * 4096) {
            const auto cb = (offset - cb_base) / 4096;
            const auto r = offset & 4095;
            // A Secure context bank is not visible to Non-secure configuration software.
            if (!secure && cb >= ns_cbs) {
                if (!write) value = 0;
                return true;
            }
            auto& w = contexts_[cb].word;
            if (cb_sync(r)) { if (write && r == 0x7f4) return false; value = 0; return true; }
            if (!cb_register(r)) return false;
            // IPAFAR is a read-only FAR alias (IHI 0062D.b 17.3.2). A stage 2 fault
            // records its IPA in the FAR of the stage 2 bank.
            if (r == 0x70 || r == 0x74) {
                if (!write) value = w[(r - 0x10) / 4];
                return true;
            }
            if (r == 0x58) {
                if (write) w[r / 4] &= ~value;
                else {
                    // IHI 0062D.b 16.5.9: Format is read-only metadata, not a fault.
                    const uint32_t format = (cba2r_[cb] & 1) ? 2U : (w[0x30 / 4] >> 31);
                    value = w[r / 4] | (format << 9);
                }
            } else if (!write) value = w[r / 4];
            else w[r / 4] = value;
            return true;
        }
        if (offset >= 0x4000 && offset < 0x4000 + ssd_.size() * 4) {
            auto& v = ssd_[(offset - 0x4000) / 4];
            if (write) v = value; else value = v;
            return true;
        }
        if (offset >= 0x2000 && offset < 0x2000 + impl_page2_.size() * 4) {
            auto& v = impl_page2_[(offset - 0x2000) / 4];
            if (write) v = value; else value = v;
            return true;
        }
        if (offset >= 0x1400 && offset < 0x1400 + cbs * 4) {
            const auto cb = (offset - 0x1400) / 4;
            if (!secure && cb >= ns_cbs) {
                if (!write) value = 0;
                return true;
            }
            if (write) return false;
            value = cbfrsynra_[cb]; return true;
        }
        if (offset >= 0x800 && offset < 0xa00) {
            const auto group = (offset - 0x800) / 4;
            // Non-secure software sees only its allocation of the common table.
            if (!secure && group >= ns_groups) {
                if (!write) value = 0;
                return true;
            }
            auto& v = smr_[group];
            if (write) v = value; else value = v;
            return true;
        }
        if (offset >= 0xc00 && offset < 0xe00) {
            const auto group = (offset - 0xc00) / 4;
            if (!secure && group >= ns_groups) {
                if (!write) value = 0;
                return true;
            }
            auto& v = s2cr_[group];
            if (write) v = value; else value = v;
            return true;
        }
        if ((offset >= 0x1000 && offset < 0x1000 + cbs * 4) ||
            (offset >= 0x1800 && offset < 0x1800 + cbs * 4)) {
            const auto cb = offset < 0x1800 ? (offset - 0x1000) / 4 : (offset - 0x1800) / 4;
            if (!secure && cb >= ns_cbs) {
                if (!write) value = 0;
                return true;
            }
            auto& v = offset < 0x1800 ? cbar_[cb] : cba2r_[cb];
            if (write) v = value; else value = v;
            return true;
        }
        if (offset >= 0x20 && offset <= 0x3c) {
            if (write) return false;
            if (offset == 0x24) {
                // IDR1.NUMCB: a Non-secure read reports sCR1.NSNUMCBO.
                const auto visible = secure ? cbs : ns_cbs;
                value = ((global_span.get_value() == 0x80000 ? 6U : 4U) << 28) | visible;
                return true;
            }
            // Compatible virtual capabilities. These are not silicon identity values.
            // IDR0 reports stage 1, stage 2, and nested translation.
            constexpr std::array<uint32_t, 8> ids{
                (1U << 30) | (1U << 29) | (1U << 28) | (1U << 27) | (1U << 26) | (3U << 24) | (1U << 16) |
                    (1U << 14) | (15U << 9) | 128U,
                0, (1U << 12) | 0x555U, 0, 0, 0, 0, 0x20};
            value = ids[(offset - 0x20) / 4];
            if (offset == 0x20) {
                // IDR0.NUMSMRG and IDR0.NUMIRPT have Non-secure views derived from
                // sCR1. The Secure view keeps the implemented values.
                value &= ~0xffU;
                value |= secure ? 128U : ns_groups;
                if (!secure) {
                    value &= ~(0xffU << 16);
                    value |= ((secure_.cr1 >> 16) & 0xffU) << 16;
                }
            }
            return true;
        }
        if (offset == 0x64 || offset == 0x68 || offset == 0x6c || offset == 0x70 || offset == 0x74) {
            if (write && offset == 0x74) return false;
            value = 0; return true; // TLBI writes invalidate the TLB synchronously.
        }
        uint32_t* v = nullptr;
        switch (offset) {
        case 0: v = &nonsecure_.cr0; break;
        case 0x10: v = &nonsecure_.acr; break;
        case 0x40: case 0x44: v = &nonsecure_.gfar[(offset - 0x40) / 4]; break;
        case 0x48: v = &nonsecure_.gfsr; break;
        case 0x50: case 0x54: case 0x58: v = &nonsecure_.gfsynr[(offset - 0x50) / 4]; break;
        default: return false;
        }
        if (!write) value = *v;
        else if (offset == 0x48) *v &= ~value;
        else *v = value;
        return true;
    }
    void mmio(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        tx.set_dmi_allowed(false);
        const auto a = tx.get_address();
        const auto n = tx.get_data_length();
        if ((!tx.is_read() && !tx.is_write()) || !tx.get_data_ptr() ||
            (n != 4 && n != 8) || a % 4 || a > 2 * uint64_t(global_span.get_value()) - n ||
            tx.get_byte_enable_ptr() || tx.get_streaming_width() < n) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE); return;
        }
        const auto* attributes = tx.get_extension<QemuMemTxAttrsExtension>();
        const bool secure = attributes && !attributes->attrs.unspecified && attributes->attrs.secure;
        for (unsigned p = 0; p < n; p += 4) {
            if (tx.is_write()) invalidate_tlb();
            uint32_t v = 0;
            if (tx.is_write()) for (unsigned b = 0; b < 4; ++b) v |= uint32_t(tx.get_data_ptr()[p + b]) << (8 * b);
            if (!reg(static_cast<unsigned>(a) + p, v, tx.is_write(), secure)) {
                tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return;
            }
            if (tx.is_read()) for (unsigned b = 0; b < 4; ++b) tx.get_data_ptr()[p + b] = v >> (8 * b);
        }
        update_irqs();
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    bool fault(bool secure, int cb, unsigned sid, uint64_t va, bool write,
               uint32_t reason, uint32_t syndrome = 0) {
        // The fault is architectural state, but it also ends the producer's transaction.
        std::fprintf(stderr, "SMMU_FAULT model=%s secure=%d context=%d stream=0x%x va=0x%llx "
                     "operation=%s reason=0x%x syndrome=0x%x cr0=0x%x ns_groups=%u\n", name(), secure, cb, sid,
                     static_cast<unsigned long long>(va), write ? "write" : "read", reason, syndrome,
                     (secure ? secure_ : nonsecure_).cr0, ns_stream_groups());
        if (cb < 0) {
            for (unsigned i = 0; i < smr_.size(); ++i)
                if (smr_[i] & (1U << 31))
                    std::fprintf(stderr, "SMMU_FAULT_STREAM model=%s index=%u smr=0x%x s2cr=0x%x\n",
                                 name(), i, smr_[i], s2cr_[i]);
        }
        if (cb < 0) {
            auto& bank = secure ? secure_ : nonsecure_;
            if (bank.gfsr) bank.gfsr |= 1U << 31;
            else {
                bank.gfsr = reason;
                bank.gfar[0] = uint32_t(va);
                bank.gfar[1] = uint32_t(va >> 32);
                bank.gfsynr[1] = sid;
            }
        } else {
            auto& w = contexts_[cb].word;
            if (w[0x58 / 4]) w[0x58 / 4] |= 1U << 31;
            else {
                w[0x58 / 4] = reason;
                cbfrsynra_[cb] = sid;
                w[0x60 / 4] = uint32_t(va);
                w[0x64 / 4] = uint32_t(va >> 32);
                w[0x68 / 4] = syndrome | (write ? 1U << 4 : 0);
                w[0x6c / 4] = 0; // No implementation-defined syndrome.
            }
        }
        update_irqs();
        return false;
    }
    // NSCFG encoding: 0 default, 1 reserved, 2 Secure, 3 Non-secure.
    // SMMUv2 interprets the reserved encoding as Non-secure (IHI 0062D.b 2.6).
    // A Non-secure origin never becomes Secure.
    static bool apply_nscfg(uint32_t field, bool source_secure) {
        if (!source_secure) return false;
        return (field & 3) == 0 || (field & 3) == 2;
    }
    struct Translation { uint64_t pa = 0; bool secure = false; };
    bool translate(unsigned sid, bool in_secure, tlm::tlm_generic_payload& source, uint64_t va,
                   sc_core::sc_time& delay, tlm::tlm_fw_transport_if<>& bus, Translation& out) {
        const bool write = source.is_write();
        auto& bank = in_secure ? secure_ : nonsecure_;
        // Secure transactions that bypass the stream table use sCR0.NSCFG. The
        // Non-secure bank has no NSCFG, and its transactions stay Non-secure.
        const uint32_t bypass_nscfg = (secure_.cr0 >> 28) & 3;
        if (bank.cr0 & 1) { out = {va, apply_nscfg(bypass_nscfg, in_secure)}; return true; }
        if (bank.cr0 & (1U << 3)) return fault(in_secure, -1, sid, va, write, 1U << 1);
        const unsigned ns_groups = ns_stream_groups();
        int match = -1;
        for (unsigned i = 0; i < smr_.size(); ++i) {
            // A transaction matches only entries allocated to its own Security state.
            if ((i >= ns_groups) != in_secure) continue;
            const auto r = smr_[i];
            if (!(r & (1U << 31)) || ((sid ^ r) & ~((r >> 16) & 0x7fffU) & 0xffffU)) continue;
            if (match >= 0) {
                if (bank.cr0 & (1U << 21)) return fault(in_secure, -1, sid, va, write, 1U << 2);
                out = {va, apply_nscfg(bypass_nscfg, in_secure)}; return true;
            }
            match = static_cast<int>(i);
        }
        if (match < 0) {
            if (bank.cr0 & (1U << 10)) return fault(in_secure, -1, sid, va, write, 1U << 1);
            out = {va, apply_nscfg(bypass_nscfg, in_secure)}; return true;
        }
        const auto stream = s2cr_[match];
        const bool group_secure = static_cast<unsigned>(match) >= ns_groups;
        const auto type = (stream >> 16) & 3;
        if (type == 1) {
            out = {va, group_secure && apply_nscfg((stream >> 18) & 3, true)};
            return true;
        }
        if (type != 0) return fault(in_secure, -1, sid, va, write, 1U);
        const unsigned cb = stream & 255;
        const unsigned ns_cbs = ns_contexts();
        // A group may select only a context bank of its own Security state. A
        // Non-secure entry that names a Secure bank faults; it cannot borrow it.
        if (cb >= implemented_contexts() || (cb >= ns_cbs) != group_secure)
            return fault(in_secure, -1, sid, va, write, 1U << 3);
        // S2CR.NSCFG is defined only for Secure stream groups. A Non-secure origin
        // can never pass through it.
        const bool base_secure = group_secure && apply_nscfg((stream >> 18) & 3, true);
        const auto& w = contexts_[cb].word;
        const uint32_t syndrome = ((stream >> 24) & 3) == 3 ? 1U << 5 : 0;
        // CBAR.TYPE: 0 stage 2 only, 1 stage 1 with stage 2 bypass, 2 stage 1 with
        // stage 2 fault, 3 stage 1 nested in the stage 2 bank CBAR.CBNDX [15:8].
        // Stage 2 exists only in the Non-secure state.
        const auto cb_type = (cbar_[cb] >> 16) & 3;
        int s2cb = -1;
        if (cb_type == 0 || cb_type == 3) {
            s2cb = cb_type == 0 ? static_cast<int>(cb) : static_cast<int>((cbar_[cb] >> 8) & 255);
            if (group_secure || static_cast<unsigned>(s2cb) >= ns_cbs || ((cbar_[s2cb] >> 16) & 3) != 0)
                return fault(in_secure, -1, sid, va, write, 1U);
        } else if (cb_type != 1) {
            return fault(in_secure, -1, sid, va, write, 1U);
        }
        const bool privileged = ((stream >> 24) & 3) == 3;
        const auto va_page = va & ~uint64_t{4095};
        const auto slot = tlb_slot(sid, cb, static_cast<unsigned>(match), va_page,
                                   in_secure, s2cb >= 0, write, privileged);
        auto& cached = tlb_[slot];
        if (cached.valid && cached.sid == sid && cached.cb == cb &&
            cached.stream == static_cast<unsigned>(match) && cached.va_page == va_page &&
            cached.input_secure == in_secure && cached.stage2 == (s2cb >= 0) &&
            cached.s2cb == static_cast<unsigned>(s2cb) && cached.write == write &&
            cached.privileged == privileged && cached.bus == &bus) {
            out = {cached.pa_page | (va & 4095), cached.secure};
            return true;
        }
        const uint32_t sctlr = w[0];
        if (cb_type == 0 || !(sctlr & 1)) {
            if (s2cb >= 0) {
                // A disabled stage 1 passes the input address to stage 2 as the IPA.
                uint64_t pa = va;
                if (!stage2(s2cb, sid, in_secure, source, va, write, delay, bus, pa)) return false;
                out = {pa, false};
                cached = {true, false, in_secure, write, privileged, true,
                          sid, cb, static_cast<unsigned>(match), static_cast<unsigned>(s2cb),
                          va_page, pa & ~uint64_t{4095}, &bus};
                return true;
            }
            // Translation disabled. SCTLR.NSCFG exists only in a Secure context bank.
            // NSCFG=3 forces Non-secure; =2 forces Secure; =0 keeps the context security.
            const bool disabled_secure = cb >= ns_cbs
                ? apply_nscfg((sctlr >> 28) & 3, in_secure && base_secure) : base_secure;
            out = {va, disabled_secure}; return true;
        }
        if (!(cba2r_[cb] & 1) || (sctlr & (1U << 4)))
            return fault(in_secure, cb, sid, va, write, 1U << 8, syndrome);
        const auto tcr = w[0x30 / 4];
        // TCR2 (offset 0x10) extends TCR. PASize is [3:0], SEP [17:15], NSCFG0
        // is bit 14 and NSCFG1 is bit 30. HA/HD/HAD/TBI stay unsupported.
        const auto tcr2 = w[0x10 / 4];
        const unsigned bits = 64 - (tcr & 63);
        const unsigned ps = tcr2 & 15;
        constexpr std::array<unsigned, 6> pa_bits{32, 36, 40, 42, 44, 48};
        constexpr uint32_t tcr2_supported = 0x4003c01fU;
        if ((tcr & (3U << 14)) || bits < 25 || bits > 48 || ps >= pa_bits.size() ||
            (tcr2 & ~tcr2_supported) ||
            (((tcr2 >> 15) & 7) != 0 && ((tcr2 >> 15) & 7) != 7) ||
            ((stream >> 24) & 3) == 1)
            return fault(in_secure, cb, sid, va, write, 1U << 8, syndrome);
        if ((tcr & (1U << 7)) || va >= (uint64_t{1} << bits))
            return fault(in_secure, cb, sid, va, write, 1U << 1, syndrome);
        uint64_t table = uint64_t(w[0x20 / 4]) | (uint64_t(w[0x24 / 4]) << 32);
        const auto first = 4 - (bits - 12 + 8) / 9;
        const auto root_bits = bits - (12 + (3 - first) * 9) + 3;
        // ASID occupies [63:48]. The initial table can be smaller than 4 KiB.
        // Reserved low bits are ignored, except the architecturally unpredictable [2:0].
        if (table & 7) return fault(in_secure, cb, sid, va, write, 1U << 8, syndrome);
        table &= (limit - 1) & ~((uint64_t{1} << root_bits) - 1);
        // TTBR0 walks start Secure for a Secure translation context unless
        // TCR2.NSCFG0 selects Non-secure tables. NSCFG1 is accepted for TTBR1
        // but TTBR1 traffic is unsupported, so it has no effect here.
        bool walk_secure = base_secure && !((tcr2 >> 14) & 1);
        bool read_only = false, privileged_only = false;
        for (unsigned level = first; level < 4; ++level) {
            const auto shift = 12 + (3 - level) * 9;
            const auto index_bits = level == first ? bits - shift : 9;
            const auto at = table + (((va >> shift) & ((uint64_t{1} << index_bits) - 1)) * 8);
            if (at >= (uint64_t{1} << pa_bits[ps])) return fault(in_secure, cb, sid, va, write, 1U << 7, syndrome | level);
            // A nested stage 1 fetches its tables through stage 2.
            uint64_t at_pa = at;
            if (s2cb >= 0 && !stage2(s2cb, sid, in_secure, source, at, false, delay, bus, at_pa)) return false;
            uint64_t d = 0;
            if (!read_descriptor(source, at_pa, walk_secure, delay, bus, d))
                return fault(in_secure, cb, sid, va, write, 1U << 4, syndrome | level | (1U << 10));
            const auto form = d & 3;
            // Invalid/reserved forms take precedence over descriptor address checks.
            if (!(form & 1) || (level == 0 && form != 3) || (level == 3 && form != 3))
                return fault(in_secure, cb, sid, va, write, 1U << 1, syndrome | level);
            if ((d & 0x000f000000000000ULL) || (d & pa_mask) >= (uint64_t{1} << pa_bits[ps]))
                return fault(in_secure, cb, sid, va, write, 1U << 7, syndrome | level);
            if (level < 3 && form == 3) {
                // NSTable selects the Security state of the next-level table.
                // A Secure walk can descend into Non-secure memory. It never returns.
                if (d & (uint64_t{1} << 63)) walk_secure = false;
                read_only |= (d & (uint64_t{1} << 62)) != 0;
                privileged_only |= (d & (uint64_t{1} << 61)) != 0;
                table = d & pa_mask; continue;
            }
            if (level == 0 || (level < 3 ? form != 1 : form != 3))
                return fault(in_secure, cb, sid, va, write, 1U << 1, syndrome | level);
            // Bit 63 is reserved on block and page descriptors.
            if (d & (uint64_t{1} << 63))
                return fault(in_secure, cb, sid, va, write, 1U << 1, syndrome | level);
            if (!(d & (1U << 10))) return fault(in_secure, cb, sid, va, write, 1U << 2, syndrome | level);
            if ((!privileged && (privileged_only || !(d & (1U << 6)))) ||
                (write && (read_only || (d & (1U << 7))))) return fault(in_secure, cb, sid, va, write, 1U << 3, syndrome | level);
            const auto mask = (uint64_t{1} << shift) - 1;
            if ((d & pa_mask) & mask) return fault(in_secure, cb, sid, va, write, 1U << 1, syndrome | level);
            // Leaf NS bit 5 selects Non-secure memory. A Non-secure origin or a
            // Non-secure walk can never produce a Secure downstream access.
            const bool leaf_secure = walk_secure && !((d >> 5) & 1);
            const uint64_t ipa = (d & pa_mask) | (va & mask);
            uint64_t pa = ipa;
            if (s2cb >= 0 && !stage2(s2cb, sid, in_secure, source, ipa, write, delay, bus, pa)) return false;
            out = {pa, leaf_secure};
            cached = {true, leaf_secure, in_secure, write, privileged, s2cb >= 0,
                      sid, cb, static_cast<unsigned>(match), static_cast<unsigned>(s2cb),
                      va_page, pa & ~uint64_t{4095}, &bus};
            return true;
        }
        return fault(in_secure, cb, sid, va, write, 1U << 1, syndrome);
    }
    // One 8-byte table read. Only the security state differs from the upstream attributes.
    // A walk never gains Secure from a Non-secure request.
    static bool read_descriptor(tlm::tlm_generic_payload& source, uint64_t at, bool secure,
                                sc_core::sc_time& delay, tlm::tlm_fw_transport_if<>& bus, uint64_t& d) {
        std::array<unsigned char, 8> bytes{};
        tlm::tlm_generic_payload walk;
        walk.set_address(at); walk.set_command(tlm::TLM_READ_COMMAND);
        walk.set_data_ptr(bytes.data()); walk.set_data_length(8); walk.set_streaming_width(8);
        QemuMemTxAttrsExtension holder;
        if (const auto* attrs = source.get_extension<QemuMemTxAttrsExtension>())
            holder.attrs = attrs->attrs;
        holder.attrs.secure = secure;
        holder.attrs.unspecified = false;
        walk.set_extension(&holder);
        bus.b_transport(walk, delay);
        // The local payload must not free the stack holder in its destructor.
        walk.clear_extension<QemuMemTxAttrsExtension>();
        if (!walk.is_response_ok()) return false;
        d = 0;
        for (unsigned b = 0; b < 8; ++b) d |= uint64_t(bytes[b]) << (8 * b);
        return true;
    }
    // AArch64 stage 2 with a 4 KiB granule (IHI 0062D.b 16.5.13). TCR uses the stage 2
    // format: T0SZ [5:0], SL0 [7:6], TG0 [15:14], PASize [18:16]. Walks are Non-secure.
    // A fault is recorded in the stage 2 bank with the IPA as its address.
    bool stage2(int cb, unsigned sid, bool in_secure, tlm::tlm_generic_payload& source, uint64_t ipa,
                bool write, sc_core::sc_time& delay, tlm::tlm_fw_transport_if<>& bus, uint64_t& pa) {
        const auto& w = contexts_[cb].word;
        const uint32_t sctlr = w[0];
        if (!(sctlr & 1)) { pa = ipa; return true; }
        const auto tcr = w[0x30 / 4];
        const unsigned bits = 64 - (tcr & 63);
        const unsigned sl0 = (tcr >> 6) & 3;
        const unsigned ps = (tcr >> 16) & 7;
        constexpr std::array<unsigned, 6> pa_bits{32, 36, 40, 42, 44, 48};
        if (!(cba2r_[cb] & 1) || (sctlr & (1U << 4)) || ((tcr >> 14) & 3) || sl0 == 3 ||
            bits < 25 || bits > 48 || ps >= pa_bits.size())
            return fault(in_secure, cb, sid, ipa, write, 1U << 8);
        const unsigned first = 2 - sl0;
        const unsigned first_shift = 12 + (3 - first) * 9;
        // The initial level resolves up to 13 bits: 16 concatenated tables of 512 entries.
        if (bits <= first_shift || bits - first_shift > 13)
            return fault(in_secure, cb, sid, ipa, write, 1U << 8);
        if (ipa >= (uint64_t{1} << bits)) return fault(in_secure, cb, sid, ipa, write, 1U << 1, first);
        uint64_t table = (uint64_t(w[0x20 / 4]) | (uint64_t(w[0x24 / 4]) << 32)) &
            (limit - 1) & ~((uint64_t{8} << (bits - first_shift)) - 1);
        if (table >= (uint64_t{1} << pa_bits[ps])) return fault(in_secure, cb, sid, ipa, write, 1U << 7, first);
        for (unsigned level = first; level < 4; ++level) {
            const auto shift = 12 + (3 - level) * 9;
            const auto index_bits = level == first ? bits - shift : 9;
            const auto at = table + (((ipa >> shift) & ((uint64_t{1} << index_bits) - 1)) * 8);
            uint64_t d = 0;
            if (!read_descriptor(source, at, false, delay, bus, d))
                return fault(in_secure, cb, sid, ipa, write, 1U << 4, level | (1U << 10));
            const auto form = d & 3;
            if (!(form & 1) || (level == 3 && form != 3))
                return fault(in_secure, cb, sid, ipa, write, 1U << 1, level);
            if ((d & 0x000f000000000000ULL) || (d & pa_mask) >= (uint64_t{1} << pa_bits[ps]))
                return fault(in_secure, cb, sid, ipa, write, 1U << 7, level);
            if (level < 3 && form == 3) { table = d & pa_mask; continue; }
            // A 4 KiB granule has no level 0 block.
            if (level == 0) return fault(in_secure, cb, sid, ipa, write, 1U << 1, level);
            // SCTLR.AFFD [3] disables access flag faults.
            if (!(d & (1U << 10)) && !(sctlr & (1U << 3)))
                return fault(in_secure, cb, sid, ipa, write, 1U << 2, level);
            // S2AP [7:6]: bit 6 permits reads and bit 7 permits writes.
            if (!(d & (1U << (write ? 7 : 6)))) return fault(in_secure, cb, sid, ipa, write, 1U << 3, level);
            const auto mask = (uint64_t{1} << shift) - 1;
            if ((d & pa_mask) & mask) return fault(in_secure, cb, sid, ipa, write, 1U << 1, level);
            pa = (d & pa_mask) | (ipa & mask);
            return true;
        }
        return fault(in_secure, cb, sid, ipa, write, 1U << 1);
    }
};

// One producer's entry into the shared SMMU. The owner keeps every register,
// stream match, context and translation decision. This port keeps only its own
// StreamID and its own physical egress, so two producers can carry different
// StreamIDs, different input security and different downstream buses.
class ArmSmmuStreamPort : public sc_core::sc_module {
public:
    cci::cci_param<unsigned> stream_id{"stream_id", 65536,
        "Required 16-bit StreamID for this producer"};
    tlm_utils::simple_target_socket<ArmSmmuStreamPort> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<ArmSmmuStreamPort> initiator_socket{"initiator_socket"};

    ArmSmmuStreamPort(sc_core::sc_module_name name, sc_core::sc_object* controller)
        : sc_module(name), controller_(dynamic_cast<ArmSmmuV2*>(controller)) {
        if (!controller_) throw std::invalid_argument("SMMU stream port requires an ArmSmmuV2 owner");
        target_socket.register_b_transport(this, &ArmSmmuStreamPort::dma);
    }

private:
    ArmSmmuV2* controller_;
    void end_of_elaboration() override {
        if (stream_id.get_value() >= 65536)
            throw std::invalid_argument("SMMU stream port requires a 16-bit stream ID");
        if (!initiator_socket.size())
            throw std::invalid_argument("SMMU stream port requires the physical memory path");
    }
    void dma(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        controller_->stream_transfer(static_cast<unsigned>(stream_id.get_value()), tx, delay,
                                     *initiator_socket.operator->());
    }
};

} // namespace qcom_vp

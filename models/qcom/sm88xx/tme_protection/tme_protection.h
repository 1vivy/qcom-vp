// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <systemc>
#include <cci_configuration>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm-extensions/mem-tx-attrs.h>
#include <ports/target-signal-socket.h>
#include <cpu.h>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <functional>
#include <utility>

namespace qcom_vp {

// Evidence: tme8850.bin, 0x20d03e..0x20d22a and 0x20cbac..0x20ce58.
// The ROM programs 0x40-byte regions at controller offset 0x1000.
class TmeProtection : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<TmeProtection> target_socket{"target_socket"};
    tlm_utils::simple_target_socket_optional<TmeProtection> mode_socket{"mode_socket"};
    tlm_utils::simple_target_socket_optional<TmeProtection> policy_control_socket{"policy_control_socket"};
    tlm_utils::simple_target_socket_optional<TmeProtection> apss_config_socket{"apss_config_socket"};
    cci::cci_param<uint32_t> config_owner_mask{"config_owner_mask", 0, "Trusted secure primary configuration capability mask"};
    cci::cci_param<uint32_t> apss_config_owner_mask{"apss_config_owner_mask", 0, "Trusted secure APSS configuration capability mask"};
    cci::cci_param<bool> config_secure_unspecified{"config_secure_unspecified", false,
        "Explicit secure attribution for the primary single-address-space requester"};
    cci::cci_param<unsigned> region_count{"region_count", 0, "Required number of protection regions"};
    cci::cci_param<unsigned> address_bits{"address_bits", 0, "Required protected address width"};
    cci::cci_param<unsigned> alignment_bits{"alignment_bits", 0, "Required protected range alignment"};
    cci::cci_param<uint64_t> aperture_size{"aperture_size", 0, "Declared client aperture size, or zero for explicit end bounds"};
    cci::cci_param<uint32_t> mode_reset{"mode_reset", 0, "Virtual protection mode at reset"};
    cci::cci_param<uint32_t> revision{"revision", 0, "Declared virtual protection revision"};
    cci::cci_param<unsigned> qad_count{"qad_count", 7, "Declared number of requester domains"};
    cci::cci_param<uint64_t> policy_control_reset{"policy_control_reset", UINT64_MAX,
        "Required initial word when the XBL policy-control socket is connected, not silicon reset evidence"};
    void register_permissions_changed_cb(std::function<void()> callback) {
        permission_callbacks_.push_back(std::move(callback));
    }

    bool permits(uint64_t address, uint64_t size, bool write, uint32_t identity) const {
        // Fixed-resource controllers require a client-resource mapping before they can authorize data accesses.
        if (fixed_resources_ || !size || size - 1 > UINT64_MAX - address) return false;
        const auto last = address + size - 1;
        for (unsigned i = 0; i < count_; ++i) {
            const auto& region = regions_[i];
            if (!(region.enabled & 1)) continue;
            const auto begin = region_begin(region);
            const auto end = region_end(region);
            if (address < end && last >= begin &&
                !(region.permissions(write) & identity)) {
                return false;
            }
        }
        return true;
    }

    bool constrain_dmi(uint64_t address, uint32_t identity, tlm::tlm_dmi& dmi) const {
        // Secure and nonsecure APSS share QBox's memory root and its DMI aliases.
        // Secure APSS also accepts nonsecure policy, so the latter is their intersection.
        if (identity == 0xc0000000U) identity = 0x40000000U;
        // A QEMU read-only RAM alias discards writes without a TLM permission fault.
        // Restricted mappings must retain the transaction path for denied accesses.
        if (!permits(address, 1, false, identity) || !permits(address, 1, true, identity)) return false;
        uint64_t begin = dmi.get_start_address();
        uint64_t end = dmi.get_end_address();
        for (unsigned i = 0; i < count_; ++i) {
            const auto& region = regions_[i];
            if (!(region.enabled & 1)) continue;
            const auto first = region_begin(region);
            const auto limit = region_end(region);
            if (address < first) end = std::min(end, first - 1);
            else if (address >= limit) begin = std::max(begin, limit);
            else {
                begin = std::max(begin, first);
                end = std::min(end, limit - 1);
            }
        }
        dmi.set_dmi_ptr(dmi.get_dmi_ptr() + (begin - dmi.get_start_address()));
        dmi.set_start_address(begin);
        dmi.set_end_address(end);
        return true;
    }

    explicit TmeProtection(sc_core::sc_module_name name) : TmeProtection(name, false) {}

protected:
    TmeProtection(sc_core::sc_module_name name, bool fixed_resources)
        : sc_module(name), fixed_resources_(fixed_resources) {
        target_socket.register_b_transport(this, &TmeProtection::primary_transport);
        apss_config_socket.register_b_transport(this, &TmeProtection::apss_transport);
        mode_socket.register_b_transport(this, &TmeProtection::mode_transport);
        policy_control_socket.register_b_transport(this, &TmeProtection::policy_control_transport);
    }

private:
    const bool fixed_resources_;
    struct Region {
        uint32_t configuration = 0;
        uint32_t enabled = 0;
        uint32_t start_high = 0;
        uint32_t start_low = 0;
        uint32_t end_high = 0;
        uint32_t end_low = 0;
        uint32_t read_permissions = 0;
        uint32_t write_permissions = 0;
        unsigned read_programmer = 2;
        unsigned write_programmer = 2;
        uint32_t access_control = 0;
        uint32_t held_read[2]{};
        uint32_t held_write[2]{};
        // A handoff seed stays visible to the authority that programmed it.
        uint32_t seed_read[2]{};
        uint32_t seed_write[2]{};

        uint32_t permissions(bool write) const {
            const auto* held = write ? held_write : held_read;
            return (write ? write_permissions : read_permissions) | held[0] | held[1];
        }
        uint32_t permissions(bool write, unsigned authority) const {
            const auto* held = write ? held_write : held_read;
            const auto* seed = write ? seed_write : seed_read;
            return (write ? write_permissions : read_permissions) | held[authority] |
                (seed[authority] & held[authority ^ 1]);
        }
    };
    static uint32_t revoke_permissions(uint32_t permissions, uint32_t released) {
        permissions &= ~(released & ~1U);
        // XBL domains zero and seven share bit zero, but use separate security qualifiers.
        if ((released & 0xc0000001U) && !(permissions & 0xc0000000U))
            permissions &= ~1U;
        return permissions;
    }

    static uint64_t region_begin(const Region& region) {
        return (uint64_t{region.start_high} << 32) | region.start_low;
    }
    uint64_t region_end(const Region& region) const {
        const auto end = (uint64_t{region.end_high} << 32) | region.end_low;
        return end ? end : aperture_size_;
    }
    std::vector<Region> regions_;
    unsigned count_ = 0;
    uint32_t geometry_ = 0;
    uint32_t mode_ = 0;
    uint32_t permission_mask_ = 0;
    uint32_t debug_access_ = 0;
    uint32_t config_error_reporting_ = 0;
    uint32_t client_error_reporting_ = 0;
    uint32_t policy_control_ = 0;
    uint32_t global_control_ = 0;
    uint64_t aperture_size_ = 0;
    std::vector<std::function<void()>> permission_callbacks_;

    void notify_permissions_changed() {
        // Queue CPU invalidation before the configuration transaction can return.
        // A delta event can run after QBox has already resumed the guest.
        for (const auto& callback : permission_callbacks_) callback();
    }

    void end_of_elaboration() override {
        count_ = region_count.get_value();
        const auto bits = address_bits.get_value();
        const auto qads = qad_count.get_value();
        const auto alignment = alignment_bits.get_value();
        if (!count_ || count_ > 1024 || !bits || bits > 63 || alignment >= bits ||
            !qads || qads > 11 ||
            (mode_reset.get_value() != 0 && mode_reset.get_value() != 0x0a)) {
            throw std::invalid_argument("TME protection requires a valid region geometry");
        }
        aperture_size_ = aperture_size.get_value();
        if (aperture_size_ && (fixed_resources_ || aperture_size_ > (uint64_t{1} << bits) ||
                              (aperture_size_ & (aperture_size_ - 1)) ||
                              aperture_size_ < (uint64_t{1} << alignment))) {
            throw std::invalid_argument("Protection aperture must fit the declared address width and alignment");
        }
        if (policy_control_socket.size()) {
            if (policy_control_reset.get_value() > UINT32_MAX)
                throw std::invalid_argument("XBL policy control requires an explicit 32-bit cold state");
            policy_control_ = static_cast<uint32_t>(policy_control_reset.get_value());
        }
        regions_.resize(count_);
        geometry_ = (bits << 24) | (bits << 16) | ((bits - 1) << 8) | alignment;
        // OEM 0x443730 maps domains zero through ten. Domain seven has no permission bit.
        permission_mask_ = 0xc0000000U | (((uint32_t{1} << qads) - 1) & ~0x80U);
        if ((config_owner_mask.get_value() | apss_config_owner_mask.get_value()) & ~permission_mask_)
            throw std::invalid_argument("Protection configuration capability exceeds declared permission bits");
        if (config_owner_mask.get_value() & apss_config_owner_mask.get_value())
            throw std::invalid_argument("Protection configuration capabilities must not overlap");
        mode_ = mode_reset.get_value();
    }

    static bool valid(tlm::tlm_generic_payload& tx) {
        tx.set_dmi_allowed(false);
        if (tx.get_byte_enable_ptr()) {
            tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            return false;
        }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4 ||
            (tx.get_address() & 3)) {
            tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    static uint32_t load(const tlm::tlm_generic_payload& tx) {
        const auto* data = tx.get_data_ptr();
        return uint32_t{data[0]} | (uint32_t{data[1]} << 8) |
            (uint32_t{data[2]} << 16) | (uint32_t{data[3]} << 24);
    }

    static void store(tlm::tlm_generic_payload& tx, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) tx.get_data_ptr()[i] = static_cast<unsigned char>(value >> (8 * i));
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    void mode_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.get_address()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            const auto value = load(tx);
            if (value != 0 && value != 0x0a) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            mode_ = value;
        }
        store(tx, mode_);
    }

    // XBL 0x14858ca0 performs a masked write and verifies full-word readback.
    // This configuration word does not grant debug access or override region locks.
    void policy_control_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&) {
        if (!valid(tx)) return;
        if (tx.get_address()) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) policy_control_ = load(tx);
        store(tx, policy_control_);
    }

    void primary_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        transport(tx, delay, config_owner_mask.get_value(), config_secure_unspecified.get_value(), 0);
    }

    void apss_transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        transport(tx, delay, apss_config_owner_mask.get_value(), false, 1);
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time&, uint32_t owner_mask,
                   bool secure_unspecified, unsigned authority) {
        if (!valid(tx)) return;
        const auto address = tx.get_address();
        const auto* attributes = tx.get_extension<QemuMemTxAttrsExtension>();
        const bool secure = attributes && !attributes->attrs.unspecified ?
            attributes->attrs.secure : secure_unspecified;
        const uint32_t requester = secure ? owner_mask & permission_mask_ : 0;
        if (address == 0x100 && tx.is_write()) {
            const auto value = load(tx);
            // Enabling namespaces cannot remove an existing lock or permission.
            // A live controller cannot return to legacy programming through GCR.
            if (!requester || (value & ~3U) || ((global_control_ & 1) && !(value & 1))) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (value != global_control_) {
                global_control_ = value;
                notify_permissions_changed();
            }
            store(tx, global_control_);
            return;
        }
        if (address == 0x208 || address == 0x20c) {
            // The error-reporting controls at 0x208 and 0x20c do not grant access.
            // The selected consumer masks 0xc00007ff at 0x14859f80.
            // The source-only names CFGERE and CLERE remain unestablished.
            auto& reporting = address == 0x208 ? config_error_reporting_ : client_error_reporting_;
            if (tx.is_write()) {
                const auto mask = 0xc0000000U | ((uint32_t{1} << qad_count.get_value()) - 1);
                reporting = load(tx) & mask;
            }
            store(tx, reporting);
            return;
        }
        if (address == 0x304) {
            // The source-only block describes DBGAR security bits and a QAD field.
            // Debug access does not replace ordinary region permissions.
            // The field positions come only from that definition.
            if (tx.is_write()) {
                const auto value = load(tx);
                if (value & ~permission_mask_) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                debug_access_ = value;
            }
            store(tx, debug_access_);
            return;
        }
        if (address == 0 || address == 4 || address == 8 || address == 0xc ||
            address == 0x100 || address == 0x104) {
            if (tx.is_write()) {
                tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            // ROM 0x20cf8c..0x20cfa0 extracts the region count minus one from bits 25:16.
            uint32_t value = address == 0 ? (count_ - 1) << 16 : geometry_;
            if (address == 0 && fixed_resources_) value |= 1;
            // OEM 0x425c72 consumes the QAD count directly. REV uses the virtual profile.
            if (address == 8) value = qad_count.get_value();
            if (address == 0xc) value = revision.get_value();
            if (address == 0x100) value = global_control_;
            if (address == 0x104) {
                unsigned first = count_;
                unsigned free = 0;
                for (unsigned i = 0; i < count_; ++i) {
                    if (!(regions_[i].enabled & 1)) {
                        if (first == count_) first = i;
                        ++free;
                    }
                }
                value = free ? (first << 16) | free : 0;
            }
            store(tx, value);
            return;
        }
        if (address < 0x1000 || address >= 0x1000 + count_ * 0x40) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        auto& region = regions_[(address - 0x1000) / 0x40];
        uint32_t* word = nullptr;
        switch ((address - 0x1000) % 0x40) {
        case 0x00: word = &region.configuration; break;
        case 0x04: word = &region.enabled; break;
        case 0x08: word = &region.start_high; break;
        case 0x0c: word = &region.start_low; break;
        case 0x10: word = &region.end_high; break;
        case 0x14: word = &region.end_low; break;
        case 0x18:
            if (tx.is_read()) { store(tx, region.permissions(false, authority)); return; }
            word = &region.read_permissions;
            break;
        case 0x1c:
            if (tx.is_read()) { store(tx, region.permissions(true, authority)); return; }
            word = &region.write_permissions;
            break;
        case 0x30: word = &region.access_control; break;
        default:
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (tx.is_write()) {
            auto value = load(tx);
            const auto offset = (address - 0x1000) % 0x40;
            if (global_control_ & 1) {
                if (!requester || (offset == 0 && (value & 1))) {
                    // OEM 0x426b34 rejects write-once requests in namespace mode.
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
                if (offset == 0x30) {
                    // XBL and QSEE release a class with the capability mask minus that class.
                    // Qualifiers without QAD zero can retain an owner, but cannot acquire another.
                    // A retained qualifier keeps its shared QAD-zero bit. QSEE 0x157b9548 reads it back.
                    if (!(value & 1)) {
                        value &= ~(0xc0000000U & ~region.access_control);
                        if ((value & 0xc0000000U) && (region.access_control & 1)) value |= 1;
                    }
                    // A TME release of an owned region frees it for every owner. TME finalizer
                    // 0x438b56 releases image ranges this way. QSEE 0x15762064 then skips them.
                    const bool primary_free = authority == 0 && !(value & requester) &&
                        (region.access_control & requester);
                    value = primary_free ? 0 :
                        (region.access_control & ~requester) | (value & requester & permission_mask_);
                    if (value != region.access_control) {
                        const auto released = region.access_control & ~value;
                        const auto acquired = value & ~region.access_control;
                        const auto previous_read = region.permissions(false, authority);
                        const auto previous_write = region.permissions(true, authority);
                        // An unowned policy programmed by another authority is a handoff seed.
                        // Its first owner retains that policy instead of duplicating it in the base.
                        const bool handoff = !region.access_control && acquired;
                        const auto read = handoff && region.read_programmer != authority ?
                            0 : revoke_permissions(region.read_permissions, released);
                        const auto write = handoff && region.write_programmer != authority ?
                            0 : revoke_permissions(region.write_permissions, released);
                        // Namespace acquisition retains the current policy until this authority releases it.
                        const auto held_read = !(value & requester) ? 0 :
                            revoke_permissions(acquired ? previous_read : region.held_read[authority], released);
                        const auto held_write = !(value & requester) ? 0 :
                            revoke_permissions(acquired ? previous_write : region.held_write[authority], released);
                        if (region.configuration &&
                            (read != region.read_permissions || write != region.write_permissions ||
                             (read | held_read) != previous_read || (write | held_write) != previous_write)) {
                            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                            return;
                        }
                        if (handoff && region.read_programmer < 2 && region.read_programmer != authority)
                            region.seed_read[region.read_programmer] = region.read_permissions;
                        if (handoff && region.write_programmer < 2 && region.write_programmer != authority)
                            region.seed_write[region.write_programmer] = region.write_permissions;
                        region.read_permissions = read;
                        region.write_permissions = write;
                        region.held_read[authority] = held_read;
                        region.held_write[authority] = held_write;
                        if (primary_free) region.held_read[1] = region.held_write[1] = 0;
                        region.access_control = value;
                        // OEM 0x426264 releases namespace ownership without a separate disable.
                        // Only an unlocked region with no owner or effective policy can retire.
                        if (!value && !region.configuration &&
                            !region.permissions(false) && !region.permissions(true))
                            region.enabled = 0;
                        notify_permissions_changed();
                    }
                    store(tx, region.access_control);
                    return;
                }
                // Foreign owners still prevent direct configuration changes.
                // Ownership release can revoke data permission, but cannot grant it.
                if (region.access_control) {
                    store(tx, *word);
                    return;
                }
            }
            if (offset == 0 || offset == 4) value &= 1;
            if (offset == 0x18 || offset == 0x1c || offset == 0x30) value &= permission_mask_;
            // RGWOWP protects range, enable, and permission state until reset.
            // The ROM writes the configuration owner after it sets RGWOWP.
            if (region.configuration && offset != 0x30) {
                store(tx, *word);
                return;
            }
            if (!fixed_resources_ && offset == 4 && value) {
                const auto end = region_end(region);
                if (end <= region_begin(region) || (aperture_size_ && end > aperture_size_)) {
                    tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                    return;
                }
            }
            const bool changed = *word != value &&
                (offset == 4 || ((region.enabled & 1) && offset >= 8 && offset <= 0x1c));
            *word = value;
            if (offset == 0x18) {
                region.read_programmer = authority;
                region.seed_read[0] = region.seed_read[1] = 0;
            }
            if (offset == 0x1c) {
                region.write_programmer = authority;
                region.seed_write[0] = region.seed_write[1] = 0;
            }
            if (changed) notify_permissions_changed();
        }
        store(tx, *word);
    }
};

// OEM 0x426102 omits address programming when IDR0 selects fixed resources.
class XpuFixedProtection : public TmeProtection {
public:
    explicit XpuFixedProtection(sc_core::sc_module_name name) : TmeProtection(name, true) {}
};

// A port assigns a virtual QAD to one initiator without changing its security attributes.
// ROM 0x212fd4 maps its three permission classes to 0x80000001, 2, and 4.
class TmeProtectionPort : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<TmeProtectionPort> target_socket{"target_socket"};
    tlm_utils::simple_initiator_socket<TmeProtectionPort> initiator_socket{"initiator_socket"};
    cci::cci_param<unsigned> qad{"qad", 0, "Virtual requester domain"};
    TargetSignalSocket<bool> runtime_phase{"runtime_phase"};
    cci::cci_param<unsigned> runtime_qad{"runtime_qad", UINT32_MAX, "Optional requester domain after runtime activation"};
    cci::cci_param<bool> secure_unspecified{"secure_unspecified", false, "Security state for a single address space"};
    cci::cci_param<uint64_t> protected_base{"protected_base", 0, "Client aperture base in this initiator address space"};
    cci::cci_param<uint64_t> protected_size{"protected_size", 0, "Client aperture size, or zero for the complete address space"};

    TmeProtectionPort(sc_core::sc_module_name name, sc_core::sc_object* protection, sc_core::sc_object* cpu)
        : sc_module(name), protection_(dynamic_cast<TmeProtection*>(protection)), cpu_(dynamic_cast<QemuCpu*>(cpu)) {
        if (!protection_ || !cpu_) throw std::invalid_argument("Protection port requires a controller and a CPU");
        target_socket.register_b_transport(this, &TmeProtectionPort::transport);
        target_socket.register_transport_dbg(this, &TmeProtectionPort::debug);
        target_socket.register_get_direct_mem_ptr(this, &TmeProtectionPort::dmi);
        initiator_socket.register_invalidate_direct_mem_ptr(this, &TmeProtectionPort::invalidate);
        protection_->register_permissions_changed_cb([this] { permissions_changed(); });
        runtime_phase.register_value_changed_cb([this](const bool& active) {
            const auto selected = active ? runtime_qad.get_value() : qad.get_value();
            if (selected > 6) throw std::invalid_argument("Runtime protection requires a requester domain from zero through six");
            const auto next = uint32_t{1} << selected;
            if (next != domain_) {
                domain_ = next;
                permissions_changed();
            }
        });
    }

private:
    TmeProtection* protection_;
    QemuCpu* cpu_;
    uint32_t domain_ = 0;
    uint64_t base_ = 0;
    uint64_t size_ = 0;
    uint64_t last_ = 0;

    void end_of_elaboration() override {
        if (qad.get_value() > 6) throw std::invalid_argument("Protection requester domain exceeds six");
        domain_ = uint32_t{1} << qad.get_value();
        base_ = protected_base.get_value();
        size_ = protected_size.get_value();
        if ((!size_ && base_) || (size_ && base_ > UINT64_MAX - (size_ - 1)))
            throw std::invalid_argument("Protection aperture must form a valid address range");
        if (size_) last_ = base_ + size_ - 1;
    }

    uint32_t identity(const tlm::tlm_generic_payload& tx) const {
        const auto* attributes = tx.get_extension<QemuMemTxAttrsExtension>();
        const bool secure = attributes && !attributes->attrs.unspecified ?
            attributes->attrs.secure : secure_unspecified.get_value();
        // Secure APSS also consumes nonsecure policy, as XBL does at TCSR offset 0xd504c.
        return domain_ == 1 ? (secure ? 0xc0000000U : 0x40000000U) : domain_;
    }

    bool permits(uint64_t address, uint64_t size, bool write, uint32_t requester) const {
        if (!size_) return protection_->permits(address, size, write, requester);
        if (!size || size - 1 > UINT64_MAX - address) return false;
        const auto end = address + size - 1;
        if (end < base_ || address > last_) return true;
        const auto first = std::max(address, base_);
        return protection_->permits(first - base_, std::min(end, last_) - first + 1, write, requester);
    }

    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        if ((!tx.is_read() && !tx.is_write()) ||
            !permits(tx.get_address(), tx.get_data_length(), tx.is_write(), identity(tx))) {
            tx.set_dmi_allowed(false);
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        initiator_socket->b_transport(tx, delay);
    }

    unsigned debug(tlm::tlm_generic_payload& tx) {
        // Debugger inspection does not grant a guest mapping or change permissions.
        return initiator_socket->transport_dbg(tx);
    }

    bool dmi(tlm::tlm_generic_payload& tx, tlm::tlm_dmi& region) {
        const auto address = tx.get_address();
        const auto requester = identity(tx);
        if (!permits(address, 1, tx.is_write(), requester) ||
            !initiator_socket->get_direct_mem_ptr(tx, region)) return false;
        if (!size_) return protection_->constrain_dmi(address, requester, region);

        const bool inside = address >= base_ && address <= last_;
        uint64_t first = region.get_start_address();
        uint64_t end = region.get_end_address();
        if (address < base_) end = std::min(end, base_ - 1);
        else if (address > last_) first = std::max(first, last_ + 1);
        else {
            first = std::max(first, base_);
            end = std::min(end, last_);
        }
        region.set_dmi_ptr(region.get_dmi_ptr() + (first - region.get_start_address()));
        region.set_start_address(first);
        region.set_end_address(end);
        if (!inside) return true;

        region.set_start_address(first - base_);
        region.set_end_address(end - base_);
        const bool allowed = protection_->constrain_dmi(address - base_, requester, region);
        region.set_start_address(region.get_start_address() + base_);
        region.set_end_address(region.get_end_address() + base_);
        return allowed;
    }

    void invalidate(sc_dt::uint64 begin, sc_dt::uint64 end) {
        target_socket->invalidate_direct_mem_ptr(begin, end);
    }

    void permissions_changed() {
        // QEMU invalidation runs under the receiving instance's lock.
        cpu_->initiator_async_run([this] { invalidate(0, UINT64_MAX); });
    }
};

}

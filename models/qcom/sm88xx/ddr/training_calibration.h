// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string_view>
#include "training_context.h"

namespace qcom_vp {
namespace training_calibration_detail {

// Ideal electrical policy: retain absolute settings and apply zero correction.
// Register facts: selected SM8850 SHRM 0x2076ba, 0x207a90, 0x21d6bc,
// 0x21ad54, 0x209924, 0x221304. Tables remain in guest SRAM, not this model.
// The caller runs the digital comparison before either PHY_SED callback.
// RD_Low_Freq_Write initializes electrical state before the digital write.
// MR operations use the existing MC command engine. Copies preserve the native
// FSP switch fields. The dispatcher owns the effective frequency transition.
// Evidence: DIT 0x221454/0x22187e, MR caches 0x211156/0x2124d8,
// HCD 0x214018/0x214b66, and eye serializers 0x220726/0x2172d6.
// The ideal eye describes electrical margin, not an unexecuted memory test.
template<class Context> class Calibration {
    Context& x;
    std::uint8_t bank = 0;
    std::uint8_t write_pins = 8;
    bool output_overflow = false;
    std::uint16_t last_context[2] = {0xffff, 0xffff};
    std::uint8_t last_pin[2] = {0xff, 0xff};

    static std::uint64_t displacement(unsigned c) {
        return ((c & 1U) << 24) | ((c & 2U) << 21);
    }
    static std::uint64_t phy(unsigned c) { return 0x31b80000ULL + displacement(c); }
    static std::uint64_t mc(unsigned c) { return 0x31a00000ULL + displacement(c); }
    bool selected(unsigned c, unsigned r) const {
        return (x.channels & (1U << c)) && (x.ranks & (1U << r));
    }
    bool half(unsigned c, unsigned b, unsigned& h) {
        std::uint8_t value;
        if (!x.read8(x.table(0x400c1c + 2*c + b), value)) return false;
        h = value != 0;
        return true;
    }
    bool pin(unsigned c, unsigned b, unsigned bit, unsigned& p) {
        std::uint8_t value;
        if (!x.read8(x.table(0x400ac8 + 18*c + 9*b + bit), value) || value > 8) return false;
        p = value;
        return true;
    }
    std::uint64_t profile(unsigned c, unsigned h, unsigned p, unsigned index) const {
        return phy(c) + (h == 2 ? 0x74fc : h * 0x3000) + 4ULL*(0x7d*p + index);
    }
    bool field(unsigned c, unsigned h, unsigned index, unsigned shift,
               unsigned mask, unsigned& value, unsigned p) {
        std::uint32_t word;
        if (!x.read32(profile(c,h,p,index), word)) return false;
        value = (word >> shift) & mask;
        return true;
    }
    bool emit(unsigned id, unsigned cls, unsigned c, unsigned r, unsigned b,
              unsigned bit, std::int32_t value) {
        std::uint8_t width, destination;
        if (!x.read8(x.local(0x4001ce + 4*id), width) ||
            !x.read8(x.local(0x4001cf + 4*id), destination)) return false;
        const unsigned stream = destination == 'e' ? 0 : 1;
        const std::uint16_t context = ((r << 4 | c) << 8) | (x.level << 4 | cls);
        const std::uint8_t lane = 0x40 | (bit << 2) | b;
        unsigned char bytes[8];
        unsigned n = 0;
        if (last_context[stream] != context) {
            bytes[n++] = context >> 8;
            bytes[n++] = context & 0xff;
        }
        if (last_pin[stream] != lane) bytes[n++] = lane;
        bytes[n++] = 0x80 | id;
        const std::uint32_t magnitude = value < 0 ? -std::int64_t(value) : value;
        if ((width == 9 && magnitude > 0x1ff) ||
            (width != 9 && width != 16 && magnitude > 0xff) || magnitude > 0xffff) return false;
        bytes[n++] = value < 0 ? 0xff : (width == 9 ? 0xfe | (magnitude >> 8) : 0xfe);
        bytes[n++] = magnitude & 0xff;
        if (width == 16) bytes[n++] = magnitude >> 8;
        constexpr unsigned limit = Context::log_capacity - 4;
        const unsigned available = x.counts[stream] < limit ? limit - x.counts[stream] : 0;
        const unsigned stored = n < available ? n : available;
        if (stored && !x.write(x.logs[stream] + x.counts[stream], bytes, stored)) return false;
        if (stored != n && !output_overflow) {
            if (!x.write8(x.local(0x405490), 1) || !x.write8(x.local(0x403400), 1)) return false;
            output_overflow = true;
        }
        x.counts[stream] += n;
        last_context[stream] = context;
        last_pin[stream] = lane;
        return true;
    }

    // Use the existing MC command path. Restore its descriptor and selection.
    bool mode(unsigned c, unsigned r, unsigned mr, std::uint8_t& value, bool write, unsigned operation=0) {
        const auto base = mc(c);
        std::uint32_t command, companion, selection, busy;
        if (!x.read32(base+0x5804,busy) || busy ||
            !x.read32(base+0x5700,command) || !x.read32(base+0x5704,companion) ||
            !x.read32(base+0x5800,selection)) return false;
        const auto request = (operation ? operation : write ? 0x74U : 0x70U) | (1U << (12+r)) |
                             (mr << 16) | (write ? unsigned(value) << 24 : 0);
        bool ok = x.write32(base+0x5700,request) && x.write32(base+0x5704,0) &&
                  x.write32(base+0x5800,1) && x.write32(base+0x5804,1) &&
                  x.read32(base+0x5804,busy) && !busy;
        if (ok && !write) {
            std::uint32_t result;
            ok = x.read32(base+0x5a38+4*r,result) && result != 0xffffffff;
            if (ok) value = result & 0xff;
        }
        const bool restore_command = x.write32(base+0x5700,command);
        const bool restore_companion = x.write32(base+0x5704,companion);
        const bool restore_selection = x.write32(base+0x5800,selection);
        return ok && restore_command && restore_companion && restore_selection;
    }
    bool cache(unsigned address, unsigned c, unsigned r, std::uint8_t& value, bool write) {
        const auto a = x.local(address + 0x3d*x.fsp + 2*c + r);
        return write ? x.write8(a,value) : x.read8(a,value);
    }
    bool abort(unsigned code, unsigned c, unsigned r, unsigned b, unsigned bit=0) {
        // Native fatal record, separate from successful result byte counts.
        x.write32(x.local(0x4029a0),code);
        x.write32(x.local(0x4029a4),x.level);
        x.write32(x.local(0x4029a8),c);
        x.write32(x.local(0x4029ac),r);
        x.write32(x.local(0x4029b0),b);
        x.write32(x.local(0x4029b4),bit);
        return false;
    }
    bool vref(bool io, bool copy) {
        for (unsigned c=0;c<4;++c) {
            if (!(x.channels & (1U<<c))) continue;
            for (unsigned b=0;b<2;++b) {
                unsigned h, value;
                if (io) h=b; // IO qualification addresses the physical halves directly.
                else if (!half(c,b,h)) return false;
                const auto a = profile(c,h,x.profile,0x61);
                const unsigned shift = io ? 20 : 8;
                if (copy) {
                    std::uint8_t v;
                    if (io && !(x.ranks & 1)) continue; // Native copy has no rank-one-only branch.
                    const unsigned r = io && x.ranks == 3 ? 1 : 0;
                    const auto s = x.scratch + (io ? 0x3788+4*c+2*r+b : 0x3778+2*c+b);
                    if (!x.read8(s,v) || !x.update32(a,0x3fU<<shift,unsigned(v)<<shift)) return false;
                    continue;
                }
                if (!field(c,h,0x61,shift,0x3f,value,x.profile) ||
                    !x.update32(phy(c)+h*0x3000+0x2314+4*bank,0x3fU<<shift,value<<shift)) return false;
                for (unsigned r=0;r<2;++r) {
                    if (!selected(c,r)) continue;
                    const auto s = x.scratch + (io ? 0x3788+4*c+2*r+b : 0x3778+2*c+b);
                    if (!x.write8(s,value)) return false;
                }
            }
        }
        return true;
    }
    bool local_vref(bool copy) {
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            for (unsigned b=0;b<2;++b) for (unsigned bit=0;bit<8;++bit) {
                unsigned h,p;
                std::uint16_t raw = 0;
                if (!half(c,b,h) || !pin(c,b,bit,p)) return false;
                const auto s=x.scratch+0x3798+64*c+32*r+16*b+2*bit;
                if (copy ? !x.read16(s,raw) : !x.write16(s,0)) return false;
                const auto correction = static_cast<std::int16_t>(raw);
                const int clamped = correction < -7 ? -7 : correction > 8 ? 8 : correction;
                std::uint8_t encoded;
                if (!x.read8(x.table(0x400b3f+clamped),encoded) ||
                    !x.update32(profile(c,h,x.profile,0x72)-4*p,0x3fU<<(8*r),unsigned(encoded)<<(8*r))) return false;
                if (!copy && !x.update32(phy(c)+h*0x3000+0x26e0-16*p+4*bank,
                                         0x3fU<<(8*r),unsigned(encoded)<<(8*r))) return false;
            }
        }
        return true;
    }
    bool timing(bool write, bool low=false) {
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            for (unsigned b=0;b<2;++b) {
                unsigned h;
                if (!half(c,b,h)) return false;
                const auto lane=8*c+4*r+2*b;
                if (!write && !low && !x.write16(x.scratch+0x39b8+lane,0x31)) return false;
                if (!write) {
                    unsigned v;
                    if (!field(c,h,0x26,8*r,0x7f,v,x.profile) ||
                        !x.write16(x.scratch+0x3898+lane,low ? 0 : v)) return false;
                    if (!low && !x.update32(phy(c)+0x1a40+h*0x3000+24*bank,0x7fU<<(8*r),v<<(8*r))) return false;
                }
                for (unsigned bit=0;bit<(write ? write_pins : 8U);++bit) {
                    unsigned p,v;
                    if (!pin(c,b,bit,p)) return false;
                    const unsigned shift=(write ? 0 : 16)+8*r;
                    const auto a=profile(c,h,x.profile,0x21)-16*p;
                    // Per-pin words precede the base register.
                    std::uint32_t word;
                    if (!x.read32(a,word)) return false;
                    v=(word>>shift)&0x7f;
                    if (low) v=12;
                    const auto s=x.scratch+(write ? (bit==8 ? 0x3890+lane : 0x3790+64*c+32*r+16*b+2*bit)
                                                              : 0x38b8+64*c+32*r+16*b+2*bit);
                    if (!x.write16(s,v)) return false;
                    if (low) {
                        for (unsigned profile_id=0;profile_id<=10;++profile_id)
                            if (!x.update32(profile(c,h,profile_id,0x21)-16*p,0x7fU<<shift,v<<shift)) return false;
                    } else if (!x.update32(phy(c)+h*0x3000+0x19fc-60*p+16*bank,0x7fU<<shift,v<<shift)) return false;
                }
            }
        }
        return true;
    }
    bool low_read() {
        // Native 0x21d6bc uses S+0x3778 as its base.
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            for (unsigned b=0;b<2;++b) {
                const auto lane=8*c+4*r+2*b;
                if (!x.write8(x.scratch+0x3778+2*c+b,0) ||
                    !x.write8(x.scratch+0x3780+2*c+b,0) ||
                    !x.write16(x.scratch+0x3898+lane,0) ||
                    !x.write16(x.scratch+0x3a18+lane,0)) return false;
                for (unsigned bit=0;bit<8;++bit) {
                    const auto offset=64*c+32*r+16*b+2*bit;
                    if (!x.write16(x.scratch+0x3798+offset,0) ||
                        !x.write16(x.scratch+0x3a38+offset,0)) return false;
                }
            }
        }
        return timing(false,true);
    }
    bool write_vref(bool clamp) {
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            for (unsigned b=0;b<2;++b) {
                std::uint8_t value;
                const unsigned cache_base=b ? 0x403888 : 0x403880;
                if (!cache(cache_base,c,r,value,false)) return false;
                if (value < 11 || value > 127) {
                    if (!mode(c,r,14+b,value,false)) return false;
                }
                if (clamp) value=value<0x46 ? 0x46 : value>0x5a ? 0x5a : value;
                if (value<11 || value>127) return abort(7,c,r,b);
                if (!x.write8(x.scratch+0x3778+4*c+2*r+b,value) ||
                    !cache(cache_base,c,r,value,true) || !mode(c,r,14+b,value,true)) return false;
            }
        }
        return true;
    }
    bool upper_fsp(unsigned address, bool pair) {
        if (x.fsp>=7) return false;
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            for (unsigned b=0;b<(pair ? 2U : 1U);++b) {
                std::uint8_t value;
                const auto dst=x.local(address+8*b+0x3d*x.fsp+2*c+r);
                if (!x.read8(dst+0x3d,value) || !x.write8(dst,value)) return false;
                unsigned mr=pair ? 14+b : address==0x403894 ? 30 : 69;
                if (!mode(c,r,mr,value,true)) return false;
            }
        }
        std::uint32_t period, frequency, resource_period;
        std::uint8_t target_fsp, mr18;
        if (!x.read32(x.local(0x402cd8),period) || !x.read32(x.local(0x402cd0),frequency) ||
            !x.read32(x.local(0x403380),resource_period) ||
            !x.read8(x.local(0x402f34),target_fsp) ||
            !x.read8(x.local(0x402fa2+6*target_fsp),mr18) ||
            (period << ((mr18&0x80) ? 1 : 2)) < resource_period) return false;
        return x.write8(x.local(0x4033fe),1) && x.write8(x.local(0x403a99),1) &&
               x.write32(x.local(0x403a94),period) && x.write32(x.local(0x403354),frequency);
    }
    bool dram_dcc() {
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            std::uint8_t zero=0;
            if (!cache(0x403894,c,r,zero,true) || !mode(c,r,30,zero,true)) return false;
        }
        return true;
    }
    bool dfe() {
        std::uint8_t capability, manufacturer, revision;
        if (!mode(0,0,24,capability,false) || !x.read8(x.local(0x402830),manufacturer) ||
            !x.read8(x.local(0x40283d),revision)) return false;
        if (!(capability&0x80) || (manufacturer==1 && (revision&15)!=10)) return true;
        if (!write_vref(false)) return false;
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            for (unsigned b=0;b<2;++b) {
                if (!x.write8(x.scratch+0x4ad8+4*c+2*r+b,0) ||
                    !x.fill(x.scratch+0x4ae8+32*c+16*r+8*b,8)) return false;
            }
            std::uint8_t zero=0;
            if (!mode(c,r,24,zero,true) || !cache(0x4038a5,c,r,zero,true)) return false;
            for (unsigned f=5;f<=7;++f)
                if (!x.write8(x.local(0x4038a5+0x3d*f+2*c+r),0)) return false;
            for (unsigned i=0;i<4;++i)
                if (!mode(c,r,70+i,zero,true) || !x.write8(x.local(0x403a58+8*i+0x3d*x.fsp+2*c+r),0)) return false;
        }
        return timing(true);
    }

    bool dit(bool periodic) {
        // 0x221454 conditionally selects a new interval, then always samples.
        // The command engine supplies the virtual oscillator, including errors.
        std::uint8_t type, ratio, slot, configured_slot, samples;
        std::uint32_t frequency, period;
        std::uint16_t interval;
        if (!x.read8(x.local(0x402831),type) ||
            !x.read8(x.local(0x4031f6+x.level),ratio) ||
            !x.read8(x.local(0x403364),slot) || slot>3 ||
            !x.read8(x.local(0x40336e + x.level),configured_slot) ||
            !x.read8(x.parameters+0xc30,samples) || samples<=2 || samples>15 ||
            !x.read32(x.local(0x402cbc+20*x.level),frequency) ||
            !x.read32(x.local(0x403a90),period) || !period) return false;
        const unsigned divisor=type==8 ? (ratio==3 ? 4 : 2) : 1;
        if (frequency<divisor) return false;
        const bool choose=!periodic || slot==configured_slot;
        const auto encode_duration=[](unsigned cycles) -> std::uint8_t {
            if (cycles<1024 && !(cycles%16)) return cycles/16;
            if (cycles==2048) return 0x40;
            if (cycles==4096) return 0x80;
            if (cycles==8192) return 0xc0;
            return 0;
        };
        if (choose) {
            // Native 0x22147a increases the interval until all margins reach 20.
            for (const unsigned candidate : {128U,256U,512U,1008U,2048U,4096U,8192U}) {
                interval=candidate;
                const unsigned wait=interval*0x4b00U/(frequency/divisor)+1;
                if (wait>0xffff || !x.write16(x.parameters+0x600+2*x.level,interval) ||
                    !x.write16(x.parameters+0x618+2*x.level,wait) ||
                    !x.write32(x.local(0x4043c4+4*x.level),wait)) return false;
                bool sufficient=true;
                for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
                    if (!selected(c,r)) continue;
                    std::uint8_t duration=encode_duration(interval),zero=0,lo,hi;
                    if (!mode(c,r,37,duration,true) || !mode(c,r,0,zero,true,0x94) ||
                        !mode(c,r,35,lo,false) || !mode(c,r,36,hi,false)) return false;
                    const unsigned count=unsigned(lo)|(unsigned(hi)<<8);
                    std::uint16_t scale;
                    if (!count || !x.read16(x.local(0x4040e0+8*x.level+2*c),scale)) return false;
                    const auto numerator=std::uint64_t(divisor)*period*interval*1000;
                    const auto denominator=numerator/count+2*scale;
                    if (!denominator) return false;
                    if (count-numerator/denominator<20) sufficient=false;
                }
                if (sufficient) break;
            }
            if (!emit(0x34,9,15,3,3,15,interval)) return false;
        } else if (!x.read16(x.parameters+0x600+2*x.level,interval) || !interval) return false;
        std::uint8_t duration=encode_duration(interval);
        if (!duration) return false;
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            std::uint8_t mr19, lanes, lo, hi, zero=0;
            if (!x.read8(x.local(0x402fa3+6*x.fsp),mr19) ||
                !x.read8(x.local(0x402839+r),lanes) || lanes>1) return false;
            mr19 &= ~0x10U;
            if ((!periodic && !mode(c,r,19,mr19,true)) || !mode(c,r,37,duration,true) ||
                !mode(c,r,0,zero,true,0x94) ||
                !mode(c,r,35,lo,false) || !mode(c,r,36,hi,false)) return false;
            const unsigned count=unsigned(lo)|(unsigned(hi)<<8);
            if (!count) return false;
            const std::uint64_t numerator=std::uint64_t(divisor)*period*interval;
            const unsigned measured=numerator/(2*count);
            // Native result 0x35 rejects periods outside 100..880.
            if (measured<100 || measured>880) {
                emit(0x35,9,c,r,0,15,measured);
                return abort(2,c,r,0);
            }
            std::uint16_t scale;
            if (!x.read16(x.local(0x4040e0+8*x.level+2*c),scale)) return false;
            const auto denominator=numerator*1000/count+2*scale;
            if (!denominator) return false;
            const unsigned margin=count-numerator*1000/denominator;
            for (unsigned b=0;b<=lanes;++b) {
                const auto a=x.parameters+128*x.level+32*slot+8*c+4*r+2*b;
                if (!x.write16(a,count) || !x.write16(a+0xc32,measured) ||
                    !x.write16(a+0x630,margin) || !emit(0x35,9,c,r,b,15,measured)) return false;
                if (!periodic && x.profile==0) {
                    std::uint16_t retained;
                    if (!x.read16(x.parameters+32*slot+8*c+4*r+2*b+0xcb2,retained)) return false;
                    if (retained>400) {
                        unsigned h;
                        if (!half(c,b,h)) return false;
                        for (unsigned bit=0;bit<=8;++bit) {
                            unsigned p;
                            if (!pin(c,b,bit,p) ||
                                !x.update32(profile(c,h,0,0x21)-16*p,0x7fU<<(8*r),0)) return false;
                        }
                    }
                }
            }
        }
        return true;
    }

    bool write_correction() {
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            for (unsigned b=0;b<2;++b) for (unsigned bit=0;bit<write_pins;++bit) {
                unsigned h,p;
                if (!half(c,b,h) || !pin(c,b,bit,p) ||
                    !x.update32(profile(c,h,x.profile,0x23)-16*p,0x1f001fU<<(8*r),0) ||
                    !x.update32(phy(c)+h*0x3000+0x1a04-60*p+16*bank,0x1f001fU<<(8*r),0)) return false;
            }
        }
        return true;
    }

    bool eye(bool write) {
        // Explicit ideal electrical eye: acceptance range is CDC taps [0,112].
        // Retain the absolute timing and Vref. Report the resulting margins.
        if (!timing(write) || (write && !write_vref(false))) return false;
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            for (unsigned b=0;b<2;++b) {
                unsigned left=112,right=112;
                for (unsigned bit=0;bit<8;++bit) {
                    std::uint16_t tap;
                    const auto a=x.scratch+(write ? 0x3790 : 0x38b8)+64*c+32*r+16*b+2*bit;
                    if (!x.read16(a,tap) || tap>112) return false;
                    if (tap<left) left=tap;
                    if (112-tap<right) right=112-tap;
                    if (!emit(0,write ? 0 : 1,c,r,b,bit,112)) return false;
                }
                if (!emit(0,write ? 0 : 1,c,r,b,15,112) ||
                    !emit(0x11,write ? 0 : 1,c,r,b,15,left) ||
                    !emit(0x12,write ? 0 : 1,c,r,b,15,right)) return false;
                std::uint8_t vref;
                if (!x.read8(x.scratch+0x3778+(write ? 4*c+2*r+b : 2*c+b),vref) ||
                    !emit(0xe,write ? 0 : 1,c,r,b,15,vref)) return false;
                for (unsigned id : {2U,3U,0x13U,0x15U})
                    for (unsigned bit=0;bit<8;++bit)
                        if (!emit(id,write ? 0 : 1,c,r,b,bit,112)) return false;
                if (write)
                    for (unsigned bit=0;bit<write_pins;++bit)
                        if (!emit(0xf,0,c,r,b,bit,0)) return false;
            }
        }
        return true;
    }
    bool hcd(bool v2) {
        // Half-cycle weights are virtual policy. Native writers use canonical rows.
        // The table has 11 V2 rows or eight V1 rows, not 16 V2 rows.
        for (unsigned c=0;c<4;++c) {
            if (!(x.channels&(1U<<c))) continue;
            for (unsigned b=0;b<2;++b) {
                unsigned h,first,second,third;
                if (!half(c,b,h) || !field(c,h,0x50,0,0x1ff,first,x.profile) ||
                    !field(c,h,0x55,0,0x1ff,second,x.profile) ||
                    !field(c,h,0x5a,12,0x1ff,third,x.profile)) return false;
                unsigned first_mask=0, second_mask=0, third_mask=0;
                const auto live=phy(c)+h*0x3000;
                for (unsigned bit=0;bit<write_pins;++bit) {
                    unsigned p;
                    std::uint32_t word;
                    if (!pin(c,b,bit,p) || !x.read32(profile(c,h,x.profile,0x22)-16*p,word)) return false;
                    const unsigned code=word&7;
                    const unsigned index=4*((first>>p)&1)+(v2 ? 2*((second>>p)&1) : 0)+
                                         ((third>>p)&1)+code;
                    if (index >= (v2 ? 11U : 8U)) return false;
                    std::uint16_t a,d,e=0,selector;
                    const auto table=x.table((v2 ? 0x400970 : 0x400940)+(v2 ? 8 : 6)*index);
                    if (!x.read16(table,a) || !x.read16(table+2,d) ||
                        (v2 && !x.read16(table+4,e)) ||
                        !x.read16(table+(v2 ? 6 : 4),selector)) return false;
                    if (a!=((first>>p)&1) || d!=((v2 ? second : third)>>p&1) ||
                        (v2 && e!=((third>>p)&1)) || selector!=code)
                        std::fprintf(stderr,
                            "DDR_HCD_CANONICAL_SUBSTITUTE level=%u profile=%u channel=%u byte=%u pin=%u index=%u policy=virtual-half-cycle-weights\n",
                            x.level,x.profile,c,b,p,index);
                    first_mask |= unsigned(a)<<p;
                    second_mask |= unsigned(d)<<p;
                    third_mask |= unsigned(v2 ? e : d)<<p;
                    for (unsigned r=0;r<2;++r)
                        if (selected(c,r) && !x.write8(x.scratch+0x42b0+44*c+22*r+11*b+bit,index)) return false;
                    if (!x.update32(profile(c,h,x.profile,0x22)-16*p,7,selector) ||
                        !x.write32(live+0x1a00-60*p+16*bank,selector) ||
                        (v2 && (!x.update32(profile(c,h,x.profile,0x58),0x0e000000U,unsigned(selector)<<25) ||
                                !x.update32(live+0x20d4+36*bank,~0x71fffff1U,unsigned(selector)<<25)))) return false;
                }
                const unsigned flag=third_mask ? 1U<<29 : 0;
                for (unsigned i=0;i<4;++i) {
                    if (!x.update32(profile(c,h,x.profile,0x50+i),0x1ffU|(v2 ? 1U<<29 : 0),
                                    first_mask|(v2 ? flag : 0)) ||
                        !x.update32(live+0x20b4+4*i+36*bank,v2 ? ~0x5f7ff000U : ~0x7f7ff000U,
                                    first_mask|(v2 ? flag : 0))) return false;
                }
                if (!x.update32(profile(c,h,x.profile,0x5a),0x1ff000,third_mask<<12) ||
                    !x.update32(live+0x21a4+20*bank,~0x0700033fU,third_mask<<12)) return false;
                for (unsigned i=0;i<4;++i) {
                    if (!v2 && (i&1)) continue;
                    const unsigned mask=v2 && second_mask ? second_mask : (i&1) ? 0 : third_mask;
                    if (!x.update32(profile(c,h,x.profile,0x54+i),0x1ffU|(v2 ? 1U<<29 : 0),
                                    mask|(v2 ? flag : 0)) ||
                        !x.update32(live+0x20c4+4*i+36*bank,v2 ? ~0x5f7ff000U : ~0x7f7ff000U,
                                    mask|(v2 ? flag : 0))) return false;
                }
                if (v2 && (!x.update32(profile(c,h,x.profile,0x58),1U<<29,flag) ||
                           !x.update32(live+0x20d4+36*bank,~0x5ffffff1U,flag))) return false;
            }
        }
        return true;
    }

    bool read_correction() {
        std::uint32_t version;
        if (!x.read32(x.local(0x402c8c),version)) return false;
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
            if (!selected(c,r)) continue;
            for (unsigned b=0;b<2;++b) for (unsigned bit=0;bit<8;++bit) {
                unsigned h,p;
                std::uint32_t selector,extra=0;
                if (!half(c,b,h) || !pin(c,b,bit,p) ||
                    !x.write16(x.scratch+0x3a38+64*c+32*r+16*b+2*bit,0) ||
                    !x.read32(phy(c)+h*0x3000+0x1a08-60*p+16*bank,selector)) return false;
                selector &= 3;
                if (version>=0x200) {
                    if (!x.read32(phy(c)+h*0x3000+0x20b4+4*p+36*bank,extra)) return false;
                    selector |= (extra>>26)&4;
                }
                if (!selector && version<0x200) continue;
                const auto a=selector>=4 || !selector
                    ? phy(c)+h*0x3000+0x29a0+4*(5*p+(selector ? selector-4 : 4))
                    : phy(c)+h*0x3000+0x19f0-60*p+4*(selector-1);
                if (!x.update32(a,0x1f1fU<<(16*r),0)) return false;
            }
        }
        return true;
    }
public:
    explicit Calibration(Context& context) : x(context) {}
    bool initialize() {
        std::uint8_t mr3;
        if (!x.channels || (x.channels&~15U) || !x.ranks || (x.ranks&~3U) ||
            x.level>=12 || x.profile>=12 || x.fsp>=8 ||
            !x.read8(x.local(0x404058),bank) || !x.read8(x.local(0x402fa0+6*x.fsp),mr3)) return false;
        write_pins=8+(mr3>>7);
        return true;
    }
    bool run(std::string_view name) {
        if (name=="DIT - Periodic Training") return dit(true);
        if (name=="tWCK2DQI (DIT)") return dit(false);
        if (name=="PHY_SED_RD") return vref(false,false) && eye(false);
        if (name=="PHY_SED_LP5_WR_Print") return eye(true);
        if (name=="RD_Low_Freq_Write") return low_read();
        if (name=="RD_Cpy_Vref_Profile") return vref(false,true);
        if (name=="RD_IO_Vref_Cpy_Profile") return vref(true,true);
        if (name=="RD_Cpy_LVref_Profile") return local_vref(true);
        if (name=="LP5_WR_CPY_DVref_Upr_FSP") return upper_fsp(0x403880,true);
        if (name=="LP5_WR_CPY_MR30_Upr_FSP") return upper_fsp(0x403894,false);
        if (name=="LP5_RD_CPY_MR69_Upr_FSP") return upper_fsp(0x40389c,false);
        if (name=="DRAM DCC") return dram_dcc();
        if (name=="RD_DRAM_DCC_HIST") {
            // MR69 zero-correction encoding is zero (0x2124b8, selector 7).
            for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
                if (!selected(c,r)) continue;
                std::uint8_t zero=0;
                if (!cache(0x40389c,c,r,zero,true) || !mode(c,r,69,zero,true)) return false;
                for (unsigned b=0;b<2;++b)
                    if (!x.write16(x.scratch+0x39d8+8*c+4*r+2*b,0) ||
                        !emit(0x32,1,c,r,b,15,0)) return false;
            }
            return run("RDT4 DCC Monitor");
        }
        if (name=="DRAM_LP5_WR_VMod") return write_vref(true);
        if (name=="PHY_Trn_LP5_DRAM_DFE") return dfe();
        if (name=="RD_Matched_1TV") return vref(false,false);
        if (name=="IO_Qual Vref Training") return vref(true,false);
        if (name=="RD_Matched_2OV") return local_vref(false);
        const bool write=name=="PHY_Trn_WR_HCD_V1" || name=="PHY_Trn_WR_HCD_V2" ||
            name=="PHY_Trn_LowFreq_WR_HCD_V1" || name=="PHY_Trn_LowFreqWR_HCD_V2" ||
            name=="PHY_Trn_WR_CLKADJ" || name=="PHY_Trn_LP5_WR_DCC_CDC";
        if (write) {
            if (!write_vref(false) || !timing(true) || !write_correction()) return false;
            if (name.find("HCD")!=std::string_view::npos && !hcd(name.back()=='2')) return false;
            for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
                if (!selected(c,r)) continue;
                for (unsigned b=0;b<2;++b) {
                    if (!x.fill(x.scratch+0x38f0+64*c+32*r+16*b,16) ||
                        !x.write16(x.scratch+0x3a10+8*c+4*r+2*b,0) ||
                        !x.write16(x.scratch+0x4290+8*c+4*r+2*b,0)) return false;
                }
            }
            return true;
        }
        if (name=="RD_Matched_RD_PB_LA" || name=="PHY_TRN_RD_TV_Modify" ||
            name=="RD_Matched_4RDQS_PB" || name=="RD_Matched_4RDDQS") return timing(false);
        if (name=="RD_Matched_3RDQDCC") return read_correction();
        if (name=="RDT4 DCC Monitor" || name=="CLKIO DCC Monitor") {
            const bool rdt=name=="RDT4 DCC Monitor";
            for (unsigned c=0;c<4;++c) {
                if (!(x.channels&(1U<<c))) continue;
                std::uint8_t mode;
                if (!x.read8(x.local(0x404059),mode)) return false;
                for (unsigned h=0;h<2;++h) {
                    if (rdt) {
                        for (unsigned r=0;r<2;++r) {
                            if (!(x.ranks&(1U<<r))) continue;
                            if (!x.update32(profile(c,h,x.profile,0x28+r),0x1f1f,0)) return false;
                        }
                        if (!x.update32(phy(c)+h*0x3000+0x1a48+24*bank,0x1f1f,0)) return false;
                    } else {
                        if (!x.update32(profile(c,h,x.profile,0x3b+(mode!=0)),0x1f1f,0) ||
                            !x.update32(phy(c)+h*0x3000+0x1bc0+20*bank,0x1f1f,0)) return false;
                    }
                }
                if (!rdt && (!x.update32(profile(c,2,x.profile,0x2d+(mode!=0)),0x1f1f,0) ||
                    !x.update32(profile(c,2,x.profile,0x28),0x1f1f,0) ||
                    !x.update32(phy(c)+0x62c8+20*bank,0x1f1f,0) ||
                    !x.update32(phy(c)+0x61d0+16*bank,0x1f1f,0))) return false;
                for (unsigned r=0;r<2;++r) for (unsigned b=0;b<2;++b) {
                    if (!selected(c,r)) continue;
                    if (rdt && !x.write16(x.scratch+0x39f8+8*c+4*r+2*b,0)) return false;
                    if (!emit(rdt ? 0x31 : 0x2d,2,c,r,b,15,0)) return false;
                    if (!rdt && (!emit(0x2e,2,c,r,b,15,0) || !emit(0x2f,2,c,r,b,15,0))) return false;
                }
            }
            return true;
        }
        if (name=="PLL Precal") {
            // Zero correction retains the guest's baseline PLL code and controls.
            if (!x.write8(x.local(0x404170+x.level),0)) return false;
            for (unsigned c=0;c<4;++c) {
                if (!(x.channels&(1U<<c))) continue;
                for (unsigned b=0;b<2;++b) {
                    std::uint32_t code;
                    if (!x.read32(phy(c)+0xa46c+0x400*b,code) ||
                        !emit(0x1b,1,c,3,b,15,(code>>16)&0xff) ||
                        !emit(0x39,1,c,3,b,15,0)) return false;
                }
            }
            return x.write8(x.local(0x404170+x.level),1);
        }
        if (name=="RCW CDC/Full Cycle Sweep") {
            for (unsigned c=0;c<4;++c) for (unsigned r=0;r<2;++r) {
                if (!selected(c,r)) continue;
                std::uint32_t latency;
                const auto cache_address=x.local(0x403560+32*x.level+8*c);
                if (!x.read32(cache_address,latency)) return false;
                if (!(latency>>20) && !x.read32(mc(c)+0x4600,latency)) return false;
                if (!(latency>>20)) return abort(8,c,r,0);
                const auto retained_latency=latency&0xfff00000U;
                if (!x.write32(cache_address,retained_latency) ||
                    !x.write32(cache_address+4,retained_latency)) return false;
                for (unsigned offset : {0x4600U,0x4604U,0x4620U,0x4624U})
                    if (!x.update32(mc(c)+offset,0xffff0000,retained_latency)) return false;
                for (unsigned b=0;b<2;++b) {
                    unsigned h,full,half_cycle,cdc,control;
                    if (!half(c,b,h) || !field(c,h,0x59,2+4*r,3,full,x.profile) ||
                        !field(c,h,r ? 0x56 : 0x54,17,1,half_cycle,x.profile) ||
                        !field(c,h,0x2d,16+8*r,0x7f,cdc,x.profile) ||
                        !field(c,h,0x5b,16,15,control,x.profile) ||
                        !emit(9,3,c,r,b,15,(latency>>20)&0xff) ||
                        !emit(10,3,c,r,b,15,control) || !emit(11,3,c,r,b,15,half_cycle) ||
                        !emit(13,3,c,r,b,15,full) || !emit(0x38,3,c,r,b,15,cdc)) return false;
                }
            }
            return true;
        }
        return false;
    }
};
} // namespace training_calibration_detail

template<class Context>
TrainingResult training_calibration(Context& context, std::string_view name) {
    if (name=="PHY_Trn_LP5_DRAM_RX_OC") return TrainingResult::failed;
    // Disabled in the selected DCB. Do not invent command/address training.
    if (name=="WCK2CK" || name=="CBT") return TrainingResult::failed;
    const bool supported = name=="PLL Precal" || name=="DRAM DCC" ||
        name=="DIT - Periodic Training" || name=="tWCK2DQI (DIT)" ||
        name=="PHY_SED_RD" || name=="PHY_SED_LP5_WR_Print" ||
        name=="CLKIO DCC Monitor" || name=="RDT4 DCC Monitor" ||
        name=="RCW CDC/Full Cycle Sweep" || name=="IO_Qual Vref Training" ||
        name=="RD_IO_Vref_Cpy_Profile" || name=="RD_Low_Freq_Write" ||
        name=="RD_Matched_1TV" || name=="RD_Matched_2OV" ||
        name=="RD_Matched_3RDQDCC" || name=="RD_Matched_RD_PB_LA" ||
        name=="PHY_TRN_RD_TV_Modify" || name=="RD_Matched_4RDQS_PB" ||
        name=="RD_Matched_4RDDQS" || name=="RD_Cpy_Vref_Profile" ||
        name=="RD_Cpy_LVref_Profile" || name=="DRAM_LP5_WR_VMod" ||
        name=="LP5_WR_CPY_DVref_Upr_FSP" || name=="LP5_WR_CPY_MR30_Upr_FSP" ||
        name=="LP5_RD_CPY_MR69_Upr_FSP" || name=="PHY_Trn_WR_HCD_V1" ||
        name=="PHY_Trn_WR_HCD_V2" || name=="PHY_Trn_WR_CLKADJ" ||
        name=="PHY_Trn_LP5_WR_DCC_CDC" || name=="PHY_Trn_LP5_DRAM_DFE" ||
        name=="RD_DRAM_DCC_HIST" ||
        name=="PHY_Trn_LowFreq_WR_HCD_V1" || name=="PHY_Trn_LowFreqWR_HCD_V2";
    if (!supported) return TrainingResult::unhandled;
    training_calibration_detail::Calibration<Context> calibration(context);
    return calibration.initialize() && calibration.run(name) ? TrainingResult::complete : TrainingResult::failed;
}
} // namespace qcom_vp

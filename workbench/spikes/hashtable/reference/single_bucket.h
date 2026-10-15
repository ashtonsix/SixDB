#pragma once
#include "map.h"
#include <span>

namespace hashtable_spike::single {
struct affine_field {
    unsigned offset, stride;
    constexpr unsigned at(unsigned rank) const { return offset + stride*rank; }
};
struct plan {
    unsigned key_bits, value_bits, slots, bytes, count_offset, count_bits, fingerprint_offset;
    affine_field suffix, value;
};
struct extent { unsigned offset, bytes; };
enum class status { inserted, replaced, erased, missing, cleared, grow_required, refused };
struct change { status result; int distinct_delta; };
// prepare(false) must leave both storage and the caller's published effects
// unchanged. This local spike adapter reserves coverage before infallible stores.
struct scratch {
    bool prepare(std::span<const extent>) const { return true; }
};

/// Shared, immutable primitive preparation. Addresses and ownership do not live
/// here. The authored family admits contiguous byte fields and one count field.
template<unsigned Bits> struct field {
    static constexpr unsigned bytes=(Bits+7)/8;
    static constexpr word field_mask=mask(Bits);
    tp::reader<8> read;
    tp::writer<8> write;
    static auto description() {
        std::array<tp::code,bytes> codes;
        for(unsigned i=0;i<bytes;++i) codes[i]={byte(i),0,byte(std::min(8u,Bits-i*8))};
        auto d=tp::layout::make(bytes,codes); require(bool(d));return *d;
    }
    field():read([] {
        const auto d=description();std::array<byte,bytes> ranks;
        std::iota(ranks.begin(),ranks.end(),byte{});auto r=tp::reader<8>::make(d,ranks);require(bool(r));return *r;
    }()),write([] {
        const auto d=description();std::array<byte,bytes> ranks;
        std::iota(ranks.begin(),ranks.end(),byte{});auto w=tp::writer<8>::make(d,ranks);require(bool(w));return *w;
    }()) {
        const auto& r=read.controls().transfer; const auto& w=write.controls();
        require(r.offset==0&&r.bytes==bytes&&r.shift==0&&r.mask==mask(Bits));
        require(w.transfer.offset==0&&w.transfer.bytes==bytes&&w.transfer.shift==0&&w.transfer.mask==mask(Bits));
        require(w.word.selected==bytes);
        require(read.read_bytes()==mask(bytes));
        require(write.write_bytes()==mask(bytes)&&(write.read_bytes()&~mask(bytes))==0);
        if constexpr(bytes==1) {
            const auto& s=w.stores[0];require(s.offset==0&&s.mask==field_mask&&s.input[0]==0&&s.shift[0]==0);
        }
    }
    template<bool Direct> [[gnu::always_inline]] word get(const byte* p) const {
        if constexpr(Direct) {
            word x{};std::memcpy(&x,p,bytes);return x&mask(Bits);
        } else {
            const auto& c=read.controls();
            __builtin_assume(c.transfer.offset==0);__builtin_assume(c.transfer.shift==0);
            __builtin_assume(c.transfer.mask==field_mask);
            return tp::native::read_transfer_body<1,bytes>(c,[&](unsigned){return p;},1);
        }
    }
    template<bool Direct> [[gnu::always_inline]] void put(byte* p,word value) const {
        if constexpr(Direct) {
            if constexpr(Bits%8) value=(value&mask(Bits))|(word(p[bytes-1]&byte(~mask(Bits%8)))<<(8*(bytes-1)));
            std::memcpy(p,&value,bytes);
        } else {
            const auto& c=write.controls();
            __builtin_assume(c.word.selected==bytes);__builtin_assume(c.transfer.bytes==bytes);
            __builtin_assume(c.transfer.offset==0);__builtin_assume(c.transfer.shift==0);
            __builtin_assume(c.transfer.mask==field_mask);
            if constexpr(bytes==1) {
                // The normal writer's one-byte endpoint precedes its transfer
                // path. Admit its actual prepared store instead of bypassing it.
                const auto& s=c.stores[0];const auto input=s.input[0],shift=s.shift[0];
                __builtin_assume(s.offset==0);__builtin_assume(s.mask==field_mask);
                __builtin_assume(input==0);__builtin_assume(shift==0);
            }
            tp::native::write_body(c,p,value);
        }
    }
};
template<> struct field<0> {
    template<bool> word get(const byte*) const {return 0;}
    template<bool> void put(byte*,word) const {}
};
template<plan P> struct binding {
    field<P.count_bits> count;
    [[no_unique_address]] field<P.key_bits-8> suffix;
    [[no_unique_address]] field<P.value_bits> value;
};

// No address hashing is necessary with one bucket. The low key byte is folded
// with its suffix for a cheap selector; storing the unchanged suffix makes this
// reversible. Every selector collision is verified, and work is bounded by N.
// This is not the permutation policy for the multi-bucket representation.
[[gnu::always_inline]] inline byte selector(word key) {
    key^=key>>32;key^=key>>16;return byte(key^(key>>8));
}

template<plan P,bool Direct=false> struct kernel {
    static_assert(P.slots==16||P.slots==32);
    byte* data;
    const binding<P>& shared;
    unsigned size() const {return unsigned(shared.count.template get<Direct>(data+P.count_offset));}
    void initialize() const {
        std::memset(data,0,P.bytes);
        std::memset(data+P.fingerprint_offset,255,P.slots);
    }
    static constexpr extent count_extent{P.count_offset,(P.count_bits+7)/8};
    static constexpr extent suffix_extent(unsigned i) {return {P.suffix.at(i),(P.key_bits-8)/8};}
    static constexpr extent value_extent(unsigned i) {return {P.value.at(i),P.value_bits/8};}
    void set_fp(unsigned i,byte fp) const {
        sp::detail::put_payload<sp::format<8>>(data+P.fingerprint_offset+(i/8)*8,i%8,fp);
    }
    [[gnu::always_inline]] int find(word key) const {
        const byte fp=selector(key);
        for(unsigned first=0;first<P.slots;first+=16) {
            const auto bytes=sp::native::read16<sp::format<8>,true>(data+P.fingerprint_offset+first,8,0).v[0];
#if defined(__aarch64__)
            const auto equal=vceqq_u8(bytes,vdupq_n_u8(fp));
            word matches=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0)&0x1111111111111111ULL;
            constexpr unsigned shift=2;
#elif defined(__AVX512BW__) && defined(__AVX512VL__)
            word matches=_mm_cmpeq_epi8_mask(bytes,_mm_set1_epi8(fp));
            constexpr unsigned shift=0;
#else
            word matches=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(bytes,_mm_set1_epi8(fp))));
            constexpr unsigned shift=0;
#endif
            while(matches) {
                const unsigned i=first+(std::countr_zero(matches)>>shift);
                if(fp==255&&i>=size()) return -1;
                if(shared.suffix.template get<Direct>(data+P.suffix.at(i))==(key>>8)) return int(i);
                matches&=matches-1;
            }
        }
        return -1;
    }
    [[gnu::always_inline]] bool get(word key,word& value) const {
        const auto i=find(key);if(i<0)return false;
        value=shared.value.template get<Direct>(data+P.value.at(unsigned(i)));return true;
    }
    template<class Effects=scratch> change set(word key,word value,Effects&& effects={}) const {
        require((key&~mask(P.key_bits))==0&&(value&~mask(P.value_bits))==0);
        const auto found=find(key);
        if(found>=0) {
            if constexpr(P.value_bits) {
                const std::array regions{value_extent(unsigned(found))};
                if(!effects.prepare(regions))return {status::refused,0};
                shared.value.template put<Direct>(data+P.value.at(unsigned(found)),value);
            }
            return {status::replaced,0};
        }
        const unsigned i=size();if(i==P.slots)return {status::grow_required,0};
        const std::array regions{count_extent,extent{P.fingerprint_offset+i,1},suffix_extent(i),value_extent(i)};
        if(!effects.prepare(regions))return {status::refused,0};
        set_fp(i,selector(key));shared.suffix.template put<Direct>(data+P.suffix.at(i),key>>8);
        shared.value.template put<Direct>(data+P.value.at(i),value);
        shared.count.template put<Direct>(data+P.count_offset,i+1);
        return {status::inserted,1};
    }
    template<class Effects=scratch> change erase(word key,Effects&& effects={}) const {
        const int found=find(key);if(found<0)return {status::missing,0};
        const unsigned i=unsigned(found),last=size()-1;
        const std::array regions{count_extent,extent{P.fingerprint_offset+last,1},extent{P.fingerprint_offset+i,1},suffix_extent(i),value_extent(i)};
        if(!effects.prepare(std::span(regions).first(i==last?2:5)))return {status::refused,0};
        if(i!=last) {
            set_fp(i,data[P.fingerprint_offset+last]);
            shared.suffix.template put<Direct>(data+P.suffix.at(i),shared.suffix.template get<Direct>(data+P.suffix.at(last)));
            shared.value.template put<Direct>(data+P.value.at(i),shared.value.template get<Direct>(data+P.value.at(last)));
        }
        set_fp(last,255);shared.count.template put<Direct>(data+P.count_offset,last);
        return {status::erased,-1};
    }
    template<class Effects=scratch> change clear(Effects&& effects={}) const {
        const unsigned n=size();if(!n)return {status::cleared,0};
        const std::array regions{count_extent,extent{P.fingerprint_offset,n}};
        if(!effects.prepare(regions))return {status::refused,0};
        for(unsigned i=0;i<n;++i)set_fp(i,255);
        shared.count.template put<Direct>(data+P.count_offset,0);return {status::cleared,-int(n)};
    }
    template<class F> void each(F&& f) const {
        const auto n=size();
        for(unsigned i=0;i<n;++i) {
            const word high=shared.suffix.template get<Direct>(data+P.suffix.at(i));
            const word key=(high<<8)|word(data[P.fingerprint_offset+i]^selector(high));
            f(key,shared.value.template get<Direct>(data+P.value.at(i)));
        }
    }
};
} // namespace hashtable_spike::single

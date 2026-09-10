#pragma once
#include "formats.h"
#include "native_bytes.h"

namespace ikea::integers {
template<unsigned K, unsigned G, unsigned C> struct Fragment {
    static constexpr auto info = [] {
        struct Info { unsigned mask=0; int shift=0; } r;
        for(unsigned b=0;b<K;++b) {
            const auto a=scan_bit<K>(G*32,b);
            if(a.byte == C*32) { r.mask |= 1u<<b; r.shift=int(a.bit)-int(b); }
        }
        return r;
    }();
};

// Result order exposes byte shift-inserts independently of physical stripe
// order. Each field is contiguous; inserting a later field replaces all bits
// above its result position, so only the final field controls excess high bits.
template<unsigned K,unsigned G> struct GroupFields {
    static constexpr auto plan=[] {
        struct Plan {
            std::array<unsigned,ScanPack<K>::stripes> stripes{},source{},destination{};
            unsigned count=0,covered=0,physical_end=0;
            bool disjoint=true;
        } r;
        unroll<ScanPack<K>::stripes>([&](auto c) {
            constexpr auto f=Fragment<K,G,c>::info;
            if constexpr(f.mask) {
                constexpr unsigned low=std::countr_zero(f.mask);
                static_assert(std::has_single_bit((f.mask>>low)+1));
                r.disjoint&=(r.covered&f.mask)==0;r.covered|=f.mask;
                r.stripes[r.count]=c;r.source[r.count]=unsigned(int(low)+f.shift);
                r.destination[r.count]=low;
                if(f.mask&(1u<<(K-1))) r.physical_end=unsigned(int(K)+f.shift);
                ++r.count;
            }
        });
        for(unsigned i=0;i<r.count;++i) for(unsigned j=i+1;j<r.count;++j)
            if(r.destination[j]<r.destination[i]) {
                std::swap(r.stripes[i],r.stripes[j]);std::swap(r.source[i],r.source[j]);
                std::swap(r.destination[i],r.destination[j]);
            }
        return r;
    }();
    static_assert(plan.disjoint && plan.covered==(1u<<K)-1 && plan.destination[0]==0);
};
template<unsigned K, unsigned G, unsigned N>
IP_INLINE Bytes<N> scan_group(const uint8_t* p) {
#if defined(__aarch64__)
    if constexpr(K==5||K==7) {
        constexpr auto plan=GroupFields<K,G>::plan;
        auto value=Bytes<N>::load(p+plan.stripes[0]*32).template shift<-int(plan.source[0])>();
        unroll<plan.count-1>([&](auto after) {
            constexpr unsigned field=after+1;
            value=value.template high<plan.destination[field]>(
                Bytes<N>::load(p+plan.stripes[field]*32).template shift<-int(plan.source[field])>());
        });
        if constexpr(plan.physical_end==8) return value;
        else return value.template mask<(1u<<K)-1>();
    }
#endif
    auto value=Bytes<N>::zero();
    constexpr auto order=[] {
        std::array<unsigned,ScanPack<K>::stripes> indices{},masks{};
        unroll<ScanPack<K>::stripes>([&](auto c) {indices[c]=c;masks[c]=Fragment<K,G,c>::info.mask;});
        for(unsigned i=0;i<indices.size();++i) for(unsigned j=i+1;j<indices.size();++j)
            if(masks[indices[j]]>masks[indices[i]]) std::swap(indices[i],indices[j]);
        return indices;
    }();
    unroll<ScanPack<K>::stripes>([&](auto at) {
        constexpr unsigned c=order[at];
        constexpr auto f=Fragment<K,G,c>::info;
        if constexpr(f.mask) {
            const auto raw=Bytes<N>::load(p+c*32);
            constexpr unsigned low_mask=(1u<<std::bit_width(f.mask))-1;
#if defined(__AVX2__)
            // Establish the result width once. Each later merge replaces only
            // lower result bits, and its mask absorbs word-shift contamination.
            if constexpr(at==0) value=raw.template extract_shifted<(1u<<K)-1,-f.shift>();
            else value=value.template replace_shifted<low_mask,-f.shift>(raw);
#else
            if constexpr(at==0) value=raw.template shift<-f.shift>();
            else if constexpr(f.shift>0 && low_mask==(255u>>f.shift)) value=value.template low<f.shift>(raw);
            else value=value.template replace<low_mask>(raw.template shift<-f.shift>());
#endif
        }
    });
#if defined(__AVX2__)
    return value;
#else
    return value.template mask<(1u<<K)-1>();
#endif
}
// The random-read lowering groups values by their fragment count. This keeps
// only the required payload reads while avoiding an eight-way dispatch.
inline constexpr uint64_t scan_three_controls=[] {
    uint64_t result=0;
    for(unsigned g=0;g<8;++g) {
        const auto low=scan_bit<3>(g*32,0), high=scan_bit<3>(g*32,2);
        const unsigned descriptor=low.byte|low.bit|(low.byte!=high.byte?8u:0u);
        result|=uint64_t(descriptor)<<(8*g);
    }
    return result;
}();
template<unsigned K> consteval bool scan_random_algebra_matches() {
    for(unsigned g=0;g<ScanPack<K>::groups;++g) for(unsigned b=0;b<K;++b) {
        BitAddress expected{};
        if constexpr(K==3) {
            const unsigned d=unsigned(scan_three_controls>>(8*g));
            expected=(d&8u)&&b==2 ? BitAddress{32,6+(g>>2)} : BitAddress{d&96u,(d&7u)+b};
        } else if constexpr(K==6) {
            const unsigned edge=g&2u;
            if(((g+1)&2u)==0) expected={edge*32,b};
            else expected=b<4 ? BitAddress{32,b+edge*2} : BitAddress{edge*32,b+2};
        } else {
            static_assert(K==5||K==7);
            const unsigned chunk=K==7?g-(g!=0):(g*K)/8;
            const unsigned start=(g*K)%8, low=start+K>8?start+K-8:0;
            expected=low&&b<low?BitAddress{(chunk+1)*32,b}:
                BitAddress{chunk*32,b+(low?8-K:start)};
        }
        const auto actual=scan_bit<K>(g*32,b);
        if(expected.byte!=actual.byte || expected.bit!=actual.bit) return false;
    }
    return true;
}
struct ScanPointValue {
    unsigned v;
    static IP_INLINE ScanPointValue load(const uint8_t* p) {return {*p};}
    template<unsigned M> IP_INLINE ScanPointValue mask() const {return {v&M};}
    template<int S> IP_INLINE ScanPointValue shift() const {
        if constexpr(S>=0) return {v<<S}; else return {v>>-S};
    }
    IP_INLINE ScanPointValue right(unsigned s) const {return {v>>s};}
    IP_INLINE ScanPointValue operator|(ScanPointValue other) const {return {v|other.v};}
    IP_INLINE ScanPointValue replace(unsigned m, ScanPointValue other) const {return {(v&~m)|(other.v&m)};}
};
template<unsigned K,class V> IP_INLINE V scan_random_group(const uint8_t* p,unsigned g) {
    static_assert(scan_random_algebra_matches<K>());
    if constexpr(K==3) {
        const unsigned d=unsigned(scan_three_controls>>(8*g));
        const auto low=V::load(p+(d&96u));
        if(d&8u) return low.template shift<-6>()|
            V::load(p+32).right(4+(g>>2)).template mask<4>();
        return low.right(d&7u).template mask<7>();
    } else if constexpr(K==5) {
        const unsigned start=g*5, chunk=start/8, shift=start%8;
        const auto first=V::load(p+chunk*32);
        if(shift<=3) return first.right(shift).template mask<31>();
        const unsigned low=(1u<<(shift-3))-1;
        return first.template shift<-3>().replace(low,V::load(p+(chunk+1)*32));
    } else if constexpr(K==6) {
        const unsigned edge=g&2u;
        const auto high=V::load(p+edge*32);
        if(((g+1)&2u)==0) return high.template mask<63>();
        return high.template shift<-2>().template mask<48>() |
            V::load(p+32).right(edge*2).template mask<15>();
    } else {
        static_assert(K==7);
        const unsigned chunk=g-(g!=0);
        const auto first=V::load(p+chunk*32);
        if(((g+1)&6u)==0) return first.right(g>>2).template mask<127>();
        const unsigned low=(1u<<(7-g))-1;
        return first.template shift<-1>().replace(low,V::load(p+(chunk+1)*32));
    }
}
// Reader lowering is independent of the encoded bit map. The constant-offset
// alternative exposes separately compiled branch leaves for a controlled
// random-access comparison; it does not change bulk kernels or stored bytes.
enum class ScanReader { fragment_classes, constant_offsets };
template<unsigned K,ScanReader Reader=ScanReader::fragment_classes>
IP_INLINE __attribute__((aligned(64))) uint8_t scan_point(const uint8_t* p,unsigned i) {
    using F=ScanPack<K>;
    p += i/F::values*F::bytes + i%32;
    if constexpr(K==1||K==2||K==4) return (*p>>((i/32%F::groups)*K))&((1u<<K)-1);
    else if constexpr(Reader==ScanReader::fragment_classes) return uint8_t(scan_random_group<K,ScanPointValue>(p,(i%F::values)/32).v);
    else return group_dispatch<F::groups>((i%F::values)/32,[&](auto g) {
        unsigned value=0;
        unroll<F::stripes>([&](auto c) {
            constexpr auto f=Fragment<K,g,c>::info;
            if constexpr(f.mask) {
                if constexpr(f.shift>=0) value |= (p[c*32]>>f.shift)&f.mask;
                else value |= (unsigned(p[c*32])<<-f.shift)&f.mask;
            }
        });
        return uint8_t(value);
    });
}
template<unsigned K,ScanReader Reader=ScanReader::fragment_classes>
IP_INLINE Bytes<16> scan_read16(const uint8_t* p,unsigned i) {
    using F=ScanPack<K>;
    p += i/F::values*F::bytes + i%32;
    if constexpr(K==1||K==2||K==4) return Bytes<16>::load(p).right(i/32%F::groups*K).template mask<(1u<<K)-1>();
    else if constexpr(Reader==ScanReader::fragment_classes) return scan_random_group<K,Bytes<16>>(p,(i%F::values)/32);
    else return group_dispatch<F::groups>((i%F::values)/32,[&](auto g) { return scan_group<K,g,16>(p); });
}
template<unsigned K,unsigned N=256> IP_INLINE void scan_decode(const uint8_t* __restrict p,uint8_t* __restrict out) {
    using F=ScanPack<K>;
    static_assert(N%F::values==0);
#if defined(__AVX2__)
    constexpr unsigned V=32;
#else
    constexpr unsigned V=16;
#endif
    for(unsigned tile=0;tile<N/F::values;++tile)
        for(unsigned lane=0;lane<32;lane+=V)
            unroll<F::groups>([&](auto g) { scan_group<K,g,V>(p+tile*F::bytes+lane).store(out+tile*F::values+g*32+lane); });
}
template<unsigned K,unsigned Begin,unsigned Count,unsigned V>
IP_INLINE Bytes<V> pack_power(const std::array<Bytes<V>,ScanPack<K>::groups>& values) {
    if constexpr(Count==1) return values[Begin];
    else return pack_power<K,Begin,Count/2>(values).template high<K*Count/2>(pack_power<K,Begin+Count/2,Count/2>(values));
}
// Destination order is a lowering decision derived from the independent bit
// map. Every stripe is full: its fields partition bits 0..7 without gaps.
template<unsigned K,unsigned C> struct StripeFields {
    static constexpr auto plan=[] {
        struct Plan {
            std::array<unsigned,ScanPack<K>::groups> groups{},source{},destination{};
            unsigned count=0,covered=0;
            bool disjoint=true;
        } r;
        unroll<ScanPack<K>::groups>([&](auto g) {
            constexpr auto f=Fragment<K,g,C>::info;
            if constexpr(f.mask) {
                constexpr auto source=std::countr_zero(f.mask);
                static_assert(std::has_single_bit((f.mask>>source)+1));
                constexpr unsigned placed=f.shift>=0 ? f.mask<<f.shift : f.mask>>-f.shift;
                r.disjoint&=(r.covered&placed)==0;r.covered|=placed;
                r.groups[r.count]=g;r.source[r.count]=source;
                r.destination[r.count]=unsigned(int(source)+f.shift);++r.count;
            }
        });
        for(unsigned i=0;i<r.count;++i) for(unsigned j=i+1;j<r.count;++j)
            if(r.destination[j]<r.destination[i]) {
                std::swap(r.groups[i],r.groups[j]);std::swap(r.source[i],r.source[j]);
                std::swap(r.destination[i],r.destination[j]);
            }
        return r;
    }();
    static_assert(plan.disjoint && plan.covered==255 && plan.destination[0]==0);
};
template<unsigned K,unsigned C,unsigned Begin,unsigned Count,unsigned V>
IP_INLINE Bytes<V> pack_fields(const std::array<Bytes<V>,ScanPack<K>::groups>& values) {
    constexpr auto plan=StripeFields<K,C>::plan;
    if constexpr(Count==1) return values[plan.groups[Begin]].template shift<-int(plan.source[Begin])>();
    else {
        constexpr unsigned middle=Begin+Count/2;
        constexpr unsigned shift=plan.destination[middle]-plan.destination[Begin];
        return pack_fields<K,C,Begin,Count/2>(values).template high<shift>(
            pack_fields<K,C,middle,Count-Count/2>(values));
    }
}
template<unsigned K,unsigned N=256> IP_INLINE void scan_encode(const uint8_t* __restrict in,uint8_t* __restrict p) {
    using F=ScanPack<K>;
    static_assert(N%F::values==0);
#if defined(__AVX2__)
    constexpr unsigned V=32;
#else
    constexpr unsigned V=16;
#endif
    for(unsigned tile=0;tile<N/F::values;++tile) for(unsigned lane=0;lane<32;lane+=V) {
        std::array<Bytes<V>,F::groups> values;
        unroll<F::groups>([&](auto g) { values[g]=Bytes<V>::load(in+tile*F::values+g*32+lane); });
        if constexpr(K==1||K==2||K==4) pack_power<K,0,F::groups>(values).store(p+tile*F::bytes+lane);
        else {
        unroll<F::stripes>([&](auto c) {
            constexpr auto plan=StripeFields<K,c>::plan;
#if defined(__aarch64__)
            // Balanced shift-inserts expose byte-field structure to NEON and
            // avoid a mask/OR network for each separately shifted fragment.
            const auto packed=pack_fields<K,c,0,plan.count>(values);
#else
            static_assert(plan.source[0]==0 && plan.destination[0]==0);
            auto packed=values[plan.groups[0]];
            unroll<plan.count-1>([&](auto after) {
                constexpr unsigned g=plan.groups[after+1];
                constexpr auto f=Fragment<K,g,c>::info;
                constexpr unsigned mask=f.shift>=0?f.mask<<f.shift:f.mask>>-f.shift;
#if defined(IP_SCAN_REGISTER_MASKS) && IP_SCAN_REGISTER_MASKS && defined(__AVX2__)
                if constexpr(K==5||K==7)
                    packed=packed.template replace_shifted_register_mask<mask,f.shift>(values[g]);
                else
#endif
                packed=packed.template replace_shifted<mask,f.shift>(values[g]);
            });
#endif
            packed.store(p+tile*F::bytes+c*32+lane);
        });
        }
    }
}
} // namespace ikea::integers

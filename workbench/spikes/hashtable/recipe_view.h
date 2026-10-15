#pragma once
#include "list.h"
#ifndef HT_RECIPE_SCAN32
#define HT_RECIPE_SCAN32 0
#endif

namespace ht {
// Physical bucket admission is independent of whole-list growth policy. These
// controls are experiments, not platform presets. Unsupported packed encodings
// are deliberately absent: solver bitstreams are not SeriesPack Local tiles.
// The first enumerator exists only to replay the historical benchmark arm.
// Power-of-two bucket counts do not constrain slots inside one bucket.
enum class record_format { legacy_power_two, exact, padded, rows, tag_plane, tag_bits, aligned };
// Read permissions are independent of wire layout. Broader reads require the
// whole initialized unit to remain stable under one owner/protection domain;
// they never grant wider writes or permission to touch a neighboring bucket.
enum class record_access { exact, selectors, suffixes, both };
inline constexpr auto record_platform=
#if defined(__aarch64__)
    layout_solver::platform::neon;
#elif defined(__AVX512BW__)
    SIXDB_TUNE_ZEN5?layout_solver::platform::avx512_amd:layout_solver::platform::avx512_intel;
#else
    SIXDB_TUNE_ZEN5?layout_solver::platform::avx2_amd:layout_solver::platform::avx2_intel;
#endif

template<unsigned Bytes,bool Direct> [[gnu::always_inline]] word recipe_matches(const byte* p,byte fp,unsigned count) {
    static_assert(Bytes>0 && Bytes<=16);
    const auto raw=[&] {
        if constexpr(!Direct)return sp::native::load128<Bytes>(p);
        else {
#if defined(__aarch64__)
            auto v=vdupq_n_u8(0);
#else
            auto v=_mm_setzero_si128();
#endif
            __builtin_memcpy(&v,p,Bytes);return v;
        }
    }();
#if defined(__aarch64__)
    const auto equal=vceqq_u8(raw,vdupq_n_u8(fp));
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0)&mask(count*4);
#elif defined(__AVX512BW__) && defined(__AVX512VL__)
    return unsigned(_mm_cmpeq_epi8_mask(raw,_mm_set1_epi8(fp)))&mask(count);
#else
    return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(raw,_mm_set1_epi8(fp))))&mask(count);
#endif
}

#if defined(__AVX2__)
// Execution-only experiment: the recipe still describes two 16-lane windows.
// Exact access consumes just the fingerprint plane; admitted access may consume
// 32 bytes inside the same initialized, stable unit. Both exclude inactive lanes
// before any suffix access. The direct control does not call SP loading bodies.
template<unsigned Bytes,bool Direct> [[gnu::always_inline]] word recipe_matches32(const byte* p,byte fp,unsigned count) {
    static_assert(Bytes>16 && Bytes<=32);
    const auto raw=[&] {
        if constexpr(!Direct) {
            const auto low=sp::native::load128<16>(p);
            const auto high=sp::native::load128<Bytes-16>(p+16);
            return _mm256_set_m128i(high,low);
        }else if constexpr(Bytes==32)return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
        else {
#if defined(__AVX512BW__) && defined(__AVX512VL__)
            return _mm256_maskz_loadu_epi8((std::uint32_t(1)<<Bytes)-1,p);
#else
            const auto low=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
            // End-aligned overlapping loads stay inside the exact plane and
            // avoid byte insertion chains from a partial-vector memcpy.
            const auto high=[&] {
                if constexpr(Bytes<=24) {
                    word last;__builtin_memcpy(&last,p+Bytes-8,8);
                    return _mm_cvtsi64_si128(last>>((24-Bytes)*8));
                }else return _mm_srli_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p+Bytes-16)),32-Bytes);
            }();
            return _mm256_set_m128i(high,low);
#endif
        }
    }();
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    const auto bits=unsigned(_mm256_cmpeq_epi8_mask(raw,_mm256_set1_epi8(fp)));
#else
    const auto bits=unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(raw,_mm256_set1_epi8(fp))));
#endif
#if defined(__BMI2__)
    // Defined for the full active-count range 0..32, including its upper end.
    // A generic 64-bit mask would retain an irrelevant count==64 case here.
    return _bzhi_u32(bits,count);
#else
    return bits&mask(count);
#endif
}
#endif

// Plan is cold, immutable admission; only the base address is in a bound view.
// Offsets, strides, tags and exact tail lengths are compiled from that plan.
// Direct independently loads/stores the same field bytes without SP/TP bodies.
template<unsigned K,container_kind Kind,unsigned ValueBytes,unsigned A,
         const layout_solver::candidate& Plan,bool Direct=false,
         record_access Access=record_access::exact,bool InactiveFingerprint=false> struct recipe_view {
    using hash_type=permutation<K,A>;
    using id=layout_solver::field_id;
    static constexpr unsigned suffix_bytes=(hash_type::C+7)/8;
    static constexpr bool multimap=Kind==container_kind::multimap;
    static constexpr unsigned payload_bytes=multimap?std::max(4u,ValueBytes):ValueBytes;
    static constexpr unsigned row_bytes=payload_bytes+multimap; // Canonical overflow record, not physical stride.
    static constexpr unsigned capacity=Plan.shape.slots;
    static constexpr bool wider_selectors=Access==record_access::selectors || Access==record_access::both;
    static constexpr bool wider_suffixes=Access==record_access::suffixes || Access==record_access::both;
    // A single cold proof covers every slot; no rank-dependent bounds branch
    // is added to lookup. Exact access remains the fallback at the unit edge.
    static constexpr unsigned suffix_read_bytes=[] {
        constexpr unsigned wider=std::bit_ceil(suffix_bytes);
        constexpr auto field=Plan.get(id::suffixes);
        constexpr unsigned last=(field.offset_bits+(capacity-1)*field.stride_bits)/8;
        return wider_suffixes && suffix_bytes && last<=Plan.shape.unit_bytes &&
            wider<=Plan.shape.unit_bytes-last?wider:suffix_bytes;
    }();
    // Physical selector windows are shared by native execution and its hint
    // description. Admitted overreads remain inside the immutable bucket.
    static constexpr layout_solver::window selector_read_window(unsigned group) {
        const auto window=*layout_solver::selector_window(Plan,group);
        const unsigned bytes=wider_selectors && window.offset<=Plan.shape.unit_bytes &&
            Plan.shape.unit_bytes-window.offset>=16?16:window.bytes;
        return {window.offset,bytes};
    }
    static constexpr layout_solver::window selector32_read_window() {
        constexpr unsigned offset=Plan.get(id::fingerprints).offset_bits/8;
        constexpr unsigned bytes=wider_selectors && offset<=Plan.shape.unit_bytes &&
            Plan.shape.unit_bytes-offset>=32?32:capacity;
        return {offset,bytes};
    }
    // Header plus all physically possible selector/suffix reads. Row layouts
    // may include intervening values; this is a hint extent, not protection.
    static constexpr unsigned lookup_bytes=[] {
        std::uint64_t end=Plan.header_bytes;
#if HT_RECIPE_SCAN32 && defined(__AVX2__)
        if constexpr(capacity>16 && capacity<=32 && Plan.shape.selector_lanes==16) {
            constexpr auto window=selector32_read_window();end=window.offset+window.bytes;
        }else
#endif
        for(unsigned group=0;group<Plan.selector_groups;++group) {
            const auto window=selector_read_window(group);end=std::max(end,window.offset+window.bytes);
        }
        if constexpr(suffix_read_bytes) {
            constexpr auto field=Plan.get(id::suffixes);
            end=std::max(end,(field.offset_bits+(capacity-1)*field.stride_bits)/8+suffix_read_bytes);
        }
        return unsigned(end);
    }();
    static_assert(lookup_bytes && lookup_bytes<=Plan.shape.unit_bytes);
    using record=slot_record<payload_bytes>;
    static_assert(Plan.spec.fingerprint_bits==hash_type::F && Plan.spec.suffix_bits==hash_type::C);
    static_assert(Plan.spec.value_bytes==ValueBytes && Plan.spec.kind==Kind);
    static_assert(Plan.shape.fp!=layout_solver::fingerprint::packed_bits && hash_type::F>0);
    static_assert(Plan.shape.suffix_encoding==layout_solver::suffix::bytes);
    static_assert(Plan.shape.selector_lanes==8 || Plan.shape.selector_lanes==16);
    static_assert(Plan.get(id::fingerprints).width_bits==8 && Plan.get(id::fingerprints).stride_bits==8);
    static_assert(Plan.get(id::suffixes).byte_addressable() && Plan.get(id::values).byte_addressable());
    static_assert(Plan.payload_bytes==payload_bytes);
    byte* base;
    // Private construction/clear only. The owner must publish before calling
    // this on discoverable storage. Count remains a separate caller-owned fact.
    void initialize_empty()const requires(InactiveFingerprint) {
        std::memset(at<id::fingerprints>(0),255,capacity);
    }
    static void prepare(unsigned n) {
        require(n==capacity,"recipe capacity binding");
        if constexpr(!Direct) {
            admit_identity<suffix_bytes>();
            admit_identity<suffix_read_bytes>();
            if constexpr(row_consumption_bytes)admit_identity<row_consumption_bytes>();
            if constexpr(payload_bytes>=8)admit_identity<8>();
            if constexpr(payload_bytes%8)admit_identity<payload_bytes%8>();
            if constexpr(multimap)admit_identity<1>();
        }
    }
    template<id Field> [[gnu::always_inline]] byte* at(unsigned slot)const {
        constexpr auto f=Plan.get(Field);
        // A bound rank is inside this bucket. Lower byte fields in byte
        // coordinates so unsigned bit arithmetic does not retain a multiply
        // followed by division on every field access. Packed tags keep their
        // bit coordinates; the physical recipe and damage are unchanged.
        if constexpr(f.byte_addressable())
            return base+f.offset_bits/8+std::size_t(slot)*(f.stride_bits/8);
        else return base+(f.offset_bits+slot*f.stride_bits)/8;
    }
    [[gnu::always_inline]] byte* suffix_at(unsigned slot)const{return at<id::suffixes>(slot);}
    [[gnu::always_inline]] byte* payload_at(unsigned slot)const{return at<id::values>(slot);}
    [[gnu::always_inline]] byte fingerprint(unsigned slot)const{return *at<id::fingerprints>(slot);}
    [[gnu::always_inline]] word suffix(unsigned slot)const {
        return get<suffix_read_bytes,Direct>(suffix_at(slot))&mask(suffix_bytes*8);
    }
    template<id Field,class Effects> void publish(unsigned slot,Effects& effects)const {
        constexpr auto f=Plan.get(Field);
        const auto bit=f.offset_bits+slot*f.stride_bits;
        publish_write(effects,*this,bit/8,f.width_bits?(bit%8+f.width_bits+7)/8:0);
    }
    template<class Effects> [[gnu::always_inline]] void key(unsigned slot,byte fp,word sf,Effects& e)const {
        publish<id::fingerprints>(slot,e);put_fingerprint<Direct>(at<id::fingerprints>(slot),0,fp);
        publish<id::suffixes>(slot,e);put<suffix_bytes,Direct>(suffix_at(slot),sf);
    }
    [[gnu::always_inline]] record read(unsigned slot)const {
        record r;
        lanes<payload_bytes/8>([&](auto i){auto v=get<8,Direct>(payload_at(slot)+8*i);std::memcpy(r.payload.data()+8*i,&v,8);});
        if constexpr(payload_bytes%8) {
            auto v=get<payload_bytes%8,Direct>(payload_at(slot)+(payload_bytes/8)*8);
            std::memcpy(r.payload.data()+(payload_bytes/8)*8,&v,payload_bytes%8);
        }
        if constexpr(multimap) {
            constexpr auto tag=Plan.get(id::tag);
            r.tag=byte(get<1,Direct>(at<id::tag>(slot))>>((tag.offset_bits+slot*tag.stride_bits)%8))&mask(tag.logical_bits);
        }
        return r;
    }
    template<class Effects> [[gnu::always_inline]] void write(unsigned slot,const record& r,Effects& e)const {
        publish<id::values>(slot,e);
        lanes<payload_bytes/8>([&](auto i){word v;std::memcpy(&v,r.payload.data()+8*i,8);put<8,Direct>(payload_at(slot)+8*i,v);});
        if constexpr(payload_bytes%8) {
            word v=0;std::memcpy(&v,r.payload.data()+(payload_bytes/8)*8,payload_bytes%8);
            put<payload_bytes%8,Direct>(payload_at(slot)+(payload_bytes/8)*8,v);
        }
        if constexpr(multimap) {
            publish<id::tag>(slot,e);constexpr auto tag=Plan.get(id::tag);
            if constexpr(tag.width_bits<8) {
                auto* p=at<id::tag>(slot);const auto shift=(tag.offset_bits+slot*tag.stride_bits)%8;
                constexpr auto bits=mask(tag.logical_bits);
                const auto old=get<1,Direct>(p);put<1,Direct>(p,(old&~(bits<<shift))|(word(r.tag&bits)<<shift));
            }else put<1,Direct>(at<id::tag>(slot),r.tag&mask(tag.logical_bits));
        }
    }
    // One selector walk supports rank-only mutation and immediate read consumers.
    // Accept is nonsuspending and may only inspect this stable borrowed unit.
    template<class Accept> [[gnu::always_inline]] int find_if(byte fp,unsigned count,Accept&& accept)const {
#if HT_RECIPE_SCAN32 && defined(__AVX2__)
        if constexpr(capacity>16 && capacity<=32 && Plan.shape.selector_lanes==16) {
            if(!count)return -1;
            constexpr auto window=selector32_read_window();
            auto bits=recipe_matches32<window.bytes,Direct>(base+window.offset,fp,count);
            while(bits) {const auto slot=take_match(bits);if(accept(slot))return slot;}
            return -1;
        }else
#endif
        {
        int result=-1;
        lanes<Plan.selector_groups>([&](auto group) {
            constexpr unsigned first=group*Plan.shape.selector_lanes;
            constexpr auto window=selector_read_window(group);
            if(result<0 && first<count) {
                constexpr auto available=std::min(Plan.shape.selector_lanes,capacity-first);
                auto bits=recipe_matches<window.bytes,Direct>(base+window.offset,fp,std::min(available,count-first));
                while(bits) {const auto slot=first+take_match(bits);if(accept(slot)){result=slot;break;}}
            }
        });return result;
        }
    }
    [[gnu::always_inline]] int find(byte fp,word sf,unsigned count)const {
        return find_if(fp,count,[&](unsigned slot){return suffix(slot)==sf;});
    }
    static constexpr unsigned row_consumption_bytes=[] {
        constexpr auto sf=Plan.get(id::suffixes),value=Plan.get(id::values);
        constexpr unsigned used=suffix_bytes+payload_bytes;
        if constexpr(Plan.shape.fields!=layout_solver::body::rows || payload_bytes==0 || used>8 ||
                     value.offset_bits!=sf.offset_bits+suffix_bytes*8)return 0u;
        else {
            constexpr unsigned wider=std::bit_ceil(used);
            constexpr unsigned last=(sf.offset_bits+(capacity-1)*sf.stride_bits)/8;
            return wider_suffixes && last<=Plan.shape.unit_bytes && wider<=Plan.shape.unit_bytes-last?wider:used;
        }
    }();
    template<class Consume> [[gnu::always_inline]] int find_read(byte fp,word sf,unsigned count,Consume&& consume)const {
        return find_if(fp,count,[&](unsigned slot) {
            if constexpr(row_consumption_bytes) {
                const word raw=get<row_consumption_bytes,Direct>(suffix_at(slot));
                if((raw&mask(suffix_bytes*8))!=sf)return false;
                record r;const word value=raw>>(suffix_bytes*8);
                std::memcpy(r.payload.data(),&value,payload_bytes);
                if constexpr(multimap) {
                    constexpr auto tag=Plan.get(id::tag);
                    r.tag=byte(get<1,Direct>(at<id::tag>(slot))>>((tag.offset_bits+slot*tag.stride_bits)%8))&mask(tag.logical_bits);
                }
                consume(std::move(r));return true;
            }else {
                if(suffix(slot)!=sf)return false;
                consume(read(slot));return true;
            }
        });
    }
    template<class Effects> std::expected<bool,mutation_error> erase(unsigned slot,unsigned count,Effects& e)const {
        require(slot<count && count<=capacity,"recipe erase rank");
        constexpr unsigned stores=1+unsigned(suffix_bytes!=0)+unsigned(payload_bytes!=0)+unsigned(multimap);
        const auto last=count-1;
        if(!effects_room(e,(slot==last?0:stores)+unsigned(InactiveFingerprint)))return std::unexpected(mutation_error::effects);
        if(slot!=last){const auto r=read(last);key(slot,fingerprint(last),suffix(last),e);write(slot,r,e);}
        if constexpr(InactiveFingerprint) {
            publish<id::fingerprints>(last,e);put_fingerprint<Direct>(at<id::fingerprints>(last),0,255);
        }
        return true;
    }
};
} // namespace ht

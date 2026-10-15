#pragma once
#include "fields.h"
#include "hash_packet.h"
#include "layout_solver.h"
#include <ikea/effects.h>
#include <optional>
#include <type_traits>
#ifndef HT_LIST_SCAN64
#define HT_LIST_SCAN64 0
#endif
#ifndef HT_LIST_GROW_PRIVATE
#define HT_LIST_GROW_PRIVATE 1
#endif

namespace ht {
using container_kind=layout_solver::container;
enum class mutation_error { capacity, effects, preparation };
struct mutation_delta { int keys=0; std::int64_t values=0; };
enum class record_scan_state { yielded, refused, done };
struct record_scan_result {
    record_scan_state state;
    unsigned work=0,records=0;
};
struct replace_bytes {
    template<class Bytes> Bytes operator()(const Bytes&,const Bytes& incoming)const noexcept {return incoming;}
};
template<unsigned Bytes> struct slot_record {
    std::array<byte,Bytes> payload{};
    byte tag=0;
    std::int64_t value_delta=0;
};

template<class Effects> bool effects_room(const Effects& effects,std::size_t needed) {
    if constexpr(requires {effects.remaining();})return effects.remaining()>=needed;
    else return true;
}
template<class Effects,class Source> void publish_write(Effects& effects,const Source& source,
                                                       std::size_t offset,std::size_t bytes) noexcept {
    if(!bytes)return;
    if constexpr(requires {effects.before(source,ikea::byte_write{});})
        effects.before(source,{0,offset,bytes});
    else effects.before({0,offset,bytes});
}

// This is the bounded native F8 prefix, not an ordinary SeriesPack view:
// ordinary view admission owns complete 8-slot tiles. Tiny lists own exactly
// 1/2/4 selector bytes and use the author's exact-window load/store bodies.
template<unsigned Bytes> [[gnu::always_inline]] word list_matches(const byte* p,byte f,unsigned active) {
    static_assert(Bytes==1||Bytes==2||Bytes==4||Bytes==8||Bytes==16);
    const auto raw=sp::native::load128<Bytes>(p);
#if defined(__aarch64__)
    const auto equal=vceqq_u8(raw,vdupq_n_u8(f));
    const word bits=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0);
    return bits&mask(active*4);
#else
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    return unsigned(_mm_cmpeq_epi8_mask(raw,_mm_set1_epi8(f)))&mask(active);
#else
    return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(raw,_mm_set1_epi8(f))))&mask(active);
#endif
#endif
}

// No count, seed, allocation or ownership is stored in the packed bytes.
// The same binding is used for a whole-table list or one hierarchy bucket.
// Capacities are powers of two, 1..256; that is a measured recipe family,
// not the solver's grammar or a chosen production list threshold.
template<unsigned K,container_kind Kind,unsigned ValueBytes,unsigned A=0,unsigned TagBits=1> struct list_view {
    using hash_type=permutation<K,A>;
    static constexpr unsigned suffix_bytes=(hash_type::C+7)/8;
    static constexpr bool multimap=Kind==container_kind::multimap;
    static constexpr unsigned payload_bytes=multimap?std::max(4u,ValueBytes):ValueBytes;
    static constexpr unsigned row_bytes=payload_bytes+multimap;
    static constexpr unsigned entry_bytes=1+suffix_bytes+row_bytes;
    static_assert(K%8==0 && K>=8 && K<=64);
    static_assert(Kind!=container_kind::set || ValueBytes==0);
    using record=slot_record<payload_bytes>;
    byte* data;
    unsigned capacity;
    static layout_solver::candidate recipe(unsigned capacity) {
        namespace ls=layout_solver;
        auto result=ls::make({.fingerprint_bits=hash_type::F,.suffix_bits=hash_type::C,
            .value_bytes=ValueBytes,.kind=Kind,.where=ls::tier::list,
            .target=ls::platform::neon,.count_bytes=0,.descendant_bytes=0,.head_bytes=0,
            .chain_reference_bytes=4,.allocation_alignment=8,.tag_bits=TagBits},
            {.slots=capacity,.unit_bytes=capacity*entry_bytes,.fp=ls::fingerprint::exact_bytes,
             .suffix_encoding=ls::suffix::bytes,.fields=ls::body::planes,
             .tag=multimap?ls::tags::row_byte:ls::tags::none,.selector_lanes=16});
        require(bool(result),"compact list solver admission");
        constexpr auto supported=ls::bit(ls::feature::selector_exact_tail)|ls::bit(ls::feature::suffix_bytes)|
            ls::bit(ls::feature::planes)|ls::bit(ls::feature::word_values)|ls::bit(ls::feature::wide_values)|
            ls::bit(ls::feature::row_tags)|ls::bit(ls::feature::list)|
            ls::bit(ls::feature::capacity_over_32)|ls::bit(ls::feature::custom_header);
        require(!result->missing(supported),"compact list lowering features");
        require(result->get(ls::field_id::fingerprints).offset_bits==0 &&
            result->get(ls::field_id::suffixes).offset_bits==capacity*8 &&
            result->get(ls::field_id::values).offset_bits==capacity*(1+suffix_bytes)*8 &&
            result->get(ls::field_id::values).stride_bits==row_bytes*8,
            "compact list solver binding");
        if constexpr(multimap)require(result->get(ls::field_id::tag).offset_bits==
            (capacity*(1+suffix_bytes)+payload_bytes)*8 &&
            result->get(ls::field_id::tag).stride_bits==row_bytes*8,"compact list tag binding");
        return *result;
    }
    static void prepare(unsigned capacity) {
        require(capacity && capacity<=256 && std::has_single_bit(capacity),"compact list capacity");
        static const bool admitted=[] {
            for(unsigned cap=1;cap<=256;cap*=2)(void)recipe(cap);
            return true;
        }();(void)admitted;
        admit_identity<suffix_bytes>();
        if constexpr(payload_bytes>=8)admit_identity<8>();
        if constexpr(payload_bytes%8)admit_identity<payload_bytes%8>();
    }
    [[gnu::always_inline]] byte fingerprint(unsigned slot)const {return data[slot];}
    [[gnu::always_inline]] byte* suffix_at(unsigned slot)const {return data+capacity+slot*suffix_bytes;}
    [[gnu::always_inline]] byte* payload_at(unsigned slot)const {
        return data+capacity*(1+suffix_bytes)+slot*row_bytes;
    }
    [[gnu::always_inline]] record read(unsigned slot)const {
        record r;
        lanes<payload_bytes/8>([&](auto i){const auto v=get<8>(payload_at(slot)+8*i);std::memcpy(r.payload.data()+8*i,&v,8);});
        if constexpr(payload_bytes%8) {
            const auto v=get<payload_bytes%8>(payload_at(slot)+(payload_bytes/8)*8);
            std::memcpy(r.payload.data()+(payload_bytes/8)*8,&v,payload_bytes%8);
        }
        if constexpr(multimap)r.tag=payload_at(slot)[payload_bytes];
        return r;
    }
    template<class Effects> [[gnu::always_inline]] void write(unsigned slot,const record& r,Effects& effects)const {
        publish_write(effects,*this,payload_at(slot)-data,row_bytes);
        lanes<payload_bytes/8>([&](auto i){word v;std::memcpy(&v,r.payload.data()+8*i,8);put<8>(payload_at(slot)+8*i,v);});
        if constexpr(payload_bytes%8) {
            word v=0;std::memcpy(&v,r.payload.data()+(payload_bytes/8)*8,payload_bytes%8);
            put<payload_bytes%8>(payload_at(slot)+(payload_bytes/8)*8,v);
        }
        if constexpr(multimap)put<1>(payload_at(slot)+payload_bytes,r.tag);
    }
    [[gnu::always_inline]] int find(byte fp,word suffix,unsigned count)const {
        if(!count)return -1;
        auto confirm=[&] [[gnu::always_inline]] (word bits,unsigned first) {
            while(bits) {auto slot=first+take_match(bits);if(get<suffix_bytes>(suffix_at(slot))==suffix)return int(slot);}
            return -1;
        };
#if HT_LIST_SCAN64
        // One cache-line selector window. This is a measured alternative to
        // early-exit 16-byte windows, not a platform preset. A power-of-two
        // capacity of at least64 owns every exact 64-byte load, including the
        // final partially occupied window; inactive lanes never reach suffixes.
        if(capacity>=64) {
            for(unsigned first=0;first<count;first+=64) {
                const auto active=std::min(64u,count-first);
#if defined(__aarch64__)
                const auto bytes=vld1q_u8_x4(data+first);const auto needle=vdupq_n_u8(fp);
                const auto e0=vceqq_u8(bytes.val[0],needle),e1=vceqq_u8(bytes.val[1],needle);
                const auto e2=vceqq_u8(bytes.val[2],needle),e3=vceqq_u8(bytes.val[3],needle);
                if(!vmaxvq_u8(vorrq_u8(vorrq_u8(e0,e1),vorrq_u8(e2,e3))))continue;
                auto test=[&] [[gnu::always_inline]] (uint8x16_t equal,unsigned group) {
                    const auto before=16*group;if(before>=active)return -1;
                    const word bits=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0);
                    return confirm(bits&mask(std::min(16u,active-before)*4),first+before);
                };
                auto at=test(e0,0);if(at>=0)return at;at=test(e1,1);if(at>=0)return at;
                at=test(e2,2);if(at>=0)return at;at=test(e3,3);if(at>=0)return at;
#elif defined(__AVX512BW__)
                const word bits=_mm512_cmpeq_epi8_mask(_mm512_loadu_si512(data+first),_mm512_set1_epi8(fp));
                const auto at=confirm(bits&mask(active),first);if(at>=0)return at;
#elif defined(__AVX2__)
                const auto needle=_mm256_set1_epi8(fp);
                const word low=unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+first)),needle)));
                const word high=unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+first+32)),needle)));
                const auto at=confirm((low|(high<<32))&mask(active),first);if(at>=0)return at;
#else
#error "64-selector experiment requires NEON, AVX2 or AVX512BW"
#endif
            }
            return -1;
        }
#endif
        if(capacity>=16) {
            for(unsigned first=0;first<count;first+=16) {
                auto at=confirm(list_matches<16>(data+first,fp,std::min(16u,count-first)),first);
                if(at>=0)return at;
            }
            return -1;
        }
        word bits;
        switch(capacity) {
            case 1:bits=list_matches<1>(data,fp,count);break;
            case 2:bits=list_matches<2>(data,fp,count);break;
            case 4:bits=list_matches<4>(data,fp,count);break;
            default:bits=list_matches<8>(data,fp,count);break;
        }
        return confirm(bits,0);
    }
    // Resolve all active queries against one immutable list interpretation.
    // Selector windows are loaded once; each query confirms its own suffix.
    // The returned ranks borrow no storage address. Their validity across a
    // mutation still requires the caller to prove rank/seed stability.
    template<unsigned N,unsigned Bytes> [[gnu::always_inline]] std::array<int,N> find_packet_windows(
            const hash_packet<N>& hash,unsigned count,unsigned active)const {
        std::array<int,N> found;found.fill(-1);unsigned pending=active&unsigned(mask(N));
        for(unsigned first=0;first<count&&pending;first+=Bytes) {
            const auto used=std::min(Bytes,count-first);
#if defined(__aarch64__)
            static_assert(Bytes==16);
            const auto raw=vld1q_u8(data+first);
#else
            const auto raw=[&] {
#if defined(__AVX512BW__)
                if constexpr(Bytes==64)return _mm512_loadu_si512(data+first);
                else
#endif
#if defined(__AVX2__)
                if constexpr(Bytes==32)return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data+first));
                else
#endif
                return _mm_loadu_si128(reinterpret_cast<const __m128i*>(data+first));
            }();
#endif
            lanes<N>([&] [[gnu::always_inline]] (auto i) {if(pending&(1u<<i)) {
                const auto fp=byte(hash.fingerprint[i]);
                word bits;
#if defined(__aarch64__)
                const auto equal=vceqq_u8(raw,vdupq_n_u8(fp));
                bits=vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(equal),4)),0)&mask(used*4);
#else
#if defined(__AVX512BW__)
                if constexpr(Bytes==64)bits=_mm512_cmpeq_epi8_mask(raw,_mm512_set1_epi8(fp));
                else
#endif
#if defined(__AVX2__)
                if constexpr(Bytes==32)bits=unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(raw,_mm256_set1_epi8(fp))));
                else
#endif
                bits=unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(raw,_mm_set1_epi8(fp))));
                bits&=mask(used);
#endif
                while(bits) {
                    const auto slot=first+take_match(bits);
                    if(get<suffix_bytes>(suffix_at(slot))==hash.suffix[i]) {
                        found[i]=int(slot);pending&=~(1u<<i);break;
                    }
                }
            }});
        }
        return found;
    }
    template<unsigned N> [[gnu::always_inline]] std::array<int,N> findN(
            const hash_packet<N>& hash,unsigned count,unsigned active)const {
#if defined(__AVX512BW__)
        if(capacity>=64)return find_packet_windows<N,64>(hash,count,active);
#endif
#if defined(__AVX2__)
        if(capacity>=32)return find_packet_windows<N,32>(hash,count,active);
#endif
        if(capacity>=16)return find_packet_windows<N,16>(hash,count,active);
        std::array<int,N> found;found.fill(-1);
        lanes<N>([&](auto i){if(active&(1u<<i))found[i]=find(byte(hash.fingerprint[i]),hash.suffix[i],count);});
        return found;
    }
    // Prepare may allocate or refuse, but cannot relocate this list or mutate
    // its visible contents. All effect capacity is admitted before calling it.
    // On success, the remaining stores and publication hooks cannot fail.
    template<class Prepare,class Effects> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    mutate(typename hash_type::parts h,unsigned count,Prepare&& prepare,Effects& effects)const {
        const int found=find(h.fingerprint,h.suffix,count);
        return mutate_at(h,count,found,std::forward<Prepare>(prepare),effects);
    }
    // A preceding lookup can cross allocation preparation without a second
    // search: private list growth preserves occupied ranks and the seed.
    template<class Prepare,class Effects> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    mutate_at(typename hash_type::parts h,unsigned count,int found,Prepare&& prepare,Effects& effects)const {
        if(found<0 && count==capacity)return std::unexpected(mutation_error::capacity);
        const unsigned stores=unsigned(row_bytes!=0)+(found<0?1+unsigned(suffix_bytes!=0):0);
        if(!effects_room(effects,stores))return std::unexpected(mutation_error::effects);
        std::optional<record> previous;if(found>=0)previous=read(found);
        auto next=prepare(previous);if(!next)return std::unexpected(mutation_error::preparation);
        const unsigned slot=found<0?count:unsigned(found);
        if constexpr(requires {next->commit();}) {
            static_assert(noexcept(next->commit()));next->commit();
        }
        if(found<0) {
            publish_write(effects,*this,slot,1);put_fingerprint(data,slot,h.fingerprint);
            publish_write(effects,*this,suffix_at(slot)-data,suffix_bytes);put<suffix_bytes>(suffix_at(slot),h.suffix);
        }
        write(slot,*next,effects);
        return mutation_delta{found<0,next->value_delta};
    }
    template<class Effects> std::expected<bool,mutation_error> erase(unsigned slot,unsigned count,Effects& effects)const {
        require(slot<count,"compact list erase rank");
        if(slot==count-1)return true; // No payload store; caller publishes count.
        if(!effects_room(effects,1+unsigned(suffix_bytes!=0)+unsigned(row_bytes!=0)))
            return std::unexpected(mutation_error::effects);
        const auto last=count-1;const auto r=read(last);
        publish_write(effects,*this,slot,1);put_fingerprint(data,slot,data[last]);
        publish_write(effects,*this,suffix_at(slot)-data,suffix_bytes);
        put<suffix_bytes>(suffix_at(slot),get<suffix_bytes>(suffix_at(last)));
        write(slot,r,effects);return true;
    }
    template<unsigned N,class Consume> [[gnu::always_inline]] unsigned getN(const hash_type& hash,
            std::span<const word,N> keys,unsigned count,unsigned active,Consume&& consume)const {
        const auto h=split_packet<N>(hash,keys);unsigned found=0;
        lanes<N>([&](auto i){if(active&(1u<<i)) {
            const auto slot=find(byte(h.fingerprint[i]),h.suffix[i],count);
            if(slot>=0) {consume(i,read(slot));found|=1u<<i;}
        }});return found;
    }
    // Ordered active lanes: duplicates see earlier successful lanes. Refusal
    // returns the processed prefix and its deltas; inactive lanes are untouched.
    struct batch_result {unsigned processed=0;mutation_delta delta;std::optional<mutation_error> error;};
    template<unsigned N,class Prepare,class Effects> [[gnu::always_inline]] batch_result setN(const hash_type& hash,
            std::span<const word,N> keys,unsigned count,unsigned active,Prepare&& prepare,Effects& effects)const {
        const auto h=split_packet<N>(hash,keys);batch_result result;
        lanes<N>([&](auto i){if((active&(1u<<i))&&!result.error) {
            auto r=mutate({h.bucket[i],h.suffix[i],byte(h.fingerprint[i])},count+result.delta.keys,
                          [&](const auto& previous){return prepare(i,previous);},effects);
            if(!r)result.error=r.error();
            else {result.processed|=1u<<i;result.delta.keys+=r->keys;result.delta.values+=r->values;}
        }});return result;
    }
};

struct no_list_growth_observer {void operator()(const oa::Allocation&)const noexcept {}};
template<unsigned K,container_kind Kind,unsigned ValueBytes,unsigned TagBits=1> class list_owner {
  public:
    using view_type=list_view<K,Kind,ValueBytes,0,TagBits>;
    oa::Allocation allocation;
    list_owner()=default;
    list_owner(list_owner&&)noexcept=default;
    list_owner& operator=(list_owner&&)noexcept=default;
    unsigned capacity()const {return allocation.size/view_type::entry_bytes;}
    view_type bind()const {return {reinterpret_cast<byte*>(allocation.address),capacity()};}
    // Both controls require private, exclusive storage. Orbital growth preserves
    // readable bytes on failure and may relocate on success; rebind afterwards.
    // Keeping an address alive with a Hold does not authorize mutating its bytes.
    template<class BeforeGrowth=no_list_growth_observer>
    bool reserve(oa::Pool& pool,unsigned count,unsigned wanted,[[maybe_unused]] BeforeGrowth before_growth={}) {
        if(wanted<=capacity())return true;
        if(wanted>256)return false;
        auto cap=std::bit_ceil(wanted);view_type::prepare(cap);
#if HT_LIST_GROW_PRIVATE
        if(allocation) {
            const auto previous=capacity();const auto old_bytes=allocation.size;
            // Unlike replacement, private growth can overwrite old plane bytes.
            // Observe the complete initialized old span before grow can retire
            // its identity/address. A refused grow may leave this conservative
            // observation, but leaves every logical byte unchanged.
            static_assert(std::is_nothrow_invocable_v<BeforeGrowth,const oa::Allocation&>);
            before_growth(allocation);
            if(!oa::grow(allocation,{.bytes=cap*view_type::entry_bytes,.alignment=8}))return false;
            auto* base=reinterpret_cast<byte*>(allocation.address);
            // Initialize newly readable bytes before moving initialized planes.
            // Descending plane order preserves sources even for overlapping moves.
            std::memset(base+old_bytes,0,allocation.size-old_bytes);
            view_type old{base,previous},dst{base,cap};
            if constexpr(view_type::row_bytes)
                std::memmove(dst.payload_at(0),old.payload_at(0),count*view_type::row_bytes);
            if constexpr(view_type::suffix_bytes)
                std::memmove(dst.suffix_at(0),old.suffix_at(0),count*view_type::suffix_bytes);
            return true;
        }
#endif
        auto next=oa::allocate(pool,{.bytes=cap*view_type::entry_bytes,.alignment=8});
        if(!next)return false;
        std::memset(next->address,0,next->size);
        view_type dst{reinterpret_cast<byte*>(next->address),cap};
        if(count) {
            auto old=bind();std::memcpy(dst.data,old.data,count);
            std::memcpy(dst.suffix_at(0),old.suffix_at(0),count*view_type::suffix_bytes);
            std::memcpy(dst.payload_at(0),old.payload_at(0),count*view_type::row_bytes);
        }
        allocation=std::move(*next);return true;
    }
    memory footprint()const {return {allocation.size,allocation?oa::capacity(allocation):0,sizeof(*this)};}
    std::optional<list_owner> clone(oa::Pool& pool)const {
        list_owner result;
        if(allocation) {
            auto next=oa::allocate(pool,{.bytes=allocation.size,.alignment=8});
            if(!next)return {};
            // reserve initializes the entire requested region. Capacity and
            // plane offsets remain identical in this independent version.
            std::memcpy(next->address,allocation.address,allocation.size);
            result.allocation=std::move(*next);
        }
        return result;
    }
};
} // namespace ht

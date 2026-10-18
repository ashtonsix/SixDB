#pragma once
#ifndef IBS_PRESERVE_OVERFLOW
#define IBS_PRESERVE_OVERFLOW 0
#endif
#ifndef IBS_VECTOR_PCS
#define IBS_VECTOR_PCS 0
#endif
#ifndef IBS_OVERFLOW_SLOT
#define IBS_OVERFLOW_SLOT 0
#endif
#ifndef IBS_STAGED_PRIMARY_VALUES
#define IBS_STAGED_PRIMARY_VALUES 0
#endif
#if defined(__aarch64__) && IBS_VECTOR_PCS
#define IBS_OVERFLOW_ABI __attribute__((aarch64_vector_pcs))
#elif IBS_PRESERVE_OVERFLOW
#define IBS_OVERFLOW_ABI __attribute__((preserve_all))
#else
#define IBS_OVERFLOW_ABI
#endif
#include "mutation.h"
#include "allocation.h"
#include <ikea/seriespack/author/read.h>
#include <ikea/seriespack/author/write.h>
#include <ikea/seriespack/author/local_equal.h>
#include <ikea/seriespack/author/indexof.h>
#include <ikea/tuplepack/author/execution.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#ifndef IBS_FILTER
#define IBS_FILTER 0
#endif
#ifndef IBS_STATIC_HINT_GEOMETRY
#define IBS_STATIC_HINT_GEOMETRY 0
#endif
#ifndef IBS_PACKED_FLAGS
#define IBS_PACKED_FLAGS 1
#endif
#ifndef IBS_VALUE_HINT_LINES
#define IBS_VALUE_HINT_LINES 0
#endif
#ifndef IBS_NEON_FIRST_ROW
#define IBS_NEON_FIRST_ROW 0
#endif
#ifndef IBS_PREFERRED_PROBE
#define IBS_PREFERRED_PROBE 0
#endif
#ifndef IBS_PREFERRED_PLACEMENT
#define IBS_PREFERRED_PLACEMENT 0
#endif
#ifndef IBS_VALUE_BASE
#define IBS_VALUE_BASE 0
#endif
#ifndef IBS_STATIC_TUPLE_RECIPE
#define IBS_STATIC_TUPLE_RECIPE 0
#endif
#ifndef IBS_ALIGNED_BUCKETS
#define IBS_ALIGNED_BUCKETS 0
#endif
#ifndef IBS_COMPACT_VALUES
#define IBS_COMPACT_VALUES 0
#endif
#ifndef IBS_WORD_TAIL
#define IBS_WORD_TAIL 0
#endif

namespace iceberg {
using word = std::uint64_t;
using byte = std::uint8_t;
namespace sp = ikea::seriespack;
template<unsigned K> constexpr word mask = ~word{0} >> (64-K);
template<> inline constexpr word mask<0> = 0;
template<unsigned Bits> [[gnu::always_inline]] word low_bits(word value) {
#if defined(__BMI2__) && defined(__x86_64__)
    if constexpr(Bits>32&&Bits<64){
        // Clang can lower a constant mask with MOVB into the previous slot's
        // register, then BZHI. BZHI depends on the full count register even
        // though its mathematical count is the low byte. Define the whole
        // register explicitly with a scratch lifetime limited to this mask.
        word count,result;
        asm("movl %3, %k1\n\tbzhiq %1, %2, %0" : "=r"(result),"=&r"(count) : "r"(value),"i"(Bits));
        return result;
    }
#endif
    return value&mask<Bits>;
}
inline void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<std::size_t N> using bytes = std::array<byte,N>;
template<unsigned N> inline word load(const byte* p) {
    static_assert(N<=8); word v=0; std::memcpy(&v,p,N); return v;
}
template<unsigned N> inline void store(byte* p, word v) { std::memcpy(p,&v,N); }
template<unsigned N, class Fn> inline void each(Fn&& f) {
    [&]<std::size_t... I>(std::index_sequence<I...>){(f(std::integral_constant<unsigned,I>{}),...);}
        (std::make_index_sequence<N>{});
}
// The named view is used only during this evaluation. Construction proves the
// whole allocation; every bucket contains T complete, disjoint, byte-aligned
// local tiles. No access extends into padding or another bucket.
template<unsigned Bits> auto placed(byte* p, unsigned rows) {
    using F=sp::format<Bits,sp::geometry::local,0>;
    return sp::view<F,byte>::assume_valid(rows,{{{{p,std::size_t(rows/8)*Bits},Bits},{}, {}}});
}
template<unsigned Bits> inline word point(const byte* p,unsigned row,unsigned rows) {
    using F=sp::format<Bits,sp::geometry::local,0>;
    const sp::dense_source<F> source{p,rows};sp::composition::scalar_ops ops;
    return sp::composition::read(ops,sp::composition::describe(source),row,true).value;
}
struct fused_scalar_ops:sp::composition::scalar_ops {
    template<class S,sp::composition::field_kind Field>
    auto read(const sp::composition::leaf<S,Field>& field,std::size_t row,bool active)const {
        using F=typename S::format_type;
        if constexpr(requires{field.source.data;}&&Field==sp::composition::field_kind::body&&F::tail==0&&F::storage==sp::geometry::local)
            return sp::composition::scalar_value<F::width>{load<F::body>(field.source.data+row*F::body)};
        else return sp::composition::scalar_ops::read(field,row,active);
    }
};
template<unsigned Bits> inline word direct_point(const byte* p,unsigned row,unsigned rows) {
    using F=sp::format<Bits,sp::geometry::local,0>;const sp::dense_source<F> source{p,rows};fused_scalar_ops ops;
    return sp::composition::read(ops,sp::composition::describe(source),row,true).value;
}
template<unsigned R> struct plane_queries {
    static_assert(R>0&&R<8);
    inline static constexpr auto patterns=[] {std::array<word,1u<<R> out{};
        for(unsigned q=0;q<out.size();++q)for(unsigned bit=0;bit<R;++bit)if(q&(1u<<bit))out[q]|=word{255}<<(bit*8);return out;}();
};
} // namespace iceberg
#include "packed30.h"
#include "bucket-filter.h"
namespace iceberg {
// The Local residual consists of R adjacent bytes, one bit per row in each.
// Update exactly those bytes together; no readable padding or extra issued
// footprint is assumed. Other lane bits survive the read/modify/write.
struct word_tail_write_ops:sp::composition::scalar_write_ops {
    template<class S,sp::composition::field_kind Field,unsigned Bits>
    void write(const sp::composition::leaf<S,Field>& destination,std::size_t row,
            sp::composition::scalar_value<Bits> value)const {
#if defined(__BMI2__) && defined(__x86_64__)
        using F=typename S::format_type;
        if constexpr(Field==sp::composition::field_kind::tail&&F::storage==sp::geometry::local){
            constexpr auto R=F::tail;
            constexpr word columns=0x0101010101010101ULL>>(8*(8-R));
            const auto& plane=destination.source.stream(0);
            auto* tail=plane.bytes.data()+(row/8)*plane.stride+8*F::body;
            const auto lane=row%8;
            const auto expanded=_pdep_u64(value.value,columns)<<lane;
            store<R>(tail,(load<R>(tail)&~(columns<<lane))|expanded);
        }else
#endif
        sp::composition::scalar_write_ops::write(destination,row,value);
    }
};
template<unsigned Bits> void set_point(byte* p,unsigned row,unsigned rows,word v) {
    auto view=placed<Bits>(p,rows);
    std::conditional_t<IBS_WORD_TAIL,word_tail_write_ops,sp::composition::scalar_write_ops> ops;
    sp::composition::assign(ops,sp::composition::describe(view),row,sp::composition::scalar_value<Bits>{v});
}
// This is one mathematical 128-bit product. On AArch64 it is MUL + UMULH.
// Low W bits are a permutation. Bucket+quotient therefore recover the key.
template<unsigned W,unsigned K> struct permutation {
    static constexpr word multiplier=0x9e3779b185ebca87ULL;
    static constexpr word inverse=[] {word a=1;for(unsigned i=0;i<6;++i)a*=2-multiplier*a;return a;}();
    word seed;
    struct result {word bucket,quotient,swiss;byte fingerprint;};
    [[gnu::always_inline]] result operator()(word key) const {
        // Trusted W-bit inputs and the construction-masked seed. Avoid a
        // redundant narrow-domain mask (Clang otherwise emitted a partial-
        // register BZHI count in the actual independent consumer).
        __builtin_assume((key&~mask<W>)==0);
        __builtin_assume((seed&~mask<W>)==0);
        const __uint128_t p=__uint128_t(key^seed)*multiplier;
        const word lo=word(p),h=lo^word(p>>64);
        return {K==64?0:(lo>>K)&mask<W-K>,low_bits<K>(lo),h,byte(h>>57)};
    }
    word recover(word bucket,word quotient)const {
        const word y=K==64?quotient:(bucket<<K)|quotient;
        return ((y*inverse)^seed)&mask<W>;
    }
};
inline unsigned equal_controls(const byte* p,byte rhs) {
#if defined(__aarch64__)
    const auto lanes=vceqq_u8(vld1q_u8(p),vdupq_n_u8(rhs));
    return ikea::detail::filter::byte_bits(lanes);
#elif defined(__SSE2__)
    return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)),_mm_set1_epi8(rhs))));
#else
    unsigned m=0;for(unsigned i=0;i<16;++i)m|=unsigned(p[i]==rhs)<<i;return m;
#endif
}
// TuplePack projections use bounded contiguous fields. A wide tuple is composed
// of adjacent units (TuplePack admits at most 64 physical bytes per unit).
template<unsigned Bytes> struct tuple_field {
    inline static const auto plan=[] {
        std::array<ikea::tuplepack::code,Bytes> codes;std::array<byte,Bytes> map;
        for(unsigned i=0;i<Bytes;++i){codes[i]={byte(i),0,8};map[i]=byte(i);}
        auto layout=ikea::tuplepack::layout::make(Bytes,codes);require(bool(layout),"TuplePack layout");
        auto read=ikea::tuplepack::reader<8>::make(*layout,map);require(bool(read),"TuplePack projection");
        const auto& transfer=read->controls().transfer;
        require(transfer.bytes==Bytes,"TuplePack contiguous transfer");
        if constexpr(IBS_STATIC_TUPLE_RECIPE)require(transfer.offset==0&&transfer.shift==0&&
            transfer.mask==mask<Bytes*8>,"TuplePack identity transfer recipe");
        return *read;
    }();
    [[gnu::always_inline]] static word read(const byte* p) {
        if constexpr(IBS_STATIC_TUPLE_RECIPE){
            // The immutable projection was admitted above. Keep the authored
            // bounded transfer while specializing its fixed shift/address/mask.
            const auto offset=plan.controls().transfer.offset,shift=plan.controls().transfer.shift;
            const auto selected=plan.controls().transfer.mask;
            __builtin_assume(offset==0);__builtin_assume(shift==0);
            __builtin_assume(selected==mask<Bytes*8>);
        }
        return ikea::tuplepack::native::read_transfer_body<1,Bytes>(plan.controls(),[&](unsigned){return p;},1,Bytes);
    }
};

// A first complete owner, deliberately independent of the retired hierarchy.
// W, b, E specialize a caller descriptor's operation; B/T never change with E.
template<unsigned W,unsigned b,unsigned V,unsigned E=(W-b),unsigned Filter=IBS_FILTER,class Storage=orbital_storage_policy> class table {
    using allocation_context=typename Storage::context;
    using allocation=typename Storage::buffer;
  public:
    using storage_policy=Storage;
    static_assert(W>=1&&W<=64&&b<=W);
    static constexpr unsigned K=W-b;
    static constexpr bool bitmap=K<=9;
    static constexpr unsigned B=bitmap?((unsigned{1}<<K)+7)/8:K<=30?64:96;
    static constexpr unsigned T=bitmap?0:(B-1)/(K+1);
    static constexpr unsigned N=bitmap?(unsigned{1}<<K):8*T;
    static constexpr unsigned encoding=E;
    static_assert(bitmap || (E>=K && E<=64 && E<=((B-1)/T)-1));
    static constexpr unsigned value_rows=std::max(8u,N);
    static constexpr std::size_t unpadded_stride=B+std::size_t(value_rows)*V;
    // Map-only alignment control: B/T/N and the key encoding stay fixed.
    // Extra inter-bucket padding is included in every footprint receipt.
    static constexpr std::size_t stride=IBS_ALIGNED_BUCKETS&&V?(unpadded_stride+63)&~std::size_t{63}:unpadded_stride;
    // Keep the B/T/N budget and reservation unchanged. Where the complete
    // keys fit 64 bytes inside B=96, a layout control puts values at byte 64
    // and moves flags/count behind them, using the existing 32-byte slack.
    static constexpr bool compact_values=IBS_COMPACT_VALUES&&!bitmap&&V&&B==96&&T*E<=64;
    static constexpr std::size_t value_offset=compact_values?64:B;
    static constexpr std::size_t flags_offset=compact_values?value_offset+std::size_t(value_rows)*V:T*E;
    static constexpr std::size_t overflow_offset=compact_values?flags_offset+T:B-1;
    static_assert(value_offset+std::size_t(value_rows)*V<=stride&&overflow_offset<stride);
    static constexpr __uint128_t logical_bucket_count=__uint128_t{1}<<b;
    static constexpr std::size_t bucket_count=std::size_t(logical_bucket_count);
    static constexpr unsigned key_bytes=(W+7)/8, tuple_bytes=key_bytes+V;
    using value_type=bytes<V>;
    using mix_type=permutation<W,K>;
    struct location {word bucket;unsigned slot;bool primary;};
    struct probe {typename mix_type::result hash;const byte* primary;};
    // Private-owner mutation cursor. The enclosing owner keeps exclusive
    // access and invalidates it on every intervening mutation/preparation.
    struct write_position {typename mix_type::result hash;std::size_t slot;bool primary,present;};
    struct staged_probe {probe first;word key;std::size_t group=0,step=0;unsigned candidates=0,slot=0;byte phase=0;bool empty_group=false;};
  private:
    static std::size_t primary_bytes() {
        const auto bytes=logical_bucket_count*stride;
        if(bytes>std::numeric_limits<std::size_t>::max()-63||
           logical_bucket_count*N>std::numeric_limits<std::size_t>::max())
            throw std::length_error("bucket geometry exceeds addressable owner");
        return std::size_t(bytes);
    }
    allocation_context* memory_=nullptr;
    allocation primary_, control_, slots_;
    std::size_t groups_=0, spill_size_=0, deleted_=0, size_=0;
    mix_type mix_;
    table(const table& source,allocation_context* memory):memory_(memory),
        primary_(duplicate_buffer<Storage>(source.primary_,memory)),
        control_(duplicate_buffer<Storage>(source.control_,memory)),
        slots_(duplicate_buffer<Storage>(source.slots_,memory)),
        groups_(source.groups_),spill_size_(source.spill_size_),deleted_(source.deleted_),
        size_(source.size_),mix_(source.mix_){}
    static constexpr byte empty=0x80,tombstone=0xfe;
    static constexpr std::size_t absent=~std::size_t{0};
    template<class Consume> static void consume_located(Consume&& consume,const value_type& value,location at) {
        if constexpr(std::is_invocable_v<Consume,const value_type&,location>)consume(value,at);
        else consume(value);
    }
    static constexpr std::size_t bucket_alignment=std::size_t{1}<<std::min(6u,unsigned(std::countr_zero(stride)));
    byte* bucket(word id){
        if constexpr(IBS_STATIC_HINT_GEOMETRY)return std::assume_aligned<bucket_alignment>(primary_.data+id*stride);
        else return primary_.data+id*stride;
    }
    const byte* bucket(word id)const{
        if constexpr(IBS_STATIC_HINT_GEOMETRY)return std::assume_aligned<bucket_alignment>(primary_.data+id*stride);
        else return primary_.data+id*stride;
    }
    static unsigned present(const byte* p,unsigned tile){return unsigned(byte(~p[flags_offset+tile]));}
    static word occupied(const byte* p){
        if constexpr(IBS_PACKED_FLAGS){static_assert(!bitmap&&T<=8);return ~load<T>(p+flags_offset)&mask<N>;}
        else {word active=0;each<T>([&](auto tile){active|=word(present(p,tile))<<(tile*8);});return active;}
    }
    static word live_matches(const byte* p,word q,word matches){
        // Empty slots contain all-ones when E>K, otherwise zero. Zero remains
        // a legal quotient: only that query needs the independent flags mask.
        if constexpr(E==K)if(q==0)[[unlikely]]return matches&occupied(p);
        return matches;
    }
    static bool has(const byte* p,unsigned q){return p[q/8]&(1u<<(q%8));}
    static void mark(byte* p,unsigned slot,bool occupied) {
        if constexpr(bitmap) {if(occupied)p[slot/8]|=byte(1u<<(slot%8));else p[slot/8]&=byte(~(1u<<(slot%8)));}
        else {const byte bit=byte(1u<<(slot%8));auto& flags=p[flags_offset+slot/8];if(occupied)flags&=byte(~bit);else flags|=bit;}
    }
    static word primary_matches(const byte* p,word q) {
        static_assert(!bitmap);
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr(Filter==20&&B==64&&E>=10&&E<=20)
            return live_matches(p,q,exact_small_bucket<E,T>(p,q));
#endif
#if defined(__AVX2__) && defined(__BMI2__)
#if defined(__GFNI__)
        if constexpr(Filter==23&&B==64&&E>8&&E<16)
            return live_matches(p,q,exact_small_bucket_avx2<E,T>(p,q));
#endif
        if constexpr(Filter==21&&B==64&&E>8&&E<16){
            static_assert((T-1)*E+16<=B);
            constexpr unsigned R=E%8;
            constexpr word plane_bits=[] {word bits=0;for(unsigned i=0;i<R;++i)bits|=word{1}<<(8*i);return bits;}();
            auto matches=live_matches(p,q,small_body_matches<E,T>(p,q));
            while(matches){
                const auto row=unsigned(std::countr_zero(matches));
                const auto planes=load<8>(p+(row/8)*E+8)>>(row%8);
                if(_pext_u64(planes,plane_bits)==(q&mask<R>))return word{1}<<row;
                matches&=matches-1;
            }return 0;
        }
#endif
        if constexpr(Filter==6&&K==30&&E==30){
            auto matches=live_matches(p,q,body30_dword(p,q));
            while(matches){const auto row=unsigned(std::countr_zero(matches));
                const auto planes=load<8>(p+(row/8)*30+24)>>(row%8);
#if defined(__BMI2__)
                const auto residual=_pext_u64(planes,0x010101010101ULL);
#else
                const auto residual=(((planes&0x010101010101ULL)*0x0102040810204080ULL)>>56)&63;
#endif
                if(residual==(q&63))return word{1}<<row;
                matches&=matches-1;
            }return 0;
        }
        else if constexpr((Filter==10||Filter==17)&&K==30&&E==30){
            const sp::composition::local_equal8<6> match(q&63);unsigned residual=0;
            each<2>([&](auto tile){residual|=unsigned(match(load<8>(p+tile*30+24)))<<(tile*8);});
            const auto body=[&]{
#if defined(__AVX2__) && !defined(__AVX512VBMI__)
                if constexpr(Filter==17)return body30_dword(p,q);
#endif
                return body30(p,q);
            }();
            const auto matches=live_matches(p,q,body&residual);
            return matches;
        }
        else if constexpr(Filter>=4&&K==30&&E==30)return live_matches(p,q,exact30<Filter==5||(Filter>=8&&Filter<=9)||Filter>=12,Filter>=14>(p,q));
        else {
            using F=sp::format<E,sp::geometry::local,0>;
            const sp::dense_source<F> source{p,N};const auto expression=sp::composition::describe(source);
            word matches=0;
            if constexpr(Filter==1){
                matches=sp::composition::filter_unchecked<ikea::comparison::equal>(expression,0,N,q);
            } else if constexpr(Filter==3&&E%8&&E>=8){
                constexpr unsigned R=E%8;const auto query=plane_queries<R>::patterns[q&mask<R>];
                each<T>([&](auto tile){
                    auto body=sp::composition::filter8_unchecked<ikea::comparison::equal>(expression.payload.body,tile*8,q>>R);
                    if constexpr(R==6){
                        static_assert(T*E+2<=B,"whole-owner residual read window");
                        matches|=word(body&six_plane_mask(load<8>(p+tile*E+8*(E/8)),query))<<(tile*8);
                    }else{
                    word lanes=(load<R>(p+tile*E+8*(E/8))^~query)|~mask<R*8>;
                    if constexpr(R>4)lanes&=lanes>>32;
                    if constexpr(R>2)lanes&=lanes>>16;
                    if constexpr(R>1)lanes&=lanes>>8;
                    matches|=word(body&unsigned(lanes)&255)<<(tile*8);
                    }
                });
            } else if constexpr(Filter==2&&E%8&&E>=8){
                each<T>([&](auto tile){matches|=word(sp::composition::filter8_unchecked<ikea::comparison::equal>(expression.payload.body,tile*8,q>>(E%8)))<<(tile*8);});
                matches=live_matches(p,q,matches);
                // Compare exact residuals only in rows matching the full body.
                // This is a physical lowering of the admitted Local tail leaf;
                // all E%8 bytes are occupied, with no padding overread.
                while(matches){const unsigned row=std::countr_zero(matches),lane=row%8;
                    const word planes=load<E%8>(p+(row/8)*E+8*(E/8));
                    const word selected=(planes>>lane)&0x0101010101010101ULL;
                    const word tail=(selected*0x0102040810204080ULL)>>56;
                    if((tail&mask<E%8>)==(q&mask<E%8>))return word{1}<<row;matches&=matches-1;
                }
            } else each<T>([&](auto tile){matches|=word(sp::composition::filter8_unchecked<ikea::comparison::equal>(expression,tile*8,q))<<(tile*8);});
            matches=live_matches(p,q,matches);
            return matches;
        }
    }
#if defined(__aarch64__)
    // E64 is an eight-qword SeriesPack tile. Preserve the native comparison
    // lanes until a single byte per row remains, then find the first row.
    // This avoids four independent SIMD-to-GPR bitmap reductions.
    [[gnu::always_inline]] static word equal64_neon(const byte* p,word q) {
        const auto query=vdupq_n_u64(q);
        const auto eq0=vceqq_u64(vld1q_u64(reinterpret_cast<const word*>(p)),query);
        const auto eq1=vceqq_u64(vld1q_u64(reinterpret_cast<const word*>(p+16)),query);
        const auto eq2=vceqq_u64(vld1q_u64(reinterpret_cast<const word*>(p+32)),query);
        const auto eq3=vceqq_u64(vld1q_u64(reinterpret_cast<const word*>(p+48)),query);
        const auto ab=vuzp1q_u32(vreinterpretq_u32_u64(eq0),vreinterpretq_u32_u64(eq1));
        const auto cd=vuzp1q_u32(vreinterpretq_u32_u64(eq2),vreinterpretq_u32_u64(eq3));
        const auto halves=vuzp1q_u16(vreinterpretq_u16_u32(ab),vreinterpretq_u16_u32(cd));
        const auto lanes=vmovn_u16(halves);
        return vget_lane_u64(vreinterpret_u64_u8(lanes),0);
    }
#endif
    static constexpr bool preferred_geometry=Filter==22&&T==4&&E>8&&E<16;
    static unsigned preferred_tile(word q){return unsigned(q^(q>>6)^(q>>12))&3;}
    // All callers establish absence across the complete primary and Swiss
    // index before choosing a vacancy. Placement changes no key encoding.
    static unsigned primary_vacancy(const byte* p,word q) {
        if constexpr(bitmap)return unsigned(q);
        else {
#if IBS_PREFERRED_PLACEMENT
            if constexpr(preferred_geometry){const auto tile=preferred_tile(q);
                const auto flags=p[flags_offset+tile];
                if(flags)return tile*8+std::countr_zero(unsigned(flags));}
#endif
            unsigned slot=N;
            each<T>([&](auto tile){const auto flags=p[flags_offset+tile];
                if(slot==N&&flags)slot=tile*8+std::countr_zero(unsigned(flags));});
            return slot;
        }
    }
    static unsigned primary_find(const byte* p,word q) {
#if defined(__aarch64__) && IBS_PREFERRED_PROBE
        if constexpr(preferred_geometry){
            static_assert((T-1)*E+16<=B);
            // Zero needs the live flags. Every other residual is checked in
            // full: neither a tile miss nor a body match establishes absence.
            if(q){
                constexpr unsigned R=E%8;
                constexpr word plane_bits=[] {word bits=0;for(unsigned i=0;i<R;++i)bits|=word{1}<<(8*i);return bits;}();
                const auto tile=preferred_tile(q);
                const auto packed=vld1q_u8(p+tile*E);
                const auto body=vceq_u8(vget_low_u8(packed),vdup_n_u8(q>>R));
                const word candidates=vget_lane_u64(vreinterpret_u64_u8(body),0)&0x0101010101010101ULL;
                if(candidates){
                    const unsigned lane=std::countr_zero(candidates)/8;
                    const auto tails=vgetq_lane_u64(vreinterpretq_u64_u8(packed),1);
                    const auto residual=((((tails>>lane)&plane_bits)*0x0102040810204080ULL)>>56)&mask<R>;
                    if(residual==(q&mask<R>))[[likely]]return tile*8+lane;
                }
            }
        }
#endif
#if defined(__aarch64__) && IBS_PREFERRED_PROBE
        if constexpr(preferred_geometry){[[clang::always_inline]] return primary_find_all(p,q);}
#endif
        return primary_find_all(p,q);
    }
    static unsigned primary_find_all(const byte* p,word q) {
        if constexpr(bitmap)return has(p,unsigned(q))?unsigned(q):N;
#if defined(__aarch64__)
        else if constexpr(Filter==22&&B==64&&E>8&&E<16){
            static_assert((T-1)*E+16<=B);
            constexpr unsigned R=E%8;
            constexpr word plane_bits=[] {word bits=0;for(unsigned i=0;i<R;++i)bits|=word{1}<<(8*i);return bits;}();
            auto exact=[&](unsigned row) __attribute__((always_inline)) {
                const auto planes=load<8>(p+(row/8)*E+8)>>(row%8);
                const auto residual=(((planes&plane_bits)*0x0102040810204080ULL)>>56)&mask<R>;
                return residual==(q&mask<R>);
            };
            word candidates;
            if constexpr(IBS_NEON_FIRST_ROW&&T==4){
                const auto bodies=small_body_comparisons_neon<E>(p,q);
                if(q){
                    const auto first=first_small_body_neon(bodies);
                    if(first==255)return N;
                    // ORN leaves each true lane's index (0..31), or 255 for
                    // a false lane. A non-255 horizontal minimum is a row.
                    __builtin_assume(first<N);
                    if(exact(first))return first;
                    // The first minimum was the lowest body match. Retain the
                    // equality vectors and build an iterable mask only on retry.
                    candidates=small_body_mask_neon(bodies);
                    candidates&=candidates-1;
                }else candidates=live_matches(p,q,small_body_mask_neon(bodies));
            }else candidates=live_matches(p,q,small_body_matches_neon<E,T>(p,q));
            while(candidates){
                const auto row=unsigned(std::countr_zero(candidates));
                if(exact(row))return row;
                candidates&=candidates-1;
            }return N;
        }
        else if constexpr(Filter==19&&K==30&&E==30){
            // Select a body row before verifying its six residual bits.
            // Empty rows encode zero, so only that exact query needs flags.
            auto candidates=body30_neon_nibbles(p,q);
            while(candidates){
                const auto row=unsigned(std::countr_zero(candidates)/4);
                const auto planes=load<8>(p+(row/8)*30+24)>>(row%8);
                const auto residual=(((planes&0x010101010101ULL)*0x0102040810204080ULL)>>56)&63;
                if(residual==(q&63)&&(q||(present(p,row/8)&(1u<<(row%8)))))return row;
                candidates&=candidates-1;
            }
            return N;
        }
        else if constexpr(Filter==18&&E==64&&N==8){
            if constexpr(E==K)if(!q){const auto matches=primary_matches(p,q);return matches?unsigned(std::countr_zero(matches)):N;}
            return std::countr_zero(equal64_neon(p,q))/8; // Zero mask gives absent row N.
        }
#endif
        else if constexpr(Filter==11){
            using F=sp::format<E,sp::geometry::local,0>;const sp::dense_source<F> source{p,N};
            const word active=E==K&&q==0?occupied(p):mask<N>;
            return sp::composition::indexof_unchecked(sp::composition::describe(source),0,N,q,active);
        }
        else {const auto matches=primary_matches(p,q);return matches?unsigned(std::countr_zero(matches)):N;}
    }
    template<class Consume> static void consume_primary(const byte* p,unsigned slot,Consume&& consume) {
        if constexpr(V>0) {
            // Wide byte values are consecutive 64-bit SeriesPack limbs. Their
            // limbs remain together: consuming one 64-byte value must not
            // require eight separately strided cache lines.
            value_type value;
            each<(V+7)/8>([&](auto c){constexpr unsigned width=std::min(8u,V-c*8);
                const auto v=[&]{if constexpr(IBS_VALUE_BASE&&V%8==0)
                        // The F64 payload is a linear array of limbs. Rebase
                        // this value once, preserving the authored bounded
                        // point reads and avoiding per-limb row arithmetic.
                        // This subwindow does not authorize padded tile loads;
                        // the argument is specific to residual-free F64.
                        return direct_point<64>(p+value_offset+std::size_t(slot)*V,c,V/8);
                    else if constexpr(V%8==0)return direct_point<64>(p+value_offset,slot*(V/8)+c,value_rows*(V/8));
                    else return direct_point<width*8>(p+value_offset+std::size_t(c)*value_rows*8,slot,value_rows);}();
                store<width>(value.data()+c*8,v);
            });consume(value);
        }
    }
    static void write_value(byte* p,unsigned slot,const value_type& value) {
        if constexpr(V>0) each<(V+7)/8>([&](auto c){constexpr unsigned width=std::min(8u,V-c*8);
            if constexpr(V%8==0)set_point<64>(p+value_offset,slot*(V/8)+c,value_rows*(V/8),load<width>(value.data()+c*8));
            else set_point<width*8>(p+value_offset+std::size_t(c)*value_rows*8,slot,value_rows,load<width>(value.data()+c*8));});
    }
    static word tuple_key(const byte* p){return tuple_field<key_bytes>::read(p);}
    template<class Consume> static void consume_tuple(const byte* p,Consume&& consume) {
        if constexpr(V){value_type value;each<(V+7)/8>([&](auto c){constexpr unsigned width=std::min(8u,V-c*8);
            store<width>(value.data()+c*8,tuple_field<width>::read(p+key_bytes+c*8));});consume(value);}
    }
    std::size_t find_swiss(word key,const typename mix_type::result& h)const {
        if(!groups_)return absent;
        std::size_t g=h.swiss&(groups_-1),step=0;
        for(;;) {
            const byte* controls=control_.data+g*16;
            auto candidates=equal_controls(controls,h.fingerprint);
            while(candidates){const auto i=std::countr_zero(candidates);candidates&=candidates-1;
                const auto index=g*16+i;
                if(tuple_key(slots_.data+index*tuple_bytes)==key)return index;
            }
            // All matches in this group precede the empty-group termination.
            if(equal_controls(controls,empty))return absent;
            g=(g+(++step))&(groups_-1);
        }
    }
    struct overflow_value {value_type value{};bool found=false;};
    using overflow_result=std::conditional_t<IBS_OVERFLOW_SLOT,const byte*,overflow_value>;
    static bool overflow_found(const overflow_result& result) {
        if constexpr(IBS_OVERFLOW_SLOT)return result!=nullptr;
        else return result.found;
    }
    // Keep TuplePack's prepared controls and Swiss probe state out of the
    // primary loop's live register set. Return the value, so the caller can
    // still consume its primary value directly in the success branch.
    [[gnu::noinline,gnu::pure]] IBS_OVERFLOW_ABI overflow_result read_overflow(word key,word swiss)const {
        const typename mix_type::result h{0,0,swiss,byte(swiss>>57)};
        const auto found=find_swiss(key,h);
        if constexpr(IBS_OVERFLOW_SLOT)return found==absent?nullptr:slots_.data+found*tuple_bytes;
        else {
            overflow_result result;
            if(found!=absent){consume_tuple(slots_.data+found*tuple_bytes,[&](const auto& value){result.value=value;});result.found=true;}
            return result;
        }
    }
    void insert_swiss_unchecked(word key,const value_type& v,const typename mix_type::result& h) {
        std::size_t g=h.swiss&(groups_-1),step=0;
        for(;;) {
            byte* controls=control_.data+g*16;
            auto available=equal_controls(controls,empty)|equal_controls(controls,tombstone);
            if(available){const auto i=std::countr_zero(available);const auto index=g*16+i;
                deleted_-=controls[i]==tombstone;store<key_bytes>(slots_.data+index*tuple_bytes,key);
                if constexpr(V)std::memcpy(slots_.data+index*tuple_bytes+key_bytes,v.data(),V);
                controls[i]=h.fingerprint;++spill_size_;return;
            }g=(g+(++step))&(groups_-1);
        }
    }
    void rehash_swiss(std::size_t groups) {
        // Prepare both fallible allocations before touching the live owner.
        allocation next_control(groups*16,memory_),next_slots(groups*16*tuple_bytes,memory_);
        std::memset(next_control.data,empty,groups*16);
        allocation old_control=std::move(control_),old_slots=std::move(slots_);const auto old_groups=groups_;
        control_=std::move(next_control);slots_=std::move(next_slots);groups_=groups;spill_size_=deleted_=0;
        for(std::size_t i=0;i<old_groups*16;++i)if(old_control.data[i]<128){
            const auto key=load<key_bytes>(old_slots.data+i*tuple_bytes);value_type v{};
            if constexpr(V)std::memcpy(v.data(),old_slots.data+i*tuple_bytes+key_bytes,V);
            insert_swiss_unchecked(key,v,mix_(key));
        }
    }
  public:
    // Geometry, seed, tombstones and sticky overflow counts are unchanged.
    // The caller publishes this independent owner only after private edits.
    table clone(allocation_context* memory=nullptr)const{return table(*this,memory);}
    explicit table(word seed,allocation_context* memory=nullptr):memory_(memory),primary_(primary_bytes(),memory),mix_{seed&mask<W>} {
        if constexpr(!bitmap)for(std::size_t i=0;i<bucket_count;++i){
            if constexpr(E>K)std::memset(bucket(i),255,T*E);
            std::memset(bucket(i)+flags_offset,255,T);
        }
        // Validate the actual tile placement, including 96-byte bucket strides.
        if constexpr(!bitmap){using F=sp::format<E,sp::geometry::local,0>;
            auto view=sp::view<F,byte>::attach(N,{{{{primary_.data,T*E},E},{},{}}});require(bool(view),"SeriesPack bucket admission");}
    }
    std::size_t size()const{return size_;}
    std::size_t spill_size()const{return spill_size_;}
    std::size_t swiss_groups()const{return groups_;}
    std::size_t primary_size()const{return size_-spill_size_;}
    std::size_t bytes_used()const{return primary_.capacity()+control_.capacity()+slots_.capacity()+sizeof(*this);}
    storage_usage storage()const{auto result=primary_.usage();result+=control_.usage();result+=slots_.usage();return result;}
    void retain_storage(typename Storage::retention& holds)const {primary_.retain(holds);control_.retain(holds);slots_.retain(holds);}
    typename Storage::trim_result relinquish_surplus(){
        for(auto* storage:{&primary_,&control_,&slots_}){auto result=storage->relinquish_surplus();if(!result)return result;}return {};
    }
    template<class Visit> void allocations(Visit&& visit)const {
        visit("primary",primary_.data,primary_.size);
        if(control_.size)visit("swiss_controls",control_.data,control_.size);
        if(slots_.size)visit("swiss_tuples",slots_.data,slots_.size);
    }
    std::size_t capacity()const{return bucket_count*N;}
    bool primary_contains(word key)const {const auto p=prepare(key);return primary_find(p.primary,p.hash.quotient)!=N;}
    write_position prepare_write(word key,value_type& previous) {
        require(!(key&~mask<W>),"prepared key outside W-bit domain");
        const auto h=mix_(key);auto* p=bucket(h.bucket);auto slot=primary_find(p,h.quotient);
        if(slot!=N){consume_primary(p,slot,[&](const auto& value){previous=value;});return {h,slot,true,true};}
        if constexpr(!bitmap)if(p[overflow_offset]){
            const auto found=find_swiss(key,h);
            if(found!=absent){consume_tuple(slots_.data+found*tuple_bytes,[&](const auto& value){previous=value;});return {h,found,false,true};}
        }
        slot=primary_vacancy(p,h.quotient);
        if(slot!=N)return {h,slot,true,false};
        if(!groups_||(spill_size_+deleted_+1)*8>groups_*16*7)rehash_swiss(groups_?groups_*2:1);
        std::size_t group=h.swiss&(groups_-1),step=0;
        for(;;){const auto* controls=control_.data+group*16;
            const auto available=equal_controls(controls,empty)|equal_controls(controls,tombstone);
            if(available)return {h,group*16+std::countr_zero(available),false,false};
            group=(group+(++step))&(groups_-1);
        }
    }
    bool insert_prepared_unchecked(word key,const write_position& position,const value_type& value) {
        auto* p=bucket(position.hash.bucket);const auto slot=position.slot;
        if(position.primary){
            if(!position.present){if constexpr(!bitmap)set_point<E>(p,slot,N,position.hash.quotient);mark(p,slot,true);}
            write_value(p,slot,value);
        }else {
            if(!position.present){store<key_bytes>(slots_.data+slot*tuple_bytes,key);
                deleted_-=control_.data[slot]==tombstone;control_.data[slot]=position.hash.fingerprint;
                ++spill_size_;if(p[overflow_offset]!=255)++p[overflow_offset];}
            if constexpr(V)std::memcpy(slots_.data+slot*tuple_bytes+key_bytes,value.data(),V);
        }
        size_+=!position.present;return !position.present;
    }
    // The caller has already established absence and retains exclusive owner
    // access until insert. This is the exact vacant slot insert will choose.
    location vacancy_for_absent(word key)const {
        const auto p=prepare(key);const auto slot=primary_vacancy(p.primary,p.hash.quotient);
        return {p.hash.bucket,slot,slot!=N};
    }
    // Private/exclusive owner preflight. A subsequent absent-key insertion
    // cannot allocate if no intervening index mutation consumes this room.
    void prepare_absent(word key) {
        require(!(key&~mask<W>),"prepared key outside W-bit domain");
        const auto p=prepare(key);
        require(primary_find(p.primary,p.hash.quotient)==N,"prepared key is present");
        if constexpr(!bitmap){
            require(!p.primary[overflow_offset]||find_swiss(key,p.hash)==absent,"prepared spill key is present");
            if(!vacancy_for_absent(key).primary&&(!groups_||(spill_size_+deleted_+1)*8>groups_*16*7))
                rehash_swiss(groups_?groups_*2:1);
        }
    }
    [[gnu::always_inline]] probe prepare(word key)const {auto h=mix_(key);return {h,bucket(h.bucket)};}
    template<bool Hint=true> [[gnu::always_inline]] void prefetch(const probe& p)const {
        if constexpr(Hint)__builtin_prefetch(p.primary,0,3);
    }
    template<bool Eager=false,class Consume> [[gnu::always_inline]] bool find(word key,Consume&& consume)const {
        const auto p=prepare(key);
        if constexpr(V>0&&IBS_VALUE_HINT_LINES>0){
            constexpr auto lines=std::min<unsigned>(IBS_VALUE_HINT_LINES,(value_rows*V+63)/64);
            each<lines>([&](auto line){__builtin_prefetch(p.primary+value_offset+line*64,0,3);});
        }
        if constexpr(Eager)if(groups_)__builtin_prefetch(control_.data+(p.hash.swiss&(groups_-1))*16,0,3);
        return finish(p,key,std::forward<Consume>(consume));
    }
    template<class Consume> [[gnu::always_inline]] bool finish(const probe& p,word key,Consume&& consume)const {
#if defined(__aarch64__)
        if constexpr(Filter==18&&E==64&&N==8&&V==0){
            // Membership-only consumers do not need a row index at all.
            bool found;
            if constexpr(E==K)found=p.hash.quotient?bool(equal64_neon(p.primary,p.hash.quotient)):bool(primary_matches(p.primary,0));
            else found=bool(equal64_neon(p.primary,p.hash.quotient));
            if(found)return true;
            return p.primary[overflow_offset]&&overflow_found(read_overflow(key,p.hash.swiss));
        }
#endif
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr((Filter==13||Filter==14)&&E==64&&N==8&&V==8){
            auto values=_mm512_loadu_si512(p.primary+value_offset);
            asm("" : "+v"(values));
            __mmask8 exact=_mm512_cmpeq_epi64_mask(_mm512_loadu_si512(p.primary),_mm512_set1_epi64(p.hash.quotient));
            if constexpr(E==K)if(p.hash.quotient==0)[[unlikely]]exact&=__mmask8(occupied(p.primary));
            asm("" : "+k"(exact));
            if(exact){
                const auto selected=_mm512_maskz_compress_epi64(exact,values);
                value_type value;store<8>(value.data(),_mm_cvtsi128_si64(_mm512_castsi512_si128(selected)));
                consume_located(consume,value,{p.hash.bucket,unsigned(std::countr_zero(unsigned(exact))),true});return true;
            }
        }else if constexpr(Filter==16&&K==30&&E==30&&V==8){
            // The six residual planes provide an early, imperfect row mask.
            // Start the candidate value load from that mask while the 24-bit
            // bodies verify exact membership. A residual tie always falls
            // back to the exact row; inactive zero rows remain excluded.
            const auto query=plane_queries<6>::patterns[p.hash.quotient&63];
            const auto residual=unsigned(live_matches(p.primary,p.hash.quotient,
                six_plane_mask(load<8>(p.primary+24),query)|
                six_plane_mask(load<8>(p.primary+54),query)<<8));
            const auto first=unsigned(std::countr_zero(residual|0x8000u));
            auto value=direct_point<64>(p.primary+value_offset,first,N);
            auto exact=residual&prepare30<true>(p.primary,p.hash.quotient).body;
            asm("" : "+r"(exact) : "r"(value));
            if(exact&(1u<<first))[[likely]]{
                value_type out;store<8>(out.data(),value);consume_located(consume,out,{p.hash.bucket,first,true});return true;
            }
            if(exact){const auto slot=unsigned(std::countr_zero(exact));
                consume_primary(p.primary,slot,[&](const auto& out){consume_located(consume,out,{p.hash.bucket,slot,true});});return true;}
        }else if constexpr(Filter==12&&K==30&&E==30&&V==8){
            const auto parts=prepare30(p.primary,p.hash.quotient);
            const auto body=unsigned(live_matches(p.primary,p.hash.quotient,parts.body));
            // A missing body speculates at initialized slot 15, never past N.
            // Checking the likely exact first candidate first avoids both the
            // body-nonempty branch and a separate exact-mask-nonempty branch.
            const auto first=unsigned(std::countr_zero(body|0x8000u));
            auto value=direct_point<64>(p.primary+value_offset,first,N);
            auto exact=body&residual30(parts.packed,p.hash.quotient);
            asm("" : "+r"(exact) : "r"(value));
            if(exact&(1u<<first))[[likely]]{
                value_type out;store<8>(out.data(),value);consume_located(consume,out,{p.hash.bucket,first,true});return true;
            }
            if(exact){const auto slot=unsigned(std::countr_zero(exact));
                consume_primary(p.primary,slot,[&](const auto& out){consume_located(consume,out,{p.hash.bucket,slot,true});});return true;}
        }else if constexpr((Filter==13||Filter==14)&&K==30&&E==30&&V==8){
            // Keep the exact match in mask registers through value selection.
            // A unique key has at most one live matching row. Compress each
            // half into its low lane, then OR the two zero-masked results.
            // This avoids mask -> GPR -> CTZ -> vector-index on the value path.
            auto lo=_mm512_loadu_si512(p.primary+value_offset);
            auto hi=_mm512_loadu_si512(p.primary+value_offset+64);
            asm("" : "+v"(lo),"+v"(hi));
            __mmask16 exact=__mmask16(exact30<true,Filter==14>(p.primary,p.hash.quotient));
            if(p.hash.quotient==0)[[unlikely]]exact&=__mmask16(occupied(p.primary));
            // Keep LLVM from undoing the mask shift into a GPR round trip.
            asm("" : "+k"(exact));
            if(!_kortestz_mask16_u8(exact,exact)){
                __mmask16 upper;asm("kshiftrw $8, %1, %0" : "=k"(upper) : "k"(exact));
                const auto a=_mm512_maskz_compress_epi64(__mmask8(exact),lo);
                const auto z=_mm512_maskz_compress_epi64(__mmask8(upper),hi);
                const auto selected=_mm_or_si128(_mm512_castsi512_si128(a),_mm512_castsi512_si128(z));
                value_type value;store<8>(value.data(),_mm_cvtsi128_si64(selected));
                consume_located(consume,value,{p.hash.bucket,unsigned(std::countr_zero(exact)),true});return true;
            }
        }else if constexpr(Filter==9&&K==30&&E==30&&V==8){
            // Private coherent owner: load both value tiles alongside the key
            // tile, then select the exact row in registers. This measures the
            // cost of removing the second dependent memory access entirely.
            auto lo=_mm512_loadu_si512(p.primary+value_offset);
            auto hi=_mm512_loadu_si512(p.primary+value_offset+64);
            asm("" : "+v"(lo),"+v"(hi));
            const auto exact=live_matches(p.primary,p.hash.quotient,exact30<true>(p.primary,p.hash.quotient));
            if(exact){
                const auto row=_mm512_castsi128_si512(_mm_cvtsi32_si128(std::countr_zero(exact)));
                const auto selected=_mm512_permutex2var_epi64(lo,row,hi);
                value_type value;store<8>(value.data(),_mm_cvtsi128_si64(_mm512_castsi512_si128(selected)));
                consume_located(consume,value,{p.hash.bucket,unsigned(std::countr_zero(exact)),true});return true;
            }
        }else if constexpr(Filter==8&&K==30&&E==30&&V==8){
            auto parts=prepare30(p.primary,p.hash.quotient);
            const auto body=unsigned(live_matches(p.primary,p.hash.quotient,parts.body));
            if(body)[[likely]]{
                const auto first=unsigned(std::countr_zero(body));
                auto value=direct_point<64>(p.primary+value_offset,first,N);
                // Private-owner experiment: a bounded, initialized value may
                // be read before its residual confirms the exact key. This
                // requires coherent backing, including for nonmatching rows.
                // The empty compiler barrier preserves the early load in the
                // emitted code; it imposes no hardware ordering or fence.
                auto exact=body&residual30(parts.packed,p.hash.quotient);
                asm("" : "+r"(exact) : "r"(value));
                if(exact){if(!(exact&(1u<<first)))value=direct_point<64>(p.primary+value_offset,std::countr_zero(exact),N);
                    value_type bytes;store<8>(bytes.data(),value);consume_located(consume,bytes,{p.hash.bucket,unsigned(std::countr_zero(exact)),true});return true;}
            }
        }else
#endif
        {
        if constexpr(bitmap||Filter==11||Filter==18||Filter==19||Filter==22){const auto slot=primary_find(p.primary,p.hash.quotient);
            if(slot!=N){consume_primary(p.primary,slot,[&](const auto& value){consume_located(consume,value,{p.hash.bucket,slot,true});});return true;}
        }else {const auto matches=primary_matches(p.primary,p.hash.quotient);
            if(matches){const unsigned slot=std::countr_zero(matches);
                consume_primary(p.primary,slot,[&](const auto& value){consume_located(consume,value,{p.hash.bucket,slot,true});});return true;}
        }
        }
        if constexpr(bitmap)return false;
        else {
            if(!p.primary[overflow_offset])return false;
            const auto result=read_overflow(key,p.hash.swiss);
            if constexpr(V)if(overflow_found(result)){
                if constexpr(IBS_OVERFLOW_SLOT)consume_tuple(result,[&](const auto& value){consume_located(consume,value,{p.hash.bucket,N,false});});
                else consume_located(consume,result.value,{p.hash.bucket,N,false});
            }
            return overflow_found(result);
        }
    }
    template<class Emit> void prepare_staged(staged_probe& state,word key,Emit&& emit)const {
        state={prepare(key),key};emit(state.first.primary,B);
        // Speculative reference-plane lookahead. Both emitted extents belong
        // to this bucket. The caller still chooses the cache tier and schedule.
        if constexpr(IBS_STAGED_PRIMARY_VALUES&&V==8&&!bitmap)
            each<value_rows/8>([&](auto line){emit(state.first.primary+value_offset+line*64,64);});
    }
    // One call advances only to an address-dependent boundary. The caller owns
    // whether to prefetch, its cache tier, and the scheduling/yield operation.
    template<class Consume,class Emit> bool advance(staged_probe& s,bool& found,Consume&& consume,Emit&& emit)const {
        auto* primary=s.first.primary;
        if constexpr(IBS_STATIC_HINT_GEOMETRY)primary=std::assume_aligned<bucket_alignment>(primary);
        if(s.phase==0){
            const auto slot=primary_find(primary,s.first.hash.quotient);
            if constexpr(!(IBS_STAGED_PRIMARY_VALUES==2&&V==8&&!bitmap))s.slot=slot;
            if(slot!=N){
                if constexpr(V){
                    if constexpr(IBS_STAGED_PRIMARY_VALUES==2&&V==8&&!bitmap){
                        consume_primary(primary,slot,consume);found=true;return true;
                    }
                    if constexpr(V%8==0||V<8)emit(primary+value_offset+std::size_t(s.slot)*V,V);
                    else each<(V+7)/8>([&](auto c){constexpr unsigned width=std::min(8u,V-c*8);emit(primary+value_offset+c*value_rows*8+s.slot*width,width);});
                    s.phase=3;return false;
                }else {found=true;return true;}
            }
            if constexpr(bitmap){found=false;return true;}
            else if(!primary[overflow_offset]){found=false;return true;}
            s.group=s.first.hash.swiss&(groups_-1);s.phase=1;emit(IBS_STATIC_HINT_GEOMETRY?std::assume_aligned<16>(control_.data+s.group*16):control_.data+s.group*16,16);return false;
        }
        if(s.phase==3){consume_primary(primary,s.slot,consume);found=true;return true;}
        if(s.phase==1){const auto* controls=control_.data+s.group*16;
            s.candidates=equal_controls(controls,s.first.hash.fingerprint);s.empty_group=equal_controls(controls,empty)!=0;
        }else {
            const auto slot=s.group*16+std::countr_zero(s.candidates);s.candidates&=s.candidates-1;
            const auto* tuple=slots_.data+slot*tuple_bytes;
            if(tuple_key(tuple)==s.key){consume_tuple(tuple,consume);found=true;return true;}
        }
        if(s.candidates){s.phase=2;emit(slots_.data+(s.group*16+std::countr_zero(s.candidates))*tuple_bytes,tuple_bytes);return false;}
        if(s.empty_group){found=false;return true;}
        s.group=(s.group+(++s.step))&(groups_-1);s.phase=1;emit(IBS_STATIC_HINT_GEOMETRY?std::assume_aligned<16>(control_.data+s.group*16):control_.data+s.group*16,16);return false;
    }
    bool insert(word key,const value_type& value={}) {
        require(!(key&~mask<W>),"key outside W-bit domain");
        const auto h=mix_(key);auto* p=bucket(h.bucket);auto slot=primary_find(p,h.quotient);
        if(slot!=N){write_value(p,slot,value);return false;}
        if constexpr(!bitmap)if(p[overflow_offset]){auto found=find_swiss(key,h);if(found!=absent){
            if constexpr(V)std::memcpy(slots_.data+found*tuple_bytes+key_bytes,value.data(),V);return false;}}
        slot=primary_vacancy(p,h.quotient);
        if(slot!=N){if constexpr(!bitmap)set_point<E>(p,slot,N,h.quotient);write_value(p,slot,value);mark(p,slot,true);}
        else {
            if(!groups_||(spill_size_+deleted_+1)*8>groups_*16*7)
                rehash_swiss(groups_?groups_*2:1);
            insert_swiss_unchecked(key,value,h);if(p[overflow_offset]!=255)++p[overflow_offset];
        }++size_;return true;
    }
    bool erase(word key) {
        require(!(key&~mask<W>),"erase key outside W-bit domain");
        const auto h=mix_(key);auto* p=bucket(h.bucket);auto slot=primary_find(p,h.quotient);
        if(slot!=N){if constexpr(!bitmap)set_point<E>(p,slot,N,E>K?mask<E>:0);mark(p,slot,false);--size_;return true;}
        if constexpr(!bitmap)if(p[overflow_offset]){const auto found=find_swiss(key,h);if(found!=absent){
            control_.data[found]=tombstone;--spill_size_;++deleted_;--size_;if(p[overflow_offset]!=255)--p[overflow_offset];return true;}}
        return false;
    }
    template<class Context,class Consume> lookup_status guarded_find(word key,Context& context,Consume&& consume)const {
        if((key&~mask<W>)||!context.enter(*this,key,access_intent::read))return lookup_status::denied;
        return find(key,std::forward<Consume>(consume))?lookup_status::found:lookup_status::absent;
    }
  private:
    template<class Context> mutation_status guarded_mutate(word key,const value_type* value,Context& context) {
        if((key&~mask<W>)||!context.enter(*this,key,access_intent::write))return mutation_status::denied;
        const auto h=mix_(key);auto* p=bucket(h.bucket);auto slot=primary_find(p,h.quotient);
        const bool existing_primary=slot!=N;
        auto swiss=absent;
        if constexpr(!bitmap)if(!existing_primary&&p[overflow_offset])swiss=find_swiss(key,h);
        const bool existing=existing_primary||swiss!=absent;
        if(!value&&!existing)return mutation_status::absent;
        if constexpr(!V)if(value&&existing)return mutation_status::unchanged;
        if(value&&!existing){
            slot=primary_vacancy(p,h.quotient);
            if(slot==N){
                if(!groups_||(spill_size_+deleted_+1)*8>groups_*16*7)return mutation_status::needs_capacity;
                std::size_t group=h.swiss&(groups_-1),step=0;
                for(;;){const auto available=equal_controls(control_.data+group*16,empty)|equal_controls(control_.data+group*16,tombstone);
                    if(available){swiss=group*16+std::countr_zero(available);break;}
                    group=(group+(++step))&(groups_-1);
                }
            }
        }
        std::array<mutation_damage,4> damage{};unsigned count=0;
        auto add=[&](owner_part part,const byte* base,std::size_t extent,std::size_t offset,std::size_t length){
            damage[count++]={{this,part,base,extent},{0,offset,length}};
        };
        if(slot!=N)add(owner_part::primary,primary_.data,primary_.size,h.bucket*stride,stride);
        else {
            if(!existing||!value){
                add(owner_part::swiss_controls,control_.data,control_.size,swiss,1);
                if(p[overflow_offset]!=255)add(owner_part::primary,primary_.data,primary_.size,h.bucket*stride+overflow_offset,1);
            }
            if(value)add(owner_part::swiss_tuples,slots_.data,slots_.size,
                swiss*tuple_bytes+(existing?key_bytes:0),existing?V:tuple_bytes);
        }
        // Population and Swiss tombstone counters are logical owner state,
        // separate from the three allocations. No pointer is replaced here.
        if(!existing||!value)add(owner_part::header,reinterpret_cast<const byte*>(this),sizeof(*this),0,sizeof(*this));
        const auto writes=std::span<const mutation_damage>(damage.data(),count);
        if(!context.prepare(writes))return mutation_status::denied;
        static_assert(noexcept(context.before(damage[0].source,damage[0].span)),"damage publication must not throw after preparation");
        for(const auto& d:writes)context.before(d.source,d.span);
        // All discovery, admission and fallible preparation have finished.
        // The existing packing writers erase leaf coverage under these spans.
        if(slot!=N){
            if(value){
                if(!existing){if constexpr(!bitmap)set_point<E>(p,slot,N,h.quotient);mark(p,slot,true);++size_;}
                write_value(p,slot,*value);
            }else {if constexpr(!bitmap)set_point<E>(p,slot,N,E>K?mask<E>:0);mark(p,slot,false);--size_;}
        }else if(value){
            if(!existing){
                deleted_-=control_.data[swiss]==tombstone;
                store<key_bytes>(slots_.data+swiss*tuple_bytes,key);
                control_.data[swiss]=h.fingerprint;++spill_size_;++size_;
                if(p[overflow_offset]!=255)++p[overflow_offset];
            }
            if constexpr(V)std::memcpy(slots_.data+swiss*tuple_bytes+key_bytes,value->data(),V);
        }else {
            control_.data[swiss]=tombstone;--spill_size_;++deleted_;--size_;
            if(p[overflow_offset]!=255)--p[overflow_offset];
        }
        return !value?mutation_status::erased:existing?mutation_status::updated:mutation_status::inserted;
    }
  public:
    template<class Context> mutation_status guarded_insert(word key,const value_type& value,Context& context) {
        return guarded_mutate(key,&value,context);
    }
    template<class Context> mutation_status guarded_erase(word key,Context& context) {
        return guarded_mutate(key,nullptr,context);
    }
    template<class Visit> void visit(Visit&& visitor)const {
        for(std::size_t i=0;i<bucket_count;++i){const auto* p=bucket(i);
            for(unsigned slot=0;slot<N;++slot){bool occupied;
                if constexpr(bitmap)occupied=has(p,slot);else occupied=present(p,slot/8)&(1u<<(slot%8));
                if(!occupied)continue;
                word q;if constexpr(bitmap)q=slot;else q=point<E>(p,slot,N);
                value_type v{};consume_primary(p,slot,[&](const auto& x){v=x;});visitor(mix_.recover(i,q),v);
            }}
        for(std::size_t i=0;i<groups_*16;++i)if(control_.data[i]<128){value_type v{};
            if constexpr(V)std::memcpy(v.data(),slots_.data+i*tuple_bytes+key_bytes,V);
            visitor(load<key_bytes>(slots_.data+i*tuple_bytes),v);}
    }
    void validate()const {
        std::size_t count=0;std::vector<std::size_t> spills(bucket_count);
        for(std::size_t i=0;i<groups_*16;++i)if(control_.data[i]<128)++spills[mix_(load<key_bytes>(slots_.data+i*tuple_bytes)).bucket];
        if constexpr(!bitmap)for(std::size_t i=0;i<bucket_count;++i)
            require(bucket(i)[overflow_offset]==255||bucket(i)[overflow_offset]==spills[i],"exact overflow count");
        if constexpr(K==30&&E==30)for(std::size_t i=0;i<bucket_count;++i)
            require(bucket(i)[62]==0,"reserved zero shuffle byte");
        visit([&](word key,const auto&){++count;require(find(key,[](auto){}),"iteration reachable");});
        require(count==size_,"owner population");
    }
};
} // namespace iceberg

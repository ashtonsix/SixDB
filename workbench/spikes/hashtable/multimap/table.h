#pragma once
#include <ikea/hashtable/table.h>
#include "effects.h"
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

namespace write_arch {
namespace ht=ikea::hashtable;
using byte=std::byte;
inline unsigned number(byte value) noexcept { return std::to_integer<unsigned>(value); }
using ht::word;
template<unsigned Bits> [[gnu::always_inline]] inline word point(const byte* p,unsigned row,unsigned rows) {
    return ht::point<Bits>(ikea::to_u8(p),row,rows);
}
template<unsigned Bits> [[gnu::always_inline]] inline word direct_point(const byte* p,unsigned row,unsigned rows) {
    return ht::direct_point<Bits>(ikea::to_u8(p),row,rows);
}
template<unsigned Bytes> struct tuple_field {
    [[gnu::always_inline]] static word read(const byte* p) { return ht::tuple_field<Bytes>::read(ikea::to_u8(p)); }
};
[[gnu::always_inline]] inline auto equal_controls(const byte* p,byte query) { return ht::equal_controls(ikea::to_u8(p),std::to_integer<std::uint8_t>(query)); }
[[gnu::always_inline]] inline auto equal_controls(const byte* p,std::uint8_t query) { return ht::equal_controls(ikea::to_u8(p),query); }
[[gnu::always_inline]] inline bool occupied_control(byte value) { return ht::detail::occupied_control(std::to_integer<std::uint8_t>(value)); }


// With no residual plane, complete Local tiles are one contiguous byte array.
// Preserve the SeriesPack assignment operation while making that address law
// visible to the compiler, as the maintained reader already does.
struct contiguous_write_ops:ht::word_tail_write_ops {
    template<class S,ht::sp::author::field_kind Field,unsigned Bits>
    void write(const ht::sp::author::leaf<S,Field>& destination,std::size_t row,
            ht::sp::author::scalar_value<Bits> value)const {
        using F=typename S::layout_type;
        if constexpr(Field==ht::sp::author::field_kind::body&&F::tail==0&&F::storage==ht::sp::geometry::local)
            ht::store<F::body>(destination.source.stream(0).bytes.data()+row*F::body,value.value);
        else ht::word_tail_write_ops::write(destination,row,value);
    }
};
template<unsigned Bits> [[gnu::always_inline]] void assign(byte* p,unsigned row,unsigned rows,word value) {
    auto view=ht::placed<Bits>(ikea::to_u8(p),rows);contiguous_write_ops ops;
    ht::sp::author::assign(ops,ht::sp::author::expression(view),row,ht::sp::author::scalar_value<Bits>{value});
}

// Independent mutable owner. The packed arm preserves the original primary
// representation and capacity. The whole-byte arm widens keys at fixed N/b,
// explicitly paying for the larger bucket. Both use real SeriesPack keys and
// values and the same contiguous TuplePack projections for Swiss overflow.
// This initial architectural control covers K14 and K30, not the all-W API.
enum class upsert_flow { split, shared_value, shared_stores };
// Keep this experiment's mask selection a select. LLVM otherwise converts the
// conditional expression into an outcome branch, including with unpredictable
// metadata, which fails to test the intended mixed-outcome path. Both inputs
// are plain masks; no speculative load/store crosses this operation.
[[gnu::always_inline]] inline unsigned select_candidates(bool fresh,unsigned found,unsigned vacancy) {
#if defined(__x86_64__)
    asm("testb %b2, %b2\n\tcmovnel %k1, %k0" : "+r"(found) : "r"(vacancy),"r"(fresh) : "cc");
    return found;
#elif defined(__aarch64__)
    asm("cmp %w2, #0\n\tcsel %w0, %w1, %w0, ne" : "+r"(found) : "r"(vacancy),"r"(fresh) : "cc");
    return found;
#else
    return fresh?vacancy:found;
#endif
}
template<unsigned W,unsigned b,unsigned V,bool WholeBytes,bool LiveRows=false,
    upsert_flow Flow=upsert_flow::split> class standalone_table {
    static constexpr unsigned K=W-b,base_B=K<=30?64:96,T=(base_B-1)/(K+1);
    static_assert(K==14||K==30);
    static_assert(V%8==0&&V<=128);
    static_assert(!LiveRows||V==8||V==64);
    static constexpr unsigned E=WholeBytes?((K+7)/8)*8:K;
    static constexpr unsigned flags=T*E;
    static constexpr unsigned B=WholeBytes?((flags+T+1+31)/32)*32:base_B;
    static constexpr unsigned stride=B+8*T*V,key_bytes=(W+7)/8,tuple_bytes=key_bytes+V;
    static constexpr std::size_t absent=~std::size_t{0};
    static constexpr byte empty=byte{ht::detail::control_empty},tombstone=byte{ht::detail::control_tombstone};
    using buffer=ht::heap_storage_policy::buffer;
    using hash_type=ht::permutation<W,K>;
    using hash_result=typename hash_type::result;
    buffer primary_,controls_,slots_;
    hash_type hash_;
    std::size_t groups_=0,spill_=0,deleted_=0,size_=0;
    byte* bucket(word id){return primary_.data+id*stride;}
    const byte* bucket(word id)const{return primary_.data+id*stride;}
    static word vacancies(const byte* p){return ht::load<T>(p+flags);}
    static unsigned locate(const byte* p,word q) {
        word matches=0;
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr(E==14)matches=ht::detail::exact_small_bucket<E,T>(ikea::to_u8(p),q);
        else if constexpr(E==16)matches=_mm512_cmpeq_epi16_mask(_mm512_loadu_si512(p),_mm512_set1_epi16(short(q)));
        else if constexpr(E==30)matches=ht::detail::exact30<true,false>(ikea::to_u8(p),q);
        else if constexpr(E==32)matches=_mm512_cmpeq_epi32_mask(_mm512_loadu_si512(p),_mm512_set1_epi32(int(q)));
        else
#endif
#if defined(__aarch64__)
        if constexpr(E==14){
            constexpr word planes=0x010101010101ULL;
            auto exact=[&](unsigned row) __attribute__((always_inline)) {
                const auto tails=ht::load<8>(p+(row/8)*E+8)>>(row%8);
                return ((((tails&planes)*0x0102040810204080ULL)>>56)&63)==(q&63);
            };
            const auto bodies=ht::detail::small_body_comparisons_neon<E>(ikea::to_u8(p),q);
            if(q){const auto first=ht::detail::first_small_body_neon(bodies);if(first==255)return N;
                if(exact(first))return first;
                matches=ht::detail::small_body_mask_neon(bodies);matches&=matches-1;
            }else matches=ht::detail::small_body_mask_neon(bodies)&~vacancies(p);
            while(matches){const auto row=unsigned(std::countr_zero(matches));if(exact(row))return row;matches&=matches-1;}
            return N;
        }else if constexpr(E==30){
            // Maintained NEON reader: pick a body row, then check its six
            // residual bits. Shared upserts retain their complete-mask path.
            auto candidates=ht::detail::body30_neon_nibbles(ikea::to_u8(p),q);
            while(candidates){
                const auto row=unsigned(std::countr_zero(candidates)/4);
                const auto planes=ht::load<8>(p+(row/8)*30+24)>>(row%8);
                const auto residual=(((planes&0x010101010101ULL)*0x0102040810204080ULL)>>56)&63;
                if(residual==(q&63)&&(q||!(vacancies(p)&(word{1}<<row))))return row;
                candidates&=candidates-1;
            }
            return N;
        }else if constexpr(E==16){
            const auto query=vdupq_n_u16(std::uint16_t(q));
            auto a=vceqq_u16(vld1q_u16(reinterpret_cast<const std::uint16_t*>(p)),query);
            auto c=vceqq_u16(vld1q_u16(reinterpret_cast<const std::uint16_t*>(p+16)),query);
            auto d=vceqq_u16(vld1q_u16(reinterpret_cast<const std::uint16_t*>(p+32)),query);
            auto e=vceqq_u16(vld1q_u16(reinterpret_cast<const std::uint16_t*>(p+48)),query);
            matches=ikea::detail::filter::byte_bits(vcombine_u8(vmovn_u16(a),vmovn_u16(c)));
            matches|=word(ikea::detail::filter::byte_bits(vcombine_u8(vmovn_u16(d),vmovn_u16(e))))<<16;
        }else
#endif
        {
            using F=ht::sp::layout<E,ht::sp::geometry::local,0>;
            const ht::sp::dense_source<F> source{p,N};
            const auto expression=ht::sp::author::expression(source);
            ht::each<T>([&](auto tile){matches|=word(ht::sp::author::filter8_unchecked<ikea::comparison::equal>(expression,tile*8,q))<<(tile*8);});
        }
        if constexpr(E==K)if(!q)matches&=~vacancies(p);
        return matches?unsigned(std::countr_zero(matches)):N;
    }
    static unsigned matching_rows(const byte* p,word q) {
        word matches=0;
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr(E==14)matches=ht::detail::exact_small_bucket<E,T>(ikea::to_u8(p),q);
        else if constexpr(E==16)matches=_mm512_cmpeq_epi16_mask(_mm512_loadu_si512(p),_mm512_set1_epi16(short(q)));
        else if constexpr(E==30)matches=ht::detail::exact30<true,false>(ikea::to_u8(p),q);
        else if constexpr(E==32)matches=_mm512_cmpeq_epi32_mask(_mm512_loadu_si512(p),_mm512_set1_epi32(int(q)));
        else
#endif
        {
            // Complete masks avoid retaining the first-body matcher's inner
            // presence/retry branches in the mixed-upsert control. The split
            // path still uses locate() and its first-row optimization.
            using F=ht::sp::layout<E,ht::sp::geometry::local,0>;
            const ht::sp::dense_source<F> source{p,N};const auto expression=ht::sp::author::expression(source);
            ht::each<T>([&](auto tile){matches|=word(ht::sp::author::filter8_unchecked<ikea::comparison::equal>(expression,tile*8,q))<<(tile*8);});
        }
        if constexpr(E==K)if(!q)matches&=~vacancies(p);
        return unsigned(matches);
    }
    static word read_matches(const byte* p,word q) {
        word matches=0;
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr(E==14)matches=ht::detail::exact_small_bucket<E,T>(ikea::to_u8(p),q);
        else if constexpr(E==16)matches=_mm512_cmpeq_epi16_mask(_mm512_loadu_si512(p),_mm512_set1_epi16(short(q)));
        else if constexpr(E==30)matches=ht::detail::exact30<true,false>(ikea::to_u8(p),q);
        else if constexpr(E==32)matches=_mm512_cmpeq_epi32_mask(_mm512_loadu_si512(p),_mm512_set1_epi32(int(q)));
        else
#endif
        {
            // Complete masks avoid retaining the first-body matcher's inner
            // presence/retry branches in the mixed-upsert control. The split
            // path still uses locate() and its first-row optimization.
            using F=ht::sp::layout<E,ht::sp::geometry::local,0>;
            const ht::sp::dense_source<F> source{p,N};const auto expression=ht::sp::author::expression(source);
            ht::each<T>([&](auto tile){matches|=word(ht::sp::author::filter8_unchecked<ikea::comparison::equal>(expression,tile*8,q))<<(tile*8);});
        }
        if constexpr(E==K)if(!q)[[unlikely]]matches&=~vacancies(p);
        return matches;
    }
    template<class Found> [[gnu::always_inline]] static bool read_primary(const byte* p,word q,Found&& found) {
#if defined(__aarch64__)
        const auto row=locate(p,q);if(row==N)return false;found(row);return true;
#else
        {const auto matches=read_matches(p,q);if(!matches)return false;found(unsigned(std::countr_zero(matches)));return true;}
#endif
    }
    static void hide_value(byte* p,unsigned row) {
#if __has_feature(address_sanitizer)
        if constexpr(LiveRows)__asan_poison_memory_region(p+B+row*V,V);
#else
        (void)p;(void)row;
#endif
    }
    static void value(byte* p,unsigned row,const ht::bytes<V>& v) {
#if __has_feature(address_sanitizer)
        if constexpr(LiveRows)__asan_unpoison_memory_region(p+B+row*V,V);
#endif
        if constexpr(V)ht::each<V/8>([&](auto limb){assign<64>(p+B,row*(V/8)+limb,N*(V/8),ht::load<8>(v.data()+limb*8));});
    }
    static void occupy(byte* p,unsigned row,word q,const ht::bytes<V>& v) {
        assign<E>(p,row,N,q);value(p,row,v);
        p[flags+row/8]&=byte(~(1u<<(row%8)));
    }
    struct swiss_position {std::size_t slot;bool present;};
    // Readers have no tombstone/destination bookkeeping. Keep the cold tier
    // outside the primary consumer's live register set, as in the maintained
    // owner. Both paths read this owner's actual mutable allocations.
    [[gnu::noinline,gnu::pure]] std::size_t find_swiss(word key,word swiss)const {
        if(!groups_)return absent;
        auto group=swiss&(groups_-1);std::size_t step=0;
        for(;;){const auto* control=controls_.data+group*16;
            auto candidates=write_arch::equal_controls(control,hash_result{0,0,swiss}.fingerprint());
            while(candidates){const auto lane=std::countr_zero(candidates);candidates&=candidates-1;
                const auto slot=group*16+lane;
                if(write_arch::tuple_field<key_bytes>::read(slots_.data+slot*tuple_bytes)==key)return slot;
            }
            if(write_arch::equal_controls(control,empty))return absent;
            group=(group+(++step))&(groups_-1);
        }
    }
    // One traversal finds either the existing key or an admissible empty/tomb
    // destination. A tombstone is reusable only after absence is established.
    swiss_position resolve_swiss(word key,const hash_result& h)const {
        if(!groups_)return {absent,false};
        auto group=h.swiss&(groups_-1);std::size_t step=0,available=absent;
        for(;;){const auto* control=controls_.data+group*16;
            auto candidates=write_arch::equal_controls(control,h.fingerprint());
            while(candidates){const auto lane=std::countr_zero(candidates);candidates&=candidates-1;
                const auto slot=group*16+lane;
                if(write_arch::tuple_field<key_bytes>::read(slots_.data+slot*tuple_bytes)==key)return {slot,true};
            }
            const auto empties=write_arch::equal_controls(control,empty);
            if(available==absent){const auto free=empties|write_arch::equal_controls(control,tombstone);
                if(free)available=group*16+std::countr_zero(free);}
            if(empties)return {available,false};
            group=(group+(++step))&(groups_-1);
        }
    }
    void store_swiss(std::size_t slot,word key,const ht::bytes<V>& v,const hash_result& h) {
        deleted_-=controls_.data[slot]==tombstone;
        ht::store<key_bytes>(slots_.data+slot*tuple_bytes,key);
        if constexpr(V)std::memcpy(slots_.data+slot*tuple_bytes+key_bytes,v.data(),V);
        controls_.data[slot]=byte(h.fingerprint());++spill_;
    }
    [[gnu::noinline]] void grow_swiss() {
        const auto next=groups_?groups_*2:1;
        buffer controls(next*16),slots(next*16*tuple_bytes);
        std::memset(controls.data,number(empty),next*16);
        auto old_controls=std::move(controls_);auto old_slots=std::move(slots_);const auto old_groups=groups_;
        controls_=std::move(controls);slots_=std::move(slots);groups_=next;spill_=deleted_=0;
        for(std::size_t i=0;i<old_groups*16;++i)if(write_arch::occupied_control(old_controls.data[i])){
            const auto key=ht::load<key_bytes>(old_slots.data+i*tuple_bytes);const auto h=hash_(key);
            ht::bytes<V> v{};if constexpr(V)std::memcpy(v.data(),old_slots.data+i*tuple_bytes+key_bytes,V);
            store_swiss(resolve_swiss(key,h).slot,key,v,h);
        }
    }
    [[gnu::noinline]] bool insert_slow(word key,const ht::bytes<V>& v,const hash_result& h,word free) {
        auto* p=bucket(h.bucket);auto found=(p[B-1]!=byte{0})?resolve_swiss(key,h):swiss_position{absent,false};
        if(found.present){if constexpr(V)std::memcpy(slots_.data+found.slot*tuple_bytes+key_bytes,v.data(),V);return false;}
        if(free)occupy(p,unsigned(std::countr_zero(free)),h.quotient,v);
        else {
            if(!groups_||(spill_+deleted_+1)*8>groups_*16*7){grow_swiss();found.slot=absent;}
            if(found.slot==absent)found=resolve_swiss(key,h);
            store_swiss(found.slot,key,v,h);if(p[B-1]!=byte{255})p[B-1]=byte(number(p[B-1])+1);
        }
        ++size_;return true;
    }
    template<class Edit> [[gnu::noinline]] bool edit_slow(word key,const hash_result& h,word free,Edit&& edit) {
        auto* p=bucket(h.bucket);auto found=(p[B-1]!=byte{0})?resolve_swiss(key,h):swiss_position{absent,false};
        if(found.present){edit(slots_.data+found.slot*tuple_bytes+key_bytes,true);return false;}
        if(free){const auto row=unsigned(std::countr_zero(free));edit(p+B+row*V,false);
            assign<E>(p,row,N,h.quotient);p[flags+row/8]&=byte(~(1u<<(row%8)));
        }else {
            if(!groups_||(spill_+deleted_+1)*8>groups_*16*7){grow_swiss();found.slot=absent;}
            if(found.slot==absent)found=resolve_swiss(key,h);
            edit(slots_.data+found.slot*tuple_bytes+key_bytes,false);
            deleted_-=controls_.data[found.slot]==tombstone;
            ht::store<key_bytes>(slots_.data+found.slot*tuple_bytes,key);
            controls_.data[found.slot]=byte(h.fingerprint());++spill_;if(p[B-1]!=byte{255})p[B-1]=byte(number(p[B-1])+1);
        }
        ++size_;return true;
    }
  public:
    static constexpr unsigned N=8*T;
    static constexpr std::size_t bucket_count=std::size_t{1}<<b;
    static constexpr unsigned bucket_bytes=B;
    using value_type=ht::bytes<V>;
    struct probe {hash_result hash;const byte* primary;};
    struct staged_probe {probe first;word key;std::size_t group=0,step=0;unsigned candidates=0,slot=0;std::uint8_t phase=0;bool empty_group=false;};
    // LiveRows is an owner policy for point consumers: keys and all probed
    // control/padding bytes remain initialized, but absent value rows do not.
    // It cannot use duplicate_buffer or an unmasked group value reader. Swiss
    // allocation is unchanged so this control isolates primary initialization.
    explicit standalone_table(word seed):primary_(bucket_count*stride,nullptr,!LiveRows),hash_{seed&ht::mask<W>} {
        for(std::size_t i=0;i<bucket_count;++i){auto* p=bucket(i);
            if constexpr(LiveRows)std::memset(p,0,B);
            if constexpr(E>K)std::memset(p,255,T*E);
            std::memset(p+flags,255,T);
            if constexpr(LiveRows)for(unsigned row=0;row<N;++row)hide_value(p,row);
        }
    }
    std::size_t size()const{return size_;}
    std::size_t spill_size()const{return spill_;}
    std::size_t bytes_used()const{return sizeof(*this)+primary_.capacity()+controls_.capacity()+slots_.capacity();}
    // The callback has one stable, exclusively owned value cell. It must not
    // reenter/mutate this index, escape the pointer, or suspend. It performs
    // fallible preparation before any visible value stores; an absent key is
    // published only after the callback returns. Swiss rehash precedes binding.
    template<class Edit> [[gnu::always_inline]] bool edit_cell(word key,Edit&& edit) {
        static_assert(V>0&&!LiveRows);ht::require(!(key&~ht::mask<W>),"mutable cell key domain");
        const auto h=hash_(key);auto* p=bucket(h.bucket);const auto row=locate(p,h.quotient);
        if(row!=N){edit(p+B+row*V,true);return false;}
        const auto free=vacancies(p);
        if(free&&p[B-1]==byte{0}){const auto at=unsigned(std::countr_zero(free));edit(p+B+at*V,false);
            assign<E>(p,at,N,h.quotient);p[flags+at/8]&=byte(~(1u<<(at%8)));++size_;return true;}
        return edit_slow(key,h,free,std::forward<Edit>(edit));
    }
    template<class Retire> bool erase_cell(word key,Retire&& retire) {
        static_assert(!LiveRows);
        const auto h=hash_(key);auto* p=bucket(h.bucket);const auto row=locate(p,h.quotient);
        if(row!=N){retire(p+B+row*V);assign<E>(p,row,N,E>K?ht::mask<E>:0);
            p[flags+row/8]|=byte(1u<<(row%8));--size_;return true;}
        if(p[B-1]!=byte{0}){const auto found=resolve_swiss(key,h);if(found.present){
            retire(slots_.data+found.slot*tuple_bytes+key_bytes);
            controls_.data[found.slot]=tombstone;--spill_;++deleted_;--size_;if(p[B-1]!=byte{255})p[B-1]=byte(number(p[B-1])-1);return true;}}
        return false;
    }
    [[gnu::always_inline]] bool insert(word key,const value_type& v={}) {
        ht::require(!(key&~ht::mask<W>),"write architecture key domain");
        const auto h=hash_(key);auto* p=bucket(h.bucket);
        if constexpr(Flow!=upsert_flow::split){
            const auto matching=matching_rows(p,h.quotient);
            const auto free=vacancies(p);const bool fresh=!matching;
            // An existing primary row wins over every vacancy. An absent key
            // needs a Swiss absence proof whenever this bucket has overflow,
            // even if erasure has since left a primary hole.
            const auto admissible=free&(word{0}-word(p[B-1]==byte{0}));
            const auto candidates=select_candidates(fresh,matching,unsigned(admissible));
            if(!candidates)[[unlikely]]return insert_slow(key,v,h,free);
            const auto destination=unsigned(std::countr_zero(candidates));
            if constexpr(Flow==upsert_flow::shared_stores){
                // Explicit control: idempotent key/flag stores on replacement
                // are charged, including residual-plane read/modify/write.
                occupy(p,destination,h.quotient,v);size_+=fresh;
            }else {
                if(fresh){assign<E>(p,destination,N,h.quotient);
                    p[flags+destination/8]&=byte(~(1u<<(destination%8)));++size_;}
                value(p,destination,v);
            }
            return fresh;
        }
        const auto row=locate(p,h.quotient);
        if(row!=N){value(p,row,v);return false;}
        const auto free=vacancies(p);
        if(free&&p[B-1]==byte{0}){occupy(p,unsigned(std::countr_zero(free)),h.quotient,v);++size_;return true;}
        return insert_slow(key,v,h,free);
    }
    bool erase(word key) {
        ht::require(!(key&~ht::mask<W>),"write architecture key domain");
        const auto h=hash_(key);auto* p=bucket(h.bucket);const auto row=locate(p,h.quotient);
        if(row!=N){assign<E>(p,row,N,E>K?ht::mask<E>:0);p[flags+row/8]|=byte(1u<<(row%8));hide_value(p,row);--size_;return true;}
        if(p[B-1]!=byte{0}){const auto found=resolve_swiss(key,h);if(found.present){controls_.data[found.slot]=tombstone;--spill_;++deleted_;--size_;if(p[B-1]!=byte{255})p[B-1]=byte(number(p[B-1])-1);return true;}}
        return false;
    }
    template<bool Primary> struct const_cell {
        const byte* source;unsigned row;
        [[gnu::always_inline]] const byte* address()const {
            if constexpr(Primary)return source+std::size_t(row)*V;
            else return source;
        }
        template<unsigned Limb> [[gnu::always_inline]] word read()const {
            static_assert(Limb<V/8);
            if constexpr(Primary)return write_arch::direct_point<64>(source,row*(V/8)+Limb,N*(V/8));
            else return write_arch::tuple_field<8>::read(source+Limb*8);
        }
        template<unsigned Bytes,unsigned First> [[gnu::always_inline]] ht::bytes<Bytes> value()const {
            static_assert(Bytes%8==0&&First+Bytes/8<=V/8);ht::bytes<Bytes> result;
            ht::each<Bytes/8>([&](auto limb){ht::store<8>(result.data()+limb*8,read<First+limb>());});return result;
        }
    };
    // Preserve the physical SeriesPack/TuplePack source and defer value loads
    // until the consumer knows which fields are needed. External multimap cells
    // must not materialize the entire inline-value area just to read a reference.
    [[gnu::always_inline]] probe prepare(word key)const {const auto h=hash_(key);return {h,bucket(h.bucket)};}
    template<bool Hint=true> [[gnu::always_inline]] void prefetch(const probe& p)const {
        if constexpr(Hint)__builtin_prefetch(p.primary,0,3);
    }
    template<class Consume> [[gnu::always_inline]] bool finish_cell(const probe& p,word key,Consume&& consume)const {
        if(read_primary(p.primary,p.hash.quotient,[&](unsigned row) __attribute__((always_inline)) {
            if constexpr(V)consume(const_cell<true>{p.primary+B,row});
        }))return true;
        if(p.primary[B-1]==byte{0})return false;const auto found=find_swiss(key,p.hash.swiss);if(found==absent)return false;
        if constexpr(V)consume(const_cell<false>{slots_.data+found*tuple_bytes+key_bytes,0});return true;
    }
    template<class Consume> [[gnu::always_inline]] bool find_cell(word key,Consume&& consume)const {
        static_assert(V>0);return finish_cell(prepare(key),key,std::forward<Consume>(consume));
    }
    template<class Consume> [[gnu::always_inline]] bool finish(const probe& p,word key,Consume&& consume)const {
        return finish_cell(p,key,[&](auto cell) __attribute__((always_inline)) {consume(cell.template value<V,0>());});
    }
    template<class Consume> [[gnu::always_inline]] bool find(word key,Consume&& consume)const {
        return finish(prepare(key),key,std::forward<Consume>(consume));
    }
    // Borrows expire on any mutation. Scheduling, hint tier and the owner's
    // stable lifetime belong to the caller, as with the maintained reader.
    template<ht::staged_values Values=ht::staged_values::dependent,class Emit>
    void prepare_staged(staged_probe& state,word key,Emit&& emit)const {
        state={prepare(key),key};emit(state.first.primary,B);
        if constexpr(Values==ht::staged_values::lookahead8&&V==8)
            ht::each<N/8>([&](auto line){emit(state.first.primary+B+line*64,64);});
    }
    template<ht::staged_values Values=ht::staged_values::dependent,unsigned FetchBytes=V,class Consume,class Emit>
    bool advance_cell(staged_probe& s,bool& found,Consume&& consume,Emit&& emit)const {
        static_assert(FetchBytes<=V);
        const auto* primary=s.first.primary;
        if(s.phase==0){const auto hit=read_primary(primary,s.first.hash.quotient,[&](unsigned row){s.slot=row;});
            if(hit){const auto slot=s.slot;
                if constexpr(V){
                    if constexpr(Values==ht::staged_values::lookahead8&&V==8){
                        consume(const_cell<true>{primary+B,slot});found=true;return true;
                    }
                    emit(primary+B+std::size_t(slot)*V,FetchBytes);s.phase=3;return false;
                }else {found=true;return true;}
            }
            if(primary[B-1]==byte{0}){found=false;return true;}
            s.group=s.first.hash.swiss&(groups_-1);s.phase=1;emit(controls_.data+s.group*16,16);return false;
        }
        if(s.phase==3){if constexpr(V)consume(const_cell<true>{primary+B,s.slot});found=true;return true;}
        if(s.phase==1){const auto* control=controls_.data+s.group*16;
            s.candidates=write_arch::equal_controls(control,s.first.hash.fingerprint());s.empty_group=write_arch::equal_controls(control,empty)!=0;
        }else {
            const auto slot=s.group*16+std::countr_zero(s.candidates);s.candidates&=s.candidates-1;
            const auto* tuple=slots_.data+slot*tuple_bytes;
            if(write_arch::tuple_field<key_bytes>::read(tuple)==s.key){
                if constexpr(V)consume(const_cell<false>{tuple+key_bytes,0});found=true;return true;
            }
        }
        if(s.candidates){s.phase=2;emit(slots_.data+(s.group*16+std::countr_zero(s.candidates))*tuple_bytes,key_bytes+FetchBytes);return false;}
        if(s.empty_group){found=false;return true;}
        s.group=(s.group+(++s.step))&(groups_-1);s.phase=1;emit(controls_.data+s.group*16,16);return false;
    }
    template<ht::staged_values Values=ht::staged_values::dependent,class Consume,class Emit>
    bool advance(staged_probe& s,bool& found,Consume&& consume,Emit&& emit)const {
        return advance_cell<Values>(s,found,[&](auto cell) __attribute__((always_inline)) {consume(cell.template value<V,0>());},emit);
    }
    // Validation/ownership census, outside timed operations. Bind the actual
    // live cell, without interpreting its outer owner's payload representation.
    template<class Visit> void visit_cells(Visit&& visit)const {
        static_assert(V>0);std::size_t count=0;
        for(std::size_t id=0;id<bucket_count;++id){const auto* p=bucket(id);
            for(unsigned row=0;row<N;++row)if(!(number(p[flags+row/8])&(1u<<(row%8)))){
                visit(hash_.recover(id,write_arch::point<E>(p,row,N)),p+B+row*V);++count;
            }
        }
        for(std::size_t slot=0;slot<groups_*16;++slot)if(write_arch::occupied_control(controls_.data[slot])){
            const auto* p=slots_.data+slot*tuple_bytes;visit(ht::load<key_bytes>(p),p+key_bytes);++count;
        }
        ht::require(count==size_,"mutable cell census");
    }
};
}

#include "reused-table.h"
namespace write_arch {
template<unsigned W,unsigned b,unsigned V,bool WholeBytes,bool LiveRows=false,
    upsert_flow Flow=upsert_flow::split,unsigned ProfileV=V,bool ProfileMulti=false>
using table=std::conditional_t<!WholeBytes&&!LiveRows,reused_table<W,b,V,Flow,ProfileV,ProfileMulti>,
    standalone_table<W,b,V,WholeBytes,LiveRows,Flow>>;
}

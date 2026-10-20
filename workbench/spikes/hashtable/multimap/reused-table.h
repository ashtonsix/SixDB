#pragma once
#include <ikea/hashtable/author/table.h>
#include "read-profile.h"
#ifndef WRITE_ARCH_MASK_FIRST_CELL
#define WRITE_ARCH_MASK_FIRST_CELL (SIXDB_TUNE_ZEN5 || SIXDB_TUNE_GRANITE_RAPIDS)
#endif
#ifndef WRITE_ARCH_DEFER_CELL_HASH
#define WRITE_ARCH_DEFER_CELL_HASH (SIXDB_TUNE_ZEN5 || SIXDB_TUNE_GRANITE_RAPIDS || SIXDB_TUNE_NEOVERSE_V2)
#endif

namespace write_arch {
// New private write kernels on the actual maintained owner. No added members,
// read wrappers, alternate read allocation or conversion. All ordinary direct
// and staged set/map reads are inherited unchanged. The selective cell methods
// below exist only for the genuinely different multimap payload representation.
template<unsigned W,unsigned b,unsigned V,upsert_flow Flow,unsigned ProfileV,bool ProfileMulti,
    class Reader=ht::table<W,b,V,read_profile<W-b,ProfileV,ProfileMulti>().width,
        read_profile<W-b,ProfileV,ProfileMulti>().kernel>>
class reused_table : public Reader {
    friend struct guarded_index_access;
    using Base=Reader;
    using staged_phase=typename Base::staged_phase;
    static_assert(std::is_same_v<typename Base::mix_type,ht::permutation<W,W-b>>);
    static_assert(std::is_same_v<typename Base::value_type,ht::bytes<V>>);
    static_assert(std::is_same_v<typename Base::storage_policy,ht::heap_storage_policy>);
    static constexpr bool LiveRows=false;
    [[gnu::always_inline]] auto state(){return ht::author::table_access::get(static_cast<Base&>(*this));}
    [[gnu::always_inline]] auto state()const{return ht::author::table_access::get(static_cast<const Base&>(*this));}
    static constexpr unsigned K=Base::K,T=Base::T;
    static_assert(V<=128);
    static_assert(!LiveRows||V==8||V==64);
    static constexpr unsigned E=Base::encoding,flags=Base::flags_offset,B=Base::B;
    static constexpr auto stride=Base::stride;
    static constexpr unsigned key_bytes=Base::key_bytes,tuple_bytes=Base::tuple_bytes;
    // These inherited entry points would run the old writer or return a sliced
    // base owner. They are outside this private write experiment's interface.
    using Base::clone;
    using Base::prepare_absent;
    using Base::vacancy_for_absent;
    static constexpr std::size_t absent=~std::size_t{0};
    static constexpr byte empty=ht::detail::control_empty,tombstone=ht::detail::control_tombstone;
    using buffer=ht::heap_storage_policy::buffer;
    using hash_type=ht::permutation<W,K>;
    using hash_result=typename hash_type::result;
    byte* bucket(word id){return state().primary.data+id*stride;}
    const byte* bucket(word id)const{return state().primary.data+id*stride;}
    static word vacancies(const byte* p){return ht::load<T>(p+flags);}
    static unsigned locate(const byte* p,word q) {
        if constexpr(Base::bitmap)return ht::author::table_access::primary_find<Base>(p,q);
        else {
        word matches=0;
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr(E==14)matches=ht::detail::exact_small_bucket<E,T>(p,q);
        else if constexpr(E==16)matches=_mm512_cmpeq_epi16_mask(_mm512_loadu_si512(p),_mm512_set1_epi16(short(q)))&ht::mask<Base::N>;
        else if constexpr(E==30)matches=ht::detail::exact30<true,false>(p,q);
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
            const auto bodies=ht::detail::small_body_comparisons_neon<E>(p,q);
            if(q){const auto first=ht::detail::first_small_body_neon(bodies);if(first==255)return N;
                if(exact(first))return first;
                matches=ht::detail::small_body_mask_neon(bodies);matches&=matches-1;
            }else matches=ht::detail::small_body_mask_neon(bodies)&~vacancies(p);
            while(matches){const auto row=unsigned(std::countr_zero(matches));if(exact(row))return row;matches&=matches-1;}
            return N;
        }else if constexpr(E==30){
            // Maintained NEON reader: pick a body row, then check its six
            // residual bits. Shared upserts retain their complete-mask path.
            auto candidates=ht::detail::body30_neon_nibbles(p,q);
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
            matches&=ht::mask<Base::N>;
        }else
#endif
        {
            using F=ht::sp::format<E,ht::sp::geometry::local,0>;
            const ht::sp::dense_source<F> source{p,N};
            const auto expression=ht::sp::composition::describe(source);
            ht::each<T>([&](auto tile){matches|=word(ht::sp::composition::filter8_unchecked<ikea::comparison::equal>(expression,tile*8,q))<<(tile*8);});
        }
        if constexpr(E==K)if(!q)matches&=~vacancies(p);
        return matches?unsigned(std::countr_zero(matches)):N;
        }
    }
    using row_mask=std::conditional_t<(Base::N<=32),unsigned,word>;
    static row_mask matching_rows(const byte* p,word q) {
        word matches=0;
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr(E==14)matches=ht::detail::exact_small_bucket<E,T>(p,q);
        else if constexpr(E==16)matches=_mm512_cmpeq_epi16_mask(_mm512_loadu_si512(p),_mm512_set1_epi16(short(q)))&ht::mask<Base::N>;
        else if constexpr(E==30)matches=ht::detail::exact30<true,false>(p,q);
        else if constexpr(E==32)matches=_mm512_cmpeq_epi32_mask(_mm512_loadu_si512(p),_mm512_set1_epi32(int(q)));
        else
#endif
        {
            // Complete masks avoid retaining the first-body matcher's inner
            // presence/retry branches in the mixed-upsert control. The split
            // path still uses locate() and its first-row optimization.
            using F=ht::sp::format<E,ht::sp::geometry::local,0>;
            const ht::sp::dense_source<F> source{p,N};const auto expression=ht::sp::composition::describe(source);
            ht::each<T>([&](auto tile){matches|=word(ht::sp::composition::filter8_unchecked<ikea::comparison::equal>(expression,tile*8,q))<<(tile*8);});
        }
        if constexpr(E==K)if(!q)matches&=~vacancies(p);
        return row_mask(matches);
    }
    // Selective MM cells reuse the maintained key matcher. Ordinary set/map
    // reads inherit Base::finish and its value-returning overflow boundary.
    template<class Found> [[gnu::always_inline]] static bool read_primary(const byte* p,word q,Found&& found) {
        constexpr auto kernel=read_profile<W-b,ProfileV,ProfileMulti>().kernel;
        if constexpr(Base::bitmap||kernel==ht::search_kernel::neon64||kernel==ht::search_kernel::neon30||kernel==ht::search_kernel::body_small_neon){
            const auto row=ht::author::table_access::primary_find<Base>(p,q);
            if(row==N)return false;found(row);return true;
        }else {
            const auto matches=ht::author::table_access::primary_matches<Base>(p,q);
            if(!matches)return false;found(unsigned(std::countr_zero(matches)));return true;
        }
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
        if constexpr(V%8!=0)ht::author::table_access::write_value<Base>(p,row,v);
        else if constexpr(V)ht::each<V/8>([&](auto limb){assign<64>(p+B,row*(V/8)+limb,Base::value_rows*(V/8),ht::load<8>(v.data()+limb*8));});
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
        if(!state().groups)return absent;
        auto group=swiss&(state().groups-1);std::size_t step=0;
        for(;;){const auto* control=state().controls.data+group*16;
            auto candidates=ht::equal_controls(control,hash_result{0,0,swiss}.fingerprint());
            while(candidates){const auto lane=std::countr_zero(candidates);candidates&=candidates-1;
                const auto slot=group*16+lane;
                if(ht::tuple_field<key_bytes>::read(state().slots.data+slot*tuple_bytes)==key)return slot;
            }
            if(ht::equal_controls(control,empty))return absent;
            group=(group+(++step))&(state().groups-1);
        }
    }
    // One traversal finds either the existing key or an admissible empty/tomb
    // destination. A tombstone is reusable only after absence is established.
    swiss_position resolve_swiss(word key,const hash_result& h)const {
        if(!state().groups)return {absent,false};
        auto group=h.swiss&(state().groups-1);std::size_t step=0,available=absent;
        for(;;){const auto* control=state().controls.data+group*16;
            auto candidates=ht::equal_controls(control,h.fingerprint());
            while(candidates){const auto lane=std::countr_zero(candidates);candidates&=candidates-1;
                const auto slot=group*16+lane;
                if(ht::tuple_field<key_bytes>::read(state().slots.data+slot*tuple_bytes)==key)return {slot,true};
            }
            const auto empties=ht::equal_controls(control,empty);
            if(available==absent){const auto free=empties|ht::equal_controls(control,tombstone);
                if(free)available=group*16+std::countr_zero(free);}
            if(empties)return {available,false};
            group=(group+(++step))&(state().groups-1);
        }
    }
    void store_swiss(std::size_t slot,word key,const ht::bytes<V>& v,const hash_result& h) {
        state().deleted-=state().controls.data[slot]==tombstone;
        ht::store<key_bytes>(state().slots.data+slot*tuple_bytes,key);
        if constexpr(V)std::memcpy(state().slots.data+slot*tuple_bytes+key_bytes,v.data(),V);
        state().controls.data[slot]=h.fingerprint();++state().spill;
    }
    [[gnu::noinline]] void grow_swiss() {
        const auto next=state().groups?state().groups*2:1;
        buffer controls(next*16),slots(next*16*tuple_bytes);
        std::memset(controls.data,empty,next*16);
        auto old_controls=std::move(state().controls);auto old_slots=std::move(state().slots);const auto old_groups=state().groups;
        state().controls=std::move(controls);state().slots=std::move(slots);state().groups=next;state().spill=state().deleted=0;
        for(std::size_t i=0;i<old_groups*16;++i)if(ht::detail::occupied_control(old_controls.data[i])){
            const auto key=ht::load<key_bytes>(old_slots.data+i*tuple_bytes);const auto h=state().mix(key);
            ht::bytes<V> v{};if constexpr(V)std::memcpy(v.data(),old_slots.data+i*tuple_bytes+key_bytes,V);
            store_swiss(resolve_swiss(key,h).slot,key,v,h);
        }
    }
    [[gnu::noinline]] bool insert_slow(word key,const ht::bytes<V>& v,const hash_result& h,word free) {
        auto* p=bucket(h.bucket);auto found=p[B-1]?resolve_swiss(key,h):swiss_position{absent,false};
        if(found.present){if constexpr(V)std::memcpy(state().slots.data+found.slot*tuple_bytes+key_bytes,v.data(),V);return false;}
        if(free)occupy(p,unsigned(std::countr_zero(free)),h.quotient,v);
        else {
            if(!state().groups||(state().spill+state().deleted+1)*8>state().groups*16*7){grow_swiss();found.slot=absent;}
            if(found.slot==absent)found=resolve_swiss(key,h);
            store_swiss(found.slot,key,v,h);if(p[B-1]!=255)++p[B-1];
        }
        ++state().size;return true;
    }
#if WRITE_ARCH_DEFER_CELL_HASH
    template<class Edit> [[gnu::noinline]] bool edit_slow(word key,word free,Edit&& edit) {
        const auto h=state().mix(key);
#else
    template<class Edit> [[gnu::noinline]] bool edit_slow(word key,const hash_result& h,word free,Edit&& edit) {
#endif
        auto* p=bucket(h.bucket);auto found=p[B-1]?resolve_swiss(key,h):swiss_position{absent,false};
        if(found.present){edit(state().slots.data+found.slot*tuple_bytes+key_bytes,true);return false;}
        if(free){const auto row=unsigned(std::countr_zero(free));edit(p+B+row*V,false);
            assign<E>(p,row,N,h.quotient);p[flags+row/8]&=byte(~(1u<<(row%8)));
        }else {
            if(!state().groups||(state().spill+state().deleted+1)*8>state().groups*16*7){grow_swiss();found.slot=absent;}
            if(found.slot==absent)found=resolve_swiss(key,h);
            edit(state().slots.data+found.slot*tuple_bytes+key_bytes,false);
            state().deleted-=state().controls.data[found.slot]==tombstone;
            ht::store<key_bytes>(state().slots.data+found.slot*tuple_bytes,key);
            state().controls.data[found.slot]=h.fingerprint();++state().spill;if(p[B-1]!=255)++p[B-1];
        }
        ++state().size;return true;
    }
    // Synchronous guarded writes reuse the maintained source/grant contract.
    // Discovery never allocates. All damage is admitted before any store; an
    // exhausted Swiss tier asks the caller to prepare capacity and re-admit.
    // No route/head escapes this call and no reader implementation changes.
    template<class Context> ht::mutation_status guarded_mutate(word key,const ht::bytes<V>* incoming,Context& context) {
        using ht::mutation_status;using ht::owner_part;
        if((key&~ht::mask<W>)||!context.enter(*this,key,ht::access_intent::write))return mutation_status::denied;
        // The input may refer to this owner. Preserve it before preparation or
        // idempotent packed-key writes can change any of its bytes.
        ht::bytes<V> snapshot{};if(incoming)snapshot=*incoming;
        const auto h=state().mix(key);auto* p=bucket(h.bucket);
        auto row=[&]{
            if constexpr(Base::bitmap||Flow==upsert_flow::split)return locate(p,h.quotient);
            else {const auto mask=matching_rows(p,h.quotient);return mask?unsigned(std::countr_zero(mask)):N;}
        }();
        auto swiss=swiss_position{absent,false};
        if constexpr(!Base::bitmap)if(row==N&&p[B-1])swiss=resolve_swiss(key,h);
        const bool existing=row!=N||swiss.present;
        if(!incoming&&!existing)return mutation_status::absent;
        if constexpr(!V)if(incoming&&existing&&
            (Base::bitmap||Flow!=upsert_flow::shared_stores||row==N))return mutation_status::unchanged;
        if(incoming&&!existing){
            if constexpr(Base::bitmap)row=unsigned(h.quotient);
            else {
                const auto free=vacancies(p);if(free)row=unsigned(std::countr_zero(free));
                if(row==N){
                    if(!state().groups||(state().spill+state().deleted+1)*8>state().groups*16*7)
                        return mutation_status::needs_capacity;
                    if(swiss.slot==absent)swiss=resolve_swiss(key,h);
                }
            }
        }
        std::array<ht::mutation_damage,4> damage{};unsigned count=0;
        auto add=[&](owner_part part,const byte* base,std::size_t extent,std::size_t offset,std::size_t bytes){
            damage[count++]={{this,part,base,extent},{0,offset,bytes}};
        };
        if(row!=N)add(owner_part::primary,state().primary.data,state().primary.size,h.bucket*stride,stride);
        else {
            if(!existing||!incoming){
                add(owner_part::swiss_controls,state().controls.data,state().controls.size,swiss.slot,1);
                if(p[B-1]!=255)add(owner_part::primary,state().primary.data,state().primary.size,h.bucket*stride+B-1,1);
            }
            if(incoming)add(owner_part::swiss_tuples,state().slots.data,state().slots.size,
                swiss.slot*tuple_bytes+(existing?key_bytes:0),existing?V:tuple_bytes);
        }
        constexpr bool shared=Flow==upsert_flow::shared_stores&&!Base::bitmap;
        if(!existing||!incoming||(shared&&row!=N))
            add(owner_part::header,reinterpret_cast<const byte*>(this),sizeof(*this),0,sizeof(*this));
        const auto writes=std::span<const ht::mutation_damage>(damage.data(),count);
        if(!context.prepare(writes))return mutation_status::denied;
        static_assert(noexcept(context.before(damage[0].source,damage[0].span)));
        for(const auto& d:writes)context.before(d.source,d.span);
        // Only owner-proven native stores follow; callbacks cannot invalidate
        // this operation's storage or reenter it under the Context contract.
        if(row!=N){
            if(incoming){
                if constexpr(Base::bitmap){
                    value(p,row,snapshot);
                    if(!existing){p[row/8]|=byte(1u<<(row%8));++state().size;}
                }else if constexpr(shared){occupy(p,row,h.quotient,snapshot);state().size+=!existing;}
                else {
                    if(!existing){assign<E>(p,row,N,h.quotient);p[flags+row/8]&=byte(~(1u<<(row%8)));++state().size;}
                    value(p,row,snapshot);
                }
            }else {
                if constexpr(Base::bitmap)p[row/8]&=byte(~(1u<<(row%8)));
                else {assign<E>(p,row,N,E>K?ht::mask<E>:0);p[flags+row/8]|=byte(1u<<(row%8));}
                --state().size;
            }
        }else if(incoming){
            if(existing){if constexpr(V)std::memcpy(state().slots.data+swiss.slot*tuple_bytes+key_bytes,snapshot.data(),V);}
            else {store_swiss(swiss.slot,key,snapshot,h);++state().size;if(p[B-1]!=255)++p[B-1];}
        }else {
            state().controls.data[swiss.slot]=tombstone;--state().spill;++state().deleted;--state().size;
            if(p[B-1]!=255)--p[B-1];
        }
        return !incoming?mutation_status::erased:existing?(V?mutation_status::updated:mutation_status::unchanged):mutation_status::inserted;
    }
  public:
    static constexpr unsigned N=Base::N;
    static constexpr std::size_t bucket_count=Base::bucket_count;
    static constexpr unsigned bucket_bytes=B;
    using value_type=ht::bytes<V>;
    using probe=typename Base::probe;
    using staged_probe=typename Base::staged_probe;
    using reader_owner=Base;
    using Base::find;
    using Base::finish;
    using Base::prepare;
    using Base::prefetch;
    using Base::prepare_staged;
    using Base::advance;
    explicit reused_table(word seed):Base(seed) {
        static_assert(Base::N==N&&Base::B==B&&Base::encoding==E&&Base::stride==stride);
    }
    explicit reused_table(Base&& owner):Base(std::move(owner)){}
    template<class Context> ht::mutation_status guarded_insert(word key,const value_type& value,Context& context) {
        return guarded_mutate(key,&value,context);
    }
    template<class Context> ht::mutation_status guarded_erase(word key,Context& context) {
        return guarded_mutate(key,nullptr,context);
    }
    std::size_t size()const{return state().size;}
    std::size_t spill_size()const{return state().spill;}
    std::size_t bytes_used()const{return sizeof(*this)+state().primary.capacity()+state().controls.capacity()+state().slots.capacity();}
    // The callback has one stable, exclusively owned value cell. It must not
    // reenter/mutate this index, escape the pointer, or suspend. It performs
    // fallible preparation before any visible value stores; an absent key is
    // published only after the callback returns. Swiss rehash precedes binding.
    template<class Edit> [[gnu::always_inline]] bool edit_cell(word key,Edit&& edit) {
        static_assert(V>0&&V%8==0&&!LiveRows);ht::require(!(key&~ht::mask<W>),"mutable cell key domain");
        if constexpr(Base::bitmap){
            const auto h=state().mix(key);auto* p=bucket(h.bucket);const auto bit=byte(1u<<(h.quotient%8));
            const bool present=p[h.quotient/8]&bit;
            edit(p+B+h.quotient*V,present);p[h.quotient/8]|=bit;state().size+=!present;return !present;
        }else {
        const auto h=state().mix(key);auto* p=bucket(h.bucket);
#if WRITE_ARCH_MASK_FIRST_CELL && defined(__x86_64__)
        const auto matches=matching_rows(p,h.quotient);
        if(matches){const auto row=unsigned(std::countr_zero(matches));edit(p+B+row*V,true);return false;}
#else
        const auto row=locate(p,h.quotient);
        if(row!=N){edit(p+B+row*V,true);return false;}
#endif
        const auto free=vacancies(p);
        if(free&&!p[B-1]){const auto at=unsigned(std::countr_zero(free));edit(p+B+at*V,false);
            assign<E>(p,at,N,h.quotient);p[flags+at/8]&=byte(~(1u<<(at%8)));++state().size;return true;}
#if WRITE_ARCH_DEFER_CELL_HASH
        return edit_slow(key,free,std::forward<Edit>(edit));
#else
        return edit_slow(key,h,free,std::forward<Edit>(edit));
#endif
        }
    }
    template<class Retire> bool erase_cell(word key,Retire&& retire) {
        static_assert(V%8==0&&!LiveRows);
        ht::require(!(key&~ht::mask<W>),"erase cell key domain");
        if constexpr(Base::bitmap){
            const auto h=state().mix(key);auto* p=bucket(h.bucket);const auto bit=byte(1u<<(h.quotient%8));
            if(!(p[h.quotient/8]&bit))return false;
            retire(p+B+h.quotient*V);p[h.quotient/8]&=byte(~bit);--state().size;return true;
        }else {
        const auto h=state().mix(key);auto* p=bucket(h.bucket);const auto row=locate(p,h.quotient);
        if(row!=N){retire(p+B+row*V);assign<E>(p,row,N,E>K?ht::mask<E>:0);
            p[flags+row/8]|=byte(1u<<(row%8));--state().size;return true;}
        if(p[B-1]){const auto found=resolve_swiss(key,h);if(found.present){
            retire(state().slots.data+found.slot*tuple_bytes+key_bytes);
            state().controls.data[found.slot]=tombstone;--state().spill;++state().deleted;--state().size;if(p[B-1]!=255)--p[B-1];return true;}}
        return false;
        }
    }
    [[gnu::always_inline]] bool insert(word key,const value_type& v={}) {
        if constexpr(Base::bitmap)return Base::insert(key,v);
        else {
        ht::require(!(key&~ht::mask<W>),"write architecture key domain");
        const auto h=state().mix(key);auto* p=bucket(h.bucket);
        if constexpr(Flow!=upsert_flow::split){
            const auto matching=matching_rows(p,h.quotient);
            const auto free=vacancies(p);const bool fresh=!matching;
            // An existing primary row wins over every vacancy. An absent key
            // needs a Swiss absence proof whenever this bucket has overflow,
            // even if erasure has since left a primary hole.
            const auto admissible=free&(word{0}-word(p[B-1]==0));
            const auto candidates=[&]{
                if constexpr(Base::N<=32)return select_candidates(fresh,matching,unsigned(admissible));
                else return matching|(admissible&(word{0}-word(fresh)));
            }();
            if(!candidates)[[unlikely]]return insert_slow(key,v,h,free);
            const auto destination=unsigned(std::countr_zero(candidates));
            if constexpr(Flow==upsert_flow::shared_stores){
                // Explicit control: idempotent key/flag stores on replacement
                // are charged, including residual-plane read/modify/write.
                occupy(p,destination,h.quotient,v);state().size+=fresh;
            }else {
                if(fresh){assign<E>(p,destination,N,h.quotient);
                    p[flags+destination/8]&=byte(~(1u<<(destination%8)));++state().size;}
                value(p,destination,v);
            }
            return fresh;
        }
        const auto row=locate(p,h.quotient);
        if(row!=N){value(p,row,v);return false;}
        const auto free=vacancies(p);
        if(free&&!p[B-1]){occupy(p,unsigned(std::countr_zero(free)),h.quotient,v);++state().size;return true;}
        return insert_slow(key,v,h,free);
        }
    }
    bool erase(word key) {
        if constexpr(Base::bitmap)return Base::erase(key);
        else {
        ht::require(!(key&~ht::mask<W>),"write architecture key domain");
        const auto h=state().mix(key);auto* p=bucket(h.bucket);const auto row=locate(p,h.quotient);
        if(row!=N){assign<E>(p,row,N,E>K?ht::mask<E>:0);p[flags+row/8]|=byte(1u<<(row%8));hide_value(p,row);--state().size;return true;}
        if(p[B-1]){const auto found=resolve_swiss(key,h);if(found.present){state().controls.data[found.slot]=tombstone;--state().spill;++state().deleted;--state().size;if(p[B-1]!=255)--p[B-1];return true;}}
        return false;
        }
    }
    template<bool Primary> struct const_cell {
        const byte* source;unsigned row;
        [[gnu::always_inline]] const byte* address()const {
            if constexpr(Primary)return source+std::size_t(row)*V;
            else return source;
        }
        template<unsigned Limb> [[gnu::always_inline]] word read()const {
            static_assert(Limb<V/8);
            if constexpr(Primary)return ht::direct_point<64>(source,row*(V/8)+Limb,Base::value_rows*(V/8));
            else return ht::tuple_field<8>::read(source+Limb*8);
        }
        template<unsigned Bytes,unsigned First> [[gnu::always_inline]] ht::bytes<Bytes> value()const {
            static_assert(Bytes%8==0&&First+Bytes/8<=V/8);ht::bytes<Bytes> result;
            ht::each<Bytes/8>([&](auto limb){ht::store<8>(result.data()+limb*8,read<First+limb>());});return result;
        }
    };
    // Preserve the physical SeriesPack/TuplePack source and defer value loads
    // until the consumer knows which fields are needed. External multimap cells
    // must not materialize the entire inline-value area just to read a reference.
    template<class Consume> [[gnu::always_inline]] bool finish_cell(const probe& p,word key,Consume&& consume)const {
        if(read_primary(p.primary,p.hash.quotient,[&](unsigned row) __attribute__((always_inline)) {
            if constexpr(V)consume(const_cell<true>{p.primary+B,row});
        }))return true;
        if constexpr(Base::bitmap)return false;
        else {
        if(!p.primary[B-1])return false;const auto found=find_swiss(key,p.hash.swiss);if(found==absent)return false;
        if constexpr(V)consume(const_cell<false>{state().slots.data+found*tuple_bytes+key_bytes,0});return true;
        }
    }
    template<class Consume> [[gnu::always_inline]] bool find_cell(word key,Consume&& consume)const {
        static_assert(V>0);return finish_cell(prepare(key),key,std::forward<Consume>(consume));
    }
    template<ht::staged_values Values=ht::staged_values::dependent,unsigned FetchBytes=V,class Consume,class Emit>
    bool advance_cell(staged_probe& s,bool& found,Consume&& consume,Emit&& emit)const {
        static_assert(FetchBytes<=V);
        const auto* primary=s.first.primary;
        if(s.phase==staged_phase::primary){const auto hit=read_primary(primary,s.first.hash.quotient,[&](unsigned row){s.slot=row;});
            if(hit){const auto slot=s.slot;
                if constexpr(V){
                    if constexpr(Values==ht::staged_values::lookahead8&&V==8){
                        consume(const_cell<true>{primary+B,slot});found=true;return true;
                    }
                    emit(primary+B+std::size_t(slot)*V,FetchBytes);s.phase=staged_phase::value;return false;
                }else {found=true;return true;}
            }
            if constexpr(Base::bitmap){found=false;return true;}
            else if(!primary[B-1]){found=false;return true;}
            s.group=s.first.hash.swiss&(state().groups-1);s.phase=staged_phase::controls;emit(state().controls.data+s.group*16,16);return false;
        }
        if(s.phase==staged_phase::value){if constexpr(V)consume(const_cell<true>{primary+B,s.slot});found=true;return true;}
        if(s.phase==staged_phase::controls){const auto* control=state().controls.data+s.group*16;
            s.candidates=ht::equal_controls(control,s.first.hash.fingerprint());s.empty_group=ht::equal_controls(control,empty)!=0;
        }else {
            const auto slot=s.group*16+std::countr_zero(s.candidates);s.candidates&=s.candidates-1;
            const auto* tuple=state().slots.data+slot*tuple_bytes;
            if(ht::tuple_field<key_bytes>::read(tuple)==s.key){
                if constexpr(V)consume(const_cell<false>{tuple+key_bytes,0});found=true;return true;
            }
        }
        if(s.candidates){s.phase=staged_phase::tuple;emit(state().slots.data+(s.group*16+std::countr_zero(s.candidates))*tuple_bytes,key_bytes+FetchBytes);return false;}
        if(s.empty_group){found=false;return true;}
        s.group=(s.group+(++s.step))&(state().groups-1);s.phase=staged_phase::controls;emit(state().controls.data+s.group*16,16);return false;
    }
    // Validation/ownership census, outside timed operations. Bind the actual
    // live cell, without interpreting its outer owner's payload representation.
    template<class Visit> void visit_cells(Visit&& visit)const {
        static_assert(V>0);std::size_t count=0;
        for(std::size_t id=0;id<bucket_count;++id){const auto* p=bucket(id);
            for(unsigned row=0;row<N;++row){
                const bool live=[&]{if constexpr(Base::bitmap)return bool(p[row/8]&(1u<<(row%8)));
                    else return !(p[flags+row/8]&(1u<<(row%8)));}();
                if(!live)continue;
                const word q=[&]{if constexpr(Base::bitmap)return word(row);else return ht::point<E>(p,row,N);}();
                visit(state().mix.recover(id,q),p+B+row*V);++count;
            }
        }
        for(std::size_t slot=0;slot<state().groups*16;++slot)if(ht::detail::occupied_control(state().controls.data[slot])){
            const auto* p=state().slots.data+slot*tuple_bytes;visit(ht::load<key_bytes>(p),p+key_bytes);++count;
        }
        ht::require(count==state().size,"mutable cell census");
    }
};
}

#include "multimap.h"
#define WRITE_ARCH_POOL_CHECK_NO_MAIN
#include "pool-check.cpp"

namespace write_arch {
struct guarded_multimap_test_access {
    template<class MM> static auto& pool(MM& owner){return owner.buffers_;}
    template<class MM> static auto& index(MM& owner){return owner.index_;}
    template<class MM> static auto& owner(prepared_multimap<MM>& session){return *session.owner_;}
    template<class MM> static auto bag(MM& mm,word key){
        std::pair<word,const byte*> result{};
        mm.index_.find_cell(key,[&](auto cell){const auto meta=cell.template read<0>();
            result.first=MM::is_external(meta)?cell.template read<1>():0;
            result.second=MM::is_external(meta)?mm.payload(result.first,meta):cell.address()+8;});
        return result;
    }
    template<class MM> static void exhaust_index(prepared_multimap<MM>& s){s.index_epoch_=~word{0};}
    template<class MM> static void exhaust_pool(prepared_multimap<MM>& s){s.pool_epoch_=~word{0};}
#if WRITE_ARCH_SCALAR_IDENTITY
    template<class MM> static auto& identities(){return prepared_multimap<MM>::next_identity_;}
#endif
};
}
namespace {
using access=write_arch::guarded_multimap_test_access;
template<class MM> struct mm_observation:observation {
    MM& owner;
    explicit mm_observation(MM& mm):observation(access::pool(mm),&mm,reinterpret_cast<const byte*>(&mm),sizeof(mm)),owner(mm){
        auto& index=access::index(mm);using Index=std::remove_reference_t<decltype(index)>;using Base=typename Index::reader_owner;
        const auto state=ht::author::table_access::get(static_cast<Base&>(index));
        auto field=[&](const auto& value){const auto [r,offset]=locate(&value,sizeof(value));
            auto get=[p=&value]{return std::vector<word>{word(*p)};};fields.push_back({r,offset,sizeof(value),get,get()});};
        field(state.size);field(state.spill);field(state.deleted);field(state.groups);
        auto buffer=[&](const auto& b){const auto [r,offset]=locate(&b,sizeof(b));
            auto get=[p=&b]{return std::vector<word>{word(p->data),p->size,p->capacity()};};
            fields.push_back({r,offset,sizeof(b),get,get()});};
        buffer(state.primary);buffer(state.controls);buffer(state.slots);
        auto add=[&](ht::owner_part part,const auto& b){if(!b.size)return;
            regions.push_back({{&mm,part,b.data,b.size},{b.data,b.data+b.size},std::vector<byte>(b.size),std::vector<byte>(b.size,1)});};
        add(ht::owner_part::primary,state.primary);add(ht::owner_part::swiss_controls,state.controls);add(ht::owner_part::swiss_tuples,state.slots);
    }
    void finish(){verify();verify_retirements(access::pool(owner),&owner);}
};
template<class MM> struct context: mm_observation<MM> {
    word key;
    bool allow_enter=true,allow_prepare=true,published=false;
    std::vector<ht::mutation_source> retirements;
    context(MM& owner,word key):mm_observation<MM>(owner),key(key){}
    bool enter(const MM& owner,word k,ht::access_intent intent)const noexcept {
        return allow_enter&&&owner==&this->owner&&k==key&&intent==ht::access_intent::write;
    }
    bool prepare(std::span<const ht::mutation_damage> writes,std::span<const ht::mutation_source> retired){
        this->verify(true);if(!allow_prepare)return false;
        for(const auto& d:writes){
            auto it=std::find_if(this->regions.begin(),this->regions.end(),[&](const auto& r){return
                r.source.owner==d.source.owner&&r.source.part==d.source.part&&r.source.base==d.source.base&&r.source.extent==d.source.extent;});
            ht::require(it!=this->regions.end()&&!d.span.plane&&d.span.size&&d.span.offset<=d.source.extent&&d.span.size<=d.source.extent-d.span.offset,"MM source-qualified damage bound");
        }
        retirements.assign(retired.begin(),retired.end());return true;
    }
    void before(ht::mutation_source source,ikea::byte_write span)noexcept {observation::before({source,span});published=true;}
    void finish(){if(published)for(auto s:retirements)this->retire(s);mm_observation<MM>::finish();}
};
template<unsigned W,unsigned Bits,unsigned V,bool Fused=false> void mm_check(bool extended){
    using MM=write_arch::multimap<W,Bits,V,false>;
    using Session=write_arch::prepared_multimap<MM>;
    static_assert(!std::is_move_constructible_v<Session>);
    static_assert(!std::is_copy_constructible_v<Session>);
    constexpr word seed=31&ht::mask<W>;
    ht::permutation<W,W-Bits> mix{seed};auto key=[&](word q){return mix.recover(0,q);};
    {
        auto preserved=std::make_unique<MM>(seed);preserved->insert(key(0),value<V>(913));
        auto* identity=preserved.get();context before(*preserved,key(0));
        failure_probe::after=0;failure_probe::hit=false;bool failed=false;
#if WRITE_ARCH_SCALAR_IDENTITY
        {Session adopted(std::move(preserved));preserved=adopted.release_owner();}
        ht::require(!failure_probe::hit&&preserved.get()==identity,"scalar adoption needs no identity allocation");
#else
        try{Session rejected(std::move(preserved));}catch(const std::bad_alloc&){failed=true;}
#endif
        failure_probe::after=-1;
#if !WRITE_ARCH_SCALAR_IDENTITY
        ht::require(failed&&failure_probe::hit&&preserved.get()==identity,"adoption allocation failure preserves caller owner");before.verify(true);
#else
        (void)failed;before.verify(true);
        // Synthetic end-of-domain boundary; no token from these temporary
        // identities escapes this test. Real operation never resets the source.
        auto& ids=access::identities<MM>();const auto saved=ids.exchange(~word{0}-1);
        {Session last(std::move(preserved));preserved=last.release_owner();}
        for(unsigned attempt=0;attempt<2;++attempt){
            bool exhausted=false;try{Session rejected(std::move(preserved));}catch(const std::runtime_error&){exhausted=true;}
            ht::require(exhausted&&preserved.get()==identity&&ids.load()==~word{0},"identity exhaustion saturates before consuming owner");
        }
        ids.store(saved);
#endif
    }
    std::map<word,std::vector<ht::bytes<V>>> expected;
    auto original=std::make_unique<MM>(seed);
    // Prepare Swiss capacity before exclusive adoption; all bags start inline.
    const unsigned keys=extended?260:std::min(MM::N+5,32u);
    for(unsigned q=0;q<keys;++q)for(unsigned i=0;i<(V==8?2u:1u);++i){auto v=value<V>(q*10+i);original->insert(key(q),v);expected[key(q)].push_back(v);}
    if constexpr(W-Bits>9){ht::require(original->erase(key(1)),"primary-hole fixture");expected.erase(key(1));}
    Session session(std::move(original));auto& owner=access::owner(session);
    auto verify=[&]{
        ht::require(session.size()==expected.size(),"prepared MM distinct-key count");
        for(const auto& [k,bag]:expected){std::vector<ht::bytes<V>> got;
            ht::require(session.find(k,[&](auto v){got.push_back(v);}),"prepared MM expected key");
            ht::require(got==bag,"prepared MM ordered complete payload");}
        owner.validate();
    };
    auto append=[&](word k,unsigned salt,bool alias=false){
        context denied(owner,k);denied.allow_enter=false;
        ht::require(session.prepare(k,denied).result==Session::status::denied,"prepare grant refusal");denied.verify(true);
        context canceled(owner,k);auto first=session.prepare(k,canceled);ht::require(first.result==Session::status::ready,"prepared cancellation fixture");
        canceled.verify(true);
        ht::require(session.prepare(k,canceled).result==Session::status::busy,"second head refused without invalidating first");
        ht::require(session.cancel(first.prepared)&&!session.cancel(first.prepared),"cancel consumes head once");canceled.verify(true);
        auto v=value<V>(salt);
        ht::require(session.commit(first.prepared,std::span<const byte,V>(v.data(),V),canceled)==ht::mutation_status::denied,"canceled token rejected");
        context admitted(owner,k);
        if constexpr(Fused){
            const auto sample=std::span<const byte,V>(v.data(),V);
            ht::require(session.append(k,sample,denied)==ht::mutation_status::denied,"immediate grant refusal");denied.verify(true);
            auto promise=session.prepare(k,admitted);ht::require(promise.result==Session::status::ready,"immediate exclusion fixture");
            ht::require(session.append(k,sample,admitted)==ht::mutation_status::denied,"promised head excludes immediate mutation");
            ht::require(session.cancel(promise.prepared),"immediate exclusion leaves promise intact");admitted.verify(true);
            context refused(owner,k);refused.allow_prepare=false;
            ht::require(session.append(k,sample,refused)==ht::mutation_status::denied,"immediate effects refusal");refused.verify(true);
            const byte* input=v.data();
            if(alias){auto& index=access::index(owner);index.visit_cells([&](word found,const byte* cell){if(found==k)input=cell;});std::memcpy(v.data(),input,V);}
            const bool existing=expected.contains(k);
            ht::require(session.append(k,std::span<const byte,V>(input,V),admitted)==
                (existing?ht::mutation_status::updated:ht::mutation_status::inserted),"immediate commit status");
            admitted.finish();expected[k].push_back(v);verify();return;
        }
        auto head=session.prepare(k,admitted);ht::require(head.result==Session::status::ready,"prepared MM ready");admitted.verify(true);
        // A token from another adapter cannot admit this owner's head.
        Session other(std::make_unique<MM>(seed));
        ht::require(other.commit(head.prepared,std::span<const byte,V>(v.data(),V),admitted)==ht::mutation_status::denied,"cross-owner token rejected");
        context refused(owner,k);refused.allow_prepare=false;
        ht::require(session.commit(head.prepared,std::span<const byte,V>(v.data(),V),refused)==ht::mutation_status::denied,"all-effects refusal");refused.verify(true);
        context stale_grant(owner,k);stale_grant.allow_enter=false;
        ht::require(session.commit(head.prepared,std::span<const byte,V>(v.data(),V),stale_grant)==ht::mutation_status::denied,"commit reacquires grant");stale_grant.verify(true);
        const byte* input=v.data();
        if(alias){
            auto& index=access::index(owner);index.visit_cells([&](word found,const byte* cell){if(found==k)input=cell;});
            std::memcpy(v.data(),input,V); // Expected snapshot includes changing metadata.
        }
        const bool existing=expected.contains(k);
        ht::require(session.commit(head.prepared,std::span<const byte,V>(input,V),admitted)==
            (existing?ht::mutation_status::updated:ht::mutation_status::inserted),"prepared commit status");admitted.finish();
        expected[k].push_back(v);verify();
        ht::require(session.commit(head.prepared,std::span<const byte,V>(v.data(),V),admitted)==ht::mutation_status::denied,"successful token consumed once");
    };
    verify();
    if constexpr(W-Bits>9)append(key(keys-1),690); // Swiss duplicate behind primary hole.
    for(unsigned q=0;q<keys;++q)append(key(q),700+q,q%17==0);
    append(key(keys),1200); // Fresh bitmap row or Swiss tuple.
    append(mix.recover(1,0),1201); // Fresh primary row in the other bucket.
    // Fit, promote, release a last-reference block, directory growth and many
    // shared-block releases. Large test crosses the 256-rank allocation boundary.
    for(unsigned q=0;q<keys;++q){append(key(q),1400+q);append(key(q),1800+q);}
    for(unsigned i=0;i<11;++i)append(key(0),2200+i,i==10);
    if constexpr(W<64){context invalid(owner,word{1}<<W);
        ht::require(session.prepare(word{1}<<W,invalid).result==Session::status::denied,"prepared domain check");invalid.verify(true);}
    // Hash discovery is complete before a head exists. Late input remains an
    // explicit snapshot, and extraction/re-adoption invalidates that head.
    context admission(owner,key(0));auto old=session.prepare(key(0),admission);
    ht::require(old.result==Session::status::ready,"handoff head");auto moved=session.release_owner();
    Session adopted(std::move(moved));context next(access::owner(adopted),key(0));auto fresh=adopted.prepare(key(0),next);
    auto v=value<V>(3500);
    ht::require(adopted.commit(old.prepared,std::span<const byte,V>(v.data(),V),next)==ht::mutation_status::denied,"re-adoption rejects old identity");
    ht::require(adopted.cancel(fresh.prepared),"new adapter cancellation");next.verify(true);
    // Allocation failure while preparing an empty pool leaves the index and
    // inline value unchanged; the same session remains usable afterwards.
    auto small=std::make_unique<MM>(seed);
    for(unsigned i=0;i<(V==8?2u:1u);++i)small->insert(key(0),value<V>(i));
    Session allocation(std::move(small));context unchanged(access::owner(allocation),key(0));
    failure_probe::after=1;failure_probe::hit=false;bool failed=false;
    try{if constexpr(Fused)(void)allocation.append(key(0),std::span<const byte,V>(v.data(),V),unchanged);
        else (void)allocation.prepare(key(0),unchanged);}catch(const std::bad_alloc&){failed=true;}
    failure_probe::after=-1;
    ht::require(failed&&failure_probe::hit,"joined preparation allocation failure");unchanged.verify(true);
    auto retry=allocation.prepare(key(0),unchanged);ht::require(retry.result==Session::status::ready,"allocation retry");
    ht::require(allocation.commit(retry.prepared,std::span<const byte,V>(v.data(),V),unchanged)==ht::mutation_status::updated,"allocation retry commit");unchanged.finish();
    if constexpr(W-Bits>9){
        auto full_index=std::make_unique<MM>(seed);
        for(unsigned q=0;q<MM::N;++q)full_index->insert(key(q),v);
        Session capacity(std::move(full_index));context no_room(access::owner(capacity),key(MM::N));
        ht::require(capacity.prepare(key(MM::N),no_room).result==Session::status::needs_capacity,"index capacity preflight performs no rehash");
        if constexpr(Fused)ht::require(capacity.append(key(MM::N),std::span<const byte,V>(v.data(),V),no_room)==ht::mutation_status::needs_capacity,"immediate capacity refusal");no_room.verify(true);
    }
    if(extended){
        auto maximum=std::make_unique<MM>(seed);
        for(unsigned i=0;i<write_arch::capacity_buffers<V>::max_values;++i)maximum->insert(key(0),value<V>(i));
        Session capped(std::move(maximum));context limit(access::owner(capped),key(0));
        ht::require(capped.prepare(key(0),limit).result==Session::status::full,"full bag rejected before mutation");
        if constexpr(Fused)ht::require(capped.append(key(0),std::span<const byte,V>(v.data(),V),limit)==ht::mutation_status::denied,"immediate full bag refusal");limit.verify(true);
        // Reuse the adapter's exact object address while an old token survives.
        alignas(Session) std::array<byte,sizeof(Session)> storage;
        auto* first=std::construct_at(reinterpret_cast<Session*>(storage.data()),std::make_unique<MM>(seed));
        context first_context(access::owner(*first),key(0));auto stale=first->prepare(key(0),first_context);
        auto same_owner=first->release_owner();std::destroy_at(first);
        auto* second=std::construct_at(reinterpret_cast<Session*>(storage.data()),std::move(same_owner));
        context second_context(access::owner(*second),key(0));auto current=second->prepare(key(0),second_context);
        ht::require(second->commit(stale.prepared,std::span<const byte,V>(v.data(),V),second_context)==ht::mutation_status::denied,"same-address incarnation reuse rejected");
        ht::require(second->cancel(current.prepared),"current incarnation survives stale attempt");second_context.verify(true);std::destroy_at(second);
    }
    std::cout<<(Fused?"FUSED_MM_PASS,":"PREPARED_MM_PASS,")<<W<<','<<Bits<<','<<V<<','<<extended<<'\n';
}
}
#ifndef WRITE_ARCH_MM_CHECK_NO_MAIN
int main(){
    mm_check<10,1,8>(false);mm_check<10,1,64>(false);
    mm_check<15,1,8>(true);mm_check<15,1,64>(true);
    mm_check<31,1,8>(false);mm_check<31,1,64>(false);
    mm_check<64,1,8>(false);mm_check<64,1,64>(false);
    mm_check<10,1,8,true>(false);mm_check<10,1,64,true>(false);
    mm_check<15,1,8,true>(true);mm_check<15,1,64,true>(true);
    mm_check<31,1,8,true>(false);mm_check<31,1,64,true>(false);
    mm_check<64,1,8,true>(false);mm_check<64,1,64,true>(false);
}
#endif

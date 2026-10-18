#define WRITE_ARCH_MM_CHECK_NO_MAIN
#include "multimap-check.cpp"

namespace {
template<unsigned W,unsigned Bits,unsigned V> void observed_check(){
    using MM=write_arch::multimap<W,Bits,V,false>;
    using Session=write_arch::prepared_multimap<MM>;
    using Head=typename Session::observation;
    using State=typename Session::observation_status;
    static_assert(!std::is_move_constructible_v<Head> && !std::is_copy_constructible_v<Head>);
    constexpr word seed=31&ht::mask<W>;
    ht::permutation<W,W-Bits> mix{seed};auto key=[&](word q){return mix.recover(0,q);};
    auto original=std::make_unique<MM>(seed);
    std::map<word,std::vector<ht::bytes<V>>> expected;
    const unsigned n=std::min(MM::N+8,64u);
    for(unsigned i=0;i<n;++i)for(unsigned j=0;j<3;++j){
        auto v=value<V>(100*i+j);original->insert(key(i),v);expected[key(i)].push_back(v);
    }
    Session session(std::move(original));auto& owner=access::owner(session);
    std::size_t hints=0;
    auto hint=[&](const auto& observation){return [&](const byte* pointer,std::size_t size)noexcept {
        const auto address=reinterpret_cast<std::uintptr_t>(pointer);
        const bool live=std::any_of(observation.regions.begin(),observation.regions.end(),[&](const auto& r){
            const auto base=reinterpret_cast<std::uintptr_t>(r.source.base);
            return address>=base&&address-base<=r.source.extent&&size&&size<=r.source.extent-(address-base);
        });
        ht::require(live,"observed hint stays within a current live source");++hints;
    };};
    {context c(owner,key(0));auto emit=hint(c);const auto before=hints;
        failure_probe::after=0;failure_probe::hit=false;
        ht::require(session.prefetch_append(key(0),c,emit),"plain lookahead grant");
        failure_probe::after=-1;ht::require(!failure_probe::hit&&hints>before,"plain lookahead emits without allocation");c.verify(true);
        c.allow_enter=false;const auto after=hints;
        ht::require(!session.prefetch_append(key(0),c,emit)&&hints==after,"plain lookahead refusal emits nothing");c.verify(true);}
    auto begin=[&](Head& h,word k){context c(owner,k);auto emit=hint(c);
        failure_probe::after=0;failure_probe::hit=false;auto p=session.begin_observation(k,h,c,emit);
        failure_probe::after=-1;ht::require(!failure_probe::hit,"observational begin allocates nothing");c.verify(true);
        ht::require(p.status==State::pending,"observed begin");
    };
    auto ready=[&](Head& h,word k){unsigned changes=0;
        for(unsigned steps=0;steps<10000;++steps){context c(owner,k);auto emit=hint(c);
            failure_probe::after=0;failure_probe::hit=false;const auto p=session.advance_observation(h,c,emit);
            failure_probe::after=-1;ht::require(!failure_probe::hit,"observational advance and restart allocate nothing");c.verify(true);changes|=p.refresh;
            if(p.status==State::ready)return changes;
            ht::require(p.status==State::pending,"observed progress is live");
        }
        throw std::runtime_error("observed traversal did not finish");
    };
    auto verify=[&]{ht::require(session.size()==expected.size(),"observed distinct count");
        for(const auto& [k,bag]:expected){std::vector<ht::bytes<V>> actual;
            ht::require(session.find(k,[&](auto v){actual.push_back(v);})&&actual==bag,"observed complete ordered bags");}
        owner.validate();
    };
    auto append=[&](word k,unsigned salt){context c(owner,k);auto v=value<V>(salt);
        const auto result=session.append(k,std::span<const byte,V>(v.data(),V),c);
        ht::require(result==(expected.contains(k)?ht::mutation_status::updated:ht::mutation_status::inserted),"observed interleaved fused write");
        c.finish();expected[k].push_back(v);verify();
    };
    auto commit=[&](Head& h,word k,unsigned salt){auto v=value<V>(salt);
        context refused(owner,k);refused.allow_prepare=false;
        ht::require(session.commit_observed(h,std::span<const byte,V>(v.data(),V),refused).status==ht::mutation_status::denied,"observed preservation refusal");
        refused.verify(true);
        context c(owner,k);const auto result=session.commit_observed(h,std::span<const byte,V>(v.data(),V),c);
        ht::require(result.status==(expected.contains(k)?ht::mutation_status::updated:ht::mutation_status::inserted),"observed complete commit");
        c.finish();expected[k].push_back(v);verify();
        context replay(owner,k);
        ht::require(session.commit_observed(h,std::span<const byte,V>(v.data(),V),replay).status==ht::mutation_status::denied,"observed head consumed once");
        replay.verify(true);return result.refresh;
    };
    // Unrelated fitting commits must not invalidate each other's observations.
    Head first,second;begin(first,key(0));begin(second,key(1));ready(first,key(0));ready(second,key(1));
    {context c(owner,key(1));auto emit=hint(c);const auto before=hints;
        ht::require(session.begin_observation(key(1),first,c,emit).status==State::denied&&hints==before,"active head cannot be rebound");c.verify(true);}
    ht::require(commit(first,key(0),10001)==0&&commit(second,key(1),10002)==0,"unrelated fitting heads survive");
    Head allocation;begin(allocation,key(0));ready(allocation,key(0));
    context allocation_before(owner,key(0));auto allocation_value=value<V>(10014);
    failure_probe::after=0;failure_probe::hit=false;bool allocation_failed=false;
    try{session.commit_observed(allocation,std::span<const byte,V>(allocation_value.data(),V),allocation_before);}
    catch(const std::bad_alloc&){allocation_failed=true;}
    failure_probe::after=-1;
    ht::require(allocation_failed&&failure_probe::hit,"observed commit allocation failure exercised");allocation_before.verify(true);
    commit(allocation,key(0),10014);
    Head content;begin(content,key(2));ready(content,key(2));append(key(2),10003);
    ht::require(ready(content,key(2))&Session::content_changed,"same-key content refresh");commit(content,key(2),10004);
    Head direct_content;begin(direct_content,key(7));ready(direct_content,key(7));append(key(7),10015);
    ht::require(commit(direct_content,key(7),10016)&Session::content_changed,"direct commit refreshes current count without advance");
    Head direct_growth;begin(direct_growth,key(0));ready(direct_growth,key(0));
    while(expected[key(0)].size()<9)append(key(0),10100+unsigned(expected[key(0)].size()));
    ht::require((commit(direct_growth,key(0),10017)&(Session::content_changed|Session::pool_changed))==
        (Session::content_changed|Session::pool_changed),"direct commit refreshes promoted bag without advance");
    Head membership;begin(membership,key(3));append(key(n),10005);
    ht::require(ready(membership,key(3))&Session::index_changed,"membership restarts before saved index use");commit(membership,key(3),10006);
    Head pool;begin(pool,key(4));ready(pool,key(4));append(key(5),10007);append(key(5),10008);
    ht::require(ready(pool,key(4))&Session::pool_changed,"pool change requalifies before hint");commit(pool,key(4),10009);
    // Two absent observations have no promise on the same empty row or rank.
    Head absent_a,absent_b;begin(absent_a,key(n+1));begin(absent_b,key(n+1));ready(absent_a,key(n+1));ready(absent_b,key(n+1));
    commit(absent_a,key(n+1),10010);
    ht::require(commit(absent_b,key(n+1),10011)&Session::index_changed,"late commit resolves inserted duplicate");
    Head overflow;begin(overflow,key(n-1));ready(overflow,key(n-1));commit(overflow,key(n-1),10012);
    Head canceled;begin(canceled,key(6));
    ht::require(session.cancel_observation(canceled)&&!session.cancel_observation(canceled),"observational cancellation is local and single-use");
    context denied(owner,key(6));denied.allow_enter=false;auto emit=hint(denied);const auto old_hints=hints;
    ht::require(session.begin_observation(key(6),canceled,denied,emit).status==State::denied&&hints==old_hints,"refused observation emits no hints");denied.verify(true);
    Head held;begin(held,key(6));ready(held,key(6));
    context grant(owner,key(6));auto promised=session.prepare(key(6),grant);
    ht::require(promised.result==Session::status::ready,"promised exclusion fixture");
    ht::require(session.advance_observation(held,grant,emit).status==State::denied,"promised head excludes observational steps");
    ht::require(session.cancel(promised.prepared),"release promised exclusion");grant.verify(true);commit(held,key(6),10013);
    Head foreign;begin(foreign,key(7));ready(foreign,key(7));auto released=session.release_owner();
    Session next(std::move(released));auto& replacement=access::owner(next);context fresh(replacement,key(7));auto fresh_hint=hint(fresh);
    ht::require(next.advance_observation(foreign,fresh,fresh_hint).status==State::denied,"re-adoption rejects old observation on identical owner address");
    ht::require(session.advance_observation(foreign,fresh,fresh_hint).status==State::denied,"released adapter rejects before pointer use");fresh.verify(true);
    // Saturation is tested on isolated sessions; production counters never reset.
    for(bool pool_limit:{false,true}){
        auto item=std::make_unique<MM>(seed);for(unsigned i=0;i<4;++i)item->insert(key(0),value<V>(i));
        Session exhausted(std::move(item));auto& material=access::owner(exhausted);
        if(pool_limit)access::exhaust_pool(exhausted);else access::exhaust_index(exhausted);
        const auto k=pool_limit?key(0):key(1);context before(material,k);auto v=value<V>(999);
        ht::require(exhausted.append(k,std::span<const byte,V>(v.data(),V),before)==ht::mutation_status::denied,"structural epoch saturation refuses mutation");
        before.verify(true);material.validate();
    }
    std::cout<<"OBSERVED_MM_PASS,"<<W<<','<<Bits<<','<<V<<','<<hints<<'\n';
}
template<unsigned V> void observed_storage_check(){
    using MM=write_arch::multimap<16,10,V,false>;
    using Session=write_arch::prepared_multimap<MM>;
    using State=typename Session::observation_status;
    using PA=write_arch::guarded_pool_test_access;
    struct lane {
        bool enter(const MM&,word,ht::access_intent)const noexcept{return true;}
        bool prepare(std::span<const ht::mutation_damage>,std::span<const ht::mutation_source>)const noexcept{return true;}
        void before(ht::mutation_source,ikea::byte_write)const noexcept{}
    };
    for(bool relocate:{true,false}){
        auto initial=std::make_unique<MM>(13);std::map<word,std::vector<ht::bytes<V>>> expected;
        const unsigned keys=relocate?512:1;
        for(unsigned k=0;k<keys;++k)for(unsigned i=0;i<(k?unsigned(V==8?2:1):(relocate?3u:4u));++i){
            auto v=value<V>(1000*k+i);initial->insert(k,v);expected[k].push_back(v);
        }
        Session session(std::move(initial));auto& owner=access::owner(session);auto& buffers=access::pool(owner);
        const auto old_directory=PA::directory(buffers);const auto [reference,payload]=access::bag(owner,0);
        const auto* directory_hint=PA::directory_slot(buffers,reference);
        auto sources=[&]{std::vector<ht::mutation_source> list;
            guarded_pool_access::sources(buffers,&owner,[&](auto source){list.push_back(source);});return list;};
        const auto before=sources();ht::mutation_source old_payload_source{};
        for(auto source:before){const auto base=reinterpret_cast<std::uintptr_t>(source.base),p=reinterpret_cast<std::uintptr_t>(payload);
            if(p>=base&&p-base<source.extent)old_payload_source=source;}
        ht::require(old_payload_source.extent,"physical fixture has a live payload source");
        typename Session::observation h;bool directory_seen=false;const byte* last_hint=nullptr;
        auto step=[&](bool begin){context c(owner,0);
            auto emit=[&](const byte* pointer,std::size_t size)noexcept {
                const auto address=reinterpret_cast<std::uintptr_t>(pointer);
                const bool live=std::any_of(c.regions.begin(),c.regions.end(),[&](const auto& r){
                    const auto base=reinterpret_cast<std::uintptr_t>(r.source.base);
                    return address>=base&&address-base<=r.source.extent&&size&&size<=r.source.extent-(address-base);
                });
                ht::require(live,"physical invalidation emits only current live addresses");
                directory_seen|=pointer==directory_hint;last_hint=pointer;
            };
            failure_probe::after=0;failure_probe::hit=false;
            auto result=begin?session.begin_observation(0,h,c,emit):session.advance_observation(h,c,emit);
            failure_probe::after=-1;ht::require(!failure_probe::hit,"physical invalidation stages allocate nothing");c.verify(true);
            return result;
        };
        ht::require(step(true).status==State::pending,"physical observation begins");
        for(unsigned i=0;!directory_seen&&i<32;++i)ht::require(step(false).status==State::pending,"reach actual directory dependency");
        ht::require(directory_seen,"head suspended after the exact directory-slot hint");
        lane exclusive;
        auto append=[&](word k,unsigned salt){auto v=value<V>(salt);
            ht::require(session.append(k,std::span<const byte,V>(v.data(),V),exclusive)==ht::mutation_status::updated,"physical fixture appends existing keys");
            expected[k].push_back(v);
        };
        if(relocate){
            for(unsigned k=1;k<keys&&PA::directory(buffers)==old_directory;++k)
                while(expected[k].size()<3)append(k,200000+k*10+unsigned(expected[k].size()));
            ht::require(PA::directory(buffers)!=old_directory,"directory really relocated while the head was suspended");
        }else{
            append(0,200000);const auto after=sources();
            ht::require(std::none_of(after.begin(),after.end(),[&](auto s){return s.base==old_payload_source.base;}),"old observed block really retired");
            ht::require(access::bag(owner,0).second!=payload,"promotion has a different live payload");
        }
        unsigned refresh=0;bool ready=false;
        for(unsigned i=0;i<32&&!ready;++i){const auto p=step(false);refresh|=p.refresh;
            ht::require(p.status!=State::denied,"physical observation remains admissible");ready=p.status==State::ready;}
        ht::require(ready&&(refresh&Session::pool_changed)&&!(refresh&Session::index_changed),"pool invalidation refreshes without membership changes");
        if(!relocate)ht::require(refresh&Session::content_changed,"retired bag refreshes metadata and reference");
        const auto* current=access::bag(owner,0).second;
        ht::require(last_hint==current+expected[0].size()*V,"last hint warms the requested append destination");
        auto v=value<V>(300000);context commit(owner,0);
        ht::require(session.commit_observed(h,std::span<const byte,V>(v.data(),V),commit).status==ht::mutation_status::updated,"physical observation commits");
        commit.finish();expected[0].push_back(v);owner.validate();
        for(const auto& [k,bag]:expected){std::vector<ht::bytes<V>> got;
            ht::require(session.find(k,[&](auto value){got.push_back(value);})&&got==bag,"physical invalidation preserves all ordered bags");}
    }
    std::cout<<"OBSERVED_STORAGE_PASS,"<<V<<'\n';
}
}
int main(){
    observed_check<10,1,8>();observed_check<10,1,64>();
    observed_check<15,1,8>();observed_check<15,1,64>();
    observed_check<31,1,8>();observed_check<31,1,64>();
    observed_check<64,1,8>();observed_check<64,1,64>();
    observed_storage_check<8>();observed_storage_check<64>();
}

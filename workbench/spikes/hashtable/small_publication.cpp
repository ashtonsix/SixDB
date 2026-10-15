#include "small.h"
#include "small_baseline.h"
#include "publication.h"
#include <object/store_impl.h>
#include <map>
#include <string_view>
#ifndef HT_PUBLICATION_INLINE
#define HT_PUBLICATION_INLINE 1
#endif
#ifndef HT_PUBLICATION_VECTOR_BAGS
#define HT_PUBLICATION_VECTOR_BAGS 0
#endif
#ifndef HT_PUBLICATION_BUFFER_BAGS
#define HT_PUBLICATION_BUFFER_BAGS 0
#endif

namespace ht {
template<unsigned K,container_kind Kind> using publication_candidate=small_table<K,Kind,
    (Kind==container_kind::set?0:8),bool(HT_PUBLICATION_BUFFER_BAGS),
    std::conditional_t<HT_PUBLICATION_VECTOR_BAGS,pooled_vector_bags<8,true,16>,
        std::conditional_t<HT_PUBLICATION_BUFFER_BAGS,buffer_bags<8>,links<64,false,0>>>,8*HT_PUBLICATION_INLINE>;
template<class Body> auto contents(const Body& body) {
    std::map<word,std::vector<word>> values;
    body.each([&](word k,word v){values[k].push_back(v);});
    for(auto& [k,v]:values)std::sort(v.begin(),v.end());return values;
}
template<class T> T accepted(orbital::Result<T> result) {require(bool(result),"ObjectStore result");return std::move(*result);}
template<unsigned K,container_kind Kind> struct publication_baseline {
    oa::Pool* pool;std::unique_ptr<small_abseil<K,Kind>> body;
    publication_baseline(oa::Pool& p,word seed):pool(&p),body(std::make_unique<small_abseil<K,Kind>>(p,seed)) {}
    publication_baseline(const publication_baseline& other)
        :pool(other.pool),body(std::make_unique<small_abseil<K,Kind>>(*other.body)) {}
    publication_baseline(publication_baseline&&)noexcept=default;
    publication_baseline& operator=(publication_baseline&&)noexcept=default;
    auto set_value(word k,word v){return body->set_value(k,v);}
    auto erase(word k){return body->erase(k);}
    void clear(){body=std::make_unique<small_abseil<K,Kind>>(*pool,0);}
    template<class Visit> void each(Visit&& f)const {body->each(std::forward<Visit>(f));}
    std::optional<publication_baseline> clone()const {
        try {return publication_baseline(*this);}
        catch(const std::bad_alloc&){return {};}
    }
    memory footprint()const {auto m=body->footprint();m.metadata+=sizeof(*this);return m;}
};
template<unsigned K> word key(unsigned i) {return (word(i)*0x9e3779b97f4a7c15ULL)&mask(K);}
struct publication_sample {const char* phase;double ns;memory current,retained;word catalog;};
template<unsigned K,container_kind Kind,class Body> std::vector<publication_sample> history(oa::Pool& pool,bool faults) {
    using Material=publication_material<Body>;using Store=orbital::ObjectStore<Material>;
    catalog_resource resource;std::vector<publication_sample> samples;constexpr orbital::ObjectId id{93,7};
    {
        Store store(resource);
        auto publish=[&](Body& body,typename Store::Scope& basis) {
            auto owner=accepted(store.capture({&body},{}));
            const std::array<typename Store::Change,1> changes{{{id,std::move(owner)}}};
            typename Store::Prepared prepared(store);require(bool(store.prepare(basis,changes,prepared)),"prepare publication");
            require(bool(store.install(prepared)),"install publication");
        };
        auto current=[&]()->const Body& {return store.borrow().bind(id).value->body;};
        auto phase=[&](const char* name,auto&& f) {
            const auto before=clock_type::now();f();
            const auto ns=std::chrono::duration<double,std::nano>(clock_type::now()-before).count();
            // Inventory traversals and sample storage stay outside the phase clock.
            samples.push_back({name,ns,current().footprint(),Material::retained(),resource.live});
        };
        phase("initial_build_publish",[&] {
            Body body(pool,773);body.set_value(0,0);body.set_value(mask(K),~word(0));
            typename Store::Scope basis(store);publish(body,basis);
        });
        const auto original_values=contents(current());
        {
            typename Store::Scope original(store); // Deliberately not bound yet.
            phase("promotion_clone_publish",[&] {
                typename Store::Scope basis(store);auto candidate=basis.bind(id).value->body.clone();
                require(bool(candidate),"clone before promotion");candidate->set_value(0,~word(0));publish(*candidate,basis);
            });
            const auto promoted_values=contents(current());
            typename Store::Scope promoted(store); // Also retained without binding.
            phase("grow_and_bags_publish",[&] {
                typename Store::Scope basis(store);auto candidate=basis.bind(id).value->body.clone();require(bool(candidate),"clone grow");
                for(unsigned i=1;i<192;++i)candidate->set_value(key<K>(i),i);
                if constexpr(Kind==container_kind::multimap)for(unsigned i=0;i<300;++i)candidate->set_value(0,i);
                publish(*candidate,basis);
            });
            phase("erase_reuse_publish",[&] {
                typename Store::Scope basis(store);auto candidate=basis.bind(id).value->body.clone();require(bool(candidate),"clone erase");
                candidate->erase(0);candidate->set_value(0,19);
                if constexpr(Kind==container_kind::multimap)for(unsigned i=0;i<18;++i)candidate->set_value(0,i);
                publish(*candidate,basis);
            });
            phase("clear_refill_publish",[&] {
                typename Store::Scope basis(store);Body empty(pool,813);empty.set_value(0,61);publish(empty,basis);
            });
            require(contents(original.bind(id).value->body)==original_values,"unbound retained initial list/bag discovery");
            require(contents(promoted.bind(id).value->body)==promoted_values,"unbound retained promoted bag discovery");
            if(faults) {
                typename Store::Scope basis(store);const auto old=basis.bind(id);const auto saved=contents(current());
                auto candidate=old.value->body.clone();require(bool(candidate),"fault candidate");
                unsigned applications=0;candidate->set_value(0,~word(0));++applications;
                const auto staged=contents(*candidate);
                resource.fail_at=resource.calls;auto refused=store.capture({&*candidate},{});resource.fail_at=SIZE_MAX;
                require(!refused&&refused.error()==orbital::Error::capacity,"capture refusal");
                require(contents(*candidate)==staged&&contents(current())==saved,"capture preserves pending and published");
                auto owner=accepted(store.capture({&*candidate},{}));
                const std::array<typename Store::Change,1> changes{{{id,std::move(owner)}}};
                typename Store::Prepared prepared(store);resource.fail_at=resource.calls;
                auto failure=store.prepare(basis,changes,prepared);resource.fail_at=SIZE_MAX;
                require(!failure&&failure.error()==orbital::Error::capacity,"prepare refusal");
                require(bool(store.prepare(basis,changes,prepared)),"same candidate metadata retry");prepared.abort();
                require(contents(current())==saved&&applications==1,"abort does not install or replay mutation");
                typename Store::Prepared install(store);require(bool(store.prepare(basis,changes,install)),"new publication packet after abort");
                require(bool(store.install(install))&&contents(current())==staged,"install staged candidate");
                require(contents(original.bind(id).value->body)==original_values&&
                    contents(promoted.bind(id).value->body)==promoted_values,"faults preserve retained discovery");
            }
        }
        phase("reclaim_history",[&]{store.drain();});
    }
    require(resource.live==0,"catalog lifetime balance");return samples;
}
template<unsigned K,container_kind Kind> void run(oa::Pool& pool,unsigned reps,bool checking) {
    if(!checking) {
        // Warm the complete ownership/publication/reclamation history for both
        // contenders, including their real allocation and bag paths.
        (void)history<K,Kind,publication_candidate<K,Kind>>(pool,false);
        (void)history<K,Kind,publication_baseline<K,Kind>>(pool,false);
    }
    for(unsigned r=0;r<reps;++r)for(unsigned step=0;step<2;++step) {
        const auto arm=(step+r)%2;
        auto rows=arm?history<K,Kind,publication_baseline<K,Kind>>(pool,checking):history<K,Kind,publication_candidate<K,Kind>>(pool,checking);
        if(!checking)for(const auto& s:rows)std::printf("%s,%u,%u,%u,%s,%.6f,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            arm?"abseil":HT_PUBLICATION_BUFFER_BAGS?"compact_list_buffers":"compact_list",K,unsigned(Kind),r,s.phase,s.ns,(unsigned long long)s.current.requested,
            (unsigned long long)s.current.backing,(unsigned long long)s.current.metadata,(unsigned long long)s.retained.requested,
            (unsigned long long)s.retained.backing,(unsigned long long)s.retained.metadata,(unsigned long long)s.catalog);
    }
}
template<unsigned K> void widths(oa::Pool& pool,unsigned reps,bool checking) {
    if constexpr(!HT_PUBLICATION_BUFFER_BAGS) {
        run<K,container_kind::set>(pool,reps,checking);run<K,container_kind::map>(pool,reps,checking);
    }
    run<K,container_kind::multimap>(pool,reps,checking);
}
} // namespace ht
int main(int argc,char** argv) {
    using namespace ht;auto mode=argc>1?std::string_view(argv[1]):"check";
    const bool checking=mode=="check";require(checking||mode=="bench","publication mode");
    const auto reps=checking?1u:(argc>2?std::stoul(argv[2]):3u);pool_owner pool;
    if(!checking)std::puts("arm,K,kind,rep,phase,ns,current_requested,current_capacity,current_metadata,retained_requested,retained_capacity,retained_external_metadata,catalog_live");
    widths<8>(*pool.pool,reps,checking);widths<16>(*pool.pool,reps,checking);widths<24>(*pool.pool,reps,checking);widths<32>(*pool.pool,reps,checking);
    widths<40>(*pool.pool,reps,checking);widths<48>(*pool.pool,reps,checking);widths<56>(*pool.pool,reps,checking);widths<64>(*pool.pool,reps,checking);
    if(checking)std::puts("publication: all key widths/kinds, unbound retained discovery, bag growth/reuse and metadata retry passed");
}

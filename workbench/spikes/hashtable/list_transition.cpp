#include "promoted.h"
#include "small_baseline.h"
#include "publication.h"
#include <object/store_impl.h>
#include <map>
#include <string_view>
#include <variant>

namespace ht {
template<class Body> auto snapshot(const Body& body) {
    std::map<word,std::vector<word>> out;body.each([&](word k,word v){out[k].push_back(v);});
    for(auto& [k,v]:out)std::sort(v.begin(),v.end());return out;
}
template<unsigned K> word transition_key(unsigned i) {return word(i)*0xd6e8feb86659fd93ULL&mask(K);}
template<unsigned K,container_kind Kind> class transition_body {
    using list=small_table<K,Kind>;using hierarchy=promoted_table<K,Kind>;
    oa::Pool* pool_;std::variant<list,hierarchy> storage_;
    transition_body(oa::Pool& pool,hierarchy&& h):pool_(&pool),storage_(std::move(h)) {}
  public:
    transition_body(oa::Pool& pool,word seed):pool_(&pool),storage_(std::in_place_type<list>,pool,seed) {}
    transition_body(transition_body&&)noexcept=default;
    transition_body& operator=(transition_body&&)noexcept=default;
    bool promote(word a,word b) {
        require(storage_.index()==0,"list promotion source");
        auto h=hierarchy::promote(std::get<list>(storage_),*pool_,a,b);if(!h)return false;
        storage_.template emplace<hierarchy>(std::move(*h));return true;
    }
    mutation_delta set_value(word k,word v){return std::visit([&](auto& table){return table.set_value(k,v);},storage_);}
    mutation_delta erase(word k){return std::visit([&](auto& table){return table.erase(k);},storage_);}
    template<class Consume> bool get(word k,Consume&& consume)const {
        return std::visit([&](const auto& table){return table.get(k,consume);},storage_);
    }
    template<class Visit> void each(Visit&& visit)const {std::visit([&](const auto& table){table.each(visit);},storage_);}
    memory footprint()const {auto m=std::visit([](const auto& table){return table.footprint();},storage_);
        // One variant body physically exists, including inactive alternative room.
        auto inner=std::visit([](const auto& table){return sizeof(table);},storage_);m.metadata+=sizeof(*this)-inner;return m;}
    std::optional<transition_body> clone()const {
        if(storage_.index()==0) {
            auto body=std::get<list>(storage_).clone();if(!body)return {};
            transition_body copy(*pool_,0);copy.storage_.template emplace<list>(std::move(*body));return copy;
        }
        auto h=std::get<hierarchy>(storage_).clone();if(!h)return {};return transition_body(*pool_,std::move(*h));
    }
};
template<unsigned K,container_kind Kind> void check(oa::Pool& pool) {
    small_table<K,Kind> source(pool,21);small_abseil<K,Kind> oracle(pool,0);
    for(unsigned i=0;i<128;++i){source.set_value(transition_key<K>(i),~word(0)-i);oracle.set_value(transition_key<K>(i),~word(0)-i);}
    if constexpr(Kind==container_kind::multimap)for(unsigned i=0;i<128;++i)for(unsigned v=0;v<i%9;++v) {
        source.set_value(transition_key<K>(i),v);oracle.set_value(transition_key<K>(i),v);
    }
    auto old=source.clone();require(bool(old),"old list clone");const auto original=snapshot(*old);
    // Deliberately tiny bucket geometry forces every row kind through Q/chains.
    using H=promoted_table<K,Kind,(Kind==container_kind::set?0:8),2,0,64,64>;
    auto target=H::promote(source,pool,71,91);require(bool(target)&&source.size()==0,"consumed list promotion");
    require(source.footprint().requested==0,"source list and bag ownership moved");
    require(snapshot(*target)==snapshot(oracle),"all-width kind transition oracle");
    bool bucket_seen=false,overflow_seen=false;
    auto inspect=[&](const small_journal& journal,mutation_delta delta) {
        require(journal.used && journal.storage[journal.used-1].type==small_event::kind::caller_delta,"final caller delta event");
        const auto published=journal.storage[journal.used-1].delta;
        require(published.keys==delta.keys&&published.values==delta.values,"published occupancy/value change");
        for(unsigned j=0;j<journal.used;++j) {
            const auto& e=journal.storage[j];
            if(e.type==small_event::kind::primary_write||e.type==small_event::kind::secondary_write) {
                require(e.storage && e.size==64 && e.offset%64==0,"whole bucket damage substitution");bucket_seen=true;
            }
            if(e.type==small_event::kind::overflow_write){require(e.storage&&e.block&&e.size,"key overflow identity");overflow_seen=true;}
        }
    };
    for(unsigned i=128;i<256;++i){std::array<small_event,64> events;small_journal journal{events};
        auto r=target->try_set(transition_key<K>(i),i,journal);require(bool(r),"observed promoted mutation");inspect(journal,*r);
        const auto expected=oracle.set_value(transition_key<K>(i),i);require(r->keys==expected.keys&&r->values==expected.values,"promoted insertion deltas");}
    // Sink refusal is checked independently of the implementation's bound.
    for(unsigned budget=0;budget<=32;++budget) {
        auto candidate=target->clone();require(bool(candidate),"budget private target");const auto before=snapshot(*candidate);
        std::array<small_event,64> events;small_journal journal{std::span<small_event>(events.data(),budget)};
        const auto changed=candidate->try_set(0,9187,journal);
        require(journal.used<=budget,"bounded mutation journal");
        if(!changed)require(snapshot(*candidate)==before,"effects refusal preserves map/bag contents");
        if(budget==0)require(!changed&&journal.used==0,"empty journal refuses before mutation");
        if(budget==32)require(bool(changed),"ample mutation journal succeeds");
    }
    for(unsigned i=0;i<256;i+=3){std::array<small_event,64> events;small_journal journal{events};
        auto r=target->erase(transition_key<K>(i),journal);require(bool(r),"observed promoted erase");inspect(journal,*r);
        auto e=oracle.erase(transition_key<K>(i));require(r->keys==e.keys&&r->values==e.values,"post-transition delete delta");}
    require(bucket_seen&&overflow_seen,"damage covers bucket and key overflow tiers");
    for(unsigned i=0;i<256;i+=3){target->set_value(transition_key<K>(i),~word(0));oracle.set_value(transition_key<K>(i),~word(0));}
    target->validate();require(snapshot(*target)==snapshot(oracle)&&snapshot(*old)==original,"transition chain reuse with old list");
    using Body=transition_body<K,Kind>;using Store=orbital::ObjectStore<publication_material<Body>>;
    catalog_resource resource;
    {
        Store store(resource);constexpr orbital::ObjectId id{95,12};
        auto publish=[&](Body& candidate,typename Store::Scope& basis) {
            auto owner=store.capture({&candidate},{});require(bool(owner),"transition capture");
            const std::array<typename Store::Change,1> changes{{{id,std::move(*owner)}}};
            typename Store::Prepared prepared(store);require(bool(store.prepare(basis,changes,prepared)),"transition prepare");
            require(bool(store.install(prepared)),"transition install");
        };
        Body first(pool,93);for(unsigned i=0;i<64;++i)first.set_value(transition_key<K>(i),i);
        if constexpr(Kind==container_kind::multimap)for(unsigned i=0;i<32;++i)first.set_value(0,i);
        const auto initial=snapshot(first);{typename Store::Scope basis(store);publish(first,basis);}
        typename Store::Scope old_list(store); // Must discover without an early bind.
        {
            typename Store::Scope basis(store);auto candidate=basis.bind(id).value->body.clone();require(bool(candidate),"retained transition clone");
            require(candidate->promote(8191,6151),"private hierarchy promotion");
            for(unsigned i=64;i<200;++i)candidate->set_value(transition_key<K>(i),i);publish(*candidate,basis);
        }
        typename Store::Scope old_hierarchy(store);const auto middle=snapshot(store.borrow().bind(id).value->body);
        {
            typename Store::Scope basis(store);auto candidate=basis.bind(id).value->body.clone();require(bool(candidate),"hierarchy clone");
            candidate->erase(0);candidate->set_value(0,~word(0));publish(*candidate,basis);
        }
        require(snapshot(old_list.bind(id).value->body)==initial,"old list discovers through hierarchy publication");
        require(snapshot(old_hierarchy.bind(id).value->body)==middle,"old hierarchy/bag discovery");
    }
    require(resource.live==0,"transition catalog balance");
}
template<unsigned K,container_kind Kind> void trial(oa::Pool& pool,unsigned n,unsigned rep,bool report=true) {
    using Body=transition_body<K,Kind>;using Baseline=small_abseil<K,Kind>;
    for(unsigned step=0;step<2;++step) {
        const auto arm=(step+rep)%2;
        std::optional<Body> candidate;std::optional<Baseline> baseline;word sum=0;
        auto timed=[&](auto&& f){auto begin=clock_type::now();f();return std::chrono::duration<double,std::nano>(clock_type::now()-begin).count();};
        const auto construct=timed([&]{if(arm)baseline.emplace(pool,0);else candidate.emplace(pool,83);});
        auto populate=[&](auto& body){for(unsigned i=0;i<n;++i) {
            body.set_value(transition_key<K>(i),transition_key<K>((i+1)%n));
            if constexpr(Kind==container_kind::multimap)if(i%3==0)body.set_value(transition_key<K>(i),~word(0)-i);
        }};
        const auto build=timed([&]{if(arm)populate(*baseline);else populate(*candidate);});
        auto expected=[&]{std::map<word,std::vector<word>> result;for(unsigned i=0;i<n;++i) {
            result[transition_key<K>(i)].push_back(Kind==container_kind::set?0:transition_key<K>((i+1)%n));
            if constexpr(Kind==container_kind::multimap)if(i%3==0)result[transition_key<K>(i)].push_back(~word(0)-i);
        }for(auto& [k,v]:result)std::sort(v.begin(),v.end());return result;}();
        require((arm?snapshot(*baseline):snapshot(*candidate))==expected,"timed initial transition contents");
        const auto before=arm?baseline->footprint():candidate->footprint();
        const auto transition=timed([&]{if(!arm)require(candidate->promote(1947,7183),"timed promotion");});
        const unsigned extra=K==8?std::min(16u,256-n):16;
        const auto grow=timed([&]{for(unsigned i=n;i<n+extra;++i)if(arm)baseline->set_value(transition_key<K>(i),i);else candidate->set_value(transition_key<K>(i),i);});
        for(unsigned i=n;i<n+extra;++i)expected[transition_key<K>(i)]={Kind==container_kind::set?0:i};
        require((arm?snapshot(*baseline):snapshot(*candidate))==expected,"post-transition insert contents");
        const auto after=arm?baseline->footprint():candidate->footprint();
        auto reads=[&](const auto& body){for(unsigned round=0;round<512;++round)for(unsigned i=0;i<n+extra;++i)
            sum+=body.get(transition_key<K>(i),[&](word v){sum+=v;});};
        if(arm)reads(*baseline);else reads(*candidate);
        const auto read=timed([&]{if(arm)reads(*baseline);else reads(*candidate);});
        auto serial_reads=[&](const auto& body){word at=0;for(unsigned round=0;round<512*n;++round) {
            if constexpr(Kind==container_kind::set) {
                const auto found=body.get(transition_key<K>(at),[](word){});
                at=(at+(found?1:7))%n;sum+=found;
            }else {bool first=true;require(body.get(at,[&](word value){if(first){at=value;first=false;}sum+=value;}),"post transition serial cycle");}
        }};
        if(arm)serial_reads(*baseline);else serial_reads(*candidate);
        const auto serial=timed([&]{if(arm)serial_reads(*baseline);else serial_reads(*candidate);});
        const auto release=timed([&]{if(arm)baseline.reset();else candidate.reset();});
        asm volatile(""::"r"(sum):"memory");
        auto emit=[&](const char* phase,double ns,unsigned operations,memory m) {
            if(!report)return;
            std::printf("%s,%u,%u,%u,%u,%s,%u,%.6f,%llu,%llu,%llu\n",arm?"abseil":"list_hierarchy",K,unsigned(Kind),n,rep,
                phase,operations,ns,(unsigned long long)m.requested,(unsigned long long)m.backing,(unsigned long long)m.metadata);
        };
        emit("construct",construct,1,before);emit("build",build,n,before);emit("promote",transition,1,after);
        if(extra)emit("insert_after",grow,extra,after);emit("read_after",read,512*(n+extra),after);
        emit(Kind==container_kind::set?"serial_caller_cycle_after":"serial_value_cycle_after",serial,512*n,after);
        emit("release",release,1,after);emit("lifetime_excluding_reads",construct+build+transition+grow+release,n+extra,after);
    }
}
template<unsigned K> void widths(oa::Pool& pool,bool checking,unsigned reps) {
    if(checking){check<K,container_kind::set>(pool);check<K,container_kind::map>(pool);check<K,container_kind::multimap>(pool);}
    else for(unsigned n:{16u,64u,128u,256u}) {
      trial<K,container_kind::set>(pool,n,0,false);trial<K,container_kind::map>(pool,n,0,false);trial<K,container_kind::multimap>(pool,n,0,false);
      for(unsigned rep=0;rep<reps;++rep) {
        trial<K,container_kind::set>(pool,n,rep);trial<K,container_kind::map>(pool,n,rep);trial<K,container_kind::multimap>(pool,n,rep);
      }
    }
}
} // namespace ht
int main(int argc,char** argv) {
    using namespace ht;auto mode=argc>1?std::string_view(argv[1]):"check";const bool checking=mode=="check";
    require(checking||mode=="bench","list transition mode");const auto reps=checking?1u:(argc>2?std::stoul(argv[2]):3u);pool_owner pool;
    if(!checking)std::puts("arm,K,kind,n,rep,phase,operations,ns,requested,owned_capacity,metadata");
    widths<8>(*pool.pool,checking,reps);widths<16>(*pool.pool,checking,reps);widths<24>(*pool.pool,checking,reps);widths<32>(*pool.pool,checking,reps);
    widths<40>(*pool.pool,checking,reps);widths<48>(*pool.pool,checking,reps);widths<56>(*pool.pool,checking,reps);widths<64>(*pool.pool,checking,reps);
    if(checking)std::puts("all-width set/map/multimap list-to-hierarchy transitions, forced overflow and retained-reader histories passed");
}

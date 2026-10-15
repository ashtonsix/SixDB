#include "compact_hash.h"
#include "small.h"
#include "ordinary_baseline.h"
#include <map>
#include <string_view>
#ifndef HT_STATE_KEY
#define HT_STATE_KEY 0
#endif

namespace ht::state_experiment {

struct stored_count {
    word value=0;
    operator word()const{return value;}
    void operator+=(std::int64_t n){value+=n;}
    void operator-=(word n){value-=n;}
    stored_count& operator=(word n){value=n;return *this;}
};
struct empty_count {
    void operator+=(std::int64_t){}
    void operator-=(word){}
    empty_count& operator=(word){return *this;}
};

// Controlled copy of the current small_table owner body. Full/compact policies
// share every allocation, primitive transfer, MM reservation and effect branch.
// Hash state and redundant value-count state are the only policy differences.
// The existing small_table is separately checked and timed as a control.
template<unsigned K,container_kind Kind,bool Compact>
class state_table {
    static constexpr unsigned ValueBytes=Kind==container_kind::set?0:8;
    static_assert(Kind!=container_kind::multimap || ValueBytes==8,
        "This bag-values control carries full64 values; list_view itself admits wider payloads");
  public:
    using view_type=list_view<K,Kind,ValueBytes>;
    using record=typename view_type::record;
    using hash_type=std::conditional_t<Compact,compact_permutation<K,0>,permutation<K,0>>;
    using bags_type=links<64,false,0>;
    static constexpr bool multimap=Kind==container_kind::multimap;
    static constexpr unsigned key_bits=K;
    static constexpr container_kind kind=Kind;
  private:
    struct empty_bags {};
    oa::Pool* pool_;
    list_owner<K,Kind,ValueBytes> list_;
    [[no_unique_address]] std::conditional_t<multimap,std::unique_ptr<bags_type>,empty_bags> bags_;
    [[no_unique_address]] std::conditional_t<Compact&&!multimap,empty_count,stored_count> values_;
    unsigned count_=0;
    hash_type hash_;
    template<class Effects,bool WholeList> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    apply(typename hash_type::parts h,const std::array<byte,ValueBytes>& value,Effects& effects) {
        int found=list_.bind().find(h.fingerprint,h.suffix,count_);
        constexpr unsigned maximum_events=5+(multimap?1+bags_type::reservation_event_bound:0);
        if(!effects_room(effects,maximum_events))return std::unexpected(mutation_error::effects);
        const auto previous_capacity=list_.capacity();
        if(found<0 && !list_.reserve(*pool_,count_,count_+1))return std::unexpected(mutation_error::capacity);
        if(list_.capacity()!=previous_capacity)effects.before({small_event::kind::list_rebound,this,
            list_.allocation.storage,0,list_.allocation.size});
        auto view=list_.bind();small_observer<Effects,WholeList> observed{effects,this,list_.allocation};
        auto result=[&]() {
            if constexpr(!multimap) {
                return view.mutate_at(h,count_,found,[&](const std::optional<record>& old)->std::optional<record> {
                    record next;next.payload=value;
                    next.value_delta=Kind==container_kind::map?!old:0;return next;
                },observed);
            } else {
                using reservation=typename bags_type::template reservation<decltype(observed)>;
                struct proposal:record {
                    std::optional<reservation> reserved;
                    std::array<word,2> append{};unsigned number=0;
                    void commit()noexcept {if(reserved)reserved->commit(std::span<const word>(append.data(),number));}
                };
                return view.mutate_at(h,count_,found,[&](const std::optional<record>& old)->std::optional<proposal> {
                    proposal next;next.value_delta=1;
                    if(!old) {next.payload=value;return next;}
                    if(!bags_) {
                        try {bags_=std::make_unique<bags_type>(*pool_);}catch(const std::bad_alloc&) {return {};}
                        effects.before({small_event::kind::bag_owner_created,this});
                    }
                    const auto head=old->tag?std::uint32_t(ht::get<4>(old->payload.data())):0;
                    const auto hint=old->tag?std::uint32_t(ht::get<4>(old->payload.data()+4)):0;
                    next.number=old->tag?1:2;
                    next.reserved=bags_->reserve_values(head,next.number,observed,hint);
                    if(!next.reserved)return {};
                    next.tag=1;put<8>(next.payload.data(),word(next.reserved->head_after())|
                        (word(next.reserved->insertion_after())<<32));
                    if(!old->tag)next.append[0]=ht::get<8>(old->payload.data());
                    next.append[next.number-1]=ht::get<8>(value.data());return next;
                },observed);
            }
        }();
        if(!result)return result;
        count_+=result->keys;values_+=result->values;
        effects.before({small_event::kind::caller_delta,this,nullptr,0,0,0,*result});return result;
    }
  public:
    explicit state_table(oa::Pool& pool,word seed):pool_(&pool),hash_(seed) {}
    state_table(state_table&&)noexcept=default;
    state_table& operator=(state_table&&)noexcept=default;
    state_table(const state_table&)=delete;
    unsigned size()const {return count_;}word value_count()const {
        if constexpr(Compact && Kind==container_kind::set)return 0;
        else if constexpr(Compact && Kind==container_kind::map)return count_;
        else return values_;
    }
    unsigned capacity()const {return list_.capacity();}
    template<class Effects,bool WholeList=false> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    set_bytes(word key,const std::array<byte,ValueBytes>& value,Effects& effects) {
        return apply<Effects,WholeList>(hash_.split(key),value,effects);
    }
    template<class Effects,bool WholeList=false> [[gnu::always_inline]] std::expected<mutation_delta,mutation_error>
    set_value(word key,word value,Effects& effects) requires(ValueBytes<=8) {
        std::array<byte,ValueBytes> bytes{};if constexpr(ValueBytes)std::memcpy(bytes.data(),&value,ValueBytes);
        return set_bytes<Effects,WholeList>(key,bytes,effects);
    }
    [[gnu::always_inline]] mutation_delta set_value(word key,word value) requires(ValueBytes<=8) {
        no_small_effects effects;auto result=set_value(key,value,effects);require(bool(result),"small insertion");return *result;
    }
    template<class Consume> [[gnu::always_inline]] bool get_bytes(word key,Consume&& consume)const {
        const auto h=hash_.split(key);auto v=list_.bind();const auto slot=v.find(h.fingerprint,h.suffix,count_);
        if(slot<0)return false;
        const auto r=v.read(slot);
        if constexpr(multimap) {
            if(r.tag)bags_->each(ht::get<4>(r.payload.data()),[&](word value,word){
                std::array<byte,8> bytes;std::memcpy(bytes.data(),&value,8);consume(bytes);
            });else consume(r.payload);
        }else consume(r.payload);
        return true;
    }
    template<class Consume> [[gnu::always_inline]] bool get(word key,Consume&& consume)const requires(ValueBytes<=8) {
        return get_bytes(key,[&](const auto& bytes){consume(ht::get<ValueBytes>(bytes.data()));});
    }
    template<class Visit> void each(Visit&& visit)const requires(ValueBytes<=8) {
        const auto v=list_.bind();for(unsigned i=0;i<count_;++i) {
            const auto key=hash_.invert({0,ht::get<view_type::suffix_bytes>(v.suffix_at(i)),v.data[i]});
            const auto r=v.read(i);
            if constexpr(multimap) {
                if(r.tag)bags_->each(ht::get<4>(r.payload.data()),[&](word value,word){visit(key,value);});
                else visit(key,ht::get<8>(r.payload.data()));
            }else visit(key,ht::get<ValueBytes>(r.payload.data()));
        }
    }
    template<class Visit> void each_record(Visit&& visit)const {
        auto v=list_.bind();for(unsigned i=0;i<count_;++i)
            visit(hash_.invert({0,ht::get<view_type::suffix_bytes>(v.suffix_at(i)),v.data[i]}),v.read(i));
    }
    struct transition_remainder {
        [[no_unique_address]] std::conditional_t<multimap,std::unique_ptr<bags_type>,empty_bags> bags;
        unsigned keys=0;word values=0;
    };
    // Destructive completion after every row is present in a private target.
    // Bag refs keep their owner/block/rank meaning; the target consumes this
    // remainder before becoming discoverable. A failed copy never calls this.
    transition_remainder release_for_transition()noexcept {
        transition_remainder result{std::move(bags_),count_,value_count()};
        list_={};count_=0;values_=0;return result;
    }
    template<class Effects> std::expected<mutation_delta,mutation_error> erase(word key,Effects& effects) {
        const auto h=hash_.split(key);auto view=list_.bind();const auto slot=view.find(h.fingerprint,h.suffix,count_);
        if(slot<0)return mutation_delta{};
        const auto old=view.read(slot);unsigned bag_links=0;word removed=Kind==container_kind::set?0:1;
        if constexpr(multimap)if(old.tag) {
            removed=0;for(auto ref=std::uint32_t(ht::get<4>(old.payload.data()));ref;ref=bags_->next(ref)) {
                ++bag_links;removed+=bags_->count(ref);
            }
        }
        if(!effects_room(effects,4+2*bag_links))return std::unexpected(mutation_error::effects);
        small_observer<Effects> observer{effects,this,list_.allocation};
        if constexpr(multimap)if(old.tag)bags_->release_values(ht::get<4>(old.payload.data()),observer);
        require(bool(view.erase(slot,count_,observer)),"preadmitted list erase");
        --count_;values_-=removed;mutation_delta result{-1,-std::int64_t(removed)};
        effects.before({small_event::kind::caller_delta,this,nullptr,0,0,0,result});return result;
    }
    mutation_delta erase(word key) {no_small_effects e;auto r=erase(key,e);require(bool(r),"small erase");return *r;}
    // This state-only discriminator uses scalar complete operations. The
    // existing packet consumer remains in small_batch.cpp; no compact packet
    // lowering or packet-width choice is implied here.
    template<class Effects> std::expected<mutation_delta,mutation_error> clear(Effects& effects) {
        if(!effects_room(effects,2))return std::unexpected(mutation_error::effects);
        mutation_delta delta{-int(count_),-std::int64_t(value_count())};
        effects.before({small_event::kind::owner_reset,this});
        list_=list_owner<K,Kind,ValueBytes>{};if constexpr(multimap)bags_.reset();count_=0;values_=0;
        effects.before({small_event::kind::caller_delta,this,nullptr,0,0,0,delta});return delta;
    }
    mutation_delta clear() {no_small_effects effects;return *clear(effects);}
    memory footprint()const {
        auto m=list_.footprint();m.metadata=sizeof(*this);
        if constexpr(multimap)if(bags_) {auto b=bags_->footprint();m.requested+=b.requested;m.backing+=b.backing;m.metadata+=b.metadata;}
        return m;
    }
    template<class Visit> void allocations(Visit&& visit)const {
        if(list_.allocation)visit(list_.allocation);
        if constexpr(multimap)if(bags_)bags_->allocations(visit);
    }
    // Whole-copy publication control: all bytes and owner state are private.
    // This is charged by the driver and does not prescribe future COW granularity.
    std::optional<state_table> clone()const requires(ValueBytes<=8) {
        state_table result(*pool_,0);result.hash_=hash_;
        if(count_&&!result.list_.reserve(*pool_,0,count_))return {};
        bool ok=true;each([&](word key,word value){if(ok){no_small_effects e;ok=bool(result.set_value(key,value,e));}});
        if(!ok)return {};return result;
    }
};

using oracle=std::map<word,std::vector<word>>;
template<class Table> oracle materialize(const Table& table) {
    oracle result;table.each([&](word k,word v){result[k].push_back(v);});
    for(auto& [k,values]:result){(void)k;std::sort(values.begin(),values.end());}
    return result;
}
template<unsigned K> word key(unsigned i) {return word(i)*0x9e3779b97f4a7c15ULL&mask(K);}
template<unsigned K,container_kind Kind,class Table> [[gnu::noinline]] void populate(Table& table,unsigned n) {
    // Three equal values exercise actual bags and leave an answer-dependent
    // XOR successor. The table cannot omit duplicate MM values.
    for(unsigned i=0;i<n;++i)for(unsigned j=0;j<(Kind==container_kind::multimap?3u:1u);++j)
        [[clang::always_inline]] (void)table.set_value(key<K>(i),key<K>((i+1)%n));
}
template<unsigned K,container_kind Kind,class Table> [[gnu::noinline]] word update_consume(Table& table,unsigned n) {
    for(unsigned round=0;round<2;++round)for(unsigned i=0;i<n;++i)
        [[clang::always_inline]] (void)table.set_value(key<K>(i),key<K>((i+2+round)%n));
    word digest=0,found=0,values=0;
    for(unsigned round=0;round<4;++round)for(unsigned i=0;i<2*n;++i) {
        bool hit;
        [[clang::always_inline]] hit=table.get(key<K>(i),[&](word value){digest+=value^key<K>(i);++values;});
        found+=hit;
    }
    return digest^(found<<32)^values;
}
template<unsigned K,container_kind Kind> word expected_digest(unsigned n) {
    word digest=0,found=0,values=0;
    for(unsigned round=0;round<4;++round)for(unsigned i=0;i<n;++i) {
        ++found;
        if constexpr(Kind==container_kind::set){digest+=key<K>(i);++values;}
        else if constexpr(Kind==container_kind::map){digest+=key<K>((i+3)%n)^key<K>(i);++values;}
        else {
            for(unsigned j=0;j<3;++j){digest+=key<K>((i+1)%n)^key<K>(i);++values;}
            digest+=key<K>((i+2)%n)^key<K>(i);digest+=key<K>((i+3)%n)^key<K>(i);values+=2;
        }
    }
    return digest^(found<<32)^values;
}
template<unsigned K,container_kind Kind,class Table> [[gnu::noinline]] double lifecycle(
        oa::Pool& pool,unsigned n,std::span<const word> entropy) {
    word result=0;const auto begin=clock_type::now();
    {
        std::vector<Table> tables;tables.reserve(entropy.size());
        for(auto seed:entropy){tables.emplace_back(pool,seed);populate<K,Kind>(tables.back(),n);}
        for(auto& table:tables)result+=update_consume<K,Kind>(table,n);
    }
    const auto ns=std::chrono::duration<double,std::nano>(clock_type::now()-begin).count();
    require(result==entropy.size()*expected_digest<K,Kind>(n),"state lifecycle digest");
    asm volatile(""::"r"(result):"memory");return ns;
}

struct node {word key;std::array<word,2> next;};
template<unsigned K> std::vector<node> membership_graph(unsigned n) {
    std::vector<node> graph(2*n);
    for(unsigned i=0;i<n;++i) {
        graph[2*i]={key<K>(i),{(2*i+3)%(2*n),2*i+1}};
        graph[2*i+1]={key<K>(n+i),{(2*i+2)%(2*n),(2*i+3)%(2*n)}};
    }
    return graph;
}
template<unsigned K,container_kind Kind,class Table> [[gnu::noinline]] double serial(
        const Table& table,unsigned n,std::span<const node> nodes,unsigned steps) {
    word current=0,digest=0,hits=0,values=0;const auto begin=clock_type::now();
    for(unsigned i=0;i<steps;++i) {
        bool found;
        if constexpr(Kind==container_kind::set) {
            const auto& query=nodes[current];
            [[clang::always_inline]] found=table.get(query.key,[&](word v){digest+=v;++values;});
            current=query.next[found];
        } else {
            word next=0;
            [[clang::always_inline]] found=table.get(current,[&](word v){next^=v;++values;});
            current=next;
        }
        hits+=found;digest+=current;
    }
    const auto ns=std::chrono::duration<double,std::nano>(clock_type::now()-begin).count();
    word expected=0;
    for(unsigned i=0;i<steps;++i)expected+=Kind==container_kind::set?(i+1)%(2*n):key<K>((i+1)%n);
    require(digest==expected&&hits==(Kind==container_kind::set?steps/2:steps)&&
        values==(Kind==container_kind::multimap?3*steps:hits),"state dependent answer graph");
    asm volatile(""::"r"(digest):"memory");return ns;
}

template<unsigned K,unsigned A> void check_hash_geometry() {
    for(unsigned seed_index=0;seed_index<32;++seed_index) {
        const word entropy=seed_index==0?0:seed_index==1?~word(0):mix(word(seed_index)*0x41954351ba39db1fULL);
        permutation<K,A> reference(entropy);compact_permutation<K,A> candidate(entropy);
        const auto expanded=candidate.expanded();
        const unsigned samples=K==8?256:512;
        for(unsigned i=0;i<samples;++i) {
            word x=K==8?i:i==0?0:i==1?~word(0):mix(word(i)*0x876741136134ab91ULL+entropy);
            const auto a=reference.split(x),b=candidate.split(x),c=expanded.split(x);
            require(a.bucket==b.bucket&&a.suffix==b.suffix&&a.fingerprint==b.fingerprint,"exact compact permutation");
            require(a.bucket==c.bucket&&a.suffix==c.suffix&&a.fingerprint==c.fingerprint,
                "expanded packet facts preserve the stored permutation");
            require(candidate.invert(b)==(x&mask(K))&&reference.invert(b)==candidate.invert(a),"cross inverse reconstruction");
        }
        std::array<word,8> keys{0,mask(K),~word(0),entropy,~entropy,1,word(1)<<63,mix(entropy)};
        auto packet=split_packet<8>(expanded,std::span<const word,8>(keys));
        for(unsigned i=0;i<8;++i) {
            const auto expected=reference.split(keys[i]);
            require(packet.bucket[i]==expected.bucket&&packet.suffix[i]==expected.suffix&&
                packet.fingerprint[i]==expected.fingerprint,"compact owner expands into exact SIMD packet lanes");
        }
    }
}
template<unsigned K,container_kind Kind> void check_owner(oa::Pool& pool) {
    for(word seed:{word(0),~word(0),word(8719)}) {
        small_table<K,Kind> original(pool,seed);
        state_table<K,Kind,false> full(pool,seed);state_table<K,Kind,true> compact(pool,seed);
        std::map<word,std::vector<word>> expected;
        for(unsigned i=0;i<64;++i)for(unsigned repeat=0;repeat<(Kind==container_kind::multimap?5u:2u);++repeat) {
            const word k=key<K>(i),v=repeat%2?~word(0):i;
            const auto a=original.set_value(k,v),b=full.set_value(k,v),c=compact.set_value(k,v);
            require(a.keys==b.keys&&a.keys==c.keys&&a.values==b.values&&a.values==c.values,"state mutation delta");
            if constexpr(Kind==container_kind::set)expected[k]={0};
            else if constexpr(Kind==container_kind::map)expected[k]={v};
            else expected[k].push_back(v);
        }
        for(auto& [k,v]:expected){(void)k;std::sort(v.begin(),v.end());}
        require(materialize(original)==expected&&materialize(full)==expected&&materialize(compact)==expected,"state owner oracle");
        auto retained=compact.clone();require(bool(retained),"state retained clone");
        for(unsigned i=0;i<64;i+=2) {
            const auto a=original.erase(key<K>(i)),b=full.erase(key<K>(i)),c=compact.erase(key<K>(i));
            require(a.keys==b.keys&&a.keys==c.keys&&a.values==b.values&&a.values==c.values,"state erase delta");
        }
        require(materialize(original)==materialize(full)&&materialize(original)==materialize(compact)&&
            materialize(*retained)==expected,"state retained owner and swap erase");
        require(original.value_count()==full.value_count()&&original.value_count()==compact.value_count(),"derived value count");
        small_journal blocked{{}};const auto saved=materialize(compact);
        require(!compact.set_value(0,91,blocked)&&materialize(compact)==saved,"state publication refusal");
        const auto a=original.clear(),b=full.clear(),c=compact.clear();
        require(a.keys==b.keys&&a.keys==c.keys&&a.values==b.values&&a.values==c.values&&compact.value_count()==0,"state clear delta");
    }
}
template<unsigned K,container_kind Kind,class Table> memory inventory(oa::Pool& pool,unsigned n,bool updated) {
    Table table(pool,8719);populate<K,Kind>(table,n);if(updated)(void)update_consume<K,Kind>(table,n);
    return table.footprint();
}
template<unsigned K,container_kind Kind,class Table> void arm(
        oa::Pool& pool,unsigned n,unsigned owners,unsigned rep,const char* label,bool timed) {
    std::vector<word> entropy(owners);for(unsigned i=0;i<owners;++i)entropy[i]=mix(8719+i);
    const auto m=inventory<K,Kind,Table>(pool,n,true);
    const auto life=lifecycle<K,Kind,Table>(pool,n,entropy);
    auto emit=[&](const char* stage,double ns,unsigned operations,memory usage,bool array) {
        if(timed)std::printf("%s,%u,%u,%u,%u,%u,%s,%u,%.6f,%zu,%llu,%llu,%llu\n",label,K,unsigned(Kind),n,owners,rep,
            stage,operations,ns,sizeof(Table),(unsigned long long)(usage.requested*owners),
            (unsigned long long)(usage.backing*owners),(unsigned long long)(usage.metadata*owners+(array?sizeof(std::vector<Table>):0)));
    };
    emit("lifecycle",life,owners*(n*(Kind==container_kind::multimap?3:1)+2*n+8*n),m,true);
    if(n && owners==1) {
        Table table(pool,8719);populate<K,Kind>(table,n);auto nodes=membership_graph<K>(n);
        const auto fresh=inventory<K,Kind,Table>(pool,n,false);
        emit("serial",serial<K,Kind>(table,n,nodes,4096),4096,fresh,false);
    }
}
template<unsigned K,container_kind Kind> void campaign(oa::Pool& pool,unsigned reps,bool timed) {
    check_owner<K,Kind>(pool);
    for(unsigned n:{0u,1u,4u,16u,64u})for(unsigned owners:{1u,64u})
    for(unsigned rep=0;rep<(timed?reps:1u);++rep)for(unsigned turn=0;turn<5;++turn) {
        switch((rep+turn)%5) {
#define HT_STATE_CASE(Index,Type,Name) case Index:arm<K,Kind,Type>(pool,n,owners,rep,Name,timed);break
            using original=small_table<K,Kind>;using full=state_table<K,Kind,false>;
            using compact=state_table<K,Kind,true>;using ordinary=ordinary_abseil<K,Kind,false>;
            using exact=ordinary_abseil<K,Kind,true>;
            HT_STATE_CASE(0,original,"integrated");HT_STATE_CASE(1,full,"full_state_control");
            HT_STATE_CASE(2,compact,"compact_state");HT_STATE_CASE(3,ordinary,"abseil");
            HT_STATE_CASE(4,exact,"abseil_exact");
#undef HT_STATE_CASE
        }
    }
}
template<unsigned K> void width(oa::Pool& pool,unsigned reps,bool timed) {
    lanes<K>([]<unsigned A>(std::integral_constant<unsigned,A>){check_hash_geometry<K,A>();});
    campaign<K,container_kind::set>(pool,reps,timed);
    campaign<K,container_kind::map>(pool,reps,timed);
    campaign<K,container_kind::multimap>(pool,reps,timed);
}
} // namespace ht::state_experiment

int main(int argc,char** argv) {
    using namespace ht;using namespace ht::state_experiment;
    const std::string_view mode=argc>1?argv[1]:"check";require(mode=="check"||mode=="bench","compact state mode");
    const unsigned reps=argc>2?std::stoul(argv[2]):3;require(reps>0,"compact state repetitions");
    const bool timed=mode=="bench";pool_owner pool;
    if(timed)std::puts("arm,K,kind,keys_per_owner,owners,rep,stage,operations,elapsed_ns,owner_size,requested,owned_capacity,metadata");
#if HT_STATE_KEY
    width<HT_STATE_KEY>(*pool.pool,reps,timed);
#else
    width<8>(*pool.pool,reps,timed);width<16>(*pool.pool,reps,timed);
    width<24>(*pool.pool,reps,timed);width<32>(*pool.pool,reps,timed);
    width<40>(*pool.pool,reps,timed);width<48>(*pool.pool,reps,timed);
    width<56>(*pool.pool,reps,timed);width<64>(*pool.pool,reps,timed);
#endif
    if(!timed)std::printf("compact state: K=%u (0=all), every legal address width, all container kinds and retained owners passed\n",HT_STATE_KEY);
}

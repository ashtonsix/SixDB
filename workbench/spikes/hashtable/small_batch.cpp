#include "small.h"
#include "ordinary_baseline.h"

#include <map>
#include <string_view>
#ifndef HT_SMALL_KEY
#define HT_SMALL_KEY 0
#endif

namespace ht::batch_receipt {

struct packet {
    std::array<word,8> keys{},values{};
    unsigned active=0;
};
struct trace {
    std::vector<packet> packets;
    unsigned occupied=0,operations=0;
    void append(word key,word value,bool active=true) {
        if(occupied%8==0)packets.emplace_back();
        auto& p=packets.back();auto lane=occupied++%8;
        p.keys[lane]=key;p.values[lane]=value;
        if(active){p.active|=1u<<lane;++operations;}
    }
};
using oracle=std::map<word,std::vector<word>>;
struct answer {
    word queries=0,hits=0,values=0,digest=0;
    bool operator==(const answer&)const=default;
};
template<unsigned K> word key(unsigned n) {return (word(n)*0x9e3779b97f4a7c15ULL)&mask(K);}
word value(unsigned n) {
    if(n%7==0)return ~word(0);
    if(n%7==1)return 0;
    return mix(word(n)+0xd713621c83455129ULL);
}
template<container_kind Kind> void expected_write(oracle& out,word k,word v) {
    if constexpr(Kind==container_kind::set)out[k]={0};
    else if constexpr(Kind==container_kind::map)out[k]={v};
    else out[k].push_back(v);
}
template<container_kind Kind> void apply_expected(oracle& out,const trace& t) {
    for(const auto& p:t.packets)for(unsigned i=0;i<8;++i)
        if(p.active&(1u<<i))expected_write<Kind>(out,p.keys[i],p.values[i]);
}
void canonicalize(oracle& out) {for(auto& [k,v]:out){(void)k;std::sort(v.begin(),v.end());}}
template<class Table> oracle materialize(const Table& table) {
    oracle out;table.each([&](word k,word v){out[k].push_back(v);});canonicalize(out);return out;
}
struct workload {
    trace build,write,read;
    oracle initial,final;
    answer expected;
};
template<unsigned K,container_kind Kind> workload make_workload(unsigned n,bool extended) {
    require(n>0&&n<=240,"batch workload size");
    workload w;
    // A permutation of the initial indices changes insertion order without
    // changing population. No per-contender randomization changes the history.
    unsigned serial=0;
    for(unsigned i=0;i<n;++i) {
        unsigned index=(i*17)%n; // n={1,16,128,240};17 is coprime to all four.
        unsigned copies=1;
        if constexpr(Kind==container_kind::multimap)
            if(extended)copies=index%3==0?12:index%3==1?4:1;
        for(unsigned j=0;j<copies;++j)w.build.append(key<K>(index),value(serial++));
    }
    apply_expected<Kind>(w.initial,w.build);canonicalize(w.initial);
    require(w.initial.size()==n,"independent initial key population");
    // Exactly sixteen absent keys cross the same list-growth boundaries in
    // every contender. Later duplicate pairs and inactive positions are common.
    for(unsigned i=0;i<16;++i)w.write.append(key<K>(n+i),value(serial++));
    constexpr std::array masks{0xffu,0xdbu,0x01u,0x80u,0u,0x55u,0xaau,0xf0u};
    for(unsigned p=0;p<16+(n+7)/8;++p)for(unsigned i=0;i<8;++i) {
        const auto index=(p*3+i/2)%n;
        w.write.append(key<K>(index),value(serial++),masks[p%masks.size()]&(1u<<i));
    }
    w.final=w.initial;apply_expected<Kind>(w.final,w.write);canonicalize(w.final);
    require(w.final.size()==n+16,"independent final key population");
    const unsigned present=n+16;
    for(unsigned round=0;round<4;++round)for(unsigned p=0;p<(present+7)/8+8;++p)
    for(unsigned i=0;i<8;++i) {
        bool absent=(p+i)%3==0 && (K!=8 || present<256);
        unsigned index=(p*5+i+round*11)%present;
        if(absent) {
            const unsigned available=K==8?256-present:256;
            index=present+(p*7+i)%available;
        }
        const bool active=masks[(p+round)%masks.size()]&(1u<<i);
        w.read.append(key<K>(index),0,active);
        if(active) {
            ++w.expected.queries;
            auto found=w.final.find(key<K>(index));
            if(found!=w.final.end()) {
                ++w.expected.hits;
                for(auto v:found->second){++w.expected.values;w.expected.digest+=v^key<K>(index);}
            }
        }
    }
    require(w.expected.queries==w.read.operations,"independent active request count");
    return w;
}

// One call per complete trace is intentional; every per-key operation and
// batch wrapper is explicitly fused into these auditable roots. SIMD hashing
// comes from split_packet, with compiler loop/SLP vectorization disabled.
template<unsigned N,class Table> [[gnu::noinline]] unsigned ingest(Table& table,const trace& t) {
    unsigned operations=0;
    for(const auto& p:t.packets)for(unsigned first=0;first<8;first+=N) {
        const unsigned active=(p.active>>first)&((1u<<N)-1);
        if(!active)continue;
        if constexpr(N==1) {
            [[clang::always_inline]] (void)table.set_value(p.keys[first],p.values[first]);
        } else {
            unsigned processed;
            const std::span<const word,N> keys(p.keys.data()+first,N),values(p.values.data()+first,N);
            // Attribute the actual effects overload, not just its forwarding
            // no-effects wrapper. The identical annotation covers Abseil's
            // actual batch body. The no-op sink changes no logical operation.
            if constexpr(requires(no_small_effects& effects){table.template setN<N>(keys,values,active,effects);}) {
                no_small_effects effects;
                [[clang::always_inline]] processed=table.template setN<N>(keys,values,active,effects);
            } else [[clang::always_inline]] processed=table.template setN<N>(keys,values,active);
            require(processed==active,"preadmitted timed batch write");
        }
        operations+=std::popcount(active);
    }
    return operations;
}
template<unsigned N,class Table> [[gnu::noinline]] answer consume(const Table& table,const trace& t) {
    answer out;
    for(const auto& p:t.packets)for(unsigned first=0;first<8;first+=N) {
        const unsigned active=(p.active>>first)&((1u<<N)-1);
        if(!active)continue;
        if constexpr(N==1) {
            bool found;
            [[clang::always_inline]] found=table.get(p.keys[first],[&](word v){
                ++out.values;out.digest+=v^p.keys[first];
            });
            out.hits+=found;
        } else {
            unsigned found;
            [[clang::always_inline]] found=table.template getN<N>(
                std::span<const word,N>(p.keys.data()+first,N),active,[&](auto lane,word v){
                    ++out.values;out.digest+=v^p.keys[first+lane];
                });
            require(!(found&~active),"inactive read lane reported found");
            out.hits+=std::popcount(found);
        }
        out.queries+=std::popcount(active);
    }
    return out;
}

struct inventory {memory initial,final;};
template<unsigned N,class Table> inventory observe(oa::Pool& pool,const workload& w) {
    Table table(pool,8719);
    require(ingest<N>(table,w.build)==w.build.operations,"initial operation count");
    require(materialize(table)==w.initial,"initial independent oracle");
    inventory result{table.footprint(),{}};
    require(ingest<N>(table,w.write)==w.write.operations,"write operation count");
    require(materialize(table)==w.final,"final independent oracle");
    require(consume<N>(table,w.read)==w.expected,"complete read answer oracle");
    result.final=table.footprint();
    // Per-lane values validate routing, including duplicate requests and holes;
    // the timed answer deliberately uses a cheaper order-independent checksum.
    for(const auto& p:w.read.packets)for(unsigned first=0;first<8;first+=N) {
        unsigned active=(p.active>>first)&((1u<<N)-1),expected_mask=0;
        std::array<std::vector<word>,N> actual;
        unsigned found=0;
        if constexpr(N==1) {
            if(active)found=table.get(p.keys[first],[&](word v){actual[0].push_back(v);});
        } else found=table.template getN<N>(std::span<const word,N>(p.keys.data()+first,N),active,
            [&](auto i,word v){actual[i].push_back(v);});
        for(unsigned i=0;i<N;++i) {
            std::sort(actual[i].begin(),actual[i].end());
            std::vector<word> expected;
            if(active&(1u<<i))if(auto it=w.final.find(p.keys[first+i]);it!=w.final.end()) {
                expected_mask|=1u<<i;expected=it->second;
            }
            require(actual[i]==expected,"per-lane bag values/inactive routing");
        }
        require(found==expected_mask,"per-lane membership mask");
    }
    return result;
}

template<unsigned N,class Table> [[gnu::noinline]] double lifecycle(
        oa::Pool& pool,const workload& w,unsigned histories) {
    answer result;word written=0;
    const auto begin=clock_type::now();
    for(unsigned history=0;history<histories;++history) {
        Table table(pool,8719);
        written+=ingest<N>(table,w.build);
        written+=ingest<N>(table,w.write);
        const auto one=consume<N>(table,w.read);
        result.queries+=one.queries;result.hits+=one.hits;
        result.values+=one.values;result.digest+=one.digest;
    } // Actual owner destruction, including every remaining multimap bag.
    const auto elapsed=std::chrono::duration<double,std::nano>(clock_type::now()-begin).count();
    const answer expected{w.expected.queries*histories,w.expected.hits*histories,
        w.expected.values*histories,w.expected.digest*histories};
    require(written==word(w.build.operations+w.write.operations)*histories && result==expected,
        "timed repeated lifecycle oracle");
    asm volatile(""::"r"(result.digest):"memory");
    return elapsed/histories;
}

template<unsigned K,container_kind Kind,unsigned N> void refusal(oa::Pool& pool) {
    std::array<word,N> keys{},values{};
    for(unsigned i=0;i<N;++i){keys[i]=key<K>(i/2);values[i]=value(90+i);}
    const unsigned active=N==4?0b1011u:0b11011011u;
    bool saw_partial=false,saw_full=false;
    // Vary actual sink capacity instead of copying the implementation's event
    // bound. Every outcome must be an ordered active prefix, and only that
    // prefix may change the independent logical state.
    for(unsigned budget=0;budget<=128;++budget) {
        if(budget>32&&budget!=48&&budget!=64&&budget!=96&&budget!=128)continue;
        small_table<K,Kind> table(pool,781);
        for(unsigned i=0;i<8;++i)table.set_value(key<K>(i),value(i));
        auto expected=materialize(table);
        std::array<small_event,128> events;
        small_journal journal{std::span<small_event>(events.data(),budget)};
        const auto processed=table.template setN<N>(keys,values,active,journal);
        require(!(processed&~active) && journal.used<=budget,"effect result escaped its admitted masks/capacity");
        bool refused=false;
        for(unsigned i=0;i<N;++i)if(active&(1u<<i)) {
            if(processed&(1u<<i)) {
                require(!refused,"effect refusal did not stop at first refused active lane");
                expected_write<Kind>(expected,keys[i],values[i]);
            } else refused=true;
        }
        canonicalize(expected);
        require(materialize(table)==expected,"partial effect refusal altered later/inactive lanes");
        saw_partial|=processed&&processed!=active;saw_full|=processed==active;
        if(!budget)require(processed==0,"empty effect sink admitted a mutation");
        small_journal none{{}};
        require(table.template setN<N>(keys,values,0,none)==0 && materialize(table)==expected,
                "all-inactive writes required effects or mutated contents");
    }
    require(saw_partial&&saw_full,"effect budgets did not exercise partial and complete batches");
    if constexpr(K>8) {
        small_table<K,Kind> table(pool,781);
        for(unsigned i=0;i<254;++i)table.set_value(key<K>(i),value(i));
        auto expected=materialize(table);
        keys[0]=key<K>(254);keys[1]=key<K>(255);keys[2]=key<K>(256);keys[3]=key<K>(0);
        for(unsigned i=4;i<N;++i)keys[i]=key<K>(i);
        const unsigned all=(1u<<N)-1;
        const auto processed=table.template setN<N>(keys,values,all);
        require(processed==3,"capacity refusal must stop before later existing-key writes");
        expected_write<Kind>(expected,keys[0],values[0]);expected_write<Kind>(expected,keys[1],values[1]);
        canonicalize(expected);require(materialize(table)==expected,"capacity refusal changed remaining lanes");
    }
}

template<unsigned K,container_kind Kind,class Table,unsigned N> void arm(
        oa::Pool& pool,const workload& w,unsigned n,bool extended,unsigned repetition,
        const char* name,bool timed,unsigned histories) {
    const auto memory=observe<N,Table>(pool,w);
    if(!timed)return;
    auto ns=lifecycle<N,Table>(pool,w,histories);
    auto print_memory=[](ht::memory m){
        std::printf(",%llu,%llu,%llu",(unsigned long long)m.requested,
            (unsigned long long)m.backing,(unsigned long long)m.metadata);
    };
    std::printf("%s,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu,%.6f,%llu",name,K,unsigned(Kind),
        n,unsigned(extended),N,repetition,w.build.operations,w.write.operations,
        (unsigned long long)w.expected.queries,(unsigned long long)w.expected.hits,
        (unsigned long long)w.expected.values,ns,(unsigned long long)w.expected.digest);
    print_memory(memory.initial);print_memory(memory.final);std::printf(",%u\n",histories);
}
template<unsigned K,container_kind Kind> void campaign(oa::Pool& pool,unsigned reps,bool timed,unsigned histories) {
    refusal<K,Kind,4>(pool);refusal<K,Kind,8>(pool);
    for(unsigned n:{1u,16u,128u,240u})for(unsigned history=0;history<(Kind==container_kind::multimap?2u:1u);++history) {
        auto w=make_workload<K,Kind>(n,history);
        for(unsigned r=0;r<(timed?reps:1u);++r)for(unsigned turn=0;turn<9;++turn) {
            switch((turn+r)%9) {
#define HT_BATCH_ARM(Index,Table,N,Name) \
                case Index:arm<K,Kind,Table,N>(pool,w,n,history,r,Name,timed,histories);break
                using compact=small_table<K,Kind>;
                using ordinary=ordinary_abseil<K,Kind,false>;
                using exact=ordinary_abseil<K,Kind,true>;
                HT_BATCH_ARM(0,compact,1,"compact_list");
                HT_BATCH_ARM(1,compact,4,"compact_list");
                HT_BATCH_ARM(2,compact,8,"compact_list");
                HT_BATCH_ARM(3,ordinary,1,"abseil");
                HT_BATCH_ARM(4,ordinary,4,"abseil");
                HT_BATCH_ARM(5,ordinary,8,"abseil");
                HT_BATCH_ARM(6,exact,1,"abseil_exact");
                HT_BATCH_ARM(7,exact,4,"abseil_exact");
                HT_BATCH_ARM(8,exact,8,"abseil_exact");
#undef HT_BATCH_ARM
            }
        }
    }
}
template<unsigned K> void width(oa::Pool& pool,unsigned reps,bool timed,unsigned histories) {
    campaign<K,container_kind::set>(pool,reps,timed,histories);
    campaign<K,container_kind::map>(pool,reps,timed,histories);
    campaign<K,container_kind::multimap>(pool,reps,timed,histories);
}
} // namespace ht::batch_receipt

int main(int argc,char** argv) {
    using namespace ht;using namespace ht::batch_receipt;
    const std::string_view mode=argc>1?argv[1]:"check";
    require(mode=="check"||mode=="bench","small batch mode");
    const unsigned reps=argc>2?std::stoul(argv[2]):3;
    require(reps>0,"small batch repetitions");pool_owner pool;
    const unsigned histories=argc>3?std::stoul(argv[3]):1;
    require(histories>0&&histories<=65536,"small batch histories per timer");
    const bool timed=mode=="bench";
    if(timed)std::puts("arm,K,kind,initial_keys,extended_history,N,rep,build_operations,write_operations,queries,hits,values,lifecycle_ns,digest,initial_requested,initial_owned_capacity,initial_metadata,final_requested,final_owned_capacity,final_metadata,lifecycle_histories");
#if HT_SMALL_KEY
    width<HT_SMALL_KEY>(*pool.pool,reps,timed,histories);
#else
    width<8>(*pool.pool,reps,timed,histories);width<16>(*pool.pool,reps,timed,histories);
    width<24>(*pool.pool,reps,timed,histories);width<32>(*pool.pool,reps,timed,histories);
    width<40>(*pool.pool,reps,timed,histories);width<48>(*pool.pool,reps,timed,histories);
    width<56>(*pool.pool,reps,timed,histories);width<64>(*pool.pool,reps,timed,histories);
#endif
    if(!timed)std::printf("matched batch consumers: key selection %u (0=all), all kinds, scalar/4/8, identical histories, ordinary/exact Abseil, per-lane answers and prefix refusals passed\n",HT_SMALL_KEY);
}

#include "concurrent_baselines.h"

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

// The same source is compiled independently for each width, bounding compiler
// memory. K=0 is only the driver; no timing is run by CTest.
#ifndef HT_CONCURRENT_K
#define HT_CONCURRENT_K 0
#endif
#define HT_JOIN_(a,b) a##b
#define HT_JOIN(a,b) HT_JOIN_(a,b)

namespace cb=ht::concurrent_baselines;
using word=cb::word;
using oracle=std::map<word,std::vector<word>>;

#if HT_CONCURRENT_K
namespace {
[[noreturn]] void fail(const char* what) {
    std::cerr<<"concurrent baseline K="<<HT_CONCURRENT_K<<": "<<what<<'\n'; std::abort();
}
void require(bool value,const char* what) { if(!value) fail(what); }
word mix(word x) { x^=x>>30; x*=0xbf58476d1ce4e5b9ull; x^=x>>27; return x; }
word make_key(word index) {
    // An odd affine map is used only for distinct test keys, not table hashing.
    return (index*0x9e3779b97f4a7c15ull+0xd1b54a32d192ed03ull)&cb::domain_mask<HT_CONCURRENT_K>;
}
template<cb::kind Kind> void expected_add(oracle& expected,word key,word value) {
    auto& values=expected[key];
    if constexpr(Kind==cb::kind::set) values={0};
    else if constexpr(Kind==cb::kind::map) {
        if(values.empty()) values.push_back(value); else values.front()+=value;
    } else values.push_back(value);
}
void normalize(oracle& expected) {
    for(auto& [key,values]:expected) { (void)key; std::sort(values.begin(),values.end()); }
}
template<class Table> void verify(const Table& table,oracle expected) {
    normalize(expected);
    oracle observed;
    table.each_quiescent([&](word key,word value){observed[key].push_back(value);});
    normalize(observed);
    require(observed==expected,"independent full-content oracle");
    require(table.size_quiescent()==expected.size(),"key cardinality");
    for(const auto& [key,values]:expected) {
        std::vector<word> protected_values,quiescent_values;
        require(table.consume(key,[&](word v){protected_values.push_back(v);}),"protected hit");
        require(table.template consume<true>(key,[&](word v){quiescent_values.push_back(v);}),"quiescent hit");
        std::sort(protected_values.begin(),protected_values.end());
        std::sort(quiescent_values.begin(),quiescent_values.end());
        require(protected_values==values && quiescent_values==values,"all values consumed");
    }
}
template<class Work> void parallel(unsigned workers,Work&& work) {
    std::barrier start(static_cast<std::ptrdiff_t>(workers));
    std::vector<std::jthread> threads;
    for(unsigned w=0;w<workers;++w) threads.emplace_back([&,w]{start.arrive_and_wait();work(w);});
}

template<class Table> void check() {
    constexpr auto Kind=Table::container_kind;
    constexpr unsigned workers=4,n=HT_CONCURRENT_K==8?173:769,per_worker=1024;
    Table table;
    require(table.size_quiescent()==0,"empty owner");
    unsigned callbacks=0;
    require(!table.consume(0,[&](word){++callbacks;}) && callbacks==0,"empty miss callback");
    oracle expected;
    std::array<std::vector<std::pair<word,word>>,workers> input;
    for(unsigned w=0;w<workers;++w) for(unsigned i=0;i<per_worker;++i) {
        const auto index=(i%8==0)?0:unsigned(mix(word(w)*per_worker+i)%n);
        const word value=1+(i%13); // duplicates and skew; no overflow in concurrent read checks
        input[w].emplace_back(make_key(index),value);
        expected_add<Kind>(expected,make_key(index),value);
    }
    // Natural concurrent growth from empty. Reads overlap inserts and bag
    // reallocations, but their callback only inspects protected values.
    parallel(workers,[&](unsigned w) {
        for(unsigned i=0;i<input[w].size();++i) {
            const auto [key,value]=input[w][i]; table.add(key,value);
            if(i%32==0) {
                unsigned seen=0;
                require(table.consume(key,[&](word v) {
                    ++seen;
                    if constexpr(Kind==cb::kind::set) require(v==0,"set value");
                    else if constexpr(Kind==cb::kind::multimap) require(v>=1 && v<=13,"protected bag value");
                    else require(v>0,"protected aggregate");
                }) && seen>0,"read after this worker's insert");
            }
        }
    });
    verify(table,expected);
    table.reserve(n*2); verify(table,expected);
    const word missing=make_key(n+1);
    require(!expected.contains(missing),"test miss is distinct");
    require(!table.consume(missing,[](word){fail("miss callback");}),"missing key");
    if constexpr(Kind==cb::kind::multimap) {
        require(!table.erase_one(missing,1),"erase missing bag");
        // One worker per key; overlapping protected writes on different keys.
        std::vector<std::pair<word,word>> removals;
        for(const auto& [key,values]:expected) removals.emplace_back(key,values.front());
        parallel(workers,[&](unsigned w) {
            for(unsigned i=w;i<removals.size();i+=workers)
                require(table.erase_one(removals[i].first,removals[i].second),"erase-one");
        });
        for(const auto& [key,value]:removals) {
            auto& values=expected.at(key);
            values.erase(std::find(values.begin(),values.end(),value));
            if(values.empty()) expected.erase(key);
        }
        verify(table,expected);
    }
    // Ordered phases define erase/overwrite semantics. No arbitrary concurrent
    // last-writer result is inferred from a serial input stream.
    std::vector<word> deletes;
    for(const auto& [key,values]:expected) { (void)values; if(deletes.size()<32) deletes.push_back(key); }
    parallel(workers,[&](unsigned w) {
        for(unsigned i=w;i<deletes.size();i+=workers) require(table.erase_key(deletes[i]),"erase-key");
    });
    for(word key:deletes) expected.erase(key);
    verify(table,expected);
    if constexpr(Kind==cb::kind::map) {
        for(unsigned i=0;i<32;++i) {
            const word key=make_key(i),value=~word{0}-i;
            table.assign(key,value); expected[key]={value};
        }
        table.upsert(make_key(0),2,cb::sum{}); expected[make_key(0)][0]+=2;
        verify(table,expected);
    }

    // Native absent-entry transfer plus duplicate combine/append, including
    // emptied source ownership. Ordinary table state, no external merge index.
    Table source;
    for(unsigned i=0;i<100;++i) {
        const word key=make_key(i),value=(word{1}<<63)+i;
        source.add(key,value); expected_add<Kind>(expected,key,value);
    }
    source.add(missing,0); expected_add<Kind>(expected,missing,0);
    table.merge_from_quiescent(source);
    require(source.size_quiescent()==0,"merge source consumed"); verify(table,expected);
    source.add(0,7); source.clear(); require(source.size_quiescent()==0,"source reusable");
    table.clear(); require(table.size_quiescent()==0,"clear");
    // Domain endpoints and a contested final-value removal.
    table.add(0,~word{0}); table.add(cb::domain_mask<HT_CONCURRENT_K>,0);
    oracle endpoints;
    expected_add<Kind>(endpoints,0,~word{0});
    expected_add<Kind>(endpoints,cb::domain_mask<HT_CONCURRENT_K>,0);
    verify(table,endpoints);
    if constexpr(Kind==cb::kind::multimap) {
        std::atomic<unsigned> removed=0;
        parallel(workers,[&](unsigned){if(table.erase_one(0,~word{0})) removed.fetch_add(1,std::memory_order_relaxed);});
        require(removed.load()==1,"one successful final-value erase");
        require(!table.consume(0,[](word){fail("removed key callback");}),"last-value key erased");
    }
}

using clock_type=std::chrono::steady_clock;
double ns(clock_type::time_point a,clock_type::time_point b) {
    return std::chrono::duration<double,std::nano>(b-a).count();
}
template<class Table> void bench(unsigned reps,unsigned workers) {
    constexpr unsigned n=HT_CONCURRENT_K==8?191:4096,operations=32768;
    // Small local receipt: not a scalability or ISA performance claim. Thread
    // setup/input creation lie outside timing; ingestion synchronization,
    // growth, protected final consume and owner destruction are inside it.
    std::vector<std::pair<word,word>> input;
    for(unsigned i=0;i<operations;++i) input.emplace_back(make_key(mix(i)%n),1+i%13);
    for(unsigned rep=0;rep<reps;++rep) {
        auto table=std::make_unique<Table>();
        std::barrier start(static_cast<std::ptrdiff_t>(workers+1));
        std::barrier finish(static_cast<std::ptrdiff_t>(workers+1));
        std::vector<std::jthread> threads;
        for(unsigned w=0;w<workers;++w) threads.emplace_back([&,w] {
            start.arrive_and_wait();
            for(unsigned i=w;i<input.size();i+=workers) table->add(input[i].first,input[i].second);
            finish.arrive_and_wait();
        });
        const auto a=clock_type::now();start.arrive_and_wait();finish.arrive_and_wait();
        const auto b=clock_type::now();
        word digest=0;
        for(unsigned i=0;i<n;++i) table->consume(make_key(i),[&](word v){digest+=mix(v+i);});
        const auto c=clock_type::now();
        table.reset(); const auto d=clock_type::now();
        std::cout<<(Table::implementation==cb::backend::boost?"boost":"gtl")<<','<<HT_CONCURRENT_K<<','
            <<Table::exact<<','<<int(Table::container_kind)<<','<<rep<<','<<workers<<','<<operations<<','<<n<<','
            <<ns(a,b)<<','<<ns(b,c)<<','<<ns(c,d)<<','<<ns(a,d)<<','<<Table::owner_bytes()<<','<<digest<<'\n';
    }
}
template<cb::backend Backend,cb::kind Kind,bool Exact> void run(bool timing,unsigned reps,unsigned workers) {
    using Table=cb::table<Backend,HT_CONCURRENT_K,Kind,Exact>;
    static_assert(sizeof(typename Table::key_type)==(Exact?HT_CONCURRENT_K/8:sizeof(cb::ordinary_key<HT_CONCURRENT_K>)));
    if(timing) bench<Table>(reps,workers); else check<Table>();
}
template<cb::backend Backend,cb::kind Kind> void both(bool timing,unsigned reps,unsigned workers) {
    run<Backend,Kind,false>(timing,reps,workers);run<Backend,Kind,true>(timing,reps,workers);
}
}
void HT_JOIN(concurrent_width_,HT_CONCURRENT_K)(bool timing,unsigned reps,unsigned workers) {
    both<cb::backend::boost,cb::kind::set>(timing,reps,workers);
    both<cb::backend::boost,cb::kind::map>(timing,reps,workers);
    both<cb::backend::boost,cb::kind::multimap>(timing,reps,workers);
    both<cb::backend::gtl,cb::kind::set>(timing,reps,workers);
    both<cb::backend::gtl,cb::kind::map>(timing,reps,workers);
    both<cb::backend::gtl,cb::kind::multimap>(timing,reps,workers);
}
#else
#define HT_DECLARE(K) void HT_JOIN(concurrent_width_,K)(bool,unsigned,unsigned);
HT_DECLARE(8) HT_DECLARE(16) HT_DECLARE(24) HT_DECLARE(32)
HT_DECLARE(40) HT_DECLARE(48) HT_DECLARE(56) HT_DECLARE(64)
int main(int argc,char** argv) {
    const bool timing=argc>1 && std::string_view(argv[1])=="bench";
    if(argc>1 && !timing && std::string_view(argv[1])!="check") return 2;
    const unsigned reps=argc>2?unsigned(std::stoul(argv[2])):3;
    const unsigned workers=argc>3?unsigned(std::stoul(argv[3])):2;
    if(!workers || workers>16 || !reps) return 2;
    if(timing) std::cout<<"backend,K,exact,kind,rep,workers,operations,query_keys,ingest_ns,consume_ns,release_ns,total_ns,owner_bytes,digest\n";
#ifdef HT_CONCURRENT_ONLY
    HT_JOIN(concurrent_width_,HT_CONCURRENT_ONLY)(timing,reps,workers);
#else
    concurrent_width_8(timing,reps,workers);concurrent_width_16(timing,reps,workers);
    concurrent_width_24(timing,reps,workers);concurrent_width_32(timing,reps,workers);
    concurrent_width_40(timing,reps,workers);concurrent_width_48(timing,reps,workers);
    concurrent_width_56(timing,reps,workers);concurrent_width_64(timing,reps,workers);
#endif
    if(!timing) std::cout<<"concurrent baseline independent-oracle checks passed\n";
}
#endif

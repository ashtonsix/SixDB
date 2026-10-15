#include "table.h"
#include "baseline.h"
#include "input.h"
#include <loom/hardware.h>
#include <loom/ring.h>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace ht {
constexpr word miss_bit=word(1)<<39;
struct answer {word sum=0,found=0,terminal=0;bool operator==(const answer&)const=default;};
struct start {word key=0,left=0;};
// Input permutation, independent of the table's seeded multiply-fold hash.
// Odd multiplication modulo 2^39 and right XOR-shifts are bijections. The
// inverse oracle checks every generated input without a set or key vector.
struct generated_keys {
    static constexpr word domain=mask(39),seed=0x49d375a621ULL;
    static constexpr word first=0x1559d6b185ULL,second=0x6c8e9cf571ULL;
    static constexpr word inverse_multiplier(word value) {
        word out=1;for(unsigned i=0;i<6;++i)out*=2-value*out;return out&domain;
    }
    static constexpr word undo_shift(word x,unsigned shift) {
        for(unsigned bit=shift;bit<39;bit*=2)x^=x>>bit;return x;
    }
    static constexpr word at(word index) {
        word x=(index+seed)&domain;x^=x>>17;x=(x*first)&domain;
        x^=x>>13;x=(x*second)&domain;return x^(x>>19);
    }
    static constexpr word invert(word x) {
        x=undo_shift(x,19);x=(x*inverse_multiplier(second))&domain;
        x=undo_shift(x,13);x=(x*inverse_multiplier(first))&domain;
        return (undo_shift(x,17)-seed)&domain;
    }
    struct proof {word sum=0,xor_keys=0,lowest_bin=0,highest_bin=0;};
    static proof check(unsigned n) {
        require(n && word(n)<=domain,"generated key population");
        constexpr std::array<word,12> boundaries{0,1,2,255,256,(word(1)<<32)-1,word(1)<<32,
            (word(1)<<38)-1,word(1)<<38,(word(1)<<38)+1,domain-1,domain};
        for(auto x:boundaries)require(invert(at(x))==x && at(invert(x))==x,"generator boundary inverse");
        std::array<word,256> high{};proof out;
        for(word i=0;i<n;++i) {
            const word x=at(i);require(x<=domain && invert(x)==i,"exhaustive generator inverse");
            ++high[x>>31];out.sum+=x;out.xor_keys^=x;
        }
        out.lowest_bin=*std::min_element(high.begin(),high.end());
        out.highest_bin=*std::max_element(high.begin(),high.end());
        if(n>=1000000)require(out.lowest_bin*256>word(n)*9/10 &&
            out.highest_bin*256<word(n)*11/10,"generated high-byte spread");
        return out;
    }
};
struct workload {
    std::vector<word> keys;
    unsigned count;
    bool mixed,generated;
    generated_keys::proof proof{};
    workload(unsigned n,bool m,bool g=false):count(n),mixed(m),generated(g) {
        if(g)proof=generated_keys::check(n);else keys=make_input<39>(n,638251).keys;
    }
    word key(unsigned i)const {return generated?generated_keys::at(i%count):keys[i%count];}
    word request(unsigned i)const {auto k=key(i);return k^((mixed&&(mix(k)&1))?miss_bit:0);}
    word value(unsigned i)const {return request(i+1);}
    template<unsigned C> auto starts()const {
        std::array<start,C> out{};
        for(unsigned j=0;j<C;++j) {
            auto first=std::size_t(j)*count/C,last=std::size_t(j+1)*count/C;
            out[j]={request(first),last-first};
            for(auto i=first;i<last;++i) out[j].left+=word(request(i)!=key(i));
        }
        return out;
    }
    template<unsigned C> answer expected()const {
        answer out{0,count,0};
        for(unsigned i=0;i<count;++i) out.sum+=value(i);
        for(unsigned j=0;j<C;++j) out.terminal^=request(std::size_t(j+1)*count/C);
        return out;
    }
};
struct lane {word key=0,sum=0,found=0,left=0;};
template<class Read> struct job:lane {typename Read::probe probe;};
template<class Range> answer result(const Range& work) {
    answer out;for(const auto& j:work) {out.sum+=j.sum;out.found+=j.found;out.terminal^=j.key;}return out;
}
[[gnu::always_inline]] inline void accept(lane& j,bool found,word value) {
    const word key=found?value:(j.key^miss_bit),sum=j.sum+value,hits=j.found+found,left=j.left-1;
    j.key=key;j.sum=sum;j.found=hits;j.left=left;
}
template<class Read> struct context {Read read;loom::HintPolicy policy;};
template<loom::Hint H,class Read> [[gnu::always_inline]] auto emitter(const context<Read>& c) {
    return [&c](const byte* p,std::size_t bytes) __attribute__((always_inline)) {
        (void)c;
        if constexpr(H!=loom::Hint::none) {
            auto line=c.policy.profile.geometry.line_bytes,page=c.policy.profile.geometry.page_bytes;
            loom::prefetch<H>(p,page,line);
            auto first=reinterpret_cast<std::uintptr_t>(p),end=first+bytes;
            for(auto next=(first&~(line-1))+line;next<end;next+=line)
                loom::prefetch<H>(reinterpret_cast<const byte*>(next),page,line);
        }
    };
}
template<class Read> using scheduled=typename loom::Ring<job<Read>*>::template WithShared<const context<Read>*>;
template<loom::Hint H,class Read>
[[gnu::noinline]] LOOM_NATIVE_CC void cold(typename scheduled<Read>::Cursor ring,typename scheduled<Read>::Slot* slot,
                                          const context<Read>* c,job<Read>* j) {
    __builtin_assume(j->probe.next==Read::stage::chain_directory);
    auto emit=emitter<H>(*c);word value=0;bool found=false;
    c->read.advance_read(j->probe,j->key,value,found,emit);
    [[clang::musttail]] return scheduled<Read>::yield_unchanged(ring,slot,c,j);
}
template<loom::Hint H,class Read>
LOOM_NATIVE_CC void visit(typename scheduled<Read>::Cursor ring,typename scheduled<Read>::Slot* slot,
                         const context<Read>* c,job<Read>* j) {
    if(j->probe.next==Read::stage::chain_directory)
        [[clang::musttail]] return cold<H,Read>(ring,slot,c,j);
    auto emit=emitter<H>(*c);word value=0;bool found=false;
    if(c->read.template advance_read<false>(j->probe,j->key,value,found,emit)) {
        accept(*j,found,value);
        if(!j->left) [[clang::musttail]] return scheduled<Read>::finish(ring,slot,c,j);
        c->read.prepare_read(j->probe,j->key,emit);
    }
    [[clang::musttail]] return scheduled<Read>::yield_unchanged(ring,slot,c,j);
}
template<unsigned C,bool Prefetch=false,class Read>
[[gnu::noinline]] answer direct(const Read& read,const std::array<start,C>& initial) {
    std::array<lane,C> work{};
    for(unsigned i=0;i<C;++i) {
        work[i].key=initial[i].key;work[i].left=initial[i].left;
        if constexpr(Prefetch) read.prefetch(work[i].key);
    }
    unsigned pending=C;
    while(pending) for(auto& j:work) if(j.left) {
        word value=0;bool found=read.get_value(j.key,value);accept(j,found,value);
        if(!j.left) --pending;
        else if constexpr(Prefetch) read.prefetch(j.key);
    }
    return result(work);
}
template<unsigned C,loom::Hint H,class Read>
[[gnu::noinline]] answer manual(const context<Read>& c,const std::array<start,C>& initial) {
    std::array<job<Read>,C> work{};auto emit=emitter<H>(c);
    for(unsigned i=0;i<C;++i) {
        auto& j=work[i];j.key=initial[i].key;j.left=initial[i].left;c.read.prepare_read(j.probe,j.key,emit);
    }
    unsigned pending=C;
    while(pending) for(auto& j:work) if(j.left) {
        word value=0;bool found=false;
        if(c.read.advance_read(j.probe,j.key,value,found,emit)) {
            accept(j,found,value);
            if(!j.left) --pending;else c.read.prepare_read(j.probe,j.key,emit);
        }
    }
    return result(work);
}
template<unsigned C,loom::Hint H,class Read>
[[gnu::noinline]] answer ring_run(const context<Read>& c,const std::array<start,C>& initial,scheduled<Read>& ring) {
    std::array<job<Read>,C> work{};auto emit=emitter<H>(c);
    for(unsigned i=0;i<C;++i) {
        auto& j=work[i];j.key=initial[i].key;j.left=initial[i].left;c.read.prepare_read(j.probe,j.key,emit);
        require(ring.push(visit<H,Read>,{&j}),"ring admission");
    }
    while(ring.pending()) ring.spin({4096});
    return result(work);
}
template<class Map> auto inspect(const Map& map,const workload& input) {
    using Read=typename Map::reader;
    auto read=map.bind_read();
    std::vector<std::span<const byte>> owners{map.directory_storage()};
    map.allocations([&](const oa::Allocation& a) {
        owners.emplace_back(reinterpret_cast<const byte*>(a.address),a.size);
    });
    std::erase_if(owners,[](auto owner){return owner.empty();});
    auto address=[](auto owner){return reinterpret_cast<std::uintptr_t>(owner.data());};
    std::sort(owners.begin(),owners.end(),[&](auto a,auto b){return address(a)<address(b);});
    for(unsigned i=1;i<owners.size();++i)require(address(owners[i-1])+owners[i-1].size()<=address(owners[i]),
        "independent owner inventory is disjoint");
    auto emit=[&](const byte* p,std::size_t bytes) {
        auto at=reinterpret_cast<std::uintptr_t>(p);bool owned=false;
        auto after=std::upper_bound(owners.begin(),owners.end(),at,[&](auto x,auto owner){return x<address(owner);});
        if(after!=owners.begin()) {
            auto owner=*std::prev(after);const auto start=address(owner);
            owned=at-start<=owner.size() && bytes<=owner.size()-(at-start);
        }
        require(bytes && owned,"emitted window within initialized owner storage");
    };
    std::array<word,4> stages{};
    for(unsigned i=0;i<input.count;++i) for(unsigned m=0;m<1+unsigned(input.request(i)!=input.key(i));++m) {
        word key=input.key(i)^(m?miss_bit:0),value=0xdeadbeef;bool found=true;
        typename Read::probe p;
        read.prepare_read(p,key,emit);
        for(;;) {++stages[unsigned(p.next)];if(read.advance_read(p,key,value,found,emit)) break;}
        require(found==(m==0) && value==(m?0xdeadbeef:input.value(i)),"staged independent oracle");
    }
    return stages;
}
template<unsigned C,class Map> void compare(loom::Worker& worker,const char* layout,const Map& map,
                                           const abseil<40>& baseline,const workload& input,unsigned repeats,word minimum,
                                           const std::array<word,4>* inspected=nullptr) {
    using Read=typename Map::reader;
    require(input.count>=C,"each scheduled lane has at least one present key");
    static_assert(sizeof(typename Read::probe)==24);
    context<Read> c{map.bind_read(),{}};scheduled<Read> ring;
    auto setup=clock_type::now();require(bool(ring.configure(worker,{C,0},{&c})),"ring configure");
    auto setup_ns=std::chrono::duration<double,std::nano>(clock_type::now()-setup).count();
    c.policy=ring.hint_policy();auto demand=loom::bind_demand({loom::MemoryPattern::irregular,1},1);
    require(bool(demand),"hint demand");auto hint=c.policy.codes[demand->code()];
    auto mh=&manual<C,loom::Hint::none,Read>;auto rh=&ring_run<C,loom::Hint::none,Read>;
    auto bind=[&]<loom::Hint H>() {mh=&manual<C,H,Read>;rh=&ring_run<C,H,Read>;};
    switch(hint) {
        case loom::Hint::none:break;
        case loom::Hint::l1_1:bind.template operator()<loom::Hint::l1_1>();break;
        case loom::Hint::l2_1:bind.template operator()<loom::Hint::l2_1>();break;
        case loom::Hint::l3_1:bind.template operator()<loom::Hint::l3_1>();break;
        default:require(false,"supported returned hint");
    }
    auto initial=input.template starts<C>();auto expected=input.template expected<C>();
    word requests=0;for(auto x:initial)requests+=x.left;
    word graphs=std::max(word(1),(minimum+requests-1)/requests);
    answer repeated{expected.sum*graphs,expected.found*graphs,(graphs&1)?expected.terminal:0};
    auto stages=inspected?*inspected:inspect(map,input);word visits=0;for(auto n:stages)visits+=n;
    auto mem=map.footprint();auto bm=baseline.footprint();
    std::printf("context,%s,%zu,%u,%u,%llu,%zu,%zu,%u,%llu,%llu,%llu,%llu,%zu,%zu,%.3f\n",layout,
        std::size_t(input.count),C,unsigned(input.mixed),(unsigned long long)c.policy.profile.revision,
        c.policy.profile.geometry.line_bytes,c.policy.profile.geometry.page_bytes,unsigned(hint),
        (unsigned long long)mem.requested,(unsigned long long)mem.backing,(unsigned long long)mem.metadata,
        (unsigned long long)bm.requested,ring.storage_bytes(),sizeof(job<Read>)*C,setup_ns);
    std::printf("workload,%s,%u,%u,%u,%llu,%llu,%llu,%llu\n",layout,input.count,C,unsigned(input.mixed),
        (unsigned long long)(requests*graphs),(unsigned long long)(expected.found*graphs),
        (unsigned long long)((requests-expected.found)*graphs),(unsigned long long)graphs);
    const char* names[]={"abseil","abseil_prefetch","direct","manual","manual_hints","ring","ring_hints"};
    for(unsigned rep=0;rep<repeats;++rep) for(unsigned order=0;order<7;++order) {
        auto arm=(order+rep)%7;
        auto execute=[&] {
            switch(arm) {
                case 0:return direct<C>(baseline,initial);
                case 1:return direct<C,true>(baseline,initial);
                case 2:return direct<C>(c.read,initial);
                case 3:return manual<C,loom::Hint::none>(c,initial);
                case 4:return mh(c,initial);
                case 5:return ring_run<C,loom::Hint::none>(c,initial,ring);
                default:return rh(c,initial,ring);
            }
        };
        require(execute()==expected,"full graph warmup");
        auto before=ring.counters().visits;auto begin=clock_type::now();answer out;
        for(word g=0;g<graphs;++g) {
            asm volatile("" ::: "memory");
            auto one=execute();out.sum+=one.sum;out.found+=one.found;out.terminal^=one.terminal;
        }
        auto ns=std::chrono::duration<double,std::nano>(clock_type::now()-begin).count()/(requests*graphs);
        require(out==repeated,"complete graph oracle");
        require(ring.counters().visits-before==(arm>=5?visits*graphs:0),"actual ring visit count");
        std::printf("time,%s,%u,%u,%u,%u,%s,%llu,%.6f,%llu,%llu,%llu,%llu\n",layout,input.count,C,
            unsigned(input.mixed),rep,names[arm],(unsigned long long)(requests*graphs),ns,
            (unsigned long long)(stages[0]*graphs),(unsigned long long)(stages[1]*graphs),
            (unsigned long long)(stages[2]*graphs),(unsigned long long)(stages[3]*graphs));
        std::fflush(stdout);
    }
    auto close_start=clock_type::now();require(bool(ring.close()),"ring close");
    std::printf("close,%s,%u,%u,%u,%.3f\n",layout,input.count,C,unsigned(input.mixed),
        std::chrono::duration<double,std::nano>(clock_type::now()-close_start).count());
}
template<class Map> void residence_snapshot(const char* name,const char* phase,const Map& map,
                                           const abseil<40>& baseline,const workload& input) {
    const auto* path=std::getenv("HASHTABLE_RING_RESIDENCE");
    require(path && *path,"dram mode requires HASHTABLE_RING_RESIDENCE receipt directory");
    const auto root=std::filesystem::path(path);std::filesystem::create_directories(root);
    const auto stem=std::string(name)+"-"+std::to_string(input.count)+"-mixed"+
        std::to_string(input.mixed)+"-"+phase;
    for(const char* leaf:{"smaps","smaps_rollup","numa_maps","status"}) {
        std::ifstream in(std::string("/proc/self/")+leaf);
        std::ofstream out(root/(stem+"."+leaf));
        require(bool(out),"residence receipt open");
        if(in)out<<in.rdbuf();
        else {
            require(std::string_view(leaf)=="numa_maps","required process memory receipt readable");
            out<<"unavailable: /proc/self/numa_maps is not exposed by this kernel\n";
        }
        out.flush();
        require(bool(out),"residence receipt write");
    }
    std::ofstream owners(root/(stem+".owners.tsv"));require(bool(owners),"owner inventory receipt");
    owners<<"kind\tordinal\taddress\tinitialized_bytes\taccessible_capacity_bytes\n";
    unsigned ordinal=0;
    map.allocations([&](const oa::Allocation& a) {
        owners<<"candidate\t"<<ordinal++<<"\t0x"<<std::hex<<reinterpret_cast<std::uintptr_t>(a.address)
            <<std::dec<<'\t'<<a.size<<'\t'<<oa::capacity(a)<<'\n';
    });
    auto directory=map.directory_storage();
    owners<<"candidate_directory\t0\t0x"<<std::hex<<reinterpret_cast<std::uintptr_t>(directory.data())
        <<std::dec<<'\t'<<directory.size()<<'\t'<<directory.size()<<'\n';
    // Public iterator gives a mapping anchor, not an invented allocation bound.
    // Abseil's requested allocation total is separately recorded in context rows.
    owners<<"abseil_entry_anchor\t0\t0x"<<std::hex<<reinterpret_cast<std::uintptr_t>(&*baseline.map.begin())
        <<std::dec<<'\t'<<sizeof(*baseline.map.begin())<<"\t0\n";
    owners.flush();require(bool(owners),"owner inventory write");
    std::printf("residence,%s,%u,%u,%s,%s\n",name,input.count,unsigned(input.mixed),phase,stem.c_str());
}
template<class Map> void run(loom::Worker& worker,const char* name,unsigned n,unsigned repeats,bool mixed,word minimum,
                            bool generated=false,bool record_residence=false) {
    Map map(*worker.memory,mix(8301),mix(8302));abseil<40> baseline(*worker.memory,0,0);baseline.reserve(n);
    workload input(n,mixed,generated);
    if(generated)std::printf("generator,%s,%u,%u,bijective39,%llu,%llu,%llu,%llu,%llu\n",name,n,unsigned(mixed),
        (unsigned long long)generated_keys::seed,(unsigned long long)input.proof.lowest_bin,
        (unsigned long long)input.proof.highest_bin,(unsigned long long)input.proof.sum,
        (unsigned long long)input.proof.xor_keys);
    for(unsigned i=0;i<n;++i) {
        require(map.set(input.key(i),input.value(i))==1,"map build");
        require(baseline.set(input.key(i),input.value(i))==1,"baseline build");
    }
    map.validate();
    auto population=map.distribution();
    std::printf("population,%s,%u,%u,%llu,%llu,%llu\n",name,n,unsigned(mixed),
        (unsigned long long)population.primary,(unsigned long long)population.secondary,(unsigned long long)population.chain);
    auto geometry=[&](const char* tier,unsigned a,unsigned f,unsigned c,recipe r) {
        std::printf("geometry,%s,%u,%u,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",name,n,unsigned(mixed),tier,
            a,f,c,r.unit,r.slots,r.fp,r.suffix,r.values,r.suffix_bytes,r.value_bytes);
    };
    geometry("primary",Map::primary_bits,Map::primary_hash::F,Map::primary_hash::C,Map::primary);
    geometry("secondary",Map::secondary_bits,Map::secondary_hash::F,Map::secondary_hash::C,Map::secondary);
    if(record_residence)residence_snapshot(name,"before",map,baseline,input);
    // Large-mode window checks are exhaustive once per immutable map. C only
    // changes scheduling; each timing arm still executes a complete warm graph.
    std::optional<std::array<word,4>> inspected;if(generated)inspected=inspect(map,input);
    // Private immutable owner scope: no physical polling, external reentry,
    // mutation, growth or release occurs until every configured Ring drains.
    // The source Allocations and directory remain owned here throughout.
    // A retained/escaping read is a separate operation with physical protection.
    compare<1>(worker,name,map,baseline,input,repeats,minimum,inspected?&*inspected:nullptr);
    compare<8>(worker,name,map,baseline,input,repeats,minimum,inspected?&*inspected:nullptr);
    compare<32>(worker,name,map,baseline,input,repeats,minimum,inspected?&*inspected:nullptr);
    if(record_residence)residence_snapshot(name,"after",map,baseline,input);
}
} // namespace ht

int main(int argc,char** argv) {
    using namespace ht;
    std::string_view mode=argc>1?argv[1]:"check";unsigned reps=argc>2?std::stoul(argv[2]):5;
    if(mode=="generator-check") {
        const auto n=argc>2?reps:57600000;const auto proof=generated_keys::check(n);
        std::printf("generator checked %u keys; high-byte bins %llu..%llu; sum %llu xor %llu\n",n,
            (unsigned long long)proof.lowest_bin,(unsigned long long)proof.highest_bin,
            (unsigned long long)proof.sum,(unsigned long long)proof.xor_keys);return 0;
    }
    require(reps>0,"repetitions");
    if(mode=="dram")require(std::getenv("HASHTABLE_RING_RESIDENCE"),"dram residence output is required");
    auto hardware=loom::discover_hardware();require(bool(hardware),"hardware discovery");
    loom::HardwareOptions options;options.calibrate=false;loom::HardwareService service(options);
    auto profile=service.request(std::move(*hardware));require(bool(profile),"hardware profile");
    loom::Worker worker;require(bool(loom::adopt(worker)),"worker adoption");
    auto arena=oa::open_arena(4ULL<<30);require(bool(arena),"worker arena");
    require(bool(loom::configure(worker,std::move(*arena),0,*profile)),"worker configuration");
    require(bool(loom::set_hint_pressure(worker,loom::HintPressure::low)),"hint pressure");
    word minimum=(mode=="check"||mode=="dram-check")?0:1000000;
    for(bool mixed:{false,true}) {
        if(mode=="check") run<table<40,3,0,64,64>>(worker,"forced_chain",300,1,mixed,minimum);
        else if(mode=="dram-check") run<table<40,3,0,64,64>>(worker,"forced_chain",301,1,mixed,minimum,true,true);
        else if(mode=="hot") {
            run<table<40,8,7,64,128>>(worker,"compact",1800,reps,mixed,minimum);
            run<table<40,8,4,128,128>>(worker,"fuller",1800,reps,mixed,minimum);
        } else if(mode=="llc") {
            run<table<40,14,13,64,128>>(worker,"compact",110000,reps,mixed,minimum);
            run<table<40,14,10,128,128>>(worker,"fuller",110000,reps,mixed,minimum);
        } else if(mode=="dram") {
            run<table<40,23,22,64,128>>(worker,"compact",57600000,reps,mixed,minimum,true,true);
            run<table<40,23,19,128,128>>(worker,"fuller",57600000,reps,mixed,minimum,true,true);
        } else {
            require(mode=="large","known mode");
            run<table<40,19,18,64,128>>(worker,"compact",3600000,reps,mixed,minimum);
            run<table<40,19,15,128,128>>(worker,"fuller",3600000,reps,mixed,minimum);
        }
    }
    auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(loom::shutdown(worker).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"worker drain");loom::poll(worker,{1024,0});std::this_thread::yield();
    }
    loom::relinquish(worker);
}

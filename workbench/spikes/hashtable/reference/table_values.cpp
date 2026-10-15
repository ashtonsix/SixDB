#include "table-plans.h"
#include "table.h"
#include "support.h"
#include <absl/container/flat_hash_map.h>
#include <absl/container/internal/hashtable_debug.h>
#include <map>
#include <random>
#include <source_location>
#include <sys/mman.h>
#include <unistd.h>

using namespace hashtable_spike;
namespace t=hashtable_spike::tiered;
namespace {
void check(bool ok,const std::source_location where=std::source_location::current()) {
    if(!ok){std::fprintf(stderr,"values check failed at %s:%u\n",where.file_name(),where.line());std::abort();}
}
using aggregate=std::array<word,3>;
template<class Access>[[gnu::always_inline]] aggregate read_all(Access a) {
    return {a.template read<0>(),a.template read<1>(),a.template read<2>()};
}
template<class Access>[[gnu::always_inline]] void accumulate(Access a,bool inserted,word x) {
    a.template write<0>(inserted?1:a.template read<0>()+1);
    a.template write<1>(inserted?x:a.template read<1>()+x);
    a.template write<2>(inserted?x:a.template read<2>()^x);
}
struct footprint {std::size_t payload{},backing{},metadata{},preparation{},primary{},secondary{},chain{};};
template<t::bucket_plan P,t::bucket_plan Q,bool Direct>struct candidate {
    using body=t::table<P,Q,t::sharing::flat,true,Direct>;
    body contents;
    candidate(oa::Pool& pool,std::size_t,word seed):contents([&] {
        auto made=body::make(pool,seed,mix(seed^0x91248523));check(bool(made));return std::move(*made);
    }()){}
    [[gnu::always_inline]] bool add(word key,word x) {
        auto result=contents.update_or_insert(key,[&](auto at,bool inserted){accumulate(at,inserted,x);});
        check(bool(result)&&result->result!=t::outcome::grow_required);return result->distinct_delta==1;
    }
    bool erase(word key){auto r=contents.erase(key);check(bool(r));return r->distinct_delta==-1;}
    template<class F>[[gnu::always_inline]] bool visit(word key,F&& f)const{return contents.visit(key,std::forward<F>(f));}
    template<class F>void each(F&& f)const{contents.visit_entries(std::forward<F>(f));}
    footprint memory()const {
        footprint m{.metadata=sizeof(*this)};
        contents.allocations([&](const auto& a){m.payload+=a.size;m.backing+=oa::capacity(a);});
        contents.metadata([&](const void*,std::size_t n){m.metadata+=n;});
        contents.preparation([&](const void*,std::size_t n){m.preparation+=n;});
        const auto c=contents.counts();m.primary=c.primary;m.secondary=c.secondary;m.chain=c.chain;return m;
    }
};
template<bool Mutable>struct ordinary_access {
    std::conditional_t<Mutable,aggregate*,const aggregate*> data;
    template<unsigned I>word read()const{return (*data)[I];}
    template<unsigned I>void write(word x)const requires Mutable{(*data)[I]=x;}
};
struct baseline {
    struct record {
        aggregate words;
        record(word count,word sum,word xor_value):words{count,sum,xor_value}{}
    };
    absl::flat_hash_map<word,record> contents;
    baseline(oa::Pool&,std::size_t n,word){contents.reserve(n);}
    [[gnu::always_inline]] bool add(word key,word x) {
        // try_emplace performs one search, constructs only on insertion, and
        // leaves the existing record in place for the update recipe.
        auto [at,inserted]=contents.try_emplace(key,word{1},x,x);
        if(!inserted)accumulate(ordinary_access<true>{&at->second.words},false,x);
        return inserted;
    }
    bool erase(word key){return contents.erase(key)!=0;}
    template<class F>[[gnu::always_inline]] bool visit(word key,F&& f)const {
        auto at=contents.find(key);if(at==contents.end())return false;
        f(ordinary_access<false>{&at->second.words});return true;
    }
    template<class F>void each(F&& f)const{for(const auto& [key,value]:contents)f(key,ordinary_access<false>{&value.words});}
    footprint memory()const{return {.payload=absl::container_internal::AllocatedByteSize(contents),.metadata=sizeof(*this)};}
};
struct inputs {
    struct event {word key,value;};
    std::vector<word> keys,replacements;
    std::vector<event> events;
    std::vector<aggregate> expected;
    explicit inputs(std::size_t n):keys(n),replacements(n),expected(n) {
        permutation<40,false> source(15733);std::mt19937_64 random(49817);
        for(std::size_t i=0;i<n;++i){keys[i]=source(i);replacements[i]=source(n+i);}
        std::shuffle(keys.begin(),keys.end(),random);
        std::vector<std::size_t> order(n);std::iota(order.begin(),order.end(),0);events.reserve(4*n);
        for(unsigned generation=0;generation<4;++generation) {
            std::shuffle(order.begin(),order.end(),random);
            for(auto i:order) {
                const word x=mix(keys[i]^word(generation+1)*0x9845623);
                events.push_back({keys[i],x});++expected[i][0];expected[i][1]+=x;expected[i][2]^=x;
            }
        }
    }
};
template<class M>void verify(const M& map,const std::map<word,aggregate>& expected) {
    std::map<word,aggregate> actual;
    map.each([&](word k,auto at){check(actual.emplace(k,read_all(at)).second);});check(actual==expected);
    for(const auto& [k,v]:expected)check(map.visit(k,[&](auto at){check(read_all(at)==v);}));
}
template<t::bucket_plan P,t::bucket_plan Q,bool Direct>void check_table() {
    pool_owner owner;candidate<P,Q,Direct> map(*owner.pool,701,37913);inputs input(701);
    std::map<word,aggregate> oracle;
    for(const auto& e:input.events){map.add(e.key,e.value);auto& a=oracle[e.key];++a[0];a[1]+=e.value;a[2]^=e.value;}
    verify(map,oracle);map.contents.audit();check(map.contents.counts().chain>0);
    auto retained=map;
    for(unsigned i=0;i<701;i+=3){check(map.erase(input.keys[i]));oracle.erase(input.keys[i]);map.add(input.replacements[i],~word{});oracle[input.replacements[i]]={1,~word{},~word{}};}
    verify(map,oracle);map.contents.audit();
    auto rebuilt=decltype(map)::body::build_distinct(*owner.pool,731,mix(891),map.contents);check(bool(rebuilt));
    std::map<word,aggregate> rebuilt_values;
    rebuilt->visit_entries([&](word key,auto value){check(rebuilt_values.emplace(key,read_all(value)).second);});
    check(rebuilt_values==oracle);rebuilt->audit();
    std::map<word,aggregate> old;
    for(unsigned i=0;i<701;++i)old[input.keys[i]]=input.expected[i];verify(retained,old);
    // Wide completion follows the same stages, with no eager wide temporary in
    // the table. Callback executes once on hits and never on misses.
    for(const auto& [key,value]:oracle) {
        typename decltype(map)::body::read_probe probe;unsigned calls=0,steps=0;bool found=false;
        auto emit=[](const byte* p,std::size_t n){check(p&&n);};
        map.contents.prepare_read(probe,key,emit);
        while(!map.contents.advance_visit(probe,[&](auto at){++calls;check(read_all(at)==value);},found,emit))check(++steps<256);
        check(found&&calls==1);
    }
    bool called=false;check(!map.visit(permutation<40,false>(15733)(20000),[&](auto){called=true;}));check(!called);
}
void check_guard() {
    // Independent bit oracle and actual guard page: all three words cross byte
    // boundaries, preserving the outside bits and requiring a ninth-byte tail.
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* area=static_cast<byte*>(mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    check(area!=MAP_FAILED&&mprotect(area+page,page,PROT_NONE)==0);
    const t::value_binding<192,0xff> binding;
    for(unsigned phase=0;phase<8;++phase) {
        std::memset(area,0xa5,page);auto* p=area+page-binding.bytes;
        std::vector<byte> expected(area,area+page);
        const aggregate words{0,~word{},0x974237648219ad13};
        for(unsigned i=0;i<192;++i) {
            const auto at=page-binding.bytes+(phase+i)/8;const byte bit=byte(1u<<((phase+i)%8));
            expected[at]=byte((expected[at]&~bit)|(((words[i/64]>>(i%64))&1)?bit:0));
        }
        const t::value_access<192,0xff,false,true> a{binding,p,phase};
        a.write<0>(words[0]);a.write<1>(words[1]);a.write<2>(words[2]);
        check(read_all(a)==words);check(std::equal(expected.begin(),expected.end(),area));
    }
    check(munmap(area,2*page)==0);
}
template<class F>double timed(F&& f) {
    const auto start=clock_type::now();word sum=f();asm volatile("" : "+r"(sum) :: "memory");
    return std::chrono::duration<double,std::nano>(clock_type::now()-start).count();
}
// Keep each complete arm behind the same outer call boundary. Otherwise Clang
// inlines both candidates into the arm selector while outlining Abseil, merging
// the candidate phase CFGs and making the direct/composed control asymmetric.
// Lookup and record recipes remain inline inside each measured operation.
template<class M>[[gnu::noinline]] void run(const char* name,const inputs& in,unsigned repetition,bool output) {
    pool_owner owner;std::optional<M> map;const auto n=in.keys.size();double total=0;
    footprint memory;word result=0;
    struct sample {const char* operation;std::size_t operations;double ns;word checksum;};
    std::array<sample,10> samples;unsigned sampled=0;
    auto measure=[&](const char* op,std::size_t ops,auto&& f) {
        const auto ns=timed([&]{result=f();return result;});total+=ns;samples[sampled++]={op,ops,ns,result};
    };
    measure("construct_fill",n,[&]{map.emplace(*owner.pool,n,mix(37913+repetition));word sum=0;for(std::size_t i=0;i<n;++i)sum+=map->add(in.events[i].key,in.events[i].value);return sum;});check(result==n);
    measure("repeated_update",3*n,[&]{word sum=0;for(std::size_t i=n;i<in.events.size();++i)sum+=map->add(in.events[i].key,in.events[i].value);return sum;});check(result==0);
    auto keys=in.keys;auto expected=in.expected;
    measure("erase_insert_pair",(n+7)/8,[&]{word sum=0;for(std::size_t i=0;i<n;i+=8){sum+=map->erase(in.keys[i]);sum+=map->add(in.replacements[i],mix(in.replacements[i]));}return sum;});check(result==2*((n+7)/8));
    for(std::size_t i=0;i<n;i+=8){keys[i]=in.replacements[i];const auto x=mix(keys[i]);expected[i]={1,x,x};}
    const unsigned rounds=unsigned(std::max<std::size_t>(1,(131072+n-1)/n));word count=0,full=0,iteration=0;
    for(std::size_t i=0;i<n;++i){count+=expected[i][0];full+=expected[i][0]+expected[i][1]+expected[i][2];iteration+=keys[i]+expected[i][0]+expected[i][1]+expected[i][2];}
    measure("project_count",n*rounds,[&]{word sum=0;for(unsigned r=0;r<rounds;++r)for(auto key:keys)map->visit(key,[&](auto at){sum+=at.template read<0>();});return sum;});check(result==count*rounds);
    measure("consume_record",n*rounds,[&]{word sum=0;for(unsigned r=0;r<rounds;++r)for(auto key:keys)map->visit(key,[&](auto at){sum+=at.template read<0>()+at.template read<1>()+at.template read<2>();});return sum;});check(result==full*rounds);
    // The external recurrence is charged equally. It is a dependent consumer,
    // not isolated table latency; every query remains reachable exactly once.
    std::vector<word> count_next(n),full_next(n);
    for(std::size_t i=0;i<n;++i) {
        const auto next=i+1==n?0:i+1;
        count_next[i]=next^expected[i][0];full_next[i]=next^(expected[i][0]+expected[i][1]+expected[i][2]);
    }
    measure("serial_project_count",n*rounds,[&]{word sum=0,at=0;for(std::size_t j=0;j<n*rounds;++j){word value=0;map->visit(keys[at],[&](auto v){value=v.template read<0>();});sum+=value;at=count_next[at]^value;asm volatile("" : "+r"(at));}check(at==0);return sum;});check(result==count*rounds);
    measure("serial_consume_record",n*rounds,[&]{word sum=0,at=0;for(std::size_t j=0;j<n*rounds;++j){word value=0;map->visit(keys[at],[&](auto v){value=v.template read<0>()+v.template read<1>()+v.template read<2>();});sum+=value;at=full_next[at]^value;asm volatile("" : "+r"(at));}check(at==0);return sum;});check(result==full*rounds);
    measure("iterate",n,[&]{word sum=0;map->each([&](word key,auto at){sum+=key+at.template read<0>()+at.template read<1>()+at.template read<2>();});return sum;});check(result==iteration);
    // Final-live accounting is outside the timed pipeline and after all probes;
    // scanning candidate buckets must not warm the next measured read phase.
    memory=map->memory();
    measure("destroy",n,[&]{map.reset();return word{};});
    samples[sampled++]={"pipeline_per_distinct_key",n,total,iteration};check(sampled==samples.size());
    if(output)for(const auto& s:samples)
        std::printf("%s,%zu,%u,%s,%zu,%.5f,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",name,n,repetition,s.operation,s.operations,s.ns/s.operations,(unsigned long long)s.checksum,
            memory.payload,memory.backing,memory.metadata,memory.preparation,memory.primary,memory.secondary,memory.chain);
}
template<t::bucket_plan P,t::bucket_plan Q>void compare(std::size_t n,unsigned reps) {
    const inputs input(n);
    auto arm=[&](unsigned which,unsigned repetition,bool output) {
        if(which==0)run<baseline>("abseil",input,repetition,output);
        else if(which==1)run<candidate<P,Q,false>>("composed",input,repetition,output);
        else run<candidate<P,Q,true>>("direct",input,repetition,output);
    };
    for(unsigned which=0;which<3;++which)arm(which,0,false);
    for(unsigned r=0;r<reps;++r)for(unsigned i=0;i<3;++i)arm((r+i)%3,r,true);
}
} // namespace
int main(int argc,char** argv) {
    if(argc==2&&std::string_view(argv[1])=="check") {
        check_guard();
        check_table<t::k40v192_a5_n16_primary_rows_byte,t::k40v192_a2_n16_secondary_rows_byte,false>();
        check_table<t::k40v192_a5_n16_primary_rows_byte,t::k40v192_a2_n16_secondary_rows_byte,true>();
        check_table<t::k40v192_a5_n16_primary_rows_tight,t::k40v192_a2_n16_secondary_rows_tight,false>();
        std::puts("wide values: oracle, shared routing, all tiers, staged completion, retained readers, dense moves and guarded unaligned words passed");return 0;
    }
    const unsigned a=argc>1?unsigned(std::stoul(argv[1])):8;
    const auto n=argc>2?std::stoull(argv[2]):3276;
    const unsigned reps=argc>3?unsigned(std::stoul(argv[3])):3;
    check(n>0&&n<=(std::size_t{1}<<a)*16&&reps>0);
    std::puts("arm,entries,repetition,operation,operations,ns_per_operation,checksum,final_payload_bytes,final_known_backing_bytes,final_known_metadata_bytes,shared_preparation_bytes,final_primary_entries,final_secondary_entries,final_chain_entries");
    if(a==8)compare<t::k40v192_a8_n16_primary_rows_byte,t::k40v192_a5_n16_secondary_rows_byte>(n,reps);
    else if(a==18)compare<t::k40v192_a18_n16_primary_rows_byte,t::k40v192_a15_n16_secondary_rows_byte>(n,reps);
    else check(false);
}

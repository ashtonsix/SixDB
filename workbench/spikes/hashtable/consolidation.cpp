#include "consolidation.h"
#include "small_baseline.h"
#include "benchmark_oracle.h"
#include <absl/container/internal/hashtable_debug.h>
#include <map>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#ifndef HT_MERGE_KEY
#define HT_MERGE_KEY 40
#endif
#ifndef HT_MERGE_KIND
#define HT_MERGE_KIND 2
#endif
#ifndef HT_MERGE_WIDTH
#define HT_MERGE_WIDTH 2
#endif
namespace ht::consolidation_test {
constexpr unsigned K=HT_MERGE_KEY,W=HT_MERGE_WIDTH;
constexpr auto Kind=container_kind(HT_MERGE_KIND);
using value=std::array<byte,W>;
using oracle=std::map<word,std::vector<value>>;
word key(unsigned n){return word(n)*0xd6e8feb86659fd93ULL&mask(K);}
value bytes(unsigned n,unsigned source,unsigned occurrence) {
    value v;for(unsigned j=0;j<W;++j)v[j]=byte(n*37+source*91+occurrence*19+j*113);return v;
}
struct combine {
    value operator()(const value& a,const value& b)const noexcept {
        value out;unsigned i=0;
#if defined(__AVX512BW__)
        if constexpr(W==64){_mm512_storeu_si512(out.data(),_mm512_xor_si512(_mm512_loadu_si512(a.data()),_mm512_loadu_si512(b.data())));return out;}
#elif defined(__AVX2__)
        for(;i+32<=W;i+=32)_mm256_storeu_si256(reinterpret_cast<__m256i*>(out.data()+i),_mm256_xor_si256(
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()+i)),_mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()+i))));
#endif
        for(;i+16<=W;i+=16) {
#if defined(__aarch64__)
            vst1q_u8(out.data()+i,veorq_u8(vld1q_u8(a.data()+i),vld1q_u8(b.data()+i)));
#else
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out.data()+i),_mm_xor_si128(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(a.data()+i)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(b.data()+i))));
#endif
        }
        for(;i<W;++i)out[i]=a[i]^b[i];return out;
    }
};
void add(oracle& out,word k,const value& v) {
    auto& vs=out[k];
    if constexpr(Kind==container_kind::set){if(vs.empty())vs.push_back(v);}
    else if constexpr(Kind==container_kind::map) {
        if(vs.empty())vs.push_back(v);else for(unsigned j=0;j<W;++j)vs[0][j]^=v[j];
    }else vs.push_back(v);
}
void order(oracle& o){for(auto& [k,vs]:o)std::sort(vs.begin(),vs.end());}
void copy_check() {
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto region=[&] {
        auto* p=static_cast<byte*>(mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        require(p!=MAP_FAILED&&!mprotect(p+page,page,PROT_NONE),"guarded exact byte-copy windows");return p;
    };
    auto* from=region();auto* to=region();for(std::size_t i=0;i<page;++i)from[i]=byte(i*37+19);
    for(std::size_t n=0;n<=page;++n) {
        std::memset(to,0xcc,page);copy_plain_bytes(to+page-n,{from+page-n,n});
        require(!std::memcmp(to+page-n,from+page-n,n),"all byte-copy sizes and native tails");
        if(n<page)require(to[page-n-1]==0xcc,"no write before exact copy window");
    }
    require(!munmap(from,2*page)&&!munmap(to,2*page),"guarded copy release");
}
template<class Table> oracle contents(const Table& t) {
    oracle out;t.each_bytes([&](word k,auto b){value v;copy_plain_bytes(v.data(),b);out[k].push_back(v);});order(out);return out;
}
unsigned fanout(unsigned n,unsigned pattern) {
    if constexpr(Kind!=container_kind::multimap)return 1;
    return pattern==0?1:pattern==1?8:n%17==0?129:n%5==0?9:1+n%2;
}
template<class Table> void fill(Table& t,unsigned first,unsigned n,unsigned source,unsigned pattern) {
    for(unsigned i=first;i<first+n;++i)for(unsigned j=0;j<fanout(i,pattern);++j)t.set_bytes(key(i),bytes(i,source,j));
}
oracle expected(unsigned n,bool overlap,unsigned pattern) {
    oracle out;for(unsigned source=0;source<2;++source) {
        const unsigned start=source?(overlap?n/2:n):0;
        for(unsigned i=start;i<start+n;++i)for(unsigned j=0;j<fanout(i,pattern);++j)add(out,key(i),bytes(i,source,j));
    }order(out);return out;
}
template<bool Large,class Bags=linked_bags<(W?W:8)>,unsigned Inline=8,bool Tight=false> struct types {
    using small=small_table<K,Kind,W,false,Bags,Inline>;
    static constexpr unsigned A=Tight?2:std::min(K-1,W<=8?8u:10u);
    using large=promoted_table<K,Kind,W,A,A-2,Tight?96:192,96,record_format::exact,false,record_access::exact,false,Bags,Inline>;
    using table=std::conditional_t<Large,large,small>;
    static table make(oa::Pool& pool,unsigned seed) {
        small s(pool,seed);
        if constexpr(Large){auto t=large::promote(s,pool,seed,seed+317);require(bool(t),"merge empty index");return std::move(*t);}
        else return s;
    }
};
// Three ordinary Orbital Pools on the fixture's Lane. This tests consolidation
// across storage owners; remote Worker messaging is a separate caller control.
struct pools {
    pool_owner shared;
    oa::Pool *left,*right,*target;
    explicit pools(word budget=4ULL<<30):shared(budget),target(shared.pool) {
        auto a=oa::add_pool(*shared.arena,shared.lane),b=oa::add_pool(*shared.arena,shared.lane);
        require(bool(a)&&bool(b),"source Pools");left=*a;right=*b;
    }
    void close(oa::Pool*& pool) {
        if(!pool)return;
        auto deadline=clock_type::now()+std::chrono::seconds(10);
        while(oa::close(*pool).kind()!=orbital::Progress::Kind::done) {
            require(clock_type::now()<deadline,"source Pool drain");orbital::worker::poll(shared.lane,1024);
        }
        require(bool(oa::remove_pool(*pool)),"source Pool removal");pool=nullptr;
    }
    ~pools(){close(left);close(right);}
};
template<bool Encoded=false,class Source,class Target> unsigned transfer(const Source& source,Target& target,unsigned grain,unsigned group) {
    if constexpr(Encoded)require(consolidation_access<Source>::same_mapping(source,target),"matching merge interpretation");
    value_cursor<Source,Encoded> cursor;no_small_effects effects;unsigned values=0;
    for(;;) {
        auto r=cursor.step(source,grain,group,[&] [[gnu::always_inline]] (auto k,auto b) {
            return consolidation_access<Target>::accept(target,k,b,combine{},effects);
        });
        require(r.state!=record_scan_state::refused && r.work<=grain,"bounded cross-Pool transfer");
        values+=r.values;if(r.state==record_scan_state::done)return values;
    }
}
template<bool Large,class Bags=linked_bags<(W?W:8)>,unsigned Inline=8,bool Encoded=false,bool Tight=false> void check(unsigned n) {
    using T=types<Large,Bags,Inline,Tight>;using Table=typename T::table;
    for(unsigned grain:{1u,7u,64u}) {
        pools p;std::optional<Table> a(T::make(*p.left,17)),b(T::make(*p.right,Encoded?17:41));
        fill(*a,0,n,0,2);fill(*b,n/2,n,1,2);
        const auto oa=contents(*a),ob=contents(*b);auto result=T::make(*p.target,Encoded?17:127);
        if constexpr(Encoded) {
            require(consolidation_access<Table>::same_mapping(*a,result),"admitted encoded mapping");
            auto different=T::make(*p.target,41);
            require(!consolidation_access<Table>::same_mapping(*a,different),"different seed is not admitted as encoded");
        }
        if constexpr(Tight)for(auto* source:{&*a,&*b}) {
            const auto pop=consolidation_access<Table>::population(*source);
            require(pop[0]&&pop[1]&&pop[2],"forced source primary, secondary and overflow records");
        }
        // Every other callback refuses before touching the target. Cursor state
        // must preserve partial bags, including yields inside a long link.
        for(auto* source:{&*a,&*b}) {
            value_cursor<Table,Encoded> cursor;bool reject=true;unsigned loops=0;
            std::map<word,word> accepted;
            require(cursor.step(*source,0,64,[](auto,auto){std::abort();return false;}).work==0,"zero scan budget");
            for(;;) {
                std::array<small_event,256> events;small_journal effects{events};
                const auto r=cursor.step(*source,grain,64,[&](auto k,auto bytes) {
                    if(std::exchange(reject,!reject))return false;
                    effects.used=0;
                    const auto ok=consolidation_access<Table>::accept(result,k,bytes,combine{},effects);
                    require(ok,"reserved effects capacity accepts group");
                    const word logical=[&] {if constexpr(Encoded)return consolidation_access<Table>::logical_key(*source,k);else return k;}();
                    accepted[logical]+=Kind==container_kind::multimap?bytes.size()/(W?W:1):1;
                    unsigned deltas=0;std::int64_t values=0;
                    for(std::size_t j=0;j<effects.used;++j)if(events[j].type==small_event::kind::caller_delta) {
                        ++deltas;values+=events[j].delta.values;
                    }
                    require(deltas==1,"one caller publication per accepted group");
                    if constexpr(Kind==container_kind::multimap)require(values==std::int64_t(bytes.size()/W),"group multiplicity delta");
                    return true;
                });
                require(r.work<=grain && ++loops<100000,"bounded/refused value progress");
                if(r.state==record_scan_state::done)break;
            }
            std::map<word,word> expected_counts;
            for(const auto& [k,vs]:contents(*source))expected_counts[k]=vs.size();
            require(accepted==expected_counts,"each source record/value accepted exactly once across refusal and yield");
        }
        require(contents(*a)==oa&&contents(*b)==ob,"retained source readers remain unchanged");
        require(contents(result)==expected(n,true,2),"independent complete merge byte/multiplicity oracle");
        a.reset();b.reset();p.close(p.left);p.close(p.right);
        require(contents(result)==expected(n,true,2),"destination survives source Pool retirement");
        for(unsigned i=0;i<n;++i)result.erase(key(i));
        fill(result,0,n,3,1);if constexpr(requires{result.validate();})result.validate();
        auto e=expected(n,true,2);for(unsigned i=0;i<n;++i)e.erase(key(i));
        for(unsigned i=0;i<n;++i)for(unsigned j=0;j<fanout(i,1);++j)add(e,key(i),bytes(i,3,j));
        order(e);require(contents(result)==e,"destination mutation/reuse after source retirement");
    }
}
template<bool Large,bool Encoded=false> void fold_check(unsigned n) {
    using T=types<Large>;using Table=typename T::table;
    for(bool overlap:{false,true}) {
        pools p;auto target=T::make(*p.left,17);
        std::optional<Table> source(T::make(*p.right,Encoded?17:41));
        fill(target,0,n,0,2);fill(*source,overlap?n/2:n,n,1,2);
        const auto original=contents(*source);value_cursor<Table,Encoded> cursor;
        if constexpr(Encoded)require(consolidation_access<Table>::same_mapping(*source,target),"fold interpretation");
        bool refuse=true;unsigned loops=0;std::map<word,word> accepted;no_small_effects effects;
        for(;;) {
            const auto r=cursor.step(*source,7,64,[&](auto k,auto bytes) {
                if(std::exchange(refuse,!refuse))return false;
                require(consolidation_access<Table>::accept(target,k,bytes,combine{},effects),"private fold admission");
                const word logical=[&] {if constexpr(Encoded)return consolidation_access<Table>::logical_key(*source,k);else return k;}();
                accepted[logical]+=Kind==container_kind::multimap?bytes.size()/(W?W:1):1;return true;
            });
            require(r.work<=7 && ++loops<100000,"bounded existing-owner fold");
            if(r.state==record_scan_state::done)break;
        }
        std::map<word,word> counts;for(const auto& [k,vs]:original)counts[k]=vs.size();
        require(accepted==counts && contents(*source)==original,"fold consumes each input once without changing its source");
        source.reset();p.close(p.right);
        auto e=expected(n,overlap,2);
        require(contents(target)==e,"existing destination survives source Pool removal");
        for(unsigned i=0;i<n;++i){target.erase(key(i));e.erase(key(i));}
        fill(target,0,n,3,1);
        for(unsigned i=0;i<n;++i)for(unsigned j=0;j<fanout(i,1);++j)add(e,key(i),bytes(i,3,j));
        order(e);require(contents(target)==e,"folded owner reuses its original Pool authority");
        if constexpr(requires{target.validate();})target.validate();
    }
}
void cross_layout_check() {
    using Bags=linked_bags<(W?W:8)>;
    using Source=types<true,Bags,8,true>;
    using Target=promoted_table<K,Kind,W,2,1,192,128,record_format::exact,false,record_access::exact,false,Bags,64>;
    pools p;auto a=Source::make(*p.left,17),b=Source::make(*p.right,17);
    small_table<K,Kind,W,false,Bags,64> empty(*p.target,17);
    auto target=Target::promote(empty,*p.target,17,901);
    require(bool(target),"cross-layout destination admission");
    fill(a,0,192,0,2);fill(b,96,192,1,2);
    require(consolidation_access<typename Source::table>::same_mapping(a,*target),
        "same primary permutation admits different units, inline budget and secondary mapping");
    transfer<true>(a,*target,7,64);transfer<true>(b,*target,7,64);
    target->validate();require(contents(*target)==expected(192,true,2),"encoded cross-layout complete bytes and multiplicity");
}
template<bool Encoded=false> void pressure_check() {
    using T=types<false>;using Table=typename T::table;
    pools p(8ULL<<20);auto source=T::make(*p.left,17),target=T::make(*p.target,Encoded?17:127);
    fill(source,0,192,0,2);const auto original=contents(source);
    value_cursor<Table,Encoded> cursor;no_small_effects effects;
    auto accept=[&](auto k,auto b){return consolidation_access<Table>::accept(target,k,b,combine{},effects);};
    auto r=cursor.step(source,7,64,accept);require(r.state==record_scan_state::yielded,"private accepted merge prefix");
    const auto prefix=contents(target);
    std::array<small_event,0> none;small_journal refused{none};
    r=cursor.step(source,64,64,[&](auto k,auto b){return consolidation_access<Table>::accept(target,k,b,combine{},refused);});
    require(r.state==record_scan_state::refused && contents(target)==prefix,"effect refusal commits no group");
    std::vector<oa::Allocation> pressure;
    for(std::size_t n=4096;n>=8;n/=2)while(auto a=oa::allocate(*p.target,{.bytes=n,.alignment=8}))pressure.push_back(std::move(*a));
    do {r=cursor.step(source,64,64,accept);}while(r.state==record_scan_state::yielded);
    require(r.state==record_scan_state::refused,"actual allocation pressure refuses before complete merge");
    const auto paused=contents(target);
    for(unsigned i=0;i<2;++i) {
        r=cursor.step(source,64,64,accept);
        require(r.state==record_scan_state::refused&&contents(target)==paused,"capacity retries do not replay values");
    }
    require(contents(source)==original,"allocation refusal preserves source reader");
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*p.target,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"merge pressure recovery");orbital::worker::poll(p.shared.lane,1024);
    }
    do {r=cursor.step(source,64,64,accept);require(r.state!=record_scan_state::refused,"resumed group allocation");}
    while(r.state!=record_scan_state::done);
    require(contents(target)==original&&contents(source)==original,"capacity recovery exact byte/multiplicity oracle");
}
template<bool Exact> struct baseline {
    using key_type=typename small_abseil<K,Kind,Exact>::key_type;
    using bag=absl::InlinedVector<value,1>;
    using mapped=std::conditional_t<Kind==container_kind::multimap,bag,value>;
    using storage_type=std::conditional_t<Kind==container_kind::set,absl::flat_hash_set<key_type>,absl::flat_hash_map<key_type,mapped>>;
    storage_type storage;
    void set_bytes(word k,const value& v) {
        if constexpr(Kind==container_kind::set){[[clang::always_inline]]storage.insert(key_type(k));}
        else if constexpr(Kind==container_kind::map){[[clang::always_inline]]storage.insert_or_assign(key_type(k),v);}
        else {std::pair<typename storage_type::iterator,bool> p;[[clang::always_inline]]p=storage.try_emplace(key_type(k));p.first->second.push_back(v);}
    }
    void merge(const baseline& source,unsigned grain) {
        unsigned work=0;
        for(const auto& entry:source.storage) {
            if constexpr(Kind==container_kind::set){[[clang::always_inline]]storage.insert(entry);++work;}
            else {
                std::pair<typename storage_type::iterator,bool> p;
                if constexpr(Kind==container_kind::map) {
                    [[clang::always_inline]]p=storage.try_emplace(entry.first,entry.second);
                    if(!p.second)p.first->second=combine{}(p.first->second,entry.second);++work;
                }else {
                    [[clang::always_inline]]p=storage.try_emplace(entry.first);
                    // One key lookup per bag. Give the baseline its efficient
                    // contiguous append; it need not simulate chain traversal.
                    const auto& from=entry.second;auto& to=p.first->second;
                    for(std::size_t at=0;at<from.size();) {
                        const auto n=std::min<std::size_t>({64,grain,from.size()-at});
                        to.insert(to.end(),from.begin()+at,from.begin()+at+n);at+=n;work+=n;
                        if(work>=grain){asm volatile("":::"memory");work=0;}
                    }
                }
            }
            if(work>=grain){asm volatile("":::"memory");work=0;}
        }
    }
    template<class F> void each_bytes(F&& f)const {
        auto key=[](const auto& k)->word {if constexpr(Exact)return k.value();else return k;};
        if constexpr(Kind==container_kind::set)for(auto k:storage)f(key(k),std::span<const byte>{});
        else for(const auto& [k,v]:storage) {
            if constexpr(Kind==container_kind::map)f(key(k),std::span<const byte>(v));
            else for(const auto& b:v)f(key(k),std::span<const byte>(b));
        }
    }
    memory footprint()const {
        word bytes=absl::container_internal::AllocatedByteSize(storage);
        if constexpr(Kind==container_kind::multimap)for(const auto& [k,v]:storage) {
            const auto p=reinterpret_cast<std::uintptr_t>(v.data()),b=reinterpret_cast<std::uintptr_t>(&v);
            if(p<b||p>=b+sizeof(v))bytes+=v.capacity()*W;
        }
        return {bytes,0,sizeof(*this)};
    }
};
struct sample {double build=0,merge=0,scan=0,release=0;word logical=0,backing=0,coexist=0,sum=0;};
double ns(clock_type::time_point a,clock_type::time_point b){return std::chrono::duration<double,std::nano>(b-a).count();}
template<class T> word consume(const T& table) {
    word out=0;table.each_bytes([&](word k,auto b){out+=mix(k);for(byte v:b)out+=v;});
    asm volatile(""::"r"(out):"memory");return out;
}
template<int Arm,bool Large> [[gnu::noinline]] sample history(pools& p,unsigned n,bool overlap,unsigned pattern,unsigned grain,bool keep_left,benchmark_oracle<value>& validation) {
    constexpr bool ours=Arm<2 || Arm>=4;
    using T=types<Large>;using Table=std::conditional_t<ours,typename T::table,baseline<Arm==3>>;
    auto make=[&](oa::Pool& pool,unsigned seed)->Table {
        if constexpr(ours)return T::make(pool,seed);else return {};
    };
    sample s;auto start=clock_type::now();
    std::optional<Table> a(make(*p.left,17)),b(make(*p.right,Arm>=4?17:41)),fresh;
    if(!keep_left)fresh.emplace(make(*p.target,Arm>=4?17:127));
    Table* target=keep_left?&*a:&*fresh;
    if constexpr(!ours){a->storage.reserve(n);b->storage.reserve(n);if(fresh)fresh->storage.reserve(n);}
    fill(*a,0,n,0,pattern);fill(*b,overlap?n/2:n,n,1,pattern);
    auto built=clock_type::now();
    if constexpr(ours){if(!keep_left)transfer<Arm==5>(*a,*target,grain,Arm?64:1);transfer<Arm==5>(*b,*target,grain,Arm?64:1);}
    else {if(!keep_left)target->merge(*a,grain);target->merge(*b,grain);}
    auto merged=clock_type::now();s.sum=consume(*target);auto scanned=clock_type::now();
    const auto af=a->footprint(),bf=b->footprint(),tf=target->footprint();
    s.logical=tf.requested+tf.metadata;s.backing=tf.backing;
    s.coexist=s.logical+bf.requested+bf.metadata+(keep_left?0:af.requested+af.metadata);
    // Validate complete values without allocator traffic between phases.
    validation.check_bytes(*target);
    auto retiring=clock_type::now();fresh.reset();b.reset();a.reset();auto end=clock_type::now();
    s.build=ns(start,built);s.merge=ns(built,merged);s.scan=ns(merged,scanned);s.release=ns(retiring,end);return s;
}
template<int Arm,bool Large> void measure(pools& p,unsigned n,bool overlap,unsigned pattern,unsigned grain,unsigned rep,unsigned histories,bool keep_left) {
    benchmark_oracle<value> validation(expected(n,overlap,pattern));
    sample sum;
    for(unsigned i=0;i<histories;++i) {
        const auto s=history<Arm,Large>(p,n,overlap,pattern,grain,keep_left,validation);
        sum.build+=s.build;sum.merge+=s.merge;sum.scan+=s.scan;sum.release+=s.release;
        sum.logical+=s.logical;sum.backing+=s.backing;sum.coexist+=s.coexist;sum.sum+=s.sum;
    }
    word units=0;for(unsigned source=unsigned(keep_left);source<2;++source) {
        const unsigned first=source?(overlap?n/2:n):0;
        for(unsigned i=first;i<first+n;++i)units+=fanout(i,pattern);
    }
    const char* names[]={"scalar_groups","grouped","abseil","abseil_exact","grouped_same_seed","encoded_same_seed"};
    const double d=histories;
    std::printf("%u,%u,%u,%s,%u,%u,%u,%u,%u,%u,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%u,%s\n",
        K,unsigned(Kind),W,names[Arm],n,overlap,pattern,grain,rep,histories,(unsigned long long)units,
        sum.build/d,sum.merge/d,sum.scan/d,sum.release/d,(sum.build+sum.merge+sum.scan+sum.release)/d,
        sum.merge/d/units,sum.logical/d,(Arm<2 || Arm>=4)?sum.backing/d:-1,sum.coexist/d,(unsigned long long)sum.sum,
        unsigned(HT_EXTERNAL_BAG_PATCH && HT_BAG_SPILL_PATCH),keep_left?"keep_left":"fresh");
}
template<bool Large> void campaign(pools& p,unsigned n,unsigned reps,unsigned histories) {
    for(bool overlap:{false,true})for(unsigned pattern=0;pattern<(Kind==container_kind::multimap?3u:1u);++pattern)
      for(unsigned grain:{16u,64u})for(bool keep_left:{false,true})for(unsigned rep=0;rep<reps;++rep)for(unsigned order=0;order<6;++order)
        switch((order+rep)%6) {
        case 0:measure<0,Large>(p,n,overlap,pattern,grain,rep,histories,keep_left);break;
        case 1:measure<1,Large>(p,n,overlap,pattern,grain,rep,histories,keep_left);break;
        case 2:measure<2,Large>(p,n,overlap,pattern,grain,rep,histories,keep_left);break;
        case 3:measure<3,Large>(p,n,overlap,pattern,grain,rep,histories,keep_left);break;
        case 4:measure<4,Large>(p,n,overlap,pattern,grain,rep,histories,keep_left);break;
        default:measure<5,Large>(p,n,overlap,pattern,grain,rep,histories,keep_left);break;
        }
}
} // namespace ht::consolidation_test
int main(int argc,char** argv) {
    using namespace ht;using namespace consolidation_test;
    if(argc<2||std::string_view(argv[1])=="check") {
        copy_check();pressure_check();pressure_check<true>();cross_layout_check();
        check<false>(96);if constexpr(K>8)check<true>(300);
        fold_check<false>(96);fold_check<false,true>(96);
        if constexpr(K>8){fold_check<true>(300);fold_check<true,true>(300);}
        check<false,linked_bags<(W?W:8)>,8,true>(96);
        if constexpr(K>8)check<true,linked_bags<(W?W:8)>,8,true>(300);
        check<true,linked_bags<(W?W:8)>,8,true,true>(192);
        if constexpr(Kind==container_kind::multimap) {
            check<false,buffer_bags<W>,64>(96);
            check<false,pooled_vector_bags<W,true,16>,64>(96);
        }
        std::puts("bounded consolidation: exact groups, refusal/retry, retained sources, destination reuse and source-Pool retirement pass");return 0;
    }
    require(std::string_view(argv[1])=="bench","consolidation mode");
    const unsigned reps=argc>2?std::stoul(argv[2]):5,histories=argc>3?std::stoul(argv[3]):16;
    require(reps&&histories,"positive measurements");pools p;
    std::puts("K,kind,W,arm,source_keys,overlap,pattern,grain,rep,histories,input_values,build_ns,merge_ns,scan_ns,release_ns,lifetime_ns,merge_ns_per_input,logical,backing,coexist_logical,digest,patches,merge_mode");
    campaign<false>(p,128,reps,histories);
    if constexpr(K>8)campaign<true>(p,1024,reps,histories);
}

#include "migration.h"
#include "small_baseline.h"
#include <absl/container/internal/hashtable_debug.h>
#include <map>
#include <string>

#ifndef HT_PREPARED_KEY
#define HT_PREPARED_KEY 40
#endif
namespace ht::prepared_list_test {
constexpr unsigned K=HT_PREPARED_KEY;
word key(unsigned i){return word(i)*0xd6e8feb86659fd93ULL&mask(K);}
template<unsigned W> using bytes=std::array<byte,W>;
template<unsigned W> bytes<W> value(unsigned i,unsigned occurrence) {
    bytes<W> out;for(unsigned j=0;j<W;++j)out[j]=byte(i*37+occurrence*19+j*113);return out;
}
template<container_kind Kind,unsigned W> struct types {
    using small=small_table<K,Kind,W>;
    static constexpr unsigned A=std::min(K-1,W<=8?4u:W<=16?5u:W<=32?6u:7u);
    using large=promoted_table<K,Kind,W,A,A-2,192,96>;
    using move=hierarchy_migration<small,large>;
};
template<class Table> auto contents(const Table& t) {
    std::map<word,std::vector<std::vector<byte>>> out;
    t.each_bytes([&](word k,auto v){out[k].emplace_back(v.begin(),v.end());});
    for(auto& [k,v]:out)std::sort(v.begin(),v.end());return out;
}
template<container_kind Kind,unsigned W,class Table> void fill(Table& t,unsigned n) {
    for(unsigned i=0;i<n;++i) {
        const unsigned count=Kind==container_kind::multimap?1+i%9:1;
        for(unsigned j=0;j<count;++j)t.set_bytes(key(i),value<W>(i,j));
    }
}
template<container_kind Kind,unsigned W> auto oracle(unsigned n) {
    std::map<word,std::vector<std::vector<byte>>> out;
    for(unsigned i=0;i<n;++i)for(unsigned j=0;j<(Kind==container_kind::multimap?1+i%9:1);++j) {
        const auto v=value<W>(i,j);out[key(i)].emplace_back(v.begin(),v.end());
    }
    for(auto& [k,v]:out)std::sort(v.begin(),v.end());return out;
}
template<container_kind Kind,unsigned W> void check(oa::Pool& pool) {
    using T=types<Kind,W>;using Move=typename T::move;
    {
        std::optional<typename T::small> empty(std::in_place,pool,117);
        auto move=Move::prepare(empty,41,71);require(bool(move),"empty list preparation");
        const auto r=move->step(0);require(r.state==record_scan_state::done&&r.work==0,"empty zero-budget completion");
        auto target=move->finish();require(!empty&&target.size()==0&&target.value_count()==0,"empty ownership transfer");
    }
    for(unsigned grain:{0u,1u,7u,64u,256u}) {
        std::optional<typename T::small> source(std::in_place,pool,117);
        fill<Kind,W>(*source,251);const auto expected=oracle<Kind,W>(251);
        auto retained=source->clone();require(bool(retained),"prepared list retained clone");
        typename T::small::scan_cursor at;unsigned accepted=0,attempts=0;bool refuse=true;
        for(;;) {
            const auto r=source->scan_records(at,std::max(1u,grain),[&](word,const auto&) {
                ++attempts;if(refuse){refuse=false;return false;}
                refuse=true;++accepted;return true;
            });
            require(r.work<=std::max(1u,grain),"bounded list scan work");
            if(r.state==record_scan_state::done)break;
        }
        require(accepted==251&&attempts==502,"refused row retried, committed row never replayed");
        {
            auto cancelled=Move::prepare(source,41,71);require(bool(cancelled),"cancelled preparation");
            require(cancelled->step(7).records==7,"prepared partial copy");
        }
        require(contents(*source)==expected,"cancellation leaves complete list and bags");
        auto move=Move::prepare(source,831,971);require(bool(move),"list index preparation");
        auto initial=move->step(grain);
        require(initial.work<=grain&&initial.records<=grain,"zero and bounded preparation");
        unsigned copied=initial.records;
        while(!move->ready()) {
            auto r=move->step(std::max(1u,grain));copied+=r.records;
            require(r.state!=record_scan_state::refused&&r.work<=std::max(1u,grain),"prepared list progress");
        }
        require(copied==251&&contents(*source)==expected,"ready source owns all values until commit");
        const auto count=source->value_count();auto target=move->finish();move.reset();
        require(!source&&target.size()==251&&target.value_count()==count,"one final ownership transfer");
        require(contents(target)==expected,"prepared target complete bytes and multiplicity");
        for(unsigned i=0;i<251;i+=2)target.erase(key(i));
        auto changed=expected;
        for(unsigned i=0;i<251;i+=2) {
            const auto v=value<W>(i,91);target.set_bytes(key(i),v);
            changed[key(i)]={std::vector<byte>(v.begin(),v.end())};
        }
        target.validate();require(contents(target)==changed,"post-commit target reuse bytes");
        require(contents(*retained)==expected,"retained copy survives post-commit reuse");
    }
    // Two ready owners can still cancel together, or commit only after both
    // prepare. This does not emulate publication or concurrent Worker handoff.
    std::optional<typename T::small> a(std::in_place,pool,17),b(std::in_place,pool,17);
    fill<Kind,W>(*a,128);fill<Kind,W>(*b,16);
    auto first=Move::prepare(a,19,23),second=Move::prepare(b,19,23);
    require(bool(first)&&bool(second),"paired preparation");
    while(!first->ready())first->step(7);
    second->step(3);second.reset();first.reset();
    require(contents(*a)==oracle<Kind,W>(128)&&contents(*b)==oracle<Kind,W>(16),"cancel pair before epoch commit");
}

void pressure_check() {
    pool_owner pool(1u<<20);
    using Small=small_table<K,container_kind::multimap,64>;
    using Target=promoted_table<K,container_kind::multimap,64,1,0,96,96>;
    using Move=hierarchy_migration<Small,Target>;
    std::optional<Small> source(std::in_place,*pool.pool,17);
    fill<container_kind::multimap,64>(*source,192);
    const auto expected=oracle<container_kind::multimap,64>(192);
    auto move=Move::prepare(source,137,6151);require(bool(move),"pressure index preparation");
    std::vector<oa::Allocation> pressure;
    for(std::size_t n=4096;n>=8;n/=2)
        while(auto allocation=oa::allocate(*pool.pool,{.bytes=n,.alignment=8}))pressure.push_back(std::move(*allocation));
    require(!Move::prepare(source,831,971),"second preparation can refuse without consuming source");
    unsigned copied=0;record_scan_result r{record_scan_state::yielded};
    while(r.state==record_scan_state::yielded){r=move->step(7);copied+=r.records;}
    require(r.state==record_scan_state::refused&&copied>0,"capacity refusal after private accepted prefix");
    for(unsigned i=0;i<2;++i) {
        r=move->step(64);require(r.state==record_scan_state::refused&&r.records==0,"capacity retry does not replay prefix");
    }
    require(contents(*source)==expected,"capacity refusal retains complete source and bags");
    pressure.clear();const auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"pressure trim progress");orbital::worker::poll(pool.lane,1024);
    }
    while(!move->ready()){r=move->step(7);require(r.state!=record_scan_state::refused,"capacity recovery");copied+=r.records;}
    auto target=move->finish();move.reset();
    require(!source&&copied==192&&contents(target)==expected,"pressure recovery transfers each record and bag once");
}

// Ordinary stateless-allocator baselines. Inventory stays outside timed loops.
template<container_kind Kind,unsigned W,bool Exact> struct Baseline {
    using key_type=typename small_abseil<K,Kind,Exact>::key_type;
    using bag=absl::InlinedVector<bytes<W>,1>;
    using mapped=std::conditional_t<Kind==container_kind::multimap,bag,bytes<W>>;
    using storage_type=std::conditional_t<Kind==container_kind::set,absl::flat_hash_set<key_type>,absl::flat_hash_map<key_type,mapped>>;
    storage_type storage;
    void set_bytes(word k,const bytes<W>& v) {
        if constexpr(Kind==container_kind::set){[[clang::always_inline]]storage.insert(key_type(k));}
        else if constexpr(Kind==container_kind::map){[[clang::always_inline]]storage.insert_or_assign(key_type(k),v);}
        else {std::pair<typename storage_type::iterator,bool> p;
            [[clang::always_inline]]p=storage.try_emplace(key_type(k));p.first->second.push_back(v);}
    }
    template<class Consume> [[gnu::always_inline]] bool get_bytes(word k,Consume&& consume)const {
        typename storage_type::const_iterator at;[[clang::always_inline]]at=storage.find(key_type(k));
        if(at==storage.end())return false;
        if constexpr(Kind==container_kind::set)consume(std::span<const byte,0>{});
        else if constexpr(Kind==container_kind::map)consume(at->second);
        else for(const auto& v:at->second)consume(v);
        return true;
    }
    memory footprint()const {
        word n=absl::container_internal::AllocatedByteSize(storage);
        if constexpr(Kind==container_kind::multimap)for(const auto& [k,v]:storage) {
            const auto p=reinterpret_cast<std::uintptr_t>(v.data()),b=reinterpret_cast<std::uintptr_t>(&v);
            if(p<b||p>=b+sizeof(v))n+=v.capacity()*W;
        }
        return {n,0,sizeof(*this)};
    }
};
struct sample {word sum=0;memory before{},coexist{},after{};};
template<class Table> word consume(const Table& t,unsigned n) {
    word sum=0;for(unsigned i=0;i<n;++i) {
        const bool found=t.get_bytes(key(i),[&] [[gnu::always_inline]] (auto v) {
            ++sum;for(byte b:v)sum+=b;
        });require(found,"prepared complete read");
    }
    asm volatile(""::"r"(sum):"memory");return sum;
}
template<container_kind Kind,unsigned W,int Arm,bool Inspect> [[gnu::noinline]] sample history(oa::Pool& pool,unsigned n,unsigned grain) {
    sample out;
    if constexpr(Arm>=2) {
        Baseline<Kind,W,Arm==3> table;table.storage.reserve(n);fill<Kind,W>(table,n);
        if constexpr(Inspect)out.before=out.coexist=out.after=table.footprint();
        out.sum=consume(table,n);
    } else {
        using T=types<Kind,W>;
        std::optional<typename T::small> source(std::in_place,pool,17);fill<Kind,W>(*source,n);
        if constexpr(Inspect)out.before=source->footprint();
        std::optional<typename T::large> target;
        if constexpr(Arm==0) {
            target=T::large::promote(*source,pool,19,23);require(bool(target),"synchronous promotion");
            source.reset();
        } else {
            auto move=T::move::prepare(source,19,23);require(bool(move),"prepared promotion");
            while(!move->ready())require(move->step(grain).state!=record_scan_state::refused,"timed prepared progress");
            if constexpr(Inspect) {
                const auto a=source->footprint(),b=move->footprint();
                out.coexist={a.requested+b.requested,a.backing+b.backing,a.metadata+b.metadata};
            }
            target.emplace(move->finish());
        }
        if constexpr(Inspect)out.after=target->footprint();
        out.sum=consume(*target,n);
    }
    return out;
}
template<container_kind Kind,unsigned W,int Arm> void measure(oa::Pool& pool,unsigned n,unsigned grain,unsigned rep,unsigned repeats) {
    word expected=0;for(const auto& [k,vs]:oracle<Kind,W>(n))for(const auto& v:vs){++expected;for(byte b:v)expected+=b;}
    const auto inventory=history<Kind,W,Arm,true>(pool,n,grain);require(inventory.sum==expected,"inventory history oracle");
    word sum=0;auto begin=clock_type::now();
    for(unsigned i=0;i<repeats;++i)sum+=history<Kind,W,Arm,false>(pool,n,grain).sum;
    const auto ns=std::chrono::duration<double,std::nano>(clock_type::now()-begin).count()/repeats;
    require(sum==expected*repeats,"complete timed lifetime oracle");
    const char* names[]={"synchronous","prepared","abseil","abseil_exact"};
    // Synchronous promotion exposes no coexistence sampling boundary. Abseil's
    // accessible backing is unmeasured. Never encode those absences as zero.
    const auto coexist=Arm==0?std::int64_t(-1):std::int64_t(inventory.coexist.requested+inventory.coexist.metadata);
    const auto backing=Arm>=2?std::int64_t(-1):std::int64_t(inventory.after.backing);
    const auto coexist_backing=Arm==1?std::int64_t(inventory.coexist.backing):std::int64_t(-1);
    std::printf("%u,%u,%u,%s,%u,%u,%u,%u,%.6f,%llu,%llu,%lld,%lld,%lld,%llu\n",K,unsigned(Kind),W,names[Arm],n,grain,rep,repeats,ns,
        (unsigned long long)(inventory.before.requested+inventory.before.metadata),
        (unsigned long long)(inventory.after.requested+inventory.after.metadata),
        (long long)coexist,(long long)backing,(long long)coexist_backing,(unsigned long long)expected);
}
template<container_kind Kind,unsigned W> void run(oa::Pool& pool,bool checking,unsigned reps,unsigned repeats) {
    if(checking){check<Kind,W>(pool);return;}
    for(unsigned n:{16u,128u,240u})for(unsigned grain:{8u,64u,256u})for(unsigned rep=0;rep<reps;++rep)
      for(unsigned order=0;order<4;++order)switch((order+rep)%4) {
        case 0:measure<Kind,W,0>(pool,n,grain,rep,repeats);break;
        case 1:measure<Kind,W,1>(pool,n,grain,rep,repeats);break;
        case 2:measure<Kind,W,2>(pool,n,grain,rep,repeats);break;
        default:measure<Kind,W,3>(pool,n,grain,rep,repeats);break;
      }
}
}
int main(int argc,char** argv) {
    using namespace ht;using namespace prepared_list_test;
    const bool checking=argc<2||std::string_view(argv[1])=="check";
    const unsigned reps=argc>2?std::stoul(argv[2]):7,repeats=argc>3?std::stoul(argv[3]):64;
    require(checking||std::string_view(argv[1])=="bench","prepared list mode");require(reps&&repeats,"positive repetitions");
    pool_owner pool;
    if(checking)pressure_check();
    if(!checking)std::puts("K,kind,W,arm,keys,grain,rep,histories,lifetime_ns,before_logical,after_logical,coexist_logical,after_backing,coexist_backing,digest");
    run<container_kind::set,0>(*pool.pool,checking,reps,repeats);
    run<container_kind::map,2>(*pool.pool,checking,reps,repeats);
    run<container_kind::map,64>(*pool.pool,checking,reps,repeats);
    run<container_kind::multimap,2>(*pool.pool,checking,reps,repeats);
    run<container_kind::multimap,64>(*pool.pool,checking,reps,repeats);
    if(checking)std::puts("Prepared lists: bounded/refused scans, cancellation, complete bytes, retained bags and commit pass");
}

#include "migration.h"
#include "small_baseline.h"
#include <absl/container/internal/hashtable_debug.h>
#include <map>
#include <string_view>
#ifndef HT_MIGRATION_K
#define HT_MIGRATION_K 0
#endif

namespace ht {
template<unsigned K> word migration_key(unsigned i) {return word(i)*0xd6e8feb86659fd93ULL&mask(K);}
template<class Body> auto migration_contents(const Body& body) {
    std::map<word,std::vector<word>> out;
    body.each([&](word k,word v){out[k].push_back(v);});
    for(auto& [k,v]:out)std::sort(v.begin(),v.end());return out;
}
template<class F> double migration_time(F&& f) {
    const auto begin=clock_type::now();f();
    return std::chrono::duration<double,std::nano>(clock_type::now()-begin).count();
}

// This timing owner contains the ordinary container itself, without allocator
// counters or a heap wrapper. Inventories run outside every timed phase.
template<unsigned K,container_kind Kind,bool Exact> struct migration_abseil {
    using reference=small_abseil<K,Kind,Exact>;
    using key_type=typename reference::key_type;
    typename reference::ordinary storage;
    [[gnu::always_inline]] void set_value(word k,word v) {
        if constexpr(Kind==container_kind::set) {[[clang::always_inline]] storage.insert(key_type(k));}
        else if constexpr(Kind==container_kind::map) {[[clang::always_inline]] storage.insert_or_assign(key_type(k),v);}
        else {
            std::pair<typename reference::ordinary::iterator,bool> result;
            [[clang::always_inline]] result=storage.try_emplace(key_type(k));result.first->second.push_back(v);
        }
    }
    template<class Consume> [[gnu::always_inline]] bool get(word key,Consume&& consume)const {
        typename reference::ordinary::iterator at;
        [[clang::always_inline]] at=const_cast<typename reference::ordinary&>(storage).find(key_type(key));
        if(at==storage.end())return false;
        if constexpr(Kind==container_kind::set)consume(word(0));
        else if constexpr(Kind==container_kind::map)consume(at->second);
        else for(auto v:at->second)consume(v);
        return true;
    }
    template<class Visit> void each(Visit&& visit)const {
        auto key=[](const auto& k)->word {if constexpr(Exact)return k.value();else return k;};
        if constexpr(Kind==container_kind::set)for(auto k:storage)visit(key(k),word(0));
        else for(const auto& [k,value]:storage) {
            if constexpr(Kind==container_kind::map)visit(key(k),value);
            else for(auto v:value)visit(key(k),v);
        }
    }
    memory footprint()const {
        word bytes=absl::container_internal::AllocatedByteSize(storage);
        if constexpr(Kind==container_kind::multimap)for(const auto& [k,values]:storage) {
            const auto data=reinterpret_cast<std::uintptr_t>(values.data());
            const auto object=reinterpret_cast<std::uintptr_t>(&values);
            if(data<object||data>=object+sizeof(values))bytes+=values.capacity()*sizeof(word);
        }
        return {bytes,0,sizeof(*this)};
    }
};

template<unsigned K,container_kind Kind> void scan_check(oa::Pool& pool) {
    using Table=record_table<K,Kind,(Kind==container_kind::set?0:8),2,0,64,64>;
    Table table(pool,71,91);
    for(unsigned i=0;i<256;++i) {
        typename Table::record row;
        for(unsigned j=0;j<row.payload.size();++j)row.payload[j]=byte(i*19+j);
        if constexpr(Kind==container_kind::multimap)row.tag=byte(i&1);
        require(table.insert_record(migration_key<K>(i),row),"scan source");
    }
    for(unsigned grain:{1u,7u,64u,4096u}) {
        typename Table::scan_cursor cursor;std::map<word,unsigned> seen;
        unsigned accepted=0,attempts=0,calls=0;bool refuse=true;
        for(;;) {
            const auto result=table.scan(cursor,grain,[&](word key,const auto& row) {
                ++attempts;
                if(refuse){refuse=false;return false;}
                refuse=true;require(seen.emplace(key,1).second,"no replay after refusal");
                unsigned i=0;while(i<256&&migration_key<K>(i)!=key)++i;
                require(i<256,"scan independently enumerated key");
                for(unsigned j=0;j<row.payload.size();++j)require(row.payload[j]==byte(i*19+j),"opaque migrated row");
                if constexpr(Kind==container_kind::multimap)require(row.tag==byte(i&1),"opaque bag tag");
                ++accepted;return true;
            });
            require(result.work<=grain&&result.records<=result.work,"bounded records plus headers");
            require(++calls<4096,"bounded scan terminates");
            if(result.state==record_scan_state::done)break;
        }
        require(accepted==256&&attempts==512&&seen.size()==256,"all rows refused once, then consumed once");
    }
    using Sparse=record_table<K,Kind,(Kind==container_kind::set?0:8),6,2,128,128>;
    Sparse empty(pool,9,17);typename Sparse::scan_cursor at;
    word work=0;unsigned visits=0;
    require(empty.scan(at,0,[&](word,const auto&){++visits;return true;}).work==0,"zero scan budget");
    for(;;) {
        auto r=empty.scan(at,1,[&](word,const auto&){++visits;return true;});work+=r.work;
        require(r.work<=1,"empty bucket work is bounded");
        if(r.state==record_scan_state::done)break;
    }
    require(work==68&&visits==0,"empty bucket inspections are charged");
}

template<unsigned K,container_kind Kind> void migration_check(oa::Pool& pool) {
    using Source=promoted_table<K,Kind,(Kind==container_kind::set?0:8),2,0,64,64>;
    using Target=promoted_table<K,Kind,(Kind==container_kind::set?0:8),4,1,96,96>;
    using Move=hierarchy_migration<Source,Target>;
    for(unsigned grain:{1u,7u,64u,4096u}) {
        small_table<K,Kind> list(pool,21);
        std::map<word,std::vector<word>> expected;
        for(unsigned i=0;i<256;++i) {
            const auto key=migration_key<K>(i);list.set_value(key,i);
            expected[key].push_back(Kind==container_kind::set?0:i);
            if constexpr(Kind==container_kind::multimap)for(unsigned j=0;j<i%17;++j) {
                list.set_value(key,~word(0)-j);expected[key].push_back(~word(0)-j);
            }
        }
        for(auto& [k,v]:expected)std::sort(v.begin(),v.end());
        const auto original=expected;
        auto source=Source::promote(list,pool,71,91);require(bool(source),"migration initial hierarchy");
        auto retained=source->clone();require(bool(retained),"independent retained owner");
        {
            auto cancelled=Move::prepare(source,981,371);require(bool(cancelled),"cancel preparation");
            require(cancelled->step(31).state!=record_scan_state::refused,"cancel partial work");
        }
        require(migration_contents(*source)==expected,"cancellation preserves source and bags");
        auto move=Move::prepare(source,1931,7183);require(bool(move),"private migration index");
        unsigned rows=0,calls=0;
        while(!move->ready()) {
            const auto result=move->step(grain);
            require(result.work<=grain&&result.state!=record_scan_state::refused,"migration grain");
            rows+=result.records;require(++calls<4096,"migration progress");
        }
        require(rows==256&&migration_contents(*source)==expected,"ready source remains readable");
        const auto values=source->value_count();auto target=move->finish();move.reset();
        require(!source&&target.size()==256&&target.value_count()==values,"atomic ownership transfer");
        require(migration_contents(target)==expected,"target survives source index retirement");
        for(const auto& [key,values]:expected) {
            std::vector<word> actual;require(target.get(key,[&](word v){actual.push_back(v);}),"migrated point lookup");
            std::sort(actual.begin(),actual.end());require(actual==values,"point lookup after reseed and geometry change");
        }
        // Subsequent bag append, swap-last erase and assigned-rank reuse exercise
        // both the transferred head and insertion hint after source retirement.
        for(unsigned i=0;i<256;i+=3){target.erase(migration_key<K>(i));expected.erase(migration_key<K>(i));}
        for(unsigned i=0;i<256;++i) {
            auto key=migration_key<K>(i);target.set_value(key,831);
            if constexpr(Kind==container_kind::multimap)expected[key].push_back(831);
            else expected[key]={Kind==container_kind::set?0:831};
        }
        for(auto& [k,v]:expected)std::sort(v.begin(),v.end());
        target.validate();require(migration_contents(target)==expected,"post-migration mutation");
        require(retained->size()==256&&migration_contents(*retained)==original,"independent retained bags survive target mutation");
    }
}

#if HT_MIGRATION_K == 0
void migration_refusal() {
    pool_owner pool(1u<<20);constexpr auto kind=container_kind::multimap;
    using Source=promoted_table<40,kind,8,5,1,128,128>;
    using Target=promoted_table<40,kind,8,2,0,64,64>; // Force destination key-overflow allocations.
    using Move=hierarchy_migration<Source,Target>;
    small_table<40,kind> list(*pool.pool,17);
    for(unsigned i=0;i<192;++i)for(unsigned j=0;j<3;++j)list.set_value(migration_key<40>(i),i+j);
    auto source=Source::promote(list,*pool.pool,31,93);require(bool(source),"pressure source");
    const auto expected=migration_contents(*source);
    auto move=Move::prepare(source,137,6151);require(bool(move),"pressure private index");
    std::vector<oa::Allocation> pressure;
    for(std::size_t bytes=4096;bytes>=8;bytes/=2)
        while(auto a=oa::allocate(*pool.pool,{.bytes=bytes,.alignment=8}))pressure.push_back(std::move(*a));
    require(!Move::prepare(source,831,971),"initial allocation refusal");
    unsigned copied=0;record_scan_result result{record_scan_state::yielded};
    while(result.state==record_scan_state::yielded){result=move->step(7);copied+=result.records;}
    require(result.state==record_scan_state::refused&&copied>0,"refusal after committed private prefix");
    for(unsigned retry=0;retry<2;++retry) {
        auto r=move->step(64);require(r.state==record_scan_state::refused&&r.records==0,"same refused row on retry");
    }
    require(migration_contents(*source)==expected,"refusal leaves source intact");
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"migration pressure trim");orbital::worker::poll(pool.lane,1024);
    }
    while(!move->ready()){auto r=move->step(7);require(r.state!=record_scan_state::refused,"resumed migration");copied+=r.records;}
    require(copied==192,"no committed-prefix replay after capacity recovery");
    auto target=move->finish();move.reset();
    require(!source&&migration_contents(target)==expected,"pressure recovery and bag ownership");
}
#endif

template<unsigned K> void wide_migration_check(oa::Pool& pool) {
    constexpr auto kind=container_kind::map;
    using Source=promoted_table<K,kind,16,1,0,64,64>;
    using Target=promoted_table<K,kind,16,2,1,96,96>;
    small_table<K,kind,16> list(pool,17);no_small_effects effects;
    for(unsigned i=0;i<64;++i) {
        std::array<byte,16> bytes;for(unsigned j=0;j<16;++j)bytes[j]=byte(i*19+j);
        require(bool(list.set_bytes(migration_key<K>(i),bytes,effects)),"wide migration source");
    }
    auto source=Source::promote(list,pool,31,97);require(bool(source),"wide promotion");
    auto move=hierarchy_migration<Source,Target>::prepare(source,137,971);require(bool(move),"wide private index");
    unsigned rows=0;
    while(!move->ready()){auto r=move->step(3);require(r.state!=record_scan_state::refused,"wide migration step");rows+=r.records;}
    auto target=move->finish();move.reset();require(!source&&rows==64,"wide migration retirement");
    for(unsigned i=0;i<64;++i)require(target.get_bytes(migration_key<K>(i),[&](const auto& bytes) {
        for(unsigned j=0;j<16;++j)require(bytes[j]==byte(i*19+j),"wide migration bytes");
    }),"wide migrated lookup");
}

template<unsigned K,container_kind Kind,unsigned A,unsigned B> void migration_trial(oa::Pool& pool,unsigned arm,unsigned rep,bool report) {
    constexpr unsigned V=Kind==container_kind::set?0:8;
    using Source=promoted_table<K,Kind,V,A,B>;
    using Target=promoted_table<K,Kind,V,A+1,B+1>;
    using Move=hierarchy_migration<Source,Target>;
    using Ordinary=migration_abseil<K,Kind,false>;
    using Exact=migration_abseil<K,Kind,true>;
    constexpr unsigned capacity=record_table<K,Kind,V,A,B>::primary.shape.slots*(1u<<A);
    constexpr unsigned n=std::min(capacity*7/8,K==8?128u:~0u);
    constexpr unsigned rounds=32;
    const unsigned grain=arm==0?8u:arm==1?64u:~0u;
    const char* names[]={"migrate8","migrate64","migrate_all","abseil","abseil_exact"};
    std::optional<Source> source;std::optional<Target> target;std::optional<Move> moving;
    std::optional<Ordinary> ordinary;std::optional<Exact> exact;
    memory before{},coexist{},after{};word sum=0;unsigned steps=0,work=0;
    auto populate=[&](auto& body,unsigned first,unsigned last) {
        for(unsigned i=first;i<last;++i) {
            body.set_value(migration_key<K>(i),migration_key<K>((i+1)%n));
            if constexpr(Kind==container_kind::multimap)if(i%3==0)for(unsigned j=0;j<4;++j)body.set_value(migration_key<K>(i),mix(i+j));
        }
    };
    const auto construct=migration_time([&] {
        if(arm<3){small_table<K,Kind> list(pool,83);source=Source::promote(list,pool,83,137);require(bool(source),"timed initial index");}
        else if(arm==3)ordinary.emplace();else exact.emplace();
    });
    const auto build=migration_time([&]{if(arm<3)populate(*source,0,n);else if(arm==3)populate(*ordinary,0,n);else populate(*exact,0,n);});
    before=arm<3?source->footprint():arm==3?ordinary->footprint():exact->footprint();
    const auto prepare=arm<3?migration_time([&]{moving=Move::prepare(source,1947,7183);require(bool(moving),"timed replacement");}):0;
    const auto migrate=arm<3?migration_time([&]{while(!moving->ready()) {
        const auto r=moving->step(grain);require(r.state!=record_scan_state::refused,"timed migration capacity");++steps;work+=r.work;
    }}):0;
    coexist=before;
    if(arm<3){const auto private_memory=moving->footprint();coexist.requested+=private_memory.requested;coexist.backing+=private_memory.backing;coexist.metadata+=private_memory.metadata;}
    const auto finish=arm<3?migration_time([&]{target.emplace(moving->finish());moving.reset();}):0;
    const auto grow=migration_time([&]{if(arm<3)populate(*target,n,n*2);else if(arm==3)populate(*ordinary,n,n*2);else populate(*exact,n,n*2);});
    after=arm<3?target->footprint():arm==3?ordinary->footprint():exact->footprint();
    std::map<word,std::vector<word>> expected;
    for(unsigned i=0;i<n*2;++i) {
        auto& values=expected[migration_key<K>(i)];values.push_back(Kind==container_kind::set?0:migration_key<K>((i+1)%n));
        if constexpr(Kind==container_kind::multimap)if(i%3==0)for(unsigned j=0;j<4;++j)values.push_back(mix(i+j));
        std::sort(values.begin(),values.end());
    }
    require((arm<3?migration_contents(*target):arm==3?migration_contents(*ordinary):migration_contents(*exact))==expected,"timed full history oracle");
    auto reads=[&](const auto& body){for(unsigned round=0;round<rounds;++round)for(unsigned i=0;i<n*2;++i)
        sum+=body.get(migration_key<K>(i),[&](word v){sum+=v;});};
    if(arm<3)reads(*target);else if(arm==3)reads(*ordinary);else reads(*exact);
    const auto read=migration_time([&]{if(arm<3)reads(*target);else if(arm==3)reads(*ordinary);else reads(*exact);});
    auto serial_reads=[&](const auto& body){word at=0;for(unsigned i=0;i<rounds*n;++i) {
        if constexpr(Kind==container_kind::set){const bool found=body.get(migration_key<K>(at),[](word){});at=(at+(found?1:7))%n;sum+=found;}
        else {bool first=true;sum+=body.get(at,[&](word v){if(first){at=v;first=false;}sum+=v;});}
    }};
    if(arm<3)serial_reads(*target);else if(arm==3)serial_reads(*ordinary);else serial_reads(*exact);
    const auto serial=migration_time([&]{if(arm<3)serial_reads(*target);else if(arm==3)serial_reads(*ordinary);else serial_reads(*exact);});
    const auto release=migration_time([&]{target.reset();ordinary.reset();exact.reset();});
    asm volatile(""::"r"(sum):"memory");
    auto emit=[&](const char* phase,double ns,unsigned ops,memory m) {
        if(report)std::printf("%s,%u,%u,%u,%u,%u,%u,%s,%u,%.3f,%llu,%llu,%llu,%u,%u\n",names[arm],K,unsigned(Kind),A,B,n,rep,phase,ops,ns,
            (unsigned long long)m.requested,(unsigned long long)m.backing,(unsigned long long)m.metadata,steps,work);
    };
    emit("construct",construct,1,before);emit("build",build,n,before);emit("prepare",prepare,1,coexist);
    emit("migrate",migrate,n,coexist);emit("finish",finish,1,after);emit("insert_after",grow,n,after);
    emit("hit_after",read,rounds*n*2,after);emit(Kind==container_kind::set?"serial_caller_cycle":"serial_value_cycle",serial,rounds*n,after);
    emit("release",release,1,after);emit("lifetime_excluding_reads",construct+build+prepare+migrate+finish+grow+release,n*2,after);
}
template<unsigned K,container_kind Kind> void migration_campaign(oa::Pool& pool,unsigned reps) {
    for(unsigned arm=0;arm<5;++arm)migration_trial<K,Kind,3,1>(pool,arm,0,false);
    for(unsigned rep=0;rep<reps;++rep)for(unsigned j=0;j<5;++j)migration_trial<K,Kind,3,1>(pool,(j+rep)%5,rep,true);
    if constexpr(K>8) {
        for(unsigned arm=0;arm<5;++arm)migration_trial<K,Kind,9,5>(pool,arm,0,false);
        for(unsigned rep=0;rep<reps;++rep)for(unsigned j=0;j<5;++j)migration_trial<K,Kind,9,5>(pool,(j+rep)%5,rep,true);
    }
}
template<unsigned K> void migration_width(oa::Pool& pool,bool checking,unsigned reps) {
    if(checking) {
        scan_check<K,container_kind::set>(pool);scan_check<K,container_kind::map>(pool);scan_check<K,container_kind::multimap>(pool);
        migration_check<K,container_kind::set>(pool);migration_check<K,container_kind::map>(pool);migration_check<K,container_kind::multimap>(pool);
        wide_migration_check<K>(pool);
    }else {
        migration_campaign<K,container_kind::set>(pool,reps);migration_campaign<K,container_kind::map>(pool,reps);
        migration_campaign<K,container_kind::multimap>(pool,reps);
    }
}
} // namespace ht
#define HT_MIGRATION_JOIN_INNER(a,b) a##b
#define HT_MIGRATION_JOIN(a,b) HT_MIGRATION_JOIN_INNER(a,b)
#if HT_MIGRATION_K
void HT_MIGRATION_JOIN(migration_width_,HT_MIGRATION_K)(ht::oa::Pool& pool,bool checking,unsigned reps) {
    ht::migration_width<HT_MIGRATION_K>(pool,checking,reps);
}
#else
void migration_width_8(ht::oa::Pool&,bool,unsigned);
void migration_width_16(ht::oa::Pool&,bool,unsigned);
void migration_width_24(ht::oa::Pool&,bool,unsigned);
void migration_width_32(ht::oa::Pool&,bool,unsigned);
void migration_width_40(ht::oa::Pool&,bool,unsigned);
void migration_width_48(ht::oa::Pool&,bool,unsigned);
void migration_width_56(ht::oa::Pool&,bool,unsigned);
void migration_width_64(ht::oa::Pool&,bool,unsigned);
int main(int argc,char** argv) {
    using namespace ht;const auto mode=argc>1?std::string_view(argv[1]):"check";
    const bool checking=mode=="check";require(checking||mode=="bench","migration mode");
    const unsigned reps=argc>2?std::stoul(argv[2]):3;require(reps>0,"migration repetitions");
    if(!checking)std::puts("arm,K,kind,A,B,n,rep,phase,operations,ns,requested,owned_capacity,metadata,steps,work");
    if(checking)migration_refusal();
    pool_owner pool;
    migration_width_8(*pool.pool,checking,reps);migration_width_16(*pool.pool,checking,reps);
    migration_width_24(*pool.pool,checking,reps);migration_width_32(*pool.pool,checking,reps);
    migration_width_40(*pool.pool,checking,reps);migration_width_48(*pool.pool,checking,reps);
    migration_width_56(*pool.pool,checking,reps);migration_width_64(*pool.pool,checking,reps);
    if(checking)std::puts("all-width/all-kind bounded migration, sparse scan, cancellation, refusal and bag lifetime passed");
}
#endif

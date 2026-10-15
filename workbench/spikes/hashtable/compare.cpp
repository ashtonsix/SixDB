#include "table.h"
#include "baseline.h"
#include "input.h"
#include <unordered_map>
#include <string_view>
#include <limits>

namespace ht {
template<class Map> void verify(const Map& map,const std::unordered_map<word,word>& oracle) {
    map.validate(); unsigned seen=0;
    map.each([&](word k,word v) {auto at=oracle.find(k);require(at!=oracle.end()&&at->second==v,"iteration oracle");++seen;});
    require(seen==oracle.size(),"iteration count");
    for(auto [k,v]:oracle) {word got=~v;require(map.get_value(k,got)&&got==v,"lookup oracle");}
}
template<class Map> void staged_check(const Map& map,const std::unordered_map<word,word>& oracle,const input& in) {
    if constexpr(requires {typename Map::reader;}) {
        auto view=map.bind_read();
        for(auto key:in.queries) {
            typename Map::reader::probe p;
            word value=0xdeadbeef;bool found=true;unsigned visits=0;
            auto emit=[](const byte* p,std::size_t bytes) {require(p&&bytes,"qualified staged window");};
            view.prepare_read(p,key,emit);
            for(;;) {
                require(++visits<8192,"bounded staged traversal");
                bool done;
                if(p.next==Map::reader::stage::chain_directory)
                    done=view.advance_read(p,key,value,found,emit);
                else done=view.template advance_read<false>(p,key,value,found,emit);
                if(done) break;
            }
            auto at=oracle.find(key);
            require(found==(at!=oracle.end()),"staged found oracle");
            require(value==(found?at->second:0xdeadbeef),"staged value/miss unchanged");
        }
    }
}
template<unsigned N,class Map> void packet_check(const Map& map,const std::unordered_map<word,word>& oracle,
                                                const input& in) {
    decltype(auto) view=map.bind_read();
    for(unsigned start=0;start<in.queries.size();start+=N) {
        std::array<word,N> keys;std::array<word,N+2> out;out.fill(0xdeadbeef);
        for(unsigned i=0;i<N;++i) keys[i]=in.queries[(start+i)%in.queries.size()];
        keys[1]=keys[0]; // Repeated hits and misses must preserve lane identity.
        unsigned expected=0;
        for(unsigned i=0;i<N;++i) if(oracle.contains(keys[i])) expected|=1u<<i;
        require(view.template getN<N>(keys.data(),out.data()+1)==expected,"packet hit mask");
        require(out[0]==0xdeadbeef && out[N+1]==0xdeadbeef,"packet output guards");
        for(unsigned i=0;i<N;++i) {
            auto at=oracle.find(keys[i]);
            require(out[i+1]==(at==oracle.end()?0xdeadbeef:at->second),"packet value/miss unchanged");
        }
    }
}
template<class Map,unsigned K> void history(oa::Pool& pool,unsigned n,word seed) {
    auto in=make_input<K>(n,seed); Map map(pool,mix(seed),mix(seed+1));
    if constexpr(requires{map.reserve(n);})map.reserve(n);
    std::unordered_map<word,word> oracle;
    for(unsigned i=0;i<n;++i) {
        auto k=in.keys[i],v=(i%3==0?~word(0):mix(i));
        require(map.set(k,v)==1,"insert delta");oracle[k]=v;
    }
    verify(map,oracle);packet_check<4>(map,oracle,in);packet_check<8>(map,oracle,in);staged_check(map,oracle,in);
    for(auto k:in.absent) {word out=0xdeadbeef;require(!map.get_value(k,out)&&out==0xdeadbeef,"miss unchanged");require(!map.erase(k),"erase absent");}
    std::mt19937_64 random(seed+100);
    for(unsigned i=0;i<8*n;++i) {
        auto k=(random()&1)?in.keys[random()%n]:in.absent[random()%n];
        if(random()%3==0) {
            bool expected=oracle.erase(k)!=0; require(map.erase(k)==expected,"erase delta");
        } else {
            auto v=random();bool fresh=!oracle.contains(k);
            require(map.set(k,v)==int(fresh),"update delta");oracle[k]=v;
        }
        if(i%127==0) verify(map,oracle);
    }
    verify(map,oracle);packet_check<4>(map,oracle,in);packet_check<8>(map,oracle,in);staged_check(map,oracle,in);
    auto all=oracle;for(auto [k,v]:all)require(map.erase(k),"drain");oracle.clear();verify(map,oracle);
    for(auto k:in.keys) {require(map.set(k,0)==1,"reuse insertion");oracle[k]=0;}
    verify(map,oracle);
    // Updates must reach entries still downstream after a primary vacancy.
    for(unsigned i=0;i<n;i+=3) {map.erase(in.keys[i]);oracle.erase(in.keys[i]);}
    for(unsigned i=0;i<4*n;++i) {
        auto k=(random()&1)?in.keys[random()%n]:in.absent[random()%n];
        auto v=i%3==0?~word(0):i%3==1?word(0):random();
        auto at=oracle.find(k);bool fresh=at==oracle.end();unsigned calls=0;
        auto expected=fresh?v:at->second+v;
        auto delta=map.upsert(k,v,[&](word old,word incoming)noexcept {++calls;return old+incoming;});
        require(delta==int(fresh) && calls==unsigned(!fresh),"combine once on existing key only");
        oracle[k]=expected;
    }
    verify(map,oracle);
}
template<unsigned K,unsigned A> void inverse_check() {
    for(word seed:{0ULL,1ULL,~0ULL,0x135a652a49ULL}) {
        permutation<K,A> h(seed); auto n=K<=16?word(1)<<K:65536;
        for(word i=0;i<n;++i) {word k=K<=16?i:mix(i)&mask(K);require(h.invert(h.split(k))==k,"permutation inverse");}
    }
}
template<unsigned Bytes> void field_check() {
    admit_identity<Bytes>();
    for(word i=0;i<4096;++i) {
        word value=mix(i); std::array<byte,Bytes+2> canonical{},ordinary{};
        canonical.fill(0x5a);ordinary=canonical;
        put<Bytes>(canonical.data()+1,value);
        tp::native::write_body(prepared_field<Bytes>.write.controls(),ordinary.data()+1,value);
        require(canonical==ordinary,"identity write differential and guards");
        require(get<Bytes>(canonical.data()+1)==(value&mask(Bytes*8)),"identity independent read");
        require(tp::native::read_body(prepared_field<Bytes>.read.controls(),ordinary.data()+1)==
                get<Bytes>(canonical.data()+1),"identity read differential");
    }
}
void admission_check() {
    auto rejected=[]<unsigned Bytes>(std::span<const tp::code> codes,unsigned unit) {
        auto l=tp::layout::make(unit,codes); require(bool(l),"negative layout");
        std::array<byte,Bytes> order{};for(unsigned i=0;i<Bytes;++i)order[i]=i;
        auto r=tp::reader<8>::make(*l,order);auto w=tp::writer<8>::make(*l,order);
        require(bool(r)&&bool(w),"negative prepared projections");
        require(!identity<Bytes>::accepts(r->controls(),w->controls()),"reject nonidentity");
    };
    std::array<tp::code,1> shifted{{{0,1,7}}},offset{{{1,0,8}}};
    std::array<tp::code,2> reversed{{{1,0,8},{0,0,8}}};
    rejected.template operator()<1>(shifted,1);rejected.template operator()<1>(offset,2);
    rejected.template operator()<2>(reversed,2);
}
void refusal_check() {
    pool_owner pool(1u<<20);
    table<40,2,0,64,64> map(*pool.pool,871,932);
    std::unordered_map<word,word> oracle;
    for(word i=0;i<100;++i) {require(map.set(i,mix(i))==1,"refusal setup");oracle[i]=mix(i);}
    std::vector<oa::Allocation> pressure;
    while(auto a=oa::allocate(*pool.pool,{.bytes=4096,.alignment=64})) pressure.push_back(std::move(*a));
    word refused=0;
    for(word k=100;k<4000;++k) {
        unsigned calls=0;
        auto result=map.upsert(k,mix(k),[&](word old,word incoming)noexcept {++calls;return old+incoming;});
        require(calls==0,"new/refused accumulation does not invoke combiner");
        if(result==-1) {refused=k;break;}
        require(result==1,"refusal precursor delta");oracle[k]=mix(k);
    }
    require(refused!=0,"must force Orbital refusal"); verify(map,oracle);
    require(map.upsert(0,~word(0),[](word old,word incoming)noexcept {return old+incoming;})==0,
            "existing accumulation needs no allocation under pressure");
    oracle[0]+=~word(0);verify(map,oracle);
    word sentinel=471; require(!map.get_value(refused,sentinel)&&sentinel==471,"refusal invisible");
    // Released pool inventory remains budget-charged until explicitly trimmed.
    pressure.clear();auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"pressure trim");
        orbital::worker::poll(pool.lane,1024);std::this_thread::yield();
    }
    while(map.set(refused,mix(refused))==-1) {
        require(clock_type::now()<deadline,"refusal recovery");orbital::worker::poll(pool.lane,1024);
        std::this_thread::yield();
    }
    oracle[refused]=mix(refused);verify(map,oracle);
}
void check() {
    field_check<1>();field_check<2>();field_check<3>();field_check<4>();
    field_check<5>();field_check<6>();field_check<7>();field_check<8>();
    admission_check();refusal_check();
    pool_owner pool;
    inverse_check<8,0>();inverse_check<16,0>();inverse_check<16,9>();inverse_check<40,8>();inverse_check<64,0>();
    inverse_check<40,14>();inverse_check<40,19>();
    // Deliberate pressure, including secondary A0 and many real geometric links.
    for(unsigned seed=0;seed<4;++seed) {
        history<table<16,3,0,64,64>,16>(*pool.pool,250,100+seed);
        history<table<40,3,1,64,128>,40>(*pool.pool,300,100+seed);
        history<table<40,3,1,128,128,true>,40>(*pool.pool,300,100+seed);
        history<abseil<16>,16>(*pool.pool,250,100+seed);
    }
    history<table<8,3,0,64,64>,8>(*pool.pool,120,413);
    history<table<24,3,1,64,128>,24>(*pool.pool,300,413);
    history<table<32,3,1,64,128>,32>(*pool.pool,300,413);
    history<table<48,3,1,64,128>,48>(*pool.pool,300,413);
    history<table<56,3,1,64,128>,56>(*pool.pool,300,413);
    history<table<64,3,1,64,128>,64>(*pool.pool,300,413);
    history<table<40,8,0,64,128,false,true>,40>(*pool.pool,1800,761);
    history<table<40,8,0,128,128,false,true>,40>(*pool.pool,2500,761);
    std::puts("correctness: independent histories, inverse, forced overflow, dense deletion, link reuse, refusal/retry and identity transfer checks passed");
}
inline void consume(word x) {asm volatile("" : : "r"(x) : "memory");}
inline double elapsed(clock_type::time_point start) {
    return std::chrono::duration<double,std::nano>(clock_type::now()-start).count();
}
// One visible caller boundary for all arms; kernels fuse into the same consumer.
template<class Map> [[gnu::noinline]] word dependent(const Map& map,word first,word count) {
    word key=first;
    for(word i=0;i<count;++i) {word value=key;map.get_value(key,value);key=value;}
    consume(key);return key;
}
template<class Map> [[gnu::noinline]] word independent(const Map& map,const std::vector<word>& query,unsigned rounds) {
    word sums[4]{};
    for(unsigned r=0;r<rounds;++r) {
        std::size_t i=0;
        for(;i+4<=query.size();i+=4) {
            word a=0,b=0,c=0,d=0;
            map.get_value(query[i],a);map.get_value(query[i+1],b);
            map.get_value(query[i+2],c);map.get_value(query[i+3],d);
            sums[0]+=a;sums[1]+=b;sums[2]+=c;sums[3]+=d;
        }
        for(;i<query.size();++i) {word v=0;map.get_value(query[i],v);sums[0]+=v;}
    }
    word sum=sums[0]+sums[1]+sums[2]+sums[3];consume(sum);return sum;
}
template<class Map> [[gnu::noinline]] word cohort_dependent(const Map& map,word first,word count) {
    decltype(auto) view=map.bind_read();
    word key=first;
    for(word i=0;i<count;++i) {word value=key;view.get_value(key,value);key=value;}
    consume(key);return key;
}
template<unsigned N,class Map> [[gnu::noinline]] word packets(const Map& map,const std::vector<word>& query,unsigned rounds) {
    decltype(auto) view=map.bind_read();
    std::array<word,N> sums{};
    for(unsigned r=0;r<rounds;++r) {
        std::size_t i=0;
        for(;i+N<=query.size();i+=N) {
            std::array<word,N> out{};
            view.template getN<N>(query.data()+i,out.data());
            lanes<N>([&](auto lane){sums[lane]+=out[lane];});
        }
        for(;i<query.size();++i) {word out=0;view.get_value(query[i],out);sums[0]+=out;}
    }
    word sum=0;lanes<N>([&](auto i){sum+=sums[i];});consume(sum);return sum;
}
void row(const char* name,unsigned k,unsigned rep,const char* phase,unsigned n,word ops,double ns) {
    std::printf("time,%s,%u,%u,%s,%u,%llu,%.6f\n",name,k,rep,phase,n,(unsigned long long)ops,ns/double(ops));
}
template<class Map,unsigned K> [[gnu::noinline]] void trial(oa::Pool& pool,const char* name,const input& in,unsigned rounds,unsigned rep) {
    auto n=unsigned(in.keys.size());
    auto begin=clock_type::now();
    std::optional<Map> storage; storage.emplace(pool,mix(8301+rep*2),mix(8302+rep*2));
    auto& map=*storage;
    if constexpr(requires{map.reserve(n);})map.reserve(n);
    for(unsigned i=0;i<n;++i) require(map.set(in.keys[i],in.keys[(i+1)%n])==1,"build delta");
    row(name,K,rep,"build",n,n,elapsed(begin));
    auto mem=map.footprint();
    std::printf("memory,%s,%u,%u,%u,%llu,%llu,%llu\n",name,K,rep,n,(unsigned long long)mem.requested,(unsigned long long)mem.backing,(unsigned long long)mem.metadata);
    if constexpr(requires{map.distribution();}) {
        auto d=map.distribution();
        std::printf("route,%s,%u,%u,%u,%llu,%llu,%llu\n",name,K,rep,n,(unsigned long long)d.primary,(unsigned long long)d.secondary,(unsigned long long)d.chain);
    }
    auto count=word(n)*rounds;
    if constexpr(requires{map.filter_stats(in.absent);}) {
        auto f=map.filter_stats(in.absent);
        std::printf("filter,%s,%u,%u,%u,%llu,%llu,%llu\n",name,K,rep,n,(unsigned long long)f[0],(unsigned long long)f[1],(unsigned long long)f[2]);
    }
    require(dependent(map,in.keys[0],n)==in.keys[0],"dependent cycle");
    begin=clock_type::now();auto answer=dependent(map,in.keys[0],count);double serial=elapsed(begin);
    require(answer==in.keys[0],"serial result");row(name,K,rep,"dependent",n,count,serial);
    begin=clock_type::now();answer=cohort_dependent(map,in.keys[0],count);serial=elapsed(begin);
    require(answer==in.keys[0],"cohort serial result");row(name,K,rep,"cohort_dependent",n,count,serial);
    word expected=0;for(auto k:in.keys)expected+=k;
    begin=clock_type::now();auto sum=independent(map,in.keys,rounds);auto read_ns=elapsed(begin);
    require(sum==expected*rounds,"independent result");row(name,K,rep,"independent_hit",n,count,read_ns);
    begin=clock_type::now();sum=independent(map,in.queries,rounds);read_ns=elapsed(begin);
    require(sum==expected*rounds,"mixed result");row(name,K,rep,"independent_half_miss",n,2*count,read_ns);
    auto packet=[&]<unsigned N>(const char* hit,const char* miss) {
        begin=clock_type::now();auto sum=packets<N>(map,in.keys,rounds);auto ns=elapsed(begin);
        require(sum==expected*rounds,"packet hit result");row(name,K,rep,hit,n,count,ns);
        begin=clock_type::now();sum=packets<N>(map,in.queries,rounds);ns=elapsed(begin);
        require(sum==expected*rounds,"packet mixed result");row(name,K,rep,miss,n,2*count,ns);
    };
    packet.template operator()<4>("packet4_hit","packet4_half_miss");
    packet.template operator()<8>("packet8_hit","packet8_half_miss");
    begin=clock_type::now();
    for(unsigned r=0;r<rounds;++r)for(auto k:in.keys)require(map.set(k,mix(k))==0,"update existing");
    row(name,K,rep,"update",n,count,elapsed(begin));
    // Restore cycle outside timing; aged reads use exactly the original graph.
    for(unsigned i=0;i<n;++i)map.set(in.keys[i],in.keys[(i+1)%n]);
    begin=clock_type::now();
    for(unsigned r=0;r<rounds;++r) {
        for(unsigned i=0;i<n/4;++i) {
            require(map.erase(in.keys[i]),"turnover erase");
            require(map.set(in.absent[i],in.keys[(i+1)%n])==1,"turnover insert");
        }
        for(unsigned i=0;i<n/4;++i) {
            require(map.erase(in.absent[i]),"turnover erase back");
            require(map.set(in.keys[i],in.keys[(i+1)%n])==1,"turnover insert back");
        }
    }
    row(name,K,rep,"turnover",n,word(n/4)*4*rounds,elapsed(begin));
    begin=clock_type::now();answer=dependent(map,in.keys[0],count);serial=elapsed(begin);
    require(answer==in.keys[0],"aged cycle");row(name,K,rep,"aged_dependent",n,count,serial);
    auto aged=map.footprint();
    if constexpr(requires{map.filter_stats(in.absent);}) {
        auto f=map.filter_stats(in.absent);
        std::printf("aged_filter,%s,%u,%u,%u,%llu,%llu,%llu\n",name,K,rep,n,(unsigned long long)f[0],(unsigned long long)f[1],(unsigned long long)f[2]);
    }
    std::printf("aged_memory,%s,%u,%u,%u,%llu,%llu,%llu\n",name,K,rep,n,(unsigned long long)aged.requested,(unsigned long long)aged.backing,(unsigned long long)aged.metadata);
    if constexpr(requires{map.distribution();}) {
        auto d=map.distribution();
        std::printf("aged_route,%s,%u,%u,%u,%llu,%llu,%llu\n",name,K,rep,n,(unsigned long long)d.primary,(unsigned long long)d.secondary,(unsigned long long)d.chain);
    }
    map.validate();
    for(unsigned i=0;i<n;++i) {word v=0;require(map.get_value(in.keys[i],v)&&v==in.keys[(i+1)%n],"final value");}
    begin=clock_type::now();storage.reset();row(name,K,rep,"owner_release",n,n,elapsed(begin));
}
template<class Map> void geometry(const char* name) {
    std::printf("geometry,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",name,Map::key_bits,
        Map::primary_bits,Map::primary_hash::F,Map::primary_hash::C,Map::primary.slots,Map::primary.unit,
        Map::secondary_bits,Map::secondary_hash::F,Map::secondary_hash::C,Map::secondary.slots,Map::secondary.unit);
}
template<unsigned K,unsigned A> void campaign(oa::Pool& pool,unsigned n,unsigned rounds,unsigned repeats) {
    using compact=table<K,A,A-1,64,128>;
    using fuller=table<K,A,A-4,128,128>;
    using direct=table<K,A,A-4,128,128,true>;
    using compact_filter=table<K,A,A-1,64,128,false,true>;
    using fuller_filter=table<K,A,A-4,128,128,false,true>;
    auto prepare=[]<class M>(const char* name) {
        auto begin=clock_type::now();M::prepare();auto ns=elapsed(begin);
        std::printf("preparation,%s,%u,%u,%llu,%.6f\n",name,K,A,(unsigned long long)M::preparation_bytes(),ns);
    };
    prepare.template operator()<compact>("compact");prepare.template operator()<fuller>("fuller");
    prepare.template operator()<direct>("fuller_direct");
    geometry<compact>("compact");geometry<fuller>("fuller");geometry<direct>("fuller_direct");
    if constexpr(K==40) {
        prepare.template operator()<compact_filter>("compact_filter");
        prepare.template operator()<fuller_filter>("fuller_filter");
        geometry<compact_filter>("compact_filter");geometry<fuller_filter>("fuller_filter");
    }
    auto in=make_input<K>(n,638251);
    for(unsigned rep=0;rep<repeats;++rep) {
        // Rotate all arms; no construction is shared with another arm.
        constexpr unsigned arms=K==40?6:4;
        for(unsigned j=0;j<arms;++j)switch((j+rep)%arms) {
            case 0:trial<abseil<K>,K>(pool,"abseil",in,rounds,rep);break;
            case 1:trial<compact,K>(pool,"compact",in,rounds,rep);break;
            case 2:trial<fuller,K>(pool,"fuller",in,rounds,rep);break;
            case 3:trial<direct,K>(pool,"fuller_direct",in,rounds,rep);break;
            case 4:if constexpr(K==40) trial<compact_filter,K>(pool,"compact_filter",in,rounds,rep);break;
            case 5:if constexpr(K==40) trial<fuller_filter,K>(pool,"fuller_filter",in,rounds,rep);break;
        }
    }
}
void allocation_history() {
    using small=table<16,5,1,128,128>;
    using large=table<16,9,5,128,128>;
    small::prepare();large::prepare();
    std::puts("rep,phase,requested,owned_capacity,owner_metadata,pool_mapped,pool_idle,pool_metadata,construction_ns");
    for(unsigned rep=0;rep<12;++rep) {
        pool_owner pool;
        auto observe=[&]<class Map>(const char* phase) {
            auto start=clock_type::now();
            std::optional<Map> map;map.emplace(*pool.pool,123,456);
            auto ns=elapsed(start);auto m=map->footprint();auto s=oa::statistics(*pool.pool);
            std::printf("%u,%s,%llu,%llu,%llu,%llu,%llu,%llu,%.3f\n",rep,phase,
                (unsigned long long)m.requested,(unsigned long long)m.backing,(unsigned long long)m.metadata,
                (unsigned long long)s.mapped_bytes,(unsigned long long)s.idle_bytes,
                (unsigned long long)s.metadata_bytes,ns);
            map.reset();orbital::worker::poll(pool.lane,1024);
            s=oa::statistics(*pool.pool);
            std::printf("%u,%s_released,0,0,0,%llu,%llu,%llu,0\n",rep,phase,
                (unsigned long long)s.mapped_bytes,(unsigned long long)s.idle_bytes,(unsigned long long)s.metadata_bytes);
        };
        observe.template operator()<small>("fresh_small");
        observe.template operator()<small>("same_size_small");
        observe.template operator()<large>("large");
        observe.template operator()<small>("after_large_small");
    }
}
template<unsigned A> void residence_campaign(oa::Pool& pool,unsigned n,unsigned operations,unsigned repeats) {
    using compact=table<40,A,A-1,64,128>;
    using fuller=table<40,A,A-4,128,128>;
    compact::prepare();fuller::prepare();
    geometry<compact>("compact");geometry<fuller>("fuller");
    auto in=make_input<40>(n,638251);
    unsigned rounds=std::max(1u,(operations+n-1)/n);
    for(unsigned rep=0;rep<repeats;++rep) for(unsigned j=0;j<3;++j)switch((j+rep)%3) {
        case 0:trial<abseil<40>,40>(pool,"abseil",in,rounds,rep);break;
        case 1:trial<compact,40>(pool,"compact",in,rounds,rep);break;
        case 2:trial<fuller,40>(pool,"fuller",in,rounds,rep);break;
    }
}
} // namespace ht
int main(int argc,char** argv) {
    using namespace ht;
    if(argc<2 || std::string_view(argv[1])=="check") {check();return 0;}
    if(std::string_view(argv[1])=="allocation") {allocation_history();return 0;}
    unsigned rounds=argc>2?std::stoul(argv[2]):50;
    unsigned repeats=argc>3?std::stoul(argv[3]):5;
    require(rounds&&repeats,"nonzero measurement");
    pool_owner pool;
    std::puts("# fixed geometry; baseline reserve is charged; local timings are screening, not native target evidence");
    if(std::string_view(argv[1])=="residence") {
        // Here argv[2] is a minimum operation count, not rounds. The larger
        // primary is64MiB: exceeds V2/Zen5 reported LLC, remains within Granite's.
        residence_campaign<14>(*pool.pool,110000,rounds,repeats);
        residence_campaign<19>(*pool.pool,3600000,rounds,repeats);
        return 0;
    }
    campaign<16,5>(*pool.pool,270,rounds,repeats);
    campaign<16,9>(*pool.pool,5000,rounds,repeats);
    campaign<40,8>(*pool.pool,1800,rounds,repeats);
    if(std::string_view(argv[1])=="panel") {
        // Neighboring occupancies include both sides of Abseil capacity cliffs;
        // the table geometry stays fixed, so extra overflow is charged.
        campaign<16,5>(*pool.pool,180,rounds,repeats);
        campaign<16,5>(*pool.pool,330,rounds,repeats);
        campaign<16,9>(*pool.pool,3500,rounds,repeats);
        campaign<16,9>(*pool.pool,6000,rounds,repeats);
        campaign<40,8>(*pool.pool,1600,rounds,repeats);
        campaign<40,8>(*pool.pool,2100,rounds,repeats);
    }
}

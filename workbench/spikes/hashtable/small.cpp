#include "small.h"
#include "small_baseline.h"
#include "benchmark_oracle.h"
#include <map>
#include <random>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#ifndef HT_SMALL_KEY
#define HT_SMALL_KEY 0
#endif

namespace ht {
template<unsigned K> word list_key(unsigned i) {return (word(i)*0x9e3779b97f4a7c15ULL)&mask(K);}
template<class Table> auto materialize(const Table& table) {
    std::map<word,std::vector<word>> out;
    table.each([&](word k,word v){out[k].push_back(v);});
    for(auto& [k,values]:out)std::sort(values.begin(),values.end());return out;
}
template<unsigned K,container_kind Kind> void check_small(oa::Pool& pool) {
    small_table<K,Kind> table(pool,8719);small_abseil<K,Kind> baseline(pool,0);
    std::array<small_event,64> events;
    for(unsigned i=0;i<256;++i) {
        auto k=list_key<K>(i);const word v=i%2?~word(0):0;
        small_journal journal{events};auto r=table.set_value(k,v,journal);
        require(bool(r)&&r->keys==1,"all-width compact insert");
        const auto expected=baseline.set_value(k,v);require(r->values==expected.values,"value delta");
        require(journal.used&&events[journal.used-1].type==small_event::kind::caller_delta,"delta publication");
        if(std::has_single_bit(i+1))require(materialize(table)==materialize(baseline),"compact growth oracle");
    }
    require(table.size()==256&&table.capacity()==256,"rank255 count256");
    for(unsigned i=0;i<256;++i) {
        auto key=list_key<K>(i);table.set_value(key,i);baseline.set_value(key,i);
        if constexpr(Kind==container_kind::multimap)for(unsigned j=0;j<7;++j) {
            table.set_value(key,j?~word(0):0);baseline.set_value(key,j?~word(0):0);
        }
    }
    require(materialize(table)==materialize(baseline),"updates and duplicate bags");
    const auto before=materialize(table);small_journal refused{{}};
    auto failed=table.set_value(0,771,refused);
    require(!failed&&failed.error()==mutation_error::effects&&materialize(table)==before,"damage refusal atomicity");
    std::array<word,8> keys{},values{};for(unsigned i=0;i<8;++i){keys[i]=list_key<K>(i/2);values[i]=~word(0)-i;}
    require(table.template setN<8>(keys,values,0b11011011)==0b11011011,"SIMD hash batch writes");
    baseline.template setN<8>(keys,values,0b11011011);
    require(materialize(table)==materialize(baseline),"ordered batch duplicates");
    std::array<std::vector<word>,8> actual,expected;
    const auto active=0b10110101u;
    require(table.template getN<8>(keys,active,[&](auto i,word v){actual[i].push_back(v);})==active,"SIMD batch lookup");
    baseline.template getN<8>(keys,active,[&](auto i,word v){expected[i].push_back(v);});
    for(unsigned i=0;i<8;++i){std::sort(actual[i].begin(),actual[i].end());std::sort(expected[i].begin(),expected[i].end());}
    require(actual==expected,"batch consumers and inactive lanes");
    auto retained=table.clone();require(bool(retained),"private clone");const auto retained_values=materialize(*retained);
    for(unsigned i=0;i<256;i+=2) {
        const auto r=table.erase(list_key<K>(i));const auto e=baseline.erase(list_key<K>(i));
        require(r.keys==e.keys&&r.values==e.values,"erase deltas");
    }
    require(materialize(table)==materialize(baseline),"swap-last erase bag release");
    for(unsigned i=0;i<256;i+=2){table.set_value(list_key<K>(i),831);baseline.set_value(list_key<K>(i),831);}
    require(materialize(table)==materialize(baseline)&&materialize(*retained)==retained_values,"refill with retained private owner");
    table.clear();baseline.clear();require(materialize(table)==materialize(baseline)&&table.size()==0,"clear");
    require(table.footprint().requested==0,"empty list owns no payload");
}
template<unsigned K> void check_widths(oa::Pool& pool) {
    check_small<K,container_kind::set>(pool);check_small<K,container_kind::map>(pool);
    check_small<K,container_kind::multimap>(pool);
    small_table<K,container_kind::map,16> wide(pool,99);no_small_effects effects;
    for(unsigned i=0;i<64;++i) {
        std::array<byte,16> bytes;for(unsigned j=0;j<16;++j)bytes[j]=i*13+j;
        require(bool(wide.set_bytes(list_key<K>(i),bytes,effects)),"wide payload insert");
    }
    for(unsigned i=0;i<64;++i)require(wide.get_bytes(list_key<K>(i),[&](const auto& bytes){
        for(unsigned j=0;j<16;++j)require(bytes[j]==byte(i*13+j),"wide payload oracle");
    }),"wide key lookup");
    auto copy=wide.clone();require(bool(copy),"wide private clone");wide.clear();
    for(unsigned i=0;i<64;++i)require(copy->get_bytes(list_key<K>(i),[&](const auto& bytes){
        for(unsigned j=0;j<16;++j)require(bytes[j]==byte(i*13+j),"wide clone payload oracle");
    }),"wide clone survives source clear");
}
void guard_prefixes() {
    auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* p=static_cast<byte*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    require(p!=MAP_FAILED&&mprotect(p+page,page,PROT_NONE)==0,"list guard pages");
    lanes<4>([&](auto i){constexpr auto n=1u<<i;auto* tail=p+page-n;
        for(unsigned j=0;j<n;++j)tail[j]=byte(j);auto bits=list_matches<n>(tail,byte(n-1),n);
        require(take_match(bits)==n-1&&!bits,"exact tiny prefix window");
    });
    for(unsigned cap:{1u,2u,4u,8u,16u,32u,64u,128u,256u}) {
        list_view<8,container_kind::set,0> view{p+page-cap,cap};
        for(unsigned i=0;i<cap;++i)view.data[i]=byte(i+13);
        for(unsigned count=0;count<=cap;++count)for(unsigned needle=0;needle<256;++needle) {
            int expected=-1;for(unsigned i=0;i<count;++i)if(view.data[i]==needle){expected=i;break;}
            require(view.find(byte(needle),0,count)==expected,"bounded 64-selector window and inactive lanes");
        }
        for(unsigned count=0;count<=cap;++count)for(unsigned first=0;first<256;first+=8) {
            hash_packet<8> hash{};
            for(unsigned i=0;i<8;++i)hash.fingerprint[i]=(first+i)&255;
            for(unsigned active:{0u,1u,128u,0xadu,255u}) {
                const auto ranks=view.findN<8>(hash,count,active);
                for(unsigned i=0;i<8;++i) {
                    const unsigned wanted=(first+i-13)&255;
                    require(ranks[i]==((active&(1u<<i))&&wanted<count?int(wanted):-1),
                        "guarded shared selector scan, zero suffix, inactive tail");
                }
            }
        }
        using Wide=list_view<64,container_kind::set,0>;
        Wide wide{p+page-cap*Wide::entry_bytes,cap};
        for(unsigned i=0;i<cap;++i){wide.data[i]=19;put<7>(wide.suffix_at(i),i);}
        for(unsigned count=0;count<=cap;++count) {
            hash_packet<8> hash{};
            for(unsigned i=0;i<8;++i){hash.fingerprint[i]=i%3?19:20;hash.suffix[i]=(count+i*37)&255;}
            for(unsigned active:{0u,1u,128u,0xadu,255u}) {
                const auto ranks=wide.findN<8>(hash,count,active);
                for(unsigned i=0;i<8;++i)
                    require(ranks[i]==((active&(1u<<i))&&i%3&&hash.suffix[i]<count?int(hash.suffix[i]):-1),
                        "guarded shared scan confirms colliding full suffixes");
            }
        }
    }
    require(munmap(p,page*2)==0,"unmap guards");
}
template<class F> double measured(F&& f) {
    const auto start=clock_type::now();f();
    return std::chrono::duration<double,std::nano>(clock_type::now()-start).count();
}
template<unsigned K,container_kind Kind,class Table> [[gnu::noinline]] void trial(
        oa::Pool& pool,unsigned n,unsigned repeat,const char* arm) {
    constexpr unsigned rounds=256;
    std::vector<unsigned> order(n);for(unsigned i=0;i<n;++i)order[i]=i;
    std::mt19937 random(771);std::shuffle(order.begin(),order.end(),random);
    constexpr unsigned writes=Kind==container_kind::multimap?1:rounds;
    std::map<word,std::vector<word>> expected;
    for(auto i:order)expected[list_key<K>(i)]={Kind==container_kind::set?0:list_key<K>((i+1)%n)};
    auto updated=expected;
    for(auto i:order) {
        auto& values=updated.at(list_key<K>(i));
        if constexpr(Kind==container_kind::map)values={~word(0)-i-word(writes-1)*257-8};
        if constexpr(Kind==container_kind::multimap){values.push_back(~word(0)-i-4);values.push_back(~word(0)-i-8);}
    }
    auto erased=updated;for(unsigned i=0;i<n;i+=2)erased.erase(list_key<K>(i));
    benchmark_oracle<word> initial_oracle(expected),updated_oracle(updated),erased_oracle(erased);
    std::optional<Table> table;word checksum=0;
    auto verify=[&](auto& oracle){oracle.check([&](auto&& emit){table->each(emit);});};
    const auto construct=measured([&]{table.emplace(pool,8719);});
    const auto build=measured([&]{for(auto i:order){auto d=table->set_value(list_key<K>(i),list_key<K>((i+1)%n));checksum+=d.keys;}});
    verify(initial_oracle);
    auto initial=table->footprint();
    auto read=[&]{for(unsigned r=0;r<rounds;++r)for(auto i:order) {
        const bool found=table->get(list_key<K>(i),[&](word v){checksum+=v;});checksum+=found;
    }};read();const auto hits=measured(read);
    auto misses=[&]{for(unsigned r=0;r<rounds;++r)for(auto i:order) {
        const bool found=table->get(list_key<K>(i+n),[&](word v){checksum+=v;});checksum+=found;
    }};
    double miss=0;if(n*2<=mask(K)+word(K<64)){misses();miss=measured(misses);}
    // The next queried key depends on the actual loaded value for map/MM.
    // Sets use a boolean-dependent caller graph and are reported separately.
    word at=0;
    const auto serial=measured([&]{for(unsigned r=0;r<rounds*n;++r) {
        if constexpr(Kind==container_kind::set) {
            auto found=table->get(list_key<K>(at),[&](word){});at=(at+(found?1:7))%n;checksum+=found;
        } else require(table->get(at,[&](word v){at=v;checksum+=v;}),"serial value graph");
    }});
    std::array<word,8> keys{},values{};
    auto batches=[&]<unsigned N>() {
        const auto reads=measured([&]{for(unsigned r=0;r<rounds;++r)for(unsigned first=0;first<n;first+=N) {
            const unsigned count=std::min(N,n-first);for(unsigned i=0;i<count;++i)keys[i]=list_key<K>(order[first+i]);
            checksum+=table->template getN<N>(std::span<const word,N>(keys.data(),N),(1u<<count)-1,
                [&](auto,word v){checksum+=v;});
        }});return reads;
    };
    const auto get4=batches.template operator()<4>(),get8=batches.template operator()<8>();
    // MM appends once per key here, avoiding an unbounded bag-growth workload
    // masquerading as a repeated point-update benchmark.
    auto update=[&]<unsigned N>() {
        return measured([&]{for(unsigned r=0;r<writes;++r)for(unsigned first=0;first<n;first+=N) {
            const unsigned count=std::min(N,n-first);for(unsigned i=0;i<count;++i) {
                keys[i]=list_key<K>(order[first+i]);values[i]=~word(0)-order[first+i]-word(r)*257-N;
            }
            require(table->template setN<N>(std::span<const word,N>(keys.data(),N),std::span<const word,N>(values.data(),N),
                (1u<<count)-1)==(1u<<count)-1,"timed batch writes");
        }});
    };
    const auto set4=update.template operator()<4>();
    const auto after4=table->footprint();
    const auto set8=update.template operator()<8>();
    verify(updated_oracle);
    auto aged=table->footprint();const auto bag_reads=measured(read);
    const auto erase=measured([&]{for(unsigned i=0;i<n;i+=2)checksum+=table->erase(list_key<K>(i)).keys;});
    verify(erased_oracle);
    const auto release=measured([&]{table.reset();});
    asm volatile(""::"r"(checksum):"memory");
    auto emit=[&](const char* phase,unsigned ops,double ns,memory m) {
        std::printf("%s,%u,%u,%u,%u,%s,%u,%.6f,%llu,%llu,%llu,%llu\n",arm,K,unsigned(Kind),n,repeat,phase,ops,ns,
            (unsigned long long)checksum,(unsigned long long)m.requested,(unsigned long long)m.backing,(unsigned long long)m.metadata);
    };
    emit("construct",1,construct,{});emit("build",n,build,initial);emit("hit",n*rounds,hits,initial);
    if(miss)emit("miss",n*rounds,miss,initial);
    emit(Kind==container_kind::set?"serial_caller_graph":"serial_value_graph",n*rounds,serial,initial);
    emit("get4",n*rounds,get4,initial);emit("get8",n*rounds,get8,initial);
    // Different MM lifecycle steps; these cannot select a packet width.
    emit(Kind==container_kind::multimap?"promote4":"set4",n*writes,set4,after4);
    emit(Kind==container_kind::multimap?"append8":"set8",n*writes,set8,aged);
    emit("aged_hit",n*rounds,bag_reads,aged);emit("erase",(n+1)/2,erase,aged);emit("release",1,release,aged);
}
template<unsigned K,container_kind Kind> void campaign(oa::Pool& pool,unsigned reps) {
    for(unsigned n:{1u,4u,16u,64u,256u})for(unsigned r=0;r<reps;++r)for(unsigned step=0;step<3;++step)switch((step+r)%3) {
        case 0:trial<K,Kind,small_table<K,Kind>>(pool,n,r,"compact_list");break;
        case 1:trial<K,Kind,small_abseil<K,Kind>>(pool,n,r,"abseil");break;
        case 2:trial<K,Kind,small_abseil<K,Kind,true>>(pool,n,r,"abseil_exact");break;
    }
}
template<unsigned K> void campaigns(oa::Pool& pool,unsigned reps) {
    campaign<K,container_kind::set>(pool,reps);campaign<K,container_kind::map>(pool,reps);
    campaign<K,container_kind::multimap>(pool,reps);
}
} // namespace ht
int main(int argc,char** argv) {
    using namespace ht;const std::string_view mode=argc>1?argv[1]:"check";
    const unsigned reps=argc>2?std::stoul(argv[2]):3;pool_owner pool;
    if(mode=="check") {
        guard_prefixes();
#if HT_SMALL_KEY
        check_widths<HT_SMALL_KEY>(*pool.pool);
#else
        check_widths<8>(*pool.pool);check_widths<16>(*pool.pool);check_widths<24>(*pool.pool);check_widths<32>(*pool.pool);
        check_widths<40>(*pool.pool);check_widths<48>(*pool.pool);check_widths<56>(*pool.pool);check_widths<64>(*pool.pool);
#endif
        std::printf("compact lists: key selection %u (0=all), sets/maps/multimaps, wide maps, tiny windows, batches, effects and lifetimes passed\n",HT_SMALL_KEY);
    } else {
        require(mode=="bench","small mode");require(reps>0,"repetitions");
        std::puts("arm,K,kind,n,rep,phase,operations,ns,checksum,requested,owned_capacity,metadata");
#if HT_SMALL_KEY
        campaigns<HT_SMALL_KEY>(*pool.pool,reps);
#else
        campaigns<8>(*pool.pool,reps);campaigns<16>(*pool.pool,reps);campaigns<24>(*pool.pool,reps);campaigns<32>(*pool.pool,reps);
        campaigns<40>(*pool.pool,reps);campaigns<48>(*pool.pool,reps);campaigns<56>(*pool.pool,reps);campaigns<64>(*pool.pool,reps);
#endif
    }
}

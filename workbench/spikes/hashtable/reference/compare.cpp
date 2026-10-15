#include "support.h"
#include "baseline.h"
#include <chrono>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace hashtable_spike;
using clock_type=std::chrono::steady_clock;

template<unsigned K=64,unsigned V=64> word cycle_value(word next,word key) {
    return next | (mix(key) & mask(V) & ~mask(K));
}

template<unsigned K=64,unsigned V=64,class Map> void verify(const Map& m,const std::vector<word>& keys) {
    require(m.size()==keys.size());
    for (std::size_t i=0;i<keys.size();++i) { word v=0; require(m.get(keys[i],v)); require(v==cycle_value<K,V>(keys[(i+1)%keys.size()],keys[i])); }
}

template<bool Direct,unsigned Bound=0,bool SplitPrimary=false,unsigned EarlyHint=0> void check_body() {
    for (word x: {word{0},word{1},~word{},word{1}<<63}) require(unmix(mix(x))==x);
    for (unsigned slots: {1u,3u,6u,8u,14u,16u,17u,24u,32u}) for (bool split: {false,true}) {
        if constexpr (Bound) {
            if (Bound>=4?slots!=8:slots<9 || slots>16) continue;
            if (split!=(Bound==2 || Bound==5)) continue;
        }
        pool_owner pool;
        basic_map<Direct,Bound,64,64,false,0,0,false,SplitPrimary,EarlyHint> p(*pool.pool,16,slots,split,1,9137);
        abseil_map a; a.reserve(16);
        std::unordered_map<word,word> expected;
        std::mt19937_64 rng(187);
        for (unsigned step=0;step<16000;++step) {
            word k=mix(rng()%256),v=rng();
            if (step%3) {
                const bool fresh=!expected.contains(k); expected[k]=v;
                require(p.insert_or_assign(k,v)==fresh); require(a.insert_or_assign(k,v)==fresh);
            } else {
                const bool exists=expected.erase(k)!=0; require(p.erase(k)==exists); require(a.erase(k)==exists);
            }
            word pv=0x97,av=pv; const bool exists=expected.contains(k);
            require(p.get(k,pv)==exists); require(a.get(k,av)==exists);
            require(pv==av); if (exists) require(pv==expected[k]); else require(pv==0x97);
            if (step%512==0) {
                p.audit();
                require(p.size()==expected.size());
                for (const auto& [key,value]:expected) { word got=0; require(p.get(key,got)); require(got==value); }
                std::unordered_set<word> seen;
                p.each([&](word key,word value){ require(seen.insert(key).second); require(expected.at(key)==value); });
                require(seen.size()==expected.size());
            }
        }
        p.rebuild(expected.size()*2,9277331);
        for (const auto& [key,value]:expected) { word got=0; require(p.get(key,got)); require(got==value); }
        for (const auto& [key,value]:expected) { (void)value; require(p.erase(key)); }
        require(p.size()==0);
        p.audit();
        auto c=p.counts(); require(c.primary+c.secondary+c.overflow==0);
        p.clear(); require(p.insert_or_assign(0,~word{})); word v=0; require(p.get(0,v) && v==~word{});
    }
}
void check() {
    check_body<false>(); check_body<true>();
    check_body<false,1>(); check_body<false,2>(); check_body<true,1>(); check_body<true,2>();
    check_body<false,4>();check_body<true,4>();
    check_body<false,1,true>();check_body<true,1,true>();
    check_body<false,4,true>();check_body<true,4,true>();
    check_body<false,5>();check_body<true,5>();
    check_body<false,1,false,2>();check_body<false,4,false,1>();
    check_body<false,5,false,1>();check_body<false,5,false,2>();
    std::puts("checks: both codecs; oracle churn, replacement, misses, swap erase, iteration, all-domain values and fresh-seed rebuild passed");
}

struct sample { double ns; word checksum; };
template<class F> sample timed(std::size_t operations,F&& f) {
    const auto start=clock_type::now(); word sum=f();
    asm volatile("" : "+r"(sum) :: "memory");
    const auto end=clock_type::now();
    return {std::chrono::duration<double,std::nano>(end-start).count()/double(operations),sum};
}
template<unsigned K=64,unsigned V=64,class Map> sample fill(Map& m,const std::vector<word>& keys) {
    return timed(keys.size(),[&] { word sum=0; for (std::size_t i=0;i<keys.size();++i) sum+=m.insert_or_assign(keys[i],cycle_value<K,V>(keys[(i+1)&(keys.size()-1)],keys[i])); return sum; });
}
template<unsigned K=64,class Map> sample serial(const Map& m,word first,std::size_t operations) {
    return timed(operations,[&] { word key=first,sum=0; for (std::size_t i=0;i<operations;++i) { word value=0; bool found=m.get(key,value); sum+=value+word(found); key=value & mask(K); asm volatile("" : "+r"(key) : "r"(found)); } return sum^key; });
}
template<class Map> sample independent(const Map& m,const std::vector<word>& keys,std::size_t operations) {
    return timed(operations,[&] { word sum=0; for (std::size_t i=0;i<operations;++i) { word value=0; bool found=m.get(keys[i&(keys.size()-1)],value); sum+=value+word(found); } return sum; });
}
template<unsigned V=64,class Map> sample update(Map& m,const std::vector<word>& keys) {
    return timed(keys.size(),[&] { word sum=0; for (auto key:keys) sum+=m.insert_or_assign(key,mix(key)&mask(V)); return sum; });
}
template<unsigned K=64,unsigned V=64,class Map> sample churn(Map& m,const std::vector<word>& before,const std::vector<word>& after) {
    return timed(before.size(),[&] { word sum=0; for (std::size_t i=0;i<before.size();++i) { sum+=m.erase(before[i]); sum+=m.insert_or_assign(after[i],cycle_value<K,V>(after[(i+1)&(after.size()-1)],after[i])); } return sum; });
}

template<bool Direct,unsigned Bound=0,unsigned K=64,unsigned V=64,bool Affine=false,unsigned BoundSlots=0,unsigned BoundStride=0,bool QuotientTable=false,bool SplitPrimary=false,unsigned EarlyHint=0> void compare(std::size_t n,unsigned repetitions,unsigned slots,bool planes,unsigned rounding,std::size_t reserve_n=0) {
    if (!reserve_n) reserve_n=n;
    using Key=std::conditional_t<(K<=8),std::uint8_t,std::conditional_t<(K<=16),std::uint16_t,std::conditional_t<(K<=32),std::uint32_t,word>>>;
    using Value=std::conditional_t<(V<=8),std::uint8_t,std::conditional_t<(V<=16),std::uint16_t,std::conditional_t<(V<=32),std::uint32_t,word>>>;
    using Candidate=basic_map<Direct,Bound,K,V,Affine,BoundSlots,BoundStride,QuotientTable,SplitPrimary,EarlyHint>;
    using Baseline=basic_abseil_map<Key,Value>;
    require(K==64 || n*2<mask(K));
    permutation<K,false> key_source(9381);
    std::vector<word> keys(n),absent(n);
    for (std::size_t i=0;i<n;++i) { keys[i]=key_source(i+1); absent[i]=key_source(i+n+1); }
    std::mt19937_64 rng(9381); std::shuffle(keys.begin(),keys.end(),rng); std::shuffle(absent.begin(),absent.end(),rng);
    // A single permutation cycle: the returned value itself is the next key.
    const std::size_t operations=std::max<std::size_t>(n,262144);
    for (unsigned r=0;r<repetitions;++r) {
        pool_owner pool;
        std::unique_ptr<Candidate> pp;
        std::unique_ptr<Baseline> ap;
        sample pf{},af{},pb{},ab{};
        auto build_p=[&] { pp=std::make_unique<Candidate>(*pool.pool,reserve_n,slots,planes,rounding,mix(r+773)); pf=fill<K,V>(*pp,keys); return pf.checksum; };
        auto build_a=[&] { ap=std::make_unique<Baseline>(); ap->reserve(n); af=fill<K,V>(*ap,keys); return af.checksum; };
        if (r%2) { ab=timed(n,build_a); pb=timed(n,build_p); } else { pb=timed(n,build_p); ab=timed(n,build_a); }
        auto& p=*pp; auto& a=*ap;
        memory_counts mem{}; table_counts count{};
        const char* label=EarlyHint==2?"packed_bound_hint_all":EarlyHint==1?"packed_bound_hint_first":BoundStride?(Direct?"direct_64B":"packed_64B"):Bound?(Direct?"direct_bound":"packed_bound"):(Direct?"direct":"packed");
        auto output=[&](const char* name,const char* operation,sample result) {
            const bool candidate=std::string_view(name)!="abseil";
            std::size_t requested=candidate?mem.payload:a.allocation_stats().current_bytes;
            std::size_t capacity=candidate?mem.capacity:0; // baseline allocator rounding is not measured
            std::size_t controls=candidate?mem.catalog+mem.plans:Baseline::ordinary_object_bytes;
            std::printf("%u,%u,%s,%zu,%u,%s,%u,%s,%s,%u,%.6f,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%u,%u,%u,%zu\n",
                K,V,QuotientTable?"quotient_table":Affine?"seeded_affine_control":"width_mixer",n,slots,SplitPrimary?"metadata_rows":Bound==5?"key_value_lines":planes?"planes":"rows",rounding,name,operation,r,result.ns,
                static_cast<unsigned long long>(result.checksum),requested,capacity,controls,
                candidate?std::size_t(mem.pool.mapped_bytes):0,candidate?std::size_t(mem.pool.metadata_bytes):0,
                candidate?count.primary:0,candidate?count.secondary:0,candidate?count.overflow:0,
                p.primary_bits(),p.primary_stride(),p.secondary_stride(),a.capacity());
            std::fflush(stdout);
        };
        require(pf.checksum==n && af.checksum==n); verify<K,V>(p,keys); verify<K,V>(a,keys); p.audit();
        for (auto key:absent) { word v=123; require(!p.get(key,v)&&v==123); require(!a.get(key,v)&&v==123); }
        mem=p.memory(); count=p.counts();
        output(label,"reserved_fill",pf); output("abseil","reserved_fill",af);
        output(label,"build",pb); output("abseil","build",ab);
        auto paired=[&](const char* name,auto&& operation,bool warm_read=false) {
            sample ps{},as{};
            auto run=[&](auto& m) { if (warm_read) { auto w=operation(m); asm volatile(""::"r"(w.checksum)); } return operation(m); };
            if (r%2) { as=run(a); ps=run(p); } else { ps=run(p); as=run(a); }
            if (std::string_view(name)=="erase_insert_pair" || std::string_view(name)=="update") {
                mem=p.memory(); count=p.counts();
            }
            require(ps.checksum==as.checksum); output(label,name,ps); output("abseil",name,as);
        };
        // Each read arm gets its own identical full-workload warmup immediately before timing.
        paired("serial_hit",[&](auto& m){ return serial<K>(m,keys[0],operations); },true);
        paired("independent_hit",[&](auto& m){ return independent(m,keys,operations); },true);
        paired("independent_miss",[&](auto& m){ return independent(m,absent,operations); },true);
        paired("update",[&](auto& m){ return update<V>(m,keys); });
        for (auto key:keys) { word pv=0,av=0; require(p.get(key,pv) && a.get(key,av)); require(pv==(mix(key)&mask(V)) && av==pv); }
        paired("erase_insert_pair",[&](auto& m){ return churn<K,V>(m,keys,absent); });
        verify<K,V>(p,absent); verify<K,V>(a,absent); p.audit();
        // Validate the complete post-churn state, including old-key absence.
        for (auto key:keys) { word v=0; require(!p.get(key,v) && !a.get(key,v)); }
        mem=p.memory(); count=p.counts();
        paired("post_churn_hit",[&](auto& m){ return independent(m,absent,operations); },true);
    }
}

template<unsigned V,bool Affine> void exact_compare(unsigned repetitions) {
    constexpr unsigned slots=V==16?16:V==32?12:6;
    compare<false,3,16,V,Affine,slots>(4096,repetitions,slots,false,1,V==16?4096:8192);
    compare<true,3,16,V,Affine,slots>(4096,repetitions,slots,false,1,V==16?4096:8192);
    constexpr unsigned line_slots=V==16?16:V==32?10:6;
    compare<false,3,16,V,Affine,line_slots,64>(4096,repetitions,line_slots,false,64,V==16?4096:8192);
    compare<true,3,16,V,Affine,line_slots,64>(4096,repetitions,line_slots,false,64,V==16?4096:8192);

}

template<unsigned Slots,bool Affine> void exact_layout_compare(unsigned repetitions) {
    // Same logical cardinality and ordinary target occupancy at every capacity.
    // Roundings include compact, 96B and cache-line multiples; report the
    // actual primary/secondary strides and all fallback populations.
    for (unsigned rounding:{1u,32u,64u,96u}) {
        compare<false,3,16,64,Affine,Slots>(4096,repetitions,Slots,false,rounding,4096);
        compare<true,3,16,64,Affine,Slots>(4096,repetitions,Slots,false,rounding,4096);
    }
}

template<unsigned Slots> void quotient_compare(unsigned repetitions) {
    for (unsigned rounding:{1u,32u,64u,96u}) {
        compare<false,3,16,64,false,Slots,0,true>(4096,repetitions,Slots,false,rounding,4096);
        compare<true,3,16,64,false,Slots,0,true>(4096,repetitions,Slots,false,rounding,4096);
    }
}

int main(int argc,char** argv) {
    if (argc==2 && std::string_view(argv[1])=="check") { check(); return 0; }
    if (argc==4 && std::string_view(argv[1])=="capacities64") {
        const auto n=std::stoull(argv[2]);const unsigned reps=std::stoul(argv[3]);
        require(n>=2 && std::has_single_bit(n) && reps);
        std::puts("key_bits,value_bits,permutation,n,slots,layout,rounding,table,operation,rep,ns_per_operation,checksum,requested_payload_bytes,backing_capacity_bytes,control_bytes,pool_mapped_bytes,pool_metadata_bytes,primary_keys,secondary_keys,overflow_keys,primary_address_bits,primary_stride,secondary_stride,abseil_capacity");
        for (unsigned slots=9;slots<=16;++slots) {
            compare<false,1>(n,reps,slots,false,1);
            compare<false,1,64,64,false,0,0,false,false,1>(n,reps,slots,false,1);
        }
        return 0;
    }
    if (argc==4 && std::string_view(argv[1])=="prefetch64") {
        const auto n=std::stoull(argv[2]);const unsigned reps=std::stoul(argv[3]);
        require(n>=2 && std::has_single_bit(n) && reps);
        std::puts("key_bits,value_bits,permutation,n,slots,layout,rounding,table,operation,rep,ns_per_operation,checksum,requested_payload_bytes,backing_capacity_bytes,control_bytes,pool_mapped_bytes,pool_metadata_bytes,primary_keys,secondary_keys,overflow_keys,primary_address_bits,primary_stride,secondary_stride,abseil_capacity");
        compare<false,1>(n,reps,16,false,1);
        compare<false,1,64,64,false,0,0,false,false,1>(n,reps,16,false,1);
        compare<false,1,64,64,false,0,0,false,false,2>(n,reps,16,false,1);
        for (unsigned rounding:{1u,32u}) {
            compare<false,4>(n,reps,8,false,rounding);
            compare<false,4,64,64,false,0,0,false,false,1>(n,reps,8,false,rounding);
            compare<false,4,64,64,false,0,0,false,false,2>(n,reps,8,false,rounding);
        }
        compare<false,5>(n,reps,8,true,1);
        compare<false,5,64,64,false,0,0,false,false,1>(n,reps,8,true,1);
        compare<false,5,64,64,false,0,0,false,false,2>(n,reps,8,true,1);
        return 0;
    }
    if (argc==4 && std::string_view(argv[1])=="metadata64") {
        const auto n=std::stoull(argv[2]);const unsigned reps=std::stoul(argv[3]);
        require(n>=2 && std::has_single_bit(n) && reps);
        std::puts("key_bits,value_bits,permutation,n,slots,layout,rounding,table,operation,rep,ns_per_operation,checksum,requested_payload_bytes,backing_capacity_bytes,control_bytes,pool_mapped_bytes,pool_metadata_bytes,primary_keys,secondary_keys,overflow_keys,primary_address_bits,primary_stride,secondary_stride,abseil_capacity");
        for (unsigned rounding:{1u,32u}) {
            compare<false,1>(n,reps,16,false,rounding);
            compare<false,1,64,64,false,0,0,false,true>(n,reps,16,false,rounding);
            compare<false,4>(n,reps,8,false,rounding);
            compare<false,4,64,64,false,0,0,false,true>(n,reps,8,false,rounding);
        }
        compare<true,1,64,64,false,0,0,false,true>(n,reps,16,false,1);
        compare<true,4,64,64,false,0,0,false,true>(n,reps,8,false,1);
        return 0;
    }
    const bool quotient=argc==3 && std::string_view(argv[1])=="quotient16";
    const bool layouts=quotient || (argc==3 && std::string_view(argv[1])=="layouts16");
    const bool exact=layouts || (argc==3 && std::string_view(argv[1])=="keys16");
    if (!exact && argc!=4) { std::fprintf(stderr,"usage: compare check | compare CARDINALITY REPETITIONS SLOTS\n"); return 2; }
    if (exact) {
        std::puts("key_bits,value_bits,permutation,n,slots,layout,rounding,table,operation,rep,ns_per_operation,checksum,requested_payload_bytes,backing_capacity_bytes,control_bytes,pool_mapped_bytes,pool_metadata_bytes,primary_keys,secondary_keys,overflow_keys,primary_address_bits,primary_stride,secondary_stride,abseil_capacity");
        const unsigned r=std::stoul(argv[2]); require(r>0);
        if (quotient) {
            quotient_compare<5>(r);quotient_compare<6>(r);quotient_compare<8>(r);
            quotient_compare<9>(r);quotient_compare<10>(r);quotient_compare<12>(r);
            quotient_compare<13>(r);quotient_compare<16>(r);
            return 0;
        }
        if (layouts) {
            exact_layout_compare<5,false>(r);exact_layout_compare<6,false>(r);exact_layout_compare<8,false>(r);
            exact_layout_compare<12,false>(r);exact_layout_compare<13,false>(r);exact_layout_compare<16,false>(r);
            exact_layout_compare<5,true>(r);exact_layout_compare<6,true>(r);exact_layout_compare<8,true>(r);
            exact_layout_compare<12,true>(r);exact_layout_compare<13,true>(r);exact_layout_compare<16,true>(r);
            return 0;
        }
        exact_compare<16,false>(r); exact_compare<32,false>(r); exact_compare<64,false>(r);
        exact_compare<16,true>(r); exact_compare<32,true>(r); exact_compare<64,true>(r);
        return 0;
    }
    const auto n=std::stoull(argv[1]); unsigned reps=std::stoul(argv[2]),slots=std::stoul(argv[3]);
    require(n>=2 && std::has_single_bit(n) && reps && slots>=1 && slots<=32);
    std::fprintf(stderr,"Abseil %s; word map; cardinality/footprint labels, no cache-residence assertion\n",abseil_revision);
    std::puts("key_bits,value_bits,permutation,n,slots,layout,rounding,table,operation,rep,ns_per_operation,checksum,requested_payload_bytes,backing_capacity_bytes,control_bytes,pool_mapped_bytes,pool_metadata_bytes,primary_keys,secondary_keys,overflow_keys,primary_address_bits,primary_stride,secondary_stride,abseil_capacity");
    for (bool planes:{false,true}) { compare<false>(n,reps,slots,planes,1); compare<true>(n,reps,slots,planes,1); }
    if (slots==16) {
        compare<false,1>(n,reps,slots,false,1); compare<true,1>(n,reps,slots,false,1);
        compare<false,2>(n,reps,slots,true,1); compare<true,2>(n,reps,slots,true,1);
    }
    if (slots==8) {
        compare<false,4>(n,reps,slots,false,1); compare<true,4>(n,reps,slots,false,1);
    }
}

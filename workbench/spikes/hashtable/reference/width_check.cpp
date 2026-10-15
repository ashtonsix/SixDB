#include "support.h"
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <sys/mman.h>
#include <unistd.h>
using namespace hashtable_spike;

template<unsigned K,bool Affine> void permutation_check() {
    std::mt19937_64 rng(319);
    for (unsigned seed=0;seed<8;++seed) {
        permutation<K,Affine> p(rng());
        std::unordered_set<word> seen;
        constexpr unsigned n=unsigned(word{1}<<std::min(K,16u));
        for (unsigned i=0;i<n;++i) {
            const word key=K<=16?i:rng()&mask(K),h=p(key);
            require(h<=mask(K) && p.invert(h)==key);
            if constexpr (K<=16) require(seen.insert(h).second);
        }
    }
}

template<unsigned K,bool Direct,bool Affine> void map_check() {
    pool_owner pool;
    basic_map<Direct,0,K,64,Affine> p(*pool.pool,16,6,false,1,31873);
    std::unordered_map<word,word> expected;
    std::mt19937_64 rng(91327);
    permutation<K,false> keys(19191);
    for (unsigned i=0;i<12000;++i) {
        const word k=keys(rng()%std::min<word>(512,mask(K)+word(K!=64))),v=rng();
        if (i%3) { const bool fresh=!expected.contains(k); expected[k]=v; require(p.insert_or_assign(k,v)==fresh); }
        else require(p.erase(k)==(expected.erase(k)!=0));
        if (i%511==0) {
            p.audit(); require(p.size()==expected.size());
            std::unordered_set<word> seen;
            p.each([&](word key,word value) { require(seen.insert(key).second); require(expected.at(key)==value); });
            require(seen.size()==expected.size());
            for (const auto& [key,value]:expected) { word out=0; require(p.get(key,out) && out==value); }
        }
    }
    p.rebuild(expected.size(),87191);
    for (const auto& [key,value]:expected) { word v=0; require(p.get(key,v)&&v==value); require(p.erase(key)); }
    require(p.size()==0); p.audit(); p.clear();
    require(p.insert_or_assign(mask(K),~word{})); word v=0;
    require(p.get(mask(K),v)&&v==~word{});
}

template<unsigned V,bool Direct,bool Affine,bool QuotientTable=false> void exact_check() {
    constexpr unsigned slots=V==16?16:V==32?12:6;
    pool_owner pool;
    basic_map<Direct,3,16,V,Affine,slots,0,QuotientTable> p(*pool.pool,V==16?4096:8192,slots,false,1,61331);
    std::vector<std::uint16_t> keys(65536);
    std::iota(keys.begin(),keys.end(),std::uint16_t{});
    std::mt19937 rng(711); std::shuffle(keys.begin(),keys.end(),rng);
    for (const auto k:keys) require(p.insert_or_assign(k,mix(k)&mask(V)));
    p.audit();
    for (const auto k:keys) { word v=0; require(p.get(k,v)&&v==(mix(k)&mask(V))); }
    for (const auto k:keys) { require(!p.insert_or_assign(k,mask(V))); word v=0; require(p.get(k,v)&&v==mask(V)); }
    p.each([&](word k,word v) { require(k<=65535 && v==mask(V)); });
    for (const auto k:keys) { require(p.erase(k)); word v=937; require(!p.get(k,v) && v==937); }
    p.audit(); require(p.size()==0);
}
void quotient_check() {
    std::mt19937_64 rng(938179);
    for (unsigned trial=0;trial<4;++trial) for (unsigned a=1;a<16;++a) {
        quotient16 p(rng(),a);
        for (word key=0;key<65536;++key) {
            const word h=p(key);require(h<=65535 && p.invert(h)==key && p(p.invert(key))==key);
            if (p.remainder_bits<=8) require(p.exact(key)==h);
        }
    }
    exact_check<16,false,false,true>();exact_check<32,false,false,true>();exact_check<64,false,false,true>();
}

template<bool Direct> void guard_check() {
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* allocation=static_cast<byte*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    require(allocation!=MAP_FAILED); require(mprotect(allocation+page,page,PROT_NONE)==0);
    // Every suffix width of the full-word geometry, both exact field planes and
    // full rows, terminating immediately before an inaccessible guard page.
    for (unsigned a=1;a<=55;++a) for (bool planes:{false,true}) for (unsigned n:{1u,6u,8u,12u,16u,17u,32u}) {
        geometry<> g(n,a,planes,1,false); require(g.stride<page);
        if ((n==16 || n==8) && !planes) g.admit_row_suffix_window();
        if (n==8 && planes) g.admit_plane_suffix_window();
        byte* b=allocation+page-g.stride;
        std::memset(b,0,g.stride); std::memset(b+g.header,255,g.lanes);
        for (unsigned i=0;i<n;++i) {
            const word h=(word(i?i:255)<<g.suffix_bits) | ((i+1)&mask(g.suffix_bits));
            g.template append<Direct>(b,h,~word{}-i);
            require(g.template find<Direct>(b,h)==int(i));
            require(g.value.template get<Direct>(g.value_at(b,i))==~word{}-i);
            if (n==16) {
                const int found=planes?g.template find16<Direct,true>(b,h):g.template find16<Direct,false,true>(b,h);
                require(found==int(i));
            }
            if (n==8 && !planes) require(g.template find8<Direct>(b,h)==int(i));
            if (n==8 && planes) require(g.template find8<Direct,true>(b,h)==int(i));
        }
        while (b[0]) g.template erase<Direct>(b,0);
    }
    require(munmap(allocation,page*2)==0);
}

template<unsigned N,unsigned V,bool Direct> void exact_guard_check() {
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* allocation=static_cast<byte*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    require(allocation!=MAP_FAILED); require(mprotect(allocation+page,page,PROT_NONE)==0);
    geometry<16,V> g(N,9,false,1,false);
    g.value.template admit_full_value<V/8>();
    byte* b=allocation+page-g.stride;
    std::memset(b,0,g.stride); std::memset(b+g.header,255,g.lanes);
    for (unsigned occupied=0;occupied<=N;++occupied) {
        for (unsigned key=0;key<128;++key) {
            const int found=g.template find_exact<N>(b,key);
            require(found==(key<occupied?int(key):-1));
            if (found>=0) require(g.value.template get_full_value<Direct,V/8>(g.value_at(b,unsigned(found)))==(mask(V)-key));
        }
        if (occupied<N) g.template append<Direct>(b,occupied,mask(V)-occupied);
    }
    require(munmap(allocation,page*2)==0);
}
template<unsigned N> void exact_guard_shapes() {
    exact_guard_check<N,16,false>(); exact_guard_check<N,16,true>();
    exact_guard_check<N,32,false>(); exact_guard_check<N,32,true>();
    exact_guard_check<N,64,false>(); exact_guard_check<N,64,true>();
}

template<unsigned K> void width_check() {
    permutation_check<K,false>(); permutation_check<K,true>();
    map_check<K,false,false>(); map_check<K,true,true>();
}
template<bool Direct,unsigned Bound=1,bool SplitPrimary=false> void staged_check() {
    pool_owner pool;
    constexpr word seed=78193;
    basic_map<Direct,Bound,64,64,false,0,0,false,SplitPrimary> map(*pool.pool,16,Bound==4?8:16,false,1,seed);
    permutation<64,false> hash(seed);
    std::unordered_map<word,word> expected;
    std::vector<word> keys;
    // Both buckets, including fingerprint255 which is also the empty sentinel.
    // Its unused lanes must never produce candidate windows or returned values.
    const unsigned a=map.primary_bits();
    for (unsigned i=0;i<512;++i) keys.push_back(hash.invert((word(i&mask(a))<<(64-a))|(word(i%3?i%256:255)<<(56-a))|(i+1)));
    auto check=[&] {
        map.audit();
        for (const auto key:keys) for (const auto query:{key,key^word{0x1579b9}}) {
            const auto probe=map.prepare_primary(query);
            auto window=[&](const byte* p,std::size_t bytes) {
                const auto storage=map.primary_storage();
                require(bytes>0 && p>=storage.data() && p+bytes<=storage.data()+storage.size());
                if constexpr (!SplitPrimary) require(p>=probe.bucket && p+bytes<=probe.bucket+map.primary_stride());
            };
            std::size_t whole_bytes=0;
            map.primary_bucket_window(probe,[&](const byte* p,std::size_t bytes) {window(p,bytes);whole_bytes+=bytes;});
            require(whole_bytes==map.primary_stride());
            map.primary_window(probe,window);
            const auto matches=map.match_primary(probe);
            std::unordered_set<const byte*> addresses;
            map.candidate_windows(probe,matches,[&](const byte* p,std::size_t n){window(p,n);require(addresses.insert(p).second);});
            word value=781,ordinary=781;
            const bool found=map.finish_primary(probe,matches,value);
            require(found==map.get(query,ordinary) && value==ordinary);
            auto wanted=expected.find(query);
            require(found==(wanted!=expected.end()));
            require(value==(found?wanted->second:781));
        }
    };
    check();
    for (unsigned i=0;i<keys.size();++i) {
        require(map.insert_or_assign(keys[i],mix(i)));expected[keys[i]]=mix(i);
        if (i<33 || i%127==0) check();
    }
    const auto counts=map.counts();require(counts.primary && counts.secondary && counts.overflow);
    check();
    for (unsigned i=0;i<keys.size();i+=2) { require(map.erase(keys[i]));expected.erase(keys[i]); }
    check();
    for (unsigned i=0;i<keys.size();++i) { const bool fresh=!expected.contains(keys[i]);require(map.insert_or_assign(keys[i],~word(i))==fresh);expected[keys[i]]=~word(i); }
    check();map.clear();expected.clear();check();
}
int main() {
    width_check<8>(); width_check<16>(); width_check<24>(); width_check<32>();
    width_check<40>(); width_check<48>(); width_check<56>(); width_check<64>();
    exact_check<16,false,false>(); exact_check<16,true,true>();
    exact_check<32,false,false>(); exact_check<32,true,true>();
    exact_check<64,false,false>(); exact_check<64,true,true>();
    guard_check<false>(); guard_check<true>();
    exact_guard_shapes<1>(); exact_guard_shapes<6>(); exact_guard_shapes<8>();
    exact_guard_shapes<12>(); exact_guard_shapes<16>();
    staged_check<false>();staged_check<true>();
    staged_check<false,4>();staged_check<true,4>();
    staged_check<false,1,true>();staged_check<true,1,true>();
    staged_check<false,4,true>();staged_check<true,4,true>();
    quotient_check();
    std::puts("width checks: inverse/domain oracle, forced-overflow histories, complete16-bit domain, exact remainders, guard-page transfers, staged hits/misses/all tiers passed");
}

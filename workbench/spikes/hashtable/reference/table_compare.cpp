#include "table-plans.h"
#include "table.h"
#include "packed_key_baseline.h"
#include "support.h"
#include <absl/container/internal/hashtable_debug.h>
#include <map>
#include <random>
#include <source_location>
#include <string_view>
#include <unordered_set>
#include <sys/mman.h>
#include <unistd.h>

using namespace hashtable_spike;
namespace t=hashtable_spike::tiered;
namespace {
void check(bool ok,const std::source_location where=std::source_location::current()) {
    if(!ok){std::fprintf(stderr,"check failed at %s:%u\n",where.file_name(),where.line());std::abort();}
}
struct memory {
    std::size_t payload{},backing{},known_metadata{},primary{},secondary{},chain{},shared_preparation{},zero_image{};
    std::unordered_set<const void*> allocations,controls,prepared;
    void add(const oa::Allocation& a) {
        if(allocations.insert(a.address).second){payload+=a.size;backing+=oa::capacity(a);}
    }
    void metadata(const void* p,std::size_t n){if(n&&controls.insert(p).second)known_metadata+=n;}
};
template<t::bucket_plan P,t::bucket_plan Q,t::sharing Share,bool Quotient,bool Direct=false,bool Promote=false>
struct candidate {
    using body=t::table<P,Q,Share,Quotient,Direct,Promote>;
    static constexpr unsigned K=P.key_bits,V=P.value_bits;
    body contents;
    candidate(oa::Pool& pool,std::size_t,word seed):contents([&] {
        auto made=body::make(pool,seed,mix(seed^0x9e3779b97f4a7c15ULL));check(bool(made));return std::move(*made);
    }()){}
    explicit candidate(body b):contents(std::move(b)){}
    candidate distinct_rebuild(oa::Pool& pool,word seed)const {
        auto made=body::build_distinct(pool,seed,mix(seed^0x9e3779b97f4a7c15ULL),*this);
        check(bool(made));return candidate(std::move(*made));
    }
    [[gnu::always_inline]] bool get(word key,word& value)const{return contents.get(key,value);}
    bool set(word key,word value){auto r=contents.set(key,value);check(bool(r));check(r->result!=t::outcome::grow_required);return r->distinct_delta==1;}
    bool erase(word key){auto r=contents.erase(key);check(bool(r));return r->distinct_delta==-1;}
    std::size_t clear(){auto r=contents.clear();check(bool(r));return *r;}
    std::size_t size()const{return contents.size();}
    template<class F>void each(F&& f)const{contents.each(std::forward<F>(f));}
    void audit()const{contents.audit();}
    void account(memory& m)const {
        contents.allocations([&](const auto& a){m.add(a);});
        contents.metadata([&](const void* p,std::size_t n){m.metadata(p,n);});
        contents.preparation([&](const void* p,std::size_t n){if(m.prepared.insert(p).second)m.shared_preparation+=n;});
        m.zero_image=body::static_zero_image_bytes;
        m.metadata(this,sizeof(*this));
        const auto c=contents.counts();m.primary+=c.primary;m.secondary+=c.secondary;m.chain+=c.chain;
    }
};
template<unsigned KeyBits,unsigned ValueBits,bool Exact>struct baseline {
    static constexpr unsigned K=KeyBits,V=ValueBits;
    using key=std::conditional_t<Exact,exact_unsigned_key<K/8>,
        std::conditional_t<(K<=16),std::uint16_t,std::conditional_t<(K<=32),std::uint32_t,word>>>;
    using value=std::conditional_t<(V<=16),std::uint16_t,word>;
    using body=std::conditional_t<V==0,absl::flat_hash_set<key>,absl::flat_hash_map<key,value>>;
    body contents;
    baseline(oa::Pool&,std::size_t n,word){contents.reserve(n);}
    [[gnu::always_inline]] bool get(word k,word& out)const {
        auto i=contents.find(key(k));if(i==contents.end())return false;
        if constexpr(V)out=i->second;else out=0;return true;
    }
    bool set(word k,word v){if constexpr(V)return contents.insert_or_assign(key(k),value(v)).second;else return contents.insert(key(k)).second;}
    bool erase(word k){return contents.erase(key(k))!=0;}
    std::size_t clear(){const auto n=contents.size();contents.clear();return n;}
    std::size_t size()const{return contents.size();}
    template<class F>void each(F&& f)const {
        if constexpr(V)for(const auto& [k,v]:contents){if constexpr(Exact)f(k.value(),v);else f(word(k),v);}
        else for(const auto& k:contents){if constexpr(Exact)f(k.value(),0);else f(word(k),0);}
    }
    void audit()const{}
    void account(memory& m)const {
        m.payload+=absl::container_internal::AllocatedByteSize(contents);
        m.metadata(this,sizeof(*this));
    }
};
struct sample {double ns;word checksum;};
template<class F>sample timed(std::size_t operations,F&& f) {
    const auto begin=clock_type::now();word sum=f();asm volatile("" : "+r"(sum) :: "memory");
    const auto end=clock_type::now();return {std::chrono::duration<double,std::nano>(end-begin).count()/operations,sum};
}
template<unsigned K,unsigned V>struct trace {
    struct query {word key,value,next;bool found;};
    std::vector<word> keys,values,missing;
    std::vector<query> queries;
    trace(std::size_t n,unsigned generation,word seed,bool structured=false):keys(n),values(n),missing(n) {
        permutation<K,false> source(seed);
        for(std::size_t i=0;i<n;++i) {
            const word ordinal=generation*n+i;
            // Consecutive disjoint banks are a separate input family, not a
            // selection made using the candidate's private placement seeds.
            keys[i]=structured?ordinal:source(ordinal);
            missing[i]=structured?ordinal+n:source(ordinal+n);
        }
        std::mt19937_64 random(seed+generation);std::shuffle(keys.begin(),keys.end(),random);
        for(std::size_t i=0;i<n;++i)values[i]=mix(keys[i]^0x25bc21e6a37f492dULL)&mask(V);
        for(std::size_t i=0;i<n;++i){queries.push_back({keys[i],values[i],0,true});queries.push_back({missing[i],0,0,false});}
        std::shuffle(queries.begin(),queries.end(),random);
        for(std::size_t i=0;i<queries.size();++i)queries[i].next=((i+1==queries.size())?0:i+1)^queries[i].value^word(queries[i].found);
    }
};
template<class M>void verify(const M& map,const trace<M::K,M::V>& input) {
    check(map.size()==input.keys.size());std::map<word,word> oracle;
    for(std::size_t i=0;i<input.keys.size();++i)oracle.emplace(input.keys[i],input.values[i]);
    std::map<word,word> visited;
    map.each([&](word k,word v){check(visited.emplace(k,v).second);});check(visited==oracle);
    for(const auto& q:input.queries){word v=q.found?0:9173;check(map.get(q.key,v)==q.found);check(v==(q.found?q.value:9173));}
    map.audit();
}
template<class M>[[gnu::noinline]]word independent(const M& map,const trace<M::K,M::V>& input,unsigned rounds,bool mixed) {
    word sum=0;
    if(mixed)for(unsigned r=0;r<rounds;++r)for(const auto& q:input.queries){word v=0;const bool found=map.get(q.key,v);sum+=v+found;}
    else for(unsigned r=0;r<rounds;++r)for(const auto key:input.keys){word v=0;const bool found=map.get(key,v);sum+=v+found;}
    return sum;
}
template<class M>[[gnu::noinline]]word serial(const M& map,const trace<M::K,M::V>& input,unsigned rounds) {
    word at=0,sum=0;
    for(std::size_t i=0;i<input.queries.size()*rounds;++i) {
        const auto& q=input.queries[at];word v=0;const bool found=map.get(q.key,v);
        sum+=v+found;at=q.next^v^word(found);asm volatile("" : "+r"(at));
    }
    check(at==0);return sum;
}
template<class M>[[gnu::noinline]] void run(const char* name,std::size_t n,unsigned turnovers,unsigned repetition,bool structured,bool output) {
    pool_owner owner;std::optional<M> map;
    const word seed=mix(0x49802+repetition),input_seed=1237+repetition;
    auto input=trace<M::K,M::V>(n,0,input_seed,structured);
    const unsigned rounds=unsigned(std::max<std::size_t>(1,(131072+n-1)/n));
    auto emit=[&](const char* op,unsigned generation,sample result,const memory* retained=nullptr) {
        if(!output)return;
        memory m;if(map)map->account(m);if(retained)m=*retained;
        std::printf("%s,%u,%u,%zu,%u,%u,%s,%s,%.5f,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",name,M::K,M::V,n,repetition,generation,structured?"consecutive":"permuted",op,result.ns,(unsigned long long)result.checksum,m.primary,m.secondary,m.chain,m.payload,m.backing,m.known_metadata,m.shared_preparation,m.zero_image);
    };
    const auto short_lived=timed(8*n,[&] {
        word sum=0;
        for(unsigned cycle=0;cycle<8;++cycle) {
            M temporary(*owner.pool,n,seed);
            for(std::size_t i=0;i<n;++i)sum+=temporary.set(input.keys[i],input.values[i]);
            sum+=independent(temporary,input,1,true);
        }
        return sum;
    });
    word short_expected=n;for(word value:input.values)short_expected+=value+1;
    check(short_lived.checksum==8*short_expected);
    emit("build_probe_destroy_per_entry",0,short_lived);
    auto built=timed(n,[&]{map.emplace(*owner.pool,n,seed);word sum=0;for(std::size_t i=0;i<n;++i)sum+=map->set(input.keys[i],input.values[i]);return sum;});
    check(built.checksum==n);verify(*map,input);emit("construct_fill",0,built);
    for(unsigned generation=0;;++generation) {
        const auto hit=timed(n*rounds,[&]{return independent(*map,input,rounds,false);});
        const auto mixed=timed(2*n*rounds,[&]{return independent(*map,input,rounds,true);});
        const auto dependent=timed(2*n*rounds,[&]{return serial(*map,input,rounds);});
        word expected=0;for(word v:input.values)expected+=v+1;expected*=rounds;
        check(hit.checksum==expected&&mixed.checksum==expected&&dependent.checksum==expected);
        emit("independent_hit",generation,hit);emit("independent_mixed",generation,mixed);emit("dependent_mixed",generation,dependent);
        if(generation==turnovers)break;
        auto next=trace<M::K,M::V>(n,generation+1,input_seed,structured);
        auto changed=timed(n,[&]{word sum=0;for(std::size_t i=0;i<n;++i){sum+=map->erase(input.keys[i]);sum+=map->set(next.keys[i],next.values[i]);}return sum;});
        check(changed.checksum==2*n);verify(*map,next);input=std::move(next);emit("turnover_pair",generation+1,changed);
    }
    // Same stable-key update pass for both policies after the unmanaged aging
    // interval. This is where opportunistic promotion can recover primary space.
    const auto updated=timed(n,[&]{word added=0;for(std::size_t i=0;i<n;++i)added+=map->set(input.keys[i],input.values[i]^mask(M::V));return added;});
    check(updated.checksum==0);
    for(auto& value:input.values)value^=mask(M::V);
    for(auto& q:input.queries)if(q.found){q.value^=mask(M::V);q.next^=mask(M::V);}
    verify(*map,input);emit("stable_key_update",turnovers,updated);
    emit("after_update_independent_hit",turnovers,timed(n*rounds,[&]{return independent(*map,input,rounds,false);}));
    emit("after_update_dependent_mixed",turnovers,timed(2*n*rounds,[&]{return serial(*map,input,rounds);}));
    // Real retained immutable copies, but not an ObjectStore publication claim.
    // Charge copy, allocation and mutation together. Source remains observable.
    constexpr unsigned versions=16;
    std::vector<std::unique_ptr<M>> copies;copies.reserve(versions);
    auto forked=timed(versions,[&] {
        for(unsigned i=0;i<versions;++i) {
            auto p=std::make_unique<M>(*map);
            if constexpr(M::V)check(!p->set(input.keys[i%n],input.values[i%n]^mask(M::V)));
            else check(p->erase(input.keys[i%n]));
            copies.push_back(std::move(p));
        }
        return word(versions);
    });
    verify(*map,input);memory retained;map->account(retained);
    for(unsigned i=0;i<versions;++i) {
        word v=999;const bool present=copies[i]->get(input.keys[i%n],v);
        if constexpr(M::V)check(present&&v==(input.values[i%n]^mask(M::V)));else check(!present&&v==999);
        copies[i]->audit();copies[i]->account(retained);
    }
    emit("fork_one_mutation",turnovers,forked,&retained);
    auto released=timed(versions,[&]{copies.clear();return word(versions);});emit("release_forks",turnovers,released);
    std::optional<M> rebuilt;
    const auto rehash=timed(n,[&]{rebuilt.emplace(*owner.pool,n,mix(seed));map->each([&](word k,word v){check(rebuilt->set(k,v));});return word(n);});
    verify(*rebuilt,input);memory overlap;map->account(overlap);rebuilt->account(overlap);emit("reseed_rebuild",turnovers,rehash,&overlap);
    rebuilt.reset();
    if constexpr(requires{map->distinct_rebuild(*owner.pool,mix(seed));}) {
        const auto distinct=timed(n,[&]{rebuilt.emplace(map->distinct_rebuild(*owner.pool,mix(seed)));return word(n);});
        verify(*rebuilt,input);memory together;map->account(together);rebuilt->account(together);emit("reseed_distinct_scalar",turnovers,distinct,&together);
    }
    rebuilt.reset();
    const auto cleared=timed(n,[&]{return word(map->clear());});
    check(cleared.checksum==n&&map->size()==0);map->audit();
    for(word key:input.keys){word value=9173;check(!map->get(key,value)&&value==9173);}
    emit("clear_per_entry",turnovers,cleared);
    const auto refill=timed(n,[&]{word inserted=0;for(std::size_t i=0;i<n;++i)inserted+=map->set(input.keys[i],input.values[i]);return inserted;});
    check(refill.checksum==n);verify(*map,input);emit("refill",turnovers,refill);
    auto destroyed=timed(n,[&]{map.reset();return word(n);});emit("destroy_old",turnovers,destroyed);
}

template<unsigned K,unsigned A>void check_hash() {
    for(word seed:{word{0},word{1},~word{0},word{8173}}) {
        t::quotient_hash<K,A> h(seed);std::unordered_set<word> seen;
        t::quotient_hash<K,A,false> raw(seed);
        for(word i=0;i<(K<=16?(word{1}<<K):4096);++i) {
            word key=K<=16?i:mix(i)&mask(K);check(h.invert(h(key))==key);check(seen.insert(h(key)).second);
            check(raw.invert(raw(key))==key);
            check((h(key)>>(K-A))==(raw(key)>>(K-A)));
        }
        for(word key:{word{0},word{1},mask(K),mask(K)>>1})check(h.invert(h(key))==key);
    }
}
template<unsigned Bytes>void check_plane() {
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* memory=static_cast<byte*>(mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    check(memory!=MAP_FAILED&&mprotect(memory+page,page,PROT_NONE)==0);
    auto* p=memory+page-Bytes;
    t::identity_plane<Bytes> plane;
    for(unsigned round=0;round<257;++round) {
        for(unsigned i=0;i<Bytes;++i)p[i]=byte(round+i*31);
        const auto generic=plane.read.get_unchecked(p);
        alignas(16) std::array<byte,16> composed,direct;
#if defined(__aarch64__)
        vst1q_u8(composed.data(),plane.template get<false>(p));vst1q_u8(direct.data(),plane.template get<true>(p));
#else
        _mm_storeu_si128(reinterpret_cast<__m128i*>(composed.data()),plane.template get<false>(p));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(direct.data()),plane.template get<true>(p));
#endif
        check(composed==direct);
        for(unsigned i=0;i<16;++i)check(composed[i]==generic[i]&&composed[i]==(i<Bytes?p[i]:0));
    }
    check(munmap(memory,2*page)==0);
}
template<class M>void chain_check(unsigned entries=1200) {
    pool_owner owner;M map(*owner.pool,1,7123);std::map<word,word> oracle;
    // Deliberately exceed both finite tiers: independent oracle covers links,
    // duplicate lookup after primary vacancies, unlinking and block reuse.
    for(word i=0;i<entries;++i){word v=mix(i)&mask(M::V);check(map.set(i,v));oracle[i]=v;}
    map.audit();auto old=map;const auto before=oracle;
    for(word i=0;i<entries;i+=3){check(map.erase(i));oracle.erase(i);}
    for(word i=1;i<entries;i+=3){const auto v=mask(M::V)^oracle[i];check(!map.set(i,v));oracle[i]=v;}
    auto verify_oracle=[](const M& table,const auto& values){std::map<word,word> got;table.each([&](word k,word v){check(got.emplace(k,v).second);});check(got==values);for(const auto& [k,v]:values){word out=0;check(table.get(k,out)&&out==v);}table.audit();};
    verify_oracle(map,oracle);verify_oracle(old,before);
    for(const auto& [k,v]:oracle)check(map.erase(k));check(map.size()==0);map.audit();
    for(word i=0;i<entries;++i)check(map.set(i,mix(i)&mask(M::V)));
    verify_oracle(map,before);verify_oracle(old,before);
    const auto removed=map.contents.clear();check(bool(removed)&&*removed==entries);
    check(map.size()==0);map.audit();verify_oracle(old,before);
    check(map.contents.clear()==std::size_t{0});
    for(word i=0;i<entries;++i)check(map.set(i,mix(i)&mask(M::V)));
    verify_oracle(map,before);verify_oracle(old,before);
}
using namespace t;
using p16=std::integral_constant<bucket_plan,k40v16_a8_n16_primary_rows_byte>;
using q16=std::integral_constant<bucket_plan,k40v16_a5_n16_secondary_rows_byte>;
template<sharing S,bool H,bool D=false,bool Promote=false>using normal=candidate<p16::value,q16::value,S,H,D,Promote>;
template<sharing S,bool D=false>using wider=candidate<k40v16_a7_n32_primary_groups8_tight,k40v16_a5_n16_secondary_rows_byte,S,true,D>;
template<sharing S>using lean=candidate<k40v16_a8_n14_primary_rows_byte,k40v16_a6_n16_secondary_rows_byte,S,true>;
using pressure=candidate<k40v16_a3_n8_primary_groups8_tight,k40v16_a1_n8_secondary_rows_byte,sharing::chunks,true>;
template<key_search Search,bool Direct=false>using key_planes=candidate<
    searching(k40v64_a8_n16_primary_key_planes_byte,Search),
    searching(k40v64_a5_n16_secondary_key_planes_byte,Search),sharing::flat,true,Direct>;
template<bool Direct=false>using key_words=candidate<k40v64_a8_n16_primary_key_words_byte,
    searching(k40v64_a5_n16_secondary_key_planes_byte,key_search::exact_raw),sharing::flat,true,Direct>;
void check_words() {
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* memory=static_cast<byte*>(mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    check(memory!=MAP_FAILED&&mprotect(memory+page,page,PROT_NONE)==0);
    auto* p=memory+page-64;
    word_tile tile;single::field<24> suffix;
    for(unsigned round=0;round<257;++round) {
        std::array<std::uint32_t,16> expected;
        for(unsigned i=0;i<16;++i) {
            expected[i]=std::uint32_t(mix(round*16+i));
            std::memset(p+4*i,0xa5,4);
            word_tags::put(p,i,byte(expected[i]>>24));
            check(p[4*i]==0xa5&&p[4*i+1]==0xa5&&p[4*i+2]==0xa5);
            suffix.template put<false>(p+4*i,expected[i]&0xffffff);
            check(word_tags::get(p,i)==byte(expected[i]>>24));
        }
        const auto projected=tile.suffixes.get_unchecked(p);
        for(unsigned i=0;i<48;++i)check(projected[i]==byte(expected[i/3]>>(8*(i%3))));
        for(unsigned q=0;q<18;++q) {
            const auto key=q<16?expected[q]:std::uint32_t(q==16?0:~0u);
            int found=-1;for(unsigned i=0;i<16;++i)if(expected[i]==key){found=int(i);break;}
            check(tile.template find<false>(p,key)==found&&tile.template find<true>(p,key)==found);
        }
    }
    check(munmap(memory,2*page)==0);
    constexpr auto plan=k40v64_a8_n16_primary_key_words_byte;
    bucket_binding<plan> binding;std::array<byte,plan.bytes> bytes;
    bucket<plan> b{bytes.data(),binding};b.initialize();
    // Every remainder bit is legal. Stale inactive words can equal a query;
    // occupancy, not a reserved key, distinguishes the dense live prefix.
    for(unsigned i=0;i<16;++i)b.append(0xff000000u+i,word(i));
    for(unsigned count=16;count>0;--count) {
        check(b.find(0xff000000u+count-1)>=0);b.erase(count-1);
        check(b.find(0xff000000u+count-1)==-1);
        for(unsigned i=0;i+1<count;++i)check(b.find(0xff000000u+i)==int(i));
    }
}
void word_matrix(std::size_t n,unsigned turns,unsigned repetition,bool structured,bool output) {
    using runner=void(*)(const char*,std::size_t,unsigned,unsigned,bool,bool);
    const std::pair<const char*,runner> arms[]={
        {"abseil_native",run<baseline<40,64,false>>},
        {"rows_selective",run<candidate<k40v64_a8_n16_primary_rows_byte,k40v64_a5_n16_secondary_rows_byte,sharing::flat,true>>},
        {"planes_exact_raw",run<key_planes<key_search::exact_raw>>},
        {"words_exact_raw",run<key_words<>>},
        {"words_exact_raw_direct",run<key_words<true>>}};
    for(unsigned i=0;i<std::size(arms);++i){const auto& [name,f]=arms[(i+repetition)%std::size(arms)];f(name,n,turns,repetition,structured,output);}
}
void exact_matrix(std::size_t n,unsigned turns,unsigned repetition,bool structured,bool output) {
    using runner=void(*)(const char*,std::size_t,unsigned,unsigned,bool,bool);
    const std::pair<const char*,runner> arms[]={
        {"abseil_native",run<baseline<40,64,false>>},
        {"rows_selective",run<candidate<k40v64_a8_n16_primary_rows_byte,k40v64_a5_n16_secondary_rows_byte,sharing::flat,true>>},
        {"planes_selective",run<key_planes<key_search::selective>>},
        {"planes_exact_encoded",run<key_planes<key_search::exact_encoded>>},
        {"planes_exact_raw",run<key_planes<key_search::exact_raw>>},
        {"planes_exact_raw_direct",run<key_planes<key_search::exact_raw,true>>}};
    for(unsigned i=0;i<std::size(arms);++i){const auto& [name,f]=arms[(i+repetition)%std::size(arms)];f(name,n,turns,repetition,structured,output);}
}
void matrix(std::size_t n,unsigned turns,unsigned repetition,bool structured,bool output,bool maintenance_only=false) {
    using runner=void(*)(const char*,std::size_t,unsigned,unsigned,bool,bool);
    const std::pair<const char*,runner> arms[]={
        {"abseil_exact",run<baseline<40,16,true>>},{"abseil_native",run<baseline<40,16,false>>},
        {"n16_strong_flat",run<normal<sharing::flat,false>>},
        {"n16_quotient_flat",run<normal<sharing::flat,true>>},
        {"n16_quotient_direct",run<normal<sharing::flat,true,true>>},
        {"n16_quotient_chunks",run<normal<sharing::chunks,true>>},
        {"n16_promote_flat",run<normal<sharing::flat,true,false,true>>},
        {"n32_tight_flat",run<wider<sharing::flat>>},
        {"n32_tight_direct",run<wider<sharing::flat,true>>},
        {"n32_tight_chunks",run<wider<sharing::chunks>>},
        {"n14_more_secondary",run<lean<sharing::flat>>}};
    if(maintenance_only) {
        constexpr unsigned chosen[]={0,1,3,6};
        for(unsigned i=0;i<std::size(chosen);++i) {
            const auto& [name,f]=arms[chosen[(i+repetition)%std::size(chosen)]];
            f(name,n,turns,repetition,structured,output);
        }
    } else for(unsigned i=0;i<std::size(arms);++i){const auto& [name,f]=arms[(i+repetition)%std::size(arms)];f(name,n,turns,repetition,structured,output);}
}
} // namespace
int main(int argc,char** argv) {
    const bool only_check=argc>1&&std::string_view(argv[1])=="check";
    const bool exact_check=argc>1&&std::string_view(argv[1])=="exact-check";
    const bool word_check=argc>1&&std::string_view(argv[1])=="word-check";
    check_hash<8,2>();check_hash<16,8>();check_hash<24,8>();check_hash<32,8>();
    check_hash<40,8>();check_hash<48,8>();check_hash<56,8>();check_hash<64,8>();
    if(word_check) {
        check_words();chain_check<key_words<>>(5000);
        word_matrix(3276,2,0,false,false);word_matrix(3276,2,1,true,false);
        std::puts("word tile: guarded coalesced load, independent projection/match oracle, disjoint stores, inactive FF tags, all tiers, retained readers and reseed passed");return 0;
    }
    if(exact_check) {
        check_plane<8>();check_plane<16>();
        chain_check<candidate<searching(k40v64_a5_n16_primary_key_planes_byte,key_search::exact_raw),
            searching(k40v64_a2_n16_secondary_key_planes_byte,key_search::exact_raw),sharing::flat,true>>();
        exact_matrix(3276,2,0,false,false);exact_matrix(3276,2,1,true,false);
        std::puts("exact keys: inverse, identical bucket placement, guarded TP planes, oracle, dense deletion, all tiers, retained readers and reseed passed");return 0;
    }
    chain_check<pressure>();
    chain_check<candidate<k40v16_a3_n8_primary_groups8_tight,k40v16_a1_n8_secondary_rows_byte,sharing::chunks,true,false,true>>();
    if(only_check){matrix(3276,2,0,false,false);matrix(3276,2,1,true,false);std::puts("tiered consumer: oracle, inverse, churn, all tiers, snapshot isolation and reseed rebuild passed");return 0;}
    const auto n=argc>1?std::stoull(argv[1]):3276;
    const unsigned repetitions=argc>2?unsigned(std::stoul(argv[2])):3;
    const bool maintenance_only=argc>3&&std::string_view(argv[3])=="maintenance";
    const bool exact=argc>3&&std::string_view(argv[3])=="exact";
    const bool words=argc>3&&std::string_view(argv[3])=="words";
    check(n>0&&n<100000);
    std::puts("arm,K,V,entries,repetition,generation,input,operation,ns_per_op,checksum,primary_entries,secondary_entries,chain_entries,payload_bytes,known_backing_bytes,known_metadata_bytes,shared_preparation_bytes,static_zero_image_bytes");
    for(unsigned r=0;r<repetitions;++r)for(bool structured:{false,true}) {
        if(words)word_matrix(n,8,r,structured,true);
        else if(exact)exact_matrix(n,8,r,structured,true);
        else matrix(n,8,r,structured,true,maintenance_only);
    }
}

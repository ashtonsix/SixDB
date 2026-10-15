#include "support.h"
#include "baseline.h"
#include <absl/container/flat_hash_set.h>
#include <chrono>
#include <random>
#include <string_view>
#include <unordered_set>
#include <sys/mman.h>
#include <unistd.h>

using namespace hashtable_spike;
using Clock=std::chrono::steady_clock;

// Zero payload bytes in primary/secondary buckets; overflow is still the
// current full-entry buffer control, whose unused value bytes remain charged.
template<bool Direct,unsigned Bound> struct packed_set {
    basic_map<Direct,Bound,64,0> table;
    packed_set(oa::Pool& pool,std::size_t n,unsigned slots,unsigned rounding,word seed)
        : table(pool,n,slots,false,rounding,seed) {}
    [[gnu::always_inline]] bool contains(word key) const { word unused=0;return table.get(key,unused); }
    [[gnu::always_inline]] bool insert(word key) { return table.insert_or_assign(key,0); }
    [[gnu::always_inline]] bool erase(word key) { return table.erase(key); }
    std::size_t size() const { return table.size(); }
};

struct abseil_set {
    using Ordinary=absl::flat_hash_set<word>;
    allocation_counters allocations;
    absl::flat_hash_set<word,Ordinary::hasher,Ordinary::key_equal,counting_allocator<word>> table;
    abseil_set(std::size_t n) : table(0,Ordinary::hasher{},Ordinary::key_equal{},counting_allocator<word>(allocations)) { table.reserve(n); }
    [[gnu::always_inline]] bool contains(word key) const { return table.contains(key); }
    [[gnu::always_inline]] bool insert(word key) { return table.insert(key).second; }
    [[gnu::always_inline]] bool erase(word key) { return table.erase(key)!=0; }
    std::size_t size() const { return table.size(); }
};

struct Sample { double ns;word checksum; };
template<class F> Sample timed(std::size_t n,F&& f) {
    const auto begin=Clock::now();word sum=f();
    asm volatile("":"+r"(sum)::"memory");
    return {std::chrono::duration<double,std::nano>(Clock::now()-begin).count()/double(n),sum};
}
template<class Set> Sample independent(const Set& set,const std::vector<word>& keys,std::size_t operations) {
    return timed(operations,[&] {word sum=0;for(std::size_t i=0;i<operations;++i) sum+=set.contains(keys[i&(keys.size()-1)]);return sum;});
}
struct Query { word key;std::size_t next[2]; };
// Each membership answer selects the address of the next index. Half the
// answers are false, in shuffled order. This includes the common query-source
// load and selection cost; it is not an isolated instruction-latency claim.
template<class Set> [[gnu::noinline]] Sample serial(const Set& set,const std::vector<Query>& queries,std::size_t operations) {
    return timed(operations,[&] {
        std::size_t index=0;word sum=0;
        for(std::size_t i=0;i<operations;++i) {
            const auto& q=queries[index];const bool found=set.contains(q.key);
            index=q.next[found];sum+=word(found);
            asm volatile("":"+r"(index):"r"(found));
        }
        return sum^index;
    });
}

template<bool Direct,unsigned Bound> void check_family(unsigned slots,unsigned rounding) {
    pool_owner pool;packed_set<Direct,Bound> set(*pool.pool,16,slots,rounding,88113);
    std::unordered_set<word> expected;std::mt19937_64 rng(1849);
    for(unsigned step=0;step<12000;++step) {
        const word k=mix(rng()%512);
        if(step%3) require(set.insert(k)==expected.insert(k).second);
        else require(set.erase(k)==(expected.erase(k)!=0));
        require(set.contains(k)==expected.contains(k));
        if(step%256==0) {
            set.table.audit();require(set.size()==expected.size());
            std::unordered_set<word> seen;
            set.table.each([&](word key,word value) {require(value==0 && expected.contains(key) && seen.insert(key).second);});
            require(seen==expected);
        }
    }
    set.table.rebuild(expected.size()*2,133771);
    for(auto k:expected) require(set.contains(k));
    for(auto k:expected) require(set.erase(k));
    set.table.audit();require(set.size()==0);
    for(auto k:expected) require(!set.contains(k));
    set.table.clear();require(set.insert(~word{}));require(set.contains(~word{}));
}
template<bool Direct> void guard_check() {
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* storage=static_cast<byte*>(mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    require(storage!=MAP_FAILED && mprotect(storage+page,page,PROT_NONE)==0);
    for(unsigned a=1;a<64;++a) for(unsigned slots:{7u,8u,12u,16u}) for(unsigned rounding:{1u,32u,64u}) {
        geometry<64,0> g(slots,a,false,rounding,false);g.admit_row_suffix_window();require(g.stride<page);
        byte* bucket=storage+page-g.stride;std::memset(bucket,0,g.stride);std::memset(bucket+g.header,255,g.lanes);
        const word domain=mask(64-a);
        const unsigned live=unsigned(std::min<word>(slots,domain+1));
        for(unsigned i=0;i<live;++i) g.template append<Direct>(bucket,domain-i,0);
        auto find=[&](word h) {return slots<=8?g.template find8<Direct>(bucket,h):g.template find16<Direct,false,true>(bucket,h);};
        for(unsigned i=0;i<live;++i) require(find(domain-i)==int(i));
        // All formerly occupied tail lanes become sentinel255. Count clipping
        // must reject them even when the query's fingerprint is255.
        for(unsigned i=live;i>0;--i) {g.template erase<Direct>(bucket,i-1);require(find(domain-(i-1))==-1);}
    }
    require(munmap(storage,2*page)==0);
}
void check() {
    guard_check<false>();guard_check<true>();
    for(unsigned n:{1u,6u,7u,8u}) for(unsigned rounding:{1u,32u,64u}) {
        check_family<false,4>(n,rounding);check_family<true,4>(n,rounding);
    }
    for(unsigned n:{9u,12u,16u}) for(unsigned rounding:{1u,32u,64u}) {
        check_family<false,1>(n,rounding);check_family<true,1>(n,rounding);
    }
    std::puts("set checks: guarded suffix windows; zero values; oracle mutation, duplicate insert, all tiers, iteration, fresh-seed rebuild, erase and clear passed");
}

template<bool Direct,unsigned Bound> void compare(std::size_t n,unsigned reps,unsigned slots,unsigned rounding) {
    std::vector<word> keys(n),absent(n);for(std::size_t i=0;i<n;++i) {keys[i]=mix(i+1);absent[i]=mix(n+i+1);}
    std::mt19937_64 rng(1849);std::shuffle(keys.begin(),keys.end(),rng);std::shuffle(absent.begin(),absent.end(),rng);
    std::vector<std::size_t> order(2*n);std::iota(order.begin(),order.end(),0);std::shuffle(order.begin(),order.end(),rng);
    std::vector<Query> queries(2*n);
    for(std::size_t i=0;i<order.size();++i) {
        const auto index=order[i],next=order[(i+1)&(order.size()-1)];const bool present=index<n;
        queries[index].key=present?keys[index]:absent[index-n];
        queries[index].next[present]=next;queries[index].next[!present]=next^1;
    }
    order.clear();order.shrink_to_fit();
    const std::size_t operations=std::max<std::size_t>(2*n,262144);
    for(unsigned rep=0;rep<reps;++rep) {
        pool_owner pool;std::unique_ptr<packed_set<Direct,Bound>> pp;std::unique_ptr<abseil_set> ap;
        Sample pf{},af{},pb{},ab{};
        auto fill=[&](auto& set) {return timed(n,[&] {word sum=0;for(auto k:keys) sum+=set.insert(k);return sum;});};
        auto build_p=[&] {pp=std::make_unique<packed_set<Direct,Bound>>(*pool.pool,n,slots,rounding,mix(773+rep));pf=fill(*pp);return pf.checksum;};
        auto build_a=[&] {ap=std::make_unique<abseil_set>(n);af=fill(*ap);return af.checksum;};
        if(rep%2) {ab=timed(n,build_a);pb=timed(n,build_p);}else {pb=timed(n,build_p);ab=timed(n,build_a);}
        auto& p=*pp;auto& a=*ap;require(pf.checksum==n && af.checksum==n);
        for(auto k:keys) require(p.contains(k)&&a.contains(k));
        for(auto k:absent) require(!p.contains(k)&&!a.contains(k));
        auto output=[&](const char* name,Sample ps,Sample as) {
            require(ps.checksum==as.checksum);const auto mem=p.table.memory();const auto count=p.table.counts();
            auto row=[&](const char* table,Sample s,std::size_t bytes,std::size_t controls,std::size_t capacity) {
                const bool candidate=std::string_view(table)!="abseil_set";
                std::printf("%zu,%u,%u,%s,%s,%u,%.6f,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%u,%u,%zu,%zu\n",n,slots,rounding,table,name,rep,s.ns,
                    static_cast<unsigned long long>(s.checksum),bytes,controls,capacity,candidate?count.primary:0,candidate?count.secondary:0,candidate?count.overflow:0,
                    p.table.primary_bits(),p.table.primary_stride(),keys.size()*sizeof(word)+absent.size()*sizeof(word),queries.size()*sizeof(Query));
            };
            row(Direct?"direct":"packed",ps,mem.payload,mem.plans+mem.catalog,mem.capacity);
            row("abseil_set",as,a.allocations.current_bytes,sizeof(abseil_set::Ordinary),0);std::fflush(stdout);
        };
        output("reserved_fill",pf,af);output("build",pb,ab);
        auto pair=[&](const char* name,auto&& operation,bool warm) {
            auto run=[&](auto& set) {if(warm) {auto s=operation(set);asm volatile(""::"r"(s.checksum):"memory");}return operation(set);};
            Sample ps{},as{};if(rep%2) {as=run(a);ps=run(p);}else {ps=run(p);as=run(a);}output(name,ps,as);
        };
        pair("independent_hit",[&](auto& s) {return independent(s,keys,operations);},true);
        pair("independent_miss",[&](auto& s) {return independent(s,absent,operations);},true);
        pair("serial_mixed",[&](auto& s) {auto r=serial(s,queries,operations);require(r.checksum==operations/2);return r;},true);
        pair("duplicate_insert",[&](auto& s) {return timed(n,[&] {word sum=0;for(auto k:keys) sum+=s.insert(k);return sum;});},false);
        pair("erase_insert_pair",[&](auto& s) {return timed(n,[&] {word sum=0;for(std::size_t i=0;i<n;++i) {sum+=s.erase(keys[i]);sum+=s.insert(absent[i]);}return sum;});},false);
        require(p.size()==n&&a.size()==n);p.table.audit();
        for(auto k:keys) require(!p.contains(k)&&!a.contains(k));
        for(auto k:absent) require(p.contains(k)&&a.contains(k));
        pair("post_churn_hit",[&](auto& s) {return independent(s,absent,operations);},true);
    }
}
int main(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="check") {check();return 0;}
    require(argc==3 || (argc==4 && std::string_view(argv[3])=="compact"));const auto n=std::stoull(argv[1]);const unsigned reps=std::stoul(argv[2]);
    require(n>=2 && std::has_single_bit(n) && reps>0);
    std::puts("n,slots,rounding,table,operation,rep,ns_per_operation,checksum,requested_bytes,control_bytes,backing_capacity_bytes,primary_keys,secondary_keys,overflow_keys,primary_address_bits,primary_stride,key_input_bytes,serial_query_bytes");
    for(unsigned rounding:{1u,32u,64u}) {
        if(argc==4 && rounding!=1) continue;
        for(unsigned slots:{7u,8u}) {compare<false,4>(n,reps,slots,rounding);compare<true,4>(n,reps,slots,rounding);}
        for(unsigned slots:{12u,16u}) {compare<false,1>(n,reps,slots,rounding);compare<true,1>(n,reps,slots,rounding);}
    }
}

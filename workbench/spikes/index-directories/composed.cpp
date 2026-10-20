#include "index.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <random>
#include <string_view>

using namespace directory_spike;
struct Memory : std::pmr::memory_resource {
    std::size_t live{},peak{},calls{},fail_at=SIZE_MAX;
    void* do_allocate(std::size_t n,std::size_t a) override {
        if(calls++==fail_at)throw std::bad_alloc{};
        auto* p=std::pmr::new_delete_resource()->allocate(n,a);live+=n;peak=std::max(peak,live);return p;
    }
    void do_deallocate(void* p,std::size_t n,std::size_t a) override {require(live>=n);live-=n;std::pmr::new_delete_resource()->deallocate(p,n,a);}
    bool do_is_equal(const std::pmr::memory_resource& m) const noexcept override {return this==&m;}
    ~Memory(){require(!live);}
};
template<class F> double clocked(F fn) {
    auto t=std::chrono::steady_clock::now();fn();return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t).count();
}
void correctness(Runtime& rt) {
    for(unsigned j:{0,2})for(unsigned duplicate:{1,17})for(unsigned width:{8,64,256}) {
        Memory memory;Index index(rt,memory,j,width);auto rows=dataset(257,j,duplicate);
        index.edit(rows);Store::Scope old(index.store);index.verify(old,rows,true);
        auto next=rows;next[17].value=-9999;next[17].valid=true;
        index.edit(std::span(&next[17],1));
        {Store::Scope fresh(index.store);index.verify(fresh,next);}
        index.verify(old,rows); // Old bytes and first discovery at all five layers.
        std::vector<u64> removed;for(auto& row:next)removed.push_back(row.id);
        index.edit({},removed);{Store::Scope empty(index.store);index.verify(empty,{});}
        index.edit(rows);{Store::Scope reborn(index.store);index.verify(reborn,rows);}
        index.store.drain();index.verify(old,rows);
        // Private capture, refusal, abort and stale-basis preparation do not
        // publish a partially coupled directory/segment update.
        const auto source=old.bind(segment_id(4,unsigned(rows[0].physical>>16)));
        auto c=source.control();Image image;for(unsigned p=0;p<6;++p)image[p].assign(bytes(source,p).begin(),bytes(source,p).end());
        CopyStats stats;auto owner=capture(rt,index.store,c,image,source,true,stats);
        const std::array<Store::Change,1> changes{{{segment_id(c.layer,c.prefix),owner}}};
        Store::Prepared stale(index.store);auto refused=index.store.prepare(old,changes,stale);require(!refused && refused.error()==orbital::Error::stale);
        Store::Scope basis(index.store);Store::Prepared packet(index.store);
        memory.fail_at=memory.calls;auto failed=index.store.prepare(basis,changes,packet);memory.fail_at=SIZE_MAX;
        require(!failed);take(index.store.prepare(basis,changes,packet));packet.abort();
        require(index.seek(basis,4,rows[0].logical).present);
    }
    std::puts("composed oracle: five layers, all entry summaries, J=0/2, duplicates, 8/64/256-byte stored keys, old/fresh/late readers, removal/recreation, refusal, stale basis and abort passed");
}
u64 digest(const Outcome& o){return o.present ? o.count+o.logical+u64(o.sum):97;}
void measurements(Runtime& rt,unsigned ops,unsigned reps) {
    std::puts("phase,rows,J,key_bytes,duplicates,spacing,trace,layer,arm,trial,operations,ns_per_operation,checksum,copied_bytes,shared_bytes,allocated_bytes,objects,dirent_bytes,catalog_live_bytes,arena_charged_bytes,selected_hash_bytes,hash_created_bytes");
    for(unsigned n:{256,4096})for(unsigned j:{0,2})for(unsigned duplicate:{1,17}) {
        Memory memory;Index index(rt,memory,j,8);auto rows=dataset(n,j,duplicate);index.edit(rows);
        std::mt19937_64 rng(971);std::vector<u64> queries(ops);
        for(std::string_view trace:{"uniform-hit","logical-gap","hot-tenant","ordered","short-range"}) {
            for(unsigned q=0;q<ops;++q) {
                unsigned rank=trace=="ordered"?q%n:rng()%n;
                if(trace=="hot-tenant" && q%10)rank%=std::max(1u,n/16);
                queries[q]=rows[rank].logical+(trace=="logical-gap"?1:0);
            }
            for(unsigned layer:{0,2,4})for(unsigned trial=0;trial<reps;++trial) {
                std::array<u64,2> checksums{};
                for(unsigned offset=0;offset<2;++offset) {
                    unsigned arm=(trial+offset)%2;u64 checksum=0;
                    const auto ns=clocked([&]{for(auto q:queries) {
                        Store::Scope scope(index.store);
                        if(trace=="short-range")for(auto o:index.range(scope,layer,q,q+12,arm?Search::boundlist:Search::ordinary))checksum+=digest(o);
                        else checksum+=digest(index.seek(scope,layer,q,arm?Search::boundlist:Search::ordinary));
                    }});
                    asm volatile("" : "+r"(checksum) : : "memory");checksums[arm]=checksum;
                    Store::Scope accounting(index.store);
                    std::printf("read,%u,%u,8,%u,257,%s,%u,%s,%u,%u,%.3f,%llu,0,0,0,0,0,%zu,%llu,%llu,0\n",n,j,duplicate,trace.data(),layer,arm?"table-hash-boundlist":"std-lower-bound",trial,ops,ns/ops,
                        static_cast<unsigned long long>(checksum),memory.live,static_cast<unsigned long long>(rt.arena->charged_bytes.load()),(unsigned long long)index.cache_bytes(accounting));
                }
                require(checksums[0]==checksums[1]);
            }
        }
        // New index per treatment/trial; both arms pay the same sequence of
        // real summary changes, immutable capture and coherent publication.
        for(unsigned trial=0;trial<reps;++trial)for(unsigned offset=0;offset<2;++offset) {
            bool selective=(trial+offset)%2;Memory edit_memory;Index candidate(rt,edit_memory,j,8);candidate.edit(rows);
            EditStats totals;const unsigned writes=64;
            std::optional<Store::Scope> retained;retained.emplace(candidate.store);
            auto changed=rows;const auto ns=clocked([&]{for(unsigned i=0;i<writes;++i) {
                auto& row=changed[(i*31)%n];row.value+=1;
                const auto s=candidate.edit(std::span(&row,1),{},selective);
                totals.copied+=s.copied;totals.shared+=s.shared;totals.allocated+=s.allocated;totals.objects+=s.objects;totals.dirent_bytes+=s.dirent_bytes;totals.hash_created_bytes+=s.hash_created_bytes;
            }});
            {Store::Scope fresh(candidate.store);candidate.verify(fresh,changed);}
            candidate.verify(*retained,rows);
            Store::Scope accounting(candidate.store);
            std::printf("edit,%u,%u,8,%u,257,value-replace,4,%s,%u,%u,%.3f,0,%llu,%llu,%llu,%llu,%llu,%zu,%llu,%llu,%llu\n",n,j,duplicate,selective?"selective-parts":"full-copy",trial,writes,ns/writes,
                (unsigned long long)totals.copied,(unsigned long long)totals.shared,(unsigned long long)totals.allocated,(unsigned long long)totals.objects,(unsigned long long)totals.dirent_bytes,edit_memory.live,(unsigned long long)rt.arena->charged_bytes.load(),(unsigned long long)candidate.cache_bytes(accounting),(unsigned long long)totals.hash_created_bytes);
            retained.reset();candidate.store.drain();
        }
    }
}
int main(int argc,char** argv) {
    Runtime rt;
    if(argc>1 && std::string_view(argv[1])=="--check"){correctness(rt);return 0;}
    measurements(rt,argc>1?std::stoul(argv[1]):8192,argc>2?std::stoul(argv[2]):3);
}

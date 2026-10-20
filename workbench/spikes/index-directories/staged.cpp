#include "index.h"
#include <loom/ring.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <random>
#include <string_view>
using namespace directory_spike;
u64 digest(Outcome o){return o.present?o.count+u64(o.sum)+o.logical:97;}
struct Frame {std::optional<Store::Scope> scope;PendingRead pending;u64 result{};bool done{};};
using Ring=loom::Ring<Frame*>;
LOOM_NATIVE_CC void consume(Ring::Cursor ring,Ring::Slot* slot,Frame* frame) {
    frame->result=digest(finish_read(frame->pending));frame->done=true;
    [[clang::musttail]]return Ring::finish(ring,slot,frame);
}
void hint(PendingRead p) {
    if(!p.body)return;
    // Same fixed L1 hint in all staged arms, covering every intersecting line.
    const auto first=reinterpret_cast<std::uintptr_t>(p.body),last=first+sizeof(Summary);
    for(auto a=first&~std::uintptr_t(63);a<last;a+=64)__builtin_prefetch(reinterpret_cast<const void*>(a),0,3);
}
u64 run(Index& index,Ring& ring,std::span<const u64> queries,unsigned arm,unsigned width) {
    std::array<Frame,32> frames;u64 checksum=0;
    for(unsigned first=0;first<queries.size();) {
        const auto count=std::min<unsigned>(arm?width:1,queries.size()-first);
        for(unsigned i=0;i<count;++i) {
            auto& f=frames[i];f.scope.emplace(index.store);f.pending=index.prepare(*f.scope,4,queries[first+i]);f.done=false;
            if(arm!=3)hint(f.pending);
            if(arm>=2)require(ring.push(consume,{&f}));
        }
        if(arm>=2) {while(ring.pending())ring.spin({64});}
        else for(unsigned i=0;i<count;++i){frames[i].result=digest(finish_read(frames[i].pending));frames[i].done=true;}
        for(unsigned i=0;i<count;++i){require(frames[i].done);checksum+=frames[i].result;frames[i].scope.reset();}
        first+=count;
    }
    return checksum;
}
void check(Runtime& rt) {
    Index index(rt,*std::pmr::new_delete_resource(),0,8,6);auto rows=dataset(1025,0,17,65537);index.edit(rows);
    Ring ring;take(ring.configure(rt.worker,{8,8}));std::array<Frame,16> frames;
    u64 expected=0;
    for(unsigned i=0;i<16;++i){auto& f=frames[i];f.scope.emplace(index.store);f.pending=index.prepare(*f.scope,4,rows[i*37].logical);expected+=digest(finish_read(f.pending));require(ring.push(consume,{&f}));}
    ring.spin({1});++rows[0].value;index.edit(std::span(&rows[0],1));index.extend_schema(4096);
    while(ring.pending())ring.spin({3});u64 actual=0;
    for(auto& f:frames){require(f.done);actual+=f.result;f.scope.reset();}require(actual==expected);
    for(auto& f:frames){f.scope.emplace(index.store);f.pending=index.prepare(*f.scope,4,0);require(ring.push(consume,{&f}));}
    require(ring.discard()==16);for(auto& f:frames)f.scope.reset();take(ring.close());
    std::puts("staged oracle: actual active/overflow Ring, stationary snapshots and interpretation across publication/schema change, bounded spin, completion and discard passed");
}
void measure(Runtime& rt,unsigned ops,unsigned reps) {
    std::puts("phase,rows,J,trace,projection,arm,trial,operations,ns_per_operation,checksum,frame_bytes,ring_bytes");
    for(unsigned n:{4096,32768}) {
        Index index(rt,*std::pmr::new_delete_resource(),0,8,6);const auto rows=dataset(n,0,17,65537);index.edit(rows);
        Ring ring;take(ring.configure(rt.worker,{32,0}));std::mt19937_64 rng(1149);
        for(std::string_view trace:{"uniform-hit","logical-gap","hot-prefix"}) {
            std::vector<u64> queries(ops);for(unsigned i=0;i<ops;++i){auto rank=rng()%n;if(trace=="hot-prefix" && i%10)rank%=64;queries[i]=rows[rank].logical+(trace=="logical-gap");}
            for(unsigned width:{8,32})for(unsigned trial=0;trial<reps;++trial) {
                std::array<u64,4> checksums;
                for(unsigned offset=0;offset<4;++offset) {
                    const auto arm=(offset+trial)%4;const auto start=std::chrono::steady_clock::now();
                    checksums[arm]=run(index,ring,queries,arm,width);
                    const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/ops;
                    std::printf("staged,%u,0,%s,%u,%s,%u,%u,%.3f,%llu,%zu,%zu\n",n,trace.data(),width,std::array{"serial-hint","manual-hint","ring-hint","ring-no-hint"}[arm],trial,ops,ns,
                        (unsigned long long)checksums[arm],sizeof(Frame)*(arm?width:1),ring.storage_bytes());
                }
                require(checksums[0]==checksums[1] && checksums[1]==checksums[2] && checksums[2]==checksums[3]);
            }
        }
        take(ring.close());
    }
}
int main(int argc,char** argv){Runtime rt;if(argc>1 && std::string_view(argv[1])=="--check"){check(rt);return 0;}measure(rt,std::min(32768ul,argc>1?std::stoul(argv[1]):8192ul),argc>2?std::stoul(argv[2]):3);}

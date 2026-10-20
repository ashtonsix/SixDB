#include "placement.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string_view>
using namespace directory_spike;
void check() {
    for(auto policy:{PlacementPolicy::shift,PlacementPolicy::hierarchical,PlacementPolicy::midpoint}) {
        OrderedPlacement duplicates({{1,100,10,1,true},{2,101,20,2,true},{3,102,20,3,true},{4,103,20,4,true},{5,105,30,5,true}},0,policy,8);
        auto edit=duplicates.insert({6,0,15,6,true});require(bool(edit));duplicates.verify();
        require(edit->moved==1 && edit->duplicate_moves==1);
        require(std::ranges::find(duplicates.rows(),6,&Row::id)->physical==101);
        // Exhaust every position in a small equivalent domain, retaining full
        // order and uniqueness after every edit. No neighbouring tenant loans.
        OrderedPlacement full({},2,policy,8);std::mt19937_64 rng(913);
        for(unsigned i=0;i<256;++i) {auto r=full.insert({i+1,0,(u64(7)<<48)+(rng()%64),i64(i),true});require(bool(r));full.verify();}
        require(!full.insert({257,0,(u64(7)<<48)+17,1,true}));
        for(unsigned i=0;i<64;++i)full.erase(i*3+1);
        for(unsigned i=0;i<64;++i){auto r=full.insert({300+i,0,(u64(7)<<48)+(rng()%64),2,true});require(bool(r));full.verify();}
    }
    std::puts("placement oracle: boundary duplicate moved once, exhaustive 256-position domain, arbitrary duplicate order, tenant confinement, full refusal and delete/refill passed");
}
void measure(Runtime& rt,unsigned writes,unsigned reps) {
    std::puts("phase,rows,J,trace,arm,trial,operations,ns_per_operation,placement_ns_per_operation,moved_labels,redistribution_positions,duplicate_moves,copied_bytes,shared_bytes,allocated_bytes,changed_objects,dirent_bytes,segments,p50_ns,p99_ns,max_ns");
    for(unsigned n:{256,4096})for(unsigned j:{0,2})for(std::string_view trace:{"append","interior","fixed-hotspot","moving-hotspot","duplicate-run","delete-refill"})
        for(unsigned trial=0;trial<reps;++trial)for(unsigned offset=0;offset<3;++offset) {
            unsigned arm=(trial+offset)%3;
            auto policy=std::array{PlacementPolicy::shift,PlacementPolicy::hierarchical,PlacementPolicy::midpoint}[arm];
            auto rows=dataset(n,j,trace=="duplicate-run"?64:1,1);
            OrderedPlacement placement(rows,j,policy);
            Index index(rt,*std::pmr::new_delete_resource(),j);index.edit(rows);
            std::mt19937_64 rng(912);std::vector<double> latencies;latencies.reserve(writes);
            u64 moved=0,windows=0,duplicate_moves=0;EditStats totals;double placement_time=0,total=0;
            // A reader retained across the full mutation burst is included in
            // allocation/publication cost; its independent validation is outside.
            {
                Store::Scope old(index.store);
                for(unsigned i=0;i<writes;++i) {
                    u64 logical;
                    const auto& live=placement.rows();
                    if(trace=="append")logical=live.back().logical+4;
                    else if(trace=="fixed-hotspot")logical=rows[n/2].logical+(i%3);
                    else if(trace=="moving-hotspot")logical=rows[(u64(i)*n/writes)%n].logical+1;
                    else logical=rows[rng()%n].logical+(trace=="duplicate-run"?0:1);
                    const auto start=std::chrono::steady_clock::now();
                    std::vector<u64> removed;
                    if(trace=="delete-refill" && i<n){const auto oldrow=placement.erase(rows[i].id);removed.push_back(oldrow.id);logical=oldrow.logical;}
                    const auto a=std::chrono::steady_clock::now();
                    auto edit=placement.insert({u64(n)+i+1,0,logical,i64(i)-50,i%7!=0});require(bool(edit));
                    placement_time+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-a).count();
                    const auto s=index.edit(edit->changed,removed,true);
                    moved+=edit->moved;windows+=edit->window;duplicate_moves+=edit->duplicate_moves;
                    totals.copied+=s.copied;totals.shared+=s.shared;totals.allocated+=s.allocated;totals.objects+=s.objects;totals.dirent_bytes+=s.dirent_bytes;
                    const auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();latencies.push_back(elapsed);total+=elapsed;
                }
                index.verify(old,rows);
                placement.verify();Store::Scope fresh(index.store);index.verify(fresh,placement.rows());
            }
            std::ranges::sort(latencies);
            std::printf("placement,%u,%u,%s,%s,%u,%u,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%.3f,%.3f,%.3f\n",n,j,trace.data(),std::array{"shift","hierarchical","midpoint"}[arm],trial,writes,total/writes,placement_time/writes,
                (unsigned long long)moved,(unsigned long long)windows,(unsigned long long)duplicate_moves,(unsigned long long)totals.copied,(unsigned long long)totals.shared,
                (unsigned long long)totals.allocated,(unsigned long long)totals.objects,(unsigned long long)totals.dirent_bytes,index.segments(),latencies[writes/2],latencies[(writes-1)*99/100],latencies.back());
            index.store.drain();
        }
}
int main(int argc,char** argv) {
    if(argc>1 && std::string_view(argv[1])=="--check"){check();return 0;}
    Runtime rt;measure(rt,argc>1?std::stoul(argv[1]):64,argc>2?std::stoul(argv[2]):3);
}

#include "index.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <tuple>
using namespace directory_spike;

struct Memory : std::pmr::memory_resource {
    std::size_t live{},calls{},fail_at=SIZE_MAX;
    void* do_allocate(std::size_t n,std::size_t a)override{if(calls++==fail_at)throw std::bad_alloc{};auto* p=std::pmr::new_delete_resource()->allocate(n,a);live+=n;return p;}
    void do_deallocate(void* p,std::size_t n,std::size_t a)override{require(live>=n);live-=n;std::pmr::new_delete_resource()->deallocate(p,n,a);}
    bool do_is_equal(const std::pmr::memory_resource& r)const noexcept override{return this==&r;}
    ~Memory(){require(!live);}
};
Index make_index(Runtime& rt,std::pmr::memory_resource& memory,unsigned j,bool stable,unsigned bits=6,unsigned locators=2,unsigned extras=4){return Index(rt,memory,j,8,bits,extras,3,64,true,false,32,true,true,0,locators,stable);}
std::vector<Row> seed(unsigned n,unsigned j) {
    std::vector<Row> rows;for(unsigned i=0;i<n;++i)rows.push_back({i+1,j?(u64(i)*2<<32)+128:(u64(i)*2+1)*65536+128,j?u64(i)*2<<48:u64(i/17)*64,i64(i%23)-11,i%7!=0});return rows;
}
bool same_rows(std::vector<Row> actual,std::vector<Row> expected) {
    std::ranges::sort(actual,{},&Row::id);std::ranges::sort(expected,{},&Row::id);
    return actual.size()==expected.size() && std::equal(actual.begin(),actual.end(),expected.begin(),[](auto a,auto b){return std::tie(a.id,a.physical,a.logical,a.value,a.valid)==std::tie(b.id,b.physical,b.logical,b.value,b.valid);});
}
struct Step {std::vector<Row> add,after;std::vector<u64> remove;Row first;u64 key{},matches{};i64 sum{};bool convert{};Migration migration{};};
std::vector<Step> history(std::vector<Row> rows,unsigned j,unsigned kind,unsigned writes) {
    const auto original=rows;std::vector<Step> steps;u64 temporary=0;
    for(unsigned i=0;i<writes;++i) {
        Step s;const unsigned at=(i*251)%original.size();auto row=original[at];
        if(kind==0){++row.physical;s.add={row};}
        if(kind==1 || (kind==3 && i%4==1)){row.id=original.size()+i+1;row.physical+=u64(1)<<(j?32:16);if(j)row.logical+=u64(1)<<48;row.value=1000+i;row.valid=i%3!=0;s.add={row};temporary=row.id;}
        if(kind==2)s.remove={original[(original.size()/2+i)%original.size()].id};
        if(kind==3 && i%4==0){row.value=-1000-i;row.valid=!row.valid;s.add={row};}
        if(kind==3 && i%4==2){row.physical+=256;s.add={row};}
        if(kind==3 && i%4==3){require(temporary);s.remove={temporary};temporary=0;}
        if(kind==4){s.convert=true;s.migration={{4,unsigned(row.physical>>16)},4};}
        for(auto id:s.remove)std::erase_if(rows,[&](auto r){return r.id==id;});
        for(auto r:s.add){std::erase_if(rows,[&](auto old){return old.id==r.id;});rows.push_back(r);}
        std::ranges::sort(rows,{},&Row::physical);s.after=rows;s.key=rows[rows.size()/2].logical;
        s.first=*std::ranges::find_if(rows,[&](auto r){return r.logical>=s.key;});
        for(auto r:rows)if(r.logical==s.key){++s.matches;if(r.valid)s.sum+=r.value;}
        steps.push_back(std::move(s));
    }
    return steps;
}
void conversion_schema(Index& index){const std::array<Definition,1> definition{{{4,8,31,1,0,0}}};index.extend_schema(4,definition);}
EditStats perform(Index& index,const Step& step){return step.convert?take(index.migrate(std::span(&step.migration,1))):index.edit(step.add,step.remove);}
u64 reads(Index& index,const Step& s) {
    u64 checksum=0;
    for(unsigned i=0;i<4;++i){Store::Scope scope(index.store);const auto o=index.seek(scope,4,s.key);require(o.present && o.logical==s.first.logical && o.prefix==unsigned(s.first.physical>>16) && o.position==unsigned(s.first.physical&65535) && o.count==1 && o.not_null==s.first.valid && o.sum==(s.first.valid?s.first.value:0));checksum+=o.count+u64(o.sum);}
    Store::Scope scope(index.store);const auto range=index.range(scope,4,s.key,s.key);i64 sum=0;u64 count=0;for(auto o:range){count+=o.count;sum+=o.sum;}
    require(count==s.matches && sum==s.sum);return checksum+count+u64(sum);
}
void check(Runtime& rt) {
    u64 splits=0,merges=0,redistributions=0,injected=0,refused=0;
    for(unsigned j:{0,2})for(unsigned bits:{4,6})for(unsigned locators:{0,2})for(unsigned kind:{0,1,2,3,4}) {
        Memory memory;auto index=make_index(rt,memory,j,true,bits,locators);const auto initial=seed(128,j);index.edit(initial);Store::Scope old(index.store);
        if(kind==4)conversion_schema(index);
        for(const auto& step:history(initial,j,kind,96)) {const auto stats=perform(index,step);splits+=stats.page_splits;merges+=stats.page_merges;redistributions+=stats.page_redistributions;Store::Scope fresh(index.store);index.verify(fresh,step.after);reads(index,step);}
        index.verify(old,initial);const auto before=index.rows();std::vector<u64> remove;for(auto row:before)remove.push_back(row.id);index.edit({},remove);
        {Store::Scope empty(index.store);index.verify(empty,{});}index.edit(initial);{Store::Scope fresh(index.store);index.verify(fresh,initial);}index.verify(old,initial);
    }
    require(splits && merges && redistributions);
    for(unsigned kind:{0,1,2,3}) {
        const auto initial=seed(kind==3?0:kind?128:64,0);auto additions=seed(kind==3?65:1,0);if(kind!=3)additions={{10000,2*65536+128,0,7000,true}};
        std::vector<u64> remove;if(kind==1 || kind==2)for(unsigned i=0;i<(kind==1?64:49);++i)remove.push_back(initial[i].id);
        if(kind==1 || kind==2)additions.clear();
        auto expected=initial;for(auto id:remove)std::erase_if(expected,[&](auto row){return row.id==id;});for(auto row:additions)expected.push_back(row);
        std::size_t visits;
        {Memory memory;auto index=make_index(rt,memory,0,true);index.edit(initial);EditFaults faults;auto s=take(index.try_edit(additions,remove,true,&faults));visits=faults.visits;require(kind==0 || kind==3?s.page_splits>0:kind==1?s.page_merges>0:s.page_redistributions>0);}
        {Memory memory;auto index=make_index(rt,memory,0,true);index.edit(initial);Store::Scope old(index.store);
        for(std::size_t i=0;i<visits;++i) {EditFaults faults;faults.fail_at=i;require(!index.try_edit(additions,remove,true,&faults));require(same_rows(index.rows(),initial));Store::Scope fresh(index.store);require(fresh.cut()==old.cut());index.verify(fresh,initial);index.verify(old,initial);++injected;}
        index.edit(additions,remove);Store::Scope fresh(index.store);index.verify(fresh,expected);index.verify(old,initial);}
        for(std::size_t offset=0;;++offset) {
            Memory memory;auto index=make_index(rt,memory,0,true);index.edit(initial);Store::Scope old(index.store);EditFaults faults;faults.journal_memory=&memory;faults.journal_local_bytes=0;
            memory.fail_at=memory.calls+offset;auto result=index.try_edit(additions,remove,true,&faults);memory.fail_at=SIZE_MAX;
            if(result){Store::Scope fresh(index.store);index.verify(fresh,expected);break;}
            require(result.error()==orbital::Error::capacity);require(same_rows(index.rows(),initial));{Store::Scope fresh(index.store);require(fresh.cut()==old.cut());index.verify(fresh,initial);}
            index.edit(additions,remove);Store::Scope fresh(index.store);index.verify(fresh,expected);index.verify(old,initial);++refused;
        }
    }
    {
        Memory memory;auto index=make_index(rt,memory,0,true,4);auto rows=seed(33,0);rows.push_back({999,u64(UINT32_MAX)*65536+65535,UINT64_MAX,7,true});const auto original=rows;index.edit(rows);Store::Scope old(index.store);index.verify(old,rows);
        rows.erase(rows.begin());index.edit({},std::array<u64,1>{1});{Store::Scope fresh(index.store);index.verify(fresh,rows);}index.verify(old,original);
    }
    std::printf("maintenance oracle: %llu splits, %llu merges, %llu redistributions; %llu injected and %llu actual metadata/journal allocation failures; mixed reads, duplicate runs, tenant hashes, disappearance/recreation and retained readers passed\n",(unsigned long long)splits,(unsigned long long)merges,(unsigned long long)redistributions,(unsigned long long)injected,(unsigned long long)refused);
}
template<class F>double timed(F f){const auto start=std::chrono::steady_clock::now();f();return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();}
void measure(unsigned operations,unsigned reps) {
    std::puts("phase,segments_L0,segments_L1,segments_L2,segments_L3,segments_L4,J,trace,arm,trial,operations,ns_per_operation,fixture_ns_per_operation,copied_bytes,allocated_bytes,directory_captures,hash_created_bytes,journal_record_bytes,fixture_copied_bytes,fixture_captures,dirent_changes,page_splits,page_merges,page_redistributions,balance_entries_visited,directory_entries_visited,overflow_entries_visited,model_ns,segment_image_ns,segment_capture_ns,dirent_ns,directory_ns,prepare_ns,install_ns,segment_changes");
    const unsigned writes=std::min(96u,operations);
    for(unsigned n:{256,4096})for(unsigned j:{0,2})for(unsigned kind:{0,1,2,3,4})for(unsigned trial=0;trial<reps;++trial)for(unsigned offset=0;offset<2;++offset) {
        const bool stable=(offset+trial)%2;Runtime rt;Memory memory;auto index=make_index(rt,memory,j,stable);const auto initial=seed(n,j);const auto steps=history(initial,j,kind,writes);index.edit(initial);if(kind==4)conversion_schema(index);const auto population=index.directory_population();Store::Scope old(index.store);EditStats total;u64 checksum=0;
        const auto ns=timed([&]{for(const auto& step:steps){const auto s=perform(index,step);total.copied+=s.copied;total.allocated+=s.allocated;total.objects+=s.objects;total.hash_created_bytes+=s.hash_created_bytes;total.journal_record_bytes+=s.journal_record_bytes;
            total.page_splits+=s.page_splits;total.page_merges+=s.page_merges;total.page_redistributions+=s.page_redistributions;total.page_balance_rows+=s.page_balance_rows;total.directory_rows+=s.directory_rows;total.overflow_rows+=s.overflow_rows;
            total.dirent_ns+=s.dirent_ns;total.directory_copied+=s.directory_copied;total.directory_allocated+=s.directory_allocated;total.directory_objects+=s.directory_objects;total.directory_journal_bytes+=s.directory_journal_bytes;
            total.dirent_changes+=s.dirent_changes;
            total.segment_changes+=s.segment_changes;
            total.model_ns+=s.model_ns;total.segment_image_ns+=s.segment_image_ns;total.segment_capture_ns+=s.segment_capture_ns;total.directory_ns+=s.directory_ns;total.prepare_ns+=s.prepare_ns;total.install_ns+=s.install_ns;if(kind==3)checksum+=reads(index,step);}});
        asm volatile("" : "+r"(checksum) : : "memory");Store::Scope fresh(index.store);index.verify(fresh,steps.back().after);index.verify(old,initial);
        std::printf("directory-maintenance,%llu,%llu,%llu,%llu,%llu,%u,%s,%s,%u,%u,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            (unsigned long long)population[0],(unsigned long long)population[1],(unsigned long long)population[2],(unsigned long long)population[3],(unsigned long long)population[4],j,std::array{"segment-rewrite","segment-insert","segment-remove","mixed-reads","representation-parts"}[kind],stable?"stable-pages":"repartition-roster",trial,writes,double(total.directory_ns+total.dirent_ns)/writes,ns/writes,
            (unsigned long long)total.directory_copied,(unsigned long long)total.directory_allocated,(unsigned long long)total.directory_objects,(unsigned long long)total.hash_created_bytes,(unsigned long long)total.directory_journal_bytes,(unsigned long long)total.copied,(unsigned long long)total.objects,
            (unsigned long long)total.dirent_changes,(unsigned long long)total.page_splits,(unsigned long long)total.page_merges,(unsigned long long)total.page_redistributions,(unsigned long long)total.page_balance_rows,(unsigned long long)total.directory_rows,(unsigned long long)total.overflow_rows,
            (unsigned long long)total.model_ns,(unsigned long long)total.segment_image_ns,(unsigned long long)total.segment_capture_ns,(unsigned long long)total.dirent_ns,(unsigned long long)total.directory_ns,(unsigned long long)total.prepare_ns,(unsigned long long)total.install_ns,(unsigned long long)total.segment_changes);
    }
}
int main(int argc,char** argv){if(argc>1 && std::string_view(argv[1])=="--check"){Runtime rt;check(rt);return 0;}measure(argc>1?std::stoul(argv[1]):96,argc>2?std::stoul(argv[2]):3);}

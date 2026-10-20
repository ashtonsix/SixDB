#include "index.h"
#include <algorithm>
#include <chrono>
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
template<class F>double timed(F f){const auto t=std::chrono::steady_clock::now();f();return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t).count();}
bool equal_rows(std::span<const Row> a,std::span<const Row> b){return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](auto x,auto y){return std::tie(x.id,x.physical,x.logical,x.value,x.valid)==std::tie(y.id,y.physical,y.logical,y.value,y.valid);});}
void settle(Runtime& rt){loom::poll(rt.worker,{1024,0});}
struct Fixture {
    Memory memory;Index index;
    unsigned kind;std::vector<Row> before,after,add;std::vector<u64> remove;
    Fixture(Runtime& rt,unsigned trace,unsigned j,unsigned mode,unsigned layout):
        index(rt,memory,j,8,layout?5:0,4,3,64,true,layout==2,32,layout==1,true,0,mode),kind(trace),before(dataset(trace?65:17,j,17,j?257:65537)),after(before) {
        if(trace==0){add=before;before.clear();return;}
        index.edit(before);
        if(trace==1){add={before[17]};add[0].value=-9000;add[0].valid=true;after[17]=add[0];}
        if(trace==2){add={before[17]};add[0].id=10000;++add[0].physical;remove={before[5].id};after.erase(after.begin()+5);after.push_back(add[0]);}
        if(trace==3){for(auto r:before)remove.push_back(r.id);after.clear();}
        if(trace==5){const std::array<Definition,1> defs{{{4,8,31,1,0,0}}};index.extend_schema(4,defs);}
    }
    orbital::Result<EditStats> apply(EditFaults* faults=nullptr) {
        if(kind==4){const std::array<Definition,1> defs{{{100,8,31,1,256,1}}};auto s=index.try_extend_schema(100,defs,faults);if(!s)return std::unexpected(s.error());EditStats e;static_cast<CopyStats&>(e)=*s;return e;}
        if(kind==5){const std::array<Migration,2> migrations{{{{0,0},4},{{4,unsigned(before[17].physical>>16)},4}}};return index.migrate(migrations,true,faults);}
        return index.try_edit(add,remove,true,faults);
    }
    void verify_before(){require(equal_rows(index.rows(),before));Store::Scope scope(index.store);index.verify(scope,before);}
    void verify_after(){require(equal_rows(index.rows(),after));Store::Scope scope(index.store);index.verify(scope,after);if(kind==4)require(scope.bind(schema_id()).control().count==100);if(kind==5)require(scope.bind(segment_id(0,0)).control().representation==4);}
};
void check(Runtime& rt) {
    u64 injected=0,metadata=0;std::array<u64,unsigned(FaultPoint::count)> reached{};
    {
        Memory memory;std::pmr::map<unsigned,unsigned> map(&memory);map.emplace(1,7);
        {
            UndoJournal undo;auto& value=undo.get(map,2);value=19;undo.save(map.at(1));map.at(1)=8;undo.erase(map,map.find(1));
            memory.fail_at=memory.calls; // Undo must reinsert the retained node without allocating.
        }
        require(map.size()==1 && map.at(1)==7 && memory.calls==memory.fail_at);memory.fail_at=SIZE_MAX;
        {UndoJournal undo;memory.fail_at=memory.calls;try{undo.get(map,3)=11;require(false);}catch(const std::bad_alloc&){}memory.fail_at=SIZE_MAX;}
        require(map.size()==1 && map.at(1)==7);
    }
    // Consecutive replacements remove repeated extrema witnesses, cross the
    // all-null boundary, change a null payload and repeat an unchanged value.
    // J=0 shares upper-layer positions; J=2 also covers separate tenant prefixes.
    for(unsigned j:{0,2})for(unsigned mode:{0,2}) {
        Memory memory;Index index(rt,memory,j,8,5,4,3,64,true,false,32,true,true,0,mode);
        auto rows=dataset(9,j,17,1);for(auto& row:rows)row.valid=false;
        rows[0].value=rows[1].value=7;rows[2].value=-3;rows[3].value=0;
        for(unsigned i=0;i<4;++i)rows[i].valid=true;index.edit(rows);
        const std::array<std::tuple<unsigned,i64,bool>,13> history{{{0,7,false},{1,0,false},{2,-3,false},{3,0,false},{4,999,false},{0,0,true},{1,-10,true},{2,20,true},{3,20,true},{2,20,false},{3,20,false},{1,10,true},{1,10,true}}};
        for(auto [id,value,valid]:history) {
            Store::Scope old(index.store);const auto before=rows;auto replacement=rows[id];replacement.value=value;replacement.valid=valid;
            EditFaults count;count.before_prepare=[](void*){throw EditFailure{orbital::Error::capacity};};require(!index.try_edit(std::span(&replacement,1),{},true,&count));
            for(std::size_t i=0;i<count.visits;++i) {
                EditFaults faults;faults.fail_at=i;require(!index.try_edit(std::span(&replacement,1),{},true,&faults));
                require(equal_rows(index.rows(),before));Store::Scope fresh(index.store);require(fresh.cut()==old.cut());index.verify(fresh,before);++injected;
            }
            index.edit(std::span(&replacement,1));rows[id]=replacement;Store::Scope fresh(index.store);index.verify(fresh,rows);index.verify(old,before);settle(rt);
        }
    }
    for(unsigned j:{0,2})for(unsigned mode:{0,2})for(unsigned layout:{0,1,2})for(unsigned trace=0;trace<6;++trace) {
        std::size_t visits=0;
        {Fixture probe(rt,trace,j,mode,layout);EditFaults count;take(probe.apply(&count));visits=count.visits;probe.verify_after();for(unsigned p=0;p<reached.size();++p)reached[p]+=count.points[p];}
        settle(rt);Fixture fixture(rt,trace,j,mode,layout);Store::Scope old(fixture.index.store);const auto population=fixture.index.segments();
        for(std::size_t i=0;i<visits;++i) {
            EditFaults faults;faults.fail_at=i;const auto failed=fixture.apply(&faults);require(!failed && failed.error()==orbital::Error::capacity);
            {Store::Scope fresh(fixture.index.store);require(fresh.cut()==old.cut());fixture.index.verify(fresh,fixture.before);}
            fixture.verify_before();require(fixture.index.segments()==population);fixture.index.verify(old,fixture.before);++injected;settle(rt);
        }
        take(fixture.apply());fixture.verify_after();fixture.index.verify(old,fixture.before);settle(rt);
    }
    for(auto count:reached)require(count);
    // Fresh Store per offset: growing preparation capacity must not remove
    // later allocator failures from the sweep through an already warmed Core.
    for(bool journal_storage:{false,true})for(unsigned trace=0;trace<6;++trace)for(std::size_t offset=0;;++offset) {
        Fixture fixture(rt,trace,0,2,1);Store::Scope old(fixture.index.store);
        EditFaults faults;if(journal_storage){faults.journal_memory=&fixture.memory;faults.journal_local_bytes=0;}
        fixture.memory.fail_at=fixture.memory.calls+offset;const auto result=fixture.apply(&faults);fixture.memory.fail_at=SIZE_MAX;
        if(result){fixture.verify_after();break;}
        require(result.error()==orbital::Error::capacity);fixture.verify_before();
        {Store::Scope fresh(fixture.index.store);require(fresh.cut()==old.cut());}
        take(fixture.apply());fixture.verify_after();fixture.index.verify(old,fixture.before);++metadata;settle(rt);
    }
    for(unsigned mode:{0,2})for(bool stale:{false,true}) {
        Fixture fixture(rt,1,0,mode,1);Store::Scope old(fixture.index.store);
        struct Context {Runtime& rt;Fixture& fixture;bool stale;} context{rt,fixture,stale};
        EditFaults faults;faults.context=&context;faults.before_prepare=[](void* value){
            auto& c=*static_cast<Context*>(value);if(!c.stale){c.fixture.memory.fail_at=c.fixture.memory.calls;return;}
            auto& store=c.fixture.index.store;Store::Scope basis(store);const auto selected=basis.bind(schema_id());auto control=selected.control();++control.revision;
            Image image;for(unsigned p=0;p<selected.part_count();++p)image[p].assign(bytes(selected,p).begin(),bytes(selected,p).end());CopyStats stats;
            const std::array<Store::Change,1> changes{{{schema_id(),capture(c.rt,store,control,image,selected,true,stats)}}};publish(store,basis,changes);
        };
        auto refused=fixture.apply(&faults);fixture.memory.fail_at=SIZE_MAX;
        require(!refused && refused.error()==(stale?orbital::Error::stale:orbital::Error::capacity));fixture.verify_before();
        {Store::Scope fresh(fixture.index.store);require(fresh.cut()==old.cut()+(stale?1:0));}
        take(fixture.apply());fixture.verify_after();fixture.index.verify(old,fixture.before);
    }
    std::printf("failure oracle: %llu injected construction/prepare/accepted-abort failures; %llu actual metadata/journal allocator failures; allocation-free reverse replay, insert/delete/recreate/schema/migration retries, actual preparation refusal/stale basis and retained readers passed\n",(unsigned long long)injected,(unsigned long long)metadata);
}
void measure(unsigned,unsigned reps) {
    std::puts("phase,rows,J,trace,arm,trial,operations,ns_per_operation,copied_bytes,allocated_bytes,changed_objects,journal_records,journal_record_bytes,journal_heap_bytes,failed_attempts,discarded_copied_bytes,model_ns,segment_capture_ns,directory_ns,prepare_ns,install_ns");
    for(unsigned n:{256,4096})for(unsigned j:{0,2})for(unsigned trace:{0,1})for(unsigned trial=0;trial<reps;++trial)for(unsigned offset=0;offset<4;++offset) {
        const unsigned arm=(offset+trial)%4;Runtime rt;Memory memory;Index index(rt,memory,j,8,6,4,3,64,true,false,32,true,true,0,2);
        auto rows=dataset(n,j,17,j?257:65537),original=rows;index.edit(rows);Store::Scope old(index.store);
        EditStats total;u64 failures=0,discarded=0;constexpr unsigned writes=32;
        const auto ns=timed([&]{for(unsigned i=0;i<writes;++i) {
            auto& row=rows[(i*251)%n];if(trace)++row.physical;else{++row.value;row.valid=true;}
            if(arm==3 && i%8==0) {EditFaults faults;faults.before_prepare=[](void*){throw EditFailure{orbital::Error::capacity};};auto failed=index.try_edit(std::span(&row,1),{},true,&faults);require(!failed);++failures;discarded+=faults.private_copied_bytes;}
            const auto s=arm?take(index.try_edit(std::span(&row,1),{},true,nullptr,arm!=1)):index.edit_unprotected(std::span(&row,1));
            total.copied+=s.copied;total.allocated+=s.allocated;total.objects+=s.objects;total.journal_records+=s.journal_records;total.journal_record_bytes+=s.journal_record_bytes;total.journal_heap_bytes+=s.journal_heap_bytes;
            total.model_ns+=s.model_ns;total.segment_capture_ns+=s.segment_capture_ns;total.directory_ns+=s.directory_ns;total.prepare_ns+=s.prepare_ns;total.install_ns+=s.install_ns;
        }});
        {Store::Scope fresh(index.store);index.verify(fresh,rows);}index.verify(old,original);
        std::printf("editor,%u,%u,%s,%s,%u,%u,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",n,j,trace?"physical-relabel":"value-replace",std::array{"direct-unprotected","generic-undo","direct-undo","direct-retry-1-in-8"}[arm],trial,writes,ns/writes,
            (unsigned long long)total.copied,(unsigned long long)total.allocated,(unsigned long long)total.objects,(unsigned long long)total.journal_records,(unsigned long long)total.journal_record_bytes,(unsigned long long)total.journal_heap_bytes,(unsigned long long)failures,(unsigned long long)discarded,
            (unsigned long long)total.model_ns,(unsigned long long)total.segment_capture_ns,(unsigned long long)total.directory_ns,(unsigned long long)total.prepare_ns,(unsigned long long)total.install_ns);
    }
}
int main(int argc,char** argv){if(argc>1 && std::string_view(argv[1])=="--check"){Runtime rt;check(rt);return 0;}measure(argc>1?std::stoul(argv[1]):8192,argc>2?std::stoul(argv[2]):3);}

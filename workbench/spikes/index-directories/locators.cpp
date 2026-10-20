#include "index.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <random>
#include <string_view>
using namespace directory_spike;

struct Memory : std::pmr::memory_resource {
    std::size_t live{},peak{};
    void* do_allocate(std::size_t n,std::size_t a)override {
        auto* p=std::pmr::new_delete_resource()->allocate(n,a);live+=n;peak=std::max(peak,live);return p;
    }
    void do_deallocate(void* p,std::size_t n,std::size_t a)override {require(live>=n);live-=n;std::pmr::new_delete_resource()->deallocate(p,n,a);}
    bool do_is_equal(const std::pmr::memory_resource& r)const noexcept override{return this==&r;}
    ~Memory(){require(!live);}
};
std::vector<Row> fixture(unsigned shape,unsigned n) {
    if(shape!=2)return dataset(n,0,129,shape?65537:1);
    std::vector<Row> rows;for(unsigned i=0;i<n;++i)rows.push_back({i+1,u64(i)<<32,u64(i)<<48,i64(i%701)-350,true});return rows;
}
unsigned segment_prefix(const Row& r,unsigned layer){return layer?r.physical>>(48-layer*8):0;}
u64 digest(Outcome o){return o.count+u64(o.sum)+o.auxiliary;}
template<class F>double timed(F f){const auto start=std::chrono::steady_clock::now();f();return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();}

void check(Runtime& rt) {
    for(unsigned shape:{0,1,2})for(unsigned grain:{64,256})for(unsigned extras:{0,4})for(unsigned mode:{0,1,2}) {
        const bool indirect=mode!=0;
        Memory memory;orbital::Part<Control>* escaped{};std::vector<std::byte> saved;
        {
            Index index(rt,memory,shape==2?2:0,64,6,extras,3,grain,true,false,32,true,true,0,mode);
            auto rows=fixture(shape,769),next=rows;index.edit(rows);
            {
                Store::Scope old(index.store);index.verify(old,rows,true);
                const auto id=segment_id(4,segment_prefix(rows[0],4));const auto selected=old.bind(id);
                if(indirect) {
                    const unsigned left_layer=shape==2?2:0;
                    const auto left=old.bind(segment_id(left_layer,0)),right=old.bind(segment_id(left_layer+1,0));
                    require(left.part_count()==right.part_count());
                    auto wrong=left.control();wrong.locators=right.control().locators;
                    std::vector<orbital::PartSource> sources;
                    for(unsigned p=0;p<left.part_count();++p)sources.push_back(take(orbital::source(left,p)));
                    const auto refused=index.store.capture(wrong,sources);
                    require(!refused && refused.error()==orbital::Error::invalid_argument);
                    Image image;for(unsigned p=0;p<left.part_count();++p)image[p].assign(bytes(left,p).begin(),bytes(left,p).end());
                    CopyStats cloned;auto copied=capture(rt,index.store,left.control(),image,right,true,cloned);
                    const auto copy=Physical::selection(copied.get());
                    require(cloned.locator_created_bytes && copy.control().locators!=right.control().locators);
                    require(copy.control().locators->object==segment_id(left_layer,0));
                }
                const auto body=bytes(selected,3);saved.assign(body.begin(),body.end());
                escaped=take(take(index.store.bound(old,id)).export_part(3,orbital::PartRetention::selected_copy));
                const auto pending=index.prepare(old,4,rows[0].logical);const auto expected=digest(finish_read(pending));
                const auto old_root=old.bind(directory_id(4)).realization;
                ++next[0].value;next[0].valid=true;auto stats=index.edit(std::span(&next[0],1));
                require(stats.objects>=5);
                if(indirect)require(!stats.dirent_bytes && !stats.hash_created_bytes && !stats.locator_created_bytes && stats.objects==5);
                ++next[700].value;stats=index.edit(std::span(&next[700],1),{},false);
                if(indirect)require(!stats.dirent_bytes && !stats.hash_created_bytes && !stats.locator_created_bytes && stats.objects==5);
                {
                    Store::Scope fresh(index.store);index.verify(fresh,next,true);
                    const auto current=fresh.bind(id);
                    require(take(current.part(3)).address()!=take(selected.part(3)).address());
                    if(indirect) {
                        require(fresh.bind(directory_id(4)).realization==old_root);
                        require(fresh.cut()>old.cut() && take(index.store.bound(fresh,id)).binding().version!=take(index.store.bound(old,id)).binding().version);
                        require(current.control().revision==selected.control().revision);
                        require(part_locator(selected,3)==part_locator(current,3));
                        require(take(resolve_locator(selected,part_locator(selected,3),3))!=take(resolve_locator(current,part_locator(current,3),3)));
                        require(!resolve_locator(current,part_locator(current,2),3));
                        const auto parent=fresh.bind(segment_id(3,segment_prefix(rows[0],3)));
                        require(!resolve_locator(current,part_locator(parent,3),3));
                    }
                }
                require(digest(finish_read(pending))==expected);index.verify(old,rows);
                // A key move and schema extension exercise fresh directory
                // interpretation while the earlier cells/bytes remain usable.
                ++next.back().physical;index.edit(std::span(&next.back(),1));index.extend_schema(100000);
                {Store::Scope fresh(index.store);index.verify(fresh,next,true);}
                std::vector<u64> removed;for(unsigned i=256;i<512;++i)removed.push_back(next[i].id);
                index.edit({},removed);next.erase(next.begin()+256,next.begin()+512);
                {Store::Scope fresh(index.store);index.verify(fresh,next,true);}
                index.verify(old,rows);
                std::vector<u64> all;for(auto r:next)all.push_back(r.id);
                index.edit({},all);{Store::Scope empty(index.store);index.verify(empty,{});}
                index.edit(rows);{
                    Store::Scope fresh(index.store);index.verify(fresh,rows,true);
                    if(indirect) {
                        const auto recreated=fresh.bind(id);
                        require(part_locator(recreated,3)!=part_locator(selected,3));
                        require(!resolve_locator(recreated,part_locator(selected,3),3));
                    }
                }
                index.verify(old,rows);
            }
            index.store.drain();
        }
        require(memory.live);require(std::ranges::equal(saved,std::span(orbital::view(escaped).address(),saved.size())));
        const auto proof=part_control(escaped);require(proof.valid(memory));
        require(proof.id(0)==orbital::PartId{unsigned(Kind::segment)+1,3} && take(proof.extent(0))==saved.size());
        if(indirect) {
            require(proof.locators->object==segment_id(4,0));
            const auto cell=proof.locators->reference(proof.first);
            require(cell.object==segment_id(4,0) && cell.ordinal==3 && proof.locators->compact==(mode==2));
        }
        orbital::release(escaped);require(!memory.live);
    }
    std::puts("locator oracle: raw/indirect memory locators, inline/overflow Parts, unchanged directories across value/copy edits, wrong-cell rejection, key/schema changes, removal/recreation, late old readers, pending reads and escaping Parts passed");
}
void measure(Runtime& rt,unsigned operations,unsigned reps) {
    std::puts("phase,rows,J,auxiliary_parts,summary_grain,trace,layer,arm,trial,operations,ns_per_operation,copied_bytes,dirent_bytes,hash_created_bytes,changed_objects,locator_created_bytes,live_locator_bytes,metadata_live_bytes,metadata_peak_bytes,model_ns,segment_image_ns,segment_capture_ns,directory_ns,prepare_ns,install_ns,checksum");
    for(unsigned shape:{0,1,2})for(unsigned grain:{64,256})for(unsigned trial=0;trial<reps;++trial) {
        std::array<u64,3> checksums{};
        for(unsigned offset=0;offset<3;++offset) {
        const unsigned mode=(trial+offset)%3;const bool indirect=mode!=0;const char* arm=std::array{"raw-address","indirect-full-cell","indirect-shared-object"}[mode];
        const char* trace=std::array{"dense","sparse","many-prefix"}[shape];constexpr unsigned n=4096,extras=4,writes=64;
        Memory memory;Index index(rt,memory,shape==2?2:0,8,6,extras,3,grain,true,false,32,true,true,0,mode);
        auto rows=fixture(shape,n);index.edit(rows);std::mt19937_64 rng(5517);std::vector<u64> queries(operations);
        for(auto& q:queries)q=rows[rng()%n].logical;
        u64 checksum=0;double ns=timed([&]{for(auto q:queries){Store::Scope scope(index.store);checksum+=digest(index.seek(scope,4,q,Search::boundlist,extras));}});
        asm volatile("" : "+r"(checksum) : : "memory");checksums[mode]=checksum;Store::Scope old(index.store);const auto cells=index.locator_bytes(old);
        std::printf("locator-read,%u,%u,%u,%u,%s,4,%s,%u,%u,%.3f,0,0,0,0,0,%llu,%zu,%zu,0,0,0,0,0,0,%llu\n",n,shape==2?2:0,extras,grain,trace,arm,trial,operations,ns/operations,(unsigned long long)cells,memory.live,memory.peak,(unsigned long long)checksum);
        const auto original=rows;EditStats total;
        ns=timed([&]{for(unsigned i=0;i<writes;++i){auto& row=rows[(i*251)%n];++row.value;row.valid=true;const auto s=index.edit(std::span(&row,1));
            total.copied+=s.copied;total.dirent_bytes+=s.dirent_bytes;total.hash_created_bytes+=s.hash_created_bytes;total.objects+=s.objects;total.locator_created_bytes+=s.locator_created_bytes;
            total.model_ns+=s.model_ns;total.segment_image_ns+=s.segment_image_ns;total.segment_capture_ns+=s.segment_capture_ns;total.directory_ns+=s.directory_ns;total.prepare_ns+=s.prepare_ns;total.install_ns+=s.install_ns;}});
        if(indirect)require(!total.dirent_bytes && !total.hash_created_bytes && !total.locator_created_bytes && total.objects==writes*5);
        std::printf("locator-edit,%u,%u,%u,%u,%s,all,%s,%u,%u,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%zu,%zu,%llu,%llu,%llu,%llu,%llu,%llu,0\n",n,shape==2?2:0,extras,grain,trace,arm,trial,writes,ns/writes,
            (unsigned long long)total.copied,(unsigned long long)total.dirent_bytes,(unsigned long long)total.hash_created_bytes,(unsigned long long)total.objects,(unsigned long long)total.locator_created_bytes,(unsigned long long)cells,memory.live,memory.peak,
            (unsigned long long)total.model_ns,(unsigned long long)total.segment_image_ns,(unsigned long long)total.segment_capture_ns,(unsigned long long)total.directory_ns,(unsigned long long)total.prepare_ns,(unsigned long long)total.install_ns);
        {Store::Scope fresh(index.store);index.verify(fresh,rows);}index.verify(old,original);
        }
        require(checksums[0]==checksums[1] && checksums[1]==checksums[2]);
    }
}
int main(int argc,char** argv) {
    Runtime rt;if(argc>1 && std::string_view(argv[1])=="--check"){check(rt);return 0;}
    measure(rt,argc>1?std::stoul(argv[1]):8192,argc>2?std::stoul(argv[2]):3);
}

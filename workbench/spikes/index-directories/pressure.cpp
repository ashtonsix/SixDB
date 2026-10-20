#include "index.h"
#include "hash.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <string_view>
using namespace directory_spike;
template<class F> double timed(F fn){auto t=std::chrono::steady_clock::now();fn();return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t).count();}
std::vector<Row> tenants(unsigned n) {
    std::vector<Row> rows;rows.reserve(n);
    for(unsigned i=0;i<n;++i)rows.push_back({i+1,u64(i)<<32,u64(i)<<48,i64(i%701)-350,true});
    return rows;
}
void check(Runtime& rt) {
    for(unsigned bits:{0,4,6,8})for(unsigned extras:{0,4})for(unsigned grain:{0,256})for(bool packed:{false,true}) {
        if(packed && !bits)continue;
        Index index(rt,*std::pmr::new_delete_resource(),0,64,bits,extras,3,grain,false,false,0,packed);
        auto rows=dataset(1025,0,700,65537);index.edit(rows);
        Store::Scope old(index.store);index.verify(old,rows,true);
        for(unsigned source=0;source<4;++source)for(auto parent:index.range(old,source,0,UINT64_MAX)) {
            for(unsigned destination=source+1;destination<=std::min(4u,source+2);++destination) {
                const auto children=index.descend(old,source,parent,destination);u64 count=0,not_null=0;i64 sum=0;
                for(auto child:children){count+=child.count;not_null+=child.not_null;sum+=child.sum;}
                require(count==parent.count && not_null==parent.not_null && sum==parent.sum);
            }
        }
        for(bool lower:{false,true})for(bool upper:{false,true}) {
            const auto exact=index.range(old,4,0,0,Search::boundlist,lower,upper);
            require(exact.size()==(lower && upper ? 700u:0u));
        }
        // A duplicate run crosses hundreds of physical segments and several
        // directory pages. A seek must retain its first possible occurrence.
        require(index.seek(old,4,0).prefix==0);
        auto next=rows;next[17].value=-7000;index.edit(std::span(&next[17],1));
        next[900].value=-8000;index.edit(std::span(&next[900],1));
        std::vector<u64> removed;for(unsigned i=256;i<512;++i)removed.push_back(rows[i].id);
        index.edit({},removed);next.erase(next.begin()+256,next.begin()+512);
        {Store::Scope fresh(index.store);index.verify(fresh,next);}
        index.verify(old,rows);
        index.edit(std::span(rows).subspan(256,256));
        {Store::Scope fresh(index.store);index.verify(fresh,index.rows());}
    }
    for(bool compact:{false,true})for(unsigned grain:{64,256}) {
        // Populate every L1 prefix, then update different table chunks. The
        // sparse fixture above only populated prefix zero at that layer.
        Index index(rt,*std::pmr::new_delete_resource(),0,8,5,4,3,grain,compact,true,32);
        auto rows=dataset(1024,0,1,u64(1)<<38);index.edit(rows);
        Store::Scope old(index.store);index.verify(old,rows,true);
        auto next=rows;
        for(unsigned i:{0,128,511,1023}){next[i].value=-9000-i64(i);index.edit(std::span(&next[i],1));}
        {Store::Scope fresh(index.store);index.verify(fresh,next,true);}
        std::vector<u64> removed;for(unsigned i=128;i<256;++i)removed.push_back(rows[i].id);
        index.edit({},removed);next.erase(next.begin()+128,next.begin()+256);
        {Store::Scope fresh(index.store);index.verify(fresh,next,true);}
        index.verify(old,rows);
        index.edit(std::span(rows).subspan(128,128));
        {Store::Scope fresh(index.store);index.verify(fresh,index.rows());}
    }
    for(unsigned shape:{0,1,2,3,4}) {
        const auto bits=shape?6u:0u;const bool packed=shape>=2,compact=shape==3;
        Index index(rt,*std::pmr::new_delete_resource(),2,8,bits,0,3,256,true,false,32,packed,compact || shape==4,shape==4?32:0);
        // The chunked-root treatment crosses three row chunks, then shrinks
        // across a Part-count boundary while its earlier root remains live.
        auto rows=tenants(shape==4?4097:1025);index.edit(rows);Store::Scope old(index.store);index.verify(old,rows,true);
        auto next=rows;for(unsigned i:{0u,255u,unsigned(rows.size()/2),unsigned(rows.size()-1)}){++next[i].value;index.edit(std::span(&next[i],1));}
        {Store::Scope fresh(index.store);index.verify(fresh,next,true);}
        const auto erased=next[512].id;index.edit({},std::span(&erased,1));next.erase(next.begin()+512);
        {Store::Scope fresh(index.store);index.verify(fresh,next,true);require(index.seek(fresh,2,rows[512].logical).prefix==513);}
        index.edit(std::span(&rows[512],1));index.verify(old,rows);
        {Store::Scope fresh(index.store);index.verify(fresh,index.rows());}
    }
    {
        Index full(rt,*std::pmr::new_delete_resource(),0);auto rows=dataset(65536,0,1,1);full.edit(rows);
        Store::Scope scope(full.store);full.verify(scope,rows);
        require(full.seek(scope,0,0).count==65536);
        auto s=scope.bind(segment_id(4,0));require(s.control().count==65536 && values<std::uint16_t>(s,0).back()==65535);
    }
    {
        Index history(rt,*std::pmr::new_delete_resource(),2,32,6,4);auto rows=dataset(257,2,17);history.edit(rows);
        Store::Scope old(history.store);history.extend_schema(100000);
        ++rows[17].value;history.edit(std::span(&rows[17],1));
        {Store::Scope fresh(history.store);history.verify(fresh,rows);require(fresh.bind(schema_id()).control().count==100000);}
        --rows[17].value;history.verify(old,rows);require(old.bind(schema_id()).control().count==3);
        // Rebuild into another live allocation set; compare semantic contents
        // while both address spaces remain protected. No pointer-byte reopen.
        Index relocated(rt,*std::pmr::new_delete_resource(),2,32,6,4,100000);relocated.edit(rows);
        Store::Scope reopened(relocated.store);relocated.verify(reopened,rows);
        require(bytes(reopened.bind(segment_id(0,0)),3).data()!=bytes(old.bind(segment_id(0,0)),3).data());
    }
    std::puts("pressure oracle: flat/paged/branched directories, compact and chunked dense tables, duplicate run across pages, disappearance/recreation, StreamPack locator overflow/remaps with old readers, full 65,536-position segment/count passed");
}
void measure(Runtime& rt,unsigned operations,unsigned reps) {
    std::puts("phase,rows,J,auxiliary_parts,summary_grain,layer,trace,arm,trial,operations,ns_per_operation,copied_bytes,shared_bytes,dirent_bytes,allocated_bytes,changed_objects,model_ns,segment_image_ns,segment_capture_ns,directory_ns,prepare_ns,install_ns,small_requests,large_requests,realization_ns,hash_created_bytes");
    struct Variant {const char* name;unsigned bits,grain;bool compact,branches;unsigned table_grain;};
    const std::array variants{
        Variant{"flat",0,0,false,false,0},Variant{"pages-16",4,0,false,false,0},Variant{"pages-64",6,0,false,false,0},
        Variant{"pages-64-chunks-256",6,256,false,false,0},Variant{"pages-64-chunks-64",6,64,false,false,0},
        Variant{"branches-32-chunks-256",5,256,true,true,32},Variant{"branches-32-chunks-64",5,64,true,true,32}};
    for(unsigned n:{256,4096})for(unsigned trial=0;trial<reps;++trial)for(unsigned offset=0;offset<variants.size();++offset) {
        const auto v=variants[(offset+trial)%variants.size()];const auto grain=v.grain;const char* arm=v.name;
        Index index(rt,*std::pmr::new_delete_resource(),0,8,v.bits,0,3,grain,v.compact,v.branches,v.table_grain);auto rows=dataset(n,0,1,65537);index.edit(rows);
        std::mt19937_64 rng(9901);std::vector<u64> queries(operations);for(auto& q:queries)q=rows[rng()%n].logical;
        double ns=0;
        for(unsigned layer:{2,4}) {
        u64 checksum=0;ns=timed([&]{for(auto q:queries){Store::Scope scope(index.store);auto o=index.seek(scope,layer,q);checksum+=o.count+u64(o.sum);}});
        asm volatile("" : "+r"(checksum) : : "memory");
        std::printf("directory-pressure,%u,0,0,%u,%u,uniform-hit,%s,%u,%u,%.3f,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n",n,grain,layer,arm,trial,operations,ns/operations);
        }
        Store::Scope old(index.store);EditStats total;constexpr unsigned writes=16;
        ns=timed([&]{for(unsigned i=0;i<writes;++i){++rows[i].value;const auto s=index.edit(std::span(&rows[i],1));total.copied+=s.copied;total.shared+=s.shared;total.dirent_bytes+=s.dirent_bytes;total.allocated+=s.allocated;total.objects+=s.objects;total.small_requests+=s.small_requests;total.large_requests+=s.large_requests;total.realization_ns+=s.realization_ns;total.hash_created_bytes+=s.hash_created_bytes;
            total.model_ns+=s.model_ns;total.segment_image_ns+=s.segment_image_ns;total.segment_capture_ns+=s.segment_capture_ns;total.directory_ns+=s.directory_ns;total.prepare_ns+=s.prepare_ns;total.install_ns+=s.install_ns;}});
        std::printf("directory-pressure,%u,0,0,%u,all,value-replace,%s,%u,%u,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",n,grain,arm,trial,writes,ns/writes,
            (unsigned long long)total.copied,(unsigned long long)total.shared,(unsigned long long)total.dirent_bytes,(unsigned long long)total.allocated,(unsigned long long)total.objects,
            (unsigned long long)total.model_ns,(unsigned long long)total.segment_image_ns,(unsigned long long)total.segment_capture_ns,(unsigned long long)total.directory_ns,(unsigned long long)total.prepare_ns,(unsigned long long)total.install_ns,(unsigned long long)total.small_requests,(unsigned long long)total.large_requests,(unsigned long long)total.realization_ns,(unsigned long long)total.hash_created_bytes);
        {Store::Scope fresh(index.store);index.verify(fresh,rows);}
    }
    for(unsigned trial=0;trial<reps;++trial)for(unsigned extras:{0,4}) {
        Index index(rt,*std::pmr::new_delete_resource(),0,8,6,extras);const auto rows=dataset(1024,0,17,65537);index.edit(rows);
        u64 checksum=0;auto ns=timed([&]{for(unsigned q=0;q<operations;++q) {Store::Scope scope(index.store);const auto o=index.seek(scope,4,rows[(q*31)%rows.size()].logical,Search::boundlist,extras);checksum+=o.count+u64(o.sum)+o.auxiliary;}});
        asm volatile("" : "+r"(checksum) : : "memory");
        std::printf("directory-pressure,1024,0,%u,0,4,overflow-projection,pages-64,%u,%u,%.3f,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n",extras,trial,operations,ns/operations);
    }
    // One populated physical/logical two-byte prefix per record. The former
    // J=2 fixture had just sixteen such prefixes, hiding whole-hash rebuilds.
    for(unsigned n:{256,4096})for(unsigned trial=0;trial<reps;++trial)for(unsigned offset=0;offset<5;++offset) {
        const auto shape=(offset+trial)%5,bits=shape?6u:0u;const bool packed=shape>=2,compact=shape>=3;
        const char* arm=std::array{"whole-hash","paged-hash-64","packed-pages64-hash-b8","packed-pages64-hash-b2","packed-pages64-hash-b2-root32"}[shape];
        Index index(rt,*std::pmr::new_delete_resource(),2,8,bits,0,3,256,true,false,32,packed,compact,shape==4?32:0);auto rows=tenants(n);index.edit(rows);
        std::mt19937_64 rng(9911);std::vector<u64> queries(operations);for(auto& q:queries)q=rows[rng()%n].logical;
        u64 checksum=0;double ns=timed([&]{for(auto q:queries){Store::Scope scope(index.store);auto o=index.seek(scope,2,q);checksum+=o.count+u64(o.sum);}});
        asm volatile("" : "+r"(checksum) : : "memory");
        std::printf("hash-pressure,%u,2,0,256,2,uniform-hit,%s,%u,%u,%.3f,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n",n,arm,trial,operations,ns/operations);
        // Attribute structured-ID resolution separately from the Ikea hash.
        // Bind the latter once under a stationary Scope, then consume its
        // actual summary pointer. This control bypasses no cost in uniform-hit.
        {
            Store::Scope protected_scope(index.store);std::vector<Selection> directories;
            if(bits)for(unsigned p=0;p<(n+63)/64;++p)directories.push_back(protected_scope.bind(page_id(2,packed?p*64:p)));
            else directories.push_back(protected_scope.bind(directory_id(2)));
            ns=timed([&]{for(auto q:queries){const unsigned p=q>>48;Dirent d;
                require(find_hash(directories[bits?p>>6:0].control().hash,p,d));
                checksum+=reinterpret_cast<const Summary*>(d.parts[3])->count;}});
            asm volatile("" : "+r"(checksum) : : "memory");
            std::printf("hash-pressure,%u,2,0,256,2,prebound-hash-consume,%s,%u,%u,%.3f,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n",n,arm,trial,operations,ns/operations);
        }
        ns=timed([&]{for(auto q:queries){Store::Scope scope(index.store);const auto selected=scope.bind(segment_id(4,unsigned(q>>32)));require(bool(selected));checksum+=values<Summary>(selected,3).front().count;}});
        asm volatile("" : "+r"(checksum) : : "memory");
        std::printf("hash-pressure,%u,2,0,256,4,protected-object-bind,%s,%u,%u,%.3f,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n",n,arm,trial,operations,ns/operations);
        Store::Scope old(index.store);EditStats total;constexpr unsigned writes=16;
        ns=timed([&]{for(unsigned i=0;i<writes;++i){auto& row=rows[(i*251)%n];++row.value;const auto s=index.edit(std::span(&row,1));
            total.copied+=s.copied;total.shared+=s.shared;total.dirent_bytes+=s.dirent_bytes;total.allocated+=s.allocated;total.objects+=s.objects;total.small_requests+=s.small_requests;total.large_requests+=s.large_requests;total.realization_ns+=s.realization_ns;total.hash_created_bytes+=s.hash_created_bytes;
            total.model_ns+=s.model_ns;total.segment_image_ns+=s.segment_image_ns;total.segment_capture_ns+=s.segment_capture_ns;total.directory_ns+=s.directory_ns;total.prepare_ns+=s.prepare_ns;total.install_ns+=s.install_ns;}});
        std::printf("hash-pressure,%u,2,0,256,all,value-strided,%s,%u,%u,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",n,arm,trial,writes,ns/writes,
            (unsigned long long)total.copied,(unsigned long long)total.shared,(unsigned long long)total.dirent_bytes,(unsigned long long)total.allocated,(unsigned long long)total.objects,
            (unsigned long long)total.model_ns,(unsigned long long)total.segment_image_ns,(unsigned long long)total.segment_capture_ns,(unsigned long long)total.directory_ns,(unsigned long long)total.prepare_ns,(unsigned long long)total.install_ns,(unsigned long long)total.small_requests,(unsigned long long)total.large_requests,(unsigned long long)total.realization_ns,(unsigned long long)total.hash_created_bytes);
        {Store::Scope fresh(index.store);index.verify(fresh,rows);}
    }
}
int main(int argc,char** argv) {
    Runtime rt;if(argc>1 && std::string_view(argv[1])=="--check"){check(rt);return 0;}
    measure(rt,std::min(32768ul,argc>1?std::stoul(argv[1]):8192ul),argc>2?std::stoul(argv[2]):3);
}

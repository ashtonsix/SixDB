#include "index.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <string_view>
using namespace directory_spike;
template<class F>double timed(F f){const auto t=std::chrono::steady_clock::now();f();return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t).count();}
unsigned physical_prefix(u64 p,unsigned layer){return layer?p>>(48-layer*8):0;}
std::vector<Definition> additions(unsigned n,unsigned width) {
    std::vector<Definition> defs;
    for(unsigned l=0;l<5;++l) {
        const auto id=n-19+l*4;
        defs.push_back({id,width,1u<<l,0,64,4});
        defs.push_back({id+1,width,1u<<l,1,64,4});
        defs.push_back({id+2,width,1u<<l,1,256,1});
        defs.push_back({id+3,width,1u<<l,0,0,0});
    }
    return defs;
}
u64 schema_bytes(Selection schema){u64 size=0;for(unsigned p=0;p<schema.part_count();++p)size+=bytes(schema,p).size();return size;}
void check(Runtime& rt) {
    for(unsigned j:{0,2})for(unsigned mode:{0,2})for(unsigned bits:{0,6})for(bool transcode:{false,true}) {
        Index index(rt,*std::pmr::new_delete_resource(),j,64,bits,4,3,64,true,false,32,bits!=0,true,0,mode);
        auto rows=dataset(1025,j,129,257);
        index.edit(rows);Store::Scope old(index.store);index.verify(old,rows,true);
        const auto pending=index.prepare(old,4,rows[0].logical);const auto answer=finish_read(pending);
        const auto defs=additions(100000,64);index.extend_schema(100000,defs);
        std::vector<Migration> targets;
        for(unsigned l=0;l<5;++l)targets.push_back({{l,physical_prefix(rows[0].physical,l)},defs[l*4+2].id});
        {
            Store::Scope extended(index.store);const auto schema=extended.bind(schema_id());
            require(schema_bytes(schema)>2400000 && values<Definition>(schema,0).size()==100000);
            require(!schema_definition(old.bind(schema_id()),defs.back().id,4));
            require(!schema_definition(schema,0,0) && !schema_definition(schema,100001,4) && !schema_definition(schema,1,5));
            require(!schema_definition(schema,defs[0].id,1));
            const auto invalid=std::array{targets[4],Migration{{0,0},defs[4].id}};
            require(!index.migrate(invalid));
            const auto duplicate=std::array{targets[4],targets[4]};require(!index.migrate(duplicate));
            const std::array<Migration,1> absent{{{{4,UINT32_MAX},1}}};require(!index.migrate(absent));
            Store::Scope unchanged(index.store);require(unchanged.cut()==extended.cut());index.verify(unchanged,rows);
        }
        const auto stats=take(index.migrate(targets,transcode));require(stats.objects>=5 && !stats.new_segments && !stats.removed_segments);
        Store::Scope middle(index.store);index.verify(middle,rows,true);
        for(unsigned l=0;l<5;++l) {
            const auto id=segment_id(l,targets[l].segment.second);const auto s=middle.bind(id);
            require(s.control().representation==targets[l].representation && s.control().summary_chunk==256);
            require(s.part_count()==5+(s.control().count+255)/256+1);
            for(const auto& row:rows) {
                const auto other=segment_id(l,physical_prefix(row.physical,l));
                if(other!=id)require(middle.bind(other).realization==old.bind(other).realization);
            }
        }
        require(middle.bind(schema_id()).control().count==100000 && old.bind(schema_id()).control().count==3);
        const auto no_op=take(index.migrate(targets));require(!no_op.objects);
        {Store::Scope same(index.store);require(same.cut()==middle.cut());}
        auto next=rows;next[0].value=-90000;next[0].valid=true;index.edit(std::span(&next[0],1));
        {Store::Scope fresh(index.store);index.verify(fresh,next,true);}
        for(unsigned l=0;l<5;++l)targets[l].representation=defs[l*4+3].id;
        take(index.migrate(targets,transcode));
        {Store::Scope fresh(index.store);index.verify(fresh,next,true);for(auto m:targets)require(fresh.bind(segment_id(m.segment.first,m.segment.second)).part_count()==6);}
        require(finish_read(pending).sum==answer.sum);index.verify(old,rows);index.verify(middle,rows);
        // Return to historical definitions while retained readers still use
        // each newer recipe. These IDs retain their original meaning.
        for(auto& m:targets)m.representation=1;take(index.migrate(targets,transcode));
        {Store::Scope fresh(index.store);index.verify(fresh,next,true);}
        index.verify(middle,rows);
    }
    for(unsigned mode:{0,2}) {
        // Overflow can appear for the first time after schema extension, then
        // disappear completely when the last extra-Part segment migrates back.
        Index index(rt,*std::pmr::new_delete_resource(),0,8,6,0,3,0,true,false,32,true,true,0,mode);
        const auto rows=dataset(257,0,17,1);index.edit(rows);Store::Scope old(index.store);
        const std::array<Definition,1> defs{{{4,8,16,1,64,4}}};index.extend_schema(4,defs);
        const std::array<Migration,1> up{{{{4,0},4}}};take(index.migrate(up));
        Store::Scope expanded(index.store);index.verify(expanded,rows,true);require(bool(expanded.bind(overflow_id(4,0))));
        const std::array<Migration,1> down{{{{4,0},1}}};take(index.migrate(down));
        {Store::Scope fresh(index.store);index.verify(fresh,rows,true);require(!fresh.bind(overflow_id(4,0)));}
        index.verify(old,rows);index.verify(expanded,rows);
    }
    std::puts("schema oracle: explicit per-layer migration, unchanged unrelated segments, codec and Part-count transitions, overflow appearance/removal, 100000 definitions, invalid-batch refusal, no-op, value edits, historical return and retained old interpretations passed");
}
void measure(Runtime& rt,unsigned operations,unsigned reps) {
    std::puts("phase,rows,J,key_bytes,schema_definitions,layer,trace,arm,trial,operations,ns_per_operation,schema_bytes,old_schema_bytes,copied_bytes,allocated_bytes,shared_bytes,changed_objects,dirent_bytes,locator_created_bytes,model_ns,segment_image_ns,segment_capture_ns,directory_ns,prepare_ns,install_ns,checksum");
    for(unsigned j:{0,2})for(unsigned n:{32,256,4096,100000})for(unsigned trial=0;trial<reps;++trial)for(unsigned offset=0;offset<4;++offset) {
        const unsigned which=(trial+offset)%4,mode=which/2*2;const bool transcode=which%2;
        const auto arm=std::array{"raw-model-image","raw-source-transcode","compact-model-image","compact-source-transcode"}[which];
        Index index(rt,*std::pmr::new_delete_resource(),j,8,6,4,3,64,true,false,32,true,true,0,mode);
        const auto rows=dataset(4096,j,129,257);index.edit(rows);Store::Scope old(index.store);
        const auto defs=additions(n,8);
        EditStats extension_stats;const double extension=timed([&]{static_cast<CopyStats&>(extension_stats)=index.extend_schema(n,defs);});
        Store::Scope current(index.store);const auto schema=current.bind(schema_id());const auto size=schema_bytes(schema),old_size=schema_bytes(old.bind(schema_id()));
        auto emit=[&](const char* phase,const char* trace,unsigned ops,double ns,const EditStats& s,u64 checksum=0){
            std::printf("%s,4096,%u,8,%u,4,%s,%s,%u,%u,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",phase,j,n,trace,arm,trial,ops,ns/ops,
                (unsigned long long)size,(unsigned long long)old_size,(unsigned long long)s.copied,(unsigned long long)s.allocated,(unsigned long long)s.shared,(unsigned long long)s.objects,(unsigned long long)s.dirent_bytes,(unsigned long long)s.locator_created_bytes,
                (unsigned long long)s.model_ns,(unsigned long long)s.segment_image_ns,(unsigned long long)s.segment_capture_ns,(unsigned long long)s.directory_ns,(unsigned long long)s.prepare_ns,(unsigned long long)s.install_ns,(unsigned long long)checksum);
        };
        emit("schema-extend","append-definitions",1,extension,extension_stats);
        std::mt19937_64 rng(99131);std::vector<unsigned> candidates;
        for(unsigned id=1;id<=n;++id)if(schema_definition(schema,id,4))candidates.push_back(id);
        std::vector<unsigned> ids(operations);for(auto& id:ids)id=candidates[rng()%candidates.size()];
        for(unsigned position=0;position<4;++position) {
            const unsigned which=(position+trial)%4;const bool wide=which/2,protected_schema=which%2;
            u64 checksum=0;const auto ns=timed([&]{for(unsigned i=0;i<operations;++i) {
                const auto id=wide?ids[i]:defs[16+(i%4)].id;
                if(protected_schema){const auto d=take(schema_definition(schema,id,4));checksum+=d.id+d.recipe+d.summary_chunk+d.auxiliary_parts;}
                else {Store::Scope scope(index.store);const auto d=take(schema_definition(scope.bind(schema_id()),id,4));checksum+=d.id+d.recipe+d.summary_chunk+d.auxiliary_parts;}
            }});
            asm volatile("" : "+r"(checksum) : : "memory");
            emit(protected_schema?"schema-protected":"schema-bind",wide?"wide-ids":"hot-tail-ids",operations,ns,{},checksum);
        }
        const auto target=SegmentKey{4,physical_prefix(rows[0].physical,4)};
        for(bool layout:{false,true}) {
            EditStats total;constexpr unsigned edits=32;
            const auto ns=timed([&]{for(unsigned i=0;i<edits;++i) {
                const Migration m{target,defs[16+(layout?2:0)+(i%2)].id};const auto s=take(index.migrate(std::span(&m,1),transcode));
                total.copied+=s.copied;total.allocated+=s.allocated;total.shared+=s.shared;total.objects+=s.objects;total.dirent_bytes+=s.dirent_bytes;total.locator_created_bytes+=s.locator_created_bytes;
                total.model_ns+=s.model_ns;total.segment_image_ns+=s.segment_image_ns;total.segment_capture_ns+=s.segment_capture_ns;total.directory_ns+=s.directory_ns;total.prepare_ns+=s.prepare_ns;total.install_ns+=s.install_ns;
            }});
            emit("schema-migrate",layout?"part-layout":"codec-only",edits,ns,total);
            {Store::Scope fresh(index.store);index.verify(fresh,rows);}
        }
        u64 checksum=0;const auto ns=timed([&]{for(unsigned i=0;i<operations;++i){Store::Scope scope(index.store);const auto o=index.seek(scope,4,rows[(i*251)%rows.size()].logical);checksum+=o.count+u64(o.sum);}});
        asm volatile("" : "+r"(checksum) : : "memory");emit("schema-read","mixed-historic-current",operations,ns,{},checksum);
        index.verify(old,rows);
    }
}
int main(int argc,char** argv){Runtime rt;if(argc>1 && std::string_view(argv[1])=="--check"){check(rt);return 0;}measure(rt,argc>1?std::stoul(argv[1]):8192,argc>2?std::stoul(argv[2]):3);}

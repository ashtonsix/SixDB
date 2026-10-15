#include "worker_owner.h"

namespace ht::epochs {
// Source and destination stay bound to their original Workers/Pools. The only
// cross-owner material is an owned, bounded message chunk of keys/value bytes.
struct fold_result {
    double setup=0,build=0,fold=0,source_retire=0,retire=0;
    word transferred=0,chunks=0,source_work=0,growth_work=0,partial=0;
    word logical=0,backing=0,coexist=0,queue_bytes=0,cursor_bytes=0;
    word pool_metadata=0,pool_mapped=0,arena_charged=0,inactive=0,context=0,inspection=0;
    unsigned source_epoch=0,destination_before=0,destination_after=0,cancellations=0;
};
std::vector<entry> source_input(unsigned n,unsigned passes,unsigned side,unsigned overlap) {
    auto records=input(n,false,passes);
    const unsigned shift=side?n*(100-overlap)/100:0;
    // input() shuffles each pass; invert its bijective multiplier to add a
    // known domain rank offset without borrowing the table's hash function.
    word inverse=1;for(unsigned i=0;i<6;++i)inverse*=2-0xd6e8feb86659fd93ULL*inverse;
    for(auto& e:records) {
        const auto rank=(e.key*inverse)&mask(K);
        e.key=(rank+shift)*0xd6e8feb86659fd93ULL&mask(K);
        for(unsigned j=0;j<W;++j)e.bytes[j]^=byte(side*83+j*7);
    }
    return records;
}
void normalize(expected_type& values){for(auto& [k,v]:values)std::sort(v.begin(),v.end());}

template<class Table> fold_result fold_trial(unsigned n,unsigned passes,unsigned overlap,bool checking,
        const std::array<int,2>& affinity) {
    using Team=team<Table,true>;
    const std::array<std::vector<entry>,2> records{source_input(n,passes,0,overlap),source_input(n,passes,1,overlap)};
    expected_type expected,source_expected;
    for(const auto& r:records)for(const auto& e:r)accumulate(expected,e);
    for(const auto& e:records[1])accumulate(source_expected,e);
    normalize(expected);normalize(source_expected);
    std::array<std::vector<entry>,2> inspection;
    for(auto& v:inspection)v.reserve(2*records[0].size());
    fold_result out;
    const auto start=clock_type::now();Team workers(affinity,&inspection);const auto created=clock_type::now();
    // The established intake protocol, including paired growth, remains in
    // force until both owners are frozen for consolidation.
    std::array<unsigned,2> at{};
    while(at[0]<records[0].size()||at[1]<records[1].size()||workers.retry_count[0]||workers.retry_count[1]) {
        if(workers.growth_requested){workers.grow(checking);continue;}
        bool growth=false;
        if constexpr(Table::native)for(unsigned i=0;i<2;++i)
            growth|=at[i]<records[i].size()&&candidate::should_grow(workers.epoch,workers.population[i],batch_size);
        if(growth){require(workers.epoch<candidate::max_epoch,"bounded build levels");workers.grow(checking);continue;}
        for(unsigned i=0;i<2;++i) {
            if(workers.retry_count[i]){require(workers.replay(i),"build retained suffix");continue;}
            const auto count=std::min<std::size_t>(batch_size,records[i].size()-at[i]);
            if(count){require(workers.offer(i,operation::mutate,{records[i].data()+at[i],count}),"bounded build");at[i]+=count;}
        }
        workers.drain();for(unsigned i=0;i<2;++i)workers.harvest(i);
    }
    workers.drain();for(unsigned i=0;i<2;++i)workers.harvest(i);
    const auto built=clock_type::now();
    require(workers.population[0]==n&&workers.population[1]==n,"both source populations");
    out.destination_before=workers.owner_epoch[0];out.source_epoch=workers.owner_epoch[1];
    const auto before_operations=workers.operations;
    const auto folding=clock_type::now();
    require(workers.offer(1,operation::scan_begin),"retain source cursor");workers.drain();
    for(;;) {
        require(workers.offer(1,operation::scan_chunk,{},false,checking?3:0),"bounded source chunk");workers.drain();
        auto& source=workers.owners[1].batch;
        const auto count=source.done;const bool source_done=source.ready;
        out.source_work+=source.work;out.chunks+=count!=0;
        require(count<=batch_size&&(count||source_done),"source chunk progress");
        unsigned accepted=0;
        while(accepted<count) {
            if constexpr(Table::native)if(candidate::should_grow(workers.owner_epoch[0],workers.population[0],count-accepted)) {
                require(workers.owner_epoch[0]<candidate::max_epoch,"bounded fold destination levels");
                workers.grow_destination(0,checking);
            }
            require(workers.offer(0,operation::fold,{source.input.data()+accepted,count-accepted},false,checking?5:0),"owned source bytes to destination");
            workers.drain();auto& target=workers.owners[0].batch;
            accepted+=target.done;out.transferred+=target.done;
            workers.population[0]+=target.key_delta;
            require(workers.population[0]==target.keys,"fold occupancy delta");
            if(accepted<count) {
                require(Table::native&&target.needs_growth&&workers.owner_epoch[0]==0,"only absent-key compact refusal");
                ++out.partial;workers.grow_destination(0,checking);
            }
            require(workers.owner_epoch[1]==out.source_epoch,"destination growth preserves source interpretation");
        }
        if(source_done)break;
    }
    const auto folded=clock_type::now();
    require(workers.operations==before_operations,"merge accounting is separate from intake");
    require(out.transferred==(Kind==container_kind::multimap?word(n)*passes:n),"source input consumed exactly once");
    require(workers.population[0]==expected.size(),"result union cardinality, not a sizing input");
    out.destination_after=workers.owner_epoch[0];out.cancellations=workers.cancellations;out.growth_work=workers.scan_work;
    // Inventory and byte checks do not allocate between measured phases. Output
    // capacity was admitted before owner construction and stays alive to close.
    for(unsigned i=0;i<2;++i)require(workers.offer(i,operation::inspect),"retained-source/final-result inspection");workers.drain();
    for(unsigned i=0;i<2;++i) {
        const auto& t=workers.owners[i].batch;
        const auto logical=t.bytes.requested+t.bytes.metadata;
        out.coexist+=logical;out.pool_metadata+=t.pool_metadata;out.pool_mapped+=t.pool_mapped;
        out.inactive+=t.inactive;out.inspection+=t.output.capacity()*sizeof(entry);
        out.arena_charged=std::max(out.arena_charged,t.arena_charged);
        if(!i){out.logical=logical;out.backing=t.bytes.backing;}
    }
    // Keep validation bytes but release the immutable source on its own Worker.
    const auto retiring=clock_type::now();
    std::vector<entry> source_output;
    source_output.swap(workers.owners[1].batch.output);
    require(workers.offer(1,operation::retire),"retire source on original Pool");workers.drain();
    const auto retired=clock_type::now();
    out.queue_bytes=2*sizeof(typename Team::ticket);
    out.cursor_bytes=2*sizeof(typename Table::cursor);
    out.context=sizeof(Team)-2*sizeof(Table)-out.queue_bytes-out.cursor_bytes;
    const auto closing=clock_type::now();workers.close();const auto closed=clock_type::now();
    expected_type actual,source_actual;
    for(const auto& e:workers.owners[0].batch.output)accumulate(actual,e);
    for(const auto& e:source_output)accumulate(source_actual,e);
    normalize(actual);normalize(source_actual);
    require(actual==expected&&source_actual==source_expected,"every byte/multiplicity and immutable source survive cross-Pool folding");
    const auto ns=[](auto a,auto b){return std::chrono::duration<double,std::nano>(b-a).count();};
    out.setup=ns(start,created);out.build=ns(created,built);out.fold=ns(folding,folded);
    out.source_retire=ns(retiring,retired);out.retire=ns(closing,closed);return out;
}
} // namespace ht::epochs

int main(int argc,char** argv) {
    using namespace ht;using namespace epochs;
    const bool checking=argc<2||std::string_view(argv[1])=="check";
    require(checking||std::string_view(argv[1])=="bench","fold mode");
    const unsigned reps=argc>2?std::stoul(argv[2]):3;
    const unsigned n=argc>3?std::stoul(argv[3]):K==8?128:768;
    const unsigned passes=argc>4?std::stoul(argv[4]):8;
    require(n&&n<=1024&&passes&&passes<=128&&(K!=8||n<=128),"bounded fold fixture");
    const auto affinity=cpus();
    std::puts("K,kind,W,arm,keys,passes,overlap,rep,setup_ns,build_ns,fold_ns,source_retire_ns,retire_ns,lifetime_ns,fold_ns_per_input,transferred,chunks,source_work,growth_work,partial,logical,backing,coexist_logical,queue_bytes,cursor_bytes,pool_metadata,pool_mapped,arena_charged,source_epoch,destination_before,destination_after,cancelled,slot_family,inactive_bytes,context_bytes,inspection_bytes");
    for(unsigned overlap:{0u,50u,100u})for(unsigned rep=0;rep<(checking?1:reps);++rep)for(unsigned order=0;order<2;++order) {
        const bool ours=(rep+order)%2;
        const auto r=ours?fold_trial<candidate>(n,passes,overlap,checking,affinity):fold_trial<baseline>(n,passes,overlap,checking,affinity);
        std::printf("%u,%u,%u,%s,%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%lld,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%llu,%llu,%llu\n",
            K,unsigned(Kind),W,ours?"hierarchy":"abseil",n,passes,overlap,rep,r.setup,r.build,r.fold,r.source_retire,r.retire,
            r.setup+r.build+r.fold+r.source_retire+r.retire,r.fold/r.transferred,
            (unsigned long long)r.transferred,(unsigned long long)r.chunks,(unsigned long long)r.source_work,(unsigned long long)r.growth_work,
            (unsigned long long)r.partial,(unsigned long long)r.logical,ours?(long long)r.backing:-1LL,
            (unsigned long long)r.coexist,(unsigned long long)r.queue_bytes,(unsigned long long)r.cursor_bytes,
            (unsigned long long)r.pool_metadata,(unsigned long long)r.pool_mapped,(unsigned long long)r.arena_charged,
            r.source_epoch,r.destination_before,r.destination_after,r.cancellations,HT_EPOCH_SLOTS,(unsigned long long)r.inactive,(unsigned long long)r.context,(unsigned long long)r.inspection);
    }
}

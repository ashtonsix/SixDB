#include "worker_owner.h"

namespace ht::epochs {
void partial_boundary_check(const std::array<int,2>& cpus) {
    if constexpr(policy==5&&K>8) {
        team<candidate> workers(cpus);auto records=input(256,false);
        expected_type expected;
        for(unsigned at=0;at<256;at+=batch_size) {
            require(workers.offer(0,operation::mutate,std::span<const entry>(records.data()+at,batch_size)),"fill one compact owner");
            workers.drain();workers.harvest(0);
            for(unsigned i=0;i<batch_size;++i)accumulate(expected,records[at+i]);
        }
        require(workers.population[0]==256&&!workers.epoch,"full compact owner");
        std::array<entry,batch_size> tail;std::copy_n(records.begin(),batch_size-1,tail.begin());
        tail.back().key=word(256)*0xd6e8feb86659fd93ULL&mask(K);
        for(unsigned j=0;j<W;++j)tail.back().bytes[j]=byte(j*31+71);
        require(workers.offer(0,operation::mutate,tail),"duplicate prefix followed by absent key");
        workers.drain();workers.harvest(0);
        require(workers.operations[0]==319&&workers.retry_begin[0]==63&&workers.retry_count[0]==1&&
            workers.partial_prefixes==1&&workers.growth_requested,"exact accepted prefix and untouched suffix");
        // One peer refuses preparation after the first is ready. Cancellation
        // must preserve both the table contents and the retired ticket suffix.
        workers.grow(true);require(workers.cancellations==1&&workers.epoch==1,"paired retry epoch");
        require(workers.replay(0),"resume after paired cancellation and growth");workers.drain();workers.harvest(0);
        require(workers.operations[0]==320&&workers.population[0]==257&&workers.retried==1,"accepted values are never replayed");
        for(const auto& e:tail)accumulate(expected,e);
        require(workers.offer(0,operation::inspect),"inspect resumed owner");workers.drain();
        expected_type actual;for(const auto& e:workers.owners[0].batch.output)accumulate(actual,e);
        for(auto& [k,v]:actual)std::sort(v.begin(),v.end());for(auto& [k,v]:expected)std::sort(v.begin(),v.end());
        require(actual==expected,"full-byte reduction and multiplicity through partial admission");
        workers.owners[0].batch.output.clear();workers.close();
    }
}
void hierarchy_boundary_check(const std::array<int,2>& cpus) {
    if constexpr(HT_EPOCH_GROWTH&&K>8) {
        const unsigned boundary=candidate::limit(1);
        auto records=input(boundary+batch_size,false,1);
        expected_type expected;
        team<candidate> workers(cpus);
        auto apply=[&](unsigned begin,unsigned count) {
            for(unsigned at=begin;at<begin+count;) {
                const auto n=std::min(batch_size,begin+count-at);
                require(workers.offer(0,operation::mutate,std::span<const entry>(records.data()+at,n)),"boundary input");
                workers.drain();workers.harvest(0);
                require(!workers.growth_requested,"admitted hierarchy boundary batch");
                for(unsigned i=0;i<n;++i)accumulate(expected,records[at+i]);
                at+=n;
            }
        };
        apply(0,256);workers.grow(true);apply(256,boundary-256);
        require(workers.population[0]==boundary&&workers.population[1]==0,"asymmetric exact threshold population");
        require(candidate::should_grow(1,boundary,batch_size)==(HT_EPOCH_GROWTH==1),
            "completed deltas avoid duplicate-only upper-bound growth");
        apply(0,batch_size);
        require(workers.population[0]==boundary,"duplicate batch preserves occupancy");
        // The completed-delta policy permits at most one accepted 64-record
        // batch beyond the trigger. The index's overflow storage absorbs it.
        apply(boundary,batch_size);
        require(workers.population[0]==boundary+batch_size&&
            candidate::should_grow(1,workers.population[0],1),"bounded actual-occupancy crossing");
        workers.grow(true);require(workers.epoch==2,"second hierarchy level committed");
        workers.grow(true);require(workers.epoch==3&&workers.cancellations==3,"third level and every cancellation exercised");
        require(workers.offer(0,operation::inspect),"inspect final doubled owner");workers.drain();
        expected_type actual;for(const auto& e:workers.owners[0].batch.output)accumulate(actual,e);
        for(auto& [k,v]:actual)std::sort(v.begin(),v.end());for(auto& [k,v]:expected)std::sort(v.begin(),v.end());
        require(actual==expected,"every key, opaque value and duplicate survives three coordinated epochs");
        workers.owners[0].batch.output.clear();workers.close();
    }
}
struct result {
    double setup=0,intake=0,growth=0,retire=0;
    word logical=0,backing=0,queue_bytes=0,offers=0,refused=0,work0=0,work1=0,scan_work=0;
    unsigned epochs=0,cancelled=0,max_inflight=0;
    word inactive_bytes=0,context_bytes=0,pool_metadata=0,pool_mapped=0,arena_charged=0;
    word partial_returns=0,partial_prefixes=0,retried=0;
    unsigned keys0=0,keys1=0;
    word inspection_bytes=0;
};
template<class Table> result trial(const std::vector<entry>& input,bool partitioned,bool checking,const std::array<int,2>& cpus) {
    // Inspection is fixture storage, allocated before any measured owner. Keep
    // output until after shutdown; validation must not perturb malloc release.
    expected_type expected;for(const auto& e:input)accumulate(expected,e);
    for(auto& [k,v]:expected)std::sort(v.begin(),v.end());
    std::array<std::vector<entry>,2> inspection;
    for(auto& output:inspection)output.reserve(input.size());
    const auto begin=clock_type::now();team<Table> workers(cpus,&inspection);const auto start=clock_type::now();
    result out;out.setup=std::chrono::duration<double,std::nano>(start-begin).count();
    std::array<std::array<entry,batch_size>,2> pending;std::array<unsigned,2> count{};
    unsigned at=0;bool cancellation_done=false;
    auto harvest=[&]{for(unsigned i=0;i<2;++i)if(workers.owners[i].batch.credit.available)workers.harvest(i);};
    while(at<input.size()||count[0]||count[1]||workers.retry_count[0]||workers.retry_count[1]||!workers.idle()) {
        workers.poll();harvest();
        if constexpr(policy==5) {
            if(workers.growth_requested) {
                require(workers.epoch==0,"actual small-list promotion request");
                const auto before=clock_type::now();workers.grow(checking&&!cancellation_done);cancellation_done=true;
                out.growth+=std::chrono::duration<double,std::nano>(clock_type::now()-before).count();continue;
            }
            for(unsigned i=0;i<2;++i)if(workers.retry_count[i]&&workers.owners[i].batch.credit.available)
                require(workers.replay(i),"resume accepted suffix before later input");
        }
        if constexpr(!policy) {
            const auto limit=workers.epoch==0?64u:128u;
            if(workers.epoch<2&&std::max(workers.population[0],workers.population[1])>=limit) {
                const auto before=clock_type::now();workers.grow(checking&&!cancellation_done);cancellation_done=true;
                out.growth+=std::chrono::duration<double,std::nano>(clock_type::now()-before).count();continue;
            }
        }
        for(unsigned filled=0;at<input.size()&&filled<batch_size;++filled) {
            const auto& e=input[at];unsigned who;
            if(partitioned)who=unsigned(e.key>>(K-1));
            else {
                std::array<word,2> score;
                for(unsigned i=0;i<2;++i)score[i]=workers.population[i]+count[i]+(workers.owners[i].batch.credit.available?0:workers.owners[i].batch.credit.count);
                who=score[0]==score[1]?(workers.operations[0]>workers.operations[1]):(score[0]>score[1]);
            }
            if(count[who]==batch_size)break;pending[who][count[who]++]=e;++at;
        }
        if constexpr(policy) {
            // Each owner has at most one accepted batch. Only offer a new one
            // after its preceding delta is known. Pending records are an upper
            // bound on new keys; K8's whole domain fits the list permanently.
            const word limit=candidate::limit(workers.epoch);
            bool growth=false;
            for(unsigned i=0;i<2;++i)if(count[i]&&workers.owners[i].batch.credit.available&&
                    !(policy==5&&workers.epoch==0)) {
                const word upper=K==8?std::min(256u,workers.population[i]+count[i]):workers.population[i]+count[i];
                if constexpr(HT_EPOCH_GROWTH)growth|=candidate::should_grow(workers.epoch,workers.population[i],count[i]);
                else growth|=upper>limit;
            }
            if(growth) {
                require(workers.epoch<candidate::max_epoch,"fixture exhausted measured hierarchy levels");
                const auto before=clock_type::now();workers.grow(checking&&!cancellation_done);cancellation_done=true;
                out.growth+=std::chrono::duration<double,std::nano>(clock_type::now()-before).count();continue;
            }
        }
        for(unsigned i=0;i<2;++i)if(count[i]&&(count[i]==batch_size||at==input.size())) {
            if(workers.offer(i,operation::mutate,std::span<const entry>(pending[i].data(),count[i])))count[i]=0;
        }
    }
    harvest();const auto end=clock_type::now();out.intake=std::chrono::duration<double,std::nano>(end-start).count()-out.growth;
    for(unsigned i=0;i<2;++i)require(workers.offer(i,operation::inspect),"outside-timing inventory/oracle");workers.drain();
    for(auto& owner:workers.owners) {
        auto& t=owner.batch;out.logical+=t.bytes.requested+t.bytes.metadata;out.backing+=t.bytes.backing;
        out.inactive_bytes+=t.inactive;out.pool_metadata+=t.pool_metadata;out.pool_mapped+=t.pool_mapped;
        out.arena_charged=std::max(out.arena_charged,t.arena_charged);
        out.inspection_bytes+=t.output.capacity()*sizeof(entry);
    }

    out.epochs=workers.epoch;out.cancelled=workers.cancellations;out.scan_work=workers.scan_work;
    out.offers=workers.offers;out.refused=workers.refused_offers;out.max_inflight=workers.max_inflight;
    out.work0=workers.operations[0];out.work1=workers.operations[1];
    out.keys0=workers.population[0];out.keys1=workers.population[1];
    out.partial_returns=workers.partial_returns;out.partial_prefixes=workers.partial_prefixes;out.retried=workers.retried;
    out.queue_bytes=2*sizeof(typename team<Table>::ticket)+sizeof(pending)+sizeof(count);
    // Separate fixed caller/Worker storage from active table and ticket storage.
    out.context_bytes=sizeof(team<Table>)-2*sizeof(Table)-2*sizeof(typename team<Table>::ticket);
    require(out.work0+out.work1==input.size()&&out.max_inflight<=2,"exact accepted-work accounting and bounded intake");
    if constexpr(policy&&K==8)require(!out.epochs,"complete K8 domain remains compact");
    const auto closing=clock_type::now();workers.close();out.retire=std::chrono::duration<double,std::nano>(clock_type::now()-closing).count();
    expected_type actual;
    for(const auto& owner:workers.owners)for(const auto& e:owner.batch.output)accumulate(actual,e);
    for(auto& [k,v]:actual)std::sort(v.begin(),v.end());
    require(actual==expected,"complete independent union/reduction/multiplicity oracle");return out;
}
} // namespace ht::epochs

int main(int argc,char** argv) {
    using namespace ht;using namespace epochs;
    const bool checking=argc<2||std::string_view(argv[1])=="check";
    require(checking||std::string_view(argv[1])=="bench","worker epoch mode");
    const unsigned reps=argc>2?std::stoul(argv[2]):3,n=argc>3?std::stoul(argv[3]):K==8?(checking?256:240):1024;const auto affinity=cpus();
    const unsigned passes=argc>4?std::stoul(argv[4]):8;require(passes&&passes<=4096,"bounded repeated-update trace");
    require(n&&n<=1024&&(K!=8||n<=256),"worker population control");
    if(checking){partial_boundary_check(affinity);hierarchy_boundary_check(affinity);}
    std::puts("K,kind,W,arm,partitioned,skew,rep,operations,setup_ns,intake_ns,growth_ns,retire_ns,owner_lifetime_ns,ns_per_update,logical,backing,queue_bytes,epochs,cancelled,offers,refused_offers,max_inflight,work0,work1,scan_work,policy,A,Next,P,Q,inline_bytes,first_capacity,second_capacity,inactive_bytes,context_bytes,pool_metadata,pool_mapped,arena_charged,keys0,keys1,input_keys,partial_returns,partial_prefixes,retried,inspection_bytes,packet,passes,lookup,growth_policy,Last,third_capacity,slot_family,P2,Q2,P3,Q3,p_slots,q_slots,p2_slots,q2_slots,p3_slots,q3_slots");
    for(bool skew:{false,true})for(bool partitioned:{false,true})for(unsigned rep=0;rep<(checking?1:reps);++rep) {
        const auto records=input(n,skew,passes);
        for(unsigned order=0;order<2;++order) {
            const auto arm=(rep+order)%2;
            const auto r=arm?trial<candidate>(records,partitioned,checking,affinity):trial<baseline>(records,partitioned,checking,affinity);
            const auto lifetime=r.setup+r.intake+r.growth+r.retire;
            if constexpr(policy==5)if(n<=256)require(r.epochs==0,"all small-key populations remain compact");
            std::printf("%u,%u,%u,%s,%u,%u,%u,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%lld,%llu,%u,%u,%llu,%llu,%u,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",K,unsigned(Kind),W,arm?"hierarchy":"abseil",partitioned,skew,rep,records.size(),
                r.setup,r.intake,r.growth,r.retire,lifetime,(r.intake+r.growth)/records.size(),
                (unsigned long long)r.logical,arm?(long long)r.backing:-1LL,(unsigned long long)r.queue_bytes,r.epochs,r.cancelled,
                (unsigned long long)r.offers,(unsigned long long)r.refused,r.max_inflight,(unsigned long long)r.work0,(unsigned long long)r.work1,(unsigned long long)r.scan_work,
                policy,candidate::A,candidate::Next,candidate::P,candidate::Q,candidate::Inline,
                (unsigned long long)candidate::first_capacity,(unsigned long long)candidate::second_capacity,
                (unsigned long long)r.inactive_bytes,(unsigned long long)r.context_bytes,(unsigned long long)r.pool_metadata,
                (unsigned long long)r.pool_mapped,(unsigned long long)r.arena_charged,r.keys0,r.keys1,n,
                (unsigned long long)r.partial_returns,(unsigned long long)r.partial_prefixes,(unsigned long long)r.retried,
                (unsigned long long)r.inspection_bytes,HT_EPOCH_PACKET,passes,HT_SMALL_PACKET_LOOKUP,
                HT_EPOCH_GROWTH,candidate::Last,(unsigned long long)candidate::third_capacity,
                HT_EPOCH_SLOTS,candidate::shape<candidate::Next>::P,candidate::shape<candidate::Next>::Q,
                candidate::shape<candidate::Last>::P,candidate::shape<candidate::Last>::Q,
                candidate::Index<candidate::A>::primary.shape.slots,candidate::Index<candidate::A>::secondary.shape.slots,
                candidate::Index<candidate::Next>::primary.shape.slots,candidate::Index<candidate::Next>::secondary.shape.slots,
                candidate::Index<candidate::Last>::primary.shape.slots,candidate::Index<candidate::Last>::secondary.shape.slots);
        }
    }
}

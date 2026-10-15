#include "links.h"
#include "reference/baseline.h"
#include <absl/container/inlined_vector.h>
#include <malloc.h>
#include <string_view>

// This executable compares complete bag owners, not a hashtable or an MVCC
// implementation. The retained-reader arm owns two private whole copies. It
// exercises lifetime and clone cost, without claiming ObjectStore publication.
namespace ht::bag_test {
using counters=hashtable_spike::allocation_counters;
struct operations {word payload_allocations=0,growths=0,metadata_allocations=0;};
struct empty {};
template<bool Audit> struct hook { [[no_unique_address]] std::conditional_t<Audit,operations*,empty> counts;
    explicit hook(operations* p) {if constexpr(Audit)counts=p;else (void)p;}
    void metadata() {if constexpr(Audit)++counts->metadata_allocations;}
    void allocation() {if constexpr(Audit)++counts->payload_allocations;}
    void growth() {if constexpr(Audit)++counts->growths;}
    operations* pointer()const {if constexpr(Audit)return counts;else return nullptr;}
};
template<bool Audit=false,bool Hint=false,bool Physical=false> class linked {
    struct row {word value=0;unsigned count=0;};
    oa::Pool* pool_;std::vector<row> rows_;std::unique_ptr<links<64,false,0>> links_;
    [[no_unique_address]] hook<Audit> events_;
    struct observer {hook<Audit>& events;void operator()(const link_event& e)const noexcept {
        if constexpr(Audit) {
            if(e.kind==link_event_kind::directory_relocated)events.metadata();
            if(e.kind==link_event_kind::directory_inserted)events.allocation();
            if(e.kind==link_event_kind::allocation_grown)events.growth();
        }else (void)e;
    }};
    struct clone_shell {};
    linked(const linked& from,clone_shell):pool_(from.pool_),rows_(from.rows_),events_(from.events_.pointer()) {
        if(!rows_.empty())events_.metadata();
    }
  public:
    static constexpr const char* name=Physical?"streampack_links_hint_physical":Hint?"streampack_links_hint":"streampack_links";
    static constexpr std::size_t row_bytes=sizeof(row);
    explicit linked(oa::Pool& pool,unsigned n,operations* op=nullptr,counters* =nullptr)
        :pool_(&pool),rows_(n),events_(op) {if(n)events_.metadata();}
    linked(linked&&)noexcept=default;
    bool append(unsigned i,word value) {
        auto& r=rows_[i];
        if(!r.count) {r.value=value;r.count=1;return true;}
        if(!links_) {links_=std::make_unique<links<64,false,0>>(*pool_);events_.metadata();}
        observer observe{events_};const auto head=r.count==1?0:std::uint32_t(r.value);
        const auto hint=Hint && r.count>1?std::uint32_t(r.value>>32):0;
        auto p=links_->reserve_values(head,r.count==1?2:1,observe,hint);if(!p)return false;
        const std::array<word,2> values{r.value,value};
        p->commit(std::span<const word>(values.data()+(r.count!=1),r.count==1?2:1));
        r.value=p->head_after();
        if constexpr(Hint)r.value|=word(p->insertion_after())<<32;
        ++r.count;return true;
    }
    void erase(unsigned i) {
        auto& r=rows_[i];if(r.count>1) {observer observe{events_};
            require(links_->release_values(std::uint32_t(r.value),observe)==r.count,"link release count");}
        r={};
    }
    template<class Visit> void each(unsigned i,Visit&& visit)const {
        const auto& r=rows_[i];if(r.count==1)visit(r.value);
        else if(r.count>1)links_->each(std::uint32_t(r.value),[&](word v,word){visit(v);});
    }
    // Read-only attribution control: resolve the existing StreamPack slot once
    // per link, then consume its admitted fields through the same TuplePack
    // operations. Keep the per-bag call, matching the observed ordinary path;
    // this measures resolution/fusion scope, not a replacement owner or codec.
    template<class Visit> [[gnu::noinline]] void each_resolved(unsigned i,Visit&& visit)const {
        const auto& r=rows_[i];if(r.count==1)visit(r.value);
        else if(r.count>1)for(auto ref=std::uint32_t(r.value);ref;) {
            const auto window=links_->payload_window(ref);const auto* p=window.data();
            const auto n=get<2,false>(p+4);
            for(unsigned j=0;j<n;++j)visit(get<8,false>(p+6+j*8));
            ref=std::uint32_t(get<4,false>(p));
        }
    }
    linked clone()const {
        if constexpr(Physical) {
            linked copy(*this,clone_shell{});
            if(links_) {
                auto blocks=links_->clone(*pool_);if(!blocks)throw std::bad_alloc();
                if constexpr(Audit) {
                    // Current clone implementation performs one exact-size
                    // directory reservation and one allocate per source block.
                    // Account from the actual destination inventory; this
                    // walk is absent in the ordinary timed owner.
                    if(!blocks->directory_storage().empty())copy.events_.metadata();
                    blocks->allocations([&](const oa::Allocation&){copy.events_.allocation();});
                }
                copy.links_=std::make_unique<links<64,false,0>>(std::move(*blocks));copy.events_.metadata();
            }
            return copy;
        } else {
            // Explicit logical-replay control, not intrinsic byte-copy cost.
            linked copy(*pool_,rows_.size(),events_.pointer());
            for(unsigned i=0;i<rows_.size();++i)each(i,[&](word value){if(!copy.append(i,value))throw std::bad_alloc();});
            return copy;
        }
    }
    unsigned size(unsigned i)const {return rows_[i].count;}
    memory footprint()const {
        memory out{0,0,sizeof(linked<false,Hint,Physical>)+rows_.capacity()*sizeof(row)};
        if(links_) {auto m=links_->footprint();out.requested=m.requested;out.backing=m.backing;out.metadata+=m.metadata;}
        return out;
    }
    void validate()const {if(links_)links_->validate();}
};

// Exactly one Orbital Allocation per promoted bag. The inline word and physical
// owner overlap; no PMR payload, handle catalog or secondary allocator exists.
struct buffer_row {
    union body {word singleton;oa::Allocation allocation;body():singleton(0){}~body(){}} data;
    unsigned count=0;
    buffer_row()=default;
    buffer_row(const buffer_row&)=delete;
    buffer_row(buffer_row&& other)noexcept:count(other.count) {
        if(count>1)std::construct_at(&data.allocation,std::move(other.data.allocation));
        else data.singleton=other.data.singleton;
        other.clear();
    }
    void clear()noexcept {
        if(count>1)std::destroy_at(&data.allocation);
        std::construct_at(&data.singleton,word(0));count=0;
    }
    ~buffer_row(){if(count>1)std::destroy_at(&data.allocation);}
};
template<bool Audit=false> class buffers {
    oa::Pool* pool_;std::vector<buffer_row> rows_;
    [[no_unique_address]] hook<Audit> events_;
  public:
    static constexpr const char* name="orbital_buffer";
    static constexpr std::size_t row_bytes=sizeof(buffer_row);
    explicit buffers(oa::Pool& pool,unsigned n,operations* op=nullptr,counters* =nullptr)
        :pool_(&pool),rows_(n),events_(op) {if(n)events_.metadata();}
    buffers(buffers&&)noexcept=default;
    bool append(unsigned i,word value) {
        auto& r=rows_[i];if(!r.count){r.data.singleton=value;r.count=1;return true;}
        if(r.count==1) {
            auto a=oa::allocate(*pool_,{.bytes=16,.alignment=8});if(!a)return false;
            events_.allocation();const std::array<word,2> values{r.data.singleton,value};
            std::memcpy(a->address,values.data(),16);std::construct_at(&r.data.allocation,std::move(*a));r.count=2;
            return true;
        }
        auto& a=r.data.allocation;
        if((r.count+1)*sizeof(word)>a.size) {
            if(!oa::grow(a,{.bytes=std::bit_ceil(r.count+1)*sizeof(word),.alignment=8}))return false;
            events_.growth();
        }
        std::memcpy(a.address+r.count*sizeof(word),&value,sizeof(word));++r.count;return true;
    }
    void erase(unsigned i){rows_[i].clear();}
    template<class Visit> void each(unsigned i,Visit&& visit)const {
        const auto& r=rows_[i];if(r.count==1)visit(r.data.singleton);
        else if(r.count>1)for(unsigned j=0;j<r.count;++j) {
            word value;std::memcpy(&value,r.data.allocation.address+j*sizeof(word),sizeof(word));visit(value);
        }
    }
    buffers clone()const {
        buffers copy(*pool_,rows_.size(),events_.pointer());
        for(unsigned i=0;i<rows_.size();++i) {
            const auto& from=rows_[i];auto& to=copy.rows_[i];
            if(from.count<=1){to.data.singleton=from.data.singleton;to.count=from.count;continue;}
            auto a=oa::allocate(*pool_,{.bytes=from.data.allocation.size,.alignment=8});
            if(!a)throw std::bad_alloc();copy.events_.allocation();
            std::memcpy(a->address,from.data.allocation.address,from.count*sizeof(word));
            std::construct_at(&to.data.allocation,std::move(*a));to.count=from.count;
        }
        return copy;
    }
    unsigned size(unsigned i)const {return rows_[i].count;}
    memory footprint()const {
        memory out{0,0,sizeof(buffers<false>)+rows_.capacity()*sizeof(buffer_row)};
        for(const auto& r:rows_)if(r.count>1) {out.requested+=r.data.allocation.size;out.backing+=oa::capacity(r.data.allocation);}
        return out;
    }
    void validate()const {for(const auto& r:rows_)if(r.count>1)require(r.count*8<=r.data.allocation.size,"buffer extent");}
};

template<bool Audit=false> class inlined {
    using allocator=std::conditional_t<Audit,hashtable_spike::counting_allocator<word>,std::allocator<word>>;
    using row=absl::InlinedVector<word,1,allocator>;
    std::vector<row> rows_;
    [[no_unique_address]] hook<Audit> events_;
  public:
    static constexpr const char* name="abseil_inlined_vector";
    static constexpr std::size_t row_bytes=sizeof(row);
    explicit inlined(oa::Pool&,unsigned n,operations* op=nullptr,counters* counts=nullptr):events_(op) {
        if(n)events_.metadata();
        if constexpr(Audit) {require(counts,"baseline allocation audit");rows_.reserve(n);
            for(unsigned i=0;i<n;++i)rows_.emplace_back(allocator(*counts));}
        else rows_.resize(n);
    }
    inlined(const inlined& other):rows_(other.rows_),events_(other.events_.pointer()) {if(!rows_.empty())events_.metadata();}
    inlined(inlined&&)noexcept=default;
    bool append(unsigned i,word value){rows_[i].push_back(value);return true;}
    void erase(unsigned i){
        // Erase the owning multimap value, rather than vector::clear (which
        // deliberately keeps its heap capacity). The slot can then be reused.
        const auto allocator=rows_[i].get_allocator();std::destroy_at(&rows_[i]);
        std::construct_at(&rows_[i],allocator);
    }
    template<class Visit> void each(unsigned i,Visit&& visit)const {for(word v:rows_[i])visit(v);}
    inlined clone()const {return *this;}
    unsigned size(unsigned i)const{return rows_[i].size();}
    unsigned capacity(unsigned i)const{return rows_[i].capacity();}
    static unsigned inline_capacity(){row r;return r.capacity();}
    memory footprint()const {
        memory out{0,0,sizeof(inlined<false>)+rows_.capacity()*inlined<false>::row_bytes};
        for(const auto& r:rows_)if(r.capacity()>inline_capacity()) {
            out.requested+=r.capacity()*sizeof(word);
            // Linux default operator new/allocator in this executable; queried
            // only in the untimed census, never used to authorize an access.
            out.backing+=malloc_usable_size(const_cast<word*>(r.data()));
        }
        return out;
    }
    void validate()const{}
};

struct action {unsigned bag;word value;};
struct history {
    std::string name;unsigned bags;std::vector<unsigned> sizes;
    std::vector<action> fill,extend,refill;std::vector<unsigned> erased;
    std::vector<std::vector<word>> initial,final;
    static word value(unsigned bag,unsigned at,unsigned epoch) {
        // Deliberate duplicates, zero and all-ones; order changes are observable.
        if(at%7==0)return 0;if(at%11==0)return ~word(0);
        return mix(word(bag)*743+at/2+word(epoch)*193919);
    }
    history(unsigned n,unsigned length):name(length?"fixed"+std::to_string(length):"skew"),bags(n),sizes(n),initial(n),final(n) {
        for(unsigned i=0;i<n;++i)sizes[i]=length?length:(i%128==0?512:i%16==0?64:i%4==0?8:i%2==0?2:1);
        auto add=[&](auto& events,auto& expected,unsigned i,word v){events.push_back({i,v});expected[i].push_back(v);};
        for(unsigned j=0;j<*std::max_element(sizes.begin(),sizes.end());++j)
            for(unsigned i=0;i<n;++i)if(j<sizes[i])add(fill,initial,i,value(i,j,0));
        final=initial;
        for(unsigned i=0;i<n;++i)add(extend,final,i,value(i,sizes[i],0));
        for(unsigned i=0;i<n;i+=3){erased.push_back(i);final[i].clear();}
        for(unsigned j=0;j<*std::max_element(sizes.begin(),sizes.end());++j)
            for(auto i:erased)if(j<sizes[i])add(refill,final,i,value(i,j,1));
    }
    word mutations()const{return fill.size()+extend.size()+erased.size()+refill.size();}
};
struct digest {word sum=0,ordered=0,count=0;bool operator==(const digest&)const=default;
    void add(word v,unsigned bag,unsigned slot){sum+=v;ordered+=std::rotl(v,int(slot&63))^word(bag)*0x9e3779b9; ++count;}};
digest expected(const std::vector<std::vector<word>>& rows) {
    digest out;for(unsigned i=0;i<rows.size();++i)for(unsigned j=0;j<rows[i].size();++j)out.add(rows[i][j],i,j);return out;
}
template<class Owner> [[gnu::noinline]] digest consume(const Owner& owner,unsigned n) {
    digest out;for(unsigned i=0;i<n;++i) {unsigned at=0;owner.each(i,[&](word v){out.add(v,i,at++);});}return out;
}
[[gnu::noinline]] digest consume_resolved(const linked<>& owner,unsigned n) {
    digest out;for(unsigned i=0;i<n;++i) {unsigned at=0;owner.each_resolved(i,[&](word v){out.add(v,i,at++);});}return out;
}
template<class Owner> void verify(const Owner& owner,const std::vector<std::vector<word>>& rows) {
    owner.validate();for(unsigned i=0;i<rows.size();++i) {
        unsigned at=0;owner.each(i,[&](word v){require(at<rows[i].size() && rows[i][at++]==v,"ordered bag oracle");});
        require(at==rows[i].size() && owner.size(i)==at,"bag cardinality oracle");
    }
}
struct sample {
    double total=0,setup=0,fill=0,read=0,clone=0,extend=0,reuse=0,retire=0,release=0;
    digest old_read{},new_read{};
    memory coexist{};word sampled_requested_peak=0,sampled_backing_peak=0,sampled_metadata_peak=0,capacity_signature=0;
    operations calls;word baseline_peak=0,baseline_allocations=0,baseline_deallocations=0;
    oa::Statistics pool_before{},pool_after{};
};
double ns(clock_type::time_point t){return std::chrono::duration<double,std::nano>(clock_type::now()-t).count();}
template<class Owner,bool Inspect> sample execute(oa::Pool& pool,const history& input) {
    sample out;counters allocation_counts;out.pool_before=oa::statistics(pool);
    std::optional<Owner> old,current;
    auto observe=[&] {
        if constexpr(Inspect) {
            memory m;for(auto* p:{&old,&current})if(*p) {
                auto one=(*p)->footprint();m.requested+=one.requested;m.backing+=one.backing;m.metadata+=one.metadata;
                if constexpr(requires{(*p)->capacity(0);})for(unsigned i=0;i<input.bags;++i)
                    out.capacity_signature=out.capacity_signature*131+(*p)->capacity(i);
            }
            out.sampled_requested_peak=std::max(out.sampled_requested_peak,m.requested);
            out.sampled_backing_peak=std::max(out.sampled_backing_peak,m.backing);
            out.sampled_metadata_peak=std::max(out.sampled_metadata_peak,m.metadata);
            out.coexist=m;
        }
    };
    auto append=[&](Owner& owner,const auto& events) {
        for(const auto& event:events) {
            require(owner.append(event.bag,event.value),"bag append admission");
            if constexpr(Inspect && requires{owner.capacity(event.bag);})
                out.capacity_signature=out.capacity_signature*131+owner.capacity(event.bag);
        }
    };
    const auto start=clock_type::now();auto t=start;
    old.emplace(pool,input.bags,&out.calls,&allocation_counts);out.setup=ns(t);
    t=clock_type::now();append(*old,input.fill);out.fill=ns(t);observe();
    if constexpr(Inspect)verify(*old,input.initial);
    t=clock_type::now();out.old_read=consume(*old,input.bags);out.read=ns(t);
    t=clock_type::now();current.emplace(old->clone());out.clone=ns(t);observe();
    if constexpr(Inspect){verify(*old,input.initial);verify(*current,input.initial);}
    t=clock_type::now();append(*current,input.extend);out.extend=ns(t);observe();
    t=clock_type::now();for(auto i:input.erased)current->erase(i);append(*current,input.refill);out.reuse=ns(t);observe();
    if constexpr(Inspect){verify(*old,input.initial);verify(*current,input.final);}
    t=clock_type::now();const auto still_old=consume(*old,input.bags);out.new_read=consume(*current,input.bags);out.read+=ns(t);
    t=clock_type::now();old.reset();out.retire=ns(t);observe();
    t=clock_type::now();const auto after_retire=consume(*current,input.bags);out.read+=ns(t);
    t=clock_type::now();current.reset();out.release=ns(t);out.total=ns(start);
    require(still_old==out.old_read && after_retire==out.new_read,"retained and surviving reader history");
    out.pool_after=oa::statistics(pool);out.baseline_peak=allocation_counts.peak_bytes;
    out.baseline_allocations=allocation_counts.allocation_calls;out.baseline_deallocations=allocation_counts.deallocation_calls;
    require(allocation_counts.current_bytes==0,"audit allocator released all payloads");return out;
}
void check_answers(const sample& out,const history& h) {
    require(out.old_read==expected(h.initial) && out.new_read==expected(h.final),"whole lifecycle digests");
}
struct accounting {sample census,audit;};
template<class Owner,class Audit> accounting inspect_history(const history& input) {
    // Full verification and memory/allocation walking happen in separate replays,
    // never between timed phases. Reported peaks are phase-boundary coexistence,
    // not transient remapping peaks hidden inside an allocator.
    // Capacity selection can depend on available Pool palettes. Start each
    // accounting replay with its own Pool, so another arm's growth history
    // cannot manufacture a physical-capacity difference between equal layouts.
    sample census,audit;
    {pool_owner pool;census=execute<Owner,true>(*pool.pool,input);}
    {pool_owner pool;audit=execute<Audit,true>(*pool.pool,input);}
    check_answers(census,input);check_answers(audit,input);
    require(census.sampled_requested_peak==audit.sampled_requested_peak,"allocation audit payload parity");
    if constexpr(requires(Owner o){o.capacity(0);})require(census.capacity_signature==audit.capacity_signature,"ordinary/counting allocator capacity history");
    return {census,audit};
}
template<class Owner> void campaign(oa::Pool& pool,const history& input,unsigned rep,const accounting& facts) {
    auto timed=execute<Owner,false>(pool,input);check_answers(timed,input);
    const auto& census=facts.census;const auto& audit=facts.audit;
    std::printf("%s,%s,%u,%zu,%llu,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu",
        Owner::name,input.name.c_str(),input.bags,input.fill.size(),(unsigned long long)input.mutations(),rep,
        timed.total,timed.setup,timed.fill,timed.read,timed.clone,timed.extend,timed.reuse,timed.retire,timed.release,
        (unsigned long long)census.sampled_requested_peak,(unsigned long long)census.sampled_backing_peak,
        (unsigned long long)census.sampled_metadata_peak,(unsigned long long)audit.calls.payload_allocations,
        (unsigned long long)audit.calls.growths,(unsigned long long)audit.calls.metadata_allocations,
        (unsigned long long)audit.baseline_allocations,(unsigned long long)audit.baseline_deallocations,
        (unsigned long long)audit.baseline_peak,
        (unsigned long long)(timed.pool_after.allocations-timed.pool_before.allocations),
        (unsigned long long)(timed.pool_after.growths-timed.pool_before.growths));
    std::printf(",%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        (unsigned long long)(timed.pool_after.reuses-timed.pool_before.reuses),
        (unsigned long long)(timed.pool_after.relocations-timed.pool_before.relocations),
        (unsigned long long)timed.pool_before.metadata_bytes,(unsigned long long)timed.pool_after.metadata_bytes,
        (unsigned long long)timed.pool_before.mapped_bytes,(unsigned long long)timed.pool_after.mapped_bytes,
        (unsigned long long)timed.pool_after.idle_bytes);
    std::fflush(stdout);
}
template<class Owner> void refusal_check() {
    pool_owner pool(1u<<20);Owner b(*pool.pool,2);
    require(b.append(0,0) && b.append(1,~word(0)),"refusal singletons");
    std::vector<oa::Allocation> pressure;
    while(auto a=oa::allocate(*pool.pool,{.bytes=4096,.alignment=64}))pressure.push_back(std::move(*a));
    // Fill every small allocation palette left by the coarse pressure requests.
    while(auto a=oa::allocate(*pool.pool,{.bytes=16,.alignment=8}))pressure.push_back(std::move(*a));
    require(!b.append(0,73),"promotion pressure refusal");
    verify(b,{{0},{~word(0)}});pressure.clear();
    auto until=clock_type::now()+std::chrono::seconds(10);
    while(oa::trim_cold(*pool.pool,0).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<until,"refusal trim");orbital::worker::poll(pool.lane,1024);
    }
    require(b.append(0,73),"promotion retry");verify(b,{{0,73},{~word(0)}});
    while(auto a=oa::allocate(*pool.pool,{.bytes=4096,.alignment=64}))pressure.push_back(std::move(*a));
    while(auto a=oa::allocate(*pool.pool,{.bytes=16,.alignment=8}))pressure.push_back(std::move(*a));
    std::vector<std::vector<word>> expected{{0,73},{~word(0)}};bool refused=false;
    for(word i=0;i<100000;++i) {
        if(!b.append(0,i)){refused=true;break;}expected[0].push_back(i);
    }
    require(refused,"growth pressure refusal");verify(b,expected);
    // A failed larger link does not exhaust the existing first-family palette:
    // consume that concrete 2 + (6 + 4*8) byte request before testing a clone.
    while(auto a=oa::allocate(*pool.pool,{.bytes=40,.alignment=8}))pressure.push_back(std::move(*a));
    bool clone_refused=false;try{auto failed=b.clone();(void)failed;}catch(const std::bad_alloc&){clone_refused=true;}
    require(clone_refused,"whole clone pressure refusal");verify(b,expected);
}
void read_diagnostic(oa::Pool& pool,const history& input,unsigned reps) {
    linked<> owner(pool,input.bags);
    for(const auto& event:input.fill)require(owner.append(event.bag,event.value),"read diagnostic fill");
    const auto answer=expected(input.initial);
    require(consume(owner,input.bags)==answer && consume_resolved(owner,input.bags)==answer,"resolved link read oracle");
    constexpr unsigned repeats=16;
    for(unsigned rep=0;rep<reps;++rep)for(unsigned arm=0;arm<2;++arm) {
        const bool resolved=(arm+rep)%2;digest check;
        auto t=clock_type::now();
        for(unsigned n=0;n<repeats;++n) {
            // The memory barrier prevents collapsing identical full reads.
            asm volatile("" ::: "memory");
            auto got=resolved?consume_resolved(owner,input.bags):consume(owner,input.bags);
            check.sum+=got.sum;check.ordered+=got.ordered;check.count+=got.count;
        }
        auto elapsed=ns(t);
        require(check==digest{answer.sum*repeats,answer.ordered*repeats,answer.count*repeats},"read diagnostic checksum");
        std::fprintf(stderr,"read_diagnostic,%s,%s,%u,%u,%zu,%.3f\n",input.name.c_str(),resolved?"once_per_link":"ordinary_each",
            rep,repeats,input.fill.size(),elapsed);
    }
}
} // namespace ht::bag_test

int main(int argc,char** argv) {
    using namespace ht;using namespace ht::bag_test;
    const auto mode=argc>1?std::string_view(argv[1]):"check";
    const unsigned reps=argc>2?std::stoul(argv[2]):5;
    require((mode=="check"||mode=="local"||mode=="read") && reps,"bag mode/repetitions");
    if(mode=="check") {
        refusal_check<buffers<>>();refusal_check<linked<>>();refusal_check<linked<false,true>>();refusal_check<linked<false,true,true>>();
    }
    auto prep=clock_type::now();links<64,false,0>::prepare();
    std::fprintf(stderr,"shared_link_format_preparation_ns=%.3f format_bytes=%llu\n",ns(prep),
        (unsigned long long)links<64,false,0>::preparation_bytes());
    std::fprintf(stderr,"ABI: linked row=%zu owner=%zu; buffer row=%zu owner=%zu; ordinary InlinedVector<word,1> row=%zu owner=%zu inline_capacity=%u; audit row=%zu inline_capacity=%u\n",
        linked<>::row_bytes,sizeof(linked<>),buffers<>::row_bytes,sizeof(buffers<>),
        inlined<>::row_bytes,sizeof(inlined<>),inlined<>::inline_capacity(),inlined<true>::row_bytes,inlined<true>::inline_capacity());
    static_assert(linked<false,true>::row_bytes==linked<>::row_bytes && sizeof(linked<false,true>)==sizeof(linked<>));
    static_assert(linked<false,true,true>::row_bytes==linked<>::row_bytes && sizeof(linked<false,true,true>)==sizeof(linked<>));
    std::fprintf(stderr,"Hint control: same row/owner size; promoted descriptor packs head32 plus live insertion-ref32. Erase destroys the entire chain and clears both refs; no individual-value erasure policy is tested.\n");
    std::fprintf(stderr,"Scope: scalar ordered full-value consumer; direct Orbital buffers preserve reservation on clone. Link arms compare logical replay and separate physical whole-block cloning with stable local refs. Erase destroys one owning bag value; its slot is then reused. Timed baseline is ordinary default allocator. Audit is separate and capacity-matched. Each accounting replay starts a fresh Pool (isolated geometry control, not a production allocation policy). Coexistence peaks are sampled, not transient allocator peaks; owned capacity means Orbital accessible capacity or baseline malloc_usable_size, not RSS. Timed Orbital Pool lifetime is shared, table destruction is included; Pool inventory/metadata is reported separately and is not attributed to one live bag.\n");
    std::puts("arm,history,bags,initial_values,mutations,rep,total_ns,setup_ns,fill_ns,reads_ns,clone_ns,extend_ns,erase_refill_ns,retire_old_ns,release_current_ns,sampled_requested_peak,sampled_owned_capacity_peak,sampled_metadata_peak,audit_payload_allocate_calls,audit_payload_grow_calls,audit_metadata_allocate_calls,baseline_audit_allocate_calls,baseline_audit_deallocate_calls,baseline_audit_exact_peak_requested,pool_allocate_calls,pool_grow_calls,pool_reuse_calls,pool_relocation_calls,pool_metadata_before,pool_metadata_after,pool_mapped_before,pool_mapped_after,pool_idle_after");
    pool_owner pool;const unsigned n=mode=="check"?17:2048,repeats=mode=="check"?1:reps;
    for(unsigned length:{1,2,4,8,16,64,512,0}) {
        history input(n,length);
        if(mode=="read") {read_diagnostic(*pool.pool,input,repeats);continue;}
        if(mode=="check")read_diagnostic(*pool.pool,input,1);
        const auto baseline=inspect_history<inlined<>,inlined<true>>(input);
        const auto chain=inspect_history<linked<>,linked<true>>(input);
        const auto chain_hint=inspect_history<linked<false,true>,linked<true,true>>(input);
        const auto chain_physical=inspect_history<linked<false,true,true>,linked<true,true,true>>(input);
        const auto buffer=inspect_history<buffers<>,buffers<true>>(input);
        require(chain.census.sampled_requested_peak==chain_hint.census.sampled_requested_peak &&
            chain.census.sampled_metadata_peak==chain_hint.census.sampled_metadata_peak &&
            chain.census.sampled_backing_peak==chain_hint.census.sampled_backing_peak,"hint layout and physical capacity parity");
        // One complete warm lifetime per arm before rotated timed lifetimes.
        check_answers(execute<inlined<>,false>(*pool.pool,input),input);
        check_answers(execute<linked<>,false>(*pool.pool,input),input);
        check_answers(execute<linked<false,true>,false>(*pool.pool,input),input);
        check_answers(execute<linked<false,true,true>,false>(*pool.pool,input),input);
        check_answers(execute<buffers<>,false>(*pool.pool,input),input);
        for(unsigned rep=0;rep<repeats;++rep)for(unsigned j=0;j<5;++j)switch((j+rep)%5) {
            case 0:campaign<inlined<>>(*pool.pool,input,rep,baseline);break;
            case 1:campaign<linked<>>(*pool.pool,input,rep,chain);break;
            case 2:campaign<linked<false,true>>(*pool.pool,input,rep,chain_hint);break;
            case 3:campaign<linked<false,true,true>>(*pool.pool,input,rep,chain_physical);break;
            case 4:campaign<buffers<>>(*pool.pool,input,rep,buffer);break;
        }
    }
}

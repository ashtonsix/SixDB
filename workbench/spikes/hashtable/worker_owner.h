#pragma once
#ifndef HT_EPOCH_POLICY
#define HT_EPOCH_POLICY 0
#endif
#ifndef HT_EPOCH_PACKET
#define HT_EPOCH_PACKET 0
#endif
#ifndef HT_EPOCH_GROWTH
#define HT_EPOCH_GROWTH 0
#endif
#ifndef HT_EPOCH_SLOTS
#define HT_EPOCH_SLOTS 0
#endif
#if HT_EPOCH_POLICY >= 2
#define HT_EXTERNAL_BAG_PATCH 1
#define HT_BAG_SPILL_PATCH 1
#endif
#include "migration.h"
#include "consolidation.h"
#include "small_baseline.h"
#include <loom/worker.h>
#include <absl/container/internal/hashtable_debug.h>
#include <atomic>
#include <map>
#include <random>
#include <string>
#include <variant>
#if defined(__aarch64__)
#include <arm_neon.h>
#else
#include <immintrin.h>
#endif

#ifndef HT_EPOCH_KEY
#define HT_EPOCH_KEY 40
#endif
#ifndef HT_EPOCH_KIND
#define HT_EPOCH_KIND 1
#endif
#ifndef HT_EPOCH_WIDTH
#define HT_EPOCH_WIDTH 2
#endif

namespace ht::epochs {
constexpr unsigned K=HT_EPOCH_KEY,W=HT_EPOCH_WIDTH;
constexpr auto Kind=container_kind(HT_EPOCH_KIND);
constexpr unsigned batch_size=64,grain=8;
constexpr unsigned policy=HT_EPOCH_POLICY;
static_assert(policy<=5);
static_assert(HT_EPOCH_PACKET>=0&&HT_EPOCH_PACKET<=2);
// Attribution controls: old quadruple/75%; double/90% with pending upper
// bounds; double/90% with completed occupancy deltas and bounded overshoot.
static_assert(HT_EPOCH_GROWTH>=0&&HT_EPOCH_GROWTH<=2);
static_assert(!HT_EPOCH_GROWTH||policy==5);
static_assert(HT_EPOCH_SLOTS==0||HT_EPOCH_SLOTS==14||HT_EPOCH_SLOTS==28);
static_assert(!HT_EPOCH_SLOTS||HT_EPOCH_GROWTH==2);
using value=std::array<byte,W>;
struct entry {word key;value bytes;};
struct combine {
    value operator()(const value& a,const value& b)const noexcept {
        value out;unsigned i=0;
        for(;i+16<=W;i+=16) {
#if defined(__aarch64__)
            vst1q_u8(out.data()+i,veorq_u8(vld1q_u8(a.data()+i),vld1q_u8(b.data()+i)));
#else
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out.data()+i),_mm_xor_si128(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(a.data()+i)),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(b.data()+i))));
#endif
        }
        for(;i<W;++i)out[i]=a[i]^b[i];return out;
    }
};

// A caller fixture dispatches once per accepted chunk, not once per record.
// Geometry/thresholds below are experimental controls, not a product owner.
struct geometry {
    static constexpr unsigned Inline=policy>=3&&Kind==container_kind::multimap?std::max(8u,std::bit_ceil(W)):8;
    using Bags=linked_bags<(W?W:8)>;
    using S=small_table<K,Kind,W,false,Bags,Inline>;
    template<unsigned Bits> struct shape {
        using Planner=record_table<K,Kind,S::stored_bytes,Bits,Bits-2,128,128,
            record_format::exact,false,record_access::exact,S::tag_bits>;
        // Same field solver for requested capacities and final fitting recipes.
        // Recompute after growth changes suffix width; padding may add slots.
        static constexpr unsigned P=HT_EPOCH_SLOTS?
            Planner::unit_for_slots(HT_EPOCH_SLOTS,false,8):
            policy<4?192:W>8?320:Kind==container_kind::set?64:96;
        static constexpr unsigned Q=HT_EPOCH_SLOTS?
            Planner::unit_for_slots(HT_EPOCH_SLOTS==14?5:8,true,8):
            policy<4?96:W>8?160:64;
        using Index=record_table<K,Kind,S::stored_bytes,Bits,Bits-2,P,Q,
            record_format::exact,false,record_access::exact,S::tag_bits>;
        using Table=promoted_table<K,Kind,W,Bits,Bits-2,P,Q,record_format::exact,
            false,record_access::exact,false,Bags,Inline>;
    };
    template<unsigned Bits> using Index=typename shape<Bits>::Index;
};
template<unsigned Bits=2> consteval unsigned address_bits() {
    // Size the first index from its actual admitted slot geometry, not W
    // alone. 512 primary slots is a measured reservation control; the caller
    // never receives the future distinct-key count.
    if constexpr(Bits==K-2 || (word(1)<<Bits)*geometry::Index<Bits>::primary.shape.slots>=512)return Bits;
    else return address_bits<Bits+1>();
}
struct candidate : geometry {
    static constexpr bool native=true;
    static constexpr unsigned A=policy?address_bits():std::min(K-2,W<=8?4u:7u);
    static constexpr unsigned Next=std::min(K-1,A+(HT_EPOCH_GROWTH?1:2));
    static constexpr unsigned Last=std::min(K-1,Next+1);
    static constexpr unsigned max_epoch=HT_EPOCH_GROWTH?3:2;
    static constexpr unsigned threshold=HT_EPOCH_GROWTH?90:75;
    static constexpr unsigned P=shape<A>::P,Q=shape<A>::Q;
    static constexpr word first_capacity=(word(1)<<A)*Index<A>::primary.shape.slots;
    static constexpr word second_capacity=(word(1)<<Next)*Index<Next>::primary.shape.slots;
    static constexpr word third_capacity=(word(1)<<Last)*Index<Last>::primary.shape.slots;
    using H1=shape<A>::Table;
    using H2=shape<Next>::Table;
    using M1=hierarchy_migration<S,H1>;
    using M2=hierarchy_migration<H1,H2>;
    using H3=shape<Last>::Table;
    using M3=hierarchy_migration<H2,H3>;
    struct third_state {std::optional<H3> owner;std::optional<M3> migration;};
    std::optional<S> small;
    std::optional<H1> first;
    std::optional<H2> second;
    std::optional<M1> to_first;
    std::optional<M2> to_second;
#if HT_EPOCH_GROWTH
    third_state third;
#endif
    unsigned epoch=0;
    bool needs_growth=false;
    explicit candidate(oa::Pool& pool):small(std::in_place,pool,117) {}
    template<class F> decltype(auto) visit(F&& f) {
#if HT_EPOCH_GROWTH
        if(epoch==3)return f(*third.owner);
#endif
        switch(epoch){case 0:return f(*small);case 1:return f(*first);default:return f(*second);}
    }
    static word limit(unsigned epoch) {
        if(!epoch)return 256;
        return (epoch==1?first_capacity:epoch==2?second_capacity:third_capacity)*threshold/100;
    }
    static bool should_grow(unsigned epoch,unsigned population,unsigned pending) {
        if constexpr(K==8)return false;
        if(!epoch)return false; // Actual compact-list refusal owns promotion.
        const word occupancy=population+(HT_EPOCH_GROWTH==2?0:pending);
        return occupancy>limit(epoch);
    }
    unsigned apply(std::span<const entry> input) {
        needs_growth=false;
        return visit([&] [[gnu::always_inline]] (auto& table) {
            unsigned done=0;no_small_effects effects;
            if constexpr(HT_EPOCH_PACKET) {
                require(input.size()<=grain,"one bounded mutation grain");
                // Charge AoS-to-packet preparation in the real Worker callback.
                // Inactive lanes stay initialized; no ticket pointers escape.
                std::array<word,grain> keys{};std::array<value,grain> values{};
                for(unsigned i=0;i<input.size();++i){keys[i]=input[i].key;values[i]=input[i].bytes;}
                const unsigned active=unsigned(mask(input.size()));
                const auto accepted=[&] [[gnu::always_inline]] {
                    if constexpr(Kind==container_kind::map)
                        return table.template combineN_bytes<grain>(keys,values,active,combine{},effects);
                    else return table.template setN_bytes<grain>(keys,values,active,effects);
                }();
                done=std::popcount(accepted);
                require(accepted==unsigned(mask(done)),"packet commits a contiguous active prefix");
                if constexpr(policy==5&&std::is_same_v<std::remove_cvref_t<decltype(table)>,S>)
                    if(done<input.size())needs_growth=table.size()==256&&
                        !table.get_bytes(input[done].key,[](const auto&){});
                return done;
            }
            for(const auto& e:input) {
                const auto r=[&] [[gnu::always_inline]] {
                    if constexpr(Kind==container_kind::map)return table.try_combine_bytes(e.key,e.bytes,combine{},effects);
                    else if constexpr(std::is_same_v<std::remove_cvref_t<decltype(table)>,S>)return table.set_bytes(e.key,e.bytes,effects);
                    else return table.try_set_bytes(e.key,e.bytes,effects);
                }();
                if(!r) {
                    if constexpr(policy==5) {
                        if constexpr(std::is_same_v<std::remove_cvref_t<decltype(table)>,S>)
                            needs_growth=table.size()==256&&r.error()==mutation_error::capacity&&
                                !table.get_bytes(e.key,[](const auto&){});
                    }
                    break;
                }
                ++done;
            }
            return done;
        });
    }
    bool prepare(word a,word b) {
        require(epoch<max_epoch&&!to_first&&!to_second,"one migration per owner");
#if HT_EPOCH_GROWTH
        require(!third.migration,"one third-level migration");
#endif
        if(!epoch){to_first=M1::prepare(small,a,b);return bool(to_first);}
#if HT_EPOCH_GROWTH
        if(epoch==2){third.migration=M3::prepare(second,a,b);return bool(third.migration);}
#endif
        to_second=M2::prepare(first,a,b);return bool(to_second);
    }
    record_scan_result step(unsigned budget) {
#if HT_EPOCH_GROWTH
        if(epoch==2)return third.migration->step(budget);
#endif
        return !epoch?to_first->step(budget):to_second->step(budget);
    }
    void cancel(){
        to_first.reset();to_second.reset();
#if HT_EPOCH_GROWTH
        third.migration.reset();
#endif
    }
    void commit() {
        if(!epoch){first.emplace(to_first->finish());to_first.reset();}
        else if(epoch==1){second.emplace(to_second->finish());to_second.reset();}
#if HT_EPOCH_GROWTH
        else {third.owner.emplace(third.migration->finish());third.migration.reset();}
#endif
        ++epoch;
    }
    unsigned size(){return visit([](const auto& t){return t.size();});}
    memory footprint(){return visit([](const auto& t){return t.footprint();});}
    word inactive_bytes(){return visit([](const auto& t){return sizeof(candidate)-sizeof(t);});}
    template<class F> void each(F&& f){visit([&](const auto& t){t.each_bytes(f);});}
    using cursor=std::variant<value_cursor<S>,value_cursor<H1>,value_cursor<H2>,value_cursor<H3>>;
    void begin_scan(cursor& c) {
        switch(epoch){case 0:c.emplace<0>();break;case 1:c.emplace<1>();break;
            case 2:c.emplace<2>();break;default:c.emplace<3>();break;}
    }
    template<class F> value_scan_result scan(cursor& c,unsigned budget,F&& f) {
        require(c.index()==epoch,"retained source interpretation");
        if(epoch==0)return std::get<0>(c).step(*small,budget,grain,f);
        if(epoch==1)return std::get<1>(c).step(*first,budget,grain,f);
#if HT_EPOCH_GROWTH
        if(epoch==3)return std::get<3>(c).step(*third.owner,budget,grain,f);
#endif
        return std::get<2>(c).step(*second,budget,grain,f);
    }
    unsigned fold(std::span<const entry> input) {
        if constexpr(Kind!=container_kind::multimap)return apply(input);
        else return visit([&](auto& table) {
            needs_growth=false;unsigned done=0;no_small_effects effects;
            while(done<input.size()) {
                unsigned end=done+1;
                while(end<input.size()&&input[end].key==input[done].key)++end;
                std::array<byte,grain*W> bytes;
                for(unsigned j=done;j<end;++j)copy_plain_bytes(bytes.data()+(j-done)*W,input[j].bytes);
                if(!consolidation_access<std::remove_cvref_t<decltype(table)>>::accept(table,input[done].key,
                        {bytes.data(),std::size_t(end-done)*W},combine{},effects)) {
                    if constexpr(std::is_same_v<std::remove_cvref_t<decltype(table)>,S>)
                        needs_growth=table.size()==256&&!table.get_bytes(input[done].key,[](const auto&){});
                    break;
                }
                done=end;
            }
            return done;
        });
    }
};

struct baseline {
    static constexpr bool native=false;
    using key_type=typename abseil<K>::key_type;
    using bag=absl::InlinedVector<value,1>;
    using mapped=std::conditional_t<Kind==container_kind::multimap,bag,value>;
    using Map=absl::flat_hash_map<key_type,mapped>;
    using Set=absl::flat_hash_set<key_type>;
    using storage_type=std::conditional_t<Kind==container_kind::set,Set,Map>;
    storage_type storage;unsigned epoch=0;
    explicit baseline(oa::Pool&) {}
    template<class Table> static unsigned insert(Table& table,std::span<const entry> input) {
        unsigned done=0;
        for(const auto& e:input) {
            if constexpr(Kind==container_kind::set){[[clang::always_inline]]table.insert(key_type(e.key));}
            else if constexpr(Kind==container_kind::map) {
                std::pair<typename Table::iterator,bool> p;
                [[clang::always_inline]]p=table.try_emplace(key_type(e.key),e.bytes);
                if(!p.second)p.first->second=combine{}(p.first->second,e.bytes);
            } else {
                std::pair<typename Table::iterator,bool> p;
                [[clang::always_inline]]p=table.try_emplace(key_type(e.key));p.first->second.push_back(e.bytes);
            }
            ++done;
        }
        return done;
    }
    unsigned apply(std::span<const entry> input){return insert(storage,input);}
    bool prepare(word,word) {
        // Abseil has no shared interpretation to migrate. Native reserve keeps
        // semantic contents usable if the coordinator cancels this epoch.
        storage.reserve(policy?storage.size()+batch_size:!epoch?192:512);return true;
    }
    record_scan_result step(unsigned){return {record_scan_state::done,0,0};}
    void cancel(){}
    void commit(){++epoch;}
    unsigned size()const{return storage.size();}
    template<class Table> static void each_impl(const Table& table,auto&& f) {
        if constexpr(Kind==container_kind::set)for(auto k:table)f(word(k),std::span<const byte,0>{});
        else for(const auto& [k,v]:table) {
            if constexpr(Kind==container_kind::map)f(word(k),v);
            else for(const auto& item:v)f(word(k),item);
        }
    }
    template<class F> void each(F&& f)const{each_impl(storage,f);}
    template<class Table> static memory inventory(const Table& table) {
        word bytes=absl::container_internal::AllocatedByteSize(table);
        if constexpr(Kind==container_kind::multimap)for(const auto& [k,v]:table) {
            const auto p=reinterpret_cast<std::uintptr_t>(v.data()),b=reinterpret_cast<std::uintptr_t>(&v);
            if(p<b||p>=b+sizeof(v))bytes+=v.capacity()*W;
        }
        return {bytes,0,sizeof(baseline)};
    }
    memory footprint()const{return inventory(storage);}
    word inactive_bytes()const{return 0;}
    struct cursor {typename storage_type::const_iterator row;unsigned offset=0;};
    void begin_scan(cursor& c)const{c={storage.cbegin(),0};}
    template<class Storage,class Cursor,class F> static value_scan_result scan_impl(const Storage& storage,Cursor& c,unsigned budget,F&& consume) {
        value_scan_result r;
        while(c.row!=storage.cend()&&r.work<budget) {
            if constexpr(Kind==container_kind::multimap) {
                const auto& [k,bag]=*c.row;
                const auto n=std::min({unsigned(bag.size()-c.offset),grain,budget-r.work});
                r.work+=n;
                if(!consume(word(k),std::span<const byte>(reinterpret_cast<const byte*>(bag.data()+c.offset),n*W)))
                    return {record_scan_state::refused,r.work,r.records,r.values};
                c.offset+=n;r.values+=n;
                if(c.offset==bag.size()){++c.row;c.offset=0;++r.records;}
            }else {
                ++r.work;
                const bool accepted=[&] {
                    if constexpr(Kind==container_kind::set)return consume(word(*c.row),std::span<const byte>{});
                    else return consume(word(c.row->first),std::span<const byte>(c.row->second));
                }();
                if(!accepted)return {record_scan_state::refused,r.work,r.records,r.values};
                ++c.row;++r.records;if constexpr(Kind==container_kind::map)++r.values;
            }
        }
        r.state=c.row==storage.cend()?record_scan_state::done:record_scan_state::yielded;return r;
    }
    template<class F> value_scan_result scan(cursor& c,unsigned budget,F&& consume)const {
        return scan_impl(storage,c,budget,consume);
    }
    template<class Storage> static unsigned fold_into(Storage& storage,std::span<const entry> input) {
        if constexpr(Kind!=container_kind::multimap)return insert(storage,input);
        else {
            unsigned done=0;
            while(done<input.size()) {
                unsigned end=done+1;
                while(end<input.size()&&input[end].key==input[done].key)++end;
                auto [p,inserted]=storage.try_emplace(key_type(input[done].key));
                std::array<value,grain> values;
                for(unsigned j=done;j<end;++j)values[j-done]=input[j].bytes;
                p->second.insert(p->second.end(),values.begin(),values.begin()+end-done);done=end;
            }
            return done;
        }
    }
    unsigned fold(std::span<const entry> input){return fold_into(storage,input);}
};

enum class operation {mutate,prepare,cancel,commit,inspect,inventory,scan_begin,scan_chunk,fold,retire};
template<class Table,bool Transfer=false> struct team {
    struct endpoint;
    struct ticket {
        // Coordinator polling must not invalidate the worker's mutation cursor
        // or Task state. The credit is touched only on the coordinator Lane.
        struct alignas(64) credit_state {bool available=true;unsigned count=0;} credit;
        endpoint* owner{};team* caller{};
        orbital::worker::Message dispatch,completion;
        loom::Task task;
        std::atomic<unsigned> references{1};
        operation op{};
        std::array<entry,batch_size> input{};
        unsigned count=0,done=0,keys=0,delay=0,work=0;
        int key_delta=0;
        word a=0,b=0;
        bool started=false,ready=false,refuse=false,needs_growth=false;
        memory bytes{};
        word inactive=0,pool_metadata=0,pool_mapped=0,arena_charged=0;
        std::vector<entry> output;
    };
    struct endpoint {
        loom::Worker worker;
        std::optional<Table> table;
        ticket batch;
        ticket* active=nullptr;
        std::atomic<bool> ready{false},stop{false};
        std::thread thread;
        word visits=0;
        struct empty_cursor {};
        [[no_unique_address]] std::conditional_t<Transfer,typename Table::cursor,empty_cursor> cursor;
    };
    orbital::worker::Lane coordinator;
    orbital::Ref<oa::Arena> arena;
    std::array<endpoint,2> owners;
    std::array<unsigned,2> population{};
    std::array<unsigned,2> owner_epoch{};
    std::array<word,2> operations{};
    // Coordinator-owned positions into a completed ticket's immutable input.
    // Prepare/cancel/commit messages never overwrite that input. Reuse begins
    // only after all prior Message references retire and credit is returned.
    std::array<unsigned,2> retry_begin{},retry_count{};
    bool growth_requested=false;
    word partial_returns=0,partial_prefixes=0,retried=0;
    unsigned epoch=0,cancellations=0,max_inflight=0;
    word offers=0,refused_offers=0,scan_work=0,poll_count=0;
    clock_type::time_point deadline=clock_type::now()+std::chrono::seconds(60);

    static void retain(orbital::worker::Message& message)noexcept {
        static_cast<ticket*>(message.context)->references.fetch_add(1,std::memory_order_relaxed);
    }
    static void release(orbital::worker::Message& message)noexcept {
        require(static_cast<ticket*>(message.context)->references.fetch_sub(1,std::memory_order_acq_rel)>1,"live message reference");
    }
    static orbital::Progress task_step(loom::Task& task)noexcept {
        auto& t=*static_cast<ticket*>(task.context);auto& table=*t.owner->table;
        if(t.delay){--t.delay;return orbital::Progress::yield();}
        switch(t.op) {
        case operation::mutate: {
            const auto n=std::min(grain,t.count-t.done);
            const auto before=table.size();
            const auto done=table.apply(std::span<const entry>(t.input.data()+t.done,n));
            t.done+=done;t.keys=table.size();t.key_delta+=int(t.keys)-int(before);
            if(done!=n) {
                if constexpr(policy==5&&std::is_same_v<Table,candidate>) {
                    require(table.needs_growth,"allocation refusal is not a growth request");
                    t.needs_growth=true;
                }
                return orbital::Progress::done();
            }
            if(t.done<t.count)return orbital::Progress::yield();
            break;
        }
        case operation::prepare: {
            if(!t.started) {
                t.started=true;if(t.refuse||!table.prepare(t.a,t.b))return orbital::Progress::done();
            }
            auto r=table.step(grain);require(r.work<=grain,"bounded prepared scan");t.work+=r.work;
            if(r.state==record_scan_state::yielded)return orbital::Progress::yield();
            t.ready=r.state==record_scan_state::done;break;
        }
        case operation::cancel:table.cancel();break;
        case operation::commit:table.commit();break;
        case operation::inspect:
        case operation::inventory:
            t.bytes=table.footprint();t.keys=table.size();
            t.inactive=table.inactive_bytes();
            {const auto stats=oa::statistics(*t.owner->worker.memory);
                t.pool_metadata=stats.metadata_bytes;t.pool_mapped=stats.mapped_bytes;
                t.arena_charged=t.caller->arena->charged_bytes.load(std::memory_order_acquire);}
            if(t.op==operation::inspect)
                table.each([&](word k,auto v){entry e{k,{}};std::copy(v.begin(),v.end(),e.bytes.begin());t.output.push_back(e);});
            break;
        case operation::scan_begin:
            if constexpr(Transfer)table.begin_scan(t.owner->cursor);
            else std::abort();
            break;
        case operation::scan_chunk:
            if constexpr(Transfer) {
                const auto r=table.scan(t.owner->cursor,grain,[&](word key,std::span<const byte> bytes) {
                    const unsigned n=[&]{if constexpr(Kind==container_kind::set)return 1u;else return unsigned(bytes.size()/W);}();
                    if(n>batch_size-t.done)return false;
                    for(unsigned j=0;j<n;++j) {
                        auto& e=t.input[t.done++];e.key=key;
                        if constexpr(W)copy_plain_bytes(e.bytes.data(),bytes.subspan(j*W,W));
                    }
                    return true;
                });
                require(r.work<=grain,"bounded source scan visit");t.work+=r.work;
                t.ready=r.state==record_scan_state::done;
                if(!t.ready&&r.state!=record_scan_state::refused&&t.done<batch_size)return orbital::Progress::yield();
            }else std::abort();
            break;
        case operation::fold:
            if constexpr(Transfer) {
                const auto n=std::min(grain,t.count-t.done),before=table.size();
                const auto done=table.fold(std::span<const entry>(t.input.data()+t.done,n));
                t.done+=done;t.keys=table.size();t.key_delta+=int(t.keys)-int(before);
                if(done!=n) {
                    if constexpr(Table::native){require(table.needs_growth,"fold allocation refusal is not growth");t.needs_growth=true;}
                    return orbital::Progress::done();
                }
                if(t.done<t.count)return orbital::Progress::yield();
            }else std::abort();
            break;
        case operation::retire:
            if constexpr(Transfer)t.owner->table.reset();
            else std::abort();
            break;
        }
        return orbital::Progress::done();
    }
    inline static const orbital::worker::MessageOps dispatch_ops{
        retain,
        [](orbital::worker::Message& message,orbital::worker::Lane& lane)noexcept {
            auto& t=*static_cast<ticket*>(message.context);auto& o=*t.owner;
            require(&lane==&o.worker.lane&&!o.active,"assigned single-credit owner");
            o.active=&t;require(loom::start(o.worker,t.task),"reserved Task admission");
        },release};
    inline static const orbital::worker::MessageOps completion_ops{
        retain,
        [](orbital::worker::Message& message,orbital::worker::Lane& lane)noexcept {
            auto& t=*static_cast<ticket*>(message.context);
            require(&lane==&t.caller->coordinator&&t.task.state==loom::TaskState::done,"completion after Task retirement");
        },
        [](orbital::worker::Message& message)noexcept {
            auto& t=*static_cast<ticket*>(message.context);release(message);
            require(t.references.load(std::memory_order_acquire)==1,"all queue touches retired");
            t.credit.available=true;
        }};

    explicit team(const std::array<int,2>& cpus,std::array<std::vector<entry>,2>* inspection=nullptr) {
        auto a=oa::open_arena(2ULL<<30);require(bool(a),"worker Arena");arena=std::move(*a);
        require(bool(orbital::worker::adopt(coordinator)),"coordinator Lane");
        for(unsigned i=0;i<2;++i) {
            auto& o=owners[i];o.batch.owner=&o;o.batch.caller=this;
            if(inspection)o.batch.output.swap((*inspection)[i]);
            o.batch.dispatch.context=&o.batch;o.batch.dispatch.ops=&dispatch_ops;
            o.batch.completion.context=&o.batch;o.batch.completion.ops=&completion_ops;
            o.thread=std::thread([&,i] {
                auto& local=owners[i];require(bool(loom::adopt(local.worker,cpus[i])),"owner thread affinity");
                require(bool(loom::configure(local.worker,arena,1)),"one accepted Task per owner");
                local.table.emplace(*local.worker.memory);local.ready.store(true,std::memory_order_release);
                while(!local.stop.load(std::memory_order_acquire)) {
                    local.visits+=loom::poll(local.worker,{4,1});
                    if(local.active&&local.active->task.state==loom::TaskState::done) {
                        auto* t=local.active;local.active=nullptr;
                        orbital::worker::post(coordinator,t->completion);
                    }
                }
                require(!local.active,"no accepted work at shutdown");local.table.reset();
                while(loom::shutdown(local.worker).kind()!=orbital::Progress::Kind::done)loom::poll(local.worker,{8,2});
                loom::relinquish(local.worker);
            });
        }
        while(!owners[0].ready.load(std::memory_order_acquire)||!owners[1].ready.load(std::memory_order_acquire))poll();
    }
    void poll() {
        orbital::worker::poll(coordinator,8);
        if(!(++poll_count&65535))require(clock_type::now()<deadline,"worker progress deadline");
    }
    bool idle()const{return owners[0].batch.credit.available&&owners[1].batch.credit.available;}
    void drain(){while(!idle())poll();}
    bool offer(unsigned who,operation op,std::span<const entry> input={},bool refuse=false,unsigned delay=0) {
        auto& t=owners[who].batch;
        if(!t.credit.available){++refused_offers;return false;}
        require(t.output.empty()&&input.size()<=batch_size,"bounded empty ticket");
        t.task=loom::Task{};t.task.context=&t;t.task.step=task_step;
        t.op=op;t.count=input.size();t.done=t.work=0;t.key_delta=0;t.started=t.ready=t.needs_growth=false;t.refuse=refuse;t.delay=delay;
        // Deterministic caller-supplied entropy fixture; no production seed policy.
        t.a=mix(owner_epoch[who]+818);t.b=mix(owner_epoch[who]+991);
        if constexpr(policy==5) {
            // A resumed suffix can overlap its ticket's destination prefix.
            if(!input.empty())std::memmove(t.input.data(),input.data(),input.size_bytes());
        }else std::copy(input.begin(),input.end(),t.input.begin());
        t.credit.available=false;t.credit.count=t.count;++offers;orbital::worker::post(owners[who].worker.lane,t.dispatch);
        max_inflight=std::max(max_inflight,unsigned(!owners[0].batch.credit.available)+unsigned(!owners[1].batch.credit.available));
        return true;
    }
    void harvest(unsigned who) {
        auto& t=owners[who].batch;require(t.credit.available,"harvest completed ticket");
        if(t.op==operation::mutate){
            if constexpr(policy==5)if(t.done!=t.count) {
                require(t.needs_growth&&epoch==0&&!retry_count[who],"one retained suffix at compact-list boundary");
                retry_begin[who]=t.done;retry_count[who]=t.count-t.done;
                growth_requested=true;++partial_returns;partial_prefixes+=t.done!=0;
            }
            if constexpr(policy!=5)require(t.done==t.count,"complete admitted mutation");
            operations[who]+=t.done;t.count=t.done=0;
            population[who]+=t.key_delta;t.key_delta=0;require(population[who]==t.keys,"caller-held occupancy delta");}
    }
    bool replay(unsigned who) {
        require(retry_count[who]&&owners[who].batch.credit.available,"retired retry ticket");
        auto& t=owners[who].batch;const auto count=retry_count[who],begin=retry_begin[who];
        if(!offer(who,operation::mutate,std::span<const entry>(t.input.data()+begin,count)))return false;
        retried+=count;retry_count[who]=retry_begin[who]=0;return true;
    }
    void grow(bool cancel_once=false) {
        drain();for(unsigned i=0;i<2;++i)harvest(i);
        auto prepare=[&](bool refuse) {
            for(unsigned i=0;i<2;++i)require(offer(i,operation::prepare,{},refuse&&i==1,i==1?17:0),"prepare offer");
            drain();for(auto& o:owners)scan_work+=o.batch.work;
        };
        if(cancel_once) {
            prepare(true);require(owners[0].batch.ready&&!owners[1].batch.ready,"paired refusal after first ready");
            for(unsigned i=0;i<2;++i)require(offer(i,operation::cancel),"cancel offer");drain();++cancellations;
        }
        prepare(false);for(auto& o:owners)require(o.batch.ready,"all replacement owners ready");
        for(unsigned i=0;i<2;++i)require(offer(i,operation::commit),"commit offer");drain();++epoch;
        owner_epoch.fill(epoch);
        growth_requested=false;
    }
    void grow_destination(unsigned who,bool cancel_once=false) requires(Transfer) {
        drain();
        const auto prepare=[&](bool refuse) {
            require(offer(who,operation::prepare,{},refuse,refuse?7:0),"private destination prepare");
            drain();scan_work+=owners[who].batch.work;
        };
        if(cancel_once) {
            prepare(false);require(owners[who].batch.ready,"cancel a fully prepared private destination");
            require(offer(who,operation::cancel),"private destination cancel");drain();++cancellations;
            prepare(true);require(!owners[who].batch.ready,"forced preparation refusal");
            require(offer(who,operation::cancel),"cancel refused destination");drain();
        }
        prepare(false);require(owners[who].batch.ready,"private destination ready");
        require(offer(who,operation::commit),"private destination commit");drain();++owner_epoch[who];
    }
    void close() {
        drain();for(auto& o:owners)o.stop.store(true,std::memory_order_release);
        for(auto& o:owners)o.thread.join();
        require(!orbital::worker::pending(coordinator)&&arena->charged_bytes.load()==0,"drained storage and Messages");
        orbital::worker::relinquish(coordinator);
    }
};

using expected_type=std::map<word,std::vector<value>>;
void accumulate(expected_type& out,const entry& e) {
    auto& values=out[e.key];
    if constexpr(Kind==container_kind::set){if(values.empty())values.emplace_back();}
    else if constexpr(Kind==container_kind::map) {
        if(values.empty())values.push_back(e.bytes);
        else for(unsigned j=0;j<W;++j)values[0][j]^=e.bytes[j]; // independent scalar oracle
    }else values.push_back(e.bytes);
}
std::vector<entry> input(unsigned n,bool skew,unsigned passes=8) {
    std::vector<entry> out;out.reserve(n*passes);
    for(unsigned repeat=0;repeat<passes;++repeat)for(unsigned i=0;i<n;++i) {
        const auto rank=skew&&repeat&&i%5?0:i;
        entry e{word(rank)*0xd6e8feb86659fd93ULL&mask(K),{}};
        for(unsigned j=0;j<W;++j)e.bytes[j]=byte(i*37+repeat*19+j*113);out.push_back(e);
    }
    // Shuffle each pass independently; every key exists before the skewed tail.
    std::mt19937_64 rng(727);for(unsigned r=0;r<passes;++r)std::shuffle(out.begin()+r*n,out.begin()+(r+1)*n,rng);
    return out;
}
std::array<int,2> cpus() {
    cpu_set_t allowed;CPU_ZERO(&allowed);require(!sched_getaffinity(0,sizeof(allowed),&allowed),"allowed worker CPUs");
    std::array<int,3> selected;unsigned n=0;for(int i=0;i<CPU_SETSIZE&&n<3;++i)if(CPU_ISSET(i,&allowed))selected[n++]=i;
    require(n==3,"two worker CPUs and one coordinator CPU");
    cpu_set_t coordinator;CPU_ZERO(&coordinator);CPU_SET(selected[2],&coordinator);
    require(!sched_setaffinity(0,sizeof(coordinator),&coordinator),"pin coordinator separately");
    return {selected[0],selected[1]};
}
} // namespace ht::epochs

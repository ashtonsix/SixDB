#include "promoted.h"
#include "small_baseline.h"
#include "table.h"
#include <absl/container/internal/hashtable_debug.h>
#include <loom/ring.h>
#include <loom/hardware.h>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <random>
#ifndef HT_STAGED_BAGS
#define HT_STAGED_BAGS 0
#endif
#ifndef HT_STAGED_A
#define HT_STAGED_A 10
#endif
#ifndef HT_STAGED_KIND
#define HT_STAGED_KIND 1
#endif
#ifndef HT_STAGED_WIDTH
#define HT_STAGED_WIDTH 8
#endif
#ifndef HT_STAGED_KEY
#define HT_STAGED_KEY 40
#endif
#ifndef HT_STAGED_UNIT
#define HT_STAGED_UNIT 0
#endif
#ifndef HT_STAGED_N
#define HT_STAGED_N 6144
#endif
#ifndef HT_STAGED_GRAPH_HINT
#define HT_STAGED_GRAPH_HINT 0
#endif
#ifndef HT_STAGED_CAPACITY
#define HT_STAGED_CAPACITY 0
#endif
#ifndef HT_STAGED_QUANTUM
#define HT_STAGED_QUANTUM 8
#endif
#ifndef HT_STAGED_NO_WORD
#define HT_STAGED_NO_WORD 0
#endif
namespace ht::staged_test {
constexpr auto Kind=container_kind(HT_STAGED_KIND);
constexpr unsigned K=HT_STAGED_KEY,W=Kind==container_kind::set?0:HT_STAGED_WIDTH;
constexpr unsigned A=HT_STAGED_A,B=HT_STAGED_CAPACITY?A-2:A>4?A-4:0;
constexpr unsigned Budget=HT_STAGED_CAPACITY?(Kind==container_kind::multimap&&W<=16?16:8):(W<=8?16:64);
static_assert(!HT_STAGED_CAPACITY || HT_STAGED_CAPACITY==14 || HT_STAGED_CAPACITY==28);
constexpr bool WordControl=!HT_STAGED_NO_WORD && Kind!=container_kind::multimap && W<=8;
constexpr bool IndirectValue=W>0 && W<4;
using Bags=std::conditional_t<HT_STAGED_BAGS==1,buffer_bags<(W?W:8)>,
    std::conditional_t<HT_STAGED_BAGS==2,pooled_vector_bags<(W?W:8),true,256>,linked_bags<(W?W:8),true,256>>>;
using Small=small_table<K,Kind,W,true,Bags,Budget>;
using Geometry=record_table<K,Kind,Small::stored_bytes,A,B,128,128,
    record_format::exact,false,record_access::both,Small::tag_bits>;
constexpr unsigned Unit=HT_STAGED_CAPACITY?Geometry::unit_for_slots(HT_STAGED_CAPACITY?HT_STAGED_CAPACITY:1,false,HT_STAGED_QUANTUM):
    HT_STAGED_UNIT?HT_STAGED_UNIT:Kind==container_kind::multimap?(W<=8?192:320):(W>8?192:128);
constexpr unsigned SecondaryUnit=HT_STAGED_CAPACITY?Geometry::unit_for_slots(HT_STAGED_CAPACITY==14?5:8,true,HT_STAGED_QUANTUM):Unit;
using Body=promoted_table<K,Kind,W,A,B,Unit,SecondaryUnit,record_format::exact,false,record_access::both,true,Bags,Budget>;
using Index=record_table<K,Kind,Small::stored_bytes,A,B,Unit,SecondaryUnit,
    record_format::exact,false,record_access::both,Small::tag_bits>;
using Bytes=std::array<byte,W>;
using Key=typename abseil<K>::key_type;
using Bag=absl::InlinedVector<Bytes,1>;
using Baseline=std::conditional_t<Kind==container_kind::set,absl::flat_hash_set<Key>,
    absl::flat_hash_map<Key,std::conditional_t<Kind==container_kind::map,Bytes,Bag>>>;
using WordBody=table<K,A,B,(WordControl?Unit:128),(WordControl?SecondaryUnit:128),false,false,(W<=8?W:8)>;
word digest(std::span<const byte,W> value) {
    word out=0;lanes<W/8>([&](auto i){word v;std::memcpy(&v,value.data()+i*8,8);out^=v;});
    if constexpr(W%8){word v=0;std::memcpy(&v,value.data()+W/8*8,W%8);out^=v;}return out;
}
word key(unsigned n){return hash_mask<K>(word(n)*0xd6e8feb86659fd93ULL);}
struct answer {word sum=0,values=0,hits=0,terminal=0;bool operator==(const answer&)const=default;};
struct input {
    unsigned n,requests;bool mixed;std::vector<std::array<unsigned,2>> next;
    std::size_t hint_page=0,hint_line=0;
    std::array<unsigned,33> boundaries{};answer expected{};
    static unsigned fanout(unsigned i) {
        if constexpr(Kind!=container_kind::multimap)return 1;
        const auto x=mix(i+317)%100;return x<75?1:x<90?2:x<96?4:x<99?16:64;
    }
    template<class Visit> void each_value(unsigned i,Visit&& visit)const {
        word correction=0;const auto count=fanout(i);
        for(unsigned j=0;j<count;++j) {
            Bytes value;for(unsigned b=0;b<W;++b)value[b]=byte(mix(word(i)+(word(j)<<32)+b*717));
            if constexpr(W>=4) {
                correction^=digest(value);
                if(j+1==count){correction^=next[i][1];for(unsigned b=0;b<std::min(W,8u);++b)value[b]^=byte(correction>>(8*b));}
            }
            visit(value);
        }
    }
    input(unsigned size,bool misses):n(size),requests(size*(misses?2:1)),mixed(misses),next(requests) {
        require(n>=32 && word(n)*(misses?2:1)<=std::numeric_limits<unsigned>::max(),"bounded request graph");
        require(K>=32 || word(requests)<=word(1)<<K,"distinct requests fit key domain");
        std::vector<unsigned> order(requests);std::iota(order.begin(),order.end(),0);
        std::mt19937 rng(713);std::shuffle(order.begin(),order.end(),rng);
        for(auto& s:next){s[0]=rng()%requests;s[1]=rng()%requests;}
        for(unsigned j=0;j<requests;++j)next[order[j]][order[j]<n]=order[(j+1)%requests];
        for(unsigned j=0;j<=32;++j)boundaries[j]=order[(std::size_t(j)*requests/32)%requests];
        expected.hits=n;
        for(unsigned i=0;i<n;++i){word edge=0;
            each_value(i,[&](const Bytes& v){const auto d=digest(v);expected.sum+=d;edge^=d;++expected.values;});
            // Narrow values cannot encode an arbitrary large successor rank.
            // Both contenders consume their real digest into a common external
            // successor. Wide values carry the successor without that load.
            if constexpr(IndirectValue)next[i][1]^=unsigned(edge);
        }
    }
    void release_unused_graph() {if constexpr(W>=4)if(!mixed)std::vector<std::array<unsigned,2>>{}.swap(next);}
    [[gnu::always_inline]] void prefetch_successor(unsigned rank)const {
        // Both possible successor columns occupy one aligned eight-byte cell.
        // Its address is known before membership; no extra dependent stage is
        // needed. This is caller-owned input, not a table-emitted read window.
        if constexpr(HT_STAGED_GRAPH_HINT) {
            if constexpr(W>=4)if(!mixed)return; // Released in the all-hit graph.
            loom::prefetch<loom::Hint::l1_1>(next.data()+rank,hint_page,hint_line);
        }
    }
    template<unsigned C> answer oracle()const {
        auto out=expected;for(unsigned j=0;j<C;++j)out.terminal^=boundaries[(j+1)*32/C];return out;
    }
    word graph_bytes()const{return next.capacity()*sizeof(next[0])+sizeof(*this);}
};
struct lane {unsigned rank=0,left=0;word sum=0,values=0,hits=0,edge=0;};
template<class Read> struct job:lane {typename Read::probe probe;};
template<class Read> struct context {Read read;const input* source;std::size_t page,line;};
template<class Read> using Scheduled=typename loom::Ring<job<Read>*>::template WithShared<const context<Read>*>;
template<bool Hint> auto emitter(std::size_t page,std::size_t line) {return [=](const byte* p,std::size_t n) __attribute__((always_inline)) {
    if constexpr(Hint)if(n){const auto start=reinterpret_cast<std::uintptr_t>(p),end=start+n;
        loom::prefetch<loom::Hint::l1_1>(p,page,line);for(auto at=(start&~std::uintptr_t(line-1))+line;at<end;at+=line)loom::prefetch<loom::Hint::l1_1>(reinterpret_cast<const byte*>(at),page,line);}
};}
void accept(lane& j,bool found,const input& source){j.hits+=found;
    if constexpr(Kind==container_kind::set)j.rank=source.next[j.rank][found];
    else if constexpr(IndirectValue)j.rank=source.next[j.rank][found]^unsigned(j.edge);
    else j.rank=found?unsigned(j.edge):source.next[j.rank][0];
    j.edge=0;--j.left;}
template<bool Hint,class Read> LOOM_NATIVE_CC void visit(typename Scheduled<Read>::Cursor ring,
    typename Scheduled<Read>::Slot* slot,const context<Read>* c,job<Read>* j);
template<bool Hint,class Read> [[gnu::noinline]] LOOM_NATIVE_CC void resolve(typename Scheduled<Read>::Cursor ring,
    typename Scheduled<Read>::Slot* slot,const context<Read>* c,job<Read>* j) {
    bool found=false;auto emit=emitter<Hint>(c->page,c->line);
    if(c->read.advance_read(j->probe,key(j->rank),found,[&] [[gnu::always_inline]] (auto b){
        const auto v=digest(b);j->sum+=v;j->edge^=v;++j->values;
    },emit)) {
        accept(*j,found,*c->source);
        if(!j->left)[[clang::musttail]] return Scheduled<Read>::finish(ring,slot,c,j);
        if constexpr(Hint)c->source->prefetch_successor(j->rank);
        c->read.prepare_read(j->probe,key(j->rank),emit);
    }
    [[clang::musttail]] return Scheduled<Read>::yield_unchanged(ring,slot,c,j);
}
template<bool Hint,class Read> LOOM_NATIVE_CC void visit(typename Scheduled<Read>::Cursor ring,
    typename Scheduled<Read>::Slot* slot,const context<Read>* c,job<Read>* j) {
    if(Read::cold_path(j->probe))[[clang::musttail]] return resolve<Hint,Read>(ring,slot,c,j);
    bool found=false;auto emit=emitter<Hint>(c->page,c->line);
    if(c->read.template advance_read<false>(j->probe,key(j->rank),found,[&] [[gnu::always_inline]] (auto b){const auto v=digest(b);j->sum+=v;j->edge^=v;++j->values;},emit)) {
        accept(*j,found,*c->source);
        if(!j->left)[[clang::musttail]] return Scheduled<Read>::finish(ring,slot,c,j);
        if constexpr(Hint)c->source->prefetch_successor(j->rank);
        c->read.prepare_read(j->probe,key(j->rank),emit);
    }
    [[clang::musttail]] return Scheduled<Read>::yield_unchanged(ring,slot,c,j);
}
template<unsigned C,class Jobs> void initialize(Jobs& jobs,const input& source) {
    for(unsigned i=0;i<C;++i){jobs[i].rank=source.boundaries[i*32/C];
        jobs[i].left=std::size_t(i+1)*source.requests/C-std::size_t(i)*source.requests/C;}
}
template<class Jobs> answer result(const Jobs& jobs) {answer out;for(const auto& j:jobs){out.sum+=j.sum;out.values+=j.values;out.hits+=j.hits;out.terminal^=j.rank;}return out;}
template<unsigned C,bool Hint=false,class Read> [[gnu::noinline]] answer direct(const Read& read,const input& source) {
    std::array<lane,C> jobs;initialize<C>(jobs,source);unsigned active=C;
    if constexpr(Hint)for(const auto& j:jobs){source.prefetch_successor(j.rank);read.prefetch(key(j.rank));}
    while(active)for(auto& j:jobs)if(j.left){const bool found=read.get_bytes(key(j.rank),[&] [[gnu::always_inline]] (auto b){const auto v=digest(b);j.sum+=v;j.edge^=v;++j.values;});
        accept(j,found,source);if(!j.left)--active;else if constexpr(Hint){source.prefetch_successor(j.rank);read.prefetch(key(j.rank));}}
    return result(jobs);
}
template<unsigned C,bool Hint,class Read> [[gnu::noinline]] answer manual(const Read& read,const input& source,std::size_t page,std::size_t line) {
    std::array<job<Read>,C> jobs;initialize<C>(jobs,source);auto emit=emitter<Hint>(page,line);
    for(auto& j:jobs){if constexpr(Hint)source.prefetch_successor(j.rank);read.prepare_read(j.probe,key(j.rank),emit);}unsigned active=C;
    while(active)for(auto& j:jobs)if(j.left){bool found=false;
        if(read.advance_read(j.probe,key(j.rank),found,[&] [[gnu::always_inline]] (auto b){const auto v=digest(b);j.sum+=v;j.edge^=v;++j.values;},emit)) {
            accept(j,found,source);if(!j.left)--active;else {if constexpr(Hint)source.prefetch_successor(j.rank);read.prepare_read(j.probe,key(j.rank),emit);}}}
    return result(jobs);
}
template<unsigned C,bool Hint,class Read> [[gnu::noinline]] answer scheduled(const context<Read>& c,Scheduled<Read>& ring) {
    std::array<job<Read>,C> jobs;initialize<C>(jobs,*c.source);auto emit=emitter<Hint>(c.page,c.line);
    for(auto& j:jobs){if constexpr(Hint)c.source->prefetch_successor(j.rank);c.read.prepare_read(j.probe,key(j.rank),emit);require(ring.push(visit<Hint,Read>,{&j}),"staged Ring admission");}
    while(ring.pending())ring.spin({4096});return result(jobs);
}
struct baseline_reader {
    const Baseline* body;
    [[gnu::always_inline]] void prefetch(word k)const {[[clang::always_inline]] body->prefetch(Key(k));}
    template<class Visit> [[gnu::always_inline]] bool get_bytes(word k,Visit&& visit)const {
        typename Baseline::const_iterator at;[[clang::always_inline]] at=body->find(Key(k));
        if(at==body->end())return false;consume(*at,visit);return true;
    }
    template<class Row,class Visit> [[gnu::always_inline]] static void consume(const Row& row,Visit&& visit) {
        if constexpr(Kind==container_kind::set){const Bytes value{};visit(std::span<const byte,W>(value));}
        else if constexpr(Kind==container_kind::map)visit(std::span<const byte,W>(row.second));
        else for(const auto& value:row.second)visit(std::span<const byte,W>(value));
    }
};
// Same graph, value consumer and scheduling as the generic owner. The old word
// layout remains an independent implementation/control, not a new public mode.
template<class T> struct word_reader {
    typename T::reader read;
    using probe=typename T::reader::probe;
    static bool cold_path(const probe& p){return p.next==T::reader::stage::chain_directory || p.next==T::reader::stage::chain_payload;}
    template<class Visit> [[gnu::always_inline]] bool get_bytes(word k,Visit&& visit)const {
        word value=0;const bool found=read.get_value(k,value);
        if(found)visit(std::span<const byte,W>(reinterpret_cast<const byte*>(&value),W));return found;
    }
    template<class Emit> [[gnu::always_inline]] void prepare_read(probe& p,word k,Emit&& emit)const {read.prepare_read(p,k,emit);}
    template<bool Resolve=true,class Visit,class Emit> [[gnu::always_inline]] bool advance_read(
            probe& p,word k,bool& found,Visit&& visit,Emit&& emit)const {
        // This consumer carries a complete byte digest as well as membership.
        // Keep rare key-overflow iteration out of the ready callback's register
        // pressure, as in the generic reader. No additional Ring stage is added.
        if constexpr(!Resolve)__builtin_assume(p.next==T::reader::stage::primary || p.next==T::reader::stage::secondary);
        word value=0;const bool done=read.template advance_read<Resolve>(p,k,value,found,emit);
        if(done&&found)visit(std::span<const byte,W>(reinterpret_cast<const byte*>(&value),W));return done;
    }
};
template<class B> void put_baseline(B& body,word k,const Bytes& v) {
    if constexpr(Kind==container_kind::set)body.insert(Key(k));
    else if constexpr(Kind==container_kind::map)body.insert_or_assign(Key(k),v);
    else body[Key(k)].push_back(v);
}
template<class T,class Visit> void storage(const T& body,Visit&& visit) {
    if constexpr(requires{body.read_storage(visit);})body.read_storage(visit);
    else {visit(body.directory_storage());body.allocations([&](const oa::Allocation& a){visit(std::span<const byte>(reinterpret_cast<const byte*>(a.address),a.size));});}
}
struct traversal {word visits=0,hint_bytes=0,hint_windows=0;};
template<class T,class Read> traversal inspect(const T& body,const Read& read,const input& source) {
    std::vector<std::pair<std::uintptr_t,std::uintptr_t>> ranges;
    storage(body,[&](auto w){if(w.size()){auto at=reinterpret_cast<std::uintptr_t>(w.data());ranges.emplace_back(at,at+w.size());}});
    std::sort(ranges.begin(),ranges.end());std::uintptr_t end=0;
    for(auto& r:ranges){end=std::max(end,r.second);r.second=end;}
    traversal stats;
    auto emit=[&](const byte* p,std::size_t n){const auto at=reinterpret_cast<std::uintptr_t>(p);
        const auto after=std::upper_bound(ranges.begin(),ranges.end(),at,[](auto p,auto r){return p<r.first;});
        require(n && after!=ranges.begin() && at<=std::prev(after)->second && n<=std::prev(after)->second-at,"emitted window belongs to one live extent");
        stats.hint_bytes+=n;++stats.hint_windows;};
    for(unsigned i=0;i<source.requests;++i){typename Read::probe p;std::vector<Bytes> got,expected;
        if(i<source.n)source.each_value(i,[&](auto v){expected.push_back(v);});
        read.prepare_read(p,key(i),emit);bool found=false;unsigned steps=0;
        do{++steps;++stats.visits;require(steps<10000,"bounded staged completion");}
        while(!read.advance_read(p,key(i),found,[&](auto b){Bytes v;std::copy(b.begin(),b.end(),v.begin());got.push_back(v);},emit));
        require(found==(i<source.n)&&got==expected,"independent complete bytes and multiplicity");
    }
    return stats;
}
void snapshot(const Body& body,const Baseline& baseline,const WordBody* old,const input& source,const char* phase) {
    const char* path=std::getenv("HT_STAGED_RESIDENCE");if(!path)return;
    const auto root=std::filesystem::path(path);std::filesystem::create_directories(root);
    const auto stem=std::to_string(K)+"-"+std::to_string(W)+"-"+std::to_string(unsigned(Kind))+"-"+
        std::to_string(source.n)+"-"+std::to_string(source.mixed)+"-"+phase;
    for(const char* leaf:{"smaps","smaps_rollup","numa_maps","status"}) {
        std::ifstream in(std::string("/proc/self/")+leaf);std::ofstream out(root/(stem+"."+leaf));
        require(bool(in)||std::string_view(leaf)=="numa_maps","process receipt input");
        if(in)out<<in.rdbuf();require(bool(out),"process receipt output");
    }
    std::ofstream out(root/(stem+".owners.tsv"));out<<"owner\taddress\tinitialized_bytes\n";
    auto owner=[&](const char* name,auto w){out<<name<<'\t'<<static_cast<const void*>(w.data())<<'\t'<<w.size()<<'\n';};
    storage(body,[&](auto w){owner("generic",w);});
    if(old)storage(*old,[&](auto w){owner("word",w);});
    out<<"abseil_anchor\t"<<static_cast<const void*>(&*baseline.begin())<<'\t'<<sizeof(*baseline.begin())<<'\n';
    owner("successors",std::as_bytes(std::span(source.next)));
    require(bool(out),"owner receipt output");
}
template<class B> memory baseline_memory(const B& b) {
    word bytes=absl::container_internal::AllocatedByteSize(b);
    if constexpr(Kind==container_kind::multimap)for(const auto& [k,v]:b){
        const auto p=reinterpret_cast<std::uintptr_t>(v.data()),base=reinterpret_cast<std::uintptr_t>(&v);
        if(p<base||p>=base+sizeof(v))bytes+=v.capacity()*W;}
    return {bytes,0,sizeof(B)};
}
void row(const char* owner,const char* method,const input& source,unsigned c,unsigned rep,word requests,double ns,
         memory bytes,word jobs,word ring,word visits,traversal stats={}) {
    std::printf("%u,%u,%u,%u,%u,%u,%u,%u,%u,%s,%s,%u,%llu,%.6f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu\n",
        K,W,unsigned(Kind),A,B,Unit,source.n,unsigned(source.mixed),c,owner,method,rep,
        (unsigned long long)requests,ns,(unsigned long long)bytes.requested,(unsigned long long)bytes.backing,
        (unsigned long long)bytes.metadata,(unsigned long long)jobs,(unsigned long long)source.graph_bytes(),
        (unsigned long long)ring,(unsigned long long)visits,unsigned(HT_STAGED_GRAPH_HINT),SecondaryUnit,
        unsigned(HT_STAGED_CAPACITY),unsigned(HT_RECORD_READ_WINDOWS),unsigned(WordControl),Budget,
        Index::primary.shape.slots,Index::secondary.shape.slots,
        (unsigned long long)stats.hint_bytes,(unsigned long long)stats.hint_windows);std::fflush(stdout);
}
template<unsigned C,class Execute> double time_checked(const input& source,word graphs,Execute&& execute) {
    const auto expected=source.template oracle<C>();require(execute()==expected,"complete warm graph");
    answer out;const auto start=clock_type::now();
    for(word g=0;g<graphs;++g){asm volatile("":::"memory");auto one=execute();out.sum+=one.sum;out.values+=one.values;out.hits+=one.hits;out.terminal^=one.terminal;}
    const double ns=std::chrono::duration<double,std::nano>(clock_type::now()-start).count()/(source.requests*graphs);
    require(out==answer{expected.sum*graphs,expected.values*graphs,expected.hits*graphs,(graphs&1)?expected.terminal:0},"complete timed graph");return ns;
}
template<unsigned C,class Read> void measure(loom::Worker& worker,const char* owner,const Read& read,const input& source,
        memory bytes,traversal stats,unsigned rep,word graphs) {
    context c{read,&source,worker.hints.profile.geometry.page_bytes,worker.hints.profile.geometry.line_bytes};
    Scheduled<Read> ring;require(bool(ring.configure(worker,{C,0},{&c})),"staged Ring configure");
    const char* names[]={"direct","manual","manual_hints","ring","ring_hints"};
    for(unsigned order=0;order<5;++order){const unsigned arm=(order+rep)%5;
        auto execute=[&]{switch(arm){case 0:return direct<C>(read,source);case 1:return manual<C,false>(read,source,c.page,c.line);
            case 2:return manual<C,true>(read,source,c.page,c.line);case 3:return scheduled<C,false>(c,ring);default:return scheduled<C,true>(c,ring);}};
        const auto before=ring.counters().visits;const auto ns=time_checked<C>(source,graphs,execute);
        require(ring.counters().visits-before==(arm>=3?stats.visits*(graphs+1):0),"complete Ring visit count");
        row(owner,names[arm],source,C,rep,source.requests*graphs,ns,bytes,arm?sizeof(job<Read>)*C:sizeof(lane)*C,
            arm>=3?ring.storage_bytes():0,arm>=3?stats.visits*graphs:0,stats);
    }
    require(bool(ring.close()),"staged Ring close");
}
template<unsigned C> void compare(loom::Worker& worker,const Body& body,const WordBody* old,const Baseline& baseline,
        const input& source,traversal generic_visits,traversal word_visits,unsigned reps,word minimum) {
    const auto graphs=std::max(word(1),(minimum+source.requests-1)/source.requests);
    for(unsigned rep=0;rep<reps;++rep)for(unsigned order=0;order<(WordControl?3:2);++order) {
        const unsigned contender=(rep+order)%(WordControl?3:2);
        if(contender==0)for(unsigned j=0;j<2;++j){const bool hint=(rep+j)%2;auto read=baseline_reader{&baseline};
            auto execute=[&]{return hint?direct<C,true>(read,source):direct<C>(read,source);};
            row("abseil",hint?"prefetch":"direct",source,C,rep,source.requests*graphs,time_checked<C>(source,graphs,execute),
                baseline_memory(baseline),sizeof(lane)*C,0,0);
        }else if(contender==1)measure<C>(worker,"generic",body.bind_read(),source,body.footprint(),generic_visits,rep,graphs);
        else if constexpr(WordControl)measure<C>(worker,"word",word_reader<WordBody>{old->bind_read()},source,old->footprint(),word_visits,rep,graphs);
    }
}
void run(loom::Worker& worker,unsigned n,unsigned reps,bool mixed,word minimum,unsigned concurrency) {
    input source(n,mixed);Small small(*worker.memory,19);
    source.hint_page=worker.hints.profile.geometry.page_bytes;
    source.hint_line=worker.hints.profile.geometry.line_bytes;
    auto body=Body::promote(small,*worker.memory,41,73);require(bool(body),"staged owner creation");
    std::optional<WordBody> old;if constexpr(WordControl)old.emplace(*worker.memory,41,73);
    Baseline baseline;baseline.reserve(n);
    for(unsigned i=0;i<n;++i)source.each_value(i,[&](const auto& v){
        body->set_bytes(key(i),v);put_baseline(baseline,key(i),v);
        if constexpr(WordControl)require(old->set(key(i),digest(v))==1,"word control insertion");});
    if constexpr(A==3&&K>8)require(body->population()[0]&&body->population()[1]&&body->population()[2],"forced primary/secondary/overflow populations");
    const auto generic_visits=inspect(*body,body->bind_read(),source);traversal word_visits;
    if constexpr(WordControl)word_visits=inspect(*old,word_reader<WordBody>{old->bind_read()},source);
    source.release_unused_graph();snapshot(*body,baseline,old?&*old:nullptr,source,"before");
    if(!concurrency || concurrency==1)compare<1>(worker,*body,old?&*old:nullptr,baseline,source,generic_visits,word_visits,reps,minimum);
    if(!concurrency || concurrency==32)compare<32>(worker,*body,old?&*old:nullptr,baseline,source,generic_visits,word_visits,reps,minimum);
    snapshot(*body,baseline,old?&*old:nullptr,source,"after");
}
}
int main(int argc,char** argv){using namespace ht;using namespace ht::staged_test;
    const bool checking=argc<2||std::string_view(argv[1])=="check";
    require(checking||std::string_view(argv[1])=="bench","known staged mode");
    const unsigned n=checking?(K==8?96:A==3?300:128):argc>2?std::stoul(argv[2]):HT_STAGED_N;
    const unsigned reps=checking?1:argc>3?std::stoul(argv[3]):3;
    const int graph=checking?-1:argc>4?std::stoi(argv[4]):-1;require(graph>=-1&&graph<=1&&reps,"graph and repetitions");
    const word minimum=checking?0:argc>5?std::stoull(argv[5]):262144;
    const unsigned concurrency=checking?0:argc>6?std::stoul(argv[6]):0;
    require(concurrency==0||concurrency==1||concurrency==32,"known concurrency control");
    if(n>=1000000)require(std::getenv("HT_STAGED_RESIDENCE"),"large run requires residence receipts");
    auto hardware=loom::discover_hardware();require(bool(hardware),"hardware discovery");
    loom::HardwareOptions options;options.calibrate=false;loom::HardwareService service(options);
    auto profile=service.request(std::move(*hardware));require(bool(profile),"hardware profile");
    loom::Worker worker;require(bool(loom::adopt(worker)),"generic worker adoption");
    auto arena=oa::open_arena(32ULL<<30);require(bool(arena),"generic arena");
    require(bool(loom::configure(worker,std::move(*arena),0,*profile)),"generic worker configure");
    std::puts("K,W,kind,A,B,unit,keys,mixed,concurrency,owner,method,rep,requests,ns_per_request,requested,backing,metadata,jobs_bytes,graph_bytes,ring_bytes,ring_visits,graph_hints,secondary_unit,capacity_family,read_windows,word_control,inline_bytes,primary_slots,secondary_slots,hint_bytes_per_graph,hint_windows_per_graph");
    for(bool mixed:{false,true})if(graph<0||graph==int(mixed))run(worker,n,reps,mixed,minimum,concurrency);
    auto deadline=clock_type::now()+std::chrono::seconds(10);
    while(loom::shutdown(worker).kind()!=orbital::Progress::Kind::done) {
        require(clock_type::now()<deadline,"generic worker drain");loom::poll(worker,{1024,0});
    }
    loom::relinquish(worker);
}

#include <benchmark/benchmark.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <sched.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using Clock = std::chrono::steady_clock;
using i64 = std::int64_t;
using u64 = std::uint64_t;
struct Agg { i64 count=0, sum=0; bool operator==(const Agg&) const = default; };
Agg operator+(Agg a, Agg b) { return {a.count+b.count,a.sum+b.sum}; }
// Guest reports 128-byte cache lines. Fix spacing explicitly, not a cache-tier claim.
struct alignas(128) Cell { Agg value; };
struct Update { std::uint32_t key; std::int32_t count; i64 sum; };
struct alignas(128) Chunk { std::array<Update,64> records; };
struct alignas(128) Bits { std::array<u64,16> words{}; };
struct Row { u64 key; std::array<std::uint32_t,9> path; };
struct Query { unsigned key, depth; Agg expected; bool dirty; };
struct Epoch { std::vector<Update> updates; std::vector<Query> queries; };
struct Config { std::string name; unsigned keys, batch, queries, depth=99, hot=0, epochs=8; };
struct Trace { Config c; std::vector<Row> rows; std::vector<Epoch> epochs; std::vector<Agg> final; u64 digest=0; };
u64 mix(u64 x) { x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL; x=(x^(x>>27))*0x94d049bb133111ebULL; return x^(x>>31); }
u64 next(u64& s) { return mix(s+=0x9e3779b97f4a7c15ULL); }
u64 prefix(u64 key,unsigned d) { return d ? key >> (64-8*d) : 0; }
u64 hash(u64 key,unsigned d) { return mix(prefix(key,d) ^ (0x9e3779b97f4a7c15ULL*d)); }
std::vector<Config> configs() {
    return {{"small",1024,1024,4},{"spread",65536,1024,4},
        {"hot",65536,1024,4,99,64},{"roots",65536,1024,64,0},
        {"points",65536,1024,64,8},{"batch256",65536,256,4},
        {"batch4096",65536,4096,4},{"hot4096",65536,4096,4,99,64},
        {"hot_roots",65536,1024,64,0,64}};
}
Trace make_trace(Config c) {
    Trace t; t.c=c; t.rows.resize(c.keys);
    std::array<std::unordered_map<u64,unsigned>,9> ids;
    unsigned nodes=1;
    for(unsigned k=0;k<c.keys;++k) {
        auto& row=t.rows[k]; row.key=mix(k+137); row.path[0]=0;
        for(unsigned d=1;d<=8;++d) {
            auto [it,inserted]=ids[d].try_emplace(prefix(row.key,d),nodes);
            if(inserted) ++nodes;
            row.path[d]=it->second;
        }
    }
    std::vector<i64> values(c.keys,0); // Zero means absent; live values are positive.
    u64 rng=17, qrng=91;
    for(unsigned e=0;e<c.epochs;++e) {
        Epoch epoch;
        std::vector<bool> dirty(nodes,false);
        for(unsigned j=0;j<c.batch;++j) {
            unsigned k=next(rng)%(c.hot?c.hot:c.keys);
            i64 old=values[k];
            i64 value=(old && next(rng)%5==0)?0:1+next(rng)%1000;
            if(value==old) ++value;
            values[k]=value;
            Update u{k,static_cast<std::int32_t>((value!=0)-(old!=0)),value-old};
            epoch.updates.push_back(u);
            t.digest=mix(t.digest^k^std::bit_cast<u64>(u.sum)^static_cast<u64>(u.count));
            for(auto id:t.rows[k].path) dirty[id]=true;
        }
        for(unsigned q=0;q<c.queries;++q) {
            unsigned k=next(qrng)%c.keys;
            unsigned d=c.depth==99?std::array<unsigned,4>{0,1,2,8}[q%4]:c.depth;
            Agg expected;
            // Independent afterimage row oracle, not the summary-update implementation.
            for(unsigned j=0;j<c.keys;++j)
                if(values[j] && prefix(t.rows[j].key,d)==prefix(t.rows[k].key,d))
                    expected=expected+Agg{1,values[j]};
            epoch.queries.push_back({k,d,expected,dirty[t.rows[k].path[d]]});
        }
        t.epochs.push_back(std::move(epoch));
    }
    t.final.resize(nodes);
    for(unsigned k=0;k<c.keys;++k) if(values[k])
        for(auto id:t.rows[k].path) t.final[id]=t.final[id]+Agg{1,values[k]};
    return t;
}
void pin(unsigned cpu) {
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu,&set);
    if(sched_setaffinity(0,sizeof(set),&set)) { std::cerr<<"affinity failed\n"; std::abort(); }
}
std::vector<unsigned> cpus;
class Pool {
    unsigned n;
    std::barrier<> start,done;
    std::vector<std::thread> workers;
    std::function<void(unsigned)> job;
    bool stop=false;
public:
    explicit Pool(unsigned count): n(count),start(count+1),done(count+1) {
        pin(n==1?cpus.front():cpus.back());
        if(n>1) for(unsigned i=0;i<n;++i) workers.emplace_back([this,i]{
            pin(cpus[i]);
            for(;;) { start.arrive_and_wait(); if(stop) return; job(i); done.arrive_and_wait(); }
        });
    }
    void run(std::function<void(unsigned)> f) {
        if(n==1) { f(0); return; }
        job=std::move(f); start.arrive_and_wait(); done.arrive_and_wait();
    }
    ~Pool() {
        if(n>1) { stop=true; start.arrive_and_wait(); for(auto& w:workers) w.join(); }
    }
};
enum class Method { plain, atomic, striped, scatter, word_or, word_check, span, exact_top, read_flush, append_scan, append_flush };
std::string name(Method m) {
    switch(m) {
        case Method::plain:return "eager_plain"; case Method::atomic:return "eager_atomic";
        case Method::striped:return "eager_striped"; case Method::scatter:return "scatter_record";
        case Method::word_or:return "word_or_record"; case Method::word_check:return "word_check_record";
        case Method::span:return "word_check_span"; case Method::exact_top:return "exact_top_span"; case Method::read_flush:return "read_flush_span";
        case Method::append_scan:return "append_scan"; case Method::append_flush:return "append_flush";
    } std::abort();
}
struct alignas(128) Stats { u64 ors=0,reservations=0; };
struct Result {
    double seconds=0,writes=0,reads=0,flush=0,drain=0,reset=0;
    u64 ors=0,reservations=0,positives=0,false_positives=0,clean=0,scanned=0,checksum=0;
    bool correct=true;
};
class Session {
public:
    const Trace& t;
    unsigned threads;
    std::vector<Cell> base;
    std::vector<Cell> top;
    std::vector<Chunk> log;
    std::vector<Bits> filter;
    Bits exact;
    alignas(128) std::atomic<unsigned> tail{0};
    // Alignment of tail alone does not pad the following member off its line.
    alignas(128) std::vector<Stats> stats;
    alignas(128) Pool pool;
    unsigned word_count;
    Session(const Trace& trace,unsigned writers,unsigned kib):t(trace),threads(writers),
        base(t.final.size()),top(writers*257),log((t.c.batch+63)/64),filter(kib*1024/128),
        stats(writers),pool(writers),word_count(kib*1024/8) {}
    Update& record(unsigned i) { return log[i/64].records[i%64]; }
    u64& word(unsigned i) { return filter[i/16].words[i%16]; }
    template<bool Account> void mark(u64& storage,u64 mask,bool checked,unsigned writer) {
        std::atomic_ref<u64> ref(storage);
        if(!checked || (ref.load(std::memory_order_relaxed)&mask)!=mask) {
            ref.fetch_or(mask,std::memory_order_relaxed);
            if constexpr(Account) ++stats[writer].ors;
        }
    }
    template<Method M,bool Account> void insert(u64 key,unsigned d,unsigned writer) {
        if constexpr(M==Method::exact_top) if(d==1) {
            unsigned p=prefix(key,1); mark<Account>(exact.words[p/64],u64{1}<<(p%64),true,writer); return;
        }
        u64 h=hash(key,d);
        if constexpr(M==Method::scatter) {
            u64 step=std::rotl(h,31)|1;
            for(unsigned j=0;j<4;++j) {
                unsigned bit=(h+j*step)&(word_count*64-1);
                mark<Account>(word(bit/64),u64{1}<<(bit%64),false,writer);
            }
        } else {
            u64 mask=0;
            for(unsigned j=0;j<4;++j) mask|=u64{1}<<((h>>(j*6))&63);
            mark<Account>(word((h>>32)&(word_count-1)),mask,M!=Method::word_or,writer);
        }
    }
    template<Method M> bool dirty(const Row& row,unsigned d) {
        if constexpr(M==Method::append_scan || M==Method::append_flush) return true;
        if(d==0) return true;
        if constexpr(M==Method::exact_top) if(d==1) {
            unsigned p=prefix(row.key,1); return (exact.words[p/64]>>(p%64))&1;
        }
        u64 h=hash(row.key,d);
        if constexpr(M==Method::scatter) {
            u64 step=std::rotl(h,31)|1;
            for(unsigned j=0;j<4;++j) {
                unsigned bit=(h+j*step)&(word_count*64-1);
                if(!(word(bit/64)&(u64{1}<<(bit%64)))) return false;
            }
            return true;
        } else {
            u64 mask=0;
            for(unsigned j=0;j<4;++j) mask|=u64{1}<<((h>>(j*6))&63);
            return (word((h>>32)&(word_count-1))&mask)==mask;
        }
    }
    static void atomic_add(Agg& a,const Update& u) {
        if(u.count) std::atomic_ref<i64>(a.count).fetch_add(u.count,std::memory_order_relaxed);
        std::atomic_ref<i64>(a.sum).fetch_add(u.sum,std::memory_order_relaxed);
    }
    template<Method M,bool Account> Result run(bool full_check) {
        constexpr bool buffered=static_cast<int>(M)>=static_cast<int>(Method::scatter);
        // Empty-state initialization and oracle checks are outside manual timing.
        for(auto& c:base) c.value={};
        for(auto& c:top) c.value={};
        for(auto& b:filter) b.words.fill(0);
        exact.words.fill(0); tail.store(0); for(auto& s:stats) s={};
        Result result;
        for(const auto& epoch:t.epochs) {
            auto a=Clock::now();
            pool.run([&](unsigned writer){
                unsigned first=writer*t.c.batch/threads,last=(writer+1)*t.c.batch/threads;
                unsigned slot=0,remaining=0;
                for(unsigned i=first;i<last;++i) {
                    const auto& u=epoch.updates[i]; const auto& row=t.rows[u.key];
                    if constexpr(buffered) {
                        constexpr unsigned span=(static_cast<int>(M)>=static_cast<int>(Method::span))?64:1;
                        if(!remaining) { slot=tail.fetch_add(span,std::memory_order_relaxed); remaining=span;
                            if constexpr(Account) ++stats[writer].reservations; }
                        record(slot++)=u; --remaining;
                        if constexpr(M!=Method::append_scan && M!=Method::append_flush)
                            for(unsigned d=1;d<=8;++d) insert<M,Account>(row.key,d,writer);
                    } else {
                        for(unsigned d=0;d<=8;++d) {
                            if constexpr(M==Method::striped) if(d<=1) {
                                unsigned p=d?1+prefix(row.key,1):0;
                                auto& value=top[writer*257+p].value;
                                value=value+Agg{u.count,u.sum}; continue;
                            }
                            auto& value=base[row.path[d]].value;
                            if constexpr(M==Method::plain) value=value+Agg{u.count,u.sum};
                            else atomic_add(value,u);
                        }
                    }
                }
            });
            auto b=Clock::now();
            if constexpr(Account && buffered) {
                result.correct &= tail.load()==t.c.batch;
                for(const auto& u:epoch.updates) for(unsigned d=1;d<=8;++d)
                    result.correct &= dirty<M>(t.rows[u.key],d);
            }
            bool pending=buffered;
            auto drain=[&] {
                auto before=Clock::now();
                // Full replay, no sorting/coalescing. Workers/readers are quiescent.
                for(unsigned i=0;i<t.c.batch;++i) {
                    auto u=record(i);
                    for(auto id:t.rows[u.key].path) base[id].value=base[id].value+Agg{u.count,u.sum};
                }
                auto replayed=Clock::now();
                for(auto& bits:filter) bits.words.fill(0);
                exact.words.fill(0); tail.store(0,std::memory_order_relaxed);
                auto cleared=Clock::now();
                result.drain+=std::chrono::duration<double>(replayed-before).count();
                result.reset+=std::chrono::duration<double>(cleared-replayed).count();
                result.flush+=std::chrono::duration<double>(cleared-before).count();
                pending=false;
            };
            double flush_before_reads=result.flush;
            for(const auto& q:epoch.queries) {
                const auto& row=t.rows[q.key];
                bool positive=false;
                if constexpr(buffered) if(pending) {
                    positive=dirty<M>(row,q.depth);
                    if constexpr(Account) { result.positives+=positive; result.false_positives+=positive&&!q.dirty; result.clean+=!q.dirty; }
                    if constexpr(M==Method::read_flush || M==Method::append_flush) if(positive) drain();
                }
                Agg value=base[row.path[q.depth]].value;
                if constexpr(M==Method::striped) if(q.depth<=1) {
                    unsigned p=q.depth?1+prefix(row.key,1):0;
                    for(unsigned w=0;w<threads;++w) value=value+top[w*257+p].value;
                }
                if constexpr(buffered) {
                    if(positive && pending) for(unsigned i=0;i<t.c.batch;++i) {
                        auto u=record(i);
                        if(prefix(t.rows[u.key].key,q.depth)==prefix(row.key,q.depth)) value=value+Agg{u.count,u.sum};
                        if constexpr(Account) ++result.scanned;
                    }
                }
                result.correct &= value==q.expected;
                result.checksum=mix(result.checksum^std::bit_cast<u64>(value.sum)^static_cast<u64>(value.count));
            }
            auto c=Clock::now();
            double flush_in_reads=result.flush-flush_before_reads;
            if constexpr(buffered) if(pending) drain();
            result.writes+=std::chrono::duration<double>(b-a).count();
            result.reads+=std::chrono::duration<double>(c-b).count()-flush_in_reads;
        }
        if(full_check) {
            if constexpr(M==Method::striped) {
                for(unsigned w=0;w<threads;++w) base[0].value=base[0].value+top[w*257].value;
                for(unsigned k=0;k<t.rows.size();++k) {
                    unsigned id=t.rows[k].path[1];
                    // First-byte nodes occur before any of their descendants, but IDs are interleaved.
                    if(base[id].value.count==-999999) continue;
                    unsigned p=1+prefix(t.rows[k].key,1); Agg value;
                    for(unsigned w=0;w<threads;++w) value=value+top[w*257+p].value;
                    result.correct &= value==t.final[id];
                    base[id].value.count=-999999;
                }
            }
            for(unsigned i=0;i<base.size();++i) {
                if constexpr(M==Method::striped) if(base[i].value.count==-999999) continue;
                result.correct &= base[i].value==t.final[i];
            }
        }
        for(const auto& s:stats) { result.ors+=s.ors; result.reservations+=s.reservations; }
        result.seconds=result.writes+result.reads+result.flush;
        return result;
    }
};
template<class F> void each_method(unsigned writers,F f) {
    if(writers==1) f.template operator()<Method::plain>();
    f.template operator()<Method::atomic>(); f.template operator()<Method::striped>();
    f.template operator()<Method::scatter>(); f.template operator()<Method::word_or>();
    f.template operator()<Method::word_check>(); f.template operator()<Method::span>();
    f.template operator()<Method::exact_top>(); f.template operator()<Method::read_flush>();
    f.template operator()<Method::append_scan>(); f.template operator()<Method::append_flush>();
}
int main(int argc,char** argv) {
    cpu_set_t allowed; CPU_ZERO(&allowed); sched_getaffinity(0,sizeof(allowed),&allowed);
    for(unsigned c=0;c<CPU_SETSIZE;++c) if(CPU_ISSET(c,&allowed)) cpus.push_back(c);
    bool check=false; unsigned kib=32; std::string only; int kept=1;
    for(int i=1;i<argc;++i) {
        std::string arg=argv[i];
        if(arg=="--check") check=true;
        else if(arg.starts_with("--case=")) only=arg.substr(7);
        else if(arg.starts_with("--filter-kib=")) kib=std::stoul(arg.substr(13));
        else argv[kept++]=argv[i];
    }
    argc=kept;
    if(cpus.size()<5 || !std::has_single_bit(kib) || kib>1024) return 2;
    if(check) std::cout<<"case,threads,method,updates,base_bytes,filter_bytes,digest,ors,reservations,positive_reads,false_positives,clean_reads,scanned,checksum,correct\n";
    else { benchmark::Initialize(&argc,argv); if(benchmark::ReportUnrecognizedArguments(argc,argv)) return 2; }
    bool found=false;
    for(auto c:configs()) {
        if(!only.empty() && only!=c.name) continue;
        found=true; auto trace=std::make_shared<Trace>(make_trace(c));
        for(unsigned writers:{1u,4u}) each_method(writers,[&]<Method M>() {
            std::string label=c.name+"/t"+std::to_string(writers)+"/"+name(M);
            if(check) {
                Session session(*trace,writers,(M==Method::append_scan || M==Method::append_flush)?0:kib); auto r=session.run<M,true>(true);
                if(!r.correct) { std::cerr<<"FAIL "<<label<<'\n'; std::exit(1); }
                std::cout<<c.name<<','<<writers<<','<<name(M)<<','<<c.batch*c.epochs<<','
                    <<session.base.size()*sizeof(Cell)<<','<<session.word_count*8<<','<<trace->digest<<','
                    <<r.ors<<','<<r.reservations<<','<<r.positives<<','<<r.false_positives<<','
                    <<r.clean<<','<<r.scanned<<','<<r.checksum<<",1\n";
            } else benchmark::RegisterBenchmark(label.c_str(),[trace,writers,kib](benchmark::State& state){
                Session session(*trace,writers,(M==Method::append_scan || M==Method::append_flush)?0:kib); Result result;
                double writes=0,reads=0,flush=0,drain=0,reset=0;
                for(auto _:state) {
                    result=session.run<M,false>(false);
                    if(!result.correct) { state.SkipWithError("oracle mismatch"); break; }
                    benchmark::DoNotOptimize(result.checksum); state.SetIterationTime(result.seconds);
                    writes+=result.writes; reads+=result.reads; flush+=result.flush; drain+=result.drain; reset+=result.reset;
                }
                auto n=trace->c.batch*trace->c.epochs;
                state.counters["updates"]=n;
                auto total=n*state.iterations();
                state.counters["write_ns_per_update"]=writes*1e9/total;
                state.counters["read_ns_per_update"]=reads*1e9/total;
                state.counters["flush_ns_per_update"]=flush*1e9/total;
                state.counters["drain_ns_per_update"]=drain*1e9/total;
                state.counters["reset_ns_per_update"]=reset*1e9/total;
            })->UseManualTime()->Unit(benchmark::kNanosecond);
        });
    }
    if(!found) return 2;
    if(check) { std::cerr<<"PASS: all queries and all final summaries match afterimage oracle; 1/4 writers.\n"; return 0; }
    benchmark::RunSpecifiedBenchmarks(); benchmark::Shutdown();
}

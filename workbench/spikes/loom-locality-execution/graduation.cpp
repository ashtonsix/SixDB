// The unchanged captured consumer supplies formats, native callbacks and its
// independent input formula. This driver changes only outer execution policy.
#define main captured_staged_main
#include LOOM_STAGED_SOURCE
#undef main
#include "locality.h"
#include <loom/execution.h>
#include <sched.h>

#ifndef LOOM_GRADUATED_POLICY
#define LOOM_GRADUATED_POLICY 0
#endif

namespace loom_graduation {
using namespace ht;
using namespace ht::staged_test;
using namespace loom_locality;

constexpr unsigned width=32;
constexpr unsigned probe_heads=4;
constexpr unsigned probe_groups=width/probe_heads;
constexpr unsigned sample_cadence_batches=32;
struct Work {
  std::array<unsigned,width> starts{};
  unsigned per_chain{};
};

Work work(const input& source,unsigned per_chain,unsigned wave=0) {
  Work out{{},per_chain};
  for(unsigned i=0;i<width;++i)
    out.starts[i]=(std::uint64_t(source.boundaries[i])+std::uint64_t(wave)*104729)%source.requests;
  return out;
}

answer expected(const input& source,const Work& in) {
  answer out;
  for(auto start:in.starts) {
    lane j{start,in.per_chain};
    while(j.left) {
      const bool found=j.rank<source.n;
      if(found)source.each_value(j.rank,[&](const auto& bytes) {
        const auto value=digest(bytes);j.sum+=value;j.edge^=value;++j.values;
      });
      accept(j,found,source);
    }
    out.sum+=j.sum;out.values+=j.values;out.hits+=j.hits;out.terminal^=j.rank;
  }
  return out;
}

answer expected_probe(const input& source,const Work& in,unsigned group) {
  answer out;
  for(unsigned head=group*probe_heads;head<(group+1)*probe_heads;++head) {
    lane j{in.starts[head],in.per_chain};
    while(j.left) {
      const bool found=j.rank<source.n;
      if(found)source.each_value(j.rank,[&](const auto& bytes) {
        const auto value=digest(bytes);j.sum+=value;j.edge^=value;++j.values;
      });
      accept(j,found,source);
    }
    out.sum+=j.sum;out.values+=j.values;out.hits+=j.hits;out.terminal^=j.rank;
  }
  return out;
}

struct Trace {
  Work repeated;
  std::array<std::vector<Work>,2> inputs;
  std::array<std::vector<answer>,2> answers;
  std::array<std::vector<std::array<answer,probe_groups>>,2> probes;
  Trace(const input& source,unsigned iterations):repeated(work(source,8)) {
    // The generator's independent value formula needs the successor graph.
    // Build every oracle before the all-hit graph is legitimately released.
    for(unsigned changing=0;changing<2;++changing)for(unsigned i=0;i<iterations;++i) {
      inputs[changing].push_back(changing?work(source,8,1+i):repeated);
      answers[changing].push_back(expected(source,inputs[changing].back()));
      std::array<answer,probe_groups> groups;
      answer combined;
      for(unsigned group=0;group<probe_groups;++group) {
        groups[group]=expected_probe(source,inputs[changing].back(),group);
        combined.sum+=groups[group].sum;combined.values+=groups[group].values;
        combined.hits+=groups[group].hits;combined.terminal^=groups[group].terminal;
      }
      require(combined==answers[changing].back(),"probe groups partition full independent oracle");
      probes[changing].push_back(groups);
    }
  }
  std::size_t bytes()const {
    std::size_t out=sizeof(*this);
    for(unsigned i=0;i<2;++i)out+=inputs[i].capacity()*sizeof(Work)+answers[i].capacity()*sizeof(answer);
    for(const auto& group:probes)out+=group.capacity()*sizeof(std::array<answer,probe_groups>);
    return out;
  }
};

// Four complete chains execute consecutively: software-serial complete-consumer
// cost. Normal hardware overlap remains possible; this is not load latency.
template<class Read> [[gnu::noinline]] answer serial_probe(
    const Read& read,const input& source,const Work& in,unsigned group) {
  answer out;
  for(unsigned head=group*probe_heads;head<(group+1)*probe_heads;++head) {
    lane j{in.starts[head],in.per_chain};
    while(j.left) {
      const bool found=read.get_bytes(key(j.rank),[&] [[gnu::always_inline]] (auto bytes) {
        const auto value=digest(bytes);j.sum+=value;j.edge^=value;++j.values;
      });
      accept(j,found,source);
    }
    out.sum+=j.sum;out.values+=j.values;out.hits+=j.hits;out.terminal^=j.rank;
  }
  return out;
}

class ProbeCadence {
  std::uint64_t batch_{},next_sample_{};
  unsigned group_{};
  std::uint8_t expensive_samples_{};
public:
  bool due(Evidence evidence) noexcept {
    if(evidence.kind==EvidenceKind::unknown && !evidence.cheap_samples) {
      // Record already qualifies the tag and full epoch. Unknown with no cheap
      // sample means no evidence, mismatch or expiry; start a fresh cadence.
      expensive_samples_=0;next_sample_=batch_;
    }
    return evidence.kind==EvidenceKind::unknown || batch_>=next_sample_;
  }
  unsigned group()const noexcept {return group_;}
  void completed(Evidence evidence) noexcept {
    group_=(group_+1)%probe_groups;
    if(evidence.kind==EvidenceKind::expensive_serial) {
      if(expensive_samples_<2)++expensive_samples_;
      // Give warming one early retry after the first expensive sample. A
      // second expensive observation backs off instead of probing every batch.
      next_sample_=batch_+(expensive_samples_==1?1:sample_cadence_batches);
    } else {
      expensive_samples_=0;
      next_sample_=batch_+(evidence.kind==EvidenceKind::unknown?1:sample_cadence_batches);
    }
  }
  void advance() noexcept {++batch_;}
};
static_assert(sizeof(ProbeCadence)==24);


template<class Read> [[gnu::noinline]] answer ready(const Read& read,const input& source,const Work& in) {
  std::array<lane,width> jobs;
  for(unsigned i=0;i<width;++i)jobs[i]={in.starts[i],in.per_chain};
  unsigned active=width;
  while(active)for(auto& j:jobs)if(j.left) {
    const bool found=read.get_bytes(key(j.rank),[&] [[gnu::always_inline]] (auto bytes) {
      const auto value=digest(bytes);j.sum+=value;j.edge^=value;++j.values;
    });
    accept(j,found,source);if(!j.left)--active;
  }
  return result(jobs);
}

template<class Read> [[gnu::noinline]] answer interleaved(
    const context<Read>& c,Scheduled<Read>& ring,const Work& in) {
  std::array<job<Read>,width> jobs;
  auto emit=emitter<true>(c.page,c.line);
  for(unsigned i=0;i<width;++i) {
    jobs[i].rank=in.starts[i];jobs[i].left=in.per_chain;
    c.source->prefetch_successor(jobs[i].rank);
    c.read.prepare_read(jobs[i].probe,key(jobs[i].rank),emit);
    require(ring.push(visit<true,Read>,{&jobs[i]}),"prefix Ring admission");
  }
  while(ring.pending())ring.spin({4096});
  return result(jobs);
}

enum class Mode { direct, ring, disabled, sampling_only, adaptive, caller_known };
constexpr std::array mode_names{"direct","ring","disabled","sampling_only","adaptive","caller_known"};
constexpr std::array phase_names{"reused","interference","changing","return","generation","domain","expired"};
constexpr unsigned phases=phase_names.size();

struct Interference {
  std::vector<word> words=std::vector<word>(16u*1024u*1024u,17);
  [[gnu::noinline]] word visit()const {
    word sum=0;
    for(std::size_t i=0;i<words.size();i+=8) {
      const auto* p=words.data()+i;
      asm volatile(""::"r"(p):"memory");
      sum+=*p;
    }
    return sum;
  }
};

Epoch epoch() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      clock_type::now().time_since_epoch()).count();
}

// State belongs to the entire trace, so return/publication/migration phases
// exercise recency and identity transitions rather than pre-seeding heat.
#if LOOM_GRADUATED_POLICY
using Identity=loom::execution::Identity;
using ExecutionPath=loom::execution::Path;
#else
using Identity=Tag;
using ExecutionPath=Path;
#endif

class BoundaryPolicy {
#if LOOM_GRADUATED_POLICY
  loom::execution::Policy state_;
  explicit BoundaryPolicy(loom::execution::Policy state):state_(std::move(state)){}
#else
  Record state_;
  ProbeCadence cadence_;
  Policy options_;
  explicit BoundaryPolicy(Policy options):options_(options){}
#endif
public:
  static BoundaryPolicy bind(unsigned cutoff) {
#if LOOM_GRADUATED_POLICY
    auto state=loom::execution::Policy::bind({cutoff,8,2,2,sample_cadence_batches});
    require(bool(state),"graduated policy options");return BoundaryPolicy(std::move(*state));
#else
    return BoundaryPolicy(Policy{cutoff,8,2,2});
#endif
  }
  bool due(const Identity& identity,Epoch now) {
#if LOOM_GRADUATED_POLICY
    return state_.sample_due(identity,now,{true,true,width,width});
#else
    return cadence_.due(state_.inspect(identity,now,options_));
#endif
  }
  bool observe(const Identity& identity,Epoch now,std::uint64_t elapsed,unsigned completed) {
#if LOOM_GRADUATED_POLICY
    return state_.observe(identity,now,{elapsed,completed});
#else
    if(!state_.observe(identity,now,{Path::direct,elapsed,completed,0,1},options_))return false;
    cadence_.completed(state_.inspect(identity,now,options_));return true;
#endif
  }
  ExecutionPath select(const Identity& identity,Epoch now,Mode mode) {
#if LOOM_GRADUATED_POLICY
    // Sampling-only exercises selection with only Ring legal for execution.
    // Its sample request used both paths, preserving the separate sample charge.
    return state_.select(identity,now,{mode!=Mode::sampling_only,true,width,width}).path;
#else
    return decide(state_,identity,now,{true,true,width,width},options_,
        mode==Mode::sampling_only?Control::fixed_ring:Control::adaptive).path;
#endif
  }
  void advance() {
#if !LOOM_GRADUATED_POLICY
    cadence_.advance();
#endif
  }
  static constexpr std::size_t state_bytes() {
#if LOOM_GRADUATED_POLICY
    return sizeof(loom::execution::Policy);
#else
    return sizeof(Record)+sizeof(ProbeCadence);
#endif
  }
};

void population(Identity& identity,unsigned value) {
#if LOOM_GRADUATED_POLICY
  identity.population=value;
#else
  identity.access_population=value;
#endif
}

template<class Read> void trial(loom::Worker& worker,const Read& read,const input& source,
    Scheduled<Read>& ring,const context<Read>& c,const Trace& trace,const Interference& interference,
    Mode mode,unsigned rep,unsigned iterations,unsigned cutoff,unsigned order,unsigned cutoff_order) {
  auto policy=BoundaryPolicy::bind(cutoff);
  Identity tag{1,1,1,1,1}; // one adopted/pinned fixture worker and cache domain.
  unsigned group=0; // Work selection remains caller-owned, separate from policy.
  const Work& repeated=trace.repeated;
  for(unsigned phase=0;phase<phases;++phase) {
    population(tag,phase==2?2:1);
    if(phase==4)++tag.generation;
    if(phase==5)++tag.cache_domain; // fixture identity invalidation, no physical migration.
    const auto& inputs=trace.inputs[phase==2];const auto& answers=trace.answers[phase==2];
    // Only the initial reuse phase is primed. Return follows real memory
    // interference, and identity controls continue the resulting useful reads.
    if(phase==0)for(unsigned warm=0;warm<3;++warm)
      require(ready(read,source,repeated)==answers.front(),"phase conditioning oracle");
    if(phase==1||phase==3)require(interference.visit()==word(17)*(interference.words.size()/8),"interference traversal");
    std::uint64_t sample_ns=0,sample_requests=0,decision_calls=0,direct_count=0,ring_count=0;
    std::uint64_t visits=ring.counters().visits,checksum=0;
    const auto begin=clock_type::now();
    for(unsigned i=0;i<iterations;++i) {
      const bool observes=mode==Mode::adaptive||mode==Mode::sampling_only;
      auto now=observes||mode==Mode::disabled?epoch():0;
      // Forces the real expiry branch without a blocking wall-clock wait.
      if(phase==6)now+=65536;
      if(observes && policy.due(tag,now)) {
        const auto sampled_begin=clock_type::now();
        auto sampled=serial_probe(read,source,inputs[i],group);
        const auto sampled_end=clock_type::now();
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(sampled_end-sampled_begin).count();
        now=std::chrono::duration_cast<std::chrono::milliseconds>(sampled_end.time_since_epoch()).count()+(phase==6?65536:0);
        require(sampled==trace.probes[phase==2][i][group],"bounded sample complete byte/graph oracle");
        const auto completed=probe_heads*inputs[i].per_chain;
        require(policy.observe(tag,now,std::uint64_t(elapsed),completed),"completed bounded serial evidence");
        sample_ns+=elapsed;sample_requests+=completed;group=(group+1)%probe_groups;
      }
      ExecutionPath decision;
      if(mode==Mode::direct||mode==Mode::caller_known)decision=ExecutionPath::direct;
      else if(mode==Mode::ring)decision=ExecutionPath::ring;
      else {decision=policy.select(tag,now,mode);++decision_calls;}
      answer got;
      if(decision==ExecutionPath::direct){++direct_count;got=ready(read,source,inputs[i]);}
      else {require(decision==ExecutionPath::ring,"legal policy path");++ring_count;got=interleaved(c,ring,inputs[i]);}
      require(got==answers[i],"prefix complete byte/graph oracle");checksum+=got.sum+got.values+got.hits+got.terminal+i;
      if(observes)policy.advance();
    }
    const auto elapsed=std::chrono::duration<double,std::nano>(clock_type::now()-begin).count();
    const auto requests=std::uint64_t(iterations)*width*repeated.per_chain;
    std::printf("%u,%u,%u,%s,%s,%u,%u,%llu,%.6f,%llu,%llu,%llu,%llu,%llu,%llu,%zu,%llu,%zu,%llu,%zu,%u,%u,%u,%u,%u,%u,%u\n",
      unsigned(Kind),source.n,unsigned(source.mixed),phase_names[phase],mode_names[unsigned(mode)],cutoff,rep,
      (unsigned long long)requests,elapsed/requests,(unsigned long long)sample_ns,
      (unsigned long long)sample_requests,(unsigned long long)decision_calls,(unsigned long long)direct_count,
      (unsigned long long)ring_count,(unsigned long long)(ring.counters().visits-visits),BoundaryPolicy::state_bytes(),
      (unsigned long long)checksum,trace.bytes(),(unsigned long long)source.graph_bytes(),interference.words.size()*sizeof(word),
      unsigned(LOOM_GRADUATED_POLICY),probe_heads*repeated.per_chain,probe_heads,probe_groups,sample_cadence_batches,
      order,cutoff_order);
  }
}

void run(loom::Worker& worker,unsigned n,unsigned repeats,unsigned iterations,bool mixed,[[maybe_unused]] bool checking,unsigned pass) {
  input source(n,mixed);source.hint_page=worker.hints.profile.geometry.page_bytes;
  source.hint_line=worker.hints.profile.geometry.line_bytes;
  Small small(*worker.memory,19);auto body=Body::promote(small,*worker.memory,41,73);
  require(bool(body),"execution owner");
  for(unsigned i=0;i<n;++i)source.each_value(i,[&](const auto& value){body->set_bytes(key(i),value);});
  // Qualify full data and read windows independently before timed prefixes.
  inspect(*body,body->bind_read(),source);Trace trace(source,iterations);source.release_unused_graph();
  const Interference interference;
  auto read=body->bind_read();context c{read,&source,source.hint_page,source.hint_line};
  if(checking)for(unsigned population=0;population<2;++population)
    for(unsigned i=0;i<iterations;++i)for(unsigned group=0;group<probe_groups;++group)
      require(serial_probe(read,source,trace.inputs[population][i],group)==trace.probes[population][i][group],
          "all bounded probe groups match independent oracle");
  Scheduled<decltype(read)> ring;require(bool(ring.configure(worker,{width,0},{&c})),"execution Ring");
  constexpr std::array cutoffs{32u,128u};
  for(unsigned rep=0;rep<repeats;++rep)for(unsigned order=0;order<6;++order) {
    const auto mode=Mode((rep+pass+order)%6);
    for(unsigned cutoff_order=0;cutoff_order<2;++cutoff_order) {
      const auto cutoff=cutoffs[(rep+pass+cutoff_order)%2];
      trial(worker,read,source,ring,c,trace,interference,mode,pass+rep,iterations,cutoff,order,cutoff_order);
    }
  }
  require(bool(ring.close()),"execution Ring close");
}
} // namespace loom_graduation

int main(int argc,char** argv) {
  using namespace ht;using namespace ht::staged_test;
  const bool check=argc<2||std::string_view(argv[1])=="check";
  require(check||std::string_view(argv[1])=="bench","execution mode");
  const unsigned n=check?128:argc>2?std::stoul(argv[2]):6144;
  const unsigned repeats=check?1:argc>3?std::stoul(argv[3]):5;
  const unsigned iterations=check?3:argc>4?std::stoul(argv[4]):64;
  const unsigned pass=check?0:argc>5?std::stoul(argv[5]):0;
  require(n>=128&&repeats&&iterations,"execution bounds");
  auto hardware=loom::discover_hardware();require(bool(hardware),"hardware discovery");
  loom::HardwareOptions options;options.calibrate=false;loom::HardwareService service(options);
  auto profile=service.request(std::move(*hardware));require(bool(profile),"hardware profile");
  loom::Worker worker;require(bool(loom::adopt(worker)),"worker adoption");
  auto arena=oa::open_arena(32ULL<<30);require(bool(arena),"arena");
  require(bool(loom::configure(worker,std::move(*arena),0,*profile)),"worker configure");
  std::puts("kind,keys,mixed,phase,mode,cutoff,rep,requests,ns_per_request,sample_ns,sample_requests,decision_calls,direct_batches,ring_batches,ring_visits,policy_bytes,checksum,trace_bytes,graph_bytes,interference_bytes,graduated,probe_requests,probe_heads,probe_groups,sample_cadence_batches,mode_order,cutoff_order");
  for(bool mixed:{false,true})loom_graduation::run(worker,n,repeats,iterations,mixed,check,pass);
  auto deadline=clock_type::now()+std::chrono::seconds(10);
  while(loom::shutdown(worker).kind()!=orbital::Progress::Kind::done) {
    require(clock_type::now()<deadline,"worker drain");loom::poll(worker,{1024,0});
  }
  loom::relinquish(worker);
}

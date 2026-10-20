// Fresh heap owner composition. Native control bodies below are preserved from
// maintained-probe/caller.cpp; only the reader binding and relative PMU include
// change. Every owner remains immobile and unchanged through physical drain.
#include "runtime.h"
#include <pmu/pmu.h>
#include <cstdlib>
#include <string>
#include <utility>

namespace independent {
using study::word;
using study::result;
using Common = loom::Ring<void*>;
static_assert(study::E==30,"this comparison fixes logical K30 geometry");
using PackedRead = study::borrowed_reader<study::Packed>;
using WholeRead = study::borrowed_reader<study::Whole>;
using MaintainedRead = study::borrowed_reader<study::MaintainedHeap>;
static_assert(sizeof(PackedRead)==sizeof(const void*));
static_assert(sizeof(WholeRead)==sizeof(const void*));
static_assert(sizeof(MaintainedRead)==sizeof(const void*));
namespace pmu = workbench::pmu;
constexpr unsigned lanes = 32;

std::uint64_t now_ns() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Completion {
  word start{}, stream_ns{}, chains_ns{};
  unsigned chains_left{};
  void stream_done() noexcept { stream_ns = now_ns() - start; }
  void chain_done() noexcept { if (!--chains_left) chains_ns = now_ns() - start; }
};

template<class Read, std::size_t Q> struct StreamFrame {
  Read read{};
  loom::IndependentStream<word,Q> cursor{};
  result output{};
  Completion* completion{};
};

template<class Read, std::size_t Q>
[[gnu::noinline]] LOOM_NATIVE_CC void stream_complete(Common::Cursor ring,
    Common::Slot* slot, void* opaque) {
  // Even a terminal-only ordinary clock call can impose a frame on every
  // visit. Keep it in this terminal tail target, with no extra Ring dispatch.
  auto* completion = static_cast<StreamFrame<Read,Q>*>(opaque)->completion;
  if (completion) completion->stream_done();
  [[clang::musttail]] return Common::finish(ring,slot,opaque);
}

template<class Read, std::size_t Q>
[[gnu::noinline]] LOOM_NATIVE_CC void stream_visit(Common::Cursor ring,
    Common::Slot* slot, void* opaque) {
  auto& frame = *static_cast<StreamFrame<Read,Q>*>(opaque);
  const auto read = frame.read;
  word sum = 0, hits = 0;
  const bool done = frame.cursor.advance(
      [&](const word& key) noexcept { read.prefetch(key); },
      [&](const word& key) noexcept {
        hits += read.find(key, write_arch::reduction_study::select<study::Multi&&study::V==64,study::ReductionOwner,Read>([&](const auto& value) __attribute__((always_inline)) {
          sum += study::digest(value);
        }));
      });
  frame.output.sum += sum;
  frame.output.hits += hits;
  if (done) {
    [[clang::musttail]] return stream_complete<Read,Q>(ring,slot,opaque);
  }
  [[clang::musttail]] return Common::yield_unchanged(ring,slot,opaque);
}

template<class Read> struct ChainFrame {
  Read read{};
  typename Read::staged_probe probe{};
  word current{}, left{}, edge{}, sum{}, hits{};
  std::size_t page{}, line{};
  Completion* completion{};
};

template<class Read>
[[gnu::noinline]] LOOM_NATIVE_CC void chain_complete(Common::Cursor ring,
    Common::Slot* slot, void* opaque) {
  auto* completion = static_cast<ChainFrame<Read>*>(opaque)->completion;
  if (completion) completion->chain_done();
  [[clang::musttail]] return Common::finish(ring,slot,opaque);
}

template<class Read, loom::Hint Hint>
[[gnu::noinline]] LOOM_NATIVE_CC void chain_visit(Common::Cursor ring,
    Common::Slot* slot, void* opaque) {
  auto& frame = *static_cast<ChainFrame<Read>*>(opaque);
  const study::hint_emitter<Hint,true> emit{frame.page,frame.line};
  bool found = false;
  const bool done = frame.read.advance(frame.probe,found,
      [&](const auto& value) __attribute__((always_inline)) {
        const auto value_digest = study::digest(value);
        frame.sum += value_digest; frame.edge ^= value_digest;
      },emit);
  if (done) {
    frame.hits += found;
    frame.current = frame.edge;
    frame.edge = 0;
    if (!--frame.left) {
      [[clang::musttail]] return chain_complete<Read>(ring,slot,opaque);
    }
    frame.read.prepare_staged(frame.probe,frame.current,emit);
  }
  [[clang::musttail]] return Common::yield_unchanged(ring,slot,opaque);
}

// Exact peer callback and typed native family, including the historical visits
// counter. Reusable bank storage is cold; all per-operation preparation is paid.
template<class Read> class TypedChains {
  study::ring_context<Read> context_;
  study::Schedule<Read> ring_;
  std::array<study::ring_job<Read>,lanes> jobs_{};
public:
  TypedChains(loom::Worker& worker,Read read,const study::input& in)
      : context_{read,&in,worker.hints.profile.geometry.page_bytes,
          worker.hints.profile.geometry.line_bytes} {
    study::require(bool(ring_.configure(worker,{lanes,0},{&context_})),"typed bank setup");
  }
  ~TypedChains() { if (!ring_.close()) std::abort(); }
  template<loom::Hint Hint> result run(std::span<const word> keys,
      std::array<word,lanes>& heads) {
    result out;
    const auto count=std::min<std::size_t>(lanes,keys.size());
    const study::hint_emitter<Hint,true> emit{context_.page,context_.line};
    for(unsigned j=0;j<count;++j) {
      auto& job=jobs_[j];job={};
      job.current=keys[j*keys.size()/count];
      job.left=(j+1)*keys.size()/count-j*keys.size()/count;
      context_.read.prepare_staged(job.probe,job.current,emit);
      study::require(ring_.push(study::visit<Read,Hint,true,false,true>,{&job}),"typed admission");
    }
    while(ring_.pending())ring_.spin({1024});
    heads={};
    for(unsigned j=0;j<count;++j) {
      out.sum+=jobs_[j].sum;out.hits+=jobs_[j].hits;
      out.terminal^=heads[j]=jobs_[j].current;
    }
    return out;
  }
};

template<bool Prefetch,class Read> [[gnu::noinline]] result direct_stream(
    Read read,std::span<const word> keys) {
  word sum=0,hits=0;
  if constexpr(Prefetch) {
    loom::prefetch_for_each<lanes>(keys,
        [&](const word& key) noexcept { read.prefetch(key); },
        [&](const word& key) noexcept {
          hits+=read.find(key,write_arch::reduction_study::select<study::Multi&&study::V==64,study::ReductionOwner,Read>([&](const auto& value) __attribute__((always_inline)) {
            sum+=study::digest(value);
          }));
        });
  } else {
    for(const auto key:keys)hits+=read.find(key,write_arch::reduction_study::select<study::Multi&&study::V==64,study::ReductionOwner,Read>([&](const auto& value) __attribute__((always_inline)) {
        sum+=study::digest(value);
      }));
  }
  return {sum,hits,0};
}

template<bool Prefetch,class Read> [[gnu::noinline]] result direct_chains(
    Read read,std::span<const word> keys,std::array<word,lanes>& final_heads) {
  std::array<word,lanes> heads{},lengths{};
  const auto count=std::min<std::size_t>(lanes,keys.size());
  for(unsigned j=0;j<count;++j) {
    heads[j]=keys[j*keys.size()/count];
    lengths[j]=(j+1)*keys.size()/count-j*keys.size()/count;
    if constexpr(Prefetch)read.prefetch(heads[j]);
  }
  word sum=0,hits=0,terminal=0;
  const auto longest=count?(keys.size()+count-1)/count:0;
  for(std::size_t i=0;i<longest;++i)for(unsigned j=0;j<count;++j)if(i<lengths[j]) {
    word edge=0;
    hits+=read.find(heads[j],[&](const auto& value) __attribute__((always_inline)) {
      const auto value_digest=study::digest(value);sum+=value_digest;edge^=value_digest;
    });
    heads[j]=edge;
    if constexpr(Prefetch)if(i+1<lengths[j])read.prefetch(edge);
  }
  for(unsigned j=0;j<count;++j)terminal^=heads[j];
  final_heads=heads;
  return {sum,hits,terminal};
}

enum class Mode { independent, chains, mixed };
enum class Arm { direct, prefetch, staged_l1, staged_l2, stream16, stream32, stream128,
  common_l1, common_l2, stream16_l2, stream32_l2, stream128_l2, prefetch_staged_l1, prefetch_staged_l2 };
const char* label(Mode mode) {
  switch(mode){case Mode::independent:return "independent";case Mode::chains:return "chains";case Mode::mixed:return "mixed";}
  std::abort();
}
const char* label(Arm arm) {
  switch(arm){
    case Arm::direct:return "direct";case Arm::prefetch:return "prefetch32";
    case Arm::staged_l1:return "staged_l1";case Arm::staged_l2:return "staged_l2";
    case Arm::stream16:return "stream_q16";case Arm::stream32:return "stream_q32";case Arm::stream128:return "stream_q128";
    case Arm::common_l1:return "common_l1";case Arm::common_l2:return "common_l2";
    case Arm::stream16_l2:return "stream_q16_chain_l2";case Arm::stream32_l2:return "stream_q32_chain_l2";case Arm::stream128_l2:return "stream_q128_chain_l2";
    case Arm::prefetch_staged_l1:return "prefetch_staged_l1";case Arm::prefetch_staged_l2:return "prefetch_staged_l2";
  }
  std::abort();
}
unsigned quantum(Arm arm){return arm==Arm::stream16||arm==Arm::stream16_l2?16:arm==Arm::stream32||arm==Arm::stream32_l2?32:arm==Arm::stream128||arm==Arm::stream128_l2?128:0;}
unsigned chain_hint(Mode mode,Arm arm) {
  if(mode==Mode::independent||arm==Arm::direct||arm==Arm::prefetch)return 0;
  return arm==Arm::staged_l2||arm==Arm::common_l2||arm==Arm::stream16_l2||arm==Arm::stream32_l2||
      arm==Arm::stream128_l2||arm==Arm::prefetch_staged_l2?2:1;
}
std::vector<Arm> arms(Mode mode) {
  if(mode==Mode::independent)return {Arm::direct,Arm::prefetch,Arm::staged_l1,Arm::staged_l2,Arm::stream16,Arm::stream32,Arm::stream128};
  if(mode==Mode::chains)return {Arm::direct,Arm::prefetch,Arm::staged_l1,Arm::staged_l2,Arm::common_l1,Arm::common_l2};
  return {Arm::direct,Arm::prefetch,Arm::stream16,Arm::stream32,Arm::stream128,
      Arm::stream16_l2,Arm::stream32_l2,Arm::stream128_l2,Arm::prefetch_staged_l1,Arm::prefetch_staged_l2};
}

template<class Read> class Executor {
  Read read_;
  const study::input& input_;
  Common ring_;
  study::ring_runner<Read> old_independent_;
  TypedChains<Read> old_chains_;
  StreamFrame<Read,16> balanced_{};
  StreamFrame<Read,32> small_{};
  StreamFrame<Read,128> large_{};
  std::array<ChainFrame<Read>,lanes> chains_{};
  std::size_t page_,line_;
  std::array<word,lanes> heads_{};
  Completion completion_{};
  void drain(){while(ring_.pending())ring_.spin({1024});}
  template<std::size_t Q> auto& stream_frame() {
    if constexpr(Q==16)return balanced_;else if constexpr(Q==32)return small_;else return large_;
  }
  template<std::size_t Q> void admit_stream(std::span<const word> keys,bool mixed) {
    auto& frame=stream_frame<Q>();frame={read_,loom::IndependentStream<word,Q>(keys),{},mixed?&completion_:nullptr};
    if(!keys.empty())study::require(ring_.push(stream_visit<Read,Q>,{&frame}),"stream admission");
  }
  template<loom::Hint Hint> unsigned admit_chains(std::span<const word> keys,bool mixed) {
    const auto count=std::min<std::size_t>(lanes,keys.size());
    completion_.chains_left=count;
    const study::hint_emitter<Hint,true> emit{page_,line_};
    for(unsigned j=0;j<count;++j) {
      auto& frame=chains_[j];frame={};frame.read=read_;frame.page=page_;frame.line=line_;
      frame.current=keys[j*keys.size()/count];
      frame.left=(j+1)*keys.size()/count-j*keys.size()/count;
      frame.completion=mixed?&completion_:nullptr;
      read_.prepare_staged(frame.probe,frame.current,emit);
      study::require(ring_.push(chain_visit<Read,Hint>,{&frame}),"common chain admission");
    }
    return count;
  }
  result collect_chains(unsigned count) {
    result out;heads_={};
    for(unsigned j=0;j<count;++j) {
      out.sum+=chains_[j].sum;out.hits+=chains_[j].hits;
      out.terminal^=heads_[j]=chains_[j].current;
    }
    return out;
  }
  template<std::size_t Q> result bulk(std::span<const word> keys) {
    admit_stream<Q>(keys,false);drain();return stream_frame<Q>().output;
  }
  template<loom::Hint Hint> result common_chains(std::span<const word> keys) {
    const auto count=admit_chains<Hint>(keys,false);drain();return collect_chains(count);
  }
  template<std::size_t Q,loom::Hint Hint> result mixed(std::span<const word> keys) {
    const auto split=keys.size()/2;
    admit_stream<Q>(keys.first(split),true);
    // Different frame types and native procedures coexist in this one bank.
    const auto count=admit_chains<Hint>(keys.subspan(split),true);
    drain();auto out=collect_chains(count);const auto stream=stream_frame<Q>().output;
    out.sum+=stream.sum;out.hits+=stream.hits;return out;
  }
public:
  Executor(loom::Worker& worker,Read read,const study::input& in)
      :read_(read),input_(in),old_independent_(worker,read,in),old_chains_(worker,read,in),
       page_(worker.hints.profile.geometry.page_bytes),line_(worker.hints.profile.geometry.line_bytes) {
    study::require(bool(ring_.configure(worker,{64,0})),"common bank setup");
  }
  ~Executor(){if(!ring_.close())std::abort();}
  void start(word now){completion_={};completion_.start=now;}
  const Completion& completion()const{return completion_;}
  const auto& heads()const{return heads_;}
  result run(Mode mode,Arm arm) {
    const std::span<const word> keys(input_.request_keys);
    if(mode==Mode::independent) {
      switch(arm){
        case Arm::direct:return direct_stream<false>(read_,keys);
        case Arm::prefetch:return direct_stream<true>(read_,keys);
        case Arm::staged_l1:return old_independent_.template run_independent<loom::Hint::l1_1,true>();
        case Arm::staged_l2:return old_independent_.template run_independent<loom::Hint::l2_1,true>();
        case Arm::stream16:return bulk<16>(keys);case Arm::stream32:return bulk<32>(keys);case Arm::stream128:return bulk<128>(keys);
        default:std::abort();
      }
    }
    if(mode==Mode::chains) {
      switch(arm){
        case Arm::direct:return direct_chains<false>(read_,keys,heads_);
        case Arm::prefetch:return direct_chains<true>(read_,keys,heads_);
        case Arm::staged_l1:return old_chains_.template run<loom::Hint::l1_1>(keys,heads_);
        case Arm::staged_l2:return old_chains_.template run<loom::Hint::l2_1>(keys,heads_);
        case Arm::common_l1:return common_chains<loom::Hint::l1_1>(keys);
        case Arm::common_l2:return common_chains<loom::Hint::l2_1>(keys);
        default:std::abort();
      }
    }
    if(arm==Arm::stream16)return mixed<16,loom::Hint::l1_1>(keys);
    if(arm==Arm::stream32)return mixed<32,loom::Hint::l1_1>(keys);
    if(arm==Arm::stream128)return mixed<128,loom::Hint::l1_1>(keys);
    if(arm==Arm::stream16_l2)return mixed<16,loom::Hint::l2_1>(keys);
    if(arm==Arm::stream32_l2)return mixed<32,loom::Hint::l2_1>(keys);
    if(arm==Arm::stream128_l2)return mixed<128,loom::Hint::l2_1>(keys);
    const auto split=keys.size()/2;
    auto out=arm!=Arm::direct?direct_stream<true>(read_,keys.first(split)):direct_stream<false>(read_,keys.first(split));
    completion_.stream_done();
    result tail;
    if(arm==Arm::prefetch_staged_l1)tail=old_chains_.template run<loom::Hint::l1_1>(keys.subspan(split),heads_);
    else if(arm==Arm::prefetch_staged_l2)tail=old_chains_.template run<loom::Hint::l2_1>(keys.subspan(split),heads_);
    else if(arm==Arm::prefetch)tail=direct_chains<true>(read_,keys.subspan(split),heads_);
    else tail=direct_chains<false>(read_,keys.subspan(split),heads_);
    completion_.chains_left=1;completion_.chain_done();
    out.sum+=tail.sum;out.hits+=tail.hits;out.terminal=tail.terminal;return out;
  }
};

std::unique_ptr<study::input> make_input(std::size_t n,std::size_t count,word seed,unsigned miss,bool concentrated=false) {
  return study::make_input(n,count,seed,miss,concentrated);
}

result expected(const study::input& in,Mode mode,std::array<word,lanes>& heads,const result* useful=nullptr) {
  result out=useful?*useful:in.independent_expected();heads={};
  if(mode==Mode::independent)return out;
  const auto begin=mode==Mode::mixed?in.requests/2:0;
  const auto size=in.requests-begin,count=std::min<std::size_t>(lanes,size);
  for(unsigned j=0;j<count;++j) {
    heads[j]=in.request_keys[(begin+(j+1)*size/count)%in.requests];
    out.terminal^=heads[j];
  }
  return out;
}

std::vector<Mode> modes(const study::input& in) {
  if(study::V>=8&&!in.miss&&in.requests>=32)return {Mode::independent,Mode::chains,Mode::mixed};
  return {Mode::independent};
}

template<class Read> void audit(loom::Worker& worker,Read read,const study::input& in) {
  std::unordered_map<word,unsigned> wanted,seen;
  for(auto key:in.request_keys)++wanted[key];
  using Audit=study::audit_reader<Read>;
  Executor<Audit> executor(worker,{read,&seen},in);
  for(auto mode:modes(in))for(auto arm:arms(mode)) {
    std::array<word,lanes> heads;const auto reference=expected(in,mode,heads);
    executor.start(now_ns());const auto observed=executor.run(mode,arm);
    study::require(observed==reference,"audited full useful result");
    study::require(seen==wanted,"every request issued exactly once, including misses");
    if(mode!=Mode::independent)study::require(executor.heads()==heads,"every ordered terminal chain head");
    seen.clear();
  }
}

pmu::Mode pmu_mode() {
  const std::string value=std::getenv("LI_PMU")?std::getenv("LI_PMU"):"off";
  if(value=="off")return pmu::Mode::off;if(value=="basic")return pmu::Mode::basic;
  if(value=="stalls")return pmu::Mode::stalls;
  throw std::invalid_argument("LI_PMU must be off, basic or stalls");
}

struct Reference {result output;std::array<word,lanes> heads;};

} // namespace independent

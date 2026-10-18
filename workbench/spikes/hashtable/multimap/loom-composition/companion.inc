// Include after the heap-bound caller definitions in caller.cpp. This private
// experiment adds a frame/procedure, not a public scheduler or owner kernel.
namespace staged_companion {
using independent::Common;
using independent::lanes;
using study::word;
using study::result;

struct Completion : independent::Completion {
  unsigned independent_left{};
  void independent_done() noexcept { if (!--independent_left) stream_done(); }
};

template<class Read> struct IndependentFrame {
  Read read{};
  typename Read::staged_probe probe{};
  std::span<const word> keys{};
  word index{}, left{}, sum{}, hits{};
  std::size_t page{}, line{};
  Completion* completion{};
};

template<class Read>
[[gnu::noinline]] LOOM_NATIVE_CC void independent_complete(Common::Cursor ring,
    Common::Slot* slot, void* opaque) {
  auto* completion = static_cast<IndependentFrame<Read>*>(opaque)->completion;
  if (completion) completion->independent_done();
  [[clang::musttail]] return Common::finish(ring,slot,opaque);
}

template<class Read, loom::Hint Hint>
[[gnu::noinline]] LOOM_NATIVE_CC void independent_visit(Common::Cursor ring,
    Common::Slot* slot, void* opaque) {
  auto& frame = *static_cast<IndependentFrame<Read>*>(opaque);
  const study::hint_emitter<Hint,true> emit{frame.page,frame.line};
  bool found = false;
  const bool done = frame.read.advance(frame.probe,found,
      write_arch::reduction_study::select<study::Multi&&study::V==64,study::ReductionOwner,Read>([&](const auto& value) __attribute__((always_inline)) {
        frame.sum += study::digest(value);
      }),emit);
  if (done) {
    frame.hits += found;
    if (!--frame.left) {
      [[clang::musttail]] return independent_complete<Read>(ring,slot,opaque);
    }
    // A complete lookup, including the full bag, releases this lane for its
    // next prepared key. Never advance from an intermediate stage or by hits.
    frame.index += lanes;
    frame.read.prepare_staged(frame.probe,frame.keys[frame.index],emit);
  }
  [[clang::musttail]] return Common::yield_unchanged(ring,slot,opaque);
}

struct Output {
  result independent{}, chains{}, combined{};
  std::array<word,lanes> heads{};
  unsigned independent_pending{}, chains_pending{};
  word independent_done_ns{}, chains_done_ns{};
};

// The actual operation owner retains the unchanged heap owners, readers, input and sinks.
// All frames stay at these addresses until the last spin/cleanup has returned.
// The two frame types/procedures share this ONE native bank; spin is external.
template<class IndependentRead, class ChainRead = IndependentRead, bool ContextChains = false> class Shared {
  IndependentRead independent_read_;
  ChainRead chain_read_;
  Common ring_;
  std::array<IndependentFrame<IndependentRead>,lanes> independent_{};
  using ChainFrame = std::conditional_t<ContextChains,context_companion::Frame<ChainRead>,independent::ChainFrame<ChainRead>>;
  std::array<ChainFrame,lanes> chains_{};
  Completion completion_{};
  std::size_t page_,line_;
  const context_companion::Context<ChainRead> chain_context_;
  unsigned independent_count_{},chain_count_{};
public:
  Shared(loom::Worker& worker,IndependentRead independent_read,ChainRead chain_read)
      :independent_read_(independent_read),chain_read_(chain_read),
       page_(worker.hints.profile.geometry.page_bytes),line_(worker.hints.profile.geometry.line_bytes),
       chain_context_{chain_read,page_,line_} {
    study::require(bool(ring_.configure(worker,{2*lanes,0})),"shared staged bank");
  }
  Shared(const Shared&)=delete;
  Shared& operator=(const Shared&)=delete;
  ~Shared(){study::require(bool(ring_.close()),"close shared staged bank after retirement");}

  template<loom::Hint Hint> void prepare(std::span<const word> keys,
      std::span<const word> chain_keys,word start) {
    study::require(!ring_.pending(),"rebind only after returned retirement/cleanup");
    completion_={};completion_.start=start;
    independent_count_=std::min<std::size_t>(lanes,keys.size());
    chain_count_=std::min<std::size_t>(lanes,chain_keys.size());
    completion_.independent_left=independent_count_;completion_.chains_left=chain_count_;
    const study::hint_emitter<Hint,true> emit{page_,line_};
    for(unsigned j=0;j<independent_count_;++j) {
      auto& frame=independent_[j];frame={};frame.read=independent_read_;frame.keys=keys;
      frame.index=j;frame.left=(keys.size()-1-j)/lanes+1;
      frame.page=page_;frame.line=line_;frame.completion=&completion_;
      frame.read.prepare_staged(frame.probe,keys[j],emit);
      study::require(ring_.push(independent_visit<IndependentRead,Hint>,{&frame}),"staged lane admission");
    }
    for(unsigned j=0;j<chain_count_;++j) {
      auto& frame=chains_[j];
      const auto first=chain_keys[j*chain_keys.size()/chain_count_];
      const auto count=(j+1)*chain_keys.size()/chain_count_-j*chain_keys.size()/chain_count_;
      if constexpr(ContextChains) {
        context_companion::prepare<ChainRead,Hint>(frame,chain_context_,first,count,&completion_);
        study::require(ring_.push(context_companion::visit<ChainRead,Hint>,{&frame}),"context chain admission");
      } else {
        frame={};frame.read=chain_read_;frame.page=page_;frame.line=line_;frame.completion=&completion_;
        frame.current=first;frame.left=count;
        frame.read.prepare_staged(frame.probe,frame.current,emit);
        study::require(ring_.push(independent::chain_visit<ChainRead,Hint>,{&frame}),"chain lane admission");
      }
    }
  }
  std::uint64_t spin(loom::SpinBudget budget) noexcept{return ring_.spin(budget);}
  std::size_t pending()const noexcept{return ring_.pending();}
  std::size_t discard()noexcept{return ring_.discard();}
  const loom::RingCounters& counters()const noexcept{return ring_.counters();}
  Output collect()const noexcept {
    Output out;
    for(unsigned j=0;j<independent_count_;++j) {
      out.independent.sum+=independent_[j].sum;out.independent.hits+=independent_[j].hits;
    }
    for(unsigned j=0;j<chain_count_;++j) {
      const auto& job=[&]() -> const auto& {
        if constexpr(ContextChains)return chains_[j].job;else return chains_[j];
      }();
      out.chains.sum+=job.sum;out.chains.hits+=job.hits;
      out.chains.terminal^=out.heads[j]=job.current;
    }
    out.combined={out.independent.sum+out.chains.sum,out.independent.hits+out.chains.hits,out.chains.terminal};
    out.independent_pending=completion_.independent_left;out.chains_pending=completion_.chains_left;
    out.independent_done_ns=completion_.stream_ns;out.chains_done_ns=completion_.chains_ns;
    return out;
  }
};

// Exact historical typed callback, bounded to an existing input's key prefix.
// No copied input/table or new owner kernel. Its setup and collection are paid.
template<class Read> class OriginalIndependent {
  study::ring_context<Read> context_;
  study::Schedule<Read> ring_;
  std::array<study::ring_job<Read>,lanes> jobs_{};
public:
  OriginalIndependent(loom::Worker& worker,Read read,const study::input& in)
      :context_{read,&in,worker.hints.profile.geometry.page_bytes,worker.hints.profile.geometry.line_bytes} {
    static_assert(study::independent_width==lanes);
    study::require(bool(ring_.configure(worker,{lanes,0},{&context_})),"original independent bank");
  }
  ~OriginalIndependent(){study::require(bool(ring_.close()),"close original independent bank");}
  template<loom::Hint Hint> result run_prefix(std::size_t count) {
    study::require(!ring_.pending()&&count<=context_.source->request_keys.size(),"original prepared prefix");
    const study::hint_emitter<Hint,true> emit{context_.page,context_.line};
    for(auto& job:jobs_)job={};
    for(unsigned j=0;j<std::min<std::size_t>(lanes,count);++j) {
      auto& job=jobs_[j];job.current=j;job.left=(count-1-j)/lanes+1;
      context_.read.prepare_staged(job.probe,context_.source->request_keys[j],emit);
      study::require(ring_.push(study::visit<Read,Hint,false,true,true>,{&job}),"original prefix admission");
    }
    while(ring_.pending())ring_.spin({1024});
    result out;for(const auto& job:jobs_){out.sum+=job.sum;out.hits+=job.hits;}return out;
  }
};

template<class IndependentRead,class ChainRead=IndependentRead> class Sequential {
  OriginalIndependent<IndependentRead> independent_;
  independent::TypedChains<ChainRead> chains_;
  const study::input& input_;
public:
  Sequential(loom::Worker& worker,IndependentRead read,ChainRead chain,const study::input& in)
      :independent_(worker,read,in),chains_(worker,chain,in),input_(in){}
  template<loom::Hint Hint> Output run(std::size_t split,word start) {
    study::require(split<=input_.request_keys.size(),"sequential split");
    Output out;out.independent=independent_.template run_prefix<Hint>(split);
    out.independent_done_ns=independent::now_ns()-start;
    out.chains=chains_.template run<Hint>(std::span<const word>(input_.request_keys).subspan(split),out.heads);
    out.chains_done_ns=independent::now_ns()-start;
    out.combined={out.independent.sum+out.chains.sum,out.independent.hits+out.chains.hits,out.chains.terminal};
    return out;
  }
};
} // namespace staged_companion

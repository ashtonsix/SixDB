// Private composition only: exact existing bulk callback plus context chains,
// with one existing Ring<void*> bank. Sequential preparation drains the stream
// at this ordinary owner boundary before admitting chains; callbacks are shared.
namespace staged_companion {
template<class Read> class ContextBulk {
  Read read_;
  Common ring_;
  independent::StreamFrame<Read,128> stream_{};
  std::array<context_companion::Frame<Read>,lanes> chains_{};
  independent::Completion completion_{};
  const context_companion::Context<Read> context_;
  unsigned count_{};
public:
  ContextBulk(loom::Worker& worker,Read read):read_(read),context_{read,
      worker.hints.profile.geometry.page_bytes,worker.hints.profile.geometry.line_bytes} {
    study::require(bool(ring_.configure(worker,{2*lanes,0})),"context bulk bank");
  }
  ContextBulk(const ContextBulk&)=delete;
  ContextBulk& operator=(const ContextBulk&)=delete;
  ~ContextBulk(){study::require(bool(ring_.close()),"context bulk returned retirement");}
  template<loom::Hint Hint,bool Sequential=false> void prepare(std::span<const word> keys,std::span<const word> chain_keys,word start) {
    study::require(!ring_.pending(),"context bulk rebind after returned retirement");
    completion_={};completion_.start=start;count_=std::min<std::size_t>(lanes,chain_keys.size());
    completion_.chains_left=count_;
    stream_={read_,loom::IndependentStream<word,128>(keys),{},&completion_};
    if(!keys.empty())study::require(ring_.push(independent::stream_visit<Read,128>,{&stream_}),"exact existing bulk admission");
    // Both entries retain the same frames, native callback specializations and
    // terminal clocks. Only admission order changes; all work remains paid by
    // the enclosing whole-operation timer, including this returned boundary.
    if constexpr(Sequential)while(ring_.pending())ring_.spin({1024});
    for(unsigned j=0;j<count_;++j) {
      auto& frame=chains_[j];const auto first=chain_keys[j*chain_keys.size()/count_];
      const auto length=(j+1)*chain_keys.size()/count_-j*chain_keys.size()/count_;
      context_companion::prepare<Read,Hint>(frame,context_,first,length,&completion_);
      study::require(ring_.push(context_companion::visit<Read,Hint>,{&frame}),"context bulk chain admission");
    }
  }
  std::uint64_t spin(loom::SpinBudget budget) noexcept{return ring_.spin(budget);}
  std::size_t pending()const noexcept{return ring_.pending();}
  std::size_t discard()noexcept{return ring_.discard();}
  Output collect()const noexcept {
    Output out;out.independent=stream_.output;
    for(unsigned j=0;j<count_;++j) {
      const auto& job=chains_[j].job;out.chains.sum+=job.sum;out.chains.hits+=job.hits;
      out.chains.terminal^=out.heads[j]=job.current;
    }
    out.combined={out.independent.sum+out.chains.sum,out.independent.hits+out.chains.hits,out.chains.terminal};
    out.independent_pending=stream_.cursor.empty()?0:1;out.chains_pending=completion_.chains_left;
    out.independent_done_ns=completion_.stream_ns;out.chains_done_ns=completion_.chains_ns;return out;
  }
};
}

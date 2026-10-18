// Include after the heap-bound caller. One typed operation binding and original
// job progress share the existing Ring<void*> ABI with other native families.
namespace context_companion {
using independent::Common;
using independent::Completion;
using study::word;

template<class Read> struct Context {
  Read read;
  std::size_t page,line;
};
template<class Read> struct Frame {
  study::ring_job<Read> job{}; // FIRST: preserve original typed progress layout.
  const Context<Read>* context{};
  Completion* completion{};
};

template<class Read>
[[gnu::noinline]] LOOM_NATIVE_CC void complete(Common::Cursor ring,
    Common::Slot* slot, void* opaque) {
  auto* completion = static_cast<Frame<Read>*>(opaque)->completion;
  if(completion)completion->chain_done();
  [[clang::musttail]] return Common::finish(ring,slot,opaque);
}

template<class Read,loom::Hint Hint>
[[gnu::noinline]] LOOM_NATIVE_CC void visit(Common::Cursor ring,
    Common::Slot* slot, void* opaque) {
  auto* frame = static_cast<Frame<Read>*>(opaque);
  const auto* context = frame->context;
  auto* job = &frame->job;
  const study::hint_emitter<Hint,true> emit{context->page,context->line};
  bool found = false;
  const bool done = context->read.advance(job->probe,found,
      [job](const auto& value) __attribute__((always_inline)) {
        const auto value_digest=study::digest(value);
        job->sum+=value_digest;job->edge^=value_digest;
      },emit);
  if(done) {
    job->hits+=found;job->current=job->edge;job->edge=0;
    if(!--job->left) {
      [[clang::musttail]] return complete<Read>(ring,slot,opaque);
    }
    context->read.prepare_staged(job->probe,job->current,emit);
  }
  [[clang::musttail]] return Common::yield_unchanged(ring,slot,opaque);
}

// Ordinary admission only. The Context and Frame remain immobile until returned
// retirement/discard; the ordinary caller keeps the heap owner unchanged through return.
template<class Read,loom::Hint Hint>
void prepare(Frame<Read>& frame,const Context<Read>& context,word first,word count,
    Completion* completion) {
  frame={};frame.context=&context;frame.completion=completion;
  auto& job=frame.job;job.current=first;job.left=count;
  context.read.prepare_staged(job.probe,first,study::hint_emitter<Hint,true>{context.page,context.line});
}
} // namespace context_companion

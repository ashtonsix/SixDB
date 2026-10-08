#include "probe.h"
#include <loom/ring.h>

namespace cb {
CB_NOINLINE Word finish(void*, Word a, Word b, Word c, unsigned e) {
  return consume(a, b, c, e);
}
CB_NOINLINE CB_NONE Word finish_native(void*, Word a, Word b, Word c, unsigned e) {
  return consume(a, b, c, e);
}
Word use_expected(Word n) {
  const auto r = expected_triple(n);
  return r ? consume(r->a, r->b, r->c, 0) : 0;
}
Word use_output(Word n) {
  Triple t;
  const auto e = output(n, t);
  return e ? 0 : consume(t.a, t.b, t.c, 0);
}
Word use_callback(Word n) { return callback(n, nullptr, finish); }
Word use_native_callback(Word n) { return native_callback(n, nullptr, finish_native); }
Word use_reg_expected(Word n) {
  const auto r = reg_expected(n);
  return r ? consume(r->a, r->b, r->c, 0) : 0;
}
Word use_inline(Word n) {
  return with_values(n, [](Word a, Word b, Word c, unsigned e) { return consume(a,b,c,e); });
}
Word live_default(Word n, Word a, Word b, Word c, Word d, Word e) {
  const auto result = leaf(n);
  return result + a + b + c + d + e;
}
Word live_none(Word n, Word a, Word b, Word c, Word d, Word e) {
  const auto result = leaf_none(n);
  return result + a + b + c + d + e;
}
Word live_most(Word n, Word a, Word b, Word c, Word d, Word e) {
  const auto result = leaf_most(n);
  return result + a + b + c + d + e;
}

// Real module interfaces, preserved as named inspection points. Ordinary
// forwarding permits direct construction into the caller's result storage.
CB_NOINLINE orbital::Result<orbital::allocation::Allocation>
allocation_forward(loom::Worker& w, const orbital::allocation::AllocationRequest& r) {
  return loom::allocate(w, r);
}
CB_NOINLINE Word hold_consumer(const orbital::allocation::Allocation& a, orbital::ByteRange range) {
  const auto h = loom::hold(a, range);
  return h ? reinterpret_cast<Word>(h->address) ^ h->footprint.size : 0;
}
CB_NOINLINE orbital::Progress task_step(loom::Task&) noexcept { return orbital::Progress::done(); }
CB_NOINLINE orbital::allocation::Statistics stats(const orbital::allocation::Pool& p) {
  return orbital::allocation::statistics(p);
}
CB_NOINLINE Word task_service(loom::Worker& w) { return loom::poll(w, {0, 1}); }
Word use_progress_result(Word n, orbital::Signal& s) {
  const auto r=progress_result(n,s);
  switch(r.kind) {
    case orbital::Progress::Kind::waiting: return reinterpret_cast<Word>(r.signal)^r.observed_generation;
    case orbital::Progress::Kind::failed: return Word(r.failure);
    default: return Word(r.kind);
  }
}
Word use_progress_current_result(Word n, orbital::Signal& s) {
  const auto r=progress_current_result(n,s);
  switch(r.kind()) {
    case orbital::Progress::Kind::waiting: return reinterpret_cast<Word>(r.signal())^r.observed_generation();
    case orbital::Progress::Kind::failed: return Word(r.failure());
    default: return Word(r.kind());
  }
}
Word use_progress_output(Word n, orbital::Signal& s) {
  WaitDetail r;
  const auto kind=progress_output(n,s,r);
  switch(kind) {
    case orbital::Progress::Kind::waiting: return reinterpret_cast<Word>(r.signal)^r.generation;
    case orbital::Progress::Kind::failed: return Word(r.failure);
    default: return Word(kind);
  }
}
Word use_progress_compact(Word n, orbital::Signal& s) {
  const auto r=progress_compact(n,s);
  if((r.reference_and_kind&3)==1) return (r.reference_and_kind&~Word(3))^r.detail;
  if((r.reference_and_kind&3)==3) return r.detail;
  return r.reference_and_kind&3;
}
} // namespace cb

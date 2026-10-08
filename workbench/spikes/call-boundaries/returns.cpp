#include "probe.h"

namespace cb {
namespace { Word drops; }
Owner::~Owner() { drops += value; }
NativeOwner::~NativeOwner() { drops += value; }
Word drop_total() { return drops; }
Pair plain_pair(Word n) { return {n + 1, n * 3 + 7}; }
Triple plain_triple(Word n) { return make(n); }
std::expected<Pair, Error> expected_pair(Word n) {
  if (refused(n)) return std::unexpected(Error::refused);
  return plain_pair(n);
}
std::expected<Triple, Error> expected_triple(Word n) {
  if (refused(n)) return std::unexpected(Error::refused);
  return make(n);
}
std::optional<Pair> optional_pair(Word n) {
  if (refused(n)) return {};
  return Pair{n + 1, n * 3 + 7};
}
std::expected<Word, Error> expected_word(Word n) {
  if (refused(n)) return std::unexpected(Error::refused);
  return n + 1;
}
unsigned output(Word n, Triple& out) {
  if (refused(n)) return 1; // Preserve output on refusal.
  out = make(n);
  return 0;
}
Word callback(Word n, void* context, Finish finish) {
  if (refused(n)) return finish(context, 0, 0, 0, 1);
  const auto t = make(n);
  return finish(context, t.a, t.b, t.c, 0);
}
CB_NONE Word native_callback(Word n, void* context, NativeFinish finish) {
  if (refused(n)) return finish(context, 0, 0, 0, 1);
  const auto t = make(n);
  return finish(context, t.a, t.b, t.c, 0);
}
CB_REG std::expected<Triple, Error> reg_expected(Word n) {
  if (refused(n)) return std::unexpected(Error::refused);
  return make(n);
}
Owner owner(Word n) { return Owner(n); }
NativeOwner native_owner(Word n) { return NativeOwner(n); }
std::expected<NativeOwner, Error> wrapped_native_owner(Word n) {
  if (refused(n)) return std::unexpected(Error::refused);
  return NativeOwner(n);
}
Word leaf(Word n) { return n * 3 + 1; }
CB_NONE Word leaf_none(Word n) { return n * 3 + 1; }
CB_MOST Word leaf_most(Word n) { return n * 3 + 1; }
HistoricalProgress32 progress_result(Word n, orbital::Signal& signal) {
  switch(n&3) {
    case 0: return HistoricalProgress32::done();
    case 1: return HistoricalProgress32::wait(signal, n);
    case 2: return HistoricalProgress32::yield();
    default: return HistoricalProgress32::fail(orbital::Error::capacity);
  }
}
orbital::Progress progress_current_result(Word n, orbital::Signal& signal) {
  switch(n&3) {
    case 0: return orbital::Progress::done();
    case 1: return orbital::Progress::wait(signal, n);
    case 2: return orbital::Progress::yield();
    default: return orbital::Progress::fail(orbital::Error::capacity);
  }
}
orbital::Progress::Kind progress_output(Word n, orbital::Signal& signal, WaitDetail& out) {
  if((n&3)==1) { out.signal=&signal; out.generation=n; }
  if((n&3)==3) out.failure=orbital::Error::capacity;
  return orbital::Progress::Kind(n&3);
}
CompactProgress progress_compact(Word n, orbital::Signal& signal) {
  if((n&3)==1) return {reinterpret_cast<Word>(&signal)|1,n};
  return {n&3,(n&3)==3 ? Word(orbital::Error::capacity) : 0};
}
} // namespace cb

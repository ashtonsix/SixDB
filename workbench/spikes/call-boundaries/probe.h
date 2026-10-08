#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <orbital/types.h>

namespace cb {
using Word = std::uint64_t;
enum class Error : unsigned { refused = 1 };
struct Pair { Word a, b; };
struct Triple { Word a, b, c; };
struct Owner {
  Word value{};
  explicit Owner(Word v) : value(v) {}
  Owner(Owner&& o) noexcept : value(std::exchange(o.value, 0)) {}
  ~Owner();
};
struct [[clang::trivial_abi]] NativeOwner {
  Word value{};
  explicit NativeOwner(Word v) : value(v) {}
  NativeOwner(NativeOwner&& o) noexcept : value(std::exchange(o.value, 0)) {}
  ~NativeOwner();
};
#if defined(CB_INLINE_CONTROL)
#define CB_NOINLINE
#else
#define CB_NOINLINE __attribute__((noinline))
#endif
#define CB_NONE __attribute__((preserve_none))
#define CB_MOST __attribute__((preserve_most))
#if defined(__x86_64__)
#define CB_REG __attribute__((regcall))
#else
#define CB_REG
#endif
using Finish = Word (*)(void*, Word, Word, Word, unsigned);
using NativeFinish = CB_NONE Word (*)(void*, Word, Word, Word, unsigned);

CB_NOINLINE Pair plain_pair(Word);
CB_NOINLINE Triple plain_triple(Word);
CB_NOINLINE std::expected<Pair, Error> expected_pair(Word);
CB_NOINLINE std::expected<Triple, Error> expected_triple(Word);
CB_NOINLINE std::optional<Pair> optional_pair(Word);
CB_NOINLINE std::expected<Word, Error> expected_word(Word);
CB_NOINLINE unsigned output(Word, Triple&);
CB_NOINLINE Word callback(Word, void*, Finish);
CB_NOINLINE CB_NONE Word native_callback(Word, void*, NativeFinish);
CB_NOINLINE CB_REG std::expected<Triple, Error> reg_expected(Word);
CB_NOINLINE Owner owner(Word);
CB_NOINLINE NativeOwner native_owner(Word);
CB_NOINLINE std::expected<NativeOwner, Error> wrapped_native_owner(Word);
CB_NOINLINE Word leaf(Word);
CB_NOINLINE CB_NONE Word leaf_none(Word);
CB_NOINLINE CB_MOST Word leaf_most(Word);

// Visible bodies share the same arithmetic and refusal outcome. Only visibility
// and materialization change, not a guessed cheaper workload.
inline Triple make(Word n) { return {n + 1, n * 3 + 7, n ^ 0x9e3779b97f4a7c15ULL}; }
inline bool refused(Word n) { return (n & 7) == 0; }
inline Word consume(Word a, Word b, Word c, unsigned error) {
  return error ? 0 : (a + b) ^ c;
}
template<class F> [[gnu::always_inline]] inline Word with_values(Word n, F finish) {
  if (refused(n)) return finish(0, 0, 0, 1);
  const auto t = make(n);
  return finish(t.a, t.b, t.c, 0);
}

CB_NOINLINE Word use_expected(Word);
CB_NOINLINE Word use_output(Word);
CB_NOINLINE Word use_callback(Word);
CB_NOINLINE Word use_native_callback(Word);
CB_NOINLINE Word use_reg_expected(Word);
CB_NOINLINE Word use_inline(Word);
CB_NOINLINE Word live_default(Word, Word, Word, Word, Word, Word);
CB_NOINLINE Word live_none(Word, Word, Word, Word, Word, Word);
CB_NOINLINE Word live_most(Word, Word, Word, Word, Word, Word);
Word drop_total();
unsigned ring_checks();
// Frozen October 8 pre-integration control. Keep the 32-byte result explicit:
// changes to the runtime Progress must not relabel historical ABI evidence.
struct HistoricalProgress32 {
  using Kind = orbital::Progress::Kind;
  Kind kind;
  orbital::Signal* signal{};
  orbital::Error failure{orbital::Error::unavailable};
  Word observed_generation{};
  static HistoricalProgress32 done() noexcept { return {Kind::done}; }
  static HistoricalProgress32 wait(orbital::Signal& s, Word g) noexcept {
    return {Kind::waiting, &s, orbital::Error::unavailable, g};
  }
  static HistoricalProgress32 yield() noexcept { return {Kind::yielded}; }
  static HistoricalProgress32 fail(orbital::Error e) noexcept {
    return {Kind::failed, nullptr, e};
  }
};
static_assert(sizeof(HistoricalProgress32)==32);
// Spike-only alternatives preserve the full generation and exact Signal
// address; the tagged control needs the current alignment proof.
struct WaitDetail { orbital::Signal* signal; Word generation; orbital::Error failure; };
struct CompactProgress { Word reference_and_kind, detail; };
static_assert(alignof(orbital::Signal)>=4);
CB_NOINLINE HistoricalProgress32 progress_result(Word, orbital::Signal&);
CB_NOINLINE orbital::Progress progress_current_result(Word, orbital::Signal&);
CB_NOINLINE orbital::Progress::Kind progress_output(Word, orbital::Signal&, WaitDetail&);
CB_NOINLINE CompactProgress progress_compact(Word, orbital::Signal&);
CB_NOINLINE Word use_progress_result(Word, orbital::Signal&);
CB_NOINLINE Word use_progress_current_result(Word, orbital::Signal&);
CB_NOINLINE Word use_progress_output(Word, orbital::Signal&);
CB_NOINLINE Word use_progress_compact(Word, orbital::Signal&);
} // namespace cb

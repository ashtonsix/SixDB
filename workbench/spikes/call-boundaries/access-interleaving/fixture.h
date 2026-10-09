#pragma once

#include <loom/ring.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <array>
#include <cstdint>
#include <memory>

namespace access_spike {
namespace tp = ikea::tuplepack;
using Word = std::uint64_t;
using Carrier = tp::native::pipeline_values;
using Vector = Carrier::vector;
using Reader = tp::native_reader<64, tp::byte>;

// Version selection, publication, interpretation ownership and immutability
// below are fixture scaffolding. Allocation/Hold and Ikea decoding are actual.
// native_reader borrows prepared/source: this complete closure never moves.
struct Selected {
  orbital::allocation::Allocation allocation;
  orbital::allocation::Hold held;
  tp::layout layout;
  tp::reader<64> prepared;
  tp::view source;
  tp::read_operation<64, tp::byte> operation;
  Reader reader;
  std::size_t mask;
  Word version, representation;
  unsigned seed, shift;
  Selected(loom::Worker&, std::size_t rows, Word version, Word representation,
           unsigned seed, unsigned shift);
  ~Selected();
  Selected(const Selected&) = delete;
  void release_owner();
  static Word link(Word row, Word mask, unsigned seed);
  tp::byte decoded(Word row, unsigned byte) const;
};

struct Frame {
  std::shared_ptr<const Selected> selected;
  loom::DemandClass demand;
  Word identity{}, start{}, steps{}, sum{}, final_row{}, visits{}, done{};
  Carrier output{};
};

[[gnu::always_inline]] inline Word successor(Word first, Word mask) {
  return ((first & 127) | (((first >> 8) & 127) << 7) |
          (((first >> 16) & 127) << 14)) & mask;
}
[[gnu::always_inline]] inline Word checksum(Word sum, Word first, Word row, Word identity) {
  // Order-sensitive: packet OR alone quickly saturates and hides wrong traversal.
  return ((sum << 9) | (sum >> 55)) ^ (first + row + identity);
}

template<std::size_t> using Part = Vector;
template<class> struct Family;
template<std::size_t... V> struct Family<std::index_sequence<V...>> {
  using Ring = loom::Ring<Frame*, Word, Word, Word, Part<V>...>;
  static LOOM_NATIVE_CC void work(Ring*, typename Ring::Slot*, Frame*,
                                 Word row, Word remaining, Word sum, Part<V>...);
  static bool enqueue(Ring&, Frame&);
};
using Native = Family<std::make_index_sequence<Carrier::parts>>;

using Kernel = void (*)(Frame*, std::size_t, const loom::HintPolicy&);
extern "C" {
void access_scalar(Frame*, std::size_t, const loom::HintPolicy&);
void access_manual_1(Frame*, std::size_t, const loom::HintPolicy&);
void access_manual_2(Frame*, std::size_t, const loom::HintPolicy&);
void access_manual_4(Frame*, std::size_t, const loom::HintPolicy&);
void access_manual_8(Frame*, std::size_t, const loom::HintPolicy&);
}
} // namespace access_spike

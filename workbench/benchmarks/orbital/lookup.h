#pragma once
#include "packed-page.h"
#include <loom/ring.h>
#include <array>

namespace orbital_bench::lookup {
using Page = packed::Page;
using Source = packed::Source;
enum class Stage : std::uint8_t { directory, bind, order, probe, payload };
struct State { Key key{}; std::size_t first{}, last{}, mid{}, physical{}; };
/// The owner batch keeps concrete pages, named bindings, slot mapping and all
/// initialized contents stable. A selected ticket is prefetched before its
/// fields are bound; binding metadata is prefetched before payload addressing.
/// Physical slots are owner-maintained and below the admitted capacity256.
struct Frame {
  const Source* source{};
  const Page* page{};
  const packed::Bindings* binding{};
  const std::uint16_t* order{};
  Outcome* output{};
  std::uint32_t remaining{};
  State state;
  Stage stage{Stage::directory};
  bool done{};
};
// One concrete private scalar family. No owning handles or aggregate carrier
// crosses the musttail ABI; Frame state belongs to the nonsuspending batch.
using NativeRing = loom::Ring<Frame*>;
class Driver {
  NativeRing ring_;
  std::array<Frame, 64> frames_{};
  std::size_t active_width_{16};
public:
  void prepare(loom::Worker&, ReadExecution, std::size_t active_width = 16);
  void run(loom::Worker&, const Source&, std::span<const Operation>, std::span<Outcome>, ReadExecution);
  void close() noexcept;
  std::size_t storage_bytes() const noexcept {
    return sizeof(*this) + ring_.storage_bytes() - sizeof(NativeRing);
  }
};
} // namespace orbital_bench::lookup

#pragma once
#include "../metadata/composed/catalog.h"
#include "../metadata/consumer/shape_read.h"
#include <loom/ring.h>
#include <array>
#include <optional>
#include <span>

// Disposable selected-Object caller, not an Engine/navigation interface.
namespace loom_object_ring {
using Catalog = orbital_mvcc::composed::Catalog;
namespace law = orbital_mvcc::metadata_consumer::compact_law;
using Proof = orbital_mvcc::metadata_consumer::shape_read::TupleProof;
using Word = std::uint64_t;
using Outcome = orbital_bench::Outcome;
struct Request { orbital::ObjectId object; orbital_bench::Key key; unsigned steps{1}; };
enum class Execution { serial, manual, ring };
enum class Stage { probe, candidate, payload };
struct State { Word key{}, first{}, last{}, remaining{}, digest{}; };

/// Scope and cached selected bytes stay at this address through native residence.
/// Output is published once on completion. Retire Ring borrowers before clear().
struct Frame {
  std::optional<Catalog::Scope> scope;
  const law::tp::byte* bytes{};
  std::size_t records{};
  loom::DemandClass demand;
  Request request{};
  State state;
  Outcome result;
  std::size_t output_index{};
  Stage stage = Stage::probe;
  bool occupied{}, done{};
  Frame() = default;
  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;
  [[nodiscard]] orbital::Result<void> open(const Catalog&, Proof, Request, bool hints) noexcept;
  void clear() noexcept;
};

// Six native values; neither a Selection nor owning state crosses musttail.
using Ring = loom::Ring<Frame*, Word, Word, Word, Word, Word>;
[[nodiscard]] bool enqueue(Ring&, Frame&, bool hints) noexcept;

struct Statistics {
  std::size_t completed{}, replacement_admissions{}, returned_quanta{};
  std::uint64_t ring_visits{}; // zero for serial/manual; not a hardware counter
};

/// One assigned Worker and a borrowed immutable proof kept live by the caller.
/// configure() is cold reusable-bank preparation; run() charges each request's
/// Scope, selection, initial hints, admission, refill and terminal result copy.
/// Manual and Ring refill only at identical returned visit-budget boundaries.
/// No polls/publications occur inside run(). Failure retires all borrowers and
/// Scopes; earlier completed outputs remain, so a failed trial is abort-only.
class Driver {
  loom::Worker& worker_;
  Proof proof_;
  Ring ring_;
  std::array<Frame, 64> frames_;
  std::size_t width_{};
public:
  Driver(loom::Worker& worker, Proof proof) : worker_(worker), proof_(proof) {}
  Driver(const Driver&) = delete;
  Driver& operator=(const Driver&) = delete;
  ~Driver();
  [[nodiscard]] orbital::Result<void> configure(std::size_t width) noexcept;
  [[nodiscard]] orbital::Result<Statistics> run(const Catalog&, std::span<const Request>,
      std::span<Outcome>, Execution, bool hints, std::uint64_t visits = 128) noexcept;
  [[nodiscard]] orbital::Result<void> close() noexcept;
};
} // namespace loom_object_ring

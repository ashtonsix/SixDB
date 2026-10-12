#pragma once

#include <ikea/streampack/view.h>
#include <loom/worker.h>
#include <cstdint>
#include <span>
#include <vector>

namespace streampack_implementation::loom_consumer {

enum class Route { direct, point_chain, manual, ring };
enum class Code { ok, rank, retired, short_value, invalid_link };
struct Request {
    unsigned source{}, rank{}, steps{};
    std::uint64_t identity{};
};
struct Outcome {
    std::uint64_t digest{};
    unsigned rank{}, consumed{}, completions{};
    Code code{Code::ok};
    bool operator==(const Outcome&) const = default;
};
struct Options {
    Route route{Route::ring};
    unsigned width{8}, overflow{4}, quantum{128}, repetitions{1};
    bool hints{}, warmup{};
};
struct Sample {
    /// Accepted/refused/promoted/visits describe native scheduling. Completed
    /// counts all delivered requests, including immediate zero-step outcomes.
    std::uint64_t elapsed_ns{}, accepted{}, refused{}, completed{}, promoted{};
    std::uint64_t stage_visits{}, quanta{}, refill_boundaries{}, consumed{};
};
struct Report {
    std::vector<Outcome> outcomes;
    std::vector<Sample> samples;
    std::uint64_t setup_ns{}, close_ns{};
    std::size_t frame_bytes{}, scheduler_bytes{};
};
/// Runs only after a bounded native region returns. The hook may publish a new
/// version, but every supplied named view, shared layout and selected byte window
/// must stay immutable and alive until assess() returns, including overflow.
/// Quantum/completed counters restart at each repetition, including warmup.
using Boundary = void (*)(void*, std::uint64_t quantum, std::uint64_t completed) noexcept;

/// This caller reads a first-eight-byte identity and a final-four-byte successor
/// rank from each value (at least twelve used bytes). Only loaded successors drive
/// subsequent reads. Digest starts at identity * 0x9e3779b97f4a7c15; each hop is
/// rotl(digest,9) XOR key XOR (length * 0x94d049bb133111eb) XOR rank.
/// Sources have admitted encoded contents and share the exact prepared layout.
/// Setup/close are reported separately; samples include admission, refusal/retry,
/// native work, returned-boundary hooks, refill, result delivery and complete drain.
/// Direct and point_chain controls process requests serially. Manual and Ring
/// use the same rotation, overflow order and returned-boundary refill policy.
[[nodiscard]] Report assess(loom::Worker&, std::span<const ikea::streampack::const_view>,
    std::span<const Request>, Options, Boundary = nullptr, void* context = nullptr);

} // namespace streampack_implementation::loom_consumer

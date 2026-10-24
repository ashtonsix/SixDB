#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

// Private original-row fixtures for the Frame fixed-policy experiment. This
// module has no encoded images, candidate answers, clocks or correction algebra.
namespace frame_histories {
using u64 = std::uint64_t;
using u128 = unsigned __int128;
inline constexpr std::size_t block_rows = 256;

enum class field { a, b };
struct columns { std::vector<u64> a, b; };
struct edit { field column; std::size_t row; u64 value; };
struct batch { std::vector<edit> edits; };
enum class operation { sum, count_ge, pair, points };
struct read {
    operation op = operation::sum;
    field column = field::b; // pair always means A >= threshold, consuming B.
    u64 threshold = 0;
    // Empty means all blocks. Otherwise exactly ceil(blocks/64) words, with
    // unused high bits zero. One bit per original 256-row block.
    std::vector<u64> coarse;
    std::vector<std::size_t> point_rows;
};
struct answer {
    u128 sum = 0;
    // SUM: active rows; COUNT: matching rows; pair: selected rows;
    // points: admitted requests, including duplicates. COUNT's sum is zero.
    u64 count = 0;
    bool operator==(const answer&) const = default;
};
enum class refusal { unequal_domains, incomplete_block, bad_mask, bad_row, bad_event, bad_field, bad_operation };
template<class T> using result = std::expected<T, refusal>;

// All structural validation precedes any value access. Inactive full blocks
// may be inaccessible. Invalid point indices are refused even if masked out.
result<answer> evaluate(std::span<const u64> a, std::span<const u64> b, const read&);
// Validate the whole batch before writing. No old value is needed to apply an
// assignment; input order is preserved and the last assignment wins.
result<void> apply(columns&, const batch&);

enum class event_kind { edits, query };
struct event { event_kind kind; std::size_t index; };
struct trace {
    std::vector<batch> batches;
    std::vector<read> reads;
    std::vector<event> events;
    // Opportunities after these counts of applied batches, not instructions
    // to a candidate owner. A fold never advances logical time.
    std::vector<std::size_t> fold_after_batches;
};
struct expected_read {
    std::size_t event_index, read_index;
    answer current, original; // independent current/retained-original rows
};
result<std::vector<expected_read>> prepare_expected(const columns& original, const trace&);

enum class edit_pattern { repeated_hot, dispersed };
enum class edit_values { local_replacement, dictionary_novelty };
enum class read_mix { reduction, selection, point_heavy };
enum class thresholds { repeated, varying };
enum class chronology { burst_then_reads, interleaved };
// Eight ordered 64-assignment batches. Local replacements are copied from an
// original neighbour in the same block; this is NOT a fitting-write promise.
result<std::vector<batch>> make_batches(const columns&, edit_pattern, edit_values);
// Fixed 16-command template; points have 256 independent requests with
// deliberate duplicates. Thresholds and request rows depend only on original
// data and fixed seeds, never candidate state or subsequent edits.
result<std::vector<read>> make_reads(const columns&, read_mix, thresholds,
                                     std::span<const u64> coarse = {});
// Both chronologies contain the same ordered edits and 8 copies of the same
// 16-read template. Only placement of those reads relative to edits changes.
trace make_trace(std::vector<batch>, std::vector<read>, chronology);
struct fixture { columns original; trace commands; };
fixture tiny_witness();
} // namespace frame_histories

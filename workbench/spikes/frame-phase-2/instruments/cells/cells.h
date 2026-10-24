#pragma once

#include "image.h"

#include <span>
#include <string>
#include <variant>
#include <vector>

namespace frame_cells {
namespace fi = frame_image;
using fi::u8;
using fi::u16;
using fi::u32;
using fi::u64;
using fi::u128;

struct answer {
    u128 sum = 0;
    u64 count = 0;
    bool operator==(const answer&) const = default;
};

// Genuine typed arrays: alignment, object lifetime and narrow vector loads do
// not depend on casting a byte allocation. No input owner can be copied into
// separate execution/conditioning closures.
struct native {
    using storage = std::variant<std::vector<u8>, std::vector<u16>, std::vector<u32>, std::vector<u64>>;
    storage values;
    u64 base = 0, maximum = 0;
    std::size_t rows = 0;
    explicit native(std::span<const u64> input);
    native(const native&) = delete;
    native& operator=(const native&) = delete;
    native(native&&) = default;
    unsigned lane() const;
};

// Private benchmark control view: construction proves the original-block
// mask's domain once. The owner keeps words immutable and readable.
struct block_mask {
    std::span<const u64> words;
    std::size_t blocks = 0;
    bool all = true, none = true;
    static block_mask bind(std::span<const u64> words, std::size_t rows);
    bool active(std::size_t b) const { return all || ((words[b / 64] >> (b % 64)) & 1); }
};

// Admit equal operand/selection extents once before entering repeated work.
void validate_native_domains(const native& payload, const block_mask& selection, const native* predicate = nullptr);

// Unchecked repeated-work entry points after validate_native_domains. Owners
// and mask are immutable for the borrow. SUM returns exact u128 and the final
// selected count; pair implements A>=threshold. No Frame image is required.
answer native_sum_once(const native& payload, const block_mask& selection);
answer native_pair_once(const native& predicate, const native& payload, u64 threshold, const block_mask& selection);

enum class operation { sum, pair };
enum class method { frame, native, alternative };

struct range {
    const void* data;
    std::size_t bytes, capacity_bytes;
    std::string role;
};

struct cell {
    fi::image a, b;
    fi::kind_index index;
    native an, bn;
    // One bit per original block; all/none flags are query preparation.
    std::vector<u64> coarse;
    std::vector<u64> scratch;
    block_mask selected;
    u64 threshold = 0;
    std::size_t rows = 0;
    cell(std::span<const u64> av, std::span<const u64> bv, std::span<const u64> mask,
         u64 threshold, fi::placement placement);
    cell(const cell&) = delete;
    cell& operator=(const cell&) = delete;
    bool active(std::size_t block) const {
        return selected.active(block);
    }
    // The very same ranges describe execution and cache conditioning. An
    // unselected representation is not flushed on another arm's behalf.
    std::vector<range> ranges(operation op, method arm) const;
};

// Separate TU from repetition and clock management; no LTO in the instrument.
answer execute(cell& c, operation op, method arm);
const char* arm_name(operation op, method arm);

} // namespace frame_cells

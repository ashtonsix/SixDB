#pragma once
#include <ikea/frame/types.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ikea::frame::author {

/// What a parent hands a child when it fits one unit. A child's rows per unit
/// need not match its parent's (a checkpoint child has one per spacing); only
/// a unit's last block may be partial.
struct fit_input {
    std::span<const std::uint64_t> values;
    const std::uint8_t *widths = nullptr; // per block: a width the parent fixed, or null
};

/// Fit facts a subtree reports for one unit, for Engine's analysis: blocks by
/// packed width and exception bucket, and evidence its laws use.
struct census {
    std::array<std::array<std::uint32_t, 5>, 65> blocks{}; // [width][exceptions: 0, 1-2, 3-8, 9-32, 33+]
    std::uint64_t exceptions = 0, monotone = 0, checkpoints = 0;
    static unsigned bucket(unsigned e) noexcept { return e == 0 ? 0 : e <= 2 ? 1 : e <= 8 ? 2 : e <= 32 ? 3 : 4; }
};

/// A leaf's direct point path for one block, for a bind-time plan: the leaf
/// kernel, its resource, and where the block's rows start in it. A null get
/// is a constant-zero block.
struct point_leaf {
    std::uint64_t (*get)(const void *, std::size_t) = nullptr;
    const void *resource = nullptr;
    const std::byte *data = nullptr; // the leaf's bytes, for prefetching a row's tile
    unsigned tile_rows = 8, tile_bytes = 0;
    std::size_t base = 0;
};

/// One transform of a column's values over units of up to 256 blocks of 256
/// rows. Every stage decodes a block, and every read has a reference
/// definition over decoded values (the defaults), so every stage x operation
/// cell answers. A stage overrides a read with a law: a rewrite into its
/// children's reads plus block-grain arithmetic over its descriptors.
///
/// Reads are trusted: the checked reader has admitted the unit, blocks,
/// rows, availability and output capacity before calling them. Calls name one
/// unit and a coarse selection of its blocks; dispatch is never per row.
class stage {
    std::vector<std::uint32_t> rows_; // per unit

  protected:
    void sized(unsigned unit, std::size_t rows);

  public:
    virtual ~stage() = default;
    unsigned rows(unsigned unit) const noexcept { return rows_[unit]; }
    unsigned blocks_in(unsigned unit) const noexcept { return (rows_[unit] + block_rows - 1) / block_rows; }
    unsigned rows_in(unsigned unit, unsigned block) const noexcept {
        return std::min(block_rows, rows_[unit] - block * block_rows);
    }
    unsigned groups_in(unsigned unit) const noexcept { return (blocks_in(unit) + group_blocks - 1) / group_blocks; }
    bitset256 blocks(unsigned unit) const noexcept { return bitset256::first_n(blocks_in(unit)); }

    virtual std::span<const std::unique_ptr<stage>> children() const { return {}; }
    // A child whose row domain differs from this stage's (intervals'
    // checkpoints) is its own image stream, emitted and attached per its own
    // groups. A stream's identity is the path of child roles from the root.
    virtual const char *role(unsigned child) const { return child ? "child1" : "child0"; }
    virtual bool inline_child(unsigned) const { return true; }
    /// Every stream below this stage, with the subtree that emits it.
    void streams(const std::string &prefix, std::vector<std::pair<std::string, const stage *>> &out) const;

    // Construction. fit builds one unit (units in order) from values; emit
    // appends this stage's section of one group, then its inline children's.
    virtual std::expected<void, error> fit(unsigned unit, const fit_input &) = 0;
    virtual void emit(unsigned unit, unsigned group, std::vector<std::byte> &out) const;
    /// Bytes a leaf spends on one block at width K, for a parent that prices
    /// widths (bound's cost model). Only leaves answer.
    virtual std::size_t body_bytes(unsigned) const { return 0; }

    // Placement. attach reads one group's section (this stage's, then its
    // inline children's) from immutable bytes, which stay placed and alive
    // while the tree is used; payload is borrowed. It checks the section's
    // byte law and this stage's premises over its children, and returns the
    // bytes it consumed. It is not a content proof: premises over children
    // and across streams are proved by validate_image for the groups it
    // admits. validate_unit is an author hook for a fully attached unit; the
    // image layer does not call it, and it proves nothing by default.
    virtual std::expected<std::size_t, error> attach(unsigned unit, std::size_t rows, unsigned group,
                                                     std::span<const std::byte>);
    virtual std::expected<void, error> validate_unit(unsigned unit) const;
    virtual void describe(unsigned unit, census &) const;
    virtual bool leaf_entry(unsigned, unsigned, point_leaf &) const { return false; }

    // Reads. The defaults are the reference definitions over decode.
    /// The rows of one block (rows_in of them).
    virtual void decode(unsigned unit, unsigned block, std::uint64_t *out) const = 0;
    virtual u128 sum(unsigned unit, const bitset256 &blocks) const;
    virtual u128 sum_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows) const;
    /// Exact sum of weights[row % period] * value over every row of the
    /// selected blocks; period is 16 or 64 (a parent's position weights).
    virtual u128 weighted_sum(unsigned unit, const bitset256 &blocks, const std::uint8_t *weights,
                              unsigned period) const;
    /// SUM, COUNT, MIN and MAX of the selected rows in one traversal, folded
    /// into out (a running summary across units).
    virtual void summarise_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows,
                                    summary &out) const;
    /// MAX of the selected rows, raising m only; false if none is selected.
    virtual bool max_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows,
                              std::uint64_t &m) const;
    virtual std::uint64_t count(comparison, unsigned unit, const bitset256 &blocks, const block_args &rhs) const;
    /// The same with one threshold for every block, so a stage can settle
    /// blocks in one pass over its descriptors.
    virtual std::uint64_t count_uniform(comparison, unsigned unit, const bitset256 &blocks, std::uint64_t rhs) const;
    /// Writes out[b] for every selected block b, with bits past its rows clear.
    virtual void filter(comparison, unsigned unit, const bitset256 &blocks, const block_args &rhs,
                        std::span<bitset256> out) const;
    virtual std::uint64_t get(unsigned unit, unsigned row) const;
    virtual void read(unsigned unit, unsigned first, unsigned n, std::uint64_t *out) const;
    /// Values of the selected rows of the selected blocks, in row order.
    virtual std::size_t select(unsigned unit, const bitset256 &blocks, const block_rows_set &rows,
                               std::uint64_t *out) const;
};

/// Runtime comparison, for laws whose operator is a call argument.
constexpr bool test(comparison op, std::uint64_t x, std::uint64_t t) noexcept {
    switch (op) {
    case comparison::equal: return x == t;
    case comparison::less: return x < t;
    case comparison::less_equal: return x <= t;
    case comparison::greater: return x > t;
    case comparison::greater_equal: return x >= t;
    }
    return false;
}

} // namespace ikea::frame::author

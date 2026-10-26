#pragma once
#include <ikea/frame/types.h>
#include <ikea/seriespack/selection.h>

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

/// The rows of one unit a read covers, in the callee's row domain: the span
/// [first, first + count) within coarse blocks, narrowed by a borrowed fine
/// selection of unit rows. Construction lowers that once to the blocks
/// holding a selected row and those wholly selected (every logical row,
/// including a short tail), so each stage of an identity chain sees one
/// lowering. Selection only: it carries no weights and grants no reads; a
/// law that needs multiplicities or other rows states them itself.
class request {
    unsigned rows_ = 0, first_ = 0, count_ = 0;
    seriespack::selection fine_ = seriespack::selection::all();
    bitset256 blocks_, full_;
    static constexpr std::uint64_t unknown = ~std::uint64_t{0};
    std::uint64_t selected_ = 0; // unknown after within(); counted on demand
    const bitset256 *borrowed_ = nullptr; // the fine selection's block for first_ / 256 onward, when contiguous
    bool inside(unsigned b) const noexcept {
        return first_ <= b * block_rows && b * block_rows + rows_in(b) <= first_ + count_;
    }
    bitset256 lower(unsigned b) const noexcept;
    std::uint64_t count_rows() const noexcept;

  public:
    /// Every row of the coarse blocks of a unit of rows rows.
    static request whole(unsigned rows, const bitset256 &coarse) noexcept;
    /// Trusted: fine covers every row of the span's coarse blocks, and its
    /// backing outlives the request.
    static request make(unsigned rows, unsigned first, unsigned count, const bitset256 &coarse,
                        const seriespack::selection &fine) noexcept;
    const bitset256 &blocks() const noexcept { return blocks_; }
    const bitset256 &full() const noexcept { return full_; }
    bitset256 partial() const noexcept { return blocks_ & ~full_; }
    bool all_full() const noexcept { return blocks_ == full_; }
    /// The same request over only the given blocks. Its selected() counts
    /// only their rows, on first use.
    request within(const bitset256 &subset) const noexcept {
        request r = *this;
        r.blocks_ = blocks_ & subset, r.full_ = full_ & subset, r.selected_ = unknown;
        return r;
    }
    /// The number of selected rows.
    std::uint64_t selected() const noexcept {
        return selected_ == unknown ? count_rows() : selected_;
    }
    unsigned rows_in(unsigned b) const noexcept { return std::min(block_rows, rows_ - b * block_rows); }
    /// The selected rows of an active block b, with bits past its rows clear.
    /// Requires blocks().contains(b); narrowing does not widen read authority.
    bitset256 rows_of(unsigned b) const noexcept {
        if (full_.contains(b))
            return bitset256::first_n(rows_in(b));
        if (borrowed_ && inside(b))
            return borrowed_[b - first_ / block_rows] & bitset256::first_n(rows_in(b));
        return lower(b);
    }
};

/// A comparison with one threshold, until a stage makes it vary per block
/// (bound subtracting each block's lo): then a plane indexed by unit block.
struct predicate {
    comparison op;
    std::uint64_t threshold = 0;
    const std::uint64_t *plane = nullptr;
    std::uint64_t at(unsigned block) const noexcept { return plane ? plane[block] : threshold; }
    bool uniform() const noexcept { return !plane; }
};

/// One transform of a column's values over units of up to 256 blocks of 256
/// rows. Every stage decodes a block, and every read has a reference
/// definition over decoded values (the defaults), so every stage x operation
/// cell answers. A stage overrides a read with a law: a rewrite into its
/// children's reads plus block-grain arithmetic over its descriptors.
///
/// Reads are trusted: the checked reader has admitted the unit, blocks,
/// rows, availability and output capacity before calling them. Calls name one
/// unit and a request over it; a stage lowers it for its own children, and
/// dispatch is never per row.
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
    virtual u128 sum(unsigned unit, const request &) const;
    /// Exact sum of weights[row % period] * value over every row of the
    /// selected blocks; period is 16 or 64 (a parent's position weights).
    virtual u128 weighted_sum(unsigned unit, const bitset256 &blocks, const std::uint8_t *weights,
                              unsigned period) const;
    /// SUM, COUNT, MIN and MAX of the selected rows in one traversal, folded
    /// into out (a running summary across units).
    virtual void summarise(unsigned unit, const request &, summary &out) const;
    /// MAX of the selected rows, raising m only; false if none is selected.
    virtual bool max(unsigned unit, const request &, std::uint64_t &m) const;
    virtual std::uint64_t count(unsigned unit, const request &, const predicate &) const;
    /// Writes out[b] for every block b of the request: the selected rows
    /// that satisfy the predicate, every other bit clear.
    virtual void filter(unsigned unit, const request &, const predicate &, std::span<bitset256> out) const;
    virtual std::uint64_t get(unsigned unit, unsigned row) const;
    virtual void read(unsigned unit, unsigned first, unsigned n, std::uint64_t *out) const;
    /// Values of the selected rows, in row order.
    virtual std::size_t select(unsigned unit, const request &, std::uint64_t *out) const;

    // Convenience block-set forms share the request-based operation bodies.
    u128 sum(unsigned unit, const bitset256 &blocks) const { return sum(unit, request::whole(rows(unit), blocks)); }
    u128 sum_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows) const;
    void summarise_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows, summary &out) const;
    bool max_selected(unsigned unit, const bitset256 &blocks, const block_rows_set &rows, std::uint64_t &m) const;
    std::uint64_t count(comparison op, unsigned unit, const bitset256 &blocks, const block_args &rhs) const {
        return count(unit, request::whole(rows(unit), blocks), {op, 0, rhs.data()});
    }
    std::uint64_t count_uniform(comparison op, unsigned unit, const bitset256 &blocks, std::uint64_t rhs) const {
        return count(unit, request::whole(rows(unit), blocks), {op, rhs});
    }
    void filter(comparison op, unsigned unit, const bitset256 &blocks, const block_args &rhs, std::span<bitset256> out) const {
        filter(unit, request::whole(rows(unit), blocks), {op, 0, rhs.data()}, out);
    }
    std::size_t select(unsigned unit, const bitset256 &blocks, const block_rows_set &rows, std::uint64_t *out) const;
};

/// Calls f.template operator()<Op>() with op as a template argument, so a
/// row loop compiles once per comparison rather than branching per row.
template <class F> decltype(auto) with_op(comparison op, F &&f) {
    switch (op) {
    case comparison::equal: return f.template operator()<comparison::equal>();
    case comparison::less: return f.template operator()<comparison::less>();
    case comparison::less_equal: return f.template operator()<comparison::less_equal>();
    case comparison::greater: return f.template operator()<comparison::greater>();
    case comparison::greater_equal: break;
    }
    return f.template operator()<comparison::greater_equal>();
}

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

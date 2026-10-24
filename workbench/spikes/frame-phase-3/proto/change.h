// Prototype 4: the change idea (../README.md, idea 4).
//
// A logical write (row, value) is classified by the stage tree before any
// payload is touched: the tree absorbs it (the value fits in place), redirects
// it (a write-time outlier) or refuses it (its group re-encodes). Where the
// redirected outliers live is the policy under test:
//
//   slack     bound's exception areas with spare slots per group (Ashton's
//             original model: encode-time and write-time patches share one
//             list in the residual domain).
//   buffer    a group delta buffer of logical overrides above the tree, with
//             a per-block bitmap of overridden rows (Ashton's leaning). Reads
//             patch outputs; the tree below is never touched by an outlier.
//   reencode  every outlier re-encodes its group at once.
//
// Every applied write publishes three things: issued-byte effects
// (ikea::journal), the exact logical SUM contribution, and block statistics
// whose precision drops when an extreme may have been removed.
#pragma once
#include "stages.h"

namespace frame3 {

/// Logical overrides in a per-group buffer with spare entries. An entry keeps
/// the child's value beneath it, so laws correct the child's answers
/// without reading the child again.
class overrides final : public stage {
    std::unique_ptr<stage> child_;
    unsigned capacity_;
    struct entry {
        u16 row;  // within the part
        u64 value, under;
    };
    struct buffer {
        std::vector<entry> entries;  // capacity_ slots, used first
        unsigned used = 0;
        std::span<const ikea::byte> plane_storage(unsigned) const noexcept { return std::as_bytes(std::span(entries)); }
    };
    struct part {
        std::vector<buffer> buffers;          // per group
        std::array<bitset256, part_blocks> rows{};  // overridden rows per block
        bitset256 dirty;                      // blocks that are not pristine
        std::span<const ikea::byte> plane_storage(unsigned plane) const noexcept {
            return plane ? std::as_bytes(std::span(&dirty, 1)) : std::as_bytes(std::span(rows));
        }
    };
    std::vector<part> parts_;

    const buffer &buffer_of(const part &s, unsigned b) const { return s.buffers[b / group_blocks]; }
    template <class F> void each_override(const part &s, unsigned b, F &&f) const {
        if (!s.dirty.contains(b))
            return;
        for (unsigned i = 0; i < buffer_of(s, b).used; ++i)
            if (const entry &e = buffer_of(s, b).entries[i]; e.row / block_rows == b)
                f(e);
    }
    int find(const part &s, unsigned row) const {
        const buffer &q = buffer_of(s, row / block_rows);
        for (unsigned i = 0; i < q.used; ++i)
            if (q.entries[i].row == row)
                return static_cast<int>(i);
        return -1;
    }

  public:
    overrides(std::unique_ptr<stage> child, unsigned capacity) : child_(std::move(child)), capacity_(capacity) {}
    const char *name() const override { return "overrides"; }
    std::span<const std::unique_ptr<stage>> children() const override { return {&child_, 1}; }
    void build(unsigned p, const build_input &in) override {
        if (parts_.size() <= p)
            parts_.resize(p + 1);
        sized(p, in.values.size());
        parts_[p] = part{};
        parts_[p].buffers.resize((blocks_in(p) + group_blocks - 1) / group_blocks);
        for (auto &q : parts_[p].buffers)
            q.entries.resize(capacity_);
        child_->build(p, in);
    }
    void clear(unsigned p) override {
        parts_[p] = {};
        child_->clear(p);
    }
    bool rebuilds_groups() const override { return child_->rebuilds_groups(); }
    bool rebuild(unsigned p, unsigned first_block, const build_input &in) override {
        if (!child_->rebuild(p, first_block, in))
            return false;
        part &s = parts_[p];
        buffer &q = s.buffers[first_block / group_blocks];
        q.used = 0;
        for (unsigned b = first_block; b < std::min(first_block + group_blocks, blocks_in(p)); ++b)
            s.rows[b] = {}, s.dirty.set(b, false);
        return true;
    }
    std::size_t bytes(unsigned p) const override { return parts_[p].buffers.size() * capacity_ * sizeof(entry) + blocks_in(p) * 32 + 32; }
    u64 digest(unsigned p) const override {
        const part &s = parts_[p];
        u64 h = hash_bytes(hash_bytes(0xcbf29ce484222325, s.rows.data(), sizeof s.rows), &s.dirty, sizeof s.dirty);
        for (const buffer &q : s.buffers)
            h = hash_bytes(h, q.entries.data(), q.used * sizeof(entry));
        return h * 0x100000001b3 ^ child_->digest(p);
    }
    const ikea::byte *resolve(const ikea::journal::entry &e) const override {
        for (const part &s : parts_) {
            if (e.source == &s)
                return s.plane_storage(e.plane).data() + e.offset;
            for (const buffer &q : s.buffers)
                if (e.source == &q)
                    return q.plane_storage(e.plane).data() + e.offset;
        }
        return child_->resolve(e);
    }
    void decode(unsigned p, unsigned b, u64 *out) const override {
        child_->decode(p, b, out);
        each_override(parts_[p], b, [&](const entry &e) { out[e.row % block_rows] = e.value; });
    }
    u128 sum(unsigned p, const bitset256 &blocks) const override {
        const part &s = parts_[p];
        u128 t = child_->sum(p, blocks);
        each(blocks & s.dirty, [&](unsigned b) { each_override(s, b, [&](const entry &e) { t += e.value, t -= e.under; }); });
        return t;
    }
    u64 count(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs) const override {
        const part &s = parts_[p];
        u64 c = child_->count(op, p, blocks, rhs);
        each(blocks & s.dirty, [&](unsigned b) {
            each_override(s, b, [&](const entry &e) { c += u64{test(op, e.value, rhs[b])} - u64{test(op, e.under, rhs[b])}; });
        });
        return c;
    }
    void filter(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs, block_rows_set &out) const override {
        const part &s = parts_[p];
        child_->filter(op, p, blocks, rhs, out);
        each(blocks & s.dirty, [&](unsigned b) { each_override(s, b, [&](const entry &e) { out[b].set(e.row % block_rows, test(op, e.value, rhs[b])); }); });
    }
    u64 get(unsigned p, unsigned row) const override {
        const part &s = parts_[p];
        if (s.rows[row / block_rows].contains(row % block_rows))
            return buffer_of(s, row / block_rows).entries[find(s, row)].value;
        return child_->get(p, row);
    }
    void read(unsigned p, unsigned first, unsigned n, u64 *out) const override {
        const part &s = parts_[p];
        child_->read(p, first, n, out);
        for (unsigned b = first / block_rows; b * block_rows < first + n; ++b)
            each_override(s, b, [&](const entry &e) {
                if (e.row >= first && e.row < first + n)
                    out[e.row - first] = e.value;
            });
    }
    std::size_t select(unsigned p, const bitset256 &blocks, const block_rows_set &rows, u64 *out) const override {
        const part &s = parts_[p];
        const std::size_t k = child_->select(p, blocks, rows, out);
        std::size_t at = 0;
        each(blocks, [&](unsigned b) {
            each_override(s, b, [&](const entry &e) {
                if (rows[b].contains(e.row % block_rows))
                    out[at + rows[b].rank(e.row % block_rows)] = e.value;
            });
            at += rows[b].count();
        });
        return k;
    }

    // An overridden row updates its entry; otherwise the child takes the
    // write if it can absorb it, and an outlier takes a buffer entry.
    verdict classify(unsigned p, unsigned row, u64 value) const override {
        const part &s = parts_[p];
        if (s.rows[row / block_rows].contains(row % block_rows))
            return verdict::absorb;
        if (const verdict k = child_->classify(p, row, value); k == verdict::absorb)
            return k;
        const buffer &q = buffer_of(s, row / block_rows);
        return q.used < capacity_ ? verdict::redirect : verdict::refuse;
    }
    void declare(unsigned p, unsigned row, u64 value, envelope &e) const override {
        const part &s = parts_[p];
        const buffer &q = buffer_of(s, row / block_rows);
        e.reads.add(&s.rows[row / block_rows], 32);
        e.reads.add(q.entries.data(), q.used * sizeof(entry));
        if (s.rows[row / block_rows].contains(row % block_rows)) {
            e.stores.add(q.entries.data(), q.used * sizeof(entry)), e.records += 1;
            return;
        }
        if (child_->classify(p, row, value) == verdict::absorb)
            return child_->declare(p, row, value, e);
        // A new entry, its row bit and the block's dirty bit; the value
        // beneath is an old-value read of the child.
        child_->declare(p, row, value, e);
        e.stores.add(q.entries.data() + q.used, sizeof(entry));
        e.stores.add(&s.rows[row / block_rows], 32);
        e.stores.add(&s.dirty, 32);
        e.records += 3;
    }
    void apply(unsigned p, unsigned row, u64 value, ikea::journal &effects) override {
        part &s = parts_[p];
        const unsigned b = row / block_rows;
        buffer &q = s.buffers[b / group_blocks];
        if (s.rows[b].contains(row % block_rows)) {
            const int i = find(s, row);
            effects.before(q, 0, static_cast<std::size_t>(i) * sizeof(entry), sizeof(entry));
            q.entries[i].value = value;
            return;
        }
        if (child_->classify(p, row, value) == verdict::absorb)
            return child_->apply(p, row, value, effects);
        effects.before(q, 0, std::size_t{q.used} * sizeof(entry), sizeof(entry));
        q.entries[q.used++] = {static_cast<u16>(row), value, child_->get(p, row)};
        effects.before(s, 0, std::size_t{b} * 32, 32);
        effects.before(s, 1, 0, 32);
        s.rows[b].set(row % block_rows), s.dirty.set(b);
    }
};

/// Block statistics a summary owner keeps beside the data: min and max are
/// exact until a write may have removed an extreme, then conservative (still
/// safe for pruning).
struct block_stats {
    u64 min = ~u64{0}, max = 0;
    bool exact = true;
};

/// What the owner learns from one write besides its stores.
struct contribution {
    __int128 sum = 0;   // exact change of the logical SUM
    bool loosened = false;  // the block's bounds became conservative
    bool reencoded = false; // the write re-encoded a group (or part)
};

/// A writable column: classify, declare, apply or re-encode, publish.
class mutable_column {
    column column_;
    std::vector<block_stats> stats_;
    std::vector<ikea::journal::entry> records_;
    bool keep_stats_ = true;

    void refresh(unsigned p, unsigned first_block, unsigned blocks) {
        if (!keep_stats_)
            return;
        alignas(64) u64 x[block_rows];
        for (unsigned b = first_block; b < first_block + blocks; ++b) {
            column_.root().decode(p, b, x);
            block_stats &st = stats_[std::size_t{p} * part_blocks + b];
            const unsigned m = column_.root().rows_in(p, b);
            st = {*std::min_element(x, x + m), *std::max_element(x, x + m), true};
        }
    }

  public:
    std::size_t reencoded_groups = 0, reencoded_parts = 0;
    envelope declared;           // of the last write
    ikea::journal effects{{}};   // of the last write

    /// Over a column that is already built or attached (a draft).
    /// Over a column that is already built or attached (a draft). Without
    /// stats, block statistics stay the owner's (none are kept here).
    explicit mutable_column(column &&c, bool stats = true) : column_(std::move(c)), records_(4096), keep_stats_(stats) {
        stats_.resize(std::size_t{column_.parts()} * part_blocks);
        if (keep_stats_)
            for (unsigned p = 0; p < column_.parts(); ++p)
                refresh(p, 0, column_.root().blocks_in(p));
    }
    column &draft() { return column_; }
    mutable_column(std::unique_ptr<stage> root, std::span<const u64> values) : column_(std::move(root), values), records_(4096) {
        stats_.resize(std::size_t{column_.parts()} * part_blocks);
        for (unsigned p = 0; p < column_.parts(); ++p)
            refresh(p, 0, column_.root().blocks_in(p));
    }
    const column &read() const { return column_; }
    const block_stats &stats(std::size_t row) const { return stats_[row / block_rows]; }

    contribution set(std::size_t row, u64 value) {
        stage &root = column_.root();
        const unsigned p = static_cast<unsigned>(row / part_rows), r = static_cast<unsigned>(row % part_rows), b = r / block_rows;
        contribution out;
        // Classify and declare from interpretation, then read the old value.
        const verdict k = root.classify(p, r, value);
        declared = {};
        if (k != verdict::refuse)
            root.declare(p, r, value, declared);
        const u64 old = root.get(p, r);
        effects = ikea::journal{records_};
        if (k != verdict::refuse) {
            root.apply(p, r, value, effects);
        } else {
            // Re-encode the row's group from its logical values.
            const unsigned first = b / group_blocks * group_blocks, n = std::min(group_blocks, root.blocks_in(p) - first);
            std::vector<u64> rows(std::size_t{n} * block_rows);
            const unsigned count = std::min(n * block_rows, root.rows(p) - first * block_rows);
            root.read(p, first * block_rows, count, rows.data());
            rows[r - first * block_rows] = value;
            rows.resize(count);
            if (root.rebuilds_groups() && root.rebuild(p, first, {rows})) {
                ++reencoded_groups;
                refresh(p, first, n);
            } else {
                std::vector<u64> all(root.rows(p));
                root.read(p, 0, root.rows(p), all.data());
                all[r] = value;
                root.clear(p);
                root.build(p, {all});
                ++reencoded_parts;
                refresh(p, 0, root.blocks_in(p));
            }
            out.reencoded = true;
        }
        out.sum = static_cast<__int128>(value) - static_cast<__int128>(old);
        if (!out.reencoded && keep_stats_) {
            block_stats &st = stats_[std::size_t{p} * part_blocks + b];
            if (st.exact && value != old && (old == st.min || old == st.max))
                st.exact = false, out.loosened = true;
            st.min = std::min(st.min, value), st.max = std::max(st.max, value);
        }
        return out;
    }
    /// Issued stores of the last write that its declaration did not cover.
    std::size_t undeclared_bytes() const {
        std::size_t n = 0;
        for (const auto &e : effects.entries()) {
            const auto *at = column_.root().resolve(e);
            if (!at)
                return ~std::size_t{0};
            frame_change::ranges one;
            one.add(at, e.size);
            if (!declared.stores.contains(one))
                n += e.size;
        }
        return n;
    }
};
}  // namespace frame3

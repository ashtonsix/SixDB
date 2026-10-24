// Prototype of the phase 3 stage contract (../README.md, ideas 1-3).
//
// A column is a tree of stages over parts of 256 blocks of 256 rows. Every
// stage can decode a block; every operation has a reference definition over
// decoded values (the defaults below), so any (stage, operation) cell works.
// A stage overrides an operation with a law: a rewrite into operations on its
// children plus block-grain arithmetic over its descriptor columns.
//
// Dispatch is per part and stage, never per row: a call names one part and a
// coarse selection of its blocks (one bit per block), with per-block
// arguments. Leaves batch blocks of one specialisation into one kernel call.
#pragma once
#include <ikea/bitset256.h>
#include <ikea/comparison.h>
#include <ikea/effects.h>

#include "../../frame-phase-2/change/envelope.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace frame3 {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using u128 = unsigned __int128;
using ikea::bitset256;
using ikea::comparison;

inline constexpr unsigned block_rows = 256, part_blocks = 256;
/// Blocks per group: the unit of placement, slack and re-encode (idea 3).
inline constexpr unsigned group_blocks = 16;
inline constexpr std::size_t part_rows = std::size_t{block_rows} * part_blocks;

/// Per-block operation arguments, indexed by block within the part.
using block_args = std::array<u64, part_blocks>;
/// Fine selections: original rows of each block, indexed by block.
using block_rows_set = std::array<bitset256, part_blocks>;

/// FNV-1a over bytes, for digests.
/// Little-endian scalar fields in an image.
template <class T> void put(std::vector<std::byte> &out, T v) {
    const auto *b = reinterpret_cast<const std::byte *>(&v);
    out.insert(out.end(), b, b + sizeof v);
}
template <class T> T take(std::span<const std::byte> in, std::size_t &at) {
    T v;
    std::memcpy(&v, in.data() + at, sizeof v);
    at += sizeof v;
    return v;
}
inline u64 hash_bytes(u64 h, const void *p, std::size_t n) {
    const auto *c = static_cast<const unsigned char *>(p);
    for (std::size_t i = 0; i < n; ++i)
        h = (h ^ c[i]) * 0x100000001b3;
    return h;
}

inline bool test(comparison op, u64 x, u64 t) {
    switch (op) {
    case comparison::equal: return x == t;
    case comparison::less: return x < t;
    case comparison::less_equal: return x <= t;
    case comparison::greater: return x > t;
    case comparison::greater_equal: return x >= t;
    }
    return false;
}

/// Visit the set positions of a bitset in order.
template <class F> inline void each(const bitset256 &s, F &&f) {
    for (unsigned w = 0; w < 4; ++w)
        for (u64 m = s.words[w]; m; m &= m - 1)
            f(w * 64 + static_cast<unsigned>(std::countr_zero(m)));
}
inline bitset256 operator&(const bitset256 &a, const bitset256 &b) {
    return {{a.words[0] & b.words[0], a.words[1] & b.words[1], a.words[2] & b.words[2], a.words[3] & b.words[3]}};
}
inline bitset256 first_n(unsigned n) {
    bitset256 s;
    for (unsigned w = 0; w < 4; ++w)
        s.words[w] = n >= 64 * (w + 1) ? ~u64{0} : n > 64 * w ? (u64{1} << (n - 64 * w)) - 1 : 0;
    return s;
}

struct width_array;

/// How a subtree takes a logical write, decided from interpretation alone
/// (descriptors, exception positions), before any payload is read.
enum class verdict : u8 { absorb, redirect, refuse };
using frame_change::envelope;

/// What a parent hands its child when it builds one part. A child's rows per
/// part need not match its parent's (runs has fewer); only the last block of
/// a part may be partial.
struct build_input {
    std::span<const u64> values;  // the part's rows
    const u8 *widths = nullptr;   // per block: the width a parent fixed, or null
};

/// What a subtree tells Engine about one part (idea 5), as by-products of
/// fitting: the width each block packs at, exceptions per block, and blocks
/// whose values never decrease.
struct census {
    std::array<std::array<u32, 5>, 65> blocks{};  // [width][exception bucket: 0, 1-2, 3-8, 9-32, 33+]
    u64 exceptions = 0, monotone = 0, checkpoints = 0;
    static unsigned bucket(unsigned e) { return e == 0 ? 0 : e <= 2 ? 1 : e <= 8 ? 2 : e <= 32 ? 3 : 4; }
};

class stage {
    std::vector<u32> rows_;  // per part

  protected:
    void sized(unsigned part, std::size_t rows) {
        if (rows_.size() <= part)
            rows_.resize(part + 1);
        rows_[part] = static_cast<u32>(rows);
    }

  public:
    virtual ~stage() = default;
    unsigned rows(unsigned part) const { return rows_[part]; }
    unsigned blocks_in(unsigned part) const { return (rows_[part] + block_rows - 1) / block_rows; }
    unsigned rows_in(unsigned part, unsigned block) const { return std::min(block_rows, rows_[part] - block * block_rows); }

    virtual const char *name() const = 0;
    virtual std::span<const std::unique_ptr<stage>> children() const { return {}; }

    // Fit and emit one part; parts are built in order.
    virtual void build(unsigned part, const build_input &) = 0;
    // The decode fallback: the rows of one block (rows_in of them).
    virtual void decode(unsigned part, unsigned block, u64 *out) const = 0;
    // This stage's own bytes for a part (descriptor columns, side data, bodies).
    virtual std::size_t bytes(unsigned part) const = 0;
    // Bytes this stage's subtree spends on one block of width K, for a
    // parent that prices widths (bound's cost model). Only leaves answer.
    virtual std::size_t body_bytes(unsigned) const { return 0; }

    // Change (idea 4). The defaults refuse every write, so the owner
    // re-encodes: every stage supports writes before it has a write law.
    virtual verdict classify(unsigned, unsigned, u64) const { return verdict::refuse; }
    // Possible reads, stores and effect records of applying a classified write.
    virtual void declare(unsigned, unsigned, u64, envelope &) const {}
    virtual void apply(unsigned, unsigned, u64, ikea::journal &) {}
    // Re-encode one group from its rows (in.values starts at the group's
    // first row). False: this subtree only rebuilds whole parts.
    virtual bool rebuild(unsigned, unsigned, const build_input &) { return false; }
    // Whether rebuild can succeed, asked before anything changes: a refused
    // rebuild must leave the tree as it was.
    virtual bool rebuilds_groups() const { return false; }
    // The bytes an effect record names, if this subtree issued it.
    virtual const ikea::byte *resolve(const ikea::journal::entry &e) const {
        for (const auto &c : children())
            if (const auto *at = c->resolve(e))
                return at;
        return nullptr;
    }
    // A digest of this subtree's bytes for a part (replay determinism checks).
    virtual u64 digest(unsigned part) const {
        u64 h = 0;
        for (const auto &c : children())
            h = h * 0x100000001b3 ^ c->digest(part);
        return h;
    }
    // A leaf's direct entry for a fused point path: the width array and row
    // base of block b. Only leaves answer.
    virtual bool leaf_entry(unsigned, unsigned, const width_array *&, std::size_t &) const { return false; }
    // Headless group images (idea 3). emit appends this stage's section of a
    // group's image, then its children's; attach reads them back from bytes
    // an owner placed (an Orbital Part), borrowing payload where it can.
    // writable, when not empty, is the same image as a private draft the tree
    // may store into; otherwise the image is read-only.
    virtual void emit(unsigned part, unsigned group, std::vector<std::byte> &out) const {
        for (const auto &c : children())
            c->emit(part, group, out);
    }
    virtual std::size_t attach(unsigned part, std::size_t rows, unsigned group, std::span<const std::byte> image, std::span<std::byte> writable) {
        sized(part, rows);
        std::size_t used = 0;
        for (const auto &c : children())
            used += c->attach(part, rows, group, image.subspan(used), writable.empty() ? writable : writable.subspan(used));
        return used;
    }
    unsigned groups_in(unsigned part) const { return (blocks_in(part) + group_blocks - 1) / group_blocks; }
    virtual void describe(unsigned part, census &c) const {
        for (const auto &ch : children())
            ch->describe(part, c);
    }
    // Forget a part so build can run again (the whole-part fallback).
    virtual void clear(unsigned part) {
        for (const auto &c : children())
            c->clear(part);
    }

    // Laws. The defaults are the reference definitions over decode.
    virtual u128 sum(unsigned part, const bitset256 &blocks) const;
    // SUM over the selected rows of the selected blocks (SUM ... WHERE).
    virtual u128 sum_selected(unsigned part, const bitset256 &blocks, const block_rows_set &rows) const;
    // The same with one row pattern for every block (a parent's fixed law,
    // such as delta's position weights). Leaves can cover runs of blocks.
    virtual u128 sum_pattern(unsigned part, const bitset256 &blocks, const bitset256 &pattern) const {
        block_rows_set rows;
        each(blocks, [&](unsigned b) { rows[b] = pattern; });
        return sum_selected(part, blocks, rows);
    }
    virtual u64 count(comparison, unsigned part, const bitset256 &blocks, const block_args &rhs) const;
    // The same with one threshold for every block (a query's constant), so a
    // stage can settle blocks in one pass over its descriptor columns.
    virtual u64 count_uniform(comparison op, unsigned part, const bitset256 &blocks, u64 t) const {
        block_args rhs;
        rhs.fill(t);
        return count(op, part, blocks, rhs);
    }
    virtual void filter(comparison, unsigned part, const bitset256 &blocks, const block_args &rhs, block_rows_set &out) const;
    virtual u64 get(unsigned part, unsigned row) const;
    virtual void read(unsigned part, unsigned first, unsigned n, u64 *out) const;
    // Values of the selected rows of the selected blocks, in row order.
    virtual std::size_t select(unsigned part, const bitset256 &blocks, const block_rows_set &rows, u64 *out) const;
};

inline u128 stage::sum(unsigned part, const bitset256 &blocks) const {
    alignas(64) u64 x[block_rows];
    u128 s = 0;
    each(blocks, [&](unsigned b) {
        decode(part, b, x);
        for (unsigned r = 0; r < rows_in(part, b); ++r)
            s += x[r];
    });
    return s;
}
inline u128 stage::sum_selected(unsigned part, const bitset256 &blocks, const block_rows_set &rows) const {
    alignas(64) u64 x[block_rows];
    u128 s = 0;
    each(blocks, [&](unsigned b) {
        decode(part, b, x);
        each(rows[b] & first_n(rows_in(part, b)), [&](unsigned r) { s += x[r]; });
    });
    return s;
}
inline u64 stage::count(comparison op, unsigned part, const bitset256 &blocks, const block_args &rhs) const {
    alignas(64) u64 x[block_rows];
    u64 c = 0;
    each(blocks, [&](unsigned b) {
        decode(part, b, x);
        for (unsigned r = 0; r < rows_in(part, b); ++r)
            c += test(op, x[r], rhs[b]);
    });
    return c;
}
inline void stage::filter(comparison op, unsigned part, const bitset256 &blocks, const block_args &rhs, block_rows_set &out) const {
    alignas(64) u64 x[block_rows];
    each(blocks, [&](unsigned b) {
        decode(part, b, x);
        bitset256 m;
        for (unsigned r = 0; r < rows_in(part, b); ++r)
            m.words[r / 64] |= u64{test(op, x[r], rhs[b])} << (r % 64);
        out[b] = m;
    });
}
inline u64 stage::get(unsigned part, unsigned row) const {
    alignas(64) u64 x[block_rows];
    decode(part, row / block_rows, x);
    return x[row % block_rows];
}
inline void stage::read(unsigned part, unsigned first, unsigned n, u64 *out) const {
    alignas(64) u64 x[block_rows];
    while (n) {
        const unsigned b = first / block_rows, r = first % block_rows, m = std::min(n, block_rows - r);
        decode(part, b, x);
        std::copy_n(x + r, m, out);
        out += m, first += m, n -= m;
    }
}
inline std::size_t stage::select(unsigned part, const bitset256 &blocks, const block_rows_set &rows, u64 *out) const {
    alignas(64) u64 x[block_rows];
    std::size_t k = 0;
    each(blocks, [&](unsigned b) {
        if (rows[b].empty())
            return;
        decode(part, b, x);
        each(rows[b], [&](unsigned r) { out[k++] = x[r]; });
    });
    return k;
}

/// A column: one stage tree over consecutive parts. Its commands cover whole
/// columns; a real reader would also take row ranges and selections.
class column {
    std::unique_ptr<stage> root_;
    std::size_t rows_ = 0;
    unsigned parts_ = 0;

  public:
    column(std::unique_ptr<stage> root, std::span<const u64> values) : root_(std::move(root)), rows_(values.size()) {
        parts_ = static_cast<unsigned>((rows_ + part_rows - 1) / part_rows);
        for (unsigned p = 0; p < parts_; ++p) {
            const std::size_t first = p * part_rows, n = std::min(part_rows, rows_ - first);
            root_->build(p, {values.subspan(first, n)});
        }
    }
    /// Attach a tree to placed group images: images[p][g] is part p's group g.
    column(std::unique_ptr<stage> root, std::span<const std::size_t> part_rows_, const std::vector<std::vector<std::span<const std::byte>>> &images,
           const std::vector<std::vector<std::span<std::byte>>> *writable = nullptr)
        : root_(std::move(root)), parts_(static_cast<unsigned>(part_rows_.size())) {
        for (unsigned p = 0; p < parts_; ++p) {
            rows_ += part_rows_[p];
            for (unsigned g = 0; g < images[p].size(); ++g)
                root_->attach(p, part_rows_[p], g, images[p][g], writable ? (*writable)[p][g] : std::span<std::byte>{});
        }
    }
    std::vector<std::byte> image(unsigned part, unsigned group) const {
        std::vector<std::byte> out;
        root_->emit(part, group, out);
        return out;
    }
    const stage &root() const { return *root_; }
    stage &root() { return *root_; }
    std::size_t size() const { return rows_; }
    unsigned parts() const { return parts_; }
    bitset256 blocks(unsigned part) const { return first_n(root_->blocks_in(part)); }
    census describe() const {
        census c;
        for (unsigned p = 0; p < parts_; ++p)
            root_->describe(p, c);
        return c;
    }
    u64 digest() const {
        u64 h = 0xcbf29ce484222325;
        for (unsigned p = 0; p < parts_; ++p)
            h = h * 0x100000001b3 ^ root_->digest(p);
        return h;
    }
    std::size_t bytes() const {
        std::size_t total = 0;
        auto walk = [&](auto &self, const stage &s) -> void {
            for (unsigned p = 0; p < parts_; ++p)
                total += s.bytes(p);
            for (const auto &c : s.children())
                self(self, *c);
        };
        walk(walk, *root_);
        return total;
    }

    u128 sum() const {
        u128 s = 0;
        for (unsigned p = 0; p < parts_; ++p)
            s += root_->sum(p, blocks(p));
        return s;
    }
    /// SUM of the rows selected by words (bit r of words[r / 64] is row r).
    u128 sum(const u64 *words) const {
        block_rows_set rows;
        u128 s = 0;
        for (unsigned p = 0; p < parts_; ++p) {
            bitset256 bl;
            each(blocks(p), [&](unsigned b) {
                const u64 *w = words + (p * part_rows + b * block_rows) / 64;
                rows[b] = {};
                std::copy_n(w, (root_->rows_in(p, b) + 63) / 64, rows[b].words.data());
                if (!rows[b].empty())
                    bl.set(b);
            });
            if (!bl.empty())
                s += root_->sum_selected(p, bl, rows);
        }
        return s;
    }
    u64 count(comparison op, u64 t) const {
        u64 c = 0;
        for (unsigned p = 0; p < parts_; ++p)
            c += root_->count_uniform(op, p, blocks(p), t);
        return c;
    }
    // Selection words: bit r of words[r / 64] is row r.
    void filter(comparison op, u64 t, u64 *words) const {
        block_args rhs;
        rhs.fill(t);
        block_rows_set out;
        for (unsigned p = 0; p < parts_; ++p) {
            const bitset256 bl = blocks(p);
            root_->filter(op, p, bl, rhs, out);
            each(bl, [&](unsigned b) {
                const unsigned m = (root_->rows_in(p, b) + 63) / 64;
                std::copy_n(out[b].words.data(), m, words + (p * part_rows + b * block_rows) / 64);
            });
        }
    }
    u64 get(std::size_t row) const {
        return root_->get(static_cast<unsigned>(row / part_rows), static_cast<unsigned>(row % part_rows));
    }
    void read(u64 *out) const {
        for (unsigned p = 0; p < parts_; ++p) {
            const std::size_t first = p * part_rows, n = std::min(part_rows, rows_ - first);
            root_->read(p, 0, static_cast<unsigned>(n), out + first);
        }
    }
    // Values of rows selected by words, in row order.
    std::size_t select(const u64 *words, u64 *out) const {
        block_rows_set rows;
        std::size_t k = 0;
        for (unsigned p = 0; p < parts_; ++p) {
            bitset256 bl;
            each(blocks(p), [&](unsigned b) {
                const u64 *w = words + (p * part_rows + b * block_rows) / 64;
                const unsigned m = (root_->rows_in(p, b) + 63) / 64;
                rows[b] = {};
                std::copy_n(w, m, rows[b].words.data());
                if (!rows[b].empty())
                    bl.set(b);
            });
            k += root_->select(p, bl, rows, out + k);
        }
        return k;
    }
};
}  // namespace frame3

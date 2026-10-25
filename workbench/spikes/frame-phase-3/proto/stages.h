// The three landing stages of the phase 3 proposal, over the contract in
// frame.h:
//
//   pack       leaf. Blocks of equal width share one SeriesPack array per
//              part, so a range kernel covers every selected block of that
//              width in one call. Width 0 is a constant (all-zero) block with
//              no body.
//   bound      lo and width per block, chosen by phase 2's bounds stage
//              (../../frame-phase-2/bounds/stage.h, reused unchanged);
//              values outside the window become exceptions (u8 position,
//              value) and their slots hold 0.
//   intervals  delta in Intervals' sizes form: a checkpoint (the value) every
//              G rows, offset-binary deltas between them, so signed deltas
//              stay a contiguous window for bound.
//
// Laws written here: bound's sum, count, filter, get, read and select; pack's
// sum, count, filter, get, read and decode; intervals' get and its monotone
// count. Everything else uses frame.h's reference definitions.
#pragma once
#include "frame.h"

#include "../../frame-phase-2/bounds/stage.h"

#include <ikea/intervals.h>
#include <ikea/seriespack.h>

#include <cassert>
#include <cstdlib>
#include <optional>

namespace frame3 {
namespace sp = ikea::seriespack;
using frame_bounds::mask;

/// A width's point kernel and its array's bytes: a direct call, no object.
struct point_kernel {
    u64 (*get)(const void *, std::size_t);
    const void *resource;    // what get takes
    const ikea::byte *data;  // the array's bytes, for prefetch
    unsigned tile_rows = 8, tile_bytes = 0;  // for prefetching a row's tile
};
// One SeriesPack array of a part's width-K blocks, slot order = block order.
struct width_array {
    virtual point_kernel point() const = 0;
    virtual ~width_array() = default;
    virtual std::size_t bytes() const = 0;
    virtual u128 sum(const bitset256 &members, const bitset256 &blocks) const = 0;
    // Selected rows: per block (rows) or one pattern for all of them.
    virtual u128 sum_selected(const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_rows_set &selected) const = 0;
    virtual u128 sum_pattern(const bitset256 &members, const bitset256 &blocks, const bitset256 &pattern) const = 0;
    // slot and rows: each block's slot in this array and its row count.
    virtual u64 count(comparison, const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_args &rhs) const = 0;
    virtual void filter(comparison, const bitset256 &blocks, const u8 *slot, const u16 *rows, const block_args &rhs, block_rows_set &out) const = 0;
    virtual u64 get(std::size_t row) const = 0;
    virtual void read(std::size_t first, unsigned n, u64 *out) const = 0;
    // Writes through SeriesPack's checked setter; declare gives its envelope.
    virtual void declare(std::size_t row, envelope &) const = 0;
    virtual bool set(std::size_t row, u64 value, ikea::journal &) = 0;
    virtual const ikea::byte *resolve(const ikea::journal::entry &) const = 0;
    virtual u64 digest() const = 0;
    virtual std::span<const ikea::byte> image() const = 0;
    virtual std::size_t rows() const = 0;
};

// Width-specialised arrays are compiled once per preset and width range in
// widths_*.cpp (widths.h), never in the TUs that compose stages.
template <sp::preset P> std::unique_ptr<width_array> make_width_array(unsigned K, const std::vector<u64> &values);
template <sp::preset P> std::size_t width_body_bytes(unsigned K);
/// Whether this build instantiated preset P's width arrays (FRAME3_PRESETS);
/// a checked factory refuses an omitted preset instead of reaching abort.
bool width_preset_available(sp::preset p) noexcept;
template <sp::preset P>
std::unique_ptr<width_array> place_width_array(unsigned K, std::span<const ikea::byte> bytes, std::size_t rows, std::span<ikea::byte> writable);

template <sp::preset P> class pack final : public stage {
    // Equal-width blocks of one placement group share an array. Groups of
    // part_blocks place a whole part together (fewest kernel calls);
    // group_blocks keep re-encode, slack and copy-on-write group-local.
    struct group {
        std::array<bitset256, 65> members{};
        std::array<std::unique_ptr<width_array>, 65> arrays;
        std::vector<u8> present;  // widths with blocks, ascending
    };
    struct part {
        std::array<u8, part_blocks> width{}, slot{};
        std::array<u16, part_blocks> rows{};
        std::vector<group> groups;
    };
    std::vector<part> parts_;
    unsigned placement_;

    void place(part &s, unsigned g, unsigned first_block, unsigned blocks, const build_input &in) {
        group &q = s.groups[g] = group{};
        std::array<std::vector<u64>, 65> staged;
        for (unsigned i = 0; i < blocks; ++i) {
            const unsigned b = first_block + i, m = s.rows[b];
            const u64 *x = in.values.data() + std::size_t{i} * block_rows;
            unsigned K = in.widths ? in.widths[i] : frame_bounds::bits(*std::max_element(x, x + m));
            for (unsigned r = 0; r < m; ++r)
                if (x[r] > mask(K))
                    std::abort();  // a parent fixed a width its values do not fit
            // Only a part's last block is partial, so it is last in its array too.
            s.width[b] = static_cast<u8>(K), s.slot[b] = static_cast<u8>(staged[K].size() / block_rows), q.members[K].set(b);
            if (K)
                staged[K].insert(staged[K].end(), x, x + m);
        }
        for (unsigned K = 0; K <= 64; ++K)
            if (!q.members[K].empty()) {
                q.present.push_back(static_cast<u8>(K));
                if (K)
                    q.arrays[K] = make_width_array<P>(K, staged[K]);
            }
    }
    width_array *array_of(const part &s, unsigned b) const { return s.groups[b / placement_].arrays[s.width[b]].get(); }
    template <class F> void each_width(const part &s, const bitset256 &blocks, F &&f) const {
        for (const group &q : s.groups)
            for (unsigned K : q.present)
                if (const bitset256 m = q.members[K] & blocks; !m.empty())
                    f(q, K, m);
    }

  public:
    explicit pack(unsigned placement = part_blocks) : placement_(placement) {}
    const char *name() const override { return "pack"; }
    std::size_t body_bytes(unsigned K) const override { return K ? width_body_bytes<P>(K) : 0; }
    void build(unsigned p, const build_input &in) override {
        if (parts_.size() <= p)
            parts_.resize(p + 1);
        part &s = parts_[p];
        sized(p, in.values.size());
        for (unsigned b = 0; b < blocks_in(p); ++b)
            s.rows[b] = static_cast<u16>(rows_in(p, b));
        s.groups.resize((blocks_in(p) + placement_ - 1) / placement_);
        for (unsigned g = 0; g < s.groups.size(); ++g) {
            const unsigned first = g * placement_, n = std::min(placement_, blocks_in(p) - first);
            place(s, g, first, n, {in.values.subspan(std::size_t{first} * block_rows), in.widths ? in.widths + first : nullptr});
        }
    }
    void clear(unsigned p) override { parts_[p] = {}; }
    u64 digest(unsigned p) const override {
        const part &s = parts_[p];
        u64 h = hash_bytes(hash_bytes(0xcbf29ce484222325, s.width.data(), part_blocks), s.slot.data(), part_blocks);
        for (const group &q : s.groups)
            for (unsigned K : q.present)
                h = K ? h * 0x100000001b3 ^ q.arrays[K]->digest() : h;
        return h;
    }
    std::size_t bytes(unsigned p) const override {
        std::size_t n = 2 * blocks_in(p);  // width and slot columns
        for (const group &q : parts_[p].groups)
            for (unsigned K : q.present)
                if (K)
                    n += q.arrays[K]->bytes();
        return n;
    }
    void decode(unsigned p, unsigned b, u64 *out) const override {
        const part &s = parts_[p];
        if (const auto *a = array_of(s, b))
            a->read(std::size_t{s.slot[b]} * block_rows, s.rows[b], out);
        else
            std::fill_n(out, s.rows[b], u64{0});
    }
    u128 sum(unsigned p, const bitset256 &blocks) const override {
        u128 t = 0;
        each_width(parts_[p], blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            if (K)
                t += q.arrays[K]->sum(q.members[K], m);
        });
        return t;
    }
    u128 sum_selected(unsigned p, const bitset256 &blocks, const block_rows_set &rows) const override {
        const part &s = parts_[p];
        u128 t = 0;
        each_width(s, blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            if (K)
                t += q.arrays[K]->sum_selected(m, s.slot.data(), s.rows.data(), rows);
        });
        return t;
    }
    u128 sum_pattern(unsigned p, const bitset256 &blocks, const bitset256 &pattern) const override {
        u128 t = 0;
        each_width(parts_[p], blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            if (K)
                t += q.arrays[K]->sum_pattern(q.members[K], m, pattern);
        });
        return t;
    }
    u64 count(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs) const override {
        const part &s = parts_[p];
        u64 c = 0;
        each_width(s, blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            if (K)
                c += q.arrays[K]->count(op, m, s.slot.data(), s.rows.data(), rhs);
            else
                each(m, [&](unsigned b) { c += test(op, 0, rhs[b]) ? s.rows[b] : 0; });
        });
        return c;
    }
    void filter(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs, block_rows_set &out) const override {
        const part &s = parts_[p];
        each_width(s, blocks, [&](const group &q, unsigned K, const bitset256 &m) {
            if (K)
                q.arrays[K]->filter(op, m, s.slot.data(), s.rows.data(), rhs, out);
            else
                each(m, [&](unsigned b) { out[b] = test(op, 0, rhs[b]) ? first_n(s.rows[b]) : bitset256{}; });
        });
    }
    u64 get(unsigned p, unsigned row) const override {
        const part &s = parts_[p];
        const unsigned b = row / block_rows;
        const auto *a = array_of(s, b);
        return a ? a->get(std::size_t{s.slot[b]} * block_rows + row % block_rows) : 0;
    }
    // Image section: per block width, slot and rows; then each width's
    // array as (K, rows, bytes, the bytes).
    void emit(unsigned p, unsigned g, std::vector<std::byte> &out) const override {
        assert(placement_ == group_blocks);
        const part &s = parts_[p];
        const group &q = s.groups[g];
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(p) - first);
        for (unsigned b = first; b < first + n; ++b)
            put<u8>(out, s.width[b]), put<u8>(out, s.slot[b]), put<u16>(out, s.rows[b]);
        put<u8>(out, static_cast<u8>(q.present.size()));
        for (unsigned K : q.present) {
            const auto bytes = K ? q.arrays[K]->image() : std::span<const ikea::byte>{};
            put<u8>(out, static_cast<u8>(K)), put<u32>(out, K ? static_cast<u32>(q.arrays[K]->rows()) : 0), put<u32>(out, static_cast<u32>(bytes.size()));
            const auto *at = reinterpret_cast<const std::byte *>(bytes.data());
            out.insert(out.end(), at, at + bytes.size());
        }
    }
    std::size_t attach(unsigned p, std::size_t rows, unsigned g, std::span<const std::byte> in, std::span<std::byte> writable) override {
        assert(placement_ == group_blocks);
        sized(p, rows);
        if (parts_.size() <= p)
            parts_.resize(p + 1);
        part &s = parts_[p];
        s.groups.resize(groups_in(p));
        group &q = s.groups[g] = group{};
        std::size_t at = 0;
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(p) - first);
        for (unsigned b = first; b < first + n; ++b) {
            s.width[b] = take<u8>(in, at), s.slot[b] = take<u8>(in, at), s.rows[b] = take<u16>(in, at);
            q.members[s.width[b]].set(b);
        }
        const unsigned present = take<u8>(in, at);
        for (unsigned i = 0; i < present; ++i) {
            const unsigned K = take<u8>(in, at), n_rows = take<u32>(in, at), n_bytes = take<u32>(in, at);
            q.present.push_back(static_cast<u8>(K));
            if (K) {
                const auto bytes = std::span(reinterpret_cast<const ikea::byte *>(in.data() + at), n_bytes);
                const auto w = writable.empty() ? std::span<ikea::byte>{} : std::span(reinterpret_cast<ikea::byte *>(writable.data() + at), n_bytes);
                q.arrays[K] = place_width_array<P>(K, bytes, n_rows, w);
            }
            at += n_bytes;
        }
        return at;
    }
    bool leaf_entry(unsigned p, unsigned b, const width_array *&leaf, std::size_t &base) const override {
        const part &s = parts_[p];
        leaf = array_of(s, b), base = std::size_t{s.slot[b]} * block_rows;
        return true;
    }
    void read(unsigned p, unsigned first, unsigned n, u64 *out) const override {
        const part &s = parts_[p];
        while (n) {
            const unsigned b = first / block_rows, r = first % block_rows, m = std::min(n, s.rows[b] - r);
            if (const auto *a = array_of(s, b))
                a->read(std::size_t{s.slot[b]} * block_rows + r, m, out);
            else
                std::fill_n(out, m, u64{0});
            out += m, first += m, n -= m;
        }
    }
    // A field that fits its block's width is stored in place; width 0 has no field.
    verdict classify(unsigned p, unsigned row, u64 value) const override {
        const part &s = parts_[p];
        const unsigned K = s.width[row / block_rows];
        return K && value <= mask(K) ? verdict::absorb : value == 0 && !K ? verdict::absorb : verdict::refuse;
    }
    void declare(unsigned p, unsigned row, u64, envelope &e) const override {
        const part &s = parts_[p];
        const unsigned b = row / block_rows;
        if (const auto *a = array_of(s, b))
            a->declare(std::size_t{s.slot[b]} * block_rows + row % block_rows, e);
    }
    void apply(unsigned p, unsigned row, u64 value, ikea::journal &effects) override {
        const part &s = parts_[p];
        const unsigned b = row / block_rows;
        if (auto *a = array_of(s, b))
            if (!a->set(std::size_t{s.slot[b]} * block_rows + row % block_rows, value, effects))
                std::abort();  // classify admitted it
    }
    const ikea::byte *resolve(const ikea::journal::entry &e) const override {
        for (const part &s : parts_)
            for (const group &q : s.groups)
                for (unsigned K : q.present)
                    if (K)
                        if (const auto *at = q.arrays[K]->resolve(e))
                            return at;
        return nullptr;
    }
    bool rebuilds_groups() const override { return placement_ == group_blocks; }  // part-wide placement moves every equal-width block
    bool rebuild(unsigned p, unsigned first_block, const build_input &in) override {
        if (!rebuilds_groups() || first_block % placement_)
            return false;
        part &s = parts_[p];
        place(s, first_block / placement_, first_block, std::min(placement_, blocks_in(p) - first_block), in);
        return true;
    }
};

class bound final : public stage {
    std::unique_ptr<stage> child_;
    unsigned penalty_, cap_, slack_;
    // Each group's exceptions live in one area with spare slots: write-time
    // outliers take a spare slot (shifting later blocks of the group), and an
    // exhausted area refuses so the owner re-encodes the group.
    // Effect sources: an area's planes are 0 positions, 1 values; a part's
    // are 0 starts, 1 counts.
    struct area {
        std::vector<u8> pos;
        std::vector<u64> val;
        unsigned used = 0;
        std::span<const ikea::byte> plane_storage(unsigned plane) const noexcept {
            return plane ? std::as_bytes(std::span(val)) : std::as_bytes(std::span(pos));
        }
    };
    struct part {
        std::array<u64, part_blocks> lo{};
        std::array<u8, part_blocks> width{};
        std::array<u16, part_blocks> start{}, count{};  // block b's exceptions: [start, start + count) of its group's area
        std::vector<area> areas;
        std::span<const ikea::byte> plane_storage(unsigned plane) const noexcept {
            return plane ? std::as_bytes(std::span(count)) : std::as_bytes(std::span(start));
        }
    };
    std::vector<part> parts_;

    frame_bounds::cost_model costs(unsigned penalty) const {
        frame_bounds::cost_model c;
        c.exception_penalty = penalty, c.max_exceptions = cap_;
        for (unsigned K = 1; K <= 64; ++K)
            c.body[K] = static_cast<u32>(child_->body_bytes(K));
        c.body[0] = c.body[1];
        return c;
    }
    // How a comparison against t settles a block's inliers [lo, lo + 2^K).
    enum class settle { none, all, partial };
    static settle classify(comparison op, u64 t, u64 lo, unsigned K, u64 &d) {
        const u64 m = mask(K);
        if (t < lo)
            return op == comparison::greater_equal || op == comparison::greater ? settle::all : settle::none;
        d = t - lo;
        switch (op) {
        case comparison::greater_equal: return d == 0 ? settle::all : d > m ? settle::none : settle::partial;
        case comparison::greater: return d >= m ? settle::none : settle::partial;
        case comparison::less_equal: return d >= m ? settle::all : settle::partial;
        case comparison::less: return d == 0 ? settle::none : d > m ? settle::all : settle::partial;
        case comparison::equal: return d > m ? settle::none : settle::partial;
        }
        return settle::none;
    }
    const area &area_of(const part &s, unsigned b) const { return s.areas[b / group_blocks]; }
    template <class F> void each_exception(const part &s, unsigned b, F &&f) const {
        const area &a = area_of(s, b);
        for (unsigned e = s.start[b]; e < s.start[b] + s.count[b]; ++e)
            f(a.pos[e], a.val[e]);
    }
    // Index of row r's exception in its area, or -1.
    int find(const part &s, unsigned b, unsigned r) const {
        const area &a = area_of(s, b);
        for (unsigned e = s.start[b]; e < s.start[b] + s.count[b]; ++e)
            if (a.pos[e] == r)
                return static_cast<int>(e);
        return -1;
    }
    bool inside(const part &s, unsigned b, u64 v) const { return v >= s.lo[b] && v - s.lo[b] <= mask(s.width[b]); }

    // Fit, emit and lay out blocks [first, first + n) of a part, the prior
    // being the previous block's window (so a rebuild fits as a build did).
    void fit(unsigned p, unsigned first, unsigned n, const build_input &in, std::vector<u64> &residuals, std::array<u8, part_blocks> &widths) {
        part &s = parts_[p];
        const auto priced = costs(penalty_), sample = frame_bounds::sample_cost(priced, block_rows / 32);
        residuals.assign(in.values.size(), 0);
        for (unsigned g = first / group_blocks; g * group_blocks < first + n; ++g) {
            area &a = s.areas[g] = area{};
            for (unsigned b = std::max(first, g * group_blocks); b < std::min(first + n, (g + 1) * group_blocks); ++b) {
                const std::size_t at = std::size_t{b - first} * block_rows;
                const u64 *x = in.values.data() + at;
                const unsigned m = rows_in(p, b);
                const auto [mn, mx] = std::minmax_element(x, x + m);
                u64 lo = *mn;
                unsigned K = 0;
                if (*mx != *mn) {
                    // The prior is the previous block's window within the
                    // group only: a group's first block fits without one, so a
                    // group rebuilt or attached alone fits as its build did.
                    std::optional<frame_bounds::choice> prior;
                    if (b % group_blocks && s.width[b - 1])
                        prior = frame_bounds::choice{s.lo[b - 1], s.width[b - 1], 0, 0, 0};
                    const auto c = frame_bounds::fast::choose<32>(x, m, priced, sample, prior ? &*prior : nullptr);
                    lo = c.lo, K = std::max(1u, c.width);
                }
                s.lo[b] = lo, s.width[b] = widths[b - first] = static_cast<u8>(K);
                s.start[b] = static_cast<u16>(a.pos.size());
                for (unsigned r = 0; r < m; ++r) {
                    const bool out = x[r] < lo || x[r] - lo > mask(K);
                    if (out)
                        a.pos.push_back(static_cast<u8>(r)), a.val.push_back(x[r]);
                    residuals[at + r] = out ? 0 : x[r] - lo;
                }
                s.count[b] = static_cast<u16>(a.pos.size() - s.start[b]);
                if (s.count[b] > cap_)
                    std::abort();  // the cost model admits no more
            }
            a.used = static_cast<unsigned>(a.pos.size());
            a.pos.resize(a.used + slack_), a.val.resize(a.used + slack_);
        }
    }

  public:
    /// penalty: Engine's per-exception read-time charge in bytes; cap: the
    /// most exceptions a block may hold (64 fits one register of positions);
    /// slack: spare exception slots per group for write-time outliers.
    explicit bound(std::unique_ptr<stage> child, unsigned penalty = 0, unsigned cap = 64, unsigned slack = 0)
        : child_(std::move(child)), penalty_(penalty), cap_(cap), slack_(slack) {}
    const char *name() const override { return "bound"; }
    const char *role(unsigned) const override { return "residuals"; }
    std::span<const std::unique_ptr<stage>> children() const override { return {&child_, 1}; }

    void build(unsigned p, const build_input &in) override {
        if (parts_.size() <= p)
            parts_.resize(p + 1);
        sized(p, in.values.size());
        parts_[p].areas.resize((blocks_in(p) + group_blocks - 1) / group_blocks);
        std::vector<u64> residuals;
        std::array<u8, part_blocks> widths{};
        fit(p, 0, blocks_in(p), in, residuals, widths);
        child_->build(p, {residuals, widths.data()});
    }
    void clear(unsigned p) override {
        parts_[p] = {};
        child_->clear(p);
    }
    const ikea::byte *resolve(const ikea::journal::entry &e) const override {
        for (const part &s : parts_) {
            if (e.source == &s)
                return s.plane_storage(e.plane).data() + e.offset;
            for (const area &a : s.areas)
                if (e.source == &a)
                    return a.plane_storage(e.plane).data() + e.offset;
        }
        return child_->resolve(e);
    }
    bool rebuilds_groups() const override { return child_->rebuilds_groups(); }
    bool rebuild(unsigned p, unsigned first_block, const build_input &in) override {
        if (!rebuilds_groups() || first_block % group_blocks)
            return false;
        const unsigned n = std::min(group_blocks, blocks_in(p) - first_block);
        std::vector<u64> residuals;
        std::array<u8, part_blocks> widths{};
        fit(p, first_block, n, in, residuals, widths);
        return child_->rebuild(p, first_block, {residuals, widths.data()});
    }
    // Image section: per block lo, width, start and count; then the group's
    // exception area with its spare slots (capacity, used, positions, values).
    void emit(unsigned p, unsigned g, std::vector<std::byte> &out) const override {
        const part &s = parts_[p];
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(p) - first);
        for (unsigned b = first; b < first + n; ++b)
            put<u64>(out, s.lo[b]), put<u8>(out, s.width[b]), put<u16>(out, s.start[b]), put<u16>(out, s.count[b]);
        const area &a = s.areas[g];
        put<u16>(out, static_cast<u16>(a.pos.size())), put<u16>(out, static_cast<u16>(a.used));
        const auto *pos = reinterpret_cast<const std::byte *>(a.pos.data()), *val = reinterpret_cast<const std::byte *>(a.val.data());
        if (!a.pos.empty()) {
            out.insert(out.end(), pos, pos + a.pos.size());
            out.insert(out.end(), val, val + a.val.size() * 8);
        }
        child_->emit(p, g, out);
    }
    std::size_t attach(unsigned p, std::size_t rows, unsigned g, std::span<const std::byte> in, std::span<std::byte> writable) override {
        sized(p, rows);
        if (parts_.size() <= p)
            parts_.resize(p + 1);
        part &s = parts_[p];
        s.areas.resize(groups_in(p));
        std::size_t at = 0;
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(p) - first);
        for (unsigned b = first; b < first + n; ++b)
            s.lo[b] = take<u64>(in, at), s.width[b] = take<u8>(in, at), s.start[b] = take<u16>(in, at), s.count[b] = take<u16>(in, at);
        area &a = s.areas[g];
        const unsigned capacity = take<u16>(in, at);
        a.used = take<u16>(in, at);
        a.pos.resize(capacity), a.val.resize(capacity);
        if (capacity) {
            std::memcpy(a.pos.data(), in.data() + at, capacity);
            std::memcpy(a.val.data(), in.data() + at + capacity, std::size_t{capacity} * 8);
        }
        at += std::size_t{capacity} * 9;
        return at + child_->attach(p, rows, g, in.subspan(at), writable.empty() ? writable : writable.subspan(at));
    }
    void describe(unsigned p, census &c) const override {
        const part &s = parts_[p];
        for (unsigned b = 0; b < blocks_in(p); ++b)
            ++c.blocks[s.width[b]][census::bucket(s.count[b])], c.exceptions += s.count[b];
    }
    u64 digest(unsigned p) const override {
        const part &s = parts_[p];
        u64 h = hash_bytes(0xcbf29ce484222325, s.lo.data(), sizeof s.lo);
        h = hash_bytes(hash_bytes(hash_bytes(h, s.width.data(), sizeof s.width), s.start.data(), sizeof s.start), s.count.data(), sizeof s.count);
        for (const area &a : s.areas)
            h = hash_bytes(hash_bytes(h, a.pos.data(), a.used), a.val.data(), a.used * 8);
        return h * 0x100000001b3 ^ child_->digest(p);
    }
    std::size_t bytes(unsigned p) const override {
        // lo, width, start and count per block; position and value per slot.
        std::size_t n = blocks_in(p) * 13;
        for (const area &a : parts_[p].areas)
            n += a.pos.size() * 9;
        return n;
    }
    void decode(unsigned p, unsigned b, u64 *out) const override {
        const part &s = parts_[p];
        child_->decode(p, b, out);
        for (unsigned r = 0; r < rows_in(p, b); ++r)
            out[r] += s.lo[b];
        each_exception(s, b, [&](u8 pos, u64 v) { out[pos] = v; });
    }
    // SUM = fields + lo x rows + each exception's value minus the lo its slot stands for.
    u128 sum(unsigned p, const bitset256 &blocks) const override {
        const part &s = parts_[p];
        u128 t = child_->sum(p, blocks);
        each(blocks, [&](unsigned b) {
            t += static_cast<u128>(s.lo[b]) * rows_in(p, b);
            each_exception(s, b, [&](u8, u64 v) { t += v, t -= s.lo[b]; });
        });
        return t;
    }
    // MAX over selected rows from block bounds: a block's selected inliers lie
    // in [lo, lo + 2^width), its selected exceptions are known. A block whose
    // largest selected exception reaches its inlier bound settles without
    // decoding; the rest decode in descending bound order until the next bound
    // cannot beat the running maximum.
    bool max_selected(unsigned p, const bitset256 &blocks, const block_rows_set &rows, u64 &m) const override {
        const part &s = parts_[p];
        std::array<std::pair<u64, u16>, part_blocks> open;
        unsigned n = 0;
        bool any = false;
        each(blocks, [&](unsigned b) {
            const bitset256 r = rows[b] & first_n(rows_in(p, b));
            if (r.empty())
                return;
            any = true;
            unsigned inliers = r.count();
            bool exc = false;
            u64 em = 0;
            each_exception(s, b, [&](u8 pos, u64 v) {
                if (r.contains(pos))
                    exc = true, em = std::max(em, v), --inliers;
            });
            const u64 k = mask(s.width[b]), bound = k > ~u64{0} - s.lo[b] ? ~u64{0} : s.lo[b] + k;
            if (exc && (!inliers || em >= bound))
                m = std::max(m, em);
            else
                open[n++] = {exc ? std::max(bound, em) : bound, static_cast<u16>(b)};
        });
        std::sort(open.begin(), open.begin() + n, [](auto &x, auto &y) { return x.first > y.first; });
        // Values are at least 0, so a bound at or below m cannot change it.
        alignas(64) u64 x[block_rows];
        for (unsigned i = 0; i < n && open[i].first > m; ++i) {
            const unsigned b = open[i].second;
            decode(p, b, x);
            each(rows[b] & first_n(rows_in(p, b)), [&](unsigned r) { m = std::max(m, x[r]); });
        }
        return any;
    }
    // SUM over selected rows: the child's fields, lo for every selected
    // non-exception row, and each selected exception's own value.
    u128 sum_selected(unsigned p, const bitset256 &blocks, const block_rows_set &rows) const override {
        const part &s = parts_[p];
        u128 t = child_->sum_selected(p, blocks, rows);
        each(blocks, [&](unsigned b) {
            const bitset256 r = rows[b] & first_n(rows_in(p, b));
            unsigned n = r.count();
            each_exception(s, b, [&](u8 pos, u64 v) {
                if (r.contains(pos))
                    t += v, --n;
            });
            t += static_cast<u128>(s.lo[b]) * n;
        });
        return t;
    }
    u128 sum_pattern(unsigned p, const bitset256 &blocks, const bitset256 &pattern) const override {
        const part &s = parts_[p];
        u128 t = child_->sum_pattern(p, blocks, pattern);
        const unsigned full = pattern.count();
        each(blocks, [&](unsigned b) {
            const unsigned m = rows_in(p, b);
            unsigned n = m == block_rows ? full : (pattern & first_n(m)).count();
            each_exception(s, b, [&](u8 pos, u64 v) {
                if (pattern.contains(pos))
                    t += v, --n;
            });
            t += static_cast<u128>(s.lo[b]) * n;
        });
        return t;
    }
    template <comparison Op> static settle classify_as(u64 t, u64 lo, unsigned K, u64 &d) {
        const u64 m = mask(K);
        if (t < lo)
            return Op == comparison::greater_equal || Op == comparison::greater ? settle::all : settle::none;
        d = t - lo;
        if constexpr (Op == comparison::greater_equal)
            return d == 0 ? settle::all : d > m ? settle::none : settle::partial;
        if constexpr (Op == comparison::greater)
            return d >= m ? settle::none : settle::partial;
        if constexpr (Op == comparison::less_equal)
            return d >= m ? settle::all : settle::partial;
        if constexpr (Op == comparison::less)
            return d == 0 ? settle::none : d > m ? settle::all : settle::partial;
        return d > m ? settle::none : settle::partial;
    }
    // One pass over the descriptors settles whole blocks; only partial blocks
    // reach the child, against t - lo.
    template <comparison Op> u64 count_as(unsigned p, const bitset256 &blocks, const block_args &rhs) const {
        const part &s = parts_[p];
        bitset256 partial;
        block_args d;
        u64 c = 0;
        each(blocks, [&](unsigned b) {
            const unsigned E = s.count[b];
            switch (classify_as<Op>(rhs[b], s.lo[b], s.width[b], d[b])) {
            case settle::all: c += rows_in(p, b) - E; break;
            case settle::none: break;
            case settle::partial:
                partial.set(b);
                c -= ikea::compare<Op>(0, d[b]) ? E : 0;  // exception slots hold field 0
                break;
            }
            if (E)
                each_exception(s, b, [&](u8, u64 v) { c += ikea::compare<Op>(v, rhs[b]); });
        });
        if (!partial.empty())
            c += child_->count(Op, p, partial, d);
        return c;
    }
    // Seam case 1: one threshold, every block of the part. One branch-free
    // pass over the lo, width and count columns settles whole blocks; the
    // exception areas are scanned once per group, not per block; only
    // partial blocks reach the child.
    template <comparison Op> u64 count_uniform_as(unsigned p, const bitset256 &blocks, u64 t) const {
        const part &s = parts_[p];
        const unsigned nb = blocks_in(p);
        if (!(blocks == first_n(nb)) || rows_in(p, nb - 1) != block_rows)
            return count_as<Op>(p, blocks, [&] { block_args r; r.fill(t); return r; }());
        u64 settled = 0;
        u8 part_flag[part_blocks];
        block_args d;
        for (unsigned b = 0; b < nb; ++b) {
            const u64 lo = s.lo[b], m = mask(s.width[b]), E = s.count[b];
            const bool below = t < lo;
            const u64 dd = t - lo;
            bool all, none;
            if constexpr (Op == comparison::greater_equal)
                all = below || dd == 0, none = !below && dd > m;
            else if constexpr (Op == comparison::greater)
                all = below, none = !below && dd >= m;
            else if constexpr (Op == comparison::less_equal)
                all = !below && dd >= m, none = below;
            else if constexpr (Op == comparison::less)
                all = !below && dd > m, none = below || dd == 0;
            else
                all = false, none = below || dd > m;
            const bool partial = !all && !none;
            settled += all ? block_rows - E : 0;
            // Exception slots hold field 0; a partial block's child count includes them.
            settled -= partial && ikea::compare<Op>(0, dd) ? E : 0;
            part_flag[b] = partial;
            d[b] = dd;
        }
        for (const area &a : s.areas)
            for (unsigned e = 0; e < a.used; ++e)
                settled += ikea::compare<Op>(a.val[e], t);
        bitset256 partial;
        for (unsigned b = 0; b < nb; ++b)
            partial.words[b / 64] |= u64{part_flag[b]} << (b % 64);
        if (!partial.empty())
            settled += child_->count(Op, p, partial, d);
        return settled;
    }
    u64 count_uniform(comparison op, unsigned p, const bitset256 &blocks, u64 t) const override {
        switch (op) {
        case comparison::equal: return count_uniform_as<comparison::equal>(p, blocks, t);
        case comparison::less: return count_uniform_as<comparison::less>(p, blocks, t);
        case comparison::less_equal: return count_uniform_as<comparison::less_equal>(p, blocks, t);
        case comparison::greater: return count_uniform_as<comparison::greater>(p, blocks, t);
        case comparison::greater_equal: return count_uniform_as<comparison::greater_equal>(p, blocks, t);
        }
        return 0;
    }
    template <comparison Op> void filter_as(unsigned p, const bitset256 &blocks, const block_args &rhs, block_rows_set &out) const {
        const part &s = parts_[p];
        bitset256 partial;
        block_args d;
        each(blocks, [&](unsigned b) {
            const settle k = classify_as<Op>(rhs[b], s.lo[b], s.width[b], d[b]);
            if (k == settle::partial)
                partial.set(b);
            else
                out[b] = k == settle::all ? first_n(rows_in(p, b)) : bitset256{};
        });
        if (!partial.empty())
            child_->filter(Op, p, partial, d, out);
        each(blocks, [&](unsigned b) {
            if (s.count[b])
                each_exception(s, b, [&](u8 pos, u64 v) { out[b].set(pos, ikea::compare<Op>(v, rhs[b])); });
        });
    }
    u64 count(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs) const override {
        switch (op) {
        case comparison::equal: return count_as<comparison::equal>(p, blocks, rhs);
        case comparison::less: return count_as<comparison::less>(p, blocks, rhs);
        case comparison::less_equal: return count_as<comparison::less_equal>(p, blocks, rhs);
        case comparison::greater: return count_as<comparison::greater>(p, blocks, rhs);
        case comparison::greater_equal: return count_as<comparison::greater_equal>(p, blocks, rhs);
        }
        return 0;
    }
    void filter(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs, block_rows_set &out) const override {
        switch (op) {
        case comparison::equal: return filter_as<comparison::equal>(p, blocks, rhs, out);
        case comparison::less: return filter_as<comparison::less>(p, blocks, rhs, out);
        case comparison::less_equal: return filter_as<comparison::less_equal>(p, blocks, rhs, out);
        case comparison::greater: return filter_as<comparison::greater>(p, blocks, rhs, out);
        case comparison::greater_equal: return filter_as<comparison::greater_equal>(p, blocks, rhs, out);
        }
    }
    /// A fused point path's entry for block b (point_plan in trees.h).
    struct point_entry {
        u64 lo;
        point_kernel kernel;
        std::size_t base;
        const u8 *pos;
        const u64 *val;
        unsigned count;
    };
    bool entry(unsigned p, unsigned b, point_entry &e) const {
        const part &s = parts_[p];
        const area &a = area_of(s, b);
        e = {s.lo[b], {}, 0, a.pos.data() + s.start[b], a.val.data() + s.start[b], s.count[b]};
        const width_array *leaf = nullptr;
        if (!child_->leaf_entry(p, b, leaf, e.base))
            return false;
        if (leaf)
            e.kernel = leaf->point();
        return true;
    }
    // Field first: a nonzero field is never an exception.
    u64 get(unsigned p, unsigned row) const override {
        const part &s = parts_[p];
        const unsigned b = row / block_rows;
        const u64 f = child_->get(p, row);
        if (f == 0 && s.count[b])
            if (const int e = find(s, b, row % block_rows); e >= 0)
                return area_of(s, b).val[e];
        return s.lo[b] + f;
    }
    void read(unsigned p, unsigned first, unsigned n, u64 *out) const override {
        const part &s = parts_[p];
        child_->read(p, first, n, out);
        for (unsigned at = 0; at < n;) {
            const unsigned row = first + at, b = row / block_rows, m = std::min(n - at, rows_in(p, b) - row % block_rows);
            for (unsigned i = 0; i < m; ++i)
                out[at + i] += s.lo[b];
            each_exception(s, b, [&](u8 pos, u64 v) {
                if (const unsigned r = b * block_rows + pos; r >= row && r < row + m)
                    out[at + r - row] = v;
            });
            at += m;
        }
    }
    std::size_t select(unsigned p, const bitset256 &blocks, const block_rows_set &rows, u64 *out) const override {
        const part &s = parts_[p];
        const std::size_t k = child_->select(p, blocks, rows, out);
        std::size_t at = 0;
        each(blocks, [&](unsigned b) {
            const unsigned n = rows[b].count();
            for (unsigned i = 0; i < n; ++i)
                out[at + i] += s.lo[b];
            each_exception(s, b, [&](u8 pos, u64 v) {
                if (rows[b].contains(pos))
                    out[at + rows[b].rank(pos)] = v;
            });
            at += n;
        });
        return k;
    }

    // An exception row updates in place; a value inside the window goes to the
    // child; an outlier takes a spare slot of its group, or is refused.
    verdict classify(unsigned p, unsigned row, u64 value) const override {
        const part &s = parts_[p];
        const unsigned b = row / block_rows;
        if (s.count[b] && find(s, b, row % block_rows) >= 0)
            return verdict::absorb;
        if (inside(s, b, value))
            return child_->classify(p, row, value - s.lo[b]);
        const area &a = area_of(s, b);
        return a.used < a.pos.size() && s.count[b] < cap_ ? verdict::redirect : verdict::refuse;
    }
    void declare(unsigned p, unsigned row, u64 value, envelope &e) const override {
        const part &s = parts_[p];
        const unsigned b = row / block_rows;
        const area &a = area_of(s, b);
        // Old-value reads: the field and the block's exceptions.
        child_->declare(p, row, 0, e);
        e.reads.add(a.pos.data() + s.start[b], s.count[b]);
        e.reads.add(a.val.data() + s.start[b], s.count[b] * 8);
        if (s.count[b] && find(s, b, row % block_rows) >= 0) {
            e.stores.add(a.val.data() + s.start[b], s.count[b] * 8);
            e.records += 1;
        } else if (!inside(s, b, value)) {
            // Insert: shift the rest of the area, bump later blocks' starts.
            const unsigned at = s.start[b];
            e.stores.add(a.pos.data() + at, a.used + 1 - at);
            e.stores.add(a.val.data() + at, (a.used + 1 - at) * 8);
            e.stores.add(s.start.data() + b, (group_blocks - b % group_blocks) * 2);
            e.stores.add(s.count.data() + b, 2);
            e.records += 4;
        }
    }
    void apply(unsigned p, unsigned row, u64 value, ikea::journal &effects) override {
        part &s = parts_[p];
        const unsigned b = row / block_rows, r = row % block_rows;
        area &a = s.areas[b / group_blocks];
        if (const int e = s.count[b] ? find(s, b, r) : -1; e >= 0) {
            effects.before(a, 1, static_cast<std::size_t>(e) * 8, 8);
            a.val[e] = value;
            return;
        }
        if (inside(s, b, value))
            return child_->apply(p, row, value - s.lo[b], effects);
        // Keep positions sorted within the block.
        unsigned at = s.start[b];
        while (at < s.start[b] + s.count[b] && a.pos[at] < r)
            ++at;
        effects.before(a, 0, at, a.used + 1 - at);
        effects.before(a, 1, std::size_t{at} * 8, std::size_t{a.used + 1 - at} * 8);
        std::copy_backward(a.pos.begin() + at, a.pos.begin() + a.used, a.pos.begin() + a.used + 1);
        std::copy_backward(a.val.begin() + at, a.val.begin() + a.used, a.val.begin() + a.used + 1);
        a.pos[at] = static_cast<u8>(r), a.val[at] = value, ++a.used;
        const unsigned end = std::min((b / group_blocks + 1) * group_blocks, blocks_in(p));
        if (end > b + 1)
            effects.before(s, 0, std::size_t{b + 1} * 2, std::size_t{end - b - 1} * 2);
        effects.before(s, 1, std::size_t{b} * 2, 2);
        for (unsigned c = b + 1; c < end; ++c)
            ++s.start[c];
        ++s.count[b];
        child_->apply(p, row, 0, effects);  // the slot holds field 0
    }
};

/// Intervals' two forms over a value series: sizes (each row's difference
/// from the previous row) or relative ends (each row's difference from its
/// region's checkpoint). Both keep a checkpoint every G rows.
enum class interval_form : u8 { sizes, relative_ends };

class intervals final : public stage {
    static constexpr u64 bias = u64{1} << 63;  // offset binary for signed deltas
    // Children: the deltas, and the checkpoints (the value at every G-th row)
    // as a series of their own, so they are framed like any other values.
    std::unique_ptr<stage> children_[2];
    unsigned G_;
    interval_form form_;
    bool relative() const { return form_ == interval_form::relative_ends; }
    struct part {
        std::vector<u64> last;     // each block's last value (evidence for the monotone law)
        std::vector<u8> monotone;  // per block: non-decreasing
        std::vector<u8> exact;     // per block: every delta fits int64, so sums of them are exact integers
    };
    std::vector<part> parts_;
    // The checkpoints of block b.
    unsigned checkpoints(unsigned p, unsigned b, u64 *cp) const {
        const unsigned n = (rows_in(p, b) + G_ - 1) / G_;
        children_[1]->read(p, b * block_rows / G_, n, cp);
        return n;
    }

  public:
    bool masked_sum_law = false;
    intervals(std::unique_ptr<stage> deltas, std::unique_ptr<stage> checkpoints, unsigned G = 16, interval_form form = interval_form::sizes)
        : children_{std::move(deltas), std::move(checkpoints)}, G_(G), form_(form) {
        assert(G >= 2 && block_rows % G == 0);
    }
    const char *name() const override { return "intervals"; }
    const char *role(unsigned child) const override { return child ? "checkpoints" : "deltas"; }
    // Checkpoints have their own row domain (one per G rows): their own stream.
    bool inline_child(unsigned child) const override { return child == 0; }
    // Image section: per block last value (u64), monotone and exact flags
    // (u8, 0 or 1); then the deltas inline. G is the law's, not the image's.
    // The flags are evidence: validation must establish them (owner A4).
    void emit(unsigned p, unsigned g, std::vector<std::byte> &out) const override {
        const part &s = parts_[p];
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(p) - first);
        for (unsigned b = first; b < first + n; ++b)
            put<u64>(out, s.last[b]), put<u8>(out, s.monotone[b]), put<u8>(out, s.exact[b]);
        children_[0]->emit(p, g, out);
    }
    std::size_t attach(unsigned p, std::size_t rows, unsigned g, std::span<const std::byte> in, std::span<std::byte> writable) override {
        sized(p, rows);
        if (parts_.size() <= p)
            parts_.resize(p + 1);
        part &s = parts_[p];
        s.last.resize(blocks_in(p)), s.monotone.resize(blocks_in(p)), s.exact.resize(blocks_in(p));
        std::size_t at = 0;
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(p) - first);
        for (unsigned b = first; b < first + n; ++b) {
            s.last[b] = take<u64>(in, at), s.monotone[b] = take<u8>(in, at), s.exact[b] = take<u8>(in, at);
            if (s.monotone[b] > 1 || s.exact[b] > 1)
                std::abort();  // flags are 0 or 1
        }
        return at + children_[0]->attach(p, rows, g, in.subspan(at), writable.empty() ? writable : writable.subspan(at));
    }
    std::span<const std::unique_ptr<stage>> children() const override { return children_; }
    void build(unsigned p, const build_input &in) override {
        if (parts_.size() <= p)
            parts_.resize(p + 1);
        part &s = parts_[p];
        sized(p, in.values.size());
        std::vector<u64> d(in.values.size()), cp;
        for (std::size_t r = 0; r < in.values.size(); ++r) {
            if (r % G_ == 0)
                cp.push_back(in.values[r]), d[r] = bias;
            else
                d[r] = (in.values[r] - (relative() ? cp.back() : in.values[r - 1])) ^ bias;
        }
        for (unsigned b = 0; b < blocks_in(p); ++b) {
            const u64 *x = in.values.data() + std::size_t{b} * block_rows;
            const unsigned m = rows_in(p, b);
            s.last.push_back(x[m - 1]);
            s.monotone.push_back(std::is_sorted(x, x + m));
            bool fits = true;
            for (unsigned r = 1; r < m; ++r) {
                const u64 ref = relative() ? x[r - r % G_] : x[r - 1];
                fits &= x[r] >= ref ? x[r] - ref < bias : ref - x[r] <= bias;
            }
            s.exact.push_back(fits);
        }
        children_[0]->build(p, {d});
        children_[1]->build(p, {cp});
    }
    std::size_t bytes(unsigned p) const override { return blocks_in(p) * 9; }
    void clear(unsigned p) override {
        parts_[p] = {};
        stage::clear(p);
    }
    void describe(unsigned p, census &c) const override {
        const part &s = parts_[p];
        c.checkpoints += (rows(p) + G_ - 1) / G_;
        for (u8 m : s.monotone)
            c.monotone += m;
        children_[0]->describe(p, c);
    }
    void decode(unsigned p, unsigned b, u64 *out) const override {
        children_[0]->decode(p, b, out);
        alignas(64) u64 cp[block_rows];
        checkpoints(p, b, cp);
        const unsigned m = rows_in(p, b);
        for (unsigned r0 = 0, g = 0; r0 < m; r0 += G_, ++g) {
            u64 acc = out[r0] = cp[g];
            for (unsigned r = r0 + 1; r < std::min(m, r0 + G_); ++r)
                out[r] = relative() ? cp[g] + (out[r] ^ bias) : (acc += out[r] ^ bias);
        }
    }
    u64 get(unsigned p, unsigned row) const override {
        if (relative())  // no prefix: the checkpoint plus one field
            return children_[1]->get(p, row / G_) + (row % G_ ? children_[0]->get(p, row) ^ bias : 0);
        u64 acc = children_[1]->get(p, row / G_);
        if (const unsigned k = row % G_) {
            alignas(64) u64 w[block_rows];
            children_[0]->read(p, row - k + 1, k, w);
            for (unsigned i = 0; i < k; ++i)
                acc += w[i] ^ bias;
        }
        return acc;
    }
    // SUM over whole blocks, from masked sums of the deltas: within a region
    // of G rows starting at checkpoint c, sum x = G c + sum_j (G - j) delta_j,
    // and (G - j) = G - sum_k 2^k [bit k of j], so one masked sum per bit of j
    // (plus one for every non-checkpoint row) replaces the prefix. Deltas are
    // offset binary: delta = d - 2^63 exactly. Partial blocks, and blocks
    // whose deltas wrap (their prefixes are exact only modulo 2^64), decode.
    u128 sum(unsigned p, const bitset256 &blocks) const override {
        if (relative())
            return relative_sum(p, blocks);
        // Measured 1.3-2.6x slower than decoding on Zen 5 (SeriesPack's
        // prefiltered sum costs several times its range sum), so off.
        if (!masked_sum_law)
            return stage::sum(p, blocks);
        bitset256 full, partial;
        each(blocks, [&](unsigned b) { (rows_in(p, b) == block_rows && parts_[p].exact[b] ? full : partial).set(b); });
        __int128 t = partial.empty() ? 0 : static_cast<__int128>(stage::sum(p, partial));
        if (full.empty())
            return static_cast<u128>(t);
        const __int128 nb = full.count();
        // The checkpoints of the full blocks.
        each(full, [&](unsigned b) {
            alignas(64) u64 cp[block_rows];
            const unsigned n = checkpoints(p, b, cp);
            for (unsigned g = 0; g < n; ++g)
                t += static_cast<__int128>(G_) * cp[g];
        });
        auto masked = [&](const bitset256 &pattern) {
            const __int128 d = static_cast<__int128>(children_[0]->sum_pattern(p, full, pattern));
            return d - static_cast<__int128>(pattern.count()) * nb * static_cast<__int128>(bias);
        };
        bitset256 rest;
        for (unsigned r = 0; r < block_rows; ++r)
            rest.set(r, r % G_ != 0);
        t += static_cast<__int128>(G_) * masked(rest);
        for (unsigned k = 0; (1u << k) < G_; ++k) {
            bitset256 bit;
            for (unsigned r = 0; r < block_rows; ++r)
                bit.set(r, (r % G_) >> k & 1);
            t -= (static_cast<__int128>(1) << k) * masked(bit);
        }
        return static_cast<u128>(t);
    }
    // Relative ends: sum x = sum over regions of G c + sum of the offsets, and
    // checkpoint rows hold offset 0, so one plain SUM of the child covers
    // every offset: no prefix, no masks. Offsets are d - 2^63 exactly on
    // blocks whose offsets fit int64; others, and partial blocks, decode.
    u128 relative_sum(unsigned p, const bitset256 &blocks) const {
        bitset256 full, partial;
        each(blocks, [&](unsigned b) { (rows_in(p, b) == block_rows && parts_[p].exact[b] ? full : partial).set(b); });
        __int128 t = partial.empty() ? 0 : static_cast<__int128>(stage::sum(p, partial));
        if (full.empty())
            return static_cast<u128>(t);
        each(full, [&](unsigned b) {
            alignas(64) u64 cp[block_rows];
            const unsigned n = checkpoints(p, b, cp);
            for (unsigned g = 0; g < n; ++g)
                t += static_cast<__int128>(G_) * cp[g];
        });
        t += static_cast<__int128>(children_[0]->sum(p, full)) - static_cast<__int128>(full.count()) * block_rows * static_cast<__int128>(bias);
        return static_cast<u128>(t);
    }
    // Monotone blocks: settle by the first and last values, else search the
    // checkpoints and read one G-row region. Other blocks decode.
    u64 count(comparison op, unsigned p, const bitset256 &blocks, const block_args &rhs) const override {
        const part &s = parts_[p];
        if (op != comparison::greater_equal)
            return stage::count(op, p, blocks, rhs);
        bitset256 other;
        u64 c = 0;
        each(blocks, [&](unsigned b) {
            if (!s.monotone[b])
                return other.set(b);
            const u64 t = rhs[b];
            const unsigned m = rows_in(p, b);
            alignas(64) u64 cp[block_rows];
            const unsigned regions = checkpoints(p, b, cp);
            if (cp[0] >= t)
                c += m;
            else if (s.last[b] >= t) {
                // Both searches are Intervals' endpoint search over ordered
                // values: the last region whose checkpoint is below t holds the
                // boundary, then the first of its values at or above t.
                // Reconstruction stays here: the deltas are a framed child in
                // offset binary, not a native u8/u16 size plane.
                const unsigned g = ikea::intervals::lower_bound(ikea::intervals::direct_source<u64>{cp, regions}, t) - 1;
                const unsigned k = std::min(G_, m - g * G_) - 1;
                alignas(64) u64 w[block_rows];
                children_[0]->read(p, b * block_rows + g * G_ + 1, k, w + 1);
                w[0] = cp[g];
                for (unsigned i = 1; i <= k; ++i)
                    w[i] = (relative() ? cp[g] : w[i - 1]) + (w[i] ^ bias);
                c += m - g * G_ - ikea::intervals::lower_bound(ikea::intervals::direct_source<u64>{w, k + 1}, t);
            }
        });
        if (!other.empty())
            c += stage::count(op, p, other, rhs);
        return c;
    }
};
}  // namespace frame3

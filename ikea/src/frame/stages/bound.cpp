// bound: lo and width per block, values outside [lo, lo + 2^width) as
// exceptions in a per-group area whose slots' child fields hold 0.
#include "../sections.h"
#include "windows.h"

#include <ikea/frame/detail/stages.h>

#include <optional>

namespace ikea::frame::detail {
namespace {
using author::stage;
using windows::mask;

class bound_stage final : public stage {
    std::unique_ptr<stage> child_;
    fitting_policy policy_;
    // A group's exceptions, block by block, then spare slots.
    struct area {
        std::vector<std::uint8_t> pos;
        std::vector<std::uint64_t> val;
        unsigned used = 0;
    };
    struct unit_state {
        std::array<std::uint64_t, unit_blocks> lo{};
        std::array<std::uint8_t, unit_blocks> width{};
        // Block b's exceptions are [start, start + count) of its group's area.
        std::array<std::uint16_t, unit_blocks> start{}, count{};
        std::vector<area> areas;
    };
    std::vector<unit_state> units_;

    unit_state &unit(unsigned u) {
        if (units_.size() <= u)
            units_.resize(u + 1);
        return units_[u];
    }
    windows::cost_model costs() const {
        windows::cost_model c;
        c.exception_penalty = policy_.penalty, c.max_exceptions = policy_.exception_cap;
        for (unsigned K = 1; K <= 64; ++K)
            c.body[K] = static_cast<std::uint32_t>(child_->body_bytes(K));
        c.body[0] = c.body[1];
        return c;
    }
    const area &area_of(const unit_state &s, unsigned b) const { return s.areas[b / group_blocks]; }
    template <class F> void each_exception(const unit_state &s, unsigned b, F &&f) const {
        const area &a = area_of(s, b);
        for (unsigned e = s.start[b]; e < s.start[b] + s.count[b]; ++e)
            f(a.pos[e], a.val[e]);
    }
    int find(const unit_state &s, unsigned b, unsigned r) const {
        const area &a = area_of(s, b);
        for (unsigned e = s.start[b]; e < s.start[b] + s.count[b]; ++e)
            if (a.pos[e] == r)
                return static_cast<int>(e);
        return -1;
    }
    static std::uint64_t top(std::uint64_t lo, unsigned K) noexcept {
        const std::uint64_t k = mask(K);
        return k > ~std::uint64_t{0} - lo ? ~std::uint64_t{0} : lo + k;
    }

    // How a comparison against t settles a block's inliers [lo, lo + 2^K).
    enum class settle { none, all, partial };
    template <comparison Op> static settle classify(std::uint64_t t, std::uint64_t lo, unsigned K, std::uint64_t &d) {
        const std::uint64_t m = mask(K);
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
    template <class F> static decltype(auto) with_op(comparison op, F &&f) {
        switch (op) {
        case comparison::equal: return f.template operator()<comparison::equal>();
        case comparison::less: return f.template operator()<comparison::less>();
        case comparison::less_equal: return f.template operator()<comparison::less_equal>();
        case comparison::greater: return f.template operator()<comparison::greater>();
        case comparison::greater_equal: break;
        }
        return f.template operator()<comparison::greater_equal>();
    }
    // Descriptors settle whole blocks; only partial blocks reach the child,
    // against t - lo.
    template <comparison Op> std::uint64_t count_as(unsigned u, const bitset256 &blocks, const block_args &rhs) const {
        const unit_state &s = units_[u];
        bitset256 partial;
        block_args d;
        std::uint64_t c = 0;
        blocks.for_each([&](unsigned b) {
            const unsigned E = s.count[b];
            switch (classify<Op>(rhs[b], s.lo[b], s.width[b], d[b])) {
            case settle::all: c += rows_in(u, b) - E; break;
            case settle::none: break;
            case settle::partial:
                partial.set(b);
                c -= ikea::compare<Op>(0, d[b]) ? E : 0; // exception slots hold field 0
                break;
            }
            if (E)
                each_exception(s, b, [&](std::uint8_t, std::uint64_t v) { c += ikea::compare<Op>(v, rhs[b]); });
        });
        if (!partial.empty())
            c += child_->count(Op, u, partial, d);
        return c;
    }
    // One threshold over every block of a full unit: one branch-free pass over
    // the lo, width and count columns, exception areas scanned once per group.
    template <comparison Op> std::uint64_t count_uniform_as(unsigned u, const bitset256 &blocks, std::uint64_t t) const {
        const unit_state &s = units_[u];
        const unsigned nb = blocks_in(u);
        if (!(blocks == bitset256::first_n(nb)) || rows_in(u, nb - 1) != block_rows) {
            block_args rhs;
            rhs.fill(t);
            return count_as<Op>(u, blocks, rhs);
        }
        std::uint64_t settled = 0;
        std::uint8_t partial_flag[unit_blocks];
        block_args d;
        for (unsigned b = 0; b < nb; ++b) {
            const std::uint64_t lo = s.lo[b], m = mask(s.width[b]), E = s.count[b];
            const bool below = t < lo;
            const std::uint64_t dd = t - lo;
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
            settled -= partial && ikea::compare<Op>(0, dd) ? E : 0;
            partial_flag[b] = partial;
            d[b] = dd;
        }
        for (const area &a : s.areas)
            for (unsigned e = 0; e < a.used; ++e)
                settled += ikea::compare<Op>(a.val[e], t);
        bitset256 partial;
        for (unsigned b = 0; b < nb; ++b)
            partial.words[b / 64] |= std::uint64_t{partial_flag[b]} << (b % 64);
        if (!partial.empty())
            settled += child_->count(Op, u, partial, d);
        return settled;
    }
    template <comparison Op>
    void filter_as(unsigned u, const bitset256 &blocks, const block_args &rhs, std::span<bitset256> out) const {
        const unit_state &s = units_[u];
        bitset256 partial;
        block_args d;
        blocks.for_each([&](unsigned b) {
            const settle k = classify<Op>(rhs[b], s.lo[b], s.width[b], d[b]);
            if (k == settle::partial)
                partial.set(b);
            else
                out[b] = k == settle::all ? bitset256::first_n(rows_in(u, b)) : bitset256{};
        });
        if (!partial.empty())
            child_->filter(Op, u, partial, d, out);
        blocks.for_each([&](unsigned b) {
            if (s.count[b])
                each_exception(s, b, [&](std::uint8_t pos, std::uint64_t v) { out[b].set(pos, ikea::compare<Op>(v, rhs[b])); });
        });
    }

  public:
    bound_stage(std::unique_ptr<stage> child, const fitting_policy &policy) : child_(std::move(child)), policy_(policy) {}
    std::span<const std::unique_ptr<stage>> children() const override { return {&child_, 1}; }
    const char *role(unsigned) const override { return "residuals"; }

    std::expected<void, error> fit(unsigned u, const author::fit_input &in) override {
        if (in.values.size() > unit_rows || policy_.exception_cap > block_rows || policy_.slack > 4096)
            return std::unexpected(error::range);
        unit_state &s = unit(u) = {};
        sized(u, in.values.size());
        s.areas.resize(groups_in(u));
        const auto priced = costs(), sample = windows::sample_cost(priced, block_rows / 32);
        std::vector<std::uint64_t> residuals(in.values.size());
        std::array<std::uint8_t, unit_blocks> widths{};
        for (unsigned g = 0; g < s.areas.size(); ++g) {
            area &a = s.areas[g];
            for (unsigned b = g * group_blocks; b < std::min((g + 1) * group_blocks, blocks_in(u)); ++b) {
                const std::size_t at = std::size_t{b} * block_rows;
                const std::uint64_t *x = in.values.data() + at;
                const unsigned m = rows_in(u, b);
                const auto [mn, mx] = std::minmax_element(x, x + m);
                std::uint64_t lo = *mn;
                unsigned K = 0;
                if (*mx != *mn) {
                    // The prior is the previous block's window within the group,
                    // so a group fits as it would alone.
                    std::optional<windows::choice> prior;
                    if (b % group_blocks && s.width[b - 1])
                        prior = windows::choice{s.lo[b - 1], s.width[b - 1], 0, 0, 0};
                    const auto c = windows::choose<32>(x, m, priced, sample, prior ? &*prior : nullptr);
                    lo = c.lo, K = std::max(1u, c.width);
                }
                s.lo[b] = lo, s.width[b] = widths[b] = static_cast<std::uint8_t>(K);
                s.start[b] = static_cast<std::uint16_t>(a.pos.size());
                for (unsigned r = 0; r < m; ++r) {
                    const bool out = x[r] < lo || x[r] - lo > mask(K);
                    if (out)
                        a.pos.push_back(static_cast<std::uint8_t>(r)), a.val.push_back(x[r]);
                    residuals[at + r] = out ? 0 : x[r] - lo;
                }
                s.count[b] = static_cast<std::uint16_t>(a.pos.size() - s.start[b]);
                if (s.count[b] > policy_.exception_cap)
                    return std::unexpected(error::capacity);
            }
            a.used = static_cast<unsigned>(a.pos.size());
            a.pos.resize(a.used + policy_.slack), a.val.resize(a.used + policy_.slack);
        }
        return child_->fit(u, {residuals, widths.data()});
    }

    // Section: per block lo u64, width u8, start u16, count u16; the area's
    // capacity u16 and used u16; capacity positions u8 then values u64; then
    // the child's section.
    void emit(unsigned u, unsigned g, std::vector<std::byte> &out) const override {
        const unit_state &s = units_[u];
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(u) - first);
        for (unsigned b = first; b < first + n; ++b) {
            put<std::uint64_t>(out, s.lo[b]), put<std::uint8_t>(out, s.width[b]);
            put<std::uint16_t>(out, s.start[b]), put<std::uint16_t>(out, s.count[b]);
        }
        const area &a = s.areas[g];
        put<std::uint16_t>(out, static_cast<std::uint16_t>(a.pos.size())), put<std::uint16_t>(out, static_cast<std::uint16_t>(a.used));
        for (std::uint8_t p : a.pos)
            put<std::uint8_t>(out, p);
        for (std::uint64_t v : a.val)
            put<std::uint64_t>(out, v);
        child_->emit(u, g, out);
    }
    // Checks this section's own law. The premises over the child (each
    // exception's field is 0, each lo + field is representable, widths agree)
    // are proved by image validation before a reader binds.
    std::expected<std::size_t, error> attach(unsigned u, std::size_t rows, unsigned g,
                                             std::span<const std::byte> in) override {
        if (rows > unit_rows)
            return std::unexpected(error::range);
        sized(u, rows);
        unit_state &s = unit(u);
        s.areas.resize(groups_in(u));
        if (g >= s.areas.size())
            return std::unexpected(error::range);
        cursor c{in};
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(u) - first);
        unsigned end = 0;
        for (unsigned b = first; b < first + n; ++b) {
            s.lo[b] = c.take<std::uint64_t>(), s.width[b] = c.take<std::uint8_t>();
            s.start[b] = c.take<std::uint16_t>(), s.count[b] = c.take<std::uint16_t>();
            if (!c.ok || s.width[b] > 64 || s.start[b] != end || s.count[b] > rows_in(u, b))
                return std::unexpected(error::contents);
            end += s.count[b];
        }
        area &a = s.areas[g] = area{};
        const unsigned capacity = c.take<std::uint16_t>();
        a.used = c.take<std::uint16_t>();
        if (!c.ok || a.used != end || a.used > capacity)
            return std::unexpected(error::contents);
        a.pos.resize(capacity), a.val.resize(capacity);
        for (auto &p : a.pos)
            p = c.take<std::uint8_t>();
        for (auto &v : a.val)
            v = c.take<std::uint64_t>();
        if (!c.ok)
            return std::unexpected(error::contents);
        for (unsigned b = first; b < first + n; ++b) {
            int last = -1;
            for (unsigned e = s.start[b]; e < s.start[b] + s.count[b]; ++e) {
                if (a.pos[e] >= rows_in(u, b) || static_cast<int>(a.pos[e]) <= last)
                    return std::unexpected(error::contents);
                last = a.pos[e];
            }
        }
        const auto used = child_->attach(u, rows, g, in.subspan(c.at));
        if (!used)
            return used;
        return c.at + *used;
    }
    void describe(unsigned u, author::census &c) const override {
        const unit_state &s = units_[u];
        for (unsigned b = 0; b < blocks_in(u); ++b)
            ++c.blocks[s.width[b]][author::census::bucket(s.count[b])], c.exceptions += s.count[b];
    }
    bool entry(unsigned u, unsigned b, bound_point_entry &e) const {
        const unit_state &s = units_[u];
        const area &a = area_of(s, b);
        // No pointer is formed into an area without exceptions (it may have no slots).
        e = {s.lo[b], {}, s.count[b] ? a.pos.data() + s.start[b] : nullptr,
             s.count[b] ? a.val.data() + s.start[b] : nullptr, s.count[b]};
        return child_->leaf_entry(u, b, e.leaf);
    }

    void decode(unsigned u, unsigned b, std::uint64_t *out) const override {
        const unit_state &s = units_[u];
        child_->decode(u, b, out);
        for (unsigned r = 0; r < rows_in(u, b); ++r)
            out[r] += s.lo[b];
        each_exception(s, b, [&](std::uint8_t pos, std::uint64_t v) { out[pos] = v; });
    }
    // SUM = fields + lo x rows + each exception's value minus the lo its slot
    // stands for (modulo 2^128, so a value below lo needs no sign).
    u128 sum(unsigned u, const bitset256 &blocks) const override {
        const unit_state &s = units_[u];
        u128 t = child_->sum(u, blocks);
        blocks.for_each([&](unsigned b) {
            t += static_cast<u128>(s.lo[b]) * rows_in(u, b);
            each_exception(s, b, [&](std::uint8_t, std::uint64_t v) { t += v, t -= s.lo[b]; });
        });
        return t;
    }
    u128 sum_selected(unsigned u, const bitset256 &blocks, const block_rows_set &rows) const override {
        const unit_state &s = units_[u];
        u128 t = child_->sum_selected(u, blocks, rows);
        blocks.for_each([&](unsigned b) {
            const bitset256 r = rows[b] & bitset256::first_n(rows_in(u, b));
            unsigned n = r.count();
            each_exception(s, b, [&](std::uint8_t pos, std::uint64_t v) {
                if (r.contains(pos))
                    t += v, --n;
            });
            t += static_cast<u128>(s.lo[b]) * n;
        });
        return t;
    }
    u128 weighted_sum(unsigned u, const bitset256 &blocks, const std::uint8_t *weights, unsigned period) const override {
        const unit_state &s = units_[u];
        u128 t = child_->weighted_sum(u, blocks, weights, period);
        unsigned period_weight = 0;
        for (unsigned r = 0; r < period; ++r)
            period_weight += weights[r];
        blocks.for_each([&](unsigned b) {
            // Blocks start on period boundaries: whole periods, then a tail.
            const unsigned m = rows_in(u, b);
            unsigned w = m / period * period_weight;
            for (unsigned r = m / period * period; r < m; ++r)
                w += weights[r % period];
            t += static_cast<u128>(s.lo[b]) * w;
            each_exception(s, b, [&](std::uint8_t pos, std::uint64_t v) {
                t += static_cast<u128>(v) * weights[pos % period], t -= static_cast<u128>(s.lo[b]) * weights[pos % period];
            });
        });
        return t;
    }
    // A block's selected inliers lie in [lo, lo + 2^width) and its selected
    // exceptions are known. A block whose largest selected exception reaches
    // its inlier bound settles without decoding; the rest decode in
    // descending bound order until the next bound cannot beat the maximum.
    bool max_selected(unsigned u, const bitset256 &blocks, const block_rows_set &rows, std::uint64_t &m) const override {
        const unit_state &s = units_[u];
        std::array<std::pair<std::uint64_t, std::uint16_t>, unit_blocks> open;
        unsigned n = 0;
        bool any = false;
        blocks.for_each([&](unsigned b) {
            const bitset256 r = rows[b] & bitset256::first_n(rows_in(u, b));
            if (r.empty())
                return;
            any = true;
            unsigned inliers = r.count();
            bool exc = false;
            std::uint64_t em = 0;
            each_exception(s, b, [&](std::uint8_t pos, std::uint64_t v) {
                if (r.contains(pos))
                    exc = true, em = std::max(em, v), --inliers;
            });
            const std::uint64_t bound = top(s.lo[b], s.width[b]);
            if (exc && (!inliers || em >= bound))
                m = std::max(m, em);
            else
                open[n++] = {exc ? std::max(bound, em) : bound, static_cast<std::uint16_t>(b)};
        });
        std::sort(open.begin(), open.begin() + n, [](auto &x, auto &y) { return x.first > y.first; });
        alignas(64) std::uint64_t x[block_rows];
        // Values are at least 0, so a bound at or below m cannot change it.
        for (unsigned i = 0; i < n && open[i].first > m; ++i) {
            const unsigned b = open[i].second;
            decode(u, b, x);
            (rows[b] & bitset256::first_n(rows_in(u, b))).for_each([&](unsigned r) { m = std::max(m, x[r]); });
        }
        return any;
    }
    std::uint64_t count(comparison op, unsigned u, const bitset256 &blocks, const block_args &rhs) const override {
        return with_op(op, [&]<comparison Op>() { return count_as<Op>(u, blocks, rhs); });
    }
    std::uint64_t count_uniform(comparison op, unsigned u, const bitset256 &blocks, std::uint64_t t) const override {
        return with_op(op, [&]<comparison Op>() { return count_uniform_as<Op>(u, blocks, t); });
    }
    void filter(comparison op, unsigned u, const bitset256 &blocks, const block_args &rhs, std::span<bitset256> out) const override {
        with_op(op, [&]<comparison Op>() { filter_as<Op>(u, blocks, rhs, out); });
    }
    // Field first: a nonzero field is never an exception.
    std::uint64_t get(unsigned u, unsigned row) const override {
        const unit_state &s = units_[u];
        const unsigned b = row / block_rows;
        const std::uint64_t f = child_->get(u, row);
        if (f == 0 && s.count[b])
            if (const int e = find(s, b, row % block_rows); e >= 0)
                return area_of(s, b).val[e];
        return s.lo[b] + f;
    }
    void read(unsigned u, unsigned first, unsigned n, std::uint64_t *out) const override {
        const unit_state &s = units_[u];
        child_->read(u, first, n, out);
        for (unsigned at = 0; at < n;) {
            const unsigned row = first + at, b = row / block_rows, m = std::min(n - at, rows_in(u, b) - row % block_rows);
            for (unsigned i = 0; i < m; ++i)
                out[at + i] += s.lo[b];
            each_exception(s, b, [&](std::uint8_t pos, std::uint64_t v) {
                if (const unsigned r = b * block_rows + pos; r >= row && r < row + m)
                    out[at + r - row] = v;
            });
            at += m;
        }
    }
    std::size_t select(unsigned u, const bitset256 &blocks, const block_rows_set &rows, std::uint64_t *out) const override {
        const unit_state &s = units_[u];
        const std::size_t k = child_->select(u, blocks, rows, out);
        std::size_t at = 0;
        blocks.for_each([&](unsigned b) {
            const bitset256 r = rows[b] & bitset256::first_n(rows_in(u, b));
            const unsigned n = r.count();
            for (unsigned i = 0; i < n; ++i)
                out[at + i] += s.lo[b];
            each_exception(s, b, [&](std::uint8_t pos, std::uint64_t v) {
                if (r.contains(pos))
                    out[at + r.rank(pos)] = v;
            });
            at += n;
        });
        return k;
    }
};
} // namespace

std::unique_ptr<author::stage> make_bound(std::unique_ptr<author::stage> child, const fitting_policy &policy) {
    return std::make_unique<bound_stage>(std::move(child), policy);
}
bool is_bound(const author::stage &s) noexcept { return dynamic_cast<const bound_stage *>(&s) != nullptr; }
bool bound_entry(const author::stage &s, unsigned unit, unsigned block, bound_point_entry &out) {
    const auto *b = dynamic_cast<const bound_stage *>(&s);
    return b && b->entry(unit, block, out);
}

} // namespace ikea::frame::detail

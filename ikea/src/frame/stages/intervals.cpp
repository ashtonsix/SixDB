// intervals: a checkpoint (the value) every spacing rows, as a series of its
// own in its own stream, and an offset-binary field per row: its difference
// from the previous row (sizes) or from its region's checkpoint (relative
// ends). Offset binary keeps signed differences one contiguous window for a
// bound child. Reconstruction and endpoint search are Intervals' (wide_sizes,
// wide_ends and lower_bound over the decoded fields); this stage supplies the
// offset-binary conversion, the evidence and the laws.
#include "../sections.h"

#include <ikea/frame/detail/stages.h>
#include <ikea/intervals/search.h>
#include <ikea/intervals/wide.h>

namespace ikea::frame::detail {
namespace {
using author::stage;
namespace in = ikea::intervals;

constexpr std::uint64_t bias = std::uint64_t{1} << 63;

class intervals_stage final : public stage {
    std::unique_ptr<stage> children_[2]; // fields, checkpoints
    unsigned spacing_;
    interval_form form_;
    // Per block evidence for the laws, emitted with the image and proved by
    // image validation: the last value, whether values never decrease, and
    // whether every field is an exact int64 difference (no wrap).
    struct unit_state {
        std::vector<std::uint64_t> last;
        std::vector<std::uint8_t> monotone, exact;
    };
    std::vector<unit_state> units_;

    bool relative() const noexcept { return form_ == interval_form::relative_ends; }
    unit_state &unit(unsigned u) {
        if (units_.size() <= u)
            units_.resize(u + 1);
        return units_[u];
    }
    unsigned checkpoints(unsigned u, unsigned b, std::uint64_t *cp) const {
        const unsigned n = (rows_in(u, b) + spacing_ - 1) / spacing_;
        children_[1]->read(u, b * (block_rows / spacing_), n, cp);
        return n;
    }
    // Full blocks whose fields are exact take the law; the rest decode.
    void split(unsigned u, const bitset256 &blocks, bitset256 &full, bitset256 &partial) const {
        blocks.for_each([&](unsigned b) {
            (rows_in(u, b) == block_rows && units_[u].exact[b] ? full : partial).set(b);
        });
    }
    __int128 checkpoint_sum(unsigned u, const bitset256 &full) const {
        __int128 t = 0;
        full.for_each([&](unsigned b) {
            alignas(64) std::uint64_t cp[block_rows];
            const unsigned n = checkpoints(u, b, cp);
            for (unsigned g = 0; g < n; ++g)
                t += static_cast<__int128>(spacing_) * cp[g];
        });
        return t;
    }

  public:
    intervals_stage(std::unique_ptr<stage> fields, std::unique_ptr<stage> checkpoints, unsigned spacing, interval_form form)
        : children_{std::move(fields), std::move(checkpoints)}, spacing_(spacing), form_(form) {}
    std::span<const std::unique_ptr<stage>> children() const override { return children_; }
    const char *role(unsigned child) const override { return child ? "checkpoints" : "deltas"; }
    // Checkpoints have their own row domain (one per spacing rows).
    bool inline_child(unsigned child) const override { return child == 0; }

    std::expected<void, error> fit(unsigned u, const author::fit_input &input) override {
        const auto values = input.values;
        if (values.size() > unit_rows)
            return std::unexpected(error::range);
        unit_state &s = unit(u) = {};
        sized(u, values.size());
        std::vector<std::uint64_t> d(values.size()), cp;
        for (std::size_t r = 0; r < values.size(); ++r) {
            if (r % spacing_ == 0)
                cp.push_back(values[r]), d[r] = bias;
            else
                d[r] = (values[r] - (relative() ? cp.back() : values[r - 1])) ^ bias;
        }
        for (unsigned b = 0; b < blocks_in(u); ++b) {
            const std::uint64_t *x = values.data() + std::size_t{b} * block_rows;
            const unsigned m = rows_in(u, b);
            s.last.push_back(x[m - 1]);
            s.monotone.push_back(std::is_sorted(x, x + m));
            bool fits = true;
            for (unsigned r = 1; r < m; ++r) {
                const std::uint64_t ref = relative() ? x[r - r % spacing_] : x[r - 1];
                fits &= x[r] >= ref ? x[r] - ref < bias : ref - x[r] <= bias;
            }
            s.exact.push_back(fits);
        }
        if (auto f = children_[0]->fit(u, {d}); !f)
            return f;
        return children_[1]->fit(u, {cp});
    }
    // Section: per block last u64, monotone u8, exact u8; then the fields'
    // section. Checkpoints are their own stream.
    void emit(unsigned u, unsigned g, std::vector<std::byte> &out) const override {
        const unit_state &s = units_[u];
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(u) - first);
        for (unsigned b = first; b < first + n; ++b)
            put<std::uint64_t>(out, s.last[b]), put<std::uint8_t>(out, s.monotone[b]), put<std::uint8_t>(out, s.exact[b]);
        children_[0]->emit(u, g, out);
    }
    std::expected<std::size_t, error> attach(unsigned u, std::size_t rows, unsigned g,
                                             std::span<const std::byte> image) override {
        if (rows > unit_rows)
            return std::unexpected(error::range);
        sized(u, rows);
        unit_state &s = unit(u);
        s.last.resize(blocks_in(u)), s.monotone.resize(blocks_in(u)), s.exact.resize(blocks_in(u));
        if (g >= groups_in(u))
            return std::unexpected(error::range);
        cursor c{image};
        const unsigned first = g * group_blocks, n = std::min(group_blocks, blocks_in(u) - first);
        for (unsigned b = first; b < first + n; ++b) {
            s.last[b] = c.take<std::uint64_t>(), s.monotone[b] = c.take<std::uint8_t>(), s.exact[b] = c.take<std::uint8_t>();
            if (!c.ok || s.monotone[b] > 1 || s.exact[b] > 1)
                return std::unexpected(error::contents);
        }
        const auto used = children_[0]->attach(u, rows, g, image.subspan(c.at));
        if (!used)
            return used;
        return c.at + *used;
    }
    void describe(unsigned u, author::census &c) const override {
        const unit_state &s = units_[u];
        c.checkpoints += (rows(u) + spacing_ - 1) / spacing_;
        for (std::uint8_t m : s.monotone)
            c.monotone += m;
        children_[0]->describe(u, c);
    }

    // Decoded fields become two's-complement differences, then Intervals
    // reconstructs the block from its checkpoints.
    template <class F> void reconstructed(const std::uint64_t *fields, const std::uint64_t *cp, unsigned n, F &&f) const {
        if (relative())
            in::wide_ends{fields, cp, n, spacing_}.visit_intervals(0, n, f);
        else
            in::wide_sizes{fields, cp, n, spacing_}.visit_intervals(0, n, f);
    }
    void decode(unsigned u, unsigned b, std::uint64_t *out) const override {
        alignas(64) std::uint64_t fields[block_rows], cp[block_rows];
        children_[0]->decode(u, b, fields);
        checkpoints(u, b, cp);
        const unsigned m = rows_in(u, b);
        for (unsigned r = 0; r < m; ++r)
            fields[r] ^= bias;
        reconstructed(fields, cp, m, [&](unsigned r, in::interval<std::uint64_t> v) { out[r] = v.limit; });
    }
    std::uint64_t get(unsigned u, unsigned row) const override {
        const std::uint64_t cp = children_[1]->get(u, row / spacing_);
        const unsigned k = row % spacing_;
        if (!k)
            return cp;
        alignas(64) std::uint64_t w[block_rows];
        if (relative()) { // no prefix: the checkpoint plus one field
            w[k] = children_[0]->get(u, row) ^ bias;
            return in::wide_ends{w, &cp, k + 1, spacing_}.at(k);
        }
        children_[0]->read(u, row - k + 1, k, w + 1);
        for (unsigned i = 1; i <= k; ++i)
            w[i] ^= bias;
        return in::wide_sizes{w, &cp, k + 1, spacing_}.at(k);
    }
    // Within a region of spacing G starting at checkpoint c, the values sum
    // to G c + sum_i (G - i) delta_i, and delta_i = field_i - 2^63: one exact
    // weighted SUM of the child per block (weight 0 at checkpoint rows).
    // Relative ends: G c + sum of the offsets, so one plain SUM of the child.
    // Partial blocks, and blocks whose fields wrap, decode.
    u128 sum(unsigned u, const bitset256 &blocks) const override {
        if (!relative() && spacing_ != 16 && spacing_ != 64)
            return stage::sum(u, blocks);
        bitset256 full, partial;
        split(u, blocks, full, partial);
        __int128 t = partial.empty() ? 0 : static_cast<__int128>(stage::sum(u, partial));
        if (full.empty())
            return static_cast<u128>(t);
        t += checkpoint_sum(u, full);
        const __int128 nb = full.count();
        if (relative()) {
            t += static_cast<__int128>(children_[0]->sum(u, full)) - nb * block_rows * static_cast<__int128>(bias);
        } else {
            std::array<std::uint8_t, 64> w{};
            for (unsigned r = 1; r < spacing_; ++r)
                w[r] = static_cast<std::uint8_t>(spacing_ - r);
            const __int128 per_block = __int128{block_rows / 2} * (spacing_ - 1); // sum of weights in a block
            t += static_cast<__int128>(children_[0]->weighted_sum(u, full, w.data(), spacing_)) -
                 nb * per_block * static_cast<__int128>(bias);
        }
        return static_cast<u128>(t);
    }
    // Monotone blocks settle by their first and last values, else by an
    // endpoint search over the checkpoints and one region. Others decode.
    std::uint64_t count(comparison op, unsigned u, const bitset256 &blocks, const block_args &rhs) const override {
        const unit_state &s = units_[u];
        if (op != comparison::greater_equal)
            return stage::count(op, u, blocks, rhs);
        bitset256 other;
        std::uint64_t c = 0;
        blocks.for_each([&](unsigned b) {
            if (!s.monotone[b])
                return other.set(b);
            const std::uint64_t t = rhs[b];
            const unsigned m = rows_in(u, b);
            alignas(64) std::uint64_t cp[block_rows];
            const unsigned regions = checkpoints(u, b, cp);
            if (cp[0] >= t) {
                c += m;
            } else if (s.last[b] >= t) {
                // The last region whose checkpoint is below t holds the
                // boundary, then the first of its values at or above t.
                const unsigned g = in::lower_bound(in::direct_source<std::uint64_t>{cp, regions}, t) - 1;
                const unsigned k = std::min(spacing_, m - g * spacing_) - 1;
                alignas(64) std::uint64_t w[block_rows];
                children_[0]->read(u, b * block_rows + g * spacing_ + 1, k, w + 1);
                for (unsigned i = 1; i <= k; ++i)
                    w[i] ^= bias;
                const unsigned below = relative() ? in::lower_bound(in::wide_ends{w, &cp[g], k + 1, spacing_}, t)
                                                  : in::lower_bound(in::wide_sizes{w, &cp[g], k + 1, spacing_}, t);
                c += m - g * spacing_ - below;
            }
        });
        if (!other.empty())
            c += stage::count(op, u, other, rhs);
        return c;
    }
};
} // namespace

std::unique_ptr<author::stage> make_intervals(std::unique_ptr<author::stage> fields, std::unique_ptr<author::stage> checkpoints,
                                              unsigned spacing, interval_form form) {
    return std::make_unique<intervals_stage>(std::move(fields), std::move(checkpoints), spacing, form);
}

} // namespace ikea::frame::detail

// Every landing stage tree against a plain-array oracle: SUM, COUNT and
// FILTER under every comparison at boundary thresholds, MAX, every point,
// whole reads and selections, on generated columns that exercise the laws'
// edge cases (constant blocks, exceptions on both sides, 64-bit extremes,
// signed and monotone differences, a partial last unit and block). Then the
// same trees rebuilt from their emitted sections alone, and the point plan.
#include <ikea/frame/build.h>
#include <ikea/frame/detail/execution.h>
#include <ikea/frame/detail/stages.h>

#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>

namespace {
namespace fr = ikea::frame;
using fr::author::stage;
using fr::detail::interval_form;
using u64 = std::uint64_t;
using ikea::bitset256;
using ikea::comparison;

int failures = 0;
void expect(bool ok, const std::string &what) {
    if (!ok && failures++ < 20)
        std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

// A leaf holding one value in every row, whose SUM law is that value times
// the request's selected rows: the shape of a constant child, so a stale
// selected() after within() would give a wrong SUM.
class constant_stage final : public stage {
    u64 c_;

  public:
    explicit constant_stage(u64 c) : c_(c) {}
    std::expected<void, fr::error> fit(unsigned u, const fr::author::fit_input &in) override {
        sized(u, in.values.size());
        return {};
    }
    void decode(unsigned u, unsigned b, u64 *out) const override { std::fill_n(out, rows_in(u, b), c_); }
    fr::u128 sum(unsigned, const fr::author::request &q) const override { return fr::u128{c_} * q.selected(); }
};

// A leaf that only decodes stored values, so every read runs the reference
// defaults (the masked SUM and summary choose a bit walk or a dense pass by
// each block's density).
class decoded_stage final : public stage {
    std::vector<std::vector<u64>> v_;

  public:
    std::expected<void, fr::error> fit(unsigned u, const fr::author::fit_input &in) override {
        sized(u, in.values.size());
        if (v_.size() <= u)
            v_.resize(u + 1);
        v_[u].assign(in.values.begin(), in.values.end());
        return {};
    }
    void decode(unsigned u, unsigned b, u64 *out) const override {
        std::copy_n(v_[u].data() + std::size_t{b} * fr::block_rows, rows_in(u, b), out);
    }
};

using make_tree = std::function<std::unique_ptr<stage>()>;
std::unique_ptr<stage> bound_pack(fr::pack_preset p = fr::pack_preset::compact, unsigned penalty = 0) {
    return fr::detail::make_bound(fr::detail::make_pack(p), {penalty, 64, 0});
}
std::unique_ptr<stage> delta(unsigned G, interval_form form) {
    return fr::detail::make_intervals(bound_pack(), bound_pack(), G, form);
}
const std::vector<std::pair<std::string, make_tree>> &trees() {
    static const std::vector<std::pair<std::string, make_tree>> all{
        {"pack", [] { return fr::detail::make_pack(fr::pack_preset::compact); }},
        {"bound_pack", [] { return bound_pack(); }},
        {"bound_pack_bulk_x86", [] { return bound_pack(fr::pack_preset::bulk_x86); }},
        {"bound_pack_bulk_arm", [] { return bound_pack(fr::pack_preset::bulk_arm); }},
        {"bound_pack_penalty4", [] { return bound_pack(fr::pack_preset::compact, 4); }},
        {"sizes16", [] { return delta(16, interval_form::sizes); }},
        {"sizes64", [] { return delta(64, interval_form::sizes); }},
        {"sizes256", [] { return delta(256, interval_form::sizes); }},
        {"relative16", [] { return delta(16, interval_form::relative_ends); }},
        {"relative64", [] { return delta(64, interval_form::relative_ends); }},
    };
    return all;
}

std::vector<std::pair<std::string, std::vector<u64>>> columns() {
    std::mt19937_64 rng(7);
    const std::size_t n = fr::unit_rows * 2 + fr::block_rows * 17 + 37; // a partial last unit and block
    std::vector<std::pair<std::string, std::vector<u64>>> out;
    auto add = [&](std::string name, auto f) {
        std::vector<u64> v(n);
        for (std::size_t i = 0; i < n; ++i)
            v[i] = f(i);
        out.emplace_back(std::move(name), std::move(v));
    };
    add("narrow", [&](std::size_t) { return rng() % 200; });
    add("constant_blocks", [&](std::size_t i) { return (i / fr::block_rows) % 3 ? 77 + i / fr::block_rows : rng() % 9; });
    add("low_outliers", [&](std::size_t) { return rng() % 50 == 0 ? rng() % 10 : 1000 + rng() % 64; });
    add("two_sided", [&](std::size_t) {
        const u64 r = rng() % 100;
        return r == 0 ? rng() % 5 : r == 1 ? (u64{1} << 40) + rng() : 5000 + rng() % 300;
    });
    add("extremes", [&](std::size_t) {
        const u64 r = rng() % 8;
        return r == 0 ? ~u64{0} : r == 1 ? 0 : ~u64{0} - rng() % 1000;
    });
    add("wide", [&](std::size_t) { return rng(); });
    add("monotone", [&, t = u64{1} << 50](std::size_t) mutable { return t += rng() % 1000; });
    add("signed_walk", [&, t = u64{1} << 33](std::size_t) mutable { return t += (rng() % 2001) - 1000; });
    add("sawtooth", [&](std::size_t i) { return (i % 700) * 3 + (rng() % 2); });
    return out;
}

/// A column of units over one tree, with commands over whole columns. Bit r
/// of words[r / 64] selects original row r.
struct column {
    std::unique_ptr<stage> root;
    std::size_t rows = 0;
    unsigned units = 0;

    bitset256 blocks(unsigned u) const { return root->blocks(u); }
    void rows_of(unsigned u, const u64 *words, fr::block_rows_set &rows_set, bitset256 &touched) const {
        touched = {};
        blocks(u).for_each([&](unsigned b) {
            rows_set[b] = {};
            std::copy_n(words + (u * fr::unit_rows + b * fr::block_rows) / 64, (root->rows_in(u, b) + 63) / 64,
                        rows_set[b].words.data());
            if (!rows_set[b].empty())
                touched.set(b);
        });
    }
    fr::u128 sum() const {
        fr::u128 s = 0;
        for (unsigned u = 0; u < units; ++u)
            s += root->sum(u, blocks(u));
        return s;
    }
    fr::u128 sum(const u64 *words) const {
        fr::block_rows_set r;
        bitset256 t;
        fr::u128 s = 0;
        for (unsigned u = 0; u < units; ++u)
            if (rows_of(u, words, r, t); !t.empty())
                s += root->sum_selected(u, t, r);
        return s;
    }
    fr::summary summarise(const u64 *words) const {
        fr::block_rows_set r;
        bitset256 t;
        fr::summary out;
        for (unsigned u = 0; u < units; ++u)
            if (rows_of(u, words, r, t); !t.empty())
                root->summarise_selected(u, t, r, out);
        return out;
    }
    std::optional<u64> max(const u64 *words) const {
        fr::block_rows_set r;
        bitset256 t;
        u64 m = 0;
        bool any = false;
        for (unsigned u = 0; u < units; ++u)
            if (rows_of(u, words, r, t); !t.empty())
                any |= root->max_selected(u, t, r, m);
        return any ? std::optional(m) : std::nullopt;
    }
    fr::u128 weighted(const std::uint8_t *weights, unsigned period) const {
        fr::u128 s = 0;
        for (unsigned u = 0; u < units; ++u)
            s += root->weighted_sum(u, blocks(u), weights, period);
        return s;
    }
    u64 count(comparison op, u64 t) const {
        u64 c = 0;
        for (unsigned u = 0; u < units; ++u)
            c += root->count_uniform(op, u, blocks(u), t);
        return c;
    }
    u64 count_per_block(comparison op, u64 t) const {
        fr::block_args rhs;
        rhs.fill(t);
        u64 c = 0;
        for (unsigned u = 0; u < units; ++u)
            c += root->count(op, u, blocks(u), rhs);
        return c;
    }
    void filter(comparison op, u64 t, u64 *words) const {
        fr::block_args rhs;
        rhs.fill(t);
        fr::block_rows_set out;
        for (unsigned u = 0; u < units; ++u) {
            root->filter(op, u, blocks(u), rhs, out);
            blocks(u).for_each([&](unsigned b) {
                std::copy_n(out[b].words.data(), (root->rows_in(u, b) + 63) / 64,
                            words + (u * fr::unit_rows + b * fr::block_rows) / 64);
            });
        }
    }
    u64 get(std::size_t row) const {
        return root->get(static_cast<unsigned>(row / fr::unit_rows), static_cast<unsigned>(row % fr::unit_rows));
    }
    void read(u64 *out) const {
        for (unsigned u = 0; u < units; ++u)
            root->read(u, 0, root->rows(u), out + u * fr::unit_rows);
    }
    std::size_t select(const u64 *words, u64 *out) const {
        fr::block_rows_set r;
        bitset256 t;
        std::size_t k = 0;
        for (unsigned u = 0; u < units; ++u)
            if (rows_of(u, words, r, t); !t.empty())
                k += root->select(u, t, r, out + k);
        return k;
    }
};

column built(std::unique_ptr<stage> root, const std::vector<u64> &v) {
    column c{std::move(root), v.size(), static_cast<unsigned>((v.size() + fr::unit_rows - 1) / fr::unit_rows)};
    for (unsigned u = 0; u < c.units; ++u) {
        const std::size_t first = std::size_t{u} * fr::unit_rows;
        if (!c.root->fit(u, {std::span(v).subspan(first, std::min(fr::unit_rows, v.size() - first))}))
            std::abort();
    }
    return c;
}
// A fresh tree attached from another's emitted sections alone, stream by stream.
column reattached(const column &from, std::unique_ptr<stage> root, bool &ok) {
    column c{std::move(root), from.rows, from.units};
    std::vector<std::pair<std::string, const stage *>> src{{"", from.root.get()}}, dst{{"", c.root.get()}};
    from.root->streams("", src);
    c.root->streams("", dst);
    ok = src.size() == dst.size();
    for (std::size_t i = 0; ok && i < src.size(); ++i)
        for (unsigned u = 0; ok && u < c.units; ++u)
            for (unsigned g = 0; ok && g < src[i].second->groups_in(u); ++g) {
                std::vector<std::byte> image;
                src[i].second->emit(u, g, image);
                static std::vector<std::vector<std::byte>> keep; // attached bytes stay alive
                keep.push_back(std::move(image));
                const auto used = const_cast<stage *>(dst[i].second)->attach(u, src[i].second->rows(u), g, keep.back());
                ok = used && *used == keep.back().size();
            }
    return c;
}

// Requests over spans of unit rows, with ragged edges inside one block,
// across a group boundary and to a short tail, under an all, alternating or
// sparse coarse mask and optionally a fine mask: SUM, COUNT, FILTER, MAX,
// summary and select against the oracle over exactly those rows.
void spans(const std::string &what, const column &c, const std::vector<u64> &v) {
    std::mt19937_64 rng(13);
    for (unsigned u = 0; u < c.units; ++u) {
        const unsigned rows = c.root->rows(u), nb = c.root->blocks_in(u);
        const std::size_t base = std::size_t{u} * fr::unit_rows;
        std::vector<std::pair<unsigned, unsigned>> ranges{{0, rows}, {3900, 4600}, {5, 200}, {256, 256}, {rows - 7, 7},
                                                          {4095, 2}, {rows / 3, rows - rows / 3}};
        for (const auto [first, count] : ranges) {
            if (first > rows || count > rows - first)
                continue;
            for (int mask = 0; mask < 3; ++mask) {
                bitset256 coarse;
                for (unsigned b = 0; b < nb; ++b)
                    coarse.set(b, mask == 0 || (mask == 1 ? b / 8 % 2 == 0 : rng() % 5 == 0));
                for (bool fine : {false, true}) {
                    fr::block_rows_set rows_set;
                    for (auto &r : rows_set)
                        for (auto &w : r.words)
                            w = fine ? rng() & rng() : ~u64{0};
                    const auto sel = fine ? ikea::seriespack::selection::bits(0, std::span<const bitset256>(rows_set))
                                          : ikea::seriespack::selection::all();
                    const auto q = fr::author::request::make(rows, first, count, coarse, sel);
                    std::vector<u64> want;
                    for (unsigned r = first; r < first + count; ++r)
                        if (coarse.contains(r / 256) && rows_set[r / 256].contains(r % 256))
                            want.push_back(v[base + r]);
                    fr::u128 ws = 0;
                    u64 lo = ~u64{0}, hi = 0;
                    for (u64 x : want)
                        ws += x, lo = std::min(lo, x), hi = std::max(hi, x);
                    const std::string at = " span " + std::to_string(first) + "+" + std::to_string(count) + " mask " +
                                           std::to_string(mask) + (fine ? " fine" : "");
                    expect(q.selected() == want.size(), what + " request rows" + at);
                    // A request narrowed to one group counts and sums only
                    // that group's rows.
                    for (unsigned g = 0; g < c.root->groups_in(u); ++g) {
                        const bitset256 group = bitset256::first_n(std::min(nb, (g + 1) * 16)) & ~bitset256::first_n(g * 16);
                        const auto part = q.within(group);
                        std::uint64_t k = 0;
                        fr::u128 gs = 0;
                        for (unsigned r = std::max(first, g * 4096); r < std::min(first + count, (g + 1) * 4096); ++r)
                            if (coarse.contains(r / 256) && rows_set[r / 256].contains(r % 256))
                                ++k, gs += v[base + r];
                        expect(part.selected() == k && c.root->sum(u, part) == gs, what + " request within group" + at);
                    }
                    expect(c.root->sum(u, q) == ws, what + " span sum" + at);
                    const u64 t = want.empty() ? 0 : want[want.size() / 2];
                    for (comparison op : {comparison::greater_equal, comparison::less}) {
                        u64 k = 0;
                        for (u64 x : want)
                            k += fr::author::test(op, x, t);
                        expect(c.root->count(u, q, {op, t}) == k, what + " span count" + at);
                    }
                    std::vector<bitset256> out(fr::unit_blocks);
                    c.root->filter(u, q, {comparison::greater_equal, t}, out);
                    bool filtered = true;
                    q.blocks().for_each([&](unsigned b) {
                        bitset256 expect_bits;
                        for (unsigned r = 0; r < c.root->rows_in(u, b); ++r) {
                            const unsigned row = b * 256 + r;
                            if (row >= first && row < first + count && coarse.contains(b) && rows_set[b].contains(r) &&
                                v[base + row] >= t)
                                expect_bits.set(r);
                        }
                        filtered &= out[b] == expect_bits;
                    });
                    expect(filtered, what + " span filter" + at);
                    u64 m = 0;
                    expect(c.root->max(u, q, m) == !want.empty() && m == hi, what + " span max" + at);
                    fr::summary sm;
                    c.root->summarise(u, q, sm);
                    expect(sm.sum == ws && sm.count == want.size() && sm.min == lo && sm.max == hi, what + " span summary" + at);
                    std::vector<u64> got(want.size() + 1);
                    got.resize(c.root->select(u, q, got.data()));
                    expect(got == want, what + " span select" + at);
                }
            }
        }
        // Fine selections covering only the span's rows, in exactly sized
        // u16 backing from a 16-aligned origin: inside one word, starting or
        // ending mid-word, and across a block edge. Lowering must read only
        // those rows.
        for (const auto [first, count] : std::vector<std::pair<unsigned, unsigned>>{
                 {17, 14}, {5, 40}, {100, 60}, {250, 20}, {4090, 30}, {0, 1}, {rows - 3, 3}}) {
            if (first > rows || count > rows - first || !count)
                continue;
            const unsigned origin = first / 16 * 16;
            std::vector<std::uint16_t> words((first + count - origin + 15) / 16);
            for (auto &w : words)
                w = static_cast<std::uint16_t>(rng());
            const auto sel = ikea::seriespack::selection::bits(origin, std::span<const std::uint16_t>(words));
            const auto q = fr::author::request::make(rows, first, count, bitset256::first_n(c.root->blocks_in(u)), sel);
            fr::u128 ws = 0;
            u64 n = 0;
            for (unsigned r = first; r < first + count; ++r)
                if (words[(r - origin) / 16] >> ((r - origin) % 16) & 1)
                    ws += v[base + r], ++n;
            const std::string at = " narrow " + std::to_string(first) + "+" + std::to_string(count);
            expect(q.selected() == n, what + at + " rows");
            expect(c.root->sum(u, q) == ws, what + at + " sum");
            u64 k = 0;
            for (unsigned r = first; r < first + count; ++r)
                if (words[(r - origin) / 16] >> ((r - origin) % 16) & 1)
                    k += v[base + r] >= v[base + first];
            expect(c.root->count(u, q, {comparison::greater_equal, v[base + first]}) == k, what + at + " count");
        }
    }
}

void check(const std::string &what, const column &c, const std::vector<u64> &v) {
    const std::size_t n = v.size();
    fr::u128 s = 0;
    for (u64 x : v)
        s += x;
    expect(c.sum() == s, what + " sum");
    std::set<u64> thresholds{0, 1, 2, ~u64{0}, ~u64{0} - 1, v[0], v[n / 2], v[n - 1]};
    std::mt19937_64 rng(11);
    for (int i = 0; i < 12; ++i)
        thresholds.insert(v[rng() % n]), thresholds.insert(v[rng() % n] + 1), thresholds.insert(v[rng() % n] - 1);
    std::vector<u64> words((n + 63) / 64), oracle((n + 63) / 64);
    for (comparison op : {comparison::equal, comparison::less, comparison::less_equal, comparison::greater, comparison::greater_equal})
        for (u64 t : thresholds) {
            u64 k = 0;
            std::fill(oracle.begin(), oracle.end(), 0);
            for (std::size_t i = 0; i < n; ++i)
                if (fr::author::test(op, v[i], t))
                    ++k, oracle[i / 64] |= u64{1} << (i % 64);
            const std::string at = " op " + std::to_string(int(op)) + " t " + std::to_string(t);
            expect(c.count(op, t) == k, what + " count" + at);
            expect(c.count_per_block(op, t) == k, what + " count per block" + at);
            std::fill(words.begin(), words.end(), 0);
            c.filter(op, t, words.data());
            expect(words == oracle, what + " filter" + at);
        }
    // Position weights, including all-zero and maximal ones, over every block
    // (the unit's partial last block takes the tail of a period).
    for (unsigned period : {16u, 64u})
        for (int kind = 0; kind < 3; ++kind) {
            std::array<std::uint8_t, 64> w{};
            for (unsigned r = 0; r < period; ++r)
                w[r] = kind == 0 ? 0 : kind == 1 ? 255 : static_cast<std::uint8_t>(rng());
            fr::u128 want = 0;
            for (std::size_t i = 0; i < n; ++i)
                want += static_cast<fr::u128>(v[i]) * w[i % fr::unit_rows % period];
            expect(c.weighted(w.data(), period) == want, what + " weighted period " + std::to_string(period));
        }
    bool points = true;
    for (std::size_t i = 0; i < n; ++i)
        points &= c.get(i) == v[i];
    expect(points, what + " get");
    std::vector<u64> all(n);
    c.read(all.data());
    expect(all == v, what + " read");
    for (unsigned density : {1u, 30u, 200u, 256u}) {
        std::fill(words.begin(), words.end(), 0);
        std::vector<u64> want;
        for (std::size_t i = 0; i < n; ++i)
            if (rng() % 256 < density)
                words[i / 64] |= u64{1} << (i % 64), want.push_back(v[i]);
        const std::string at = " density " + std::to_string(density);
        fr::u128 ws = 0;
        u64 lo = ~u64{0}, hi = 0;
        for (u64 x : want)
            ws += x, lo = std::min(lo, x), hi = std::max(hi, x);
        expect(c.sum(words.data()) == ws, what + " sum selected" + at);
        const auto m = c.max(words.data());
        expect(want.empty() ? !m : m && *m == hi, what + " max" + at);
        const auto sm = c.summarise(words.data());
        expect(sm.sum == ws && sm.count == want.size() && sm.min == lo && sm.max == hi, what + " summarise" + at);
        std::vector<u64> got(want.size() + 1);
        got.resize(c.select(words.data(), got.data()));
        expect(got == want, what + " select" + at);
    }
    spans(what, c, v);
}
} // namespace

int main() {
    const auto cols = columns();
    for (const auto &[name, v] : cols)
        for (const auto &[tree, make] : trees()) {
            const column c = built(make(), v);
            check(name + " / " + tree, c, v);
            bool ok = false;
            const column again = reattached(c, make(), ok);
            expect(ok, name + " / " + tree + " sections");
            if (ok && (tree == "bound_pack" || tree == "sizes16" || tree == "relative64"))
                check(name + " / " + tree + " attached", again, v);
        }

    // Checkpoint contributions weigh each checkpoint by its region's
    // selected rows (Codex's witness): regions 10.. and 100.. at G16, rows
    // {0, 1, 3, 16} sum to 3*10 + 100 + offsets 0+1+3+0 = 134, not 110 or
    // 16 * 110. Then a short last region (seven rows) under every row.
    for (const auto form : {interval_form::relative_ends, interval_form::sizes}) {
        std::vector<u64> v(4096 + 7);
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] = (i < 16 ? 10 : i < 32 ? 100 : 1000 + 3 * i) + (i < 32 ? i % 16 : 0);
        const column c = built(delta(16, form), v);
        fr::block_rows_set rows{};
        for (unsigned r : {0u, 1u, 3u, 16u})
            rows[0].set(r);
        expect(c.root->sum_selected(0, bitset256::first_n(1), rows) == 134, "checkpoint weight witness");
        const auto tail = fr::author::request::make(c.root->rows(0), 4096, 7, c.root->blocks(0), ikea::seriespack::selection::all());
        fr::u128 want = 0;
        for (std::size_t i = 4096; i < v.size(); ++i)
            want += v[i];
        expect(c.root->sum(0, tail) == want, "short last region weight");
    }

    // A span over three groups with a coarse hole and fine edges, narrowed
    // to each group: selected() and the constant child's SUM law count only
    // that group's rows.
    {
        const std::vector<u64> v(3 * 4096 + 77, 7);
        const column c = built(std::make_unique<constant_stage>(7), v);
        const unsigned rows = c.root->rows(0);
        bitset256 coarse = c.root->blocks(0);
        coarse.set(20, false), coarse.set(21, false);
        fr::block_rows_set fine;
        std::mt19937_64 rng(29);
        for (auto &r : fine)
            for (auto &w : r.words)
                w = rng() | rng();
        const auto q = fr::author::request::make(rows, 3900, 4600, coarse,
                                                 ikea::seriespack::selection::bits(0, std::span<const bitset256>(fine)));
        for (unsigned g = 0; g < 3; ++g) {
            const auto part = q.within(bitset256::first_n((g + 1) * 16) & ~bitset256::first_n(g * 16));
            std::uint64_t k = 0;
            for (unsigned r = std::max(3900u, g * 4096); r < std::min(8500u, (g + 1) * 4096); ++r)
                k += coarse.contains(r / 256) && fine[r / 256].contains(r % 256);
            expect(part.selected() == k && c.root->sum(0, part) == fr::u128{7} * k,
                   "constant child within group " + std::to_string(g));
        }
    }

    // Masked reductions by density, through the reference defaults: every
    // selection shape picks the bit walk or the dense identity pass, and
    // must agree with brute force for SUM, COUNT, MIN and MAX, over extreme
    // values (sums cross 2^64 per block) and a short last block.
    {
        std::mt19937_64 rng(31);
        std::vector<u64> v(fr::block_rows * 3 + 200);
        for (auto &x : v) {
            const u64 r = rng() % 6;
            x = r == 0 ? ~u64{0} : r == 1 ? 0 : r == 2 ? (u64{1} << 32) - 1 : r == 3 ? u64{1} << 32 : rng();
        }
        const column c = built(std::make_unique<decoded_stage>(), v);
        const unsigned nb = c.root->blocks_in(0);
        const std::vector<std::pair<std::string, std::function<bool(unsigned)>>> shapes{
            {"all", [](unsigned) { return true; }},
            {"none", [](unsigned) { return false; }},
            {"row 0", [](unsigned r) { return r == 0; }},
            {"row 63", [](unsigned r) { return r == 63; }},
            {"row 64", [](unsigned r) { return r == 64; }},
            {"block 255", [](unsigned r) { return r == 255; }},
            {"word edges", [](unsigned r) { return r % 64 == 0 || r % 64 == 63; }},
            {"period 3", [](unsigned r) { return r % 3 == 1; }},
            {"three in four", [&](unsigned) { return rng() % 4 != 0; }},
            {"all but one", [](unsigned r) { return r != 130; }},
            {"alternate halves", [](unsigned r) { return r / 128 % 2 == 0; }},
        };
        for (const auto &[name, keep] : shapes) {
            fr::block_rows_set rows{};
            for (unsigned b = 0; b < nb; ++b)
                for (unsigned r = 0; r < c.root->rows_in(0, b); ++r)
                    rows[b].set(r, keep(r));
            const auto q = fr::author::request::make(c.root->rows(0), 0, c.root->rows(0), c.root->blocks(0),
                                                     ikea::seriespack::selection::bits(0, std::span<const bitset256>(rows)));
            fr::u128 ws = 0;
            u64 n = 0, lo = ~u64{0}, hi = 0, ge = 0;
            const u64 t = u64{1} << 63;
            for (unsigned b = 0; b < nb; ++b)
                for (unsigned r = 0; r < c.root->rows_in(0, b); ++r)
                    if (rows[b].contains(r)) {
                        const u64 x = v[b * fr::block_rows + r];
                        ws += x, ++n, lo = std::min(lo, x), hi = std::max(hi, x), ge += x >= t;
                    }
            fr::summary sm;
            c.root->summarise(0, q, sm);
            u64 m = 0;
            const bool any = c.root->max(0, q, m);
            expect(c.root->sum(0, q) == ws && sm.sum == ws && sm.count == n && sm.min == lo && sm.max == hi &&
                       c.root->count(0, q, {comparison::greater_equal, t}) == ge && (n ? any && m == hi : !any),
                   "masked density " + name);
        }
        // Two partial blocks of one request, one sparse and one dense, keep
        // their own choices.
        fr::block_rows_set mixed{};
        mixed[0].set(5);
        mixed[1] = ~bitset256{} & bitset256::first_n(256);
        mixed[1].set(17, false);
        const auto q = fr::author::request::make(c.root->rows(0), 0, 512, bitset256::first_n(2),
                                                 ikea::seriespack::selection::bits(0, std::span<const bitset256>(mixed)));
        fr::u128 want = v[5];
        for (unsigned r = 0; r < 256; ++r)
            want += r == 17 ? 0 : v[256 + r];
        expect(c.root->sum(0, q) == want, "masked density mixed blocks");
    }

    // Cross-column selected movement: A's filter selects B's rows over the
    // same original rows, under different trees, clustered and dispersed.
    {
        const auto &a = cols[6].second, &d = cols[5].second, &bv = cols[2].second; // monotone, wide, low_outliers
        const column cb = built(delta(16, interval_form::relative_ends), bv);
        for (const auto *av : {&a, &d}) {
            const column ca = built(bound_pack(), *av);
            const u64 t = (*av)[av->size() * 9 / 10];
            std::vector<u64> words((bv.size() + 63) / 64), want;
            ca.filter(comparison::greater_equal, t, words.data());
            for (std::size_t i = 0; i < bv.size(); ++i)
                if ((*av)[i] >= t)
                    want.push_back(bv[i]);
            std::vector<u64> got(want.size() + 1);
            got.resize(cb.select(words.data(), got.data()));
            expect(!want.empty() && got == want, av == &a ? "movement clustered" : "movement dispersed");
        }
    }

    // The point plan: admission limits, then every point of every unit.
    using fr::detail::point_plan;
    expect(point_plan::admits(65536, 0) && !point_plan::admits(65537, 0), "point plan leaf limit");
    expect(point_plan::admits(1, ~std::uint32_t{0}) && !point_plan::admits(1, std::size_t{~std::uint32_t{0}} + 1),
           "point plan exception limit");
    // Constant columns have no exceptions and, with no slack, empty areas.
    auto plan_cols = cols;
    plan_cols.emplace_back("zeros", std::vector<u64>(fr::unit_rows + 300, 0));
    plan_cols.emplace_back("sevens", std::vector<u64>(fr::unit_rows + 300, 77));
    for (const auto &[name, v] : plan_cols) {
        const column c = built(bound_pack(), v);
        bool ok = true;
        for (unsigned u = 0; ok && u < c.units; ++u) {
            const auto plan = point_plan::make(*c.root, u);
            ok = plan.has_value();
            std::vector<std::uint32_t> rows(c.root->rows(u));
            for (std::uint32_t r = 0; r < rows.size(); ++r)
                rows[r] = r;
            std::vector<u64> out(rows.size());
            if (ok)
                plan->get_many<16>(rows.data(), rows.size(), out.data());
            for (std::size_t r = 0; ok && r < rows.size(); ++r)
                ok = out[r] == v[u * fr::unit_rows + r];
        }
        expect(ok, name + " point plan");
        expect(!point_plan::make(*built(delta(16, interval_form::sizes), v).root, 0), name + " point plan shape");
    }

    if (failures) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::printf("frame stages: every tree exact on %zu columns\n", cols.size());
}

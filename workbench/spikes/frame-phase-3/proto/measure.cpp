// Prototype 1 timing: do runtime-composed block-grain laws (frame.h,
// stages.h) match phase 2's hand glue over the same bytes?
//
// Arms over one column (phase 1's ClickBench slices, or a generated one):
//   glue       the no-RLE path of ../../frame-phase-2/runs/pipelines.cpp
//              (c49fb643): constant blocks exit early, other blocks take the
//              bounds stage's window and one SeriesPack array per width,
//              read through a virtual call per block. Copied, not changed,
//              except that it shares this file's loader.
//   frame      stage trees from trees.h: bound -> pack under the host's
//              presets, and intervals (G16) -> bound -> pack.
//   native     this thread's own control: the column as a u64 array.
// Operations: SUM, COUNT(x >= median), COUNT(x == a sampled value),
// FILTER(x >= median) to row words, 65,536 random points, SELECT at ~10%
// random rows, and a whole READ. Every timed answer is checked against the
// raw column after its timer stops. Warm; arm order rotates per round.
//
// usage: frame3_measure COLS_DIR ROWS ROUNDS COLUMN...  (COLUMN gen_ts or gen_walk to generate)
#include "trees.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>

using namespace frame3;

namespace {
std::vector<u64> load(const std::string &dir, const std::string &name, std::size_t rows) {
    if (name == "gen_ts" || name == "gen_walk") {
        std::mt19937_64 rng(5);
        std::vector<u64> v(rows / block_rows * block_rows);
        u64 t = u64{1} << 40;
        for (auto &x : v)
            x = name == "gen_ts" ? (t += rng() % 1000) : (t += (rng() % 2001) - 1000);
        return v;
    }
    std::ifstream f(dir + "/" + name + ".u64", std::ios::binary | std::ios::ate);
    if (!f)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(f.tellg()) / 8) / block_rows * block_rows;
    std::vector<u64> v(rows);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}

// ---- The hand glue (pipelines.cpp's no-RLE path with the bounds stage). ----
struct packed_width {
    virtual ~packed_width() = default;
    virtual u128 sum(std::size_t block) const = 0;
    virtual unsigned count_ge(std::size_t block, u64 rhs) const = 0;
    virtual u64 get(std::size_t row) const = 0;
    virtual std::size_t bytes() const = 0;
};
template <unsigned K, sp::preset P> struct packed_width_at final : packed_width {
    using L = sp::preset_layout<K, P>;
    std::vector<ikea::byte> storage;
    std::optional<sp::const_view<L>> view;
    std::optional<decltype(sp::bind_reader(std::declval<const sp::const_view<L> &>()))> reader;
    explicit packed_width_at(const std::vector<u64> &values) : storage(values.size() / L::tile_rows * L::tile_bytes) {
        const sp::dense_output target{K, L::storage, storage, values.size()};
        std::vector<ikea::journal::entry> records(*sp::effect_capacity_initialize(target));
        ikea::journal effects{records};
        if (!sp::initialize(target, std::span<const u64>(values), effects))
            std::abort();
        auto v = sp::const_view<L>::attach(values.size(), {{{std::span<const ikea::byte>(storage), L::tile_bytes}, {}, {}}});
        if (!v)
            std::abort();
        view.emplace(*v);
        reader.emplace(sp::bind_reader(*view));
    }
    u128 sum(std::size_t block) const override {
        const std::size_t first = block * block_rows;
        if constexpr (K <= 55) {
            return reader->sum_unchecked(first, block_rows);
        } else {
            alignas(64) u64 x[block_rows];
            reader->read_unchecked(first, block_rows, x);
            u128 s = 0;
            for (u64 y : x)
                s += y;
            return s;
        }
    }
    unsigned count_ge(std::size_t block, u64 rhs) const override {
        return static_cast<unsigned>(reader->template count_unchecked<ikea::comparison::greater_equal>(block * block_rows, block_rows, rhs));
    }
    u64 get(std::size_t row) const override { return reader->get_unchecked(row); }
    std::size_t bytes() const override { return storage.size(); }
};
template <sp::preset P> std::unique_ptr<packed_width> make_width(unsigned K, const std::vector<u64> &values) {
    std::unique_ptr<packed_width> out;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((K == I + 1 ? (out = std::make_unique<packed_width_at<I + 1, P>>(values), 0) : 0), ...);
    }(std::make_integer_sequence<unsigned, 64>{});
    return out;
}
template <sp::preset P> struct glue {
    enum class kind : u8 { constant, packed };
    std::vector<kind> kinds;
    std::vector<u64> base;
    std::vector<u8> width;
    std::vector<u32> slot;
    std::array<std::unique_ptr<packed_width>, 65> packed;
    std::vector<std::pair<u8, u64>> exceptions;
    std::vector<u32> ex_first;
    std::size_t bytes = 0;

    explicit glue(const std::vector<u64> &v) {
        frame_bounds::cost_model c;
        for (unsigned K = 1; K <= 64; ++K)
            c.body[K] = static_cast<u32>(width_body_bytes<P>(K));
        c.body[0] = c.body[1];
        c.max_exceptions = 64;
        const auto sample = frame_bounds::sample_cost(c, block_rows / 32);
        std::optional<frame_bounds::choice> prior;
        std::array<std::vector<u64>, 65> staged;
        for (std::size_t b = 0; b < v.size() / block_rows; ++b) {
            const u64 *x = v.data() + b * block_rows;
            const auto [lo_at, hi_at] = std::minmax_element(x, x + block_rows);
            ex_first.push_back(static_cast<u32>(exceptions.size()));
            if (*lo_at == *hi_at) {
                kinds.push_back(kind::constant), base.push_back(*lo_at), width.push_back(0), slot.push_back(0);
                bytes += 10;
                continue;
            }
            const auto ch = frame_bounds::fast::choose<32>(x, block_rows, c, sample, prior ? &*prior : nullptr);
            const u64 lo = ch.lo;
            const unsigned pk = std::max(1u, ch.width);
            u32 low = 0, high = 0;
            kinds.push_back(kind::packed), base.push_back(lo), width.push_back(static_cast<u8>(pk));
            slot.push_back(static_cast<u32>(staged[pk].size() / block_rows));
            for (unsigned i = 0; i < block_rows; ++i) {
                if (x[i] < lo || x[i] - lo > frame_bounds::mask(pk))
                    exceptions.push_back({static_cast<u8>(i), x[i]}), staged[pk].push_back(0), ++(x[i] < lo ? low : high);
                else
                    staged[pk].push_back(x[i] - lo);
            }
            prior = frame_bounds::choice{lo, pk, low, high, 0};
            bytes += 10 + (low + high) * 9;
        }
        ex_first.push_back(static_cast<u32>(exceptions.size()));
        for (unsigned K = 1; K <= 64; ++K)
            if (!staged[K].empty())
                packed[K] = make_width<P>(K, staged[K]), bytes += packed[K]->bytes();
    }
    u128 sum() const {
        u128 total = 0;
        for (std::size_t b = 0; b < kinds.size(); ++b) {
            if (kinds[b] == kind::constant) {
                total += static_cast<u128>(base[b]) * block_rows;
                continue;
            }
            total += packed[width[b]]->sum(slot[b]) + static_cast<u128>(base[b]) * block_rows;
            for (u32 e = ex_first[b]; e < ex_first[b + 1]; ++e)
                total += exceptions[e].second, total -= base[b];
        }
        return total;
    }
    u64 count_ge(u64 t) const {
        u64 c = 0;
        for (std::size_t b = 0; b < kinds.size(); ++b) {
            if (kinds[b] == kind::packed) {
                const bool all = t <= base[b];
                unsigned k = all ? block_rows : frame_bounds::bits(t - base[b]) > width[b] ? 0 : packed[width[b]]->count_ge(slot[b], t - base[b]);
                for (u32 e = ex_first[b]; e < ex_first[b + 1]; ++e)
                    k += unsigned(exceptions[e].second >= t) - unsigned(all);
                c += k;
                continue;
            }
            if (t <= base[b])
                c += block_rows;
        }
        return c;
    }
    u64 point(std::size_t row) const {
        const std::size_t b = row / block_rows;
        const unsigned r = static_cast<unsigned>(row % block_rows);
        if (kinds[b] == kind::constant)
            return base[b];
        const auto *e0 = exceptions.data() + ex_first[b], *e1 = exceptions.data() + ex_first[b + 1];
        if (e0 != e1) {
            const auto *e = std::lower_bound(e0, e1, std::pair<u8, u64>{static_cast<u8>(r), 0});
            if (e < e1 && e->first == r)
                return e->second;
        }
        return base[b] + packed[width[b]]->get(std::size_t{slot[b]} * block_rows + r);
    }
};

struct op_times {
    std::vector<double> sum, count_ge, count_eq, filter, points, select, read;
};
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS COLUMN...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
#if defined(__x86_64__)
    constexpr auto bulk = sp::preset::bulk_x86;
    const char *bulk_name = "bulk_x86";
#else
    constexpr auto bulk = sp::preset::bulk_arm;
    const char *bulk_name = "bulk_arm";
#endif
    for (int a = 4; a < argc; ++a) {
        const std::string name = argv[a];
        const auto v = load(dir, name, want);
        const std::size_t n = v.size(), blocks = n / block_rows;
        std::vector<u64> sorted = v;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        const u64 t = sorted[n / 2], eq = v[n / 3];
        std::mt19937_64 rng(29);
        std::vector<u32> rows(65536);
        for (auto &r : rows)
            r = static_cast<u32>(rng() % n);
        std::vector<u64> mask((n + 63) / 64);
        for (std::size_t i = 0; i < n; ++i)
            if (rng() % 10 == 0)
                mask[i / 64] |= u64{1} << (i % 64);
        // Oracles.
        u128 sum_oracle = 0;
        u64 ge_oracle = 0, eq_oracle = 0, point_oracle = 0, select_oracle = 0;
        std::vector<u64> filter_oracle((n + 63) / 64);
        for (std::size_t i = 0; i < n; ++i) {
            sum_oracle += v[i], ge_oracle += v[i] >= t, eq_oracle += v[i] == eq;
            filter_oracle[i / 64] |= u64{v[i] >= t} << (i % 64);
            if (mask[i / 64] >> (i % 64) & 1)
                select_oracle += v[i];
        }
        for (u32 r : rows)
            point_oracle += v[r];

        struct arm {
            std::string name;
            std::size_t bytes;
            std::function<u128()> sum;
            std::function<u64()> count_ge, count_eq, points;
            std::function<bool()> filter, select, read;  // check inside, after the timer
            op_times times;
        };
        std::vector<arm> arms;
        std::vector<std::shared_ptr<void>> keep;
        std::vector<u64> words(filter_oracle.size()), out(n);
        auto add_glue = [&]<sp::preset P>(const char *preset_name) {
            auto g = std::make_shared<glue<P>>(v);
            keep.push_back(g);
            const auto *q = g.get();
            arms.push_back({std::string("glue_") + preset_name, q->bytes, [q] { return q->sum(); }, [q, t] { return q->count_ge(t); }, nullptr,
                            [q, &rows] {
                                u64 s = 0;
                                for (u32 r : rows)
                                    s += q->point(r);
                                return s;
                            },
                            nullptr, nullptr, nullptr, {}});
        };
        std::vector<std::pair<std::string, const column *>> frames;
        auto add_frame = [&](const std::string &tname, std::unique_ptr<stage> root) {
            auto c = std::make_shared<column>(std::move(root), v);
            keep.push_back(c);
            frames.emplace_back("frame_" + tname, c.get());
            const auto *q = c.get();
            arms.push_back({"frame_" + tname, q->bytes(), [q] { return q->sum(); }, [q, t] { return q->count(comparison::greater_equal, t); },
                            [q, eq] { return q->count(comparison::equal, eq); },
                            [q, &rows] {
                                u64 s = 0;
                                for (u32 r : rows)
                                    s += q->get(r);
                                return s;
                            },
                            [q, t, &words] {
                                q->filter(comparison::greater_equal, t, words.data());
                                return true;
                            },
                            [q, &mask, &out] { return q->select(mask.data(), out.data()) > 0; },
                            [q, &out] {
                                q->read(out.data());
                                return true;
                            },
                            {}});
        };
        add_glue.template operator()<sp::preset::compact>("compact");
        add_glue.template operator()<bulk>(bulk_name);
        add_frame("bound_pack_compact", bound_pack<sp::preset::compact>());
        add_frame(std::string("bound_pack_") + bulk_name, bound_pack<bulk>());
        {
            // The same tree with the hand-fused point path (trees.h point_plan).
            auto c = std::make_shared<column>(bound_pack<sp::preset::compact>(), v);
            auto plan = std::make_shared<point_plan>(*point_plan::make(*c));
            keep.push_back(c), keep.push_back(plan);
            const auto *q = c.get();
            const auto *pl = plan.get();
            arms.push_back({"frame_bound_pack_compact_fused_points", q->bytes(), [q] { return q->sum(); },
                            [q, t] { return q->count(comparison::greater_equal, t); }, [q, eq] { return q->count(comparison::equal, eq); },
                            [pl, &rows] {
                                u64 s = 0;
                                for (u32 r : rows)
                                    s += pl->get(r);
                                return s;
                            },
                            nullptr, nullptr, nullptr, {}});
        }
        add_frame("bound_pack_compact_groups", std::make_unique<bound>(std::make_unique<pack<sp::preset::compact>>(group_blocks)));
        add_frame("intervals16_bound_pack_compact", delta_bound_pack<sp::preset::compact>(16));
        arms.push_back({"native_u64", n * 8,
                        [&v] {
                            u128 s = 0;
                            u64 lo = 0, hi = 0;
                            for (u64 x : v) {
                                lo += x;
                                hi += lo < x;
                            }
                            return (static_cast<u128>(hi) << 64) + lo;
                        },
                        [&v, t] {
                            u64 c = 0;
                            for (u64 x : v)
                                c += x >= t;
                            return c;
                        },
                        [&v, eq] {
                            u64 c = 0;
                            for (u64 x : v)
                                c += x == eq;
                            return c;
                        },
                        [&v, &rows] {
                            u64 s = 0;
                            for (u32 r : rows)
                                s += v[r];
                            return s;
                        },
                        [&v, t, &words] {
                            for (std::size_t w = 0; w < words.size(); ++w) {
                                u64 m = 0;
                                for (unsigned i = 0; i < 64; ++i)
                                    m |= u64{v[w * 64 + i] >= t} << i;
                                words[w] = m;
                            }
                            return true;
                        },
                        [&v, &mask, &out] {
                            std::size_t k = 0;
                            for (std::size_t w = 0; w < mask.size(); ++w)
                                for (u64 m = mask[w]; m; m &= m - 1)
                                    out[k++] = v[w * 64 + std::countr_zero(m)];
                            return k > 0;
                        },
                        [&v, &out] {
                            std::copy(v.begin(), v.end(), out.begin());
                            return true;
                        },
                        {}});

        auto check_filter = [&] { return words == filter_oracle; };
        auto check_select = [&] {
            u64 s = 0, k = 0;
            for (std::size_t w = 0; w < mask.size(); ++w)
                k += std::popcount(mask[w]);
            for (std::size_t i = 0; i < k; ++i)
                s += out[i];
            return s == select_oracle;
        };
        auto check_read = [&] { return std::equal(v.begin(), v.end(), out.begin()); };
        for (int r = 0; r < rounds; ++r)
            for (std::size_t j = 0; j < arms.size(); ++j) {
                auto &x = arms[(j + static_cast<std::size_t>(r)) % arms.size()];
                bool ok = true;
                u128 s = 0;
                u64 c = 0;
                x.times.sum.push_back(time_ns([&] { s = x.sum(); }));
                ok &= s == sum_oracle;
                x.times.count_ge.push_back(time_ns([&] { c = x.count_ge(); }));
                ok &= c == ge_oracle;
                if (x.count_eq) {
                    x.times.count_eq.push_back(time_ns([&] { c = x.count_eq(); }));
                    ok &= c == eq_oracle;
                }
                x.times.points.push_back(time_ns([&] { c = x.points(); }));
                ok &= c == point_oracle;
                if (x.filter) {
                    std::fill(words.begin(), words.end(), 0);
                    x.times.filter.push_back(time_ns([&] { x.filter(); }));
                    ok &= check_filter();
                    x.times.select.push_back(time_ns([&] { x.select(); }));
                    ok &= check_select();
                    x.times.read.push_back(time_ns([&] { x.read(); }));
                    ok &= check_read();
                }
                if (!ok) {
                    std::fprintf(stderr, "%s %s: wrong answer\n", name.c_str(), x.name.c_str());
                    return 1;
                }
            }
        // SUM over a selection: a random 10% mask, and the column's own
        // FILTER(x >= median) mask (materialised, then summed).
        {
            u128 masked_oracle = 0, where_oracle = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (mask[i / 64] >> (i % 64) & 1)
                    masked_oracle += v[i];
                if (v[i] >= t)
                    where_oracle += v[i];
            }
            std::vector<std::pair<std::string, std::function<u128(bool)>>> sums;
            for (const auto &[fname, c] : frames)
                sums.emplace_back(fname, [c, &mask, &words, t](bool where) {
                    if (!where)
                        return c->sum(mask.data());
                    c->filter(comparison::greater_equal, t, words.data());
                    return c->sum(words.data());
                });
            sums.emplace_back("native_u64", [&v, &mask, t](bool where) {
                u128 s = 0;
                for (std::size_t i = 0; i < v.size(); ++i)
                    s += where ? (v[i] >= t ? v[i] : 0) : (mask[i / 64] >> (i % 64) & 1 ? v[i] : 0);
                return s;
            });
            std::vector<std::vector<double>> tm(sums.size()), tw(sums.size());
            for (int r = 0; r < rounds; ++r)
                for (std::size_t j = 0; j < sums.size(); ++j) {
                    const std::size_t i = (j + static_cast<std::size_t>(r)) % sums.size();
                    u128 a = 0, b = 0;
                    tm[i].push_back(time_ns([&] { a = sums[i].second(false); }));
                    tw[i].push_back(time_ns([&] { b = sums[i].second(true); }));
                    if (a != masked_oracle || b != where_oracle) {
                        std::fprintf(stderr, "%s %s: wrong selected sum\n", name.c_str(), sums[i].first.c_str());
                        return 1;
                    }
                }
            for (std::size_t i = 0; i < sums.size(); ++i)
                std::printf("{\"column\":\"%s\",\"selected\":\"%s\",\"sum_masked\":%.2f,\"sum_where\":%.2f}\n", name.c_str(), sums[i].first.c_str(),
                            median(tm[i]) / static_cast<double>(blocks), median(tw[i]) / static_cast<double>(blocks));
        }
        for (const auto &x : arms) {
            auto per_block = [&](const std::vector<double> &ts) { return ts.empty() ? -1.0 : median(ts) / static_cast<double>(blocks); };
            std::printf("{\"column\":\"%s\",\"arm\":\"%s\",\"rows\":%zu,\"bits_per_value\":%.3f,\"sum\":%.2f,\"count_ge\":%.2f,\"count_eq\":%.2f,"
                        "\"filter\":%.2f,\"select\":%.2f,\"read\":%.2f,\"ns_per_point\":%.2f}\n",
                        name.c_str(), x.name.c_str(), n, 8.0 * static_cast<double>(x.bytes) / static_cast<double>(n), per_block(x.times.sum),
                        per_block(x.times.count_ge), per_block(x.times.count_eq), per_block(x.times.filter), per_block(x.times.select),
                        per_block(x.times.read), median(x.times.points) / static_cast<double>(rows.size()));
        }
    }
}

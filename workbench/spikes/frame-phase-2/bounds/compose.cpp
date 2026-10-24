// The bounds stage composed with SeriesPack: FOR's minimum and maximum, the
// bounds stage (today's lo = minimum with every width, or guess-verify-refine
// choosing lo), exceptions pulled out, and SeriesPack Local packing per width
// (runs/pipelines.cpp's no-RLE path, with lo in place of the minimum).
//
// Representation of a packed block: lo (u64), width K, the block's slot in
// its width's SeriesPack array, and its exceptions as (u8 position, signed
// value x - lo), costed at whole bytes with a sign bit once any lies below lo. Exception slots store field 0. Operations
// compensate for lo at the decode level (the SeriesPack thread owns fused
// compensation):
//   SUM      fields + lo * 256 + sum of (x - lo) over exceptions
//   COUNT>=t t <= lo: every non-exception row, else fields >= t - lo; then
//            each exception by its own value
//   point    field + lo, unless the row is an exception
// Every answer is checked against the raw column.
//
// usage: frame_bounds_compose COLS_DIR ROWS ROUNDS COLUMN...
#include "fast.h"

#include <ikea/seriespack.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <utility>

namespace sp = ikea::seriespack;
using namespace frame_bounds;
using u8 = std::uint8_t;
using i64 = std::int64_t;
using u128 = unsigned __int128;
using i128 = __int128;

namespace {
constexpr std::size_t block_rows = 256;

std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(f.tellg()) / 8) / block_rows * block_rows;
    std::vector<u64> v(rows);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}

template <sp::preset P> cost_model seriespack_cost(std::size_t n) {
    cost_model c;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((c.body[I + 1] = static_cast<u32>((n + sp::preset_layout<I + 1, P>::tile_rows - 1) / sp::preset_layout<I + 1, P>::tile_rows *
                                           sp::preset_layout<I + 1, P>::tile_bytes)),
         ...);
    }(std::make_integer_sequence<unsigned, 64>{});
    return c;
}

template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// One SeriesPack Local array of 256-row blocks at width K.
struct packed_width {
    virtual ~packed_width() = default;
    virtual u128 sum(std::size_t slot) const = 0;
    virtual unsigned count_ge(std::size_t slot, u64 rhs) const = 0;
    virtual u64 get(std::size_t row) const = 0;
    virtual std::size_t bytes() const = 0;
};
template <unsigned K> struct packed_width_at final : packed_width {
    using L = sp::preset_layout<K, sp::preset::compact>;
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
    u128 sum(std::size_t slot) const override {
        const std::size_t first = slot * block_rows;
        if constexpr (K <= 55) {  // 256 rows below 2^56 cannot wrap the modulo-2^64 range sum
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
    unsigned count_ge(std::size_t slot, u64 rhs) const override {
        return static_cast<unsigned>(reader->template count_unchecked<ikea::comparison::greater_equal>(slot * block_rows, block_rows, rhs));
    }
    u64 get(std::size_t row) const override { return reader->get_unchecked(row); }
    std::size_t bytes() const override { return storage.size(); }
};
std::unique_ptr<packed_width> make_width(unsigned K, const std::vector<u64> &values) {
    std::unique_ptr<packed_width> out;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((K == I + 1 ? (out = std::make_unique<packed_width_at<I + 1>>(values), 0) : 0), ...);
    }(std::make_integer_sequence<unsigned, 64>{});
    return out;
}

enum class method { today, guess };

struct column {
    // Per block: constant or packed.
    std::vector<u8> width;  // 0 for a constant block
    std::vector<u64> lo;    // the block's base: lo, or the constant
    std::vector<u32> slot, ex_first;
    std::vector<std::pair<u8, i128>> exceptions;  // (position, x - lo); 64-bit spans need the sign beyond i64
    std::array<std::unique_ptr<packed_width>, 65> packed;
    std::size_t bytes = 0, low_exceptions = 0, high_exceptions = 0;
    double encode_ns = 0;

    // c prices the stored bytes; the stage chooses under choose (c plus
    // Engine's per-exception penalty).
    column(const std::vector<u64> &v, method m, const cost_model &c, const cost_model &choose, const cost_model &sc) {
        const std::size_t blocks = v.size() / block_rows;
        std::array<std::vector<u64>, 65> staged;
        choice prior;
        bool have_prior = false;
        // Encode: bounds, exception extraction and staging, timed together;
        // SeriesPack packing per width is timed separately below.
        encode_ns = time_ns([&] {
            for (std::size_t b = 0; b < blocks; ++b) {
                const u64 *x = v.data() + b * block_rows;
                ex_first.push_back(static_cast<u32>(exceptions.size()));
                const auto [mn, mx] = minmax(x, block_rows);
                if (mn == mx) {
                    width.push_back(0), lo.push_back(mn), slot.push_back(0);
                    bytes += 10;  // base, width, kind
                    continue;
                }
                const choice ch = m == method::today ? fast::min_exhaustive(x, block_rows, choose)
                                                     : fast::guess<7, 32>(x, block_rows, choose, sc, have_prior ? &prior : nullptr);
                prior = ch, have_prior = true;
                const unsigned K = ch.width;
                const u64 top = ch.lo > ~u64{0} - mask(K) ? ~u64{0} : ch.lo + mask(K);  // saturates at the domain's top
                width.push_back(static_cast<u8>(K)), lo.push_back(ch.lo), slot.push_back(static_cast<u32>(staged[K].size() / block_rows));
                std::size_t low = 0, high = 0;
                for (unsigned i = 0; i < block_rows; ++i) {
                    const bool out = x[i] < ch.lo || x[i] > top;
                    if (out)
                        exceptions.push_back({static_cast<u8>(i), static_cast<i128>(x[i]) - static_cast<i128>(ch.lo)});
                    staged[K].push_back(out ? 0 : x[i] - ch.lo);
                    low += x[i] < ch.lo, high += out && x[i] >= ch.lo;
                }
                low_exceptions += low, high_exceptions += high;
                bytes += c(K, low, high, bits(mx - mn)) + 10;
            }
        });
        ex_first.push_back(static_cast<u32>(exceptions.size()));
        for (unsigned K = 1; K <= 64; ++K)
            if (!staged[K].empty())
                packed[K] = make_width(K, staged[K]);
    }

    u128 sum() const {
        i128 total = 0;
        for (std::size_t b = 0; b < width.size(); ++b) {
            if (!width[b]) {
                total += static_cast<i128>(lo[b]) * block_rows;
                continue;
            }
            total += static_cast<i128>(packed[width[b]]->sum(slot[b])) + static_cast<i128>(lo[b]) * block_rows;
            for (u32 e = ex_first[b]; e < ex_first[b + 1]; ++e)
                total += exceptions[e].second;
        }
        return static_cast<u128>(total);
    }
    u64 count_ge(u64 t) const {
        u64 n = 0;
        for (std::size_t b = 0; b < width.size(); ++b) {
            const u64 base = lo[b];
            if (!width[b]) {
                n += base >= t ? block_rows : 0;
                continue;
            }
            const u32 e0 = ex_first[b], e1 = ex_first[b + 1];
            if (t <= base)
                n += block_rows - (e1 - e0);
            else if (bits(t - base) <= width[b])
                n += packed[width[b]]->count_ge(slot[b], t - base);  // exception slots hold 0 < t - base
            for (u32 e = e0; e < e1; ++e)
                n += static_cast<u64>(static_cast<i128>(base) + exceptions[e].second) >= t;
        }
        return n;
    }
    u64 get(std::size_t row) const {
        const std::size_t b = row / block_rows;
        if (!width[b])
            return lo[b];
        const auto *e0 = exceptions.data() + ex_first[b], *e1 = exceptions.data() + ex_first[b + 1];
        const u8 r = static_cast<u8>(row % block_rows);
        const auto *e = std::lower_bound(e0, e1, std::pair<u8, i128>{r, -(static_cast<i128>(1) << 100)});
        if (e < e1 && e->first == r)
            return static_cast<u64>(static_cast<i128>(lo[b]) + e->second);
        return packed[width[b]]->get(slot[b] * block_rows + r) + lo[b];
    }
};
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5)
        return 2;
    const std::string dir = argv[1];
    const std::size_t rows = std::strtoull(argv[2], nullptr, 10);
    const unsigned rounds = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));
    const cost_model c = seriespack_cost<sp::preset::compact>(block_rows), sc = sample_cost(c, 8);
    for (int a = 4; a < argc; ++a) {
        const std::string name = argv[a];
        const auto v = load(dir + "/" + name + ".u64", rows);
        u128 want_sum = 0;
        for (u64 x : v)
            want_sum += x;
        std::vector<u64> sorted = v;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const u64 t = sorted[sorted.size() / 2];
        u64 want_count = 0;
        for (u64 x : v)
            want_count += x >= t;
        std::mt19937_64 g(7);
        std::vector<u32> points(65536);
        for (auto &p : points)
            p = static_cast<u32>(g() % v.size());
        for (auto [m, penalty] : {std::pair{method::today, 0u}, {method::guess, 0u}, {method::guess, 2u}, {method::guess, 8u}}) {
            cost_model choose = c, scp = sc;
            choose.exception_penalty = scp.exception_penalty = penalty;
            std::vector<double> enc, sum_ns, count_ns, point_ns;
            std::optional<column> col;
            for (unsigned r = 0; r < rounds; ++r) {
                col.emplace(v, m, c, choose, scp);
                enc.push_back(col->encode_ns / (v.size() / block_rows));
            }
            u128 s = 0;
            u64 k = 0, p = 0;
            for (unsigned r = 0; r < rounds; ++r) {
                sum_ns.push_back(time_ns([&] { s = col->sum(); }) / (v.size() / block_rows));
                count_ns.push_back(time_ns([&] { k = col->count_ge(t); }) / (v.size() / block_rows));
                point_ns.push_back(time_ns([&] {
                                       u64 acc = 0;
                                       for (u32 q : points)
                                           acc += col->get(q);
                                       p = acc;
                                   }) /
                                   points.size());
            }
            u64 want_p = 0;
            for (u32 q : points)
                want_p += v[q];
            if (s != want_sum || k != want_count || p != want_p) {
                std::fprintf(stderr, "%s %d: wrong answer sum %d count %d (%llu vs %llu) point %d\n", name.c_str(), int(m), s != want_sum, k != want_count,
                             (unsigned long long)k, (unsigned long long)want_count, p != want_p);
                std::abort();
            }
            for (u32 q = 0; q < v.size(); q += 97)
                if (col->get(q) != v[q])
                    std::abort();
            std::size_t body = 0;
            for (const auto &w : col->packed)
                body += w ? w->bytes() : 0;
            std::printf("{\"column\":\"%s\",\"method\":\"%s\",\"exception_penalty\":%u,\"bits_per_value\":%.4f,\"body_bytes\":%zu,\"bytes\":%zu,\"low_exceptions\":%zu,"
                        "\"high_exceptions\":%zu,\"encode_ns_per_block\":%.1f,\"sum_ns_per_block\":%.1f,\"count_ns_per_block\":%.1f,\"point_ns\":%.1f}\n",
                        name.c_str(), m == method::today ? "today" : "guess", penalty, 8.0 * col->bytes / v.size(), body, col->bytes, col->low_exceptions,
                        col->high_exceptions, median(enc), median(sum_ns), median(count_ns), median(point_ns));
            std::fflush(stdout);
        }
    }
}

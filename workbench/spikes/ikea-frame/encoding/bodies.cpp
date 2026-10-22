// Real encoded bodies for one column: 256-row blocks of constant, runs or
// frame-of-reference kind, built with actual SeriesPack planes and Boundlist
// run geometry. Reports every byte (descriptors, payload, run geometry) and
// times exact SUM and COUNT(u >= t) under coarse block masks against plain
// native-array controls. Values arrive as u64 working-domain values (offset
// binary for signed columns); results are exact in that domain.
//
// Research prototype: the block selector is an oracle (smallest bytes per
// block), there are no exceptions or overrides yet, and nothing here is a
// Frame interface.

#include <ikea/boundlist.h>
#include <ikea/effects.h>
#include <ikea/seriespack.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
namespace sp = ikea::seriespack;
namespace bl = ikea::boundlist;
using u64 = std::uint64_t;
using u128 = unsigned __int128;

constexpr std::size_t block_rows = 256;

enum class kind : std::uint8_t { constant, runs, packed };

// The per-block descriptor as stored; its size is charged to every block.
struct descriptor {
    u64 base;              // constant value, or frame-of-reference base
    std::uint32_t offset;  // payload offset in the column body
    std::uint16_t count;   // packed values (256, or the number of runs)
    kind type;
    std::uint8_t width;    // SeriesPack width of packed values (0 = none)
};
static_assert(sizeof(descriptor) == 16);

unsigned width_of(u64 span) {
    return span ? 64 - std::countl_zero(span) : 0;
}

std::size_t plane_bytes(unsigned k, std::size_t count) {
    return (count + 7) / 8 * k;  // Local geometry: 8-row tiles of k bytes
}

// Encode values (already relative to the base) as a Local SeriesPack plane of
// runtime width k, through the real typed mutation path.
void pack(unsigned k, std::span<const u64> values, std::uint8_t *out) {
    bool done = false;
    sp::detail::each<64>([&](auto index) {
        constexpr unsigned K = decltype(index)::value + 1;
        if (k != K)
            return;
        using F = sp::format<K>;
        const std::size_t bytes = plane_bytes(K, values.size());
        auto v = sp::view<F, std::uint8_t>::attach(
            values.size(), {{{std::span(out, bytes), F::tile_bytes}, {}, {}}});
        if (!v)
            std::abort();
        auto prepared = sp::bind_mutation(*v);
        if (!prepared)
            std::abort();
        auto write = prepared->template erase<u64, sp::no_summary>();
        auto capacity = prepared->construction_effect_capacity();
        if (!capacity)
            std::abort();
        std::vector<sp::composition::owner_write> records(*capacity);
        sp::composition::write_journal effects{records};
        if (!write.initialize(values, effects))
            std::abort();
        done = true;
    });
    if (!done)
        std::abort();
}

const bl::layout &run_layout() {
    static const auto layout = bl::layout::prepare(
        // A runs block never holds one 256-row run (that block is constant), so
        // every run is at most 255 rows and sizes fit u8.
        {.family = bl::geometry::sizes, .maximum = 255, .extent_limit = 256, .immortal = true});
    if (!layout)
        std::abort();
    return *layout;
}

struct runs_geometry {
    std::vector<bl::byte> directory, lengths, control;
    std::optional<bl::view> view;
};

struct column {
    std::vector<descriptor> blocks;
    std::vector<std::uint8_t> payload;
    std::vector<std::unique_ptr<runs_geometry>> runs;  // per block, null unless runs
    std::vector<sp::decoder<u64>> decoders;            // per block with a plane
    // Narrowest output lane per block (u8/u16/u32), for narrow-lane reduction.
    std::vector<std::optional<sp::decoder<std::uint8_t>>> d8;
    std::vector<std::optional<sp::decoder<std::uint16_t>>> d16;
    std::vector<std::optional<sp::decoder<std::uint32_t>>> d32;
    std::size_t rows = 0;
    std::size_t descriptor_bytes() const { return blocks.size() * sizeof(descriptor); }
    std::size_t run_geometry_bytes() const {
        std::size_t total = 0;
        for (const auto &r : runs)
            if (r)
                total += r->directory.size() + r->lengths.size() + r->control.size();
        return total;
    }
};

column encode(std::span<const u64> values) {
    column c;
    c.rows = values.size() / block_rows * block_rows;
    const auto &layout = run_layout();
    std::vector<u64> scratch(block_rows);
    std::vector<std::pair<std::size_t, std::size_t>> planes;  // (offset, block)
    for (std::size_t b = 0; b * block_rows < c.rows; ++b) {
        const auto block = values.subspan(b * block_rows, block_rows);
        const auto [lo, hi] = std::minmax_element(block.begin(), block.end());
        descriptor d{*lo, static_cast<std::uint32_t>(c.payload.size()), 0, kind::constant, 0};
        c.runs.emplace_back();
        if (*lo == *hi) {
            c.blocks.push_back(d);
            continue;
        }
        const unsigned k = width_of(*hi - *lo);
        std::vector<u64> run_values;
        std::vector<unsigned> run_sizes;
        for (std::size_t i = 0; i < block_rows;) {
            std::size_t j = i + 1;
            while (j < block_rows && block[j] == block[i])
                ++j;
            run_values.push_back(block[i] - *lo);
            run_sizes.push_back(static_cast<unsigned>(j - i));
            i = j;
        }
        const std::size_t packed_cost = plane_bytes(k, block_rows);
        const std::size_t runs_cost = plane_bytes(k, run_values.size()) +
                                      layout.directory_bytes() + layout.control_bytes() +
                                      layout.lengths_bytes(static_cast<unsigned>(run_sizes.size()));
        if (runs_cost < packed_cost) {
            auto g = std::make_unique<runs_geometry>();
            g->directory.resize(layout.directory_bytes());
            g->lengths.resize(layout.lengths_bytes(static_cast<unsigned>(run_sizes.size())));
            g->control.resize(layout.control_bytes());
            auto view = bl::view::bind(layout, {g->directory, g->lengths, g->control});
            if (!view)
                std::abort();
            g->view.emplace(*view);
            std::vector<ikea::owner_write> records(8 * (run_sizes.size() + 2));
            ikea::source_write_journal effects{records};
            if (!bl::initialize(*g->view, effects))
                std::abort();
            for (unsigned size : run_sizes) {
                effects.used = 0;
                if (!bl::append(*g->view, size, effects))
                    std::abort();
            }
            d.type = kind::runs;
            d.count = static_cast<std::uint16_t>(run_values.size());
            d.width = static_cast<std::uint8_t>(k);
            c.payload.resize(c.payload.size() + plane_bytes(k, run_values.size()));
            pack(k, run_values, c.payload.data() + d.offset);
            c.runs.back() = std::move(g);
        } else {
            for (std::size_t i = 0; i < block_rows; ++i)
                scratch[i] = block[i] - *lo;
            d.type = kind::packed;
            d.count = block_rows;
            d.width = static_cast<std::uint8_t>(k);
            c.payload.resize(c.payload.size() + packed_cost);
            pack(k, scratch, c.payload.data() + d.offset);
        }
        c.blocks.push_back(d);
    }
    // Bind decoders once, after the payload stops growing.
    c.decoders.reserve(c.blocks.size());
    for (const auto &d : c.blocks) {
        c.d8.emplace_back();
        c.d16.emplace_back();
        c.d32.emplace_back();
        if (d.type == kind::constant) {
            c.decoders.emplace_back(nullptr, 0, nullptr, nullptr, nullptr);
            continue;
        }
        const sp::dense_input input{
            d.width, sp::geometry::local,
            std::span<const std::uint8_t>(c.payload).subspan(d.offset, plane_bytes(d.width, d.count)),
            d.count};
        auto decoder = sp::bind_decoder<u64>(input);
        if (!decoder)
            std::abort();
        c.decoders.push_back(*decoder);
        if (d.width <= 8)
            c.d8.back() = *sp::bind_decoder<std::uint8_t>(input);
        else if (d.width <= 16)
            c.d16.back() = *sp::bind_decoder<std::uint16_t>(input);
        else if (d.width <= 32)
            c.d32.back() = *sp::bind_decoder<std::uint32_t>(input);
    }
    return c;
}

// Exact SUM of one block, decoding packed values into u64.
u128 block_sum(const column &c, std::size_t b) {
    const auto &d = c.blocks[b];
    alignas(64) u64 buffer[block_rows];
    u128 total = 0;
    switch (d.type) {
    case kind::constant:
        return static_cast<u128>(d.base) * block_rows;
    case kind::packed: {
        c.decoders[b].read_unchecked(0, block_rows, buffer);
        if (d.width <= 56) {  // 256 residuals below 2^56 cannot overflow u64
            u64 residuals = 0;
            for (std::size_t i = 0; i < block_rows; ++i)
                residuals += buffer[i];
            total = residuals;
        } else {
            for (std::size_t i = 0; i < block_rows; ++i)
                total += buffer[i];
        }
        return total + static_cast<u128>(d.base) * block_rows;
    }
    case kind::runs:
        c.decoders[b].read_unchecked(0, d.count, buffer);
        if (!bl::range(*c.runs[b]->view, 0, d.count, [&](unsigned rank, auto interval) {
                total += static_cast<u128>(buffer[rank] + d.base) *
                         (interval.limit - interval.first);
            }))
            std::abort();
        return total;
    }
    std::abort();
}

// Exact SUM over the blocks selected by mask (one bit per block).
u128 encoded_sum(const column &c, const std::vector<std::uint8_t> &mask) {
    u128 total = 0;
    for (std::size_t b = 0; b < c.blocks.size(); ++b)
        if (mask[b])
            total += block_sum(c, b);
    return total;
}

// The same SUM, decoding packed blocks into their narrowest lane and reducing
// there before one widening add per block (Calico's split-arithmetic idea).
template <class T, class A>
A narrow_block_sum(const sp::decoder<T> &decoder) {
    alignas(64) T buffer[block_rows];
    decoder.read_unchecked(0, block_rows, buffer);
    A s = 0;
    for (std::size_t i = 0; i < block_rows; ++i)
        s += buffer[i];
    return s;
}

u128 encoded_sum_narrow(const column &c, const std::vector<std::uint8_t> &mask) {
    u128 total = 0;
    for (std::size_t b = 0; b < c.blocks.size(); ++b) {
        if (!mask[b])
            continue;
        const auto &d = c.blocks[b];
        if (d.type != kind::packed) {
            total += block_sum(c, b);
            continue;
        }
        total += static_cast<u128>(d.base) * block_rows;
        if (c.d8[b])
            total += narrow_block_sum<std::uint8_t, std::uint32_t>(*c.d8[b]);
        else if (c.d16[b])
            total += narrow_block_sum<std::uint16_t, std::uint32_t>(*c.d16[b]);
        else if (c.d32[b])
            total += narrow_block_sum<std::uint32_t, u64>(*c.d32[b]);
        else
            total += block_sum(c, b) - static_cast<u128>(d.base) * block_rows;
    }
    return total;
}

// Exact COUNT(u >= threshold) over the selected blocks.
u64 encoded_count_ge(const column &c, const std::vector<std::uint8_t> &mask, u64 threshold) {
    u64 total = 0;
    alignas(64) u64 buffer[block_rows];
    for (std::size_t b = 0; b < c.blocks.size(); ++b) {
        if (!mask[b])
            continue;
        const auto &d = c.blocks[b];
        if (d.base >= threshold) {  // the whole block is at or above the base
            total += block_rows;
            continue;
        }
        switch (d.type) {
        case kind::constant:
            break;
        case kind::packed: {
            const u64 rhs = threshold - d.base;
            if (d.width < 64 && rhs >> d.width)
                break;  // the threshold exceeds every representable residual
            c.decoders[b].read_unchecked(0, block_rows, buffer);
            for (std::size_t i = 0; i < block_rows; ++i)
                total += buffer[i] >= rhs;
            break;
        }
        case kind::runs: {
            const u64 rhs = threshold - d.base;
            c.decoders[b].read_unchecked(0, d.count, buffer);
            if (!bl::range(*c.runs[b]->view, 0, d.count, [&](unsigned rank, auto interval) {
                    if (buffer[rank] >= rhs)
                        total += interval.limit - interval.first;
                }))
                std::abort();
            break;
        }
        }
    }
    return total;
}

// Plain control: values relative to the column minimum at the narrowest byte
// width that holds them (the natural typed array a scan engine would use).
template <class T> struct plain {
    std::vector<T> values;
    u64 base;
};

template <class T> u128 plain_sum(const plain<T> &p, const std::vector<std::uint8_t> &mask) {
    u128 total = 0;
    for (std::size_t b = 0; b < mask.size(); ++b) {
        if (!mask[b])
            continue;
        const T *v = p.values.data() + b * block_rows;
        u64 s = 0;
        if constexpr (sizeof(T) < 8) {
            for (std::size_t i = 0; i < block_rows; ++i)
                s += v[i];
            total += s;
        } else {
            for (std::size_t i = 0; i < block_rows; ++i)
                total += v[i];
        }
        total += static_cast<u128>(p.base) * block_rows;
    }
    return total;
}

template <class T>
u64 plain_count_ge(const plain<T> &p, const std::vector<std::uint8_t> &mask, u64 threshold) {
    if (threshold <= p.base)
        return std::count(mask.begin(), mask.end(), 1) * block_rows;
    const u64 rhs = threshold - p.base;
    u64 total = 0;
    for (std::size_t b = 0; b < mask.size(); ++b) {
        if (!mask[b])
            continue;
        const T *v = p.values.data() + b * block_rows;
        for (std::size_t i = 0; i < block_rows; ++i)
            total += static_cast<u64>(v[i]) >= rhs;
    }
    return total;
}

template <class F> double median_ns(F &&fn, int repeats = 21) {
    std::vector<double> samples;
    fn();
    for (int r = 0; r < repeats; ++r) {
        const auto start = std::chrono::steady_clock::now();
        fn();
        samples.push_back(std::chrono::duration<double, std::nano>(
                              std::chrono::steady_clock::now() - start)
                              .count());
    }
    std::nth_element(samples.begin(), samples.begin() + repeats / 2, samples.end());
    return samples[repeats / 2];
}

std::string u128_text(u128 v) {
    if (!v)
        return "0";
    std::string s;
    while (v) {
        s.insert(s.begin(), static_cast<char>('0' + static_cast<int>(v % 10)));
        v /= 10;
    }
    return s;
}

volatile u64 sink;

template <class T>
void run(const char *name, std::span<const u64> values, u64 lo, u64 threshold) {
    const column c = encode(values);
    plain<T> p{std::vector<T>(c.rows), lo};
    for (std::size_t i = 0; i < c.rows; ++i)
        p.values[i] = static_cast<T>(values[i] - lo);
    std::size_t counts[3] = {};
    for (const auto &d : c.blocks)
        ++counts[static_cast<int>(d.type)];
    const std::size_t total_bytes = c.descriptor_bytes() + c.payload.size() + c.run_geometry_bytes();
    std::printf("{\"column\":\"%s\",\"rows\":%zu,\"blocks\":%zu,\"constant\":%zu,\"runs\":%zu,"
                "\"packed\":%zu,\"descriptor_bytes\":%zu,\"payload_bytes\":%zu,"
                "\"run_geometry_bytes\":%zu,\"bits_per_value\":%.3f,\"plain_bytes_per_value\":%zu",
                name, c.rows, c.blocks.size(), counts[0], counts[1], counts[2],
                c.descriptor_bytes(), c.payload.size(), c.run_geometry_bytes(),
                8.0 * total_bytes / c.rows, sizeof(T));
    const std::size_t n = c.blocks.size();
    struct shape { const char *name; std::vector<std::uint8_t> mask; };
    std::vector<shape> shapes{{"all", std::vector<std::uint8_t>(n, 1)},
                              {"clustered50", std::vector<std::uint8_t>(n, 0)},
                              {"sparse10", std::vector<std::uint8_t>(n, 0)}};
    for (std::size_t b = 0; b < n / 2; ++b)
        shapes[1].mask[b] = 1;
    for (std::size_t b = 0; b < n; b += 10)
        shapes[2].mask[b] = 1;
    for (const auto &s : shapes) {
        const u128 es = encoded_sum(c, s.mask), ps = plain_sum(p, s.mask);
        if (encoded_sum_narrow(c, s.mask) != ps) {
            std::fprintf(stderr, "%s %s: narrow sum mismatch\n", name, s.name);
            std::exit(1);
        }
        const u64 ec = encoded_count_ge(c, s.mask, threshold), pc = plain_count_ge(p, s.mask, threshold);
        if (es != ps || ec != pc) {
            std::fprintf(stderr, "%s %s: encoded/plain mismatch\n", name, s.name);
            std::exit(1);
        }
        const double t_es = median_ns([&] { sink = static_cast<u64>(encoded_sum(c, s.mask)); });
        const double t_ps = median_ns([&] { sink = static_cast<u64>(plain_sum(p, s.mask)); });
        const double t_en = median_ns([&] { sink = static_cast<u64>(encoded_sum_narrow(c, s.mask)); });
        const double t_ec = median_ns([&] { sink = encoded_count_ge(c, s.mask, threshold); });
        const double t_pc = median_ns([&] { sink = plain_count_ge(p, s.mask, threshold); });
        std::printf(",\"%s\":{\"sum\":\"%s\",\"count_ge\":%llu,"
                    "\"encoded_sum_ns_per_row\":%.4f,\"encoded_narrow_sum_ns_per_row\":%.4f,"
                    "\"plain_sum_ns_per_row\":%.4f,"
                    "\"encoded_count_ns_per_row\":%.4f,\"plain_count_ns_per_row\":%.4f}",
                    s.name, u128_text(es).c_str(), static_cast<unsigned long long>(ec),
                    t_es / c.rows, t_en / c.rows, t_ps / c.rows, t_ec / c.rows, t_pc / c.rows);
    }
    std::printf("}\n");
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s NAME FILE.u64\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
    const auto bytes = static_cast<std::size_t>(in.tellg());
    std::vector<u64> values(bytes / 8);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(values.data()), static_cast<std::streamsize>(bytes));
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    std::vector<u64> sorted(values);
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const u64 threshold = sorted[sorted.size() / 2];
    const u64 span = *hi - *lo;
    if (span <= 0xff)
        run<std::uint8_t>(argv[1], values, *lo, threshold);
    else if (span <= 0xffff)
        run<std::uint16_t>(argv[1], values, *lo, threshold);
    else if (span <= 0xffffffffu)
        run<std::uint32_t>(argv[1], values, *lo, threshold);
    else
        run<u64>(argv[1], values, *lo, threshold);
}

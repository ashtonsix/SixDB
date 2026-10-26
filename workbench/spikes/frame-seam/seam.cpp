// Supplementary cells for the stage-seam spike, over the public checked API
// only, so one source builds against every step: bind (validate + bind), a
// clustered fine selection (half the unit fully selected, the rest period 3),
// alternating coarse selections, uniform FILTER, and a pack-rooted column.
// Same inputs and recipes as landing/measure.cpp. argv: COLS ARM [ROUNDS].
#include <ikea/frame.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <optional>
#include <random>
#include <string>

namespace fr = ikea::frame;
using u64 = std::uint64_t;
using u128 = fr::u128;
namespace {
volatile u64 sink;
void check(bool ok) {
    if (!ok)
        std::abort();
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(bool(value));
    return std::move(*value);
}
struct unit {
    fr::built_unit image;
    std::vector<std::size_t> extents;
    std::vector<fr::stream_manifest> streams;
    std::vector<fr::borrowed_part> parts;
    std::vector<fr::section_key> keys;
    std::size_t rows;
    std::optional<fr::reader> reader;
    unit(const fr::description &d, std::span<const u64> values)
        : image(take(fr::build(d, values, {0, 64, 0}))), rows(values.size()) {
        for (const auto &s : image.streams) {
            fr::stream_manifest m{s.root_node, s.rows, {}};
            for (std::size_t g = 0; g < s.groups.size(); ++g) {
                const auto id = static_cast<std::uint32_t>(parts.size());
                m.groups.push_back({id, 0, s.groups[g].size()});
                extents.push_back(s.groups[g].size()), parts.push_back({id, s.groups[g]}), keys.push_back({s.root_node, g});
            }
            streams.push_back(std::move(m));
        }
        reader.emplace(bind(d));
    }
    fr::reader bind(const fr::description &d) const { return take(fr::bind_reader(d, {rows, extents, streams, parts, keys})); }
};
// form: 0 bound->pack, 1 sizes16, 2 relative16, 3 pack alone.
fr::description recipe(unsigned form, fr::pack_preset p) {
    fr::description d;
    if (form == 3) {
        d.nodes = {{fr::node_kind::pack, std::uint16_t(p), 0, {}}};
        d.root = 0;
        return d;
    }
    d.nodes = {{fr::node_kind::pack, std::uint16_t(p), 0, {}}, {fr::node_kind::bound, 0, 0, {{0, fr::coordinate::identity}}}};
    d.root = 1;
    if (form) {
        d.nodes.push_back({fr::node_kind::pack, std::uint16_t(p), 0, {}});
        d.nodes.push_back({fr::node_kind::bound, 0, 0, {{2, fr::coordinate::identity}}});
        d.nodes.push_back({form == 1 ? fr::node_kind::intervals_sizes : fr::node_kind::intervals_relative_ends, 16, 0,
                           {{1, fr::coordinate::identity}, {3, fr::coordinate::checkpoints}}});
        d.root = 4;
    }
    return d;
}
std::vector<u64> load(const std::string &dir, const std::string &name, std::size_t n) {
    std::vector<u64> values(n);
    if (name == "gen_ts") {
        std::mt19937_64 rng(5);
        u64 v = u64{1} << 40;
        for (auto &x : values)
            x = (v += rng() % 1000);
    } else {
        std::ifstream in(dir + "/" + name + ".u64", std::ios::binary | std::ios::ate);
        check(bool(in));
        values.resize(std::min(n, std::size_t(in.tellg()) / 8));
        in.seekg(0);
        in.read(reinterpret_cast<char *>(values.data()), std::streamsize(values.size() * 8));
        check(bool(in));
    }
    return values;
}
void run(const std::string &dir, const std::string &label, const std::string &name, unsigned form, const char *arm,
         unsigned rounds) {
    const auto values = load(dir, name, fr::unit_rows);
#if defined(__aarch64__)
    constexpr auto p = fr::pack_preset::bulk_arm;
#else
    constexpr auto p = fr::pack_preset::bulk_x86;
#endif
    const auto d = recipe(form, p);
    unit image(d, values);
    const fr::reader &r = *image.reader;
    const std::size_t n = values.size();
    const unsigned nb = unsigned((n + 255) / 256);
    std::vector<u64> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const u64 threshold = sorted[n / 2];
    // Clustered: rows [0, n/2) all selected, then every third row.
    std::array<ikea::bitset256, fr::unit_blocks> fine{};
    ikea::bitset256 alt; // blocks in alternating runs of 8
    for (unsigned b = 0; b < nb; ++b)
        alt.set(b, b / 8 % 2 == 0);
    u128 want_clustered = 0, want_alt_sum = 0;
    u64 want_alt_count = 0, lo = ~u64{0}, hi = 0, selected = 0;
    std::array<ikea::bitset256, fr::unit_blocks> want_filter{};
    for (std::size_t i = 0; i < n; ++i) {
        if (i < n / 2 || i % 3 == 1) {
            fine[i / 256].set(unsigned(i % 256));
            want_clustered += values[i], ++selected, lo = std::min(lo, values[i]), hi = std::max(hi, values[i]);
        }
        if (alt.contains(unsigned(i / 256)))
            want_alt_sum += values[i], want_alt_count += values[i] >= threshold;
        if (values[i] >= threshold)
            want_filter[i / 256].set(unsigned(i % 256));
    }
    const auto rows = ikea::seriespack::selection::bits(0, std::span<const ikea::bitset256>(fine));
    const auto coarse = ikea::intervals::selection::bits(0, alt);
    std::array<ikea::bitset256, fr::unit_blocks> out{};
    enum op { bind, sum_clustered, summarise_clustered, sum_alt8, count_alt8, filter_ge, ops };
    const std::array<const char *, ops> names{"bind", "sum_selected_clustered", "summarise_clustered", "sum_alt8",
                                              "count_ge_alt8", "filter_ge"};
    // The timed body only executes and consumes; results are checked against
    // the oracle after each sample, outside the timer.
    u128 last = 0;
    fr::summary summary{};
    const auto execute = [&](unsigned o) {
        switch (o) {
        case bind: last = image.bind(d).size(); break;
        case sum_clustered: last = take(r.sum_selected(rows)); break;
        case summarise_clustered: summary = take(r.summarise_selected(rows)), last = summary.sum; break;
        case sum_alt8: last = take(r.sum(coarse)); break;
        case count_alt8: last = take(r.count(ikea::comparison::greater_equal, threshold, coarse)); break;
        case filter_ge:
            check(bool(r.filter(ikea::comparison::greater_equal, threshold, out)));
            last = out[nb / 2].words[0];
            break;
        }
        sink = u64(last) ^ u64(last >> 64);
    };
    const auto valid = [&](unsigned o) {
        switch (o) {
        case bind: return last == n;
        case sum_clustered: return last == want_clustered;
        case summarise_clustered:
            return summary.sum == want_clustered && summary.count == selected && summary.min == lo && summary.max == hi;
        case sum_alt8: return last == want_alt_sum;
        case count_alt8: return last == want_alt_count;
        case filter_ge: return std::equal(out.begin(), out.begin() + nb, want_filter.begin());
        }
        return false;
    };
    for (unsigned o = 0; o < ops; ++o)
        execute(o), check(valid(o));
    for (unsigned round = 0; round < rounds; ++round)
        for (unsigned o = 0; o < ops; ++o) {
            const unsigned repeats = o == bind ? 4 : 32;
            out = {};
            const auto begin = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < repeats; ++i)
                execute(o);
            const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / repeats;
            check(valid(o));
            std::printf("{\"column\":\"%s\",\"op\":\"%s\",\"arm\":\"%s\",\"conditioning\":\"repeated\",\"round\":%u,\"ns\":%.3f}\n",
                        label.c_str(), names[o], arm, round, ns);
        }
}
} // namespace
int main(int argc, char **argv) {
    check(argc >= 3);
    const unsigned rounds = argc > 3 ? unsigned(std::stoul(argv[3])) : 5;
    const struct {
        const char *label, *name;
        unsigned form;
    } cases[]{{"OS", "OS", 0}, {"UserID", "UserID", 0}, {"URLHash", "URLHash", 0},
              {"EventTime", "EventTime", 2}, {"gen_ts", "gen_ts", 1}, {"OS_pack", "OS", 3}};
    for (const auto &c : cases)
        run(argv[1], c.label, c.name, c.form, argv[2], rounds);
}

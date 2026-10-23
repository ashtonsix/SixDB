#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
// Run-value programs (runs.h) on one column:
//
//   run_programs NAME FILE.u64 [OPERATION]
//
// Images: exact run widths (the default encoding), byte_lanes run widths (K
// rounded up to 8, 16 or 32) and a control that also rounds packed planes;
// each with a 1% uniform NULL nullable twin (encode_nullable, predictor
// fill). Bytes and encode time are reported per image. Arms per operation,
// timed in bodies.cpp's fixed-seed shuffled order (21 trials):
//   a         exact image, wide program (the current leaf)
//   a_dup     the same again (the noise control)
//   b         exact image, narrow program (catalogue narrow lane, u8 ends)
//   b_run     masks only: b with the per-run mask fill instead of the
//             boundary toggle (runs.h, run_fill)
//   c         byte_lanes image, narrow program
//   d         byte_lanes image, in_place program
//   d_run     masks only: d with the per-run mask fill
//   ctl       packed-and-runs byte_lanes image, in_place program
//   plain     SUM and COUNT(>=) only: the column minus its minimum at the
//             narrowest byte width
// Operations over every block: SUM; COUNT(x >= median), COUNT(x >= max + 1)
// (above every value: only width-derived bounds or a pass answer it); COUNT(x
// == mode), COUNT(x == rare) (a present value of least frequency); predicate
// masks x >= median, x == rare, x >= max + 1 and x in [p99, max]; fused SUM +
// COUNT(>= median). Under 1% NULL: masked SUM, COUNT(>= median), COUNT(==
// rare) and fused SUM + COUNT. Every arm's answer must equal arm a's before
// timing.
#define FRAME_ENCODING_BODY_NO_MAIN
#include "bodies.cpp"

namespace {
using namespace frame_encoding;

u64 mix(u64 x) {
    x += 0x9e3779b97f4a7c15u;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9u;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebu;
    return x ^ (x >> 31);
}
std::vector<u64> uniform_validity(std::size_t rows, unsigned per_mille10) {
    std::vector<u64> v(rows / 64, ~u64{0});
    for (std::size_t i = 0; i < rows; ++i)
        if (mix(i * 7919 + per_mille10 * 2) % 10000000 < per_mille10 * 1000)
            v[i / 64] &= ~(u64{1} << (i % 64));
    return v;
}

struct image {
    const char *name;
    encode_options options;
    column c;
    nullable_column n;
    double encode_ns;
};

// Bytes of a column's runs blocks: value planes and u8 ends.
std::pair<std::size_t, std::size_t> runs_bytes(const column &c) {
    std::size_t values = 0, ends = 0;
    for (const auto &d : c.blocks)
        if (d.type == kind::runs)
            values += plane_bytes(plane_width(d), d.count), ends += d.count;
    return {values, ends};
}

void consume_result(const fused_result &r) {
    consume(r.sum);
    sink = r.ge[0] + r.eq + r.rows;
}

template <class T> void run(const char *name, std::span<const u64> values, u64 lo, const char *only) {
    // Timing order (bodies.cpp): ARM_SEED gives a round its own fixed seed,
    // ARM_PREWARM=1 an untimed call of the same arm before each timed one.
    if (const char *e = std::getenv("ARM_SEED"))
        arm_order_seed = std::strtoull(e, nullptr, 0);
    if (const char *e = std::getenv("ARM_PREWARM"))
        arm_prewarm = *e == '1';
    const int trials = std::getenv("RUN_TRIALS") ? std::atoi(std::getenv("RUN_TRIALS")) : 21;  // profiling
    encode_options exact;
    exact.bind_decoders = false;
    encode_options lanes = exact;
    lanes.run_values = encode_options::run_width::byte_lanes;
    encode_options control = lanes;
    control.packed_byte_lanes = true;
    const auto validity = uniform_validity(values.size(), 100);  // 1% NULL
    std::vector<image> images;
    for (auto [label, o] : {std::pair{"exact", exact}, std::pair{"byte_lanes", lanes}, std::pair{"control", control}})
        images.push_back({label, o, encode(values, o), encode_nullable(values, validity, o), 0});
    {
        // Encode time: whole-column encode, 7 rounds in rotated image order,
        // median per image.
        std::vector<std::vector<double>> ns(images.size());
        for (std::size_t round = 0; round < 7; ++round)
            for (std::size_t j = 0; j < images.size(); ++j) {
                auto &im = images[(j + round) % images.size()];
                const auto start = std::chrono::steady_clock::now();
                const column c = encode(values, im.options);
                ns[(j + round) % images.size()].push_back(
                    std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count());
                sink = c.payload.size();
            }
        for (std::size_t j = 0; j < images.size(); ++j) {
            std::sort(ns[j].begin(), ns[j].end());
            images[j].encode_ns = ns[j][ns[j].size() / 2];
        }
    }
    const std::size_t n = images[0].c.blocks.size();
    block_mask all(n);
    for (std::size_t b = 0; b < n; ++b)
        all.set(b);

    // Probes from the input.
    std::vector<u64> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const u64 median = sorted[sorted.size() / 2], max = sorted.back(), p99 = sorted[sorted.size() * 99 / 100];
    u64 mode = sorted[0], rare = sorted[0];
    {
        std::size_t best = 0, least = SIZE_MAX;
        for (std::size_t i = 0; i < sorted.size();) {
            std::size_t j = i;
            while (j < sorted.size() && sorted[j] == sorted[i])
                ++j;
            if (j - i > best)
                best = j - i, mode = sorted[i];
            if (j - i < least)
                least = j - i, rare = sorted[i];
            i = j;
        }
    }
    const u64 above = max == UINT64_MAX ? max : max + 1;

    std::printf("{\"column\":\"%s\",\"arm_seed\":%llu,\"arm_prewarm\":%s,\"rows\":%zu,\"blocks\":%zu,\"median\":%llu,"
                "\"mode\":%llu,\"rare\":%llu,\"max\":%llu,\"p99\":%llu,\"plain_bytes_per_value\":%zu,\"images\":{",
                name, static_cast<unsigned long long>(arm_order_seed), arm_prewarm ? "true" : "false", values.size(), n, static_cast<unsigned long long>(median),
                static_cast<unsigned long long>(mode), static_cast<unsigned long long>(rare), static_cast<unsigned long long>(max),
                static_cast<unsigned long long>(p99), sizeof(T));
    for (std::size_t i = 0; i < images.size(); ++i) {
        const auto &im = images[i];
        std::size_t kinds[7] = {};
        for (const auto &d : im.c.blocks)
            ++kinds[static_cast<int>(d.type)];
        const auto [rv, re] = runs_bytes(im.c);
        std::size_t widths[65] = {};
        for (const auto &d : im.c.blocks)
            if (d.type == kind::runs)
                ++widths[plane_width(d)];
        std::printf("%s\"%s\":{\"total_bytes\":%zu,\"payload_bytes\":%zu,\"bits_per_value\":%.3f,"
                    "\"nullable_total_bytes\":%zu,\"runs_value_bytes\":%zu,\"runs_end_bytes\":%zu,"
                    "\"kinds\":[%zu,%zu,%zu,%zu,%zu,%zu,%zu],\"encode_ns\":%.0f,\"runs_widths\":{",
                    i ? "," : "", im.name, im.c.total_bytes(), im.c.payload.size(),
                    8.0 * im.c.total_bytes() / values.size(), im.n.total_bytes(), rv, re, kinds[0], kinds[1],
                    kinds[2], kinds[3], kinds[4], kinds[5], kinds[6], im.encode_ns);
        bool first = true;
        for (unsigned w = 0; w <= 64; ++w)
            if (widths[w]) {
                std::printf("%s\"%u\":%zu", first ? "" : ",", w, widths[w]);
                first = false;
            }
        std::printf("}}");
    }
    std::printf("}");

    plain<T> p{std::vector<T>(values.size()), lo};
    for (std::size_t i = 0; i < values.size(); ++i)
        p.values[i] = static_cast<T>(values[i] - lo);

    struct program_arm {
        const char *name;
        const image *im;
        run_program program;
        run_fill fill = run_fill::toggle;
        bool masks_only = false;  // the fill only differs in predicate masks
    };
    const std::vector<program_arm> program_arms{{"a", &images[0], run_program::wide},
                                                {"a_dup", &images[0], run_program::wide},
                                                {"b", &images[0], run_program::narrow},
                                                {"b_run", &images[0], run_program::narrow, run_fill::per_run, true},
                                                {"c", &images[1], run_program::narrow},
                                                {"d", &images[1], run_program::in_place},
                                                {"d_run", &images[1], run_program::in_place, run_fill::per_run, true},
                                                {"ctl", &images[2], run_program::in_place}};
    fused_request q2;
    q2.sum = true, q2.ge_count = 1, q2.ge[0] = median;
    // One operation: f(image, nullable?) returns a comparable answer; plain
    // (optional) is the native control.
    auto operation = [&](const char *op, auto f, std::function<void()> plain_call = {}) {
        using answer = decltype(f(images[0]));
        if (only && std::strcmp(only, op))
            return;
        run_values_program = run_program::wide;
        const answer want = f(images[0]);
        std::vector<arm> arms;
        const char *keep_arms = std::getenv("RUN_ARMS");  // optional comma list, for profiling
        for (const auto &pa : program_arms) {
            if (keep_arms && !std::strstr(keep_arms, pa.name))
                continue;
            if (pa.masks_only && std::strncmp(op, "mask", 4))
                continue;
            run_values_program = pa.program;
            run_mask_fill = pa.fill;
            if (!(f(*pa.im) == want)) {
                std::fprintf(stderr, "%s %s: arm %s differs from arm a\n", name, op, pa.name);
                std::exit(1);
            }
            arms.push_back({pa.name, [&, pa] {
                                run_values_program = pa.program;
                                run_mask_fill = pa.fill;
                                const answer r = f(*pa.im);
                                if constexpr (std::is_same_v<answer, fused_result>)
                                    consume_result(r);
                                else if constexpr (std::is_same_v<answer, u128>)
                                    consume(r);
                                else
                                    sink = static_cast<u64>(r);
                            },
                            {}});
        }
        if (plain_call)
            arms.push_back({"plain", plain_call, {}});
        time_arms(arms, trials);
        run_values_program = run_program::wide;
        std::printf(",\"%s\":{\"answer\":\"%s\"", op, [&] {
            if constexpr (std::is_same_v<answer, fused_result>)
                return u128_text(want.sum) + "/" + std::to_string(want.ge[0]);
            else
                return u128_text(static_cast<u128>(want));
        }().c_str());
        print_arms(arms);
        std::printf("}");
        std::fflush(stdout);
    };
    std::vector<u64> out(4 * n);
    auto mask = [&](const image &im, value_predicate pr) { return predicate_masks(im.c, all, pr, out); };
    // Native mask control: the plain array compared into the same plane,
    // checked against the owner's mask count once.
    auto plain_mask = [&](value_predicate pr) {
        const value_interval iv = interval_of(pr);
        const bool none = iv.empty || iv.last < p.base;
        const u64 rlo = none ? 0 : std::max(iv.lo, p.base) - p.base, span = none ? 0 : iv.last - p.base - rlo;
        auto call = [&, none, rlo, span] {
            u64 total = 0;
            for (std::size_t b = 0; b < n; ++b) {
                const T *x = p.values.data() + b * block_rows;
                for (unsigned w = 0; w < 4; ++w) {
                    u64 bits = 0;
                    for (unsigned i = 0; i < 64; ++i)
                        bits |= static_cast<u64>(static_cast<u64>(x[w * 64 + i]) - rlo <= span) << i;
                    bits = none ? 0 : bits;
                    out[4 * b + w] = bits;
                    total += static_cast<u64>(std::popcount(bits));
                }
            }
            return total;
        };
        if (call() != mask(images[0], pr)) {
            std::fprintf(stderr, "%s: plain mask differs\n", name);
            std::exit(1);
        }
        return std::function<void()>([call] { sink = call(); });
    };

    operation("sum", [&](const image &im) { return encoded_sum(im.c, all); },
              [&] { consume(plain_sum(p, all)); });
    operation("ge_median", [&](const image &im) { return encoded_count_ge(im.c, all, median); },
              [&] { sink = plain_count_ge(p, all, median); });
    operation("ge_above_max", [&](const image &im) { return encoded_count_ge(im.c, all, above); },
              [&] { sink = plain_count_ge(p, all, above); });
    operation("eq_mode", [&](const image &im) { return encoded_count_eq(im.c, all, mode); },
              [&] { sink = plain_count_eq(p, all, mode); });
    operation("eq_rare", [&](const image &im) { return encoded_count_eq(im.c, all, rare); },
              [&] { sink = plain_count_eq(p, all, rare); });
    operation("mask_ge_median", [&](const image &im) { return mask(im, {predicate_op::ge, median}); }, plain_mask({predicate_op::ge, median}));
    operation("mask_eq_rare", [&](const image &im) { return mask(im, {predicate_op::eq, rare}); }, plain_mask({predicate_op::eq, rare}));
    operation("mask_ge_above_max", [&](const image &im) { return mask(im, {predicate_op::ge, above}); }, plain_mask({predicate_op::ge, above}));
    operation("mask_p99_max", [&](const image &im) { return mask(im, {predicate_op::between, p99, above}); }, plain_mask({predicate_op::between, p99, above}));
    operation("fused", [&](const image &im) { return encoded_fused(im.c, all, q2); });
    operation("null_sum", [&](const image &im) { return masked_encoded_sum(im.n.values, all, im.n.validity); });
    operation("null_ge_median",
              [&](const image &im) { return masked_encoded_count_ge(im.n.values, all, im.n.validity, median); });
    operation("null_eq_rare",
              [&](const image &im) { return masked_encoded_count_eq(im.n.values, all, im.n.validity, rare); });
    operation("null_fused", [&](const image &im) { return masked_encoded_fused(im.n.values, all, im.n.validity, q2); });
    std::printf("}\n");
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s NAME FILE.u64\n", argv[0]);
        return 2;
    }
    std::vector<u64> values;
    {
        std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
        const auto end = static_cast<std::size_t>(in.tellg());
        if (!in || !end || end % 8 || (end / 8) % block_rows)
            return 2;
        values.resize(end / 8);
        in.seekg(0);
        in.read(reinterpret_cast<char *>(values.data()), static_cast<std::streamsize>(end));
    }
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    const u64 span = *hi - *lo;
    if (span <= 0xff)
        run<std::uint8_t>(argv[1], values, *lo, argc > 3 ? argv[3] : nullptr);
    else if (span <= 0xffff)
        run<std::uint16_t>(argv[1], values, *lo, argc > 3 ? argv[3] : nullptr);
    else if (span <= 0xffffffffu)
        run<std::uint32_t>(argv[1], values, *lo, argc > 3 ? argv[3] : nullptr);
    else
        run<u64>(argv[1], values, *lo, argc > 3 ? argv[3] : nullptr);
}

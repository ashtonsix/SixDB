#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
// Fused multi-aggregate programs (fused.h) against the separate owner calls and
// native-array controls, on one column:
//
//   fused NAME FILE.u64 [--checkpoints=...] [--delta-exceptions] [--dictionary=...]
//
// Requests: SUM + COUNT(x >= median) and SUM + COUNT + COUNT(x == middle row's
// value). Shapes: every block, every 10th block (coarse masks), and every block
// under a 1% uniform NULL validity plane (encode_nullable, predictor fill) as
// the row selection. Arms per shape, timed in bodies.cpp's fixed-seed shuffled
// order:
//   sep2, sep3        the existing owner calls, one per aggregate
//   sep2_best         coarse shapes: narrow SUM + catalogue COUNT, the fastest
//                     separate owner arms
//   fused2, fused3    one fused program
//   sep2_dup, fused2_dup  exact repeats (the noise control)
//   plain_sep2/3      native arrays (the column minus its minimum at the
//                     narrowest byte width), one pass per aggregate
//   plain_fused2/3    the same arrays, every aggregate in one pass
//   cached_*          coded columns: sep and fused through a warm dictionary_cache
// Native SUM over u64 lanes uses 32-bit halves, as the owner's wide SUM does;
// masked native passes take the validity bits as a keep mask. Every arm's
// result is checked against the others before timing.
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
// Uniform NULLs at per_mille10 / 10 per mille, from nulls.cpp's seed law.
std::vector<u64> uniform_validity(std::size_t rows, unsigned per_mille10) {
    std::vector<u64> v(rows / 64, ~u64{0});
    for (std::size_t i = 0; i < rows; ++i)
        if (mix(i * 7919 + per_mille10 * 2) % 10000000 < per_mille10 * 1000)
            v[i / 64] &= ~(u64{1} << (i % 64));
    return v;
}

// Native program: the requested aggregates in one pass per block (Sum, Ge,
// Eq), over every row or (Masked) the validity rows.
template <bool Sum, bool Ge, bool Eq, bool Masked, class T>
fused_result plain_program(const plain<T> &p, const block_mask &mask, const std::vector<u64> *rows, u64 t, u64 v) {
    fused_result r;
    // Comparisons in the lane: a right-hand side past the lane counts nothing.
    constexpr u64 top = std::numeric_limits<T>::max();
    const u64 ge_wide = t <= p.base ? 0 : t - p.base;
    const bool ge_possible = ge_wide <= top, eq_possible = v >= p.base && v - p.base <= top;
    const T ge_rhs = static_cast<T>(ge_wide), eq_rhs = static_cast<T>(v - p.base);
    for_each_block(mask, [&](std::size_t b) {
        const T *x = p.values.data() + b * block_rows;
        u64 lo = 0, hi = 0, n = block_rows;
        unsigned ge = 0, eq = 0;
        if constexpr (Masked) {
            const row_selection s = selection_at(*rows, b);
            n = selected_count(s);
            if (!n)
                return;
            for (unsigned w = 0; w < 4; ++w) {
                const u64 m = s[w];
                const T *y = x + w * 64;
                for (unsigned i = 0; i < 64; ++i) {
                    const u64 bit = m >> i & 1;
                    if constexpr (Sum) {
                        const u64 keep = u64{0} - bit, z = static_cast<u64>(y[i]) & keep;
                        if constexpr (sizeof(T) < 8)
                            lo += z;
                        else
                            lo += z & 0xffffffffu, hi += z >> 32;
                    }
                    if constexpr (Ge)
                        ge += static_cast<unsigned>(y[i] >= ge_rhs) & static_cast<unsigned>(bit);
                    if constexpr (Eq)
                        eq += static_cast<unsigned>(y[i] == eq_rhs) & static_cast<unsigned>(bit);
                }
            }
        } else {
            for (unsigned i = 0; i < block_rows; ++i) {
                if constexpr (Sum) {
                    if constexpr (sizeof(T) < 8)
                        lo += x[i];
                    else
                        lo += x[i] & 0xffffffffu, hi += x[i] >> 32;
                }
                if constexpr (Ge)
                    ge += x[i] >= ge_rhs;
                if constexpr (Eq)
                    eq += x[i] == eq_rhs;
            }
        }
        r.rows += n;
        if constexpr (Sum)
            r.sum += static_cast<u128>(lo) + (static_cast<u128>(hi) << 32) + static_cast<u128>(p.base) * n;
        r.ge[0] += ge_possible ? ge : 0;
        r.eq += eq_possible ? eq : 0;
    });
    return r;
}

void consume_result(const fused_result &r) {
    consume(r.sum);
    sink = r.ge[0] + r.eq + r.rows;
}

struct shape {
    const char *name;
    block_mask mask;
    const column *image;
    const std::vector<u64> *rows;  // null: coarse mask only
};

template <class T>
void run(const char *name, std::span<const u64> values, u64 lo, u64 threshold, u64 equal, encode_options options) {
    const column c = encode(values, options);
    const auto validity = uniform_validity(c.rows, 100);  // 1% NULL
    const auto nullable = encode_nullable(values, validity, options);
    plain<T> p{std::vector<T>(c.rows), lo};
    for (std::size_t i = 0; i < c.rows; ++i)
        p.values[i] = static_cast<T>(values[i] - lo);
    const std::size_t n = c.blocks.size();
    std::size_t kinds[7] = {}, mixed = 0;
    for (const auto &d : c.blocks)
        ++kinds[static_cast<int>(d.type)];
    for (std::size_t b = 0; b < n; ++b) {
        const auto s = selection_at(validity, b);
        mixed += !selection_empty(s) && !selection_full(s);
    }
    std::printf("{\"column\":\"%s\",\"delta_checkpoints\":\"%s\",\"delta_exceptions\":%s,\"dictionary\":\"%s\","
                "\"rows\":%zu,\"blocks\":%zu,\"kinds\":[%zu,%zu,%zu,%zu,%zu,%zu,%zu],\"coded\":%zu,"
                "\"nullable_coded\":%zu,\"null_mixed_blocks\":%zu,\"plain_bytes_per_value\":%zu",
                name, checkpoint_name(options.delta_checkpoints), options.delta_exceptions ? "true" : "false",
                dictionary_name(options.dictionary), c.rows, n, kinds[0], kinds[1], kinds[2], kinds[3], kinds[4],
                kinds[5], kinds[6], c.coded_blocks(), nullable.values.coded_blocks(), mixed, sizeof(T));

    fused_request q2, q3;
    q2.sum = q3.sum = true;
    q2.ge_count = q3.ge_count = 1;
    q2.ge[0] = q3.ge[0] = threshold;
    q3.eq = true, q3.eq_value = equal;

    std::vector<shape> shapes;
    shapes.push_back({"all", block_mask(n), &c, nullptr});
    shapes.push_back({"sparse10", block_mask(n), &c, nullptr});
    shapes.push_back({"null1", block_mask(n), &nullable.values, &nullable.validity});
    for (std::size_t b = 0; b < n; ++b) {
        shapes[0].mask.set(b), shapes[2].mask.set(b);
        if (b % 10 == 0)
            shapes[1].mask.set(b);
    }
    for (const auto &s : shapes) {
        const column &image = *s.image;
        const auto &m = s.mask;
        const bool masked = s.rows != nullptr;
        dictionary_cache cache;
        // Separate owner calls and fused programs, per call or cached.
        auto sep = [&](bool eq, dictionary_cache *k) {
            fused_result r;
            r.sum = masked ? masked_encoded_sum(image, m, *s.rows, k) : encoded_sum(image, m, k);
            r.ge[0] = masked ? masked_encoded_count_ge(image, m, *s.rows, threshold, k)
                             : encoded_count_ge(image, m, threshold, k);
            if (eq)
                r.eq = masked ? masked_encoded_count_eq(image, m, *s.rows, equal, k) : encoded_count_eq(image, m, equal, k);
            r.rows = masked ? masked_selected_rows(m, *s.rows) : m.popcount() * block_rows;
            return r;
        };
        auto sep_best = [&] {
            fused_result r;
            r.sum = encoded_sum_narrow(image, m);
            r.ge[0] = catalogue_count_ge(image, m, threshold);
            r.rows = m.popcount() * block_rows;
            return r;
        };
        auto fused = [&](const fused_request &q, dictionary_cache *k) {
            return masked ? masked_encoded_fused(image, m, *s.rows, q, k) : encoded_fused(image, m, q, k);
        };
        auto plain_sep = [&](bool eq) {
            fused_result r = masked ? plain_program<true, false, false, true>(p, m, s.rows, threshold, equal)
                                    : plain_program<true, false, false, false>(p, m, s.rows, threshold, equal);
            r.ge[0] = (masked ? plain_program<false, true, false, true>(p, m, s.rows, threshold, equal)
                              : plain_program<false, true, false, false>(p, m, s.rows, threshold, equal))
                          .ge[0];
            if (eq)
                r.eq = (masked ? plain_program<false, false, true, true>(p, m, s.rows, threshold, equal)
                               : plain_program<false, false, true, false>(p, m, s.rows, threshold, equal))
                           .eq;
            return r;
        };
        auto plain_fused = [&](bool eq) {
            if (eq)
                return masked ? plain_program<true, true, true, true>(p, m, s.rows, threshold, equal)
                              : plain_program<true, true, true, false>(p, m, s.rows, threshold, equal);
            return masked ? plain_program<true, true, false, true>(p, m, s.rows, threshold, equal)
                          : plain_program<true, true, false, false>(p, m, s.rows, threshold, equal);
        };
        // Every arm's answer agrees before anything is timed.
        const fused_result want2 = sep(false, nullptr), want3 = sep(true, nullptr);
        bool ok = fused(q2, nullptr) == want2 && fused(q3, nullptr) == want3 && fused(q2, &cache) == want2 &&
                  fused(q3, &cache) == want3 && sep(false, &cache) == want2 && sep(true, &cache) == want3 &&
                  plain_sep(false) == want2 && plain_sep(true) == want3 && plain_fused(false) == want2 &&
                  plain_fused(true) == want3;
        if (!masked)
            ok = ok && sep_best() == want2;
        if (!ok) {
            std::fprintf(stderr, "%s %s: fused, separate and native results differ\n", name, s.name);
            std::exit(1);
        }
        std::vector<arm> arms{
            {"sep2", [&] { consume_result(sep(false, nullptr)); }, {}},
            {"sep2_dup", [&] { consume_result(sep(false, nullptr)); }, {}},
            {"fused2", [&] { consume_result(fused(q2, nullptr)); }, {}},
            {"fused2_dup", [&] { consume_result(fused(q2, nullptr)); }, {}},
            {"sep3", [&] { consume_result(sep(true, nullptr)); }, {}},
            {"fused3", [&] { consume_result(fused(q3, nullptr)); }, {}},
            {"plain_sep2", [&] { consume_result(plain_sep(false)); }, {}},
            {"plain_fused2", [&] { consume_result(plain_fused(false)); }, {}},
            {"plain_sep3", [&] { consume_result(plain_sep(true)); }, {}},
            {"plain_fused3", [&] { consume_result(plain_fused(true)); }, {}}};
        if (!masked)
            arms.push_back({"sep2_best", [&] { consume_result(sep_best()); }, {}});
        if (image.coded_blocks()) {
            arms.push_back({"cached_sep2", [&] { consume_result(sep(false, &cache)); }, {}});
            arms.push_back({"cached_fused2", [&] { consume_result(fused(q2, &cache)); }, {}});
            arms.push_back({"cached_sep3", [&] { consume_result(sep(true, &cache)); }, {}});
            arms.push_back({"cached_fused3", [&] { consume_result(fused(q3, &cache)); }, {}});
        }
        time_arms(arms);
        std::printf(",\"%s\":{\"selected_rows\":%llu,\"sum\":\"%s\",\"count_ge\":%llu,\"count_eq\":%llu", s.name,
                    static_cast<unsigned long long>(want3.rows), u128_text(want3.sum).c_str(),
                    static_cast<unsigned long long>(want3.ge[0]), static_cast<unsigned long long>(want3.eq));
        print_arms(arms);
        std::printf("}");
        std::fflush(stdout);
    }
    std::printf("}\n");
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s NAME FILE.u64 [--checkpoints=native|packed|every64|none] "
                     "[--delta-exceptions] [--dictionary=none|sorted|frequency|arrival]\n", argv[0]);
        return 2;
    }
    encode_options options;
    options.bind_decoders = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--delta-exceptions") options.delta_exceptions = true;
        else if (a == "--dictionary=none") options.dictionary = dictionary_order::none;
        else if (a == "--dictionary=sorted") options.dictionary = dictionary_order::sorted;
        else if (a == "--dictionary=frequency") options.dictionary = dictionary_order::frequency;
        else if (a == "--dictionary=arrival") options.dictionary = dictionary_order::arrival;
        else if (a == "--checkpoints=native") options.delta_checkpoints = checkpoints::native;
        else if (a == "--checkpoints=packed") options.delta_checkpoints = checkpoints::packed;
        else if (a == "--checkpoints=every64") options.delta_checkpoints = checkpoints::every64;
        else if (a == "--checkpoints=none") options.delta_checkpoints = checkpoints::none;
        else return 2;
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
    std::vector<u64> sorted(values);
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const u64 threshold = sorted[sorted.size() / 2], equal = values[values.size() / 2];
    const u64 span = *hi - *lo;
    if (span <= 0xff)
        run<std::uint8_t>(argv[1], values, *lo, threshold, equal, options);
    else if (span <= 0xffff)
        run<std::uint16_t>(argv[1], values, *lo, threshold, equal, options);
    else if (span <= 0xffffffffu)
        run<std::uint32_t>(argv[1], values, *lo, threshold, equal, options);
    else
        run<u64>(argv[1], values, *lo, threshold, equal, options);
}

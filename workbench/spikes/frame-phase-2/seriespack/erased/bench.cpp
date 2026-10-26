// The runtime-width reader's compiled range operations against the typed
// reader over the same bytes, at Frame's grain: one call per 256-row block,
// blocks visited in order over a 64K-row part (warm). ns per call; every
// answer is checked against the values.
#include <ikea/seriespack.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>
#include <vector>
namespace sp = ikea::seriespack;
using u64 = std::uint64_t;
using ikea::comparison;

template <class F> double ns_per(std::size_t calls, F&& f) {
    std::vector<double> t;
    for (int r = 0; r < 31; ++r) {
        const auto a = std::chrono::steady_clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count() / calls);
    }
    std::nth_element(t.begin(), t.begin() + 15, t.end());
    return t[15];
}
template <unsigned K, sp::preset P> void run(const char* name) {
    using L = sp::preset_layout<K, P>;
    constexpr std::size_t n = 65536, block = 256, blocks = n / block;
    std::mt19937_64 rng(K);
    std::vector<u64> v(n);
    const u64 top = K == 64 ? ~u64{0} : (u64{1} << K) - 1;
    for (auto& x : v) x = rng() & top;
    std::vector<ikea::byte> bytes(n / L::tile_rows * L::tile_bytes);
    const sp::dense_output out{K, L::storage, bytes, n};
    std::vector<ikea::journal::entry> rec(*sp::effect_capacity_initialize(out));
    ikea::journal effects{rec};
    if (!sp::initialize(out, std::span<const u64>(v), effects)) std::abort();
    const auto view = *sp::const_view<L>::attach(n, {{{std::span<const ikea::byte>(bytes), L::tile_bytes}, {}, {}}});
    const auto typed = sp::bind_reader(view);
    const auto erased = *sp::bind_range_reader<u64>(sp::dense_input{K, L::storage, bytes, n});
    if (!erased.compiled_ranges()) std::abort();
    const u64 t = v[n / 3];
    std::vector<std::uint16_t> masks(n / 16);
    for (auto& m : masks) m = static_cast<std::uint16_t>(rng());
    u64 want_sum = 0, want_ge = 0, want_masked = 0;
    for (std::size_t i = 0; i < n; ++i)
        want_sum += v[i], want_ge += v[i] >= t, want_masked += (masks[i / 16] >> (i % 16) & 1) ? v[i] : 0;
    std::vector<u64> words(4);
    volatile u64 sink = 0;
    auto check = [](bool ok) { if (!ok) { std::fprintf(stderr, "wrong answer\n"); std::exit(1); } };
    // ns_per_call is per 256 rows: one call per 256-row block (call_rows
    // 256), except exact_sum, one 65,536-row call amortized per 256 rows.
    auto emit = [&](const char* op, const char* arm, double ns) {
        const unsigned call_rows = std::string_view(op) == "exact_sum" ? 65536 : std::string_view(op) == "point" ? 1 : 256;
        std::printf("{\"k\":%u,\"layout\":\"%s\",\"op\":\"%s\",\"arm\":\"%s\",\"ns_per_call\":%.3f,\"call_rows\":%u}\n", K, name, op,
                    arm, ns, call_rows);
    };
    // SUM per block.
    { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += typed.sum_unchecked(b * block, block); check(s == want_sum); }
    { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += erased.sum_unchecked(b * block, block); check(s == want_sum); }
    emit("sum", "typed", ns_per(blocks, [&] { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += typed.sum_unchecked(b * block, block); sink = s; }));
    emit("sum", "erased", ns_per(blocks, [&] { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += erased.sum_unchecked(b * block, block); sink = s; }));
    // Masked SUM per block (prefiltered).
    auto typed_masked = [&](std::size_t b) {
        return typed.sum_unchecked(b * block, block, ~u64{0}, [&](std::size_t origin) { return masks[origin / 16]; });
    };
    if constexpr (K < 64) {  // the typed cutoff ~0 excludes the maximum 64-bit value
        { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += typed_masked(b); check(s == want_masked); }
    }
    { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += erased.sum_masked_unchecked(b * block, block, masks.data() + b * 16); check(s == want_masked); }
    emit("masked_sum", "typed", ns_per(blocks, [&] { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += typed_masked(b); sink = s; }));
    emit("masked_sum", "erased", ns_per(blocks, [&] { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += erased.sum_masked_unchecked(b * block, block, masks.data() + b * 16); sink = s; }));
    // COUNT >= t per block.
    { u64 c = 0; for (std::size_t b = 0; b < blocks; ++b) c += erased.count_unchecked(comparison::greater_equal, b * block, block, t); check(c == want_ge); }
    { u64 c = 0; for (std::size_t b = 0; b < blocks; ++b) c += typed.template count_unchecked<comparison::greater_equal>(b * block, block, t); check(c == want_ge); }
    emit("count_ge", "typed", ns_per(blocks, [&] { u64 c = 0; for (std::size_t b = 0; b < blocks; ++b) c += typed.template count_unchecked<comparison::greater_equal>(b * block, block, t); sink = c; }));
    emit("count_ge", "erased", ns_per(blocks, [&] { u64 c = 0; for (std::size_t b = 0; b < blocks; ++b) c += erased.count_unchecked(comparison::greater_equal, b * block, block, t); sink = c; }));
    // Filter words per block: every word checked against the values first,
    // then all four consumed by a non-cancelling digest.
    auto words_ok = [&](std::size_t b) {
        for (unsigned w = 0; w < 4; ++w) {
            u64 bits = 0;
            for (unsigned j = 0; j < 64; ++j)
                bits |= u64{v[b * block + 64 * w + j] >= t} << j;
            if (words[w] != bits) return false;
        }
        return true;
    };
    auto digest = [&](u64 c) { return c * 0x9e3779b97f4a7c15ULL + words[0] + 3 * words[1] + 5 * words[2] + 7 * words[3]; };
    for (std::size_t b = 0; b < blocks; ++b) {
        typed.template filter_words_unchecked<comparison::greater_equal>(b * block, block, t, words.data());
        check(words_ok(b));
        erased.filter_words_unchecked(comparison::greater_equal, b * block, block, t, words.data());
        check(words_ok(b));
    }
    emit("filter_ge", "typed", ns_per(blocks, [&] { u64 c = 0; for (std::size_t b = 0; b < blocks; ++b) { typed.template filter_words_unchecked<comparison::greater_equal>(b * block, block, t, words.data()); c = digest(c); } sink = c; }));
    emit("filter_ge", "erased", ns_per(blocks, [&] { u64 c = 0; for (std::size_t b = 0; b < blocks; ++b) { erased.filter_words_unchecked(comparison::greater_equal, b * block, block, t, words.data()); c = digest(c); } sink = c; }));
    // Position-weighted SUM (weights[row % 16] = row % 16, a delta law's
    // in-region positions): one decode, against the masked-sum law (one
    // prefiltered SUM per bit of the position, weighted by 2^b).
    static constexpr std::uint8_t weights[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    using u128 = unsigned __int128;
    u128 want_weighted = 0, want_exact = 0;
    for (std::size_t i = 0; i < n; ++i)
        want_weighted += static_cast<u128>(i % 16) * v[i], want_exact += v[i];
    static constexpr std::uint16_t bit_masks[4] = {0xaaaa, 0xcccc, 0xf0f0, 0xff00};
    auto by_masks = [&](std::size_t b) {
        u128 s = 0;
        for (unsigned bit = 0; bit < 4; ++bit)
            s += erased.sum_masked_exact_unchecked(b * block, block, &bit_masks[bit], 1) << bit;
        return s;
    };
    auto fold = [](u128 x) { return static_cast<u64>(x) ^ static_cast<u64>(x >> 64); };
    { u128 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += erased.weighted_sum_unchecked(b * block, block, weights); check(s == want_weighted); }
    { u128 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += by_masks(b); check(s == want_weighted); }
    { u128 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += typed.weighted_sum_unchecked(b * block, block, weights); check(s == want_weighted); }
    emit("weighted_sum", "typed", ns_per(blocks, [&] { u128 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += typed.weighted_sum_unchecked(b * block, block, weights); sink = fold(s); }));
    emit("weighted_sum", "erased", ns_per(blocks, [&] { u128 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += erased.weighted_sum_unchecked(b * block, block, weights); sink = fold(s); }));
    emit("weighted_sum", "masked_law", ns_per(blocks, [&] { u128 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += by_masks(b); sink = fold(s); }));
    // Exact SUM of the whole part in one call, against Frame's former route
    // (modulo sums in exact chunks, or decode and add above 55 bits).
    check(erased.sum_exact_unchecked(0, n) == want_exact);
    auto frame_route = [&] {
        u128 s = 0;
        if constexpr (K <= 55) {
            const std::size_t chunk = K <= 48 ? n : (std::size_t{1} << (64 - K)) / block * block;
            for (std::size_t at = 0; at < n; at += chunk)
                s += erased.sum_unchecked(at, std::min(chunk, n - at));
        } else {
            alignas(64) u64 x[block];
            for (std::size_t at = 0; at < n; at += block) {
                erased.read_unchecked(at, block, x);
                for (std::size_t i = 0; i < block; ++i) s += x[i];
            }
        }
        return s;
    };
    check(frame_route() == want_exact);
    emit("exact_sum", "erased", ns_per(blocks, [&] { sink = fold(erased.sum_exact_unchecked(0, n)); }));
    emit("exact_sum", "frame_route", ns_per(blocks, [&] { sink = fold(frame_route()); }));
    // Points through the exposed kernel.
    std::vector<std::uint32_t> rows(4096);
    u64 want_points = 0;
    for (auto& r : rows) r = static_cast<std::uint32_t>(rng() % n), want_points += v[r];
    const auto kernel = erased.point_kernel();
    const auto* resource = erased.resource();
    { u64 s = 0; for (auto r : rows) s += kernel(resource, r); check(s == want_points); }
    { u64 s = 0; for (auto r : rows) s += typed.get_unchecked(r); check(s == want_points); }
    emit("point", "typed", ns_per(rows.size(), [&] { u64 s = 0; for (auto r : rows) s += typed.get_unchecked(r); sink = s; }));
    emit("point", "erased", ns_per(rows.size(), [&] { u64 s = 0; for (auto r : rows) s += kernel(resource, r); sink = s; }));
}
int main() {
    run<3, sp::preset::compact>("compact");
    run<7, sp::preset::compact>("compact");
    run<12, sp::preset::compact>("compact");
    run<20, sp::preset::compact>("compact");
    run<33, sp::preset::compact>("compact");
    run<55, sp::preset::compact>("compact");
    run<60, sp::preset::compact>("compact");
    run<64, sp::preset::compact>("compact");
#if defined(__aarch64__)
    run<3, sp::preset::bulk_arm>("bulk");
    run<14, sp::preset::bulk_arm>("bulk");
#else
    run<3, sp::preset::bulk_x86>("bulk");
    run<12, sp::preset::bulk_x86>("bulk");
#endif
}

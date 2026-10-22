#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
// NULL fill on real columns: bits/row of predictor fill, zero fill and a
// modelled compaction arm under synthetic NULLs, then (--time) row-selected
// SUM and COUNT >= median over the predictor-filled image against the
// decode-then-mask control and the no-NULL image.
//
//   nulls NAME FILE.u64 [--time=PER_MILLE10[:clustered]]... [--encode-cost]
//
// --encode-cost instead reports encoder time and allocations of NULL fill
// against plain encode() on the same column.
//
// NULLs are synthetic: uniform (each row independently) or clustered (runs
// with mean length 64 rows), at 0.1%, 1%, 10% and 50% of rows, from a fixed
// seed. Arms, all under default encode options plus the 32 B/block validity
// plane:
//   predictor  encode_nullable(..., null_fill::predictor)
//   zero       encode_nullable(..., null_fill::zero)
//   compacted  a byte model: each block stores only its NN valid rows, priced
//              by material.cpp's rules for the kind chosen on those values
//              (constant, runs, packed, delta, patched at NN rows; no
//              delta_patched, which default options exclude), plus its
//              16-byte descriptor and the validity plane. No NN body exists.
// The model is calibrated against real bodies on the no-NULL column: for
// every block it must equal the real body's bytes.
#define FRAME_ENCODING_BODY_NO_MAIN
#include "bodies.cpp"

#include <new>

// Allocation accounting for --encode-cost: every operator new in this binary.
namespace {
std::size_t allocations = 0, allocated_bytes = 0;
}
void *operator new(std::size_t n) {
    ++allocations, allocated_bytes += n;
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

namespace {
using namespace frame_encoding;

u64 mix(u64 x) {
    x += 0x9e3779b97f4a7c15u;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9u;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebu;
    return x ^ (x >> 31);
}

std::vector<u64> make_validity(std::size_t rows, unsigned per_mille10, bool clustered) {
    // per_mille10: tenths of a per mille (1 = 0.01%; 10 = 0.1%).
    std::vector<u64> v(rows / 64, ~u64{0});
    const u64 scale = 10000000;  // probability unit
    const u64 density = per_mille10 * 1000;  // in scale units
    bool in_null = false;
    for (std::size_t i = 0; i < rows; ++i) {
        const u64 r = mix(i * 7919 + per_mille10 * 2 + clustered) % scale;
        if (clustered)  // two-state chain: exit 1/64, entry chosen for the density
            in_null = in_null ? r >= scale / 64 : r < scale / 64 * density / (scale - density);
        else
            in_null = r < density;
        if (in_null)
            v[i / 64] &= ~(u64{1} << (i % 64));
    }
    return v;
}

// Bytes of one block's body under material.cpp's rules (default options:
// native checkpoints, no delta exceptions), for n <= 256 values.
std::size_t model_bytes(const u64 *v, std::size_t n, checkpoints mode) {
    if (!n)
        return 0;
    const auto [lo, hi] = std::minmax_element(v, v + n);
    if (*lo == *hi)
        return 0;
    const unsigned k = width_of(*hi - *lo);
    std::size_t runs = 1;
    for (std::size_t i = 1; i < n; ++i)
        runs += v[i] != v[i - 1];
    const std::size_t packed = plane_bytes(k, n), run_cost = plane_bytes(k, runs) + runs;
    std::size_t delta = SIZE_MAX;
    if (n >= 2) {
        bool ok = true;
        i128 dmin = 0, dmax = 0;
        for (std::size_t i = 1; i < n; ++i) {
            const i128 step = static_cast<i128>(v[i]) - static_cast<i128>(v[i - 1]);
            if (step < -(static_cast<i128>(1) << 63) || step >= (static_cast<i128>(1) << 63))
                ok = false;
            dmin = i == 1 ? step : std::min(dmin, step);
            dmax = i == 1 ? step : std::max(dmax, step);
        }
        const unsigned dk = ok ? std::max(1u, width_of(static_cast<u64>(dmax - dmin))) : 64;
        if (ok)
            delta = plane_bytes(dk, n) + 8 + checkpoint_cost(mode, dk);
    }
    std::vector<u64> sorted(v, v + n);
    std::sort(sorted.begin(), sorted.end());
    std::size_t patched = SIZE_MAX;
    for (unsigned e : {2u, 8u, 32u}) {
        if (e >= n)
            continue;
        const std::size_t keep = n - e;
        u64 best_span = UINT64_MAX, best_base = 0;
        for (std::size_t s = 0; s + keep <= n; ++s)
            if (sorted[s + keep - 1] - sorted[s] < best_span)
                best_span = sorted[s + keep - 1] - sorted[s], best_base = sorted[s];
        const unsigned pk = std::max(1u, width_of(best_span));
        if (pk > 56 || pk >= k)
            continue;
        const u64 limit = (u64{1} << pk) - 1;
        std::size_t x = 0;
        for (std::size_t i = 0; i < n; ++i)
            x += v[i] < best_base || v[i] - best_base > limit;
        if (x)
            patched = std::min(patched, plane_bytes(pk, n) + 10 + x + plane_bytes(k, x));
    }
    return std::min({packed, run_cost, delta, patched});
}

struct shares {
    std::size_t kinds[7] = {};
};
shares kinds_of(const column &c) {
    shares s;
    for (const auto &d : c.blocks)
        ++s.kinds[static_cast<int>(d.type)];
    return s;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s NAME FILE.u64 [--time=PER_MILLE10[:clustered]]...\n", argv[0]);
        return 2;
    }
    std::vector<std::pair<unsigned, bool>> timed;
    bool encode_cost = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--encode-cost") {
            encode_cost = true;
            continue;
        }
        if (a.rfind("--time=", 0) != 0)
            return 2;
        timed.emplace_back(static_cast<unsigned>(std::stoul(a.substr(7))), a.find(":clustered") != std::string::npos);
    }
    std::vector<u64> values;
    {
        std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
        const auto end = static_cast<std::size_t>(in.tellg());
        if (!in || end % 8 || (end / 8) % block_rows)
            return 2;
        values.resize(end / 8);
        in.seekg(0);
        in.read(reinterpret_cast<char *>(values.data()), static_cast<std::streamsize>(end));
    }
    const std::size_t rows = values.size(), nblocks = rows / block_rows;
    const encode_options options{};
    if (encode_cost) {
        // Encoder cost of NULL fill against plain encode() on the same column:
        // median wall time of 5 runs and allocations (calls, bytes requested)
        // of one run, for fill_nulls alone, encode() of its output, and
        // encode_nullable (both plus the validity copy).
        auto measure = [&](auto &&run) {
            std::vector<double> ns;
            std::size_t calls = 0, bytes = 0;
            for (int t = 0; t < 5; ++t) {
                const std::size_t c0 = allocations, b0 = allocated_bytes;
                const auto start = std::chrono::steady_clock::now();
                run();
                ns.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count());
                calls = allocations - c0, bytes = allocated_bytes - b0;
            }
            std::sort(ns.begin(), ns.end());
            return std::tuple{ns[2] / 1e6, calls, bytes};
        };
        const auto [plain_ms, plain_calls, plain_bytes] = measure([&] { sink = encode(values, options).payload.size(); });
        for (unsigned per_mille10 : {10u, 100u, 1000u, 5000u})
            for (bool clustered : {false, true}) {
                const auto validity = make_validity(rows, per_mille10, clustered);
                std::vector<u64> filled;
                const auto [fill_ms, fill_calls, fill_bytes] =
                    measure([&] { filled = fill_nulls(values, validity, options, null_fill::predictor); });
                const auto [enc_ms, enc_calls, enc_bytes] = measure([&] { sink = encode(filled, options).payload.size(); });
                const auto [all_ms, all_calls, all_bytes] = measure(
                    [&] { sink = encode_nullable(values, validity, options, null_fill::predictor).values.payload.size(); });
                const auto [zero_ms, zero_calls, zero_bytes] = measure(
                    [&] { sink = encode_nullable(values, validity, options, null_fill::zero).values.payload.size(); });
                std::printf("{\"column\":\"%s\",\"rows\":%zu,\"nulls_per_mille\":%.1f,\"pattern\":\"%s\","
                            "\"plain_encode_ms\":%.2f,\"plain_allocs\":%zu,\"plain_alloc_bytes\":%zu,"
                            "\"fill_ms\":%.2f,\"fill_allocs\":%zu,\"fill_alloc_bytes\":%zu,"
                            "\"encode_filled_ms\":%.2f,\"encode_filled_allocs\":%zu,\"encode_filled_alloc_bytes\":%zu,"
                            "\"encode_nullable_ms\":%.2f,\"encode_nullable_allocs\":%zu,\"encode_nullable_alloc_bytes\":%zu,"
                            "\"zero_nullable_ms\":%.2f,\"zero_nullable_allocs\":%zu,\"zero_nullable_alloc_bytes\":%zu}\n",
                            argv[1], rows, per_mille10 / 10.0, clustered ? "clustered" : "uniform", plain_ms, plain_calls,
                            plain_bytes, fill_ms, fill_calls, fill_bytes, enc_ms, enc_calls, enc_bytes, all_ms, all_calls,
                            all_bytes, zero_ms, zero_calls, zero_bytes);
                std::fflush(stdout);
            }
        return 0;
    }
    const column plain = encode(values, options);
    // Calibrate the compaction model on full blocks.
    for (std::size_t b = 0; b < nblocks; ++b) {
        const auto &d = plain.blocks[b];
        const auto x = *extent_of(d, options.delta_checkpoints, plain.payload.data() + d.offset,
                                  plain.payload.size() - d.offset);
        if (model_bytes(values.data() + b * block_rows, block_rows, options.delta_checkpoints) != x.body_bytes) {
            std::fprintf(stderr, "%s: compaction model differs from the real body at block %zu\n", argv[1], b);
            return 1;
        }
    }
    const double plain_bits = 8.0 * plain.total_bytes() / rows;
    block_mask all(nblocks);
    for (std::size_t b = 0; b < nblocks; ++b)
        all.set(b);
    std::vector<u64> sorted(values);
    std::nth_element(sorted.begin(), sorted.begin() + rows / 2, sorted.end());
    const u64 median = sorted[rows / 2];

    for (unsigned per_mille10 : {10u, 100u, 1000u, 5000u})
        for (bool clustered : {false, true}) {
            const auto validity = make_validity(rows, per_mille10, clustered);
            std::size_t nulls = 0, mixed = 0, empty = 0;
            for (std::size_t b = 0; b < nblocks; ++b) {
                const unsigned n = selected_count(selection_at(validity, b));
                nulls += block_rows - n, mixed += n && n < block_rows, empty += !n;
            }
            const auto predictor = encode_nullable(values, validity, options, null_fill::predictor);
            const auto zero = encode_nullable(values, validity, options, null_fill::zero);
            // Compaction model, and its packed-heavy share: blocks whose
            // compacted kind would be packed or patched at NN rows.
            std::size_t compact = 0, compact_wins = 0, saved_in_wins = 0;
            std::vector<u64> buffer(block_rows);
            for (std::size_t b = 0; b < nblocks; ++b) {
                const auto s = selection_at(validity, b);
                std::size_t n = 0;
                for (unsigned i = 0; i < block_rows; ++i)
                    if (selected(s, i))
                        buffer[n++] = values[b * block_rows + i];
                const std::size_t c = model_bytes(buffer.data(), n, options.delta_checkpoints);
                compact += c;
                const auto &d = predictor.values.blocks[b];
                const std::size_t real = extent_of(d, options.delta_checkpoints, predictor.values.payload.data() + d.offset,
                                                   predictor.values.payload.size() - d.offset)->body_bytes;
                if (c < real)
                    ++compact_wins, saved_in_wins += real - c;
            }
            const std::size_t compact_total = compact + nblocks * sizeof(descriptor) + predictor.validity_bytes();
            // Exact logical results over valid rows, for both filled images.
            u128 sum = 0;
            u64 ge = 0;
            for (std::size_t i = 0; i < rows; ++i)
                if (validity[i / 64] >> (i % 64) & 1)
                    sum += values[i], ge += values[i] >= median;
            for (const auto *image : {&predictor, &zero})
                if (masked_encoded_sum(image->values, all, image->validity) != sum ||
                    masked_encoded_count_ge(image->values, all, image->validity, median) != ge ||
                    reference_masked_sum(image->values, all, image->validity) != sum ||
                    reference_masked_count_ge(image->values, all, image->validity, median) != ge) {
                    std::fprintf(stderr, "%s: logical result over valid rows differs\n", argv[1]);
                    return 1;
                }
            const auto pk = kinds_of(predictor.values), zk = kinds_of(zero.values);
            std::printf("{\"column\":\"%s\",\"rows\":%zu,\"nulls_per_mille\":%.1f,\"pattern\":\"%s\",\"nulls\":%zu,"
                        "\"mixed_blocks\":%zu,\"all_null_blocks\":%zu,\"no_null_bits\":%.3f,\"validity_bits\":%.3f,"
                        "\"predictor_bits\":%.3f,\"zero_bits\":%.3f,\"compacted_bits\":%.3f,"
                        "\"compact_wins\":%zu,\"compact_saved_bits\":%.3f,"
                        "\"predictor_kinds\":[%zu,%zu,%zu,%zu,%zu,%zu],\"zero_kinds\":[%zu,%zu,%zu,%zu,%zu,%zu]",
                        argv[1], rows, per_mille10 / 10.0, clustered ? "clustered" : "uniform", nulls, mixed, empty,
                        plain_bits, 8.0 * predictor.validity_bytes() / rows, 8.0 * predictor.total_bytes() / rows,
                        8.0 * zero.total_bytes() / rows, 8.0 * compact_total / rows, compact_wins,
                        8.0 * saved_in_wins / rows, pk.kinds[0], pk.kinds[1], pk.kinds[2], pk.kinds[3], pk.kinds[4],
                        pk.kinds[5], zk.kinds[0], zk.kinds[1], zk.kinds[2], zk.kinds[3], zk.kinds[4], zk.kinds[5]);
            if (std::find(timed.begin(), timed.end(), std::pair{per_mille10, clustered}) != timed.end()) {
                const column &c = predictor.values;
                const std::vector<u64> &v = predictor.validity;
                // Arms in a fixed-seed shuffled order per trial; masked_*_dup
                // repeats masked_* exactly (the noise control).
                std::vector<arm> arms{
                    {"masked_sum", [&] { consume(masked_encoded_sum(c, all, v)); }, {}},
                    {"masked_sum_dup", [&] { consume(masked_encoded_sum(c, all, v)); }, {}},
                    {"reference_sum", [&] { consume(reference_masked_sum(c, all, v)); }, {}},
                    {"no_null_sum", [&] { consume(encoded_sum(plain, all)); }, {}},
                    {"filled_unmasked_sum", [&] { consume(encoded_sum(c, all)); }, {}},
                    {"masked_count", [&] { sink = masked_encoded_count_ge(c, all, v, median); }, {}},
                    {"masked_count_dup", [&] { sink = masked_encoded_count_ge(c, all, v, median); }, {}},
                    {"reference_count", [&] { sink = reference_masked_count_ge(c, all, v, median); }, {}},
                    {"no_null_count", [&] { sink = encoded_count_ge(plain, all, median); }, {}},
                    {"filled_unmasked_count", [&] { sink = encoded_count_ge(c, all, median); }, {}}};
                time_arms(arms);
                std::printf(",\"timing\":{\"trials\":21");
                print_arms(arms);
                std::printf("}");
            }
            std::printf("}\n");
            std::fflush(stdout);
        }
}

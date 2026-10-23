// NULL fill check for the private Frame material. Synthetic blocks pin the
// unsigned-domain boundaries (values near 0 and UINT64_MAX, negative and
// positive rbase, leading and trailing holes, all-NULL blocks, one valid
// sample, alternating and run NULLs, no NULLs), and a real column (optional
// arguments) adds random and clustered NULLs. Every image must:
//   - keep every valid row and fill NULL rows within the block's valid
//     [min, max] (the span is unchanged and no filler wraps);
//   - not depend on NULL payloads: poisoned NULL inputs give the same image;
//   - equal the plain encode() image when a block has no NULL;
//   - decode non-decreasing wherever a delta block has rbase >= 0 (the
//     monotone COUNT premise);
//   - give exact logical SUM, COUNT(>= t) and COUNT(== v) over valid rows (and
//     valid rows AND a random selection) against the input oracle.
#include "material.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
using namespace frame_encoding;

[[noreturn]] void fail(const char *what, const std::string &name, std::size_t b = 0) {
    std::fprintf(stderr, "null check: %s (%s, block %zu)\n", what, name.c_str(), b);
    std::exit(1);
}

u64 mix(u64 x) {
    x += 0x9e3779b97f4a7c15u;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9u;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebu;
    return x ^ (x >> 31);
}

void set(std::vector<u64> &v, std::size_t row, bool on) {
    if (on)
        v[row / 64] |= u64{1} << (row % 64);
    else
        v[row / 64] &= ~(u64{1} << (row % 64));
}
bool is_set(const std::vector<u64> &v, std::size_t row) { return v[row / 64] >> (row % 64) & 1; }

std::size_t checked_images = 0;

void check(const std::string &name, const std::vector<u64> &values, const std::vector<u64> &validity,
           encode_options options) {
    const std::size_t nblocks = values.size() / block_rows;
    // NULL payloads must not be read: poison them two ways.
    std::vector<u64> poisoned(values), zeroed(values);
    for (std::size_t i = 0; i < values.size(); ++i)
        if (!is_set(validity, i))
            poisoned[i] = mix(i), zeroed[i] = 0;
    for (null_fill mode : {null_fill::predictor, null_fill::zero}) {
        const auto image = encode_nullable(poisoned, validity, options, mode);
        const auto other = encode_nullable(zeroed, validity, options, mode);
        if (image.values.payload != other.values.payload ||
            std::memcmp(image.values.blocks.data(), other.values.blocks.data(), nblocks * sizeof(descriptor)))
            fail("image depends on NULL payloads", name);
        const auto filled = fill_nulls(poisoned, validity, options, mode);
        const column &c = image.values;
        alignas(64) u64 out[block_rows];
        for (std::size_t b = 0; b < nblocks; ++b) {
            u64 lo = UINT64_MAX, hi = 0;
            unsigned valid = 0;
            for (std::size_t i = b * block_rows; i < (b + 1) * block_rows; ++i)
                if (is_set(validity, i)) {
                    if (filled[i] != values[i])
                        fail("a valid row changed", name, b);
                    lo = std::min(lo, values[i]), hi = std::max(hi, values[i]), ++valid;
                }
            decode_block(c, b, out);
            if (std::memcmp(out, filled.data() + b * block_rows, sizeof out))
                fail("decode differs from the filled values", name, b);
            if (mode == null_fill::predictor && valid)
                for (unsigned i = 0; i < block_rows; ++i)
                    if (out[i] < lo || out[i] > hi)
                        fail("a filler left the valid span", name, b);
            if (!valid && mode == null_fill::predictor && c.blocks[b].type != kind::constant)
                fail("an all-NULL block is not constant", name, b);
            const auto k = borrow(c, b);
            if (!k.coded && k.d.type == kind::delta && delta_rbase(k) >= 0)
                for (unsigned i = 1; i < block_rows; ++i)
                    if (out[i] < out[i - 1])
                        fail("a delta block with rbase >= 0 decreases", name, b);
            if (valid == block_rows && options.dictionary == dictionary_order::none) {
                // No NULL: the block's body is the plain encoder's.
                const column plain = encode(std::span<const u64>(values).subspan(b * block_rows, block_rows), options);
                const auto x = *extent_of(c.blocks[b], c.options.delta_checkpoints, c.payload.data() + c.blocks[b].offset,
                                          c.payload.size() - c.blocks[b].offset);
                if (!k.coded && (plain.blocks[0].type != k.d.type || plain.blocks[0].base != k.d.base ||
                                 plain.blocks[0].width != c.blocks[b].width || plain.payload.size() != x.body_bytes ||
                                 std::memcmp(plain.payload.data(), k.body, x.body_bytes)))
                    fail("a block without NULL differs from encode()", name, b);
            }
        }
        // Logical results over valid rows, and over valid rows AND a selection.
        block_mask all(nblocks);
        for (std::size_t b = 0; b < nblocks; ++b)
            all.set(b);
        std::vector<u64> selection(image.validity);
        for (std::size_t w = 0; w < selection.size(); ++w)
            selection[w] &= mix(w * 31 + 7) | mix(w * 37 + 1);  // about 3/4 of rows
        std::vector<u64> sorted;
        for (std::size_t i = 0; i < values.size(); ++i)
            if (is_set(validity, i))
                sorted.push_back(values[i]);
        std::sort(sorted.begin(), sorted.end());
        const u64 median = sorted.empty() ? 0 : sorted[sorted.size() / 2];
        const u64 probe = sorted.empty() ? 0 : sorted[sorted.size() / 3];
        for (const std::vector<u64> *rows : {&image.validity, static_cast<const std::vector<u64> *>(&selection)}) {
            u128 sum = 0;
            u64 ge = 0, eq = 0, n = 0;
            for (std::size_t i = 0; i < values.size(); ++i)
                if (is_set(*rows, i))
                    sum += values[i], ge += values[i] >= median, eq += values[i] == probe, ++n;
            dictionary_cache cache;
            if (masked_encoded_sum(c, all, *rows) != sum || reference_masked_sum(c, all, *rows) != sum ||
                masked_encoded_sum(c, all, *rows, &cache) != sum ||
                masked_encoded_count_ge(c, all, *rows, median) != ge ||
                reference_masked_count_ge(c, all, *rows, median) != ge ||
                masked_encoded_count_eq(c, all, *rows, probe) != eq || masked_selected_rows(all, *rows) != n)
                fail("logical result over valid rows differs from the input", name);
        }
        ++checked_images;
    }
}

// One block of 256 values from a generator, with validity from a predicate.
template <class V, class N>
void synthetic(const std::string &name, V value, N null, encode_options options) {
    std::vector<u64> values(block_rows), validity(4, 0);
    for (unsigned i = 0; i < block_rows; ++i) {
        values[i] = value(i);
        set(validity, i, !null(i));
    }
    check(name, values, validity, options);
}
}  // namespace

int main(int argc, char **argv) {
    const encode_options option_sets[] = {
        {},
        {checkpoints::packed, true, dictionary_order::none, false, false},
        {checkpoints::none, true, dictionary_order::none, false, false},
        {checkpoints::every64, false, dictionary_order::sorted, false, false},
        {checkpoints::native, false, dictionary_order::arrival, false, false},
    };
    const u64 top = UINT64_MAX;
    for (const auto &o : option_sets) {
        auto run = [&](const char *name, auto value, auto null) { synthetic(name, value, null, o); };
        // Rising and falling walks at both ends of the domain, with leading,
        // trailing and interior holes.
        for (unsigned lead : {0u, 1u, 9u})
            for (unsigned trail : {0u, 1u, 13u}) {
                auto holes = [=](unsigned i) { return i < lead || i >= block_rows - trail || (i > 100 && i < 110); };
                run("rise at 0", [](unsigned i) { return u64{5} * i; }, holes);
                run("fall to 0", [](unsigned i) { return u64{3} * (block_rows - 1 - i); }, holes);
                run("rise to max", [=](unsigned i) { return top - u64{7} * (block_rows - 1 - i); }, holes);
                run("fall from max", [=](unsigned i) { return top - u64{2} * i; }, holes);
                run("noisy rise to max", [=](unsigned i) { return top - 1000 * (block_rows - 1 - i) - mix(i) % 50; }, holes);
                run("steps of 2^62", [=](unsigned i) { return (i % 4) * (u64{1} << 62) + (i == 0 ? 0 : top % 7); }, holes);
            }
        run("all NULL", [](unsigned i) { return mix(i); }, [](unsigned) { return true; });
        for (unsigned keep : {0u, 77u, 255u}) {
            run("one valid", [](unsigned i) { return top - i; }, [=](unsigned i) { return i != keep; });
            run("one valid at 0", [](unsigned) { return u64{0}; }, [=](unsigned i) { return i != keep; });
        }
        run("two valid at max", [=](unsigned i) { return i == 3 ? top : top - 1; }, [](unsigned i) { return i != 3 && i != 250; });
        run("alternating", [](unsigned i) { return 1000 + 3 * i; }, [](unsigned i) { return i % 2; });
        run("runs of NULL", [](unsigned i) { return u64{1} << 40 | (i / 32); }, [](unsigned i) { return i / 16 % 3 == 0; });
        run("no NULL", [](unsigned i) { return 1000 + mix(i) % 100; }, [](unsigned) { return false; });
        run("no NULL walk", [](unsigned i) { return top - 3 * i; }, [](unsigned) { return false; });
        run("sentinels", [=](unsigned i) { return i % 50 == 7 ? top : 500 + i; }, [](unsigned i) { return i % 9 == 0; });
    }
    std::size_t real_rows = 0;
    if (argc >= 3) {
        std::FILE *f = std::fopen(argv[2], "rb");
        if (!f)
            return 2;
        std::fseek(f, 0, SEEK_END);
        const long end = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<u64> values(static_cast<std::size_t>(end) / 8 / block_rows * block_rows);
        if (std::fread(values.data(), 8, values.size(), f) != values.size())
            return 2;
        std::fclose(f);
        if (values.size() > 4 * part_rows)
            values.resize(4 * part_rows);  // four parts are enough for the laws
        real_rows = values.size();
        for (unsigned per_mille : {1u, 100u, 500u})
            for (bool clustered : {false, true}) {
                std::vector<u64> validity(values.size() / 64, ~u64{0});
                bool in_null = false;
                for (std::size_t i = 0; i < values.size(); ++i) {
                    const u64 r = mix(i * 1000 + per_mille) % 1000000;
                    if (clustered)  // mean NULL run 64 rows, density per_mille / 1000
                        in_null = in_null ? r >= 1000000 / 64
                                          : r < 1000000 / 64 * per_mille / (1000 - per_mille);
                    else
                        in_null = r < per_mille * 1000u;
                    if (in_null)
                        set(validity, i, false);
                }
                for (const auto &o : option_sets)
                    check(std::string(argv[1]) + (clustered ? " clustered" : " uniform"), values, validity, o);
            }
    }
    std::printf("{\"null_check\":\"pass\",\"images\":%zu,\"real\":\"%s\",\"real_rows\":%zu}\n", checked_images,
                argc >= 2 ? argv[1] : "", real_rows);
}

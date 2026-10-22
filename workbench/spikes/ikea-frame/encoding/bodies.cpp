// Real encoded bodies for one column: 256-row blocks of constant, runs or
// frame-of-reference kind, built with actual SeriesPack planes and Boundlist
// run geometry. Reports every byte (descriptors, payload, run geometry) and
// times exact SUM and COUNT(u >= t) under coarse block masks against plain
// native-array controls. Values arrive as u64 working-domain values (offset
// binary for signed columns); results are exact in that domain.
//
// Research prototype: the block selector is an oracle (smallest bytes per
// block, and per block values or part dictionary codes), there are no
// overrides yet, and nothing here is a Frame interface.

#include "material.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace {
using namespace frame_encoding;

// Plain control: values relative to the column minimum at the narrowest byte
// width that holds them (the natural typed array a scan engine would use).
template <class T> struct plain {
    std::vector<T> values;
    u64 base;
};

template <class T> u128 plain_sum(const plain<T> &p, const block_mask &mask) {
    u128 total = 0;
    for_each_block(mask, [&](std::size_t b) {
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
    });
    return total;
}

template <class T>
u64 plain_count_ge(const plain<T> &p, const block_mask &mask, u64 threshold) {
    if (threshold <= p.base)
        return mask.popcount() * block_rows;
    const u64 rhs = threshold - p.base;
    u64 total = 0;
    for_each_block(mask, [&](std::size_t b) {
        const T *v = p.values.data() + b * block_rows;
        for (std::size_t i = 0; i < block_rows; ++i)
            total += static_cast<u64>(v[i]) >= rhs;
    });
    return total;
}

// Paired, rotated arms: every trial times each arm once, starting from a
// different arm, so drift lands on all arms alike instead of on whichever arm
// ran last. Raw whole-call nanoseconds are kept per arm.
struct arm {
    const char *name;
    std::function<void()> call;
    std::vector<double> ns;
};

void time_arms(std::vector<arm> &arms, int trials = 21) {
    for (auto &a : arms)
        a.call();  // warm
    for (int t = 0; t < trials; ++t)
        for (std::size_t i = 0; i < arms.size(); ++i) {
            auto &a = arms[(i + t) % arms.size()];
            const auto start = std::chrono::steady_clock::now();
            a.call();
            a.ns.push_back(std::chrono::duration<double, std::nano>(
                               std::chrono::steady_clock::now() - start)
                               .count());
        }
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


template <class T>
void run(const char *name, std::span<const u64> values, u64 lo, u64 threshold,
         encode_options options) {
    const column c = encode(values, options);
    plain<T> p{std::vector<T>(c.rows), lo};
    for (std::size_t i = 0; i < c.rows; ++i)
        p.values[i] = static_cast<T>(values[i] - lo);
    // Whole-block decode must reproduce the input exactly.
    {
        alignas(64) u64 decoded[block_rows];
        for (std::size_t b = 0; b < c.blocks.size(); ++b) {
            decode_block(c, b, decoded);
            if (std::memcmp(decoded, values.data() + b * block_rows, sizeof decoded) != 0) {
                std::fprintf(stderr, "%s: decode_block mismatch at block %zu\n", name, b);
                std::exit(1);
            }
        }
    }
    std::size_t counts[6] = {}, coded_counts[6] = {};
    for (std::size_t b = 0; b < c.blocks.size(); ++b) {
        ++counts[static_cast<int>(c.blocks[b].type)];
        if (is_coded(c, b))
            ++coded_counts[static_cast<int>(c.blocks[b].type)];
    }
    const std::size_t total_bytes = c.total_bytes();
    std::size_t dictionary_parts = 0;
    for (const auto &part : c.parts)
        dictionary_parts += part.count != 0;
    std::printf("{\"column\":\"%s\",\"delta_checkpoints\":\"%s\",\"rows\":%zu,\"blocks\":%zu,\"constant\":%zu,\"runs\":%zu,"
                "\"packed\":%zu,\"delta\":%zu,\"patched\":%zu,\"delta_patched\":%zu,\"descriptor_bytes\":%zu,\"payload_bytes\":%zu,"
                "\"dictionary\":\"%s\",\"dictionary_every_part\":%s,"
                "\"parts\":%zu,\"dictionary_parts\":%zu,\"dictionary_bytes\":%zu,\"part_record_bytes\":%zu,"
                "\"coded_bit_bytes\":%zu,\"coded_blocks\":%zu,\"bits_per_value\":%.3f,\"plain_bytes_per_value\":%zu",
                name, checkpoint_name(c.options.delta_checkpoints),
                c.rows, c.blocks.size(), counts[0], counts[1], counts[2], counts[3], counts[4], counts[5],
                c.descriptor_bytes(), c.payload.size(),
                dictionary_name(c.options.dictionary), c.options.dictionary_every_part ? "true" : "false",
                c.parts.size(), dictionary_parts, c.dictionary_bytes(), c.part_record_bytes(),
                c.coded_bit_bytes(), c.coded_blocks(), 8.0 * total_bytes / c.rows, sizeof(T));
    // Retained owner memory: the column object, every vector's capacity, and
    // of that the binding vectors (zero under --no-bindings).
    const std::size_t retained = sizeof(column) + c.blocks.capacity() * sizeof(descriptor) +
                                 c.payload.capacity() + c.parts.capacity() * sizeof(dictionary_part) +
                                 c.coded.capacity() * sizeof(u64) + c.binding_bytes();
    std::printf(",\"bind_decoders\":%s,\"column_object_bytes\":%zu,\"binding_bytes\":%zu,"
                "\"retained_bytes\":%zu",
                c.options.bind_decoders ? "true" : "false", sizeof(column), c.binding_bytes(),
                retained);
    std::printf(",\"coded_kinds\":{\"constant\":%zu,\"runs\":%zu,\"packed\":%zu,\"delta\":%zu,"
                "\"patched\":%zu,\"delta_patched\":%zu}",
                coded_counts[0], coded_counts[1], coded_counts[2], coded_counts[3], coded_counts[4],
                coded_counts[5]);
    const std::size_t n = c.blocks.size();
    struct shape { const char *name; block_mask mask; };
    std::vector<shape> shapes{{"all", block_mask(n)}, {"clustered50", block_mask(n)},
                              {"sparse10", block_mask(n)}, {"none", block_mask(n)}};
    for (std::size_t b = 0; b < n; ++b)
        shapes[0].mask.set(b);
    for (std::size_t b = 0; b < n / 2; ++b)
        shapes[1].mask.set(b);
    for (std::size_t b = 0; b < n; b += 10)
        shapes[2].mask.set(b);
    for (const auto &s : shapes) {
        const u128 es = encoded_sum(c, s.mask), ps = plain_sum(p, s.mask);
        if (encoded_sum_narrow(c, s.mask) != ps) {
            std::fprintf(stderr, "%s %s: narrow sum mismatch\n", name, s.name);
            std::exit(1);
        }
        const u64 ec = encoded_count_ge(c, s.mask, threshold), pc = plain_count_ge(p, s.mask, threshold);
        if (catalogue_sum(c, s.mask) != ps || catalogue_count_ge(c, s.mask, threshold) != pc) {
            std::fprintf(stderr, "%s %s: catalogue arm mismatch\n", name, s.name);
            std::exit(1);
        }
        if (es != ps || ec != pc) {
            std::fprintf(stderr, "%s %s: encoded/plain mismatch\n", name, s.name);
            std::exit(1);
        }
        std::vector<arm> arms{
            {"encoded_sum", [&] { consume(encoded_sum(c, s.mask)); }, {}},
            {"encoded_narrow_sum", [&] { consume(encoded_sum_narrow(c, s.mask)); }, {}},
            {"plain_sum", [&] { consume(plain_sum(p, s.mask)); }, {}},
            {"catalogue_sum", [&] { consume(catalogue_sum(c, s.mask)); }, {}},
            {"encoded_count", [&] { sink = encoded_count_ge(c, s.mask, threshold); }, {}},
            {"catalogue_count", [&] { sink = catalogue_count_ge(c, s.mask, threshold); }, {}},
            {"plain_count", [&] { sink = plain_count_ge(p, s.mask, threshold); }, {}}};
        time_arms(arms);
        std::printf(",\"%s\":{\"physical_rows\":%zu,\"selected_rows\":%zu,\"sum\":\"%s\","
                    "\"count_ge\":%llu",
                    s.name, c.rows, s.mask.popcount() * block_rows, u128_text(es).c_str(),
                    static_cast<unsigned long long>(ec));
        for (auto &a : arms) {
            // Raw per-trial times in trial order, so paired ratios and drift
            // can be reconstructed; summary statistics are derived from them.
            std::printf(",\"%s_ns\":{\"trials\":[", a.name);
            for (std::size_t t = 0; t < a.ns.size(); ++t)
                std::printf("%s%.0f", t ? "," : "", a.ns[t]);
            auto sorted = a.ns;
            std::sort(sorted.begin(), sorted.end());
            std::printf("],\"min\":%.0f,\"median\":%.0f,\"max\":%.0f}", sorted.front(),
                        sorted[sorted.size() / 2], sorted.back());
        }
        std::printf("}");
    }
    std::printf("}\n");
}

}  // namespace

#ifndef FRAME_ENCODING_BODY_NO_MAIN
int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s NAME FILE.u64 "
                     "[--checkpoints=native|packed|every64|none] [--delta-exceptions] "
                     "[--dictionary=none|sorted|frequency] [--dictionary-every-part] "
                     "[--no-bindings]\n",
                     argv[0]);
        return 2;
    }
    encode_options options;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--delta-exceptions") == 0)
            options.delta_exceptions = true;
        else if (std::strcmp(argv[i], "--dictionary=none") == 0)
            options.dictionary = dictionary_order::none;
        else if (std::strcmp(argv[i], "--dictionary=sorted") == 0)
            options.dictionary = dictionary_order::sorted;
        else if (std::strcmp(argv[i], "--dictionary=frequency") == 0)
            options.dictionary = dictionary_order::frequency;
        else if (std::strcmp(argv[i], "--dictionary-every-part") == 0)
            options.dictionary_every_part = true;
        else if (std::strcmp(argv[i], "--no-bindings") == 0)
            options.bind_decoders = false;
        else if (std::strcmp(argv[i], "--checkpoints=native") == 0)
            options.delta_checkpoints = checkpoints::native;
        else if (std::strcmp(argv[i], "--checkpoints=packed") == 0)
            options.delta_checkpoints = checkpoints::packed;
        else if (std::strcmp(argv[i], "--checkpoints=every64") == 0)
            options.delta_checkpoints = checkpoints::every64;
        else if (std::strcmp(argv[i], "--checkpoints=none") == 0)
            options.delta_checkpoints = checkpoints::none;
        else
            return 2;
    }
    std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
    if (!in) {
        std::fprintf(stderr, "%s: cannot open\n", argv[2]);
        return 2;
    }
    const auto end = in.tellg();
    if (end <= 0 || end % 8 != 0 || (end / 8) % block_rows != 0) {
        std::fprintf(stderr, "%s: need a non-empty whole number of 256-row u64 blocks\n",
                     argv[2]);
        return 2;
    }
    const auto bytes = static_cast<std::size_t>(end);
    std::vector<u64> values(bytes / 8);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char *>(values.data()), static_cast<std::streamsize>(bytes))) {
        std::fprintf(stderr, "%s: short read\n", argv[2]);
        return 2;
    }
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    std::vector<u64> sorted(values);
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const u64 threshold = sorted[sorted.size() / 2];
    const u64 span = *hi - *lo;
    if (span <= 0xff)
        run<std::uint8_t>(argv[1], values, *lo, threshold, options);
    else if (span <= 0xffff)
        run<std::uint16_t>(argv[1], values, *lo, threshold, options);
    else if (span <= 0xffffffffu)
        run<std::uint32_t>(argv[1], values, *lo, threshold, options);
    else
        run<u64>(argv[1], values, *lo, threshold, options);
}
#endif  // FRAME_ENCODING_BODY_NO_MAIN

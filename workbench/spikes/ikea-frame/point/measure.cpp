// Prepared independent point reads on complete real images and matched deltas.
#include "point_read.h"
#include "compact_read.h"
#include <array>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>

namespace {
namespace fe = frame_encoding;
using fe::u64;
using fe::u128;
using Clock = std::chrono::steady_clock;
constexpr unsigned query_count = 4096;

void need(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
u64 elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
std::vector<u64> load(const char *path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    need(bool(input), "cannot open input");
    const std::streamoff bytes = input.tellg();
    need(bytes > 0 && bytes % (256 * sizeof(u64)) == 0, "expected nonempty complete u64 blocks");
    std::vector<u64> values(static_cast<std::size_t>(bytes) / sizeof(u64));
    input.seekg(0);
    need(bool(input.read(reinterpret_cast<char *>(values.data()), bytes)), "short input");
    return values;
}

struct footprint {
    std::size_t image{}, image_capacity{}, prepared{}, owner{};
};
template <class T> std::size_t capacity_bytes(const std::vector<T> &v) {
    return v.capacity() * sizeof(T);
}
template <class Reader> footprint bytes(const fe::column &c, const Reader &reader) {
    footprint f;
    f.image = c.descriptor_bytes() + c.payload.size() + c.run_geometry_bytes();
    f.image_capacity = capacity_bytes(c.blocks) + c.payload.capacity();
    f.prepared = capacity_bytes(c.runs) + capacity_bytes(c.decoders) + capacity_bytes(c.d8) +
                 capacity_bytes(c.d16) + capacity_bytes(c.d32) + capacity_bytes(c.exceptions) +
                 reader.prepared_bytes();
    f.owner = sizeof(c);
    for (const auto &r : c.runs) if (r) {
        f.image_capacity += r->directory.capacity() + r->lengths.capacity() + r->control.capacity();
        f.owner += sizeof(*r);
    }
    return f;
}

template <bool Selected, class Read>
u128 sum_points(std::span<const std::size_t> rows, const fe::block_mask &mask, Read read) {
    u128 a = 0, b = 0, c = 0, d = 0;
    const auto one = [&](std::size_t row) -> u64 {
        if constexpr (Selected) {
            const auto block = row / fe::block_rows;
            if (!((mask.words[block / 64] >> (block % 64)) & 1)) return 0;
        }
        return read(row);
    };
    std::size_t q = 0;
    for (; q + 4 <= rows.size(); q += 4) {
        a += one(rows[q]);
        b += one(rows[q + 1]);
        c += one(rows[q + 2]);
        d += one(rows[q + 3]);
    }
    for (; q < rows.size(); ++q) a += one(rows[q]);
    return a + b + c + d;
}
template <class Read>
u128 sum_points(std::span<const std::size_t> rows, const fe::block_mask &mask,
                bool selected, Read read) {
    return selected ? sum_points<true>(rows, mask, read) : sum_points<false>(rows, mask, read);
}

struct arm {
    std::string name;
    footprint storage;
    std::array<std::size_t, 5> kinds{};
    u64 construct_ns{}, prepare_ns{};
    std::function<u128(std::span<const std::size_t>, const fe::block_mask &, bool)> read;
};

struct native_for {
    u64 base;
    std::vector<std::uint8_t> v8;
    std::vector<std::uint16_t> v16;
    std::vector<std::uint32_t> v32;
    std::vector<u64> v64;
    template <class T> void fill(std::vector<T> &out, std::span<const u64> in) {
        out.reserve(in.size());
        for (u64 value : in) out.push_back(static_cast<T>(value - base));
    }
    explicit native_for(std::span<const u64> in) {
        const auto [lo, hi] = std::minmax_element(in.begin(), in.end());
        base = *lo;
        const auto k = fe::width_of(*hi - *lo);
        if (k <= 8) fill(v8, in);
        else if (k <= 16) fill(v16, in);
        else if (k <= 32) fill(v32, in);
        else fill(v64, in);
    }
    std::size_t bytes() const {
        return capacity_bytes(v8) + capacity_bytes(v16) + capacity_bytes(v32) + capacity_bytes(v64);
    }
    template <class F> void with_values(F &&f) const {
        if (!v8.empty()) f(v8);
        else if (!v16.empty()) f(v16);
        else if (!v32.empty()) f(v32);
        else f(v64);
    }
};

void experiment(std::vector<u64> &values, unsigned trials, unsigned repeat, bool reverse) {
    std::array<fe::column, 8> images;
    std::vector<frame_points::point_reader> readers;
    readers.reserve(images.size());
    std::vector<std::unique_ptr<frame_points::compact_image>> lean;
    std::vector<frame_points::compact_reader> lean_readers;
    lean.reserve(images.size());
    lean_readers.reserve(images.size());
    std::vector<arm> arms;
    arms.reserve(18);
    for (unsigned i = 0; i < images.size(); ++i) {
        const auto mode = static_cast<fe::checkpoints>(i % 4);
        const bool compact = i >= 4;
        const auto start = Clock::now();
        images[i] = fe::encode(values, {compact, mode});
        const auto constructed = elapsed(start);
        const auto prepare = Clock::now();
        readers.emplace_back(images[i]);
        const auto prepared = elapsed(prepare);
        arm a;
        a.name = std::string(compact ? "compact_" : "u8ends_") + fe::checkpoint_name(mode);
        a.storage = bytes(images[i], readers.back());
        for (const auto &d : images[i].blocks) {
            const auto kind = static_cast<unsigned>(d.type);
            need(kind < a.kinds.size(), "new material kind needs explicit point/memory qualification");
            ++a.kinds[kind];
        }
        a.construct_ns = constructed;
        a.prepare_ns = prepared;
        const auto *reader = &readers.back();
        a.read = [reader](auto rows, const auto &mask, bool selected) {
            return sum_points(rows, mask, selected, [&](auto row) { return reader->get(row); });
        };
        arms.push_back(std::move(a));

        const auto lean_start = Clock::now();
        auto candidate = fe::encode(values, {compact, mode});
        const auto lean_constructed = elapsed(lean_start);
        const auto lean_prepare = Clock::now();
        lean.push_back(std::make_unique<frame_points::compact_image>(std::move(candidate)));
        need(lean.back()->body.payload == images[i].payload, "prepared comparison changed body bytes");
        lean_readers.emplace_back(*lean.back());
        const auto lean_prepared = elapsed(lean_prepare);
        arm shared;
        shared.name = "shared_" + arms.back().name;
        shared.storage = bytes(lean.back()->body, lean_readers.back());
        shared.storage.owner += sizeof(frame_points::compact_image) - sizeof(fe::column);
        shared.kinds = arms.back().kinds;
        shared.construct_ns = lean_constructed;
        shared.prepare_ns = lean_prepared;
        const auto *lean_reader = &lean_readers.back();
        shared.read = [lean_reader](auto rows, const auto &mask, bool selected) {
            return sum_points(rows, mask, selected, [&](auto row) { return lean_reader->get(row); });
        };
        arms.push_back(std::move(shared));
    }
    arm raw;
    raw.name = "native_u64";
    raw.storage = {values.size() * 8, values.capacity() * 8, 0, sizeof(values)};
    raw.read = [&values](auto rows, const auto &mask, bool selected) {
        return sum_points(rows, mask, selected, [&](auto row) {
            assert(row < values.size());
            return values[row];
        });
    };
    arms.push_back(std::move(raw));
    const auto begin_for = Clock::now();
    native_for plain(values);
    const auto for_ns = elapsed(begin_for);
    plain.with_values([&](const auto &stored) {
        arm a;
        a.name = "native_global_for";
        a.construct_ns = for_ns;
        a.storage = {plain.bytes() + 8, plain.bytes() + 8, 0, sizeof(plain) - 8};
        a.read = [&stored, &plain](auto rows, const auto &mask, bool selected) {
            return sum_points(rows, mask, selected, [&](auto row) {
                assert(row < stored.size());
                return plain.base + stored[row];
            });
        };
        arms.push_back(std::move(a));
    });
    if (reverse) std::reverse(arms.begin(), arms.end());

    std::vector<std::size_t> common;
    for (std::size_t b = 0; b < images[0].blocks.size(); ++b) {
        bool matches = true;
        for (const auto &image : images)
            matches &= image.blocks[b].type == fe::kind::delta &&
                       image.blocks[b].width == images[0].blocks[b].width;
        if (matches) {
            const auto &first = images[0].blocks[b];
            const auto bytes = fe::plane_bytes(first.width, fe::block_rows) + 8;
            for (const auto &image : images) {
                const auto &current = image.blocks[b];
                need(current.base == first.base &&
                     std::equal(images[0].payload.begin() + first.offset,
                                images[0].payload.begin() + first.offset + bytes,
                                image.payload.begin() + current.offset),
                     "common delta body changed beyond checkpoints");
            }
            common.push_back(b);
        }
    }
    std::mt19937 random(713);
    for (unsigned domain = 0; domain < 2; ++domain) {
        if (domain && common.empty()) continue;
        std::array<std::size_t, query_count> rows;
        for (auto &row : rows)
            row = domain ? common[random() % common.size()] * 256 + random() % 256
                         : random() % values.size();
        for (unsigned selection = 0; selection < 4; ++selection) {
            fe::block_mask mask(images[0].blocks.size());
            for (std::size_t b = 0; b < mask.blocks; ++b)
                if (!selection || (selection == 2 && b % 2) ||
                    (selection == 3 && b / 16 % 2)) mask.set(b);
            u128 expected = 0;
            unsigned selected_count = 0;
            for (auto row : rows)
                if ((mask.words[row / 256 / 64] >> (row / 256 % 64)) & 1) {
                    expected += values[row];
                    ++selected_count;
                }
            for (const auto &a : arms)
                need(a.read(rows, mask, selection != 0) == expected, "point result differs from input");
            for (unsigned trial = 0; trial < trials; ++trial) {
                for (unsigned position = 0; position < arms.size(); ++position) {
                    const auto &a = arms[(position + trial) % arms.size()];
                    fe::consume(a.read(rows, mask, selection != 0)); // warm the actual requested work
                    const auto start = Clock::now();
                    for (unsigned r = 0; r < repeat; ++r) {
                        asm volatile("" ::: "memory");
                        fe::consume(a.read(rows, mask, selection != 0));
                    }
                    const auto ns = elapsed(start);
                    std::printf("{\"arm\":\"%s\",\"domain\":\"%s\",\"mask\":%u,\"trial\":%u,"
                        "\"rows\":%zu,\"common_delta_blocks\":%zu,\"elapsed_ns\":%llu,"
                        "\"requests\":%llu,\"selected_points\":%llu,\"image_bytes\":%zu,"
                        "\"image_capacity_bytes\":%zu,\"prepared_bytes\":%zu,\"owner_bytes\":%zu,"
                        "\"shared_catalogue_bytes_once_per_process\":%zu,"
                        "\"construct_observation_ns\":%llu,\"point_prepare_observation_ns\":%llu,"
                        "\"kinds\":[%zu,%zu,%zu,%zu,%zu],\"answer_lo\":\"%llu\",\"answer_hi\":\"%llu\"}\n",
                        a.name.c_str(), domain ? "common_delta" : "whole_image", selection, trial,
                        values.size(), common.size(), static_cast<unsigned long long>(ns),
                        static_cast<unsigned long long>(u64(repeat) * query_count),
                        static_cast<unsigned long long>(u64(repeat) * selected_count),
                        a.storage.image, a.storage.image_capacity, a.storage.prepared, a.storage.owner,
                        frame_points::shared_kernel_bytes(),
                        static_cast<unsigned long long>(a.construct_ns),
                        static_cast<unsigned long long>(a.prepare_ns), a.kinds[0], a.kinds[1],
                        a.kinds[2], a.kinds[3], a.kinds[4],
                        static_cast<unsigned long long>(expected),
                        static_cast<unsigned long long>(expected >> 64));
                }
            }
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        need(argc >= 2 && argc <= 5, "usage: frame_point_measure INPUT.u64 [TRIALS=10] [REPEAT=16] [REVERSE=0]");
        const auto number = [&](unsigned index, unsigned fallback) {
            if (index >= static_cast<unsigned>(argc)) return fallback;
            char *end;
            const auto n = std::strtoul(argv[index], &end, 10);
            need(*end == 0 && n <= 1000, "invalid numeric argument");
            return static_cast<unsigned>(n);
        };
        const auto trials = number(2, 10), repeat = number(3, 16), reverse = number(4, 0);
        need(trials && repeat && reverse <= 1, "invalid work count/order");
        auto values = load(argv[1]);
        experiment(values, trials, repeat, reverse);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

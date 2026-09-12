#include "native.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <vector>

using namespace tuple_runtime;
void check_tuple_composition();
namespace {
void require(bool condition, const char* what, unsigned width, unsigned trial) {
    if (!condition) {
        std::fprintf(stderr, "%s: tuple_bytes=%u trial=%u\n", what, width, trial);
        std::abort();
    }
}
}
int main() {
    std::mt19937 random(0x143);
    unsigned reads = 0, writes = 0, native_writes = 0;
    for (unsigned width = 1; width <= 64; ++width) {
        std::vector<code> codes;
        for (unsigned offset = 0; offset < width; ++offset) {
            unsigned low = 1 + random() % 7;
            codes.push_back({byte(offset), 0, byte(low)});
            codes.push_back({byte(offset), byte(low), byte(8 - low)});
        }
        schema s{width, codes};
        for (unsigned trial = 0; trial < 96; ++trial) {
            mapping m;
            m.fill(255);
            std::vector<unsigned> ranks(codes.size());
            std::iota(ranks.begin(), ranks.end(), 0);
            std::shuffle(ranks.begin(), ranks.end(), random);
            for (unsigned i = 0; i < std::min<std::size_t>(64, ranks.size()); ++i)
                if (trial < 2 || random() % 4) m[i] = ranks[i];
            if (trial == 0 || trial == 1) {
                // Independent complementary packet trials. The compound check
                // below covers their whole-operation failure boundary.
                for (unsigned i = 0; i < 64; ++i)
                    m[i] = i < width ? byte(2 * i + trial) : 255;
            }
            const auto rp = prepare_read(s, m);
            const auto wp = prepare_write(s, m);
            require(bool(rp) && bool(wp), "binding", width, trial);
            // Exact-size heap allocation lets ASan catch final-chunk overreads.
            std::vector<byte> row(width);
            for (auto& v : row) v = random();
            const auto expected = reference_read(s, m, row.data());
            std::array<byte, 64> got;
            store_packet(got.data(), read_native(*rp, row.data()));
            require(got == expected, "native read", width, trial);
            require(read_scalar(*rp, row.data()) == expected, "scalar read", width, trial);
            std::uint64_t expected8;
            std::memcpy(&expected8, expected.data(), 8);
            require(read8(*rp, row.data()) == expected8, "scalar eight", width, trial);
            ++reads;

            std::array<byte, 64> input{};
            for (unsigned i = 0; i < 64; ++i)
                input[i] = m[i] == 255 ? byte(random()) : byte(random() & ((1u << codes[m[i]].width) - 1));
            auto reference = row, scalar = row;
            reference_write(s, m, reference.data(), input);
            std::uint64_t effects = 0x85;
            require(bool(write_scalar(*wp, scalar.data(), input, effects)), "scalar write admission", width, trial);
            require(scalar == reference && effects == (0x85 | wp->issued_writes), "scalar write/effects", width, trial);
            ++writes;
            if (wp->dense_native) {
                auto native = row;
                effects = 0x85;
                require(write_native(*wp, native.data(), load_packet(input.data()), effects) == mutation_status::ok,
                        "native write admission", width, trial);
                require(native == reference && effects == (0x85 | wp->issued_writes), "native write/effects", width, trial);
                ++native_writes;
            }
            for (unsigned i = 0; i < 64; ++i) {
                if (m[i] == 255) continue;
                auto invalid = input;
                invalid[i] |= 1u << codes[m[i]].width;
                auto unchanged = row;
                effects = 0x85;
                require(!write_scalar(*wp, unchanged.data(), invalid, effects) && unchanged == row && effects == 0x85,
                        "scalar invalid value atomicity", width, trial);
                if (wp->dense_native)
                    require(write_native(*wp, unchanged.data(), load_packet(invalid.data()), effects) == mutation_status::value &&
                            unchanged == row && effects == 0x85, "native invalid value atomicity", width, trial);
                break;
            }
            // Duplication is legal for readers; writers currently reject it.
            if (m[0] != 255) {
                m[63] = m[0];
                auto duplicated = prepare_read(s, m);
                store_packet(got.data(), read_native(*duplicated, row.data()));
                require(got == reference_read(s, m, row.data()), "duplicate reader", width, trial);
                require(!prepare_write(s, m), "duplicate writer rejection", width, trial);
            }
        }
    }
    std::array<code, 2> overlap{{{0, 0, 5}, {0, 4, 4}}};
    mapping m; m.fill(255);
    require(!prepare_read({1, overlap}, m), "overlapping schema rejected", 1, 0);
    overlap[1] = {0, 5, 3};
    m[0] = 2;
    require(!prepare_read({1, overlap}, m), "invalid rank rejected", 1, 0);
    m.fill(255);
    auto empty = prepare_read({1, overlap}, m);
    std::array<byte, 64> zero{};
    std::array<byte, 64> got;
    store_packet(got.data(), read_native(*empty, nullptr));
    require(got == zero, "empty map no reads", 1, 0);
    std::printf("%u schemas/maps: reads, scalar8, %u scalar writes, %u dense native writes; holes, duplicate reads, rejections and effects passed\n",
                reads, writes, native_writes);
    check_tuple_composition();
}

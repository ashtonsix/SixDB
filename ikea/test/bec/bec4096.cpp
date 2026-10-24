// Bec4096 wire, access and mutation checks against an independent reference.
#include "bec4096_reference.h"
#include <ikea/bec/bec4096.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
using namespace ikea::bec4096;
using ikea::bitset256;
using ikea::bitset4096;
namespace reference = bec4096_reference;
namespace inputs = bec4096_inputs;
unsigned failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                             \
            if (++failures > 20)                                                                   \
                std::exit(1);                                                                      \
        }                                                                                          \
    } while (0)

// The body occupies the last bytes before an inaccessible page.
struct guarded {
    std::size_t page = std::size_t(sysconf(_SC_PAGESIZE));
    byte *base;
    guarded() {
        base = static_cast<byte *>(
            mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        mprotect(base + page, page, PROT_NONE);
    }
    ~guarded() { munmap(base, 2 * page); }
    std::span<byte> place(std::span<const byte> body) {
        auto *p = base + page - body.size();
        std::memcpy(p, body.data(), body.size());
        return {p, body.size()};
    }
};

void check_value(const bitset4096 &x, guarded &guard) {
    const unsigned population = x.count();
    const auto expected = reference::encode(x);
    // Exact write into a canary-filled destination at an odd offset.
    std::vector<byte> storage(max_bytes + 64, byte{0xa5});
    view target{storage};
    std::array<ikea::journal::entry, 1> records;
    ikea::journal effects{records};
    auto written = encode(x, population, target, 13, effects);
    CHECK(written && *written == expected.size());
    if (!written)
        return;
    auto prepared = encoding::make(x, population);
    CHECK(prepared && prepared->bytes() == *written &&
          std::equal(expected.begin(), expected.end(), prepared->body().begin()));
    if (!std::equal(expected.begin(), expected.end(), storage.begin() + 13)) {
        std::size_t at = 0;
        while (expected[at] == storage[13 + at])
            ++at;
        std::printf("wire mismatch: population %u, %zu bytes, first difference at byte %zu;"
                    " blocks:", population, expected.size(), at);
        for (unsigned k = 0; k < 16; ++k)
            std::printf(" %u", x.block(k).count());
        std::printf("\n");
        CHECK(false);
    }
    for (std::size_t i = 0; i < storage.size(); ++i)
        if (i < 13 || i >= 13 + *written)
            CHECK(storage[i] == byte{0xa5});
    CHECK(effects.used == (*written ? 1u : 0u));
    if (*written)
        CHECK(records[0].offset == 13 && records[0].size == *written && records[0].source == &target);
    CHECK(validate(expected, population));
    CHECK(*written <= max_bytes);

    // Exact span before a guard page, and a span with a readable suffix.
    const auto exact = guard.place(expected);
    std::vector<byte> padded(expected.begin(), expected.end());
    padded.resize(expected.size() + 64);
    for (auto storage_span : {std::span<const byte>(exact), std::span<const byte>(padded)}) {
        auto source = bind_reader(storage_span, unsigned(expected.size()), population);
        CHECK(source);
        if (!source)
            continue;
        bitset4096 all;
        decode(*source, all);
        CHECK(all == x);
        std::array<std::uint8_t, 512> all_counts;
        counts(*source, all_counts);
        for (unsigned j = 0; j < 512; ++j)
            CHECK(all_counts[j] == std::popcount(unsigned(x.words[j / 8] >> (8 * (j % 8)) & 255)));
        for (unsigned k = 0; k < 16; ++k) {
            bitset256 one;
            decode(*source, k, one);
            CHECK(one == x.block(k));
            CHECK(source->population(k) == x.block(k).count());
            std::array<std::uint8_t, 32> c;
            counts(*source, k, c);
            for (unsigned j = 0; j < 32; ++j)
                CHECK(c[j] == std::popcount(unsigned(x.words[4 * k + j / 8] >> (8 * (j % 8)) & 255)));
        }
        unsigned prefix = 0;
        for (unsigned p = 0; p <= 4096; ++p) {
            if (p % 7 == 0 || p % 256 < 2 || p == 4096)
                CHECK(rank(*source, p) == prefix);
            if (p < 4096)
                prefix += x.contains(p);
        }
        CHECK(ikea::bec4096::population(*source, 100, 3000) == rank(*source, 3100) - rank(*source, 100));
        auto fast = bind_reader_unchecked(storage_span, unsigned(expected.size()), population);
        CHECK(fast.frame().population == source->frame().population &&
              fast.frame().start == source->frame().start);
    }

    // Framing corruptions are rejected; arbitrary flips must not crash validation.
    // Another population can describe other valid content, as with Bec256.
    if (!expected.empty()) {
        CHECK(!validate(std::span(expected).first(expected.size() - 1), population));
        auto longer = expected;
        longer.push_back(byte{0});
        CHECK(!validate(longer, population));
    }
    std::mt19937_64 rng(population);
    for (unsigned t = 0; t < 8 && !expected.empty(); ++t) {
        auto damaged = expected;
        damaged[rng() % damaged.size()] ^= byte(1u << (rng() % 8));
        if (auto ok = validate(damaged, population); ok) {
            auto source = bind_reader(damaged, unsigned(damaged.size()), population);
            CHECK(source);
            bitset4096 y;
            decode(*source, y);
            CHECK(y.count() == population && reference::encode(y) == damaged);
        }
    }
}

void check_rejections() {
    auto x = inputs::make(inputs::shape::random, 1);
    std::vector<byte> storage(max_bytes, byte{0x5a});
    view target{storage};
    std::array<ikea::journal::entry, 1> records;
    ikea::journal effects{records};
    CHECK(encode(x, x.count() + 1, target, 0, effects).error() == error::population);
    CHECK(encode(x, x.count(), target, max_bytes - 3, effects).error() == error::capacity);
    ikea::journal none{std::span<ikea::journal::entry>{}};
    CHECK(encode(x, x.count(), target, 0, none).error() == error::effects);
    for (auto b : storage)
        CHECK(b == byte{0x5a});
    CHECK(effects.used == 0);
    bitset4096 empty;
    CHECK(encode(empty, 0, target, max_bytes, effects) == 0u);
    // A prepared candidate admits and writes like the immediate operation.
    auto prepared = encoding::make(x, x.count());
    CHECK(prepared && prepared->admit_write(target, 0, effects));
    CHECK(!prepared->admit_write(target, max_bytes - 3, effects));
    CHECK(prepared->write(target, 0, effects) && effects.used == 1);
    CHECK(encoding::make(x, x.count() + 1).error() == error::population);
    CHECK(validate({}, 0) && validate({}, 4096) && !validate({}, 4097));
}

void check_bounds() {
    for (unsigned c = 0; c <= 256; ++c) {
        auto [lo, hi] = reference::extremes(256, c);
        CHECK(detail::min_bits[c] == lo && detail::max_bits[c] == hi);
    }
}
} // namespace

int main() {
    check_bounds();
    check_rejections();
    guarded guard;
    unsigned cases = 0;
    for (auto s : inputs::all)
        for (unsigned seed = 0; seed < 24; ++seed, ++cases)
            check_value(inputs::make(s, seed), guard);
    // Single positions and single holes in every block.
    for (unsigned p = 0; p < 4096; p += 37, cases += 2) {
        bitset4096 one;
        one.set(p);
        check_value(one, guard);
        bitset4096 hole;
        for (auto &w : hole.words)
            w = ~std::uint64_t{0};
        hole.set(p, false);
        check_value(hole, guard);
    }
    std::printf("Bec4096: %u values against the independent reference, exact guard boundaries, %u failures\n", cases, failures);
    return failures != 0;
}

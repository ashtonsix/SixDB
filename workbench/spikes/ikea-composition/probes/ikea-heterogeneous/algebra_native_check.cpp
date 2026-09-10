#include "algebra_native.h"
#include "../ikea-blocks/codec.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace {
using namespace ikea::heterogeneous::algebra;
using Plain = std::array<std::uint8_t, 32>;
using Values = std::array<Plain, 4>; // A0, A1, B0, B1 in logical order.
using Inputs = std::array<const std::uint8_t*, 4>;

unsigned population(const Plain& bits) {
    unsigned count = 0;
    for (auto byte : bits) count += std::popcount(byte);
    return count;
}
std::uint64_t random_word(std::uint64_t& state) {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * UINT64_C(2685821657736338717);
}
Plain scattered(unsigned pop, std::uint64_t& state) {
    std::array<unsigned, 256> order;
    for (unsigned i = 0; i < 256; ++i) order[i] = i;
    for (unsigned i = 255; i; --i)
        std::swap(order[i], order[random_word(state) % (i + 1)]);
    Plain value{};
    for (unsigned i = 0; i < pop; ++i)
        value[order[i] / 8] |= std::uint8_t(1u << (order[i] % 8));
    return value;
}
template<bool Union> Plain oracle(const Plain& a, const Plain& b) {
    Plain result;
    for (unsigned i = 0; i < 32; ++i)
        result[i] = Union ? a[i] | b[i] : a[i] & b[i];
    return result;
}

template<bool Union>
bool exercise(const Values& values, const Inputs& plain, const Inputs& body,
              std::uint8_t* out_a, std::uint8_t* out_b) {
    const auto expected_a = oracle<Union>(values[0], values[2]);
    const auto expected_b = oracle<Union>(values[1], values[3]);
    const auto one = [&](Native1 value) {
        store1(out_a, value);
        return std::memcmp(out_a, expected_a.data(), 32) == 0;
    };
    const auto two = [&](Native2 value) {
        store2(out_a, out_b, value);
        return std::memcmp(out_a, expected_a.data(), 32) == 0 &&
               std::memcmp(out_b, expected_b.data(), 32) == 0;
    };
    const auto ap = load_plain1(plain[0]), bp = load_plain1(plain[2]);
    const auto ac = decode_bec1(body[0], population(values[0]));
    const auto bc = decode_bec1(body[2], population(values[2]));
    if (!one(combine1<Union>(ap, bp)) || !one(combine1<Union>(ac, bp)) ||
        !one(combine1<Union>(ap, bc)) || !one(combine1<Union>(ac, bc))) return false;

    const auto app = load_plain2(plain[0], plain[1]);
    const auto bpp = load_plain2(plain[2], plain[3]);
    const auto acc = decode_bec2(body[0], population(values[0]),
                                body[1], population(values[1]));
    const auto bcc = decode_bec2(body[2], population(values[2]),
                                body[3], population(values[3]));
    if (!two(combine2<Union>(app, bpp)) || !two(combine2<Union>(acc, bpp)) ||
        !two(combine2<Union>(app, bcc)) || !two(combine2<Union>(acc, bcc))) return false;

    // Two sources for one isolated logical slice, using both decoder domains.
    if (!one(combine_halves<Union>(load_plain2(plain[0], plain[2]))) ||
        !one(combine_halves<Union>(decode_bec2(
            body[0], population(values[0]), body[2], population(values[2]))))) return false;

    // Reverse both slice orders, including destination addresses.
    store2(out_b, out_a, combine2<Union>(
        decode_bec2(body[1], population(values[1]), body[0], population(values[0])),
        load_plain2(plain[3], plain[2])));
    return std::memcmp(out_a, expected_a.data(), 32) == 0 &&
           std::memcmp(out_b, expected_b.data(), 32) == 0;
}

struct Cases {
    unsigned count = 0;
    std::uint64_t state = UINT64_C(0xbec65536256);
    bool run(const Values& values) {
        std::array<std::array<std::uint8_t, 96>, 4> plain_bytes;
        std::array<std::array<std::uint8_t, 128>, 4> bodies;
        std::array<std::array<std::uint8_t, 128>, 2> output;
        Inputs plain, body;
        for (unsigned i = 0; i < 4; ++i) {
            // Physical order differs from logical order and changes by case.
            const auto physical = (3 * i + count) % 4;
            const auto offset = (count * 17 + i * 7) % 64;
            plain[i] = plain_bytes[physical].data() + offset;
            std::copy(values[i].begin(), values[i].end(), plain_bytes[physical].data() + offset);
            bodies[physical].fill(0xa5);
            body[i] = bodies[physical].data() + offset;
            ikea_probe::encode_reference(values[i].data(), bodies[physical].data() + offset);
        }
        output[0].fill(0x5a);
        output[1].fill(0x5a);
        const auto offset_a = count % 64, offset_b = (count * 13 + 5) % 64;
        auto* out_a = output[0].data() + offset_a;
        auto* out_b = output[1].data() + offset_b;
        if (!exercise<false>(values, plain, body, out_a, out_b) ||
            !exercise<true>(values, plain, body, out_a, out_b)) return false;
        for (unsigned i = 0; i < 128; ++i) {
            if ((i < offset_a || i >= offset_a + 32) && output[0][i] != 0x5a) return false;
            if ((i < offset_b || i >= offset_b + 32) && output[1][i] != 0x5a) return false;
        }
        ++count;
        return true;
    }
};

class GuardedPage {
    std::uint8_t* mapping_ = nullptr;
    std::size_t page_ = 0;
public:
    GuardedPage() {
        const auto page = sysconf(_SC_PAGESIZE);
        if (page <= 0) return;
        page_ = std::size_t(page);
        void* p = mmap(nullptr, 3 * page_, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return;
        mapping_ = static_cast<std::uint8_t*>(p);
        if (mprotect(mapping_ + page_, page_, PROT_READ | PROT_WRITE) != 0) {
            munmap(mapping_, 3 * page_);
            mapping_ = nullptr;
        }
    }
    ~GuardedPage() { if (mapping_) munmap(mapping_, 3 * page_); }
    GuardedPage(const GuardedPage&) = delete;
    GuardedPage& operator=(const GuardedPage&) = delete;
    explicit operator bool() const { return mapping_ != nullptr; }
    std::uint8_t* edge(unsigned bytes, bool end) const {
        return mapping_ + page_ + (end ? page_ - bytes : 0);
    }
};

bool guard_checks(std::uint64_t& state) {
    std::array<GuardedPage, 4> plain_pages, body_pages;
    std::array<GuardedPage, 2> out_pages;
    for (const auto& page : plain_pages) if (!page) return false;
    for (const auto& page : body_pages) if (!page) return false;
    for (const auto& page : out_pages) if (!page) return false;
    for (bool end : {false, true}) {
        Inputs plain, body;
        for (unsigned i = 0; i < 4; ++i) {
            plain[i] = plain_pages[i].edge(32, end);
            body[i] = body_pages[i].edge(64, end);
        }
        for (unsigned pop = 0; pop <= 256; ++pop) {
            Values values;
            for (unsigned i = 0; i < 4; ++i) {
                values[i] = scattered((pop * (2 * i + 1) + 37 * i) % 257, state);
                std::copy(values[i].begin(), values[i].end(), plain_pages[i].edge(32, end));
                std::fill_n(body_pages[i].edge(64, end), 64, 0xa5);
                ikea_probe::encode_reference(values[i].data(), body_pages[i].edge(64, end));
            }
            auto* a = out_pages[0].edge(32, end);
            auto* b = out_pages[1].edge(32, end);
            if (!exercise<false>(values, plain, body, a, b) ||
                !exercise<true>(values, plain, body, a, b)) return false;
        }
    }
    return true;
}
}

int check_algebra_native() {
    Cases cases;
    auto run = [&](const Values& values) {
        if (cases.run(values)) return true;
        std::fprintf(stderr, "Native slice algebra failed in case %u\n", cases.count);
        return false;
    };
    for (unsigned pop = 0; pop <= 256; ++pop)
        for (unsigned repeat = 0; repeat < 4; ++repeat) {
            Values values;
            for (unsigned i = 0; i < 4; ++i)
                values[i] = scattered((pop * (2 * i + 1) + 37 * i) % 257, cases.state);
            if (!run(values)) return 1;
        }
    for (unsigned byte = 0; byte < 32; ++byte)
        for (unsigned code = 0; code < 256; ++code) {
            Values values{};
            values[0][byte] = std::uint8_t(code);
            values[1][(byte + 17) % 32] = std::uint8_t(code ^ 0x96);
            for (unsigned i = 0; i < 32; ++i) {
                values[2][i] = std::uint8_t(~values[0][i]);
                values[3][i] = std::uint8_t(~values[1][i]);
            }
            if (!run(values)) return 1;
        }
    for (unsigned grain : {1, 2, 4, 8, 16, 32, 64, 128})
        for (unsigned phase = 0; phase < 2 * grain; ++phase) {
            Values values{};
            for (unsigned j = 0; j < 4; ++j)
                for (unsigned i = 0; i < 256; ++i)
                    values[j][i / 8] |= std::uint8_t(
                        (((i + phase + j) / grain) & 1u) << (i % 8));
            if (!run(values)) return 1;
        }
    if (!guard_checks(cases.state)) {
        std::fprintf(stderr, "Native slice algebra guarded-boundary check failed\n");
        return 1;
    }
    std::printf("Native slice algebra: %u structural groups, both operations, "
                "all populations, independent addresses and exact guards passed.\n", cases.count);
    return 0;
}

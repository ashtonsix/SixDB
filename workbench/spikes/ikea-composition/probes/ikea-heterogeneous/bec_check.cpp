#include "bec_region.h"
#include "../ikea-blocks/codec.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace {
using Plain = std::array<std::uint8_t, 32>;
using namespace ikea::heterogeneous;

unsigned population(const Plain& p) {
    unsigned result = 0;
    for (auto byte : p) result += std::popcount(byte);
    return result;
}
unsigned oracle(const Plain& p, const std::uint8_t* query) {
    unsigned result = 0;
    for (unsigned i = 0; i < 32; ++i)
        result += std::popcount(unsigned(p[i] & query[i]));
    return result;
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
    Plain result{};
    for (unsigned i = 0; i < pop; ++i)
        result[order[i] / 8] |= std::uint8_t(1u << (order[i] % 8));
    return result;
}

bool check_functions(const Plain& a, const Plain& b,
                     const std::uint8_t* body_a, const std::uint8_t* body_b,
                     const std::uint8_t* query64) {
    const unsigned pop_a = population(a), pop_b = population(b);
    const auto ca = oracle(a, query64), cb = oracle(b, query64 + 32);
    return bec_count1_inline(body_a, pop_a, query64) == ca &&
        ikea_heterogeneous_bec_count1(body_a, pop_a, query64) == ca &&
        bec_count1_inline(body_b, pop_b, query64 + 32) == cb &&
        ikea_heterogeneous_bec_count1(body_b, pop_b, query64 + 32) == cb &&
        bec_count2_inline(body_a, pop_a, body_b, pop_b, query64) == ca + cb &&
        ikea_heterogeneous_bec_count2(body_a, pop_a, body_b, pop_b, query64) == ca + cb;
}

struct Cases {
    std::uint64_t count = 0, state = UINT64_C(0x6bec25610);
    bool run(const Plain& a, const Plain& b) {
        alignas(64) std::array<std::uint8_t, 384> bodies;
        alignas(64) std::array<std::uint8_t, 128> queries;
        bodies.fill(0xa5);
        auto* body_a = bodies.data() + count % 64;
        auto* body_b = bodies.data() + 192 + (count * 13) % 64;
        ikea_probe::encode_reference(a.data(), body_a);
        ikea_probe::encode_reference(b.data(), body_b);
        auto* query = queries.data() + (count * 17) % 64;
        for (unsigned mode = 0; mode < 4; ++mode) {
            for (unsigned j = 0; j < 64; ++j)
                query[j] = mode == 0 ? 0 : mode == 1 ? 255 :
                    mode == 2 ? std::uint8_t(random_word(state)) :
                    std::uint8_t(j < 32 ? ~a[j] : b[j - 32]);
            if (!check_functions(a, b, body_a, body_b, query)) return false;
            // Reverse both coordinate blocks, independently of physical order.
            for (unsigned j = 0; j < 32; ++j) std::swap(query[j], query[j + 32]);
            if (!check_functions(b, a, body_b, body_a, query)) return false;
        }
        // Exact concatenation is also legal: the two 64-byte readable windows
        // may overlap. The owner provides slack once after the last body.
        const auto bits_a = ikea_probe::encode_reference(a.data(), bodies.data());
        const auto bytes_a = (bits_a + 7) / 8;
        ikea_probe::encode_reference(b.data(), bodies.data() + bytes_a);
        if (!check_functions(a, b, bodies.data(), bodies.data() + bytes_a, query)) return false;
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

bool check_guards(std::uint64_t& state) {
    GuardedPage ba, bb, q32, q64;
    if (!ba || !bb || !q32 || !q64) return false;
    for (bool end : {false, true}) {
        auto* body_a = ba.edge(64, end);
        auto* body_b = bb.edge(64, end);
        auto* query32 = q32.edge(32, end);
        auto* query64 = q64.edge(64, end);
        for (unsigned pop = 0; pop <= 256; ++pop) {
            const auto a = scattered(pop, state);
            const auto b = scattered((pop * 73 + 19) % 257, state);
            std::fill_n(body_a, 64, 0x5a);
            std::fill_n(body_b, 64, 0xa5);
            ikea_probe::encode_reference(a.data(), body_a);
            ikea_probe::encode_reference(b.data(), body_b);
            for (unsigned j = 0; j < 64; ++j) query64[j] = std::uint8_t(random_word(state));
            std::copy_n(query64, 32, query32);
            const auto expected = oracle(a, query32);
            if (bec_count1_inline(body_a, pop, query32) != expected ||
                ikea_heterogeneous_bec_count1(body_a, pop, query32) != expected ||
                !check_functions(a, b, body_a, body_b, query64)) return false;
        }
    }
    return true;
}
}

int check_bec_regions() {
    Cases cases;
    auto run = [&](const Plain& a, const Plain& b) {
        if (cases.run(a, b)) return true;
        std::fprintf(stderr, "BEC consumer mismatch in case %llu\n",
                     static_cast<unsigned long long>(cases.count));
        return false;
    };
    // Every external population, with scattered and boundary-crossing runs.
    for (unsigned pop = 0; pop <= 256; ++pop) {
        const auto b = scattered((pop * 73 + 19) % 257, cases.state);
        for (unsigned repeat = 0; repeat < 4; ++repeat)
            if (!run(scattered(pop, cases.state), b)) return 1;
        for (unsigned start : {0, 1, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255}) {
            Plain a{};
            for (unsigned j = 0; j < pop; ++j) {
                const auto i = (start + j) % 256;
                a[i / 8] |= std::uint8_t(1u << (i % 8));
            }
            if (!run(a, b)) return 1;
        }
    }
    // Every enumerative byte code at every byte position, against its dense
    // complement; this also exercises singleton and missing-singleton trees.
    for (unsigned position = 0; position < 32; ++position)
        for (unsigned code = 0; code < 256; ++code) {
            Plain a{}, b;
            a[position] = std::uint8_t(code);
            for (unsigned j = 0; j < 32; ++j) b[j] = std::uint8_t(~a[j]);
            if (!run(a, b)) return 1;
        }
    // Alternating full/empty subtrees at each binary subdivision, all phases.
    for (unsigned grain : {1, 2, 4, 8, 16, 32, 64, 128})
        for (unsigned phase = 0; phase < 2 * grain; ++phase) {
            Plain a{}, b{};
            for (unsigned i = 0; i < 256; ++i) {
                a[i / 8] |= std::uint8_t((((i + phase) / grain) & 1u) << (i % 8));
                b[i / 8] |= std::uint8_t((((i + phase + 1) / grain) & 1u) << (i % 8));
            }
            if (!run(a, b)) return 1;
        }
    if (!check_guards(cases.state)) {
        std::fprintf(stderr, "BEC consumer guarded-boundary check failed\n");
        return 1;
    }
    std::printf("BEC consumer regions: %llu structural pairs; all populations; "
                "exact 32/64-byte query and 64-byte body guards passed.\n",
                static_cast<unsigned long long>(cases.count));
    return 0;
}

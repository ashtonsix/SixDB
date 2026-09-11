#include "pack.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace {
namespace experiment = local1_experiment;
namespace sp = ikea::seriespack;
std::size_t checks = 0;
std::uint64_t state = 0x572df0938c6ab4e1ULL;
std::uint8_t random_byte() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
void require(bool v) { if (!v) std::abort(); }
struct guarded {
    std::size_t page = ::sysconf(_SC_PAGESIZE);
    std::size_t size = 65536;
    std::uint8_t* mapping = static_cast<std::uint8_t*>(
        ::mmap(nullptr, size + 2 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    std::uint8_t* bytes = mapping == MAP_FAILED ? nullptr : mapping + page;
    guarded() { require(mapping != MAP_FAILED); require(::mprotect(bytes, size, PROT_READ | PROT_WRITE) == 0); }
    ~guarded() { ::munmap(mapping, size + 2 * page); }
};

template<unsigned L, unsigned Begin, unsigned Count>
[[gnu::noinline]] void fragment(const std::uint8_t* p) {
    using UInt = sp::scalar_for_width<8 * L>;
    std::array<UInt, 32 / L> got;
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(got.data()), experiment::read_fragment<L, Begin, Count>(p));
    for (unsigned i = 0; i < got.size(); ++i) {
        const auto want = i < Count ? ((*p >> (Begin + i)) & 1u) : 0;
        require(got[i] == want);
    }
    ++checks;
}

template<unsigned L>
void fragments(guarded& source) {
    constexpr unsigned max_count = std::min(8u, 32u / L);
    sp::detail::static_for<max_count + 1>([&](auto count) {
        sp::detail::static_for<9 - count>([&](auto begin) {
            if constexpr (count == 0) fragment<L, begin, count>(nullptr);
            else {
                for (unsigned value = 0; value < 256; ++value) {
                    source.bytes[0] = value;
                    fragment<L, begin, count>(source.bytes);
                    source.bytes[source.size - 1] = value;
                    fragment<L, begin, count>(source.bytes + source.size - 1);
                }
                for (unsigned offset = 0; offset < 128; ++offset) {
                    source.bytes[128 + offset] = random_byte();
                    fragment<L, begin, count>(source.bytes + 128 + offset);
                }
            }
        });
    });
}

[[gnu::noinline]] void region(const std::uint8_t* p) {
    std::array<std::uint8_t, 32> got;
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(got.data()), experiment::read_region32(p));
    for (unsigned i = 0; i < 32; ++i) require(got[i] == ((p[i / 8] >> (i % 8)) & 1u));
    ++checks;
}

template<class UInt, unsigned Regions>
[[gnu::noinline]] void dense(const std::uint8_t* p, std::uint8_t* storage, std::size_t tiles) {
    // Misaligned callers are part of the existing native contract. Read result
    // bytes via memcpy so the oracle itself has no misaligned UInt dereference.
    experiment::decode_tiles<UInt, Regions>(p, reinterpret_cast<UInt*>(storage), tiles);
    for (std::size_t i = 0; i < tiles * 8; ++i) {
        UInt got;
        std::memcpy(&got, storage + i * sizeof(UInt), sizeof got);
        require(got == ((p[i / 8] >> (i % 8)) & 1u));
    }
    ++checks;
}

template<class UInt>
void dense_checks(guarded& source, guarded& target) {
    constexpr std::array counts{0u,1u,2u,3u,4u,5u,7u,8u,9u,16u,17u,31u,32u,33u,
        63u,64u,65u,127u,128u,129u,255u,256u,257u};
    for (auto n : counts) {
        const std::size_t bytes = n * 8 * sizeof(UInt);
        for (bool end : {false, true}) {
            auto* p = source.bytes + (end ? source.size - n : 0);
            auto* out = target.bytes + (end ? target.size - bytes : 0);
            for (unsigned i = 0; i < n; ++i) p[i] = random_byte();
            dense<UInt, 1>(p, out, n);
            dense<UInt, 4>(p, out, n);
        }
        // All input and output positions modulo128, in opposing directions.
        // Redzones catch writes beyond the requested exact output, including
        // the non-native-grain remainder after a four-region loop.
        for (unsigned offset = 0; offset < 128; ++offset) {
            auto* p = source.bytes + 128 + offset;
            auto* out = target.bytes + 256 - offset;
            for (unsigned i = 0; i < n; ++i) p[i] = random_byte();
            for (auto run : {dense<UInt, 1>, dense<UInt, 4>}) {
                std::memset(out - 64, 0xa9, bytes + 128);
                run(p, out, n);
                for (unsigned i = 0; i < 64; ++i) require(out[-int(i) - 1] == 0xa9 && out[bytes + i] == 0xa9);
            }
        }
    }
}
}

int main() {
    guarded source, target;
    fragments<1>(source); fragments<2>(source); fragments<4>(source); fragments<8>(source);
    for (unsigned changed = 0; changed < 4; ++changed)
        for (unsigned value = 0; value < 256; ++value)
            for (bool end : {false, true}) {
                auto* p = source.bytes + (end ? source.size - 4 : 0);
                for (unsigned i = 0; i < 4; ++i) p[i] = i == changed ? value : random_byte();
                region(p);
            }
    for (unsigned offset = 0; offset < 128; ++offset)
        for (unsigned repetition = 0; repetition < 64; ++repetition) {
            auto* p = source.bytes + 128 + offset;
            for (unsigned i = 0; i < 4; ++i) p[i] = random_byte();
            region(p);
        }
    dense_checks<std::uint8_t>(source, target);
    dense_checks<std::uint16_t>(source, target);
    dense_checks<std::uint32_t>(source, target);
    dense_checks<std::uint64_t>(source, target);
    std::printf("AVX2 Local1 direct expansion: %zu oracle/extent cases passed\n", checks);
}

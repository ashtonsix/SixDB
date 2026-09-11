#include <ikea/seriespack/detail/body_avx2.h>
#include <ikea/seriespack/detail/body_avx512.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

#if defined(__AVX2__)

namespace {

constexpr std::uint8_t canary = 0xa9;
std::uint64_t random_state = 0x31e6ab92749580dfULL;
std::size_t checked_cases = 0;
unsigned checked_configurations = 0;

std::uint8_t random_byte() {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return static_cast<std::uint8_t>(random_state >> 24);
}

[[noreturn]] void fail(const char* operation, unsigned q, unsigned lane_bytes,
                       unsigned vector_bytes, std::size_t byte) {
    std::fprintf(stderr, "%s: Q=%u L=%u vector=%u at byte %zu\n",
                 operation, q, lane_bytes, vector_bytes, byte);
    std::abort();
}

void fill_random(std::span<std::uint8_t> bytes) {
    for (auto& byte : bytes) byte = random_byte();
}

class GuardedPage {
public:
    const std::size_t size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    std::uint8_t* const mapping = static_cast<std::uint8_t*>(
        ::mmap(nullptr, 3 * size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    std::uint8_t* const bytes = mapping == MAP_FAILED ? nullptr : mapping + size;

    GuardedPage() {
        if (mapping == MAP_FAILED || ::mprotect(bytes, size, PROT_READ | PROT_WRITE) != 0) {
            std::perror("guarded page");
            std::abort();
        }
    }
    ~GuardedPage() { ::munmap(mapping, 3 * size); }
    GuardedPage(const GuardedPage&) = delete;
    GuardedPage& operator=(const GuardedPage&) = delete;
};

template<unsigned Q, unsigned L>
[[gnu::noinline]] void decode256(const std::uint8_t* input, std::uint8_t* output) {
    const auto value = ikea::seriespack::detail::avx2::decode_body<Q, L>(input);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(output), value);
}

template<unsigned Q, unsigned L>
[[gnu::noinline]] void encode256(const std::uint8_t* input, std::uint8_t* output) {
    const auto value = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(input));
    ikea::seriespack::detail::avx2::encode_body<Q, L>(output, value);
}

template<unsigned Q, unsigned L>
[[gnu::noinline]] void decode256_prefix(const std::uint8_t* input, std::uint8_t* output) {
    const auto value = ikea::seriespack::detail::avx2::decode_body_prefix<Q, L, 8>(input);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(output), value);
}

template<unsigned Q, unsigned L>
[[gnu::noinline]] void encode256_prefix(const std::uint8_t* input, std::uint8_t* output) {
    ikea::seriespack::detail::avx2::encode_body_prefix<Q, L, 8>(
        output, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(input)));
}

#if defined(__AVX512BW__)
template<unsigned Q, unsigned L>
[[gnu::noinline]] void decode512(const std::uint8_t* input, std::uint8_t* output) {
    const auto value = ikea::seriespack::detail::avx512::decode_body<Q, L>(input);
    _mm512_storeu_si512(output, value);
}

template<unsigned Q, unsigned L>
[[gnu::noinline]] void encode512(const std::uint8_t* input, std::uint8_t* output) {
    ikea::seriespack::detail::avx512::encode_body<Q, L>(output, _mm512_loadu_si512(input));
}

template<unsigned Q, unsigned L>
[[gnu::noinline]] void decode512_prefix(const std::uint8_t* input, std::uint8_t* output) {
    _mm512_storeu_si512(output, ikea::seriespack::detail::avx512::decode_body_prefix<Q, L, 8>(input));
}

template<unsigned Q, unsigned L>
[[gnu::noinline]] void encode512_prefix(const std::uint8_t* input, std::uint8_t* output) {
    ikea::seriespack::detail::avx512::encode_body_prefix<Q, L, 8>(output, _mm512_loadu_si512(input));
}
#endif

using Operation = void (*)(const std::uint8_t*, std::uint8_t*);

template<unsigned Q, unsigned L, unsigned VectorBytes, unsigned Count = VectorBytes / L>
void check_body(Operation decode, Operation encode) {
    constexpr unsigned count = Count;
    constexpr unsigned extent = count * Q;
    ++checked_configurations;

    const auto check_decode = [&](const std::uint8_t* input) {
        std::array<std::uint8_t, VectorBytes + 32> result;
        result.fill(canary);
        decode(input, result.data() + 16);
        for (unsigned lane = 0; lane < VectorBytes / L; ++lane) {
            for (unsigned byte = 0; byte < L; ++byte) {
                // Independent byte oracle: no production shuffle map, helper,
                // or round-trip comparison determines the expected result.
                const auto expected = lane < count && byte < Q ? input[lane * Q + byte] : 0;
                if (result[16 + lane * L + byte] != expected)
                    fail("decode value", Q, L, VectorBytes, lane * L + byte);
            }
        }
        for (unsigned i = 0; i < 16; ++i) {
            if (result[i] != canary || result[16 + VectorBytes + i] != canary)
                fail("decode result extent", Q, L, VectorBytes, i);
        }
        ++checked_cases;
    };

    const auto check_encode = [&](const std::uint8_t* input, std::uint8_t* output,
                                  std::span<const std::uint8_t> containing) {
        encode(input, output);
        const auto start = static_cast<std::size_t>(output - containing.data());
        for (std::size_t i = 0; i < containing.size(); ++i) {
            std::uint8_t expected = canary;
            if constexpr (Q != 0) {
                if (i >= start && i < start + extent) {
                    const auto packed = i - start;
                    expected = input[packed / Q * L + packed % Q];
                }
            }
            if (containing[i] != expected)
                fail("encode value/extent", Q, L, VectorBytes, i);
        }
        ++checked_cases;
    };

    // All possible offsets within a cache line, including nonzero upper input
    // bytes: packing must truncate, never signed- or unsigned-saturate them.
    std::array<std::uint8_t, 256> encoded;
    std::array<std::uint8_t, 256> destination;
    std::array<std::uint8_t, VectorBytes + 64> values;
    for (unsigned offset = 0; offset < 64; ++offset) {
        for (unsigned pattern = 0; pattern < 10; ++pattern) {
            fill_random(encoded);
            fill_random(values);
            if (pattern < 2) {
                std::fill_n(encoded.data() + offset, extent, pattern == 0 ? 0 : 255);
                std::fill_n(values.data() + offset, VectorBytes, pattern == 0 ? 0 : 255);
            }
            destination.fill(canary);
            check_decode(encoded.data() + offset);
            check_encode(values.data() + offset, destination.data() + offset, destination);
        }
    }

    // Each encoded and native bit separately distinguishes omitted, duplicated,
    // permuted, accidentally signed and nonzero-padding bytes.
    encoded.fill(0);
    for (unsigned bit = 0; bit < extent * 8; ++bit) {
        encoded[bit / 8] = static_cast<std::uint8_t>(1u << (bit % 8));
        check_decode(encoded.data());
        encoded[bit / 8] = 0;
    }
    values.fill(0);
    for (unsigned bit = 0; bit < VectorBytes * 8; ++bit) {
        values[bit / 8] = static_cast<std::uint8_t>(1u << (bit % 8));
        destination.fill(canary);
        check_encode(values.data(), destination.data() + 7, destination);
        values[bit / 8] = 0;
    }

    // No accessible byte precedes the left case or follows the right case.
    // Q=0 receives a PROT_NONE address and must perform no data access.
    GuardedPage source;
    GuardedPage target;
    fill_random(values);
    for (unsigned side = 0; side < 2; ++side) {
        const auto offset = side == 0 ? 0 : source.size - extent;
        auto* input = source.bytes + offset;
        auto* output = target.bytes + offset;
        if constexpr (Q != 0) fill_random({input, extent});
        std::memset(target.bytes, canary, target.size);
        check_decode(Q == 0 ? source.mapping : input);
        if constexpr (Q == 0) {
            encode(values.data(), target.mapping);
            ++checked_cases;
        } else {
            check_encode(values.data(), output, {target.bytes, target.size});
        }
    }
}

template<unsigned L>
void check_lane_width() {
    []<std::size_t... Q>(std::index_sequence<Q...>) {
        (check_body<Q, L, 32>(&decode256<Q, L>, &encode256<Q, L>), ...);
        if constexpr (32 / L > 8)
            (check_body<Q, L, 32, 8>(&decode256_prefix<Q, L>, &encode256_prefix<Q, L>), ...);
#if defined(__AVX512BW__)
        ([&] {
#if !defined(__AVX512VBMI__)
            // The BW-only build deliberately exercises the feature subset
            // that does not require byte permutations.
            if constexpr (Q == 0 || Q == 1 || Q == 2 || Q == 4 || Q == L)
#endif
            {
                check_body<Q, L, 64>(&decode512<Q, L>, &encode512<Q, L>);
                if constexpr (64 / L > 8)
                    check_body<Q, L, 64, 8>(&decode512_prefix<Q, L>, &encode512_prefix<Q, L>);
            }
        }(), ...);
#endif
    }(std::make_index_sequence<L + 1>{});
}

#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
[[gnu::noinline]] void encode_region(const std::uint8_t* input, std::uint8_t* output) {
    ikea::seriespack::detail::avx512::encode_body_region32_7(
        output, _mm512_loadu_si512(input), _mm512_loadu_si512(input + 64),
        _mm512_loadu_si512(input + 128), _mm512_loadu_si512(input + 192));
}

void check_region() {
    std::array<std::uint8_t, 320> values;
    std::array<std::uint8_t, 352> encoded;
    const auto verify = [](const std::uint8_t* input, std::uint8_t* output,
                           std::span<const std::uint8_t> containing) {
        encode_region(input, output);
        const auto start = static_cast<std::size_t>(output - containing.data());
        for (std::size_t i = 0; i < containing.size(); ++i) {
            auto expected = canary;
            if (i >= start && i < start + 224) {
                const auto packed = i - start;
                expected = input[packed / 7 * 8 + packed % 7];
            }
            if (containing[i] != expected) fail("region32 encode", 7, 8, 64, i);
        }
        ++checked_cases;
    };
    for (unsigned offset = 0; offset < 64; ++offset) {
        fill_random(values);
        encoded.fill(canary);
        verify(values.data() + offset, encoded.data() + offset, encoded);
    }
    values.fill(0);
    for (unsigned bit = 0; bit < 256 * 8; ++bit) {
        values[bit / 8] = static_cast<std::uint8_t>(1u << (bit % 8));
        encoded.fill(canary);
        verify(values.data(), encoded.data() + 3, encoded);
        values[bit / 8] = 0;
    }
    GuardedPage source;
    GuardedPage target;
    for (unsigned side = 0; side < 2; ++side) {
        auto* input = source.bytes + (side == 0 ? 0 : source.size - 256);
        auto* output = target.bytes + (side == 0 ? 0 : target.size - 224);
        fill_random({input, 256});
        std::memset(target.bytes, canary, target.size);
        verify(input, output, {target.bytes, target.size});
    }
}
#endif

} // namespace

int main() {
    check_lane_width<1>();
    check_lane_width<2>();
    check_lane_width<4>();
    check_lane_width<8>();
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    check_region();
#endif
    std::printf("SeriesPack x86 bodies: %u configurations, %zu cases passed"
                " (independent byte/bit oracles, unaligned starts, guards).\n",
                checked_configurations, checked_cases);
}
#else
int main() {
    std::puts("SeriesPack x86 bodies: native checks skipped (AVX2 is not enabled)." );
}
#endif

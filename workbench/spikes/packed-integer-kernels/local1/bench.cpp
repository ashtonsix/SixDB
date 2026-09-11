#include "pack.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <stdexcept>
#include <vector>

namespace {
namespace sp = ikea::seriespack;
namespace candidate = local1_experiment;
using function = void (*)(const std::uint8_t*, const std::uint8_t*, void*, std::size_t);
struct arm { const char* name; function call; };
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct buffer {
    std::uint8_t* bytes = nullptr;
    std::size_t size;
    explicit buffer(std::size_t n) : size(n) {
        void* p = nullptr;
        require(posix_memalign(&p, 64, std::max(std::size_t{64}, (n + 63) & ~std::size_t{63})) == 0, "allocation");
        bytes = static_cast<std::uint8_t*>(p);
        std::memset(bytes, 0, n);
    }
    ~buffer() { std::free(bytes); }
    buffer(const buffer&) = delete;
};

// Only the headless W1 layout admits adjacent tail bytes as a dense region.
// Region arms share exactly the same loop and widening, changing just the
// packed-bit expansion. They do not assume the native tile loop has that grain.
template<class UInt, unsigned Mode>
[[gnu::noinline]] void dense(const std::uint8_t* __restrict p,
    const std::uint8_t*, void* output, std::size_t count) {
    auto* __restrict out = static_cast<UInt*>(output);
    if constexpr (Mode == 0) sp::avx2::decode_tiles<1, sp::geometry::local8>(p, out, count / 8);
    else {
#pragma clang loop unroll(disable)
        for (; count >= 32; count -= 32, p += 4, out += 32) {
            const auto values = [&] [[gnu::always_inline]] {
                if constexpr (Mode == 1) return sp::avx2::native_detail::transpose(
                    sp::detail::avx2::decode_body<1, 8>(p));
                else return candidate::read_region32(p);
            }();
            candidate::store_region32(values, out);
        }
        for (; count; count -= 8, ++p, out += 8) {
            if constexpr (Mode == 1) sp::avx2::decode_tile<1, sp::geometry::local8>(p, out);
            else candidate::decode_tile(p, out);
        }
    }
}

// Body bytes and the one-bit tail remain inside their own W-byte Local packet.
// Only the tail reader changes. Native body expansion, optional head expansion,
// joins, and output stores have identical source spelling for both arms.
template<unsigned W, unsigned H, class UInt, bool Direct>
[[gnu::noinline]] void fragments(const std::uint8_t* __restrict p,
    const std::uint8_t* __restrict heads, void* output, std::size_t count) {
    static_assert(W % 8 == 1 && (H == 0 || H == 8 || H == 16));
    static_assert(W + H <= sizeof(UInt) * 8);
    constexpr unsigned L = sizeof(UInt), N = std::min(8u, 32u / L), Q = W / 8;
    auto* __restrict out = static_cast<UInt*>(output);
#pragma clang loop unroll(disable)
    for (; count; count -= 8, p += W, heads += H, out += 8) {
        sp::detail::static_for<8 / N>([&](auto part) {
            constexpr unsigned begin = part * N;
            const auto tail = [&] [[gnu::always_inline]] {
                if constexpr (Direct) return candidate::read_fragment<L, begin, N>(p + 8 * Q);
                else return sp::avx2::read_tail<W, sp::geometry::local8, L, begin>(p);
            }();
            const auto body = sp::avx2::read_body<W, sp::geometry::local8, L, begin>(p);
            auto values = sp::avx2::join<L, 1>(body, tail);
            if constexpr (H != 0) {
                const auto head = sp::detail::avx2::decode_body_prefix<H / 8, L, N>(heads + begin * H / 8);
                values = sp::avx2::join<L, W>(head, values);
            }
            sp::detail::avx2::encode_body_prefix<L, L, N>(reinterpret_cast<std::uint8_t*>(out + begin), values);
        });
    }
}

[[gnu::noinline]] double elapsed(function f, const std::uint8_t* p,
    const std::uint8_t* heads, void* out, std::size_t count, std::size_t passes) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    for (std::size_t i = 0; i < passes; ++i) {
        f(p, heads, out, count);
        asm volatile("" ::: "memory");
    }
    return std::chrono::duration<double, std::nano>(clock::now() - start).count();
}

std::size_t checked_cases = 0;
template<unsigned W, unsigned H, class UInt, bool Dense>
void run(std::size_t count, bool check_only) {
    static_assert(!Dense || (W == 1 && H == 0));
    constexpr unsigned K = W + H, Q = W / 8;
    constexpr auto arms = [] {
        if constexpr (Dense) return std::array<arm, 3>{{
            {"current_native", dense<UInt, 0>},
            {"transpose_region32", dense<UInt, 1>},
            {"direct_region32", dense<UInt, 2>}}};
        else return std::array<arm, 2>{{
            {"current_fragment", fragments<W, H, UInt, false>},
            {"direct_fragment", fragments<W, H, UInt, true>}}};
    }();
    buffer payload(count / 8 * W), heads(count * H / 8), output(count * sizeof(UInt));
    std::vector<UInt> oracle(count);
    // Independent wire construction: no native or scalar SeriesPack encoder
    // supplies the bit oracle or the expected value for either candidate arm.
    for (std::size_t i = 0; i < count; ++i) {
        std::uint64_t value = i + 0x849275bc1ed031a6ULL;
        value ^= value >> 17; value *= 0x9e3779b97f4a7c15ULL; value ^= value >> 31;
        value &= (std::uint64_t{1} << K) - 1;
        oracle[i] = static_cast<UInt>(value);
        auto* tile = payload.bytes + i / 8 * W;
        for (unsigned byte = 0; byte < Q; ++byte)
            tile[i % 8 * Q + byte] = static_cast<std::uint8_t>(value >> (1 + byte * 8));
        tile[8 * Q] |= std::uint8_t((value & 1) << (i % 8));
        for (unsigned byte = 0; byte < H / 8; ++byte)
            heads.bytes[i * H / 8 + byte] = static_cast<std::uint8_t>(value >> (W + byte * 8));
    }
    const auto validate = [&](const arm& a) {
        a.call(payload.bytes, heads.bytes, output.bytes, count);
        require(std::memcmp(output.bytes, oracle.data(), output.size) == 0, "decode mismatch");
        ++checked_cases;
    };
    for (const auto& a : arms) validate(a);
    if (check_only) return;
    std::size_t passes = 1;
    while (elapsed(arms[0].call, payload.bytes, heads.bytes, output.bytes, count, passes) < 20000000.0)
        passes *= 2;
    // Twelve rotations balance both the three-arm region comparison and the
    // two-arm fragment comparison. Every arm has the same buffers and passes.
    for (unsigned repetition = 0; repetition < 12; ++repetition)
        for (unsigned position = 0; position < arms.size(); ++position) {
            const auto& a = arms[(position + repetition) % arms.size()];
            for (unsigned warm = 0; warm < 16; ++warm)
                a.call(payload.bytes, heads.bytes, output.bytes, count);
            const auto ns = elapsed(a.call, payload.bytes, heads.bytes, output.bytes, count, passes);
            validate(a);
            std::printf("{\"family\":\"%s\",\"width\":%u,\"payload_width\":%u,\"head_bits\":%u,"
                "\"carrier_bits\":%zu,\"arm\":\"%s\",\"values\":%zu,\"passes\":%zu,"
                "\"repetition\":%u,\"position\":%u,\"elapsed_ns\":%.0f,\"ns_per_value\":%.9f,"
                "\"payload_page_offset\":%zu,\"head_page_offset\":%zu,\"output_page_offset\":%zu}\n",
                Dense ? "dense_local1" : "packet_r1", K, W, H, sizeof(UInt) * 8, a.name, count, passes,
                repetition, position, ns, ns / (double(passes) * count),
                reinterpret_cast<std::uintptr_t>(payload.bytes) % 4096,
                reinterpret_cast<std::uintptr_t>(heads.bytes) % 4096,
                reinterpret_cast<std::uintptr_t>(output.bytes) % 4096);
        }
}

int pin() {
    cpu_set_t allowed; CPU_ZERO(&allowed);
    require(sched_getaffinity(0, sizeof allowed, &allowed) == 0, "get affinity");
    int cpu = -1;
    if (const auto* text = std::getenv("SIXDB_CPU")) cpu = std::atoi(text);
    else for (int i = 0; i < CPU_SETSIZE; ++i) if (CPU_ISSET(i, &allowed)) { cpu = i; break; }
    require(cpu >= 0 && cpu < CPU_SETSIZE && CPU_ISSET(cpu, &allowed), "allowed CPU");
    cpu_set_t one; CPU_ZERO(&one); CPU_SET(cpu, &one);
    require(sched_setaffinity(0, sizeof one, &one) == 0, "set affinity");
    return cpu;
}
}

int main(int argc, char** argv) {
    const bool check_only = argc == 2 && std::strcmp(argv[1], "--check-only") == 0;
    require(argc == 1 || check_only, "usage: local1_bench [--check-only]");
    const auto cpu = pin();
    std::printf("{\"diagnostic\":\"seriespack-avx2-local1\",\"cpu\":%d,\"gfni\":%s,"
        "\"repetitions\":12,\"shared_buffers\":true,\"residency\":\"unestablished\"}\n", cpu,
#if defined(__GFNI__)
        "true"
#else
        "false"
#endif
    );
    for (std::size_t count : {256u, 8192u, 65536u}) {
        run<1, 0, std::uint8_t, true>(count, check_only);
        run<1, 0, std::uint16_t, true>(count, check_only);
        run<1, 0, std::uint32_t, true>(count, check_only);
        run<1, 0, std::uint64_t, true>(count, check_only);
        run<1, 8, std::uint16_t, false>(count, check_only);
        run<1, 8, std::uint32_t, false>(count, check_only);
        run<1, 8, std::uint64_t, false>(count, check_only);
        run<1, 16, std::uint32_t, false>(count, check_only);
        run<1, 16, std::uint64_t, false>(count, check_only);
        run<9, 8, std::uint32_t, false>(count, check_only);
        run<9, 8, std::uint64_t, false>(count, check_only);
        run<9, 16, std::uint32_t, false>(count, check_only);
        run<9, 16, std::uint64_t, false>(count, check_only);
        run<17, 8, std::uint32_t, false>(count, check_only);
        run<17, 8, std::uint64_t, false>(count, check_only);
        run<49, 8, std::uint64_t, false>(count, check_only);
    }
    if (check_only) std::printf("{\"checks\":\"passed\",\"cases\":%zu}\n", checked_cases);
}

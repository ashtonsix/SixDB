// Focused workbench diagnostic: shared buffers distinguish native loop grain
// from admitted-wrapper work. No production selection is changed by this TU.
#include <ikea/seriespack.h>
#include <ikea/seriespack/native_avx2.h>
#include <ikea/seriespack/native_avx512.h>
#include "../ikea-composition/probes/ikea-integers/local.h"
#include "../ikea-composition/probes/ikea-integers/scan.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <sched.h>
#include <span>
#include <stdexcept>
#include <vector>

namespace sp = ikea::seriespack;
namespace probe = ikea::integers;
namespace {
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
namespace native = sp::avx512;
constexpr auto target = sp::execution_target::avx512;
constexpr unsigned region = 64;
#else
namespace native = sp::avx2;
constexpr auto target = sp::execution_target::avx2;
constexpr unsigned region = 32;
#endif
using clock_type = std::chrono::steady_clock;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct buffer {
    std::uint8_t* bytes = nullptr;
    std::size_t size;
    explicit buffer(std::size_t n) : size(n) {
        void* pointer = nullptr;
        require(posix_memalign(&pointer, 64, (n + 63) & ~std::size_t{63}) == 0, "allocation");
        bytes = static_cast<std::uint8_t*>(pointer);
        std::memset(bytes, 0, n);
    }
    ~buffer() { std::free(bytes); }
    buffer(const buffer&) = delete;
    std::span<std::byte> span() { return {reinterpret_cast<std::byte*>(bytes), size}; }
};
struct context {
    std::optional<sp::bound_reader> reader;
    std::optional<sp::bound_encoder> encoder;
};
using function = void (*)(const std::uint8_t*, std::uint8_t*, std::size_t, const context&);
struct arm { const char* name; function encode; function decode; };

template<unsigned W, sp::geometry G, unsigned Mode>
[[gnu::noinline]] void encode(const std::uint8_t* __restrict in,
    std::uint8_t* __restrict out, std::size_t count, const context& ctx) {
    constexpr unsigned tile_values = sp::payload_layout<W, G>::tile_values;
    if constexpr (Mode == 0) ctx.encoder->encode(sp::input_values{std::span(in, count)});
    else if constexpr (Mode == 1 || (Mode == 2 && G == sp::geometry::striped))
        native::encode_low_tiles<W, G>(in, out, count / tile_values);
    else if constexpr (Mode == 2) {
        // Keep this diagnostic arm at exactly four physical chunks per loop.
        // Otherwise Clang further unrolls Local1 to sixteen chunks here.
#pragma clang loop unroll(disable)
        for (; count >= 4 * region; count -= 4 * region,
            in += 4 * region, out += 4 * region / 8 * W) {
            sp::detail::static_for<4>([&](auto part) {
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
                native::write_local_region64<W>(out + part * region / 8 * W,
                    _mm512_loadu_si512(in + part * region));
#else
                native::write_local_region32<W>(out + part * region / 8 * W,
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + part * region)));
#endif
            });
        }
        native::encode_low_tiles<W, G>(in, out, count / 8);
    } else {
        __builtin_assume(count % 256 == 0);
        for (; count; count -= 256, in += 256, out += 32 * W) {
            if constexpr (G == sp::geometry::local8) probe::local_encode<W>(in, out);
            else probe::scan_encode<W>(in, out);
        }
    }
}

template<unsigned W, sp::geometry G, unsigned Mode>
[[gnu::noinline]] void decode(const std::uint8_t* __restrict in,
    std::uint8_t* __restrict out, std::size_t count, const context& ctx) {
    constexpr unsigned tile_values = sp::payload_layout<W, G>::tile_values;
    if constexpr (Mode == 0) ctx.reader->decode({0, count}, sp::output_values{std::span(out, count)});
    else if constexpr (Mode == 1 || (Mode == 2 && G == sp::geometry::striped))
        native::decode_tiles<W, G>(in, out, count / tile_values);
    else if constexpr (Mode == 2) {
#pragma clang loop unroll(disable)
        for (; count >= 4 * region; count -= 4 * region,
            in += 4 * region / 8 * W, out += 4 * region) {
            sp::detail::static_for<4>([&](auto part) {
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
                _mm512_storeu_si512(out + part * region,
                    native::read_local_region64<W>(in + part * region / 8 * W));
#else
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + part * region),
                    native::read_local_region32<W>(in + part * region / 8 * W));
#endif
            });
        }
        native::decode_tiles<W, G>(in, out, count / 8);
    } else {
        __builtin_assume(count % 256 == 0);
        for (; count; count -= 256, in += 32 * W, out += 256) {
            if constexpr (G == sp::geometry::local8) probe::local_decode<W>(in, out);
            else probe::scan_decode<W>(in, out);
        }
    }
}

template<unsigned W, sp::geometry G>
constexpr auto arms = std::array<arm, 4>{{
    {"bound", encode<W, G, 0>, decode<W, G, 0>},
    {"native", encode<W, G, 1>, decode<W, G, 1>},
    {"native_region4", encode<W, G, 2>, decode<W, G, 2>},
    {"predecessor", encode<W, G, 3>, decode<W, G, 3>}
}};

[[gnu::noinline]] double elapsed(function f, const std::uint8_t* in,
    std::uint8_t* out, std::size_t count, const context& ctx, std::size_t passes) {
    const auto start = clock_type::now();
    for (std::size_t p = 0; p < passes; ++p) {
        f(in, out, count, ctx);
        asm volatile("" ::: "memory");
    }
    return std::chrono::duration<double, std::nano>(clock_type::now() - start).count();
}

template<unsigned W, sp::geometry G>
void run(std::size_t count, bool check_only) {
    const sp::description layout{W, 0, G};
    buffer input(count), packed(count * W / 8), output(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto x = std::uint64_t(i) + 0x59736abeULL;
        x ^= x >> 17; x *= 0x9e3779b97f4a7c15ULL; x ^= x >> 31;
        input.bytes[i] = std::uint8_t(x & ((1u << W) - 1));
    }
    const auto attached = sp::mutable_view::attach(layout, count,
        {{packed.span(), sp::tile_bytes(layout)}, {}});
    require(attached.has_value(), "attach");
    require(sp::encode(*attached, sp::input_values{std::span(input.bytes, count)},
        nullptr, sp::execution_target::scalar).has_value(), "canonical encode");
    const std::vector<std::uint8_t> canonical(packed.bytes, packed.bytes + packed.size);
    context ctx;
    const auto reader = sp::bind_reader(attached->as_const(), target);
    const auto encoder = sp::bind_encoder(*attached, target);
    require(reader.has_value() && encoder.has_value(), "bind");
    ctx.reader = *reader; ctx.encoder = *encoder;
    const auto validate = [&](const arm& a, bool encoding) {
        if (encoding) {
            a.encode(input.bytes, packed.bytes, count, ctx);
            require(std::memcmp(packed.bytes, canonical.data(), packed.size) == 0, "encoded mismatch");
        } else {
            a.decode(packed.bytes, output.bytes, count, ctx);
            require(std::memcmp(output.bytes, input.bytes, count) == 0, "decoded mismatch");
        }
    };
    for (const auto& a : arms<W, G>) { validate(a, true); validate(a, false); }
    if (check_only) return;
    for (bool encoding : {false, true}) {
        const auto* in = encoding ? input.bytes : packed.bytes;
        auto* out = encoding ? packed.bytes : output.bytes;
        const auto raw = encoding ? arms<W, G>[1].encode : arms<W, G>[1].decode;
        std::size_t passes = 1;
        while (elapsed(raw, in, out, count, ctx, passes) < 20000000.0) passes *= 2;
        // The same addresses, input values and work count serve all four arms.
        // Eight rotations give each arm each sequential position twice.
        for (unsigned repetition = 0; repetition < 8; ++repetition) {
            for (unsigned position = 0; position < 4; ++position) {
                const auto& a = arms<W, G>[(position + repetition) % 4];
                const auto f = encoding ? a.encode : a.decode;
                for (unsigned warm = 0; warm < 16; ++warm) f(in, out, count, ctx);
                const double ns = elapsed(f, in, out, count, ctx, passes);
                validate(a, encoding);
                std::printf("{\"layout\":\"%s\",\"width\":%u,\"operation\":\"%s\",\"arm\":\"%s\","
                    "\"values\":%zu,\"passes\":%zu,\"repetition\":%u,\"position\":%u,\"elapsed_ns\":%.0f,"
                    "\"ns_per_value\":%.9f,\"input_page_offset\":%zu,\"output_page_offset\":%zu}\n",
                    G == sp::geometry::local8 ? "local" : "striped", W, encoding ? "encode" : "decode", a.name,
                    count, passes, repetition, position, ns, ns / (double(passes) * count),
                    reinterpret_cast<std::uintptr_t>(in) % 4096, reinterpret_cast<std::uintptr_t>(out) % 4096);
            }
        }
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
    require(argc == 1 || check_only, "usage: seriespack_local_diagnostic [--check-only]");
    const int cpu = pin();
    std::printf("{\"diagnostic\":\"seriespack-local-loop-grain\",\"cpu\":%d,\"vector_bits\":%u,"
        "\"repetitions\":8,\"shared_buffers\":true,\"residency\":\"unestablished\"}\n", cpu, region * 8);
    for (std::size_t count : {256u, 8192u, 65536u}) {
        run<1, sp::geometry::local8>(count, check_only);
        run<2, sp::geometry::local8>(count, check_only);
        run<5, sp::geometry::local8>(count, check_only);
        run<1, sp::geometry::striped>(count, check_only);
        run<5, sp::geometry::striped>(count, check_only);
    }
    if (check_only) std::puts("{\"checks\":\"passed\",\"cases\":120}");
}

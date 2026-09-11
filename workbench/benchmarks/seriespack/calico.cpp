#include "calico.h"

// These come from an externally configured, recorded Calico checkout. No
// production Ikea target or installed header depends on this prior-art code.
#include <algorithm>
#include <bit>
#include <bytepack.h>

// Clang otherwise outlines planes.h's internal lambdas before its small plane
// loop can fold against Shape(K). Annotate only that header, after its standard
// and native-kernel dependencies are included. The paired adapter-only unroll
// settings expose the fixed shape; no Calico algorithm or wire rule changes.
#if defined(__clang__)
#pragma clang attribute push(__attribute__((always_inline)), apply_to=function)
#endif
#include <planes.h>
#if defined(__clang__)
#pragma clang attribute pop
#endif

#include <array>
#include <utility>

namespace seriespack_measurement {
namespace {

namespace bp = bytepack;
namespace planes = bytepack::planes;

// Match Calico's own access harness. Its implementation retains feature guards
// for GFNI, VBMI/VBMI2 and SVE2 bit-permutation; BP_CPU is independently supplied
// by the build. A BW-only profile does not acquire absent GFNI/VBMI features.
#if defined(__AVX512VL__) && defined(__AVX512BW__)
inline constexpr auto target = bp::Target::avx512;
#elif defined(__AVX2__)
inline constexpr auto target = bp::Target::avx2;
#elif defined(__ARM_FEATURE_SVE2_BITPERM)
inline constexpr auto target = bp::Target::sve2;
#elif defined(__aarch64__)
inline constexpr auto target = bp::Target::neon;
#else
inline constexpr auto target = bp::Target::scalar;
#endif

template<unsigned K, bp::Layout Layout>
struct full_width {
    static constexpr planes::Shape shape{K};
    using impl = planes::Codec<Layout, target>;

    static void encode(const std::uint64_t* __restrict input,
                       std::uint8_t* __restrict output, std::size_t n) {
        bp::assume(n % 256 == 0);
        for (; n != 0; n -= 256, input += 256, output += shape.bytes())
            impl::set256({output, output + shape.body_bytes, &shape}, input);
    }

    static void decode(const std::uint8_t* __restrict input,
                       std::uint64_t* __restrict output, std::size_t n) {
        bp::assume(n % 256 == 0);
        for (; n != 0; n -= 256, input += shape.bytes(), output += 256)
            impl::get256({input, input + shape.body_bytes, &shape}, output);
    }

    static std::uint64_t get(const std::uint8_t* input, std::size_t index) {
        input += index / 256 * shape.bytes();
        return impl::get1({input, input + shape.body_bytes, &shape}, index % 256);
    }

    static void get16(const std::uint8_t* __restrict input, std::size_t index,
                      std::uint64_t* __restrict output) {
        bp::assume(index % 16 == 0);
        input += index / 256 * shape.bytes();
        impl::get16({input, input + shape.body_bytes, &shape}, index % 256, output);
    }

    static void set(std::uint8_t* output, std::size_t index, std::uint64_t value) {
        output += index / 256 * shape.bytes();
        impl::set1({output, output + shape.body_bytes, &shape}, index % 256, value);
    }

    static void set16(std::uint8_t* __restrict output, std::size_t index,
                      const std::uint64_t* __restrict input) {
        bp::assume(index % 16 == 0);
        output += index / 256 * shape.bytes();
        impl::set16({output, output + shape.body_bytes, &shape}, index % 256, input);
    }
};

template<unsigned K, bp::Layout Layout>
struct residual_only {
    using impl = bp::Kernel<Layout, K, target>;

    static void encode(const std::uint8_t* __restrict input,
                       std::uint8_t* __restrict output, std::size_t n) {
        bp::assume(n % 256 == 0);
        for (; n != 0; n -= 256, input += 256, output += impl::bytes)
            impl::set256(output, input);
    }

    static void decode(const std::uint8_t* __restrict input,
                       std::uint8_t* __restrict output, std::size_t n) {
        bp::assume(n % 256 == 0);
        for (; n != 0; n -= 256, input += impl::bytes, output += 256)
            impl::get256(input, output);
    }

    static std::uint8_t get(const std::uint8_t* input, std::size_t index) {
        return impl::get1(input + index / 256 * impl::bytes, index % 256);
    }

    static void get16(const std::uint8_t* __restrict input, std::size_t index,
                      std::uint8_t* __restrict output) {
        bp::assume(index % 16 == 0);
        impl::get16(input + index / 256 * impl::bytes, index % 256, output);
    }

    static void set(std::uint8_t* output, std::size_t index, std::uint8_t value) {
        impl::set1(output + index / 256 * impl::bytes, index % 256, value);
    }

    static void set16(std::uint8_t* __restrict output, std::size_t index,
                      const std::uint8_t* __restrict input) {
        bp::assume(index % 16 == 0);
        impl::set16(output + index / 256 * impl::bytes, index % 256, input);
    }
};

template<bp::Layout Layout, std::size_t... I>
constexpr auto make_full(std::index_sequence<I...>) {
    return std::array<prior_codec, sizeof...(I)>{{
        {full_width<I + 1, Layout>::encode, full_width<I + 1, Layout>::decode,
         full_width<I + 1, Layout>::get, full_width<I + 1, Layout>::get16,
         full_width<I + 1, Layout>::set, full_width<I + 1, Layout>::set16}...
    }};
}

template<bp::Layout Layout, std::size_t... I>
constexpr auto make_residual(std::index_sequence<I...>) {
    return std::array<prior_u8_codec, sizeof...(I)>{{
        {residual_only<I + 1, Layout>::encode, residual_only<I + 1, Layout>::decode,
         residual_only<I + 1, Layout>::get, residual_only<I + 1, Layout>::get16,
         residual_only<I + 1, Layout>::set, residual_only<I + 1, Layout>::set16}...
    }};
}

inline constexpr auto local = make_full<bp::Layout::bitplanes>(std::make_index_sequence<64>{});
inline constexpr auto striped = make_full<bp::Layout::interleaved>(std::make_index_sequence<64>{});
inline constexpr auto local_u8 = make_residual<bp::Layout::bitplanes>(std::make_index_sequence<7>{});
inline constexpr auto striped_u8 = make_residual<bp::Layout::interleaved>(std::make_index_sequence<7>{});

} // namespace

prior_codec prior(unsigned width, bool use_stripes) noexcept {
    if (width < 1 || width > 64) return {};
    return (use_stripes ? striped : local)[width - 1];
}

const char* prior_target() noexcept {
    if constexpr (target == bp::Target::avx512) return "avx512";
    if constexpr (target == bp::Target::avx2) return "avx2";
    if constexpr (target == bp::Target::sve2) return "sve2";
    if constexpr (target == bp::Target::neon) return "neon";
    return "scalar";
}

prior_u8_codec prior_u8(unsigned width, bool use_stripes) noexcept {
    if (width < 1 || width > 7) return {};
    return (use_stripes ? striped_u8 : local_u8)[width - 1];
}

} // namespace seriespack_measurement

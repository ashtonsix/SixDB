#include "probe_controls.h"

#if defined(__aarch64__) || defined(__AVX2__)
// Workbench controls only. Production Ikea does not include these probe files.
#include "../probes/ikea-integers/local.h"
#include "../probes/ikea-integers/scan.h"
#include "../probes/ikea-integers/wide56/kernels.h"
#include "../probes/ikea-integers/wide56/region_encode.h"
#include <ikea_predecessor/seriespack/detail/physical.h>

namespace seriespack_measurement {
namespace {
namespace probe = ikea::integers;
namespace sp = ikea_predecessor::seriespack;

template<unsigned K, bool Striped>
consteval bool same_wire() {
    for (unsigned i = 0; i != 256; ++i) {
        for (unsigned bit = 0; bit != K; ++bit) {
            if constexpr (Striped) {
                constexpr unsigned T = sp::payload_layout<K, sp::geometry::striped>::tile_values;
                constexpr unsigned B = sp::payload_layout<K, sp::geometry::striped>::tile_bytes;
                const auto position = sp::detail::residual_position<K>((i % T) / 32, bit);
                const auto old = probe::scan_bit<K>(i, bit);
                if (old.byte != (i / T) * B + (position / 8) * 32 + i % 32 ||
                    old.bit != position % 8) return false;
            } else {
                const auto old = probe::local_bit<K>(i, bit);
                if (old.byte != (i / 8) * K + bit || old.bit != i % 8) return false;
            }
        }
    }
    return true;
}

template<unsigned K, bool Striped>
void encode(const std::uint8_t* __restrict in, std::uint8_t* __restrict out, std::size_t count) {
    __builtin_assume(count % 256 == 0);
    for (; count; count -= 256, in += 256, out += 32 * K) {
        if constexpr (Striped) probe::scan_encode<K>(in, out);
        else probe::local_encode<K>(in, out);
    }
}

template<unsigned K, bool Striped>
void decode(const std::uint8_t* __restrict in, std::uint8_t* __restrict out, std::size_t count) {
    __builtin_assume(count % 256 == 0);
    for (; count; count -= 256, in += 32 * K, out += 256) {
        if constexpr (Striped) probe::scan_decode<K>(in, out);
        else probe::local_decode<K>(in, out);
    }
}

template<unsigned K, bool Striped>
std::uint64_t get(const std::uint8_t* in, std::size_t index) {
    in += (index / 256) * (32 * K);
    const auto local = static_cast<unsigned>(index % 256);
    __builtin_assume(local < 256);
    if constexpr (Striped)
        return probe::scan_point<K, probe::ScanReader::fragment_classes>(in, local);
    else return probe::local_point<K>(in, local);
}

// The predecessor produces sixteen unsigned byte lanes. This bridge changes
// only the comparison endpoint's output carrier, without a byte scratch or a
// changed reader. Each stored lane keeps its original position in the group.
[[gnu::always_inline]] inline void widen16(probe::Bytes<16> bytes, std::uint64_t* out) {
#if defined(__aarch64__)
    probe::unroll<8>([&](auto part) {
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> result{};
            result.fill(255);
            result[0] = 2 * decltype(part)::value;
            result[8] = 2 * decltype(part)::value + 1;
            return result;
        }();
        vst1q_u64(out + 2 * part, vreinterpretq_u64_u8(vqtbl1q_u8(
            bytes.v, vld1q_u8(indices.data()))));
    });
#elif defined(__AVX512BW__)
    _mm512_storeu_si512(out, _mm512_cvtepu8_epi64(bytes.v));
    _mm512_storeu_si512(out + 8, _mm512_cvtepu8_epi64(_mm_srli_si128(bytes.v, 8)));
#else
    probe::unroll<4>([&](auto part) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 4 * part),
            _mm256_cvtepu8_epi64(_mm_srli_si128(bytes.v, 4 * part)));
    });
#endif
}

template<unsigned K, bool Striped>
void get16(const std::uint8_t* __restrict in, std::size_t index, std::uint64_t* __restrict out) {
    __builtin_assume(index % 16 == 0);
    in += (index / 256) * (32 * K);
    const auto local = static_cast<unsigned>(index % 256);
    __builtin_assume(local < 256 && local % 16 == 0);
    if constexpr (Striped)
        widen16(probe::scan_read16<K, probe::ScanReader::fragment_classes>(in, local), out);
    else widen16(probe::local_read16<K>(in, local), out);
}

template<unsigned K>
predecessor_codec choose(bool striped) {
    static_assert(same_wire<K, false>() && same_wire<K, true>());
    if (striped) return {encode<K, true>, decode<K, true>, get<K, true>, get16<K, true>};
    return {encode<K, false>, decode<K, false>, get<K, false>, get16<K, false>};
}

namespace wide = probe::wide56;

// Keep the predecessor's native packet operations inline in the enclosing
// array loop, so an artificial external call every256 values is not its cost.
template<bool Region32>
void encode56(const std::uint64_t* __restrict in, std::uint8_t* __restrict out, std::size_t count) {
    __builtin_assume(count % 256 == 0);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    if constexpr (Region32) {
        for (std::size_t i = 0; i < count; i += 32) wide::encode_region32(in + i, out + 7 * i);
        return;
    }
#else
    static_assert(!Region32);
#endif
    for (std::size_t i = 0; i < count; i += wide::packet_values) {
        std::array<wide::Fragment, wide::fragments_per_packet> fragments;
        wide::unroll<wide::fragments_per_packet>([&](auto part) {
            fragments[part] = wide::load_values(in + i + part * wide::fragment_values);
        });
        wide::encode_packet(fragments, out + 7 * i);
    }
}

void decode56(const std::uint8_t* __restrict in, std::uint64_t* __restrict out, std::size_t count) {
    __builtin_assume(count % 256 == 0);
    for (std::size_t i = 0; i < count; i += wide::packet_values) {
        wide::unroll<wide::fragments_per_packet>([&](auto part) {
            wide::store_values(out + i + part * wide::fragment_values,
                wide::decode_fragment<part>(in + 7 * i));
        });
    }
}

std::uint64_t get56(const std::uint8_t* in, std::size_t index) {
    in += (index / wide::block_values) * wide::block_bytes;
    const auto local = static_cast<unsigned>(index % wide::block_values);
    __builtin_assume(local < wide::block_values);
    return wide::point(in + 7 * local);
}

void get16_56(const std::uint8_t* __restrict in, std::size_t index, std::uint64_t* __restrict out) {
    __builtin_assume(index % 16 == 0);
    in += (index / wide::block_values) * wide::block_bytes;
    const auto local = static_cast<unsigned>(index % wide::block_values);
    __builtin_assume(local < wide::block_values && local % 16 == 0);
    in += 7 * local;
    wide::unroll<2>([&](auto packet) {
        wide::unroll<wide::fragments_per_packet>([&](auto part) {
            wide::store_values(out + packet * wide::packet_values + part * wide::fragment_values,
                wide::decode_fragment<part>(in + packet * wide::packet_bytes));
        });
    });
}
}

predecessor_codec predecessor(unsigned width, bool striped) noexcept {
    switch (width) {
        case 1: return choose<1>(striped);
        case 2: return choose<2>(striped);
        case 3: return choose<3>(striped);
        case 4: return choose<4>(striped);
        case 5: return choose<5>(striped);
        case 6: return choose<6>(striped);
        case 7: return choose<7>(striped);
        default: return {};
    }
}

predecessor_u64_codec predecessor56(bool region32) noexcept {
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    if (region32) return {encode56<true>, decode56, get56, get16_56};
#endif
    if (region32) return {};
    return {encode56<false>, decode56, get56, get16_56};
}

const char* predecessor_target() noexcept {
#if defined(__ARM_FEATURE_SVE2_BITPERM)
    return "sve2";
#elif defined(__aarch64__)
    return "neon";
#elif defined(__AVX512VBMI__) && defined(__AVX512VBMI2__) && defined(__GFNI__)
    return "avx512";
#else
    return "avx2";
#endif
}
}
#else
namespace seriespack_measurement {
predecessor_codec predecessor(unsigned, bool) noexcept { return {}; }
predecessor_u64_codec predecessor56(bool) noexcept { return {}; }
const char* predecessor_target() noexcept { return "unavailable"; }
}
#endif

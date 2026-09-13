#include <algorithm>
#include <benchmark/benchmark.h>
#include <cassert>
#include <ikea/tuplepack/author/execution.h>
#include <numeric>
#include <string>
#include <vector>

namespace tp = ikea::tuplepack;
namespace d = tp::detail;
namespace n = tp::native;
namespace nd = tp::native::native_detail;
namespace pd = tp::native::packet_detail;

// Destination of every old row-major byte. Groups count decoded byte slots;
// unused per-row capacity forms a final group. Application types play no part.
template <unsigned N, unsigned Rows>
std::array<tp::byte, N> grouping(std::span<const unsigned> sizes) {
    std::array<tp::byte, N> out{};
    unsigned first = 0;
    auto append = [&](unsigned width) {
        assert(width && first + width <= N / Rows);
        for (unsigned row = 0; row < Rows; ++row)
            for (unsigned b = 0; b < width; ++b)
                out[row * (N / Rows) + first + b] = Rows * first + row * width + b;
        first += width;
    };
    for (auto size : sizes)
        append(size);
    if (first < N / Rows)
        append(N / Rows - first);
    return out;
}
struct plans {
    d::packet_read read;
    d::packet_write write;
    d::shuffle decode, encode, forward, inverse;
    std::array<tp::byte, 64> mapping;
    plans() {
        const auto layout = *tp::layout::make(
            2, std::array<tp::code, 3>{tp::code{0, 0, 8}, tp::code{1, 0, 4}, tp::code{1, 4, 4}});
        const std::array<tp::byte, 4> read_map{0, 1, 2, tp::hole};
        const std::array<tp::byte, 4> write_map{0, 1, tp::hole, tp::hole};
        read = *d::prepare_packet_read(layout, read_map, 16);
        write = *d::prepare_packet_write(layout, write_map, 16);
        mapping = grouping<64, 16>(std::array<unsigned, 2>{2, 1});
        d::shuffle_description forward_route, inverse_route, decoded, encoded;
        for (unsigned old = 0; old < 64; ++old) {
            forward_route.index[mapping[old]] = old;
            forward_route.mask[mapping[old]] = 255;
            inverse_route.index[old] = mapping[old];
            inverse_route.mask[old] = 255;
        }
        for (unsigned row = 0; row < 16; ++row) {
            for (unsigned slot = 0; slot < 3; ++slot) {
                const auto code = read.codes[slot];
                const auto out = mapping[row * 4 + slot];
                decoded.index[out] = row * 2 + code.offset;
                decoded.shift[out] = -code.shift;
                decoded.mask[out] = (1u << code.width) - 1;
            }
            for (unsigned slot = 0; slot < 2; ++slot) {
                encoded.index[row * 2 + slot] = mapping[row * 4 + slot];
                encoded.mask[row * 2 + slot] = slot == 0 ? 255 : 15;
            }
        }
        assert(read.place.grain == 2 && write.place.grain == 2);
        assert(write.round_count == 1 && write.needs_old);
        forward = d::compile_shuffle(forward_route, false);
        inverse = d::compile_shuffle(inverse_route, false);
        decode = d::compile_shuffle(decoded, false);
        encode = d::compile_shuffle(encoded, true);
    }
};
template <bool Integrated>
[[gnu::always_inline]] inline n::packet read(const plans &p, const tp::byte *data,
                                             std::size_t stride, std::uint64_t active) {
    if (!active)
        return nd::join(nd::zero16(), nd::zero16(), nd::zero16(), nd::zero16());
    auto address = [&](unsigned row) {
        assert(active & (1u << row));
        return data + row * stride;
    };
    if constexpr (Integrated)
        return n::transform<false>(pd::gather_bound<16>(p.read.place, address, active, stride),
                                   p.decode);
    else
        return n::transform<false>(n::read_body<16>(p.read, address, active, stride), p.forward);
}
template <bool Integrated>
[[gnu::always_inline]] inline void write(const plans &p, tp::byte *data, std::size_t stride,
                                         std::uint64_t active, n::packet input) {
    if (!active)
        return;
    auto address = [&](unsigned row) {
        assert(active & (1u << row));
        return data + row * stride;
    };
    if constexpr (Integrated) {
        auto old = pd::gather_bound<16>(p.write.place, address, active, stride);
        old = n::bit_and(old, n::load_packet(p.write.preserve.data()));
        auto updated = n::bit_or(old, n::transform<true>(input, p.encode));
        pd::scatter_bound<16>(p.write.place, address, active, stride, updated);
    } else
        n::write_body<16>(p.write, address, n::transform<false>(input, p.inverse), active, stride);
}
[[gnu::always_inline]] inline std::uint64_t consume(n::packet packet) {
#if defined(__aarch64__)
    const auto c = vcltq_u8(nd::split<2>(packet), vdupq_n_u8(8));
    const auto a = vreinterpretq_u16_u8(nd::split<0>(packet));
    const auto b = vreinterpretq_u16_u8(nd::split<1>(packet));
    const auto lo = vceqq_u16(vmovl_u8(vget_low_u8(c)), vdupq_n_u16(255));
    const auto hi = vceqq_u16(vmovl_u8(vget_high_u8(c)), vdupq_n_u16(255));
    return vaddvq_u16(vandq_u16(a, vandq_u16(lo, vcltq_u16(a, vdupq_n_u16(2048))))) +
           vaddvq_u16(vandq_u16(b, vandq_u16(hi, vcltq_u16(b, vdupq_n_u16(2048)))));
#else
    const auto ab = _mm256_set_m128i(nd::split<1>(packet), nd::split<0>(packet));
    const auto c = _mm256_cvtepi8_epi16(_mm_cmpgt_epi8(_mm_set1_epi8(8), nd::split<2>(packet)));
    const auto selected = _mm256_and_si256(c, _mm256_cmpgt_epi16(_mm256_set1_epi16(2048), ab));
    const auto sums = _mm256_madd_epi16(_mm256_and_si256(ab, selected), _mm256_set1_epi16(1));
    auto sum = _mm_add_epi32(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
    sum = _mm_hadd_epi32(sum, sum);
    return _mm_cvtsi128_si32(_mm_hadd_epi32(sum, sum));
#endif
}
[[gnu::always_inline]] inline n::packet increment(n::packet packet) {
#if defined(__aarch64__)
    auto add = [](auto part) {
        return vreinterpretq_u8_u16(
            vandq_u16(vaddq_u16(vreinterpretq_u16_u8(part), vdupq_n_u16(1)), vdupq_n_u16(4095)));
    };
    return nd::join(add(nd::split<0>(packet)), add(nd::split<1>(packet)), nd::split<2>(packet),
                    nd::split<3>(packet));
#else
    auto add = [](auto part) {
        return _mm_and_si128(_mm_add_epi16(part, _mm_set1_epi16(1)), _mm_set1_epi16(4095));
    };
    return nd::join(add(nd::split<0>(packet)), add(nd::split<1>(packet)), nd::split<2>(packet),
                    nd::split<3>(packet));
#endif
}
[[gnu::always_inline]] inline std::uint64_t consume_rows(n::packet packet) {
#if defined(__AVX512VBMI__)
    const auto ab = _mm512_and_si512(packet, _mm512_set1_epi32(65535));
    const auto c = _mm512_srli_epi32(packet, 16);
    const auto mask = _mm512_cmplt_epu32_mask(ab, _mm512_set1_epi32(2048)) &
                      _mm512_cmplt_epu32_mask(c, _mm512_set1_epi32(8));
    return _mm512_mask_reduce_add_epi32(mask, ab);
#elif defined(__aarch64__)
    auto sum = [](auto part) {
        const auto x = vreinterpretq_u32_u8(part);
        const auto ab = vandq_u32(x, vdupq_n_u32(65535));
        const auto mask = vandq_u32(vcltq_u32(ab, vdupq_n_u32(2048)),
                                    vcltq_u32(vshrq_n_u32(x, 16), vdupq_n_u32(8)));
        return vaddvq_u32(vandq_u32(ab, mask));
    };
    return sum(packet.a) + sum(packet.b) + sum(packet.c) + sum(packet.d);
#else
    auto selected = [](auto x) {
        const auto ab = _mm256_and_si256(x, _mm256_set1_epi32(65535));
        const auto mask =
            _mm256_and_si256(_mm256_cmpgt_epi32(_mm256_set1_epi32(2048), ab),
                             _mm256_cmpgt_epi32(_mm256_set1_epi32(8), _mm256_srli_epi32(x, 16)));
        return _mm256_and_si256(ab, mask);
    };
    const auto x = _mm256_add_epi32(selected(packet.a), selected(packet.b));
    auto sum = _mm_add_epi32(_mm256_castsi256_si128(x), _mm256_extracti128_si256(x, 1));
    sum = _mm_hadd_epi32(sum, sum);
    return _mm_cvtsi128_si32(_mm_hadd_epi32(sum, sum));
#endif
}
[[gnu::always_inline]] inline n::packet increment_rows(n::packet packet) {
#if defined(__AVX512VBMI__)
    return _mm512_or_si512(
        _mm512_and_si512(packet, _mm512_set1_epi32(int(0xffff0000))),
        _mm512_and_si512(_mm512_add_epi32(packet, _mm512_set1_epi32(1)), _mm512_set1_epi32(4095)));
#elif defined(__aarch64__)
    auto add = [](auto part) {
        const auto x = vreinterpretq_u32_u8(part);
        return vreinterpretq_u8_u32(
            vorrq_u32(vandq_u32(x, vdupq_n_u32(0xffff0000)),
                      vandq_u32(vaddq_u32(x, vdupq_n_u32(1)), vdupq_n_u32(4095))));
    };
    return {add(packet.a), add(packet.b), add(packet.c), add(packet.d)};
#else
    auto add = [](auto x) {
        return _mm256_or_si256(
            _mm256_and_si256(x, _mm256_set1_epi32(int(0xffff0000))),
            _mm256_and_si256(_mm256_add_epi32(x, _mm256_set1_epi32(1)), _mm256_set1_epi32(4095)));
    };
    return {add(packet.a), add(packet.b)};
#endif
}
void check(const plans &p) {
    const auto sample = grouping<64, 4>(std::array<unsigned, 2>{2, 1});
    for (unsigned row = 0; row < 4; ++row) {
        assert(sample[row * 16] == row * 2);
        assert(sample[row * 16 + 1] == row * 2 + 1);
        assert(sample[row * 16 + 2] == 8 + row);
    }
    const auto word = grouping<8, 2>(std::array<unsigned, 2>{2, 1});
    assert((word == std::array<tp::byte, 8>{0, 1, 4, 6, 2, 3, 5, 7}));
    for (auto stride : {2, 3, 16, 64})
        for (auto active : {0xffffu, 0x5555u, 0xaaaau, 0x7fffu, 1u, 0u}) {
            std::vector<tp::byte> before(16 * stride, 0xa6), a, b;
            for (unsigned row = 0; row < 16; ++row) {
                before[row * stride] = row * 17;
                before[row * stride + 1] = row * 31;
            }
            a = b = before;
            auto row_major = before;
            auto address = [&](unsigned row) {
                assert(active & (1u << row));
                return row_major.data() + row * stride;
            };
            const auto rows = n::read_body<16>(p.read, address, active, stride);
            auto x = read<true>(p, a.data(), stride, active);
            auto y = read<false>(p, b.data(), stride, active);
            std::array<tp::byte, 64> actual{}, control{}, expected{};
            n::store_packet(actual.data(), x);
            n::store_packet(control.data(), y);
            std::uint64_t sum = 0;
            for (unsigned row = 0; row < 16; ++row)
                if (active & (1u << row)) {
                    const auto aa = before[row * stride], bc = before[row * stride + 1];
                    expected[row * 2] = aa;
                    expected[row * 2 + 1] = bc & 15;
                    expected[32 + row] = bc >> 4;
                    const auto ab = aa | ((bc & 15) << 8);
                    if (ab < 2048 && (bc >> 4) < 8)
                        sum += ab;
                }
            assert(actual == expected && control == expected && consume(x) == sum &&
                   consume_rows(rows) == sum);
            write<true>(p, a.data(), stride, active, increment(x));
            write<false>(p, b.data(), stride, active, increment(y));
            n::write_body<16>(p.write, address, increment_rows(rows), active, stride);
            for (unsigned row = 0; row < 16; ++row)
                if (active & (1u << row)) {
                    auto &aa = before[row * stride];
                    auto &bc = before[row * stride + 1];
                    const auto next = ((aa | ((bc & 15) << 8)) + 1) & 4095;
                    aa = next;
                    bc = (bc & 0xf0) | (next >> 8);
                }
            assert(a == before && b == before && row_major == before);
        }
}
template <unsigned Style, bool Mutate> void measure(benchmark::State &state) {
    plans p;
    const unsigned stride = state.range(0), active = state.range(1);
    std::vector<tp::byte> bytes(8192 * stride);
    std::iota(bytes.begin(), bytes.end(), 0);
    const auto format = *tp::layout::make(
        2, std::array<tp::code, 3>{tp::code{0, 0, 8}, tp::code{1, 0, 4}, tp::code{1, 4, 4}});
    const std::array<tp::byte, 4> rm{0, 1, 2, tp::hole}, wm{0, 1, tp::hole, tp::hole};
    const std::array<unsigned, 2> groups{2, 1};
    const auto used = Style >= 6 ? 4 : 3;
    const auto shape = Style >= 6 ? std::span<const unsigned>{} : std::span<const unsigned>(groups);
    auto rp = *tp::reader<64, 16>::make(format, std::span(rm).first(used), shape);
    auto wp = *tp::writer<64, 16>::make(format, std::span(wm).first(used), shape);
    auto view = *tp::view::bind(format, bytes, 8192, stride);
    auto br = *tp::bind_reader(rp, view);
    auto bw = *tp::bind_writer(wp, view);
    auto nr = tp::native_reader(br);
    auto nw = tp::native_writer(bw);
    std::array<ikea::owner_write, 16> records;
    ikea::source_write_journal effects{records};
    benchmark::DoNotOptimize(p);
    for (auto _ : state) {
        std::uint64_t sum = 0;
        for (unsigned row = 0; row < 8192; row += 16) {
            if constexpr (Style >= 3) {
                auto x = [&] __attribute__((always_inline)) {
                    if constexpr (Style == 4 || Style == 6) {
                        auto value = br.get_unchecked(row, active);
                        return n::load_packet(value.data());
                    } else
                        return nr.get_unchecked(row, active);
                }();
                sum += Style >= 6 ? consume_rows(x) : consume(x);
                if constexpr (Mutate) {
                    auto next = Style >= 6 ? increment_rows(x) : increment(x);
                    effects.used = 0;
                    if constexpr (Style == 3)
                        n::write_body<16>(
                            wp.controls(),
                            [&](unsigned r) { return bytes.data() + (row + r) * stride; }, next,
                            active, stride);
                    else if constexpr (Style == 4 || Style == 6) {
                        std::array<tp::byte, 64> value;
                        n::store_packet(value.data(), next);
                        auto status = bw.set(row, value, effects, active);
                        benchmark::DoNotOptimize(status);
                        assert(status);
                    } else {
                        auto status = nw.set(row, next, effects, active);
                        benchmark::DoNotOptimize(status);
                        assert(status);
                    }
                }
            } else if constexpr (Style == 0) {
                auto address = [&](unsigned r) { return bytes.data() + (row + r) * stride; };
                auto x = n::read_body<16>(p.read, address, active, stride);
                sum += consume_rows(x);
                if constexpr (Mutate)
                    n::write_body<16>(p.write, address, increment_rows(x), active, stride);
            } else {
                auto x = read<Style == 1>(p, bytes.data() + row * stride, stride, active);
                sum += consume(x);
                if constexpr (Mutate)
                    write<Style == 1>(p, bytes.data() + row * stride, stride, active, increment(x));
            }
        }
        benchmark::DoNotOptimize(sum);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 512);
}
// Small packets test whether byte grouping earns its extraction/assembly cost
// in a GPR consumer. Both orders have exactly the same three-slot map.
template <bool Grouped, bool Mutate, bool Native> void measure_word(benchmark::State &state) {
    const auto format = *tp::layout::make(
        2, std::array<tp::code, 3>{tp::code{0, 0, 8}, tp::code{1, 0, 4}, tp::code{1, 4, 4}});
    const std::array<tp::byte, 3> rm{0, 1, 2}, wm{0, 1, tp::hole};
    const std::array<unsigned, 2> groups{2, 1};
    const auto grouping = Grouped ? std::span<const unsigned>(groups) : std::span<const unsigned>{};
    const auto rp = *tp::reader<8, 2>::make(format, rm, grouping);
    const auto wp = *tp::writer<8, 2>::make(format, wm, grouping);
    const unsigned stride = state.range(0), active = state.range(1);
    std::vector<tp::byte> data(8192 * stride);
    std::iota(data.begin(), data.end(), 0);
    auto view = *tp::view::bind(format, data, 8192, stride);
    auto br = *tp::bind_reader(rp, view);
    auto bw = *tp::bind_writer(wp, view);
    auto nr = tp::native_reader(br);
    std::array<ikea::owner_write, 2> records;
    ikea::source_write_journal effects{records};
    auto operation = [&](unsigned first) __attribute__((always_inline)) {
        auto value = Native ? nr.get_unchecked(first, active) : br.get_unchecked(first, active);
        std::uint64_t sum = 0, next = 0;
        for (unsigned r = 0; r < 2; ++r) {
            const unsigned ab_shift = 8 * (Grouped ? 2 * r : 3 * r);
            const unsigned c_shift = 8 * (Grouped ? 4 + r : 3 * r + 2);
            const auto ab = (value >> ab_shift) & 65535, c = (value >> c_shift) & 255;
            if (ab < 2048 && c < 8)
                sum += ab;
            next |= ((ab + 1) & 4095) << ab_shift;
        }
        if constexpr (Mutate) {
            effects.used = 0;
            if constexpr (Native)
                n::write_body<2>(
                    wp.controls(), [&](unsigned r) { return data.data() + (first + r) * stride; },
                    next, active, stride);
            else {
                auto status = bw.set(first, next, effects, active);
                benchmark::DoNotOptimize(status);
                assert(status);
            }
        }
        return sum;
    };
    // Check the measured operation against physical bits before timing.
    auto before = data;
    std::uint64_t expected_sum = 0;
    for (unsigned first = 0; first < 8192; first += 2) {
        std::uint64_t wanted = 0;
        for (unsigned r = 0; r < 2; ++r)
            if (active & (1u << r)) {
                const auto pos = (first + r) * stride;
                const auto ab = before[pos] | ((before[pos + 1] & 15) << 8),
                           c = before[pos + 1] >> 4;
                if (ab < 2048 && c < 8)
                    wanted += ab;
                if constexpr (Mutate) {
                    const auto next = (ab + 1) & 4095;
                    before[pos] = next;
                    before[pos + 1] = (before[pos + 1] & 0xf0) | (next >> 8);
                }
            }
        expected_sum += wanted;
        assert(operation(first) == wanted);
    }
    assert(data == before);
    benchmark::DoNotOptimize(expected_sum);
    for (auto _ : state) {
        std::uint64_t sum = 0;
        for (unsigned first = 0; first < 8192; first += 2)
            sum += operation(first);
        benchmark::DoNotOptimize(sum);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 4096);
}

int main(int argc, char **argv) {
    check(plans{});
    benchmark::Initialize(&argc, argv);
    auto add = [&](const char *name, auto function) {
        auto *b = benchmark::RegisterBenchmark(name, function);
        for (auto stride : {2, 3, 16, 64})
            for (auto active : {0xffff, 0x5555})
                b->Args({stride, active});
    };
    add("read/row-major", measure<0, false>);
    add("read/route-folded", measure<1, false>);
    add("read/explicit-transpose", measure<2, false>);
    add("update/row-major", measure<0, true>);
    add("update/route-folded", measure<1, true>);
    add("update/explicit-transpose", measure<2, true>);
    add("read/public-native", measure<3, false>);
    add("read/public-buffered", measure<4, false>);
    add("read/row-major-buffered", measure<6, false>);
    add("update/public-native-body", measure<3, true>);
    add("update/public-buffered-checked", measure<4, true>);
    add("update/public-native-checked", measure<5, true>);
    add("update/row-major-buffered-checked", measure<6, true>);
    add("update/row-major-native-checked", measure<7, true>);
    auto add_word = [&](const char *name, auto function) {
        auto *b = benchmark::RegisterBenchmark(name, function);
        for (auto stride : {2, 3, 16, 64})
            for (auto active : {3, 1})
                b->Args({stride, active});
    };
    add_word("word/read/rows-native", measure_word<false, false, true>);
    add_word("word/read/groups-native", measure_word<true, false, true>);
    add_word("word/read/rows-ordinary", measure_word<false, false, false>);
    add_word("word/read/groups-ordinary", measure_word<true, false, false>);
    add_word("word/update/rows-body", measure_word<false, true, true>);
    add_word("word/update/groups-body", measure_word<true, true, true>);
    add_word("word/update/rows-ordinary", measure_word<false, true, false>);
    add_word("word/update/groups-ordinary", measure_word<true, true, false>);
    benchmark::RunSpecifiedBenchmarks();
}

#include <ikea/hashtable/accumulate.h>
#include "accumulation-history.h"
#include <array>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
namespace ms = ikea::hashtable::detail::accumulation;
namespace ht = ikea::hashtable;
using ht::word;
using hashtable_test::identity;
using hashtable_test::payload;
template <unsigned W, unsigned sb, unsigned db, unsigned V, unsigned SE = W - sb,
          unsigned DE = W - db>
void history() {
    for (unsigned mode : {2u, 3u, 4u, 5u})
        hashtable_test::ordered_history<W, sb, db, V, SE, DE>(
            [&](auto& dst, auto sources, auto& reduce, std::size_t) {
                for (const auto* src : sources) {
                    const auto status = mode == 5   ? ht::accumulate(dst, *src, reduce)
                                        : mode == 4 ? ms::accumulate_images<true>(dst, *src, reduce)
                                        : mode == 3 ? ms::accumulate_images(dst, *src, reduce)
                                                    : ms::accumulate_points(dst, *src, reduce);
                    if (status != ht::accumulation_status::complete)
                        return false;
                }
                return true;
            });
}
void refusals() {
    using T = ht::table<24, 2, 8>;
    ht::permutation<24, 22> mix{713};
    T src(713), dst(713);
    for (unsigned q = 0; q < T::N + 80; ++q)
        src.insert(mix.recover(0, q), payload<8>(q, 1));
    const auto before = identity(src);
    auto reduce = [](const auto& a, const auto&) { return a; };
    ht::require(ht::accumulate(dst, src, reduce) == ms::merge_status::needs_overflow,
                "fixed overflow refusal"); // discard dst
    T alias(713);
    ht::require(ht::accumulate(alias, alias, reduce) == ms::merge_status::alias, "alias rejected");
    T foreign(714), fresh(713);
    ht::require(ht::accumulate(fresh, foreign, reduce) == ms::merge_status::incompatible,
                "seed rejected");
    T target(713);
    target.insert(mix.recover(0, 0), payload<8>(0, 0));
    ht::reserve_overflow(target, src.size() + 1);
    bool threw = false;
    try {
        (void)ht::accumulate(target, src, [](const auto&, const auto&) -> ht::bytes<8> {
            throw std::runtime_error("reducer");
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ht::require(threw && identity(src) == before,
                "throw leaves source unchanged"); // discard target
    // Cleanup after sticky overflow saturation and tombstones happens outside merge.
    T saturated(713);
    for (unsigned q = 0; q < 340; ++q)
        saturated.insert(mix.recover(0, q), payload<8>(q, 0));
    for (unsigned q = 0; q < 200; ++q)
        saturated.erase(mix.recover(0, q));
    ht::reserve_overflow(saturated, 400);
    saturated.validate();
}
template <unsigned V, bool Wide = false> void construction_failures() {
    using S = std::conditional_t<Wide, ht::table<57, 10, V, 64>, ht::table<40, 10, V, 30>>;
    using D = std::conditional_t<Wide, ht::table<57, 12, V, 46>, ht::table<40, 13, V, 29>>;
    typename S::mix_type mix{713};
    S source(713);
    for (unsigned q = 0; q < S::N + 80; ++q)
        source.insert(mix.recover(0, q), payload<V>(q, 1));
    const auto hash = identity(source);
    auto invoke = [](auto& dst, const auto& src, auto&& reduce, unsigned mode) {
        if (mode == 0)
            return ms::accumulate_images(dst, src, reduce);
        if (mode == 1)
            return ms::accumulate_images<true>(dst, src, reduce);
        if (mode == 2)
            return ms::accumulate_points(dst, src, reduce);
        return ht::accumulate(dst, src, reduce);
    };
    for (unsigned mode = 0; mode < 4; ++mode) {
        D small(713);
        auto keep = [](const auto& a, const auto&) { return a; };
        ht::require(invoke(small, source, keep, mode) == ms::merge_status::needs_overflow,
                    "construction refuses backing exhaustion after partial image updates");
        D target(713);
        target.insert(mix.recover(0, 0), payload<V>(0, 0));
        ht::reserve_overflow(target, source.size() + 1);
        bool threw = false;
        try {
            (void)invoke(
                target, source,
                [](const auto&, const auto&) -> ht::bytes<V> {
                    throw std::runtime_error("construction reducer");
                },
                mode);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        ht::require(threw && identity(source) == hash,
                    "construction exception preserves complete source");
        D alias(713);
        ht::require(invoke(alias, alias, keep, mode) == ms::merge_status::alias,
                    "construction rejects alias");
        S foreign(714);
        D fresh(713);
        ht::require(invoke(fresh, foreign, keep, mode) == ms::merge_status::incompatible,
                    "construction rejects seed mismatch");
        ht::require(identity(source) == hash, "construction refusals preserve complete source");
    }
    std::cout << "FAILURE_PASS," << V << '\n';
}
#if defined(__AVX512VBMI__) && defined(__GFNI__)
template <unsigned E> void packed_roundtrip() {
    constexpr unsigned tail = E % 8;
    std::mt19937_64 random(0x873625 + E);
    for (unsigned trial = 0; trial < 200; ++trial) {
        alignas(64) std::array<std::uint8_t, 64> actual, expected;
        alignas(64) std::array<std::uint32_t, 16> q, read;
        for (auto& b : actual)
            b = std::uint8_t(random());
        actual[63] = trial % 2 ? 255 : 0;
        expected = actual;
        for (unsigned r = 0; r < 16; ++r)
            q[r] = r == 0 ? 0 : r == 1 ? ht::mask<E> : random() & ht::mask<E>;
        const unsigned live = unsigned(random()) & 65535;
        for (unsigned t = 0; t < 2; ++t) {
            for (unsigned r = 0; r < 8; ++r)
                for (unsigned j = 0; j < 3; ++j)
                    expected[t * E + r * 3 + j] = std::uint8_t(q[t * 8 + r] >> (tail + j * 8));
            for (unsigned bit = 0; bit < tail; ++bit) {
                std::uint8_t plane = 0;
                for (unsigned r = 0; r < 8; ++r)
                    plane |= std::uint8_t(((q[t * 8 + r] >> bit) & 1) << r);
                expected[t * E + 24 + bit] = plane;
            }
        }
        expected[2 * E] = std::uint8_t(~live);
        expected[2 * E + 1] = std::uint8_t(~(live >> 8));
        ms::write_packed<E>(actual.data(), q.data(), live);
        ht::require(actual == expected,
                    "whole-bucket pack independent byte oracle and metadata preservation");
        _mm512_store_si512(read.data(), ms::read_packed<E>(actual.data()));
        ht::require(read == q, "whole-bucket decode complete quotients");
    }
    std::cout << "PACKED_PASS," << E << '\n';
}
#endif
#if defined(__AVX512VBMI__) && defined(__GFNI__)
template <unsigned E, unsigned T> void wide_roundtrip() {
    constexpr unsigned N = T * 8, B = E / 8, R = E % 8;
    std::mt19937_64 rng(816273 + E * 19 + T);
    for (unsigned trial = 0; trial < 100; ++trial) {
        std::array<std::uint8_t, 112> actual, expected;
        std::array<word, N> q, read;
        for (auto& x : actual)
            x = std::uint8_t(rng());
        actual[8 + 95] = trial % 2 ? 255 : 0;
        expected = actual;
        const auto initial = actual;
        for (unsigned row = 0; row < N; ++row)
            q[row] = row == 0 ? 0 : row == 1 ? ht::mask<E> : rng() & ht::mask<E>;
        const auto live = rng() & ht::mask<N>;
        for (unsigned t = 0; t < T; ++t) {
            for (unsigned row = 0; row < 8; ++row)
                for (unsigned j = 0; j < B; ++j)
                    expected[8 + t * E + row * B + j] = std::uint8_t(q[t * 8 + row] >> (R + j * 8));
            for (unsigned bit = 0; bit < R; ++bit) {
                std::uint8_t plane = 0;
                for (unsigned row = 0; row < 8; ++row)
                    plane |= std::uint8_t(((q[t * 8 + row] >> bit) & 1) << row);
                expected[8 + t * E + 8 * B + bit] = plane;
            }
            expected[8 + T * E + t] = std::uint8_t((~live) >> (t * 8));
        }
        ms::wide_image<T> image;
        ht::each<T>([&](auto tile) { image[tile] = _mm512_loadu_si512(q.data() + tile * 8); });
        ms::write_wide<E, T>(actual.data() + 8, image, live);
        ht::require(actual == expected,
                    "96-byte pack exact independent bytes, slack, sticky count and guards");
        const auto decoded = ms::read_wide<E, T>(actual.data() + 8);
        ht::each<T>([&](auto tile) { _mm512_storeu_si512(read.data() + tile * 8, decoded[tile]); });
        ht::require(q == read, "96-byte complete quotient decode");
        if constexpr (E <= 32) {
            using D = ht::table<31, 0, 0, E>;
            using Codec = ms::image_codec<D>;
            std::array<std::uint32_t, N> input, output;
            for (unsigned row = 0; row < N; ++row)
                input[row] = std::uint32_t(q[row]);
            typename Codec::image compact{_mm512_loadu_si512(input.data())};
            auto compact_bytes = initial;
            Codec::write(compact_bytes.data() + 8, compact, live);
            ht::require(compact_bytes == expected,
                        "compact 96-byte codec independent complete bytes");
            const auto compact_read = Codec::read(compact_bytes.data() + 8);
            _mm512_storeu_si512(output.data(), compact_read[0]);
            ht::require(output == input, "compact 96-byte codec complete values");
            if constexpr (E == 32) {
                std::array<word, N + 2> prepared;
                prepared.fill(0x9182736455aabbccULL);
                ms::prepare_source<D>(expected.data() + 8, prepared.data() + 1);
                ht::require(prepared.front() == 0x9182736455aabbccULL &&
                                prepared.back() == 0x9182736455aabbccULL,
                            "32-bit source widening exact output extent");
                for (unsigned row = 0; row < N; ++row)
                    ht::require(prepared[row + 1] == q[row],
                                "unsigned source widening independent bytes");
            }
        }
    }
    std::cout << "WIDE_PASS," << E << ',' << T << '\n';
}
template <unsigned E, unsigned T> void flat_roundtrip() {
    constexpr unsigned N = T * 8, B = E / 8, R = E % 8, Banks = (T + 1) / 2;
    std::mt19937_64 random(0x125783 + E * 17 + T);
    for (unsigned trial = 0; trial < 100; ++trial) {
        alignas(64) std::array<std::uint8_t, 64> actual, expected;
        alignas(64) std::array<std::uint32_t, N> q, read;
        for (auto& b : actual)
            b = std::uint8_t(random());
        actual[63] = trial % 2 ? 255 : 0;
        expected = actual;
        for (unsigned r = 0; r < N; ++r)
            q[r] = r == 0 ? 0 : r == 1 ? ht::mask<E> : random() & ht::mask<E>;
        const auto live = random() & ht::mask<N>;
        for (unsigned t = 0; t < T; ++t) {
            for (unsigned r = 0; r < 8; ++r)
                for (unsigned j = 0; j < B; ++j)
                    expected[t * E + r * B + j] = std::uint8_t(q[t * 8 + r] >> (R + j * 8));
            for (unsigned bit = 0; bit < R; ++bit) {
                std::uint8_t plane = 0;
                for (unsigned r = 0; r < 8; ++r)
                    plane |= std::uint8_t(((q[t * 8 + r] >> bit) & 1) << r);
                expected[t * E + 8 * B + bit] = plane;
            }
        }
        for (unsigned t = 0; t < T; ++t)
            expected[T * E + t] = std::uint8_t((~live) >> (t * 8));
        ms::flat_image<T> input;
        ht::each<Banks>([&](auto bank) __attribute__((always_inline)) {
            constexpr unsigned Bank = decltype(bank)::value;
            constexpr auto valid = std::min(16u, N - Bank * 16);
            input[bank] = _mm512_maskz_loadu_epi32((1u << valid) - 1, q.data() + Bank * 16);
        });
        ms::write_flat<E, T>(actual.data(), input, live);
        ht::require(actual == expected, "general flat pack independent bytes and sticky overflow");
        const auto output = ms::read_flat<E, T>(actual.data());
        ht::each<Banks>([&](auto bank) __attribute__((always_inline)) {
            constexpr unsigned Bank = decltype(bank)::value;
            constexpr auto valid = std::min(16u, N - Bank * 16);
            _mm512_mask_storeu_epi32(read.data() + Bank * 16, (1u << valid) - 1, output[bank]);
        });
        ht::require(read == q, "general flat decode full quotients");
    }
    std::cout << "FLAT_PASS," << E << ',' << T << '\n';
}
#endif
template <unsigned R> void column_oracle() {
    for (unsigned v = 0; v < (1u << R); ++v) {
        word want = 0;
        for (unsigned bit = 0; bit < R; ++bit)
            want |= word((v >> bit) & 1) << (8 * bit);
        ht::require(ms::spread_columns<R>(v) == want,
                    "exhaustive regular deposit independent bits");
    }
}
template <unsigned E> void point_oracle() {
    constexpr unsigned B = E / 8, R = E % 8;
    std::mt19937_64 rng(E + 1821);
    for (unsigned row = 0; row < 8; ++row)
        for (unsigned trial = 0; trial < 40; ++trial) {
            std::array<std::uint8_t, E + 16> actual, expected;
            for (auto& b : actual)
                b = std::uint8_t(rng());
            expected = actual;
            const word q = trial == 0 ? 0 : trial == 1 ? ht::mask<E> : rng() & ht::mask<E>;
            for (unsigned j = 0; j < B; ++j)
                expected[8 + row * B + j] = std::uint8_t(q >> (R + j * 8));
            for (unsigned bit = 0; bit < R; ++bit)
                expected[8 + 8 * B + bit] = std::uint8_t((expected[8 + 8 * B + bit] & ~(1u << row)) |
                                                     (((q >> bit) & 1) << row));
            ms::assign_point<E>(actual.data() + 8, row, q);
            ht::require(actual == expected, "point key store exact bytes and row/extent guards");
        }
}
template <class D, bool Values> void prefetch_oracle() {
    for (std::size_t start = 0; start < 64; start += D::bucket_alignment) {
        std::array<bool, (D::stride + 63) / 64 + 2> wanted{}, actual{};
        for (unsigned i = 0; i < D::B; ++i)
            wanted[(start + i) / 64] = true;
        if constexpr (Values && std::tuple_size_v<typename D::value_type> == 8)
            for (unsigned row = 0; row < D::N; ++row)
                for (unsigned i = 0; i < 8; ++i)
                    wanted[(start + D::value_offset + row * 8 + i) / 64] = true;
        unsigned requests = 0;
        ms::primary_prefetch_offsets<D, Values>([&](std::size_t offset) {
            ht::require(offset < D::stride, "prefetch stays within bucket");
            actual[(start + offset) / 64] = true;
            ++requests;
        });
        ht::require(actual == wanted,
                    "prefetch covers every key/value cache line at every admitted alignment");
        if constexpr (D::bucket_alignment == 64 && Values &&
                      std::tuple_size_v<typename D::value_type> == 8)
            ht::require(requests == std::count(wanted.begin(), wanted.end(), true),
                        "aligned maps retain minimal prefetch count");
    }
}
template <unsigned V> void prefetch_layouts() {
    auto check = []<class D>() {
        prefetch_oracle<D, false>();
        prefetch_oracle<D, true>();
    };
    check.template operator()<ht::table<40, 10, V, 30>>();
    check.template operator()<ht::table<24, 10, V, 14>>();
    check.template operator()<ht::table<21, 10, V, 11>>();
    check.template operator()<ht::table<41, 10, V, 32>>();
    check.template operator()<ht::table<57, 10, V, 64>>();
    check.template operator()<ht::table<4, 1, V, 3>>();
    check.template operator()<ht::table<5, 1, V, 4>>();
    check.template operator()<ht::table<6, 1, V, 5>>();
}
void descriptor_accumulation() {
    using Descriptor = ht::erased_table<24, 8>;
    auto first = Descriptor::explicit_encoding<10, 14>(713);
    auto second = Descriptor::explicit_encoding<10, 14>(713);
    auto result = Descriptor::explicit_encoding<13, 11>(713, nullptr, ht::write_strategy::existing);
    ht::bytes<8> initial{}, a{}, b{};
    for (unsigned i = 0; i < 8; ++i) {
        initial[i] = std::byte(i + 1);
        a[i] = std::byte(i + 3);
        b[i] = std::byte(i + 5);
    }
    first.insert(42, a);
    second.insert(42, b);
    result.insert(42, initial);
    result.reserve_overflow<13, 11>(3);
    const auto* functions = result.functions();
    unsigned calls = 0;
    auto reduce = [&](const auto& left, const auto& right) {
        ++calls;
        auto out = left;
        for (unsigned i = 0; i < 8; ++i)
            out[i] = std::byte(std::to_integer<unsigned>(left[i]) * 3 + std::to_integer<unsigned>(right[i]) * 5 + 7);
        return out;
    };
    ht::require(result.accumulate<13, 11, ht::search_kernel::generic, 10, 14>(first, reduce) ==
                    ht::accumulation_status::complete,
                "descriptor first ordered source");
    ht::require(result.accumulate<13, 11, ht::search_kernel::generic, 10, 14>(second, reduce) ==
                    ht::accumulation_status::complete,
                "descriptor second ordered source");
    result.read().find(42, [&](const auto& value) {
        for (unsigned i = 0; i < 8; ++i) {
            const auto intermediate = std::byte((i + 1) * 3 + (i + 3) * 5 + 7);
            ht::require(value[i] == std::byte(std::to_integer<unsigned>(intermediate) * 3 + (i + 5) * 5 + 7),
                        "descriptor independent non-associative source order");
        }
    });
    ht::require(calls == 2 && result.functions() == functions &&
                    result.writes() == ht::write_strategy::existing,
                "descriptor accumulation neither replaces backing nor dispatch");
    first.read().find(
        42, [&](const auto& value) { ht::require(value == a, "descriptor source preserved"); });
    auto copy = first.clone();
    ht::require(copy.functions() == first.functions(), "explicit clone retains binding");
    ht::require(copy.accumulate<10, 14>(first, reduce) == ht::accumulation_status::complete,
                "explicit caller clone is independent");
    ht::require(first.accumulate<10, 14>(first, reduce) == ht::accumulation_status::alias,
                "descriptor self alias refuses");
    auto foreign = Descriptor::explicit_encoding<10, 14>(714);
    ht::require(copy.accumulate<10, 14>(foreign, reduce) == ht::accumulation_status::incompatible,
                "descriptor seed mismatch refuses");
    result.validate();
    first.validate();
    second.validate();
}
int main() {
    try {
        descriptor_accumulation();
        prefetch_layouts<0>();
        prefetch_layouts<3>();
        prefetch_layouts<8>();
        prefetch_layouts<9>();
        prefetch_layouts<64>();
        std::cout << "PREFETCH_PASS,all admitted cacheline alignments\n";
        history<1, 0, 1, 0>();
        history<8, 1, 0, 3>();
        history<10, 0, 1, 9>();
        history<15, 1, 2, 8, 14, 14>();
        history<24, 3, 1, 8>();
        history<32, 1, 2, 64>();
        history<34, 1, 4, 8, 40, 30>();
        history<24, 10, 13, 8, 14, 11>();
        history<24, 10, 13, 64, 14, 11>();
        history<24, 10, 11, 8, 14, 14>();
        history<40, 10, 13, 8, 30, 28>();
        history<40, 10, 13, 3, 30, 29>();
        history<40, 10, 13, 9, 30, 29>();
        history<34, 10, 10, 8, 24, 24>();
        history<40, 10, 13, 8, 30, 30>();
        history<40, 10, 13, 64, 30, 30>();
        history<45, 15, 18, 8, 30, 29>();
        history<64, 0, 1, 8, 64, 64>();
        history<24, 2, 2, 0>();
        history<24, 2, 2, 9>();
        history<57, 10, 12, 8, 64, 46>();
        history<57, 10, 12, 64, 64, 46>();
        history<56, 10, 10, 8, 46, 46>();
        history<48, 8, 9, 3, 40, 40>();
        history<41, 10, 10, 8, 31, 31>();
        history<41, 10, 11, 8, 31, 30>();
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        packed_roundtrip<24>();
        packed_roundtrip<25>();
        packed_roundtrip<26>();
        packed_roundtrip<27>();
        packed_roundtrip<28>();
        packed_roundtrip<29>();
        packed_roundtrip<30>();
#endif
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        flat_roundtrip<10, 5>();
        flat_roundtrip<11, 5>();
        flat_roundtrip<12, 4>();
        flat_roundtrip<13, 4>();
        flat_roundtrip<14, 4>();
        flat_roundtrip<15, 3>();
        flat_roundtrip<16, 3>();
        flat_roundtrip<17, 3>();
        flat_roundtrip<18, 3>();
        flat_roundtrip<19, 3>();
        flat_roundtrip<20, 3>();
        flat_roundtrip<21, 2>();
        flat_roundtrip<22, 2>();
        flat_roundtrip<23, 2>();
        ht::each<16>([](auto i) { wide_roundtrip<decltype(i)::value + 31, 2>(); });
        ht::each<18>([](auto i) { wide_roundtrip<decltype(i)::value + 47, 1>(); });
#endif
        column_oracle<1>();
        column_oracle<2>();
        column_oracle<3>();
        column_oracle<4>();
        column_oracle<5>();
        column_oracle<6>();
        column_oracle<7>();
        ht::each<64>([](auto i) { point_oracle<decltype(i)::value + 1>(); });
        refusals();
        construction_failures<8>();
        construction_failures<64>();
        construction_failures<8, true>();
        construction_failures<64, true>();
        std::cout << "PASS ordered fixed-capacity mutable accumulation histories\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#include <ikea/seriespack/composition_neon.h>
#include <ikea/seriespack/composition_x86.h>
#include <ikea/seriespack/detail/physical.h>
#include <ikea/seriespack/view.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#if defined(__AVX2__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(__aarch64__) || defined(__AVX2__)

namespace sp = ikea::seriespack;
#if defined(__aarch64__)
namespace native = sp::neon;
namespace body = sp::detail::neon;
inline constexpr const char* target_name = "NEON";
inline constexpr unsigned register_bytes = 16;
#elif defined(__AVX512BW__) && defined(__AVX512VBMI__)
namespace native = sp::avx512;
namespace body = sp::detail::avx512;
inline constexpr const char* target_name = "AVX-512";
inline constexpr unsigned register_bytes = 64;
#else
namespace native = sp::avx2;
namespace body = sp::detail::avx2;
inline constexpr const char* target_name = "AVX2";
inline constexpr unsigned register_bytes = 32;
#endif

namespace {

std::uint64_t cases = 0;
unsigned formats = 0;
constexpr std::size_t count = 768;

template<unsigned Bits>
constexpr std::uint64_t low_mask = [] {
    if constexpr (Bits == 64) return ~std::uint64_t{0};
    else return (std::uint64_t{1} << Bits) - 1;
}();

[[noreturn]] void fail(const char* operation, unsigned k, unsigned h,
                       sp::geometry g, std::size_t index) {
    std::fprintf(stderr, "%s %s: K=%u H=%u geometry=%s index=%zu case=%llu\n",
        target_name, operation, k, h, g == sp::geometry::local8 ? "local8" : "striped",
        index, static_cast<unsigned long long>(cases));
    std::abort();
}

std::uint64_t scramble(std::uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Owners retain exact declared envelopes with explicit foreign stride gaps.
// Values are generated before encoding and are the independent sum/predicate
// oracle throughout. The separately checked scalar codec only creates wire data.
template<class Format>
struct fixture {
    static constexpr auto d = Format::layout;
    static constexpr unsigned K = d.width, H = d.head_bits, W = K - H;
    static constexpr std::size_t T = Format::payload::tile_values;
    static constexpr std::size_t B = Format::payload::tile_bytes;
    static constexpr std::size_t tiles = count / T;
    static constexpr std::size_t stride = W == 0 ? 0 : B + (d.storage == sp::geometry::striped ? 32 : 7);
    static constexpr std::size_t head_stride = T + 5;
    static constexpr std::size_t envelope = W == 0 ? 0 : (tiles - 1) * stride + B;
    static constexpr std::size_t head_envelope = (tiles - 1) * head_stride + T;

    std::array<std::uint64_t, count> original{};
    std::vector<std::byte> payload_owner;
    std::array<std::vector<std::byte>, 2> head_owner;
    sp::basic_placement<const std::byte> placement{};

    explicit fixture(std::uint64_t seed) : payload_owner(envelope + 64, std::byte{0xa5}) {
        for (std::size_t i = 0; i != count - 1; ++i) {
            auto value = scramble(i + seed) & low_mask<K>;
            switch ((i + seed) % 7) {
                case 0: value = low_mask<K>; break;
                case 1: value = low_mask<K> - 1; break;
                case 2: value = 0; break;
                case 3: value = 1; break;
                case 4: value = std::uint64_t{1} << (K - 1); break;
                default: break;
            }
            original[i] = value;
        }
        // One canonical zero slack row is physically present but not logical.
        original.back() = 0;
        if constexpr (W != 0) {
            auto* p = payload_owner.data();
            p += (-reinterpret_cast<std::uintptr_t>(p)) & 63u;
            placement.payload = {{p, envelope}, stride};
            for (std::size_t t = 0; t != tiles; ++t)
                sp::detail::encode_low_tile<W, d.storage>(original.data() + t * T,
                    reinterpret_cast<std::uint8_t*>(p + t * stride));
        }
        for (unsigned plane = 0; plane != H / 8; ++plane) {
            head_owner[plane].resize(head_envelope, std::byte{0xa5});
            auto* p = head_owner[plane].data();
            placement.heads[plane] = {{p, head_envelope}, head_stride};
            for (std::size_t t = 0; t != tiles; ++t)
                for (std::size_t i = 0; i != T; ++i)
                    p[t * head_stride + i] = std::byte(original[t * T + i] >> (K - 8 * (plane + 1)));
        }
    }

    auto view() const {
        return sp::static_const_view<Format>::assume_valid(count - 1, placement);
    }
};

template<class Ops>
std::uint64_t mask_bits(typename Ops::mask_type mask) {
#if defined(__AVX512BW__) && defined(__AVX512VBMI__) && !defined(__aarch64__)
    return static_cast<std::uint64_t>(mask);
#else
    using UInt = sp::scalar_for_width<Ops::L * 8>;
    std::array<UInt, register_bytes / Ops::L> values{};
#if defined(__aarch64__)
    vst1q_u8(reinterpret_cast<std::uint8_t*>(values.data()), mask);
#else
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(values.data()), mask);
#endif
    std::uint64_t bits = 0;
    for (unsigned i = 0; i != values.size(); ++i) {
        if (values[i] == std::numeric_limits<UInt>::max()) bits |= std::uint64_t{1} << i;
        else if (values[i] != 0)
            fail("noncanonical predicate", Ops::layout.width, Ops::layout.head_bits, Ops::G, i);
    }
    return bits;
#endif
}

template<class Format, unsigned Begin>
[[gnu::noinline]] void check_region(const fixture<Format>& a, const fixture<Format>& b,
                                    std::size_t tile) {
    using Ops = native::composition_ops<Format, Begin>;
    constexpr auto d = Format::layout;
    constexpr unsigned N = Ops::lanes;
    constexpr unsigned R = (d.width - d.head_bits) % 8;
    const auto av = a.view();
    const auto bv = b.view();
    const auto ae = sp::composition::describe(av);
    const auto be = sp::composition::describe(bv);
    const decltype(ae) substituted{{ae.payload.body, be.payload.tail}, ae.head0, ae.head1};
    const native::tile_position rows{tile};
    const std::size_t origin = tile * Ops::tile_values + Begin;
    std::uint64_t valid = 0;
    for (unsigned lane = 0; lane != N; ++lane) {
        if (Ops::original_index(rows, lane) != origin + lane)
            fail("original index", d.width, d.head_bits, d.storage, origin + lane);
        if (origin + lane < count - 1) valid |= std::uint64_t{1} << lane;
    }
    if (mask_bits<Ops>(Ops::active_all()) != low_mask<N>)
        fail("all-active mask", d.width, d.head_bits, d.storage, origin);
    const std::array masks{std::uint64_t{0}, low_mask<N>, std::uint64_t{1},
        std::uint64_t{1} << (N - 1), std::uint64_t{0xaaaaaaaaaaaaaaaaULL},
        std::uint64_t{0x5555555555555555ULL}, scramble(origin), ~std::uint64_t{0}};
    const std::array constants{std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{2},
        low_mask<d.width>, std::uint64_t{1} << (d.width - 1),
        (std::uint64_t{1} << (d.width - 1)) - 1,
        (std::uint64_t{1} << (d.width - 1)) + 1, ~std::uint64_t{0},
        low_mask<d.width> + (d.width != 64), a.original[origin], b.original[origin]};

    Ops ops;
    if constexpr (d.width < 64) {
        if (origin + N <= count - 1) {
            std::uint64_t expected_all = 0;
            for (unsigned lane = 0; lane != N; ++lane) expected_all += a.original[origin + lane];
            const auto got = sp::composition::selected_sum(ops, ae, rows, Ops::active_all(),
                std::uint64_t{1} << d.width);
            if (got != expected_all) fail("constant all-selected", d.width, d.head_bits, d.storage, origin);
            ++cases;
        }
    }
    for (const auto raw_mask : masks) {
        const auto bits = raw_mask & valid;
        const auto active = Ops::active_bits(bits);
        if (mask_bits<Ops>(active) != bits)
            fail("active adapter", d.width, d.head_bits, d.storage, origin);
        auto values = sp::composition::read(ops, ae, rows, active);
        // Keep the first register value alive across another producer. This is
        // ordinary by-value reuse, not an adapter that stores/reloads an array.
        auto other = sp::composition::read(ops, be, rows, active);
        auto mixed = sp::composition::read(ops, substituted, rows, active);
        std::uint64_t expected_a = 0, expected_b = 0, expected_mixed = 0;
        for (unsigned lane = 0; lane != N; ++lane) {
            if (((bits >> lane) & 1) == 0) continue;
            expected_a += a.original[origin + lane];
            expected_b += b.original[origin + lane];
            expected_mixed += (a.original[origin + lane] & ~low_mask<R>) |
                              (b.original[origin + lane] & low_mask<R>);
        }
        if (ops.sum(values, active, sp::modulo_u64_sum{}) != expected_a ||
            ops.sum(other, active, sp::modulo_u64_sum{}) != expected_b ||
            ops.sum(mixed, active, sp::modulo_u64_sum{}) != expected_mixed)
            fail("sum/reuse/independent tail", d.width, d.head_bits, d.storage, origin);
        for (auto cutoff : constants) {
            std::uint64_t expected_less = 0, expected_equal = 0, expected_sum = 0;
            for (unsigned lane = 0; lane != N; ++lane) {
                if (((bits >> lane) & 1) == 0) continue;
                const auto value = a.original[origin + lane];
                if (value < cutoff) {
                    expected_less |= std::uint64_t{1} << lane;
                    expected_sum += value;
                }
                if (value == cutoff) expected_equal |= std::uint64_t{1} << lane;
            }
            const auto keep = ops.unsigned_less(values, cutoff, active);
            if (mask_bits<Ops>(keep) != expected_less ||
                mask_bits<Ops>(ops.unsigned_equal(values, cutoff, active)) != expected_equal)
                fail("unsigned predicate", d.width, d.head_bits, d.storage, origin);
            if (ops.sum(values, keep, sp::modulo_u64_sum{}) != expected_sum ||
                sp::composition::selected_sum(ops, ae, rows, active, cutoff) != expected_sum)
                fail("authored selected sum", d.width, d.head_bits, d.storage, origin);
            ++cases;
        }
    }
}

template<class Format>
[[gnu::noinline]] void check_format() {
    fixture<Format> a(31), b(74);
    constexpr unsigned N = native::composition_ops<Format, 0>::lanes;
    constexpr unsigned last = Format::payload::tile_values - N;
    check_region<Format, 0>(a, b, 1);
    check_region<Format, last>(a, b, fixture<Format>::tiles - 1);
    ++formats;
}

void check_mixed_formats() {
    // Both representations use full-value u64 lanes on every target. Their tile
    // ordinals differ; explicit coordinates align the same original positions.
    using A = sp::static_format<36, sp::geometry::local8, 16>;
    using B = sp::static_format<36, sp::geometry::striped, 16>;
    using AO = native::composition_ops<A, 0>;
    using BO = native::composition_ops<B, 40>;
    static_assert(AO::lanes == BO::lanes);
    fixture<A> a(19);
    fixture<B> b(43);
    const auto av = a.view();
    const auto bv = b.view();
    const auto ae = sp::composition::describe(av);
    const auto be = sp::composition::describe(bv);
    AO ao;
    BO bo;
    const native::tile_position ar{13}, br{1}; // 13*8 == 1*64+40.
    const auto active_a = AO::active_bits(0x55);
    const auto active_b = BO::active_bits(0x55);
    auto a_values = sp::composition::read(ao, ae, ar, active_a);
    auto b_values = sp::composition::read(bo, be, br, active_b);
    std::uint64_t expected_a = 0, expected_b = 0;
    for (unsigned lane = 0; lane != AO::lanes; ++lane) {
        const auto index = AO::original_index(ar, lane);
        if (index != BO::original_index(br, lane)) fail("mixed coordinates", 36, 16, A::layout.storage, index);
        if ((0x55 >> lane) & 1) {
            expected_a += a.original[index];
            expected_b += b.original[index];
        }
    }
    if (ao.sum(a_values, active_a, sp::modulo_u64_sum{}) != expected_a ||
        bo.sum(b_values, active_b, sp::modulo_u64_sum{}) != expected_b)
        fail("mixed representation reuse", 36, 16, A::layout.storage, 104);
    ++cases;
}

#if defined(__AVX2__)
// Put one exact owner against a prefix guard and the independent replacement
// against a suffix guard. Together these placements exercise both boundaries
// of the declared envelopes; the native reads have no padding permission.
template<unsigned K, unsigned N>
struct dense_fixture {
    using Format = sp::static_format<K, sp::geometry::local8>;
    static constexpr std::size_t size = 2 * N + 8;
    static constexpr std::size_t bytes = size / 8 * K;
    std::array<std::uint8_t, size> original{};
    std::size_t page;
    void* mapping;
    sp::basic_placement<const std::byte> placement{};

    dense_fixture(std::uint64_t seed, bool suffix)
        : page(static_cast<std::size_t>(sysconf(_SC_PAGESIZE))),
          mapping(mmap(nullptr, 3 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) {
        if (mapping == MAP_FAILED || bytes > page ||
            mprotect(static_cast<std::byte*>(mapping) + page, page, PROT_READ | PROT_WRITE) != 0)
            fail("dense guard allocation", K, 0, sp::geometry::local8, N);
        auto* p = static_cast<std::byte*>(mapping) + page + (suffix ? page - bytes : 0);
        placement.payload = {{p, bytes}, K};
        for (std::size_t i = 0; i != size; ++i) {
            auto value = scramble(seed + i) & low_mask<K>;
            if (i % 5 == 0) value = low_mask<K>;
            if (i % 7 == 0) value = 0;
            original[i] = static_cast<std::uint8_t>(value);
        }
        // Also permits a separately attached size-1 view with canonical slack.
        original.back() = 0;
        for (std::size_t i = 0; i != size; i += 8)
            sp::detail::encode_low_tile<K, sp::geometry::local8>(original.data() + i,
                reinterpret_cast<std::uint8_t*>(p + i / 8 * K));
    }
    ~dense_fixture() { munmap(mapping, 3 * page); }
    dense_fixture(const dense_fixture&) = delete;
    dense_fixture& operator=(const dense_fixture&) = delete;
};

template<class Ops>
auto dense_bytes(typename Ops::value_type values) {
    std::array<std::uint8_t, Ops::lanes> result{};
    if constexpr (Ops::lanes == 32)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), values);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    else _mm512_storeu_si512(result.data(), values);
#endif
    return result;
}

template<class Ops>
std::uint64_t dense_mask_bits(typename Ops::mask_type mask) {
    if constexpr (Ops::lanes == 32) {
        const auto bytes = dense_bytes<Ops>(mask);
        std::uint64_t bits = 0;
        for (unsigned lane = 0; lane != Ops::lanes; ++lane) {
            if (bytes[lane] == 255) bits |= std::uint64_t{1} << lane;
            else if (bytes[lane] != 0)
                fail("dense noncanonical mask", Ops::W, 0, Ops::G, lane);
        }
        return bits;
    } else return static_cast<std::uint64_t>(mask);
}

template<template<class> class Executor, unsigned K>
[[gnu::noinline]] void check_dense_format() {
    using F = sp::static_format<K, sp::geometry::local8>;
    using Ops = Executor<F>;
    constexpr unsigned N = Ops::lanes;
    constexpr std::size_t Size = dense_fixture<K, N>::size;
    static_assert(Ops::L == 1 && Ops::encoded_bytes == N * K / 8);
    const dense_fixture<K, N> a(0x8711, false), b(0x4312, true);
    const auto av = sp::static_const_view<F>::attach(Size, a.placement);
    const auto bv = sp::static_const_view<F>::attach(Size, b.placement);
    if (!av || !bv) fail("dense attachment", K, 0, Ops::G, N);

    // Complete ranges may start at any packet, including nonzero positions
    // that are not multiples of the native working grain.
    for (const sp::index_range rows : {sp::index_range{0, 2 * N}, {8, Size}, {Size, Size}})
        if (!Ops::validate_region(*av, rows) || !Ops::validate_region(*bv, rows))
            fail("dense valid region", K, 0, Ops::G, rows.begin);
    for (const sp::index_range rows : {sp::index_range{1, 0}, {1, N + 1},
                                     {0, N - 1}, {0, Size + N}, {Size + 1, Size + 1}}) {
        const auto check = Ops::validate_region(*av, rows);
        if (check || check.error() != sp::error::invalid_range)
            fail("dense invalid region", K, 0, Ops::G, rows.begin);
    }
    const auto short_view = sp::static_const_view<F>::attach(Size - 1, b.placement);
    if (!short_view || Ops::validate_region(*short_view, {N + 8, Size}))
        fail("dense logical slack is not a lane", K, 0, Ops::G, Size - 1);
    const fixture<F> strided(91);
    const auto strided_view = sp::static_const_view<F>::attach(count - 1, strided.placement);
    if (!strided_view) fail("dense strided attachment", K, 0, Ops::G, N);
    const auto stride_check = Ops::validate_region(*strided_view, {0, N});
    if (stride_check || stride_check.error() != sp::error::invalid_stride)
        fail("dense rejects stride gaps", K, 0, Ops::G, N);

    Ops ops;
    const auto ae = sp::composition::describe(*av);
    const auto be = sp::composition::describe(*bv);
    const decltype(ae) substituted{{ae.payload.body, be.payload.tail}, ae.head0, ae.head1};
    if (dense_mask_bits<Ops>(Ops::active_all()) != Ops::lane_bits)
        fail("dense active all", K, 0, Ops::G, N);
    for (const std::size_t origin : {std::size_t{0}, std::size_t{8}, std::size_t{N + 8}}) {
        // Both actual sources, not just the enclosing expression, are admitted.
        if (!Ops::validate_region(*av, {origin, origin + N}) ||
            !Ops::validate_region(*bv, {origin, origin + N}))
            fail("dense source admission", K, 0, Ops::G, origin);
        const typename Ops::position_type rows{origin};
        const auto va = sp::composition::read(ops, ae, rows, Ops::active_all());
        const auto vb = sp::composition::read(ops, be, rows, Ops::active_all());
        const auto vs = sp::composition::read(ops, substituted, rows, Ops::active_all());
        // Reading the distinct replacement cannot invalidate the first value.
        const auto ba = dense_bytes<Ops>(va), bb = dense_bytes<Ops>(vb), bs = dense_bytes<Ops>(vs);
        for (unsigned lane = 0; lane != N; ++lane)
            if (Ops::original_index(rows, lane) != origin + lane ||
                ba[lane] != a.original[origin + lane] || bb[lane] != b.original[origin + lane] ||
                bs[lane] != b.original[origin + lane])
                fail("dense original coordinates or substituted source", K, 0, Ops::G, origin + lane);

        std::uint64_t original_mask = 0;
        for (unsigned lane = 0; lane != N; ++lane)
            if ((origin + lane) % 7 == 3) original_mask |= std::uint64_t{1} << lane;
        for (const std::uint64_t bits : std::array<std::uint64_t, 7>{0, 1,
                std::uint64_t{1} << (N - 1), original_mask, 0xddddddddddddddddULL,
                0xffffffff00000000ULL, ~std::uint64_t{0}}) {
            const auto active = Ops::active_bits(bits);
            if (dense_mask_bits<Ops>(active) != (bits & Ops::lane_bits))
                fail("dense positional mask", K, 0, Ops::G, origin);
            for (const std::uint64_t cutoff : {std::uint64_t{0}, std::uint64_t{1},
                    low_mask<K>, std::uint64_t{1} << K, std::uint64_t{128},
                    std::uint64_t{256}, ~std::uint64_t{0}}) {
                std::uint64_t less_a = 0, equal_a = 0, sum_a = 0, sum_b = 0;
                for (unsigned lane = 0; lane != N; ++lane) {
                    if ((bits & (std::uint64_t{1} << lane)) == 0) continue;
                    const auto x = a.original[origin + lane], y = b.original[origin + lane];
                    if (x < cutoff) { less_a |= std::uint64_t{1} << lane; sum_a += x; }
                    if (x == cutoff) equal_a |= std::uint64_t{1} << lane;
                    if (y < cutoff) sum_b += y;
                }
                if (dense_mask_bits<Ops>(ops.unsigned_less(va, cutoff, active)) != less_a ||
                    dense_mask_bits<Ops>(ops.unsigned_equal(va, cutoff, active)) != equal_a ||
                    ops.sum(va, ops.unsigned_less(va, cutoff, active), sp::modulo_u64_sum{}) != sum_a ||
                    sp::composition::selected_sum(ops, ae, rows, active, cutoff) != sum_a ||
                    sp::composition::selected_sum(ops, substituted, rows, active, cutoff) != sum_b)
                    fail("dense predicate, reuse or authored sum", K, 0, Ops::G, origin);
                ++cases;
            }
        }
    }
}
#endif

template<class F, unsigned Begin = 0>
[[gnu::always_inline]] inline std::uint64_t wrapper_sum(
    const sp::static_const_view<F>& source, std::size_t tile,
    std::uint64_t bits, std::uint64_t cutoff) {
    native::composition_ops<F, Begin> ops;
    const auto expression = sp::composition::describe(source);
    return sp::composition::selected_sum(ops, expression, native::tile_position{tile},
                                         ops.active_bits(bits), cutoff);
}

template<class F, unsigned Begin = 0>
[[gnu::always_inline]] inline std::uint64_t direct_sum(
    const sp::static_const_view<F>& source, std::size_t tile,
    std::uint64_t bits, std::uint64_t cutoff) {
    using Ops = native::composition_ops<F, Begin>;
    constexpr unsigned W = F::layout.width - F::layout.head_bits, H = F::layout.head_bits;
    constexpr unsigned L = Ops::L;
    Ops ops;
    auto values = [&] {
        if constexpr (W != 0) {
            const auto& p = source.placement().payload;
            return native::read_fragment<W, F::layout.storage, L, Begin>(
                reinterpret_cast<const std::uint8_t*>(p.bytes.data() + tile * p.stride));
        } else {
            const auto& h = source.placement().heads[0];
            return body::decode_body_prefix<1, L, Ops::lanes>(
                reinterpret_cast<const std::uint8_t*>(h.bytes.data() + tile * h.stride + Begin));
        }
    }();
    if constexpr (H != 0) {
        const auto& h0 = source.placement().heads[0];
        auto heads = body::decode_body_prefix<1, L, Ops::lanes>(
            reinterpret_cast<const std::uint8_t*>(h0.bytes.data() + tile * h0.stride + Begin));
        if constexpr (H == 16) {
            const auto& h1 = source.placement().heads[1];
            const auto low = body::decode_body_prefix<1, L, Ops::lanes>(
                reinterpret_cast<const std::uint8_t*>(h1.bytes.data() + tile * h1.stride + Begin));
            heads = native::join<L, 8>(heads, low);
        }
        if constexpr (W != 0) values = native::join<L, W>(heads, values);
        else values = heads;
    }
    const auto active = Ops::active_bits(bits);
    return ops.sum(values, ops.unsigned_less(values, cutoff, active), sp::modulo_u64_sum{});
}

} // namespace

using Local12 = sp::static_format<12, sp::geometry::local8>;
using Headed60 = sp::static_format<60, sp::geometry::local8, 16>;
using Striped12 = sp::static_format<12, sp::geometry::striped>;

// Kept as real externally named functions for same-TU wrapper/direct assembly
// comparisons. Their handoff is native throughout and their result is scalar.
extern "C" std::uint64_t seriespack_local12_wrapper(const sp::static_const_view<Local12>* source,
    std::size_t tile, std::uint64_t bits, std::uint64_t cutoff) { return wrapper_sum(*source, tile, bits, cutoff); }
extern "C" std::uint64_t seriespack_local12_direct(const sp::static_const_view<Local12>* source,
    std::size_t tile, std::uint64_t bits, std::uint64_t cutoff) { return direct_sum(*source, tile, bits, cutoff); }
extern "C" std::uint64_t seriespack_headed60_wrapper(const sp::static_const_view<Headed60>* source,
    std::size_t tile, std::uint64_t bits, std::uint64_t cutoff) { return wrapper_sum(*source, tile, bits, cutoff); }
extern "C" std::uint64_t seriespack_headed60_direct(const sp::static_const_view<Headed60>* source,
    std::size_t tile, std::uint64_t bits, std::uint64_t cutoff) { return direct_sum(*source, tile, bits, cutoff); }
extern "C" std::uint64_t seriespack_striped12_wrapper(const sp::static_const_view<Striped12>* source,
    std::size_t tile, std::uint64_t bits, std::uint64_t cutoff) { return wrapper_sum(*source, tile, bits, cutoff); }
extern "C" std::uint64_t seriespack_striped12_direct(const sp::static_const_view<Striped12>* source,
    std::size_t tile, std::uint64_t bits, std::uint64_t cutoff) { return direct_sum(*source, tile, bits, cutoff); }

int main() {
    sp::detail::static_for<64>([](auto width) {
        constexpr unsigned K = width + 1;
        check_format<sp::static_format<K, sp::geometry::local8>>();
        if constexpr (K >= 8) check_format<sp::static_format<K, sp::geometry::local8, 8>>();
        if constexpr (K >= 16) check_format<sp::static_format<K, sp::geometry::local8, 16>>();
        if constexpr (sp::supports_stripes(K)) {
            check_format<sp::static_format<K, sp::geometry::striped>>();
            check_format<sp::static_format<K + 8, sp::geometry::striped, 8>>();
            check_format<sp::static_format<K + 16, sp::geometry::striped, 16>>();
        }
    });
    check_mixed_formats();
#if defined(__AVX2__)
    sp::detail::static_for<7>([](auto width) {
        check_dense_format<sp::avx2::dense_local_ops, width + 1>();
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        check_dense_format<sp::avx512::dense_local_ops, width + 1>();
#endif
    });
#endif
    // Execute the audit symbols as well as inspecting them after compilation.
    const auto check_endpoints = []<class F>(auto wrapper, auto direct) {
        fixture<F> data(31);
        const auto view = data.view();
        if (wrapper(&view, 1, 0x55, low_mask<F::layout.width>) !=
            direct(&view, 1, 0x55, low_mask<F::layout.width>))
            fail("wrapper/direct", F::layout.width, F::layout.head_bits, F::layout.storage, F::payload::tile_values);
    };
    check_endpoints.template operator()<Local12>(seriespack_local12_wrapper, seriespack_local12_direct);
    check_endpoints.template operator()<Headed60>(seriespack_headed60_wrapper, seriespack_headed60_direct);
    check_endpoints.template operator()<Striped12>(seriespack_striped12_wrapper, seriespack_striped12_direct);
    std::printf("%s native composition: %u formats, %llu cases passed\n", target_name,
                formats, static_cast<unsigned long long>(cases));
}

#else

int main() { std::puts("native composition: no enabled native target"); }

#endif

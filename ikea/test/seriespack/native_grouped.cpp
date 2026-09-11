#include <ikea/seriespack/composition_x86.h>
#include <ikea/seriespack/detail/physical.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
namespace sp = ikea::seriespack;
namespace group = ikea::seriespack::avx512;
template<class Format>
using grouped_carrier_ops = group::deferred_sum_ops<group::grouped_local_ops<Format>>;

using U = std::uint64_t;
std::size_t cases = 0;

[[noreturn]] void fail(const char* message, unsigned k, std::size_t origin) {
    std::fprintf(stderr, "FAIL grouped K%u origin=%zu case=%zu: %s\n", k, origin, cases, message);
    std::abort();
}
U mix(U x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Every individual packet, not just the whole source, abuts an inaccessible
// page. Different owners have different strides and prefix/suffix placement.
template<class F>
struct guarded_source {
    static constexpr unsigned K = F::layout.width;
    static constexpr unsigned N = group::grouped_local_ops<F>::lanes;
    static constexpr std::size_t size = 2 * N + 8, tiles = size / 8;
    std::array<U, size> original{};
    std::size_t page, stride, mapping_bytes;
    void* mapping;
    sp::basic_placement<const std::byte> placement{};

    guarded_source(U seed, unsigned page_step, bool suffix)
        : page(static_cast<std::size_t>(sysconf(_SC_PAGESIZE))), stride(page * page_step),
          mapping_bytes(tiles * stride + 2 * page),
          mapping(mmap(nullptr, mapping_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) {
        if (mapping == MAP_FAILED || page_step < 2 || K > page)
            fail("guard allocation", K, 0);
        auto* first_page = static_cast<std::byte*>(mapping) + page;
        auto* first = first_page + (suffix ? page - K : 0);
        placement.payload = {{first, (tiles - 1) * stride + K}, stride};
        for (std::size_t i = 0; i != size; ++i) {
            original[i] = mix(seed + i) & ((U{1} << K) - 1);
            if (i % 9 == 0) original[i] = 0;
            if (i % 11 == 0) original[i] = (U{1} << K) - 1;
        }
        original.back() = 0; // Canonical slack for a separately admitted size-1 source.
        for (std::size_t tile = 0; tile != tiles; ++tile) {
            auto* current_page = first_page + tile * stride;
            if (mprotect(current_page, page, PROT_READ | PROT_WRITE) != 0)
                fail("guard page enable", K, tile * 8);
            sp::detail::encode_low_tile<K, sp::geometry::local8>(original.data() + tile * 8,
                reinterpret_cast<std::uint8_t*>(first + tile * stride));
            if (mprotect(current_page, page, PROT_READ) != 0)
                fail("guard page read-only", K, tile * 8);
        }
    }
    ~guarded_source() { munmap(mapping, mapping_bytes); }
    guarded_source(const guarded_source&) = delete;
    guarded_source& operator=(const guarded_source&) = delete;
};

template<class Ops>
auto inspect(typename Ops::value_type value) {
    std::array<typename Ops::format_type::scalar_type, Ops::lanes> values{};
    _mm512_storeu_si512(values.data(), value);
    return values;
}

template<unsigned K>
void check_format(bool reverse_guards) {
    using F = sp::static_format<K, sp::geometry::local8>;
    using Ops = group::grouped_local_ops<F>;
    constexpr unsigned N = Ops::lanes, R = K % 8;
    constexpr U maximum = (U{1} << K) - 1, tail_mask = (U{1} << R) - 1;
    constexpr auto Size = guarded_source<F>::size;
    const guarded_source<F> a(0x81297123, 2, reverse_guards);
    const guarded_source<F> b(0x57134e69, 3, !reverse_guards);
    const auto av = sp::static_const_view<F>::attach(Size, a.placement);
    const auto bv = sp::static_const_view<F>::attach(Size, b.placement);
    if (!av || !bv) fail("source attachment", K, 0);
    const auto ae = sp::composition::describe(*av);
    const auto be = sp::composition::describe(*bv);
    const decltype(ae) ab{{ae.payload.body, be.payload.tail}, ae.head0, ae.head1};
    const decltype(ae) ba{{be.payload.body, ae.payload.tail}, be.head0, be.head1};
    const auto short_view = sp::static_const_view<F>::attach(Size - 1, b.placement);
    if (!short_view || Ops::validate_region(*short_view, {N + 8, Size}))
        fail("logical final slack admitted as a full group", K, Size - 1);
    for (auto rows : {sp::index_range{1, 0}, {1, N + 1}, {0, N - 1}, {0, Size + N}}) {
        const auto admitted = Ops::validate_region(*av, rows);
        if (admitted || admitted.error() != sp::error::invalid_range)
            fail("invalid range accepted", K, rows.begin);
    }
    if (!Ops::validate_region(*av, {Size, Size}) || !Ops::validate_region(*bv, {8, Size}))
        fail("valid nonzero or empty range rejected", K, 8);

    Ops ops;
    grouped_carrier_ops<F> deferred;
    for (std::size_t origin : {std::size_t{0}, std::size_t{8}, std::size_t{N + 8}}) {
        // Admission belongs to each actual child, despite their matching F.
        if (!Ops::validate_region(*av, {origin, origin + N}) || !Ops::validate_region(*bv, {origin, origin + N}))
            fail("independent strided group admission", K, origin);
        const group::grouped_position rows{origin};
        const auto first_value = sp::composition::read(ops, ae, rows, Ops::active_all());
        const auto second_value = sp::composition::read(ops, be, rows, Ops::active_all());
        const auto ab_value = sp::composition::read(ops, ab, rows, Ops::active_all());
        const auto ba_value = sp::composition::read(ops, ba, rows, Ops::active_all());
        // Values survive later reads and each native lane still names its row.
        const auto aa = inspect<Ops>(first_value), bb = inspect<Ops>(second_value);
        const auto mixed_ab = inspect<Ops>(ab_value), mixed_ba = inspect<Ops>(ba_value);
        std::array<U, N> expected_ab{}, expected_ba{};
        U sparse = 0;
        for (unsigned lane = 0; lane != N; ++lane) {
            const auto index = origin + lane;
            expected_ab[lane] = (a.original[index] & ~tail_mask) | (b.original[index] & tail_mask);
            expected_ba[lane] = (b.original[index] & ~tail_mask) | (a.original[index] & tail_mask);
            if (Ops::original_index(rows, lane) != index || aa[lane] != a.original[index] ||
                bb[lane] != b.original[index] || mixed_ab[lane] != expected_ab[lane] ||
                mixed_ba[lane] != expected_ba[lane]) fail("child identity or lane order", K, index);
            if (index % 7 == 3) sparse |= U{1} << lane;
        }
        for (U bits : std::array<U, 7>{0, 1, U{1} << (N - 1), sparse,
                                      0xddddddddddddddddULL, 0xffffffff00000000ULL, ~U{0}}) {
            const auto active = Ops::active_bits(bits);
            if (U(active) != (bits & Ops::lane_bits)) fail("active lane mask", K, origin);
            for (U cutoff : std::array<U, 8>{0, 1, maximum, U{1} << K, U{1} << (K - 1),
                                             U{1} << 16, U{1} << 32, ~U{0}}) {
                const auto check = [&](const auto& expression, const auto& expected) {
                    U sum = 0, less = 0, equal = 0;
                    for (unsigned lane = 0; lane != N; ++lane) {
                        if ((bits & (U{1} << lane)) == 0) continue;
                        if (expected[lane] < cutoff) { sum += expected[lane]; less |= U{1} << lane; }
                        if (expected[lane] == cutoff) equal |= U{1} << lane;
                    }
                    const auto value = sp::composition::read(ops, expression, rows, active);
                    const auto fragment = sp::composition::selected_sum(deferred, expression, rows, active, cutoff);
                    const group::deferred_u64_sum overflowing{_mm512_set1_epi64(-1)};
                    if (U(ops.unsigned_less(value, cutoff, active)) != less ||
                        U(ops.unsigned_equal(value, cutoff, active)) != equal ||
                        sp::composition::selected_sum(ops, expression, rows, active, cutoff) != sum ||
                        fragment.finish() != sum || overflowing.plus(fragment).finish() != U(sum - 8))
                        fail("cutoff, predicate or modulo carrier result", K, origin);
                    ++cases;
                };
                check(ae, aa); check(be, bb); check(ab, expected_ab); check(ba, expected_ba);
            }
        }
    }
}

int main() {
    []<std::size_t... I>(std::index_sequence<I...>) {
        ((check_format<9 + I>(false), check_format<9 + I>(true)), ...);
    }(std::make_index_sequence<24>{});
    std::printf("PASS: grouped composition %zu cutoff/mask/source cases; per-packet guards, independent strides, origins and modulo carriers\n", cases);
}
#else
int main() { std::puts("SKIP: grouped composition requires AVX-512 BW/VBMI"); return 0; }
#endif

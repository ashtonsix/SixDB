#include <ikea/seriespack/composition_x86.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <vector>

#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
namespace sp = ikea::seriespack;
namespace avx = sp::avx512;
using U = std::uint64_t;
static std::size_t checks = 0;
static void require(bool pass, const char* message) {
    ++checks;
    if (!pass) { std::fprintf(stderr, "FAIL: %s\n", message); std::abort(); }
}
static U random_word(U& state) {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    return state;
}
template<unsigned Bytes> using scalar = std::conditional_t<Bytes == 1, std::uint8_t,
    std::conditional_t<Bytes == 2, std::uint16_t,
    std::conditional_t<Bytes == 4, std::uint32_t, U>>>;
template<unsigned K> constexpr U maximum = [] {
    if constexpr (K == 64) return ~U{0};
    else return (U{1} << K) - 1;
}();

// Native fixture executor only: leaves its selected_sum caller unchanged.
// The offset deliberately resides in Base to catch discarded base state.
template<unsigned K, unsigned Bytes>
struct fixture_ops : avx::composition_detail::consumer_ops<K, Bytes> {
    static constexpr unsigned L = Bytes;
    std::size_t offset;
    template<class Parts, class Rows, class Active>
    __m512i read_payload(const Parts& parts, Rows rows, Active) const {
        return _mm512_loadu_si512(parts.body.source.data() + offset + rows);
    }
};

static_assert(std::is_trivially_copyable_v<avx::deferred_u64_sum>);
static_assert(sizeof(avx::deferred_u64_sum) == sizeof(__m512i));
static_assert(alignof(avx::deferred_u64_sum) == alignof(__m512i));

template<unsigned K, unsigned Bytes>
void check_domain() {
    constexpr unsigned N = 64 / Bytes;
    constexpr U lane_bits = [] { if constexpr (N == 64) return ~U{0}; else return (U{1} << N) - 1; }();
    using Base = fixture_ops<K, Bytes>;
    using Ops = avx::deferred_sum_ops<Base>;
    using Mask = typename Ops::mask_type;
    using F = sp::static_format<K, sp::geometry::local8>;
    static_assert(std::is_same_v<typename Ops::value_type, typename Base::value_type>);
    static_assert(std::is_same_v<typename Ops::mask_type, typename Base::mask_type>);
    static_assert(Ops::L == Base::L);
    Base base{{}, N};
    Ops ops{base};
    require(ops.offset == N, "stateful Base retained");
    U rng = 0x5c3cb7ed714612adULL + K;
    std::vector<U> masks{0, lane_bits, 0xddddddddddddddddULL & lane_bits,
        0xaaaaaaaaaaaaaaaaULL & lane_bits, 0x5555555555555555ULL & lane_bits};
    for (unsigned lane = 0; lane != N; ++lane) masks.push_back(U{1} << lane);
    std::vector<U> cutoffs{0, 1, maximum<K> / 2, maximum<K> - 1, maximum<K>, ~U{0}};
    if constexpr (K < 64) cutoffs.push_back(U{1} << K);
    cutoffs.push_back(U{1} << 31);
    cutoffs.push_back(U{1} << 63);
    avx::deferred_u64_sum running = avx::deferred_u64_sum::zero();
    U running_oracle = 0;
    for (unsigned iteration = 0; iteration != 40; ++iteration) {
        // First half is poison for an accidentally default-constructed Base.
        std::array<scalar<Bytes>, N * 2> source{};
        for (unsigned lane = 0; lane != N; ++lane) {
            U x = random_word(rng) & maximum<K>;
            if (iteration == 0) x = maximum<K>;
            if (iteration == 1) x = 0;
            if (iteration == 2) x = lane & 1 ? maximum<K> : 1;
            if (iteration == 3) x = (U{1} << (K - 1)) + lane % 2;
            source[N + lane] = static_cast<scalar<Bytes>>(x & maximum<K>);
        }
        auto expr = sp::composition::describe<F>(source);
        const auto native = _mm512_loadu_si512(source.data() + N);
        for (U bits : masks) {
            const Mask selected = static_cast<Mask>(bits);
            U direct_oracle = 0;
            for (unsigned lane = 0; lane != N; ++lane)
                if ((bits >> lane) & 1) direct_oracle += U(source[N + lane]);
            const auto direct = ops.sum(native, selected, sp::modulo_u64_sum{});
            require(direct.finish() == direct_oracle, "direct selected sum");
            require(base.sum(native, selected, sp::modulo_u64_sum{}) == direct_oracle,
                    "immediate scalar result unchanged");
            running = running.plus(direct);
            running_oracle += direct_oracle;
            for (U cutoff : cutoffs) {
                U expected = 0;
                U predicate = 0;
                for (unsigned lane = 0; lane != N; ++lane)
                    if (((bits >> lane) & 1) && U(source[N + lane]) < cutoff) {
                        expected += U(source[N + lane]);
                        predicate |= U{1} << lane;
                    }
                require(U(ops.unsigned_less(native, cutoff, selected)) == predicate,
                        "cutoff preserves domain and selected lanes");
                const auto actual = sp::composition::selected_sum(ops, expr, std::size_t{0}, selected, cutoff);
                static_assert(std::is_same_v<decltype(actual), const avx::deferred_u64_sum>);
                require(actual.finish() == expected, "same generic selected_sum");
            }
        }
        // Carries are owned values: changing the fixture after production must
        // not change their result or keep a reference into producer storage.
        const auto owned = ops.sum(native, static_cast<Mask>(lane_bits), sp::modulo_u64_sum{});
        const U before = owned.finish();
        source.fill(0);
        require(owned.finish() == before, "owned result survives source overwrite");
        require(owned.plus(avx::deferred_u64_sum::zero()).finish() == before, "right identity");
        require(avx::deferred_u64_sum::zero().plus(owned).finish() == before, "left identity");
    }
    require(running.finish() == running_oracle, "cross-fragment aggregate modulo law");

    // Every native partial eventually wraps; a u32 carried representation
    // would fail much earlier. Compare only the logical result, not lane maps.
    std::array<scalar<Bytes>, N> maximum_values{};
    maximum_values.fill(static_cast<scalar<Bytes>>(maximum<K>));
    const auto full = _mm512_loadu_si512(maximum_values.data());
    auto doubled = ops.sum(full, static_cast<Mask>(lane_bits), sp::modulo_u64_sum{});
    U expected = 0;
    for (auto value : maximum_values) expected += U(value);
    require(doubled.finish() == expected, "independent initial wrap-witness sum");
    const auto a = ops.sum(full, static_cast<Mask>(lane_bits & 0xddddddddddddddddULL), sp::modulo_u64_sum{});
    const auto b = ops.sum(full, static_cast<Mask>(lane_bits & 0xaaaaaaaaaaaaaaaaULL), sp::modulo_u64_sum{});
    const auto c = ops.sum(full, static_cast<Mask>(lane_bits), sp::modulo_u64_sum{});
    require(a.plus(b).plus(c).finish() == a.plus(b.plus(c)).finish(), "logical associativity");
    bool observed_wrap = false;
    for (unsigned i = 0; i != 64; ++i) {
        observed_wrap |= expected > (~U{0} / 2);
        expected += expected;
        doubled = doubled.plus(doubled);
        require(doubled.finish() == expected, "modulo u64 wrap under repeated composition");
    }
    require(observed_wrap, "overflow witness reached");
}

template<unsigned K>
void check_actual_base() {
    using Base = avx::composition_ops<sp::static_format<K, sp::geometry::local8>, 0>;
    using Ops = avx::deferred_sum_ops<Base>;
    Ops deferred{};
    const auto values = _mm512_set1_epi8(-1);
    const auto selected = Ops::active_bits(0xddddddddddddddddULL);
    require(deferred.sum(values, selected, sp::modulo_u64_sum{}).finish() ==
            Base{}.sum(values, selected, sp::modulo_u64_sum{}), "actual tile Base adapted");
    require(Ops::original_index({3}, 2) == Base::original_index({3}, 2), "original positions inherited");
}

static void check_u16_long_accumulation() {
    using Ops = avx::deferred_sum_ops<fixture_ops<16, 2>>;
    Ops ops{{{}, 0}};
    const auto one = ops.sum(_mm512_set1_epi16(-1), __mmask32{0xffffffff}, sp::modulo_u64_sum{});
    auto result = avx::deferred_u64_sum::zero();
    constexpr U repetitions = 20000;
    for (U i = 0; i != repetitions; ++i) result = result.plus(one);
    require(result.finish() == repetitions * 32 * 65535, "u16 total exceeds u32");
    // This white-box check protects the specific carry-width boundary, not a
    // public coordinate map: every current partial has crossed 2^32 here.
    std::array<U, 8> partials{};
    _mm512_storeu_si512(partials.data(), result.partials);
    for (U partial : partials) require(partial == repetitions * 4 * 65535 && partial > 0xffffffffULL,
                                      "u16 partial was widened before repeated addition");
}

int main() {
    check_domain<7, 1>(); check_domain<8, 1>();
    check_domain<12, 2>(); check_domain<16, 2>();
    check_domain<31, 4>(); check_domain<32, 4>();
    check_domain<60, 8>(); check_domain<64, 8>();
    check_actual_base<8>(); check_actual_base<16>();
    check_actual_base<32>(); check_actual_base<64>();
    using Dense = avx::dense_local_ops<sp::static_format<7, sp::geometry::local8>>;
    avx::deferred_sum_ops<Dense> dense{};
    require(dense.sum(_mm512_set1_epi8(127), Dense::active_all(), sp::modulo_u64_sum{}).finish() == 64 * 127,
            "actual dense Base adapted");
    check_u16_long_accumulation();
    std::printf("PASS: %zu deferred-sum checks; lane bytes 1/2/4/8, domains/cutoffs/masks, inherited state and generic selected_sum, owned result, u32 crossing and u64 wrap\n", checks);
}

#else
int main() { std::puts("deferred sum: native checks skipped (AVX512 BW/VBMI is not enabled)"); }
#endif

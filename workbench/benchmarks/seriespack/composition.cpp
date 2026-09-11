// Immediate native producer/consumer seams and a materialized alternative.
// The shared benchmark executable
// supplies CPU affinity, repetitions and JSON; this TU only registers workloads.
#include <ikea/seriespack/composition_neon.h>
#include <ikea/seriespack/composition_x86.h>
#include <ikea/seriespack/detail/physical.h>
#include <ikea/seriespack/view.h>
#include <benchmark/benchmark.h>
#include "../../spikes/ikea-composition/native-regions/carriers/carrier.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace seriespack_measurement {
namespace {
namespace sp = ikea::seriespack;
namespace carrier = diagnostics::reduction_carrier;
using U = std::uint64_t;
constexpr std::size_t count = 8192;

#if defined(__aarch64__) || defined(__AVX2__)

// These selectors choose concrete target executors; the benchmark does not
// add an instruction vocabulary or an alternate production composition layer.
#if defined(__aarch64__)
struct neon_target {
    static constexpr const char* name = "neon";
    static constexpr auto execution = sp::execution_target::neon;
    using rows = sp::neon::tile_position;
    template<class F, unsigned Begin> using ops = sp::neon::composition_ops<F, Begin>;
    using sum_carrier = carrier::neon_sum_carrier;
    template<class F, unsigned Begin> using carrier_ops = carrier::neon_carrier_ops<F, Begin>;
};
#endif
#if defined(__AVX2__)
struct avx2_target {
    static constexpr const char* name = "avx2";
    static constexpr auto execution = sp::execution_target::avx2;
    using rows = sp::avx2::tile_position;
    template<class F, unsigned Begin> using ops = sp::avx2::composition_ops<F, Begin>;
    using sum_carrier = carrier::avx2_sum_carrier;
    template<class F, unsigned Begin> using carrier_ops = carrier::avx2_carrier_ops<F, Begin>;
    template<class F> using dense_ops = sp::avx2::dense_local_ops<F>;
};
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
struct avx512_target {
    static constexpr const char* name = "avx512";
    static constexpr auto execution = sp::execution_target::avx512;
    using rows = sp::avx512::tile_position;
    template<class F, unsigned Begin> using ops = sp::avx512::composition_ops<F, Begin>;
    using sum_carrier = carrier::avx512_sum_carrier;
    template<class F, unsigned Begin> using carrier_ops = carrier::avx512_carrier_ops<F, Begin>;
    template<class F> using dense_ops = sp::avx512::dense_local_ops<F>;
};
#endif
#endif

U mix(U x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

template<class F>
struct fixture {
    static constexpr auto d = F::layout;
    static constexpr unsigned K = d.width, H = d.head_bits, W = K - H;
    static constexpr std::size_t T = F::payload::tile_values;
    static constexpr std::size_t B = F::payload::tile_bytes;
    static constexpr std::size_t tiles = count / T;
    static constexpr std::size_t encoded_bytes = count * K / 8;
    static constexpr U maximum = ~U{0} >> (64 - K);
    using UInt = typename F::scalar_type;
    static_assert(count % T == 0);

    // Original scalar values are exclusively the construction/checking oracle,
    // never a timed intermediary. All fixtures start with deterministic values.
    std::vector<UInt> original;
    std::vector<std::byte> payload_owner;
    std::array<std::vector<std::byte>, 2> head_owner;
    sp::basic_placement<const std::byte> placement{};
    U cutoff = maximum - (maximum >> 2);

    fixture() : original(count), payload_owner(tiles * B + 63) {
        for (std::size_t i = 0; i != count; ++i) {
            U value = mix(0x6ba7'41c9'053e'd28fULL + i) & maximum;
            if (i % 17 == 0) value = maximum;
            if (i % 19 == 0) value = 0;
            original[i] = static_cast<UInt>(value);
        }
        auto* payload = payload_owner.data();
        payload += (-reinterpret_cast<std::uintptr_t>(payload)) & 63U;
        placement.payload = {{payload, tiles * B}, B};
        for (std::size_t t = 0; t != tiles; ++t)
            sp::detail::encode_low_tile<W, d.storage>(original.data() + t * T,
                reinterpret_cast<std::uint8_t*>(payload + t * B));
        for (unsigned plane = 0; plane != H / 8; ++plane) {
            head_owner[plane].resize(count);
            auto* head = head_owner[plane].data();
            placement.heads[plane] = {{head, count}, T};
            for (std::size_t i = 0; i != count; ++i)
                head[i] = std::byte(U(original[i]) >> (K - 8 * (plane + 1)));
        }
    }

    auto view() const { return sp::static_const_view<F>::assume_valid(count, placement); }

    template<bool Prefilter>
    std::array<U, 3> expected() const {
        U sum = 0, active = 0, selected = 0;
        for (std::size_t i = 0; i != count; ++i) {
            if constexpr (Prefilter) if (i % 4 == 1) continue;
            ++active;
            if (U(original[i]) < cutoff) { sum += U(original[i]); ++selected; }
        }
        return {sum, active, selected};
    }
};

// The direct control expands the same native leaves and joins by hand. In
// particular it makes no same-source fused-read assumption and creates no
// scalar values array. Both paths retain the native value for compare and sum.
template<class F, class Ops, class Expression, class Rows>
[[gnu::always_inline]] inline U direct_sum(Ops& ops, const Expression& source,
    Rows rows, typename Ops::mask_type active, U cutoff) {
    constexpr unsigned K = F::layout.width, H = F::layout.head_bits, W = K - H;
    const auto read_payload = [&] [[gnu::always_inline]] {
        if constexpr (W < 8) return ops.read_tail(source.payload.tail, rows, active);
        else if constexpr (W % 8 == 0) return ops.read_body(source.payload.body, rows, active);
        else {
            auto high = ops.read_body(source.payload.body, rows, active);
            auto low = ops.read_tail(source.payload.tail, rows, active);
            return ops.template join<W % 8>(high, low);
        }
    };
    auto values = [&] [[gnu::always_inline]] {
        if constexpr (H == 0) return read_payload();
        else {
            auto high = ops.read_head(source.head0, rows, active);
            if constexpr (H == 16) {
                auto low = ops.read_head(source.head1, rows, active);
                high = ops.template join<8>(high, low);
            }
            if constexpr (W == 0) return high;
            else return ops.template join<W>(high, read_payload());
        }
    }();
    auto selected = ops.unsigned_less(values, cutoff, active);
    return ops.sum(values, selected, sp::modulo_u64_sum{});
}

template<class Target, class F, bool Authored, bool Prefilter>
[[gnu::always_inline]] inline U batch(const sp::static_const_view<F>& view, U cutoff) {
    using First = typename Target::template ops<F, 0>;
    constexpr unsigned N = First::lanes;
    constexpr unsigned T = F::payload::tile_values;
    static_assert(T % N == 0 && T % 4 == 0);
    const auto source = sp::composition::describe(view);
    U sum = 0;
    for (std::size_t tile = 0; tile != count / T; ++tile) {
        sp::detail::static_for<T / N>([&](auto fragment) {
            constexpr unsigned Begin = fragment * N;
            using Ops = typename Target::template ops<F, Begin>;
            Ops ops;
            const auto active = [] {
                if constexpr (!Prefilter) return Ops::active_all();
                else {
                    constexpr U bits = [] {
                        U result = 0;
                        for (unsigned lane = 0; lane != N; ++lane)
                            if ((Begin + lane) % 4 != 1) result |= U{1} << lane;
                        return result;
                    }();
                    return Ops::active_bits(bits);
                }
            }();
            const typename Target::rows rows{tile};
            if constexpr (Authored)
                sum += sp::composition::selected_sum(ops, source, rows, active, cutoff);
            else sum += direct_sum<F>(ops, source, rows, active, cutoff);
        });
    }
    return sum;
}

template<class Target, class F, bool Authored, bool Prefilter>
void run(benchmark::State& state) {
    // Google Benchmark excludes work before its iteration loop from timing.
    // Each case has one resident fixture; no prior case's footprint accumulates.
    const fixture<F> data;
    const auto view = data.view();
    const auto expected = data.template expected<Prefilter>();
    const U authored = batch<Target, F, true, Prefilter>(view, data.cutoff);
    const U direct = batch<Target, F, false, Prefilter>(view, data.cutoff);
    if (authored != expected[0] || direct != expected[0]) {
        state.SkipWithError("native composition differs from original scalar values");
        return;
    }
    U result = 0;
    for (auto _ : state) {
        // Prevent encoded loads being hoisted out of repeated batches. This
        // barrier is shared by both controls and occurs once per 8,192 values.
        benchmark::ClobberMemory();
        result = batch<Target, F, Authored, Prefilter>(view, data.cutoff);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * count);
    state.SetBytesProcessed(state.iterations() * fixture<F>::encoded_bytes);
    state.counters["logical_values"] = count;
    state.counters["encoded_bytes"] = fixture<F>::encoded_bytes;
    state.counters["scalar_source_bytes"] = count * sizeof(typename F::scalar_type);
    state.counters["payload_bytes"] = count * fixture<F>::W / 8;
    state.counters["head_bytes"] = count * fixture<F>::H / 8;
    state.counters["native_fragment_values"] = Target::template ops<F, 0>::lanes;
    state.counters["active_values"] = expected[1];
    state.counters["selected_values"] = expected[2];
    state.SetLabel("dense8192; native immediate compare+modulo64 sum; scalar source outside timing");
}

template<class Target, class F, bool Prefilter>
[[gnu::always_inline]] inline U batch_carrier(const sp::static_const_view<F>& view, U cutoff) {
    using First = typename Target::template carrier_ops<F, 0>;
    constexpr unsigned N = First::lanes;
    constexpr unsigned T = F::payload::tile_values;
    static_assert(T % N == 0 && T % 4 == 0);
    const auto source = sp::composition::describe(view);
    auto sum = Target::sum_carrier::zero();
    for (std::size_t tile = 0; tile != count / T; ++tile) {
        sp::detail::static_for<T / N>([&](auto fragment) {
            constexpr unsigned Begin = fragment * N;
            using Ops = typename Target::template carrier_ops<F, Begin>;
            Ops ops;
            const auto active = [] {
                if constexpr (!Prefilter) return Ops::active_all();
                else {
                    constexpr U bits = [] {
                        U result = 0;
                        for (unsigned lane = 0; lane != N; ++lane)
                            if ((Begin + lane) % 4 != 1) result |= U{1} << lane;
                        return result;
                    }();
                    return Ops::active_bits(bits);
                }
            }();
            const typename Target::rows rows{tile};
            // By-value logical fragment result, combined in native u64 lanes.
            const auto fragment_sum = sp::composition::selected_sum(ops, source, rows, active, cutoff);
            sum = sum.plus(fragment_sum);
        });
    }
    return sum.finish();
}


// Untimed checks use the original fixture/oracle and runtime cutoffs. The timed
// fixture and all three old benchmark controls above remain unchanged.
template<class F>
void repack_fixture(fixture<F>& data) {
    auto* payload = const_cast<std::byte*>(data.placement.payload.bytes.data());
    for (std::size_t t = 0; t != fixture<F>::tiles; ++t)
        sp::detail::encode_low_tile<fixture<F>::W, F::layout.storage>(
            data.original.data() + t * fixture<F>::T,
            reinterpret_cast<std::uint8_t*>(payload + t * fixture<F>::B));
    for (unsigned plane = 0; plane != fixture<F>::H / 8; ++plane)
        for (std::size_t i = 0; i != count; ++i)
            data.head_owner[plane][i] = std::byte(U(data.original[i]) >>
                (fixture<F>::K - 8 * (plane + 1)));
}

template<class Target, class F, bool Prefilter>
bool check_carrier() {
    fixture<F> data;
    const auto view = data.view();
    constexpr U maximum = fixture<F>::maximum;
    const std::array<U, 7> cutoffs{
        0, 1, maximum >> 1, maximum - 1, maximum,
        maximum == ~U{0} ? maximum : maximum + 1, ~U{0}};
    const auto agrees = [&] {
        const U expected = data.template expected<Prefilter>()[0];
        return batch_carrier<Target, F, Prefilter>(view, data.cutoff) == expected &&
            batch<Target, F, true, Prefilter>(view, data.cutoff) == expected &&
            batch<Target, F, false, Prefilter>(view, data.cutoff) == expected;
    };
    for (U cutoff : cutoffs) {
        asm("" : "+r"(cutoff)); // actual runtime-domain comparison, even at edges
        data.cutoff = cutoff;
        if (!agrees()) return false;
    }
    if constexpr (fixture<F>::K >= 56) {
        // Known wrap witness: every admitted value is maximum-1, and the exact
        // integer total exceeds 2^64. U's addition provides the modulo oracle.
        std::ranges::fill(data.original, static_cast<typename F::scalar_type>(maximum - 1));
        repack_fixture(data);
        U cutoff = maximum;
        asm("" : "+r"(cutoff));
        data.cutoff = cutoff;
        if (!agrees()) return false;
        constexpr U admitted = Prefilter ? count * 3 / 4 : count;
        static_assert(maximum - 1 > ~U{0} / admitted);
        if (data.template expected<Prefilter>()[2] != admitted) return false;
    }
    return true;
}

template<class Target>
bool check_carrier_target() {
    bool good = true;
    const auto check = [&]<class F> {
        good = check_carrier<Target, F, false>() && good;
        good = check_carrier<Target, F, true>() && good;
    };
    check.template operator()<sp::static_format<5, sp::geometry::local8>>();
    check.template operator()<sp::static_format<7, sp::geometry::local8>>();
    check.template operator()<sp::static_format<12, sp::geometry::local8>>();
    check.template operator()<sp::static_format<31, sp::geometry::local8>>();
    check.template operator()<sp::static_format<56, sp::geometry::local8>>();
    check.template operator()<sp::static_format<64, sp::geometry::local8>>();
    check.template operator()<sp::static_format<60, sp::geometry::local8, 8>>();
    check.template operator()<sp::static_format<12, sp::geometry::striped>>();
    return good;
}

template<class Target, class F, bool Prefilter>
void run_carrier(benchmark::State& state) {
    // Google Benchmark excludes work before its iteration loop from timing.
    // Each case has one resident fixture; no prior case's footprint accumulates.
    const fixture<F> data;
    const auto view = data.view();
    const auto expected = data.template expected<Prefilter>();
    const U authored = batch<Target, F, true, Prefilter>(view, data.cutoff);
    const U direct = batch<Target, F, false, Prefilter>(view, data.cutoff);
    const U carried = batch_carrier<Target, F, Prefilter>(view, data.cutoff);
    if (authored != expected[0] || direct != expected[0] || carried != expected[0] || !check_carrier<Target, F, Prefilter>()) {
        state.SkipWithError("native composition differs from original scalar values");
        return;
    }
    U result = 0;
    for (auto _ : state) {
        // Prevent encoded loads being hoisted out of repeated batches. This
        // barrier is shared by both controls and occurs once per 8,192 values.
        benchmark::ClobberMemory();
        result = batch_carrier<Target, F, Prefilter>(view, data.cutoff);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * count);
    state.SetBytesProcessed(state.iterations() * fixture<F>::encoded_bytes);
    state.counters["logical_values"] = count;
    state.counters["encoded_bytes"] = fixture<F>::encoded_bytes;
    state.counters["scalar_source_bytes"] = count * sizeof(typename F::scalar_type);
    state.counters["payload_bytes"] = count * fixture<F>::W / 8;
    state.counters["head_bytes"] = count * fixture<F>::H / 8;
    state.counters["native_fragment_values"] = Target::template ops<F, 0>::lanes;
    state.counters["active_values"] = expected[1];
    state.counters["selected_values"] = expected[2];
    state.SetLabel("dense8192; native fragment modulo64 carrier; one final horizontal sum; scalar source outside timing");
}

// A separate whole-operation plan: decode all original positions first, then
// consume the smallest sufficient unsigned array. The ordinary loop may be
// vectorized by the compiler; no instruction vocabulary or per-value barriers
// constrain that choice. This is an alternative, not a lower performance bound.
template<bool Prefilter, class UInt>
U consume_materialized(std::span<const UInt> values, U cutoff) {
    U sum = 0;
    for (std::size_t i = 0; i != values.size(); ++i) {
        if constexpr (Prefilter) if (i % 4 == 1) continue;
        const U value = values[i];
        if (value < cutoff) sum += value;
    }
    return sum;
}

template<class Target, class F, bool Prefilter>
void run_materialized(benchmark::State& state) {
    const fixture<F> data;
    const auto view = sp::const_view::attach(F::layout, count, data.placement);
    if (!view) {
        state.SkipWithError("materialized composition placement admission failed");
        return;
    }
    const auto reader = sp::bind_reader(*view, Target::execution);
    if (!reader) {
        state.SkipWithError("materialized composition target binding failed");
        return;
    }
    using UInt = typename F::scalar_type;
    std::vector<UInt> scratch(count);
    const sp::output_values output{std::span(scratch)};
    const std::span<const UInt> values{scratch};
    const auto expected = data.template expected<Prefilter>();
    const auto operation = [&] {
        reader->decode({0, count}, output);
        return consume_materialized<Prefilter>(values, data.cutoff);
    };

    U result = operation();
    if (result != expected[0] || !std::ranges::equal(scratch, data.original)) {
        state.SkipWithError("materialized composition differs from original scalar values");
        return;
    }
    for (auto _ : state) {
        // Like the immediate cases, one barrier per 8,192 original positions.
        // Decode, scratch stores/reads, comparison and reduction are all timed.
        benchmark::ClobberMemory();
        result = operation();
        benchmark::DoNotOptimize(result);
    }
    if (result != expected[0] || !std::ranges::equal(scratch, data.original)) {
        state.SkipWithError("materialized composition post-check differs from original scalar values");
        return;
    }
    state.SetItemsProcessed(state.iterations() * count);
    state.SetBytesProcessed(state.iterations() * fixture<F>::encoded_bytes);
    state.counters["logical_values"] = count;
    state.counters["encoded_bytes"] = fixture<F>::encoded_bytes;
    state.counters["scalar_source_bytes"] = count * sizeof(UInt);
    state.counters["payload_bytes"] = count * fixture<F>::W / 8;
    state.counters["head_bytes"] = count * fixture<F>::H / 8;
    state.counters["scratch_bytes"] = scratch.size() * sizeof(UInt);
    state.counters["active_values"] = expected[1];
    state.counters["selected_values"] = expected[2];
    state.SetLabel("dense8192; materialized decode+compare+modulo64 sum; ordinary loop may vectorize; allocation/admission/binding and scalar oracle excluded");
}

#if defined(__AVX2__)
template<class Ops, bool Prefilter>
[[gnu::always_inline]] inline U dense_batch(
    const sp::static_const_view<typename Ops::format_type>& view, U cutoff) {
    static_assert(count % Ops::lanes == 0 && Ops::lanes % 4 == 0);
    const auto source = sp::composition::describe(view);
    Ops ops;
    const auto active = Prefilter ? Ops::active_bits(0xddddddddddddddddULL) : Ops::active_all();
    U sum = 0;
    for (std::size_t origin = 0; origin != count; origin += Ops::lanes)
        sum += sp::composition::selected_sum(ops, source,
            typename Ops::position_type{origin}, active, cutoff);
    return sum;
}

template<class Ops, bool Prefilter>
bool check_dense_values(const fixture<typename Ops::format_type>& data,
                        const sp::static_const_view<typename Ops::format_type>& view) {
    const auto source = sp::composition::describe(view);
    Ops ops;
    const auto active = Prefilter ? Ops::active_bits(0xddddddddddddddddULL) : Ops::active_all();
    const U bits = [&] {
        if constexpr (Ops::lanes == 32)
            return static_cast<U>(static_cast<std::uint32_t>(_mm256_movemask_epi8(active)));
        else return static_cast<U>(active);
    }();
    std::array<std::uint8_t, Ops::lanes> values{};
    for (std::size_t origin = 0; origin != count; origin += Ops::lanes) {
        const typename Ops::position_type rows{origin};
        const auto native = sp::composition::read(ops, source, rows, Ops::active_all());
        if constexpr (Ops::lanes == 32)
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(values.data()), native);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        else _mm512_storeu_si512(values.data(), native);
#endif
        for (unsigned lane = 0; lane != Ops::lanes; ++lane) {
            const auto index = Ops::original_index(rows, lane);
            if (index != origin + lane || values[lane] != data.original[index]) return false;
            if (bool((bits >> lane) & 1) != (!Prefilter || index % 4 != 1)) return false;
        }
    }
    return true;
}

template<class Target, class F, bool Prefilter>
void run_dense_region(benchmark::State& state) {
    using Ops = typename Target::template dense_ops<F>;
    const fixture<F> data;
    // Attachment and complete, dense region admission happen once, outside
    // timing. This named source and its owner outlive every immediate call.
    const auto attached = sp::static_const_view<F>::attach(count, data.placement);
    if (!attached || !Ops::validate_region(*attached, {0, count})) {
        state.SkipWithError("dense region admission failed");
        return;
    }
    const auto& view = *attached;
    const auto expected = data.template expected<Prefilter>();
    U result = dense_batch<Ops, Prefilter>(view, data.cutoff);
    if (result != expected[0] || !check_dense_values<Ops, Prefilter>(data, view)) {
        state.SkipWithError("dense region differs from original scalar values or coordinates");
        return;
    }
    U all_sum = 0;
    for (std::size_t i = 0; i != count; ++i)
        if (!Prefilter || i % 4 != 1) all_sum += data.original[i];
    if (dense_batch<Ops, Prefilter>(view, 0) != 0 ||
        dense_batch<Ops, Prefilter>(view, U{1} << F::layout.width) != all_sum) {
        state.SkipWithError("dense region boundary cutoff differs from scalar oracle");
        return;
    }
    for (auto _ : state) {
        // Same one barrier per 8,192 original positions as the other plans.
        benchmark::ClobberMemory();
        result = dense_batch<Ops, Prefilter>(view, data.cutoff);
        benchmark::DoNotOptimize(result);
    }
    if (result != expected[0] || !check_dense_values<Ops, Prefilter>(data, view)) {
        state.SkipWithError("dense region post-check differs from original scalar values");
        return;
    }
    state.SetItemsProcessed(state.iterations() * count);
    state.SetBytesProcessed(state.iterations() * fixture<F>::encoded_bytes);
    state.counters["logical_values"] = count;
    state.counters["encoded_bytes"] = fixture<F>::encoded_bytes;
    state.counters["scalar_source_bytes"] = count * sizeof(typename F::scalar_type);
    state.counters["payload_bytes"] = count * fixture<F>::W / 8;
    state.counters["head_bytes"] = 0;
    state.counters["native_fragment_values"] = Ops::lanes;
    state.counters["encoded_region_bytes"] = Ops::encoded_bytes;
    state.counters["scratch_bytes"] = 0;
    state.counters["active_values"] = expected[1];
    state.counters["selected_values"] = expected[2];
    state.SetLabel("dense8192; native dense local region+authored compare+modulo64 sum; admission and scalar oracle excluded");
}
#endif

template<class Target, class F, bool Prefilter>
void register_mask() {
    constexpr auto d = F::layout;
    const std::string name = std::string("composition/") + Target::name + '/' +
        (d.storage == sp::geometry::local8 ? "local" : "striped") +
        "/k" + std::to_string(d.width) + "/h" + std::to_string(d.head_bits) +
        "/u" + std::to_string(sizeof(typename F::scalar_type) * 8) +
        (Prefilter ? "/prefilter75/" : "/all/");
    benchmark::RegisterBenchmark((name + "authored").c_str(), &run<Target, F, true, Prefilter>);
    benchmark::RegisterBenchmark((name + "direct").c_str(), &run<Target, F, false, Prefilter>);
    benchmark::RegisterBenchmark((name + "materialized").c_str(), &run_materialized<Target, F, Prefilter>);
    benchmark::RegisterBenchmark((name + "authored-u64-carrier").c_str(), &run_carrier<Target, F, Prefilter>);
#if defined(__AVX2__)
    if constexpr (d.storage == sp::geometry::local8 && d.head_bits == 0 &&
                  (d.width == 5 || d.width == 7)) {
        using Ops = typename Target::template dense_ops<F>;
        benchmark::RegisterBenchmark((name + "dense" + std::to_string(Ops::lanes) + "-authored").c_str(),
            &run_dense_region<Target, F, Prefilter>);
    }
#endif
}

template<class Target, class F>
void register_format() {
    register_mask<Target, F, false>();
    register_mask<Target, F, true>();
}

template<class Target>
void register_target() {
    register_format<Target, sp::static_format<5, sp::geometry::local8>>();
    register_format<Target, sp::static_format<7, sp::geometry::local8>>();
    register_format<Target, sp::static_format<12, sp::geometry::local8>>();
    register_format<Target, sp::static_format<31, sp::geometry::local8>>();
    register_format<Target, sp::static_format<56, sp::geometry::local8>>();
    register_format<Target, sp::static_format<64, sp::geometry::local8>>();
    register_format<Target, sp::static_format<60, sp::geometry::local8, 8>>();
    register_format<Target, sp::static_format<12, sp::geometry::striped>>();
}

#endif
} // namespace

bool run_reduction_probe_checks() {
    bool good = true;
#if defined(__aarch64__)
    good = check_carrier_target<neon_target>() && good;
#endif
#if defined(__AVX2__)
    good = check_carrier_target<avx2_target>() && good;
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    good = check_carrier_target<avx512_target>() && good;
#endif
#endif
    return good;
}

void register_composition_benchmarks() {
#if defined(__aarch64__)
    register_target<neon_target>();
#endif
#if defined(__AVX2__)
    register_target<avx2_target>();
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    register_target<avx512_target>();
#endif
#endif
}

} // namespace seriespack_measurement

#include "fixture.h"
#include <ikea/seriespack/author/chain.h>
#include <ikea/seriespack/detail/mutation/physical.h>

enum class chain_mode { inline_stages, cps, scratch_calls, fused };
template <unsigned K> struct pipeline_frame {
    using F = sp::format<K>;
    using Plan = sp::chain<K>;
    using Values = sp::native::values<K>;
    sp::view<F, std::uint8_t> destination;
    sp::write_journal journal;
    sp::sum_change summary;
    std::array<std::uint64_t, 7> cutoffs;
    std::uint64_t fused_cutoff;
    [[gnu::always_inline]] static std::uint16_t bits(Values mask) {
        const auto bytes = sp::native::low_bytes(mask);
#if defined(__aarch64__)
        static constexpr std::array<std::uint8_t, 16> weights{1, 2, 4, 8, 16, 32, 64, 128,
                                                              1, 2, 4, 8, 16, 32, 64, 128};
        const auto weighted = vandq_u8(bytes, vld1q_u8(weights.data()));
        return vaddv_u8(vget_low_u8(weighted)) | (unsigned(vaddv_u8(vget_high_u8(weighted))) << 8);
#else
        return _mm_movemask_epi8(bytes);
#endif
    }
    [[gnu::always_inline]] static typename Plan::result
    filter(void* pointer, std::size_t, std::uint16_t active, Values value, unsigned ordinal) {
        auto& frame = *static_cast<pipeline_frame*>(pointer);
        active = predicate(value, frame.cutoffs[ordinal], active);
        return {value, active, active == 0};
    }
    [[gnu::always_inline]] static typename Plan::result
    mutate(void* pointer, std::size_t row, std::uint16_t active, Values value, unsigned) {
        auto& frame = *static_cast<pipeline_frame*>(pointer);
        const auto destination = frame.destination;
        sp::replace_native16_unchecked(destination, row, value, active, frame.summary,
                                       frame.journal);
        return {value, active, false};
    }
    static void finish(void*, std::size_t, std::uint16_t, Values) {}
    [[gnu::always_inline]] static std::uint16_t predicate(Values values, std::uint64_t cutoff,
                                                          std::uint16_t active) {
#if defined(__AVX2__)
        return sp::native::less_bits(values, cutoff, active);
#else
        return bits(sp::native::less(values, cutoff, active));
#endif
    }
    [[gnu::always_inline]] static Values load(const sp::uint_for<K>* input) {
        return sp::native::narrow<K>(sp::native::load_values(input));
    }
    using ordinary_function = void (*)(pipeline_frame*, std::size_t, std::uint16_t&,
                                       sp::uint_for<K>*, unsigned);
    template <auto Body>
    [[gnu::noinline]] static void ordinary(pipeline_frame* frame, std::size_t row,
                                           std::uint16_t& active, sp::uint_for<K>* scratch,
                                           unsigned ordinal) {
        const auto after = Body(frame, row, active, load(scratch), ordinal);
        sp::native::store16(scratch, after.values);
        active = after.active;
    }
};
template <unsigned K, unsigned Depth, chain_mode Mode>
[[gnu::noinline]] std::uint64_t
execute_pipeline(pipeline_frame<K>& frame, const typename pipeline_frame<K>::Plan& plan,
                 const std::array<typename pipeline_frame<K>::ordinary_function, Depth>& ordinary,
                 const sp::uint_for<K>* input, std::size_t count) {
    using Frame = pipeline_frame<K>;
    frame.journal.used = 0;
    frame.summary = {};
    for (std::size_t row = 0; row < count; row += 16) {
        constexpr std::uint16_t incoming = 0xeeee;
        if constexpr (Mode == chain_mode::cps)
            plan.run(&frame, row, incoming, Frame::load(input + row));
        if constexpr (Mode == chain_mode::inline_stages) {
            auto value = Frame::load(input + row);
            auto active = incoming;
            sp::detail::each<Depth - 1>([&](auto stage) {
                if (active) {
                    const auto next = Frame::filter(&frame, row, active, value, stage);
                    active = next.active;
                    value = next.values;
                }
            });
            if (active)
                Frame::mutate(&frame, row, active, value, Depth - 1);
        }
        if constexpr (Mode == chain_mode::fused) {
            const auto value = Frame::load(input + row);
            const auto active = Frame::predicate(value, frame.fused_cutoff, incoming);
            if (active)
                Frame::mutate(&frame, row, active, value, 0);
        }
        if constexpr (Mode == chain_mode::scratch_calls) {
            alignas(64) std::array<sp::uint_for<K>, 16> scratch;
            std::memcpy(scratch.data(), input + row, sizeof(scratch));
            auto active = incoming;
            for (unsigned stage = 0; stage < Depth && active; ++stage)
                ordinary[stage](&frame, row, active, scratch.data(), stage);
        }
    }
    return frame.summary.finish();
}
template <unsigned K, unsigned Depth, chain_mode Mode>
void pipeline_benchmark(benchmark::State& state) {
    using Frame = pipeline_frame<K>;
    using Plan = typename Frame::Plan;
    using U = sp::uint_for<K>;
    fixture<K, sp::geometry::local> f(8192);
    const auto destination = *sp::view<typename Frame::F, std::uint8_t>::attach(
        f.count, {{{{f.storage.get(), f.bytes}, Frame::F::tile_bytes}, {}, {}}});
    std::vector<sp::byte_write> effects(f.count / 8);
    Frame frame{destination, {effects}, {}, {}, ~std::uint64_t{0}};
    std::array<typename Plan::function, Depth> stages;
    stages.fill(&Plan::template stage<&Frame::filter>);
    stages.back() = &Plan::template stage<&Frame::mutate>;
    const auto plan = Plan::prepare(stages, &Plan::template completion<&Frame::finish>);
    if (!plan)
        std::abort();
    std::array<typename Frame::ordinary_function, Depth> ordinary;
    ordinary.fill(&Frame::template ordinary<&Frame::filter>);
    ordinary.back() = &Frame::template ordinary<&Frame::mutate>;
    constexpr auto mask = ~std::uint64_t{0} >> (64 - K);
    for (unsigned i = 0; i < Depth - 1; ++i) {
        frame.cutoffs[i] = mask - (mask / (Depth + 1)) * (i + 1);
        frame.fused_cutoff = std::min(frame.fused_cutoff, frame.cutoffs[i]);
    }
    std::array<std::vector<U>, 2> inputs{std::vector<U>(f.count), std::vector<U>(f.count)};
    for (std::size_t i = 0; i < f.count; ++i) {
        inputs[0][i] = f.values[i];
        inputs[1][i] = f.values[i] ^ mask;
    }
    for (unsigned which : {1u, 0u}) {
        const auto delta =
            execute_pipeline<K, Depth, Mode>(frame, *plan, ordinary, inputs[which].data(), f.count);
        std::uint64_t wanted = 0;
        for (std::size_t i = 0; i < f.count; ++i) {
            const auto after = i % 4 != 0 && inputs[which][i] < frame.fused_cutoff
                                   ? inputs[which][i]
                                   : f.values[i];
            wanted += std::uint64_t(after) - f.values[i];
            f.values[i] = after;
            if (sp::get_unchecked(destination, i) != after)
                std::abort();
        }
        if (delta != wanted)
            std::abort();
    }
    unsigned which = 1;
    for (auto _ : state) {
        auto delta =
            execute_pipeline<K, Depth, Mode>(frame, *plan, ordinary, inputs[which].data(), f.count);
        which ^= 1;
        benchmark::DoNotOptimize(delta);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * f.count);
    state.counters["logical_values"] = f.count;
    state.counters["stages"] = Depth;
    state.counters["native_rows"] = 16;
}
template <unsigned K, unsigned Depth> void register_pipeline() {
    const auto name =
        std::string("pipeline-mutation/k") + std::to_string(K) + "/depth" + std::to_string(Depth);
    benchmark::RegisterBenchmark((name + "/inline").c_str(),
                                 pipeline_benchmark<K, Depth, chain_mode::inline_stages>);
    benchmark::RegisterBenchmark((name + "/cps").c_str(),
                                 pipeline_benchmark<K, Depth, chain_mode::cps>);
    benchmark::RegisterBenchmark((name + "/scratch-calls").c_str(),
                                 pipeline_benchmark<K, Depth, chain_mode::scratch_calls>);
    benchmark::RegisterBenchmark((name + "/fused").c_str(),
                                 pipeline_benchmark<K, Depth, chain_mode::fused>);
}
void register_pipelines() {
    register_pipeline<7, 3>();
    register_pipeline<12, 3>();
    register_pipeline<31, 3>();
    register_pipeline<64, 3>();
    register_pipeline<7, 7>();
    register_pipeline<31, 7>();
}

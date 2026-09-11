#include "fixture.h"
#include "pipeline_recipe.h"
namespace pp = pipeline_probe;

template <unsigned N, unsigned Source, unsigned Transform, unsigned Predicate, unsigned Sink,
          pp::mode Mode, bool Empty = false>
void packet_benchmark(benchmark::State& state) {
    using Frame = pp::frame<N>;
    fixture<12, sp::geometry::striped> a(8192);
    fixture<23, sp::geometry::local> b(8192);
    fixture<32, sp::geometry::local> d(8192);
    const auto av = *sp::view<typename Frame::A>::attach(
        a.count, {{{{a.storage.get(), a.bytes}, Frame::A::tile_bytes}, {}, {}}});
    const auto bv = *sp::view<typename Frame::B>::attach(
        b.count, {{{{b.storage.get(), b.bytes}, Frame::B::tile_bytes}, {}, {}}});
    const auto dv = *sp::view<typename Frame::D, std::uint8_t>::attach(
        d.count, {{{{d.storage.get(), d.bytes}, Frame::D::tile_bytes}, {}, {}}});
    std::vector<sp::byte_write> effects(d.count);
    Frame frame{av, bv, dv, {effects}, {}};
    // A nonzero cutoff produces empty packets from the actual data. Zero can
    // let an inline compiler eliminate the entire scan before loading values.
    frame.upper = Empty ? 16 : Source == 0 ? 3000 : 6000000;
    const auto plan = Frame::template bind<Source, Transform, Predicate, Sink>();
    const auto& source = Source == 0 ? a.values : b.values;
    for (unsigned run = 0; run < 2; ++run) {
        frame.parameter ^= 0x321;
        auto result =
            pp::execute<N, Source, Transform, Predicate, Sink, Mode>(frame, plan, d.count);
        std::uint64_t wanted = 0;
        for (std::size_t i = 0; i < d.count; ++i) {
            std::uint32_t value = source[i];
            if constexpr (Transform == 0)
                value += frame.parameter;
            if constexpr (Transform == 1)
                value ^= frame.parameter;
            if constexpr (Transform == 2)
                value *= frame.parameter;
            bool selected = (Frame::incoming >> (i % N)) & 1;
            if constexpr (Predicate == 0)
                selected &= value < frame.upper;
            if constexpr (Predicate == 1)
                selected &= value >= frame.lower && value < frame.upper;
            if constexpr (Predicate == 2)
                selected &= (value & 7) == 0;
            if constexpr (Sink == 0) {
                if (selected)
                    wanted += value;
            } else {
                if (selected) {
                    wanted += std::uint64_t(value) - d.values[i];
                    d.values[i] = value;
                }
                if (sp::get_unchecked(dv, i) != d.values[i])
                    std::abort();
            }
        }
        if (result != wanted)
            std::abort();
    }
    for (auto _ : state) {
        frame.parameter ^= 0x321;
        auto result =
            pp::execute<N, Source, Transform, Predicate, Sink, Mode>(frame, plan, d.count);
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * d.count);
    state.counters["logical_values"] = d.count;
    state.counters["native_rows"] = N;
    state.counters["stages"] = 4;
}
template <unsigned N, unsigned Source, unsigned Transform, unsigned Predicate, unsigned Sink,
          bool Empty = false>
void register_packet() {
    const auto name = std::string("pipeline-packet/n") + std::to_string(N) + "/source" +
                      std::to_string(Source) + "-map" + std::to_string(Transform) + "-filter" +
                      std::to_string(Predicate) + "-sink" + std::to_string(Sink) +
                      (Empty ? "-empty" : "");
    benchmark::RegisterBenchmark(
        (name + "/inline").c_str(),
        packet_benchmark<N, Source, Transform, Predicate, Sink, pp::mode::inline_stages, Empty>);
    benchmark::RegisterBenchmark(
        (name + "/cps").c_str(),
        packet_benchmark<N, Source, Transform, Predicate, Sink, pp::mode::cps, Empty>);
    benchmark::RegisterBenchmark(
        (name + "/fused").c_str(),
        packet_benchmark<N, Source, Transform, Predicate, Sink, pp::mode::fused, Empty>);
}
template <unsigned N> void register_grain() {
    register_packet<N, 0, 0, 0, 0>();
    register_packet<N, 0, 1, 1, 1>();
    register_packet<N, 1, 0, 0, 1>();
    register_packet<N, 1, 2, 2, 0>();
    register_packet<N, 1, 0, 0, 1, true>();
}
void register_packet_pipelines() {
    register_grain<16>();
    register_grain<32>();
#if defined(__AVX2__)
    register_grain<64>();
#endif
}

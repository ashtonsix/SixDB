#include <benchmark/benchmark.h>
#include <cstdlib>
#include <ikea/seriespack/read.h>
#include <ikea/seriespack/write.h>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace sp = ikea::seriespack;
// Probe-only physical type: identical kernels, different 12-bit byte offsets.
// Never serialize it as the production format's descriptor.
struct centred12 : sp::format<12, sp::geometry::striped> {};
namespace ikea::seriespack::detail {
template <> constexpr std::size_t body_offset<centred12>(std::size_t row) {
    return row + (row >= 32 ? 32 : 0);
}
template <> constexpr std::size_t stripe_offset<centred12>(unsigned) { return 32; }
} // namespace ikea::seriespack::detail
using body_first12 = sp::format<12, sp::geometry::striped>;
struct release {
    void operator()(std::uint8_t *p) const { std::free(p); }
};
std::uint16_t value(std::size_t row) { return ((row * 2654435761ULL) ^ (row >> 8)) & 4095; }

template <class F, unsigned Operation> void measure(benchmark::State &state) {
    const std::size_t count = state.range(0), stride = state.range(1);
    const auto bytes = count / 64 * stride;
    std::unique_ptr<std::uint8_t, release> memory(
        static_cast<std::uint8_t *>(std::aligned_alloc(64, bytes)));
    if (!memory)
        std::abort();
    std::memset(memory.get(), 0x96, bytes);
    for (std::size_t row = 0; row < count; ++row)
        sp::detail::put_payload<F>(memory.get() + row / 64 * stride, row % 64, value(row));
    const auto view =
        *sp::view<F, std::uint8_t>::attach(count, {{{{memory.get(), bytes}, stride}, {}, {}}});
    auto reader = sp::bind_decoder<std::uint16_t>(view);
    const auto prepared = *sp::bind_mutation(view);
    auto writer = prepared.template erase<std::uint16_t, sp::no_summary>();
    benchmark::DoNotOptimize(reader);
    benchmark::DoNotOptimize(writer);
    // Advance through a large query stream, rather than repeatedly warming a
    // tiny fixed address set inside the nominally large allocation.
    std::vector<std::uint32_t> queries(1 << 20);
    std::mt19937 random(47);
    for (auto &row : queries)
        row = random() % count;
    std::vector<std::uint16_t> output(Operation == 2 ? count : 16);
    const auto written = std::min<std::size_t>(count, 8192);
    std::vector<std::uint16_t> input(written);
    for (unsigned i = 0; i < written; ++i)
        input[i] = value(i) ^ 4095;
    std::vector<sp::composition::owner_write> records(written);
    sp::composition::write_journal effects{records};
    sp::no_summary summary;
    for (std::size_t row = 0; row < count; row += 16) {
        reader.read16_unchecked(row, output.data());
        for (unsigned j = 0; j < 16; ++j)
            if (output[j] != value(row + j))
                std::abort();
    }
    std::size_t cursor = 0;
    constexpr unsigned batch = 8192;
    for (auto _ : state) {
        std::uint64_t sum = 0;
        if constexpr (Operation < 2) {
            for (unsigned q = 0; q < batch; ++q) {
                const auto row = queries[(cursor + q) & (queries.size() - 1)];
                if constexpr (Operation == 0)
                    sum += reader.get_unchecked(row);
                else {
                    reader.read16_unchecked(row & ~std::size_t{15}, output.data());
                    for (auto x : output)
                        sum += x;
                }
            }
            cursor += batch;
            benchmark::DoNotOptimize(sum);
        } else if constexpr (Operation == 2) {
            reader.read_unchecked(0, count, output.data());
            benchmark::ClobberMemory();
        } else {
            effects.used = 0;
            writer.replace_unchecked(0, written, input.data(), sp::row_selection::all(), summary,
                                     effects);
            benchmark::DoNotOptimize(effects.used);
            benchmark::ClobberMemory();
        }
    }
    if constexpr (Operation == 3)
        for (unsigned row = 0; row < written; ++row)
            if (reader.get_unchecked(row) != input[row])
                std::abort();
    state.SetItemsProcessed(state.iterations() * (Operation < 2    ? batch
                                                  : Operation == 2 ? count
                                                                   : written));
    state.counters["payload_bytes"] = bytes;
}
int main(int argc, char **argv) {
    benchmark::Initialize(&argc, argv);
    sp::detail::each<4>([](auto operation) {
        const std::string name = std::array{"point", "group16", "scan", "write"}[operation];
        auto add = [&](const char *layout, auto function) {
            auto *b = benchmark::RegisterBenchmark((name + "/" + layout).c_str(), function);
            for (auto count : {8192, 1 << 20, 1 << 26})
                for (auto stride : {96, 128})
                    if (operation < 3 || count == 8192)
                        b->Args({count, stride});
        };
        add("body-first", measure<body_first12, operation>);
        add("centred", measure<centred12, operation>);
    });
    benchmark::RunSpecifiedBenchmarks();
}

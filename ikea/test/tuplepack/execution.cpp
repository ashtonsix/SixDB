#include <ikea/tuplepack/author/batch.h>
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/routes.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>

using namespace ikea::tuplepack;
#if defined(__aarch64__) || defined(__AVX2__)
namespace {
using pipeline = chain<8>;
struct context {
    unsigned visits = 0;
    std::size_t row = 0;
    std::array<byte, 64> result{};
};
pipeline::result stage(void* p, std::size_t, std::uint64_t active, native::pipeline_values values,
                       unsigned ordinal) {
#if defined(__aarch64__)
    // A preserve_none stage may use x19, including as its own realigned frame
    // base. Deliberately exercise that freedom across a realigned caller below.
    asm volatile("mov x19, #7" ::: "x19");
#endif
    auto& c = *static_cast<context*>(p);
    ++c.visits;
    // One authored native body can be called directly or through this shell.
    std::array<byte, 64> mask;
    mask.fill(byte(0xff >> (ordinal + 1)));
    auto value = native::bit_and(values.get(), native::load_packet(mask.data()));
    return {native::pipeline_values::from(value), active, ordinal == 1};
}
void finish(void* p, std::size_t row, std::uint64_t active, native::pipeline_values values) {
    auto& c = *static_cast<context*>(p);
    c.row = row;
    assert(active == 5);
    native::store_packet(c.result.data(), values.get());
}
[[gnu::noinline]] void realigned_caller(const pipeline& plan, context& c, unsigned count) {
    alignas(64) volatile unsigned fixed[64];
    auto* dynamic = static_cast<volatile unsigned*>(__builtin_alloca(count * sizeof(unsigned)));
    for (unsigned i = 0; i < 64; ++i)
        fixed[i] = i + 3;
    for (unsigned i = 0; i < count; ++i)
        dynamic[i] = i + 7;
    std::array<byte, 64> input;
    input.fill(0xff);
    plan.run(&c, 70001, 5, native::pipeline_values::from(native::load_packet(input.data())));
    for (unsigned i = 0; i < 64; ++i)
        assert(fixed[i] == i + 3);
    for (unsigned i = 0; i < count; ++i)
        assert(dynamic[i] == i + 7);
}
template <unsigned Rows> void batch_check(std::mt19937& random) {
    for (unsigned extent : {1, 15, 16, 17, 31, 32, 47, 64}) {
        std::array<code, 64> codes;
        for (unsigned i = 0; i < extent; ++i) {
            const auto width = 1 + i % 8;
            codes[i] = {byte(i), byte(i % (9 - width)), byte(width)};
        }
        const auto format = *layout::make(extent, std::span(codes).first(extent));
        for (bool spread : {false, true}) {
            std::array<byte, 64 / Rows> map;
            for (unsigned i = 0; i < map.size(); ++i)
                map[i] = i % 7 == 0 ? hole : byte((spread ? 11 * i + 3 : i) % extent);
            const auto batch = *batch_reader<Rows>::make(format, map);
            for (unsigned stride : {extent, 96u}) {
                std::vector<byte> storage((Rows - 1) * stride + extent);
                for (auto& b : storage)
                    b = random();
                const auto view = *const_view::bind(format, storage, Rows, stride);
                for (unsigned mask = 0; mask < (1u << Rows); ++mask) {
                    std::array<const byte*, Rows> pointers{};
                    std::array<byte, 64> wanted{}, actual{};
                    for (unsigned row = 0; row < Rows; ++row)
                        if (mask & (1u << row)) {
                            pointers[row] = storage.data() + row * stride;
                            for (unsigned i = 0; i < map.size(); ++i)
                                if (map[i] != hole) {
                                    const auto c = codes[map[i]];
                                    for (unsigned bit = 0; bit < c.width; ++bit)
                                        wanted[row * map.size() + i] |=
                                            ((pointers[row][c.offset] >> (c.shift + bit)) & 1)
                                            << bit;
                                }
                        }
                    native::store_packet(actual.data(), batch.gather_unchecked(pointers, mask));
                    assert(actual == wanted);
                    native::store_packet(actual.data(), batch.read_unchecked(view, 0, mask));
                    assert(actual == wanted);
                }
            }
        }
    }
}
} // namespace
#endif
int main(int argc, char**) {
#if defined(__aarch64__) || defined(__AVX2__)
    std::mt19937 random(391);
    for (unsigned trial = 0; trial < 512; ++trial) {
        auto bits = composition::zero_routes();
        if (trial < 256) {
            for (unsigned i = 0; i < 64; ++i) {
                unsigned source = random() % 64, rotation = random() % 8;
                for (unsigned b = 0; b < 8; ++b)
                    bits[i * 8 + b] = source * 8 + (rotation + b) % 8;
            }
        } else
            for (auto& bit : bits)
                bit = random() % 513 - 1;
        std::array<composition::route_term, 16> terms;
        auto route = composition::prepare_routes(bits, terms);
        assert(route);
        assert(route->normalized == (trial < 256));
        std::array<byte, 64> input{}, wanted{}, actual{};
        for (auto& b : input)
            b = random();
        for (unsigned b = 0; b < 512; ++b)
            if (bits[b] >= 0)
                wanted[b / 8] |= ((input[bits[b] / 8] >> (bits[b] % 8)) & 1) << (b % 8);
        native::store_packet(actual.data(),
                             composition::apply_routes(native::load_packet(input.data()), *route));
        assert(actual == wanted);
        assert(*composition::compose(composition::identity_routes(), bits) == bits);
        assert(*composition::unite(composition::zero_routes(), bits) == bits);
    }
    auto illegal = composition::identity_routes();
    illegal[0] = 512;
    std::array<composition::route_term, 16> terms;
    assert(!composition::prepare_routes(illegal, terms));
    auto other = composition::identity_routes();
    other[0] = 1;
    assert(!composition::unite(composition::identity_routes(), other));
    std::array<pipeline::function, 3> stages{pipeline::stage<stage>, pipeline::stage<stage>,
                                             pipeline::stage<stage>};
    auto chain = pipeline::prepare(stages, pipeline::completion<finish>);
    assert(chain);
    std::array<byte, 64> input;
    input.fill(0xff);
    context c;
    chain->run(&c, 70001, 5, native::pipeline_values::from(native::load_packet(input.data())));
    if (c.visits != 2 || c.row != 70001)
        std::fprintf(stderr, "CPS completion: visits=%u row=%zu (expected 2,70001)\n", c.visits,
                     c.row);
    assert(c.visits == 2 && c.row == 70001);
    for (auto b : c.result)
        assert(b == 0x3f);
    context realigned;
    realigned_caller(*chain, realigned, unsigned(argc) * 11);
    assert(realigned.visits == 2 && realigned.row == 70001 && realigned.result == c.result);
    batch_check<2>(random);
    batch_check<4>(random);
    // Two register-valued packets write disjoint nibble codes in shared bytes.
    // Width admission covers both packets before the first native store.
    std::array<code, 128> codes;
    std::array<byte, 64> low_map, high_map, low_values, high_values, data;
    for (unsigned i = 0; i < 64; ++i) {
        codes[i] = {byte(i), 0, 4};
        codes[64 + i] = {byte(i), 4, 4};
        low_map[i] = i;
        high_map[i] = 64 + i;
        low_values[i] = i % 16;
        high_values[i] = 15 - i % 16;
    }
    auto format = *layout::make(64, codes);
    auto source = *view::bind(format, data, 1, 64);
    auto lo = *writer<64>::make(format, low_map), hi = *writer<64>::make(format, high_map);
    auto group = *composition::bind_group(native_writer(*bind_writer(lo, source)),
                                          native_writer(*bind_writer(hi, source)));
    std::array<ikea::owner_write, 2> records;
    ikea::source_write_journal effects{records};
    data.fill(0x5a);
    auto bad_high = high_values;
    bad_high[63] = 16;
    assert(!group.set(
        0, {native::load_packet(low_values.data()), native::load_packet(bad_high.data())},
        effects));
    for (auto b : data)
        assert(b == 0x5a);
    assert(effects.used == 0);
    assert(group.set(
        0, {native::load_packet(low_values.data()), native::load_packet(high_values.data())},
        effects));
    for (unsigned i = 0; i < 64; ++i)
        assert(data[i] == (low_values[i] | (high_values[i] << 4)));
    assert(effects.used == 1 && records[0].source == &source && records[0].bytes.size == 64);
    std::puts("TuplePack: 512 independent routes, CPS early completion, 2/4-row batch maps and "
              "inactive-null lanes passed");
#endif
}

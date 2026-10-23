#include "../support.h"
#include "../fixture.h"
#include <ikea/seriespack/detail/mutation/assignment.h>
#include <ikea/seriespack/write.h>

#if defined(__aarch64__) || defined(__AVX2__)
namespace {
template <class Operation>
concept temporary_summary_command = requires(const Operation& operation, ikea::journal& effects) {
    operation.set(0, std::uint64_t{1}, effects, sp::author::sum_change{});
};
template <class Operation>
concept old_summary_order = requires(const Operation& operation, ikea::journal& effects,
                                     sp::author::sum_change& summary) {
    operation.set(0, std::uint64_t{1}, summary, effects);
};
template <class Operation>
concept implicit_summary_command = requires(const Operation& operation, ikea::journal& effects) {
    operation.set(0, std::uint64_t{1}, effects);
};

void check_writer_arguments() {
    ikea_test::scope context{"writer argument order and no-summary defaults"};
    using F = sp::layout<7, sp::geometry::striped>;
    placed<F> storage;
    const auto destination = *sp::view<F>::attach(storage.count, storage.planes);
    const auto concrete = *sp::bind_writer(destination);
    const auto erased = concrete.erase<std::uint64_t>();
    using Concrete = decltype(concrete);
    using ErasedSummary = sp::erased_writer<std::uint64_t, sp::author::sum_change>;
    static_assert(!temporary_summary_command<Concrete>);
    static_assert(!temporary_summary_command<ErasedSummary>);
    static_assert(!old_summary_order<Concrete> && !old_summary_order<ErasedSummary>);
    static_assert(implicit_summary_command<Concrete>);
    static_assert(implicit_summary_command<decltype(erased)>);
    static_assert(!implicit_summary_command<ErasedSummary>);

    auto expected = storage.truth;
    std::array<ikea::journal::entry, 256> records;
    ikea::journal effects{records};
    IKEA_CHECK(concrete.set(3, std::uint64_t{11}, effects));
    IKEA_CHECK(erased.set(4, 12, effects));
    expected[3] = 11;
    expected[4] = 12;
    std::array<std::uint64_t, 20> values;
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = 30 + i;
    const std::span<const std::uint64_t> input{values};
    auto update_expected = [&](std::size_t first, std::size_t count, std::uint64_t active) {
        for (std::size_t i = 0; i < count; ++i)
            if (active & (std::uint64_t{1} << i))
                expected[first + i] = values[i];
    };
    IKEA_CHECK(concrete.replace(7, input.first(3), effects));
    update_expected(7, 3, 7);
    IKEA_CHECK(erased.replace(12, input.first(4), effects));
    update_expected(12, 4, 15);
    const std::array<std::uint16_t, 1> selected_bits{std::uint16_t{1u << 7}};
    const auto selected = sp::selection::bits(16, selected_bits);
    IKEA_CHECK(concrete.replace(22, input.first(3), effects, selected));
    update_expected(22, 3, 2);
    IKEA_CHECK(erased.replace(22, input.first(3), effects, selected));
    IKEA_CHECK(concrete.replace16(32, input.first(16), effects));
    update_expected(32, 16, 0xffff);
    erased.replace16_unchecked(48, values.data(), effects);
    update_expected(48, 16, 0xffff);
    IKEA_CHECK(concrete.replace16(64, input.first(16), effects, 0x5555));
    update_expected(64, 16, 0x5555);
    erased.replace16_unchecked(80, values.data(), effects, 0xaaaa);
    update_expected(80, 16, 0xaaaa);
    concrete.replace_unchecked(95, values.size(), values.data(), effects);
    update_expected(95, values.size(), 0xfffff);
    erased.replace_unchecked(123, values.size(), values.data(), effects);
    update_expected(123, values.size(), 0xfffff);

    // A late invalid value and a refused point leave bytes and effects intact.
    const auto used = effects.used;
    values.back() = 128;
    IKEA_CHECK(!erased.replace(160, input, effects));
    IKEA_CHECK(effects.used == used);
    ikea::journal no_space{{}};
    IKEA_CHECK(!concrete.set(3, std::uint64_t{29}, no_space));
    IKEA_CHECK(!no_space.used);
    for (std::size_t row = 0; row < storage.count; ++row)
        IKEA_CHECK(sp::get_unchecked(destination, row) == expected[row]);
    // The independent old wire encoder also checks inactive/padding bits.
    for (std::size_t tile = 0; tile < storage.tiles; ++tile) {
        std::array<std::uint8_t, F::tile_bytes> wire;
        old::detail::encode_tile<7, old::geometry::striped>(
            expected.data() + tile * F::tile_rows, wire.data());
        IKEA_CHECK(std::memcmp(wire.data(),
                               storage.planes[0].bytes.data() + tile * storage.planes[0].stride,
                               wire.size()) == 0);
    }
    IKEA_CHECK(effects.used > 0);
    for (const auto& write : effects.entries()) {
        IKEA_CHECK(write.source == &destination && write.plane == 0);
        IKEA_CHECK(write.offset <= storage.planes[0].bytes.size());
        IKEA_CHECK(write.size <= storage.planes[0].bytes.size() - write.offset);
    }
}
} // namespace
#endif

void check_composed_mutation() {
    ikea_test::scope context{"nested substitution: local23/head16 + striped7 + head8"};
#if defined(__aarch64__) || defined(__AVX2__)
    using A = sp::layout<23, sp::geometry::local, 16>;
    using B = sp::layout<7, sp::geometry::striped>;
    using C = sp::layout<8, sp::geometry::local, 8>;
    placed<A> a;
    placed<B> b;
    placed<C> c;
    auto av = *sp::view<A>::attach(a.count, a.planes);
    const auto bv = *sp::view<B>::attach(b.count, b.planes);
    const auto cv = *sp::view<C>::attach(c.count, c.planes);
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const auto bytes = (a.planes[0].bytes.size() + page - 1) / page * page;
    auto* dead = static_cast<std::uint8_t*>(
        mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    IKEA_CHECK(dead != MAP_FAILED);
    auto planes = a.planes;
    planes[0] = {std::as_writable_bytes(std::span(dead, bytes)), a.planes[0].stride};
    av = *sp::view<A>::attach(a.count, planes);
    const auto ax = cp::expression(av);
    const auto bx = cp::expression(bv);
    const auto cx = cp::expression(cv);
    using P = cp::payload_expression<7, decltype(ax.payload.body), decltype(bx)>;
    using X = cp::value_expression<23, 16, P, decltype(ax.head0), decltype(cx.head0)>;
    const X expression{{ax.payload.body, bx}, ax.head0, cx.head0};
    const auto prepared = sp::bind_writer(expression, a.count);
    IKEA_CHECK(prepared);
    const auto erased = prepared->template erase<std::uint64_t, sp::author::sum_change>();
    sp::mutation_diagnostic diagnostic;
    IKEA_CHECK(!sp::bind_writer(expression, a.count + 1, &diagnostic));
    IKEA_CHECK(diagnostic.source != nullptr);
    // Two semantic heads cannot silently write the same destination field.
    using Aliased = cp::value_expression<23, 16, P, decltype(ax.head0), decltype(ax.head0)>;
    const Aliased alias{{ax.payload.body, bx}, ax.head0, ax.head0};
    const auto rejected = sp::bind_writer(alias, a.count, &diagnostic);
    IKEA_CHECK(!rejected && rejected.error() == sp::error::overlap);
    IKEA_CHECK(diagnostic.source == &av && diagnostic.conflicting_source == &av);
    IKEA_CHECK(diagnostic.plane == 1 && diagnostic.conflicting_plane == 1);
    const auto av_copy = av;
    const auto ax_copy = cp::expression(av_copy);
    const Aliased alias_copy{{ax.payload.body, bx}, ax.head0, ax_copy.head0};
    IKEA_CHECK(!sp::bind_writer(alias_copy, a.count)); // compare bytes, not object identity
    struct captured_write {
        const void* owner;
        ikea::journal::entry write;
    };
    std::vector<captured_write> effects;
    effects.reserve(32);
    auto collect = [&](const auto& source, ikea::journal::entry write) {
        IKEA_CHECK((static_cast<const void*>(&source) == &av && write.plane == 1) ||
                    (static_cast<const void*>(&source) == &bv && write.plane == 0) ||
                    (static_cast<const void*>(&source) == &cv && write.plane == 1));
        effects.push_back({&source, write});
    };
    struct coverage_adapter {
        decltype(collect)& callback;
        void before(const sp::view<A>& v, unsigned plane, std::size_t offset, std::size_t size) noexcept {
            callback(v, {&v, plane, offset, size});
        }
        void before(const sp::view<B>& v, unsigned plane, std::size_t offset, std::size_t size) noexcept {
            callback(v, {&v, plane, offset, size});
        }
        void before(const sp::view<C>& v, unsigned plane, std::size_t offset, std::size_t size) noexcept {
            callback(v, {&v, plane, offset, size});
        }
    } coverage{collect};
    std::array<std::uint64_t, 16> input;
    std::mt19937_64 random(42);
    sp::author::sum_change summary;
    std::uint64_t delta = 0;
    std::array<ikea::journal::entry, 32> owned_effects;
    ikea::journal owned{owned_effects};
    input.fill(std::uint64_t{1} << 23);
    IKEA_CHECK(
        !prepared->replace16(0, std::span<const std::uint64_t>(input), owned, 0xffff, summary));
    IKEA_CHECK(!owned.used && !summary.finish());
    input.fill(123);
    ikea::journal no_capacity{{}};
    IKEA_CHECK(!prepared->replace16(0, std::span<const std::uint64_t>(input), no_capacity, 0xffff,
                                     summary));
    IKEA_CHECK(
        !prepared->replace16(1, std::span<const std::uint64_t>(input), owned, 0xffff, summary));
    IKEA_CHECK(!owned.used && !summary.finish());
    for (auto mask : {std::uint16_t{0xffff}, std::uint16_t{0xb6db}, std::uint16_t{1}}) {
        const auto first = mask == 1 ? 512u : 32u;
        for (auto& x : input)
            x = random() & 0x7fffff;
        const auto old_a =
            std::vector<std::uint8_t>(ikea::to_u8(a.planes[0].bytes).begin(), ikea::to_u8(a.planes[0].bytes).end());
        const auto old_head =
            std::vector<std::uint8_t>(ikea::to_u8(a.planes[2].bytes).begin(), ikea::to_u8(a.planes[2].bytes).end());
        effects.clear();
        owned.used = 0;
        if (mask == 0xffff) {
            IKEA_CHECK(prepared->replace16(first, std::span<const std::uint64_t>(input), owned,
                                            mask, summary));
        } else if (mask == 0xb6db)
            erased.replace16_unchecked(first, input.data(), owned, mask, summary);
        else
            cp::replace16_unchecked(expression, first, input.data(), mask, summary, coverage);
        for (const auto& effect : owned.entries()) {
            IKEA_CHECK((effect.source == &av && effect.plane == 1) ||
                        (effect.source == &bv && effect.plane == 0) ||
                        (effect.source == &cv && effect.plane == 1));
            effects.push_back({effect.source, effect});
        }
        for (unsigned j = 0; j < 16; ++j)
            if (mask & (1u << j)) {
                const auto i = first + j;
                const auto before = ((a.truth[i] >> 15) << 15) | (c.truth[i] << 7) | b.truth[i];
                delta += input[j] - before;
                a.truth[i] = (input[j] & 0x7f8000) | (a.truth[i] & 0x7fff);
                b.truth[i] = input[j] & 127;
                c.truth[i] = (input[j] >> 7) & 255;
            }
        IKEA_CHECK(summary.finish() == delta && !effects.empty());
        IKEA_CHECK(std::equal(old_a.begin(), old_a.end(), ikea::to_u8(a.planes[0].bytes).begin()));
        IKEA_CHECK(std::equal(old_head.begin(), old_head.end(), ikea::to_u8(a.planes[2].bytes).begin()));
        const auto actual_a = a.source();
        const auto actual_b = b.source();
        const auto actual_c = c.source();
        for (std::size_t i = 0; i < a.count; ++i) {
            IKEA_CHECK(sp::get_unchecked(actual_a, i) == a.truth[i]);
            IKEA_CHECK(sp::get_unchecked(actual_b, i) == b.truth[i]);
            IKEA_CHECK(sp::get_unchecked(actual_c, i) == c.truth[i]);
            cp::scalar_ops reader;
            IKEA_CHECK(cp::read(reader, expression, i, true).value ==
                        (((a.truth[i] >> 15) << 15) | (c.truth[i] << 7) | b.truth[i]));
        }
    }
    effects.clear();
    cp::replace16_unchecked(expression, 0, static_cast<const std::uint64_t*>(nullptr), 0, summary,
                            coverage);
    IKEA_CHECK(effects.empty() && summary.finish() == delta);
    std::vector<std::uint64_t> range_input(a.count);
    std::vector<ikea::journal::entry> range_records(512);
    std::array<std::uint16_t, 34> mask_words;
    for (unsigned pattern = 0; pattern < 4; ++pattern) {
        ikea_test::scope scenario{"bulk mutation", 0, 0, pattern};
        for (auto first :
             {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{257}, a.count}) {
            for (auto& value : range_input)
                value = random() & 0x7fffff;
            for (std::size_t r = 0; r < mask_words.size(); ++r)
                mask_words[r] = pattern == 2 && r % 4 == 1 ? 0 : 0xb6db;
            const auto selected = pattern == 0   ? sp::selection::all()
                                  : pattern == 3 ? sp::selection::none()
                                                 : sp::selection::bits(0, mask_words);
            ikea::journal writes{range_records};
            const auto count = a.count - first;
            IKEA_CHECK(erased.replace(first,
                                       std::span<const std::uint64_t>(range_input.data(), count),
                                       writes, selected, summary));
            for (std::size_t j = 0; j < count; ++j)
                if (selected.mask((first + j) / 16 * 16, 16) & (1u << ((first + j) % 16))) {
                    const auto row = first + j, value = range_input[j];
                    const auto before =
                        ((a.truth[row] >> 15) << 15) | (c.truth[row] << 7) | b.truth[row];
                    delta += value - before;
                    a.truth[row] = (value & 0x7f8000) | (a.truth[row] & 0x7fff);
                    b.truth[row] = value & 127;
                    c.truth[row] = (value >> 7) & 255;
                }
            IKEA_CHECK(summary.finish() == delta);
            if (pattern == 3)
                IKEA_CHECK(!writes.used);
            const auto actual_a = a.source();
            const auto actual_b = b.source();
            const auto actual_c = c.source();
            for (std::size_t i = 0; i < a.count; ++i) {
                IKEA_CHECK(sp::get_unchecked(actual_a, i) == a.truth[i]);
                IKEA_CHECK(sp::get_unchecked(actual_b, i) == b.truth[i]);
                IKEA_CHECK(sp::get_unchecked(actual_c, i) == c.truth[i]);
            }
        }
    }
    {
        ikea::journal writes{range_records};
        const auto before = summary.finish();
        range_input.back() = std::uint64_t{1} << 23;
        IKEA_CHECK(!erased.replace(0, std::span<const std::uint64_t>(range_input),
                                    writes, sp::selection::all(), summary));
        range_input.back() = 123;
        IKEA_CHECK(!erased.replace(0, std::span<const std::uint64_t>(range_input),
                                    writes,
                                    sp::selection::bits(0, std::span(mask_words).first(33)), summary));
        IKEA_CHECK(!erased.replace(0, std::span<const std::uint64_t>(range_input),
                                    writes, sp::selection::bits(1, mask_words), summary));
        IKEA_CHECK(!erased.replace(0, std::span<const std::uint64_t>(range_input),
                                    no_capacity, sp::selection::all(), summary));
        IKEA_CHECK(!writes.used && summary.finish() == before);
    }
    for (std::size_t row : {0, 7, 8, 15, 16, 255, 256, 528}) {
        const auto value = random() & 0x7fffff;
        const auto before = ((a.truth[row] >> 15) << 15) | (c.truth[row] << 7) | b.truth[row];
        effects.clear();
        owned.used = 0;
        IKEA_CHECK(erased.set(row, value, owned, summary));
        for (const auto& write : owned.entries())
            effects.push_back({write.source, write});
        delta += value - before;
        a.truth[row] = (value & 0x7f8000) | (a.truth[row] & 0x7fff);
        b.truth[row] = value & 127;
        c.truth[row] = (value >> 7) & 255;
        IKEA_CHECK(summary.finish() == delta && !effects.empty());
        const auto actual_a = a.source();
        const auto actual_b = b.source();
        const auto actual_c = c.source();
        for (std::size_t i = 0; i < a.count; ++i) {
            IKEA_CHECK(sp::get_unchecked(actual_a, i) == a.truth[i]);
            IKEA_CHECK(sp::get_unchecked(actual_b, i) == b.truth[i]);
            IKEA_CHECK(sp::get_unchecked(actual_c, i) == c.truth[i]);
        }
    }
    // Construction initializes actual substituted fields, including padding,
    // without reading the inaccessible original payload or clearing its peers.
    {
        const auto old_payload =
            std::vector<std::uint8_t>(ikea::to_u8(a.planes[0].bytes).begin(), ikea::to_u8(a.planes[0].bytes).end());
        const auto old_head =
            std::vector<std::uint8_t>(ikea::to_u8(a.planes[2].bytes).begin(), ikea::to_u8(a.planes[2].bytes).end());
        std::array<std::vector<std::uint8_t>, 3> before{
            std::vector<std::uint8_t>(ikea::to_u8(a.planes[1].bytes).begin(), ikea::to_u8(a.planes[1].bytes).end()),
            std::vector<std::uint8_t>(ikea::to_u8(b.planes[0].bytes).begin(), ikea::to_u8(b.planes[0].bytes).end()),
            std::vector<std::uint8_t>(ikea::to_u8(c.planes[1].bytes).begin(), ikea::to_u8(c.planes[1].bytes).end())};
        ikea::journal writes{range_records};
        range_input.back() = std::uint64_t{1} << 23;
        IKEA_CHECK(!erased.initialize(std::span<const std::uint64_t>(range_input), writes));
        range_input.back() = 42;
        IKEA_CHECK(!erased.initialize(std::span<const std::uint64_t>(range_input), no_capacity));
        const auto prefix = sp::bind_writer(expression, a.count - 1);
        IKEA_CHECK(prefix);
        IKEA_CHECK(!prefix->initialize(
            std::span<const std::uint64_t>(range_input).first(a.count - 1), writes));
        IKEA_CHECK(!writes.used);
        IKEA_CHECK(erased.initialize(std::span<const std::uint64_t>(range_input), writes));
        IKEA_CHECK(summary.finish() == delta); // initialization has no old-value delta
        IKEA_CHECK(std::equal(old_payload.begin(), old_payload.end(), ikea::to_u8(a.planes[0].bytes).begin()));
        IKEA_CHECK(std::equal(old_head.begin(), old_head.end(), ikea::to_u8(a.planes[2].bytes).begin()));
        for (std::size_t row = 0; row < a.count; ++row) {
            cp::scalar_ops reader;
            IKEA_CHECK(cp::read(reader, expression, row, true).value == range_input[row]);
        }
        for (std::size_t row = a.count; row < a.truth.size(); ++row)
            IKEA_CHECK(ikea::to_u8(a.planes[1].bytes)[row / 8 * a.planes[1].stride + row % 8] == 0);
        for (std::size_t row = c.count; row < c.truth.size(); ++row)
            IKEA_CHECK(ikea::to_u8(c.planes[1].bytes)[row / 8 * c.planes[1].stride + row % 8] == 0);
        for (std::size_t tile = 0; tile < b.tiles; ++tile) {
            std::array<std::uint64_t, B::tile_rows> values{};
            for (std::size_t j = 0; j < B::tile_rows && tile * B::tile_rows + j < a.count; ++j)
                values[j] = range_input[tile * B::tile_rows + j] & 127;
            std::array<std::uint8_t, B::tile_bytes> expected;
            old::detail::encode_tile<7, old::geometry::striped>(values.data(), expected.data());
            IKEA_CHECK(std::equal(expected.begin(), expected.end(),
                                   ikea::to_u8(b.planes[0].bytes).begin() + tile * b.planes[0].stride));
        }
        const std::array<const void*, 3> owners{&av, &bv, &cv};
        const std::array<unsigned, 3> planes_index{1, 0, 1};
        const std::array<sp::plane<ikea::byte>, 3> output{a.planes[1], b.planes[0], c.planes[1]};
        const std::array<std::size_t, 3> occupied{8, B::tile_bytes, 8};
        for (unsigned p = 0; p < 3; ++p)
            for (std::size_t offset = 0; offset < before[p].size(); ++offset) {
                bool covered = false;
                for (const auto& effect : writes.entries())
                    if (effect.source == owners[p] && effect.plane == planes_index[p])
                        covered |= offset >= effect.offset &&
                                   offset - effect.offset < effect.size;
                const bool field = offset % output[p].stride < occupied[p];
                IKEA_CHECK(covered == field);
                if (!field)
                    IKEA_CHECK(before[p][offset] == ikea::to_u8(output[p].bytes)[offset]);
            }
    }
    IKEA_CHECK(munmap(dead, bytes) == 0);
    check_writer_arguments();
#endif
}

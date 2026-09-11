#include "../support.h"
#include "../fixture.h"
#include <ikea2/seriespack/detail/mutation/assignment.h>
#include <ikea2/seriespack/author/write.h>

void check_composed_mutation() {
    ikea2_test::scope context{"nested substitution: local23/head16 + striped7 + head8"};
#if defined(__aarch64__) || defined(__AVX2__)
    using A = sp::format<23, sp::geometry::local, 16>;
    using B = sp::format<7, sp::geometry::striped>;
    using C = sp::format<8, sp::geometry::local, 8>;
    placed<A> a;
    placed<B> b;
    placed<C> c;
    auto av = *sp::view<A, std::uint8_t>::attach(a.count, a.planes);
    const auto bv = *sp::view<B, std::uint8_t>::attach(b.count, b.planes);
    const auto cv = *sp::view<C, std::uint8_t>::attach(c.count, c.planes);
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const auto bytes = (a.planes[0].bytes.size() + page - 1) / page * page;
    auto* dead = static_cast<std::uint8_t*>(
        mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    IKEA2_CHECK(dead != MAP_FAILED);
    auto planes = a.planes;
    planes[0] = {{dead, bytes}, a.planes[0].stride};
    av = *sp::view<A, std::uint8_t>::attach(a.count, planes);
    const auto ax = cp::describe(av);
    const auto bx = cp::describe(bv);
    const auto cx = cp::describe(cv);
    using P = cp::payload_expression<7, decltype(ax.payload.body), decltype(bx)>;
    using X = cp::value_expression<23, 16, P, decltype(ax.head0), decltype(cx.head0)>;
    const X expression{{ax.payload.body, bx}, ax.head0, cx.head0};
    const auto prepared = cp::prepare_mutation(expression, a.count);
    IKEA2_CHECK(prepared);
    const auto erased = prepared->template erase<std::uint64_t, sp::sum_change>();
    sp::mutation_diagnostic diagnostic;
    IKEA2_CHECK(!cp::prepare_mutation(expression, a.count + 1, &diagnostic));
    IKEA2_CHECK(diagnostic.source != nullptr);
    // Two semantic heads cannot silently write the same destination field.
    using Aliased = cp::value_expression<23, 16, P, decltype(ax.head0), decltype(ax.head0)>;
    const Aliased alias{{ax.payload.body, bx}, ax.head0, ax.head0};
    const auto rejected = cp::prepare_mutation(alias, a.count, &diagnostic);
    IKEA2_CHECK(!rejected && rejected.error() == sp::error::overlap);
    IKEA2_CHECK(diagnostic.source == &av && diagnostic.conflicting_source == &av);
    IKEA2_CHECK(diagnostic.plane == 1 && diagnostic.conflicting_plane == 1);
    const auto av_copy = av;
    const auto ax_copy = cp::describe(av_copy);
    const Aliased alias_copy{{ax.payload.body, bx}, ax.head0, ax_copy.head0};
    IKEA2_CHECK(!cp::prepare_mutation(alias_copy, a.count)); // compare bytes, not object identity
    struct captured_write {
        const void* owner;
        sp::byte_write write;
    };
    std::vector<captured_write> effects;
    effects.reserve(32);
    auto collect = [&](const auto& source, sp::byte_write write) {
        IKEA2_CHECK((static_cast<const void*>(&source) == &av && write.plane == 1) ||
                    (static_cast<const void*>(&source) == &bv && write.plane == 0) ||
                    (static_cast<const void*>(&source) == &cv && write.plane == 1));
        effects.push_back({&source, write});
    };
    struct coverage_adapter {
        decltype(collect)& callback;
        void before(const sp::view<A, std::uint8_t>& v, sp::byte_write w) {
            callback(v, w);
        }
        void before(const sp::view<B, std::uint8_t>& v, sp::byte_write w) {
            callback(v, w);
        }
        void before(const sp::view<C, std::uint8_t>& v, sp::byte_write w) {
            callback(v, w);
        }
    } coverage{collect};
    std::array<std::uint64_t, 16> input;
    std::mt19937_64 random(42);
    sp::sum_change summary;
    std::uint64_t delta = 0;
    std::array<cp::owner_write, 32> owned_effects;
    cp::write_journal owned{owned_effects};
    input.fill(std::uint64_t{1} << 23);
    IKEA2_CHECK(
        !prepared->replace16(0, std::span<const std::uint64_t>(input), 0xffff, summary, owned));
    IKEA2_CHECK(!owned.used && !summary.finish());
    input.fill(123);
    cp::write_journal no_capacity{{}};
    IKEA2_CHECK(!prepared->replace16(0, std::span<const std::uint64_t>(input), 0xffff, summary,
                                     no_capacity));
    IKEA2_CHECK(
        !prepared->replace16(1, std::span<const std::uint64_t>(input), 0xffff, summary, owned));
    IKEA2_CHECK(!owned.used && !summary.finish());
    for (auto mask : {std::uint16_t{0xffff}, std::uint16_t{0xb6db}, std::uint16_t{1}}) {
        const auto first = mask == 1 ? 512u : 32u;
        for (auto& x : input)
            x = random() & 0x7fffff;
        const auto old_a =
            std::vector<std::uint8_t>(a.planes[0].bytes.begin(), a.planes[0].bytes.end());
        const auto old_head =
            std::vector<std::uint8_t>(a.planes[2].bytes.begin(), a.planes[2].bytes.end());
        effects.clear();
        owned.used = 0;
        if (mask == 0xffff) {
            IKEA2_CHECK(prepared->replace16(first, std::span<const std::uint64_t>(input), mask,
                                            summary, owned));
        } else if (mask == 0xb6db)
            erased.replace16_unchecked(first, input.data(), mask, summary, owned);
        else
            cp::replace16_unchecked(expression, first, input.data(), mask, summary, coverage);
        for (const auto& effect : owned.entries()) {
            IKEA2_CHECK((effect.source == &av && effect.bytes.plane == 1) ||
                        (effect.source == &bv && effect.bytes.plane == 0) ||
                        (effect.source == &cv && effect.bytes.plane == 1));
            effects.push_back({effect.source, effect.bytes});
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
        IKEA2_CHECK(summary.finish() == delta && !effects.empty());
        IKEA2_CHECK(std::equal(old_a.begin(), old_a.end(), a.planes[0].bytes.begin()));
        IKEA2_CHECK(std::equal(old_head.begin(), old_head.end(), a.planes[2].bytes.begin()));
        const auto actual_a = a.source();
        const auto actual_b = b.source();
        const auto actual_c = c.source();
        for (std::size_t i = 0; i < a.count; ++i) {
            IKEA2_CHECK(sp::get_unchecked(actual_a, i) == a.truth[i]);
            IKEA2_CHECK(sp::get_unchecked(actual_b, i) == b.truth[i]);
            IKEA2_CHECK(sp::get_unchecked(actual_c, i) == c.truth[i]);
            cp::scalar_ops reader;
            IKEA2_CHECK(cp::read(reader, expression, i, true).value ==
                        (((a.truth[i] >> 15) << 15) | (c.truth[i] << 7) | b.truth[i]));
        }
    }
    effects.clear();
    cp::replace16_unchecked(expression, 0, static_cast<const std::uint64_t*>(nullptr), 0, summary,
                            coverage);
    IKEA2_CHECK(effects.empty() && summary.finish() == delta);
    std::vector<std::uint64_t> range_input(a.count);
    std::vector<cp::owner_write> range_records(512);
    std::array<std::uint16_t, 34> mask_words;
    for (unsigned pattern = 0; pattern < 4; ++pattern) {
        ikea2_test::scope scenario{"bulk mutation", 0, 0, pattern};
        for (auto first :
             {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{257}, a.count}) {
            for (auto& value : range_input)
                value = random() & 0x7fffff;
            for (std::size_t r = 0; r < mask_words.size(); ++r)
                mask_words[r] = pattern == 2 && r % 4 == 1 ? 0 : 0xb6db;
            const auto selected = pattern == 0   ? sp::row_selection::all()
                                  : pattern == 3 ? sp::row_selection::none()
                                                 : sp::row_selection::regions(0, mask_words);
            cp::write_journal writes{range_records};
            const auto count = a.count - first;
            IKEA2_CHECK(erased.replace(first,
                                       std::span<const std::uint64_t>(range_input.data(), count),
                                       selected, summary, writes));
            for (std::size_t j = 0; j < count; ++j)
                if (selected((first + j) / 16 * 16) & (1u << ((first + j) % 16))) {
                    const auto row = first + j, value = range_input[j];
                    const auto before =
                        ((a.truth[row] >> 15) << 15) | (c.truth[row] << 7) | b.truth[row];
                    delta += value - before;
                    a.truth[row] = (value & 0x7f8000) | (a.truth[row] & 0x7fff);
                    b.truth[row] = value & 127;
                    c.truth[row] = (value >> 7) & 255;
                }
            IKEA2_CHECK(summary.finish() == delta);
            if (pattern == 3)
                IKEA2_CHECK(!writes.used);
            const auto actual_a = a.source();
            const auto actual_b = b.source();
            const auto actual_c = c.source();
            for (std::size_t i = 0; i < a.count; ++i) {
                IKEA2_CHECK(sp::get_unchecked(actual_a, i) == a.truth[i]);
                IKEA2_CHECK(sp::get_unchecked(actual_b, i) == b.truth[i]);
                IKEA2_CHECK(sp::get_unchecked(actual_c, i) == c.truth[i]);
            }
        }
    }
    {
        cp::write_journal writes{range_records};
        const auto before = summary.finish();
        range_input.back() = std::uint64_t{1} << 23;
        IKEA2_CHECK(!erased.replace(0, std::span<const std::uint64_t>(range_input),
                                    sp::row_selection::all(), summary, writes));
        range_input.back() = 123;
        IKEA2_CHECK(!erased.replace(0, std::span<const std::uint64_t>(range_input),
                                    sp::row_selection::regions(0, std::span(mask_words).first(33)),
                                    summary, writes));
        IKEA2_CHECK(!erased.replace(0, std::span<const std::uint64_t>(range_input),
                                    sp::row_selection::regions(1, mask_words), summary, writes));
        IKEA2_CHECK(!erased.replace(0, std::span<const std::uint64_t>(range_input),
                                    sp::row_selection::all(), summary, no_capacity));
        IKEA2_CHECK(!writes.used && summary.finish() == before);
    }
    for (std::size_t row : {0, 7, 8, 15, 16, 255, 256, 528}) {
        const auto value = random() & 0x7fffff;
        const auto before = ((a.truth[row] >> 15) << 15) | (c.truth[row] << 7) | b.truth[row];
        effects.clear();
        owned.used = 0;
        IKEA2_CHECK(erased.set(row, value, summary, owned));
        for (const auto& write : owned.entries())
            effects.push_back({write.source, write.bytes});
        delta += value - before;
        a.truth[row] = (value & 0x7f8000) | (a.truth[row] & 0x7fff);
        b.truth[row] = value & 127;
        c.truth[row] = (value >> 7) & 255;
        IKEA2_CHECK(summary.finish() == delta && !effects.empty());
        const auto actual_a = a.source();
        const auto actual_b = b.source();
        const auto actual_c = c.source();
        for (std::size_t i = 0; i < a.count; ++i) {
            IKEA2_CHECK(sp::get_unchecked(actual_a, i) == a.truth[i]);
            IKEA2_CHECK(sp::get_unchecked(actual_b, i) == b.truth[i]);
            IKEA2_CHECK(sp::get_unchecked(actual_c, i) == c.truth[i]);
        }
    }
    // Construction initializes actual substituted fields, including padding,
    // without reading the inaccessible original payload or clearing its peers.
    {
        const auto old_payload =
            std::vector<std::uint8_t>(a.planes[0].bytes.begin(), a.planes[0].bytes.end());
        const auto old_head =
            std::vector<std::uint8_t>(a.planes[2].bytes.begin(), a.planes[2].bytes.end());
        std::array<std::vector<std::uint8_t>, 3> before{
            std::vector<std::uint8_t>(a.planes[1].bytes.begin(), a.planes[1].bytes.end()),
            std::vector<std::uint8_t>(b.planes[0].bytes.begin(), b.planes[0].bytes.end()),
            std::vector<std::uint8_t>(c.planes[1].bytes.begin(), c.planes[1].bytes.end())};
        cp::write_journal writes{range_records};
        range_input.back() = std::uint64_t{1} << 23;
        IKEA2_CHECK(!erased.initialize(std::span<const std::uint64_t>(range_input), writes));
        range_input.back() = 42;
        IKEA2_CHECK(!erased.initialize(std::span<const std::uint64_t>(range_input), no_capacity));
        const auto prefix = cp::prepare_mutation(expression, a.count - 1);
        IKEA2_CHECK(prefix);
        IKEA2_CHECK(!prefix->initialize(
            std::span<const std::uint64_t>(range_input).first(a.count - 1), writes));
        IKEA2_CHECK(!writes.used);
        IKEA2_CHECK(erased.initialize(std::span<const std::uint64_t>(range_input), writes));
        IKEA2_CHECK(summary.finish() == delta); // initialization has no old-value delta
        IKEA2_CHECK(std::equal(old_payload.begin(), old_payload.end(), a.planes[0].bytes.begin()));
        IKEA2_CHECK(std::equal(old_head.begin(), old_head.end(), a.planes[2].bytes.begin()));
        for (std::size_t row = 0; row < a.count; ++row) {
            cp::scalar_ops reader;
            IKEA2_CHECK(cp::read(reader, expression, row, true).value == range_input[row]);
        }
        for (std::size_t row = a.count; row < a.truth.size(); ++row)
            IKEA2_CHECK(a.planes[1].bytes[row / 8 * a.planes[1].stride + row % 8] == 0);
        for (std::size_t row = c.count; row < c.truth.size(); ++row)
            IKEA2_CHECK(c.planes[1].bytes[row / 8 * c.planes[1].stride + row % 8] == 0);
        for (std::size_t tile = 0; tile < b.tiles; ++tile) {
            std::array<std::uint64_t, B::tile_rows> values{};
            for (std::size_t j = 0; j < B::tile_rows && tile * B::tile_rows + j < a.count; ++j)
                values[j] = range_input[tile * B::tile_rows + j] & 127;
            std::array<std::uint8_t, B::tile_bytes> expected;
            old::detail::encode_tile<7, old::geometry::striped>(values.data(), expected.data());
            IKEA2_CHECK(std::equal(expected.begin(), expected.end(),
                                   b.planes[0].bytes.begin() + tile * b.planes[0].stride));
        }
        const std::array<const void*, 3> owners{&av, &bv, &cv};
        const std::array<unsigned, 3> planes_index{1, 0, 1};
        const std::array<sp::plane<std::uint8_t>, 3> output{a.planes[1], b.planes[0], c.planes[1]};
        const std::array<std::size_t, 3> occupied{8, B::tile_bytes, 8};
        for (unsigned p = 0; p < 3; ++p)
            for (std::size_t offset = 0; offset < before[p].size(); ++offset) {
                bool covered = false;
                for (const auto& effect : writes.entries())
                    if (effect.source == owners[p] && effect.bytes.plane == planes_index[p])
                        covered |= offset >= effect.bytes.offset &&
                                   offset - effect.bytes.offset < effect.bytes.size;
                const bool field = offset % output[p].stride < occupied[p];
                IKEA2_CHECK(covered == field);
                if (!field)
                    IKEA2_CHECK(before[p][offset] == output[p].bytes[offset]);
            }
    }
    IKEA2_CHECK(munmap(dead, bytes) == 0);
#endif
}

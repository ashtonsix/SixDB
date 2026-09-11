#include "adapter.h"
#include "../ikea-composition/probes/ikea-heterogeneous/fixture.h"
#include <cstdio>
#include <cstdlib>
#include <optional>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
using namespace bec_metadata;
static void require(bool value, const char* why) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", why); std::abort(); }
}
template<class F> static void rejects(F f) {
    try { f(); } catch (const std::invalid_argument&) { return; }
    require(false, "invalid input accepted");
}
int main() {
    std::uint64_t ranges = 0, entries = 0;
    for (unsigned n : {0u, 1u, 2u, 3u, 15u, 16u, 17u, 31u, 32u, 33u,
                        127u, 128u, 129u, 255u, 256u}) {
        for (unsigned seed : {0u, 1u, 128u, 255u, 256u}) {
            const auto plain = h::structural_blocks(n, seed);
            const auto encoded = h::encode_source(plain);
            auto metadata = std::make_shared<h::MetadataOwner>(encoded, h::MetadataKind::scan128);
            auto query = h::make_query(n, seed + 123);
            Source source(metadata, encoded.body(), query);
            require(source.lengths().size() == n, "logical length includes slack");
            require(source.lengths().placement().payload.stride == 96, "child stride");
            require(source.lengths().placement().payload.bytes.size() == metadata->capacity() * 6 / 8,
                    "child precise extent");
            for (unsigned i = 0; i < n; ++i)
                require(source.reader().get(i) == encoded.entries()[i].length, "SeriesPack same wire");
            for (unsigned group = 0; group < n; group += 16) {
                for (auto reader : {Reader::specialized, Reader::native, Reader::materialized}) {
                    std::uint32_t output[18];
                    std::fill_n(output, 18, 0xdededeadu);
                    refill_kernel(reader)(source, group, output + 1);
                    require(output[0] == 0xdededeadu && output[17] == 0xdededeadu, "refill output extent");
                    for (unsigned lane = 0; lane < 16; ++lane) {
                        const auto e = h::read_entry_reference(h::MetadataKind::scan128,
                            {metadata->bytes().data(), metadata->bytes().size()}, metadata->capacity(), group + lane);
                        require(output[1 + lane] == (unsigned(e.offset) << 16 | e.population), "refill oracle");
                        ++entries;
                    }
                }
            }
            for (unsigned first = 0; first <= n; ++first) {
                for (unsigned count = 0; count <= n - first; ++count) {
                    if (n > 33 && !(count <= 3 || count == n - first ||
                        ((first + count) % 16 <= 1 && (first % 16 <= 1 || first % 16 == 15)))) continue;
                    const auto expected = h::reference_count(plain, *query, first, count);
                    source.admit_range(first, count);
#if __has_feature(address_sanitizer)
                    // Bodies retain the existing whole allocation +64 admission;
                    // queries grant only requested 32-byte blocks, even on refills.
                    __asan_poison_memory_region(query->data(), first * 32);
                    __asan_poison_memory_region(query->data() + (first + count) * 32, (n - first - count) * 32);
#endif
                    for (auto reader : {Reader::specialized, Reader::native, Reader::materialized})
                        for (auto execution : {h::Execution::inlined, h::Execution::split}) {
                            require(count_kernel(reader, execution)(source, first, count) == expected, "compound count");
                            ++ranges;
                        }
#if __has_feature(address_sanitizer)
                    __asan_unpoison_memory_region(query->data(), n * 32);
#endif
                }
            }
            auto other = h::encode_source(plain);
            rejects([&] { Source bad(metadata, other.body(), query); });
            rejects([&] { source.admit_range(n + 1, 0); });
            rejects([&] { source.admit_range(n, 1); });
            if (n) {
                auto short_query = std::make_shared<h::AlignedBytes>(n * 32 - 1);
                rejects([&] { Source bad(metadata, encoded.body(), short_query); });
            }
        }
    }
    std::optional<Source> retained;
    std::uint64_t expected;
    {
        const auto plain = h::structural_blocks(129, 240);
        const auto encoded = h::encode_source(plain);
        auto metadata = std::make_shared<h::MetadataOwner>(encoded, h::MetadataKind::scan128);
        auto query = h::make_query(129, 98);
        expected = h::reference_count(plain, *query, 127, 2);
        Source temporary(metadata, encoded.body(), query);
        std::vector<Source> moved;
        moved.push_back(temporary);
        moved.push_back(std::move(temporary)); // reallocation exercises stored view lifetimes
        retained.emplace(std::move(moved.back()));
    }
    for (auto reader : {Reader::specialized, Reader::native, Reader::materialized})
        for (auto execution : {h::Execution::inlined, h::Execution::split})
            require(count_kernel(reader, execution)(*retained, 127, 2) == expected, "retained owners and moved views");
    std::printf("passed %llu compound ranges, %llu metadata entries, logical-tail, query-access, admission and owner-move checks\n",
        (unsigned long long)ranges, (unsigned long long)entries);
}

#include "consumer.h"
#include <random>
#include <cstdio>
using namespace bec_study;
template <layout L> void check_entries(const directory &d, std::span<const entry> expected) {
    cursor<L, resolution::native16> native(d);
    cursor<L, resolution::buffered16> buffered(d);
    for (unsigned j = 0; j < expected.size(); ++j) {
        const unsigned i = j * 73 % expected.size();
        for (auto got : {native.get(i), buffered.get(i)}) {
            assert(got.population == expected[i].population);
            assert(got.offset == expected[i].offset);
            assert(got.bytes == expected[i].bytes);
        }
    }
}
void check_entries(const directory &d, std::span<const entry> entries) {
    switch (d.kind()) {
    case layout::direct:
        return check_entries<layout::direct>(d, entries);
    case layout::direct32:
        return check_entries<layout::direct32>(d, entries);
    case layout::tuple_absolute:
        return check_entries<layout::tuple_absolute>(d, entries);
    case layout::tuple_checkpoint:
        return check_entries<layout::tuple_checkpoint>(d, entries);
    case layout::series_local:
        return check_entries<layout::series_local>(d, entries);
    case layout::series_scan:
        return check_entries<layout::series_scan>(d, entries);
    case layout::tuple_folded:
        return check_entries<layout::tuple_folded>(d, entries);
    case layout::series_folded_local:
        return check_entries<layout::series_folded_local>(d, entries);
    case layout::series_folded_scan:
        return check_entries<layout::series_folded_scan>(d, entries);
    }
}
int main() {
    std::mt19937_64 random(0xbec256);
    unsigned checked = 0;
    for (unsigned count : {1u, 17u, 129u, 256u, 257u, 4096u}) {
        std::vector<bc::byte> plain(count * 32), query(count * 32);
        for (unsigned i = 0; i < plain.size(); ++i) {
            plain[i] = bc::byte(random());
            query[i] = bc::byte(random());
        }
        // Terminal and short bodies among ordinary dense cases.
        for (unsigned i = 0; i < count; ++i)
            if (i % 7 == 0 || i % 11 == 0)
                std::fill_n(plain.data() + i * 32, 32, i % 7 == 0 ? bc::byte{0} : bc::byte{255});
        bitset bits(plain);
        std::vector<std::uint64_t> active((count + 63) / 64), inactive(active.size(), 0);
        for (auto &word : active)
            word = random() & random();
        for (auto kind : layouts)
            for (unsigned checkpoint : {16u, 64u}) {
                if (kind == layout::direct32 && bits.catalog.back().offset >= (1u << 17))
                    continue;
                directory d(kind, bits.catalog, checkpoint);
                bits.admit(d);
                check_entries(d, bits.catalog);
                for (auto mode : {resolution::point, resolution::buffered16, resolution::native16})
                for (auto e : {execution::inline_body, execution::shared_body,
                               execution::materialized, execution::cps, execution::cps_fused}) {
                        const auto call = bind_count(kind, mode, e);
                        for (unsigned first : {0u, std::min(15u, count - 1), count - 1})
                            for (unsigned n : {0u, 1u, std::min(18u, count - first), count - first})
                                for (unsigned every : {1u, 2u, 17u}) {
                                    request r{first, n, every};
                                    for (const auto *mask : std::array<const std::uint64_t *, 3>{
                                             nullptr, active.data(), inactive.data()}) {
                                        r.active = mask;
                                        const auto *query_data =
                                            mask == inactive.data() ? nullptr : query.data();
                                        const auto wanted = count_reference(bits, query_data, r);
                                        const auto got = call(d, bits, query_data, r);
                                        if (wanted != got) {
                                            std::fprintf(stderr,
                                                         "%s/%s/%s checkpoint=%u N=%u "
                                                         "range=%u/%u/%u got=%llu wanted=%llu\n",
                                                         name(kind), name(mode), name(e),
                                                         checkpoint, count, first, n, every,
                                                         (unsigned long long)got,
                                                         (unsigned long long)wanted);
                                            std::abort();
                                        }
                                        ++checked;
                                    }
                                }
                    }
                assert(d.update(count / 2, bits.catalog[count / 2]) > 0);
                bits.admit(d);
            }
    }
    std::printf(
        "Heterogeneous Bec256: %u composed range/selection/metadata/execution checks passed\n",
        checked);
}

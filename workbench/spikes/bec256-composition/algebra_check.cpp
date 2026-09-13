#include "algebra.h"
#include <cstdio>
#include <random>

int main() {
    using namespace bec_study;
    std::mt19937_64 rng(0xabc256);
    unsigned cases = 0;
    for (unsigned n : {129u, 256u}) {
        std::vector<bc::byte> pa(n * 32), pb(n * 32);
        for (unsigned i = 0; i < n; ++i)
            for (unsigned j = 0; j < 32; ++j) {
                pa[i * 32 + j] = bc::byte(i % 7 == 0 ? 0 : i % 7 == 1 ? 255 : rng());
                pb[i * 32 + j] = bc::byte(i % 11 == 0 ? 255 : i % 11 == 1 ? 0 : rng());
            }
        bitset a(pa), b(pb);
        std::vector<std::unique_ptr<directory>> da, db;
        for (auto kind : layouts) {
            da.push_back(std::make_unique<directory>(kind, a.catalog, 16));
            db.push_back(std::make_unique<directory>(kind, b.catalog, 64));
            a.admit(*da.back());
            b.admit(*db.back());
        }
        // Every layout appears independently on each side, including plain.
        for (unsigned ai = 0; ai <= layouts.size(); ++ai)
            for (unsigned bi = 0; bi <= layouts.size(); ++bi)
                for (auto lookup :
                     {resolution::point, resolution::buffered16, resolution::native16})
                    for (auto op : {boolean_op::intersection, boolean_op::set_union})
                        for (auto out : {output_kind::complete, output_kind::selected_only})
                            for (unsigned selection = 0; selection < 5; ++selection) {
                                std::vector<std::uint64_t> mask((n + 63) / 64);
                                for (unsigned i = 0; i < n; ++i) {
                                    bool active = selection == 0 ||
                                                  (selection == 2 && i % 17 == 0) ||
                                                  (selection == 3 && i >= 15 && i < 34) ||
                                                  (selection == 4 && ((rng() & 3) == 0));
                                    if (active)
                                        mask[i / 64] |= std::uint64_t{1} << (i % 64);
                                }
                                for (unsigned grain : {1u, 2u}) {
                                    std::vector<bc::byte> storage(n * 32 + 17, bc::byte{0xa5});
                                    bc::destination output{std::span(storage).first(n * 32)};
                                    std::vector<ikea::owner_write> records(n);
                                    ikea::source_write_journal effects{records};
                                    bind_algebra(op, out, grain)(
                                        {a, ai == layouts.size() ? nullptr : da[ai].get(), lookup},
                                        {b, bi == layouts.size() ? nullptr : db[bi].get(),
                                         lookup == resolution::point ? resolution::native16
                                                                     : resolution::point},
                                        mask.data(), output, effects);
                                    std::vector<bool> covered(n * 32);
                                    for (auto e : effects.entries()) {
                                        assert(e.source == &output && e.bytes.plane == 0);
                                        assert(e.bytes.offset + e.bytes.size <= n * 32);
                                        for (std::size_t j = e.bytes.offset;
                                             j < e.bytes.offset + e.bytes.size; ++j)
                                            covered[j] = true;
                                    }
                                    for (unsigned i = 0; i < n; ++i) {
                                        const bool selected = (mask[i / 64] >> (i % 64)) & 1;
                                        for (unsigned j = i * 32; j < (i + 1) * 32; ++j) {
                                            auto expected =
                                                selected ? (op == boolean_op::intersection
                                                                ? pa[j] & pb[j]
                                                                : pa[j] | pb[j])
                                                : out == output_kind::complete ? bc::byte{0}
                                                                               : bc::byte{0xa5};
                                            if (storage[j] != expected ||
                                                covered[j] !=
                                                    (selected || out == output_kind::complete)) {
                                                std::fprintf(stderr,
                                                             "algebra mismatch n=%u left=%u "
                                                             "right=%u lookup=%u op=%u output=%u "
                                                             "selection=%u grain=%u byte=%u\n",
                                                             n, ai, bi, unsigned(lookup),
                                                             unsigned(op), unsigned(out), selection,
                                                             grain, j);
                                                std::abort();
                                            }
                                        }
                                    }
                                    for (unsigned j = n * 32; j < storage.size(); ++j)
                                        assert(storage[j] == bc::byte{0xa5});
                                    ++cases;
                                }
                            }
    }
    std::printf("Heterogeneous algebra: %u independent layout/lookup/mask/grain/output/effect "
                "cases passed\n",
                cases);
}

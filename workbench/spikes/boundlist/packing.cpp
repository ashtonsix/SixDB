#include "mutation-fixture.h"
#include "legacy-read.h"
#include "legacy-write.h"
#include <chrono>
#include <cstdio>
#include <string>

struct alignas(64) journal {
    std::array<ikea::owner_write, 1024> entries;
    unsigned used = 0;
    template <class S> [[gnu::always_inline]] void before(const S& s, ikea::byte_write w) {
        assert(used < entries.size());
        entries[used++] = {&s, w};
    }
};
struct prepared {
    fixture table;
    std::vector<std::byte> metadata, payload;
    bool shrink;
    std::unique_ptr<journal[]> logs = std::make_unique<journal[]>(8);
    prepared(sp::description schema, bool shrink) : table(8, schema), shrink(shrink) {
        ikea::no_coverage none;
        for (auto& v : table.views) {
            for (unsigned r = 0; r < 256; ++r) {
                if (table.format.has_used()) {
                    const unsigned used = 8 + r % 4 * 4;
                    std::array<std::byte, 32> input;
                    input.fill(std::byte((r * 17 + 3) % 256));
                    assert(sp::replace(v, r, std::span(input).first(used), none));
                }
                if (!(r % 5)) assert(sp::retire(v, r, none));
            }
        }
        metadata.assign(table.metadata.get(), table.metadata.get() + table.stride * 8);
        payload = table.payload;
    }
    void reset() {
        // Identical input for every timed operation, restored outside its window.
        std::memcpy(table.metadata.get(), metadata.data(), metadata.size());
        std::memcpy(table.payload.data(), payload.data(), payload.size());
    }
    void verify(unsigned first, unsigned count) const {
        for (unsigned node = first; node < first + count; ++node) {
            const auto& v = table.views[node];
            assert(v.size() == 256 && v.validate());
            unsigned position = 0;
            sp::author::dispatch(v, [&](auto a) {
                for (unsigned r = 0; r < 256; ++r) {
                    const bool live = r % 5 != 0;
                    const unsigned used = table.format.has_used() ? 8 + r % 4 * 4 : 32;
                    const unsigned reservation = live ? (shrink ? used : 32) : 0;
                    assert(a.start(r) == position && a.reserved(r) == reservation);
                    assert(a.live(r) == live);
                    if (live) {
                        assert(a.length(r) == used);
                        const auto bytes = a.value(r);
                        const auto* original = payload.data() + node * 8192 + r * 32;
                        assert(std::memcmp(bytes.data(), original, bytes.size()) == 0);
                    } else if (table.format.has_used()) assert(a.length(r) == 0);
                    position += reservation;
                }
            });
            assert(v.extent() == position);
        }
    }
};
template <bool Legacy, unsigned Blocks, class A>
[[gnu::noinline]] word pack(fixture& f, unsigned first, bool shrink, A, journal* logs) {
    word result = 0;
    for (unsigned i = 0; i < Blocks; ++i) {
        const auto& v = f.views[first + i];
        auto& log = logs[i]; log.used = 0;
        using Access = std::conditional_t<Legacy, boundlist_before::author::access<A::family,
            typename A::width_type, typename A::offset_type, std::byte>, A>;
        const Access a(v);
        if constexpr (Legacy) [[clang::always_inline]] boundlist_legacy::soft_pack_unchecked(a, shrink, log);
        else [[clang::always_inline]] sp::author::soft_pack_unchecked(a, shrink, log);
        // Consume the actual source-qualified publication journal, not a count-only stand-in.
        word digest = 0;
        for (unsigned j = 0; j < log.used; ++j) {
            assert(log.entries[j].source == &v);
            const auto w = log.entries[j].bytes;
            digest += word(w.offset) * 17 + w.size * 3 + w.plane;
        }
        result += digest + v.extent();
    }
    return result;
}
volatile word observed;
template <bool Legacy, unsigned Blocks, class A>
void trial(prepared& p, unsigned rounds, unsigned rep, A a) {
    auto* logs = p.logs.get();
    std::chrono::nanoseconds elapsed{};
    word sum = 0;
    unsigned first = 0;
    for (unsigned r = 0; r < rounds; ++r) {
        p.reset();
        const auto begin = std::chrono::steady_clock::now();
        const auto result = pack<Legacy, Blocks>(p.table, first, p.shrink, a, logs);
        const auto end = std::chrono::steady_clock::now();
        elapsed += std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);
        sum += result;
        p.verify(first, Blocks);
        // Serial requests really depend on the previous completed publication.
        if constexpr (Blocks == 1) first = result % 8;
    }
    observed = sum;
    std::printf("%s,%u,%u,%u,%u,%s,%u,%u,%.6f,%llu,%zu,%zu,%zu\n",
        A::family == sp::geometry::width ? "width" : "direct", p.table.format.schema().maximum,
        unsigned(p.table.format.has_used()), unsigned(p.table.format.schema().lengths), unsigned(p.shrink),
        Legacy ? "before" : "production", Blocks, rep, double(elapsed.count()) / (rounds * Blocks),
        (unsigned long long)sum, p.metadata.size(), p.payload.size(), Blocks * sizeof(journal));
}
int main(int argc, char** argv) {
    const unsigned rounds = argc > 1 ? std::stoul(argv[1]) : 64;
    const unsigned reps = argc > 2 ? std::stoul(argv[2]) : 5;
    if (!rounds || !reps) return 2;
    std::puts("layout,maximum,used,placement,shrink,implementation,blocks,rep,ns_per_block,checksum,metadata_bytes,payload_bytes,journal_bytes");
    for (auto g : {sp::geometry::direct, sp::geometry::width})
        for (unsigned maximum : {255u, 4095u}) for (bool used : {false, true})
            for (auto placement : {sp::placement::grouped, sp::placement::separated}) for (bool shrink : {false, true}) {
                prepared p({g, placement, std::uint16_t(maximum), 8192, {}, !used, false}, shrink);
                sp::author::dispatch(p.table.views[0], [&](auto a) {
                    for (unsigned rep = 0; rep < reps; ++rep) {
                        if (rep % 2) { trial<false, 1>(p, rounds, rep, a); trial<false, 8>(p, rounds, rep, a); }
                        trial<true, 1>(p, rounds, rep, a); trial<true, 8>(p, rounds, rep, a);
                        if (!(rep % 2)) { trial<false, 1>(p, rounds, rep, a); trial<false, 8>(p, rounds, rep, a); }
                    }
                });
            }
}

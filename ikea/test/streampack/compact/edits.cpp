#include <ikea/intervals.h>
#include <ikea/streampack/detail/compact/read.h>
#include <ikea/streampack/detail/compact/write.h>
#include <ikea/intervals/author/search.h>
#include <cassert>
#include <iostream>
#include <vector>
namespace sc = ikea::streampack::detail::compact;
namespace bl = ikea::intervals;
struct fixture {
    sc::layout format;
    alignas(64) std::array<std::byte, 4096> directory{}, lengths{};
    std::array<std::byte, 6> control{};
    sc::view source;
    fixture(sc::description d)
        : format(*sc::layout::make(d)),
          source(*sc::view::attach(format, {std::span(directory).first(format.directory_bytes()),
                                          std::span(lengths).first(format.lengths_bytes()),
                                          std::span(control).first(format.control_bytes())})) {}
};
struct audit {
    const sc::view &named;
    std::array<std::array<std::byte, 4096>, 3> before{};
    std::array<std::array<bool, 4096>, 3> covered{};
    unsigned writes = 0, limit = 64;
    explicit audit(const sc::view &v) : named(v) {
        const auto p = v.windows().planes();
        for (unsigned s = 0; s < 3; ++s)
            std::copy(p[s].begin(), p[s].end(), before[s].begin());
    }
    unsigned remaining() const noexcept { return limit - writes; }
    void before_write(const sc::view &source, ikea::detail::write_span w) noexcept {
        assert(&source == &named && writes < limit && w.plane < 3 && w.size);
        const auto p = source.windows().planes()[w.plane];
        assert(w.offset <= p.size() && w.size <= p.size() - w.offset);
        std::fill_n(covered[w.plane].begin() + w.offset, w.size, true);
        ++writes;
    }
    void verify() const {
        const auto p = named.windows().planes();
        for (unsigned s = 0; s < 3; ++s)
            for (unsigned i = 0; i < p[s].size(); ++i)
                assert(covered[s][i] || before[s][i] == p[s][i]);
    }
};
struct effects {
    audit &out;
    unsigned remaining() const noexcept { return out.remaining(); }
    void before(const sc::view &s, unsigned plane, std::size_t offset, std::size_t size) noexcept { out.before_write(s, {plane, offset, size}); }
};
void verify(fixture &f, const std::vector<unsigned> &ends) {
    assert(f.source.validate() && f.source.size() == ends.size());
    assert(f.source.extent() == (ends.empty() ? 0 : ends.back()));
    sc::author::dispatch(f.source, [&](auto a) {
        unsigned previous = 0;
        for (unsigned r = 0; r < ends.size(); ++r) {
            const auto location = a.locate(r);
            assert(location.offset == previous && location.size == ends[r] - previous);
            previous = ends[r];
        }
    });
}
unsigned history(sc::geometry family, unsigned maximum, unsigned n) {
    fixture f({family, std::uint16_t(maximum), 256 * maximum, {}, false});
    ikea::no_effects scratch;
    assert(sc::initialize(f.source, scratch));
    std::vector<unsigned> sizes, ends;
    unsigned total = 0, cases = 0;
    for (unsigned i = 0; i < n; ++i) {
        const unsigned r = i % 7 == 0 ? 0 : i % 7 == 1 ? maximum : maximum / 2;
        sizes.push_back(r);
        ends.push_back(total += r);
        assert(sc::append(f.source, r, scratch));
    }
    const auto directory = f.directory, lengths = f.lengths;
    const auto control = f.control;
    auto reset = [&] {
        f.directory = directory;
        f.lengths = lengths;
        f.control = control;
    };
    for (unsigned rank = 0; rank < n; ++rank)
        for (int delta : {-1, 1}) {
            const auto next = std::int64_t(sizes[rank]) + delta;
            if (next < 0 || next > maximum)
                continue;
            reset();
            audit record(f.source);
            effects sink{record};
            assert(sc::resize(f.source, rank, next, sink));
            record.verify();
            auto expected = ends;
            for (unsigned r = rank; r < n; ++r)
                expected[r] += delta;
            verify(f, expected);
            ++cases;
            if (rank + 1 < n && (std::int64_t(sizes[rank + 1]) - delta < 0 ||
                                 std::int64_t(sizes[rank + 1]) - delta > maximum))
                continue;
            reset();
            audit second(f.source);
            effects output{second};
            assert(sc::set_boundary(f.source, rank, ends[rank] + delta, output));
            second.verify();
            expected = ends;
            expected[rank] += delta;
            verify(f, expected);
            ++cases;
            reset();
            audit refusal(f.source);
            refusal.limit = 0;
            effects denied{refusal};
            assert(!sc::set_boundary(f.source, rank, ends[rank] + delta, denied));
            assert(!refusal.writes && f.directory == directory && f.lengths == lengths &&
                   f.control == control);
            assert(!sc::resize(f.source, rank, next, denied));
            assert(!refusal.writes && f.directory == directory && f.lengths == lengths &&
                   f.control == control);
        }
    return cases;
}
void phase_lifetime() {
    for (auto family :
         {sc::geometry::absolute_ends, sc::geometry::sizes, sc::geometry::relative_ends}) {
        fixture f({family, 31, 256 * 31, {}, false});
        ikea::no_effects effects;
        assert(sc::initialize(f.source, effects));
        for (unsigned r = 0; r < 33; ++r)
            assert(sc::append(f.source, 3, effects));
        assert(sc::retire(f.source, 16, effects) && sc::retire(f.source, 17, effects));
        sc::author::dispatch(f.source, [&](auto a) {
            const auto raw = [&] {
                auto temporary = a;
                return bl::author::prepare_lower_bound(temporary, 51u);
            }();
            assert(raw.finish() == 16);
            const bl::selected_source selected{a, a.live_ranks()};
            const auto pending = bl::author::prepare_lower_bound(selected, 51u);
            assert(pending.finish() == 18);
        });
    }
}
int main() {
    unsigned cases = 0;
    for (auto family :
         {sc::geometry::absolute_ends, sc::geometry::sizes, sc::geometry::relative_ends})
        for (unsigned maximum : {1u, 15u, 255u, 4095u, 4096u, 65535u})
            for (unsigned n : {1u, 15u, 16u, 17u, 31u, 32u, 33u, 128u, 255u, 256u})
                cases += history(family, maximum, n);
    phase_lifetime();
    std::cout << cases
              << " independent boundary/resize cases and refusal/effect/lifetime checks passed\n";
}

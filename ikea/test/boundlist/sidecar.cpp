#include <ikea/boundlist.h>
#include <ikea/boundlist/author/native.h>
#include <ikea/boundlist/author/search.h>
#include <ikea/effects.h>
#include <array>
#include <cassert>
#include <vector>
#include <algorithm>

namespace sc = ikea::boundlist::sidecar;
namespace bl = ikea::boundlist;
struct fixture {
    sc::layout format;
    alignas(64) std::array<std::byte, 4096> directory{}, lengths{};
    std::array<std::byte, 6> control{};
    sc::view view;
    explicit fixture(sc::description d)
        : format(*sc::layout::prepare(d)),
          view(*sc::view::bind(format, {std::span(directory).first(format.directory_bytes()),
                                        std::span(lengths).first(format.lengths_bytes()),
                                        std::span(control).first(format.control_bytes())})) {}
};
struct model {
    unsigned reserved, used;
    bool dead = false;
};
struct audit {
    const sc::view* source;
    std::array<std::vector<std::byte>, 3> replay;
    unsigned writes = 0, limit = 4096;
    unsigned remaining() const {
        return limit - writes;
    }
    explicit audit(const sc::view& s) : source(&s) {
        reset();
    }
    void reset() {
        const auto p = source->windows().planes();
        for (unsigned i = 0; i < 3; ++i)
            replay[i] = {p[i].begin(), p[i].end()};
    }
    void before(const sc::view& s, ikea::byte_write w) {
        assert(&s == source && writes < limit && w.size);
        const auto p = s.windows().planes();
        assert(w.plane < 3 && w.offset <= p[w.plane].size() &&
               w.size <= p[w.plane].size() - w.offset);
        // Mark the exact issued window. After each command, unmarked bytes must
        // still match the snapshot, including complete readable tail groups.
        for (unsigned i = 0; i < w.size; ++i)
            replay[w.plane][w.offset + i] = std::byte{};
        ranges.push_back(w);
        ++writes;
    }
    std::vector<ikea::byte_write> ranges;
    void verify() {
        const auto p = source->windows().planes();
        for (auto w : ranges)
            std::copy_n(p[w.plane].data() + w.offset, w.size, replay[w.plane].data() + w.offset);
        for (unsigned i = 0; i < 3; ++i)
            assert(std::equal(replay[i].begin(), replay[i].end(), p[i].begin(), p[i].end()));
        ranges.clear();
        reset();
    }
};
void verify(fixture& f, const std::vector<model>& values) {
    assert(f.view.validate() && f.view.size() == values.size());
    unsigned offset = 0;
    for (unsigned r = 0; r < values.size(); ++r) {
        const auto& v = values[r];
        const auto loc = sc::locate(f.view, r);
        if (v.dead)
            assert(!loc && loc.error() == sc::error::retired);
        else
            assert(loc && loc->offset == offset && loc->length == v.used &&
                   loc->reservation == v.reserved);
        for (unsigned x = 0; x < v.reserved; ++x) {
            const auto owner = sc::reverse(f.view, offset + x);
            assert(owner && owner->rank == r && owner->tombstone == v.dead);
        }
        offset += v.reserved;
    }
    assert(f.view.extent() == offset && !sc::reverse(f.view, offset));
    sc::author::dispatch(f.view, [&](const auto& access) {
        for (unsigned q = 0; q < offset + 3; q += 13) {
            unsigned lower = values.size(), upper = values.size(), end = 0;
            for (unsigned r = 0; r < values.size(); ++r) {
                end += values[r].reserved;
                if (!values[r].dead) {
                    if (lower == values.size() && end >= q)
                        lower = r;
                    if (upper == values.size() && end > q)
                        upper = r;
                }
            }
            assert(bl::lower_bound(access, q) == lower && bl::upper_bound(access, q) == upper);
            const auto l = bl::author::prepare_lower_bound(access, q);
            const auto u = bl::author::prepare_upper_bound(access, q);
            assert(l.finish() == lower && u.finish() == upper);
        }
    });
    for (unsigned lo = 0; lo < offset + 3; lo += 11)
        for (unsigned span : {0u, 1u, 27u, 4096u})
            for (bool live : {false, true}) {
                unsigned first = values.size(), limit = values.size(), start = 0;
                for (unsigned r = 0; r < values.size(); ++r) {
                    const auto end = start + values[r].reserved;
                    if (span && lo < end && start < lo + span && start < end &&
                        (!live || !values[r].dead)) {
                        if (first == values.size())
                            first = r;
                        limit = r + 1;
                    }
                    start = end;
                }
                assert((sc::overlap(f.view, lo, lo + span, live) == bl::rank_window{first, limit}));
            }
}
void history(sc::description d) {
    fixture f(d);
    audit log(f.view);
    assert(sc::initialize(f.view, log));
    log.verify();
    std::vector<model> m;
    for (unsigned r = 0; r < 65; ++r) {
        const auto reserve = d.fixed_reservation.value_or(std::uint16_t(r % 7 ? r % 31 : 0));
        const unsigned used = d.full ? reserve : reserve / 2;
        assert(sc::append(f.view, used, reserve, log));
        log.verify();
        m.push_back({reserve, used});
        if (r == 14 || r == 15 || r == 16 || r == 64)
            verify(f, m);
    }
    if (!d.immortal)
        for (unsigned r = 0; r < 65; r += 3) {
            assert(sc::retire(f.view, r, log));
            m[r].dead = true;
            log.verify();
        }
    for (unsigned r = 1; r < 65; r += 7)
        if (!m[r].dead) {
            const auto used = d.full ? m[r].reserved : 0;
            assert(sc::replace(f.view, r, used, log));
            m[r].used = used;
            log.verify();
        }
    verify(f, m);
    // Refused commands preserve metadata and journal output.
    const auto count = log.writes;
    log.limit = count;
    assert(!sc::append(f.view, 0, d.fixed_reservation.value_or(0), log));
    assert(log.writes == count);
    log.verify();
    log.limit = 4096;
    if (!d.fixed_reservation || !*d.fixed_reservation) {
        assert(sc::soft_pack(f.view, true, log));
        log.verify();
        for (auto& v : m) {
            v.reserved = v.dead ? 0 : v.used;
            if (v.dead)
                v.used = 0;
        }
        verify(f, m);
    }
    fixture dst(d);
    audit output(dst.view);
    std::vector<std::uint16_t> selected;
    std::vector<model> ordered;
    for (int r = 64; r >= 0; --r)
        if (!m[r].dead) {
            selected.push_back(r);
            ordered.push_back(m[r]);
        }
    assert(sc::transfer(f.view, selected, dst.view, false, output));
    output.verify();
    verify(dst, ordered);
}
void sidecar_checks() {
    for (auto g : {sc::geometry::direct, sc::geometry::width})
        for (auto p : {sc::placement::grouped, sc::placement::separated})
            for (bool full : {false, true})
                for (bool immortal : {false, true})
                    for (auto fixed :
                         {std::optional<std::uint16_t>{}, std::optional<std::uint16_t>{0},
                          std::optional<std::uint16_t>{8}})
                        history({g, p, 255, 65280, fixed, full, immortal});
    // Frozen schema bytes and a hand-authored Direct interpretation.
    fixture f({sc::geometry::direct, sc::placement::grouped, 255, 65280, {}, false, false});
    const std::array<std::byte, 8> wire{std::byte{0x40}, std::byte{0xff}, std::byte{0},
                                        std::byte{0},    std::byte{0xff}, std::byte{0},
                                        std::byte{0},    std::byte{0}};
    assert(f.format.encode() == wire && sc::layout::decode(wire)->encode() == wire);
    const auto at = f.format.geometry_offset();
    f.directory[at] = std::byte{3};
    f.directory[at + 2] = std::byte{3};
    f.directory[at + 4] = std::byte{9};
    f.control[0] = std::byte{3};
    f.lengths[0] = std::byte{2};
    f.lengths[2] = std::byte{4};
    assert(f.view.validate());
    assert(sc::reverse(f.view, 3)->rank == 2 && bl::lower_bound(f.view, 3u) == 0 &&
           bl::upper_bound(f.view, 3u) == 2);
    f.lengths[15] = std::byte{1};
    assert(!f.view.validate());
}

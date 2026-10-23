#include <algorithm>
#include <array>
#include <cassert>
#include <ikea/streampack.h>
#include <ikea/streampack/author/native.h>
#include <iostream>
#include <numeric>
#include <type_traits>
#include <vector>

namespace sp = ikea::streampack;
using sp::byte;
static_assert(!std::is_same_v<sp::error, ikea::intervals::error>);
struct fixture {
    sp::layout layout;
    alignas(64) std::array<byte, 4096> directory, lengths;
    std::array<byte, 6> control;
    std::vector<byte> payload;
    sp::view view;
    explicit fixture(sp::layout::options d)
        : layout(*sp::layout::make(d)), payload(d.byte_limit),
          view(*sp::view::attach(layout,
                               {payload, std::span(directory).first(layout.directory_bytes()),
                                std::span(lengths).first(layout.lengths_bytes()),
                                std::span(control).first(layout.control_bytes())})) {
        directory.fill(byte{0xa5});
        lengths.fill(byte{0xa5});
        control.fill(byte{0xa5});
    }
};
struct slot {
    std::vector<byte> value;
    unsigned reserved;
    bool dead = false;
};
struct snapshot {
    std::array<std::vector<byte>, 4> planes;
    explicit snapshot(const sp::view &v) {
        const auto p = v.windows().planes();
        for (unsigned i = 0; i < 4; ++i)
            planes[i] = {p[i].begin(), p[i].end()};
    }
    void coverage(const sp::view &v, const ikea::journal &log, unsigned first = 0) const {
        const auto p = v.windows().planes();
        auto replay = planes;
        for (unsigned i = first; i < log.used; ++i) {
            const auto &e = log.entries()[i];
            assert(e.source == &v);
            const auto w = e;
            assert(w.plane < p.size() && w.offset <= p[w.plane].size() &&
                   w.size <= p[w.plane].size() - w.offset);
            std::copy_n(p[w.plane].data() + w.offset, w.size, replay[w.plane].data() + w.offset);
        }
        for (unsigned i = 0; i < 4; ++i)
            assert(std::equal(p[i].begin(), p[i].end(), replay[i].begin(), replay[i].end()));
    }
    void unchanged(const sp::view &v) const {
        const auto p = v.windows().planes();
        for (unsigned i = 0; i < 4; ++i)
            assert(std::equal(p[i].begin(), p[i].end(), planes[i].begin(), planes[i].end()));
    }
};
void verify(const sp::view &v, const std::vector<slot> &model) {
    assert(v.validate());
    assert(v.size() == model.size());
    unsigned offset = 0;
    for (unsigned r = 0; r < model.size(); ++r) {
        const auto &s = model[r];
        const auto value = sp::value(v, r);
        if (s.dead)
            assert(!value && value.error() == sp::error::retired);
        else {
            assert(value && value->size() == s.value.size());
            assert(std::equal(value->begin(), value->end(), s.value.begin()));
            assert(*sp::length(v, r) == s.value.size());
        }
        for (unsigned at = 0; at < s.reserved; ++at) {
            const auto owner = sp::reverse(v, offset + at);
            assert(owner && owner->rank == r && owner->tombstone == s.dead);
        }
        offset += s.reserved;
    }
    assert(v.extent() == offset && !sp::reverse(v, offset));
#if defined(__aarch64__) || defined(__AVX2__)
    sp::author::visit(v, [&](auto a) {
        for (unsigned g = 0; g < (v.size() + 15) / 16; ++g) {
            const auto group = sp::native::read16(a, g);
            std::array<unsigned, 16> starts, lengths;
            sp::native::each_chunk([&](auto i) {
                sp::native::store(starts.data() + i * sp::native::lanes, group.values.offsets(i));
                sp::native::store(lengths.data() + i * sp::native::lanes, group.values.lengths(i));
            });
            unsigned expected = 0;
            for (unsigned r = 0; r < g * 16; ++r)
                expected += model[r].reserved;
            for (unsigned i = 0; i < std::min(16u, v.size() - g * 16); ++i) {
                const auto &s = model[g * 16 + i];
                assert(starts[i] == expected && lengths[i] == s.value.size());
                assert(bool(group.live & (1u << i)) == !s.dead);
                expected += s.reserved;
            }
            assert(!(group.live >> std::min(16u, v.size() - g * 16)));
        }
    });
#endif
    assert(!sp::value(v, model.size()));
    unsigned visited = 0;
    assert(sp::range(v, 0, v.size(), [&](unsigned r, sp::bytes value) {
        assert(!model[r].dead && std::equal(value.begin(), value.end(), model[r].value.begin(),
                                            model[r].value.end()));
        ++visited;
    }));
    assert(visited == std::count_if(model.begin(), model.end(), [](auto &s) { return !s.dead; }));
    std::vector<std::uint16_t> ranks;
    for (unsigned i = 0; i < model.size(); ++i)
        ranks.push_back((i * 19) % model.size());
    std::array<std::uint32_t, sp::slot_capacity> scratch;
    std::vector<unsigned> sparse, dense;
    auto consumer = [&](std::vector<unsigned> &out) {
        return [&](unsigned r, sp::bytes b) {
            assert(std::equal(b.begin(), b.end(), model[r].value.begin(), model[r].value.end()));
            out.push_back(r);
        };
    };
    assert(sp::select(v, ranks, consumer(sparse)));
    assert(sp::select(v, ranks, scratch, consumer(dense)));
    assert(sparse == dense);
}
void history(sp::layout::options d) {
    fixture f(d);
    std::array<byte, sp::layout::encoded_size()> encoded;
    assert(f.layout.encode(encoded));
    const auto decoded = sp::layout::decode(encoded);
    assert(decoded && decoded->schema() == f.layout.schema());
    std::vector<ikea::journal::entry> log_records(8192);
    ikea::journal log{log_records};
    snapshot old(f.view);
    assert(sp::initialize(f.view, log));
    old.coverage(f.view, log);
    std::vector<slot> model;
    for (unsigned rank = 0; rank < sp::slot_capacity; ++rank) {
        const unsigned reserve = d.fixed_reservation.value_or(
            std::uint16_t(std::min(unsigned(d.maximum), (rank * 31) % 57)));
        slot s{std::vector<byte>(d.full ? reserve : reserve / 2), reserve};
        for (unsigned i = 0; i < s.value.size(); ++i)
            s.value[i] = byte((rank + i) % 256);
        // A journal may merge this command into its existing last entry. Use a
        // fresh command log so earlier coverage cannot hide an omitted hook.
        log.used = 0;
        snapshot before(f.view);
        assert(sp::append(f.view, s.value, reserve, log));
        before.coverage(f.view, log);
        model.push_back(s);
        if (rank == 0 || rank == 14 || rank == 15 || rank == 16 || rank + 1 == sp::slot_capacity)
            verify(f.view, model);
    }
    snapshot full(f.view);
    const auto effects = log.used;
    assert(!sp::append(f.view, {}, 0, log));
    full.unchanged(f.view);
    assert(effects == log.used);
    for (unsigned r = 0; r < model.size(); r += 7) {
        auto value = model[r].value;
        std::reverse(value.begin(), value.end());
        if (!d.full && value.size())
            value.pop_back();
        log.used = 0;
        snapshot before(f.view);
        assert(sp::replace(f.view, r, value, log));
        before.coverage(f.view, log);
        model[r].value = value;
    }
    if (!d.immortal)
        for (unsigned r = 0; r < model.size(); r += 3) {
            log.used = 0;
            snapshot before(f.view);
            assert(sp::retire(f.view, r, log));
            before.coverage(f.view, log);
            model[r].dead = true;
        }
    verify(f.view, model);
    if (!d.fixed_reservation || *d.fixed_reservation == 0) {
        log.used = 0;
        snapshot before(f.view);
        assert(sp::soft_pack(f.view, true, log));
        before.coverage(f.view, log);
        for (auto &s : model) {
            s.reserved = s.dead ? 0 : s.value.size();
            if (s.dead)
                s.value.clear();
        }
        verify(f.view, model);
    }
    fixture dest(d);
    std::vector<std::uint16_t> ranks;
    std::vector<slot> transferred;
    for (unsigned r = model.size(); r-- > 0;)
        if (!model[r].dead) {
            ranks.push_back(r);
            transferred.push_back(model[r]);
        }
    std::vector<ikea::journal::entry> moved_records(8192);
    ikea::journal moved{moved_records};
    snapshot before(dest.view);
    assert(sp::transfer(f.view, ranks, dest.view, false, moved));
    before.coverage(dest.view, moved);
    verify(dest.view, transferred);
    const snapshot before_oversized(dest.view);
    const auto before_effects = moved.used;
    const std::array<std::uint16_t, sp::slot_capacity + 1> too_many{};
    const auto oversized = sp::transfer(f.view, too_many, dest.view, false, moved);
    assert(!oversized && oversized.error() == sp::error::capacity);
    before_oversized.unchanged(dest.view);
    assert(moved.used == before_effects);
    ikea::journal none{};
    snapshot refused(dest.view);
    assert(!sp::initialize(dest.view, none));
    refused.unchanged(dest.view);
    assert(!none.used);
}
void coalesced_commands() {
    fixture f({sp::geometry::absolute_ends, sp::placement::grouped, 0, 0, 0, true, true});
    std::array<ikea::journal::entry, 4> records;
    ikea::journal log{records};
    unsigned other_owner = 0;
    log.before(other_owner, 0, 0, sizeof(other_owner));
    const auto prefix = records[0];
    snapshot before(f.view);
    assert(sp::initialize(f.view, log));
    const auto count = log.used;
    const auto control = records[count - 1];
    assert(count == 2 && control.source == &f.view && control.plane == 3 &&
           control.offset == 0 && control.size == f.layout.control_bytes());
    for (unsigned i = 0; i < 2; ++i) {
        assert(sp::append(f.view, {}, 0, log));
        assert(log.used == count && f.view.size() == i + 1);
        assert(records[1].source == control.source && records[1].plane == control.plane &&
               records[1].offset == control.offset && records[1].size == control.size);
    }
    assert(records[0].source == prefix.source && records[0].plane == prefix.plane &&
           records[0].offset == prefix.offset && records[0].size == prefix.size);
    // Replay the combined issued coverage from before all three commands. The
    // two appends legitimately reuse the initialization's last control record.
    before.coverage(f.view, log, 1);
    assert(f.view.validate());
}
int main() {
    coalesced_commands();
    for (auto family :
         {sp::geometry::absolute_ends, sp::geometry::sizes, sp::geometry::relative_ends})
        for (auto placement : {sp::placement::grouped, sp::placement::separated})
            for (auto maximum : {0u, 1u, 255u, 256u, 4095u, 65535u}) {
                sp::layout::options d{family,
                                  placement,
                                  std::uint16_t(maximum),
                                  std::min(sp::slot_capacity * maximum, 65536u),
                                  {},
                                  false,
                                  false};
                history(d);
                d.full = true;
                d.immortal = true;
                history(d);
            }
    for (auto family :
         {sp::geometry::absolute_ends, sp::geometry::sizes, sp::geometry::relative_ends}) {
        history({family, sp::placement::grouped, 255, sp::slot_capacity * 255, {}, true, false});
        history({family, sp::placement::grouped, 255, sp::slot_capacity * 255, {}, false, true});
        history({family, sp::placement::grouped, 255, sp::slot_capacity * 255, 0, false, false});
        history({family, sp::placement::grouped, 255, sp::slot_capacity * 255, 7, false, false});
    }
    std::cout << "StreamPack histories, reverse byte owners, transfer, effects and omitted facts "
                 "passed\n";
}

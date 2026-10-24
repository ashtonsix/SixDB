// Prototype 6: basic database operations end to end through Orbital.
//
// A column of four parts is four Orbital Objects published in one packet;
// each Representation holds one Part per group image. Readers attach a stage
// tree to whatever version their Scope selects. An UPDATE copies its group's
// image into a draft, classifies and applies the write there (or re-encodes
// the group), and publishes a Representation that reuses the other groups'
// Parts. Checks: every version answers SUM, COUNT, points and FILTER exactly
// while older Scopes stay live; untouched Parts are reused by address; the
// SUM contributions add up. Timings: publish, attach, reads, and the parts
// of a write.
//
// usage: frame3_e2e COLS_DIR COLUMN
#include "change.h"
#include "orbital_frame.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <random>

using namespace frame3;
namespace fo = frame3_orbital;

namespace {
struct Memory final : std::pmr::memory_resource {
    void *do_allocate(std::size_t bytes, std::size_t alignment) override { return std::pmr::new_delete_resource()->allocate(bytes, alignment); }
    void do_deallocate(void *p, std::size_t bytes, std::size_t alignment) override { std::pmr::new_delete_resource()->deallocate(p, bytes, alignment); }
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
int failures = 0;
void expect(bool ok, const char *what) {
    if (!ok && failures++ < 20)
        std::fprintf(stderr, "FAIL %s\n", what);
}
std::unique_ptr<stage> tree() { return std::make_unique<bound>(std::make_unique<pack<sp::preset::compact>>(group_blocks), 0, 64, 8); }

class table {
    Memory memory_;
    std::deque<fo::Geometry> geometries_;
    std::vector<std::size_t> rows_;

  public:
    fo::Store store{memory_};
    std::size_t published_bytes = 0, reused_parts = 0, fresh_parts = 0;
    explicit table(std::vector<std::size_t> rows) : rows_(std::move(rows)) {}
    unsigned parts() const { return static_cast<unsigned>(rows_.size()); }
    std::size_t rows(unsigned p) const { return rows_[p]; }
    static orbital::ObjectId object(unsigned p) { return {0xF3A3E, p}; }

    // Capture part p from group images (fresh backing) and retained sources.
    fo::Store::Owner capture(unsigned p, std::vector<orbital::PartSource> sources, std::vector<std::size_t> extents) {
        fo::Geometry &g = geometries_.emplace_back(fo::Geometry{&memory_, object(p).low + 1, std::move(extents)});
        for (std::size_t i = 0; i < sources.size(); ++i)
            sources[i].id = fo::Control{&g}.id(i);
        auto owner = store.capture(fo::Control{&g}, sources);
        if (!owner)
            std::abort();
        return std::move(*owner);
    }
    static orbital::PartSource fresh(std::vector<std::byte> image) {
        auto backing = std::make_shared<const std::vector<std::byte>>(std::move(image));
        return {{}, std::span<const std::byte>(*backing), backing, nullptr};
    }
    void publish(std::span<const fo::Store::Change> changes) {
        fo::Store::Scope basis(store);
        fo::Store::Prepared packet(store);
        if (!store.prepare(basis, changes, packet) || !store.install(packet))
            std::abort();
    }
    // A read-only column over the version a Scope selects, borrowing its Parts.
    column attach(const fo::Store::Scope &scope) const {
        std::vector<std::vector<std::span<const std::byte>>> images(parts());
        for (unsigned p = 0; p < parts(); ++p) {
            const fo::Selection sel = scope.bind(object(p));
            for (std::size_t g = 0; g < sel.part_count(); ++g)
                images[p].push_back({sel.part(g)->address(), *sel.control().extent(g)});
        }
        return column(tree(), rows_, images);
    }
    // UPDATE one row: draft its group, apply or re-encode, publish the part.
    contribution update(std::size_t row, u64 value, double &apply_ns, double &publish_ns) {
        const unsigned p = static_cast<unsigned>(row / part_rows), r = static_cast<unsigned>(row % part_rows), g = r / block_rows / group_blocks;
        fo::Store::Scope basis(store);
        const fo::Selection sel = basis.bind(object(p));
        std::vector<std::vector<std::span<const std::byte>>> images(1);
        std::vector<std::vector<std::span<std::byte>>> writable(1);
        std::vector<std::byte> draft;
        for (std::size_t i = 0; i < sel.part_count(); ++i) {
            const std::span<const std::byte> bytes{sel.part(i)->address(), *sel.control().extent(i)};
            if (i == g) {
                draft.assign(bytes.begin(), bytes.end());
                images[0].push_back(draft), writable[0].push_back(draft);
            } else
                images[0].push_back(bytes), writable[0].push_back({});
        }
        contribution out;
        std::vector<std::byte> image;
        apply_ns += time_ns([&] {
            mutable_column m(column(tree(), std::span(&rows_[p], 1), images, &writable), false);
            out = m.set(r, value);
            image = m.draft().image(0, g);
        });
        publish_ns += time_ns([&] {
            std::vector<orbital::PartSource> sources;
            std::vector<std::size_t> extents;
            for (std::size_t i = 0; i < sel.part_count(); ++i)
                if (i == g)
                    extents.push_back(image.size()), published_bytes += image.size(), ++fresh_parts, sources.push_back(fresh(std::move(image)));
                else
                    extents.push_back(*sel.control().extent(i)), ++reused_parts, sources.push_back(*fo::reuse(sel, i));
            const std::array changes{fo::Store::Change{object(p), capture(p, std::move(sources), std::move(extents))}};
            publish(changes);
        });
        return out;
    }
};

struct answers {
    u128 sum;
    u64 count;
    u64 points;
    std::vector<u64> filter;
};
answers oracle(const std::vector<u64> &v, u64 t, const std::vector<u32> &rows) {
    answers a{0, 0, 0, std::vector<u64>((v.size() + 63) / 64)};
    for (std::size_t i = 0; i < v.size(); ++i)
        a.sum += v[i], a.count += v[i] >= t, a.filter[i / 64] |= u64{v[i] >= t} << (i % 64);
    for (u32 r : rows)
        a.points += v[r];
    return a;
}
answers ask(const column &c, u64 t, const std::vector<u32> &rows) {
    answers a{c.sum(), c.count(comparison::greater_equal, t), 0, std::vector<u64>((c.size() + 63) / 64)};
    for (u32 r : rows)
        a.points += c.get(r);
    c.filter(comparison::greater_equal, t, a.filter.data());
    return a;
}
bool same(const answers &a, const answers &b) { return a.sum == b.sum && a.count == b.count && a.points == b.points && a.filter == b.filter; }
}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s COLS_DIR COLUMN\n", argv[0]);
        return 2;
    }
    const std::string name = argv[2];
    std::vector<u64> v(4 * part_rows);
    {
        std::ifstream f(std::string(argv[1]) + "/" + name + ".u64", std::ios::binary);
        if (!f)
            std::abort();
        f.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(v.size() * 8));
    }
    std::vector<u64> sorted = v;
    std::nth_element(sorted.begin(), sorted.begin() + v.size() / 2, sorted.end());
    const u64 t = sorted[v.size() / 2];
    std::mt19937_64 rng(41);
    std::vector<u32> rows(4096);
    for (auto &r : rows)
        r = static_cast<u32>(rng() % v.size());

    // Load: build each part, publish all four Objects in one packet.
    table tab({part_rows, part_rows, part_rows, part_rows});
    double load_ns = time_ns([&] {
        column built(tree(), v);
        std::vector<fo::Store::Change> changes;
        for (unsigned p = 0; p < tab.parts(); ++p) {
            std::vector<orbital::PartSource> sources;
            std::vector<std::size_t> extents;
            for (unsigned g = 0; g < built.root().groups_in(p); ++g) {
                auto image = built.image(p, g);
                extents.push_back(image.size()), tab.published_bytes += image.size();
                sources.push_back(table::fresh(std::move(image)));
            }
            changes.push_back({table::object(p), tab.capture(p, std::move(sources), std::move(extents))});
        }
        tab.publish(changes);
    });
    const std::size_t loaded_bytes = tab.published_bytes;
    tab.published_bytes = 0;

    fo::Store::Scope first(tab.store);
    const answers want1 = oracle(v, t, rows);
    column c1 = tab.attach(first);
    expect(same(ask(c1, t, rows), want1), "version 1 reads");
    const column built(tree(), v);
    double attach_ns = time_ns([&] { column again = tab.attach(first); });
    double read_attached = time_ns([&] { (void)c1.sum(); (void)c1.count(comparison::greater_equal, t); });
    double read_built = time_ns([&] { (void)built.sum(); (void)built.count(comparison::greater_equal, t); });

    // Updates: in-window values, outliers spread over groups, then a burst of
    // outliers into one group until its slack runs out and it re-encodes.
    const auto [mn, mx] = std::minmax_element(v.begin(), v.end());
    std::vector<std::pair<std::size_t, u64>> writes;
    for (int i = 0; i < 64; ++i)
        writes.push_back({rng() % v.size(), v[rng() % v.size()]});
    for (int i = 0; i < 64; ++i)
        writes.push_back({rng() % v.size(), *mx + 1 + rng() % 1000});
    for (int i = 0; i < 40; ++i)
        writes.push_back({part_rows + 3 * group_blocks * block_rows + rng() % (group_blocks * block_rows), *mx + 7 + rng() % 1000});
    __int128 sum = static_cast<__int128>(want1.sum);
    double apply_ns = 0, publish_ns = 0;
    std::size_t reencoded = 0;
    std::unique_ptr<fo::Store::Scope> middle;
    answers want_middle;
    for (std::size_t i = 0; i < writes.size(); ++i) {
        const auto [row, value] = writes[i];
        const contribution out = tab.update(row, value, apply_ns, publish_ns);
        sum += out.sum, reencoded += out.reencoded;
        v[row] = value;
        if (i == writes.size() / 2) {
            middle = std::make_unique<fo::Store::Scope>(tab.store);
            want_middle = oracle(v, t, rows);
        }
    }
    fo::Store::Scope last(tab.store);
    const answers want_last = oracle(v, t, rows);
    expect(same(ask(tab.attach(last), t, rows), want_last), "last version reads");
    expect(same(ask(tab.attach(*middle), t, rows), want_middle), "middle Scope reads its version");
    expect(same(ask(tab.attach(first), t, rows), want1), "first Scope still reads version 1");
    expect(same(ask(c1, t, rows), want1), "column attached at version 1 still reads it");
    expect(static_cast<u128>(sum) == want_last.sum, "contributions add up");
    expect(tab.fresh_parts == writes.size() && tab.reused_parts == writes.size() * (built.root().groups_in(0) - 1), "one fresh Part per write");

    std::printf("{\"column\":\"%s\",\"rows\":%zu,\"parts\":%u,\"groups_per_part\":%u,\"load_ms\":%.2f,\"loaded_bytes\":%zu,\"attach_us\":%.1f,"
                "\"sum_count_attached_us\":%.1f,\"sum_count_built_us\":%.1f,\"writes\":%zu,\"reencoded\":%zu,\"apply_us_per_write\":%.1f,"
                "\"publish_us_per_write\":%.1f,\"bytes_per_write\":%.0f,\"reused_parts\":%zu,\"fresh_parts\":%zu,\"cut\":%llu,\"ok\":%d}\n",
                name.c_str(), v.size(), tab.parts(), built.root().groups_in(0), load_ns / 1e6, loaded_bytes, attach_ns / 1e3, read_attached / 1e3,
                read_built / 1e3, writes.size(), reencoded, apply_ns / 1e3 / static_cast<double>(writes.size()),
                publish_ns / 1e3 / static_cast<double>(writes.size()), static_cast<double>(tab.published_bytes) / static_cast<double>(writes.size()),
                tab.reused_parts, tab.fresh_parts, static_cast<unsigned long long>(last.cut()), failures == 0);
    return failures ? 1 : 0;
}

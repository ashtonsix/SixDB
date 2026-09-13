#include "packet_wire.h"
#include "grouped_maintenance.h"
#include <cstdio>
#include <cstdlib>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/execution.h>
#include <ikea/tuplepack/author/maintenance.h>
#include <random>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace tp = ikea::tuplepack;
void require(bool valid, unsigned rows, unsigned trial, const char *scenario) {
    if (!valid) {
        std::fprintf(stderr, "TuplePack packets: rows=%u trial=%u %s\n", rows, trial, scenario);
        std::abort();
    }
}
// A late bad packet must not leave earlier packets or observations committed.
struct row_observer {
    unsigned before_count = 0, after_count = 0;
    bool covers(std::size_t first, std::size_t count) const {
        return first <= 65 && count <= 65 - first;
    }
    unsigned before(std::size_t) {
        ++before_count;
        return 0;
    }
    void after(std::size_t, unsigned) { ++after_count; }
};
void range_admission() {
    const std::array<tp::code, 1> codes{{{0, 2, 3}}};
    const std::array<tp::byte, 1> map{0};
    const auto f = *tp::layout::make(1, codes);
    const auto wp = *tp::writer<64, 32>::make(f, map);
    std::array<tp::byte, 65> bytes;
    bytes.fill(0xc3);
    const auto saved = bytes;
    auto view = *tp::view::bind(f, bytes, 65, 1);
    auto write = *tp::bind_writer(wp, view);
    std::array<tp::packet<64>, 3> input{};
    for (auto &packet : input)
        for (unsigned r = 0; r < 32; ++r)
            packet[r] = 5;
    input[2][0] = 8;
    std::array<ikea::owner_write, 65> storage;
    ikea::source_write_journal effects{storage};
    row_observer observer;
    const auto selected = tp::selection::all();
    require(!write.replace(0, input, effects, selected, observer), 32, 0, "late width rejection");
    require(bytes == saved && effects.used == 0 && observer.before_count == 0, 32, 0,
            "late error leaves whole range unchanged");
    input[2][0] = 5;
    ikea::source_write_journal short_effects{std::span(storage).first(64)};
    require(!write.replace(0, input, short_effects, selected, observer), 32, 0,
            "late capacity rejection");
    require(bytes == saved && short_effects.used == 0 && observer.before_count == 0, 32, 0,
            "late capacity leaves whole range unchanged");
    tp::erased_mutation<tp::packet<64>, row_observer, 32> erased(write);
    require(erased.size() == 65 && bool(erased.replace(0, input, effects, selected, observer)), 32,
            0, "erased packet traversal and tail");
    require(observer.before_count == 65 && observer.after_count == 65, 32, 0,
            "point observer adapts to packet range");
    for (auto b : bytes)
        require(b == 0xd7, 32, 0, "range independent bits");
}
void guarded_tails() {
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto allocation = static_cast<tp::byte *>(
        mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    require(allocation != MAP_FAILED, 64, 0, "guard allocation");
    require(mprotect(allocation + page, page, PROT_NONE) == 0, 64, 0, "guard protection");
    const std::array<tp::code, 1> codes{{{0, 1, 3}}};
    const std::array<tp::byte, 1> map{0};
    const auto f = *tp::layout::make(1, codes);
    const auto rp = *tp::reader<64, 64>::make(f, map);
    const auto wp = *tp::writer<64, 64>::make(f, map);
    for (unsigned count : {3u, 64u}) {
        auto data = std::span(allocation + page - count, count);
        std::fill(data.begin(), data.end(), 0xa5);
        auto view = *tp::view::bind(f, data, count, 1);
        const auto read = *tp::bind_reader(rp, view);
        const auto write = *tp::bind_writer(wp, view);
        const auto all = ~0ull >> (64 - count);
        for (auto active : {all, all & 0xaaaaaaaaaaaaaaaaull}) {
            auto value = *read.get(0, active);
            std::array<ikea::owner_write, 64> slots;
            ikea::source_write_journal effects{slots};
            require(bool(write.set(0, value, effects, active)), 64, count,
                    "guarded buffered masked update");
#if defined(__aarch64__) || defined(__AVX2__)
            const auto nr = tp::native_reader(read);
            const auto nw = tp::native_writer(write);
            effects.used = 0;
            require(bool(nw.set(0, nr.get_unchecked(0, active), effects, active)), 64, count,
                    "guarded native masked update");
#endif
            for (auto b : data)
                require(b == 0xa5, 64, count, "guarded preserved bytes");
        }
    }
    require(munmap(allocation, 2 * page) == 0, 64, 0, "guard release");
}
#if defined(__aarch64__) || defined(__AVX2__)
struct batch_law {
    static constexpr bool needs_before = true, needs_after = true;
    unsigned calls = 0;
    std::uint64_t seen = 0;
    std::array<tp::byte, 64> old_a{}, old_bc{}, new_a{}, new_bc{};
    template <class Before, class After>
    void observe_batch(std::size_t first, std::uint64_t active, const Before &before,
                       const After &after) {
        require(first == 1, 64, 0, "maintenance original first");
        ++calls;
        seen = active;
        tp::native::store_packet(old_a.data(), std::get<0>(before));
        tp::native::store_packet(old_bc.data(), std::get<1>(before));
        tp::native::store_packet(new_a.data(), std::get<0>(after));
        tp::native::store_packet(new_bc.data(), std::get<1>(after));
    }
};
void composition() {
    constexpr unsigned Rows = 64;
    std::array<tp::code, 1> a_codes{{{2, 3, 1}}}, b_codes{{{0, 1, 1}}};
    std::array<tp::code, 1> bc_codes{{{0, 1, 2}}};
    const auto a_format = *tp::layout::make(3, a_codes), b_format = *tp::layout::make(1, b_codes);
    const auto bc_format = *tp::layout::make(1, bc_codes);
    const std::array<tp::byte, 1> map{0};
    auto ar = *tp::reader<64, Rows>::make(a_format, map),
         br = *tp::reader<64, Rows>::make(bc_format, map);
    auto aw = *tp::writer<64, Rows>::make(a_format, map),
         bw = *tp::writer<64, Rows>::make(b_format, map);
    std::array<tp::byte, 3 * 65> a;
    a.fill(0xa5);
    std::array<tp::byte, 65> b;
    b.fill(0xf4);
    auto av = *tp::view::bind(a_format, a, 65, 3), bv = *tp::view::bind(b_format, b, 65, 1);
    auto bcv = *tp::const_view::bind(bc_format, b, 65, 1);
    auto ao = *tp::bind_writer(aw, av), bo = *tp::bind_writer(bw, bv);
    const auto child = *tp::composition::bind_group(tp::native_writer(ao));
    const auto parent = *tp::composition::bind_group(child, tp::native_writer(bo));
    auto a_read = *tp::bind_reader(ar, av);
    auto bc_read = *tp::bind_reader(br, bcv);
    const auto projection =
        tp::composition::projection(tp::native_reader(a_read), tp::native_reader(bc_read));
    batch_law law;
    auto observation = tp::observation(projection, law);
    std::array<tp::byte, 64> input;
    input.fill(1);
    const auto values = tp::native::load_packet(input.data());
    std::array<ikea::owner_write, 128> records{};
    ikea::source_write_journal effects{records};
    const std::uint64_t active = 0x8000000000000005ull;
    require(bool(parent.set(1, {{values}, values}, effects, active, observation)), Rows, 0,
            "nested native window");
    require(law.calls == 1 && law.seen == active, Rows, 0, "one complete batch observation");
    for (unsigned r = 0; r < Rows; ++r) {
        const bool chosen = active & (std::uint64_t(1) << r);
        require(law.old_a[r] == 0 && law.new_a[r] == chosen, Rows, r, "substituted A observation");
        require(law.old_bc[r] == (chosen ? 2 : 0) && law.new_bc[r] == (chosen ? 3 : 0), Rows, r,
                "complete B plus untouched C");
        require(a[(r + 1) * 3 + 2] == (chosen ? 0xad : 0xa5) && b[r + 1] == (chosen ? 0xf6 : 0xf4),
                Rows, r, "substituted source preservation");
    }
    bool seen_a = false, seen_b = false;
    for (unsigned i = 0; i < effects.used; ++i) {
        seen_a |= records[i].source == &av;
        seen_b |= records[i].source == &bv;
        require(records[i].source == &av || records[i].source == &bv, Rows, i,
                "actual owner identity");
    }
    require(seen_a && seen_b, Rows, 0, "both substituted effects");
}
#endif
int main() {
    grouped_maintenance<64>();
    std::mt19937 random(89123);
    wire<64, 1>(random);
    wire<64, 2>(random);
    wire<64, 4>(random);
    wire<64, 8>(random);
    wire<64, 16>(random);
    wire<64, 32>(random);
    wire<64, 64>(random);
    range_admission();
    guarded_tails();
#if defined(__aarch64__) || defined(__AVX2__)
    composition();
#endif
    std::puts("TuplePack packet operations: seven shapes, buffered/native wire, admission, sparse "
              "tails, effects and nested native maintenance passed");
}

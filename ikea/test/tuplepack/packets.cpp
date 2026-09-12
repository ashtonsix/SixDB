#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/maintenance.h>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

namespace tp = ikea::tuplepack;
void require(bool valid, unsigned rows, unsigned trial, const char* scenario) {
    if (!valid) {
        std::fprintf(stderr, "TuplePack packets: rows=%u trial=%u %s\n", rows, trial, scenario);
        std::abort();
    }
}
template <unsigned Rows> void wire(std::mt19937& random) {
    constexpr unsigned B = 64 / Rows;
    constexpr auto all = tp::detail::all_rows<Rows>;
    for (unsigned trial = 0; trial < 120; ++trial) {
        const unsigned extent = trial % 4 ? 1 + random() % 64 : 1;
        const unsigned stride = trial % 2 ? extent + 11 : extent;
        const std::size_t count = Rows * 2 + 1, offset = 3;
        std::vector<tp::code> codes;
        for (unsigned i = 0; i < extent; ++i) {
            const unsigned width = 1 + random() % 8;
            codes.push_back({tp::byte(i), 0, tp::byte(width)});
            if (width < 8)
                codes.push_back({tp::byte(i), tp::byte(width), tp::byte(8 - width)});
        }
        const auto format = *tp::layout::make(extent, codes);
        std::vector<unsigned> ranks(codes.size());
        for (unsigned i = 0; i < ranks.size(); ++i)
            ranks[i] = i;
        std::shuffle(ranks.begin(), ranks.end(), random);
        std::array<tp::byte, B> map;
        map.fill(tp::hole);
        for (unsigned i = 0; i < std::min(B, unsigned(ranks.size())); ++i)
            if (trial % 3 || i % 3)
                map[i] = ranks[i];
        auto rp = *tp::reader<64, Rows>::make(format, map);
        auto wp = *tp::writer<64, Rows>::make(format, map);
        std::vector<tp::byte> data(offset + (count - 1) * stride + extent);
        for (auto& b : data)
            b = random();
        auto view = *tp::view::bind(format, data, count, stride, offset);
        auto read = *tp::bind_reader(rp, view);
        auto write = *tp::bind_writer(wp, view);
        const std::uint64_t active =
            trial % 3 ? ((std::uint64_t(random()) << 32) | random()) & all : all;
        const std::size_t first = 1;
        std::array<tp::byte, 64> expected{}, values{};
        auto wanted = data;
        for (unsigned r = 0; r < Rows; ++r)
            for (unsigned i = 0; i < B; ++i) {
                values[r * B + i] = random();
                if (map[i] == tp::hole)
                    continue;
                const auto c = codes[map[i]];
                if (!(active & (std::uint64_t(1) << r)))
                    continue;
                const auto pos = offset + (first + r) * stride + c.offset;
                values[r * B + i] &= (1u << c.width) - 1;
                for (unsigned bit = 0; bit < c.width; ++bit) {
                    expected[r * B + i] |= ((data[pos] >> (c.shift + bit)) & 1) << bit;
                    wanted[pos] = tp::byte((wanted[pos] & ~(1u << (c.shift + bit))) |
                                           (((values[r * B + i] >> bit) & 1) << (c.shift + bit)));
                }
            }
        require(read.get(first, active) == expected, Rows, trial, "buffered read/reference");
        require(!read.get(count, all), Rows, trial, "active tail rejection");
        require(read.get(count, 0) == std::array<tp::byte, 64>{}, Rows, trial, "empty end window");
        if constexpr (Rows < 64)
            require(!read.get(0, all + 1), Rows, trial, "high active bit rejected");
#if defined(__aarch64__) || defined(__AVX2__)
        const auto native_read = tp::native_reader(read);
        std::array<tp::byte, 64> actual{};
        tp::native::store_packet(actual.data(), native_read.get_unchecked(first, active));
        require(actual == expected, Rows, trial, "native read/reference");
        tp::native::store_packet(actual.data(), tp::native::row_mask<Rows>(active));
        for (unsigned i = 0; i < 64; ++i)
            require(actual[i] == ((active & (std::uint64_t(1) << (i / B))) ? 255 : 0), Rows, trial,
                    "native row mask");
#endif
        std::array<ikea::owner_write, 4096> entries{};
        ikea::source_write_journal effects{entries};
        auto saved = data;
        if (active && wp.effect_capacity()) {
            std::array<ikea::owner_write, 0> none;
            ikea::source_write_journal too_small{none};
            require(!write.set(first, values, too_small, active), Rows, trial,
                    "capacity before bytes");
            require(data == saved && !too_small.used, Rows, trial, "capacity unchanged");
        }
        for (unsigned r = 0; r < Rows; ++r) {
            if (!(active & (std::uint64_t(1) << r)))
                continue;
            bool done = false;
            for (unsigned i = 0; i < B; ++i) {
                if (map[i] == tp::hole || codes[map[i]].width == 8)
                    continue;
                auto bad = values;
                bad[r * B + i] = 1u << codes[map[i]].width;
                require(!write.set(first, bad, effects, active), Rows, trial, "width before bytes");
                require(data == saved && !effects.used, Rows, trial, "width unchanged");
#if defined(__aarch64__) || defined(__AVX2__)
                const auto nw = tp::native_writer(write);
                require(!nw.set(first, tp::native::load_packet(bad.data()), effects, active), Rows,
                        trial, "native width before bytes");
                require(data == saved && !effects.used, Rows, trial, "native width unchanged");
#endif
                done = true;
                break;
            }
            if (done)
                break;
        }
        require(bool(write.set(first, values, effects, active)), Rows, trial, "buffered set");
        require(data == wanted, Rows, trial, "buffered write/reference");
        for (unsigned i = 0; i < effects.used; ++i) {
            const auto& e = entries[i];
            require(e.source == &view && e.bytes.offset >= offset &&
                        e.bytes.offset + e.bytes.size <= data.size(),
                    Rows, trial, "qualified bounds");
        }
#if defined(__aarch64__) || defined(__AVX2__)
        std::copy(saved.begin(), saved.end(), data.begin());
        effects.used = 0;
        const auto nw = tp::native_writer(write);
        require(bool(nw.set(first, tp::native::load_packet(values.data()), effects, active)), Rows,
                trial, "native set");
        require(data == wanted, Rows, trial, "native write/reference");
#endif
        // Three packet windows include one row in the last; selection crosses
        // a backing word boundary with an origin unrelated to the packet shape.
        std::array<std::uint64_t, 4> selected_words{0xaaaaaaaaaaaaaaaaull, ~0ull, ~0ull, ~0ull};
        const auto selected = tp::selection::bits(0, selected_words);
        std::array<tp::packet<64>, 3> out{};
        require(bool(read.read(0, out, selected)), Rows, trial, "packet range tail");
        for (unsigned r = 0; r < 3 * Rows; ++r)
            for (unsigned i = 0; i < B; ++i) {
                tp::byte value = 0;
                if (r < count && selected.contains(r) && map[i] != tp::hole) {
                    const auto c = codes[map[i]];
                    value =
                        (data[offset + r * stride + c.offset] >> c.shift) & ((1u << c.width) - 1);
                }
                require(out[r / Rows][(r % Rows) * B + i] == value, Rows, trial,
                        "range original coordinates");
            }
        effects.used = 0;
        require(bool(write.replace(0, out, effects, selected)), Rows, trial, "range replacement");
        require(data == wanted, Rows, trial, "range preserves same values and inactive rows");
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
    void after(std::size_t, unsigned) {
        ++after_count;
    }
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
    for (auto& packet : input)
        for (unsigned r = 0; r < 32; ++r)
            packet[2 * r] = 5;
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
    auto allocation = static_cast<tp::byte*>(
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
    void observe_batch(std::size_t first, std::uint64_t active, const Before& before,
                       const After& after) {
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
    std::mt19937 random(89123);
    wire<1>(random);
    wire<2>(random);
    wire<4>(random);
    wire<8>(random);
    wire<16>(random);
    wire<32>(random);
    wire<64>(random);
    range_admission();
    guarded_tails();
#if defined(__aarch64__) || defined(__AVX2__)
    composition();
#endif
    std::puts("TuplePack packet operations: seven shapes, buffered/native wire, admission, sparse "
              "tails, effects and nested native maintenance passed");
}

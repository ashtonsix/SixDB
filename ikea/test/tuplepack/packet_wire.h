#pragma once
#include <algorithm>
#include <bit>
#include <cstdio>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <random>
#include <vector>
namespace tp = ikea::tuplepack;
template <unsigned N>
constexpr bool has_native = N == 8
#if defined(__aarch64__) || defined(__AVX2__)
                            || true
#endif
    ;
void require(bool, unsigned rows, unsigned trial, const char *scenario);
template <unsigned N> tp::packet<N> buffered(const std::array<tp::byte, N> &bytes) {
    return std::bit_cast<tp::packet<N>>(bytes);
}
template <unsigned N, class Value> std::array<tp::byte, N> unpack(Value value) {
    std::array<tp::byte, N> bytes{};
    if constexpr (N == 8)
        bytes = std::bit_cast<decltype(bytes)>(value);
#if defined(__aarch64__) || defined(__AVX2__)
    else
        tp::native::store_packet(bytes.data(), value);
#endif
    return bytes;
}
template <unsigned N> auto native_value(const std::array<tp::byte, N> &bytes) {
    if constexpr (N == 8)
        return buffered<N>(bytes);
#if defined(__aarch64__) || defined(__AVX2__)
    else
        return tp::native::load_packet(bytes.data());
#endif
}
template <unsigned N, unsigned Rows> void wire(std::mt19937 &random) {
    constexpr unsigned B = N / Rows;
    constexpr auto all = tp::detail::all_rows<Rows>;
    const auto simple = *tp::layout::make(1, std::array<tp::code, 1>{tp::code{0, 0, 8}});
    const std::array<tp::byte, 1> singleton{0};
    require(!tp::reader<N, Rows>::make(simple, singleton, std::array{0u, 1u}) &&
                !tp::writer<N, Rows>::make(simple, singleton, std::array{2u}) &&
                !tp::reader<N, Rows>::make(simple, singleton, std::array{1u, 1u}),
            Rows, 0, "groups require positive exact coverage");
    require(tp::reader<N, Rows>::make(simple, {}) && tp::writer<N, Rows>::make(simple, {}), Rows, 0,
            "empty map uses empty packet prefix");
    if constexpr (B >= 2) {
        const std::array<tp::byte, 2> duplicate{0, 0};
        require(tp::reader<N, Rows>::make(simple, duplicate, std::array{1u, 1u}) &&
                    !tp::writer<N, Rows>::make(simple, duplicate, std::array{1u, 1u}),
                Rows, 0, "groups do not change alias/duplicate rules");
    }
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
        // Independent group-major byte enumeration, deliberately unrelated to
        // prepared shuffle controls or packet_layout::offset.
        const unsigned map_slots = trial % 4 ? 1 + random() % B : B;
        std::vector<unsigned> groups;
        unsigned left = map_slots;
        while (left) {
            const unsigned length = trial % 3 ? 1 + random() % left : left;
            groups.push_back(length);
            left -= length;
        }
        std::array<unsigned, N> position{}, owner{};
        owner.fill(Rows);
        unsigned next_byte = 0, begin = 0;
        for (auto length : groups) {
            for (unsigned r = 0; r < Rows; ++r)
                for (unsigned i = 0; i < length; ++i) {
                    position[r * B + begin + i] = next_byte;
                    owner[next_byte++] = r;
                }
            begin += length;
        }
        const auto selected_map = std::span(map).first(map_slots);
        const auto grouping =
            trial % 3 ? std::span<const unsigned>(groups) : std::span<const unsigned>{};
        auto rp = *tp::reader<N, Rows>::make(format, selected_map, grouping);
        auto wp = *tp::writer<N, Rows>::make(format, selected_map, grouping);
        std::vector<tp::byte> data(offset + (count - 1) * stride + extent);
        for (auto &b : data)
            b = random();
        auto view = *tp::view::bind(format, data, count, stride, offset);
        auto read = *tp::bind_reader(rp, view);
        auto write = *tp::bind_writer(wp, view);
        const std::uint64_t active =
            trial % 3 ? ((std::uint64_t(random()) << 32) | random()) & all : all;
        const std::size_t first = 1;
        std::array<tp::byte, N> expected{}, values{};
        auto wanted = data;
        for (unsigned r = 0; r < Rows; ++r)
            for (unsigned i = 0; i < map_slots; ++i) {
                values[position[r * B + i]] = random();
                if (map[i] == tp::hole)
                    continue;
                const auto c = codes[map[i]];
                if (!(active & (std::uint64_t(1) << r)))
                    continue;
                const auto pos = offset + (first + r) * stride + c.offset;
                values[position[r * B + i]] &= (1u << c.width) - 1;
                for (unsigned bit = 0; bit < c.width; ++bit) {
                    expected[position[r * B + i]] |= ((data[pos] >> (c.shift + bit)) & 1) << bit;
                    wanted[pos] =
                        tp::byte((wanted[pos] & ~(1u << (c.shift + bit))) |
                                 (((values[position[r * B + i]] >> bit) & 1) << (c.shift + bit)));
                }
            }
        if (read.get(first, active) != buffered<N>(expected)) {
            std::fprintf(stderr, "N=%u extent=%u stride=%u map=%u active=%llx groups:", N, extent,
                         stride, map_slots, (unsigned long long)active);
            for (auto g : groups)
                std::fprintf(stderr, " %u", g);
            std::fprintf(stderr, "\n");
            auto actual = std::bit_cast<std::array<tp::byte, N>>(*read.get(first, active));
            for (unsigned i = 0; i < N; ++i)
                if (actual[i] != expected[i])
                    std::fprintf(stderr, "byte%u actual=%u expected=%u\n", i, actual[i],
                                 expected[i]);
        }
        require(read.get(first, active) == buffered<N>(expected), Rows, trial,
                "buffered read/reference");
        require(!read.get(count, all), Rows, trial, "active tail rejection");
        require(read.get(count, 0) == tp::packet<N>{}, Rows, trial, "empty end window");
        if constexpr (Rows < 64)
            require(!read.get(0, all + 1), Rows, trial, "high active bit rejected");
        if constexpr (has_native<N>) {
            const auto native_read = tp::native_reader(read);
            std::array<tp::byte, N> actual{};
            actual = unpack<N>(native_read.get_unchecked(first, active));
            require(actual == expected, Rows, trial, "native read/reference");
            actual = unpack<N>(tp::native::row_mask(rp, active));
            for (unsigned i = 0; i < N; ++i)
                require(
                    actual[i] ==
                        ((owner[i] < Rows && (active & (std::uint64_t(1) << owner[i]))) ? 255 : 0),
                    Rows, trial, "native row mask");
        }
        std::array<ikea::owner_write, 4096> entries{};
        ikea::source_write_journal effects{entries};
        auto saved = data;
        if (active && wp.effect_capacity()) {
            std::array<ikea::owner_write, 0> none;
            ikea::source_write_journal too_small{none};
            require(!write.set(first, buffered<N>(values), too_small, active), Rows, trial,
                    "capacity before bytes");
            require(data == saved && !too_small.used, Rows, trial, "capacity unchanged");
        }
        for (unsigned r = 0; r < Rows; ++r) {
            if (!(active & (std::uint64_t(1) << r)))
                continue;
            bool done = false;
            for (unsigned i = 0; i < map_slots; ++i) {
                if (map[i] == tp::hole || codes[map[i]].width == 8)
                    continue;
                auto bad = values;
                bad[position[r * B + i]] = 1u << codes[map[i]].width;
                require(!write.set(first, buffered<N>(bad), effects, active), Rows, trial,
                        "width before bytes");
                require(data == saved && !effects.used, Rows, trial, "width unchanged");
                if constexpr (has_native<N>) {
                    const auto nw = tp::native_writer(write);
                    require(!nw.set(first, native_value<N>(bad), effects, active), Rows, trial,
                            "native width before bytes");
                    require(data == saved && !effects.used, Rows, trial, "native width unchanged");
                }
                done = true;
                break;
            }
            if (done)
                break;
        }
        require(bool(write.set(first, buffered<N>(values), effects, active)), Rows, trial,
                "buffered set");
        require(data == wanted, Rows, trial, "buffered write/reference");
        for (unsigned i = 0; i < effects.used; ++i) {
            const auto &e = entries[i];
            require(e.source == &view && e.bytes.offset >= offset &&
                        e.bytes.offset + e.bytes.size <= data.size(),
                    Rows, trial, "qualified bounds");
        }
        if constexpr (has_native<N>) {
            std::copy(saved.begin(), saved.end(), data.begin());
            effects.used = 0;
            const auto nw = tp::native_writer(write);
            require(bool(nw.set(first, native_value<N>(values), effects, active)), Rows, trial,
                    "native set");
            require(data == wanted, Rows, trial, "native write/reference");
        }
        // Three packet windows include one row in the last; selection crosses
        // a backing word boundary with an origin unrelated to the packet shape.
        std::array<std::uint64_t, 4> selected_words{0xaaaaaaaaaaaaaaaaull, ~0ull, ~0ull, ~0ull};
        const auto selected = tp::selection::bits(0, selected_words);
        std::array<tp::packet<N>, 3> out{};
        require(bool(read.read(0, out, selected)), Rows, trial, "packet range tail");
        for (unsigned r = 0; r < 3 * Rows; ++r)
            for (unsigned i = 0; i < map_slots; ++i) {
                tp::byte value = 0;
                if (r < count && selected.contains(r) && map[i] != tp::hole) {
                    const auto c = codes[map[i]];
                    value =
                        (data[offset + r * stride + c.offset] >> c.shift) & ((1u << c.width) - 1);
                }
                require(tp::detail::input_byte<N>(out[r / Rows], position[(r % Rows) * B + i]) ==
                            value,
                        Rows, trial, "range original coordinates");
            }
        effects.used = 0;
        require(bool(write.replace(0, out, effects, selected)), Rows, trial, "range replacement");
        require(data == wanted, Rows, trial, "range preserves same values and inactive rows");
    }
}

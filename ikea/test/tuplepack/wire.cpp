#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/native.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <random>
#include <source_location>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace tp = ikea::tuplepack;
namespace {
std::string_view family;
std::size_t case_id = 0, checks = 0;

void check(bool valid, const char* expression,
           std::source_location where = std::source_location::current()) {
    ++checks;
    if (valid)
        return;
    std::fprintf(stderr, "%s:%u: %s; family=%.*s case=%zu\n", where.file_name(), where.line(),
                 expression, int(family.size()), family.data(), case_id);
    std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)

template <class T> void fails(const std::expected<T, tp::error>& value, tp::error reason) {
    CHECK(!value);
    CHECK(value.error() == reason);
}

// Expected values use description bits directly. No prepared routes, masks,
// source chunks, store groups or spike controls participate in the reference.
template <unsigned N> std::uint8_t input_byte(const tp::packet<N>& input, unsigned slot) {
    if constexpr (N == 8)
        return std::uint8_t(input >> (8 * slot));
    else
        return input[slot];
}
template <unsigned N> void put_byte(tp::packet<N>& input, unsigned slot, std::uint8_t value) {
    if constexpr (N == 8) {
        const auto mask = std::uint64_t(255) << (8 * slot);
        input = (input & ~mask) | (std::uint64_t(value) << (8 * slot));
    } else
        input[slot] = value;
}
template <unsigned N>
tp::packet<N> reference_read(const tp::layout& format, std::span<const std::uint8_t> map, const std::uint8_t* row) {
    tp::packet<N> result{};
    for (unsigned i = 0; i < map.size(); ++i) {
        if (map[i] == tp::hole)
            continue;
        const auto c = format.codes()[map[i]];
        unsigned value = 0;
        for (unsigned bit = 0; bit < c.width; ++bit)
            if (row[c.offset] & (1u << (c.shift + bit)))
                value |= 1u << bit;
        put_byte<N>(result, i, std::uint8_t(value));
    }
    return result;
}
template <unsigned N>
void reference_write(const tp::layout& format, std::span<const std::uint8_t> map, std::uint8_t* row,
                     const tp::packet<N>& input) {
    for (unsigned i = 0; i < map.size(); ++i) {
        if (map[i] == tp::hole)
            continue;
        const auto c = format.codes()[map[i]];
        for (unsigned bit = 0; bit < c.width; ++bit) {
            const std::uint8_t target = std::uint8_t(1u << (c.shift + bit));
            if (input_byte<N>(input, i) & (1u << bit))
                row[c.offset] |= target;
            else
                row[c.offset] &= std::uint8_t(~target);
        }
    }
}

std::vector<std::uint8_t> wire_bytes(unsigned bytes, std::span<const tp::code> codes) {
    const auto body_size = 1 + codes.size() * 3;
    std::vector<std::uint8_t> result{'T', 'P', 2, std::uint8_t(body_size), std::uint8_t(body_size >> 8), std::uint8_t(bytes)};
    for (auto c : codes)
        result.insert(result.end(), {c.offset, c.shift, c.width});
    return result;
}
bool reference_description(unsigned bytes, std::span<const tp::code> codes) {
    if (bytes < 1 || bytes > 64 || codes.empty() || codes.size() > 128)
        return false;
    std::array<bool, 512> occupied{};
    for (auto c : codes) {
        if (c.offset >= bytes || c.width < 1 || c.width > 8 || unsigned(c.shift) + c.width > 8)
            return false;
        for (unsigned bit = 0; bit < c.width; ++bit) {
            const unsigned position = unsigned(c.offset) * 8 + c.shift + bit;
            if (occupied[position])
                return false;
            occupied[position] = true;
        }
    }
    return true;
}
bool reference_wire(std::span<const std::uint8_t> input) {
    if (input.size() < 6 || input[0] != 'T' || input[1] != 'P' || input[2] != 2)
        return false;
    const unsigned body_size = input[3] + unsigned(input[4]) * 256;
    if (input.size() != 5 + body_size || !body_size || (body_size - 1) % 3)
        return false;
    const unsigned count = (body_size - 1) / 3;
    std::vector<tp::code> codes;
    for (unsigned i = 0; i < count; ++i)
        codes.push_back({input[6 + i * 3], input[7 + i * 3], input[8 + i * 3]});
    return reference_description(input[5], codes);
}
auto decode_layout(std::span<const std::uint8_t> input) {
    return tp::layout::decode(std::as_bytes(input));
}

void check_description(const tp::layout& format) {
    const auto golden = wire_bytes(format.bytes(), format.codes());
    CHECK(format.encoded_size() == golden.size());
    std::vector<std::uint8_t> actual(golden.size() + 9, 0xa5);
    const auto encoded = format.encode(std::as_writable_bytes(std::span{actual}));
    CHECK(encoded && *encoded == golden.size());
    CHECK(std::equal(golden.begin(), golden.end(), actual.begin()));
    CHECK(std::ranges::all_of(std::span(actual).subspan(golden.size()),
                              [](std::uint8_t b) { return b == 0xa5; }));
    const auto decoded = decode_layout(golden);
    CHECK(decoded && *decoded == format);
    for (std::size_t length : {std::size_t(0), golden.size() - 1}) {
        std::ranges::fill(actual, 0xa5);
        fails(format.encode(std::as_writable_bytes(std::span(actual).first(length))), tp::error::capacity);
        CHECK(std::ranges::all_of(actual, [](std::uint8_t b) { return b == 0xa5; }));
    }
}

void description_cases() {
    family = "description";
    const std::array codes{tp::code{3, 0, 8}, tp::code{0, 5, 3}, tp::code{0, 1, 4}};
    const auto format = tp::layout::make(4, codes);
    CHECK(format);
    check_description(*format);
    const std::vector<std::uint8_t> golden{'T', 'P', 2, 10, 0, 4, 3, 0, 8, 0, 5, 3, 0, 1, 4};
    CHECK(wire_bytes(4, codes) == golden);
    for (std::size_t i = 0; i < golden.size(); ++i) {
        case_id = i;
        CHECK(!decode_layout(std::span(golden).first(i)));
    }
    auto trailing = golden;
    trailing.push_back(0);
    CHECK(!decode_layout(trailing));
    for (unsigned position : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) {
        auto invalid = golden;
        invalid[position] = 255;
        CHECK(!decode_layout(invalid));
    }
    for (unsigned bytes : {0u, 65u, std::numeric_limits<unsigned>::max()})
        fails(tp::layout::make(bytes, codes), tp::error::description);
    fails(tp::layout::make(4, {}), tp::error::description);
    for (auto invalid : {tp::code{4, 0, 1}, tp::code{0, 0, 0}, tp::code{0, 0, 9}, tp::code{0, 8, 1},
                         tp::code{0, 7, 2}, tp::code{0, 255, 1}})
        fails(tp::layout::make(4, std::span(&invalid, 1)), tp::error::description);
    const std::array overlap{tp::code{0, 0, 3}, tp::code{0, 2, 2}};
    fails(tp::layout::make(1, overlap), tp::error::overlap);
    CHECK(!decode_layout(wire_bytes(1, overlap)));
    std::vector<tp::code> maximum;
    for (unsigned i = 0; i < 128; ++i)
        maximum.push_back({std::uint8_t(i / 8), std::uint8_t(i % 8), 1});
    auto full = tp::layout::make(16, maximum);
    CHECK(full);
    check_description(*full);
    maximum.push_back({16, 0, 1});
    fails(tp::layout::make(17, maximum), tp::error::description);
    CHECK(!decode_layout(wire_bytes(17, maximum)));

    std::mt19937_64 random(0xdec0de);
    for (case_id = 0; case_id < 4000; ++case_id) {
        auto mutated = case_id % 2 ? golden : wire_bytes(full->bytes(), full->codes());
        switch (case_id % 4) {
        case 0:
            mutated.resize(random() % (mutated.size() + 1));
            break;
        case 1:
            mutated.push_back(std::uint8_t(random()));
            break;
        default:
            for (unsigned j = 0; j < 1 + case_id % 4; ++j)
                mutated[random() % mutated.size()] = std::uint8_t(random());
        }
        CHECK(bool(decode_layout(mutated)) == reference_wire(mutated));
    }
}

template <unsigned N>
void check_read(const tp::read_plan<N>& plan, const tp::layout& format, std::span<const std::uint8_t> map,
                const std::uint8_t* row) {
    const auto expected = reference_read<N>(format, map, row);
    CHECK(plan.get_unchecked(std::as_bytes(std::span(row, format.bytes())).data()) == expected);
#if defined(__aarch64__) || defined(__AVX2__)
    if constexpr (N == 64) {
        tp::packet<64> actual;
        tp::native::store_packet(actual.data(), tp::native::read(plan.controls(), row));
        CHECK(actual == expected);
    }
#endif
}
template <unsigned N>
void check_write(const tp::write_plan<N>& plan, const tp::layout& format, std::span<const std::uint8_t> map,
                 std::span<const std::uint8_t> initial, const tp::packet<N>& input) {
    CHECK(plan.accepts(input));
    // Both sides retain canaries beyond the unit; the reference touches bits
    // individually, while the implementation chooses its own store widths.
    std::array<std::uint8_t, 80> expected{}, actual{};
    CHECK(initial.size() <= expected.size());
    std::copy(initial.begin(), initial.end(), expected.begin());
    actual = expected;
    reference_write<N>(format, map, expected.data(), input);
    plan.set_unchecked(std::as_writable_bytes(std::span{actual}).data(), input);
    CHECK(actual == expected);
#if defined(__aarch64__) || defined(__AVX2__)
    if constexpr (N == 64) {
        std::copy(initial.begin(), initial.end(), actual.begin());
        tp::native::write(plan.controls(), actual.data(), tp::native::load_packet(input.data()));
        CHECK(actual == expected);
    }
#endif
}

template <unsigned N> void single_codes() {
    family = N == 8 ? "single/scalar8" : "single/native64";
    case_id = 0;
    for (unsigned offset = 0; offset < 64; ++offset)
        for (unsigned shift = 0; shift < 8; ++shift)
            for (unsigned width = 1; width + shift <= 8; ++width, ++case_id) {
                const tp::code c{std::uint8_t(offset), std::uint8_t(shift), std::uint8_t(width)};
                auto format = tp::layout::make(offset + 1, std::span(&c, 1));
                CHECK(format);
                std::array<std::uint8_t, 80> row;
                for (unsigned i = 0; i < row.size(); ++i)
                    row[i] = std::uint8_t(37 * i + case_id);
                // Every source byte and every admissible value for every
                // width/shift/offset; the selected outer lane also varies.
                const unsigned lane = (offset * 11 + shift * 7 + width) % N;
                std::vector<std::uint8_t> map(lane + 1, tp::hole);
                map[lane] = 0;
                auto read = tp::read_plan<N>::make(*format, map);
                auto write = tp::write_plan<N>::make(*format, map);
                CHECK(read && write);
                tp::packet<N> input{};
                for (unsigned i = 0; i < N; ++i)
                    put_byte<N>(input, i, 255);
                for (unsigned value = 0; value < 256; ++value) {
                    row[offset] = std::uint8_t(value);
                    check_read<N>(*read, *format, map, row.data());
                    put_byte<N>(input, lane, std::uint8_t(value));
                    CHECK(write->accepts(input) == (value < (1u << width)));
                    if (value < (1u << width)) {
                        row[offset] = std::uint8_t(181 * value + 0xa5);
                        check_write<N>(*write, *format, map, row, input);
                    }
                }
                // Exhaust every output/input slot, including all ISA register
                // boundaries. Alternate exact tails with full 64-byte units.
                for (unsigned slot = 0; slot < N; ++slot) {
                    auto placed = tp::layout::make(slot % 2 ? 64 : offset + 1, std::span(&c, 1));
                    CHECK(placed);
                    map.assign(N, tp::hole);
                    map[slot] = 0;
                    read = tp::read_plan<N>::make(*placed, map);
                    write = tp::write_plan<N>::make(*placed, map);
                    CHECK(read && write);
                    row[offset] = std::uint8_t(case_id * 13 + slot * 29);
                    check_read<N>(*read, *placed, map, row.data());
                    for (unsigned i = 0; i < N; ++i)
                        put_byte<N>(input, i, 255);
                    put_byte<N>(input, slot, std::uint8_t((case_id + slot * 3) % (1u << width)));
                    check_write<N>(*write, *placed, map, row, input);
                }
            }
}

template <unsigned N> void map_errors() {
    family = N == 8 ? "maps/scalar8" : "maps/native64";
    std::vector<tp::code> codes;
    for (unsigned i = 0; i < 128; ++i)
        codes.push_back({std::uint8_t(i / 8), std::uint8_t(i % 8), 1});
    auto format = tp::layout::make(16, codes);
    CHECK(format);
    for (unsigned rank = 128; rank < tp::hole; ++rank) {
        const std::array map{std::uint8_t(rank)};
        fails(tp::read_plan<N>::make(*format, map), tp::error::map);
        fails(tp::write_plan<N>::make(*format, map), tp::error::map);
    }
    std::vector<std::uint8_t> oversized(N + 1, tp::hole);
    fails(tp::read_plan<N>::make(*format, oversized), tp::error::map);
    fails(tp::write_plan<N>::make(*format, oversized), tp::error::map);
    for (unsigned i = 0; i < N; ++i)
        for (unsigned j = i + 1; j < N; ++j) {
            case_id = i * N + j;
            std::vector<std::uint8_t> map(N, tp::hole);
            map[i] = map[j] = std::uint8_t((i * 11 + j) % 128);
            auto read = tp::read_plan<N>::make(*format, map);
            CHECK(read);
            fails(tp::write_plan<N>::make(*format, map), tp::error::duplicate);
            std::array<std::uint8_t, 16> row;
            row.fill(0xa5);
            check_read<N>(*read, *format, map, row.data());
        }
    for (unsigned size = 0; size <= N; ++size) {
        std::vector<std::uint8_t> map(size, tp::hole);
        auto read = tp::read_plan<N>::make(*format, map);
        auto write = tp::write_plan<N>::make(*format, map);
        CHECK(read && write);
        CHECK(read->read_bytes() == 0 && write->write_bytes() == 0 &&
              write->store_runs() == 0);
        std::array<std::uint8_t, 16> row;
        row.fill(0xa5);
        tp::packet<N> input{};
        for (unsigned i = 0; i < N; ++i)
            put_byte<N>(input, i, 255);
        check_read<N>(*read, *format, map, row.data());
        check_write<N>(*write, *format, map, row, input);
    }
}

template <unsigned N>
void bound_case(const tp::layout& format, std::span<const std::uint8_t> read_map,
                std::span<const std::uint8_t> write_map, std::mt19937_64& random) {
    auto read_plan = tp::read_plan<N>::make(format, read_map);
    auto write_plan = tp::write_plan<N>::make(format, write_map);
    CHECK(read_plan && write_plan);
    const std::size_t offset = 3 + random() % 31, stride = format.bytes() + 1 + random() % 29;
    constexpr unsigned rows = 5;
    std::vector<std::uint8_t> storage(offset + (rows - 1) * stride + format.bytes() + 19);
    for (auto& b : storage)
        b = std::uint8_t(random());
    const auto original = storage;
    auto view = tp::view::attach(format, std::as_writable_bytes(std::span{storage}), rows, stride, offset);
    auto const_view = tp::const_view::attach(format, std::as_bytes(std::span{storage}), rows, stride, offset);
    CHECK(view && const_view);
    auto read = tp::bind_reader(*read_plan, *const_view);
    auto mutable_read = tp::bind_reader(*read_plan, *view);
    auto write = tp::bind_writer(*write_plan, *view);
    CHECK(read && mutable_read && write);
    for (unsigned row = 0; row < rows; ++row) {
        const auto expected =
            reference_read<N>(format, read_map, storage.data() + offset + row * stride);
        const auto actual = read->get(row), mutable_actual = mutable_read->get(row);
        CHECK(actual && mutable_actual && *actual == expected && *mutable_actual == expected);
    }
    fails(read->get(rows), tp::error::range);
    std::array<tp::packet<N>, rows> output{};
    for (auto& p : output)
        for (unsigned i = 0; i < N; ++i)
            put_byte<N>(p, i, 0xa5);
    const auto unchanged = output;
    fails(read->read(rows - 1, output), tp::error::range);
    CHECK(output == unchanged);
    CHECK(read->read(0, output));
    for (unsigned row = 0; row < rows; ++row)
        CHECK(output[row] ==
              reference_read<N>(format, read_map, storage.data() + offset + row * stride));

    std::array<tp::packet<N>, rows> input{};
    for (auto& p : input) {
        for (unsigned i = 0; i < N; ++i)
            put_byte<N>(p, i, std::uint8_t(random()));
        for (unsigned i = 0; i < write_map.size(); ++i)
            if (write_map[i] != tp::hole)
                put_byte<N>(p, i, std::uint8_t(random() % (1u << format.codes()[write_map[i]].width)));
    }
    auto expected = original;
    for (unsigned row = 0; row < rows; ++row)
        reference_write<N>(format, write_map, expected.data() + offset + row * stride, input[row]);
    std::array<ikea::journal::entry, rows * 64> records{};
    ikea::journal effects{records};
    CHECK(write->replace(0, input, effects));
    CHECK(storage == expected);
    std::vector<bool> covered(storage.size());
    for (const auto& effect : effects.entries()) {
        CHECK(effect.source == &*view && effect.plane == 0);
        CHECK(effect.size && effect.offset < storage.size());
        CHECK(effect.size <= storage.size() - effect.offset);
        for (std::size_t b = effect.offset; b < effect.offset + effect.size;
             ++b) {
            CHECK(b >= offset);
            CHECK((b - offset) / stride < rows && (b - offset) % stride < format.bytes());
            covered[b] = true;
        }
    }
    for (std::size_t b = 0; b < storage.size(); ++b)
        if (storage[b] != original[b])
            CHECK(covered[b]);
    for (unsigned row = 0; row < rows; ++row) {
        check_read<N>(*read_plan, format, read_map, storage.data() + offset + row * stride);
        check_write<N>(*write_plan, format, write_map,
                       std::span(original).subspan(offset + row * stride, format.bytes()),
                       input[row]);
    }
    // The final selected value fails; the earlier rows must remain untouched.
    for (unsigned slot = 0; slot < write_map.size(); ++slot) {
        if (write_map[slot] == tp::hole)
            continue;
        const auto width = format.codes()[write_map[slot]].width;
        if (width == 8)
            continue;
        put_byte<N>(input.back(), slot, std::uint8_t(1u << width));
        const auto saved = storage;
        const auto used = effects.used;
        fails(write->replace(0, input, effects), tp::error::value);
        CHECK(storage == saved && effects.used == used);
        break;
    }
}

void random_schemas() {
    family = "random/seed=0x7475706c65706163";
    std::mt19937_64 random(0x7475706c65706163);
    for (case_id = 0; case_id < 1200; ++case_id) {
        const unsigned bytes = 1 + random() % 64;
        std::vector<tp::code> codes;
        for (unsigned offset = 0; offset < bytes && codes.size() < 128; ++offset)
            for (unsigned bit = 0; bit < 8 && codes.size() < 128;) {
                if (random() % 5 == 0) {
                    ++bit;
                    continue;
                }
                const unsigned width = 1 + random() % (8 - bit);
                codes.push_back({std::uint8_t(offset), std::uint8_t(bit), std::uint8_t(width)});
                bit += width;
            }
        if (codes.empty())
            codes.push_back({0, 3, 2});
        std::shuffle(codes.begin(), codes.end(), random);
        CHECK(reference_description(bytes, codes));
        const auto format = tp::layout::make(bytes, codes);
        CHECK(format);
        check_description(*format);
        auto exercise = [&]<unsigned N>() {
            std::vector<std::uint8_t> reads(random() % (N + 1));
            for (auto& rank : reads)
                rank = random() % 4 ? std::uint8_t(random() % codes.size()) : tp::hole;
            std::vector<std::uint8_t> ranks(codes.size());
            std::iota(ranks.begin(), ranks.end(), std::uint8_t(0));
            std::shuffle(ranks.begin(), ranks.end(), random);
            std::vector<std::uint8_t> writes(random() % (N + 1), tp::hole);
            unsigned next = 0;
            for (auto& rank : writes)
                if (next < ranks.size() && random() % 4)
                    rank = ranks[next++];
            bound_case<N>(*format, reads, writes, random);
        };
        exercise.template operator()<8>();
        exercise.template operator()<64>();
    }
}

void placement_cases() {
    family = "placement";
    const std::array codes{tp::code{2, 0, 8}};
    auto format = tp::layout::make(3, codes);
    CHECK(format);
    std::array<std::uint8_t, 24> storage{};
    fails(tp::view::attach(*format, std::as_writable_bytes(std::span{storage}), 1, 2), tp::error::stride);
    fails(tp::view::attach(*format, std::as_writable_bytes(std::span{storage}), 1, 3, 25), tp::error::capacity);
    fails(tp::view::attach(*format, std::as_writable_bytes(std::span{storage}), 1, 3, 22), tp::error::capacity);
    fails(tp::view::attach(*format, std::as_writable_bytes(std::span{storage}), 9, 3), tp::error::capacity);
    fails(tp::view::attach(*format, std::as_writable_bytes(std::span{storage}), std::numeric_limits<std::size_t>::max(), 3),
          tp::error::capacity);
    fails(tp::view::attach(*format, std::as_writable_bytes(std::span{storage}), 2, std::numeric_limits<std::size_t>::max()),
          tp::error::capacity);
    CHECK(tp::view::attach(*format, std::as_writable_bytes(std::span{storage}), 1, std::numeric_limits<std::size_t>::max(), 21));
    auto empty = tp::view::attach(*format, std::as_writable_bytes(std::span{std::span<std::byte>{}}), 0, 3);
    CHECK(empty);
    auto plan = tp::read_plan<8>::make(*format, std::array<std::uint8_t, 1>{0});
    CHECK(plan);
    auto read = tp::bind_reader(*plan, *empty);
    CHECK(read && read->size() == 0);
    CHECK(read->read(0, {}));
    fails(read->get(0), tp::error::range);
    auto larger = tp::layout::make(4, codes);
    CHECK(larger);
    auto wrong = tp::view::attach(*larger, std::as_writable_bytes(std::span{storage}), 1, 4);
    CHECK(wrong);
    fails(tp::bind_reader(*plan, *wrong), tp::error::description);
    auto write_plan = tp::write_plan<8>::make(*format, std::array<std::uint8_t, 1>{0});
    CHECK(write_plan);
    fails(tp::bind_writer(*write_plan, *wrong), tp::error::description);
}

struct guarded_page {
    std::size_t size;
    std::uint8_t* mapping;
    guarded_page()
        : size(std::size_t(sysconf(_SC_PAGESIZE))),
          mapping(static_cast<std::uint8_t*>(
              mmap(nullptr, size * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))) {
        CHECK(size >= 512 && mapping != MAP_FAILED);
        CHECK(mprotect(data(), size, PROT_READ | PROT_WRITE) == 0);
    }
    ~guarded_page() {
        CHECK(munmap(mapping, size * 3) == 0);
    }
    guarded_page(const guarded_page&) = delete;
    guarded_page& operator=(const guarded_page&) = delete;
    std::uint8_t* data() const {
        return mapping + size;
    }
};

template <unsigned N>
void guard_case(guarded_page& page, const tp::layout& format, std::span<const std::uint8_t> read_map,
                std::span<const std::uint8_t> write_map, bool at_end) {
    auto read = tp::read_plan<N>::make(format, read_map);
    auto write = tp::write_plan<N>::make(format, write_map);
    CHECK(read && write);
    const std::size_t offset = at_end ? page.size - format.bytes() : 0;
    std::vector<std::uint8_t> expected(page.size);
    for (std::size_t i = 0; i < page.size; ++i)
        expected[i] = std::uint8_t(i * 13 + case_id * 7);
    std::copy(expected.begin(), expected.end(), page.data());
    CHECK(mprotect(page.data(), page.size, PROT_READ) == 0);
    check_read<N>(*read, format, read_map, page.data() + offset);
    CHECK(mprotect(page.data(), page.size, PROT_READ | PROT_WRITE) == 0);
    tp::packet<N> input{};
    for (unsigned i = 0; i < N; ++i)
        put_byte<N>(input, i, 255);
    for (unsigned i = 0; i < write_map.size(); ++i)
        if (write_map[i] != tp::hole)
            put_byte<N>(input, i,
                        std::uint8_t((case_id + i * 31) % (1u << format.codes()[write_map[i]].width)));
    reference_write<N>(format, write_map, expected.data() + offset, input);
    write->set_unchecked(std::as_writable_bytes(std::span(page.data() + offset, format.bytes())).data(), input);
    CHECK(std::equal(expected.begin(), expected.end(), page.data()));
#if defined(__aarch64__) || defined(__AVX2__)
    if constexpr (N == 64) {
        // Use different preserved bits for the native entry; testing the same
        // update twice could hide an omitted store.
        for (std::size_t i = 0; i < page.size; ++i)
            expected[i] ^= 0xff;
        std::copy(expected.begin(), expected.end(), page.data());
        reference_write<N>(format, write_map, expected.data() + offset, input);
        tp::native::write(write->controls(), page.data() + offset,
                          tp::native::load_packet(input.data()));
        CHECK(std::equal(expected.begin(), expected.end(), page.data()));
    }
#endif
}

void guard_cases() {
    family = "guard-pages";
    guarded_page page;
    case_id = 0;
    for (unsigned bytes = 1; bytes <= 64; ++bytes)
        for (unsigned width : {1u, 5u, 8u}) {
            std::vector<tp::code> codes;
            for (unsigned i = 0; i < bytes; ++i)
                codes.push_back({std::uint8_t(i), std::uint8_t(8 - width), std::uint8_t(width)});
            auto format = tp::layout::make(bytes, codes);
            CHECK(format);
            for (bool at_end : {false, true}) {
                ++case_id;
                // Touch every physical byte: both full replacement and spare
                // preservation must use exact final chunks, including 1..15.
                std::vector<std::uint8_t> all(bytes);
                std::iota(all.rbegin(), all.rend(), std::uint8_t(0));
                guard_case<64>(page, *format, all, all, at_end);
                // Sparse first/last source chunks and holes; no implicit unit
                // padding is readable beyond either guard boundary.
                const std::array<std::uint8_t, 8> reads{
                    std::uint8_t(bytes - 1), tp::hole,        0, std::uint8_t(bytes / 2),
                    tp::hole,        std::uint8_t(bytes - 1), 0, tp::hole};
                std::vector<std::uint8_t> writes{std::uint8_t(bytes - 1), tp::hole};
                if (bytes > 1)
                    writes.push_back(0);
                guard_case<8>(page, *format, reads, writes, at_end);
                guard_case<64>(page, *format, reads, writes, at_end);
                auto wire = wire_bytes(bytes, codes);
                std::uint8_t* end = page.data() + page.size - wire.size();
                std::copy(wire.begin(), wire.end(), end);
                auto decoded = decode_layout(std::span<const std::uint8_t>(end, wire.size()));
                CHECK(decoded && *decoded == *format);
                CHECK(format->encode(std::as_writable_bytes(std::span(end, wire.size()))));
                CHECK(std::equal(wire.begin(), wire.end(), end));
            }
        }
    // Eight independently routed bit contributions to each byte exercise all
    // write rounds, not just one whole-byte contribution per destination.
    for (unsigned bytes = 1; bytes <= 8; ++bytes) {
        std::vector<tp::code> codes;
        for (unsigned i = 0; i < bytes * 8; ++i)
            codes.push_back({std::uint8_t(i / 8), std::uint8_t(i % 8), 1});
        auto format = tp::layout::make(bytes, codes);
        CHECK(format);
        std::vector<std::uint8_t> map(bytes * 8);
        std::iota(map.rbegin(), map.rend(), std::uint8_t(0));
        for (bool at_end : {false, true}) {
            ++case_id;
            guard_case<64>(page, *format, map, map, at_end);
        }
    }
}
} // namespace

int main() {
    description_cases();
    single_codes<8>();
    single_codes<64>();
    map_errors<8>();
    map_errors<64>();
    random_schemas();
    placement_cases();
    guard_cases();
    std::printf("TuplePack wire: %zu checks passed; independent description-bit reference; "
                "2,304 single-code shapes per packet size, every byte value and slot; "
                "1,200 random schemas; descriptor mutation and guard pages\n",
                checks);
}

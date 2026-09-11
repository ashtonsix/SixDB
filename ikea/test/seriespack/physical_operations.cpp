#include <ikea/seriespack/operations.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

namespace {

namespace sp = ikea::seriespack;
using image = std::array<std::vector<std::byte>, 3>;
sp::execution_target execution = sp::execution_target::scalar;
const char* target_name = "scalar";

[[noreturn]] void fail(const char* what, sp::description description,
                       std::size_t length, std::size_t index = 0) {
    std::fprintf(stderr, "SeriesPack public wire: %s, k=%u h=%u geometry=%s "
                         "n=%zu index=%zu\n", what, description.width,
                 description.head_bits,
                 description.storage == sp::geometry::local8 ? "local8" : "striped",
                 length, index);
    std::abort();
}

std::uint64_t random_bits() {
    static std::uint64_t state = 0x803bf1ce97462da5ULL;
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

std::uint64_t mask(unsigned width) {
    return width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

// Independently derive payload positions from ikea/seriespack/reference.md. This test never
// calls physical kernels, production residual maps or placement-offset helpers.
struct location { std::size_t byte; unsigned bit; };

unsigned residual_position(unsigned tail, unsigned group, unsigned bit) {
    constexpr unsigned three[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 14}, {8, 9, 10},
        {11, 12, 13}, {22, 23, 15}, {16, 17, 18}, {19, 20, 21},
    };
    constexpr unsigned six[4][6] = {
        {0, 1, 2, 3, 4, 5}, {8, 9, 10, 11, 6, 7},
        {12, 13, 14, 15, 22, 23}, {16, 17, 18, 19, 20, 21},
    };
    if (tail == 3) return three[group][bit];
    if (tail == 6) return six[group][bit];
    const unsigned start = group * tail;
    const unsigned room = 8 - start % 8;
    if (tail <= room) return start + bit;
    const unsigned low_bits = tail - room;
    return bit < low_bits ? (start / 8 + 1) * 8 + bit : start + bit - low_bits;
}

location payload_location(unsigned width, sp::geometry storage,
                          std::size_t value, unsigned bit) {
    const unsigned q = width / 8, r = width % 8;
    if (storage == sp::geometry::local8) {
        if (bit < r) return {8 * q + bit, unsigned(value)};
        return {value * q + (bit - r) / 8, (bit - r) % 8};
    }
    const auto group = unsigned(value / 32), lane = unsigned(value % 32);
    if (bit >= r) {
        std::size_t begin = 0;
        switch (width) {
        case 10: {
            constexpr unsigned starts[] = {0, 32, 96, 128};
            begin = starts[group];
            break;
        }
        case 12: begin = 32 * group; break;
        case 14:
        case 15: begin = 64 * group; break;
        case 20: begin = 96 * group; break;
        default: std::abort();
        }
        return {begin + lane * q + (bit - r) / 8, (bit - r) % 8};
    }
    const unsigned position = residual_position(r, group, bit);
    const unsigned stripe = position / 8;
    const unsigned start = width < 8 ? 32 * stripe
                           : (width == 14 || width == 15) ? 32 + 64 * stripe : 64;
    return {start + lane, position % 8};
}

bool striped_width(unsigned width) {
    return (width >= 1 && width <= 7) || width == 10 || width == 12 ||
           width == 14 || width == 15 || width == 20;
}

struct fixture {
    sp::description description;
    std::size_t length;
    std::size_t values_per_tile;
    std::size_t tiles;
    std::array<std::size_t, 3> tile_bytes{};
    std::array<std::size_t, 3> stride{};
    std::array<std::size_t, 3> envelope{};
    std::array<std::size_t, 3> base{};
    image bytes;

    fixture(sp::description desc, std::size_t n, bool strided)
        : description(desc), length(n),
          values_per_tile(desc.storage == sp::geometry::local8 ? 8
              : 32 * (8 / std::gcd((desc.width - desc.head_bits) % 8, 8U))),
          tiles(n / values_per_tile + (n % values_per_tile != 0)) {
        const unsigned payload = desc.width - desc.head_bits;
        tile_bytes[0] = values_per_tile * payload / 8;
        for (unsigned h = 0; h < desc.head_bits / 8; ++h) tile_bytes[1 + h] = values_per_tile;
        for (std::size_t plane = 0; plane < 3; ++plane) {
            stride[plane] = tile_bytes[plane] + (strided ?
                (plane == 0 && desc.storage == sp::geometry::striped ? 32 : 11 + plane * 2) : 0);
            envelope[plane] = tiles == 0 || tile_bytes[plane] == 0 ? 0
                : (tiles - 1) * stride[plane] + tile_bytes[plane];
            bytes[plane].assign(envelope[plane] + 128, std::byte{0xd3});
            const auto address = reinterpret_cast<std::uintptr_t>(bytes[plane].data());
            base[plane] = std::size_t((64 - address % 64) % 64);
            if (plane != 0 || desc.storage == sp::geometry::local8) base[plane] += desc.width % 8;
            // Owner-provided initialized tiles. Gaps and outside bytes retain
            // distinct canaries; expected images include all of them.
            for (std::size_t tile = 0; tile < tiles; ++tile) {
                std::fill_n(bytes[plane].begin() + std::ptrdiff_t(base[plane] + tile * stride[plane]),
                            tile_bytes[plane], std::byte{0});
            }
        }
    }

    std::span<std::byte> plane(std::size_t which) {
        if (envelope[which] == 0) return {};
        return {bytes[which].data() + base[which], envelope[which]};
    }

    sp::basic_placement<std::byte> placement() {
        return {{plane(0), stride[0]}, {{{plane(1), stride[1]}, {plane(2), stride[2]}}}};
    }

    sp::mutable_view view(std::size_t n) {
        auto result = sp::mutable_view::attach(description, n, placement());
        if (!result) fail("attach", description, n);
        return *result;
    }

    sp::mutable_view view() { return view(length); }

    void oracle_set(image& expected, std::size_t index, std::uint64_t value) const {
        const auto tile = index / values_per_tile, lane = index % values_per_tile;
        const unsigned payload = description.width - description.head_bits;
        for (unsigned bit = 0; bit < payload; ++bit) {
            const auto location = payload_location(payload, description.storage, lane, bit);
            auto& byte = expected[0][base[0] + tile * stride[0] + location.byte];
            const auto bit_mask = std::byte(1U << location.bit);
            byte = (byte & ~bit_mask) | (((value >> bit) & 1) ? bit_mask : std::byte{0});
        }
        for (unsigned head = 0; head < description.head_bits / 8; ++head) {
            expected[1 + head][base[1 + head] + tile * stride[1 + head] + lane] =
                std::byte((value >> (description.width - 8 * (head + 1))) & 255);
        }
    }

    void compare(const image& expected, const char* what) const {
        for (std::size_t plane = 0; plane < 3; ++plane) {
            if (bytes[plane] != expected[plane]) fail(what, description, length, plane);
        }
    }

    bool owned(std::size_t plane, std::size_t offset) const {
        if (tile_bytes[plane] == 0 || offset < base[plane]) return false;
        const auto relative = offset - base[plane];
        return relative < envelope[plane] && relative % stride[plane] < tile_bytes[plane];
    }
};

struct effects {
    inline static std::byte prefix_marker{};
    std::vector<sp::byte_span> records;
    sp::effect_output output;

    explicit effects(std::size_t capacity)
        : records(capacity, {nullptr, 0}), output{records, 1} {
        records[0] = {&prefix_marker, 1};
    }

    void check(const fixture& storage, const image& before, bool full_construction) const {
        if (output.size < 1 || output.size > records.size() ||
            records[0].data != &prefix_marker || records[0].size != 1) {
            fail("effect prefix/capacity", storage.description, storage.length);
        }
        std::array<std::vector<bool>, 3> covered;
        for (std::size_t p = 0; p < 3; ++p) covered[p].resize(storage.bytes[p].size());
        for (std::size_t s = 1; s < output.size; ++s) {
            const auto start = reinterpret_cast<std::uintptr_t>(records[s].data);
            for (std::size_t b = 0; b < records[s].size; ++b) {
                bool found = false;
                for (std::size_t p = 0; p < 3; ++p) {
                    const auto base = reinterpret_cast<std::uintptr_t>(storage.bytes[p].data());
                    if (start + b >= base && start + b - base < storage.bytes[p].size()) {
                        const auto offset = std::size_t(start + b - base);
                        if (!storage.owned(p, offset)) {
                            fail("effect claims foreign bytes", storage.description, storage.length, p);
                        }
                        covered[p][offset] = true;
                        found = true;
                        break;
                    }
                }
                if (!found) fail("effect outside owners", storage.description, storage.length, s);
            }
        }
        for (std::size_t p = 0; p < 3; ++p) {
            for (std::size_t b = 0; b < storage.bytes[p].size(); ++b) {
                if ((storage.bytes[p][b] != before[p][b] ||
                     (full_construction && storage.owned(p, b))) && !covered[p][b]) {
                    fail("effect misses written/initialized byte", storage.description, storage.length, b);
                }
            }
        }
    }

    void unchanged(const std::vector<sp::byte_span>& before) const {
        if (output.size != 1 || records.size() != before.size()) std::abort();
        for (std::size_t i = 0; i < records.size(); ++i) {
            if (records[i].data != before[i].data || records[i].size != before[i].size) std::abort();
        }
    }
};

std::size_t descriptions = 0, placements = 0, reads = 0, writes = 0, appends = 0;

template<sp::unsigned_element UInt>
void check_decode(sp::const_view view, sp::index_range rows,
                  const std::vector<std::uint64_t>& expected) {
    const UInt sentinel = std::numeric_limits<UInt>::max();
    std::vector<UInt> result(rows.size() + 3, sentinel);
    auto status = sp::decode(view, rows, sp::output_values{std::span(result)},
                             execution);
    if (!status) fail("decode status", view.layout(), view.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (result[i] != expected[rows.begin + i]) fail("decode value", view.layout(), view.size(), i);
    }
    for (std::size_t i = rows.size(); i < result.size(); ++i) {
        if (result[i] != sentinel) fail("decode surplus changed", view.layout(), view.size(), i);
    }
    ++reads;
}

void check_readers(sp::const_view view, const std::vector<std::uint64_t>& expected) {
    const auto bound = sp::bind_reader(view, execution);
    if (!bound || bound->target() != execution) {
        fail("explicit target bind", view.layout(), view.size());
    }
    const auto offsets = sp::bind_reader(view, execution, sp::point_reader::constant_offsets);
    if (!offsets || offsets->point_strategy() != sp::point_reader::constant_offsets ||
        bound->point_strategy() != sp::point_reader::arithmetic)
        fail("point strategy bind", view.layout(), view.size());
    for (std::size_t i = 0; i < view.size(); ++i) {
        const auto actual = sp::get(view, i);
        if (!actual || *actual != expected[i] || bound->get(i) != expected[i] ||
            offsets->get(i) != expected[i]) {
            fail("point/bound reader", view.layout(), view.size(), i);
        }
    }
    if (sp::get(view, view.size())) fail("get accepts logical slack", view.layout(), view.size());
    const std::array ranges = {sp::index_range{0, view.size()},
        sp::index_range{view.size(), view.size()},
        sp::index_range{view.size() / 3, view.size() - view.size() / 4}};
    for (auto rows : ranges) {
        check_decode<std::uint64_t>(view, rows, expected);
        if (view.layout().width <= 8) check_decode<std::uint8_t>(view, rows, expected);
        else if (view.layout().width <= 16) check_decode<std::uint16_t>(view, rows, expected);
        else if (view.layout().width <= 32) check_decode<std::uint32_t>(view, rows, expected);
        std::vector<std::uint64_t> output(rows.size() + 2, 0xaeea'bcdd'5643'6712ULL);
        bound->decode(rows, sp::output_values{std::span(output)});
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (output[i] != expected[rows.begin + i]) fail("bound decode", view.layout(), view.size(), i);
        }
        if (output[rows.size()] != 0xaeea'bcdd'5643'6712ULL ||
            output[rows.size() + 1] != 0xaeea'bcdd'5643'6712ULL) {
            fail("bound decode surplus", view.layout(), view.size());
        }
        ++reads;
    }
}

void check_bound_encode(fixture& storage, sp::input_values input, const image& expected) {
    const auto encoder = sp::bind_encoder(storage.view(), execution);
    if (!encoder || encoder->target() != execution)
        fail("explicit encoder target bind", storage.description, storage.length);
    // Poison owned bytes so an accidental no-op cannot pass a replay against
    // already-canonical storage. Keep foreign gaps unchanged.
    for (std::size_t p = 0; p < 3; ++p)
        for (std::size_t b = 0; b < storage.bytes[p].size(); ++b)
            if (storage.owned(p, b)) storage.bytes[p][b] = std::byte{0xc7};
    const auto before = storage.bytes;
    effects coverage(8 * (storage.tiles * storage.values_per_tile + storage.tiles + 16));
    encoder->encode(input, &coverage.output);
    storage.compare(expected, "bound encode canonical bytes/slack/gaps");
    coverage.check(storage, before, true);
}

template<sp::unsigned_element UInt>
void check_encode_type(fixture& storage, const std::vector<std::uint64_t>& values,
                       const image& expected) {
    std::vector<UInt> input(values.size(), std::numeric_limits<UInt>::max());
    for (std::size_t i = 0; i < storage.length; ++i) input[i] = static_cast<UInt>(values[i]);
    effects coverage(8 * (storage.tiles * storage.values_per_tile + storage.tiles + 16));
    const auto before = storage.bytes;
    if (!sp::encode(storage.view(), sp::input_values{std::span(input)}, &coverage.output,
                     execution)) {
        fail("narrow encode", storage.description, storage.length);
    }
    storage.compare(expected, "narrow encode canonical bytes/slack/gaps");
    coverage.check(storage, before, true);
    check_bound_encode(storage, sp::input_values{std::span(input)}, expected);
}

void check_case(sp::description desc, std::size_t length, bool strided) {
    fixture storage(desc, length, strided);
    auto view = storage.view();
    std::vector<std::uint64_t> values(length + 3, ~std::uint64_t{0});
    for (std::size_t i = 0; i < length; ++i) values[i] = random_bits() & mask(desc.width);
    if (length > 0) values[0] = mask(desc.width);
    if (length > 1) values[1] = 0;
    if (desc.width == 64 && length > 2) values[2] = std::uint64_t{1} << 63;
    auto expected = storage.bytes;
    for (std::size_t i = 0; i < storage.tiles * storage.values_per_tile; ++i) {
        storage.oracle_set(expected, i, i < length ? values[i] : 0);
    }
    effects construction(8 * (storage.tiles * storage.values_per_tile + storage.tiles + 16));
    const auto before = storage.bytes;
    const auto encoded = sp::encode(view, sp::input_values{std::span(values)},
                                    &construction.output, execution);
    if (!encoded) fail("encode", desc, length);
    storage.compare(expected, "encode canonical bytes/slack/gaps");
    construction.check(storage, before, true);
    if (length == 0 && construction.output.size != 1) fail("empty encode effects", desc, length);
    check_bound_encode(storage, sp::input_values{std::span(values)}, expected);
    check_readers(view.as_const(), values);
    if (desc.width <= 8) check_encode_type<std::uint8_t>(storage, values, expected);
    else if (desc.width <= 16) check_encode_type<std::uint16_t>(storage, values, expected);
    else if (desc.width <= 32) check_encode_type<std::uint32_t>(storage, values, expected);

    // A source carrier need not represent the whole destination domain. In
    // particular, headed u8 inputs exercise per-byte shifts in x86 adapters,
    // which an oracle using only carriers wide enough for K would miss.
    const auto smaller_source = [&]<class UInt> {
        auto small = values;
        for (auto& value : small) value &= mask(sizeof(UInt) * 8);
        auto small_expected = expected;
        for (std::size_t i = 0; i < storage.tiles * storage.values_per_tile; ++i)
            storage.oracle_set(small_expected, i, i < length ? small[i] : 0);
        check_encode_type<UInt>(storage, small, small_expected);
    };
    if (desc.width > 8) smaller_source.template operator()<std::uint8_t>();
    if (desc.width > 16) smaller_source.template operator()<std::uint16_t>();
    if (desc.width > 32) smaller_source.template operator()<std::uint32_t>();
    if (desc.width > 8) check_bound_encode(storage, sp::input_values{std::span(values)}, expected);

    if (length != 0) {
        std::vector<std::size_t> positions{0, length / 2, length - 1};
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
        for (auto index : positions) {
            for (auto value : {std::uint64_t{0}, mask(desc.width), random_bits() & mask(desc.width)}) {
                effects changed(32);
                const auto prior = storage.bytes;
                if (!sp::set(view, index, value, &changed.output)) fail("set", desc, length, index);
                storage.oracle_set(expected, index, value);
                values[index] = value;
                storage.compare(expected, "set preserves other bits/heads/gaps");
                changed.check(storage, prior, false);
                ++writes;
            }
        }
    }

    const sp::index_range rows{length >= 6 ? 3U : 0U, length > 1 ? length - 1 : length};
    const std::size_t origin = rows.begin == 0 ? 0 : rows.begin - 1;
    const std::size_t count = rows.end - origin + 2;
    std::vector<std::uint64_t> words(count / 64 + (count % 64 != 0), ~std::uint64_t{0});
    std::vector<std::uint64_t> source(rows.size() + 2, ~std::uint64_t{0});
    for (std::size_t i = rows.begin; i < rows.end; ++i) {
        if (i % 3 == 0 || i == rows.end - 1) {
            source[i - rows.begin] = random_bits() & mask(desc.width);
        } else {
            const auto bit = i - origin;
            words[bit / 64] &= ~(std::uint64_t{1} << (bit % 64));
        }
    }
    const auto selected = sp::selection::bitmap(origin, count, words);
    effects selected_effects(8 * (rows.size() + 16));
    const auto before_write = storage.bytes;
    if (!sp::write(view, rows, sp::input_values{std::span(source)}, selected, &selected_effects.output)) {
        fail("selected write", desc, length);
    }
    for (std::size_t i = rows.begin; i < rows.end; ++i) {
        const auto bit = i - origin;
        if ((words[bit / 64] >> (bit % 64)) & 1) {
            values[i] = source[i - rows.begin];
            storage.oracle_set(expected, i, values[i]);
        }
    }
    storage.compare(expected, "selected original-index/source-slot mapping");
    selected_effects.check(storage, before_write, false);
    check_readers(view.as_const(), values);
    ++writes;

    if (desc.width < 64 && !rows.empty()) {
        source[rows.size() - 1] = std::uint64_t{1} << desc.width;
        effects rejected(8 * (rows.size() + 16));
        const auto prior_records = rejected.records;
        const auto status = sp::write(view, rows, sp::input_values{std::span(source)},
                                       selected, &rejected.output);
        if (status || status.error() != sp::error::invalid_value) fail("late selected value accepted", desc, length);
        storage.compare(expected, "late rejection changed destination");
        rejected.unchanged(prior_records);
    }
    std::fill(words.begin(), words.end(), 0);
    std::fill(source.begin(), source.end(), ~std::uint64_t{0});
    effects empty_effects(8 * (rows.size() + 16));
    const auto empty_records = empty_effects.records;
    if (!sp::write(view, rows, sp::input_values{std::span(source)}, selected, &empty_effects.output)) {
        fail("empty selection validates unselected values", desc, length);
    }
    storage.compare(expected, "empty selection changed bytes");
    empty_effects.unchanged(empty_records);
    ++placements;
}

void check_append(sp::description desc) {
    // The larger unpublished view uses already initialized spare lanes. Old
    // readers keep their old logical bound; this test owns storage exclusively.
    const std::size_t tile_values = desc.storage == sp::geometry::local8 ? 8
        : 32 * (8 / std::gcd((desc.width - desc.head_bits) % 8, 8U));
    const std::size_t old_length = tile_values + 2, new_length = tile_values + 5;
    fixture storage(desc, new_length, true);
    auto old_view = storage.view(old_length);
    std::vector<std::uint64_t> old_values(old_length);
    for (auto& value : old_values) value = random_bits() & mask(desc.width);
    if (!sp::encode(old_view, sp::input_values{std::span(old_values)}, nullptr,
                    execution)) fail("append seed", desc, old_length);
    auto expected = storage.bytes;
    auto staged = storage.view();
    std::array<std::uint64_t, 3> appended{mask(desc.width), 0, random_bits() & mask(desc.width)};
    effects coverage(32);
    const auto before = storage.bytes;
    if (!sp::write(staged, {old_length, new_length}, sp::input_values{std::span(appended)},
                   sp::selection::all(), &coverage.output)) fail("append write", desc, new_length);
    for (std::size_t i = old_length; i < new_length; ++i) {
        storage.oracle_set(expected, i, appended[i - old_length]);
    }
    storage.compare(expected, "append canonical bytes/slack");
    coverage.check(storage, before, false);
    if (sp::get(old_view.as_const(), old_length)) fail("old read extent grew", desc, old_length);
    old_values.insert(old_values.end(), appended.begin(), appended.end());
    check_readers(staged.as_const(), old_values);
    ++appends;
}

void check_description(sp::description description) {
    const std::size_t values = description.storage == sp::geometry::local8 ? 8
        : 32 * (8 / std::gcd((description.width - description.head_bits) % 8, 8U));
    for (auto length : {std::size_t{0}, std::size_t{1}, values - 1, values, values + 1, 2 * values + 3}) {
        check_case(description, length, false);
        check_case(description, length, true);
    }
    if (description.storage == sp::geometry::local8) {
        // Cross the dense region boundaries as well as the eight-value wire
        // tiles, including a partial final region and non-dense placement.
        for (std::size_t length : {31, 32, 33, 63, 64, 65, 127, 128, 129}) {
            check_case(description, length, false);
            check_case(description, length, true);
        }
    }
    check_append(description);
    ++descriptions;
}

} // namespace

void run_target() {
    descriptions = placements = reads = writes = appends = 0;
    for (unsigned width = 1; width <= 64; ++width) {
        for (unsigned head : {0U, 8U, 16U}) {
            if (head > width) continue;
            check_description({width, head, sp::geometry::local8});
            if (striped_width(width - head)) check_description({width, head, sp::geometry::striped});
        }
    }
    std::printf("SeriesPack public wire (%s): %zu descriptions, %zu placements, %zu "
                "range-read checks, %zu mutation checks, %zu append scenarios passed\n",
                target_name, descriptions, placements, reads, writes, appends);
}

int main(int argc, char** argv) {
    const bool all = argc == 2 && std::strcmp(argv[1], "--all-available") == 0;
    if (argc != 1 && !all) {
        if (argc != 3 || std::strcmp(argv[1], "--target") != 0) {
            std::fprintf(stderr, "usage: %s [--all-available | --target scalar|avx2|avx512|neon]\n", argv[0]);
            return 2;
        }
        target_name = argv[2];
        if (std::strcmp(target_name, "scalar") == 0) execution = sp::execution_target::scalar;
        else if (std::strcmp(target_name, "avx2") == 0) execution = sp::execution_target::avx2;
        else if (std::strcmp(target_name, "avx512") == 0) execution = sp::execution_target::avx512;
        else if (std::strcmp(target_name, "neon") == 0) execution = sp::execution_target::neon;
        else {
            std::fprintf(stderr, "unknown target: %s\n", target_name);
            return 2;
        }
    }
    if (!all) {
        run_target();
        return 0;
    }
    fixture admission({1, 0, sp::geometry::local8}, 0, false);
    const auto view = admission.view();
    for (auto target : {sp::execution_target::scalar, sp::execution_target::avx2,
                        sp::execution_target::avx512, sp::execution_target::neon}) {
        const auto bound = sp::bind_reader(view.as_const(), target);
        if (!bound) {
            if (bound.error() != sp::error::unsupported)
                fail("target admission unexpected error", view.layout(), 0);
            continue;
        }
        if (bound->target() != target) fail("target admission silently changed target", view.layout(), 0);
        execution = target;
        target_name = target == sp::execution_target::scalar ? "scalar" :
            target == sp::execution_target::avx2 ? "avx2" :
            target == sp::execution_target::avx512 ? "avx512" : "neon";
        run_target();
    }
}

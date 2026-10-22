// Independent logical values exercise every delta-exception reader path.
#include "compact_read.h"
#include <array>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
namespace fe = frame_encoding;
namespace fp = frame_points;
using fe::u64;
void need(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }

std::array<u64, 256> original(unsigned width, std::span<const std::uint8_t> positions) {
    std::array<bool, 256> excluded{};
    for (auto p : positions) excluded[p] = true;
    std::array<u64, 256> values;
    const u64 anchor = u64{1} << 63;
    unsigned chain = 0;
    for (unsigned row = 0; row < 256; ++row) {
        if (excluded[row]) values[row] = row % 2 ? UINT64_MAX - row : 0;
        else {
            if (width == 1) values[row] = anchor + (chain + 1) / 2;
            else values[row] = anchor + (chain % 2 ? (u64{1} << (width - 1)) - 1 : 0);
            ++chain;
        }
    }
    return values;
}

// Build the published layout using real SeriesPack packing. Expected values
// come from original(), not from the reader's prefix/step-count formula.
fe::column literal(const std::array<u64, 256> &values,
                   std::span<const std::uint8_t> positions, fe::checkpoints mode) {
    need(!positions.empty() && positions.size() < 256, "invalid authored exception count");
    std::array<bool, 256> excluded{};
    for (auto row : positions) {
        need(!excluded[row], "duplicate authored exception");
        excluded[row] = true;
    }
    need(std::is_sorted(positions.begin(), positions.end()), "unordered authored positions");
    std::array<fe::i64, 256> steps{};
    std::array<u64, 256> residuals{};
    u64 anchor = 0, previous = 0;
    fe::i64 rbase = std::numeric_limits<fe::i64>::max();
    unsigned chain = 0;
    for (unsigned row = 0; row < 256; ++row) if (!excluded[row]) {
        if (!chain) anchor = values[row];
        else {
            const fe::i128 step = fe::i128(values[row]) - previous;
            need(step >= std::numeric_limits<fe::i64>::min() &&
                     step <= std::numeric_limits<fe::i64>::max(), "authored step outside law");
            steps[row] = static_cast<fe::i64>(step);
            rbase = std::min(rbase, steps[row]);
        }
        previous = values[row]; ++chain;
    }
    if (chain == 1) rbase = 0;
    unsigned rank = 0;
    u64 largest = 0;
    for (unsigned row = 0; row < 256; ++row) if (!excluded[row]) {
        if (rank++) residuals[row] = static_cast<u64>(fe::i128(steps[row]) - rbase);
        largest = std::max(largest, residuals[row]);
    }
    const unsigned k = std::max(1u, fe::width_of(largest));
    const u64 lo = *std::min_element(values.begin(), values.end());
    std::vector<u64> exceptions;
    for (auto row : positions) exceptions.push_back(values[row] - lo);
    const unsigned w = std::max(1u, fe::width_of(*std::max_element(exceptions.begin(), exceptions.end())));
    const auto plane = fe::plane_bytes(k, 256), cp_bytes = fe::checkpoint_cost(mode, k);
    fe::column c;
    c.options = {false, mode, true};
    c.rows = 256;
    c.blocks.push_back({anchor, 0, 256, fe::kind::delta_patched, static_cast<std::uint8_t>(k)});
    c.payload.resize(plane + 8 + cp_bytes + 10 + positions.size() + fe::plane_bytes(w, positions.size()));
    fe::pack(k, residuals, c.payload.data());
    std::memcpy(c.payload.data() + plane, &rbase, 8);
    if (mode != fe::checkpoints::none) {
        const unsigned spacing = mode == fe::checkpoints::every64 ? 64 : 16;
        std::vector<u64> marks;
        u64 sum = 0;
        for (unsigned row = 0; row < 256; ++row) {
            sum += residuals[row]; // Deliberate modulo-u64 checkpoint law.
            if ((row + 1) % spacing == 0 && row < 255) marks.push_back(sum);
        }
        auto *at = c.payload.data() + plane + 8;
        if (mode == fe::checkpoints::packed && k + 8 <= 64) fe::pack(k + 8, marks, at);
        else {
            const unsigned bytes = k + 8 <= 32 ? 4 : 8;
            for (unsigned j = 0; j < marks.size(); ++j) {
                if (bytes == 4) {
                    const auto value = static_cast<std::uint32_t>(marks[j]);
                    std::memcpy(at + j * bytes, &value, bytes);
                } else std::memcpy(at + j * bytes, &marks[j], bytes);
            }
        }
    }
    auto *tail = c.payload.data() + plane + 8 + cp_bytes;
    std::memcpy(tail, &lo, 8);
    tail[8] = static_cast<std::uint8_t>(positions.size());
    tail[9] = static_cast<std::uint8_t>(w);
    std::memcpy(tail + 10, positions.data(), positions.size());
    fe::pack(w, exceptions, tail + 10 + positions.size());
    auto plane_decoder = fe::sp::bind_decoder<u64>(fe::sp::dense_input{
        k, fe::sp::geometry::local, std::span(c.payload.data(), plane), 256});
    auto exception_decoder = fe::sp::bind_decoder<u64>(fe::sp::dense_input{
        w, fe::sp::geometry::local,
        std::span<const std::uint8_t>(tail + 10 + positions.size(), fe::plane_bytes(w, positions.size())),
        positions.size()});
    need(plane_decoder && exception_decoder, "literal decoder binding failed");
    c.decoders.push_back(*plane_decoder);
    c.exceptions.emplace_back(*exception_decoder);
    c.runs.resize(1); c.d8.resize(1); c.d16.resize(1); c.d32.resize(1);
    return c;
}

std::size_t check(unsigned width, const std::vector<std::uint8_t> &positions) {
    const auto values = original(width, positions);
    std::size_t comparisons = 0;
    for (auto mode : {fe::checkpoints::native, fe::checkpoints::packed,
                      fe::checkpoints::every64, fe::checkpoints::none}) {
        const auto c = literal(values, positions, mode);
        const fp::compact_image lean(literal(values, positions, mode));
        need(lean.body.payload == c.payload, "preparation changed literal bytes");
        fp::point_reader eager(c);
        fp::compact_reader scalar(lean), native(lean, true);
        std::array<u64, 256> decoded;
        fe::decode_block(c, 0, decoded.data());
        fe::u128 sum = 0;
        for (unsigned row = 0; row < 256; ++row) {
            need(decoded[row] == values[row], "owner decode differs from original logical values");
            need(eager.get(row) == values[row], "eager exception point differs");
            need(scalar.get(row) == values[row], "shared exception point differs");
            need(native.get(row) == values[row], "native exception point differs");
            sum += values[row]; comparisons += 4;
        }
        need(fe::block_sum(c, 0) == sum, "owner exception SUM differs from exact reference");
        for (u64 rhs : {u64{0}, u64{1} << 63, UINT64_MAX}) {
            u64 expected = 0;
            for (auto value : values) expected += value >= rhs;
            need(fe::block_count_ge(c, 0, rhs) == expected, "owner exception COUNT differs");
        }
        const std::array<std::size_t, 10> rows{0, 1, 15, 16, 17, 63, 64, 65, 254, 255};
        for (bool selected : {false, true}) {
            fe::block_mask mask(1);
            if (selected) mask.set(0);
            unsigned n = 0;
            fp::selected_points(native, mask, rows, [&](auto row, auto value) {
                need(value == values[row], "exception point changed original coordinate"); ++n;
            });
            need(n == (selected ? rows.size() : 0), "exception coarse selection differs");
        }
    }
    return comparisons;
}
} // namespace
int main() {
    try {
        std::size_t comparisons = 0;
        const std::vector<std::uint8_t> mixed{0, 1, 15, 16, 17, 63, 64, 127, 128, 239, 240, 255};
        for (unsigned k = 1; k <= 64; ++k) comparisons += check(k, mixed);
        std::vector<std::vector<std::uint8_t>> shapes{{0}, {255}, {0, 1, 2}, {15, 16, 17},
                                                    {63, 64, 65}, {127, 128, 129}, {239, 240, 254, 255}};
        std::vector<std::uint8_t> alternating, leading, trailing;
        for (unsigned row = 0; row < 256; ++row) {
            if (!(row % 2)) alternating.push_back(row);
            if (row < 255) leading.push_back(row);
            if (row > 0) trailing.push_back(row);
        }
        shapes.push_back(alternating); shapes.push_back(leading); shapes.push_back(trailing);
        for (const auto &shape : shapes) comparisons += check(64, shape);
        std::printf("%zu exact delta-exception values pass: residual widths1..64, four checkpoint "
                    "modes, first/last/consecutive/alternating/255 exceptions, skipped chain, "
                    "wide wrapping checkpoints, coarse coordinates, exact SUM/COUNT.\n", comparisons);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what()); return 1;
    }
}

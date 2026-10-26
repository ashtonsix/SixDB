#include <ikea/frame/reader.h>
#include <ikea/frame/detail/image_state.h>
#include <algorithm>
#include <cassert>
#include <limits>

namespace ikea::frame {
namespace {
bool valid_comparison(comparison op) noexcept {
    return op == comparison::equal || op == comparison::less || op == comparison::less_equal ||
           op == comparison::greater || op == comparison::greater_equal;
}
enum class predicate_truth { unknown, always, never };
predicate_truth domain_truth(comparison op, std::uint64_t threshold) noexcept {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if ((op == comparison::greater_equal && threshold == 0) ||
        (op == comparison::less_equal && threshold == maximum)) return predicate_truth::always;
    if ((op == comparison::less && threshold == 0) ||
        (op == comparison::greater && threshold == maximum)) return predicate_truth::never;
    return predicate_truth::unknown;
}
}
std::string_view describe(error e) noexcept {
    switch (e) {
    case error::range: return "operation coordinates or comparison are invalid";
    case error::description: return "invalid Frame description";
    case error::contents: return "image violates its representation or evidence law";
    case error::unsupported: return "described shape has no executable implementation";
    case error::unavailable: return "required section is unavailable";
    case error::capacity: return "output does not cover the complete result";
    case error::resources: return "resource allocation refused";
    case error::authority: return "required read authority is absent";
    }
    return "unknown Frame error";
}
reader bind_reader(const validated_image& image) {
    return reader(image);
}
std::expected<reader, error> bind_reader(const description& d, image_view view) {
    const auto image = validate_image(d, view);
    if (!image) return std::unexpected(image.error());
    return bind_reader(*image);
}
std::size_t reader::size() const noexcept { return state_->rows_; }
std::span<const section_key> reader::available_sections() const noexcept { return state_->available_; }
std::expected<std::vector<section_key>, error> reader::required(std::span<const row_range> ranges) const {
    auto keys = required_sections(state_->description_, size(), state_->streams_, ranges);
    if (!keys) {
        if (keys.error() == manifest_error::resources) return std::unexpected(error::resources);
        return std::unexpected(error::range);
    }
    return std::move(*keys);
}
std::expected<bitset256, error> reader::blocks(ikea::intervals::selection selected) const {
    const auto n = static_cast<unsigned>((size() + block_rows - 1) / block_rows);
    if (!selected.covers(0, n)) return std::unexpected(error::range);
    bitset256 result;
    for (unsigned first = 0; first < n; first += 64)
        result.words[first / 64] = selected.mask(first, std::min(64u, n - first));
    return result;
}
std::expected<void, error> reader::admit_range(std::size_t first, std::size_t count) const {
    if (first > size() || count > size() - first) return std::unexpected(error::range);
    if (!count) return {};
    for (auto group = first / 4096; group <= (first + count - 1) / 4096; ++group)
        if (!(state_->ready_groups_ & (1u << group))) return std::unexpected(error::unavailable);
    return {};
}
std::expected<void, error> reader::admit_blocks(const bitset256& selected) const {
    bool ready = true;
    selected.for_each([&](unsigned b) { ready &= bool(state_->ready_groups_ & (1u << (b / group_blocks))); });
    if (!ready) return std::unexpected(error::unavailable);
    return {};
}
std::expected<std::uint64_t, error> reader::get(std::size_t row) const {
    if (auto admitted = admit_range(row, 1); !admitted) return std::unexpected(admitted.error());
    return state_->root_->get(0, static_cast<unsigned>(row));
}
std::expected<void, error> reader::read(std::size_t first, std::span<std::uint64_t> output) const {
    if (auto admitted = admit_range(first, output.size()); !admitted) return admitted;
    if (!output.empty()) state_->root_->read(0, static_cast<unsigned>(first), static_cast<unsigned>(output.size()), output.data());
    return {};
}
std::expected<u128, error> reader::sum(ikea::intervals::selection selected) const {
    const auto active = blocks(selected);
    if (!active) return std::unexpected(active.error());
    if (auto admitted = admit_blocks(*active); !admitted) return std::unexpected(admitted.error());
    return active->empty() ? u128{0} : state_->root_->sum(0, *active);
}
std::expected<std::uint64_t, error> reader::count(comparison op, std::uint64_t threshold,
    ikea::intervals::selection selected) const {
    if (!valid_comparison(op)) return std::unexpected(error::range);
    const auto active = blocks(selected);
    if (!active) return std::unexpected(active.error());
    const auto truth = domain_truth(op, threshold);
    if (truth == predicate_truth::never) return std::uint64_t{0};
    if (truth == predicate_truth::always) {
        std::uint64_t count = 0;
        active->for_each([&](unsigned b) { count += std::min(std::size_t{block_rows}, size() - std::size_t{b} * block_rows); });
        return count;
    }
    if (auto admitted = admit_blocks(*active); !admitted) return std::unexpected(admitted.error());
    return active->empty() ? std::uint64_t{0} : state_->root_->count_uniform(op, 0, *active, threshold);
}
std::expected<void, error> reader::filter(comparison op, std::uint64_t threshold,
    std::span<bitset256> output, ikea::intervals::selection selected) const {
    if (!valid_comparison(op)) return std::unexpected(error::range);
    const auto active = blocks(selected);
    if (!active) return std::unexpected(active.error());
    bool capacity = true;
    active->for_each([&](unsigned b) { capacity &= b < output.size(); });
    if (!capacity) return std::unexpected(error::capacity);
    const auto truth = domain_truth(op, threshold);
    if (truth != predicate_truth::unknown) {
        active->for_each([&](unsigned b) {
            const auto n = static_cast<unsigned>(std::min(std::size_t{block_rows}, size() - std::size_t{b} * block_rows));
            output[b] = truth == predicate_truth::always ? bitset256::first_n(n) : bitset256{};
        });
        return {};
    }
    if (auto admitted = admit_blocks(*active); !admitted) return admitted;
    if (active->empty()) return {};
    block_args thresholds;
    thresholds.fill(threshold);
    state_->root_->filter(op, 0, *active, thresholds, output);
    active->for_each([&](unsigned b) {
        const auto n = static_cast<unsigned>(std::min(std::size_t{block_rows}, size() - std::size_t{b} * block_rows));
        output[b] = output[b] & bitset256::first_n(n);
    });
    return {};
}
std::expected<reader::fine_request, error> reader::selected_rows(const bitset256& coarse,
    ikea::seriespack::selection selected) const {
    fine_request result{coarse, {}, 0};
    bool covered = true;
    coarse.for_each([&](unsigned b) {
        const auto first = std::size_t{b} * block_rows;
        const auto n = static_cast<unsigned>(std::min(std::size_t{block_rows}, size() - first));
        if (!selected.covers(first, n)) { covered = false; return; }
        for (unsigned r = 0; r < n; r += 64)
            result.rows[b].words[r / 64] = selected.mask(first + r, std::min(64u, n - r));
        if (result.rows[b].empty()) result.active.set(b, false);
        result.count += result.rows[b].count();
    });
    if (!covered) return std::unexpected(error::range);
    return result;
}
std::expected<u128, error> reader::sum_selected(ikea::seriespack::selection rows,
    ikea::intervals::selection selected) const {
    const auto coarse = blocks(selected);
    if (!coarse) return std::unexpected(coarse.error());
    const auto fine = selected_rows(*coarse, rows);
    if (!fine) return std::unexpected(fine.error());
    if (auto admitted = admit_blocks(fine->active); !admitted) return std::unexpected(admitted.error());
    return !fine->count ? u128{0} : state_->root_->sum_selected(0, fine->active, fine->rows);
}
std::expected<summary, error> reader::summarise_selected(ikea::seriespack::selection rows,
    ikea::intervals::selection selected) const {
    const auto coarse = blocks(selected);
    if (!coarse) return std::unexpected(coarse.error());
    const auto fine = selected_rows(*coarse, rows);
    if (!fine) return std::unexpected(fine.error());
    if (auto admitted = admit_blocks(fine->active); !admitted) return std::unexpected(admitted.error());
    summary result;
    if (fine->count) state_->root_->summarise_selected(0, fine->active, fine->rows, result);
    return result;
}
std::expected<std::size_t, error> reader::select(ikea::seriespack::selection rows,
    std::span<std::uint64_t> output, ikea::intervals::selection selected) const {
    const auto coarse = blocks(selected);
    if (!coarse) return std::unexpected(coarse.error());
    const auto fine = selected_rows(*coarse, rows);
    if (!fine) return std::unexpected(fine.error());
    if (fine->count > output.size()) return std::unexpected(error::capacity);
    if (auto admitted = admit_blocks(fine->active); !admitted) return std::unexpected(admitted.error());
    return !fine->count ? std::size_t{0} : state_->root_->select(0, fine->active, fine->rows, output.data());
}
} // namespace ikea::frame

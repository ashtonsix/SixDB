#include "stream_binding.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>

namespace frame3_owner {
namespace {
binding_error translate(manifest_error e) noexcept {
    switch (e) {
    case manifest_error::description: return binding_error::description;
    case manifest_error::unsupported: return binding_error::unsupported;
    case manifest_error::range: return binding_error::range;
    case manifest_error::unavailable: return binding_error::unavailable;
    case manifest_error::resources: return binding_error::resources;
    default: return binding_error::image;
    }
}
image_preset image_layout(pack_preset preset) noexcept {
    switch (preset) {
    case pack_preset::compact: return image_preset::compact;
    case pack_preset::bulk_x86: return image_preset::bulk_x86;
    case pack_preset::bulk_arm: return image_preset::bulk_arm;
    }
    return image_preset::compact;
}
bool key_less(section_key a, section_key b) noexcept {
    return a.root_node < b.root_node || (a.root_node == b.root_node && a.group < b.group);
}
const stream_manifest *find_stream(std::span<const stream_manifest> streams, std::uint16_t node) noexcept {
    for (const auto &s : streams) if (s.root_node == node) return &s;
    return nullptr;
}
frame3::u64 little_u64(std::span<const std::byte> bytes, std::size_t at) noexcept {
    frame3::u64 value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= frame3::u64{std::to_integer<unsigned>(bytes[at + i])} << (i * 8);
    return value;
}
std::size_t blocks_in_group(std::size_t rows, std::size_t group) noexcept {
    return (std::min(std::size_t{4096}, rows - group * 4096) + 255) / 256;
}
bool comparison_valid(ikea::comparison op) noexcept {
    return op == ikea::comparison::equal || op == ikea::comparison::less ||
        op == ikea::comparison::less_equal || op == ikea::comparison::greater || op == ikea::comparison::greater_equal;
}
enum class predicate_truth { unknown, always, never };
predicate_truth domain_truth(ikea::comparison op, frame3::u64 threshold) noexcept {
    constexpr auto maximum = std::numeric_limits<frame3::u64>::max();
    if ((op == ikea::comparison::greater_equal && threshold == 0) ||
        (op == ikea::comparison::less_equal && threshold == maximum)) return predicate_truth::always;
    if ((op == ikea::comparison::less && threshold == 0) ||
        (op == ikea::comparison::greater && threshold == maximum)) return predicate_truth::never;
    return predicate_truth::unknown;
}
} // namespace

std::expected<std::unique_ptr<stream_binding>, binding_error> stream_binding::attach(const description &d,
    std::size_t rows, std::span<const std::size_t> extents, std::span<const stream_manifest> streams,
    std::span<const borrowed_part> parts, std::span<const section_key> available) try {
    if (const auto valid = validate_manifest(d, rows, extents, streams); !valid)
        return std::unexpected(translate(valid.error()));
    // Check every provided Part/key before resolving even the first section.
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].owner_part >= extents.size() || parts[i].bytes.size() != extents[parts[i].owner_part] ||
            (!parts[i].bytes.data() && !parts[i].bytes.empty())) return std::unexpected(binding_error::range);
        for (std::size_t j = 0; j < i; ++j)
            if (parts[i].owner_part == parts[j].owner_part) return std::unexpected(binding_error::range);
    }
    for (auto key : available) {
        const auto *stream = find_stream(streams, key.root_node);
        if (!stream || key.group >= stream->groups.size()) return std::unexpected(binding_error::range);
        const auto part = stream->groups[key.group].owner_part;
        if (std::none_of(parts.begin(), parts.end(), [&](const auto &p) { return p.owner_part == part; }))
            return std::unexpected(binding_error::unavailable);
    }
    auto root = make_tree(d);
    if (!root) return std::unexpected(root.error());
    auto result = std::unique_ptr<stream_binding>(new stream_binding);
    result->root_ = std::move(*root);
    result->description_ = d;
    result->streams_.assign(streams.begin(), streams.end());
    result->available_.assign(available.begin(), available.end());
    std::sort(result->available_.begin(), result->available_.end(), key_less);
    result->available_.erase(std::unique(result->available_.begin(), result->available_.end()), result->available_.end());
    result->rows_ = rows;
    const bool intervals = is_intervals(d.nodes[d.root].kind);
    const bool relative = d.nodes[d.root].kind == node_kind::intervals_relative_ends;
    const auto delta_node = intervals ? d.nodes[d.root].children[0].child : d.root;
    const auto delta_recipe = binding_recipe(d, delta_node);
    if (!delta_recipe) return std::unexpected(binding_error::unsupported);
    const auto checkpoint_node = intervals ? d.nodes[d.root].children[1].child : d.root;
    // Section spans are resolved only after all metadata/availability checks.
    const auto section = [&](section_key key) {
        const auto &location = find_stream(streams, key.root_node)->groups[key.group];
        const auto p = std::find_if(parts.begin(), parts.end(), [&](const auto &part) { return part.owner_part == location.owner_part; });
        return p->bytes.subspan(location.offset, location.extent);
    };
    std::array<std::span<const std::byte>, 16> root_sections{};
    // Structural admission of every available child section precedes trusted
    // stage attachment; no absent section has an address formed here.
    for (auto key : result->available_) {
        const auto image = section(key);
        if (key.root_node == d.root) {
            const auto prefix = intervals ? blocks_in_group(rows, key.group) * 10 : 0;
            if (prefix > image.size()) return std::unexpected(binding_error::image);
            for (std::size_t at = 0; at < prefix; at += 10)
                if (std::to_integer<unsigned>(image[at + 8]) > 1 || std::to_integer<unsigned>(image[at + 9]) > 1)
                    return std::unexpected(binding_error::image);
            if (!validate_group(image.subspan(prefix), image_layout(delta_recipe->preset), delta_recipe->bounded,
                rows, static_cast<unsigned>(key.group))) return std::unexpected(binding_error::image);
            root_sections[key.group] = image;
        } else {
            const auto recipe = binding_recipe(d, checkpoint_node);
            if (!recipe) return std::unexpected(binding_error::unsupported);
            const auto *stream = find_stream(streams, key.root_node);
            if (!validate_group(image, image_layout(recipe->preset), recipe->bounded, stream->rows,
                static_cast<unsigned>(key.group))) return std::unexpected(binding_error::image);
        }
    }
    for (auto key : result->available_) {
        const auto *stream = find_stream(streams, key.root_node);
        auto *target = key.root_node == d.root ? result->root_.get() : result->root_->children()[1].get();
        const auto image = section(key);
        if (target->attach(0, stream->rows, static_cast<unsigned>(key.group), image, {}) != image.size())
            return std::unexpected(binding_error::image);
    }
    for (std::size_t g = 0; g < root_sections.size(); ++g) {
        if (root_sections[g].empty()) continue;
        const std::array range{row_range{g * 4096, std::min(std::size_t{4096}, rows - g * 4096)}};
        const auto closure = result->required(range);
        if (!closure) return std::unexpected(closure.error());
        if (!admit_sections(*closure, result->available_)) continue;
        if (intervals) {
            alignas(64) frame3::u64 values[256];
            for (std::size_t b = 0; b < blocks_in_group(rows, g); ++b) {
                const auto block = static_cast<unsigned>(g * 16 + b);
                const auto count = static_cast<unsigned>(std::min(std::size_t{256}, rows - std::size_t{block} * 256));
                result->root_->decode(0, block, values); // Never count/get/sum: no evidence shortcut.
                const auto image = root_sections[g];
                if (little_u64(image, b * 10) != values[count - 1]) return std::unexpected(binding_error::image);
                const bool monotone = std::to_integer<unsigned>(image[b * 10 + 8]);
                const bool exact = std::to_integer<unsigned>(image[b * 10 + 9]);
                if (monotone && !std::is_sorted(values, values + count)) return std::unexpected(binding_error::image);
                constexpr auto bias = frame3::u64{1} << 63;
                const unsigned spacing = d.nodes[d.root].parameter;
                // Decode ignores the field at every checkpoint row. The
                // relative-ends SUM law includes it, so decoded logical values
                // alone cannot establish that law's zero-offset premise.
                for (unsigned i = 0; i < count; i += spacing)
                    if (result->root_->children()[0]->get(0, block * 256 + i) != bias)
                        return std::unexpected(binding_error::image);
                if (exact) {
                    for (unsigned i = 1; i < count; ++i) {
                        const auto reference = values[relative ? i - i % spacing : i - 1];
                        if (values[i] >= reference ? values[i] - reference >= bias : reference - values[i] > bias)
                            return std::unexpected(binding_error::image);
                    }
                }
            }
        }
        result->ready_groups_ |= static_cast<std::uint16_t>(1u << g);
    }
    return result;
} catch (const std::bad_alloc &) { return std::unexpected(binding_error::resources); }

std::expected<std::vector<section_key>, binding_error> stream_binding::required(std::span<const row_range> ranges) const {
    auto keys = required_sections(description_, rows_, streams_, ranges);
    if (!keys) return std::unexpected(translate(keys.error()));
    return std::move(*keys);
}
std::expected<void, binding_error> stream_binding::admit_range(std::size_t first, std::size_t count) const {
    if (first > rows_ || count > rows_ - first) return std::unexpected(binding_error::range);
    if (!count) return {};
    for (auto g = first / 4096; g <= (first + count - 1) / 4096; ++g)
        if (!(ready_groups_ & (1u << g))) return std::unexpected(binding_error::unavailable);
    return {};
}
std::expected<void, binding_error> stream_binding::valid_blocks(const frame3::bitset256 &blocks) const {
    const auto legal = frame3::first_n(static_cast<unsigned>((rows_ + 255) / 256));
    if (!(blocks == frame3::operator&(blocks, legal))) return std::unexpected(binding_error::range);
    return {};
}
std::expected<void, binding_error> stream_binding::admit_blocks(const frame3::bitset256 &blocks) const {
    if (auto valid = valid_blocks(blocks); !valid) return valid;
    bool ready = true;
    frame3::each(blocks, [&](unsigned b) { ready &= bool(ready_groups_ & (1u << (b / 16))); });
    if (!ready) return std::unexpected(binding_error::unavailable);
    return {};
}
std::expected<frame3::u64, binding_error> stream_binding::get(unsigned row) const {
    if (auto admitted = admit_range(row, 1); !admitted) return std::unexpected(admitted.error());
    return root_->get(0, row);
}
std::expected<void, binding_error> stream_binding::read(unsigned first, std::span<frame3::u64> output) const {
    if (auto admitted = admit_range(first, output.size()); !admitted) return admitted;
    if (!output.empty()) root_->read(0, first, static_cast<unsigned>(output.size()), output.data());
    return {};
}
std::expected<frame3::u128, binding_error> stream_binding::sum(const frame3::bitset256 &blocks) const {
    if (auto admitted = admit_blocks(blocks); !admitted) return std::unexpected(admitted.error());
    return blocks.empty() ? frame3::u128{0} : root_->sum(0, blocks);
}
std::expected<frame3::u64, binding_error> stream_binding::count(ikea::comparison op,
    const frame3::bitset256 &blocks, frame3::u64 threshold) const {
    if (!comparison_valid(op)) return std::unexpected(binding_error::range);
    if (auto valid = valid_blocks(blocks); !valid) return std::unexpected(valid.error());
    const auto truth = domain_truth(op, threshold);
    if (truth == predicate_truth::never) return frame3::u64{0};
    if (truth == predicate_truth::always) {
        frame3::u64 count = 0;
        frame3::each(blocks, [&](unsigned b) { count += std::min(std::size_t{256}, rows_ - std::size_t{b} * 256); });
        return count;
    }
    if (auto admitted = admit_blocks(blocks); !admitted) return std::unexpected(admitted.error());
    return blocks.empty() ? frame3::u64{0} : root_->count_uniform(op, 0, blocks, threshold);
}
std::expected<void, binding_error> stream_binding::filter(ikea::comparison op, const frame3::bitset256 &blocks,
    frame3::u64 threshold, frame3::block_rows_set &output) const {
    if (!comparison_valid(op)) return std::unexpected(binding_error::range);
    if (auto valid = valid_blocks(blocks); !valid) return valid;
    const auto truth = domain_truth(op, threshold);
    if (truth != predicate_truth::unknown) {
        frame3::each(blocks, [&](unsigned b) {
            const auto n = static_cast<unsigned>(std::min(std::size_t{256}, rows_ - std::size_t{b} * 256));
            output[b] = truth == predicate_truth::always ? frame3::first_n(n) : frame3::bitset256{};
        });
        return {};
    }
    if (auto admitted = admit_blocks(blocks); !admitted) return admitted;
    if (blocks.empty()) return {};
    frame3::block_args thresholds;
    thresholds.fill(threshold);
    root_->filter(op, 0, blocks, thresholds, output);
    frame3::each(blocks, [&](unsigned b) {
        const auto n = static_cast<unsigned>(std::min(std::size_t{256}, rows_ - std::size_t{b} * 256));
        output[b] = frame3::operator&(output[b], frame3::first_n(n));
    });
    return {};
}
std::expected<stream_binding::fine_request, binding_error> stream_binding::selected_rows(
    const frame3::bitset256 &blocks, const frame3::block_rows_set &selected) const {
    if (auto valid = valid_blocks(blocks); !valid) return std::unexpected(valid.error());
    fine_request request{blocks, 0};
    bool valid = true;
    frame3::each(blocks, [&](unsigned b) {
        const auto n = static_cast<unsigned>(std::min(std::size_t{256}, rows_ - std::size_t{b} * 256));
        if (!(selected[b] == frame3::operator&(selected[b], frame3::first_n(n)))) valid = false;
        if (selected[b].empty()) request.active.set(b, false);
        request.count += selected[b].count();
    });
    if (!valid) return std::unexpected(binding_error::range);
    return request;
}
std::expected<frame3::u128, binding_error> stream_binding::sum_selected(const frame3::bitset256 &blocks,
    const frame3::block_rows_set &selected) const {
    const auto request = selected_rows(blocks, selected);
    if (!request) return std::unexpected(request.error());
    if (auto admitted = admit_blocks(request->active); !admitted) return std::unexpected(admitted.error());
    return request->count == 0 ? frame3::u128{0} : root_->sum_selected(0, request->active, selected);
}
std::expected<std::size_t, binding_error> stream_binding::select(const frame3::bitset256 &blocks,
    const frame3::block_rows_set &selected, std::span<frame3::u64> output) const {
    const auto request = selected_rows(blocks, selected);
    if (!request) return std::unexpected(request.error());
    if (request->count > output.size()) return std::unexpected(binding_error::resources);
    if (auto admitted = admit_blocks(request->active); !admitted) return std::unexpected(admitted.error());
    if (request->count == 0) return std::size_t{0};
    return root_->select(0, request->active, selected, output.data());
}
} // namespace frame3_owner

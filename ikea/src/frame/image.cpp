#include <ikea/frame/image.h>
#include <ikea/frame/build.h>
#include <ikea/frame/detail/image_state.h>
#include <ikea/frame/detail/recipe.h>
#include <ikea/frame/detail/group_image.h>
#include <algorithm>
#include <array>
#include <limits>
#include <new>

namespace ikea::frame {
namespace {
error translate(manifest_error e) noexcept {
    switch (e) {
    case manifest_error::description: return error::description;
    case manifest_error::unsupported: return error::unsupported;
    case manifest_error::range: return error::range;
    case manifest_error::unavailable: return error::unavailable;
    case manifest_error::resources: return error::resources;
    default: return error::contents;
    }
}
detail::image_preset image_layout(pack_preset preset) noexcept {
    switch (preset) {
    case pack_preset::compact: return detail::image_preset::compact;
    case pack_preset::bulk_x86: return detail::image_preset::bulk_x86;
    case pack_preset::bulk_arm: return detail::image_preset::bulk_arm;
    }
    return detail::image_preset::compact;
}
bool key_less(section_key a, section_key b) noexcept {
    return a.root_node < b.root_node || (a.root_node == b.root_node && a.group < b.group);
}
const stream_manifest *find_stream(std::span<const stream_manifest> streams, std::uint16_t node) noexcept {
    for (const auto &s : streams) if (s.root_node == node) return &s;
    return nullptr;
}
std::uint64_t little_u64(std::span<const std::byte> bytes, std::size_t at) noexcept {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{std::to_integer<unsigned>(bytes[at + i])} << (i * 8);
    return value;
}
std::size_t blocks_in_group(std::size_t rows, std::size_t group) noexcept {
    return (std::min(std::size_t{4096}, rows - group * 4096) + 255) / 256;
}
} // namespace

std::expected<image_view, error> attach_image(const description &d, image_view view) {
    const auto [rows, extents, streams, parts, available] = view;
    if (const auto valid = validate_manifest(d, rows, extents, streams); !valid)
        return std::unexpected(translate(valid.error()));
    // Check every provided Part/key before resolving even the first section.
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].owner_part >= extents.size() || parts[i].bytes.size() != extents[parts[i].owner_part] ||
            (!parts[i].bytes.data() && !parts[i].bytes.empty())) return std::unexpected(error::range);
        for (std::size_t j = 0; j < i; ++j)
            if (parts[i].owner_part == parts[j].owner_part) return std::unexpected(error::range);
    }
    for (auto key : available) {
        const auto *stream = find_stream(streams, key.root_node);
        if (!stream || key.group >= stream->groups.size()) return std::unexpected(error::range);
        const auto part = stream->groups[key.group].owner_part;
        if (std::none_of(parts.begin(), parts.end(), [&](const auto &p) { return p.owner_part == part; }))
            return std::unexpected(error::unavailable);
    }
    return view;
}

std::expected<validated_image, error> validate_image(const description &d, image_view view) try {
    if (const auto attached = attach_image(d, view); !attached) return std::unexpected(attached.error());
    const auto [rows, extents, streams, parts, available] = view;
    auto root = detail::make_stages(d);
    if (!root) return std::unexpected(root.error());
    auto result = std::make_shared<detail::image_state>();
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
    const auto delta_recipe = detail::binding_recipe(d, delta_node);
    if (!delta_recipe) return std::unexpected(error::unsupported);
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
            if (prefix > image.size()) return std::unexpected(error::contents);
            for (std::size_t at = 0; at < prefix; at += 10)
                if (std::to_integer<unsigned>(image[at + 8]) > 1 || std::to_integer<unsigned>(image[at + 9]) > 1)
                    return std::unexpected(error::contents);
            if (!detail::validate_group(image.subspan(prefix), image_layout(delta_recipe->preset), delta_recipe->bounded,
                rows, static_cast<unsigned>(key.group))) return std::unexpected(error::contents);
            root_sections[key.group] = image;
        } else {
            const auto recipe = detail::binding_recipe(d, checkpoint_node);
            if (!recipe) return std::unexpected(error::unsupported);
            const auto *stream = find_stream(streams, key.root_node);
            if (!detail::validate_group(image, image_layout(recipe->preset), recipe->bounded, stream->rows,
                static_cast<unsigned>(key.group))) return std::unexpected(error::contents);
        }
    }
    for (auto key : result->available_) {
        const auto *stream = find_stream(streams, key.root_node);
        auto *target = key.root_node == d.root ? result->root_.get() : result->root_->children()[1].get();
        const auto image = section(key);
        const auto attached = target->attach(0, stream->rows, static_cast<unsigned>(key.group), image);
        if (!attached || *attached != image.size())
            return std::unexpected(error::contents);
    }
    for (std::size_t g = 0; g < root_sections.size(); ++g) {
        if (root_sections[g].empty()) continue;
        const std::array range{row_range{g * 4096, std::min(std::size_t{4096}, rows - g * 4096)}};
        const auto closure = required_sections(d, rows, streams, range);
        if (!closure) return std::unexpected(translate(closure.error()));
        if (!admit_sections(*closure, result->available_)) continue;
        if (intervals) {
            alignas(64) std::uint64_t values[256];
            for (std::size_t b = 0; b < blocks_in_group(rows, g); ++b) {
                const auto block = static_cast<unsigned>(g * 16 + b);
                const auto count = static_cast<unsigned>(std::min(std::size_t{256}, rows - std::size_t{block} * 256));
                result->root_->decode(0, block, values); // Never count/get/sum: no evidence shortcut.
                const auto image = root_sections[g];
                if (little_u64(image, b * 10) != values[count - 1]) return std::unexpected(error::contents);
                const bool monotone = std::to_integer<unsigned>(image[b * 10 + 8]);
                const bool exact = std::to_integer<unsigned>(image[b * 10 + 9]);
                if (monotone && !std::is_sorted(values, values + count)) return std::unexpected(error::contents);
                constexpr auto bias = std::uint64_t{1} << 63;
                const unsigned spacing = d.nodes[d.root].parameter;
                // Decode ignores the field at every checkpoint row. The
                // relative-ends SUM law includes it, so decoded logical values
                // alone cannot establish that law's zero-offset premise.
                for (unsigned i = 0; i < count; i += spacing)
                    if (result->root_->children()[0]->get(0, block * 256 + i) != bias)
                        return std::unexpected(error::contents);
                if (exact) {
                    for (unsigned i = 1; i < count; ++i) {
                        const auto reference = values[relative ? i - i % spacing : i - 1];
                        if (values[i] >= reference ? values[i] - reference >= bias : reference - values[i] > bias)
                            return std::unexpected(error::contents);
                    }
                }
            }
        }
        result->ready_groups_ |= static_cast<std::uint16_t>(1u << g);
    }
    return validated_image{std::move(result)};
} catch (const std::bad_alloc &) { return std::unexpected(error::resources); }


std::size_t validated_image::size() const noexcept { return state_ ? state_->rows_ : 0; }
} // namespace ikea::frame

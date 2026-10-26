#pragma once
#include <ikea/frame/description.h>

namespace ikea::frame::detail {
struct bound_pack_recipe {
    pack_preset preset{pack_preset::compact};
    bool bounded{};
    bool operator==(const bound_pack_recipe &) const = default;
};
// Refuses valid placeholders before any payload attachment. The owner builds
// the pack stage (16 blocks per group), optionally wrapping it in bound;
// its separately admitted fit policy supplies penalty/cap/slack.
std::expected<bound_pack_recipe, description_error> binding_recipe(const description &) noexcept;
// The same admission at an identified basic subtree of one validated program;
// node identities stay those of the original DAG. It does not clone a child
// description or claim a different row domain for a shared executable node.
std::expected<bound_pack_recipe, description_error> binding_recipe(const description &, std::uint16_t root_node) noexcept;

} // namespace ikea::frame::detail

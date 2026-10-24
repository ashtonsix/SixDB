#pragma once
#include "description.h"
#include "group_image.h"
#include "../proto/frame.h"
#include <memory>

namespace frame3_owner {
enum class binding_error { description, image, range, unavailable, authority, resources, effects, unsupported };
/// A fitting policy is a construction/rebuild input, never an interpretation
/// of bytes already stored. Changing it does not change a reader's program.
struct fitting_policy { unsigned penalty = 0, exception_cap = 64, slack = 8; };
struct borrowed_group { unsigned group{}; std::span<const std::byte> image; };
std::expected<std::unique_ptr<frame3::stage>, binding_error> make_tree(const description &, fitting_policy = {});

/// Checked partial attachment. Source bytes remain immutable, placed and alive
/// through the binding's life; an owning caller keeps its Orbital selection or
/// exported Parts alive. The binding owns its interpretation/operation objects,
/// but borrows the image. A selected absent block is a refusal, never a zero.
class binding {
    std::unique_ptr<frame3::stage> root_;
    std::size_t rows_{};
    frame3::bitset256 available_;
    binding(std::unique_ptr<frame3::stage>, std::size_t, frame3::bitset256);
    bool admitted(const frame3::bitset256 &) const;
  public:
    static std::expected<std::unique_ptr<binding>, binding_error> attach(const description &,
        std::size_t rows, std::span<const borrowed_group>);
    std::size_t size() const { return rows_; }
    frame3::bitset256 available_blocks() const { return available_; }
    std::expected<frame3::u64, binding_error> get(unsigned row) const;
    std::expected<void, binding_error> read(unsigned first, std::span<frame3::u64>) const;
    std::expected<frame3::u128, binding_error> sum(const frame3::bitset256 &) const;
    std::expected<frame3::u64, binding_error> count(ikea::comparison, const frame3::bitset256 &, frame3::u64) const;
    std::expected<frame3::u128, binding_error> sum_selected(const frame3::bitset256 &,
        const frame3::block_rows_set &) const;
};
struct command { unsigned row{}; frame3::u64 value{}; };
struct preparation_budget {
    bool source_readable = false;
    std::size_t source_copy_bytes{}, commands{}, effect_records{}, output_bytes{};
    bool allow_rebuild = true;
};
// issued_bytes/records count private apply stores only, cumulatively; rebuild
// and emission construction stores are not included. new_bytes is final image.
struct prepared_group {
    std::vector<std::byte> image;
    __int128 contribution{};
    frame3::u128 sum{};
    frame3::u64 min{}, max{};
    std::size_t copied_bytes{}, new_bytes{}, issued_bytes{}, records{}, rebuilds{};
};
/// An afterimage preparation, not an in-place owner write. Admission covers the
/// whole source group before reading it. All commands execute in order on one
/// private binding, with one final emission; failure drops that private work
/// and leaves the source and published state unchanged. Resource allocation
/// can still fail privately. The budget bounds source copying, command count,
/// peak reusable per-command journal capacity and final image; it is not a
/// bound on allocator overhead.
std::expected<prepared_group, binding_error> prepare_group(const description &, std::size_t rows,
    unsigned group, std::span<const std::byte>, std::span<const command>, preparation_budget,
    fitting_policy = {});
} // namespace frame3_owner

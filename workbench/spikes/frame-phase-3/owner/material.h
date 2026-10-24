#pragma once

#include <orbital/object.h>
#include <orbital/representation.h>

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <vector>

namespace frame3_owner {

/// Immutable after admission. The caller validates the pipeline byte law and
/// logical interpretation; this material owns neither payload nor Frame policy.
struct interpretation {
    std::vector<std::byte> pipeline;
    std::size_t rows{};
    std::vector<std::size_t> group_extents;
    std::uint64_t logical_schema{};
    orbital::ObjectId object{};
};

namespace detail { struct interpretation_state; }
class interpretation_owner;

/// Non-owning, trivial proof handle. Copying it retains nothing. Its issuing
/// owner, Representation or escaping Part keeps the interpretation alive.
struct Control {
    Control() = default;
    const interpretation &meaning() const noexcept;
    bool valid(std::pmr::memory_resource &) const noexcept;
    std::pmr::memory_resource *resource() const noexcept;
    std::size_t part_count() const noexcept { return count_; }
    orbital::PartId id(std::size_t) const noexcept;
    orbital::Result<std::size_t> extent(std::size_t) const noexcept;
    std::uint32_t padding(std::size_t) const noexcept { return 0; }
    std::size_t alignment(std::size_t) const noexcept { return 1; }
    void retain() const noexcept;
    void release() const noexcept;
    std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
    Control copy_selected(void *, std::size_t) const noexcept;
    std::size_t metadata_bytes() const noexcept { return 0; }
    void destroy_selected() const noexcept;

  private:
    detail::interpretation_state *state_{};
    std::size_t first_{}, count_{};
    Control(detail::interpretation_state *, std::size_t, std::size_t) noexcept;
    friend class interpretation_owner;
};
static_assert(std::is_trivially_copyable_v<Control>);

/// One independent interpretation use. The resource outlives every derived
/// Representation and Part. All lifetime operations stay on the Store's owner.
class interpretation_owner {
    detail::interpretation_state *state_{};
    explicit interpretation_owner(detail::interpretation_state *) noexcept;

  public:
    interpretation_owner() = default;
    interpretation_owner(const interpretation_owner &) = delete;
    interpretation_owner &operator=(const interpretation_owner &) = delete;
    interpretation_owner(interpretation_owner &&) noexcept;
    interpretation_owner &operator=(interpretation_owner &&) noexcept;
    ~interpretation_owner();
    explicit operator bool() const noexcept { return state_; }
    [[nodiscard]] static orbital::Result<interpretation_owner> make(
        std::pmr::memory_resource &, interpretation) noexcept;
    const interpretation &meaning() const noexcept;
    Control control() const noexcept;
};

using Selection = orbital::RepresentationSelection<Control>;
struct Physical {
    using Value = orbital::Representation<Control>;
    using Selection = frame3_owner::Selection;
    using Specification = Control;
    using PartSource = orbital::PartSource;
    using ExportPolicy = orbital::PartRetention;
    struct Footprint { std::size_t requested_bytes{}, copied_metadata{}; };
    static void retain(Value *) noexcept;
    static void release(Value *) noexcept;
    static Value *get(Selection selected) noexcept { return selected.realization; }
    static Selection selection(Value *value) noexcept { return {value}; }
    [[nodiscard]] static orbital::Result<Value *> capture(std::pmr::memory_resource &,
        const Control &, std::span<const PartSource>) noexcept;
    static bool accepts(Value *, orbital::ObjectId, std::uint64_t) noexcept;
    static bool accepts_resource(Value *, std::pmr::memory_resource &) noexcept;
    /// Necessary compatibility for adoption; the converter still owes exact
    /// logical equivalence. Normal logical publication may change row count.
    static bool compatible(Value *, Value *) noexcept;
    static Footprint footprint(Value *) noexcept;
    [[nodiscard]] static orbital::Result<orbital::Part<Control> *> export_part(
        Selection, std::size_t, orbital::ObjectBinding, ExportPolicy) noexcept;
};
using Store = orbital::ObjectStore<Physical>;

/// Sources name destination Control::id(index). Retained source certificates
/// and their selected backing stay alive and fixed through synchronous capture.
[[nodiscard]] orbital::Result<Store::Owner> capture(Store &, const interpretation_owner &,
    std::span<const orbital::PartSource>);
[[nodiscard]] orbital::Result<Store::Owner> capture(Store &, std::pmr::memory_resource &,
    interpretation, std::span<const orbital::PartSource>);
[[nodiscard]] orbital::Result<orbital::PartSource> reuse(Selection, std::size_t) noexcept;
[[nodiscard]] orbital::Result<orbital::PartSource> reuse(const orbital::Part<Control> *) noexcept;

}  // namespace frame3_owner

extern template class orbital::ObjectStore<frame3_owner::Physical>;
extern template struct orbital::RepresentationSelection<frame3_owner::Control>;
extern template void orbital::release(orbital::Part<frame3_owner::Control> *) noexcept;
extern template orbital::View orbital::view(const orbital::Part<frame3_owner::Control> *) noexcept;
extern template orbital::ObjectBinding orbital::binding(const orbital::Part<frame3_owner::Control> *) noexcept;

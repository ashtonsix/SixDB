#pragma once
#include <ikea/streampack.h>
#include <loom/worker.h>
#include <orbital/object.h>
#include <orbital/representation.h>
#include <array>
#include <memory>
#include <memory_resource>
#include <optional>

// A concrete integration caller, not a proposed collection or allocator facade.
// One Object owns a StreamPack block and its application-level holder map.
namespace streampack_implementation::orbital_material {
namespace sp = ikea::streampack;
namespace oa = orbital::allocation;
constexpr std::uint32_t absent = UINT32_MAX;
enum class Placement { grouped, separate, shared_payload };

struct Format {
    sp::layout layout;
    unsigned references{};
    void retain() noexcept { ++references; }
    void release() noexcept { if (!--references) delete this; }
};
inline void retain_ref(Format& format) noexcept { format.retain(); }
inline void release_ref(Format& format) noexcept { format.release(); }
using FormatOwner = orbital::Ref<Format>;
std::expected<FormatOwner, sp::error> prepare_format(sp::description);

struct Control {
    std::pmr::memory_resource* memory{};
    Format* format{};
    std::array<std::size_t, 5> extents{};
    Placement placement{Placement::grouped};
    unsigned selected{5};
    bool valid(std::pmr::memory_resource&) const noexcept;
    auto* resource() const noexcept { return memory; }
    std::size_t part_count() const noexcept { return selected == 5 ? 5 : 1; }
    unsigned original(std::size_t i) const noexcept { return selected == 5 ? unsigned(i) : selected; }
    orbital::PartId id(std::size_t i) const noexcept { return {0x535049, original(i)}; }
    orbital::Result<std::size_t> extent(std::size_t) const noexcept;
    std::size_t alignment(std::size_t) const noexcept;
    std::uint32_t padding(std::size_t) const noexcept { return 0; }
    void retain() const noexcept { format->retain(); }
    void release() const noexcept { format->release(); }
    std::size_t selected_bytes(std::size_t) const noexcept { return 0; }
    Control copy_selected(void*, std::size_t i) const noexcept;
    std::size_t metadata_bytes() const noexcept { return 0; }
    void destroy_selected() const noexcept { release(); }
};

using Selection = orbital::RepresentationSelection<Control>;
struct Physical {
    using Value = orbital::Representation<Control>;
    using Selection = orbital_material::Selection;
    using Specification = Control;
    using PartSource = orbital::PartSource;
    using ExportPolicy = orbital::PartRetention;
    struct Footprint { std::size_t requested_bytes{}, copied_metadata{}; };
    static void retain(Value* v) noexcept { orbital::retain(v); }
    static void release(Value* v) noexcept { orbital::release(v); }
    static Value* get(Selection s) noexcept { return s.realization; }
    static Selection selection(Value* v) noexcept { return {v}; }
    static orbital::Result<Value*> capture(std::pmr::memory_resource&, const Control&,
        std::span<const PartSource>) noexcept;
    static bool accepts(Value*, orbital::ObjectId, std::uint64_t) noexcept { return true; }
    static bool accepts_resource(Value*, std::pmr::memory_resource&) noexcept;
    static bool compatible(Value*, Value*) noexcept;
    static Footprint footprint(Value*) noexcept;
    static orbital::Result<orbital::Part<Control>*> export_part(Selection, std::size_t,
        orbital::ObjectBinding, ExportPolicy) noexcept;
};
using Store = orbital::ObjectStore<Physical>;

sp::const_view read(Selection);
std::span<const std::uint32_t> holders(Selection);

struct Draft {
    FormatOwner format;
    std::pmr::memory_resource* memory{};
    oa::Pool* pool{};
    Placement placement{Placement::grouped};
    std::array<oa::Allocation, 5> allocations;
    std::array<std::size_t, 5> offsets{}, extents{};
    std::optional<sp::view> source;
    std::uint64_t copied_bytes{};

    static orbital::Result<Draft> create(loom::Worker&, FormatOwner,
        std::pmr::memory_resource&, std::size_t payload_bytes,
        Placement = Placement::grouped, unsigned length_slots = 0);
    static orbital::Result<Draft> clone(loom::Worker&, Selection,
        std::size_t payload_bytes = 0, Placement = Placement::grouped);
    sp::view& write() noexcept { return *source; }
    const sp::view& write() const noexcept { return *source; }
    std::span<std::uint32_t> holders() noexcept;
    // Growth ends the old address-qualified binding. Resolve effects first.
    // It never grants mutation authority over a published or retained footprint.
    orbital::Result<void> grow(std::size_t payload_bytes, unsigned length_slots);
    orbital::Result<Store::Owner> capture(Store&);
    std::byte* address(unsigned) const noexcept;
private:
    orbital::Result<void> rebind();
};
orbital::Result<void> publish(Store&, const Store::Scope&, orbital::ObjectId,
    const Store::Owner&);
} // namespace streampack_implementation::orbital_material
extern template class orbital::ObjectStore<streampack_implementation::orbital_material::Physical>;

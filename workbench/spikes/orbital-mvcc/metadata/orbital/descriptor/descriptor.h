#pragma once

#include <orbital/types.h>
#include <memory>
#include <memory_resource>

// Experimental frozen descriptor. The resource outlives every descriptor and
// exported Part; capture copies descriptions and borrows immutable body bytes.
namespace compact_descriptor {

using orbital::Bytes;
using orbital::Error;
using orbital::ObjectId;
using orbital::PartId;
using orbital::ProgramId;
using orbital::ReplicaId;
using orbital::Result;

struct RepresentationSpec {
  ObjectId object{};
  std::uint64_t version{};
  ReplicaId replica{};
  ProgramId format{};
  Bytes logical_metadata;
  Bytes interpretation;
  std::shared_ptr<const void> dependencies;
};

struct PartSource {
  PartId id{};
  Bytes body;
  Bytes interpretation;
  std::size_t readable_padding{};
  std::size_t alignment{1};
  std::shared_ptr<const void> backing;
};

struct Description {
  ObjectId object{};
  std::uint64_t version{};
  ReplicaId replica{};
  ProgramId format{};
  Bytes logical_metadata;
  Bytes interpretation;
};

struct FrozenDescription {
  FrozenDescription() = default;
  FrozenDescription(const FrozenDescription&) = delete;
  FrozenDescription& operator=(const FrozenDescription&) = delete;
  FrozenDescription(FrozenDescription&&) = delete;
  FrozenDescription& operator=(FrozenDescription&&) = delete;
  ObjectId object{};
  std::uint64_t version{};
  ReplicaId replica{};
  ProgramId format{};
  std::uint32_t logical_offset{}, logical_bytes{};
  std::uint32_t interpretation_offset{}, interpretation_bytes{};
  std::uint32_t parts_offset{}, parts{};
  Bytes logical_metadata() const noexcept {
    return {static_cast<const std::byte*>(static_cast<const void*>(this)) + logical_offset, logical_bytes};
  }
  Bytes interpretation() const noexcept {
    return {static_cast<const std::byte*>(static_cast<const void*>(this)) + interpretation_offset, interpretation_bytes};
  }
};

struct Descriptor;
struct Export;

namespace detail {
struct Part {
  PartId id;
  const std::byte* body;
  std::uint64_t body_bytes;
  std::uint32_t metadata_distance;
  std::uint32_t interpretation_offset;
  std::uint32_t interpretation_bytes;
  std::uint32_t readable_padding;
  std::uint8_t alignment_log2;
};
} // namespace detail

// Native packet borrows its scope's exact fixed descriptor/backing. It carries
// no independent owner. Its catalog/read scope, Descriptor or Export must live.
struct View {
  const detail::Part* leaf{};
  explicit operator bool() const noexcept { return leaf; }
  Bytes body() const noexcept { return {leaf->body, leaf->body_bytes}; }
  PartId id() const noexcept { return leaf->id; }
  std::size_t readable_padding() const noexcept { return leaf->readable_padding; }
  std::size_t alignment() const noexcept { return std::size_t{1} << leaf->alignment_log2; }
  const FrozenDescription& description() const noexcept {
    auto* start = static_cast<const std::byte*>(static_cast<const void*>(leaf)) - leaf->metadata_distance;
    return *static_cast<const FrozenDescription*>(static_cast<const void*>(start));
  }
  Bytes interpretation() const noexcept {
    auto* start = static_cast<const std::byte*>(static_cast<const void*>(&description()));
    return {start + leaf->interpretation_offset, leaf->interpretation_bytes};
  }
};

struct Selection {
  Descriptor* descriptor{};
  explicit operator bool() const noexcept { return descriptor; }
  std::size_t part_count() const noexcept;
  [[nodiscard]] Result<View> part(std::size_t) const noexcept;
  [[nodiscard]] Result<View> part(PartId) const noexcept;
};

/// Body and padding remain immutable while their supplied backing owner lives.
/// Metadata is limited to a 32-bit slab; body extents remain 64-bit. Empty Parts
/// need no backing when they expose no padding. Caller owners/control blocks and
/// their body allocations are not charged to this metadata resource. Input
/// spans have ordinary readable-memory validity; null/overflow extents refuse.
[[nodiscard]] Result<Descriptor*> capture(std::pmr::memory_resource&,
    const RepresentationSpec&, std::span<const PartSource>) noexcept;
void retain(Descriptor*) noexcept;
void release(Descriptor*) noexcept;
Description describe(Descriptor*) noexcept;
Description describe(View) noexcept;
const FrozenDescription& description(Selection) noexcept;
ObjectId object(Descriptor*) noexcept;
std::uint64_t version(Descriptor*) noexcept;
bool same_logical_state(Descriptor*, Descriptor*) noexcept;

// Allocate the export only when an individual resolved Part escapes. It pins
// frozen descriptions/dependencies plus the selected backing, not sibling
// bodies or the representation's backing directory. Export::view is borrowed;
// copy the Export owner explicitly rather than trying to retain that raw View.
[[nodiscard]] Result<Export*> retain_part(Selection, std::size_t) noexcept;
void retain(Export*) noexcept;
void release(Export*) noexcept;
View view(Export*) noexcept;

struct Footprint {
  std::size_t descriptor_header{}, owner_directory{}, metadata_header{};
  std::size_t part_directory{}, copied_metadata{}, alignment_slack{};
  std::size_t requested_bytes{};
};
Footprint footprint(Descriptor*) noexcept;
std::size_t export_bytes() noexcept;

static_assert(sizeof(View) == 8 && sizeof(Result<View>) == 16);
static_assert(sizeof(Selection) == 8 && sizeof(Result<Selection>) == 16);
static_assert(sizeof(detail::Part) == 56);
static_assert(sizeof(FrozenDescription) == 80);

} // namespace compact_descriptor

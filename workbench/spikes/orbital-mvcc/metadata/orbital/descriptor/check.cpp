#include "descriptor.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <source_location>
#include <type_traits>
#include <vector>

namespace cd = compact_descriptor;

static_assert(!std::is_copy_constructible_v<cd::FrozenDescription>);
static_assert(!std::is_move_constructible_v<cd::FrozenDescription>);
static_assert(!std::is_copy_assignable_v<cd::FrozenDescription>);
static_assert(!std::is_move_assignable_v<cd::FrozenDescription>);

template<class T>
void check(const T& condition, std::source_location location = std::source_location::current()) {
  if (condition) return;
  std::fprintf(stderr, "check failed: %s:%u\n", location.file_name(), location.line());
  std::abort();
}

struct Memory : std::pmr::memory_resource {
  std::size_t live{}, peak{}, calls{}, limit = std::numeric_limits<std::size_t>::max();
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (bytes > limit - live) throw std::bad_alloc();
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes;
    peak = std::max(peak, live);
    ++calls;
    return result;
  }
  void do_deallocate(void* value, std::size_t bytes, std::size_t alignment) override {
    check(live >= bytes);
    live -= bytes;
    std::pmr::new_delete_resource()->deallocate(value, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

struct Body {
  std::uint64_t value;
  std::size_t* destroyed;
  ~Body() { ++*destroyed; }
};
struct Dependency {
  std::size_t* destroyed;
  ~Dependency() { ++*destroyed; }
};

cd::Bytes bytes(const auto& value) {
  return std::as_bytes(std::span(&value, 1));
}

std::uint64_t word(cd::View value) {
  check(value.body().size() == sizeof(std::uint64_t));
  std::uint64_t result;
  std::memcpy(&result, value.body().data(), sizeof(result));
  return result;
}

void closure(std::size_t count) {
  Memory metadata, external;
  std::size_t destroyed{}, dependencies_destroyed{};
  const std::array<std::byte, 5> logical{
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
  std::array<std::byte, 3> interpretation{std::byte{8}, std::byte{7}, std::byte{6}};
  auto dependency = std::allocate_shared<Dependency>(std::pmr::polymorphic_allocator<Dependency>(&external),
      &dependencies_destroyed);
  cd::RepresentationSpec spec{{0x71, 99}, 4, {23, 42}, {91, 92}, logical, interpretation, dependency};
  std::vector<cd::PartSource> sources;
  for (std::size_t i = 0; i < count; ++i) {
    auto body = std::allocate_shared<Body>(std::pmr::polymorphic_allocator<Body>(&external),
        100 + i, &destroyed);
    sources.push_back({{8, 100 - i}, bytes(body->value), interpretation, 0, alignof(std::uint64_t), body});
  }
  const auto external_bytes = external.live;
  auto captured = cd::capture(metadata, spec, sources);
  check(captured);
  auto* descriptor = *captured;
  auto cost = cd::footprint(descriptor);
  check(metadata.live == cost.requested_bytes);
  check(cost.descriptor_header == 16 && cost.metadata_header == 112);
  check(cost.part_directory == 56 * count && cost.owner_directory == 16 * count);
  check(cost.copied_metadata == 8 + 3 * count && cost.alignment_slack == 0);
  check(cd::object(descriptor) == spec.object && cd::version(descriptor) == spec.version);
  auto selected = cd::Selection{descriptor};
  check(selected.part_count() == count && !selected.part(count));
  check(!selected.part(cd::PartId{1, 2}));
  cd::Export* escaped = nullptr;
  const cd::FrozenDescription* frozen_borrow = nullptr;
  cd::Bytes logical_borrow, interpretation_borrow;
  if (count) {
    auto part = selected.part(count - 1);
    check(part && word(*part) == 99 + count);
    check(part->id() == (cd::PartId{8, 101 - count}));
    check(selected.part(part->id())->leaf == part->leaf);
    escaped = *cd::retain_part(selected, count - 1);
    frozen_borrow = &part->description();
    logical_borrow = frozen_borrow->logical_metadata();
    interpretation_borrow = frozen_borrow->interpretation();
    check(metadata.live == cost.requested_bytes + cd::export_bytes());
  }
  interpretation.fill(std::byte{0});
  sources.clear();
  spec.dependencies.reset();
  dependency.reset();
  check(!destroyed && !dependencies_destroyed);
  check(cd::describe(descriptor).interpretation[0] == std::byte{8});
  check(cd::description(selected).logical_metadata()[4] == std::byte{5});
  check(cd::description(selected).format == spec.format);
  cd::release(descriptor);
  if (count) {
    check(destroyed == count - 1 && !dependencies_destroyed);
    check(metadata.live == cost.metadata_header + cost.part_directory + cost.copied_metadata + cd::export_bytes());
    check(word(cd::view(escaped)) == 99 + count);
    check(&cd::view(escaped).description() == frozen_borrow);
    check(logical_borrow[4] == std::byte{5} && interpretation_borrow[0] == std::byte{8});
    check(cd::view(escaped).interpretation()[0] == std::byte{8});
    check(cd::view(escaped).description().interpretation()[0] == std::byte{8});
    check(cd::describe(cd::view(escaped)).format == (cd::ProgramId{91, 92}));
    cd::retain(escaped);
    cd::release(escaped);
    cd::release(escaped);
  }
  check(destroyed == count && dependencies_destroyed == 1);
  check(!metadata.live && !external.live);
  std::printf("parts=%zu metadata=%zu external=%zu peak=%zu calls=%zu export=%zu\n",
      count, cost.requested_bytes, external_bytes, metadata.peak, metadata.calls, cd::export_bytes());
}

void descriptions_and_refusal() {
  Memory memory;
  const std::uint64_t metadata = 17;
  cd::RepresentationSpec spec{{1, 2}, 3, {}, {1, 0}, bytes(metadata), {}, {}};
  const std::array<cd::PartSource, 4> columns{{
      {{0, 0}, {}, {}, 0, 1, {}}, {{0, 1}, {}, {}, 0, 1, {}},
      {{0, 2}, {}, {}, 0, 1, {}}, {{0, 3}, {}, {}, 0, 1, {}}}};
  auto* a = *cd::capture(memory, spec, columns);
  const std::array<cd::PartSource, 1> row{{{{0, 0}, {}, {}, 0, 1, {}}}};
  spec.format = {2, 0};
  auto* b = *cd::capture(memory, spec, row);
  check(cd::same_logical_state(a, b));
  check(cd::Selection{a}.part_count() == 4 && cd::Selection{b}.part_count() == 1);
  auto* e = *cd::retain_part({a}, 3);
  cd::release(a);
  check(cd::view(e).body().empty() && cd::view(e).id() == (cd::PartId{0, 3}));
  spec.version = 4;
  auto* c = *cd::capture(memory, spec, row);
  check(!cd::same_logical_state(b, c));
  cd::release(b); cd::release(c); cd::release(e);
  check(!memory.live);

  auto duplicate = columns;
  duplicate[3].id = duplicate[2].id;
  check(!cd::capture(memory, spec, duplicate) && !memory.live);
  auto invalid = row;
  invalid[0].alignment = 3;
  check(!cd::capture(memory, spec, invalid) && !memory.live);
  invalid = row;
  invalid[0].readable_padding = 1;
  check(!cd::capture(memory, spec, invalid) && !memory.live);
  memory.limit = 120; // metadata slab succeeds; backing directory must refuse.
  check(!cd::capture(memory, spec, {}) && !memory.live);
  memory.limit = std::numeric_limits<std::size_t>::max();
  auto* d = *cd::capture(memory, spec, row);
  memory.limit = memory.live;
  check(!cd::retain_part({d}, 0));
  check(cd::Selection{d}.part(0)->body().empty());
  cd::release(d);
  check(!memory.live);

  memory.limit = std::numeric_limits<std::size_t>::max();
  auto owner = std::make_shared<std::uint64_t>(901);
  std::shared_ptr<const void> alias(owner, nullptr);
  const std::array<cd::PartSource, 1> aliased{{{{0, 1}, bytes(*owner), {}, 0, 1, alias}}};
  auto* aliased_descriptor = *cd::capture(memory, spec, aliased);
  check(word(*cd::Selection{aliased_descriptor}.part(0)) == 901);
  cd::release(aliased_descriptor);

  const std::array<cd::PartSource, 1> padded_empty{{
      {{0, 1}, {bytes(*owner).data(), 0}, {}, sizeof(std::uint64_t), 1, alias}}};
  auto* empty_descriptor = *cd::capture(memory, spec, padded_empty);
  auto empty = cd::Selection{empty_descriptor}.part(0);
  check(empty && empty->body().empty() && empty->readable_padding() == 8);
  cd::release(empty_descriptor);
  check(!memory.live);
}

int main() {
  for (auto count : {0U, 1U, 4U, 17U}) closure(count);
  descriptions_and_refusal();
}

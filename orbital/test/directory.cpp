#include <orbital/detail/object/directory.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <span>
#include <vector>

namespace orbital::object_detail {
template<> struct Access<void> {
  struct Probes { std::size_t total{}, maximum{}; };
  static Probes probes(const TipDirectory& directory, std::span<const ObjectId> ids) {
    Probes result;
    const auto mask = directory.buckets_.size() - 1;
    for (auto id : ids) {
      const auto home = static_cast<std::size_t>(directory.hash(id)) & mask;
      const auto count = ((directory.position(id) - home) & mask) + 1;
      result.total += count;
      result.maximum = std::max(result.maximum, count);
    }
    return result;
  }
};
} // namespace orbital::object_detail

namespace {
using orbital::ObjectId;
using orbital::object_detail::Handle;
using orbital::object_detail::TipDirectory;

void check(bool condition, const char* message,
    std::source_location where = std::source_location::current()) {
  if (condition) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), message);
  std::abort();
}

void exercise(const char* name, std::span<const ObjectId> ids, bool bounded) {
  TipDirectory directory(*std::pmr::new_delete_resource());
  std::vector<Handle> handles;
  std::vector<const orbital::object_detail::Entry*> addresses;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    check(!directory.find(ids[i]), "new full-width identity already present");
    auto [handle, inserted] = directory.ensure(ids[i]);
    check(inserted, "new identity was not inserted");
    directory.get(handle).cut = i + 1;
    handles.push_back(handle);
    addresses.push_back(&directory.get(handle));
  }
  check(directory.size() == ids.size(), "directory lost an identity during growth");
  for (std::size_t i = 0; i < ids.size(); ++i) {
    check(directory.find(ids[i]) == handles[i], "rehash changed an identity's handle");
    check(&directory.get(handles[i]) == addresses[i], "rehash moved an owning entry");
    check(directory.get(handles[i]).cut == i + 1, "rehash changed entry contents");
    check(directory.ensure(ids[i]) == std::pair{handles[i], false}, "duplicate ensure changed identity");
  }
  if (bounded) {
    const auto probes = orbital::object_detail::Access<void>::probes(directory, ids);
    // These ordinary structured IDs must not become one linear scan. Loose
    // bounds constrain the actual table, without fixing a hash or bucket layout.
    if (probes.total > ids.size() * 8 || probes.maximum > 128) {
      std::fprintf(stderr, "%s: %zu IDs, %zu total probes, %zu maximum\n",
          name, ids.size(), probes.total, probes.maximum);
      check(false, "structured ObjectIds form pathological probe chains");
    }
  }

  // Remove interior entries, then find survivors through the tombstones. Entry
  // reuse must preserve full identities and stable handles of untouched entries.
  for (std::size_t i = 1; i < ids.size(); i += 3) {
    check(directory.erase(handles[i]), "unowned entry did not erase");
    check(!directory.find(ids[i]), "erased identity remains discoverable");
  }
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i % 3 == 1) continue;
    check(directory.find(ids[i]) == handles[i], "tombstone hid a surviving identity");
    check(&directory.get(handles[i]) == addresses[i], "erase moved an unrelated entry");
  }
  for (std::size_t i = 1; i < ids.size(); i += 3) {
    auto [handle, inserted] = directory.ensure(ids[i]);
    check(inserted, "erased identity did not recreate");
    handles[i] = handle;
    directory.get(handle).cut = i + 1;
  }
  for (std::size_t i = 0; i < ids.size(); ++i) {
    check(directory.find(ids[i]) == handles[i] && directory.get(handles[i]).cut == i + 1,
        "recreated or surviving entry has wrong identity/content");
  }
  check(directory.size() == ids.size(), "erase/recreate changed final population");
}
} // namespace

int main() {
  constexpr std::uint64_t domain = 0x494e4458;
  constexpr std::size_t count = 4096;
  std::array<ObjectId, count> ids;
  for (unsigned family = 0; family < 6; ++family) {
    for (std::uint64_t i = 0; i < count; ++i) {
      switch (family) {
        case 0: ids[i] = {domain, (std::uint64_t{5} << 56) | (i << 16)}; break;
        case 1: ids[i] = {domain, (std::uint64_t{4} << 56) | (i << 8)}; break;
        case 2: ids[i] = {domain, i}; break;
        case 3: ids[i] = {i << 32, domain}; break;
        case 4: ids[i] = {domain, i << 48}; break;
        default: ids[i] = {i, i}; break;
      }
    }
    constexpr std::array names{"prefix-shift16", "prefix-shift8", "sequential",
        "high-only", "upper-low-word", "equal-halves"};
    exercise(names[family], ids, true);
  }
  // Genuine full-hash collisions still require full ObjectId comparisons and
  // correct tombstone traversal; spreading ordinary prefixes cannot replace that.
  for (std::uint64_t i = 0; i < 64; ++i)
    ids[i] = {domain ^ (i * 0x9e3779b97f4a7c15ULL), i};
  exercise("fold-collisions", std::span(ids).first(64), false);
  std::puts("structured ObjectId probe bounds, growth, collision and erase/recreate checks pass");
}

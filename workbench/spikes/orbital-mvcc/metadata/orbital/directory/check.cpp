#include "object_directory.h"
#include "../retention/checks.h"
#include "../candidates/common.h"

#include <map>
#include <memory>
#include <random>

using compact_directory::Directory;
using compact_directory::Entry;
using compact_directory::Handle;
using compact_version_probe::Memory;
using orbital::ObjectId;

ObjectId id(std::uint64_t value) { return {value ^ 0x60162a77, value}; }

void contents(const Directory& directory, const std::map<ObjectId, std::uint32_t>& expected) {
  CHECK(directory.size() == expected.size());
  for (const auto& [key, head] : expected) {
    const auto handle = directory.find(key);
    CHECK(handle && directory.get(handle).object == key && directory.get(handle).head == head);
  }
}

void collisions_and_stability() {
  Memory memory;
  {
    Directory directory(memory);
    std::map<ObjectId, std::uint32_t> expected;
    std::vector<std::pair<Handle, const Entry*>> addresses;
    constexpr auto multiplier = 0x9e3779b97f4a7c15ULL;
    for (std::uint32_t n = 0; n < 256; ++n) {
      const ObjectId key{n * multiplier, n}; // All share the control hash value 0.
      const auto [handle, inserted] = directory.ensure(key);
      CHECK(inserted && handle && directory.get(handle).object == key);
      directory.get(handle).head = n + 17;
      expected.emplace(key, n + 17);
      addresses.emplace_back(handle, &directory.get(handle));
      const auto again = directory.ensure(key);
      CHECK(again.first == handle && !again.second);
    }
    contents(directory, expected);
    for (const auto& [handle, address] : addresses) CHECK(&directory.get(handle) == address);
    const ObjectId absent{1, 0};
    CHECK(!directory.find(absent));
    std::map<ObjectId, std::uint32_t> visited;
    directory.for_each_live([&](Handle handle, Entry& entry) {
      CHECK(directory.find(entry.object) == handle);
      CHECK(visited.emplace(entry.object, entry.head).second);
    });
    CHECK(visited == expected && memory.live == directory.capacity_bytes());
  }
  CHECK(memory.live == 0);
}

void pins_debt_and_reuse() {
  Memory memory;
  {
    Directory directory(memory);
    const auto old = directory.ensure(id(1)).first;
    auto* stable = &directory.get(old);
    directory.get(old).head = 81;
    constexpr std::uint32_t owner_payload = 0xa5f67c08;
    directory.get(old).flags |= owner_payload;
    CHECK(directory.pin(old) && !directory.pin(old));
    CHECK((directory.get(old).flags & ~std::uint32_t{7}) == owner_payload);
    CHECK(!directory.erase(old) && directory.get(old).head == 81);
    bool called = false;
    {
      auto callback = std::shared_ptr<int>(new int(1), [&](int* value) {
        delete value; called = true;
        CHECK(!directory.erase(old));
        for (std::uint64_t n = 2; n < 1024; ++n) directory.ensure(id(n));
        CHECK(directory.find(id(1)) == old && &directory.get(old) == stable);
      });
    }
    CHECK(called && directory.pinned(old));
    directory.unpin(old);
    directory.set_indebted(old, true);
    CHECK(directory.indebted(old) && !directory.erase(old));
    directory.set_indebted(old, false);
    CHECK((directory.get(old).flags & ~std::uint32_t{7}) == owner_payload);
    CHECK(directory.erase(old) && !directory.valid(old) && !directory.find(id(1)));
    const auto next = directory.ensure(id(99999)).first;
    CHECK(next == old && &directory.get(next) == stable);
    CHECK(directory.get(next).object == id(99999) && directory.get(next).head == 0);
    CHECK(!directory.pinned(next) && !directory.indebted(next));
    CHECK((directory.get(next).flags & ~std::uint32_t{7}) == 0);
    CHECK(memory.live == directory.capacity_bytes());
  }
  CHECK(memory.live == 0);
}

void refusal() {
  Memory memory;
  {
    Directory directory(memory);
    memory.limit = 0;
    try { directory.ensure(id(1)); CHECK(false); } catch (const std::bad_alloc&) {}
    CHECK(!directory.find(id(1)) && directory.size() == 0 && memory.live == 0);
    memory.limit = std::numeric_limits<std::size_t>::max();
    std::map<ObjectId, std::uint32_t> expected;
    for (std::uint32_t n = 1; n <= 12; ++n) {
      auto handle = directory.ensure(id(n)).first;
      directory.get(handle).head = n + 100;
      expected.emplace(id(n), n + 100);
    }
    const auto first = directory.find(id(1));
    CHECK(directory.pin(first));
    const auto before = memory.live;
    memory.limit = before;
    try { directory.ensure(id(13)); CHECK(false); } catch (const std::bad_alloc&) {}
    CHECK(memory.live == before && directory.pinned(first));
    contents(directory, expected);
    memory.limit = std::numeric_limits<std::size_t>::max();
    for (std::uint32_t n = 13; n <= 32; ++n) {
      const auto handle = directory.ensure(id(n)).first;
      directory.get(handle).head = n + 100;
      expected.emplace(id(n), n + 100);
    }
    const auto full_page = memory.live;
    memory.limit = full_page + 32 * sizeof(Entry); // Page fits; page-table growth must refuse.
    try { directory.ensure(id(33)); CHECK(false); } catch (const std::bad_alloc&) {}
    CHECK(memory.live == full_page && directory.pinned(first));
    contents(directory, expected);
    memory.limit = std::numeric_limits<std::size_t>::max();
    CHECK(directory.ensure(id(33)).second);
    CHECK(directory.get(first).head == 101 && directory.pinned(first));
    directory.unpin(first);
  }
  CHECK(memory.live == 0);
}

void churn_and_oracle() {
  Memory memory;
  {
    Directory directory(memory);
    for (std::uint64_t n = 1; n <= 8192; ++n) {
      auto handle = directory.ensure(id(n)).first;
      CHECK(directory.find(id(n)) == handle);
      CHECK(directory.erase(handle) && directory.size() == 0 && !directory.find(id(n)));
      CHECK(memory.live == directory.capacity_bytes());
    }
    CHECK(directory.entry_bytes() == 32 * sizeof(Entry) && directory.bucket_bytes() == 16 * sizeof(Handle));
    std::map<ObjectId, std::uint32_t> expected;
    std::mt19937_64 random(0x60b61d);
    for (unsigned step = 0; step < 16384; ++step) {
      const auto value = random() % 257;
      const auto key = id(value);
      if (random() & 1) {
        auto handle = directory.ensure(key).first;
        auto head = static_cast<std::uint32_t>(random());
        directory.get(handle).head = head; expected[key] = head;
      } else {
        auto handle = directory.find(key);
        CHECK(bool(handle) == bool(expected.count(key)));
        if (handle) CHECK(directory.erase(handle));
        expected.erase(key);
      }
      if (step % 256 == 0) contents(directory, expected);
    }
    contents(directory, expected);
    CHECK(memory.live == directory.capacity_bytes());
  }
  CHECK(memory.live == 0);
}

void measured_dense() {
  Memory memory;
  {
    Directory directory(memory);
    for (std::uint64_t n = 0; n < 4096; ++n) CHECK(directory.ensure(id(n)).second);
    CHECK(directory.size() == 4096 && memory.live == directory.capacity_bytes());
    std::printf("directory: entries=%zu entry_bytes=%zu bucket_bytes=%zu page_pointer_bytes=%zu "
        "requested_bytes=%zu fixed_record=%zu peak=%zu\n", directory.size(), directory.entry_bytes(),
        directory.bucket_bytes(), directory.page_pointer_bytes(), memory.live, sizeof(Directory), memory.peak);
  }
  CHECK(memory.live == 0);
}

int main() {
  collisions_and_stability(); pins_debt_and_reuse(); refusal(); churn_and_oracle(); measured_dense();
  std::puts("directory: full-width keys, collision/growth stability, pin/debt/reuse, refusal and independent churn oracle passed");
}

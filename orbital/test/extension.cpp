#include "extension_material.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>

using namespace extension_test;
namespace {
void check(bool condition, const char* message,
    std::source_location where = std::source_location::current()) {
  if (condition) return;
  std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), message);
  std::abort();
}

template<class T> T take(orbital::Result<T> value,
    std::source_location where = std::source_location::current()) {
  check(bool(value), "unexpected refusal", where);
  return std::move(*value);
}

struct Memory final : std::pmr::memory_resource {
  std::size_t live{};
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live += bytes;
    return result;
  }
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
    check(live >= bytes, "metadata released more than once");
    live -= bytes;
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

using Witnesses = std::array<std::weak_ptr<const std::uint64_t>, 2>;
Store::Owner capture(Store& store, Memory& memory,
    std::array<std::uint64_t, 2> numbers, Witnesses& witnesses) {
  const Control control{&memory};
  std::array<orbital::PartSource, 2> parts;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    auto body = std::make_shared<const std::uint64_t>(numbers[i]);
    witnesses[i] = body;
    parts[i] = {control.id(i), std::as_bytes(std::span{body.get(), std::size_t{1}}), body, nullptr};
  }
  return take(store.capture(control, parts));
}

constexpr orbital::ObjectId object{0x455854, 1};
void publish(Store& store, const Store::Owner& owner) {
  Store::Scope basis(store);
  Store::Prepared packet(store);
  const std::array<Store::Change, 1> changes{{{object, owner}}};
  check(bool(store.prepare(basis, changes, packet)), "publication refused");
  check(bool(store.install(packet)), "publication did not install");
}

std::uint64_t read(orbital::View view) {
  check(bool(view), "missing native Part view");
  std::uint64_t number;
  std::memcpy(&number, view.address(), sizeof(number));
  return number;
}

void expect(Selection selected, std::uint64_t first, std::uint64_t second) {
  check(bool(selected) && selected.part_count() == 2, "wrong selected geometry");
  check(read(take(selected.part(std::size_t{0}))) == first, "wrong first Part");
  check(read(take(selected.part(std::size_t{1}))) == second, "wrong second Part");
  check(!selected.part(std::size_t{2}), "out-of-range Part was admitted");
}
} // namespace

int main() {
  Memory memory;
  Witnesses original, replacement;
  orbital::Part<Control>* escaped{};
  {
    Store store(memory);
    { const auto owner = capture(store, memory, {41, 42}, original); publish(store, owner); }
    Store::Scope old(store), delayed(store);
    check(old.cut() == 1, "initial publication has wrong cut");
    expect(old.bind(object), 41, 42);
    auto exact = take(store.bound(old, object));
    { const auto owner = capture(store, memory, {141, 142}, replacement); publish(store, owner); }
    expect(store.borrow().bind(object), 141, 142);
    expect(old.bind(object), 41, 42);
    expect(delayed.bind(object), 41, 42);
    escaped = take(exact.export_part(1, orbital::PartRetention::selected_copy));
    check(read(orbital::view(escaped)) == 42, "export selected replacement bytes");
    const auto association = orbital::binding(escaped);
    check(association.object == object && association.version == 1,
        "export lost the old Object/version association");
    store = {};
    expect(old.bind(object), 41, 42);
    expect(delayed.bind(object), 41, 42);
  }
  // Only the selected body and its inline interpretation may survive the catalog.
  check(original[0].expired() && !original[1].expired(), "export retained the wrong physical owners");
  check(replacement[0].expired() && replacement[1].expired(), "replacement outlived all catalog owners");
  check(memory.live != 0 && read(orbital::view(escaped)) == 42,
      "escaping Part lost its native bytes or metadata after catalog teardown");
  orbital::release(escaped);
  check(original[1].expired() && memory.live == 0, "final Part release leaked backing or metadata");
  std::puts("public-only two-TU ObjectStore/Representation extension and lifetime checks pass");
}

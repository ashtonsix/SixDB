#pragma once
#include <orbital/object.h>

// Independent logical-catalog fixture: one integer has two equivalent encodings.
// This material supplies no Ikea format, Allocation behavior or container policy.
namespace object_test {
struct Specification {
  std::uint64_t number{};
  bool complemented{};
  void (*disposed)(void*) noexcept{};
  void* context{};
};
struct Value {
  std::pmr::memory_resource* memory;
  std::size_t references{1};
  Specification specification;
  std::uint64_t encoded;
};
struct Selection {
  Value* value{};
  explicit operator bool() const noexcept { return value; }
  std::uint64_t number() const noexcept;
};
struct Export {
  Value* value{};
  orbital::ObjectBinding binding{};
  Export(Value*, orbital::ObjectBinding) noexcept;
  Export(Export&&) noexcept;
  Export(const Export&) = delete;
  ~Export();
  std::uint64_t number() const noexcept { return Selection{value}.number(); }
};
struct Physical {
  using Value = object_test::Value;
  using Selection = object_test::Selection;
  using Specification = object_test::Specification;
  using PartSource = std::byte;
  enum class ExportPolicy { owned };
  struct Footprint { std::size_t requested_bytes{}, copied_metadata{}; };
  static void retain(Value*) noexcept;
  static void release(Value*) noexcept;
  static Value* get(Selection selected) noexcept { return selected.value; }
  static Selection selection(Value* value) noexcept { return {value}; }
  static orbital::Result<Value*> capture(std::pmr::memory_resource&, const Specification&,
      std::span<const PartSource>) noexcept;
  static bool accepts(Value*, orbital::ObjectId, std::uint64_t) noexcept { return true; }
  static bool accepts_resource(Value* value, std::pmr::memory_resource& memory) noexcept {
    return value->memory == &memory;
  }
  // Both encodings represent the same kind of integer. The converter owes equality.
  static bool compatible(Value*, Value*) noexcept { return true; }
  static Footprint footprint(Value*) noexcept { return {sizeof(Value), 0}; }
  static orbital::Result<Export> export_part(Selection, std::size_t,
      orbital::ObjectBinding, ExportPolicy) noexcept;
};
using Store = orbital::ObjectStore<Physical>;
} // namespace object_test
extern template class orbital::ObjectStore<object_test::Physical>;

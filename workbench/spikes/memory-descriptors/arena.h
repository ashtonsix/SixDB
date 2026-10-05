#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace descriptors {

using Head = std::uintptr_t;
enum class Palette { a, b };
enum class Directory { planes, records };
enum class Packing { packed, private_large };

struct Geometry {
  std::size_t page;
  std::size_t grid_bytes;
  std::size_t pitch;
  std::array<std::size_t, 16> stride{};
  std::array<std::size_t, 16> offset{};
};

Geometry geometry(Palette palette, Directory directory, Packing packing,
                  std::size_t page);
inline constexpr Head tag_mask = Head{63} << 57;
inline Head native(Head head) { return head & ~tag_mask; }
inline unsigned arena_id(Head head) { return (head >> 57) & 63; }

/// Caller protects the grid, knows the finite palette, and admits its read extent.
inline std::byte* resolve(Head head, const Geometry& g) {
  const auto a = native(head);
  const auto coordinate = __atomic_load_n(
      reinterpret_cast<const std::uint8_t*>(a), __ATOMIC_ACQUIRE);
  const auto row = (a & (g.page - 1)) / g.pitch;
  return reinterpret_cast<std::byte*>(
      (a & ~(g.page - 1)) + g.page + row * g.grid_bytes +
      g.offset[coordinate & 15] + g.stride[coordinate & 15] * (coordinate >> 4));
}

struct Counters {
  std::size_t payload_pages{};
  std::size_t released_pages{};
  std::size_t copied_bytes{};
  std::size_t blocked_moves{};
};

class Arena;
struct Row;

class Grant {
 public:
  Grant() = default;
  Grant(Grant&& other) noexcept;
  Grant& operator=(Grant&& other) noexcept;
  ~Grant();
  Grant(const Grant&) = delete;
  Grant& operator=(const Grant&) = delete;
  std::byte* data() const { return data_; }
  std::size_t size() const { return size_; }
  std::uint64_t generation() const { return generation_; }
  explicit operator bool() const { return arena_ != nullptr; }
  /// Cancellation alone leaves the grant active until backend/worker completion.
  void cancel() { cancelled_ = true; }
  void complete();
 private:
  friend class Arena;
  Arena* arena_{};
  Row* row_{};
  unsigned coordinate_{};
  std::byte* data_{};
  std::size_t size_{};
  std::uint64_t generation_{};
  bool cancelled_{};
};

class Scope {
 public:
  Scope() = default;
  Scope(Scope&& other) noexcept;
  Scope& operator=(Scope&& other) noexcept;
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
 private:
  friend class Arena;
  Arena* arena_{};
  Row* row_{};
  void complete();
};

/// Serialized owner transitions; this spike does not select a production latch.
class Arena {
 public:
  Arena(Palette palette, Directory directory, Packing packing,
        std::size_t grids, unsigned id = 1);
  ~Arena();
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  const Geometry& layout() const { return geometry_; }
  std::size_t grids() const { return rows_.size(); }
  std::size_t reserved_bytes() const { return reserved_; }
  std::size_t control_pages() const { return blocks_; }
  std::size_t owner_record_bytes() const;
  /// Capacity can exceed logical length to admit a wider native read window.
  Head create(std::size_t grid, std::size_t bytes, std::uint8_t pattern,
              std::size_t minimum_capacity = 0);
  /// Optional readable extent includes initialized padding; zero uses payload length.
  Grant borrow(Head head, std::size_t readable_extent = 0);
  Scope protect(std::size_t grid);
  /// Append preserves admitted read windows; overlapping growth moves or blocks.
  bool grow(Head head, std::size_t bytes);
  bool replace(Head head, std::size_t bytes, std::uint8_t pattern);
  /// Caller authorizes logical in-place mutation; physical users can reject it.
  bool try_inplace(Head head, std::uint8_t pattern);
  /// Caller has ended all usable descriptor aliases; raw grants may remain.
  void retire(Head head);
  Counters counters() const;
  std::byte* hint(Head head) const;
 private:
  friend class Grant;
  friend class Scope;
  static constexpr std::size_t grids_per_block = 64;
  Geometry geometry_;
  unsigned id_;
  std::byte* base_{};
  std::size_t reserved_{};
  std::size_t blocks_{};
  std::size_t block_bytes_{};
  std::vector<std::unique_ptr<Row>> rows_;
  Row& row_for(Head head) const;
  bool change(Head head, std::size_t bytes, bool copy,
              std::uint8_t pattern);
  void collect(Row& row);
  void release(Grant& grant);
  void release(Scope& scope);
};

} // namespace descriptors

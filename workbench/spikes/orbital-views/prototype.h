#pragma once
#include "recycling.h"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <vector>

// Owner operations are serialized. Native leases can outlive their owner calls.
namespace views {
using Bytes = std::vector<std::uint8_t>;
using Epoch = std::uint64_t;
struct Range { std::size_t offset, size; };
enum class Method { copy, cow, take };
enum class Error { busy, unavailable, range, version, pending, unstaged };
template<class T> using Result = std::expected<T, Error>;
struct Counts { std::size_t mapped{}, copied{}, staged{}, source_bytes{}, material_bytes{}; };
struct Image;
struct State;
struct Borrow;
struct Preparation;
struct Fetch;
struct Delivery { std::vector<std::pair<Range, Bytes>> patches; bool complete{}; };

class Lease {
 public:
  const std::uint8_t* data() const;
  std::uint8_t* writable() const;
  std::size_t size() const;
  void close() { borrow_.reset(); image_.reset(); }
  void release(recycling::Hint hint);
 private:
  friend class Part;
  friend class Draft;
  friend class Worker;
  friend struct Fetch;
  Lease(std::shared_ptr<Image> image, bool write, std::span<const std::size_t> pages);
  std::shared_ptr<Image> image_;
  std::shared_ptr<Borrow> borrow_;
  bool write_{};
};

// A captured source and actual I/O destination. No CPU lease before completion.
struct Fetch {
  int source_fd() const;
  std::span<const std::size_t> pages() const { return pages_; }
  std::span<std::uint8_t> destination(std::size_t page);
  Result<Lease> finish(bool complete);
 private:
  friend class Part;
  Fetch(std::shared_ptr<State>, std::shared_ptr<Image>, std::span<const std::size_t>);
  std::shared_ptr<State> state_;
  std::shared_ptr<Image> image_;
  std::vector<std::size_t> pages_;
  bool finished_{};
};

class Prepared {
 public:
  ~Prepared();
  Prepared(Prepared&&) noexcept;
  Prepared(const Prepared&) = delete;
  const Bytes& bytes() const;
  Result<void> install();
 private:
  friend class Draft;
  explicit Prepared(std::unique_ptr<Preparation>);
  std::unique_ptr<Preparation> preparation_;
};

class Draft {
 public:
  ~Draft();
  Draft(Draft&&) noexcept;
  Draft& operator=(Draft&&) = delete;
  Draft(const Draft&) = delete;
  Result<Lease> access(std::span<const std::size_t> pages, bool write = true);
  /// Growth preserves live addresses; exceeding the reservation requires no borrowers.
  Result<void> grow(std::size_t bytes);
  /// Captures bytes now. The returned delivery owns them through completion.
  Result<std::shared_ptr<Delivery>> stage(std::span<const Range> ranges);
  /// Seal and validate before a journal decision; install needs no allocation.
  Result<Prepared> prepare(Epoch epoch);
  Result<void> publish(Epoch epoch);
  Result<void> abort();
 private:
  friend class Part;
  Draft(std::shared_ptr<State> state, std::shared_ptr<Image> image);
  std::shared_ptr<State> state_;
  std::shared_ptr<Image> image_;
  std::vector<std::shared_ptr<Delivery>> staged_;
};

class Part {
 public:
  Part(Bytes initial, Epoch epoch = 1, bool hardware_protection = false,
       std::size_t reserve_bytes = 0, std::shared_ptr<recycling::Pool> pool = {});
  Result<Lease> open(Epoch epoch, std::span<const std::size_t> pages, Method method = Method::cow);
  Result<std::shared_ptr<Fetch>> fetch(Epoch epoch, std::span<const std::size_t> pages);
  Result<Draft> begin(Method method, std::span<const std::size_t> pages);
  bool can_take() const;
  void evict();
  Counts counts() const;
  void reset_counts();
  Epoch latest() const;
  std::size_t size() const;
  std::size_t capacity() const;
  Result<void> advance(Epoch epoch);
  /// A revision applies until the next revision of this part, not each journal epoch.
  Result<std::pair<Epoch, Epoch>> interval(Epoch epoch) const;
 private:
  std::shared_ptr<State> state_;
};

class Worker {
 public:
  /// Ranges describe exposed input and accepted output independently.
  Worker(const Lease& source, std::span<const Range> read, std::span<const Range> write);
  ~Worker();
  Worker(const Worker&) = delete;
  const std::uint8_t* data() const { return data_; }
  std::uint8_t* writable() const { return data_; }
  Result<void> apply(Lease& destination) const;
  std::size_t copied() const { return copied_; }
 private:
  std::uint8_t* data_{};
  std::size_t size_{}, copied_{};
  std::vector<Range> writes_;
  std::shared_ptr<const void> source_material_;
  std::weak_ptr<Image> source_draft_;
  bool from_draft_{};
};
std::size_t page_size();
const char* name(Method method);
}

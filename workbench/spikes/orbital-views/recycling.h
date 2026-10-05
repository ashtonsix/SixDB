#pragma once
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

namespace recycling {
enum class Temperature { hot, warm, cold };
struct Hint {
  Temperature temperature{Temperature::cold};
  int cpu{-1};
  std::chrono::steady_clock::time_point last_access{};
};
struct Counts { std::size_t maps{}, unmaps{}, reused{}, fallback{}, held{}, free{}, restored{}; };
struct State;
struct Slot;
std::size_t page_size();
class Lease {
 public:
  Lease(const Lease&);
  Lease(Lease&&) noexcept;
  Lease& operator=(const Lease&) = delete;
  ~Lease();
  std::byte* data() const;
  std::size_t size() const;
  Hint hint() const;
  void mapping_changed();
  /// Only the final borrower returns the page. Earlier hints survive backend completion.
  void close();
  void release(Temperature last_use, int last_cpu);
  void release(Hint last_use);
 private:
  friend class Pool;
  Lease(std::shared_ptr<State>, Slot*);
  std::shared_ptr<State> state_;
  Slot* slot_{};
};
class Pool {
 public:
  Pool();
  Lease acquire(Temperature request, int cpu, std::size_t bytes = page_size());
  Counts counts() const;
  /// Enqueue cold reclamation. Keep fallback pages while HOT/WARM inventory is empty.
  void trim_cold(std::size_t keep = 1);
  void wait_idle();
  void reclaim_hook(std::function<void()> hook);
 private:
  std::shared_ptr<State> state_;
};
}

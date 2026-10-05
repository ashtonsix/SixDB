#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace control {

enum class Owners { global, group, object };
enum class Metadata { ordered, bitmap };
enum class Lifetime { window, page, store };
enum class History { ordered, contiguous, captured };

struct Receipt {
  std::uint64_t operations{}, units{}, checksum{}, holds{};
  double elapsed_ns{};
};

// Epochs are supplied: completing local heads does not imply a contiguous cut.
class Prefix {
 public:
  explicit Prefix(std::size_t epochs);
  void complete(std::size_t epoch);
  std::size_t load() const;
 private:
  std::size_t size_;
  std::unique_ptr<std::atomic<std::uint64_t>[]> bits_;
  std::atomic<std::size_t> frontier_{0};
};

std::vector<int> available_cpus();
void pin_thread(int cpu);
Receipt ownership(Owners policy, unsigned threads, bool shared_group, bool cut,
                  std::size_t iterations, std::span<const int> cpus);
Receipt readiness(Metadata policy, bool cached, std::size_t pages,
                  std::span<const std::size_t> selected, std::size_t iterations);
Receipt lifetime(Lifetime policy, std::size_t pages,
                 std::span<const std::size_t> selected, std::size_t iterations);
Receipt history(History policy, std::size_t revisions, std::size_t iterations);
Receipt combined(bool compact, std::size_t blocks,
                 std::span<const std::size_t> selected, std::size_t iterations);

}  // namespace control

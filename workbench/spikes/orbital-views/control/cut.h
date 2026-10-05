#pragma once

#include "mechanisms.h"

#include <atomic>
#include <memory>

namespace control {
enum class CutKind { bitmap, producers };

class Cuts {
 public:
  Cuts(CutKind kind, unsigned producers, std::size_t rounds);
  // Producer ownership and strictly ordered rounds are supplied. A skipped
  // local round is rejected rather than advertising a hole as completed.
  bool complete(unsigned producer, std::size_t round);
  std::size_t query() const;
 private:
  struct alignas(128) Progress { std::atomic<std::size_t> rounds{0}; };
  CutKind kind_;
  unsigned producers_;
  std::size_t rounds_;
  std::unique_ptr<Prefix> prefix_;
  std::unique_ptr<Progress[]> progress_;
};

struct CutReceipt {
  std::size_t publications{}, queries{}, final_cut{}, max_head_distance{}, max_unqueried_updates{};
  std::uint64_t checksum{}, query_checksum{};
  double elapsed_ns{};
};
CutReceipt cut_updates(CutKind kind, unsigned producers, std::size_t cadence,
                      std::size_t rounds, std::span<const int> cpus);
CutReceipt cut_queries(CutKind kind, unsigned producers, std::size_t queries);
}  // namespace control

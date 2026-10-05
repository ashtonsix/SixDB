#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace residency {
struct Counts {
  std::uint64_t maps{}, unmaps{}, protects{}, copied{}, zeroed{}, prepared{};
};
struct Profile {
  std::string name;
  std::size_t pages;
  std::vector<std::size_t> selected;
};
struct Sample {
  std::string method, profile, readers;
  std::size_t pages{}, selected{}, iterations{}, page_bytes{};
  double seed_ns{}, initial_ns{}, draft_ns{}, use_ns{}, old_ns{}, next_ns{}, release_ns{};
  Counts seed_counts{}, counts{};
  long minor_faults{}, rss_before_kib{}, peak_rss_kib{}, vmas_before{}, vmas_peak{};
  std::size_t pool_bytes{}, checks{};
};
std::size_t page_size();
void require(bool, const char*);
long rss_kib();
long vma_count();
std::vector<Profile> profiles();
Sample pipeline(const std::string& method, const Profile&, const std::string& readers, int iterations);
void memory_sweep(bool check_only);
void print_header();
void print(const Sample&);
}

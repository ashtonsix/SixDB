#include "prototype.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string_view>
#include <sched.h>
#include <sys/resource.h>
#include <sys/utsname.h>

using namespace views;
namespace {
volatile std::uint64_t observed{};
template<class T> T get(Result<T> r) { if (!r) std::abort(); return std::move(*r); }
void done(Result<void> r) { if (!r) std::abort(); }
void touch(const Lease& lease, std::span<const std::size_t> pages) {
  std::uint64_t checksum{};
  for (auto p : pages) checksum += lease.data()[p * page_size()];
  observed = checksum;
}
void measure(std::size_t page_count, std::size_t selected, Method method, bool protected_pages, int repeats) {
  std::vector<std::size_t> pages(selected); std::iota(pages.begin(), pages.end(), 0);
  Bytes base(page_count * page_size(), 19); Part part(base, 1, protected_pages);
  double ns{}, prepare_ns{}, close_ns{}; long faults{}; Counts work{};
  for (int i = 0; i < repeats; ++i) {
    part.evict(); { auto warm = get(part.open(1, pages, Method::copy)); touch(warm, pages); }
    part.reset_counts(); rusage before{}, after{}; ::getrusage(RUSAGE_SELF, &before);
    const auto start = std::chrono::steady_clock::now();
    auto draft = get(part.begin(method, pages)); auto access = get(draft.access(pages));
    const auto prepared = std::chrono::steady_clock::now();
    // Read every prepared page and write 64 bytes: all methods perform identical work.
    touch(access, pages); auto* output = access.writable();
    for (std::size_t j = 0; j < 64; ++j) output[j] = static_cast<std::uint8_t>(j + 1);
    observed = output[0] + output[63];
    if (output[0] != 1 || output[63] != 64) std::abort();
    const auto used = std::chrono::steady_clock::now();
    access.close(); done(draft.abort());
    const auto end = std::chrono::steady_clock::now(); ::getrusage(RUSAGE_SELF, &after);
    ns += std::chrono::duration<double, std::nano>(end - start).count(); faults += after.ru_minflt - before.ru_minflt;
    prepare_ns += std::chrono::duration<double, std::nano>(prepared - start).count();
    close_ns += std::chrono::duration<double, std::nano>(end - used).count();
    const auto c = part.counts(); work.copied += c.copied; work.mapped += c.mapped; work.source_bytes += c.source_bytes;
  }
  std::printf("%zu,%zu,%s,%d,%d,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%zu\n", page_count * page_size(), selected,
      name(method), protected_pages, repeats, ns / repeats, prepare_ns / repeats, close_ns / repeats, double(faults) / repeats,
      work.copied / repeats, work.mapped / repeats, work.source_bytes / repeats);
}
}
int main(int argc, char** argv) {
  const bool reverse = argc > 1 && std::string_view(argv[1]) == "--reverse";
  const int repeats = 120;
  std::printf("part_bytes,prepared_pages,method,hardware_protection,repeats,mean_ns,prepare_ns,close_ns,minor_faults,copy_bytes,mapped_bytes,source_bytes\n");
  std::vector<Method> methods{Method::copy, Method::cow, Method::take};
  if (reverse) std::reverse(methods.begin(), methods.end());
  for (auto n : {1u, 16u, 256u}) for (auto selected : {1u, n}) {
    if (n == 1 && selected != 1) continue;
    for (bool h : {false, true}) for (auto method : methods) measure(n, selected, method, h, repeats);
    if (n == 1) break;
  }
}

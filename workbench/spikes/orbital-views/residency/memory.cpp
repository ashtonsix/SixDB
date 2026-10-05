#include "core.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sstream>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <csignal>

namespace residency {
using Clock = std::chrono::steady_clock;
struct Memory { long rss{}, pss{}, huge{}, faults{}, vmas{}; };
static Memory memory() {
  Memory result; rusage use{}; require(getrusage(RUSAGE_SELF, &use) == 0, "usage");
  result.faults = use.ru_minflt; result.vmas = vma_count();
  std::ifstream input("/proc/self/smaps_rollup"); std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line); std::string key; long value;
    if (!(fields >> key >> value)) continue;
    if (key == "Rss:") result.rss = value;
    if (key == "Pss:") result.pss = value;
    if (key == "AnonHugePages:") result.huge = value;
  }
  return result;
}
struct Record {
  std::string kind, method, profile;
  std::size_t capacity{}, used{}, iterations{};
  double ns{}, seed_ns{}, release_ns{}, reuse_ns{};
  Counts counts;
  Memory before{}, after{};
  bool stable{true};
  long release_rss_delta{};
  std::size_t advises{};
  Record(std::string k, std::string m, std::string p, std::size_t cap, std::size_t u, std::size_t i)
      : kind(std::move(k)), method(std::move(m)), profile(std::move(p)), capacity(cap), used(u), iterations(i) {}
};
static void output(const Record& r) {
  std::printf("%s,%s,%s,%zu,%zu,%zu,%.2f,%.2f,%llu,%llu,%llu,%llu,%llu,%ld,%ld,%ld,%ld,%ld,%d,%.2f,%.2f,%ld,%zu\n",
    r.kind.c_str(), r.method.c_str(), r.profile.c_str(), r.capacity, r.used, r.iterations,
    r.ns, r.seed_ns, static_cast<unsigned long long>(r.counts.maps),
    static_cast<unsigned long long>(r.counts.protects), static_cast<unsigned long long>(r.counts.unmaps),
    static_cast<unsigned long long>(r.counts.copied), static_cast<unsigned long long>(r.counts.zeroed),
    r.after.rss - r.before.rss, r.after.pss - r.before.pss, r.after.huge - r.before.huge,
    r.after.faults - r.before.faults, r.after.vmas - r.before.vmas, r.stable,
    r.release_ns, r.reuse_ns, r.release_rss_delta, r.advises);
  std::fflush(stdout);
}
static std::uint8_t* map(std::size_t bytes, int protection, Counts& c, int fd = -1) {
  auto* p = static_cast<std::uint8_t*>(mmap(nullptr, bytes, protection,
                 MAP_PRIVATE | (fd < 0 ? MAP_ANONYMOUS : 0), fd, 0));
  require(p != MAP_FAILED, "memory map"); ++c.maps; return p;
}
static void protect(std::uint8_t* p, std::size_t bytes, int rights, Counts& c) {
  require(mprotect(p, bytes, rights) == 0, "memory protect"); ++c.protects;
}
static void unmap(std::uint8_t* p, std::size_t bytes, Counts& c) {
  require(munmap(p, bytes) == 0, "memory unmap"); ++c.unmaps;
}
static void reservation(const std::string& method, std::size_t capacity, std::size_t used) {
  Record r{"reservation", method, "prefix", capacity, used, 1}; r.before = memory();
  auto start = Clock::now();
  auto* p = map(capacity, method == "none_runs" ? PROT_NONE : PROT_READ | PROT_WRITE, r.counts);
  if (method == "thp_advice") require(madvise(p, capacity, MADV_HUGEPAGE) == 0, "THP advice");
  else require(madvise(p, capacity, MADV_NOHUGEPAGE) == 0, "base pages");
  if (method == "none_runs") protect(p, used * page_size(), PROT_READ | PROT_WRITE, r.counts);
  if (method == "eager_all") { std::memset(p, 0, capacity); r.counts.zeroed += capacity; }
  for (std::size_t i = 0; i != used; ++i) std::memset(p + i * page_size(), 19, 64);
  r.ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
  for (std::size_t i = 0; i != used; ++i) require(p[i * page_size() + 63] == 19, "reservation bytes");
  r.after = memory(); unmap(p, capacity, r.counts); output(r);
}
static void old_sources(std::size_t number) {
  constexpr std::size_t capacity = 64u << 20;
  Record r{"old_sources", "pinned_sparse", "16_pages_each", number * capacity, number * 16, 1};
  r.before = memory(); auto start = Clock::now();
  std::vector<int> fds; std::vector<std::uint8_t*> mappings;
  std::vector<std::uint8_t> bytes(page_size(), 73);
  for (std::size_t n = 0; n != number; ++n) {
    int fd = static_cast<int>(syscall(SYS_memfd_create, "old-material", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    require(fd >= 0 && ftruncate(fd, capacity) == 0, "old source");
    for (std::size_t i = 0; i != 16; ++i)
      require(pwrite(fd, bytes.data(), bytes.size(), static_cast<off_t>(i * (capacity / 16))) == static_cast<ssize_t>(bytes.size()), "old source bytes");
    require(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) == 0, "old seal");
    auto* p = map(capacity, PROT_READ, r.counts, fd); fds.push_back(fd); mappings.push_back(p);
    for (std::size_t i = 0; i != 16; ++i) require(p[i * (capacity / 16)] == 73, "old source oracle");
  }
  r.ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
  r.after = memory();
  for (auto* p : mappings) unmap(p, capacity, r.counts);
  for (auto fd : fds) require(close(fd) == 0, "old fd close"); output(r);
}
static void growth(const std::string& method) {
  constexpr std::size_t initial = 1u << 20, final = 16u << 20, capacity = 64u << 20;
  Record r{"growth", method, "1MiB_to_16MiB", capacity, final / page_size(), 1};
  Counts seed; auto seed_start = Clock::now();
  auto* base = map(method == "reserved_rw" || method == "reserved_none" ? capacity : initial,
                   method == "reserved_none" ? PROT_NONE : PROT_READ | PROT_WRITE, seed);
  if (method == "reserved_none") protect(base, initial, PROT_READ | PROT_WRITE, seed);
  std::memset(base, 27, initial);
  r.seed_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - seed_start).count());
  auto* capture = base; const auto original = reinterpret_cast<std::uintptr_t>(base);
  r.before = memory(); auto start = Clock::now();
  if (method == "reserved_none") protect(base + initial, final - initial, PROT_READ | PROT_WRITE, r.counts);
  else if (method == "copy_with_reader") {
    auto* next = map(final, PROT_READ | PROT_WRITE, r.counts);
    std::memcpy(next, base, initial); r.counts.copied += initial; base = next;
  } else if (method == "mremap_no_reader") {
    // No capture may remain active across this potentially moving operation.
    capture = nullptr;
    auto* next = static_cast<std::uint8_t*>(mremap(base, initial, final, MREMAP_MAYMOVE));
    require(next != MAP_FAILED, "grow remap"); base = next; ++r.counts.maps;
  }
  for (std::size_t offset = initial; offset != final; offset += page_size()) std::memset(base + offset, 38, 64);
  r.ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
  require(base[0] == 27 && base[initial - 1] == 27 && base[final - page_size()] == 38, "growth oracle");
  if (capture) require(capture[0] == 27 && capture[initial - 1] == 27, "captured old allocation");
  r.stable = original == reinterpret_cast<std::uintptr_t>(base); r.after = memory();
  unmap(base, method == "reserved_rw" || method == "reserved_none" ? capacity : final, r.counts);
  if (method == "copy_with_reader") unmap(capture, initial, r.counts); output(r);
}
static void projection(const std::string& method, bool scattered, bool check_only) {
  const auto page = page_size(); constexpr std::size_t total = 256, selected = 16;
  Record r{"projection", method, scattered ? "scattered" : "contiguous", total * page, selected,
           static_cast<std::size_t>(check_only ? 2 : 128)};
  std::vector<std::size_t> pages;
  for (std::size_t i = 0; i != selected; ++i) pages.push_back(i * (scattered ? 16 : 1));
  std::vector<std::uint8_t> source(total * page, 0xD7), destination = source;
  Counts seed; auto seed_start = Clock::now();
  auto* p = map(total * page, PROT_NONE, seed);
  for (auto i : pages) {
    require(mmap(p + i * page, page, PROT_READ | PROT_WRITE,
             MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == p + i * page, "projection prepare");
    p[i * page] = 0; ++seed.maps;
  }
  r.seed_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - seed_start).count());
  auto each_readonly_run = [&](auto&& f) {
    for (std::size_t i = 0; i != selected / 2;) {
      std::size_t j = i + 1;
      if (method != "private_page_protect")
        while (j != selected / 2 && pages[j] == pages[j - 1] + 1) ++j;
      f(p + pages[i] * page, (j - i) * page); i = j;
    }
  };
  r.before = memory(); auto start = Clock::now(); std::uint64_t sum = 0;
  for (std::size_t iteration = 0; iteration != r.iterations; ++iteration) {
    if (iteration && method != "trusted_partial") each_readonly_run([&](auto* at, auto n) {
      protect(at, n, PROT_READ | PROT_WRITE, r.counts);
    });
    for (std::size_t index = 0; index != pages.size(); ++index) {
      auto i = pages[index];
      // Read-only pages retain zeros outside the fixed input mask. Writable pages
      // may contain arbitrary worker stores and therefore need complete clearing.
      if (method != "trusted_partial" && (method != "private_selective_clear" || index >= selected / 2)) {
        std::memset(p + i * page, 0, page); r.counts.zeroed += page;
      }
      std::memcpy(p + i * page, source.data() + i * page, 32); r.counts.copied += 32;
    }
    if (method != "trusted_partial") each_readonly_run([&](auto* at, auto n) { protect(at, n, PROT_READ, r.counts); });
    for (std::size_t i = 0; i != selected; ++i) {
      sum += p[pages[i] * page];
      if (i >= selected / 2) std::memset(p + pages[i] * page + 64, 91, 32);
    }
    for (std::size_t i = selected / 2; i != selected; ++i) {
      std::memcpy(destination.data() + pages[i] * page + 64, p + pages[i] * page + 64, 32); r.counts.copied += 32;
    }
  }
  r.ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()) / r.iterations;
  require(sum == r.iterations * selected * 0xD7, "projection input oracle");
  for (std::size_t i = 0; i != total * page; ++i) {
    bool changed = false;
    for (std::size_t j = selected / 2; j != selected; ++j)
      changed |= i >= pages[j] * page + 64 && i < pages[j] * page + 96;
    require(destination[i] == (changed ? 91 : 0xD7), "projection output mask");
  }
  if (method != "trusted_partial") {
    for (auto i : pages) require(p[i * page + 32] == 0 && p[i * page + page - 1] == 0, "masked secrets");
    pid_t child = fork(); require(child >= 0, "readonly child");
    if (!child) { *static_cast<volatile std::uint8_t*>(p) = 99; _exit(0); }
    int status; require(waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV, "readonly enforcement");
    std::memset(p + pages.back() * page, 0xA9, page);
    std::memcpy(destination.data() + pages.back() * page + 64, p + pages.back() * page + 64, 32);
    require(destination[pages.back() * page] == 0xD7 && destination[pages.back() * page + 96] == 0xD7,
            "malicious output confined");
  }
  r.after = memory(); unmap(p, total * page, r.counts); output(r);
}
static void trim(const std::string& method, std::size_t used, bool scattered) {
  constexpr std::size_t capacity = 64u << 20;
  const auto page = page_size();
  Record r{"trim", method, scattered ? "scattered" : "prefix", capacity, used, 1};
  Counts seed;
  auto seed_start = Clock::now();
  auto* p = map(capacity, PROT_READ | PROT_WRITE, seed);
  require(madvise(p, capacity, MADV_NOHUGEPAGE) == 0, "trim base pages");
  std::vector<std::size_t> pages;
  const auto stride = scattered ? capacity / page / used : 1;
  for (std::size_t i = 0; i != used; ++i) {
    pages.push_back(i * stride);
    std::memset(p + pages.back() * page, 0x43, page);
  }
  r.seed_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - seed_start).count());
  r.before = memory(); auto start = Clock::now();
  auto advise = [&](std::uint8_t* at, std::size_t n) {
    require(madvise(at, n, method == "free_touched" ? MADV_FREE : MADV_DONTNEED) == 0, "trim advice");
    ++r.advises;
  };
  if (method == "dontneed_whole") advise(p, capacity);
  else if (method != "retain") {
    for (std::size_t i = 0; i != pages.size();) {
      std::size_t j = i + 1;
      while (j != pages.size() && pages[j] == pages[j - 1] + 1) ++j;
      advise(p + pages[i] * page, (j - i) * page); i = j;
    }
  }
  r.release_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
  r.release_rss_delta = memory().rss - r.before.rss;
  start = Clock::now();
  for (auto i : pages) {
    const auto old = *static_cast<volatile std::uint8_t*>(p + i * page);
    require(method == "retain" ? old == 0x43 : method == "free_touched" ? old == 0 || old == 0x43 : old == 0,
            "discarded-page contents");
    std::memset(p + i * page, 0x53, 64);
  }
  r.reuse_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
  r.ns = r.release_ns + r.reuse_ns;
  for (auto i : pages) require(p[i * page + 63] == 0x53, "trim reuse bytes");
  r.after = memory(); unmap(p, capacity, r.counts); output(r);
}
void memory_sweep(bool check_only) {
  std::puts("kind,method,profile,capacity_bytes,used_pages,iterations,total_ns,seed_ns,maps,protects,unmaps,copied_bytes,zeroed_bytes,rss_delta_kib,pss_delta_kib,anonhuge_delta_kib,minor_faults,vma_delta,stable_address,release_ns,reuse_ns,release_rss_delta_kib,advises");
  std::fflush(stdout);
  auto isolate = [&](auto&& f) {
    pid_t child = fork(); require(child >= 0, "memory cell fork");
    if (!child) {
      // Prime stream/allocator metadata work before taking an RSS baseline.
      static_cast<void>(memory()); static_cast<void>(memory());
      f(); _exit(0);
    }
    int status; require(waitpid(child, &status, 0) == child && status == 0, "memory cell exit");
  };
  for (std::size_t capacity : {1u << 20, 64u << 20, 1u << 30})
    for (std::string method : {"none_runs", "rw_lazy", "thp_advice", "eager_all"}) {
      if (method == "eager_all" && capacity > (64u << 20)) continue;
      isolate([&] { reservation(method, capacity, std::min(capacity / page_size(), std::size_t{256})); });
    }
  for (std::size_t n : {32u, 256u}) isolate([&] { old_sources(n); });
  for (std::string method : {"reserved_rw", "reserved_none", "copy_with_reader", "mremap_no_reader"})
    isolate([&] { growth(method); });
  for (bool scattered : {false, true}) for (std::string method : {"trusted_partial", "private_page_protect", "private_run_protect", "private_selective_clear"})
    isolate([&] { projection(method, scattered, check_only); });
  for (std::size_t used : {16u, 256u}) for (bool scattered : {false, true})
    for (std::string method : {"retain", "dontneed_touched", "dontneed_whole", "free_touched"})
      isolate([&] { trim(method, used, scattered); });
}
}

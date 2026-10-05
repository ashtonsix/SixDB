#include "core.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

namespace residency {
using Clock = std::chrono::steady_clock;
void require(bool good, const char* what) {
  if (!good) { std::perror(what); std::exit(2); }
}
std::size_t page_size() {
  static const auto bytes = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  return bytes;
}
long rss_kib() {
  std::ifstream input("/proc/self/status"); std::string key, line;
  while (std::getline(input, line)) if (line.starts_with("VmRSS:")) {
    std::istringstream fields(line); long value; fields >> key >> value; return value;
  }
  return -1;
}
long vma_count() {
  std::ifstream input("/proc/self/maps"); std::string line; long count = 0;
  while (std::getline(input, line)) ++count;
  return count;
}
static long faults() {
  rusage usage{}; require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage");
  return usage.ru_minflt;
}
static long peak_rss() {
  rusage usage{}; require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage");
  return usage.ru_maxrss;
}
std::vector<Profile> profiles() {
  std::vector<Profile> result;
  for (auto [name, total, used, stride] : {
      std::tuple{"small", 4u, 4u, 1u}, std::tuple{"dense", 256u, 256u, 1u},
      std::tuple{"sparse_runs", 4096u, 16u, 1u}, std::tuple{"sparse_scattered", 4096u, 16u, 256u}}) {
    Profile p{name, total, {}};
    for (std::size_t i = 0; i != used; ++i) p.selected.push_back(i * stride);
    result.push_back(std::move(p));
  }
  return result;
}
struct Window {
  std::uint8_t* data;
  std::size_t bytes;
  Counts& counts;
  Window(std::size_t size, Counts& c, int protection = PROT_READ | PROT_WRITE,
         int fd = -1, int flags = MAP_PRIVATE) : bytes(size), counts(c) {
    data = static_cast<std::uint8_t*>(mmap(nullptr, bytes, protection,
             flags | (fd < 0 ? MAP_ANONYMOUS : 0), fd, 0));
    require(data != MAP_FAILED, "map window"); ++counts.maps;
    if (fd < 0) require(madvise(data, bytes, MADV_NOHUGEPAGE) == 0, "disable THP");
  }
  ~Window() { require(munmap(data, bytes) == 0, "unmap window"); ++counts.unmaps; }
  void replace(std::size_t offset, std::size_t length, int fd = -1) {
    require(mmap(data + offset, length, PROT_READ | PROT_WRITE,
                 MAP_FIXED | MAP_PRIVATE | (fd < 0 ? MAP_ANONYMOUS : 0),
                 fd, fd < 0 ? 0 : static_cast<off_t>(offset)) == data + offset, "replace range");
    ++counts.maps;
  }
};
static std::uint8_t expected(std::size_t page, std::size_t offset, int version) {
  return static_cast<std::uint8_t>(page * 13 + offset * 7 + 91 + (offset < 64 ? version : 0));
}
struct Source {
  int fd;
  std::unique_ptr<Window> mapping;
  Source(const Profile& p, int version, Counts& c) {
    std::vector<std::uint8_t> bytes(p.pages * page_size());
    for (std::size_t i = 0; i != p.pages; ++i)
      for (std::size_t j = 0; j != page_size(); ++j) bytes[i * page_size() + j] = expected(i, j, 0);
    for (auto i : p.selected) for (std::size_t j = 0; j != 64; ++j)
      bytes[i * page_size() + j] = expected(i, j, version);
    fd = static_cast<int>(syscall(SYS_memfd_create, "residency-source", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    require(fd >= 0 && ftruncate(fd, static_cast<off_t>(bytes.size())) == 0, "source fd");
    std::size_t done = 0;
    while (done != bytes.size()) {
      auto n = pwrite(fd, bytes.data() + done, bytes.size() - done, static_cast<off_t>(done));
      require(n > 0, "source bytes"); done += static_cast<std::size_t>(n);
    }
    c.copied += bytes.size();
    require(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) == 0, "source seal");
    mapping = std::make_unique<Window>(bytes.size(), c, PROT_READ, fd);
    volatile std::uint8_t sink = 0;
    for (std::size_t i = 0; i != p.pages; ++i) sink = mapping->data[i * page_size()];
    static_cast<void>(sink);
  }
  ~Source() { mapping.reset(); require(close(fd) == 0, "close source"); }
};
struct Image {
  std::shared_ptr<Window> home;
  std::vector<std::uint64_t> ready;
  int version{};
  bool writable{};
  bool restore{};
  ~Image() { if (restore) home->replace(0, home->bytes); }
  bool contains(std::size_t p) const { return (ready[p / 64] >> (p % 64)) & 1; }
};
struct Pool {
  std::vector<std::shared_ptr<Window>> homes;
  const Profile& profile;
  Counts& counts;
  Pool(const Profile& p, Counts& c) : profile(p), counts(c) {}
  std::shared_ptr<Window> acquire() {
    for (auto& h : homes) if (h.use_count() == 1) return h;
    auto result = std::make_shared<Window>(profile.pages * page_size(), counts);
    homes.push_back(result); return result;
  }
  void seed(std::size_t number) {
    for (std::size_t i = 0; i != number; ++i) {
      auto h = std::make_shared<Window>(profile.pages * page_size(), counts);
      for (auto p : profile.selected) h->data[p * page_size()] = 0;
      homes.push_back(std::move(h));
    }
  }
};
template <class Function> static void runs(const std::vector<std::size_t>& pages, Function&& f) {
  for (std::size_t i = 0; i != pages.size();) {
    std::size_t j = i + 1;
    while (j != pages.size() && pages[j] == pages[j - 1] + 1) ++j;
    f(pages[i] * page_size(), (j - i) * page_size()); i = j;
  }
}
static std::shared_ptr<Image> acquire(const std::string& method, const Profile& p,
    const Source& source, const std::shared_ptr<Image>& current, Pool& pool, Counts& c) {
  if (method == "retained_take" && current && current.use_count() == 1) {
    require(!current->writable, "take published only");
    current->writable = true; return current;
  }
  auto result = std::make_shared<Image>(); result->ready.resize((p.pages + 63) / 64);
  result->writable = true;
  bool file = method == "file_lazy" || method == "file_populate" || method == "file_runs";
  bool retained = method == "retained_copy" || method == "retained_take" || method == "pool_rebind";
  if (retained) result->home = pool.acquire();
  else if (method == "file_lazy" || method == "file_populate")
    result->home = std::make_shared<Window>(p.pages * page_size(), c, PROT_READ | PROT_WRITE,
                         source.fd, MAP_PRIVATE | (method == "file_populate" ? MAP_POPULATE : 0));
  else result->home = std::make_shared<Window>(p.pages * page_size(), c,
                       method == "anon_eager" ? PROT_READ | PROT_WRITE : PROT_NONE);
  if (method == "pool_rebind") {
    require(mprotect(result->home->data, result->home->bytes, PROT_NONE) == 0, "pooled protection reset");
    ++c.protects; result->restore = true;
  }
  if (method == "anon_eager") {
    std::memset(result->home->data, 0, result->home->bytes); c.zeroed += result->home->bytes;
  }
  auto prepare = [&](std::size_t offset, std::size_t length) {
    if ((!retained || method == "pool_rebind") && method != "file_lazy" && method != "file_populate" && method != "anon_eager")
      result->home->replace(offset, length, file ? source.fd : -1);
    if (!file) {
      const auto* input = current ? current->home->data : source.mapping->data;
      std::memcpy(result->home->data + offset, input + offset, length); c.copied += length;
    }
    c.prepared += length / page_size();
    if (method == "page_copy" || method == "pool_rebind") {
      require(mprotect(result->home->data + offset, length, PROT_READ | PROT_WRITE) == 0, "per-page rights");
      ++c.protects;
    }
  };
  if (method == "page_copy" || method == "pool_rebind") {
    std::set<std::size_t> ready;
    for (auto page : p.selected) { ready.insert(page); prepare(page * page_size(), page_size()); }
  } else runs(p.selected, prepare);
  for (auto page : p.selected) result->ready[page / 64] |= std::uint64_t{1} << (page % 64);
  return result;
}
static std::uint64_t read(const std::uint8_t* p, const Profile& profile) {
  std::uint64_t sum = 0;
  for (auto page : profile.selected)
    for (std::size_t j = 0; j != 64; ++j) sum += p[page * page_size() + j] * (1 + j + page * 67);
  return sum;
}
static std::uint64_t oracle(const Profile& p, int version) {
  std::uint64_t sum = 0;
  for (auto page : p.selected) for (std::size_t j = 0; j != 64; ++j) sum += expected(page, j, version) * (1 + j + page * 67);
  return sum;
}
static std::uint64_t update(Image& image, const Profile& p) {
  require(image.writable, "draft writable");
  for (auto page : p.selected) {
    require(image.contains(page), "prepared page");
    for (std::size_t j = 0; j != 64; ++j) ++image.home->data[page * page_size() + j];
  }
  return read(image.home->data, p);
}
template <class Function> static void measure(double& total, Function&& f) {
  const auto begin = Clock::now(); f();
  total += static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}
Sample pipeline(const std::string& method, const Profile& p, const std::string& readers, int iterations) {
  Sample sample; sample.method = method; sample.profile = p.name; sample.readers = readers;
  sample.pages = p.pages; sample.selected = p.selected.size(); sample.iterations = iterations;
  sample.page_bytes = page_size();
  Counts c;
  auto seed_start = Clock::now();
  Source source0(p, 0, c), source1(p, 1, c); Pool pool(p, c);
  if (method == "retained_copy" || method == "retained_take" || method == "pool_rebind") pool.seed(3);
  sample.seed_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - seed_start).count());
  sample.seed_counts = c; sample.rss_before_kib = rss_kib(); sample.vmas_before = vma_count();
  const auto faults_before = faults();
  for (int iteration = 0; iteration != iterations; ++iteration) {
    std::shared_ptr<Image> current, held, draft;
    std::uint64_t initial = 0, first = 0, second = 0, old = 0;
    measure(sample.initial_ns, [&] {
      current = acquire(method, p, source0, {}, pool, c); current->writable = false;
      initial = read(current->home->data, p);
      if (readers == "held") held = current;
    });
    measure(sample.draft_ns, [&] { draft = acquire(method, p, source0, current, pool, c); current.reset(); });
    measure(sample.use_ns, [&] { first = update(*draft, p); draft->version = 1; draft->writable = false; current = std::move(draft); });
    const bool old_due = readers == "held" || readers == "late_map" || readers == "late_pinned" ||
                         (readers == "late_1_in_8" && iteration % 8 == 0);
    if (old_due) measure(sample.old_ns, [&] {
      if (held) old = read(held->home->data, p);
      else if (readers == "late_pinned") old = read(source0.mapping->data, p);
      else { Window historical(p.pages * page_size(), c, PROT_READ, source0.fd); old = read(historical.data, p); }
    });
    measure(sample.next_ns, [&] {
      draft = acquire(method, p, source1, current, pool, c); current.reset();
      second = update(*draft, p); draft->version = 2; draft->writable = false;
    });
    if (iteration == 0) {
      sample.vmas_peak = vma_count();
      for (auto page : p.selected) for (std::size_t j = 0; j != page_size(); ++j)
        require(draft->home->data[page * page_size() + j] == expected(page, j, 2), "full-page version oracle");
    }
    require(initial == oracle(p, 0) && first == oracle(p, 1) && second == oracle(p, 2), "version oracle");
    require(!old_due || old == oracle(p, 0), "old version oracle");
    sample.checks += old_due ? 4 : 3;
    measure(sample.release_ns, [&] { held.reset(); draft.reset(); current.reset(); });
  }
  sample.minor_faults = faults() - faults_before; sample.peak_rss_kib = peak_rss();
  sample.counts = {c.maps - sample.seed_counts.maps, c.unmaps - sample.seed_counts.unmaps,
    c.protects - sample.seed_counts.protects, c.copied - sample.seed_counts.copied,
    c.zeroed - sample.seed_counts.zeroed, c.prepared - sample.seed_counts.prepared};
  sample.pool_bytes = pool.homes.size() * p.pages * page_size();
  for (auto field : {&sample.initial_ns, &sample.draft_ns, &sample.use_ns, &sample.old_ns, &sample.next_ns, &sample.release_ns}) *field /= iterations;
  return sample;
}
void print_header() {
  std::puts("method,profile,readers,pages,selected,iterations,page_bytes,seed_ns,initial_ns,draft_ns,use_ns,old_ns,next_ns,release_ns,maps,unmaps,protects,copied_bytes,zeroed_bytes,prepared_pages,seed_maps,seed_copied_bytes,minor_faults,rss_before_kib,peak_rss_kib,vmas_before,vmas_peak,pool_bytes,checks");
}
void print(const Sample& s) {
  std::printf("%s,%s,%s,%zu,%zu,%zu,%zu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%ld,%ld,%ld,%ld,%ld,%zu,%zu\n",
    s.method.c_str(), s.profile.c_str(), s.readers.c_str(), s.pages, s.selected, s.iterations, s.page_bytes,
    s.seed_ns, s.initial_ns, s.draft_ns, s.use_ns, s.old_ns, s.next_ns, s.release_ns,
    static_cast<unsigned long long>(s.counts.maps), static_cast<unsigned long long>(s.counts.unmaps),
    static_cast<unsigned long long>(s.counts.protects), static_cast<unsigned long long>(s.counts.copied),
    static_cast<unsigned long long>(s.counts.zeroed), static_cast<unsigned long long>(s.counts.prepared),
    static_cast<unsigned long long>(s.seed_counts.maps), static_cast<unsigned long long>(s.seed_counts.copied),
    s.minor_faults, s.rss_before_kib, s.peak_rss_kib, s.vmas_before, s.vmas_peak, s.pool_bytes, s.checks);
  std::fflush(stdout);
}
}

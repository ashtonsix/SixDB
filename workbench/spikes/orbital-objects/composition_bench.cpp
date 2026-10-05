#include "composition_fixture.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace object_fixture;
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t ns(Clock::duration d) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}
void need(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void socket_io(int fd, std::uint8_t* bytes, std::size_t size, bool send) {
  std::size_t at{};
  while (at < size) {
    const auto n = send ? ::send(fd, bytes + at, size - at, MSG_NOSIGNAL)
                        : ::recv(fd, bytes + at, size - at, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) throw std::runtime_error("socket length/error");
    at += static_cast<std::size_t>(n);
  }
}
struct Sample {
  std::uint64_t operations{}, time{}, checksum{}, maps{}, unmaps{}, copies{},
      fetched{}, reconstructed{}, grants{}, reused{}, replacements{}, discarded{},
      zeroed{}, peak{}, peak_active{}, cached_end{}, home_end{}, peak_home{};
  void counts(const Counts& c) {
    maps += c.maps; unmaps += c.unmaps; copies += c.copies;
    fetched += c.fetched; reconstructed += c.reconstructed;
    grants += c.grants; reused += c.reused; replacements += c.replacements;
    discarded += c.discarded; zeroed += c.zeroed;
    peak = std::max(peak, c.peak);
    peak_active = std::max(peak_active, c.peak_active);
    peak_home = std::max(peak_home, c.peak_home);
    cached_end = c.cached; home_end = c.charged - c.cached;
    need(c.active == 0 && c.undo == 0 && c.unreleased == 0, "measured operation fully retired");
  }
};
void print(const char* strategy, const char* history, std::size_t cache,
           unsigned repeat, const Sample& s, long minor, long major) {
  std::printf("%s,%s,%zu,%u,%zu,%zu,%zu,%zu,%zu,%zu,%llu,%llu,%.3f,%llu,%llu,%ld,%ld,"
              "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
      strategy, history, cache, repeat, page(), 4 * page(),
      std::string(history) == "file_socket" ? 1ul : 4ul,
      std::string(strategy) == "direct" ? 0ul : 72ul,
      std::string(history) == "file_socket" ? page() : 0ul, 8 * page(),
      static_cast<unsigned long long>(s.operations), static_cast<unsigned long long>(s.time),
      static_cast<double>(s.time) / s.operations,
      static_cast<unsigned long long>(s.checksum),
      static_cast<unsigned long long>(s.operations * (2 * 123 + 46 * (11 + 14))),
      minor, major,
      static_cast<unsigned long long>(s.maps), static_cast<unsigned long long>(s.unmaps),
      static_cast<unsigned long long>(s.copies), static_cast<unsigned long long>(s.fetched),
      static_cast<unsigned long long>(s.reconstructed), static_cast<unsigned long long>(s.grants),
      static_cast<unsigned long long>(s.reused), static_cast<unsigned long long>(s.replacements),
      static_cast<unsigned long long>(s.discarded), static_cast<unsigned long long>(s.zeroed),
      static_cast<unsigned long long>(s.peak), static_cast<unsigned long long>(s.peak_active),
      static_cast<unsigned long long>(s.cached_end), static_cast<unsigned long long>(s.home_end),
      static_cast<unsigned long long>(s.peak_home));
}
void measure(const Caller& fixture, Strategy strategy, const std::string& history,
             std::size_t cache, unsigned repeat, unsigned ms) {
  Pool pool(64 * page(), page(), cache);
  if (cache) {
    std::vector<std::shared_ptr<Pool::Home>> warm;
    for (std::size_t i = 0; i < cache; ++i) warm.push_back(pool.allocate());
  }
  int sockets[2]{-1, -1};
  if (history == "file_socket" && ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets))
    throw std::runtime_error("benchmark socketpair");
  Bytes received(page());
  Sample sample;
  rusage before{}, after{};
  ::getrusage(RUSAGE_SELF, &before);
  while (sample.time < static_cast<std::uint64_t>(ms) * 1000000) {
    Context c(pool, fixture.history);
    if (history == "file_socket") one(c, 0, {0, page()}).complete();
    else need(read_all(c, 0, fixture.before.size()) == fixture.before, "prepared initial state");
    ByteUse held_old;
    if (history == "retained_old") held_old = one(c, 0, {3 * page(), 64});
    pool.reset_counts();
    const auto start = Clock::now();
    const auto result = fixture.run(c, strategy);
    need(result == fixture.expected, "independent result checksum");
    std::array<Range, 2> verify{{{256, 8}, {3 * page(), 64}}};
    {
      auto output = c.open(c.retain(1), verify);
      need(std::equal(output.bytes[0].data(), output.bytes[0].data() + 8, fixture.after.data() + 256) &&
           std::equal(output.bytes[1].data(), output.bytes[1].data() + 64, fixture.after.data() + 3 * page()),
           "actual published output and metadata");
    }
    if (history == "file_socket") {
      auto use = one(c, 1, {3 * page(), page()});
      socket_io(sockets[0], const_cast<std::uint8_t*>(use.data()), use.size(), true);
      socket_io(sockets[1], received.data(), received.size(), false);
      need(std::equal(received.begin(), received.end(), fixture.after.data() + 3 * page()),
           "actual socket consumer result");
    } else if (history == "late_old") {
      auto old = one(c, 0, {3 * page(), 64});
      need(std::equal(old.data(), old.data() + 64, fixture.before.data() + 3 * page()),
           "actual late historical result");
    } else if (history == "retained_old") {
      need(std::equal(held_old.data(), held_old.data() + 64, fixture.before.data() + 3 * page()),
           "changed output remains available through captured old bytes");
      held_old.complete();
    }
    c.close();
    const auto end = Clock::now();
    sample.time += ns(end - start);
    ++sample.operations;
    sample.checksum += result;
    sample.counts(pool.counts());
  }
  ::getrusage(RUSAGE_SELF, &after);
  if (sockets[0] >= 0) { ::close(sockets[0]); ::close(sockets[1]); }
  // Full byte oracle outside timing, on an independent operation.
  {
    Context c(pool, fixture.history);
    need(fixture.run(c, strategy) == fixture.expected, "outside timing caller oracle");
    need(read_all(c, 1, fixture.after.size()) == fixture.after, "outside timing full image oracle");
  }
  print(name(strategy), history.c_str(), cache, repeat, sample,
        after.ru_minflt - before.ru_minflt, after.ru_majflt - before.ru_majflt);
}
void floor(const Caller& fixture, unsigned repeat, unsigned ms) {
  Pool pool(16 * page());
  Context c(pool, fixture.history);
  auto v = c.open(c.retain(0), whole(fixture.before.size()));
  std::array<const std::uint8_t*, 4> pointers{};
  for (std::size_t i = 0; i < pointers.size(); ++i) pointers[i] = v.bytes[i].data();
  Sample s;
  const auto start = Clock::now();
  do {
    for (unsigned batch = 0; batch < 256; ++batch) {
      __asm__ volatile("" ::: "memory");
      std::uint64_t sum{};
      for (std::size_t row = 0; row < 8; ++row) {
        if (!pointers[0][128 + row]) continue;
        const auto* body = pointers[row < 4 ? 1 : 2] + (row % 4) * 64;
        if (body[0] != 123) continue;
        for (std::size_t i = 0; i < pointers[0][row * 16]; ++i) sum += body[i];
      }
      s.checksum += sum;
      ++s.operations;
    }
    s.time = ns(Clock::now() - start);
  } while (s.time < static_cast<std::uint64_t>(ms) * 1000000);
  need(s.checksum == s.operations * fixture.expected, "direct-pointer floor checksum");
  // Preparation is outside the read floor, but its live backing is not zero.
  const auto counts = pool.counts();
  s.peak = counts.peak;
  s.peak_home = counts.peak_home;
  s.peak_active = counts.peak_active;
  s.home_end = counts.charged - counts.cached;
  print("direct", "native_filter", 0, repeat, s, 0, 0);
}
}  // namespace
int main(int argc, char** argv) {
  try {
    unsigned ms = 20, repetitions = 3;
    bool reverse{};
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--reverse") reverse = true;
      else if (arg == "--ms" && i + 1 < argc) ms = std::stoul(argv[++i]);
      else if (arg == "--repetitions" && i + 1 < argc) repetitions = std::stoul(argv[++i]);
      else throw std::invalid_argument("benchmark arguments");
    }
    if (!ms || !repetitions) throw std::invalid_argument("empty benchmark");
    Caller fixture;
    std::puts("strategy,history,cache_pages,repeat,page_bytes,image_bytes,prepared_pages,write_bytes,socket_bytes,fixture_file_bytes,"
              "operations,elapsed_ns,ns_per_operation,checksum,expected_checksum,minor_faults_with_setup,"
              "major_faults_with_setup,maps,unmaps,copy_bytes,file_read_bytes,historical_fetch_bytes,"
              "physical_records,reused_pages,replacement_pages,discarded_bytes,zeroed_bytes,"
              "peak_charged_bytes,peak_active_records,end_cached_bytes,end_active_home_bytes,peak_active_home_bytes");
    struct Case { Strategy strategy; std::string history; std::size_t cache; };
    std::vector<Case> cases;
    for (auto cache : {0ul, 8ul})
      for (const auto& history : {"resident", "file_socket", "late_old", "retained_old"})
        for (auto strategy : {Strategy::copy, Strategy::share, Strategy::reuse})
          cases.push_back({strategy, history, cache});
    if (reverse) std::reverse(cases.begin(), cases.end());
    for (const auto& test : cases)
      for (unsigned r = 0; r < repetitions; ++r)
        measure(fixture, test.strategy, test.history, test.cache, r, ms);
    for (unsigned r = 0; r < repetitions; ++r) floor(fixture, r, ms);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}

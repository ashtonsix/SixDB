#include "population_fixture.h"
#include <pmu/pmu.h>
#include <linux/perf_event.h>
using namespace streampack_population;
namespace pmu = workbench::pmu;

void snapshot(const std::string& prefix, const char* phase, oa::Pool& pool,
    const std::array<const oa::Allocation*, 3>& allocations) {
  if (prefix.empty()) return;
  for (const char* file : {"smaps_rollup", "smaps", "numa_maps"}) {
    std::ifstream in(std::string("/proc/self/") + file); std::ofstream out(prefix + "." + phase + "." + file);
    require(bool(out), "open mapping output");
    if (in) out << in.rdbuf(); else out << "UNAVAILABLE: /proc/self/" << file << '\n';
    out.flush(); require(bool(out), "write mapping receipt");
  }
  const auto s = oa::statistics(pool); std::ofstream out(prefix + "." + phase + ".allocation.txt");
  out << "mapped_bytes " << s.mapped_bytes << "\nreserved_bytes " << s.reserved_bytes << "\nmetadata_bytes " << s.metadata_bytes << "\nidle_bytes " << s.idle_bytes << '\n';
  const std::array<const char*, 3> names{"metadata", "bases", "body"};
  for (unsigned i = 0; i < allocations.size(); ++i) {
    const auto& a = *allocations[i];
    out << names[i] << ".address 0x" << std::hex << reinterpret_cast<std::uintptr_t>(a.address) << std::dec << '\n'
        << names[i] << ".size " << a.size << '\n' << names[i] << ".readable_padding " << a.readable_padding << '\n';
  }
  out << "per_plane_reserved_bytes unavailable (Pool totals above)\n";
  out.flush(); require(bool(out), "write allocation receipt");
}
template<Method M> void run(oa::Pool& pool, const char* name, unsigned count, unsigned capacity, unsigned repeats, const std::string& receipt) {
  Fixture<M> fixture(pool, count, capacity); const auto reader = fixture.reader();
  const std::array<const oa::Allocation*, 3> allocations{&fixture.metadata, &fixture.bases, &fixture.body};
  snapshot(receipt, "ready", pool, allocations);
  const auto trial = [&](const char* op, auto operation, auto expected) {
    auto actual = operation(); require(actual == expected, "full-sweep output oracle"); // explicit whole-trace warming
    unsigned measured_iterations = 1;
    for (unsigned rep = 0; rep < repeats; ++rep) {
      unsigned iterations = 1; double ns;
      for (;;) {
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < iterations; ++i) { actual = operation(); escape(&actual); }
        ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
        if (ns >= 30000000 || iterations >= 1048576) break;
        iterations *= 2;
      }
      require(actual == expected, "post-timing full-sweep oracle");
      measured_iterations = iterations;
      const char* planes = std::string_view(op) == "independent-length" ? "metadata" : fixture.base_bytes ? "metadata+bases+payload" : "metadata+payload";
      std::printf("%s,%u,%u,%s,%u,%u,%.3f,%zu,%zu,%zu,%zu,%s\n", name, count, capacity, op, rep, iterations,
          ns / iterations / count, fixture.logical_bytes, fixture.payload_bytes, fixture.metadata_bytes, fixture.base_bytes, planes);
      std::fflush(stdout);
    }
    if (!receipt.empty()) {
      const std::array<pmu::EventSpec, 1> extras{{{"l1d-load-misses", PERF_TYPE_HW_CACHE,
          PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)}}};
      pmu::Session counters(pmu::Mode::basic, extras);
      actual = operation(); escape(&actual); // rewarm after opening descriptors
      counters.start();
      for (unsigned i = 0; i < measured_iterations; ++i) { actual = operation(); escape(&actual); }
      counters.stop();
      std::ofstream out(receipt + "." + op + ".pmu.json");
      out << "{\"iterations\":" << measured_iterations << ",\"items\":" << std::uint64_t(measured_iterations) * count << ",\"counters\":";
      counters.write_json(out); out << "}\n";
      out.flush(); require(bool(out), "write PMU receipt");
      require(actual == expected, "counter-trace oracle");
    }
  };
  const auto chains = [&]<unsigned C> {
    Result<C> expected; expected.lengths = fixture.logical_bytes;
    for (unsigned j = 0; j < C; ++j) expected.rows[j] = permute(((j + 1) * (count / C)) & (count - 1), count - 1);
    trial(C == 1 ? "dependent-tail4" : "eight-chain-tail4", [&] { return chase<C>(reader, count); }, expected);
  };
  chains.template operator()<1>(); chains.template operator()<8>();
  trial("independent-length", [&] { return lengths(reader, count); }, std::uint64_t(fixture.logical_bytes));
  snapshot(receipt, "finished", pool, allocations);
}
int main(int argc, char** argv) {
  require(argc >= 5, "method count capacity repeats [receipt-prefix]");
  const std::string method = argv[1]; const unsigned count = std::strtoul(argv[2], nullptr, 10), capacity = std::strtoul(argv[3], nullptr, 10), repeats = std::strtoul(argv[4], nullptr, 10);
  require(std::has_single_bit(count) && count >= 512 && count <= (1u << 26) && (capacity == 128 || capacity == 256 || capacity == 512) && repeats > 0, "sweep bounds");
  const std::string receipt = argc > 5 ? argv[5] : "";
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt Lane");
  auto arena = take(oa::open_arena((count > (1u << 24) ? 3072ull : 1536ull) << 20)); auto* pool = take(oa::add_pool(*arena, lane));
  std::puts("method,rows,capacity,operation,repetition,iterations,ns_per_row,logical_bytes,payload_bytes,metadata_bytes,base_bytes,touched_planes");
  if (method == "flat32") run<Method::flat32>(*pool, method.c_str(), count, capacity, repeats, receipt);
  else if (method == "direct16") run<Method::direct16>(*pool, method.c_str(), count, capacity, repeats, receipt);
  else if (method == "widths8") run<Method::widths8>(*pool, method.c_str(), count, capacity, repeats, receipt);
  else if (method == "widths8-native") run<Method::widths8_native>(*pool, method.c_str(), count, capacity, repeats, receipt);
  else if (method == "location8") run<Method::location8>(*pool, method.c_str(), count, capacity, repeats, receipt);
  else if (method == "duckdb16") run<Method::duckdb16>(*pool, method.c_str(), count, capacity, repeats, receipt);
  else require(false, "known method");
  for (;;) { auto p = oa::close(*pool); if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "close Pool"); orbital::worker::poll(lane, 128); std::this_thread::yield(); }
  require(arena->charged_bytes == 0, "sweep refund"); require(bool(oa::remove_pool(*pool)), "remove Pool");
  arena.reset(); orbital::worker::relinquish(lane);
}

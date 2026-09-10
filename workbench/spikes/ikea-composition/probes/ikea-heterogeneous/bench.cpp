#include "fixture.h"
#include "bench_support.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
using namespace ikea::heterogeneous;
namespace {
using namespace ikea::heterogeneous::measurement;
struct Workload {
  std::string name;
  std::vector<std::vector<PlainBlock>> windows;
};
std::vector<Workload> workloads(const std::filesystem::path &directory) {
  std::vector<Workload> out{{"structural", {}}, {"random_half", {}}};
  for (unsigned window = 0; window < 8; ++window) {
    out[0].windows.push_back(structural_blocks(256, window * 31));
    std::vector<PlainBlock> plain(256);
    for (unsigned i = 0; i < 256; ++i)
      for (unsigned j = 0; j < 32; ++j)
        plain[i][j] = mix(window * 8192 + i * 32 + j);
    out[1].windows.push_back(std::move(plain));
  }
  if (!directory.empty()) {
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
      if (entry.path().extension() == ".windows")
        paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    for (auto path : paths) {
      require(std::filesystem::file_size(path) % 8192 == 0,
              "window file extent");
      Workload w{path.stem().string(), {}};
      std::ifstream input(path, std::ios::binary);
      for (unsigned i = 0; i < std::filesystem::file_size(path) / 8192; ++i) {
        std::vector<PlainBlock> plain(256);
        input.read(reinterpret_cast<char *>(plain.data()), 8192);
        require(bool(input), "window read");
        w.windows.push_back(std::move(plain));
      }
      if (!w.windows.empty())
        out.push_back(std::move(w));
    }
  }
  return out;
}
struct Arm {
  MetadataKind kind;
  Execution execution;
  std::vector<PreparedRange> ranges;
  U metadata_bytes = 0, body_bytes = 0, query_bytes = 0, expected = 0;
};
__attribute__((noinline)) U repeat(const Arm &arm, U passes) {
  U checksum = 0;
  for (U pass = 0; pass < passes; ++pass)
    for (const auto &prepared : arm.ranges)
      checksum += prepared.count();
  return checksum;
}
} // namespace
int main(int argc, char **argv) {
  try {
    unsigned repetitions = 5;
    U min_ns = 10000000;
    bool pmu = false;
    std::filesystem::path data;
    for (int i = 1; i < argc; ++i) {
      std::string option = argv[i];
      if (option == "--quick") {
        repetitions = 1;
        min_ns = 1000000;
      } else if (option == "--pmu")
        pmu = true;
      else if (option == "--data" && i + 1 < argc)
        data = argv[++i];
      else if (option == "--repetitions" && i + 1 < argc)
        repetitions = std::stoul(argv[++i]);
      else
        throw std::runtime_error("unknown/missing argument: " + option);
    }
    require(repetitions > 0, "zero repetitions");
    const auto cpu = pin();
    std::cout << "dataset,first,count,layout,execution,repetition,windows,"
                 "passes,calls,logical_blocks,metadata_bytes,body_bytes,query_"
                 "bytes,body_suffix_bytes,checksum,elapsed_ns,ns_per_call,ns_"
                 "per_block,cycles_raw,instructions_raw,enabled_ns,running_ns,"
                 "pmu_status,major_faults,minor_faults,residence\n";
    for (const auto &workload : workloads(data)) {
      std::vector<EncodedSource> sources;
      std::vector<std::shared_ptr<const AlignedBytes>> queries;
      std::array<std::vector<std::shared_ptr<const MetadataOwner>>, 3> metadata;
      for (unsigned i = 0; i < workload.windows.size(); ++i) {
        sources.push_back(encode_source(workload.windows[i]));
        queries.push_back(make_query(256, 1234 + i * 8192));
        for (auto kind : {MetadataKind::direct32, MetadataKind::local16,
                          MetadataKind::scan128})
          metadata[unsigned(kind)].push_back(
              std::make_shared<MetadataOwner>(sources.back(), kind));
      }
      for (auto [first, count] :
           {std::pair{0u, 256u}, {3u, 37u}, {15u, 18u}, {255u, 1u}}) {
        std::vector<Arm> arms;
        for (auto kind : {MetadataKind::direct32, MetadataKind::local16,
                          MetadataKind::scan128})
          for (auto execution : {Execution::inlined, Execution::split}) {
            Arm arm{kind, execution, {}, 0, 0, 0, 0};
            for (unsigned i = 0; i < sources.size(); ++i) {
              PreparedRange prepared;
              require(prepare(metadata[unsigned(kind)][i], sources[i].body(),
                              queries[i], first, count, execution,
                              prepared) == AdmissionError::none,
                      "benchmark admission");
              const auto expected = reference_count(workload.windows[i],
                                                    *queries[i], first, count);
              require(prepared.count() == expected, "benchmark oracle");
              arm.expected += expected;
              arm.metadata_bytes += metadata[unsigned(kind)][i]->bytes().size();
              arm.body_bytes += sources[i].body()->logical_bytes;
              arm.query_bytes += queries[i]->size();
              arm.ranges.push_back(std::move(prepared));
            }
            arms.push_back(std::move(arm));
          }
        U passes = 1;
        for (;;) {
          const auto start = now();
          const auto checksum = repeat(arms[0], passes);
          const auto elapsed = now() - start;
          require(checksum == arms[0].expected * passes,
                  "calibration checksum");
          if (elapsed >= min_ns)
            break;
          passes *= 2;
        }
        for (const auto &arm : arms) {
          require(repeat(arm, 1) == arm.expected, "warm pass");
          for (unsigned rep = 0; rep < repetitions; ++rep) {
            Counters perf(pmu);
            rusage before{}, after{};
            getrusage(RUSAGE_SELF, &before);
            require(sched_getcpu() == cpu, "CPU before");
            perf.start();
            const auto start = now();
            const auto checksum = repeat(arm, passes);
            const auto elapsed = now() - start;
            perf.stop();
            require(sched_getcpu() == cpu, "CPU after");
            getrusage(RUSAGE_SELF, &after);
            require(checksum == arm.expected * passes, "timed checksum");
            U calls = passes * arm.ranges.size(), blocks = calls * count;
            std::cout << workload.name << ',' << first << ',' << count << ','
                      << metadata_name(arm.kind) << ','
                      << (arm.execution == Execution::inlined ? "inline"
                                                              : "split")
                      << ',' << rep << ',' << arm.ranges.size() << ',' << passes
                      << ',' << calls << ',' << blocks << ','
                      << arm.metadata_bytes << ',' << arm.body_bytes << ','
                      << arm.query_bytes << ',' << arm.ranges.size() * 64 << ','
                      << checksum << ',' << elapsed << ','
                      << double(elapsed) / calls << ','
                      << double(elapsed) / blocks << ',' << perf.cycles << ','
                      << perf.instructions << ',' << perf.enabled << ','
                      << perf.running << ',' << perf.status << ','
                      << after.ru_majflt - before.ru_majflt << ','
                      << after.ru_minflt - before.ru_minflt
                      << ",unestablished\n";
          }
        }
        std::cerr << workload.name << " first=" << first << " count=" << count
                  << " checked and timed\n";
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

#include "pmu.h"

#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace p = orbital_bench::pmu;
int main(int argc, char** argv) try {
  auto mode = p::Mode::basic;
  std::vector<p::EventSpec> extras;
  std::uint64_t iterations = 1000000;
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 == argc) throw std::invalid_argument("every option needs a value");
    const std::string_view key = argv[i], value = argv[i + 1];
    if (key == "--pmu") {
      if (value == "off") mode = p::Mode::off;
      else if (value == "basic") mode = p::Mode::basic;
      else if (value == "stalls") mode = p::Mode::stalls;
      else throw std::invalid_argument("PMU mode must be off, basic or stalls");
    } else if (key == "--pmu-event") extras.push_back(p::parse_event_spec(value));
    else if (key == "--iterations") {
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), iterations);
      if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
          !iterations || iterations > 10000000)
        throw std::invalid_argument("iterations must be 1..10000000");
    } else throw std::invalid_argument("unknown probe option");
  }
  if (mode == p::Mode::off && !extras.empty())
    throw std::invalid_argument("PMU events require basic or stalls mode");
  p::Session session(mode, extras);
  std::uint64_t consumed = 1;
  session.start();
  // A short real dependency chain proves whether a counting group runs.
  // This is an availability probe, never a database IPC or throughput result.
  for (std::uint64_t i = 0; i < iterations; ++i)
    consumed = (consumed ^ (consumed >> 17)) * 0x9e3779b97f4a7c15ull + i;
  session.stop();
  std::cout << "{\"schema\":1,\"scope\":\"PMU availability probe only\",\"iterations\":"
            << iterations << ",\"consumed\":" << consumed << ",\"measurement\":";
  session.write_json(std::cout);
  std::cout << "}\n";
  if (!std::cout) throw std::runtime_error("PMU probe receipt write failed");
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n'; return 1;
}

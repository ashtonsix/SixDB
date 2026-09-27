#include "orbital.hpp"
#include "client.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace sixdb::sim;
namespace {
using client::number;
template<class E> E choice(std::string_view value, std::initializer_list<E> choices) {
  for (auto item : choices) if (orbital::name(item) == value) return item;
  throw std::invalid_argument("unknown choice: " + std::string(value));
}
void help() {
  std::cout << "Orbital reference model client; programmatic API: models/orbital.hpp\n"
      "  --seed N --fifo --points N --interval NS --start NS --delay NS\n"
      "  --until NS --link NS --disk NS --retry NS --memory BYTES --storage BYTES\n"
      "  --events N --incident none|one-follower|quorum-pause|consumer-reset|coordinator-reset|checker-reset\n"
      "  --negative none|skip-pending|skip-verification|corrupt-checker\n"
      "  --queue-policy older-conflicts-drain|no-overtaking|eligible-first|oldest-live\n"
      "  --placement isolated|shared --name NAME\n"
      "  --trace PATH --choices PATH --replay PATH\n"
      "Output is one JSON result. Exit 2 means observed violation; unfinished work is reported separately.\n";
}
} // namespace
int main(int argc, char** argv) {
  try {
    orbital::Case spec; Options options;
    client::Evidence evidence;
    for (int i = 1; i < argc; ++i) {
      std::string_view key = argv[i];
      if (key == "--help") { help(); return 0; }
      if (key == "--fifo") { options.ordering = Ordering::fifo; continue; }
      if (++i == argc) throw std::invalid_argument("missing option value");
      std::string_view value = argv[i];
      if (evidence.option(key, value)) continue;
      if (key == "--name") spec.name = value;
      else if (key == "--seed") options.seed = number(value);
      else if (key == "--points") {
        auto n = number(value); if (n > 1'000'000) throw std::invalid_argument("point count exceeds client bound");
        spec.point_writes = static_cast<std::uint32_t>(n);
      } else if (key == "--interval") spec.interval_ns = number(value);
      else if (key == "--start") spec.point_start_ns = number(value);
      else if (key == "--delay") spec.checker_delay_ns = number(value);
      else if (key == "--until") spec.until_ns = number(value);
      else if (key == "--link") spec.link_ns = number(value);
      else if (key == "--disk") spec.disk_ns = number(value);
      else if (key == "--retry") spec.retry_ns = number(value);
      else if (key == "--memory") spec.memory_bytes = number(value);
      else if (key == "--storage") spec.storage_bytes = number(value);
      else if (key == "--events") spec.max_events = number(value);
      else if (key == "--incident") spec.incident = choice(value, {orbital::Incident::none, orbital::Incident::one_follower,
          orbital::Incident::quorum_pause, orbital::Incident::consumer_reset, orbital::Incident::coordinator_reset, orbital::Incident::checker_reset});
      else if (key == "--queue-policy") spec.queue_policy = choice(value, {orbital::QueuePolicy::older_conflicts_drain,
          orbital::QueuePolicy::no_overtaking, orbital::QueuePolicy::eligible_first, orbital::QueuePolicy::oldest_live});
      else if (key == "--negative") spec.negative = choice(value, {orbital::Negative::none, orbital::Negative::skip_pending,
          orbital::Negative::skip_verification, orbital::Negative::corrupt_checker});
      else if (key == "--placement") {
        if (value == "shared") {
          for (auto actor : orbital::roles()) {
            bool witness = false;
            for (std::uint32_t shard = 0; shard < 2; ++shard)
              witness |= actor == orbital::leader(shard) || actor == orbital::follower(shard,0) || actor == orbital::follower(shard,1);
            if (!witness) spec.placement[actor] = 1;
          }
        }
        else if (value == "isolated") spec.placement.clear();
        else throw std::invalid_argument("unknown placement");
      } else throw std::invalid_argument("unknown option: " + std::string(key));
    }
    evidence.open(options);
    auto result = orbital::run_case(spec, options, evidence.trace());
    evidence.finish();
    orbital::write_json(std::cout, result);
    evidence.finish();
    return result.violations.empty() ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << "simulator: " << error.what() << '\n'; return 1;
  }
}

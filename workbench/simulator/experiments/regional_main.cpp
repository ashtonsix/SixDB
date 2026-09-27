#include "client.hpp"
#include "regional.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
  using namespace sixdb::sim;
  try {
    regional::Spec spec; Options options; client::Evidence evidence;
    for (int i = 1; i < argc; ++i) {
      std::string_view key = argv[i];
      if (key == "--help") {
        std::cout << "Finite regional localisation / isolated policy experiment\n"
          "  --region NS --bridge|--bridge-cut --shared --eligible-first --points N --interval NS --until NS --retry NS\n"
          "  --shape regional|held-broad|chain|sparse-wan|younger-wan|unrelated-old|ordinary|hot\n  --policy drain|ordered|eligible|head (default drain) --incident none|consumer-reset|coordinator-reset\n  --progress-rounds N (separate local broad-waiter fixture)\n  --seed N --fifo --events N --name LABEL --trace PATH --choices PATH --replay PATH\n";
        return 0;
      }
      if (key == "--bridge") { spec.bridge = true; continue; }
      if (key == "--bridge-cut") { spec.bridge = true; spec.bridge_cut = true; continue; }
      if (key == "--eligible-first") { spec.queue_policy = orbital::QueuePolicy::eligible_first; continue; }
      if (key == "--shared") { spec.shared = true; continue; }
      if (key == "--fifo") { options.ordering = Ordering::fifo; continue; }
      if (++i == argc) throw std::invalid_argument("missing option value"); std::string_view value = argv[i];
      if (evidence.option(key, value)) continue;
      if(key=="--policy") {
        if(value=="ordered") spec.queue_policy=orbital::QueuePolicy::no_overtaking;
        else if(value=="eligible") spec.queue_policy=orbital::QueuePolicy::eligible_first;
        else if(value=="drain") spec.queue_policy=orbital::QueuePolicy::older_conflicts_drain;
        else if(value=="head") spec.queue_policy=orbital::QueuePolicy::oldest_live;
        else throw std::invalid_argument("unknown policy");
      }
      else if(key=="--shape") spec.shape=value;
      else if(key=="--incident") {
        if(value=="consumer-reset") spec.incident=orbital::Incident::consumer_reset;
        else if(value=="coordinator-reset") spec.incident=orbital::Incident::coordinator_reset;
        else if(value=="none") spec.incident=orbital::Incident::none;
        else throw std::invalid_argument("unsupported study incident");
      }
      else if (key == "--region") spec.region_ns = client::number(value);
      else if (key == "--interval") spec.interval_ns = client::number(value);
      else if (key == "--until") spec.until_ns = client::number(value);
      else if (key == "--retry") spec.retry_ns = client::number(value);
      else if (key == "--events") spec.max_events = client::number(value);
      else if (key == "--seed") options.seed = client::number(value);
      else if (key == "--progress-rounds") { auto n = client::number(value); if (!n || n > 64) throw std::invalid_argument("progress rounds must be in [1,64]"); spec.progress_rounds = static_cast<std::uint32_t>(n); }
      else if (key == "--points") { auto n = client::number(value); if (n > 192) throw std::invalid_argument("regional point count exceeds finite-case bound"); spec.points = static_cast<std::uint32_t>(n); }
      else if (key == "--name") spec.name = value;
      else throw std::invalid_argument("unknown regional option: " + std::string(key));
    }
    evidence.open(options); auto observation = regional::run(spec, options, evidence.trace()); evidence.finish(); regional::write_json(std::cout, observation); evidence.finish();
    return observation.result.violations.empty() ? 0 : 2;
  } catch (const std::exception& e) { std::cerr << "regional: " << e.what() << '\n'; return 1; }
}

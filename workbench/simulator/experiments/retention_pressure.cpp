#include "client.hpp"
#include "retention_pressure.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
  using namespace sixdb::sim;
  try {
    retention_pressure::Case config; Options options; client::Evidence evidence;
    for (int i = 1; i < argc; ++i) {
      std::string_view key = argv[i];
      if (key == "--help") {
        std::cout << "--seed N --memory BYTES --storage BYTES --incident none|checkpoint-reset|cursor-reset "
                     "--negative none|skip-replay-root|lose-retry --until-ns N --trace PATH --choices PATH --replay PATH\n";
        return 0;
      }
      if (i + 1 == argc) throw std::invalid_argument("option requires value");
      std::string_view value = argv[++i];
      if (evidence.option(key, value)) continue;
      if (key == "--seed") options.seed = client::number(value);
      else if (key == "--memory") config.memory_bytes = client::number(value);
      else if (key == "--storage") config.storage_bytes = client::number(value);
      else if (key == "--until-ns") config.until_ns = client::number(value);
      else if (key == "--incident") {
        if (value == "none") config.incident = retention_pressure::Incident::none;
        else if (value == "checkpoint-reset") config.incident = retention_pressure::Incident::checkpoint_reset;
        else if (value == "cursor-reset") config.incident = retention_pressure::Incident::replay_cursor_reset;
        else throw std::invalid_argument("unknown incident");
      } else if (key == "--negative") {
        if (value == "none") config.negative = retention_pressure::Negative::none;
        else if (value == "skip-replay-root") config.negative = retention_pressure::Negative::skip_replay_root;
        else if (value == "lose-retry") config.negative = retention_pressure::Negative::lose_retry;
        else throw std::invalid_argument("unknown negative control");
      } else throw std::invalid_argument("unknown retention option");
    }
    evidence.open(options);
    auto result = retention_pressure::run_case(config, options, evidence.trace());
    evidence.finish();
    retention_pressure::write_json(std::cout, result); std::cout.flush();
    if (!std::cout) throw std::runtime_error("result output failed");
    return config.negative == retention_pressure::Negative::none && !result.violations.empty() ? 2 : 0;
  } catch (const std::exception& error) {
    std::cerr << "retention pressure: " << error.what() << '\n'; return 1;
  }
}

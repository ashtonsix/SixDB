#pragma once

#include <sixdb/sim/runtime.hpp>

#include <fstream>
#include <string>
#include <string_view>

namespace sixdb::sim::client {

std::uint64_t number(std::string_view);

/// Shared file handling for experiment executables. Validate every path before
/// opening an output, so aliases cannot truncate replay input or another sink.
class Evidence {
 public:
  bool option(std::string_view key, std::string_view value);
  void open(Options&);
  [[nodiscard]] std::ostream* trace();
  void finish();

 private:
  std::string trace_path_, choices_path_, replay_path_;
  std::ofstream trace_, choices_;
  std::ifstream replay_;
};

} // namespace sixdb::sim::client

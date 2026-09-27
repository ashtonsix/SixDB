#include "client.hpp"

#include <charconv>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace sixdb::sim::client {
namespace {
bool same_path(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return false;
  namespace fs = std::filesystem;
  std::error_code error;
  if (fs::equivalent(a, b, error) && !error) return true;
  return fs::weakly_canonical(a) == fs::weakly_canonical(b);
}
} // namespace

std::uint64_t number(std::string_view text) {
  std::uint64_t value{};
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("invalid unsigned number");
  return value;
}

bool Evidence::option(std::string_view key, std::string_view value) {
  if (key == "--trace") trace_path_ = value;
  else if (key == "--choices") choices_path_ = value;
  else if (key == "--replay") replay_path_ = value;
  else return false;
  return true;
}

void Evidence::open(Options& options) {
  if (same_path(choices_path_, replay_path_)) throw std::invalid_argument("cannot overwrite replay input");
  if (same_path(trace_path_, choices_path_) || same_path(trace_path_, replay_path_))
    throw std::invalid_argument("output paths overlap");
  // Check input before creating output even when the paths are distinct.
  if (!replay_path_.empty()) {
    replay_.open(replay_path_);
    if (!replay_) throw std::runtime_error("cannot open replay input");
    options.decisions_in = &replay_;
  }
  if (!trace_path_.empty()) {
    trace_.open(trace_path_);
    if (!trace_) throw std::runtime_error("cannot open trace output");
  }
  if (!choices_path_.empty()) {
    choices_.open(choices_path_);
    if (!choices_) throw std::runtime_error("cannot open choice output");
    options.decisions_out = &choices_;
  }
}

std::ostream* Evidence::trace() { return trace_.is_open() ? &trace_ : nullptr; }

void Evidence::finish() {
  if (trace_.is_open()) {
    trace_.flush();
    if (!trace_) throw std::runtime_error("failed to flush trace output");
  }
  if (choices_.is_open()) {
    choices_.flush();
    if (!choices_) throw std::runtime_error("failed to flush choice output");
  }
  std::cout.flush();
  if (!std::cout) throw std::runtime_error("failed to write result");
}

} // namespace sixdb::sim::client

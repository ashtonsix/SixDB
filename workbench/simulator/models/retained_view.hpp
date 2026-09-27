#pragma once

#include <sixdb/sim/runtime.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sixdb::sim::retained_view {

enum class Root { reader, replay };
enum class Incident { none, checkpoint_device_reset, checkpoint_process_loss };
enum class Negative { none, drop_live_root, publish_before_checkpoint };

/// A serialized checkpoint/root fixture, not an Orbital reclamation contract.
/// Raw payload bytes, descriptor and decoder identities cross public ports.
/// The actor's 4-KiB workspace is a synthetic allowance, not measured C++ heap.
struct Case {
  Root root{Root::reader};
  Incident incident{Incident::none};
  Negative negative{Negative::none};
  std::uint64_t memory_bytes{8192}, storage_bytes{16384};
  Time disk_ns{4000}, until_ns{8'000'000};
};

struct Result {
  bool old_read_complete{}, checkpoint_read_complete{}, incident_exercised{};
  std::uint64_t resets{}, bytes_after_process_loss{}, trace_hash{};
  Usage usage;
  Run execution;
  std::vector<std::string> violations, missing_milestones;
  std::vector<Record> evidence;
};

/// Factories retain only Case, never earlier actor values. The harness injects
/// initial records once; all subsequent reads discover roots via Context::read.
Result run_retained_view(const Case& = {}, Options = {});

/// Prefix oracle over model write-intents paired with physical storage receipts.
/// Intent alone is not durable evidence; read values are checked independently
/// against authored bytes and the currently durable root.
std::vector<std::string> audit(const std::vector<Record>&);

} // namespace sixdb::sim::retained_view

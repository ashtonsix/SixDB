#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace index_spike {
// All bytes and optional mask/output remain admitted through the SAME external
// Scope/Borrowed call. This view owns nothing; it is not a retained cursor.
// A nonnull mask grants ceil(mask_bits/64) readable uint64 words, including the
// final partial word; mask_base is64-aligned and base+bits<=2^32. Writable output
// and counters are disjoint from each other, request, mask and ALL borrowed
// directory/material bytes. Output capacity covers every selected result.
struct ScanItem { std::uint32_t prefix{},reserved{}; std::uint64_t xor8{}; };
struct ScanResult { std::uint64_t sum,selected; };
struct ScanCounters {
  std::uint64_t visited_dirents{},visited_groups{},visited_nodes{},material_bytes{};
};
struct GraphScanRequest {
  const std::byte* root{};
  const std::uint64_t* mask{}; // null selects all; outside a nonnull span selects none
  std::uint64_t mask_base{},mask_bits{}; // ORIGINAL prefix coordinates; base %64==0
  std::uint32_t low{},high{}; // inclusive; low>high is empty
  ScanItem* output{};
  std::uint64_t output_capacity{}; // caller admits >= selected when output!=nullptr
};
static_assert(sizeof(ScanItem)==16 && sizeof(ScanResult)==16 && sizeof(ScanCounters)==32);
static_assert(sizeof(GraphScanRequest)==56 && offsetof(GraphScanRequest,low)==32 &&
    offsetof(GraphScanRequest,output)==40);

struct GraphGroup {
  const std::byte* payload{};
  std::uint32_t index{};
  std::uint16_t original_mask{};
};
// Functional/reference iterator for the admitted header-free F32 graph. The
// native scan uses the same tree law, not repeated point lookups. Frames borrow
// actual immutable directory nodes; the caller keeps its read token stationary.
class GraphGroupCursor {
  struct Frame {
    const std::byte* node{};
    std::uint32_t base{};
    std::uint8_t next{},shift{};
  };
  std::array<Frame,6> frames_{};
  unsigned depth_{};
  std::uint32_t first_{},last_{};
  std::uint64_t nodes_{};
public:
  GraphGroupCursor(const std::byte* root,std::uint32_t low,std::uint32_t high) noexcept;
  bool next(GraphGroup&) noexcept;
  std::uint64_t visited_nodes() const noexcept { return nodes_; }
};
// Independent C++ traversal reference, not an assembly performance result.
ScanResult graph_scan_reference(const GraphScanRequest&,ScanCounters* =nullptr) noexcept;
}
extern "C" index_spike::ScanResult idx_asm_graph_scan64(
    const index_spike::GraphScanRequest*,index_spike::ScanCounters*) noexcept;
extern "C" index_spike::ScanResult idx_asm_graph_scan64_dense(
    const index_spike::GraphScanRequest*,index_spike::ScanCounters*) noexcept;
extern "C" index_spike::ScanResult idx_asm_graph_scan64_lookahead_none(
    const index_spike::GraphScanRequest*,index_spike::ScanCounters*) noexcept;
extern "C" index_spike::ScanResult idx_asm_graph_scan64_lookahead(
    const index_spike::GraphScanRequest*,index_spike::ScanCounters*) noexcept;

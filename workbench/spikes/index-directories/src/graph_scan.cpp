#include "graph_scan.h"
#include "native_abi.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>

namespace index_spike {
namespace {
template<class T> T load(const std::byte* p) noexcept {
  T value; std::memcpy(&value,p,sizeof(value)); return value;
}
}
GraphGroupCursor::GraphGroupCursor(const std::byte* root,std::uint32_t low,
    std::uint32_t high) noexcept:first_(low>>4),last_(high>>4) {
  if (!root || low>high) return;
  nodes_=1;
  const auto minimum=load<std::uint32_t>(root+8),maximum=load<std::uint32_t>(root+12);
  if (first_>maximum || last_<minimum) return;
  first_=std::max(first_,minimum); last_=std::min(last_,maximum);
  const auto shift=std::to_integer<unsigned>(root[2]);
  assert(shift<=25 && shift%5==0);
  const auto span=(UINT64_C(1)<<(shift+5))-1;
  const auto base=load<std::uint32_t>(root+8)&~span;
  frames_[0]={root,static_cast<std::uint32_t>(base),
      static_cast<std::uint8_t>(first_<base?0:(first_>>shift)&31),
      static_cast<std::uint8_t>(shift)};
  depth_=1;
}
bool GraphGroupCursor::next(GraphGroup& out) noexcept {
  while (depth_) {
    auto& frame=frames_[depth_-1];
    if (frame.next==32) { --depth_; continue; }
    const auto slot=frame.next++;
    const auto base=frame.base|(std::uint32_t(slot)<<frame.shift);
    if (base>last_) { depth_=0; return false; }
    if (base+((std::uint32_t{1}<<frame.shift)-1)<first_) continue;
    const auto* entry=frame.node+64+slot*16;
    const auto* child=load<const std::byte*>(entry);
    if (!child) continue;
    if (!frame.shift) {
      out={child,base,load<std::uint16_t>(entry+8)};
      return true;
    }
    const auto shift=frame.shift-5;
    const auto next=base<first_?(first_>>shift)&31:0;
    assert(depth_<frames_.size());
    frames_[depth_++]={child,base,static_cast<std::uint8_t>(next),
        static_cast<std::uint8_t>(shift)};
    ++nodes_;
  }
  return false;
}
ScanResult graph_scan_reference(const GraphScanRequest& request,ScanCounters* counters) noexcept {
  assert(!(request.mask_base&63) && request.mask_base<=(UINT64_C(1)<<32) &&
      request.mask_bits<=(UINT64_C(1)<<32)-request.mask_base);
  if (request.mask && !request.mask_bits) {
    if (counters) *counters={};
    return {};
  }
  ScanResult result{};
  ScanCounters work;
  GraphGroupCursor cursor(request.root,request.low,request.high);
  GraphGroup group;
  while (cursor.next(group)) {
    ++work.visited_groups;
    const auto prefix=std::uint64_t(group.index)<<4;
    const unsigned lo=request.low>prefix?request.low-prefix:0;
    const unsigned hi=std::min<std::uint64_t>(15,std::uint64_t(request.high)-prefix);
    const auto in_range=unsigned(group.original_mask)&
        ((std::uint32_t{1}<<(hi+1))-1)&~((std::uint32_t{1}<<lo)-1);
    work.visited_dirents+=std::popcount(in_range);
    auto selected=in_range;
    if (request.mask) {
      if (prefix<request.mask_base || prefix-request.mask_base>=request.mask_bits) selected=0;
      else {
        const auto bit=prefix-request.mask_base;
        const auto bits=std::min<std::uint64_t>(16,request.mask_bits-bit);
        const auto mask=(request.mask[bit>>6]>>(bit&63))&((std::uint64_t{1}<<bits)-1);
        selected&=mask;
      }
    }
    while (selected) {
      const auto suffix=std::countr_zero(selected);
      selected&=selected-1;
      const auto rank=std::popcount(unsigned(group.original_mask)&((1u<<suffix)-1));
      const auto* material=load<const std::byte*>(group.payload+64*rank);
      const auto digest=idx_asm_digest64(material);
      if (request.output) {
        assert(result.selected<request.output_capacity);
        request.output[result.selected]={static_cast<std::uint32_t>(prefix+suffix),0,digest};
      }
      result.sum+=digest; ++result.selected;
    }
  }
  work.visited_nodes=cursor.visited_nodes(); work.material_bytes=result.selected*64;
  if (counters) *counters=work;
  return result;
}
}

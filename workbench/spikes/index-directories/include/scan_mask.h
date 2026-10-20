#pragma once
#include "graph_scan.h"
#include <algorithm>
#include <bit>

namespace index_spike {
// Private bounded scan-driver helpers over the admitted ORIGINAL-prefix mask.
// Construct/use inside the complete scan interval, never from oracle state.
struct MaskWindow {
  const std::uint64_t* words{};
  std::uint64_t base{},first{},end{};
  explicit MaskWindow(const GraphScanRequest& request):words(request.mask),base(request.mask_base) {
    if (request.low>request.high) return;
    first=std::max<std::uint64_t>(request.low,base);
    end=std::min<std::uint64_t>(std::uint64_t(request.high)+1,base+request.mask_bits);
    if (first>=end) first=end=0;
  }
  std::uint64_t word(std::uint64_t index) const noexcept {
    auto bits=words[index];
    const auto start=base+index*64;
    if (first>start) bits&=UINT64_MAX<<(first-start);
    if (end<start+64) bits&=(UINT64_C(1)<<(end-start))-1;
    return bits;
  }
};
inline bool mask_has_candidate(const GraphScanRequest& request) noexcept {
  if (request.low>request.high) return false;
  if (!request.mask) return true;
  const MaskWindow window(request);
  if (window.first==window.end) return false;
  for (auto i=(window.first-window.base)/64;i<=(window.end-1-window.base)/64;++i)
    if (window.word(i)) return true;
  return false;
}

class MaskRuns {
  const GraphScanRequest& request_;
  MaskWindow window_;
  std::uint64_t index_{},last_{},bits_{};
  bool ended_{};
public:
  explicit MaskRuns(const GraphScanRequest& request):request_(request),window_(request) {
    if (!request.mask) { ended_=request.low>request.high; return; }
    ended_=window_.first==window_.end;
    if (!ended_) {
      index_=(window_.first-window_.base)/64;
      last_=(window_.end-1-window_.base)/64; bits_=window_.word(index_);
    }
  }
  bool next(std::uint32_t& low,std::uint32_t& high) noexcept {
    if (ended_) return false;
    if (!request_.mask) { low=request_.low; high=request_.high; ended_=true; return true; }
    while (!bits_) {
      if (index_==last_) { ended_=true; return false; }
      bits_=window_.word(++index_);
    }
    const auto start=std::countr_zero(bits_);
    const auto length=std::countr_one(bits_>>start);
    const auto first=window_.base+index_*64+start;
    auto end=first+length;
    const auto local_end=start+length;
    bits_=local_end==64?0:bits_&(UINT64_MAX<<local_end);
    // Join adjacent selected runs across word boundaries without another seek.
    if (local_end==64) while (index_<last_) {
      bits_=window_.word(++index_);
      const auto count=std::countr_one(bits_);
      end+=count;
      bits_=count==64?0:bits_&(UINT64_MAX<<count);
      if (count<64) break;
    }
    low=static_cast<std::uint32_t>(first);
    high=static_cast<std::uint32_t>(end-1); return true;
  }
};
}

#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

// Logical inputs and independent expected results. No Engine types, native
// pointers, allocation policy, routing representation or publication API.
namespace index_spike::logical_oracle {
using Bytes=std::vector<std::byte>;
using Identity=std::pair<unsigned,std::uint32_t>; // layer, physical prefix
struct Entry {
  Bytes minimum,maximum;
  std::vector<Bytes> payloads;
  friend bool operator==(const Entry&,const Entry&)=default;
};
using State=std::map<Identity,Entry>;
struct Edit { Identity identity; std::optional<Entry> after; };
struct Batch { std::string_view name; std::vector<Edit> edits; };

inline bool valid_identity(Identity id) {
  return id.first<=4 && std::uint64_t(id.second)<(UINT64_C(1)<<(8*id.first));
}
inline void validate_entry(Identity id,const Entry& value,unsigned width,unsigned shared) {
  if (!valid_identity(id) || shared>std::min(6u,width) || value.minimum.size()!=width ||
      value.maximum.size()!=width || value.minimum>value.maximum)
    throw std::invalid_argument("invalid logical fixture entry");
  for (unsigned i=0;i<std::min(id.first,shared);++i) {
    const auto expected=std::byte(id.second>>(8*(id.first-1-i)));
    if (value.minimum[i]!=expected || value.maximum[i]!=expected)
      throw std::invalid_argument("logical fixture shared prefix");
  }
}
inline void validate(const State& state,unsigned width,unsigned shared=2) {
  std::optional<unsigned> layer;
  const Bytes* previous=nullptr;
  for (const auto& [id,value]:state) {
    validate_entry(id,value,width,shared);
    if (layer==id.first && *previous>value.minimum)
      throw std::invalid_argument("logical fixture order");
    layer=id.first; previous=&value.maximum;
  }
}
// The original state stays available for late old-version queries. Each input
// is valid on its own; ordering is a property of the final batch, not a sequence
// of scalar intermediate publications. Last edit for an identity wins.
inline State apply(const State& before,std::span<const Edit> edits,unsigned width,unsigned shared=2) {
  auto after=before;
  for (const auto& edit:edits) {
    if (!valid_identity(edit.identity)) throw std::invalid_argument("logical fixture identity");
    if (edit.after) {
      validate_entry(edit.identity,*edit.after,width,shared);
      after[edit.identity]=*edit.after;
    } else after.erase(edit.identity);
  }
  validate(after,width,shared); return after;
}
inline std::vector<Identity> prefixes(const State& state,unsigned layer,
    std::uint32_t low=0,std::uint32_t high=UINT32_MAX) {
  std::vector<Identity> result;
  for (const auto& [id,value]:state)
    if (id.first==layer && id.second>=low && id.second<=high) result.push_back(id);
  return result;
}
inline std::vector<Identity> overlap(const State& state,unsigned layer,
    const std::optional<Bytes>& low={},const std::optional<Bytes>& high={},
    bool low_inclusive=true,bool high_inclusive=false) {
  std::vector<Identity> result;
  if (low && high && (*low>*high || (*low==*high && (!low_inclusive || !high_inclusive))))
    return result;
  for (const auto& [id,value]:state) {
    if (id.first!=layer) continue;
    if (low && (value.maximum<*low || (value.maximum==*low && !low_inclusive))) continue;
    if (high && (value.minimum>*high || (value.minimum==*high && !high_inclusive))) continue;
    result.push_back(id);
  }
  return result;
}

// These fixture keys exercise BE order and, at width12, equal eight-byte
// abbreviations. Their suffix encoding is test data, not an index format.
inline Bytes key(unsigned width,Identity id,std::uint32_t suffix) {
  if ((width!=8 && width!=12) || !valid_identity(id))
    throw std::invalid_argument("logical fixture key");
  Bytes result(width);
  for (unsigned i=0;i<std::min(id.first,2u);++i)
    result[i]=std::byte(id.second>>(8*(id.first-1-i)));
  for (unsigned i=0;i<4;++i) result[width-4+i]=std::byte(suffix>>(24-8*i));
  return result;
}
inline Entry record(unsigned width,Identity id,unsigned generation=1,unsigned parts=2,
    std::uint32_t low=10,std::uint32_t high=19) {
  Entry value{key(width,id,low),key(width,id,high),{}};
  for (unsigned ordinal=0;ordinal<parts;++ordinal) {
    Bytes body(64*(1+ordinal%3));
    for (unsigned i=0;i<body.size();++i)
      body[i]=std::byte((id.second*17u+id.first*11u+generation*31u+ordinal*7u+i)^0xa5u);
    value.payloads.push_back(std::move(body));
  }
  return value;
}
inline std::vector<Batch> batches(unsigned width) {
  std::vector<Batch> result;
  const auto add=[&](std::string_view name,std::vector<Edit> edits) {
    result.push_back({name,std::move(edits)});
  };
  const auto r=[&](Identity id,unsigned generation=1,unsigned parts=2,
      std::uint32_t low=10,std::uint32_t high=19) {
    return record(width,id,generation,parts,low,high);
  };
  add("first",{{{0,0},r({0,0})}});
  std::vector<Edit> edits;
  for (const Identity id:std::array{Identity{1,1},Identity{1,255},Identity{2,0x2211},
      Identity{2,0x221f},Identity{2,0x2220},Identity{3,0x221101},Identity{3,0x221102},
      Identity{4,UINT32_MAX-1},Identity{4,UINT32_MAX}}) {
    const bool second=id==Identity{3,0x221102} || id==Identity{4,UINT32_MAX};
    edits.push_back({id,r(id,1,id.second&1?15:8,second?20:10,second?29:19)});
  }
  add("ordinary-hash-ordered-and-extra-payloads",std::move(edits));
  add("replace-and-delete-with-late-old-read",{
      {{2,0x2211},r({2,0x2211},2,6,11,18)},
      {{3,0x221101},r({3,0x221101},2,1,11,18)},{{4,UINT32_MAX},{}}});
  add("repeated-final-state-and-cancelled-birth",{
      {{2,0x221f},r({2,0x221f},3,15)},{{2,0x221f},{}},{{2,0x221f},r({2,0x221f},4,7)},
      {{2,0x2213},r({2,0x2213})},{{2,0x2213},{}}});
  add("remove-ordinary-and-hash-members",{{{1,1},{}},{{2,0x221f},{}},{{2,0x2220},{}}});
  add("recreate-and-rank-shift",{
      {{1,1},r({1,1},5)},{{1,128},r({1,128},5)},
      {{2,0x221f},r({2,0x221f},5,15)},{{2,0x2220},r({2,0x2220},5,1)}});
  edits.clear();
  for (unsigned i=0;i<900;++i) {
    const Identity id{3,0x334400+i};
    edits.push_back({id,r(id,1,1,100+i/3,100+i/3)});
  }
  add("grow-900-equal-key-fences",std::move(edits));
  edits.clear();
  for (unsigned i=0;i<900;++i) if (i!=413) edits.push_back({{3,0x334400+i},{}});
  add("shrink-900-to-one-with-late-old-read",std::move(edits));
  add("empty-ordered-layer",{{{3,0x221101},{}},{{3,0x221102},{}},{{3,0x334400+413},{}}});
  add("recreate-ordered-layer",{{{3,0x3344ff},r({3,0x3344ff},9,15)}});
  edits.clear();
  for (unsigned i=0;i<73;++i) {
    const Identity id{4,0x44550000+i};
    edits.push_back({id,r(id,1,2,100+i*5,104+i*5)});
  }
  add("grow-L4-73",std::move(edits));
  edits.clear();
  for (unsigned i:std::array{2u,37u,70u}) {
    const Identity id{4,0x44550000+i};
    edits.push_back({id,r(id,9,2,100+i*5,104+i*5)});
  }
  add("payload-only-replacements",std::move(edits));
  edits.clear();
  for (unsigned i=0;i<73;++i) if (i!=2 && i!=37 && i!=70)
    edits.push_back({{4,0x44550000+i},{}});
  add("adjacent-deficiencies",std::move(edits));
  return result;
}
}

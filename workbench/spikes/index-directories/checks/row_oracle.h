#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

// Logical workload and independent expectations only; no Engine/storage API.
namespace index_directory_oracle {
using Row=std::array<std::uint64_t,8>;
using U128=unsigned __int128;
struct Record { std::uint64_t physical; Row fields; friend bool operator==(const Record&,const Record&)=default; };
struct Command { std::uint64_t physical,id,value; unsigned field; };
struct Field { std::uint64_t minimum{},maximum{}; U128 sum{}; std::uint64_t not_null{}; };
struct Cell { unsigned layer; std::uint32_t prefix; std::uint16_t position; std::uint64_t records{}; std::array<Field,8> fields{}; };
inline std::uint64_t mix(std::uint64_t x) {
  x^=x>>30; x*=UINT64_C(0xbf58476d1ce4e5b9); x^=x>>27;
  x*=UINT64_C(0x94d049bb133111eb); return x^(x>>31);
}
inline std::vector<Record> rows(unsigned segments,unsigned full_rows) {
  if (segments<2 || segments>2000 || !full_rows || full_rows>65536)
    throw std::invalid_argument("workload population");
  std::vector<Record> output;
  for (unsigned segment=0;segment<segments;++segment) {
    const auto n=segment+1==segments?3u:full_rows;
    for (unsigned r=0;r<n;++r) {
      const auto ordinal=output.empty()?0:1+(output.size()-1)/3;
      const auto id=segment+1==segments?UINT64_MAX:ordinal<256?ordinal:ordinal<512?
          65536+ordinal-256:ordinal<768?(UINT64_C(1)<<32)+ordinal-512:(UINT64_C(1)<<40)+ordinal-768;
      const auto slot=segment+1==segments?std::array<unsigned,3>{255,256,65535}[r]:r;
      Record row{(std::uint64_t(segment*17)<<16)|slot,{}};
      row.fields[0]=id;
      for (unsigned f=1;f<8;++f) row.fields[f]=mix(row.physical*17+f);
      output.push_back(row);
    }
  }
  return output;
}
inline std::vector<Record> equality(std::span<const Record> input,std::uint64_t id) {
  std::vector<Record> output;
  for (const auto& row:input) if (row.fields[0]==id) output.push_back(row);
  return output;
}
inline std::vector<Cell> summaries(std::span<const Record> input,unsigned layer) {
  if (layer>=4) throw std::invalid_argument("summary layer");
  std::vector<Cell> output;
  for (const auto& row:input) {
    const auto prefix=std::uint32_t(row.physical>>(48-8*layer));
    const auto position=std::uint16_t(row.physical>>(32-8*layer));
    if (output.empty() || output.back().prefix!=prefix || output.back().position!=position)
      output.push_back({layer,prefix,position,0,{}});
    auto& cell=output.back();
    for (unsigned f=0;f<8;++f) {
      const auto value=row.fields[f]; auto& a=cell.fields[f];
      if (!cell.records) a.minimum=a.maximum=value;
      else { if (value<a.minimum) a.minimum=value; if (value>a.maximum) a.maximum=value; }
      a.sum+=U128(value); ++a.not_null;
    }
    ++cell.records;
  }
  return output;
}
inline void apply(std::span<Record> input,std::span<const Command> commands) {
  for (const auto& c:commands) {
    if (!c.field || c.field>=8) throw std::invalid_argument("payload field");
    bool found=false;
    for (auto& row:input) if (row.physical==c.physical) {
      if (row.fields[0]!=c.id) throw std::invalid_argument("row identity");
      row.fields[c.field]=c.value; found=true; break;
    }
    if (!found) throw std::invalid_argument("missing row");
  }
}
}

#include "workload.h"
#include "packed-page.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <new>
#include <numeric>
#include <random>
#include <stdexcept>
namespace b = orbital_bench;
namespace p = b::packed;
namespace {
std::atomic<std::size_t> reject_size{};
void require(bool ok, const char* why) {
  if (!ok) { std::fprintf(stderr, "split transfer: %s\n", why); std::abort(); }
}
b::Record record(std::uint64_t i) {
  return {0xfedcba9876543210ull ^ (i * 0x102030405ull),
          i % 3 == 0 ? b::value_mask : std::uint32_t((i * 73471) & b::value_mask),
          std::uint32_t(0x87654321u ^ (i * 0xf0e1d2c3u))};
}
std::vector<b::Row> rows() {
  std::vector<b::Row> out;
  for (std::size_t i = 0; i < 256; ++i) out.push_back({8*i+8, record(i)});
  return out;
}
std::uint64_t word(const std::byte* p, unsigned bytes) {
  std::uint64_t v = 0;
  for (unsigned i = 0; i < bytes; ++i) v |= std::uint64_t(std::to_integer<unsigned>(p[i])) << (8*i);
  return v;
}
void verify(b::Store& store, const std::map<b::Key,b::Record>& expected) {
  store.audit();
  std::vector<b::Row> wanted;
  for (const auto& [key, value] : expected) wanted.push_back({key,value});
  std::vector<b::Row> got(expected.size()+7);
  store.begin(false);
  require(store.scan(0, got) == wanted.size(), "scan size differs from map oracle");
  for (const auto& row : wanted) require(store.get(row.key) == row.record, "point differs from map oracle");
  store.end();
  require(std::equal(wanted.begin(),wanted.end(),got.begin()), "scan differs from map oracle");
  std::size_t occupied_count = 0;
  for (const auto& region : store.memory_regions()) if (region.kind == "allocation") {
    const auto* base = reinterpret_cast<const std::byte*>(region.address);
    require(region.bytes == p::page_bytes, "page extent changed");
    for (std::size_t slot=0; slot<256; ++slot) {
      const auto* tuple=base+p::tuple_offset+slot*p::tuple_stride;
      const auto key=word(tuple,8), next=word(tuple+8,8), tag=word(tuple+16,4);
      const auto bitmap=word(base+p::bitmap_offset+(slot/64)*8,8);
      if (bitmap & (std::uint64_t{1} << (slot%64))) {
        ++occupied_count;
        const auto it=expected.find(key);
        require(it != expected.end() && it->second.next==next && it->second.tag==tag,
                "occupied tuple wire bytes differ from independent field encoding");
      }
    }
  }
  require(occupied_count == expected.size(), "bitmap row count differs");
}
std::vector<std::byte> allocation_bytes(b::Store& store) {
  std::vector<std::byte> out;
  for (const auto& region : store.memory_regions()) if (region.kind == "allocation") {
    const auto* begin=reinterpret_cast<const std::byte*>(region.address);
    out.insert(out.end(),begin,begin+region.bytes);
  }
  return out;
}
void fragmented(b::Access access, unsigned seed) {
  auto initial=rows();
  auto store=b::packed_store(64u<<20,access,true);
  store->load(initial);
  std::map<b::Key,b::Record> expected;
  for (const auto& row:initial) expected.emplace(row.key,row.record);
  // Leave both fences, then refill in a shuffled order. Right-half logical
  // ranks now span arbitrary physical slots, not contiguous source regions.
  std::array<unsigned,254> order;
  std::iota(order.begin(),order.end(),1);
  for (const auto i:order) {
    require(b::execute(*store,{b::Kind::erase,initial[i].key,{},1}).changes==1,"erase failed");
    expected.erase(initial[i].key);
  }
  std::mt19937 random(seed); std::shuffle(order.begin(),order.end(),random);
  for (const auto i:order) {
    auto value=record(i+1024);
    require(b::execute(*store,{b::Kind::insert,initial[i].key,value}).changes==1,"refill failed");
    expected.emplace(initial[i].key,value);
  }
  unsigned touched=0;
  for (const auto& region:store->memory_regions()) if (region.kind=="rank_index") {
    const auto* ranks=reinterpret_cast<const std::uint16_t*>(region.address);
    for (unsigned i=128;i<256;++i) touched |= 1u << (ranks[i]/16);
  }
  require(touched==65535,"fixture did not scatter moved ranks across all Series regions");
  const auto key=initial[seed%128].key+1; const auto value=record(9999+seed);
  const auto before=store->counters(false);
  std::uintptr_t old_address=0;
  for(const auto& r:store->memory_regions()) if(r.kind=="allocation") old_address=r.address;
  const auto old_bytes=allocation_bytes(*store);
  require(b::execute(*store,{b::Kind::insert,key,value}).changes==1,"split insert failed");
  expected.emplace(key,value);
  {
    const auto* base=reinterpret_cast<const std::byte*>(old_address);
    std::size_t retained=0;
    for(std::size_t slot=0;slot<256;++slot) {
      const auto bitmap=word(base+p::bitmap_offset+(slot/64)*8,8);
      if(bitmap & (std::uint64_t{1} << (slot%64))) continue;
      const auto offset=p::tuple_offset+slot*p::tuple_stride;
      require(std::equal(base+offset,base+offset+p::tuple_stride,old_bytes.begin()+offset),
              "split wrote an invisible old tuple");
      ++retained;
    }
    require(retained==127,"expected all127 unused old slots to retain their tuple bytes");
  }
  require(store->counters(false).splits==before.splits+1,"fixture did not split");
  verify(*store,expected);
  // Reuse the cleared physical slots and subsequently split again.
  for (unsigned i=0;i<255;++i) {
    const auto k=initial[i].key+2; const auto v=record(i+8192);
    require(b::execute(*store,{b::Kind::insert,k,v}).changes==1,"postsplit reuse failed");
    expected.emplace(k,v);
  }
  verify(*store,expected);
  {
    const auto erased=std::next(expected.begin(),seed)->first;
    const std::byte* retired=nullptr;
    std::array<std::byte,p::tuple_stride> original{};
    for(const auto& region:store->memory_regions()) if(region.kind=="allocation") {
      const auto* base=reinterpret_cast<const std::byte*>(region.address);
      for(std::size_t slot=0;slot<256;++slot) {
        const auto bitmap=word(base+p::bitmap_offset+(slot/64)*8,8);
        const auto* tuple=base+p::tuple_offset+slot*p::tuple_stride;
        if((bitmap & (std::uint64_t{1} << (slot%64))) && word(tuple,8)==erased) {
          retired=tuple;std::copy_n(tuple,p::tuple_stride,original.begin());
        }
      }
    }
    require(retired,"erase fixture did not locate live slot");
    require(b::execute(*store,{b::Kind::erase,erased,{},1}).changes==1,"erase after split failed");
    expected.erase(erased);
    require(std::equal(original.begin(),original.end(),retired),"erase rewrote invisible tuple fields");
    verify(*store,expected);
    const auto replacement=record(0xfedc+seed);
    require(b::execute(*store,{b::Kind::insert,erased,replacement}).changes==1,"erase/refill failed");
    expected.emplace(erased,replacement);verify(*store,expected);
  }
}
void failure(b::Access access, std::size_t bytes, bool malformed) {
  auto initial=rows(); auto store=b::packed_store(64u<<20,access,true); store->load(initial);
  std::span<std::uint16_t> ranks;
  for(const auto& r:store->memory_regions()) if(r.kind=="rank_index")
    ranks={reinterpret_cast<std::uint16_t*>(r.address),r.bytes/2};
  const auto last=ranks.back();
  if(malformed) ranks.back()=std::numeric_limits<std::uint16_t>::max();
  const auto original=allocation_bytes(*store); const auto before=store->counters(false);
  bool rejected=false;
  reject_size=bytes; store->begin(true);
  try { (void)store->insert(initial[0].key+1,record(123)); }
  catch(const std::bad_alloc&) { require(!malformed,"unexpected allocation failure"); rejected=true; }
  catch(const std::runtime_error&) { require(malformed,"unexpected operation error"); rejected=true; }
  store->end(); reject_size=0;
  require(rejected,"failure was not exercised");
  require(allocation_bytes(*store)==original,"rejected split changed original encoded bytes");
  const auto after=store->counters(false);
  require(after.logical_live_rows==before.logical_live_rows && after.splits==before.splits &&
          after.issued_write_bytes==before.issued_write_bytes,"rejected split changed live/pack maintenance");
  ranks.back()=last;
  std::map<b::Key,b::Record> expected;
  for(const auto& row:initial) expected.emplace(row.key,row.record);
  verify(*store,expected);
  const auto k=initial[0].key+1; const auto v=record(123);
  require(b::execute(*store,{b::Kind::insert,k,v}).changes==1,"failure retry did not split");
  expected.emplace(k,v); verify(*store,expected);
}
}
void* operator new(std::size_t bytes) {
  auto expected=bytes;
  if(bytes && reject_size.compare_exchange_strong(expected,0)) throw std::bad_alloc();
  if(auto* p=std::malloc(bytes ? bytes : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
int main() {
  for(auto access:{b::Access::owner_ready,b::Access::fresh_hold,b::Access::retained_hold}) {
    for(unsigned seed=1;seed<=8;++seed) fragmented(access,seed);
    failure(access,sizeof(p::Page),false);
    failure(access,0,true);
  }
  std::puts("split transfer: fragmented gather, independent map/wire/spare checks, slot reuse, allocation failures and retry pass in all access modes");
}

#include "graph_scan.h"
#include "group_fixture.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory_resource>
#include <source_location>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace index_spike;
std::uint64_t cases{},items{};
void check(bool yes,const char* why,
    std::source_location at=std::source_location::current()) {
  if (yes) return;
  std::fprintf(stderr,"%s:%u: %s\n",at.file_name(),at.line(),why); std::abort();
}
template<class T> T take(orbital::Result<T> result) {
  check(bool(result),"real module refused scan fixture"); return std::move(*result);
}
std::uint64_t mix(std::uint64_t x) {
  x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
  x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);
  return x^(x>>31);
}
Record record(std::uint32_t prefix) {
  Record out{prefix};
  for (unsigned field=0;field<8;++field) {
    const auto value=mix(std::uint64_t(prefix)*17+field+71);
    std::memcpy(out.material.data()+field*8,&value,8);
    std::memcpy(out.descriptor.data()+field*8,&value,8);
  }
  return out;
}
std::uint64_t digest(const Record& row) {
  std::uint64_t result=0;
  for (unsigned field=0;field<8;++field) {
    std::uint64_t word; std::memcpy(&word,row.material.data()+field*8,8); result^=word;
  }
  return result;
}
struct Mask {
  std::uint64_t base{},bits{};
  std::vector<std::uint64_t> words;
};
Mask mask(unsigned kept,std::uint64_t base=0,std::uint64_t bits=1025) {
  Mask out{base,bits,std::vector<std::uint64_t>((bits+63)/64)};
  for (unsigned word=0;word<out.words.size();++word) {
    // A seeded permutation of all64 positions: exact density without periodic
    // power-of-two selection or a contiguous-run assumption.
    std::array<unsigned,64> order;
    for (unsigned i=0;i<64;++i) order[i]=i;
    auto state=mix(713+word);
    for (unsigned i=63;i;--i) {
      state=mix(state); std::swap(order[i],order[state%(i+1)]);
    }
    for (unsigned i=0;i<kept;++i) out.words[word]|=UINT64_C(1)<<order[i];
  }
  if (bits%64) out.words.back()&=(UINT64_C(1)<<(bits%64))-1;
  return out;
}
bool selected(const Mask* mask,std::uint32_t prefix) {
  if (!mask) return true;
  if (prefix<mask->base || std::uint64_t(prefix)-mask->base>=mask->bits) return false;
  const auto offset=prefix-mask->base;
  return (mask->words[offset/64]>>(offset%64))&1;
}
const std::byte* root(const Store::Scope& scope) {
  const auto selected=scope.bind({GroupFixture::graph_space,GroupFixture::root_base});
  return selected?take(selected.part(0)).address():nullptr;
}
void expect(GroupFixture& fixture,const Store::Scope& scope,
    const std::vector<Record>& oracle,std::uint32_t low,std::uint32_t high,const Mask* mask) {
  std::vector<ScanItem> output(oracle.size()+1,ScanItem{0xa5a5a5a5,0x5a5a5a5a,UINT64_MAX});
  // Nonnull empty-mask pointer must never be read, including at max-prefix.
  const std::uint64_t empty_mask=0;
  GraphScanRequest request{root(scope),mask?(mask->words.empty()?&empty_mask:
      mask->words.data()):nullptr,mask?mask->base:0,mask?mask->bits:0,low,high,
      output.data(),oracle.size()};
  ScanCounters counters;
  const auto reference=graph_scan_reference(request,&counters);
#if defined(__aarch64__) || defined(__x86_64__)
  const auto reference_output=output;
  const auto reference_counters=counters;
  std::fill(output.begin(),output.end(),ScanItem{0xa5a5a5a5,0x5a5a5a5a,UINT64_MAX});
  const auto result=idx_asm_graph_scan64(&request,&counters);
  check(result.sum==reference.sum && result.selected==reference.selected &&
      !std::memcmp(&counters,&reference_counters,sizeof(counters)),"native/reference scan result/counters differ");
  check(!std::memcmp(output.data(),reference_output.data(),output.size()*sizeof(ScanItem)),
      "native/reference ordered output or untouched tail differs");
#if defined(__aarch64__)
  for (auto candidate:{idx_asm_graph_scan64_dense,idx_asm_graph_scan64_lookahead_none,
      idx_asm_graph_scan64_lookahead}) {
    std::fill(output.begin(),output.end(),ScanItem{0xa5a5a5a5,0x5a5a5a5a,UINT64_MAX});
    const auto actual=candidate(&request,&counters);
    check(actual.sum==reference.sum && actual.selected==reference.selected &&
        !std::memcmp(&counters,&reference_counters,sizeof(counters)) &&
        !std::memcmp(output.data(),reference_output.data(),output.size()*sizeof(ScanItem)),
        "candidate native/reference output/counters differ");
  }
#endif
#else
  const auto result=reference;
#endif
  std::uint64_t sum=0,count=0,visited=0;
  for (const auto& row:oracle) {
    if (row.prefix<low || row.prefix>high || low>high) continue;
    ++visited;
    if (!selected(mask,row.prefix)) continue;
    const auto wanted=digest(row);
    check(output[count].prefix==row.prefix && !output[count].reserved &&
        output[count].xor8==wanted,"ordered scan output/row association differs");
    // Independently bind actual material on the same Scope, checking ALL bytes
    // and directory pointer association, not only the commutative reduction.
    const auto actual=scope.bind(GroupFixture::material_id(row.prefix));
    check(bool(actual),"selected Segment absent");
    const auto* bytes=take(actual.part(0)).address();
    check(!std::memcmp(bytes,row.material.data(),64),"scan selected wrong complete material");
    ReadContext context{GroupFixture::graph_space,&scope,{}};
    const auto view=fixture.view(context);
    const auto* descriptor=fixture.read(view,row.prefix);
    const std::byte* pointer; std::memcpy(&pointer,descriptor,8);
    check(pointer==bytes,"scan directory association disagrees with actual Segment");
    sum+=wanted; ++count;
  }
  check(result.sum==sum && result.selected==count,"complete range reduction differs");
  std::uint64_t groups=0,nodes=0;
  if (low<=high) {
    nodes=1;
    const auto first=oracle.front().prefix>>4,last=oracle.back().prefix>>4;
    if ((low>>4)<=last && (high>>4)>=first) {
      const auto width=std::bit_width(first^last);
      const unsigned root_shift=width?((width-1)/5)*5:0;
      std::vector<std::uint64_t> node_ids,group_ids;
      for (const auto& row:oracle) {
        const auto group=row.prefix>>4;
        if (group>=(low>>4) && group<=(high>>4)) group_ids.push_back(group);
        for (int shift=root_shift;shift>=0;shift-=5) {
          const auto span=(UINT64_C(1)<<(shift+5))-1;
          const auto base=group&~span;
          if (base<=(high>>4) && base+span>=(low>>4))
            node_ids.push_back((std::uint64_t(shift)<<32)|base);
        }
      }
      std::sort(node_ids.begin(),node_ids.end());
      nodes=std::unique(node_ids.begin(),node_ids.end())-node_ids.begin();
      std::sort(group_ids.begin(),group_ids.end());
      groups=std::unique(group_ids.begin(),group_ids.end())-group_ids.begin();
    }
  }
  if (mask && !mask->bits) visited=groups=nodes=0; // empty-domain fast path
  check(counters.visited_dirents==visited && counters.material_bytes==64*count,
      "visited/selected/material counters use wrong denominator");
  check(counters.visited_nodes==nodes && counters.visited_groups==groups,
      "saved-frame traversal repeats or skips a real node/group");
  check(output[count].prefix==0xa5a5a5a5 && output[count].xor8==UINT64_MAX,
      "scan output exceeded selected result count");
  request.output=nullptr; request.output_capacity=0;
  const auto reduced=graph_scan_reference(request,nullptr);
  check(reduced.sum==sum && reduced.selected==count,"no-output reduction differs");
#if defined(__aarch64__) || defined(__x86_64__)
  const auto native_reduced=idx_asm_graph_scan64(&request,nullptr);
  check(native_reduced.sum==sum && native_reduced.selected==count,"native no-output reduction differs");
#if defined(__aarch64__)
  for (auto candidate:{idx_asm_graph_scan64_dense,idx_asm_graph_scan64_lookahead_none,
      idx_asm_graph_scan64_lookahead}) {
    const auto reduced=candidate(&request,nullptr);
    check(reduced.sum==sum && reduced.selected==count,"candidate no-output reduction differs");
  }
#endif
#endif
  ++cases; items+=count;
}
void all_ranges(GroupFixture& fixture,const Store::Scope& scope,const std::vector<Record>& oracle) {
  const std::array<std::array<std::uint32_t,2>,20> ranges{{
      {0,UINT32_MAX},{0,0},{1,16},{15,64},{16,255},
      {63,256},{127,129},{257,511},{511,513},{8191,8193},{UINT32_MAX-15,UINT32_MAX},
      {UINT32_MAX,UINT32_MAX},{0x1f0,0xabcde100},{9,8},
      {0,62},{0,63},{0,64},{0,126},{0,127},{0,128}}};
  for (const auto& range:ranges) {
    expect(fixture,scope,oracle,range[0],range[1],nullptr);
    for (unsigned kept:{0u,1u,8u,32u,64u}) {
      const auto filter=mask(kept);
      expect(fixture,scope,oracle,range[0],range[1],&filter);
    }
    const auto tail=mask(32,UINT64_C(0xffffff80),128);
    expect(fixture,scope,oracle,range[0],range[1],&tail);
    const Mask empty{0,0,{}};
    expect(fixture,scope,oracle,range[0],range[1],&empty);
  }
}
struct FaultResource final:std::pmr::memory_resource {
  bool refuse{};
  std::size_t live{};
  void* do_allocate(std::size_t bytes,std::size_t alignment) override {
    if (refuse) throw std::bad_alloc{};
    auto* p=std::pmr::new_delete_resource()->allocate(bytes,alignment); live+=bytes; return p;
  }
  void do_deallocate(void* p,std::size_t bytes,std::size_t alignment) override {
    check(live>=bytes,"metadata underflow"); live-=bytes;
    std::pmr::new_delete_resource()->deallocate(p,bytes,alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this==&other; }
};
void run(loom::Worker& worker,unsigned grain,bool sparse,std::uint32_t offset=0,bool eleven=false) {
  std::vector<Record> input;
  if (eleven) {
    for (unsigned prefix=0;prefix<320;++prefix)
      if (prefix%16<11) input.push_back(record(prefix));
  } else if (sparse) {
    for (std::uint32_t prefix:{0u,1u,14u,15u,16u,31u,32u,63u,64u,127u,128u,255u,256u,
        511u,512u,8191u,8192u,1u<<20,UINT32_MAX-15,UINT32_MAX}) input.push_back(record(prefix));
  } else for (unsigned prefix=0;prefix<257;++prefix) input.push_back(record(offset+prefix));
  FaultResource memory;
  {
    GroupFixture fixture(worker,input,&memory,true,grain);
    Store::Scope old(fixture.store()),late(fixture.store());
    all_ranges(fixture,old,input);
    memory.refuse=true;
    check(!fixture.replace(input.back().prefix,0,713),"capture refusal not surfaced");
    memory.refuse=false;
    fixture.store().drain();
    all_ranges(fixture,late,input); // first later-leaf discovery after failed attempt
    Store::Scope never_read(fixture.store());
    auto after=input;
    for (unsigned i:{0u,unsigned(input.size()/2),unsigned(input.size()-1)}) {
      const auto value=mix(input[i].prefix+977ULL);
      check(bool(fixture.replace(input[i].prefix,7,value)),"actual moving-Part update refused");
      std::memcpy(after[i].material.data()+56,&value,8);
    }
    fixture.store().drain();
    while (true) {
      const auto trimmed=orbital::allocation::trim_cold(*worker.memory,0);
      check(trimmed.kind()!=orbital::Progress::Kind::failed,"actual physical trimming refused");
      if (trimmed.kind()==orbital::Progress::Kind::done) break;
      loom::poll(worker,{64,0});
    }
    all_ranges(fixture,late,input);
    all_ranges(fixture,never_read,input); // FIRST bind after successful install/drain/trim
    Store::Scope fresh(fixture.store()); all_ranges(fixture,fresh,after);
  }
  check(!memory.live,"scan fixture retained metadata after all owners/readers ended");
}
void guarded_kernel() {
  const auto page=std::size_t(sysconf(_SC_PAGESIZE));
  std::array<std::byte*,5> mappings;
  for (auto& mapping:mappings) {
    mapping=static_cast<std::byte*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    check(mapping!=MAP_FAILED && !mprotect(mapping+page,page,PROT_NONE),"scan guard mapping");
  }
  auto* node=mappings[0]+page-576;
  auto* payload=mappings[1]+page-3*64;
  auto* material=mappings[2]+page-64;
  auto* mask_bytes=mappings[3]+page-8;
  auto* output=reinterpret_cast<ScanItem*>(mappings[4]+page-16);
  const std::uint32_t group=31;
  node[0]=std::byte{1};
  std::memcpy(node+8,&group,4); std::memcpy(node+12,&group,4);
  std::memcpy(node+64+31*16,&payload,8);
  const std::uint16_t original=(1u<<1)|(1u<<14)|(1u<<15);
  std::memcpy(node+64+31*16+8,&original,2);
  for (unsigned rank=0;rank<3;++rank) std::memcpy(payload+rank*64,&material,8);
  const auto expected=record(511);
  std::memcpy(material,expected.material.data(),64);
  const std::uint64_t selected=UINT64_C(1)<<63;
  std::memcpy(mask_bytes,&selected,8);
  GraphScanRequest request{node,reinterpret_cast<const std::uint64_t*>(mask_bytes),448,64,
      0,UINT32_MAX,output,1};
  ScanCounters counters;
  auto result=graph_scan_reference(request,&counters);
  check(result.selected==1 && result.sum==digest(expected) && output->prefix==511 &&
      counters.visited_dirents==3 && counters.material_bytes==64,"guarded reference rank/extent");
#if defined(__aarch64__) || defined(__x86_64__)
  result=idx_asm_graph_scan64(&request,&counters);
  check(result.selected==1 && result.sum==digest(expected) && output->prefix==511 &&
      counters.visited_dirents==3 && counters.material_bytes==64,"guarded native rank/extent");
  request.root=mappings[0]+page; request.mask_bits=0; request.mask=
      reinterpret_cast<const std::uint64_t*>(mappings[3]+page);
  result=idx_asm_graph_scan64(&request,&counters);
  check(!result.selected && !result.sum && !counters.visited_nodes,
      "empty-domain native scan touched forbidden root/mask");
#endif
  for (auto* mapping:mappings) check(!munmap(mapping,page*2),"scan guard unmap");
}
}
int main() {
  guarded_kernel();
  loom::Worker worker;
  check(bool(loom::adopt(worker)),"Worker adoption failed");
  auto arena=take(orbital::allocation::open_arena(32u<<20));
  check(bool(loom::configure(worker,std::move(arena),0)),"Worker configuration failed");
  for (unsigned grain:{16u,64u,256u}) {
    run(worker,grain,false); run(worker,grain,true);
    run(worker,grain,false,0xabcde000); // root omits common high prefix bits
    run(worker,grain,false,0,true); // full pointer tiles end WITHIN sparse original groups
  }
  const GraphScanRequest empty{};
  ScanCounters counters;
  const auto result=graph_scan_reference(empty,&counters);
  check(!result.sum && !result.selected && !counters.visited_nodes,"null graph not empty");
#if defined(__aarch64__) || defined(__x86_64__)
  const auto native=idx_asm_graph_scan64(&empty,&counters);
  check(!native.sum && !native.selected && !counters.visited_nodes,"native null graph not empty");
#endif
  while (true) {
    const auto progress=loom::shutdown(worker);
    check(progress.kind()!=orbital::Progress::Kind::failed,"physical shutdown failed");
    if (progress.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
  std::printf("PASS: saved-frame graph range; cases=%llu items=%llu; native/reference when ISA available, ordered full64 association, masks, sparse/max/boundaries, late old/fresh/refusal/trim; no timing\n",
      static_cast<unsigned long long>(cases),static_cast<unsigned long long>(items));
}

#include "composed_runtime.h"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace p = orbital_versioned_performance;
namespace c = orbital_mvcc::composed;
namespace b = orbital_bench;
void require(bool v,const char* why) {if(!v)throw std::runtime_error(why);}
struct Options {
  std::size_t rows=1024,ops=513,batch=64,age=0;
  bool latency=false,accounting=false;
  std::string samples;
};
std::size_t number(std::string_view s) {
  std::size_t n{};const auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),n);
  require(error==std::errc{} && end==s.data()+s.size(),"invalid number");return n;
}
Options options(int argc,char** argv) {
  Options o;
  for(int i=1;i<argc;++i) {
    const std::string_view key=argv[i];require(++i<argc,"missing value");const std::string_view v=argv[i];
    if(key=="--rows")o.rows=number(v);else if(key=="--ops")o.ops=number(v);
    else if(key=="--batch")o.batch=number(v);else if(key=="--retention-age")o.age=number(v);
    else if(key=="--mode") {require(v=="throughput"||v=="latency","invalid mode");o.latency=v=="latency";}
    else if(key=="--accounting") {require(v=="0"||v=="1","invalid accounting");o.accounting=v=="1";}
    else if(key=="--latency-samples")o.samples=v;
    else throw std::invalid_argument("unknown option");
  }
  require(o.rows && o.ops && (o.batch==1||o.batch==64),"invalid extents");
  require(o.samples.empty() || (o.latency && !std::filesystem::exists(o.samples)),"samples require latency and new file");
  return o;
}
void footprint(const c::Inline& store) {
  const auto s=store.statistics();const auto a=store.accounting();
  std::cout<<"{\"versions\":"<<s.versions<<",\"contexts\":"<<s.scopes
    <<",\"metadata_live\":"<<a.metadata_live<<",\"metadata_peak\":"<<a.metadata_peak
    <<",\"active_allocations\":"<<a.active_allocations<<",\"live_capacity_bytes\":"<<a.live_capacity_bytes
    <<",\"held_extent_bytes\":";if(a.held_extent_bytes)std::cout<<*a.held_extent_bytes;else std::cout<<"null";
  std::cout<<",\"arena_charge\":"<<a.arena_charge<<",\"pool_mapped_bytes\":"<<a.pool.mapped_bytes
    <<",\"pool_idle_bytes\":"<<a.pool.idle_bytes<<",\"pool_allocations\":"<<a.pool.allocations
    <<",\"pool_reuses\":"<<a.pool.reuses<<",\"retirement_records\":null}";
}
int main(int argc,char** argv) try {
  const auto o=options(argc,argv);p::Corpus corpus(o.rows,1,8132);
  const auto trace=b::make_trace("append",o.rows,o.ops,8133);
  std::vector<b::Row> expected=corpus.images[0].rows;expected.reserve(o.rows+o.ops);
  for(const auto& op:trace.operations)expected.push_back({op.key,op.record});
  c::Inline store(corpus,1024ULL*1024*1024,p::data::Format::rows,64*1024*1024);
  std::optional<c::Inline::Scope> old(std::in_place,store.catalog());
  std::size_t old_count=o.rows,published{},comparisons{};
  std::vector<std::uint64_t> samples;samples.reserve((o.ops+o.batch-1)/o.batch);
  struct Sample {std::size_t publication,count,live;std::optional<std::size_t> extent;std::size_t charge,versions;};std::vector<Sample> accounting;
  accounting.reserve((o.ops+o.batch-1)/o.batch);
  std::cout<<"{\"passed\":true,\"rows\":"<<o.rows<<",\"operations\":"<<o.ops<<",\"batch\":"<<o.batch
    <<",\"retention_age\":"<<o.age<<",\"mode\":\""<<(o.latency?"latency":"throughput")
    <<"\",\"accounting_replay\":"<<(o.accounting?"true":"false")<<",\"timing_valid\":"<<(o.accounting?"false":"true")
    <<",\"initial\":";footprint(store);
  const auto process=p::process_cpu_ns(),cpu=p::caller_cpu_ns(),start=p::clock_ns();
  for(std::size_t first=0;first<o.ops;first+=o.batch) {
    const auto n=std::min(o.batch,o.ops-first);const auto group_start=o.latency?p::clock_ns():0;
    std::array<b::Row,64> tail;
    for(std::size_t i=0;i<n;++i)tail[i]={trace.operations[first+i].key,trace.operations[first+i].record};
    p::need(store.append(std::span(tail).first(n)));++published;
    if(o.age && published%o.age==0) {
      // Count and previous last-row witness belong to the old cut before refresh.
      const auto before=store.body(*old,0);
      require(before.size()==old_count,"old count changed before rotation");
      require(before.row_unchecked(old_count-1)==expected[old_count-1],"old last row changed before rotation");
      ++comparisons;old.reset();old.emplace(store.catalog());old_count=o.rows+first+n;store.drain();store.poll(128);
    }
    if(o.latency)samples.push_back(p::clock_ns()-group_start);
    if(o.accounting) {
      const auto a=store.accounting();const auto s=store.statistics();
      accounting.push_back({published,o.rows+first+n,a.live_capacity_bytes,a.held_extent_bytes,
        std::size_t(a.arena_charge),s.versions});
    }
  }
  const auto wall=p::clock_ns()-start,caller=p::caller_cpu_ns()-cpu,process_elapsed=p::process_cpu_ns()-process;
  std::uint64_t digest{};
  {
    c::Inline::Scope current(store.catalog());const auto now=store.body(current,0),before=store.body(*old,0);
    require(now.size()==expected.size() && before.size()==old_count,"selected count differs from vector oracle");
    for(std::size_t i=0;i<expected.size();++i) {
      const auto row=now.row_unchecked(i);require(row==expected[i],"current row differs from vector oracle");
      b::detail::mix(digest,row.key);b::detail::mix(digest,row.record.next);b::detail::mix(digest,row.record.value);b::detail::mix(digest,row.record.tag);++comparisons;
      if(i<old_count) {require(before.row_unchecked(i)==expected[i],"old prefix differs from vector oracle");++comparisons;}
    }
    require(!before.get(expected.back().key) || old_count==expected.size(),"old count exposed appended tail");
  }
  std::cout<<",\"held\":";footprint(store);
  const auto release_start=p::clock_ns();old.reset();const auto drain=store.drain();store.poll(65536);
  const auto release_ns=p::clock_ns()-release_start;
  require(store.statistics().versions==1 && store.statistics().scopes==0,"final logical settlement differs");
  std::cout<<",\"released\":";footprint(store);
  std::uint64_t expected_copy{},expected_request{};
  for(std::size_t first=0;first<o.ops;first+=o.batch) {
    const auto n=std::min(o.batch,o.ops-first);expected_copy+=(o.rows+first)*p::data::tuple_bytes;
    expected_request+=(o.rows+first+n)*p::data::tuple_bytes;
  }
  const auto& k=store.costs.kernel;
  require(store.costs.copied_bytes==expected_copy && store.costs.requested_body_bytes==expected_request &&
    k.completed_rows==o.ops && k.encoded_values==4*o.ops && k.constructor_calls==o.ops && k.decoded_values==0,"copy/codec counts differ from independent formula");
  if(!o.samples.empty()) {
    std::ofstream out(o.samples);require(bool(out),"cannot write latency samples");out<<"publication,rows,ns\n";
    for(std::size_t i=0;i<samples.size();++i)out<<i<<','<<std::min(o.batch,o.ops-i*o.batch)<<','<<samples[i]<<'\n';
  }
  std::cout<<",\"wall_ns\":"<<wall<<",\"caller_cpu_ns\":"<<caller<<",\"process_cpu_ns\":"<<process_elapsed
    <<",\"publications\":"<<published<<",\"old_count\":"<<old_count<<",\"final_count\":"<<expected.size()
    <<",\"prefix_copied_bytes\":"<<expected_copy<<",\"requested_body_bytes\":"<<expected_request
    <<",\"tail_encoded_values\":"<<k.encoded_values<<",\"tail_constructor_calls\":"<<k.constructor_calls
    <<",\"comparisons\":"<<comparisons<<",\"digest\":"<<digest<<",\"release_ns\":"<<release_ns<<",\"drain_steps\":"<<drain
    <<",\"transient_peak_body_capacity\":null,\"postpublication_samples\":[";
  bool comma=false;for(const auto& v:accounting) {
    if(comma)std::cout<<',';comma=true;
    std::cout<<"{\"publication\":"<<v.publication<<",\"count\":"<<v.count<<",\"live_capacity\":"<<v.live<<",\"held_extent\":";if(v.extent)std::cout<<*v.extent;else std::cout<<"null";
    std::cout<<",\"held_extent_available\":"<<(v.extent?"true":"false")<<",\"arena_charge\":"<<v.charge<<",\"versions\":"<<v.versions<<'}';
  }
  std::cout<<"]}\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}

#include "run.h"
#include "oracle.h"
#include "pmu.h"
#include "candidate_runtime.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sys/resource.h>
#include <sstream>

using namespace orbital_versioned_performance;
using namespace orbital_bench;

namespace {
struct Options {
  std::string backend{"orbital"}, workload{"point-hit"}, mode{"throughput"}, from{"rows"}, db;
  std::string codec{"legacy"},conditioning{"none"},samples;
  std::string sample_storage{"equal"};
  orbital_bench::pmu::Mode pmu{orbital_bench::pmu::Mode::off};
  std::vector<orbital_bench::pmu::EventSpec> pmu_events;
  std::size_t rows{4096}, objects{1}, batch{1}, ops{}, bytes{256*1024*1024},metadata_bytes{},retention_age{};
  std::uint64_t seed{8132};
};
std::uint64_t number(std::string_view text) {
  std::uint64_t value{};
  const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  if (error!=std::errc{} || end!=text.data()+text.size()) throw std::invalid_argument("invalid unsigned integer");
  return value;
}
Options options(int argc,char** argv) {
  Options result;
  for (int i=1;i<argc;++i) {
    const std::string_view key=argv[i];
    if (key=="--help") {
      std::cout<<"--backend orbital|borrowed|lmdb|direct|row-lmdb|candidate8|candidate16|candidate32 --workload point-hit|point-miss|dependent-16|scan-16|point-update|convert\n"
          "--mode throughput|latency --rows N --objects 1..4096 --batch 1|64 --ops N --seed N --bytes N --metadata-bytes N --db NEW_DIRECTORY --from rows|columns\n";
      std::cout<<"--codec legacy|direct16|consume16 --conditioning none|warm-trace --latency-samples NEW_FILE --retention-age N_PUBLICATIONS (0 holds old reader)\n";
      std::exit(0);
    }
    if (++i==argc) throw std::invalid_argument("missing option value");
    const std::string_view value=argv[i];
    if (key=="--backend") result.backend=value;
    else if (key=="--workload") result.workload=value;
    else if (key=="--mode") result.mode=value;
    else if (key=="--from") result.from=value;
    else if (key=="--db") result.db=value;
    else if (key=="--codec") result.codec=value;
    else if (key=="--conditioning") result.conditioning=value;
    else if (key=="--latency-samples") result.samples=value;
    else if (key=="--sample-storage") result.sample_storage=value;
    else if (key=="--pmu-event") result.pmu_events.push_back(orbital_bench::pmu::parse_event_spec(value));
    else if (key=="--pmu") {
      if (value=="off") result.pmu=orbital_bench::pmu::Mode::off;
      else if (value=="basic") result.pmu=orbital_bench::pmu::Mode::basic;
      else if (value=="stalls") result.pmu=orbital_bench::pmu::Mode::stalls;
      else throw std::invalid_argument("unknown PMU mode");
    }
    else if (key=="--rows") result.rows=number(value);
    else if (key=="--objects") result.objects=number(value);
    else if (key=="--batch") result.batch=number(value);
    else if (key=="--ops") {
      result.ops=number(value);
      if (!result.ops) throw std::invalid_argument("operations must be nonzero");
    }
    else if (key=="--seed") result.seed=number(value);
    else if (key=="--bytes") result.bytes=number(value);
    else if (key=="--metadata-bytes") result.metadata_bytes=number(value);
    else if (key=="--retention-age") result.retention_age=number(value);
    else throw std::invalid_argument("unknown option");
  }
  const bool candidate=result.backend=="candidate8" || result.backend=="candidate16" || result.backend=="candidate32";
  if (result.backend!="orbital" && result.backend!="borrowed" && result.backend!="lmdb" && result.backend!="direct" && result.backend!="row-lmdb" && !candidate)
    throw std::invalid_argument("unknown backend");
  if (result.mode!="throughput" && result.mode!="latency") throw std::invalid_argument("unknown mode");
  if (result.sample_storage!="equal" && result.sample_storage!="mode") throw std::invalid_argument("sample storage must be equal or mode");
  if (result.codec!="legacy" && result.codec!="direct16" && result.codec!="consume16") throw std::invalid_argument("unknown codec treatment");
  if (result.conditioning!="none" && result.conditioning!="warm-trace") throw std::invalid_argument("unknown conditioning");
  if (result.pmu!=orbital_bench::pmu::Mode::off && result.mode!="throughput")
    throw std::invalid_argument("PMU requires a separate throughput replay");
  if (!result.pmu_events.empty() && result.pmu==orbital_bench::pmu::Mode::off)
    throw std::invalid_argument("PMU events require basic or stalls mode");
  if (!result.samples.empty() && (result.mode!="latency" || std::filesystem::exists(result.samples)))
    throw std::invalid_argument("latency samples require latency mode and a new file");
  if (result.from!="rows" && result.from!="columns") throw std::invalid_argument("unknown source format");
  if (result.batch!=1 && result.batch!=64) throw std::invalid_argument("batch must be 1 or 64");
  if (result.workload!="point-hit" && result.workload!="point-miss" && result.workload!="dependent-16" && result.workload!="scan-16" && result.workload!="point-update" && result.workload!="convert")
    throw std::invalid_argument("unsupported workload");
  if (result.workload=="point-update" && result.backend!="orbital" && result.backend!="lmdb" && !candidate)
    throw std::invalid_argument("updates require orbital or mapped lmdb");
  if (result.workload=="convert" && (result.backend!="orbital" || result.batch!=1))
    throw std::invalid_argument("conversion requires orbital and batch 1");
  if (result.workload!="convert" && result.from!="rows") throw std::invalid_argument("common read/write comparison uses Tuple rows");
  if (candidate && (result.workload=="convert" || result.codec=="direct16"))
    throw std::invalid_argument("candidate adapter supports generic updates and legacy/consume16 reads");
  if (result.retention_age && result.workload!="point-update") throw std::invalid_argument("retention-age requires point-update");
  if ((result.codec!="legacy" || result.conditioning!="none") &&
      ((result.backend!="orbital" && result.backend!="lmdb" && !candidate) || result.workload=="point-update" || result.workload=="convert"))
    throw std::invalid_argument("codec/conditioning treatment requires an owned common-Tuple read");
  if (result.backend=="row-lmdb" && result.objects!=1) throw std::invalid_argument("row-key LMDB control requires one Object");
  if ((result.backend=="lmdb" || result.backend=="row-lmdb") && result.db.empty()) throw std::invalid_argument("LMDB requires --db NEW_DIRECTORY");
  if (!result.ops) result.ops=result.workload=="convert"?2*result.objects:result.workload=="point-update"?64:10000;
  if (result.retention_age>std::numeric_limits<std::size_t>::max()/result.batch)
    throw std::invalid_argument("retention window extent overflows");
  if (result.bytes<1024*1024) throw std::invalid_argument("byte limit must be at least 1 MiB");
  if (result.metadata_bytes>=result.bytes) throw std::invalid_argument("metadata partition must leave body budget");
  return result;
}
void require(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
struct ProcessMemory {std::uint64_t rss_kib{},hwm_kib{};};
ProcessMemory process_memory() {
  ProcessMemory memory;std::ifstream status("/proc/self/status");
  if (!status) throw std::runtime_error("cannot read process memory status");
  for (std::string line;std::getline(status,line);) {
    if (!line.starts_with("VmRSS:") && !line.starts_with("VmHWM:")) continue;
    std::istringstream fields(line);std::string key,unit;std::uint64_t amount;
    fields>>key>>amount>>unit;
    if (unit!="kB") throw std::runtime_error("unexpected process memory unit");
    if (key=="VmRSS:") memory.rss_kib=amount;else memory.hwm_kib=amount;
  }
  return memory;
}
void verify(const data::ReadData& body,const Golden& expected) {
  require(body.size()==expected.size(),"final row count differs");
  std::size_t index{};
  for (const auto& [key,record]:expected) require(body.row_unchecked(index++)==Row{key,record},"final row differs from independent map");
}
void print_interval(const Interval& interval) {
  auto samples=interval.batch_ns;
  std::sort(samples.begin(),samples.end());
  const auto quantile=[&](std::size_t numerator) {
    return samples.empty()?0:samples[(samples.size()*numerator+999)/1000-1];
  };
  std::cout<<"{\"wall_ns\":"<<interval.wall_ns<<",\"caller_cpu_ns\":"<<interval.caller_cpu_ns
      <<",\"process_cpu_ns\":"<<interval.process_cpu_ns
      <<",\"snapshot_scopes\":"<<interval.snapshot_scopes<<",\"logical_requests\":"<<interval.logical_requests
      <<",\"logical_rows\":"<<interval.logical_rows<<",\"requests_per_second\":"
      <<(interval.wall_ns?double(interval.logical_requests)*1e9/double(interval.wall_ns):0)
      <<",\"batch_samples\":"<<samples.size()<<",\"batch_makespan_ns\":{\"p50\":"<<quantile(500)
      <<",\"p99\":"<<quantile(990)<<",\"p999\":"<<quantile(999)<<",\"max\":"<<(samples.empty()?0:samples.back())<<"}}";
}
void print_costs(const data::Costs& costs) {
  const auto& k=costs.kernel;
  std::cout<<"{\"requested_body_bytes\":"<<costs.requested_body_bytes<<",\"requested_parts\":"<<costs.requested_parts
      <<",\"copied_bytes\":"<<costs.copied_bytes<<",\"copied_spans\":"<<costs.copied_spans
      <<",\"decoded_values\":"<<k.decoded_values<<",\"encoded_values\":"<<k.encoded_values
      <<",\"bindings\":"<<k.bindings<<",\"constructor_calls\":"<<k.constructor_calls
      <<",\"issued_bytes\":"<<k.issued_bytes<<",\"issued_spans\":"<<k.issued_spans
      <<",\"completed_parts\":"<<k.completed_parts<<",\"completed_rows\":"<<k.completed_rows
      <<",\"scratch_bound_bytes\":"<<k.scratch_bound_bytes<<"}";
}
struct OrbitalFootprint {
  obj::Statistics metadata;
  std::uint64_t arena_charge{};
  orbital::allocation::Statistics pool;
  std::size_t core_bytes{},entry_bytes{},node_bytes{},metadata_allocation_calls{};
  std::optional<compact_history::Store32::Statistics> candidate;
};
OrbitalFootprint footprint(const Orbital& store) {
  OrbitalFootprint result;result.metadata=store.statistics();result.arena_charge=store.charged_bytes();
  result.pool=store.pool_statistics();return result;
}
template<class Store> OrbitalFootprint footprint(const Candidate<Store>& store) {
  const auto s=store.statistics();
  obj::Statistics metadata{};
  metadata.metadata_bytes=s.requested_bytes;metadata.metadata_peak=s.peak_bytes;metadata.metadata_limit=store.metadata_budget();
  metadata.objects=s.objects;metadata.versions=s.versions;metadata.contexts=s.scopes;metadata.completed_cut=store.cut();
  OrbitalFootprint result;result.metadata=metadata;result.arena_charge=store.charged_bytes();result.pool=store.pool_statistics();
  result.core_bytes=s.core_bytes;result.entry_bytes=s.entry_bytes;result.node_bytes=s.node_bytes;result.metadata_allocation_calls=store.memory_calls();
  // The nested Statistics type differs for each Store; copy named diagnostics.
  result.candidate=compact_history::Store32::Statistics{s.requested_bytes,s.peak_bytes,s.objects,s.versions,s.scopes,
      s.core_bytes,s.entry_bytes,s.node_bytes,s.alternative_count,s.allocation_calls,
      s.history_capacity_bytes,s.descriptor_directory_capacity_bytes,s.copied_metadata_bytes,s.captured_descriptor_bytes,
      s.reclamation_work,s.last_sweep_bytes,s.last_sweep_cuts,s.maintenance_refusals,
      s.object_directory_capacity_bytes,s.debt_capacity_bytes};
  return result;
}
void print_footprint(const OrbitalFootprint& f) {
  const auto& s=f.metadata;
  std::cout<<"{\"arena_charged_bytes\":"<<f.arena_charge<<",\"metadata_bytes\":"<<s.metadata_bytes
      <<",\"metadata_peak\":"<<s.metadata_peak<<",\"metadata_limit\":"<<s.metadata_limit
      <<",\"objects\":"<<s.objects<<",\"versions\":"<<s.versions<<",\"contexts\":"<<s.contexts
      <<",\"retirement_records\":"<<s.retirement_records<<",\"completed_cut\":"<<s.completed_cut
      <<",\"pool_mapped_bytes\":"<<f.pool.mapped_bytes<<",\"pool_reserved_bytes\":"<<f.pool.reserved_bytes
      <<",\"pool_metadata_bytes\":"<<f.pool.metadata_bytes<<",\"pool_idle_bytes\":"<<f.pool.idle_bytes
      <<",\"pool_allocations\":"<<f.pool.allocations<<",\"pool_reuses\":"<<f.pool.reuses
      <<",\"pool_growths\":"<<f.pool.growths<<",\"pool_trim_pages\":"<<f.pool.trim_pages;
  if (f.node_bytes) std::cout<<",\"catalog_core_bytes\":"<<f.core_bytes<<",\"catalog_entry_bytes\":"<<f.entry_bytes
      <<",\"version_node_bytes\":"<<f.node_bytes<<",\"metadata_allocation_calls\":"<<f.metadata_allocation_calls;
  if (f.candidate) {
    const auto& c=*f.candidate;
    std::cout<<",\"history_capacity_bytes\":"<<c.history_capacity_bytes
        <<",\"descriptor_directory_capacity_bytes\":"<<c.descriptor_directory_capacity_bytes
        <<",\"object_directory_capacity_bytes\":"<<c.object_directory_capacity_bytes
        <<",\"debt_capacity_bytes\":"<<c.debt_capacity_bytes
        <<",\"copied_metadata_bytes\":"<<c.copied_metadata_bytes
        <<",\"captured_descriptor_bytes\":"<<c.captured_descriptor_bytes
        <<",\"reclamation_work\":"<<c.reclamation_work
        <<",\"last_sweep_bytes\":"<<c.last_sweep_bytes<<",\"last_sweep_cuts\":"<<c.last_sweep_cuts
        <<",\"maintenance_refusals\":"<<c.maintenance_refusals;
  }
  std::cout<<"}";
}
void print_footprint(const Lmdb::Footprint& f) {
  std::cout<<"{\"map_bytes\":"<<f.map_bytes<<",\"file_used_bytes\":"<<f.file_used_bytes
      <<",\"file_bytes\":"<<f.file_bytes<<",\"data_pages\":"<<f.data_pages<<",\"tree_depth\":"<<f.tree_depth<<"}";
}
struct Faults {std::uint64_t minor{},major{};};
template<class Publish>
Interval writes(const Options& o,orbital_bench::pmu::Session& hardware,std::optional<Faults>& faults,Publish&& publish,std::span<const Operation> input,std::span<Outcome> output) {
  rusage before{},after{};
  if (getrusage(RUSAGE_SELF,&before)) throw std::runtime_error("pre-write resource sample failed");
  active_hardware=o.pmu==orbital_bench::pmu::Mode::off?nullptr:&hardware;
  auto result=o.mode=="latency"?write_trial<true>(publish,input,output,o.batch):write_trial<false>(publish,input,output,o.batch);
  active_hardware=nullptr;
  if (getrusage(RUSAGE_SELF,&after)) throw std::runtime_error("post-write resource sample failed");
  faults=Faults{std::uint64_t(after.ru_minflt-before.ru_minflt),std::uint64_t(after.ru_majflt-before.ru_majflt)};
  return result;
}
} // namespace

int main(int argc,char** argv) try {
  const auto o=options(argc,argv);
  equal_sample_preparation=o.sample_storage=="equal";
  std::optional<Faults> write_faults;
  orbital_bench::pmu::Session hardware(o.pmu,o.pmu_events);
  orbital_bench::pmu::Session release_hardware(o.pmu,o.pmu_events);
  Corpus corpus(o.rows,o.objects,o.seed);
  std::vector<Golden> original(o.objects);
  for (std::size_t object=0;object<o.objects;++object)
    for (const auto& row:corpus.images[object].rows) original[object].emplace(row.key,row.record);
  auto expected=original;
  auto last_old_expected=original;
  const bool conversion=o.workload=="convert", updating=o.workload=="point-update", latency=o.mode=="latency";
  Trace trace;
  std::vector<std::size_t> objects(o.ops);
  std::vector<Outcome> output(conversion?0:o.ops),expected_output(conversion?0:o.ops);
  const auto groups=(o.ops+o.batch-1)/o.batch;
  const auto rotations=o.retention_age?groups/o.retention_age:0;
  std::vector<Operation> witness_operations;
  std::vector<std::size_t> witness_objects;
  std::vector<Outcome> expected_witnesses,witnesses(rotations);
  const auto last_old_start=std::min(o.ops,rotations*o.retention_age*o.batch);
  if (!conversion) {
    trace=make_trace(o.workload,o.rows/o.objects,o.ops,o.seed+1);
    for (std::size_t i=0;i<o.ops;++i) {
      objects[i]=i%o.objects;
      const auto& op=trace.operations[i];
      if (updating) {
        if (o.retention_age && i%(o.retention_age*o.batch)==0 && witness_operations.size()<rotations) {
          const Operation witness{.kind=Kind::read,.key=op.key,.record={},.count=1};
          witness_operations.push_back(witness);witness_objects.push_back(objects[i]);
          expected_witnesses.push_back(oracle(expected[objects[i]],witness));
        }
        auto& record=expected[objects[i]].at(op.key);
        record.value=op.record.value;record.tag=op.record.tag;
        expected_output[i].changes=1;detail::mix(expected_output[i].digest,1);
        if (o.retention_age && i+1==last_old_start) last_old_expected=expected;
      } else expected_output[i]=oracle(expected[objects[i]],op);
    }
  }
  Interval interval,release,conditioning;
  data::Costs costs;
  std::uint64_t bindings{},drain_steps{};
  OrbitalFootprint orbital_initial,orbital_held,orbital_released;
  Lmdb::Footprint lmdb_initial,lmdb_held,lmdb_released;
  if (o.backend=="orbital" || o.backend=="borrowed") {
    const auto initial=o.from=="rows"?data::Format::rows:data::Format::columns;
    Orbital store(corpus,o.bytes,initial,o.metadata_bytes);
    store.costs={};orbital_initial=footprint(store);
    if (updating || conversion) {
      std::optional<obj::ReadContext> old(store.read());
      std::vector<data::ReadData> old_bodies;old_bodies.reserve(o.objects);
      if (!o.retention_age) for (std::size_t object=0;object<o.objects;++object) old_bodies.push_back(store.body(*old,object));
      if (updating) {
        std::size_t first{},published{},witness{};
        interval=writes(o,hardware,write_faults,[&](auto input,auto target) {
          store.update(input,target,std::span{objects}.subspan(first,input.size()));first+=input.size();
          if (o.retention_age && ++published%o.retention_age==0) {
            witnesses[witness]=read_operation(store.body(*old,witness_objects[witness]),witness_operations[witness]);++witness;
            old.reset();old.emplace(store.read());store.drain(128);store.poll(128);
          }
        },trace.operations,output);
      } else {
        std::vector<data::Format> formats(o.objects,initial);
        if (latency) interval.batch_ns.reserve(o.ops);
        hardware.start();
        const auto process_cpu=process_cpu_ns(),cpu=caller_cpu_ns(),start=clock_ns();
        for (std::size_t i=0;i<o.ops;++i) {
          const auto object=i%o.objects;
          const auto target=formats[object]==data::Format::rows?data::Format::columns:data::Format::rows;
          const auto batch_start=latency?clock_ns():0;
          store.convert(object,target);
          if (latency) interval.batch_ns.push_back(clock_ns()-batch_start);
          formats[object]=target;
        }
        interval.wall_ns=clock_ns()-start;interval.caller_cpu_ns=caller_cpu_ns()-cpu;
        interval.process_cpu_ns=process_cpu_ns()-process_cpu;hardware.stop();
        interval.snapshot_scopes=o.ops;interval.logical_requests=o.ops;interval.logical_rows=o.ops*(o.rows/o.objects);
        auto current=store.read();
        for (std::size_t object=0;object<o.objects;++object) {
          const auto body=store.body(current,object,object<o.ops);
          require(body.format()==formats[object],"conversion selected wrong format");verify(body,expected[object]);
        }
      }
      orbital_held=footprint(store);
      if (o.retention_age) for (std::size_t object=0;object<o.objects;++object) verify(store.body(*old,object),last_old_expected[object]);
      else for (std::size_t object=0;object<o.objects;++object) verify(old_bodies[object],original[object]);
      if (updating) {
        auto current=store.read();
        for (std::size_t object=0;object<o.objects;++object) verify(store.body(current,object),expected[object]);
      }
      old_bodies.clear();
      release_hardware.start();
      const auto process_cpu=process_cpu_ns(),cpu=caller_cpu_ns(),start=clock_ns();
      old.reset();drain_steps=store.drain(SIZE_MAX);store.poll(65536);
      release.wall_ns=clock_ns()-start;release.caller_cpu_ns=caller_cpu_ns()-cpu;
      release.process_cpu_ns=process_cpu_ns()-process_cpu;release_hardware.stop();
      orbital_released=footprint(store);
    } else {
      const auto read=[&](bool timed_latency,std::uint64_t& count) {
        if (o.codec=="consume16") return run_orbital_consume_reads(store,timed_latency,trace.operations,objects,output,o.batch,count);
        return o.codec=="direct16"?
            run_orbital_codec_reads(store,timed_latency,trace.operations,objects,output,o.batch,count):
            run_orbital_reads(store,o.backend=="borrowed",timed_latency,trace.operations,objects,output,o.batch,count);
      };
      if (o.conditioning=="warm-trace") {
        std::uint64_t ignored{};conditioning=read(false,ignored);
        require(output==expected_output,"warm trace differs from independent oracle");
      }
      active_hardware=o.pmu==orbital_bench::pmu::Mode::off?nullptr:&hardware;
      interval=read(latency,bindings);active_hardware=nullptr;
      orbital_held=orbital_released=footprint(store);
    }
    costs=store.costs;
  } else if (o.backend=="lmdb" || o.backend=="row-lmdb") {
    Lmdb store(o.db,o.bytes);
    if (o.backend=="row-lmdb") store.load(corpus.images[0].rows);
    else {auto write=store.edit();for (const auto& image:corpus.images) write.put_part(image.address,image.encoded);write.publish();}
    lmdb_initial=store.footprint();
    if (updating) {
      std::optional<Lmdb::Read> old(store.read());std::size_t first{},published{},witness{};
      interval=writes(o,hardware,write_faults,[&](auto input,auto target) {
        lmdb_update(store,corpus,input,std::span{objects}.subspan(first,input.size()),target,costs);first+=input.size();
        if (o.retention_age && ++published%o.retention_age==0) {
          const auto object=witness_objects[witness];const auto& image=corpus.images[object];
          witnesses[witness]=read_operation(corpus.bind(object,*old->part(image.address),image.shape.parts[0].interpretation),witness_operations[witness]);++witness;
          old.reset();old.emplace(store.read());
        }
      },trace.operations,output);
      lmdb_held=store.footprint();
      {
        auto current=store.read();
        for (std::size_t object=0;object<o.objects;++object) {
          const auto& image=corpus.images[object];
          verify(corpus.bind(object,*old->part(image.address),image.shape.parts[0].interpretation),o.retention_age?last_old_expected[object]:original[object]);
          verify(corpus.bind(object,*current.part(image.address),image.shape.parts[0].interpretation),expected[object]);
        }
      }
      release_hardware.start();
      const auto process_cpu=process_cpu_ns(),cpu=caller_cpu_ns(),start=clock_ns();old.reset();
      release.wall_ns=clock_ns()-start;release.caller_cpu_ns=caller_cpu_ns()-cpu;
      release.process_cpu_ns=process_cpu_ns()-process_cpu;release_hardware.stop();
      lmdb_released=store.footprint();
    } else {
      const auto read=[&](bool timed_latency,std::uint64_t& count) {
        if (o.backend=="row-lmdb") return run_row_lmdb_reads(store,timed_latency,trace.operations,output,o.batch);
        if (o.codec=="consume16") return run_lmdb_consume_reads(store,corpus,timed_latency,trace.operations,objects,output,o.batch,count);
        return o.codec=="direct16"?
            run_lmdb_codec_reads(store,corpus,timed_latency,trace.operations,objects,output,o.batch,count):
            run_lmdb_reads(store,corpus,timed_latency,trace.operations,objects,output,o.batch,count);
      };
      if (o.conditioning=="warm-trace") {
        std::uint64_t ignored{};conditioning=read(false,ignored);
        require(output==expected_output,"warm trace differs from independent oracle");
      }
      active_hardware=o.pmu==orbital_bench::pmu::Mode::off?nullptr:&hardware;
      interval=read(latency,bindings);active_hardware=nullptr;
      lmdb_held=lmdb_released=store.footprint();
    }
  } else if (o.backend.starts_with("candidate")) {
    const auto run=[&]<class Store>() {
      Candidate<Store> store(corpus,o.bytes,o.metadata_bytes);orbital_initial=footprint(store);
      if (updating) {
        std::optional<typename Store::Scope> old(std::in_place,store.catalog());
        std::size_t first{},published{},witness{};
        interval=writes(o,hardware,write_faults,[&](auto input,auto target) {
          store.update(input,target,std::span{objects}.subspan(first,input.size()));first+=input.size();
          if (o.retention_age && ++published%o.retention_age==0) {
            witnesses[witness]=read_operation(store.body(*old,witness_objects[witness]),witness_operations[witness]);++witness;
            old.reset();old.emplace(store.catalog());store.drain(128);store.poll(128);
          }
        },trace.operations,output);
        orbital_held=footprint(store);
        {
          typename Store::Scope current(store.catalog());
          for (std::size_t object=0;object<o.objects;++object) {
            verify(store.body(*old,object),o.retention_age?last_old_expected[object]:original[object]);
            verify(store.body(current,object),expected[object]);
          }
        }
        release_hardware.start();
        const auto process_cpu=process_cpu_ns(),cpu=caller_cpu_ns(),start=clock_ns();
        old.reset();drain_steps=store.drain(SIZE_MAX);store.poll(65536);
        release.wall_ns=clock_ns()-start;release.caller_cpu_ns=caller_cpu_ns()-cpu;
        release.process_cpu_ns=process_cpu_ns()-process_cpu;release_hardware.stop();
        orbital_released=footprint(store);costs=store.costs;return;
      }
      const auto read=[&](bool timed_latency,std::uint64_t& count) {
        return o.codec=="consume16"?
            candidate_reads<Store,true>(store,timed_latency,trace.operations,objects,output,o.batch,count):
            candidate_reads<Store,false>(store,timed_latency,trace.operations,objects,output,o.batch,count);
      };
      if (o.conditioning=="warm-trace") {
        std::uint64_t ignored{};conditioning=read(false,ignored);
        require(output==expected_output,"candidate warm trace differs from independent oracle");
      }
      active_hardware=o.pmu==orbital_bench::pmu::Mode::off?nullptr:&hardware;
      interval=read(latency,bindings);active_hardware=nullptr;
      orbital_held=orbital_released=footprint(store);costs=store.costs;
    };
    if (o.backend=="candidate8") run.template operator()<compact_history::Store8>();
    else if (o.backend=="candidate16") run.template operator()<compact_history::Store16>();
    else run.template operator()<compact_history::Store32>();
  } else {
    active_hardware=o.pmu==orbital_bench::pmu::Mode::off?nullptr:&hardware;
    interval=run_direct_reads(corpus,latency,trace.operations,objects,output,o.batch);active_hardware=nullptr;
    interval.snapshot_scopes=0; // Qualified bytes control acquires no snapshot.
  }
  require(output==expected_output,"timed outcomes differ from independent oracle");
  require(witnesses==expected_witnesses,"timed first discovery through retained cut differs from independent oracle");
  if (!o.samples.empty()) {
    std::ofstream samples(o.samples);
    if (!samples) throw std::runtime_error("cannot create latency sample output");
    samples<<"batch,requests,ns\n";
    for (std::size_t i=0;i<interval.batch_ns.size();++i)
      samples<<i<<','<<std::min(o.batch,o.ops-i*o.batch)<<','<<interval.batch_ns[i]<<'\n';
    samples.close();
    if (!samples) throw std::runtime_error("cannot finish latency sample output");
  }
  std::uint64_t digest{};for (const auto& outcome:output) {detail::mix(digest,outcome.digest);detail::mix(digest,outcome.changes);}
  std::cout<<"{\"passed\":true,\"report_schema\":2,\"backend\":\""<<o.backend<<"\",\"workload\":\""<<o.workload
      <<"\",\"mode\":\""<<o.mode<<"\",\"rows\":"<<o.rows<<",\"objects\":"<<o.objects
      <<",\"codec\":\""<<o.codec<<"\",\"conditioning\":\""<<o.conditioning<<"\""
      <<",\"sample_storage\":\""<<o.sample_storage<<"\",\"reserved_sample_bytes\":"<<interval.batch_ns.capacity()*sizeof(std::uint64_t)
      <<",\"batch\":"<<o.batch<<",\"operations\":"<<o.ops<<",\"seed\":"<<o.seed<<",\"byte_limit\":"<<o.bytes
      <<",\"offered_metadata_partition\":"<<o.metadata_bytes<<",\"retention_age_publications\":"<<o.retention_age
      <<",\"publication_groups\":"<<(updating?groups:0)
      <<",\"retained_reader_rotations\":"<<(updating?rotations:0)
      <<",\"final_retained_age_publications\":"<<(updating?(o.retention_age?groups%o.retention_age:groups):0)
      <<",\"timed_retained_first_discoveries\":"<<witnesses.size()
      <<",\"object_pattern\":\"round-robin\",\"quantile_rule\":\"nearest-rank\",\"from\":\""<<o.from<<"\",\"trace_fingerprint\":"<<trace.fingerprint
      <<",\"outcome_digest\":"<<digest<<",\"resolved_bindings\":"<<bindings<<",\"interval\":";
  print_interval(interval);std::cout<<",\"costs\":";print_costs(costs);
  std::cout<<",\"write_wrapper_faults\":";
  if (write_faults) std::cout<<"{\"minor\":"<<write_faults->minor<<",\"major\":"<<write_faults->major
      <<",\"scope\":\"write wrapper including sample preparation; fixture and validation excluded\"}";
  else std::cout<<"null";
  std::cout<<",\"hardware\":";hardware.write_json(std::cout);
  std::cout<<",\"release_hardware\":";release_hardware.write_json(std::cout);
  std::cout<<",\"hardware_scope\":\"complete trial loop and boundary clock reads; setup, qualification before direct control, conditioning and post-clock logical accounting excluded\""
      <<",\"metadata_allocation_counts_available\":"<<(o.backend.starts_with("candidate")?"true":"false");
  std::cout<<",\"cost_counter_scope\":\"clone/update/conversion native kernels only; read decode counters unavailable\""
      <<",\"maintenance_policy\":\""<<(o.backend.starts_with("candidate")?"full catalog drain plus bounded physical poll":o.backend=="orbital"?"bounded drain plus bounded physical poll":"transaction release")<<"\"";
  rusage usage{};
  if (getrusage(RUSAGE_SELF,&usage)) throw std::runtime_error("process resource receipt failed");
  const auto memory=process_memory();
  std::cout<<",\"process_resources\":{\"peak_rss_kib\":"<<memory.hwm_kib<<",\"current_rss_kib\":"<<memory.rss_kib
      <<",\"rusage_inherited_peak_rss_kib\":"<<usage.ru_maxrss
      <<",\"minor_faults\":"<<usage.ru_minflt<<",\"major_faults\":"<<usage.ru_majflt
      <<",\"voluntary_switches\":"<<usage.ru_nvcsw<<",\"involuntary_switches\":"<<usage.ru_nivcsw
      <<",\"memory_source\":\"/proc/self/status VmHWM/VmRSS; rusage high-water may be inherited from launcher\""
      <<",\"scope\":\"whole process including fixture, oracle, traces and validation\"}";
  std::cout<<",\"conditioning_interval\":";print_interval(conditioning);
  std::cout<<",\"old_reader_held\":"<<(updating || conversion?"true":"false")<<",\"release_and_drain\":";
  print_interval(release);std::cout<<",\"drain_steps\":"<<drain_steps<<",\"physical_poll_budget\":"
      <<((updating || conversion) && (o.backend=="orbital" || o.backend.starts_with("candidate"))?65536:0);
  if (o.backend=="orbital" || o.backend=="borrowed" || o.backend.starts_with("candidate")) {
    std::cout<<",\"footprint_initial\":";print_footprint(orbital_initial);
    std::cout<<",\"footprint_after_interval\":";print_footprint(orbital_held);
    std::cout<<",\"footprint_after_reader_release\":";print_footprint(orbital_released);
  } else if (o.backend=="lmdb" || o.backend=="row-lmdb") {
    std::cout<<",\"footprint_initial\":";print_footprint(lmdb_initial);
    std::cout<<",\"footprint_after_interval\":";print_footprint(lmdb_held);
    std::cout<<",\"footprint_after_reader_release\":";print_footprint(lmdb_released);
  }
  std::cout<<",\"comparison\":\""<<(conversion?"actual-conversion":o.backend=="row-lmdb"?"contextual-row-key":o.backend=="direct"?"prequalified-native":"common-tuple")
      <<"\",\"update_preparation\":{\"source_bindings\":"
      <<(updating?costs.copied_spans*(o.backend=="orbital"?2:1):0)
      <<",\"dynamic_shape_preparations\":"<<(updating && o.backend=="orbital"?costs.copied_spans:0)
      <<",\"accounting\":\"logical counts of current adapters; no isolated publication ratio\"}"
      <<",\"scope\":\"single-caller resident fixed-membership; adapter-inclusive; durability flushes disabled\"}\n";
  return 0;
} catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}

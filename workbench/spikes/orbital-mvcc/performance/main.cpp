#include "run.h"
#include "oracle.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iostream>

using namespace orbital_versioned_performance;
using namespace orbital_bench;

namespace {
struct Options {
  std::string backend{"orbital"}, workload{"point-hit"}, mode{"throughput"}, from{"rows"}, db;
  std::size_t rows{4096}, objects{1}, batch{1}, ops{}, bytes{256*1024*1024};
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
      std::cout<<"--backend orbital|borrowed|lmdb|direct|row-lmdb --workload point-hit|point-miss|dependent-16|scan-16|point-update|convert\n"
          "--mode throughput|latency --rows N --objects 1..8 --batch 1|64 --ops N --seed N --bytes N --db NEW_DIRECTORY --from rows|columns\n";
      std::exit(0);
    }
    if (++i==argc) throw std::invalid_argument("missing option value");
    const std::string_view value=argv[i];
    if (key=="--backend") result.backend=value;
    else if (key=="--workload") result.workload=value;
    else if (key=="--mode") result.mode=value;
    else if (key=="--from") result.from=value;
    else if (key=="--db") result.db=value;
    else if (key=="--rows") result.rows=number(value);
    else if (key=="--objects") result.objects=number(value);
    else if (key=="--batch") result.batch=number(value);
    else if (key=="--ops") {
      result.ops=number(value);
      if (!result.ops) throw std::invalid_argument("operations must be nonzero");
    }
    else if (key=="--seed") result.seed=number(value);
    else if (key=="--bytes") result.bytes=number(value);
    else throw std::invalid_argument("unknown option");
  }
  if (result.backend!="orbital" && result.backend!="borrowed" && result.backend!="lmdb" && result.backend!="direct" && result.backend!="row-lmdb")
    throw std::invalid_argument("unknown backend");
  if (result.mode!="throughput" && result.mode!="latency") throw std::invalid_argument("unknown mode");
  if (result.from!="rows" && result.from!="columns") throw std::invalid_argument("unknown source format");
  if (result.batch!=1 && result.batch!=64) throw std::invalid_argument("batch must be 1 or 64");
  if (result.workload!="point-hit" && result.workload!="point-miss" && result.workload!="dependent-16" && result.workload!="scan-16" && result.workload!="point-update" && result.workload!="convert")
    throw std::invalid_argument("unsupported workload");
  if (result.workload=="point-update" && result.backend!="orbital" && result.backend!="lmdb")
    throw std::invalid_argument("updates require orbital or mapped lmdb");
  if (result.workload=="convert" && (result.backend!="orbital" || result.batch!=1))
    throw std::invalid_argument("conversion requires orbital and batch 1");
  if (result.workload!="convert" && result.from!="rows") throw std::invalid_argument("common read/write comparison uses Tuple rows");
  if (result.backend=="row-lmdb" && result.objects!=1) throw std::invalid_argument("row-key LMDB control requires one Object");
  if ((result.backend=="lmdb" || result.backend=="row-lmdb") && result.db.empty()) throw std::invalid_argument("LMDB requires --db NEW_DIRECTORY");
  if (!result.ops) result.ops=result.workload=="convert"?2*result.objects:result.workload=="point-update"?64:10000;
  if (result.bytes<1024*1024) throw std::invalid_argument("byte limit must be at least 1 MiB");
  return result;
}
void require(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
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
struct OrbitalFootprint { obj::Statistics metadata; std::uint64_t arena_charge{}; };
OrbitalFootprint footprint(const Orbital& store) { return {store.statistics(),store.charged_bytes()}; }
void print_footprint(const OrbitalFootprint& f) {
  const auto& s=f.metadata;
  std::cout<<"{\"arena_charged_bytes\":"<<f.arena_charge<<",\"metadata_bytes\":"<<s.metadata_bytes
      <<",\"metadata_peak\":"<<s.metadata_peak<<",\"metadata_limit\":"<<s.metadata_limit
      <<",\"objects\":"<<s.objects<<",\"versions\":"<<s.versions<<",\"contexts\":"<<s.contexts
      <<",\"retirement_records\":"<<s.retirement_records<<",\"completed_cut\":"<<s.completed_cut<<"}";
}
void print_footprint(const Lmdb::Footprint& f) {
  std::cout<<"{\"map_bytes\":"<<f.map_bytes<<",\"file_used_bytes\":"<<f.file_used_bytes
      <<",\"file_bytes\":"<<f.file_bytes<<",\"data_pages\":"<<f.data_pages<<",\"tree_depth\":"<<f.tree_depth<<"}";
}
template<class Publish>
Interval writes(const Options& o,Publish&& publish,std::span<const Operation> input,std::span<Outcome> output) {
  return o.mode=="latency"?write_trial<true>(publish,input,output,o.batch):write_trial<false>(publish,input,output,o.batch);
}
} // namespace

int main(int argc,char** argv) try {
  const auto o=options(argc,argv);
  Corpus corpus(o.rows,o.objects,o.seed);
  std::vector<Golden> original(o.objects);
  for (std::size_t object=0;object<o.objects;++object)
    for (const auto& row:corpus.images[object].rows) original[object].emplace(row.key,row.record);
  auto expected=original;
  const bool conversion=o.workload=="convert", updating=o.workload=="point-update", latency=o.mode=="latency";
  Trace trace;
  std::vector<std::size_t> objects(o.ops);
  std::vector<Outcome> output(conversion?0:o.ops),expected_output(conversion?0:o.ops);
  if (!conversion) {
    trace=make_trace(o.workload,o.rows/o.objects,o.ops,o.seed+1);
    for (std::size_t i=0;i<o.ops;++i) {
      objects[i]=i%o.objects;
      const auto& op=trace.operations[i];
      if (updating) {
        auto& record=expected[objects[i]].at(op.key);
        record.value=op.record.value;record.tag=op.record.tag;
        expected_output[i].changes=1;detail::mix(expected_output[i].digest,1);
      } else expected_output[i]=oracle(expected[objects[i]],op);
    }
  }
  Interval interval,release;
  data::Costs costs;
  std::uint64_t bindings{},drain_steps{};
  OrbitalFootprint orbital_initial,orbital_held,orbital_released;
  Lmdb::Footprint lmdb_initial,lmdb_held,lmdb_released;
  if (o.backend=="orbital" || o.backend=="borrowed") {
    const auto initial=o.from=="rows"?data::Format::rows:data::Format::columns;
    Orbital store(corpus,o.bytes,initial);
    store.costs={};orbital_initial=footprint(store);
    if (updating || conversion) {
      std::optional<obj::ReadContext> old(store.read());
      std::vector<data::ReadData> old_bodies;old_bodies.reserve(o.objects);
      for (std::size_t object=0;object<o.objects;++object) old_bodies.push_back(store.body(*old,object));
      if (updating) {
        std::size_t first{};
        interval=writes(o,[&](auto input,auto target) {
          store.update(input,target,std::span{objects}.subspan(first,input.size()));first+=input.size();
        },trace.operations,output);
      } else {
        std::vector<data::Format> formats(o.objects,initial);
        if (latency) interval.batch_ns.reserve(o.ops);
        const auto cpu=caller_cpu_ns(),start=clock_ns();
        for (std::size_t i=0;i<o.ops;++i) {
          const auto object=i%o.objects;
          const auto target=formats[object]==data::Format::rows?data::Format::columns:data::Format::rows;
          const auto batch_start=latency?clock_ns():0;
          store.convert(object,target);
          if (latency) interval.batch_ns.push_back(clock_ns()-batch_start);
          formats[object]=target;
        }
        interval.wall_ns=clock_ns()-start;interval.caller_cpu_ns=caller_cpu_ns()-cpu;
        interval.snapshot_scopes=o.ops;interval.logical_requests=o.ops;interval.logical_rows=o.ops*(o.rows/o.objects);
        auto current=store.read();
        for (std::size_t object=0;object<o.objects;++object) {
          const auto body=store.body(current,object,object<o.ops);
          require(body.format()==formats[object],"conversion selected wrong format");verify(body,expected[object]);
        }
      }
      orbital_held=footprint(store);
      for (std::size_t object=0;object<o.objects;++object) verify(old_bodies[object],original[object]);
      if (updating) {
        auto current=store.read();
        for (std::size_t object=0;object<o.objects;++object) verify(store.body(current,object),expected[object]);
      }
      old_bodies.clear();
      const auto cpu=caller_cpu_ns(),start=clock_ns();
      old.reset();drain_steps=store.drain(SIZE_MAX);store.poll(65536);
      release.wall_ns=clock_ns()-start;release.caller_cpu_ns=caller_cpu_ns()-cpu;
      orbital_released=footprint(store);
    } else {
      interval=run_orbital_reads(store,o.backend=="borrowed",latency,trace.operations,objects,output,o.batch,bindings);
      orbital_held=orbital_released=footprint(store);
    }
    costs=store.costs;
  } else if (o.backend=="lmdb" || o.backend=="row-lmdb") {
    Lmdb store(o.db,o.bytes);
    if (o.backend=="row-lmdb") store.load(corpus.images[0].rows);
    else {auto write=store.edit();for (const auto& image:corpus.images) write.put_part(image.address,image.encoded);write.publish();}
    lmdb_initial=store.footprint();
    if (updating) {
      std::optional<Lmdb::Read> old(store.read());std::size_t first{};
      interval=writes(o,[&](auto input,auto target) {
        lmdb_update(store,corpus,input,std::span{objects}.subspan(first,input.size()),target,costs);first+=input.size();
      },trace.operations,output);
      lmdb_held=store.footprint();
      {
        auto current=store.read();
        for (std::size_t object=0;object<o.objects;++object) {
          const auto& image=corpus.images[object];
          verify(corpus.bind(object,*old->part(image.address),image.shape.parts[0].interpretation),original[object]);
          verify(corpus.bind(object,*current.part(image.address),image.shape.parts[0].interpretation),expected[object]);
        }
      }
      const auto cpu=caller_cpu_ns(),start=clock_ns();old.reset();
      release.wall_ns=clock_ns()-start;release.caller_cpu_ns=caller_cpu_ns()-cpu;
      lmdb_released=store.footprint();
    } else {
      interval=o.backend=="lmdb"?run_lmdb_reads(store,corpus,latency,trace.operations,objects,output,o.batch,bindings):
          run_row_lmdb_reads(store,latency,trace.operations,output,o.batch);
      lmdb_held=lmdb_released=store.footprint();
    }
  } else {
    interval=run_direct_reads(corpus,latency,trace.operations,objects,output,o.batch);
    interval.snapshot_scopes=0; // Qualified bytes control acquires no snapshot.
  }
  require(output==expected_output,"timed outcomes differ from independent oracle");
  std::uint64_t digest{};for (const auto& outcome:output) {detail::mix(digest,outcome.digest);detail::mix(digest,outcome.changes);}
  std::cout<<"{\"passed\":true,\"backend\":\""<<o.backend<<"\",\"workload\":\""<<o.workload
      <<"\",\"mode\":\""<<o.mode<<"\",\"rows\":"<<o.rows<<",\"objects\":"<<o.objects
      <<",\"batch\":"<<o.batch<<",\"operations\":"<<o.ops<<",\"seed\":"<<o.seed<<",\"byte_limit\":"<<o.bytes
      <<",\"object_pattern\":\"round-robin\",\"quantile_rule\":\"nearest-rank\",\"from\":\""<<o.from<<"\",\"trace_fingerprint\":"<<trace.fingerprint
      <<",\"outcome_digest\":"<<digest<<",\"resolved_bindings\":"<<bindings<<",\"interval\":";
  print_interval(interval);std::cout<<",\"costs\":";print_costs(costs);
  std::cout<<",\"old_reader_held\":"<<(updating || conversion?"true":"false")<<",\"release_and_drain\":";
  print_interval(release);std::cout<<",\"drain_steps\":"<<drain_steps<<",\"physical_poll_budget\":"
      <<((updating || conversion) && o.backend=="orbital"?65536:0);
  if (o.backend=="orbital" || o.backend=="borrowed") {
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

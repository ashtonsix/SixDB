#include "encoded.h"
#include "oracle.h"
#include "trial.h"
#include "../callers/object_ring.h"
#include "../metadata/orbital/candidates/common.h"
#include <charconv>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <thread>

namespace p = orbital_versioned_performance;
namespace lr = loom_object_ring;
namespace cs = compact_shape;
namespace oa = orbital::allocation;
namespace sr = orbital_mvcc::metadata_consumer::shape_read;
constexpr orbital::ProgramId program{0x72696e67, 0x737572766579};
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Options {
  std::size_t rows=32768, objects=1, ops=65539, width=8, group=1024;
  unsigned steps=16;
  bool hints=false, latency=false;
  lr::Execution execution=lr::Execution::ring;
  std::string name="ring", samples;
};
std::size_t number(std::string_view s) {
  std::size_t n{};const auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),n);
  require(error==std::errc{} && end==s.data()+s.size(),"invalid number");return n;
}
Options options(int argc,char** argv) {
  Options o;
  for (int i=1;i<argc;++i) {
    const std::string_view key=argv[i];require(++i<argc,"missing value");const std::string_view v=argv[i];
    if(key=="--rows")o.rows=number(v);else if(key=="--objects")o.objects=number(v);
    else if(key=="--ops")o.ops=number(v);else if(key=="--width")o.width=number(v);
    else if(key=="--group")o.group=number(v);else if(key=="--steps")o.steps=number(v);
    else if(key=="--hints") {require(v=="0"||v=="1","invalid hints");o.hints=v=="1";}
    else if(key=="--latency-samples")o.samples=v;
    else if(key=="--mode") {require(v=="throughput"||v=="latency","invalid mode");o.latency=v=="latency";}
    else if(key=="--execution") {
      require(v=="direct"||v=="serial"||v=="manual"||v=="ring","invalid execution");o.name=v;
      o.execution=v=="serial"?lr::Execution::serial:v=="manual"?lr::Execution::manual:lr::Execution::ring;
    } else throw std::invalid_argument("unknown option");
  }
  require(o.name!="direct" || (!o.hints && o.width==1),"direct proven control has no hint or width treatment");
  require(o.samples.empty() || (o.latency && !std::filesystem::exists(o.samples)),"samples require latency and new file");
  require(o.ops && o.group && o.width>=1 && o.width<=64 && o.steps>=1 && o.steps<=16,"invalid extents");
  return o;
}
struct Worker {
  orbital::Ref<oa::Arena> arena=p::need(oa::open_arena(1024ULL*1024*1024));
  loom::Worker worker;
  Worker() {p::need(loom::adopt(worker));p::need(loom::configure(worker,arena,0));}
  ~Worker() {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(;;) {
      const auto s=loom::shutdown(worker);
      if(s.kind()==orbital::Progress::Kind::done)break;
      if(s.kind()==orbital::Progress::Kind::failed || std::chrono::steady_clock::now()>=until)std::terminate();
      loom::poll(worker,{.messages=64,.tasks=0});std::this_thread::yield();
    }
    if(arena->charged_bytes.load()!=0 || worker.memory || orbital::worker::pending(worker.lane))std::terminate();
    loom::relinquish(worker);
  }
};
struct Fixture {
  p::Corpus corpus;
  compact_version_probe::Memory memory;
  Worker worker;
  std::unique_ptr<lr::Catalog> store;
  cs::Shape* shape{};
  std::optional<sr::TupleProof> proof;
  Fixture(std::size_t rows,std::size_t objects):corpus(rows,objects,8132),store(std::make_unique<lr::Catalog>(memory)) {
    memory.limit=64*1024*1024;
    const auto& image=corpus.images.front();const auto& part=image.shape.parts[0];
    const std::array<compact_descriptor::PartSource,1> descriptions{{{part.id,image.encoded,
        part.interpretation,part.readable_padding,part.alignment,{}}}};
    shape=p::need(cs::prove_fixture(memory,corpus.schema,program,image.shape.interpretation,descriptions));
    proof=p::need(sr::admit_tuple_shape(shape,program,part.id));
    lr::Catalog::Prepared packet(*store);
    {
      lr::Catalog::Scope basis(*store);std::vector<lr::Catalog::Change> changes;changes.reserve(objects);
      for(const auto& im:corpus.images) {
        const auto& layout=im.shape.parts[0];
        auto allocation=p::need(loom::allocate(worker.worker,{.bytes=layout.body_bytes,
            .alignment=layout.alignment,.readable_padding=layout.readable_padding}));
        const auto bytes=loom::bytes(allocation);std::memcpy(bytes.data(),im.encoded.data(),im.encoded.size());
        const std::array<orbital::PartSource,1> sources{{{layout.id,bytes,{},&allocation}}};
        auto owner=p::need(store->capture({shape,im.rows.size(),orbital::PartOwners::independent_allocation},sources));
        require(!allocation,"capture did not transfer allocation");changes.push_back({im.address.object,std::move(owner)});
      }
      p::need(store->prepare(basis,changes,packet));
    }
    p::need(store->install(packet));store->drain();
  }
  ~Fixture() {store.reset();cs::release(shape);if(memory.live)std::terminate();}
};
int main(int argc,char** argv) try {
  const auto o=options(argc,argv);const auto setup_start=p::clock_ns();Fixture f(o.rows,o.objects);
  std::vector<p::Golden> golden(o.objects);
  for(std::size_t i=0;i<o.objects;++i)for(const auto& row:f.corpus.images[i].rows)golden[i].emplace(row.key,row.record);
  auto trace=orbital_bench::make_trace(o.steps==1?"point-hit":"dependent-16",o.rows/o.objects,o.ops,8133);
  std::vector<lr::Request> input;input.reserve(o.ops);std::vector<lr::Outcome> output(o.ops),expected(o.ops);
  for(std::size_t i=0;i<o.ops;++i) {
    const auto object=i%o.objects;auto op=trace.operations[i];op.count=o.steps;
    input.push_back({f.corpus.images[object].address.object,op.key,o.steps});expected[i]=p::oracle(golden[object],op);
  }
  std::unique_ptr<lr::Driver> driver;const auto before=f.worker.arena->charged_bytes.load();
  const auto configure_start=p::clock_ns();if(o.name!="direct") {driver=std::make_unique<lr::Driver>(f.worker.worker,*f.proof);p::need(driver->configure(o.width));}const auto configure_ns=p::clock_ns()-configure_start;
  const auto ring_charge=f.worker.arena->charged_bytes.load()-before;const auto setup_ns=p::clock_ns()-setup_start;
  const auto execute=[&](bool latency) {
    p::Interval interval;lr::Statistics total;
    interval.batch_ns.resize((o.ops+o.group-1)/o.group);interval.batch_ns.clear();
    const auto cpu=p::caller_cpu_ns(),process=p::process_cpu_ns(),start=p::clock_ns();
    for(std::size_t first=0;first<o.ops;first+=o.group) {
      const auto n=std::min(o.group,o.ops-first);const auto group_start=latency?p::clock_ns():0;
      lr::Statistics stats;
      if(driver) stats=p::need(driver->run(*f.store,std::span(input).subspan(first,n),std::span(output).subspan(first,n),o.execution,o.hints));
      else for(std::size_t i=first;i<first+n;++i) {
        lr::Catalog::Scope scope(*f.store);const auto selected=scope.bind(input[i].object);
        p::need(sr::read_proven(*f.proof,selected,input[i].key,input[i].steps,output[i]));++stats.completed;
      }
      if(latency)interval.batch_ns.push_back(p::clock_ns()-group_start);
      total.completed+=stats.completed;total.replacement_admissions+=stats.replacement_admissions;
      total.returned_quanta+=stats.returned_quanta;total.ring_visits+=stats.ring_visits;
    }
    interval.wall_ns=p::clock_ns()-start;interval.caller_cpu_ns=p::caller_cpu_ns()-cpu;
    interval.process_cpu_ns=p::process_cpu_ns()-process;interval.snapshot_scopes=total.completed;
    return std::pair{std::move(interval),total};
  };
  execute(false);require(output==expected,"warm output differs from independent map");
  const auto [interval,stats]=execute(o.latency);require(output==expected,"timed output differs from independent map");
  require(stats.completed==o.ops,"request Scope leaked");
  std::uint64_t digest{},rows{};for(const auto& v:output){orbital_bench::detail::mix(digest,v.digest);orbital_bench::detail::mix(digest,v.changes);rows+=v.rows;}
  auto samples=interval.batch_ns;std::sort(samples.begin(),samples.end());
  const auto q=[&](std::size_t rank){return samples.empty()?0:samples[(samples.size()*rank+999)/1000-1];};
  if(driver)p::need(driver->close());
  if(!o.samples.empty()) {
    std::ofstream out(o.samples);require(bool(out),"cannot write group latency samples");out<<"group,requests,ns\n";
    for(std::size_t i=0;i<interval.batch_ns.size();++i)out<<i<<','<<std::min(o.group,o.ops-i*o.group)<<','<<interval.batch_ns[i]<<'\n';
  }
  std::cout<<"{\"passed\":true,\"rows\":"<<o.rows<<",\"objects\":"<<o.objects<<",\"operations\":"<<o.ops
      <<",\"steps\":"<<o.steps<<",\"execution\":\""<<o.name<<"\",\"width\":"<<o.width<<",\"hints\":"<<(o.hints?"true":"false")
      <<",\"mode\":\""<<(o.latency?"latency":"throughput")<<"\",\"group\":"<<o.group
      <<",\"wall_ns\":"<<interval.wall_ns<<",\"caller_cpu_ns\":"<<interval.caller_cpu_ns<<",\"process_cpu_ns\":"<<interval.process_cpu_ns
      <<",\"completed\":"<<stats.completed<<",\"logical_rows\":"<<rows<<",\"digest\":"<<digest
      <<",\"replacement_admissions\":"<<stats.replacement_admissions<<",\"returned_quanta\":"<<stats.returned_quanta<<",\"ring_visits\":"<<stats.ring_visits
      <<",\"driver_record_bytes\":"<<(driver?sizeof(lr::Driver):0)<<",\"frame_record_bytes\":"<<sizeof(lr::Frame)<<",\"fixed_frames\":"<<(driver?64:0)
      <<",\"encoded_body_bytes\":"<<o.rows*p::data::tuple_bytes<<",\"metadata_live\":"<<f.memory.live<<",\"metadata_peak\":"<<f.memory.peak
      <<",\"configure_ns\":"<<configure_ns<<",\"ring_arena_charge\":"<<ring_charge<<",\"setup_ns\":"<<setup_ns
      <<",\"batch_samples\":"<<samples.size()<<",\"batch_p50_ns\":"<<q(500)<<",\"batch_p99_ns\":"<<q(990)
      <<",\"latency_scope\":\"complete offered group, not request p99\",\"retirement_records\":null}\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}

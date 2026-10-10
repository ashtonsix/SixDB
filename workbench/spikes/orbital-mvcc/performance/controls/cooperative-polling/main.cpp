#include "../../encoded.h"
#include "../../oracle.h"
#include "../../trial.h"
#include "../../../callers/object_ring.h"
#include "../../../callers/object_messages.h"
#include "../../../metadata/orbital/candidates/common.h"
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
  std::size_t rows=8388608, objects=256, ops=524291, group=1024, quantum=128;
  bool diagnostic=false, refusal_check=false, verify_only=false;
  lr::Execution execution=lr::Execution::ring;
  lr::CompletionBurst::Mode treatment=lr::CompletionBurst::Mode::per_quantum;
  std::string execution_name="ring", treatment_name="loaded", samples;
};
std::size_t number(std::string_view s) {
  std::size_t n{}; const auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),n);
  require(error==std::errc{} && end==s.data()+s.size(),"invalid number"); return n;
}
Options options(int argc,char** argv) {
  Options o;
  for (int i=1;i<argc;++i) {
    const std::string_view key=argv[i]; require(++i<argc,"missing value"); const std::string_view v=argv[i];
    if(key=="--rows")o.rows=number(v); else if(key=="--objects")o.objects=number(v);
    else if(key=="--ops")o.ops=number(v); else if(key=="--group")o.group=number(v);
    else if(key=="--quantum")o.quantum=number(v);
    else if(key=="--samples")o.samples=v;
    else if(key=="--verify-only") {require(v=="0"||v=="1","invalid verify-only");o.verify_only=v=="1";}
    else if(key=="--refusal-check") {require(v=="0"||v=="1","invalid refusal check");o.refusal_check=v=="1";}
    else if(key=="--mode") {require(v=="throughput"||v=="delay","invalid mode");o.diagnostic=v=="delay";}
    else if(key=="--execution") {
      require(v=="manual"||v=="ring","invalid execution");o.execution_name=v;
      o.execution=v=="manual"?lr::Execution::manual:lr::Execution::ring;
    } else if(key=="--treatment") {
      require(v=="unloaded"||v=="empty"||v=="loaded"||v=="deferred","invalid treatment");o.treatment_name=v;
      o.treatment=v=="unloaded"?lr::CompletionBurst::Mode::unloaded:v=="empty"?lr::CompletionBurst::Mode::empty_poll:
          v=="loaded"?lr::CompletionBurst::Mode::per_quantum:lr::CompletionBurst::Mode::deferred;
    } else throw std::invalid_argument("unknown option");
  }
  require(o.rows && o.objects && o.rows%o.objects==0 && o.ops && o.group &&
      (o.quantum==32||o.quantum==128),"invalid extents");
  require(o.samples.empty() || !std::filesystem::exists(o.samples),"samples must use a new file");
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
struct Sample {
  std::uint64_t read_ns{}, complete_ns{}, caller_cpu_ns{}, process_cpu_ns{};
  std::size_t quanta{}, polls{}, messages_at_read{};
};
struct Run {
  std::vector<Sample> groups;
  std::vector<std::array<std::uint64_t,lr::CompletionBurst::count>> delays;
  std::uint64_t read_ns{}, complete_ns{}, caller_cpu_ns{}, process_cpu_ns{}, quanta{}, polls{}, handled{};
};
int main(int argc,char** argv) try {
  const auto o=options(argc,argv); Fixture f(o.rows,o.objects);
  std::vector<p::Golden> golden(o.objects);
  for(std::size_t i=0;i<o.objects;++i)for(const auto& row:f.corpus.images[i].rows)golden[i].emplace(row.key,row.record);
  const auto trace=orbital_bench::make_trace("dependent-16",o.rows/o.objects,o.ops,8133);
  std::vector<lr::Request> input;input.reserve(o.ops);
  std::vector<lr::Outcome> output(o.ops),expected(o.ops);
  for(std::size_t i=0;i<o.ops;++i) {
    const auto object=i%o.objects; const auto op=trace.operations[i];
    input.push_back({f.corpus.images[object].address.object,op.key,16});expected[i]=p::oracle(golden[object],op);
  }
  const auto before=f.worker.arena->charged_bytes.load();
  lr::Driver driver(f.worker.worker,*f.proof);p::need(driver.configure(32));
  const auto ring_charge=f.worker.arena->charged_bytes.load()-before;
  lr::CompletionBurst burst(f.worker.worker,o.treatment);
  auto* const service=burst.polls_during_reads()?&burst:nullptr;
  if(o.refusal_check) {
    lr::CompletionBurst refusal_burst(f.worker.worker,lr::CompletionBurst::Mode::per_quantum);
    std::array<lr::Request,33> refused;std::array<lr::Outcome,33> discarded{};
    refused.fill(input.front());refused.back().object={UINT64_MAX,UINT64_MAX};
    refusal_burst.post(0,false);
    const auto result=driver.run(*f.store,refused,discarded,o.execution,true,o.quantum,&refusal_burst);
    require(!result && result.error()==orbital::Error::absent,"absent replacement did not abort");
    refusal_burst.drain();
    require(refusal_burst.correct(refusal_burst.boundaries()),"refusal lost a Message or owner");
    require(refusal_burst.boundaries()>0,"refusal did not follow resident native work");
    p::need(driver.close());p::need(driver.configure(32));
  }
  const auto execute=[&](bool diagnostic) {
    Run run;
    const auto groups=(o.ops+o.group-1)/o.group;
    run.groups.resize(groups);run.delays.resize(groups); // identical cold sample preparation
    for(std::size_t first=0,g=0;first<o.ops;first+=o.group,++g) {
      const auto n=std::min(o.group,o.ops-first);
      auto& sample=run.groups[g];
      const auto cpu=o.verify_only?0:p::caller_cpu_ns(),process=o.verify_only?0:p::process_cpu_ns(),
          start=o.verify_only?0:p::clock_ns();
      burst.post(g,diagnostic);
      const auto stats=p::need(driver.run(*f.store,std::span(input).subspan(first,n),
          std::span(output).subspan(first,n),o.execution,true,o.quantum,service));
      sample.read_ns=o.verify_only?0:p::clock_ns()-start;
      sample.messages_at_read=burst.handled();
      burst.drain(); // charged even in the deferred control and partial final group
      sample.complete_ns=o.verify_only?0:p::clock_ns()-start;
      sample.caller_cpu_ns=o.verify_only?0:p::caller_cpu_ns()-cpu;
      sample.process_cpu_ns=o.verify_only?0:p::process_cpu_ns()-process;
      sample.quanta=stats.returned_quanta;sample.polls=burst.polls();
      // Verification/sample collection occurs after this group's measured work.
      require(stats.completed==n && burst.correct(stats.returned_quanta),"read count or Message oracle failed");
      if(diagnostic && burst.loaded())for(std::size_t i=0;i<lr::CompletionBurst::count;++i) {
        const auto& record=burst.records()[i];run.delays[g][i]=record.completed_ns-record.posted_ns;
      }
      run.read_ns+=sample.read_ns;run.complete_ns+=sample.complete_ns;
      run.caller_cpu_ns+=sample.caller_cpu_ns;run.process_cpu_ns+=sample.process_cpu_ns;
      run.quanta+=sample.quanta;run.polls+=sample.polls;run.handled+=burst.handled();
    }
    require(output==expected,"unchanged reads differ from independent map oracle");
    return run;
  };
  execute(false);
  const auto run=execute(o.diagnostic);
  std::uint64_t digest{},rows{};
  for(const auto& v:output){orbital_bench::detail::mix(digest,v.digest);rows+=v.rows;}
  std::vector<std::uint64_t> delays;
  if(o.diagnostic && burst.loaded())for(const auto& group:run.delays)for(auto delay:group)delays.push_back(delay);
  std::sort(delays.begin(),delays.end());
  const auto percentile=[&](std::size_t rank){return delays.empty()?0:delays[(delays.size()*rank+999)/1000-1];};
  if(!o.samples.empty()) {
    std::ofstream out(o.samples);require(bool(out),"cannot write samples");
    out<<"group,requests,read_ns,complete_ns,messages_at_read,polls,quanta";
    for(std::size_t i=0;i<lr::CompletionBurst::count;++i)out<<",completion_delay_"<<i<<"_ns";
    out<<'\n';
    for(std::size_t g=0;g<run.groups.size();++g) {
      const auto& s=run.groups[g];out<<g<<','<<std::min(o.group,o.ops-g*o.group)<<','<<s.read_ns<<','<<s.complete_ns
          <<','<<s.messages_at_read<<','<<s.polls<<','<<s.quanta;
      for(auto delay:run.delays[g])out<<','<<delay;out<<'\n';
    }
  }
  p::need(driver.close());
  std::cout<<"{\"passed\":true,\"rows\":"<<o.rows<<",\"objects\":"<<o.objects<<",\"operations\":"<<o.ops
      <<",\"execution\":\""<<o.execution_name<<"\",\"treatment\":\""<<o.treatment_name<<"\",\"mode\":\""<<(o.diagnostic?"delay":"throughput")
      <<"\",\"width\":32,\"hints\":true,\"steps\":16,\"quantum\":"<<o.quantum<<",\"group\":"<<o.group
      <<",\"read_complete_ns\":"<<run.read_ns<<",\"all_work_complete_ns\":"<<run.complete_ns
      <<",\"caller_cpu_ns\":"<<run.caller_cpu_ns<<",\"process_cpu_ns\":"<<run.process_cpu_ns
      <<",\"returned_quanta\":"<<run.quanta<<",\"poll_calls\":"<<run.polls<<",\"messages\":"<<run.handled
      <<",\"logical_rows\":"<<rows<<",\"digest\":"<<digest<<",\"groups\":"<<run.groups.size()
      <<",\"final_group_requests\":"<<o.ops-(run.groups.size()-1)*o.group
      <<",\"driver_record_bytes\":"<<sizeof(lr::Driver)<<",\"message_record_bytes\":"<<sizeof(lr::CompletionBurst::Record)
      <<",\"completion_control_bytes\":"<<sizeof(lr::CompletionBurst)<<",\"message_ops_static_bytes\":"<<lr::CompletionBurst::operations_bytes()
      <<",\"group_sample_bytes\":"<<run.groups.size()*sizeof(Sample)<<",\"delay_storage_bytes\":"<<run.delays.size()*sizeof(run.delays[0])
      <<",\"ring_arena_charge\":"<<ring_charge<<",\"completion_delay_samples\":"<<delays.size()
      <<",\"completion_delay_p50_ns\":"<<percentile(500)<<",\"completion_delay_p99_ns\":"<<percentile(990)
      <<",\"completion_delay_max_ns\":"<<(delays.empty()?0:delays.back())
      <<",\"verify_only\":"<<(o.verify_only?"true":"false")
      <<",\"refusal_checked\":"<<(o.refusal_check?"true":"false")
      <<",\"arrival_model\":\"eight owner-posted ready Messages per group\",\"timing_scope\":\"sum of group work intervals; posting, reads, service and final drain; excludes oracle checks\"}\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}

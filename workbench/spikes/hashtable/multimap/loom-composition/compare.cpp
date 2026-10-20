#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "caller.cpp"
#pragma clang diagnostic pop
#include "context.inc"
#include "companion.inc"
#include "context_bulk.inc"
#include <optional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <absl/container/internal/hashtable_debug.h>

namespace staged_native {
using independent::Mode;
using independent::word;
using independent::lanes;
using independent::now_ns;
using study::result;
namespace pmu=workbench::pmu;
static_assert(study::V==0 || study::V==8 || study::V==64);
inline constexpr bool extra_policy=study::V==8&&!study::Multi;
std::vector<const char*> implementations() {
  if constexpr(extra_policy)return {"packed","whole","maintained","abseil","packed_lookahead8","whole_lookahead8","maintained_lookahead8"};
  return {"packed","whole","maintained","abseil"};
}
struct OwnerFacts {
  word elapsed_ns{};
  std::size_t bytes{},capacity{},spill{},fixture_bytes{},hardware_line_bytes{};
  bool functional_only{};
};

enum class Arm {direct,prefetch,typed_l1,typed_l2,common_l1,common_l2,
  sequential_l1,sequential_l2,shared_l1,shared_l2,bulk16_l1,bulk16_l2,bulk32_l1,bulk32_l2,bulk_l1,bulk_l2,
  prefetch_typed_l1,prefetch_typed_l2,context_l1,context_l2,staged_context_l1,staged_context_l2,context_bulk_l1,context_bulk_l2,context_sequential_l1,context_sequential_l2};
const char* label(Arm arm) {
  switch(arm) {
    case Arm::direct:return "direct";case Arm::prefetch:return "prefetch32";
    case Arm::typed_l1:return "staged_l1";case Arm::typed_l2:return "staged_l2";
    case Arm::common_l1:return "common_l1";case Arm::common_l2:return "common_l2";
    case Arm::sequential_l1:return "staged_sequential_l1";case Arm::sequential_l2:return "staged_sequential_l2";
    case Arm::shared_l1:return "staged_common_l1";case Arm::shared_l2:return "staged_common_l2";
    case Arm::bulk16_l1:return "stream_q16";case Arm::bulk16_l2:return "stream_q16_chain_l2";
    case Arm::bulk32_l1:return "stream_q32";case Arm::bulk32_l2:return "stream_q32_chain_l2";
    case Arm::bulk_l1:return "stream_q128";case Arm::bulk_l2:return "stream_q128_chain_l2";
    case Arm::prefetch_typed_l1:return "prefetch_staged_l1";case Arm::prefetch_typed_l2:return "prefetch_staged_l2";
    case Arm::context_l1:return "context_l1";case Arm::context_l2:return "context_l2";
    case Arm::staged_context_l1:return "staged_context_l1";case Arm::staged_context_l2:return "staged_context_l2";
    case Arm::context_bulk_l1:return "context_stream_q128";case Arm::context_bulk_l2:return "context_stream_q128_chain_l2";
    case Arm::context_sequential_l1:return "context_sequential_q128";case Arm::context_sequential_l2:return "context_sequential_q128_chain_l2";
  }
  std::abort();
}
unsigned quantum(Arm arm){
  if(arm==Arm::bulk16_l1||arm==Arm::bulk16_l2)return 16;
  if(arm==Arm::bulk32_l1||arm==Arm::bulk32_l2)return 32;
  return arm==Arm::bulk_l1||arm==Arm::bulk_l2||arm==Arm::context_bulk_l1||arm==Arm::context_bulk_l2||
      arm==Arm::context_sequential_l1||arm==Arm::context_sequential_l2?128:0;
}
unsigned hint(Mode mode,Arm arm) {
  if(mode==Mode::independent&&quantum(arm))return 0;
  switch(arm) {
    case Arm::direct:case Arm::prefetch:return 0;
    case Arm::typed_l2:case Arm::common_l2:case Arm::sequential_l2:case Arm::shared_l2:
    case Arm::bulk16_l2:case Arm::bulk32_l2:case Arm::bulk_l2:case Arm::prefetch_typed_l2:
    case Arm::context_l2:case Arm::staged_context_l2:case Arm::context_bulk_l2:case Arm::context_sequential_l2:return 2;
    default:return 1;
  }
}
std::vector<Arm> arms(Mode mode) {
#if WRITE_ARCH_PHASE_MATRIX
  if(mode==Mode::independent)return {Arm::direct,Arm::prefetch,Arm::bulk_l1,Arm::common_l1};
  if(mode==Mode::chains)return {Arm::direct,Arm::prefetch,Arm::typed_l1,Arm::common_l1,Arm::context_l1};
  return {Arm::context_bulk_l1,Arm::context_sequential_l1};
#endif
#if WRITE_ARCH_REDUCTION_MATRIX
  if(mode==Mode::independent)return {Arm::direct,Arm::prefetch,Arm::bulk_l1,Arm::common_l1};
  if(mode==Mode::chains)return {Arm::typed_l1};
  return {Arm::context_bulk_l1,Arm::context_sequential_l1};
#endif
  if(mode==Mode::independent)return {Arm::direct,Arm::prefetch,Arm::typed_l1,Arm::typed_l2,Arm::bulk16_l1,Arm::bulk32_l1,Arm::bulk_l1,Arm::common_l1,Arm::common_l2};
  if(mode==Mode::chains)return {Arm::direct,Arm::prefetch,Arm::typed_l1,Arm::typed_l2,Arm::common_l1,Arm::common_l2,Arm::context_l1,Arm::context_l2};
  return {Arm::direct,Arm::prefetch,Arm::bulk16_l1,Arm::bulk32_l1,Arm::bulk_l1,Arm::bulk16_l2,Arm::bulk32_l2,Arm::bulk_l2,
      Arm::prefetch_typed_l1,Arm::prefetch_typed_l2,Arm::sequential_l1,Arm::sequential_l2,Arm::shared_l1,Arm::shared_l2,
      Arm::staged_context_l1,Arm::staged_context_l2,Arm::context_bulk_l1,Arm::context_bulk_l2,
      Arm::context_sequential_l1,Arm::context_sequential_l2};
}
std::vector<Mode> modes(unsigned miss) {
  if(miss || study::V==0)return {Mode::independent};
  return {Mode::independent,Mode::chains,Mode::mixed};
}
struct Outcome {
  result output{};
  std::array<word,lanes> heads{};
  word independent_done{},chains_done{};
};

template<class Read> class Executor {
  const study::input& input_;
  independent::Executor<Read> original_;
  staged_companion::Shared<Read> shared_;
  staged_companion::Shared<Read,Read,true> shared_context_;
  staged_companion::ContextBulk<Read> context_bulk_;
  staged_companion::Sequential<Read> sequential_;
  template<loom::Hint Hint,bool ContextChains=false> Outcome shared(Mode mode,word start) {
    const std::span<const word> keys(input_.request_keys);
    const auto split=mode==Mode::chains?0:mode==Mode::mixed?keys.size()/2:keys.size();
    auto& bank=[&]() -> auto& {if constexpr(ContextChains)return shared_context_;else return shared_;}();
    bank.template prepare<Hint>(keys.first(split),keys.subspan(split),start);
    while(bank.pending())bank.spin({1024});
    const auto out=bank.collect();
    study::require(!out.independent_pending&&!out.chains_pending,"physically complete staged cohorts");
    return {out.combined,out.heads,mode==Mode::mixed?out.independent_done_ns:0,
        mode==Mode::mixed?out.chains_done_ns:0};
  }
  template<loom::Hint Hint,bool Sequential=false> Outcome context_bulk(word start) {
    const std::span<const word> keys(input_.request_keys);const auto split=keys.size()/2;
    context_bulk_.template prepare<Hint,Sequential>(keys.first(split),keys.subspan(split),start);
    while(context_bulk_.pending())context_bulk_.spin({1024});
    const auto out=context_bulk_.collect();
    study::require(!out.independent_pending&&!out.chains_pending,"physically complete context bulk cohorts");
    return {out.combined,out.heads,out.independent_done_ns,out.chains_done_ns};
  }
  template<loom::Hint Hint> Outcome sequential(word start) {
    const auto out=sequential_.template run<Hint>(input_.requests/2,start);
    return {out.combined,out.heads,out.independent_done_ns,out.chains_done_ns};
  }
public:
  Executor(loom::Worker& worker,Read read,const study::input& in)
      :input_(in),original_(worker,read,in),shared_(worker,read,read),shared_context_(worker,read,read),
       context_bulk_(worker,read),sequential_(worker,read,read,in){}
  Outcome run(Mode mode,Arm arm,word start) {
    if(mode==Mode::chains&&(arm==Arm::context_l1||arm==Arm::context_l2))
      return arm==Arm::context_l1?shared<loom::Hint::l1_1,true>(mode,start):shared<loom::Hint::l2_1,true>(mode,start);
    if(mode==Mode::independent&&(arm==Arm::common_l1||arm==Arm::common_l2))
      return arm==Arm::common_l1?shared<loom::Hint::l1_1>(mode,start):shared<loom::Hint::l2_1>(mode,start);
    if(mode==Mode::mixed) {
      if(arm==Arm::shared_l1)return shared<loom::Hint::l1_1>(mode,start);
      if(arm==Arm::shared_l2)return shared<loom::Hint::l2_1>(mode,start);
      if(arm==Arm::sequential_l1)return sequential<loom::Hint::l1_1>(start);
      if(arm==Arm::sequential_l2)return sequential<loom::Hint::l2_1>(start);
      if(arm==Arm::staged_context_l1)return shared<loom::Hint::l1_1,true>(mode,start);
      if(arm==Arm::staged_context_l2)return shared<loom::Hint::l2_1,true>(mode,start);
      if(arm==Arm::context_bulk_l1)return context_bulk<loom::Hint::l1_1>(start);
      if(arm==Arm::context_bulk_l2)return context_bulk<loom::Hint::l2_1>(start);
      if(arm==Arm::context_sequential_l1)return context_bulk<loom::Hint::l1_1,true>(start);
      if(arm==Arm::context_sequential_l2)return context_bulk<loom::Hint::l2_1,true>(start);
    }
    auto selected=independent::Arm::direct;
    switch(arm) {
      case Arm::direct:break;case Arm::prefetch:selected=independent::Arm::prefetch;break;
      case Arm::typed_l1:selected=independent::Arm::staged_l1;break;
      case Arm::typed_l2:selected=independent::Arm::staged_l2;break;
      case Arm::common_l1:selected=independent::Arm::common_l1;break;
      case Arm::common_l2:selected=independent::Arm::common_l2;break;
      case Arm::bulk16_l1:selected=independent::Arm::stream16;break;
      case Arm::bulk16_l2:selected=independent::Arm::stream16_l2;break;
      case Arm::bulk32_l1:selected=independent::Arm::stream32;break;
      case Arm::bulk32_l2:selected=independent::Arm::stream32_l2;break;
      case Arm::bulk_l1:selected=independent::Arm::stream128;break;
      case Arm::bulk_l2:selected=independent::Arm::stream128_l2;break;
      case Arm::prefetch_typed_l1:selected=independent::Arm::prefetch_staged_l1;break;
      case Arm::prefetch_typed_l2:selected=independent::Arm::prefetch_staged_l2;break;
      default:std::abort();
    }
    original_.start(start);
    Outcome out;out.output=original_.run(mode,selected);
    if(mode!=Mode::independent)out.heads=original_.heads();
    if(mode==Mode::mixed){out.independent_done=original_.completion().stream_ns;out.chains_done=original_.completion().chains_ns;}
    return out;
  }
};

void validate(const Outcome& actual,const independent::Reference& reference) {
  study::require(actual.output==reference.output,"complete useful digest/hits/terminal");
  study::require(actual.heads==reference.heads,"all32 ordered terminal heads");
}
template<class Read> void audit(loom::Worker& worker,Read read,const study::input& in) {
  std::unordered_map<word,unsigned> wanted,seen;
  for(auto key:in.request_keys)++wanted[key];
  using Audit=study::audit_reader<Read>;
  Executor<Audit> executor(worker,{read,&seen},in);
  for(auto mode:modes(in.miss))for(auto arm:staged_native::arms(mode)) {
    independent::Reference reference;reference.output=independent::expected(in,mode,reference.heads);
    validate(executor.run(mode,arm,now_ns()),reference);
    study::require(seen==wanted,"exact full request multiplicity in every arm");seen.clear();
  }
}

void schema(const std::string& path,const study::input& in,unsigned repeats,const char* process_owner,
    const study::conditioning& condition,const std::array<independent::Reference,3>& references,
    const OwnerFacts& facts) {
  std::ofstream out(path+".schema.json");study::require(bool(out),"schema destination");
  const std::string_view process(process_owner),suffix("_lookahead8");
  const bool lookahead=process.ends_with(suffix);
  const auto base=process.substr(0,process.size()-(lookahead?suffix.size():0));
  out<<"{\"schema\":\"loom-fresh-heap-composition-v1\",\"requests\":"<<in.requests<<",\"repeats\":"<<repeats
      <<",\"seed\":"<<in.seed<<",\"n\":"<<in.n<<",\"W\":"<<study::W<<",\"b\":"<<study::b
      <<",\"K\":"<<study::W-study::b<<",\"E\":"<<study::E<<",\"V\":"<<study::V
      <<",\"multi\":"<<study::Multi<<",\"bags\":"<<IBS_BAGS<<",\"miss_percent\":"<<in.miss
      <<",\"input_pattern\":\"sample_without_replacement\",\"allocation_model\":\"fresh_heap\""
      <<",\"candidate_reader\":\"borrowed_const_heap_owner\",\"common_ring_capacity\":64"
      <<",\"memory_preparation\":\"scalar_inserts_appends_no_conversion\",\"native_geometry\":\"qualified_static64\""
      <<",\"reader_policy_binding\":\"stable_unchanged_owner\",\"staging_policy\":\""<<(lookahead?"lookahead8":"dependent")<<"\""
      <<",\"logical_key_bits\":30,\"column_E_scope\":\"logical_residual_geometry\",\"process_owner\":\""<<process_owner<<"\",\"process_base_owner\":\""<<base<<"\""
      <<",\"functional_only\":"<<(facts.functional_only?"true":"false")<<",\"hardware_line_bytes\":"<<facts.hardware_line_bytes
#if WRITE_ARCH_REDUCTION_MATRIX
      <<",\"reduction_study\":true,\"reduction_arm\":"<<WRITE_ARCH_VALUEWISE_REDUCTION
#endif
#if WRITE_ARCH_PHASE_MATRIX
      <<",\"phase_study\":true,\"phase_arm\":"
#ifdef WRITE_ARCH_READ_COMBINED_ARM
      <<WRITE_ARCH_READ_COMBINED_ARM
#else
      <<WRITE_ARCH_MM_INLINE_ADVANCE
#endif
#endif
      <<",\"owner_profiles\":{\"packed\":{\"logical_key_bits\":30,\"stored_key_bits\":30,\"primary_header_bytes\":64,\"source_profile\":\""
      <<(study::Multi?"fresh_packed_pooled_multimap":"fresh_packed_shared_stores")<<"\"},"
      <<"\"whole\":{\"logical_key_bits\":30,\"stored_key_bits\":32,\"primary_header_bytes\":96,\"source_profile\":\""
      <<(study::Multi?"fresh_whole_pooled_multimap":"fresh_whole_shared_stores")<<"\"},"
      <<"\"maintained\":{\"logical_key_bits\":30,\"stored_key_bits\":30,\"primary_header_bytes\":64,\"source_profile\":\"maintained_heap_native_k30\"},"
      <<"\"abseil\":{\"logical_key_bits\":30,\"stored_key_bits\":\"implementation\",\"primary_header_bytes\":\"implementation\",\"source_profile\":\"actual_abseil\"}}"
      <<",\"reader_policies\":{\"packed\":\"dependent\",\"whole\":\"dependent\",\"maintained\":\"dependent\",\"abseil\":\"actual_abseil\"";
  if constexpr(extra_policy)out<<",\"packed_lookahead8\":\"lookahead8\",\"whole_lookahead8\":\"lookahead8\",\"maintained_lookahead8\":\"lookahead8\"";
  out<<'}'
      <<",\"completion_style\":\"terminal_native_helper\",\"chain_accumulation\":\"persistent_frame\""
      <<",\"chain_candidates\":\"direct_persistent_and_job_first_context_value\""
      <<",\"prefetch_kernel\":\"loom_prefetch_for_each_shared_with_bounded_stream\""
      <<",\"context_bulk_composition\":\"same_prepared_callbacks_shared_or_sequential\""
      <<",\"independent_staged\":\"32_stride_lanes_full_lookup_before_reuse\""
      <<",\"owner_bytes\":"<<facts.bytes<<",\"owner_capacity\":"<<facts.capacity<<",\"owner_spill\":"<<facts.spill
      <<",\"fixture_input_bytes\":"<<facts.fixture_bytes
      <<",\"owner_setup\":{\"elapsed_ns\":"<<facts.elapsed_ns
      <<",\"scope\":\"construction and actual scalar insert/append population, outside steady query timers; owner reused unchanged across arms\"}"
      <<",\"storage_scope\":\"reported allocated backing, not RSS; Abseil table bytes omit external bag storage; no Pool or retained-reader qualification\""
      <<",\"eviction_bytes\":"<<condition.bytes()<<",\"expected\":[";
  bool first=true;
  for(auto mode:modes(in.miss)) {
    if(!first)out<<',';first=false;const auto& ref=references[unsigned(mode)];
    out<<"{\"mode\":\""<<independent::label(mode)<<"\",\"sum\":"<<ref.output.sum<<",\"hits\":"<<ref.output.hits
       <<",\"terminal\":"<<ref.output.terminal<<",\"stream_requests\":"<<(mode==Mode::chains?0:mode==Mode::mixed?in.requests/2:in.requests)
       <<",\"chain_requests\":"<<(mode==Mode::independent?0:mode==Mode::mixed?in.requests-in.requests/2:in.requests)<<",\"ordered_heads\":[";
    for(unsigned i=0;i<lanes;++i){if(i)out<<',';out<<ref.heads[i];}out<<"]}";
  }
  out<<"],\"expected_arms\":[";first=true;
  for(auto mode:modes(in.miss))for(auto arm:staged_native::arms(mode)) {
    if(!first)out<<',';first=false;
    out<<"{\"implementation\":\""<<process_owner<<"\",\"mode\":\""<<independent::label(mode)<<"\",\"arm\":\""<<label(arm)
       <<"\",\"Q\":"<<quantum(arm)<<",\"chain_hint\":"<<hint(mode,arm)<<'}';
  }
  out<<"],\"scope\":\"K30 fresh packed64 and whole96 heap owners, maintained heap and actual Abseil; scalar population, full bags, all-hit value-carried chains/mixed and independent miss0/50.\","
      <<"\"completion_scope\":\"Mixed cohort completion since shared start, not per-query latency.\","
      <<"\"legacy_counter\":\"Original typed control retains historical visits counter; common callbacks have no diagnostic visit counter.\","
      <<"\"comparison_scope\":\"Separate owner processes with matching input and complete procedure; heap-only, no retained-reader or public Pool inference.\"}\n";
}

template<class Read> void time_one(std::ofstream& out,const std::string& output,const char* impl,
    Executor<Read>& executor,const study::input& in,study::conditioning& condition,Mode mode,Arm arm,unsigned rep,
    pmu::Mode setting,const independent::Reference& reference) {
  validate(executor.run(mode,arm,now_ns()),reference);
  pmu::Session measurement(setting);condition.apply();
  measurement.start();const auto begin=now_ns();const auto actual=executor.run(mode,arm,begin);const auto end=now_ns();measurement.stop();
  validate(actual,reference);study::require(end>begin,"positive full-operation time");
  if(mode==Mode::mixed)study::require(actual.independent_done&&actual.chains_done&&
      actual.independent_done<=end-begin&&actual.chains_done<=end-begin,"complete cohort clocks within full operation");
  out<<impl<<','<<label(arm)<<','<<independent::label(mode)<<','<<quantum(arm)<<','<<in.seed<<','<<rep<<','
      <<study::W<<','<<study::b<<','<<study::W-study::b<<','<<study::E<<','<<study::V<<','<<study::Multi<<','<<IBS_BAGS<<','<<in.n<<','<<in.requests
      <<','<<in.miss<<','<<end-begin<<','<<actual.output.sum<<','<<actual.output.hits<<','<<actual.output.terminal<<','
      <<(mode==Mode::chains?0:mode==Mode::mixed?in.requests/2:in.requests)<<','
      <<(mode==Mode::independent?0:mode==Mode::mixed?in.requests-in.requests/2:in.requests)<<','
      <<actual.independent_done<<','<<actual.chains_done<<','<<hint(mode,arm);
  for(auto head:actual.heads)out<<','<<head;out<<'\n'<<std::flush;
  if(setting!=pmu::Mode::off||std::getenv("LI_PMU_OUTPUT")) {
    const auto directory=std::getenv("LI_PMU_OUTPUT")?std::filesystem::path(std::getenv("LI_PMU_OUTPUT")):std::filesystem::path(output+".pmu");
    std::filesystem::create_directories(directory);
    std::ofstream receipt(directory/(std::string(impl)+"-"+independent::label(mode)+"-"+label(arm)+"-seed"+std::to_string(in.seed)+"-rep"+std::to_string(rep)+".json"));
    study::require(bool(receipt),"PMU receipt destination");
    receipt<<"{\"implementation\":\""<<impl<<"\",\"mode\":\""<<independent::label(mode)<<"\",\"arm\":\""<<label(arm)
        <<"\",\"Q\":"<<quantum(arm)<<",\"chain_hint\":"<<hint(mode,arm)<<",\"seed\":"<<in.seed<<",\"rep\":"<<rep
        <<",\"requests\":"<<in.requests<<",\"elapsed_ns\":"<<end-begin<<",\"measurement\":";
    measurement.write_json(receipt);receipt<<"}\n";
  }
}

template<class Owner,ikea::hashtable::staged_values Values=ikea::hashtable::staged_values::dependent> void execute_owner(const std::string& output,std::size_t n,std::size_t requests,word seed,unsigned repeats,
    std::size_t eviction,unsigned miss,bool checking,bool concentrated,bool functional,const char* label) {
  auto input=independent::make_input(n,requests,seed,miss,concentrated);study::runtime runtime(!checking&&!functional);
  OwnerFacts facts;facts.functional_only=functional;facts.hardware_line_bytes=runtime.worker.hints.profile.geometry.line_bytes;
  facts.fixture_bytes=input->keys.capacity()*sizeof(word)+input->request_keys.capacity()*sizeof(word)+
      input->next.capacity()*sizeof(std::uint32_t)+input->order.capacity()*sizeof(std::uint32_t)+
      input->narrow_edges.capacity()*sizeof(std::array<std::uint32_t,2>);
  const auto setup_start=now_ns();
  auto owner=[&]{if constexpr(std::is_same_v<Owner,study::Abseil>) {
      auto value=std::make_unique<Owner>();value->reserve(n);return value;
    }else return std::make_unique<Owner>(study::random_word(seed)&study::mask<study::W>);
  }();
  study::fill(*owner,*input);facts.elapsed_ns=now_ns()-setup_start;
  facts.bytes=study::owner_bytes(*owner);facts.capacity=study::owner_capacity(*owner);facts.spill=study::owner_spill(*owner);
  const auto read=[&]{if constexpr(std::is_same_v<Owner,study::Abseil>)return study::abseil_reader{owner.get()};
    else return study::borrowed_reader<Owner,Values>{owner.get()};}();
  if(checking){audit(runtime.worker,read,*input);study::inspect(read,*input);study::validate_owner(*owner);return;}
  study::inspect(read,*input);
  Executor<std::remove_cv_t<decltype(read)>> executor(runtime.worker,read,*input);
  study::conditioning condition(eviction<<20);std::array<independent::Reference,3> references{};
  const auto useful=input->independent_expected();for(auto mode:modes(input->miss))
    references[unsigned(mode)].output=independent::expected(*input,mode,references[unsigned(mode)].heads,&useful);
  std::ofstream out(output);study::require(bool(out),"CSV destination");
  out<<"implementation,arm,mode,Q,seed,rep,W,b,K,E,V,multi,bags,n,requests,miss_percent,elapsed_ns,sum,hits,terminal,stream_requests,chain_requests,stream_done_ns,chain_done_ns,chain_hint";
  for(unsigned i=0;i<lanes;++i)out<<",head"<<i;out<<'\n';
  staged_native::schema(output,*input,repeats,label,condition,references,facts);
  const auto setting=independent::pmu_mode();
  for(unsigned rep=0;rep<repeats;++rep)for(auto mode:modes(input->miss)) {
    const auto selected=staged_native::arms(mode);const auto offset=(rep+seed)%selected.size();
    for(unsigned i=0;i<selected.size();++i)time_one(out,output,label,executor,*input,condition,mode,
        selected[(i+offset)%selected.size()],rep,setting,references[unsigned(mode)]);
  }
  study::validate_owner(*owner);
}
void execute(const std::string& output,std::size_t n,std::size_t requests,word seed,unsigned repeats,
    std::size_t eviction,unsigned miss,bool checking,bool concentrated=false,bool functional=false,const char* owner="packed") {
  const std::string_view selected(owner);
  if constexpr(extra_policy) {
    constexpr auto policy=ikea::hashtable::staged_values::lookahead8;
    if(selected=="packed_lookahead8"){execute_owner<study::Packed,policy>(output,n,requests,seed,repeats,eviction,miss,checking,concentrated,functional,owner);return;}
    if(selected=="whole_lookahead8"){execute_owner<study::Whole,policy>(output,n,requests,seed,repeats,eviction,miss,checking,concentrated,functional,owner);return;}
    if(selected=="maintained_lookahead8"){execute_owner<study::MaintainedHeap,policy>(output,n,requests,seed,repeats,eviction,miss,checking,concentrated,functional,owner);return;}
  }
  if(selected=="packed")execute_owner<study::Packed>(output,n,requests,seed,repeats,eviction,miss,checking,concentrated,functional,owner);
  else if(selected=="whole")execute_owner<study::Whole>(output,n,requests,seed,repeats,eviction,miss,checking,concentrated,functional,owner);
  else if(selected=="maintained")execute_owner<study::MaintainedHeap>(output,n,requests,seed,repeats,eviction,miss,checking,concentrated,functional,owner);
  else if(selected=="abseil")execute_owner<study::Abseil>(output,n,requests,seed,repeats,eviction,miss,checking,concentrated,functional,owner);
  else study::require(false,"unknown heap process owner");
}

}

int main(int argc,char** argv) {
  try {
    if(argc==2&&std::string_view(argv[1])=="check") {
      for(const auto* owner:staged_native::implementations()) {
        for(unsigned miss:{0u,50u})for(unsigned requests:{0u,1u,31u,32u,33u,65u,67u,129u})
          staged_native::execute("",257,requests,713,1,0,miss,true,false,false,owner);
        for(unsigned miss:{0u,50u})staged_native::execute("",257,129,713,1,0,miss,true,true,false,owner);
      }
      std::puts("PASS: fresh scalar heap owners, maintained heap and actual Abseil; full values/multiplicity/all32 heads; empty/short/ragged/concentrated checks");return 0;
    }
    study::require(argc==9||argc==10,"OUT n requests seed repeats evictionMiB miss0/50 --owner=NAME [--functional]");
    const auto n=std::stoull(argv[2]),requests=std::stoull(argv[3]),seed=std::stoull(argv[4]);
    const auto repeats=std::stoull(argv[5]),eviction=std::stoull(argv[6]),miss=std::stoull(argv[7]);
    const std::string_view selection(argv[8]);study::require(selection.starts_with("--owner="),"explicit process owner");
    const auto owner=std::string(selection.substr(8));const bool functional=argc==10;
    if(functional)study::require(std::string_view(argv[9])=="--functional"&&repeats==1&&eviction==0&&
        n<=65536&&requests<=65536&&independent::pmu_mode()==staged_native::pmu::Mode::off,
        "functional fixed64 smoke is small, unconditioned and PMU-disabled; never native evidence");
    study::require(requests>=32&&repeats&&repeats<=100&&(miss==0||miss==50)&&eviction<=std::numeric_limits<std::size_t>::max()/(1u<<20),"supported request/repeat/miss scope");
    staged_native::execute(argv[1],n,requests,seed,repeats,eviction,miss,false,false,functional,owner.c_str());
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

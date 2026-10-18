#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "caller.cpp"
#pragma clang diagnostic pop
#include "context.inc"
#include "companion.inc"
#include "context_bulk.inc"

namespace staged_companion {
template<class Read,unsigned Kind> struct Observed : study::audit_reader<Read> {
  using Base=study::audit_reader<Read>;
  std::vector<unsigned>* trace{};
  Observed()=default;
  Observed(Read read,std::unordered_map<word,unsigned>& seen,std::vector<unsigned>& log)
      :Base{read,&seen},trace(&log){}
  template<class Consume,class Emit> bool advance(typename Read::staged_probe& probe,
      bool& found,Consume&& consume,Emit&& emit)const {
    trace->push_back(Kind);
    return Base::advance(probe,found,consume,emit);
  }
};

std::unordered_map<word,unsigned> multiplicity(std::span<const word> keys) {
  std::unordered_map<word,unsigned> out;for(auto key:keys)++out[key];return out;
}
void same(const Output& actual,const result& expected,const std::array<word,lanes>& heads) {
  study::require(actual.combined==expected,"staged companion full useful digest/hits/terminal");
  study::require(actual.heads==heads,"staged companion every ordered chain head");
  study::require(!actual.independent_pending&&!actual.chains_pending,"both cohorts physically complete");
}
template<class Shared> void finish(Shared& shared,unsigned budget=1) {
  while(shared.pending()) {
    const auto visited=shared.spin({budget});
    study::require(visited&&visited<=budget,"one shared bounded spin progresses");
  }
}

enum class BulkEvent { hint, request, chain_prepare, chain_stage };
template<class Read> struct BulkObserved {
  Read read;
  std::unordered_map<word,unsigned>* hints;
  std::unordered_map<word,unsigned>* requests;
  std::unordered_map<word,unsigned>* chains;
  std::vector<BulkEvent>* trace;
  using staged_probe=typename Read::staged_probe;
  void prefetch(word key)const {
    ++(*hints)[key];trace->push_back(BulkEvent::hint);read.prefetch(key);
  }
  template<class Consume> bool find(word key,Consume&& consume)const {
    ++(*requests)[key];trace->push_back(BulkEvent::request);
    return read.find(key,std::forward<Consume>(consume));
  }
  template<class Emit> void prepare_staged(staged_probe& probe,word key,Emit&& emit)const {
    ++(*chains)[key];trace->push_back(BulkEvent::chain_prepare);
    read.prepare_staged(probe,key,std::forward<Emit>(emit));
  }
  template<class Consume,class Emit> bool advance(staged_probe& probe,bool& found,
      Consume&& consume,Emit&& emit)const {
    trace->push_back(BulkEvent::chain_stage);
    return read.advance(probe,found,std::forward<Consume>(consume),std::forward<Emit>(emit));
  }
};

// The oracle uses the input's rank/value construction, not either lookup path.
result prefix_expected(const study::input& in,std::size_t count) {
  result out;
  for(std::size_t i=0;i<count;++i)if(const auto rank=in.order[i];rank<in.n) {
    ++out.hits;
    for(unsigned j=0;j<in.fanout(rank);++j)out.sum+=study::digest(in.value(rank,j));
  }
  return out;
}

template<class Read,loom::Hint Hint> void check_bulk(loom::Worker& worker,Read read,const study::input& in) {
  const std::span<const word> keys(in.request_keys);
  std::unordered_map<word,unsigned> hints,requests,chains;
  std::vector<BulkEvent> trace;
  using Observed=BulkObserved<Read>;
  const Observed observed{read,&hints,&requests,&chains,&trace};
  ContextBulk<Observed> bank(worker,observed);
  ContextBulk<Read> actual(worker,read);
  const auto reset=[&]{hints.clear();requests.clear();chains.clear();trace.clear();};
  const auto events=[&](BulkEvent kind){return std::size_t(std::count(trace.begin(),trace.end(),kind));};

  const auto check=[&]<bool Sequential>() {
    // Both admission orders accept empty/short/odd/all-miss independent input.
    // A set has no value-carried successor, so its checks stop at this cohort.
    reset();bank.template prepare<Hint,Sequential>(keys,{},independent::now_ns());
    study::require(bank.pending()==(Sequential||keys.empty()?std::size_t{0}:std::size_t{1}),"bulk independent admission/drain boundary");
    if constexpr(!Sequential)study::require(trace.empty(),"shared bulk admission performs no native work");
    finish(bank,3);same(bank.collect(),in.independent_expected(),{});
    study::require(hints==multiplicity(keys)&&requests==multiplicity(keys)&&chains.empty(),
        "bulk exact hint/useful request multiplicity including misses");
    study::require(!events(BulkEvent::chain_stage),"independent-only bulk never enters a chain callback");
    actual.template prepare<Hint,Sequential>(keys,{},independent::now_ns());finish(actual,17);
    same(actual.collect(),in.independent_expected(),{});

    if constexpr(study::V>=8)if(!in.miss) {
      const auto split=keys.size()/2;
      const auto head=keys.first(split),tail=keys.subspan(split);
      const auto prefix=prefix_expected(in,split);
      std::array<word,lanes> heads;
      const auto expected=independent::expected(in,independent::Mode::mixed,heads);
      const result chain_expected{expected.sum-prefix.sum,expected.hits-prefix.hits,expected.terminal};
      const auto count=std::min<std::size_t>(lanes,tail.size());
      reset();bank.template prepare<Hint,Sequential>(head,tail,independent::now_ns());
      study::require(bank.pending()==count+unsigned(!Sequential&&!head.empty()),"bulk shared/sequential active inventory");
      word first_receipt=0;
      if constexpr(Sequential) {
        const auto first=bank.collect();first_receipt=first.independent_done_ns;
        study::require(first.independent==prefix&&!first.independent_pending&&first.chains_pending==count,
            "sequential admission preserves complete first cohort and unvisited chain inventory");
        study::require(requests==multiplicity(head)&&!events(BulkEvent::chain_stage),
            "sequential first cohort is fully consumed before any chain stage");
        if(!head.empty()&&!tail.empty()) {
          const auto first_chain=std::find(trace.begin(),trace.end(),BulkEvent::chain_prepare);
          const auto last_request=std::find(trace.rbegin(),trace.rend(),BulkEvent::request).base();
          study::require(last_request<=first_chain,"sequential chain hints follow the returned first-cohort drain");
        }
      } else {
        study::require(!events(BulkEvent::request)&&!events(BulkEvent::chain_stage),
            "shared bulk admits both frame kinds before native service");
        if(!head.empty()) {
          study::require(bank.spin({1})==1,"one actual public stream callback");
          study::require(events(BulkEvent::request)==std::min<std::size_t>(128,head.size()),
              "one callback consumes exactly one bounded stream quantum");
          if(head.size()>128&&count) {
            study::require(bank.collect().independent_pending,"stream still live before mixed chain visit");
            study::require(bank.spin({1})==1&&events(BulkEvent::chain_stage)==1,
                "same-bank chain callback runs while the stream is unfinished");
            study::require(events(BulkEvent::request)==128,"chain service did not recursively drain the stream");
          }
        }
      }
      finish(bank,3);const auto out=bank.collect();same(out,expected,heads);
      study::require(out.independent==prefix&&out.chains==chain_expected,"both complete useful cohort outputs retained");
      study::require(hints==multiplicity(head)&&requests==multiplicity(head)&&chains==multiplicity(tail),
          "bulk/context composition exact per-cohort request multiplicity");
      if constexpr(Sequential)study::require(out.independent_done_ns==first_receipt,
          "first terminal clock receipt survives chain admission/completion without reset");
      // Only receipt preservation is checked, never clock magnitudes or timing.
      actual.template prepare<Hint,Sequential>(head,tail,independent::now_ns());finish(actual,7);
      same(actual.collect(),expected,heads);

      if(keys.size()>=259) {
        reset();bank.template prepare<Hint,Sequential>(head,tail,independent::now_ns());
        study::require(bank.spin({1})==1,"partial bulk/context service before cancellation");
        const auto pending=bank.pending(),recorded=trace.size();
        study::require(bank.discard()==pending&&!bank.pending(),"bulk/context returned-boundary discard");
        study::require(bank.spin({1024})==0&&trace.size()==recorded,"discarded bulk/context frames never resume");
        const auto partial=bank.collect();
        study::require(partial.independent_pending||partial.chains_pending,"discard is not successful cohort completion");
        reset();bank.template prepare<Hint,Sequential>(head,tail,independent::now_ns());finish(bank);
        same(bank.collect(),expected,heads);
        study::require(hints==multiplicity(head)&&requests==multiplicity(head)&&chains==multiplicity(tail),
            "fresh binding after partial discard neither replays nor retains prior output");
      }
    }
  };
  check.template operator()<false>();
  check.template operator()<true>();
}

template<class Read,loom::Hint Hint,bool ContextChains=false> void check_read(loom::Worker& worker,Read read,const study::input& in) {
  const std::span<const word> keys(in.request_keys);
  std::unordered_map<word,unsigned> seen_independent,seen_chain;
  std::vector<unsigned> trace;trace.reserve(100000);
  using I=Observed<Read,0>;using C=Observed<Read,1>;
  const I independent_read{read,seen_independent,trace};
  const C chain_read{read,seen_chain,trace};
  Shared<I,C,ContextChains> shared(worker,independent_read,chain_read);

  // Independent paths include empty/short/ragged batches and all requested
  // misses. Hit count cannot control the stride or request completion.
  shared.template prepare<Hint>(keys,{},independent::now_ns());
  study::require(trace.empty(),"admission did not run any native callback");
  study::require(shared.pending()==std::min<std::size_t>(lanes,keys.size()),"exact staged active inventory");
  finish(shared,3);
  same(shared.collect(),in.independent_expected(),{});
  study::require(seen_independent==multiplicity(keys)&&seen_chain.empty(),"independent exact key multiplicity including misses");
  study::require(std::all_of(trace.begin(),trace.end(),[](unsigned kind){return kind==0;}),"independent callback identity");
  seen_independent.clear();seen_chain.clear();trace.clear();

  // Bare actual Read instantiations use no audit map or visit instrumentation.
  Shared<Read,Read,ContextChains> actual(worker,read,read);
  actual.template prepare<Hint>(keys,{},independent::now_ns());finish(actual,17);
  same(actual.collect(),in.independent_expected(),{});

  if constexpr(study::V>=8)if(!in.miss) {
    const auto split=keys.size()/2;
    const auto head=keys.first(split),tail=keys.subspan(split);
    const auto independent_wanted=multiplicity(head),chains_wanted=multiplicity(tail);
    std::array<word,lanes> heads;const auto expected=independent::expected(in,independent::Mode::mixed,heads);
    shared.template prepare<Hint>(head,tail,independent::now_ns());
    const auto first_lap=std::min<std::size_t>(lanes,head.size())+std::min<std::size_t>(lanes,tail.size());
    study::require(shared.pending()==first_lap&&trace.empty(),"both kinds admitted to one64-slot bank before work");
    if(first_lap) {
      study::require(shared.spin({first_lap})==first_lap,"one fair native rotation");
      study::require(trace.size()==first_lap,"one actual stage visit per native dispatch");
      const auto independent_count=std::min<std::size_t>(lanes,head.size());
      for(std::size_t i=0;i<first_lap;++i)
        study::require(trace[i]==unsigned(i>=independent_count),"different frame procedures actually interleave in one rotation");
    }
    finish(shared);
    same(shared.collect(),expected,heads);
    study::require(seen_independent==independent_wanted&&seen_chain==chains_wanted,
        "mixed independent keys and chain keys each issued exactly once");
    seen_independent.clear();seen_chain.clear();trace.clear();

    Sequential<I,C> sequential(worker,independent_read,chain_read,in);
    same(sequential.template run<Hint>(split,independent::now_ns()),expected,heads);
    study::require(seen_independent==independent_wanted&&seen_chain==chains_wanted,
        "original typed sequential control exact useful demand");
    seen_independent.clear();seen_chain.clear();trace.clear();

    actual.template prepare<Hint>(head,tail,independent::now_ns());finish(actual,7);
    same(actual.collect(),expected,heads);
    Sequential<Read> actual_sequential(worker,read,read,in);
    same(actual_sequential.template run<Hint>(split,independent::now_ns()),expected,heads);
    if(keys.size()>=65) {
      shared.template prepare<Hint>(head,tail,independent::now_ns());
      study::require(shared.spin({1})==1,"partial shared work before cancellation");
      const auto pending=shared.pending();const auto events=trace.size();
      study::require(shared.discard()==pending&&!shared.pending(),"returned-boundary discard removes both kinds");
      study::require(shared.spin({1024})==0&&trace.size()==events,"removed borrowed frames never resume");
      const auto partial=shared.collect();
      study::require(partial.independent_pending||partial.chains_pending,"discard does not masquerade as successful cohort completion");
      seen_independent.clear();seen_chain.clear();trace.clear();
      shared.template prepare<Hint>(head,tail,independent::now_ns());finish(shared);
      same(shared.collect(),expected,heads);
      study::require(seen_independent==independent_wanted&&seen_chain==chains_wanted,"safe fresh binding after returned discard");
    }
  }
}

template<class Read> void all_reads(loom::Worker& worker,Read read,const study::input& in) {
  check_read<Read,loom::Hint::l1_1>(worker,read,in);
  check_read<Read,loom::Hint::l2_1>(worker,read,in);
  check_read<Read,loom::Hint::l1_1,true>(worker,read,in);
  check_read<Read,loom::Hint::l2_1,true>(worker,read,in);
  check_bulk<Read,loom::Hint::l1_1>(worker,read,in);
  check_bulk<Read,loom::Hint::l2_1>(worker,read,in);
}

template<class Owner,ikea::hashtable::staged_values Values=ikea::hashtable::staged_values::dependent>
auto borrow(const Owner& owner) {
  if constexpr(std::is_same_v<Owner,study::Abseil>)return study::abseil_reader{&owner};
  else return study::borrowed_reader<Owner,Values>{&owner};
}

template<class Owner> void insert_rank(Owner& owner,const study::input& in,std::size_t rank) {
  for(unsigned j=0;j<in.fanout(rank);++j) {
    const bool fresh=study::insert_value(owner,in.keys[rank],in.value(rank,j));
    study::require(fresh==(j==0),"fresh owner rank and complete bag append");
  }
}

// This oracle is independent of both direct and staged lookup. Erased ranks
// are absent; the live ranks retain the exact input-generated ordered values.
template<class Read> void inspect_live(Read read,const study::input& in,const std::vector<bool>& live) {
  for(std::size_t rank=0;rank<in.keys.size();++rank) {
    const bool present=rank<live.size()&&live[rank];
    const auto wanted=present&&study::V?in.fanout(rank):0;
    const auto consume=[&](unsigned& count,const auto& value) {
      study::require(present&&count<wanted&&value==in.value(rank,count),"mutation ordered full value");++count;
    };
    unsigned count=0;
    const bool found=read.find(in.keys[rank],[&](const auto& value){consume(count,value);});
    study::require(found==present&&count==wanted,"mutation direct membership and multiplicity");
    typename Read::staged_probe probe{};bool staged_found=false;count=0;unsigned visits=0;
    const study::hint_emitter<loom::Hint::l1_1,true> emit{4096,64};
    read.prepare_staged(probe,in.keys[rank],emit);
    while(!read.advance(probe,staged_found,[&](const auto& value){consume(count,value);},emit))
      study::require(++visits<10000,"mutation staged termination");
    study::require(staged_found==present&&count==wanted,"mutation staged membership and multiplicity");
  }
}

template<class Read> void live_independent(loom::Worker& worker,Read read,
    const study::input& in,const std::vector<bool>& live) {
  inspect_live(read,in,live);
  result expected;
  for(auto rank:in.order)if(rank<live.size()&&live[rank]) {
    ++expected.hits;
    for(unsigned j=0;j<in.fanout(rank);++j)expected.sum+=study::digest(in.value(rank,j));
  }
  const std::span<const word> keys(in.request_keys);
  std::unordered_map<word,unsigned> seen;std::vector<unsigned> trace;
  using ReadObserved=Observed<Read,0>;
  const ReadObserved observed{read,seen,trace};
  Shared<ReadObserved> staged(worker,observed,observed);
  staged.template prepare<loom::Hint::l1_1>(keys,{},independent::now_ns());finish(staged,3);
  same(staged.collect(),expected,{});
  study::require(seen==multiplicity(keys),"mutation staged independent exact issued multiplicity");
  std::unordered_map<word,unsigned> hints,requests,chains;std::vector<BulkEvent> bulk_trace;
  const BulkObserved<Read> bulk_read{read,&hints,&requests,&chains,&bulk_trace};
  ContextBulk<BulkObserved<Read>> bulk(worker,bulk_read);
  bulk.template prepare<loom::Hint::l2_1>(keys,{},independent::now_ns());finish(bulk,7);
  same(bulk.collect(),expected,{});
  study::require(hints==multiplicity(keys)&&requests==multiplicity(keys)&&chains.empty(),
      "mutation bulk exact hint/useful request multiplicity including misses");
}

template<class Read> void repeated_independent(loom::Worker& worker,Read read,const study::input& in) {
  auto repeated=in;repeated.order.clear();repeated.request_keys.clear();repeated.requests=67;
  for(unsigned i=0;i<67;++i) {
    const auto rank=in.miss&&i%3==0?in.n+(i%7):i%5;
    repeated.order.push_back(rank);repeated.request_keys.push_back(in.keys[rank]);
  }
  std::vector<bool> live(in.keys.size(),false);std::fill_n(live.begin(),in.n,true);
  // Repeated demand is an independent-only fixture; no successor graph is
  // fabricated for it. Expected sums/counts include every occurrence and miss.
  live_independent(worker,read,repeated,live);
}

template<class Owner,ikea::hashtable::staged_values Values=ikea::hashtable::staged_values::dependent>
void mutation_reads(loom::Worker& worker,Owner& owner,const study::input& in) {
  // Concentration exceeds one primary bucket, forcing actual Swiss keys. No
  // outstanding probe, frame, or borrowed view crosses any mutation below.
  std::vector<bool> live(in.n,true);
  if constexpr(!std::is_same_v<Owner,study::Abseil>)
    study::require(study::owner_spill(owner)>0,"concentrated mutation fixture has real overflow");
  if constexpr(study::Multi&&study::Bags) {
    std::array<bool,65> fanout{};for(std::size_t rank=0;rank<in.n;++rank)fanout[in.fanout(rank)]=true;
    for(auto n:{1u,2u,4u,16u,64u})study::require(fanout[n],"mutation fixture crosses inline/external bag boundaries");
  }
  live_independent(worker,borrow<Owner,Values>(owner),in,live);
  for(std::size_t rank=0;rank<in.n;rank+=2) {
    study::require(study::erase_key(owner,in.keys[rank]),"erase live mutation rank");live[rank]=false;
    study::require(!study::erase_key(owner,in.keys[rank]),"repeat erase remains absent");
  }
  study::validate_owner(owner);
  // Leave holes in place while reading: Swiss tombstones and absent primary
  // rows must not turn misses into stale payloads or terminate searches early.
  live_independent(worker,borrow<Owner,Values>(owner),in,live);
  for(std::size_t rank=0;rank<in.n;rank+=2){insert_rank(owner,in,rank);live[rank]=true;}
  study::validate_owner(owner);live_independent(worker,borrow<Owner,Values>(owner),in,live);
  // Original graph values were restored, so genuine value-carried chains are
  // again valid, with their oracle and all ordered heads still input-derived.
  all_reads(worker,borrow<Owner,Values>(owner),in);
  for(auto key:in.keys)study::require(study::erase_key(owner,key),"drain before same-instance refill");
  std::fill(live.begin(),live.end(),false);live_independent(worker,borrow<Owner,Values>(owner),in,live);
  study::validate_owner(owner);
  auto replacement=study::make_input(in.n,in.requests,3191,0,false);
  study::fill(owner,*replacement);study::validate_owner(owner);
  // A newly constructed input supplies a different valid payload graph; no
  // expected edge/value is obtained by inspecting the owner being checked.
  study::inspect(borrow<Owner,Values>(owner),*replacement);all_reads(worker,borrow<Owner,Values>(owner),*replacement);
}

template<class Owner,ikea::hashtable::staged_values Values=ikea::hashtable::staged_values::dependent>
void run_owner(loom::Worker& worker,const study::input& in,bool mutate) {
  Owner owner(study::random_word(in.seed));study::fill(owner,in);
  study::inspect(borrow<Owner,Values>(owner),in);all_reads(worker,borrow<Owner,Values>(owner),in);study::validate_owner(owner);
  if(in.requests==67)repeated_independent(worker,borrow<Owner,Values>(owner),in);
  if(mutate)mutation_reads<Owner,Values>(worker,owner,in);
}

void execute(std::size_t count,unsigned miss,bool concentrated=false) {
  auto in=independent::make_input(std::max<std::size_t>(257,count),count,713,miss,concentrated);
  study::runtime runtime(false);const bool mutate=concentrated&&!miss&&count==513;
  run_owner<study::Packed>(runtime.worker,*in,mutate);
  run_owner<study::Whole>(runtime.worker,*in,mutate);
  run_owner<study::MaintainedHeap>(runtime.worker,*in,mutate);
  if constexpr(study::V==8&&!study::Multi) {
    run_owner<study::Packed,ikea::hashtable::staged_values::lookahead8>(runtime.worker,*in,mutate);
    run_owner<study::Whole,ikea::hashtable::staged_values::lookahead8>(runtime.worker,*in,mutate);
    run_owner<study::MaintainedHeap,ikea::hashtable::staged_values::lookahead8>(runtime.worker,*in,mutate);
  }
  study::Abseil baseline;baseline.reserve(in->n);study::fill(baseline,*in);
  study::inspect(borrow(baseline),*in);all_reads(runtime.worker,borrow(baseline),*in);
  if(in->requests==67)repeated_independent(runtime.worker,borrow(baseline),*in);
  if(mutate)mutation_reads(runtime.worker,baseline,*in);
}
}
int main() {
  for(unsigned miss:{0u,50u})for(unsigned count:{0u,1u,2u,31u,32u,33u,63u,64u,65u,67u,129u,259u,513u})
    staged_companion::execute(count,miss);
  for(unsigned miss:{0u,50u})staged_companion::execute(129,miss,true);
  staged_companion::execute(513,0,true);
  std::puts("PASS: raw heap dependent and explicit Map8 lookahead8 reads; original typed controls and same-callback shared/sequential; full values/all heads/exact multiplicity/fair rotation/drain/discard/rebind; same-instance mutation and fresh borrows");
}

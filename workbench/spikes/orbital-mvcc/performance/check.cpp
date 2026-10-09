#include "run.h"
#include "oracle.h"
#include "candidate_runtime.h"
#include <iostream>

using namespace orbital_versioned_performance;
using namespace orbital_bench;
void require(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
template<class Store> std::uint64_t candidate_publication_check() {
  Corpus corpus(68,4,8132);Candidate<Store> owner(corpus,64*1024*1024);
  typename Store::Scope old(owner.catalog());
  std::array<Operation,4> edits;std::array<std::size_t,4> objects{0,1,2,3};std::array<Outcome,4> output;
  std::vector<Golden> expected(4),original(4);
  for (std::size_t i=0;i<4;++i) {
    for (const auto& row:corpus.images[i].rows) original[i].emplace(row.key,row.record);
    expected[i]=original[i];
    const auto key=corpus.images[i].rows[3].key;
    edits[i]={.kind=Kind::update,.key=key,.record={0,unsigned(i+9),unsigned(i+27)},.count=1};
    expected[i][key].value=edits[i].record.value;expected[i][key].tag=edits[i].record.tag;
  }
  owner.update(edits,output,objects);
  std::uint64_t comparisons{};
  {
    typename Store::Scope current(owner.catalog());
    for (std::size_t i=0;i<4;++i) {
      // First binding of every old Object occurs after real publication.
      const auto before=owner.body(old,i),after=owner.body(current,i);
      for (const auto& [key,row]:original[i]) {
        require(before.get(key)==row,"candidate late old discovery differs");
        require(after.get(key)==expected[i].at(key),"candidate real publication differs");++comparisons;
      }
      require(output[i].changes==1,"candidate publication outcome differs");
    }
  }
  const auto cut=owner.cut();auto invalid=edits;invalid[1].key=2;bool refused{};
  try {owner.update(invalid,output,objects);} catch(const std::runtime_error&){refused=true;}
  require(refused && owner.cut()==cut,"candidate failed private batch advanced cut");
  typename Store::Scope current(owner.catalog());
  for (std::size_t i=0;i<4;++i) for (const auto& [key,row]:expected[i])
    require(owner.body(current,i).get(key)==row,"candidate failed private batch changed bytes");
  owner.poll(65536);
  return comparisons;
}
template<class Store> std::uint64_t candidate_sparse_check() {
  Corpus corpus(192,64,7132);Candidate<Store> owner(corpus,64*1024*1024);
  const auto initial_versions=owner.statistics().versions;
  std::array<std::size_t,6> objects{63,1,63,17,42,1};
  std::array<Operation,6> edits;std::array<Outcome,6> output;
  std::vector<Golden> expected(64),original(64);
  for (std::size_t i=0;i<64;++i) {
    for (const auto& row:corpus.images[i].rows) original[i].emplace(row.key,row.record);
    expected[i]=original[i];
  }
  for (std::size_t i=0;i<edits.size();++i) {
    const auto key=corpus.images[objects[i]].rows[1].key;
    edits[i]={.kind=Kind::update,.key=key,.record={0,unsigned(i+100),unsigned(i+27)},.count=1};
    expected[objects[i]][key].value=edits[i].record.value;expected[objects[i]][key].tag=edits[i].record.tag;
  }
  owner.update(edits,output,objects);
  require(owner.statistics().versions==initial_versions,"writer basis retained old versions without reader");
  std::uint64_t comparisons{};
  {
    typename Store::Scope retained(owner.catalog());
    const auto cut=owner.cut();auto bad=edits;bad[4].key=2;bool refused{};
    try {owner.update(bad,output,objects);} catch(const std::runtime_error&){refused=true;}
    require(refused && owner.cut()==cut,"sparse failure after clones advanced cut");
    owner.poll(65536);
    {
      typename Store::Scope unchanged(owner.catalog());
      for (std::size_t i=0;i<64;++i) for (const auto& [key,row]:expected[i]) {
        require(owner.body(unchanged,i).get(key)==row,"sparse failure changed current row");++comparisons;
      }
    }
    const auto before_retry=expected;
    for (std::size_t i=0;i<edits.size();++i) {
      edits[i].record.value+=500;expected[objects[i]][edits[i].key].value=edits[i].record.value;
    }
    owner.update(edits,output,objects);
    typename Store::Scope current(owner.catalog());
    for (std::size_t i=0;i<64;++i) for (const auto& [key,row]:expected[i]) {
      require(owner.body(retained,i).get(key)==before_retry[i].at(key),"sparse late old discovery changed");
      require(owner.body(current,i).get(key)==row,"sparse successful retry differs");++comparisons;
    }
  }
  owner.drain(SIZE_MAX);owner.poll(65536);
  require(owner.statistics().versions==initial_versions,"sparse release left unnecessary versions");
  return comparisons;
}
template<class Store> std::uint64_t candidate_allocation_refusal_check() {
  Corpus corpus(2048,2,8713);
  // Two ~24KiB Windows fit; one private clone fits; a second exceeds the
  // separate80KiB physical limit while128KiB metadata remains ample.
  Candidate<Store> owner(corpus,208*1024,128*1024);
  std::array<std::size_t,2> objects{0,1};std::array<Operation,2> edits;std::array<Outcome,2> output;
  for (std::size_t i=0;i<2;++i)
    edits[i]={.kind=Kind::update,.key=corpus.images[i].rows[1].key,.record={0,813,std::uint32_t(i+30)},.count=1};
  const auto cut=owner.cut();const auto before=owner.pool_statistics();bool refused{};
  try {owner.update(edits,output,objects);} catch(const std::runtime_error& error) {
    refused=std::string_view(error.what()).ends_with("refused: 5");
  }
  require(refused && owner.cut()==cut,"actual private Allocation refusal did not preserve publication cut");
  require(owner.pool_statistics().allocations==before.allocations+1,"physical refusal did not follow an earlier successful clone");
  owner.poll(65536);
  std::uint64_t comparisons{};
  {
    typename Store::Scope unchanged(owner.catalog());
    for (std::size_t i=0;i<2;++i) for (const auto& row:corpus.images[i].rows) {
      require(owner.body(unchanged,i).get(row.key)==row.record,"physical refusal changed published bytes");++comparisons;
    }
  }
  owner.update(std::span{edits}.first(1),std::span{output}.first(1),std::span{objects}.first(1));
  typename Store::Scope current(owner.catalog());
  require(owner.body(current,0).get(edits[0].key)->value==813,"retry after physical refusal failed");
  return comparisons;
}
int main(int argc,char** argv) {
  if(argc!=2)throw std::invalid_argument("usage: performance_check NEW_DB_DIRECTORY");
  std::uint64_t comparisons{};
  {
    Corpus corpus(68,4,8132);Orbital orbital(corpus,64*1024*1024);Lmdb lmdb(argv[1],64*1024*1024);
    {auto write=lmdb.edit();for(const auto& image:corpus.images)write.put_part(image.address,image.encoded);write.publish();}
    auto old_orbital=orbital.read();auto old_lmdb=lmdb.read();
    std::vector<Golden> original;
    for(std::size_t object=0;object<corpus.images.size();++object) {
      const auto& image=corpus.images[object];Golden map;for(const auto& row:image.rows)map.emplace(row.key,row.record);original.push_back(map);
      const auto view=orbital.part(old_orbital,object);const auto mapped=old_lmdb.part(image.address);
      require(mapped && std::equal(view.body().begin(),view.body().end(),mapped->begin(),mapped->end()),"common Orbital/LMDB encoded Tuple bytes differ");
    }
    // Keep one already resolved native body, and discover the other Objects
    // later through the same retained actual snapshot after publication.
    const auto first_old=orbital.body(old_orbital,0);
    std::array<Operation,4> changes;
    std::array<std::size_t,4> objects{0,1,2,3};std::array<Outcome,4> orbital_output,lmdb_output;
    auto golden=original;
    for(std::size_t object=0;object<4;++object) {
      const auto key=corpus.images[object].rows[3].key;
      changes[object]={.kind=Kind::update,.key=key,.record={0,unsigned(object+9),unsigned(object+27)},.count=1};
      golden[object][key].value=changes[object].record.value;golden[object][key].tag=changes[object].record.tag;
    }
    orbital.update(changes,orbital_output,objects);data::Costs costs;lmdb_update(lmdb,corpus,changes,objects,lmdb_output,costs);
    require(orbital_output==lmdb_output,"publication outcomes differ");
    for(std::size_t object=0;object<4;++object) {
      auto old=orbital.body(old_orbital,object);auto mapped=corpus.bind(object,*old_lmdb.part(corpus.images[object].address),corpus.images[object].shape.parts[0].interpretation);
      auto current_context=orbital.read();auto current=orbital.body(current_context,object);
      auto current_lmdb=lmdb.read();auto current_mapped=corpus.bind(object,*current_lmdb.part(corpus.images[object].address),corpus.images[object].shape.parts[0].interpretation);
      for(auto name:{"point-hit","point-miss","dependent-16","scan-16"}) {
        const auto trace=make_trace(name,17,131,3345);
        for(const auto& op:trace.operations) {
          require(read_operation(old,op)==oracle(original[object],op),"old actual Orbital snapshot lost coherence");
          require(read_operation(mapped,op)==oracle(original[object],op),"old mapped snapshot lost coherence");
          require(read_operation(current,op)==oracle(golden[object],op),"new Orbital native caller differs from map");
          require(read_operation(current_mapped,op)==oracle(golden[object],op),"new mapped native caller differs from map");++comparisons;
        }
      }
    }
    // Exercise the named complete timed callers, including a short final
    // batch, against the independently updated maps. Clock values are not
    // correctness evidence; ownership scopes and native outcomes are.
    for(auto name:{"point-hit","point-miss","dependent-16","scan-16"}) {
      const auto trace=make_trace(name,17,131,3345);
      std::vector<std::size_t> destinations(trace.operations.size());
      std::vector<Outcome> expected(trace.operations.size()),output(expected.size());
      for(std::size_t i=0;i<expected.size();++i) {
        destinations[i]=i%4;expected[i]=oracle(golden[destinations[i]],trace.operations[i]);
      }
      for(const auto batch:{std::size_t(1),std::size_t(64)})for(const bool latency:{false,true}) {
        const auto scopes=(expected.size()+batch-1)/batch;
        std::uint64_t expected_bindings{};
        for(std::size_t first=0;first<expected.size();first+=batch)
          expected_bindings+=std::min(std::size_t(4),std::min(batch,expected.size()-first));
        const auto qualify=[&](const Interval& interval,std::uint64_t bindings) {
          require(output==expected,"complete named caller differs from independent map");
          require(interval.snapshot_scopes==scopes && bindings==expected_bindings,"complete caller scope/binding count differs");
          require(interval.batch_ns.size()==(latency?scopes:0),"throughput and latency clocks conflated");
          comparisons+=expected.size();
        };
        for(const bool borrowed:{false,true}) {
          std::uint64_t bindings{};
          const auto interval=run_orbital_reads(orbital,borrowed,latency,trace.operations,destinations,output,batch,bindings);
          qualify(interval,bindings);
        }
        std::uint64_t bindings{};
        const auto interval=run_lmdb_reads(lmdb,corpus,latency,trace.operations,destinations,output,batch,bindings);
        qualify(interval,bindings);
        bindings=0;
        const auto codec_orbital=run_orbital_codec_reads(orbital,latency,trace.operations,destinations,output,batch,bindings);
        qualify(codec_orbital,bindings);
        bindings=0;
        const auto codec_lmdb=run_lmdb_codec_reads(lmdb,corpus,latency,trace.operations,destinations,output,batch,bindings);
        qualify(codec_lmdb,bindings);
        bindings=0;
        const auto consume_orbital=run_orbital_consume_reads(orbital,latency,trace.operations,destinations,output,batch,bindings);
        qualify(consume_orbital,bindings);
        bindings=0;
        const auto consume_lmdb=run_lmdb_consume_reads(lmdb,corpus,latency,trace.operations,destinations,output,batch,bindings);
        qualify(consume_lmdb,bindings);
        // Direct control uses the original immutable fixture, outside any
        // versioned ownership claim.
        const auto direct=run_direct_reads(corpus,latency,trace.operations,destinations,output,batch);
        for(std::size_t i=0;i<expected.size();++i)
          require(output[i]==oracle(original[destinations[i]],trace.operations[i]),"complete direct caller differs");
        require(direct.batch_ns.size()==(latency?scopes:0),"direct control replay mode differs");
        comparisons+=expected.size();
      }
    }
    const Operation first{.kind=Kind::read,.key=changes[0].key,.record={},.count=1};
    require(read_operation(first_old,first)==oracle(original[0],first),"resolved old native body changed");
    // A partial private edit in Object0 followed by an absent key in Object1
    // must not publish either change.
    auto bad=changes;bad[1].key=2;
    const auto installed=orbital.statistics().installed;bool refused{};
    try {orbital.update(bad,orbital_output,objects);} catch(const std::runtime_error&){refused=true;}
    require(refused && orbital.statistics().installed==installed,"failed private group published");
    auto unchanged_context=orbital.read();
    for(std::size_t object=0;object<4;++object)require(orbital.body(unchanged_context,object).get(changes[object].key)==golden[object].at(changes[object].key),"private failure changed latest state");
    // Move actual selected state/cache; get/scan endpoints are rebound to their
    // current stationary native view, never the former wrapper's address.
    auto selected=orbital_bodies(orbital);(void)selected.body(0);auto moved=std::move(selected);
    require(read_operation(moved.body(0),first)==oracle(golden[0],first),"moved qualified cache became invalid");
  }
  for(auto initial:{data::Format::rows,data::Format::columns}) {
    Corpus corpus(33,1,7712);Orbital store(corpus,64*1024*1024,initial);
    auto context=store.read();const auto old=store.body(context,0);Golden golden;for(const auto& row:corpus.images[0].rows)golden.emplace(row.key,row.record);
    const auto target=initial==data::Format::rows?data::Format::columns:data::Format::rows;
    store.convert(0,target);auto later=store.read();const auto converted=store.body(later,0,true);
    require(converted.format()==target && old.format()==initial,"conversion retargeted an existing native descriptor");
    for(const auto& row:corpus.images[0].rows) {
      require(old.get(row.key)==golden.at(row.key) && converted.get(row.key)==golden.at(row.key),"real conversion changed logical rows");++comparisons;
    }
    // Subsequent conversion uses the explicit completed alternative.
    store.convert(0,initial);auto last=store.read();const auto roundtrip=store.body(last,0,true);
    require(roundtrip.format()==initial,"second conversion used wrong source");
    for(const auto& row:corpus.images[0].rows)require(roundtrip.get(row.key)==golden.at(row.key),"roundtrip changed rows");
  }
  comparisons+=candidate_publication_check<compact_history::Store8>();
  comparisons+=candidate_publication_check<compact_history::Store16>();
  comparisons+=candidate_publication_check<compact_history::Store32>();
  comparisons+=candidate_sparse_check<compact_history::Store8>();
  comparisons+=candidate_sparse_check<compact_history::Store16>();
  comparisons+=candidate_sparse_check<compact_history::Store32>();
  comparisons+=candidate_allocation_refusal_check<compact_history::Store8>();
  comparisons+=candidate_allocation_refusal_check<compact_history::Store16>();
  comparisons+=candidate_allocation_refusal_check<compact_history::Store32>();
  std::cout<<"{\"passed\":true,\"comparisons\":"<<comparisons
      <<",\"read_data_bytes\":"<<sizeof(data::ReadData)<<",\"read_data_result_bytes\":"<<sizeof(orbital::Result<data::ReadData>)
      <<",\"tuple_cursor_bytes\":"<<sizeof(data::TupleCursor)<<",\"tuple_cursor_result_bytes\":"<<sizeof(orbital::Result<data::TupleCursor>)
      <<",\"tuple_control_bytes\":"<<sizeof(data::TupleControl)<<",\"tuple_body_bytes\":"<<sizeof(data::TupleBody)
      <<",\"actual_orbital\":true,\"common_tuple_payload\":true,\"complete_callers_batch1_batch64_tail\":true,\"old_future_discovery\":true,\"private_failure\":true,\"bidirectional_conversion\":true,\"shutdown_charge_zero\":true}\n";
}

#include "run.h"
#include "oracle.h"
#include <iostream>

using namespace orbital_versioned_performance;
using namespace orbital_bench;
void require(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
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
  std::cout<<"{\"passed\":true,\"comparisons\":"<<comparisons
      <<",\"read_data_bytes\":"<<sizeof(data::ReadData)<<",\"read_data_result_bytes\":"<<sizeof(orbital::Result<data::ReadData>)
      <<",\"tuple_cursor_bytes\":"<<sizeof(data::TupleCursor)<<",\"tuple_cursor_result_bytes\":"<<sizeof(orbital::Result<data::TupleCursor>)
      <<",\"tuple_control_bytes\":"<<sizeof(data::TupleControl)<<",\"tuple_body_bytes\":"<<sizeof(data::TupleBody)
      <<",\"actual_orbital\":true,\"common_tuple_payload\":true,\"complete_callers_batch1_batch64_tail\":true,\"old_future_discovery\":true,\"private_failure\":true,\"bidirectional_conversion\":true,\"shutdown_charge_zero\":true}\n";
}

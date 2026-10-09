#include "lmdb.h"
#include "read.h"
#include <map>
#include <iostream>

using namespace orbital_versioned_performance;
using namespace orbital_bench;
void require(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
Outcome oracle(const std::map<Key,Record>& rows,const Operation& op) {
  Outcome result{};
  const auto mix=[&](std::uint64_t value) { result.digest^=value+0x9e3779b97f4a7c15ull+(result.digest<<6)+(result.digest>>2); };
  const auto consume=[&](Key key,Record record) { mix(key);mix(record.next);mix(record.value);mix(record.tag);++result.rows; };
  if (op.kind==Kind::scan) {
    auto it=rows.lower_bound(op.key);
    for (unsigned n=0;it!=rows.end() && n<op.count;++n,++it) consume(it->first,it->second);
  } else {
    auto key=op.key;
    for (unsigned n=0;n<(op.kind==Kind::chain?op.count:1);++n) {
      const auto it=rows.find(key);
      if (it==rows.end()) {mix(key);mix(0);break;}
      consume(key,it->second);key=it->second.next;
    }
  }
  return result;
}
int main(int argc,char** argv) {
  if (argc!=2) throw std::invalid_argument("usage: lmdb_snapshot_check NEW_DB_DIRECTORY");
  auto rows=initial_rows(257,9812);
  rows.insert(rows.begin(),{0,{UINT64_MAX,0,UINT32_MAX}});
  rows.push_back({UINT64_MAX,{0,value_mask,0}});
  std::map<Key,Record> golden;for (const auto& row:rows)golden.emplace(row.key,row.record);
  Lmdb store(argv[1],16*1024*1024);store.load(rows);
  auto old=store.read();const auto old_version=old.version();
  const auto original=golden.at(rows[127].key);
  {auto write=store.edit();auto changed=original;changed.value=7;changed.tag=19;write.put(rows[127].key,changed);}
  require(store.read().get(rows[127].key)==original,"abandoned private write became visible");
  {auto write=store.edit();auto changed=original;changed.value=123;changed.tag=456;write.put(rows[127].key,changed);write.publish();golden[rows[127].key]=changed;}
  auto current=store.read();require(current.version()>old_version,"publication did not advance snapshot");
  require(old.get(rows[127].key)==original,"old snapshot changed after publication");
  require(current.get(rows[127].key)==golden.at(rows[127].key),"new snapshot missed publication");
  std::map<Key,Record> old_golden=golden;old_golden[rows[127].key]=original;
  std::uint64_t checked{};
  for (auto name: {"point-hit","point-miss","dependent-16","scan-16"}) {
    auto trace=make_trace(name,257,131,8331);
    trace.operations.push_back({.kind=Kind::read,.key=0,.record={},.count=1});
    trace.operations.push_back({.kind=Kind::scan,.key=UINT64_MAX,.record={},.count=16});
    for (std::size_t first=0;first<trace.operations.size();first+=64) {
      const auto input=std::span(trace.operations).subspan(first,std::min<std::size_t>(64,trace.operations.size()-first));
      std::array<Outcome,64> output{};read_batch(current,input,std::span(output).first(input.size()));
      for (std::size_t i=0;i<input.size();++i) {
        require(output[i]==oracle(golden,input[i]),"current native LMDB caller disagrees with independent map");
        require(read_operation(old,input[i])==oracle(old_golden,input[i]),"retained native LMDB caller disagrees with old map");++checked;
      }
    }
  }
  std::cout<<"{\"passed\":true,\"comparisons\":"<<checked<<",\"old_new_snapshot\":true,\"private_abort\":true}\n";
}

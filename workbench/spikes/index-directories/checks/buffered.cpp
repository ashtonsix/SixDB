#include "buffered_producer.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

namespace {
using namespace index_spike;
using namespace index_spike::driver;
void check(bool x,const char* why) {
  if (x) return;
  std::fprintf(stderr,"buffered producer: %s\n",why); std::abort();
}
template<class T> T take(orbital::Result<T> r) {
  check(bool(r),"actual module refusal"); return std::move(*r);
}
void done(orbital::Result<void> r) { check(bool(r),"producer refused valid operation"); }
using Rows=std::array<Row,64>;
Rows initial(unsigned n) {
  Rows rows{};
  for (unsigned r=0;r<n;++r) for (unsigned f=0;f<8;++f) rows[r][f]=(r+1)*UINT64_C(0x200000000000000)+f;
  rows[0][3]=7; if (n>1) rows[1][3]=11;
  return rows;
}
FieldSummary expected(const Rows& rows,unsigned n,unsigned field) {
  std::uint64_t lo=UINT64_MAX,hi=0; Sum sum=0;
  for (unsigned r=0;r<n;++r) { lo=std::min(lo,rows[r][field]); hi=std::max(hi,rows[r][field]); sum+=rows[r][field]; }
  return {n,lo,hi,std::uint64_t(sum),std::uint64_t(sum>>64),n};
}
void oracle(BufferedProducer& p,const Store::Scope& scope,const Rows& rows) {
  Sum sum=0;
  for (unsigned r=0;r<p.size();++r) {
    check(p.row(scope,r)==rows[r],"all8 logical fields / late selected prefix");
    check(p.field3(scope,r)==rows[r][3],"projected field3 differs from full row"); sum+=rows[r][3];
  }
  check(p.sum(scope)==sum,"absolute SUM128 or lazy sum lost a contribution");
  auto reader=p.reader(scope);
  const std::array<std::array<unsigned,2>,6> ranges{{{0,UINT32_MAX},{0,15},
      {14,32},{63,63},{64,65},{1,0}}};
  for (const auto& range:ranges) for (const auto mask:{UINT64_C(0),UINT64_MAX,
      UINT64_C(0x9249249249249249),UINT64_C(0x8000000000004002)}) {
    std::uint64_t wanted=0,selected=0;
    for (unsigned r=0;r<p.size();++r)
      if (r>=range[0] && r<=range[1] && ((mask>>r)&1)) {
        std::uint64_t digest=0; for (auto word:rows[r]) digest^=word;
        wanted+=digest; ++selected;
      }
    const auto actual=reader.range(range[0],range[1],mask);
    check(actual.sum==wanted && actual.selected==selected,
        "same-Scope masked range lost base/pending/fold/old-cut coherence");
  }
  for (unsigned f=0;f<8;++f) for (unsigned l=0;l<4;++l) {
    if (l<2) check(p.layer_summary(scope,l,0,f)==expected(rows,p.size(),f),"upper summary count/min/max/SUM128/nn");
    else for (unsigned r=0;r<p.size();++r) {
      Rows one{}; one[0]=rows[r];
      check(p.layer_summary(scope,l,std::uint16_t(l==2?r:r<<8),f)==expected(one,1,f),"middle-layer local summary / dirty descent");
    }
  }
}
void transitions(loom::Worker& worker,BufferPolicy policy) {
  auto rows=initial(2); BufferedProducer p(worker,policy,std::span(rows).first(2),2048);
  std::vector<std::unique_ptr<Store::Scope>> cuts;
  std::vector<Rows> answers;
  auto keep=[&] { cuts.push_back(std::make_unique<Store::Scope>(p.store())); answers.push_back(rows); };
  keep(); const auto dirs=p.directories(*cuts[0]);
  auto write=[&](unsigned r,std::uint64_t after) {
    const std::array commands{Command{r,after}};
    done(p.update(commands)); rows[r][3]=after; keep();
  };
  write(0,9); write(1,13); write(0,6);
  check(p.directories(*cuts.back())==dirs,"ordinary updates changed a directory");
  done(p.compact_row(0)); keep();
  const auto partial=p.directories(*cuts.back());
  for (unsigned l=0;l<4;++l) check(partial[l]==dirs[l],"partial port changed an upper directory");
  check(partial[4]!=dirs[4],"partial port omitted actual moved Part locator");
  for (const auto fault:{Fault::capture,Fault::prepare,Fault::stale}) {
    const std::array rejected{Command{1,999}};
    check(!p.update(rejected,fault),"actual capture/prepare/stale refusal not reached");
    Store::Scope current(p.store()); oracle(p,current,rows);
  }
  write(1,15); write(0,8); // partial P fold must not lose Q or double-apply P
  const auto cut_before_noop=p.store().borrow().cut;
  const auto counters=p.counters();
  const std::array no_op{Command{0,9},Command{0,8}};
  done(p.update(no_op));
  check(p.store().borrow().cut==cut_before_noop && p.counters().captures==counters.captures,
      "coalesced no-op captured or published");
  const std::array repeats{Command{0,9},Command{1,16},Command{0,6},Command{1,11}};
  done(p.update(repeats)); rows[0][3]=6; rows[1][3]=11; keep();
  done(p.compact_all()); keep();
  write(0,UINT64_MAX); write(1,UINT64_MAX);
  done(p.compact_all()); keep();
  // Repeated back-to-back updates force real automatic full folds in both
  // append formats; prefix copies after refusal must not replay stale footers.
  for (unsigned i=0;i<100;++i) write(i%2,31+i);
  p.store().drain();
  for (unsigned i=0;i<cuts.size();++i) oracle(p,*cuts[i],answers[i]);
  check(p.counters().folds>=2,"full compaction was not exercised");
  if (policy!=BufferPolicy::eager) check(p.counters().rotations>=1,"refused sealed tail did not rotate");
}
void atomic64(loom::Worker& worker,BufferPolicy policy) {
  auto rows=initial(64); BufferedProducer p(worker,policy,rows);
  Store::Scope old(p.store()); // first actual read after64-item publication
  std::array<Command,64> commands;
  for (unsigned i=0;i<64;++i) commands[i]={i%17,UINT64_MAX-i};
  done(p.update(commands));
  for (const auto& c:commands) rows[c.row][3]=c.after;
  Store::Scope fresh(p.store());
  for (const auto fault:{Fault::capture,Fault::prepare,Fault::stale}) {
    check(!p.compact_all(fault),"partial full-fold candidate refusal not reached");
    p.store().drain();
    oracle(p,old,initial(64)); oracle(p,fresh,rows);
    Store::Scope current(p.store()); oracle(p,current,rows);
  }
  done(p.compact_row(3)); done(p.compact_all()); p.store().drain();
  oracle(p,old,initial(64)); oracle(p,fresh,rows);
  Store::Scope final(p.store()); oracle(p,final,rows);
}
void guarded() {
  const auto page=std::size_t(sysconf(_SC_PAGESIZE));
  auto* area=static_cast<std::byte*>(mmap(nullptr,page*5,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
  check(area!=MAP_FAILED,"guard mapping");
  check(!mprotect(area+page,page,PROT_READ|PROT_WRITE) && !mprotect(area+page*3,page,PROT_READ|PROT_WRITE),"guard protection");
  idx_log_copy8(nullptr,nullptr,0);
  for (unsigned bytes=8;bytes<=512;bytes+=8) {
    auto* source=area+page*2-bytes; auto* destination=area+page*4-bytes;
    for (unsigned i=0;i<bytes;++i) source[i]=std::byte(i*13+71);
    std::memset(destination-8,0x49,bytes+8);
    idx_log_copy8(destination,source,bytes);
    check(!std::memcmp(destination,source,bytes),"exact vector copy8 body/tail");
    for (int i=-8;i<0;++i) check(destination[i]==std::byte{0x49},"vector copy8 leading canary");
  }
  // Last event / copy ends at the inaccessible boundary. The leading canary
  // catches underwrites; no rounded vector tail is allowed into sealed bytes.
  auto* src=area+page*2-192; auto* dst=area+page*4-192;
  std::memset(src,0,192); std::memset(dst-8,0x69,200);
  idx_log_append24(src+8,1,1,UINT64_MAX);
  check(idx_log_find(src,32,1,7)==UINT64_MAX && idx_log_find(src,32,0,7)==7,"plain exact-end find");
  std::array<std::uint64_t,64> values{}; idx_log_apply(src,32,values.data());
  check(values[1]==UINT64_MAX && !values[0],"plain apply original coordinates");
  idx_log_copy8(dst,src,192); check(!std::memcmp(src,dst,192),"copy8 complete exact-end bytes");
  for (unsigned i=0;i<8;++i) check(dst[-8+int(i)]==std::byte{0x69},"copy underwrite");
  // Footer starts at184, deliberately only8-aligned. Last row offset is read
  // exactly; event24 stores must not overlap the previous160-byte footer.
  auto* indexed=area+page*2-344; std::memset(indexed,0,344);
  idx_log_append24(indexed+160,63,1,17);
  const std::uint16_t offset=20;
  std::memcpy(indexed+184+24+63*2,&offset,2);
  check(idx_log_index_find(indexed,344,63,7)==17 && idx_log_index_find(indexed,344,0,7)==7,
      "indexed offset / default /8-aligned footer");
  check(!munmap(area,page*5),"guard unmap");
}
}
int main() {
  guarded(); loom::Worker worker;
  check(bool(loom::adopt(worker)),"Worker adoption");
  auto arena=take(orbital::allocation::open_arena(64u<<20));
  check(bool(loom::configure(worker,std::move(arena),0)),"Worker configuration");
  for (const auto policy:{BufferPolicy::plain,BufferPolicy::indexed,BufferPolicy::eager}) {
    transitions(worker,policy); atomic64(worker,policy);
  }
  while (true) {
    const auto result=loom::shutdown(worker);
    check(result.kind()!=orbital::Progress::Kind::failed,"physical shutdown failed");
    if (result.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
  std::puts("PASS: plain/indexed/eager real publication,8-field/5-layer/SUM128 oracles, late cuts, partial/full folds, refusal/rotation, repeated atomic64, exact-end guards and physical shutdown");
}

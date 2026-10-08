#include "probe.h"
#include <loom/ring.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <array>
#include <cstring>
#include <cstdlib>
#ifndef CB_METADATA
#define CB_METADATA 8
#endif

namespace cb {
namespace tp = ikea::tuplepack;
using Carrier = tp::native::pipeline_values;
using Vector = Carrier::vector;
using Reader = tp::native_reader<64, tp::byte>;
namespace {
void check(bool x) { if (!x) std::abort(); }
template<std::size_t> using Scalar = Word;
template<std::size_t> using Part = Vector;
template<unsigned Copies, unsigned Metadata, class G, class V> struct Family;

template<unsigned Copies, unsigned Metadata, std::size_t... G, std::size_t... V>
struct Family<Copies, Metadata, std::index_sequence<G...>, std::index_sequence<V...>> {
  struct Job {
    const Reader* reader{};
    std::array<Carrier, Copies> results;
    std::array<Word, Metadata> metadata;
    Word sum{};
  };
  using Ring = loom::Ring<Job*, Word, Word, Word, Scalar<G>..., Part<V>...>;

  [[gnu::noinline]] static LOOM_NATIVE_CC void callback(Ring* ring, typename Ring::Slot* slot,
      Job* job, Word row, Word remaining, Word sum, Scalar<G>... metadata, Part<V>... vectors) {
    const auto decoded=job->reader->get_unchecked(row);
    std::array<Vector, Copies * Carrier::parts> parts{vectors...};
    [&]<std::size_t... C>(std::index_sequence<C...>) __attribute__((always_inline)) {
      ([&]() __attribute__((always_inline)) {
        Carrier input;
        for(unsigned p=0;p<Carrier::parts;++p) input.v[p]=parts[C*Carrier::parts+p];
        const auto updated=Carrier::from(tp::native::bit_or(input.get(), decoded));
        for(unsigned p=0;p<Carrier::parts;++p) parts[C*Carrier::parts+p]=updated.v[p];
        if(remaining==1) job->results[C]=updated;
      }(), ...);
    }(std::make_index_sequence<Copies>{});
    sum+=tp::native::word<0>(decoded)+row;
    if(remaining==1) {
      job->sum=sum;
      job->metadata={(metadata+G+1)...};
      ring->complete(*slot);
    }
    [[clang::musttail]] return Ring::yield(ring,slot,job,(row+1)&15,remaining-1,sum,
      (metadata+G+1)...,parts[V]...);
  }

  // Vector arguments are truly live across the ordinary entry and native spin.
  // This is an inspection control, rather than deleting the required bridge.
  [[gnu::noinline]] static Word spin_live(Ring* ring, Part<V>... vectors) {
    const auto visits=ring->spin({31});
    std::array<Vector,Copies*Carrier::parts> values{vectors...};
    Word sum=visits;
    [&]<std::size_t... C>(std::index_sequence<C...>) __attribute__((always_inline)) {
      ([&]() __attribute__((always_inline)) {
        Carrier p; for(unsigned i=0;i<Carrier::parts;++i) p.v[i]=values[C*Carrier::parts+i];
        sum+=tp::native::word<0>(p.get());
      }(),...);
    }(std::make_index_sequence<Copies>{});
    return sum;
  }
  [[gnu::noinline]] static bool enqueue(Ring& ring, Job* job, Word row, Word rounds) {
    return ring.push(callback,typename Ring::Arguments{job,row,rounds,0,Word(G+11)...,Part<V>{}...});
  }
  static void run(loom::Worker& worker,const Reader& reader,const tp::byte* source,
      std::size_t active, std::size_t jobs, Word rounds) {
    Ring ring;
    check(bool(ring.configure(worker,{active,jobs-active})));
    std::array<Job,5> work;
    for(std::size_t j=0;j<jobs;++j) { work[j].reader=&reader; check(enqueue(ring,&work[j],j,rounds)); }
    while(ring.pending()) spin_live(&ring,Part<V>{}...);
    for(std::size_t j=0;j<jobs;++j) {
      std::array<tp::byte,64> expected{}; Word sum=0;
      for(Word step=0;step<rounds;++step) {
        const auto row=(j+step)&15; Word first=0;
        for(unsigned i=0;i<64;++i) {
          auto value=source[row*64+i]>>1; expected[i]|=value;
          if(i<8) first|=Word(value)<<(8*i);
        }
        sum+=first+row;
      }
      check(work[j].sum==sum);
      for(unsigned c=0;c<Copies;++c) {
        std::array<tp::byte,64> actual;
        tp::native::store_packet(actual.data(),work[j].results[c].get());
        check(actual==expected);
      }
      for(unsigned g=0;g<Metadata;++g) check(work[j].metadata[g]==g+11+rounds*(g+1));
    }
    check(ring.counters().completed==jobs && ring.counters().visits==jobs*rounds);
    check(ring.counters().promoted==jobs-active);
    check(bool(ring.close()));
  }
};

template<unsigned Copies,unsigned Metadata>
using F = Family<Copies,Metadata,std::make_index_sequence<Metadata>,
  std::make_index_sequence<Copies*Carrier::parts>>;
}

unsigned ring_checks() {
  auto arena=orbital::allocation::open_arena(8*1024*1024); check(bool(arena));
  loom::Worker worker; check(bool(loom::adopt(worker))); check(bool(loom::configure(worker,*arena,0)));
  auto storage=loom::allocate(worker,{.bytes=16*64}); check(bool(storage));
  auto held=loom::hold(*storage,{0,16*64}); check(bool(held));
  auto* source=reinterpret_cast<tp::byte*>(storage->address);
  for(unsigned row=0;row<16;++row) for(unsigned i=0;i<64;++i) source[row*64+i]=(row*17+i*13)&255;
  std::array<tp::code,64> codes; std::array<tp::byte,64> map;
  for(unsigned i=0;i<64;++i) { codes[i]={tp::byte(i),1,7}; map[i]=i; }
  const auto layout=tp::layout::make(64,codes); check(bool(layout));
  const auto plan=tp::reader<64>::make(*layout,map); check(bool(plan));
  const auto view=tp::view::bind(*layout,{source,16*64},16,64); check(bool(view));
  const auto operation=tp::bind_reader(*plan,*view); check(bool(operation));
  const Reader reader(*operation); check(bool(reader.admit(15)));
  unsigned cases=0;
  for(auto active:{std::size_t(2),std::size_t(5)}) {
    F<1,0>::run(worker,reader,source,active,5,23); ++cases;
    F<1,CB_METADATA>::run(worker,reader,source,active,5,23); ++cases;
    F<2,0>::run(worker,reader,source,active,5,23); ++cases;
    F<3,0>::run(worker,reader,source,active,5,23); ++cases;
  }
  loom::release(*held); loom::release(*storage);
  for(;;) {
    const auto progress=loom::shutdown(worker);
    check(progress.kind()!=orbital::Progress::Kind::failed);
    if(progress.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
  return cases;
}
} // namespace cb

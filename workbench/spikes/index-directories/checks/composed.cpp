#include "group_fixture.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using namespace index_spike;
void check(bool good,const char* why) {
  if (good) return;
  std::fprintf(stderr,"composed producer: %s\n",why); std::abort();
}
template<class T> T take(orbital::Result<T> result) {
  check(bool(result),"actual module refused material"); return std::move(*result);
}
using Body=std::array<std::byte,64>;
template<class T> T load(const std::byte* bytes,std::size_t offset) {
  T value; std::memcpy(&value,bytes+offset,sizeof(value)); return value;
}
template<class T> void put(std::byte* bytes,std::size_t offset,T value) {
  std::memcpy(bytes+offset,&value,sizeof(value));
}
struct Advice { std::uint64_t generation; bool dirty{}; };
struct Summary {
  std::uint64_t count;
  std::int64_t min,max,sum;
  std::uint64_t not_null;
  bool operator==(const Summary&) const=default;
};
static_assert(sizeof(Summary)==40);

// A bounded TWO-RECORD producer/oracle, separate from directory algorithms.
// J=K=4; physical keys0 and1<<16, logical keys0 and1, slot0 in both L4
// segments. All five layers exist. This tests actual composition, not a chosen
// production delta codec, full row engine, NULL policy or append performance.
class Producer {
  static constexpr std::uint64_t correction_space=0x434f5252;
  static constexpr std::uint64_t summary_space=0x53554d53;
  static constexpr std::uint64_t upper_directory_space=0x55504449;
  static constexpr Format correction_format{{64},1,correction_space};
  static constexpr Format data_format{{64},1,GroupFixture::material_space};
  static constexpr Format directory_format{{192},1,GroupFixture::directory_space};
  static constexpr Format top_format{{64},1,upper_directory_space};
  static constexpr Format group_format{{128},1,upper_directory_space};
  static constexpr Format summary_one{{64},1,summary_space};
  static constexpr Format summary_two{{128},1,summary_space};
  loom::Worker& worker_;
  // Monotone advice is outside sealed body/padding. These stable members
  // outlive every physical Control/Scope in this finite check; generations are
  // never reused. Escaping lifetimes would need actual retained proof ownership.
  std::array<Advice,3> advice_{{{1,false},{1,false},{3,false}}};
  bool root_dirty_{};
  std::array<Record,2> input_;
  GroupFixture fixture_;
  static orbital::ObjectId correction(unsigned row) { return {correction_space,row+1}; }
  static orbital::ObjectId root_correction() { return {correction_space,100}; }
  static orbital::ObjectId upper(unsigned layer) { return {upper_directory_space,layer+1}; }
  static std::array<Record,2> initial(std::array<Advice,3>& advice) {
    std::array<Record,2> records{};
    for (unsigned i=0;i<2;++i) {
      auto& row=records[i]; row.prefix=i;
      put(row.descriptor.data(),48,std::uint32_t{1}); // native base representation
      put(row.material.data(),0,std::int64_t(i?11:7));
      put(row.material.data(),8,&advice[i]);
      put(row.material.data(),16,std::uint64_t{1});
      put(row.material.data(),24,std::uint64_t(i)<<16); // physical key
      put(row.material.data(),32,std::uint64_t{1});
    }
    return records;
  }
  Store::Owner finish(orbital::ObjectId id,const Format& format,
      std::span<const std::byte> source) {
    check(source.size()==format.bytes[0],"constructor/body shape mismatch");
    auto allocation=take(loom::allocate(worker_,{.bytes=source.size(),.alignment=64}));
    // All native bytes and padding initialized privately before capture.
    std::memcpy(allocation.address,source.data(),source.size());
    const Control control{&fixture_.metadata(),&format,id.low,0,1};
    const std::array parts{orbital::PartSource{control.id(0),loom::bytes(allocation),{},&allocation}};
    return take(store().capture(control,parts));
  }
  static const std::byte* part(Selection selected) {
    check(bool(selected),"missing selected physical material");
    return take(selected.part(0)).address();
  }
  void publish(const Store::Scope& basis,std::span<const Store::Change> changes) {
    Store::Prepared packet(store());
    check(bool(store().prepare(basis,changes,packet)),"coupled prepare refused");
    check(bool(store().install(packet)),"coupled install refused");
  }
  Lookup locate(const Store::Scope& scope,unsigned row) {
    ReadContext context{GroupFixture::directory_space,&scope,{}};
    const auto view=fixture_.view(context);
    Lookup found; check(idx_asm_group16(&view,row,&found),"L4 segment missing");
    return found;
  }
  const std::byte* base(const Store::Scope& scope,unsigned row) {
    const auto found=locate(scope,row);
    return load<const std::byte*>(found.descriptor,0);
  }
public:
  explicit Producer(loom::Worker& worker)
      : worker_(worker),input_(initial(advice_)),fixture_(worker,input_) {
    std::array<Store::Change,11> changes;
    unsigned n=0;
    for (unsigned row=0;row<2;++row)
      changes[n++]={correction(row),finish(correction(row),correction_format,Body{})};
    changes[n++]={root_correction(),finish(root_correction(),correction_format,Body{})};
    for (unsigned layer=0;layer<4;++layer) {
      std::array<std::byte,128> bytes{};
      const unsigned count=layer<2?1:2;
      put(bytes.data(),0,std::uint16_t(count));
      put(bytes.data(),2,std::uint16_t{0});
      put(bytes.data(),4,std::uint16_t(layer==3?256:1));
      put(bytes.data(),6,std::uint8_t(layer));
      const std::array cells{Summary{2,7,11,18,2},Summary{1,11,11,11,1}};
      const Summary first=layer<2?cells[0]:Summary{1,7,7,7,1};
      put(bytes.data(),16,first);
      if (count==2) put(bytes.data(),56,cells[1]);
      const auto id=orbital::ObjectId{summary_space,layer+1};
      const auto& format=count==1?summary_one:summary_two;
      auto material=finish(id,format,std::span(bytes).first(format.bytes[0]));
      std::array<std::byte,128> directory{};
      const unsigned at=layer<2?0:64;
      if (layer>=2) put(directory.data(),0,std::uint16_t{1});
      put(directory.data(),at,part(material.selection()));
      put(directory.data(),at+48,std::uint32_t(layer+2));
      changes[n++]={id,std::move(material)};
      const auto& dformat=layer<2?top_format:group_format;
      changes[n++]={upper(layer),finish(upper(layer),dformat,
          std::span(directory).first(dformat.bytes[0]))};
    }
    check(n==changes.size(),"initial publication count");
    Store::Scope basis(store()); publish(basis,changes);
  }
  Store& store() { return fixture_.store(); }
  std::int64_t value(const Store::Scope& scope,unsigned row) {
    const auto* bytes=base(scope,row);
    const auto* advice=load<const Advice*>(bytes,8);
    const auto base_value=load<std::int64_t>(bytes,0);
    if (advice->generation==load<std::uint64_t>(bytes,16) && !advice->dirty) return base_value;
    return base_value+load<std::int64_t>(part(scope.bind(correction(row))),0);
  }
  std::int64_t sum(const Store::Scope& scope) {
    // Query explicitly starts in L0's ordinary table; no cross-layer order.
    const auto* directory=part(scope.bind(upper(0)));
    const auto* summary=load<const std::byte*>(directory,0);
    return load<Summary>(summary,16).sum+
        load<std::int64_t>(part(scope.bind(root_correction())),0);
  }
  Summary exact_summary(const Store::Scope& scope) {
    if (!root_dirty_) {
      const auto* directory=part(scope.bind(upper(0)));
      return load<Summary>(load<const std::byte*>(directory,0),16);
    }
    // Dirty extrema cannot prune or return their stale base. This tiny producer
    // chooses exact descent; the sum-only path above uses an invertible delta.
    const auto p=value(scope,0),q=value(scope,1);
    return {2,std::min(p,q),std::max(p,q),p+q,2};
  }
  Summary layer_summary(const Store::Scope& scope,unsigned layer,std::uint16_t position) {
    check(layer<4,"summary layer outside this native representation");
    const std::byte* directory;
    if (layer<2) directory=part(scope.bind(upper(layer)));
    else {
      ReadContext context{upper_directory_space,&scope,{}};
      const NativeRead view{&context,layer+1,0,0,0,0,0,0,0,0};
      directory=idx_asm_group16(&view,0,nullptr);
      check(directory,"middle-layer prefix0 segment missing");
    }
    const auto* bytes=load<const std::byte*>(directory,0);
    check(bytes==part(scope.bind({summary_space,layer+1})),"middle summary locator association");
    const auto n=load<std::uint16_t>(bytes,0);
    unsigned rank=0;
    // The segment's finite native representation has at most2 explicit sorted
    // positions. This is segment projection, not an alternate directory index.
    while (rank<n && load<std::uint16_t>(bytes,2+rank*2)<position) ++rank;
    check(rank<n && load<std::uint16_t>(bytes,2+rank*2)==position,"summary position absent");
    auto result=load<Summary>(bytes,16+rank*40);
    if (layer<2) {
      result.sum+=load<std::int64_t>(part(scope.bind(root_correction())),0);
      if (root_dirty_) {
        const auto p=value(scope,0),q=value(scope,1);
        result.min=std::min(p,q); result.max=std::max(p,q);
      }
    } else {
      const unsigned row=layer==2?position:position/256;
      check(row<2 && result.count==1,"single-record summary domain mismatch");
      // Own SUMMARY base frontier: data_delta[0] can be0 afterP compaction
      // while summary_delta[8] remains-1 against this unchanged summarybase7.
      const auto delta=load<std::int64_t>(part(scope.bind(correction(row))),8);
      result.sum+=delta; result.min+=delta; result.max+=delta;
    }
    return result;
  }
  void write(unsigned row,std::int64_t after) {
    Store::Scope basis(store());
    const auto before=value(basis,row);
    const auto* selected_base=base(basis,row);
    auto* advice=load<Advice*>(selected_base,8);
    advice->dirty=true; root_dirty_=true; // before publication, never cleared
    const auto* old_row=part(basis.bind(correction(row)));
    const auto* old_root=part(basis.bind(root_correction()));
    Body r,g;
    idx_asm_clone_patch64(r.data(),old_row,1,0,
        std::uint64_t(after-load<std::int64_t>(selected_base,0)));
    put(r.data(),8,load<std::int64_t>(old_row,8)+after-before);
    const auto sequence=load<std::uint64_t>(old_root,8)+1;
    put(r.data(),16,sequence);
    idx_asm_clone_patch64(g.data(),old_root,1,0,
        std::uint64_t(load<std::int64_t>(old_root,0)+after-before));
    put(g.data(),8,sequence);
    const std::array changes{
        Store::Change{correction(row),finish(correction(row),correction_format,r)},
        Store::Change{root_correction(),finish(root_correction(),correction_format,g)}};
    publish(basis,changes); // ZERO directory or base/ancestor-summary changes
  }
  void compact_p() {
    Store::Scope basis(store());
    const auto logical=value(basis,0);
    const auto found=locate(basis,0);
    const auto* before=base(basis,0);
    check(before==part(basis.bind(GroupFixture::material_id(0))),"compaction before association");
    Body next;
    idx_asm_clone_patch64(next.data(),before,1,0,std::uint64_t(logical));
    put(next.data(),8,&advice_[2]); put(next.data(),16,std::uint64_t{3});
    auto material=finish(GroupFixture::material_id(0),data_format,next);
    std::array<std::byte,192> directory;
    const Selection leaf{static_cast<Physical::Value*>(found.leaf_selection)};
    idx_asm_clone_patch64(directory.data(),part(leaf),3,64+found.rank*64,
        std::uintptr_t(part(material.selection())));
    auto changed=finish({GroupFixture::directory_space,found.leaf_name},directory_format,directory);
    const auto* old_row=part(basis.bind(correction(0)));
    Body row;
    idx_asm_clone_patch64(row.data(),old_row,1,0,0);
    put(row.data(),16,std::uint64_t{3});
    // summary_delta remains-1 relative to summary base7. Root delta+1 and
    // Q's+2 remain selected and unchanged; P-only incorporation cannot drop Q.
    check(load<std::int64_t>(row.data(),8)==-1,"P summary correction lost");
    const std::array changes{
        Store::Change{GroupFixture::material_id(0),std::move(material)},
        Store::Change{{GroupFixture::directory_space,found.leaf_name},std::move(changed)},
        Store::Change{correction(0),finish(correction(0),correction_format,row)}};
    publish(basis,changes);
  }
  std::array<void*,5> directories(const Store::Scope& scope) {
    std::array<void*,5> result;
    for (unsigned layer=0;layer<4;++layer) result[layer]=scope.bind(upper(layer)).realization;
    result[4]=locate(scope,0).leaf_selection;
    return result;
  }
  std::array<void*,5> current_directories() {
    Store::Scope current(store()); return directories(current);
  }
};
void expect(Producer& producer,const Store::Scope& scope,std::int64_t p,std::int64_t q) {
  check(producer.value(scope,0)==p && producer.value(scope,1)==q,"independent logical row oracle");
  check(producer.sum(scope)==p+q,"fixed-locator authoritative aggregate correction");
  check(producer.exact_summary(scope)==Summary{2,std::min(p,q),std::max(p,q),p+q,2},
      "count/min/max/sum/not-null oracle after dirty descent");
  for (unsigned layer=0;layer<2;++layer)
    check(producer.layer_summary(scope,layer,0)==Summary{2,std::min(p,q),std::max(p,q),p+q,2},
        "merged summary selected correction/extrema domain");
  for (unsigned layer=2;layer<4;++layer) {
    check(producer.layer_summary(scope,layer,0)==Summary{1,p,p,p,1},"P middle-layer summary frontier");
    check(producer.layer_summary(scope,layer,layer==2?1:256)==Summary{1,q,q,q,1},
        "Q middle-layer summary frontier");
  }
}
}
int main() {
  loom::Worker worker; check(bool(loom::adopt(worker)),"Worker adoption");
  auto arena=take(orbital::allocation::open_arena(16u<<20));
  check(bool(loom::configure(worker,std::move(arena),0)),"Worker configuration");
  {
    Producer producer(worker);
    const auto d0=producer.current_directories();
    Store::Scope s0(producer.store()); // deliberately NO first lookup yet
    producer.write(0,9); Store::Scope s1(producer.store());
    check(producer.current_directories()==d0,"ordinary P update rewrote directory");
    producer.write(1,13); Store::Scope s2(producer.store());
    check(producer.current_directories()==d0,"ordinary Q update rewrote directory");
    producer.write(0,6); Store::Scope s3(producer.store());
    check(producer.current_directories()==d0,"ordinary P/Q/P update rewrote directory");
    producer.compact_p(); Store::Scope s4(producer.store());
    const auto d4=producer.current_directories();
    for (unsigned layer=0;layer<4;++layer) check(d4[layer]==d0[layer],"compaction rewrote upper directory");
    check(d4[4]!=d0[4],"moving P base did not publish its changed locator");
    producer.store().drain();
    expect(producer,s0,7,11); expect(producer,s1,9,11);
    expect(producer,s2,9,13); expect(producer,s3,6,13); expect(producer,s4,6,13);
    Store::Scope late4(producer.store());
    producer.write(0,10); Store::Scope s5(producer.store());
    check(producer.current_directories()==d4,"post-compaction ordinary update rewrote directory");
    producer.store().drain();
    expect(producer,late4,6,13); expect(producer,s4,6,13);
    expect(producer,s0,7,11); expect(producer,s5,10,13);
  }
  while (true) {
    const auto p=loom::shutdown(worker);
    check(p.kind()!=orbital::Progress::Kind::failed,"shutdown failed");
    if (p.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
  std::puts("PASS: five-layer native P/Q/P producer, zero-directory updates, fixed-pointer selected corrections, P-only compaction, dirty extrema descent, late snapshot oracles; no timing claim");
}

#include "arena.h"

#include <algorithm>
#include <array>
#include <barrier>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace descriptors;

namespace {
void contents(const Grant& grant, std::uint8_t expected) {
  assert(grant);
  for (std::size_t i=0; i<grant.size(); ++i)
    assert(grant.data()[i] == std::byte{expected});
}
void address_oracle() {
  constexpr std::array<std::size_t,15> a{16,32,48,64,96,128,192,256,384,512,768,1024,1536,2048,3072};
  constexpr std::array<std::size_t,15> b{256,320,384,448,512,640,768,896,1024,1280,1536,1792,2048,2560,3072};
  std::size_t checks=0;
  for (auto palette : {Palette::a,Palette::b})
    for (auto packing : {Packing::packed,Packing::private_large})
      for (auto directory : {Directory::planes,Directory::records}) {
        Arena arena(palette,directory,packing,130,37);
        const auto& g=arena.layout();
        const auto& capacities=palette==Palette::a ? a : b;
        std::array<std::size_t,15> offsets{};
        std::size_t end=0;
        for (unsigned c=0;c<15;++c) {
          if (packing==Packing::private_large && (palette==Palette::b || c>=5))
            while (end%g.page) ++end;
          offsets[c]=end;
          end+=16*capacities[c];
        }
        std::size_t span=end;
        while(span%g.page) ++span;
        assert(span==g.grid_bytes);
        if (palette==Palette::a && g.page==4096) assert(offsets[5]==4096);
        for (std::size_t grid : {0u,1u,63u,64u,129u}) {
          auto head=arena.create(grid,capacities[0],0x5a);
          assert(head && arena_id(head)==37);
          auto* cell=reinterpret_cast<std::uint8_t*>(native(head));
          const auto old=__atomic_load_n(cell,__ATOMIC_RELAXED);
          const auto control_page=native(head)/g.page*g.page;
          const auto local_grid=grid%64;
          std::vector<std::pair<std::uintptr_t,std::uintptr_t>> ranges;
          for(unsigned c=0;c<15;++c) for(unsigned i=0;i<16;++i) {
            __atomic_store_n(cell,std::uint8_t((i<<4)|c),__ATOMIC_RELEASE);
            const auto expected=control_page+g.page+local_grid*span+offsets[c]+i*capacities[c];
            assert(reinterpret_cast<std::uintptr_t>(resolve(head,g))==expected);
            assert(expected%(palette==Palette::a ? 16 : 64)==0);
            ranges.emplace_back(expected,expected+capacities[c]);
            ++checks;
          }
          std::sort(ranges.begin(),ranges.end());
          for(std::size_t i=1;i<ranges.size();++i) assert(ranges[i-1].second<=ranges[i].first);
          __atomic_store_n(cell,old,__ATOMIC_RELEASE);
          arena.retire(head);
        }
      }
  std::cout << "address_oracle checks=" << checks << " supported_finite_coordinates=240\n";
}
void retained_reader() {
  Arena arena(Palette::a,Directory::records,Packing::private_large,1);
  const auto head=arena.create(0,16,0x41);
  auto old=arena.borrow(head);
  auto* address=old.data();
  assert(arena.grow(head,512));
  assert(arena.counters().payload_pages==2);
  contents(old,0x41);
  auto current=arena.borrow(head);
  assert(current.data()!=address && current.generation()!=old.generation());
  for(std::size_t i=0;i<512;++i)
    assert(current.data()[i]==(i<16 ? std::byte{0x41} : std::byte{0}));
  old.cancel();
  assert(arena.counters().payload_pages==2);
  old.complete();
  assert(arena.counters().payload_pages==1);
  current.complete();
  arena.retire(head);
  assert(arena.counters().payload_pages==0);
  std::cout << "retained_reader old_address_stable cancellation_retains_until_completion copied_bytes=16\n";
}
void delayed_thread() {
  Arena arena(Palette::a,Directory::planes,Packing::private_large,1);
  auto head=arena.create(0,48,0x27);
  std::barrier rendezvous(2);
  std::thread reader([&] {
    auto grant=arena.borrow(head);
    rendezvous.arrive_and_wait();
    rendezvous.arrive_and_wait();
    contents(grant,0x27);
    grant.cancel();
    contents(grant,0x27);
  });
  rendezvous.arrive_and_wait();
  assert(arena.replace(head,768,0x39));
  auto now=arena.borrow(head);
  contents(now,0x39);
  rendezvous.arrive_and_wait();
  reader.join();
  now.complete();
  arena.retire(head);
  assert(arena.counters().payload_pages==0);
  std::cout << "delayed_thread old_grant_and_new_binding_coexist\n";
}
void pressure_and_exclusive() {
  Arena arena(Palette::a,Directory::planes,Packing::private_large,1);
  std::array<Head,16> heads{};
  for(auto& head:heads) { head=arena.create(0,64,0x11); assert(head); }
  auto old=arena.borrow(heads[0]);
  assert(!arena.replace(heads[0],64,0x22));
  assert(!arena.try_inplace(heads[0],0x22));
  assert(arena.grow(heads[0],96));
  assert(!arena.replace(heads[0],64,0x22));
  contents(old,0x11);
  old.complete();
  assert(arena.replace(heads[0],64,0x22));
  assert(arena.try_inplace(heads[0],0x33));
  auto updated=arena.borrow(heads[0]); contents(updated,0x33); updated.complete();
  for(auto head:heads) arena.retire(head);
  assert(arena.counters().blocked_moves==2);
  std::cout << "pressure full_class_blocks_retained_same_class_replacement exclusive_gates_active_grants\n";
}
void coarse_scope() {
  for(bool coarse:{false,true}) {
    Arena arena(Palette::a,Directory::records,Packing::private_large,1);
    auto moving=arena.create(0,128,0x4a);
    auto reader_head=arena.create(0,16,0x5b);
    auto reader=arena.borrow(reader_head);
    Scope scope;
    if(coarse) scope=arena.protect(0);
    assert(arena.grow(moving,512));
    assert(arena.counters().payload_pages==(coarse?3u:2u));
    contents(reader,0x5b);
    scope=Scope{};
    assert(arena.counters().payload_pages==2);
    reader.complete(); arena.retire(reader_head); arena.retire(moving);
  }
  std::cout << "coarse_scope unrelated_reader_retains_one_extra_page\n";
}
void append_and_identity() {
  Arena arena(Palette::a,Directory::planes,Packing::private_large,1);
  auto head=arena.create(0,25,0x61);
  auto prefix=arena.borrow(head);
  assert(arena.grow(head,28));
  contents(prefix,0x61);
  auto longer=arena.borrow(head);
  assert(longer.data()==prefix.data() && longer.size()==28);
  for(unsigned i=25;i<28;++i) assert(longer.data()[i]==std::byte{0});
  longer.complete();
  // Identity proof ends head aliases; the old raw grant has a separate lifetime.
  arena.retire(head);
  auto replacement=arena.create(0,16,0x72);
  assert(replacement==head);
  contents(prefix,0x61);
  prefix.complete(); arena.retire(replacement);
  std::cout << "append sealed_prefix_stable identity_reuse_can_precede_old_payload_release\n";
}
void admitted_window() {
  Arena arena(Palette::a,Directory::records,Packing::private_large,1);
  auto undersized=arena.create(0,47,0x62);
  assert(!arena.borrow(undersized,64));
  arena.retire(undersized);
  auto head=arena.create(0,47,0x62,64);
  assert((__atomic_load_n(reinterpret_cast<std::uint8_t*>(native(head)),
                         __ATOMIC_ACQUIRE)&15)==3);
  assert(!arena.borrow(head,65));
  auto window=arena.borrow(head,64);
  assert(window && window.size()==64);
  auto* old=window.data();
  assert(arena.grow(head,50));
  contents(window,0x62);
  auto current=arena.borrow(head);
  assert(current.data()!=old && current.size()==50);
  assert((__atomic_load_n(reinterpret_cast<std::uint8_t*>(native(head)),
                         __ATOMIC_ACQUIRE)&15)==3); // Both homes are class64.
  for(unsigned i=0;i<50;++i)
    assert(current.data()[i]==(i<47?std::byte{0x62}:std::byte{0}));
  current.complete(); window.complete(); arena.retire(head);
  std::cout << "admitted_window 47B_body_with_64B_read_window_survives_same_class_append\n";
}
void native_cow() {
  const auto p=static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  const int fd=memfd_create("descriptor-views",MFD_CLOEXEC);
  assert(fd>=0 && ftruncate(fd,3*p)==0);
  auto* source=static_cast<std::byte*>(mmap(nullptr,3*p,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
  assert(source!=MAP_FAILED);
  std::memset(source,0,3*p);
  std::memset(source+p,0x11,16);
  std::memset(source+p+32,0x44,16);
  source[64]=std::byte{1}; // Demonstrates stale authority if copied with view state.
  source[2*p]=std::byte{1};
  auto* before=static_cast<std::byte*>(mmap(nullptr,2*p,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0));
  auto* after=static_cast<std::byte*>(mmap(nullptr,2*p,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0));
  assert(before!=MAP_FAILED && after!=MAP_FAILED);
  const auto g=geometry(Palette::a,Directory::planes,Packing::private_large,p);
  const Head old_head=reinterpret_cast<Head>(before);
  const Head new_head=reinterpret_cast<Head>(after);
  auto* old_raw=resolve(old_head,g);
  std::memset(after+p+16,0x22,16);
  __atomic_store_n(reinterpret_cast<std::uint8_t*>(after),std::uint8_t{0x10},__ATOMIC_RELEASE);
  assert(resolve(new_head,g)==after+p+16);
  for(unsigned i=0;i<16;++i) {
    assert(old_raw[i]==std::byte{0x11});
    assert(resolve(new_head,g)[i]==std::byte{0x22});
    assert(after[p+32+i]==std::byte{0x44});
  }
  source[64]=std::byte{2};
  assert(after[64]==std::byte{1}); // Its whole control page was privately copied.
  source[2*p]=std::byte{2};
  assert(source[2*p]==std::byte{2}); // Separate live owner state remains authoritative.
  assert(munmap(before,2*p)==0 && munmap(after,2*p)==0 && munmap(source,3*p)==0);
  close(fd);
  std::cout << "native_cow view_binding_and_payload_independent adjacent_global_metadata_becomes_stale\n";
}
}

int main() {
  address_oracle(); retained_reader(); delayed_thread(); pressure_and_exclusive();
  coarse_scope(); append_and_identity(); native_cow();
  admitted_window();
}

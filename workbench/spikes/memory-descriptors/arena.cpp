#include "arena.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace descriptors {
namespace {
std::size_t round_up(std::size_t n, std::size_t alignment) {
  return (n + alignment - 1) & ~(alignment - 1);
}
void check(bool ok) {
  if (!ok) throw std::runtime_error("native mapping operation failed");
}
unsigned storage_class(const Geometry& g, std::size_t bytes) {
  for (unsigned c = 0; c < 15; ++c) if (bytes <= g.stride[c]) return c;
  throw std::invalid_argument("virtual placement remains open in this spike");
}
struct Home {
  bool occupied{};
  bool current{};
  std::size_t pins{};
  std::size_t length{};
  std::uint64_t generation{};
  std::size_t pinned_extent{};
};
}

Geometry geometry(Palette palette, Directory directory, Packing packing,
                  std::size_t page) {
  assert(page && !(page & (page - 1)));
  Geometry g{page, 0, directory == Directory::planes ? 16u : 64u};
  if (palette == Palette::a)
    g.stride = {16,32,48,64,96,128,192,256,384,512,768,1024,1536,2048,3072,0};
  else
    g.stride = {256,320,384,448,512,640,768,896,1024,1280,1536,1792,2048,2560,3072,0};
  std::size_t end = 0;
  for (unsigned c = 0; c < 15; ++c) {
    if (packing == Packing::private_large &&
        (palette == Palette::b || c >= 5)) end = round_up(end, page);
    g.offset[c] = end;
    end += 16 * g.stride[c];
  }
  // No addresses are assigned to the open virtual class.
  g.grid_bytes = round_up(end, page);
  return g;
}

struct Row {
  std::mutex latch;
  const Geometry* geometry{};
  std::uint8_t* cells{};
  std::byte* payload{};
  std::byte* side{};
  std::array<bool,16> live{};
  std::array<Home,256> homes{};
  std::vector<bool> mapped;
  std::size_t scopes{};
  std::uint64_t next_generation{1};
  Counters counters;
};

namespace {
int free_home(const Row& row, unsigned c) {
  for (unsigned i = 0; i < 16; ++i) {
    const unsigned coordinate = (i << 4) | c;
    if (!row.homes[coordinate].occupied) return coordinate;
  }
  return -1;
}
std::byte* home_address(const Row& row, unsigned coordinate) {
  const auto& g = *row.geometry;
  return row.payload + g.offset[coordinate & 15] +
      g.stride[coordinate & 15] * (coordinate >> 4);
}
void map_home(Row& row, unsigned coordinate) {
  const auto& g = *row.geometry;
  const auto offset = home_address(row, coordinate) - row.payload;
  const auto first = offset / g.page;
  const auto last = (offset + g.stride[coordinate & 15] - 1) / g.page;
  for (auto p = first; p <= last; ++p) if (!row.mapped[p]) {
    check(mprotect(row.payload + p*g.page, g.page, PROT_READ|PROT_WRITE) == 0);
    row.mapped[p] = true;
    ++row.counters.payload_pages;
  }
}
}

Arena::Arena(Palette palette, Directory directory, Packing packing,
             std::size_t grids, unsigned id)
    : geometry_(geometry(palette, directory, packing,
                         static_cast<std::size_t>(sysconf(_SC_PAGESIZE)))),
      id_(id) {
  assert(grids && id < 64 && geometry_.page >= 4096);
  blocks_ = (grids + grids_per_block - 1) / grids_per_block;
  block_bytes_ = geometry_.page + grids_per_block * geometry_.grid_bytes;
  reserved_ = blocks_ * block_bytes_;
  auto* mapping = mmap(nullptr, reserved_, PROT_NONE,
                       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  check(mapping != MAP_FAILED);
  base_ = static_cast<std::byte*>(mapping);
  assert(!(reinterpret_cast<Head>(base_) & tag_mask));
  for (std::size_t b = 0; b < blocks_; ++b)
    check(mprotect(base_ + b*block_bytes_, geometry_.page,
                   PROT_READ|PROT_WRITE) == 0);
  for (std::size_t r = 0; r < grids; ++r) {
    auto row = std::make_unique<Row>();
    auto* control = base_ + (r/grids_per_block)*block_bytes_;
    const auto local = r % grids_per_block;
    row->geometry = &geometry_;
    row->cells = reinterpret_cast<std::uint8_t*>(control + local*geometry_.pitch);
    row->side = directory == Directory::planes
        ? control + 16*grids_per_block + local*48
        : control + local*64 + 16;
    *row->side = std::byte{64}; // Read-only preparation hint for layout comparison.
    row->payload = control + geometry_.page + local*geometry_.grid_bytes;
    row->mapped.resize(geometry_.grid_bytes/geometry_.page);
    rows_.push_back(std::move(row));
  }
}

Arena::~Arena() {
  for (const auto& row : rows_) {
    assert(!row->scopes);
    for (const auto& home : row->homes) assert(!home.pins);
  }
  if (base_) check(munmap(base_, reserved_) == 0);
}

Row& Arena::row_for(Head head) const {
  assert(arena_id(head) == id_);
  const auto a = native(head);
  const auto start = reinterpret_cast<Head>(base_);
  assert(a >= start && a < start + reserved_);
  const auto block = (a-start)/block_bytes_;
  const auto offset = (a-start)%block_bytes_;
  assert(offset < geometry_.page && offset%geometry_.pitch < 16);
  const auto r = block*grids_per_block + offset/geometry_.pitch;
  assert(r < rows_.size());
  return *rows_[r];
}

Head Arena::create(std::size_t grid, std::size_t bytes, std::uint8_t pattern,
                   std::size_t minimum_capacity) {
  auto& row = *rows_.at(grid);
  std::lock_guard lock(row.latch);
  const unsigned c = storage_class(geometry_, std::max(bytes, minimum_capacity));
  const auto owner = std::find(row.live.begin(), row.live.end(), false);
  const int destination = free_home(row, c);
  if (owner == row.live.end() || destination < 0) return 0;
  const auto index = owner-row.live.begin();
  map_home(row, destination);
  std::memset(home_address(row,destination), pattern, geometry_.stride[c]);
  row.homes[destination] = {true,true,0,bytes,row.next_generation++};
  row.live[index] = true;
  __atomic_store_n(row.cells+index, std::uint8_t(destination), __ATOMIC_RELEASE);
  return reinterpret_cast<Head>(row.cells+index) | (Head{id_} << 57);
}

Grant Arena::borrow(Head head, std::size_t readable_extent) {
  auto& row = row_for(head);
  std::lock_guard lock(row.latch);
  const auto index = native(head)-reinterpret_cast<Head>(row.cells);
  assert(index < 16 && row.live[index]);
  const unsigned coordinate = __atomic_load_n(row.cells+index,__ATOMIC_ACQUIRE);
  auto& home = row.homes[coordinate];
  assert(home.occupied && home.current);
  const auto extent = readable_extent ? readable_extent : home.length;
  if (extent > geometry_.stride[coordinate&15]) return {};
  ++home.pins;
  home.pinned_extent=std::max(home.pinned_extent,extent);
  Grant grant;
  grant.arena_ = this; grant.row_ = &row; grant.coordinate_ = coordinate;
  grant.data_ = home_address(row,coordinate); grant.size_ = extent;
  grant.generation_ = home.generation;
  return grant;
}

Scope Arena::protect(std::size_t grid) {
  auto& row = *rows_.at(grid);
  std::lock_guard lock(row.latch);
  ++row.scopes;
  Scope scope; scope.arena_ = this; scope.row_ = &row;
  return scope;
}

bool Arena::change(Head head, std::size_t bytes, bool copy,
                   std::uint8_t pattern) {
  auto& row = row_for(head);
  std::lock_guard lock(row.latch);
  const auto index = native(head)-reinterpret_cast<Head>(row.cells);
  assert(index < 16 && row.live[index]);
  const unsigned old = __atomic_load_n(row.cells+index,__ATOMIC_ACQUIRE);
  auto& previous = row.homes[old];
  assert(!copy || bytes >= previous.length);
  const bool append_overlaps = row.scopes || previous.pinned_extent > previous.length;
  if (copy && bytes <= geometry_.stride[old&15] && !append_overlaps) {
    // Append initializes only bytes beyond the admitted old prefix.
    std::memset(home_address(row,old)+previous.length, 0, bytes-previous.length);
    previous.length = bytes;
    return true;
  }
  const unsigned c = storage_class(geometry_,bytes);
  const int destination = free_home(row,c);
  if (destination < 0) { ++row.counters.blocked_moves; return false; }
  map_home(row,destination);
  std::memset(home_address(row,destination), pattern, geometry_.stride[c]);
  if (copy) {
    std::memcpy(home_address(row,destination),home_address(row,old),previous.length);
    row.counters.copied_bytes += previous.length;
  }
  row.homes[destination] = {true,true,0,bytes,row.next_generation++};
  previous.current = false;
  __atomic_store_n(row.cells+index,std::uint8_t(destination),__ATOMIC_RELEASE);
  collect(row);
  return true;
}
bool Arena::grow(Head head, std::size_t bytes) { return change(head,bytes,true,0); }
bool Arena::replace(Head head, std::size_t bytes, std::uint8_t pattern) {
  return change(head,bytes,false,pattern);
}
bool Arena::try_inplace(Head head, std::uint8_t pattern) {
  auto& row = row_for(head);
  std::lock_guard lock(row.latch);
  const auto index = native(head)-reinterpret_cast<Head>(row.cells);
  assert(index < 16 && row.live[index]);
  const unsigned coordinate = __atomic_load_n(row.cells+index,__ATOMIC_ACQUIRE);
  auto& home = row.homes[coordinate];
  if (home.pins || row.scopes) return false;
  std::memset(home_address(row,coordinate),pattern,home.length);
  home.generation = row.next_generation++;
  return true;
}
void Arena::retire(Head head) {
  auto& row = row_for(head);
  std::lock_guard lock(row.latch);
  const auto index = native(head)-reinterpret_cast<Head>(row.cells);
  assert(index < 16 && row.live[index]);
  const unsigned coordinate = __atomic_load_n(row.cells+index,__ATOMIC_ACQUIRE);
  row.homes[coordinate].current = false;
  row.live[index] = false;
  collect(row);
}

void Arena::collect(Row& row) {
  if (row.scopes) return;
  for (auto& home : row.homes)
    if (home.occupied && !home.current && !home.pins) home = {};
  std::vector<bool> needed(row.mapped.size());
  for (unsigned coordinate = 0; coordinate < 256; ++coordinate) {
    if (!row.homes[coordinate].occupied) continue;
    const auto offset = home_address(row,coordinate)-row.payload;
    const auto first = offset/geometry_.page;
    const auto last = (offset+geometry_.stride[coordinate&15]-1)/geometry_.page;
    for (auto p = first; p <= last; ++p) needed[p] = true;
  }
  for (std::size_t p = 0; p < needed.size(); ++p)
    if (row.mapped[p] && !needed[p]) {
      check(madvise(row.payload+p*geometry_.page,geometry_.page,MADV_DONTNEED)==0);
      check(mprotect(row.payload+p*geometry_.page,geometry_.page,PROT_NONE)==0);
      row.mapped[p] = false;
      --row.counters.payload_pages;
      ++row.counters.released_pages;
    }
}
void Arena::release(Grant& grant) {
  std::lock_guard lock(grant.row_->latch);
  auto& home = grant.row_->homes[grant.coordinate_];
  assert(home.pins);
  --home.pins;
  if (!home.pins) home.pinned_extent=0;
  if (!home.current && !home.pins) collect(*grant.row_);
}
void Arena::release(Scope& scope) {
  std::lock_guard lock(scope.row_->latch);
  assert(scope.row_->scopes);
  if (--scope.row_->scopes == 0) collect(*scope.row_);
}
Counters Arena::counters() const {
  Counters sum;
  for (const auto& row : rows_) {
    std::lock_guard lock(row->latch);
    sum.payload_pages += row->counters.payload_pages;
    sum.released_pages += row->counters.released_pages;
    sum.copied_bytes += row->counters.copied_bytes;
    sum.blocked_moves += row->counters.blocked_moves;
  }
  return sum;
}
std::size_t Arena::owner_record_bytes() const {
  std::size_t bytes=sizeof(*this)+rows_.capacity()*sizeof(std::unique_ptr<Row>);
  for (const auto& row:rows_) bytes+=sizeof(Row)+(row->mapped.capacity()+7)/8;
  return bytes;
}
std::byte* Arena::hint(Head head) const { return row_for(head).side; }

Grant::Grant(Grant&& other) noexcept { *this = std::move(other); }
Grant& Grant::operator=(Grant&& other) noexcept {
  if (this != &other) {
    complete();
    arena_=std::exchange(other.arena_,nullptr); row_=other.row_;
    coordinate_=other.coordinate_; data_=other.data_; size_=other.size_;
    generation_=other.generation_; cancelled_=other.cancelled_;
  }
  return *this;
}
Grant::~Grant() { complete(); }
void Grant::complete() { if (arena_) { arena_->release(*this); arena_=nullptr; } }
Scope::Scope(Scope&& other) noexcept { *this = std::move(other); }
Scope& Scope::operator=(Scope&& other) noexcept {
  if (this != &other) { complete(); arena_=std::exchange(other.arena_,nullptr); row_=other.row_; }
  return *this;
}
Scope::~Scope() { complete(); }
void Scope::complete() { if (arena_) { arena_->release(*this); arena_=nullptr; } }

} // namespace descriptors

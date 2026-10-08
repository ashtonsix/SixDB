#include "packed-page.h"
#include "lookup.h"
#include "packed-construction.h"
#include "inventory.h"

#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <loom/worker.h>
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace orbital_bench {
namespace {
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
namespace alloc = orbital::allocation;
using namespace packed;

template<class T, class E> T checked(std::expected<T, E> result) {
  if (!result) throw std::runtime_error("packed consumer admission/capacity failure");
  return std::move(*result);
}
template<class E> void checked(std::expected<void, E> result) {
  if (!result) throw std::runtime_error("packed consumer operation failure");
}


// This consumer's point writes observe one scalar replacement. Range writes
// retain SeriesPack's vector-capable sum_change.
struct PointSum {
  static constexpr bool needs_before = true;
  std::uint64_t delta = 0;
  void observe_scalar(std::uint64_t before, std::uint64_t after) { delta += after - before; }
};
class Packed final : public Store {
  Schema schema_;
  loom::Worker worker_;
  lookup::Driver lookup_;
  std::vector<Entry> directory_;
  Counters counts_;
  std::unique_ptr<Inventory> inventory_;
  PreparationDemands preparation_demands_;
  bool preparation_stopped_{};
  bool begun_{};
  Access access_{};
  using BeforeImage = std::array<std::byte, page_bytes>;
  std::unique_ptr<BeforeImage> before_;

  void index_write(std::size_t offset, std::size_t size) {
    if (!size) return;
    if (offset < order_offset || offset > page_bytes || size > page_bytes - offset)
      throw std::runtime_error("index write exceeds held page metadata");
    counts_.index_write_bytes += size; ++counts_.index_write_spans;
  }
  void occupy(Page& page, std::size_t slot, bool live) {
    auto word = slot / 64;
    index_write(bitmap_offset + word * 8, 8);
    if (live) page.occupied[word] |= std::uint64_t{1} << (slot % 64);
    else page.occupied[word] &= ~(std::uint64_t{1} << (slot % 64));
  }
  std::size_t free_slot(const Page& page) {
    for (std::size_t word = 0; word < 4; ++word)
      if (~page.occupied[word]) return word * 64 + std::countr_zero(~page.occupied[word]);
    throw std::runtime_error("full physical page without split");
  }

  alloc::Hold acquire(Page& page) {
    // The ordinary owner-ready path never suspends/polls/reenters/grows/releases
    // backing during native use. Its actual Allocation already protects bytes;
    // admitted stable Ikea bindings qualify the full footprint. No use escapes.
    if (access_ == Access::owner_ready) return {};
    if (page.retained) return {};
    ++counts_.lease_calls;
    return checked(loom::hold(page.allocation, {0, page_bytes}));
  }
  void account(Bindings& binding, const ikea::source_write_journal& effects) {
    // Resolve issued source-qualified bytes while the named view is alive.
    // This is local maintenance accounting, never a durable journal.
    const auto delta = pack_writes(binding, effects);
    counts_.issued_write_bytes += delta.issued_write_bytes;
    counts_.issued_write_spans += delta.issued_write_spans;
  }
  const BeforeImage* before_image(const Page& page) {
    if (!before_) return nullptr;
    std::copy_n(page.allocation.address, page_bytes, before_->begin());
    return before_.get();
  }
  [[gnu::noinline]] void verify_coverage(Page& page, const BeforeImage& before,
      const ikea::source_write_journal& effects) {
    const auto& b = *page.bindings;
    std::array<bool, page_bytes> covered{};
    for (const auto& effect : effects.entries()) {
      auto base = effect.source == &b.series ? 0 : tuple_offset;
      for (std::size_t i = 0; i < effect.bytes.size; ++i)
        covered[base + effect.bytes.offset + i] = true;
    }
    for (std::size_t i = 0; i < page_bytes; ++i)
      if (before[i] != page.allocation.address[i] && !covered[i])
        throw std::runtime_error("changed page byte outside issued coverage");
  }
  void publish_page(std::size_t index, Key maximum, std::unique_ptr<Page> page) {
    directory_.insert(directory_.begin() + index, Entry{maximum, page.get()});
    page.release(); // Transfer to Packed only after the directory accepts it.
  }
  std::unique_ptr<Page> new_page() {
    std::unique_ptr<Page> page;
    ConstructionDelta delta;
    alloc::Hold hold;
    if (inventory_ && !preparation_stopped_) {
      page=inventory_->take_ready(preparation_demands_,delta);
    }
    if (!page) {
      page=std::make_unique_for_overwrite<Page>();
      alloc::AllocationRequest request;
      request.bytes=page_bytes; request.reserved_bytes=page_bytes+64;
      request.alignment=64; request.readable_padding=64;
      request.initialization=alloc::Initialization::uninitialized;
      page->allocation=checked(loom::allocate(worker_,request));
      hold=acquire(*page);
      delta=construct_blank_page(*page,schema_,
          {page->allocation.address,page->allocation.size},page->allocation.readable_padding,bool(before_));
    }
    if (page && !hold && access_!=Access::owner_ready) hold=acquire(*page);
    // Assigned owner adoption only. A preparation helper returns this delta;
    // the constructor never reaches live Packed counters or directory state.
    counts_.issued_write_bytes += delta.issued_write_bytes;
    counts_.issued_write_spans += delta.issued_write_spans;
    counts_.index_write_bytes += delta.index_write_bytes;
    counts_.index_write_spans += delta.index_write_spans;
    if (access_ == Access::retained_hold) page->retained = std::move(hold);
    ++counts_.allocated_pages;
    counts_.payload_bytes += order_offset;
    return page;
  }
  std::size_t page_index(Key key) const {
    auto found = std::lower_bound(directory_.begin(), directory_.end(), key,
        [](const Entry& entry, Key k) { return entry.maximum < k; });
    return found - directory_.begin();
  }
  // Bindings admits every named field over the same initialized capacity.
  // A live logical rank still needs its physical slot checked before native
  // access; the owner/physical Hold keeps those placement proofs stable.
  std::size_t physical_slot(const Page& page, std::size_t rank) const {
    const auto slot = page.order[rank];
    if (slot >= capacity)
      throw std::runtime_error("packed consumer admission/capacity failure");
    return slot;
  }
  Key key_at(const Page& page, std::size_t rank) const {
    return packed::read_key(*page.bindings, physical_slot(page, rank));
  }
  std::size_t lower(Page& page, Key key) {
    std::size_t first = 0, last = page.live;
    while (first < last) {
      auto mid = first + (last - first) / 2;
      if (key_at(page, mid) < key) first = mid + 1; else last = mid;
    }
    return first;
  }
  Row row(Page& page, std::size_t index) {
    auto& b = *page.bindings;
    const auto slot = physical_slot(page, index);
    return {packed::read_key(b, slot), packed::record(b, slot)};
  }
  void write_row(Page& page, std::size_t index, const Row& row) {
    if (index >= capacity || row.record.value > value_mask)
      throw std::runtime_error("invalid composed packed write");
    auto& b = *page.bindings;
    const auto* before = before_image(page);
    std::array<ikea::owner_write, 64> records;
    ikea::source_write_journal effects{records};
    const auto needed = checked(b.prepared.effect_capacity(index, 1, sp::row_selection::all())) +
        b.write_key.effect_capacity() + b.write_next.effect_capacity() + b.write_tag.effect_capacity();
    if (needed > records.size()) throw std::runtime_error("insufficient composed effect capacity");
    // Whole compound admission precedes dirty bytes: every tuple code is eight
    // bits, all rows fit, Series value fits, and effect space covers all calls.
    PointSum change;
    checked(b.prepared.set(index, row.record.value, change, effects));
    checked(b.write_key.set(index, row.key, effects));
    checked(b.write_next.set(index, row.record.next, effects));
    checked(b.write_tag.set(index, row.record.tag, effects));
    page.sum += change.delta;
    account(b, effects);
    if (before) verify_coverage(page, *before, effects);
  }
  void retire_slot(Page& page, std::size_t slot) {
    auto& b = *page.bindings;
    const auto* before = before_image(page);
    std::array<ikea::owner_write, 64> records;
    ikea::source_write_journal effects{records};
    PointSum change;
    // One checked point operation; no full-capacity scan for a single erase.
    checked(b.prepared.set(slot, 0u, change, effects));
    page.sum += change.delta;
    account(b, effects);
    if (before) verify_coverage(page, *before, effects);
  }
  void publish(std::size_t index) {
    auto& entry = directory_[index];
    const auto& page = *entry.page;
    entry.maximum = key_at(page, page.live - 1);
  }
  // For this headless Local expression, preflight counts body and residual
  // fields separately: two records per eight-row tile. Aligned16-row masks
  // can omit tile pairs but cannot increase that conservative bound.
  static_assert(Format::storage == sp::geometry::local && Format::heads == 0 &&
                Format::tile_rows == 8 && capacity % 32 == 0);
  static constexpr auto series_effects_per_page = capacity / Format::tile_rows *
      ((Format::body != 0) + (Format::tail != 0));
  struct Transfer {
    std::array<std::uint32_t, capacity / 2> values;
    std::array<std::uint16_t, capacity / 2> slots;
  };
  void fill_transfer(Page& page, const Page& source, const Transfer& input) {
    auto& b = *page.bindings;
    constexpr auto count = capacity / 2;
    std::array<ikea::owner_write, series_effects_per_page / 2 + 1> records;
    ikea::source_write_journal effects{records};
    const auto selected = sp::row_selection::all();
    const auto series_needed = checked(b.prepared.effect_capacity(0, count, selected));
    for (auto value : input.values)
      if (value > value_mask) throw std::runtime_error("over-width transfer value");
    const auto tuples = checked(tp::bind_transfer<tuple_stride>(
        schema_.layout, source.bindings->tuples, schema_.layout, b.tuples));
    const auto needed = series_needed + tuples.effect_capacity(count);
    if (needed > records.size()) throw std::runtime_error("insufficient transfer effects");
    const std::span<const std::uint16_t> slots{input.slots};
    checked(tuples.admit(0, slots, records.size() - series_needed));
    const auto* before = before_image(page);
    sp::sum_change change;
    b.write_value.replace_unchecked(0, count, input.values.data(), selected, change, effects);
    tuples.copy_unchecked(0, slots, effects);
    page.sum += change.finish();
    account(b, effects);
    if (before) verify_coverage(page, *before, effects);
  }
  void clear_slots(Page& page, const std::array<std::uint16_t, capacity / 16>& selected) {
    auto& b = *page.bindings;
    const auto selection = sp::row_selection::regions(0, selected);
    std::array<std::uint32_t, capacity> values{};
    std::array<ikea::owner_write, series_effects_per_page> records;
    ikea::source_write_journal effects{records};
    const auto needed = checked(b.prepared.effect_capacity(0, capacity, selection));
    if (needed > records.size()) throw std::runtime_error("insufficient clear effects");
    const auto* before = before_image(page);
    sp::sum_change change;
    b.write_value.replace_unchecked(0, capacity, values.data(), selection, change, effects);
    page.sum += change.finish();
    account(b, effects);
    if (before) verify_coverage(page, *before, effects);
    // Dead Tuple slots retain initialized valid eight-bit codes. Only live
    // ranks/bitmap expose rows; insertion overwrites every field before it
    // publishes a reused slot. Series spares must be zero for the live sum.
  }
  [[gnu::noinline]] void split(std::size_t index) {
    // Reserve publication capacity before changing either live page. Grow only
    // at the capacity boundary, preserving geometric directory expansion.
    if (directory_.size() == directory_.capacity()) {
      const auto size = directory_.size(), limit = directory_.max_size();
      if (size == limit) throw std::length_error("packed fence directory is full");
      directory_.reserve(size + std::min(size, limit - size));
    }
    auto& old = *directory_[index].page;
    auto left_hold = acquire(old);
    const auto half = old.live / 2;
    if (old.live != capacity) throw std::runtime_error("split requires a full page");
    Transfer moved;
    std::array<std::uint16_t, capacity / 16> selected{};
    // Logical order supplies physical source slots; Ikea qualifies the whole
    // Tuple unit transfer. Only the separately encoded Series value is decoded.
    for (std::size_t i = half; i < old.live; ++i) {
      const auto slot = physical_slot(old, i);
      const auto at = i - half;
      moved.values[at] = std::uint32_t(sp::get_unchecked(old.bindings->series, slot));
      moved.slots[at] = slot;
      const auto bit = std::uint16_t{1} << (slot % 16);
      if (selected[slot / 16] & bit) throw std::runtime_error("duplicate moved physical slot");
      selected[slot / 16] |= bit;
    }
    auto right = new_page();
    auto right_hold = acquire(*right);
    fill_transfer(*right, old, moved);
    clear_slots(old, selected);
    for (std::size_t i = half; i < old.live; ++i) {
      const auto slot = right->live++;
      index_write(order_offset + slot * 2, 2); right->order[slot] = slot;
      occupy(*right, slot, true);
      const auto old_slot = old.order[i];
      occupy(old, old_slot, false);
    }
    old.live = half;
    const auto max = key_at(*right, right->live - 1);
    publish_page(index + 1, max, std::move(right));
    publish(index); ++counts_.splits;
  }
public:
  explicit Packed(std::uint64_t byte_limit, Access access, bool check_effects)
      : access_(access),
        before_(check_effects ? std::make_unique_for_overwrite<BeforeImage>() : nullptr) {
    checked(loom::adopt(worker_));
    try {
      auto arena = checked(alloc::open_arena(byte_limit));
      checked(loom::configure(worker_, std::move(arena), 64));
    } catch (...) {
      // Failed configuration publishes no Pool. Member destruction still
      // requires the adopted Lane to be relinquished on its owning thread.
      if (orbital::worker::on_lane(worker_.lane)) loom::relinquish(worker_);
      throw;
    }
  }
  ~Packed() override {
    if (inventory_) { try { drain(); } catch (...) { std::terminate(); } }
    inventory_.reset();
    lookup_.close();
    for (const auto& entry : directory_) delete entry.page;
    directory_.clear();
    for (;;) {
      auto progress = loom::shutdown(worker_);
      if (progress.kind() == orbital::Progress::Kind::done) break;
      if (progress.kind() == orbital::Progress::Kind::failed) std::terminate();
      loom::poll(worker_, {1024, 1024});
    }
    loom::relinquish(worker_);
  }
  void load(std::span<const Row> rows) override {
    if (!directory_.empty()) throw std::runtime_error("packed store must be fresh");
    directory_.reserve((rows.size() + capacity - 1) / capacity);
    for (std::size_t first = 0; first < rows.size(); first += capacity) {
      auto page = new_page(); auto hold = acquire(*page);
      auto count = std::min(capacity, rows.size() - first);
      for (std::size_t i = 0; i < count; ++i) {
        if (first + i && rows[first + i - 1].key >= rows[first + i].key)
          throw std::runtime_error("load rows must be strictly ordered");
        write_row(*page, i, rows[first + i]);
        index_write(order_offset + i * 2, 2); page->order[i] = i;
        occupy(*page, i, true);
      }
      page->live = count;
      const auto maximum = rows[first + count - 1].key;
      publish_page(directory_.size(), maximum, std::move(page));
      counts_.logical_live_rows += count;
    }
  }
  void begin(bool) override {
    assert(orbital::worker::on_lane(worker_.lane));
    if (begun_) throw std::runtime_error("nested packed request");
    begun_ = true;
  }
  void end() override { begun_ = false; }
  std::optional<Record> get(Key key) override {
    auto index = page_index(key);
    if (index == directory_.size()) return std::nullopt;
    auto& page = *directory_[index].page; auto hold = acquire(page);
    std::size_t first = 0, last = page.live;
    while (first < last) {
      const auto mid = first + (last - first) / 2;
      const auto slot = page.order[mid];
      if (slot >= capacity)
        throw std::runtime_error("packed consumer admission/capacity failure");
      const auto found = packed::read_key(*page.bindings, slot);
      if (found == key) return packed::record(*page.bindings, slot);
      if (found < key) first = mid + 1;
      else last = mid;
    }
    return std::nullopt;
  }
  void prepare_read_batch(ReadExecution execution, std::size_t active_width = 16) override {
    if (execution == ReadExecution::serial) {
      Store::prepare_read_batch(execution, active_width);
      return;
    }
    if (access_ != Access::owner_ready)
      throw std::invalid_argument("staged packed reads require uninterrupted owner access");
    assert(orbital::worker::on_lane(worker_.lane));
    lookup_.prepare(worker_, execution, active_width);
  }
  void read_batch(std::span<const Operation> input, std::span<Outcome> output,
      ReadExecution execution) override {
    if (execution == ReadExecution::serial) {
      Store::read_batch(input, output, execution);
      return;
    }
    if (access_ != Access::owner_ready)
      throw std::invalid_argument("staged packed reads require uninterrupted owner access");
    // One uninterrupted owner visit freezes directory, live/rank metadata,
    // initialized capacity bytes and named Ikea bindings. No poll, reentry,
    // mutation, growth or release occurs until every native borrower stops.
    begin(false);
    struct End { Packed& owner; ~End() { owner.end(); } } end{*this};
    const lookup::Source source{directory_};
    lookup_.run(worker_, source, input, output, execution);
  }
  bool update(Key key, Record record) override {
    if (record.value > value_mask) throw std::runtime_error("over-width packed value");
    auto index = page_index(key);
    if (index == directory_.size()) return false;
    auto& page = *directory_[index].page; auto hold = acquire(page);
    auto at = lower(page, key);
    if (at == page.live || key_at(page, at) != key) return false;
    write_row(page, page.order[at], {key, record}); return true;
  }
  bool insert(Key key, Record record) override {
    if (record.value > value_mask) throw std::runtime_error("over-width packed value");
    if (directory_.empty()) publish_page(0, key, new_page());
    auto index = std::min(page_index(key), directory_.size() - 1);
    auto* page = directory_[index].page;
    auto hold = acquire(*page); auto at = lower(*page, key);
    if (at < page->live && key_at(*page, at) == key) return false;
    if (page->live == capacity) {
      // New largest keys are genuine appends; avoid splitting a full old page.
      if (index + 1 == directory_.size() && key > directory_[index].maximum) {
        auto fresh = new_page();
        publish_page(directory_.size(), key, std::move(fresh)); ++index;
      } else split(index);
      index = std::min(page_index(key), directory_.size() - 1);
      page = directory_[index].page; hold = acquire(*page); at = lower(*page, key);
    }
    // Sorted ranks refer to stable physical slots. Move the real two-byte index,
    // never rewrite every packed value for a positional insertion.
    const auto slot = free_slot(*page);
    write_row(*page, slot, {key, record});
    index_write(order_offset + at * 2, (page->live - at + 1) * 2);
    std::move_backward(page->order + at, page->order + page->live, page->order + page->live + 1);
    page->order[at] = slot; occupy(*page, slot, true); ++page->live;
    publish(index); ++counts_.logical_live_rows; return true;
  }
  bool erase(Key key) override {
    auto index = page_index(key);
    if (index == directory_.size()) return false;
    auto& page = *directory_[index].page; auto hold = acquire(page);
    auto at = lower(page, key);
    if (at == page.live || key_at(page, at) != key) return false;
    const auto slot = page.order[at];
    retire_slot(page, slot); occupy(page, slot, false);
    index_write(order_offset + at * 2, (page.live - at - 1) * 2);
    std::move(page.order + at + 1, page.order + page.live, page.order + at); --page.live;
    if (!page.live) {
      directory_.erase(directory_.begin() + index);
      delete &page; // No bound reader survives; the local Hold retires on return.
    } else publish(index);
    --counts_.logical_live_rows;
    return true;
  }
  std::size_t scan(Key key, std::span<Row> output) override {
    std::size_t used = 0;
    auto index = page_index(key);
    while (index < directory_.size() && used < output.size()) {
      auto& page = *directory_[index++].page; auto hold = acquire(page);
      auto first = lower(page, key);
      auto count = std::min(page.live - first, output.size() - used);
      std::array<std::uint32_t, capacity> values;
      bool contiguous = count != 0;
      for (std::size_t i = 1; i < count; ++i)
        contiguous &= page.order[first + i] == page.order[first] + i;
      if (contiguous)
        checked(page.bindings->values.read(page.order[first], std::span(values).first(count)));
      else for (std::size_t i = 0; i < count; ++i)
        values[i] = std::uint32_t(sp::get_unchecked(page.bindings->series, physical_slot(page, first + i)));
      for (std::size_t i = 0; i < count; ++i) {
        auto& b = *page.bindings;
        const auto slot = physical_slot(page, first + i);
        output[used++] = {packed::read_key(b, slot), {packed::read<8>(b.next, slot),
                            values[i], std::uint32_t(packed::read<4>(b.tags, slot))}};
      }
    }
    return used;
  }
  void start_preparation(std::size_t count,int cpu,bool prefill) override {
    if (!count) return;
    if (inventory_) throw std::invalid_argument("preparation already started");
    auto candidate=std::make_unique<Inventory>(worker_,schema_,bool(before_));
    candidate->start(count,cpu,prefill);
    inventory_=std::move(candidate);
  }
  PreparationStats preparation_stats(bool include_storage = false) const override {
    return inventory_ ? inventory_->snapshot(include_storage) : PreparationStats{};
  }
  PreparationDemands preparation_demands() const override { return preparation_demands_; }
  void drain() override {
    if (inventory_) {
      preparation_stopped_=true;
      inventory_->drain();
    }
    loom::poll(worker_, {1024, 1024});
  }
  void audit() override {
    std::uint64_t live_rows = 0;
    Key previous = 0;
    bool first = true;
    for (auto& entry : directory_) {
      auto& page = *entry.page; auto hold = acquire(page);
      if (!page.live || page.live > capacity) throw std::runtime_error("invalid live page extent");
      live_rows += page.live;
      std::uint64_t sum = 0;
      std::array<bool, capacity> occupied{};
      for (std::size_t i = 0; i < page.live; ++i) {
        const auto slot = page.order[i];
        if (slot >= capacity || occupied[slot] || !(page.occupied[slot / 64] & (std::uint64_t{1} << (slot % 64))))
          throw std::runtime_error("invalid packed rank/slot directory");
        occupied[slot] = true;
        auto logical = row(page, i);
        if ((!first && logical.key <= previous) || logical.record.value > value_mask)
          throw std::runtime_error("invalid packed page order/domain");
        previous = logical.key; first = false; sum += logical.record.value;
      }
      if (sum != page.sum || previous != entry.maximum)
        throw std::runtime_error("packed live summary/fence mismatch");
      for (std::size_t i = 0; i < capacity; ++i) {
        if (occupied[i] != bool(page.occupied[i / 64] & (std::uint64_t{1} << (i % 64))))
          throw std::runtime_error("rank directory disagrees with free bitmap");
        if (!occupied[i] && checked(page.bindings->values.get(i)))
          throw std::runtime_error("spare slot contributes a live summary");
      }
    }
    if (live_rows != counts_.logical_live_rows)
      throw std::runtime_error("logical live row counter differs from page visibility");
  }
  std::vector<MemoryRegion> memory_regions() const override {
    // Cold post-interval diagnostics: these overlapping owned extents are not
    // a measured access set. Enumeration itself touches Page metadata.
    std::vector<MemoryRegion> regions;
    regions.reserve(4 + directory_.size() * 8);
    auto add = [&](const void* address, std::size_t bytes, std::string_view kind) {
      if (bytes) regions.push_back({reinterpret_cast<std::uintptr_t>(address), bytes, kind});
    };
    add(this, sizeof(*this), "packed_fixture");
    add(&lookup_, sizeof(lookup_), "lookup_driver");
    add(directory_.data(), directory_.size() * sizeof(Entry), "directory_entries");
    add(directory_.data(), directory_.capacity() * sizeof(Entry), "directory_capacity");
    for (const auto& entry : directory_) {
      const auto& page = *entry.page;
      const auto* base = page.allocation.address;
      add(&page, sizeof(Page), "page");
      add(std::addressof(*page.bindings), sizeof(Bindings), "bindings");
      add(base, page.allocation.size, "allocation");
      add(base + page.allocation.size, page.allocation.readable_padding, "readable_padding");
      add(base, series_bytes, "series_capacity");
      add(base + tuple_offset, capacity * tuple_stride, "tuple_capacity");
      add(page.order, capacity * sizeof(std::uint16_t), "rank_index");
      add(page.occupied, 4 * sizeof(std::uint64_t), "free_bitmap");
    }
    return regions;
  }
  Counters counters(bool include_storage = true) const override {
    auto counts = counts_;
    counts.metadata_bytes = lookup_.storage_bytes() + directory_.capacity() * sizeof(Entry) +
                            directory_.size() * (sizeof(Page) + page_bytes - order_offset);
    counts.live_payload_bytes = directory_.size() * order_offset;
    // Logical fields are key8 + next8 + value4 + tag4, independent of Row ABI
    // padding and the encoded capacity retained by a partially occupied page.
    counts.logical_live_field_bytes = counts.logical_live_rows * 24;
    if (!include_storage) return counts;
    const auto pool = alloc::statistics(*worker_.memory);
    counts.pool_mapped_bytes = pool.mapped_bytes;
    counts.pool_reserved_bytes = pool.reserved_bytes;
    counts.pool_metadata_bytes = pool.metadata_bytes;
    counts.arena_charged_bytes = worker_.arena->charged_bytes.load();
    counts.pool_idle_bytes = pool.idle_bytes;
    counts.pool_allocations = pool.allocations;
    counts.pool_reuses = pool.reuses;
    counts.pool_growths = pool.growths;
    counts.pool_trim_pages = pool.trim_pages;
    return counts;
  }
};
} // namespace
std::unique_ptr<Store> packed_store(std::uint64_t byte_limit, Access access, bool check_effects) {
  return std::make_unique<Packed>(byte_limit, access, check_effects);
}
} // namespace orbital_bench

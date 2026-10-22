#pragma once
#include <loom/worker.h>
#include <orbital/object.h>
#include <orbital/representation.h>
#include <array>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Segment directory: locates an index's Segments, one layer at a time.
//
// Each layer is a tree of Orbital Objects ordered by the Segment's physical
// prefix. Leaves hold the prefix plane, 64-byte dirents (Parts of `tile`) and,
// above the shared prefix J, min/max logical fences. Interior rows hold a
// physical separator, a logical separator above J, and the child's name. The
// root keeps a fixed name and the tree deepens instead of growing a flat root.
// Separators are loose: an edit rewrites a parent only when it would violate
// one, or on split and merge. Evidence and rejected alternatives:
// workbench/spikes/segment-directory/README.md.
namespace engine::directory {

/// Geometry of one directory Object, enough to derive every Part's extent.
/// No interpretation outlives the Control; retain/release are no-ops.
struct Control {
  std::pmr::memory_resource* memory{};
  std::uint64_t name{};
  std::uint32_t count{};       // rows in this node
  std::uint16_t height{};      // 0 = leaf
  std::uint16_t parts{};       // full_part_count(), kept for inline Part access
  std::uint16_t first{};
  std::uint8_t key_bytes{};
  std::uint8_t fenced{};
  std::uint8_t tile{};
  std::uint8_t projected{};
  std::uint16_t reserved{};
  bool valid(std::pmr::memory_resource&) const noexcept;
  auto* resource() const noexcept {return memory;}
  std::size_t part_count() const noexcept {return projected?1:parts;}
  orbital::PartId id(std::size_t i) const noexcept {return {name,std::uint32_t(first+i)};}
  [[nodiscard]] orbital::Result<std::size_t> extent(std::size_t i) const noexcept;
  std::size_t alignment(std::size_t) const noexcept {return 64;}
  std::uint32_t padding(std::size_t i) const noexcept;
  void retain() const noexcept {}
  void release() const noexcept {}
  std::size_t selected_bytes(std::size_t) const noexcept {return 0;}
  Control copy_selected(void*,std::size_t i) const noexcept {
    auto c=*this;c.first=std::uint16_t(first+i);c.projected=1;return c;
  }
  std::size_t metadata_bytes() const noexcept {return 0;}
  void destroy_selected() const noexcept {}
  std::size_t full_part_count() const noexcept;
  std::size_t full_extent(std::size_t i) const noexcept;
};
static_assert(std::is_trivially_copyable_v<Control>&&sizeof(Control)==32);

using Selection=orbital::RepresentationSelection<Control>;
struct Physical {
  using Value=orbital::Representation<Control>;
  using Selection=directory::Selection;
  using Specification=Control;
  using PartSource=orbital::PartSource;
  using ExportPolicy=orbital::PartRetention;
  struct Footprint {std::size_t requested_bytes{},copied_metadata{};};
  static void retain(Value*) noexcept;
  static void release(Value*) noexcept;
  static Value* get(Selection s) noexcept {return s.realization;}
  static Selection selection(Value* p) noexcept {return {p};}
  static orbital::Result<Value*> capture(std::pmr::memory_resource&,const Control&,
      std::span<const PartSource>) noexcept;
  static bool accepts(Value*,orbital::ObjectId,std::uint64_t) noexcept;
  static bool accepts_resource(Value*,std::pmr::memory_resource&) noexcept;
  static bool compatible(Value*,Value*) noexcept;
  static Footprint footprint(Value*) noexcept;
  static orbital::Result<orbital::Part<Control>*> export_part(Selection,std::size_t,
      orbital::ObjectBinding,ExportPolicy) noexcept;
};
using Store=orbital::ObjectStore<Physical>;

/// Six 8-byte part locators and 16 bytes of metadata; opaque to the directory.
struct Dirent {std::array<std::uint64_t,8> word{};};
static_assert(sizeof(Dirent)==64);

/// One Segment's entry. Fences (key_bytes each) are read only above J.
struct Entry {
  std::uint32_t prefix{};
  std::span<const std::uint8_t> min,max;
  Dirent dirent{};
};
struct Edit {
  enum Kind {insert,erase,update} kind{};
  Entry entry;
};

struct Shape {
  unsigned layer{};             // 0..4; a Segment prefix has `layer` bytes
  unsigned key_bytes{};         // K
  unsigned shared_prefix{};     // J: physical and logical keys agree on this many bytes
  unsigned leaf_capacity=128;   // L0/L1 always fit one leaf (256)
  unsigned inner_capacity=512;
  double fill=0.75;             // bulk-build and append-split occupancy
  double merge_below=0.25;
  unsigned tile=16;             // dirents per Part
  bool fenced() const {return layer>shared_prefix;}
};

class Layer {
public:
  /// Names are {object_namespace, layer << 56 | n}; the root is n = 0.
  /// Writers and readers stay on the Store's owner, like every Orbital call.
  Layer(loom::Worker&,Store&,std::pmr::memory_resource&,std::uint64_t object_namespace,Shape);

  /// Bulk-loads an empty layer from entries sorted by prefix, in one publication.
  [[nodiscard]] orbital::Result<void> build(std::span<const Entry>);
  /// Applies edits sorted by prefix, one per prefix, in one publication.
  /// Identical replacements publish nothing.
  [[nodiscard]] orbital::Result<void> apply(std::span<const Edit>);

  /// Borrows of Parts selected under the caller's Scope; valid while it lives.
  struct Cursor {
    std::array<Selection,8> stack{};
    std::array<unsigned,8> index{};
    unsigned depth{};
    Selection leaf{};
    unsigned rank{},count{};
    explicit operator bool() const {return bool(leaf)&&rank<count;}
  };
  const Dirent* find(const Store::Scope&,std::uint32_t prefix) const;
  /// First entry whose prefix is at least `prefix`.
  Cursor seek(const Store::Scope&,std::uint32_t prefix) const;
  /// Above J: first entry whose max fence is at least `low`.
  Cursor seek(const Store::Scope&,std::span<const std::uint8_t> low) const;
  bool next(const Store::Scope&,Cursor&) const;
  std::uint32_t prefix(const Cursor&) const;
  const Dirent& dirent(const Cursor&) const;
  std::span<const std::uint8_t> min(const Cursor&) const;
  std::span<const std::uint8_t> max(const Cursor&) const;

  const Shape& shape() const {return shape_;}
  /// Structural invariants under a fresh Scope; aborts with a message on failure.
  void validate() const;

private:
  struct Node {
    unsigned height{};
    std::vector<std::uint32_t> prefix;
    std::vector<std::uint8_t> min,max;
    std::vector<Dirent> dirent;
    std::vector<std::uint32_t> sep;
    std::vector<std::uint8_t> lsep;
    std::vector<std::uint64_t> child;
    std::size_t size() const {return height?child.size():prefix.size();}
  };
  struct Step {std::uint64_t name{};Node node;unsigned index{};bool dirty{};std::size_t before{};};
  // Latest decoded versions of nodes touched by one publication.
  struct Work {
    const Store::Scope* basis{};
    std::unordered_map<std::uint64_t,Node> nodes;
    std::unordered_set<std::uint64_t> dirty,erased;
  };

  loom::Worker* worker_;
  Store* store_;
  std::pmr::memory_resource* memory_;
  std::uint64_t space_;
  Shape shape_;
  std::uint64_t next_{1};

  std::uint64_t root() const {return std::uint64_t(shape_.layer)<<56;}
  std::uint64_t fresh() {return root()|next_++;}
  unsigned leaf_capacity() const;
  Control control(std::uint64_t name,const Node&) const;
  Node decode(Selection) const;
  Node load(Work&,std::uint64_t name) const;
  void write(Work&,std::uint64_t name,Node) const;
  void erase(Work&,std::uint64_t name) const;
  [[nodiscard]] orbital::Result<void> publish(Work&);
  std::vector<Step> descend(Work&,std::uint32_t prefix) const;
  void edit_leaf(Step&,const Edit&) const;
  void widen_separators(std::vector<Step>&) const;
  void settle(Work&,std::vector<Step>&);
  std::vector<Node> split_at(const Node&,const std::vector<std::size_t>& bounds) const;
  std::vector<Node> split(const Node&,unsigned pieces) const;
  Node concat(Node left,const Node& right,const Node& parent,unsigned right_index) const;
  void set_row(Node& parent,unsigned i,const Node& child) const;
  void insert_rows(Node& parent,unsigned at,const std::vector<Node>&,const std::vector<std::uint64_t>&) const;
  void remove_row(Node& parent,unsigned i) const;
  std::vector<std::vector<std::byte>> encode(const Node&) const;

  Selection bind(const Store::Scope& s,std::uint64_t name) const {return s.bind({space_,name});}
  unsigned route(Selection,std::uint32_t) const;
  unsigned lower(Selection,std::uint32_t) const;
  unsigned route(Selection,std::span<const std::uint8_t>) const;
  unsigned lower(Selection,std::span<const std::uint8_t>) const;
  Cursor& settle_cursor(const Store::Scope&,Cursor&,Selection leaf,unsigned rank) const;
};
}

extern template class orbital::ObjectStore<engine::directory::Physical>;
extern template struct orbital::RepresentationSelection<engine::directory::Control>;
extern template orbital::Result<orbital::PartSource> orbital::source(
    engine::directory::Selection,std::size_t) noexcept;
extern template void orbital::release(orbital::Part<engine::directory::Control>*) noexcept;
extern template orbital::View orbital::view(const orbital::Part<engine::directory::Control>*) noexcept;
extern template orbital::ObjectBinding orbital::binding(const orbital::Part<engine::directory::Control>*) noexcept;

#pragma once
#include "store.h"
#include <array>
#include <optional>
#include <span>

namespace segdir {
struct Key {std::array<std::uint8_t,16> b{};};
struct Dirent {std::array<std::uint64_t,8> w{};};
static_assert(sizeof(Dirent)==64);
// One directory entry: a Segment's physical prefix, its logical fences (used
// only above J) and its dirent.
struct Entry {
  std::uint32_t prefix{};
  Key min{},max{};
  Dirent dirent{};
};

struct Config {
  unsigned layer=4,k=12,j=0;
  unsigned leaf_capacity=128,inner_capacity=128;
  double fill=0.75;          // bulk-build occupancy of leaves and interior nodes
  double merge_below=0.25;   // merge or redistribute below this occupancy
  unsigned tile=16;          // dirents per Part; 0 keeps one dirent Part
  bool bounded_root=true;    // false: leaves under one unbounded root
  bool exact_bounds=false;   // parent rows hold exact child bounds and a 64-byte row
  bool hash_index=false;     // prefix -> leaf pages at L <= J
  bool inline_hash=false;    // prefix -> dirent pages at L <= J; leaves keep only prefixes
  unsigned page_target=192;  // mean entries per hash page
  bool fenced() const {return layer>j;}
  bool hashed() const {return hash_index||inline_hash;}
};

// Bounded-node tree over one layer, ordered by physical prefix. Interior rows
// hold a physical separator, a logical separator above J, and the child's name.
// Separators are loose: they change only when an edit would violate them, so a
// child's new version usually leaves its parent untouched.
class Tree {
public:
  struct Node {
    unsigned height{};
    std::vector<std::uint32_t> prefix;
    std::vector<Key> min,max;
    std::vector<Dirent> dirent;
    std::vector<std::uint32_t> sep;
    std::vector<Key> lsep;
    std::vector<std::uint64_t> child;
    std::vector<std::uint32_t> last;  // exact_bounds only
    std::vector<Key> lmax;            // exact_bounds only
    std::size_t size() const {return height?child.size():prefix.size();}
  };
  struct Cursor {
    std::vector<std::pair<ObjectPtr,unsigned>> path;
    ObjectPtr leaf;
    unsigned rank{},count{};
    explicit operator bool() const {return leaf&&rank<count;}
  };

  Tree(Store&,Config,std::uint64_t name_space);
  void build(std::span<const Entry>);
  void insert(const Entry&);
  void erase(std::uint32_t prefix);
  void update(const Entry&);
  std::optional<Dirent> find(std::uint32_t prefix) const;
  Cursor seek(std::uint32_t prefix) const;
  Cursor seek(const Key& low) const;
  bool next(Cursor&) const;
  Entry read(const Cursor&) const;
  std::vector<Entry> dump() const;
  void validate() const;
  const Config& config() const {return c_;}
  std::uint64_t resident_bytes() const {return store_->resident_bytes(ns_);}
  std::uint64_t resident_objects() const {return store_->resident_objects(ns_);}

private:
  struct Step {std::uint64_t name{};Node node;unsigned index{};bool dirty{};};
  Store* store_;
  Config c_;
  std::uint64_t ns_,next_name_{1};
  // Linear-hashing pages: prefix -> leaf name.
  unsigned level_{};
  std::uint64_t split_{},entries_{};
  std::unordered_map<std::uint32_t,std::optional<Dirent>> pending_;  // empty erases

  std::uint64_t root() const {return ns_<<40;}
  std::uint64_t fresh() {return ns_<<40|next_name_++;}
  std::uint64_t page_name(std::uint64_t page) const {return ns_<<40|std::uint64_t(1)<<39|page;}
  int cmp(const Key& a,const Key& b) const;
  int cmp(const std::byte* a,const Key& b) const;
  Node decode(const ObjectPtr&) const;
  void write(std::uint64_t name,const Node&);
  std::vector<Step> descend(std::uint32_t prefix) const;
  void settle(std::vector<Step>&);
  std::vector<Node> split(const Node&,unsigned capacity) const;
  void set_row(Node& parent,unsigned i,const Node& child) const;
  void insert_rows(Node& parent,unsigned at,const std::vector<Node>&,const std::vector<std::uint64_t>&) const;
  void remove_row(Node& parent,unsigned i) const;
  void moved(const Node& leaf,std::uint64_t name);
  Node concat(Node left,const Node& right,const Node& parent,unsigned right_index) const;
  void leftmost(Cursor&,ObjectPtr) const;
  // Hash pages
  std::uint64_t bucket(std::uint32_t prefix) const;
  std::uint64_t pages() const {return (std::uint64_t(1)<<level_)+split_;}
  std::size_t slot_bytes() const;
  std::vector<std::pair<std::uint32_t,Dirent>> page_entries(std::uint64_t page) const;
  void write_page(std::uint64_t page,const std::vector<std::pair<std::uint32_t,Dirent>>&);
  void flush_pages();
  const std::byte* page_find(std::uint32_t prefix) const;
};

// Old-spike control for L <= J: partitions named by prefix >> natural_bits,
// each one hash table with inline 64-byte dirents beside an ordered prefix
// plane, and an ordered roster of partition numbers.
class Natural {
public:
  struct Cursor {
    ObjectPtr roster,group;
    unsigned group_rank{},groups{},rank{},count{};
    explicit operator bool() const {return group&&rank<count;}
  };
  Natural(Store&,Config,unsigned natural_bits,std::uint64_t name_space);
  void build(std::span<const Entry>);
  void insert(const Entry&);
  void erase(std::uint32_t prefix);
  void update(const Entry&);
  std::optional<Dirent> find(std::uint32_t prefix) const;
  Cursor seek(std::uint32_t prefix) const;
  bool next(Cursor&) const;
  Entry read(const Cursor&) const;
  std::vector<Entry> dump() const;
  void validate() const {}
  const Config& config() const {return c_;}
  std::uint64_t resident_bytes() const {return store_->resident_bytes(ns_);}
  std::uint64_t resident_objects() const {return store_->resident_objects(ns_);}
private:
  Store* store_;
  Config c_;
  unsigned bits_;
  std::uint64_t ns_;
  std::uint64_t name(std::uint32_t group) const {return ns_<<40|1|std::uint64_t(group)<<1;}
  std::uint64_t roster() const {return ns_<<40;}
  std::vector<std::pair<std::uint32_t,Dirent>> entries(std::uint32_t group) const;
  void write(std::uint32_t group,const std::vector<std::pair<std::uint32_t,Dirent>>&);
  bool enter(Cursor&,unsigned group_rank) const;
  const std::byte* probe(const ObjectPtr&,std::uint32_t prefix) const;
};
}

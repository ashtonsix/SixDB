// Bounded access-method study, not an Engine implementation.
// Natural nodes compress unary paths; remapped blocks share an ordered fence
// directory and prefix-to-Page array. Account=false removes movement counters.
// Metadata references are prelocated secondary cells; totals cover containers
// and the collection. See probe.md for the timer and representation boundaries.
#include "model.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace remap {
namespace {
U mix(U x) {
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
U next(U &x) { return mix(x += 0x9e3779b97f4a7c15ULL); }
U value(unsigned id, unsigned c) { return 1 + mix(U(id) * 31 + c) % 1024; }
U query_key(const Case &c, U &random) {
  U key = c.universe ? next(random) % c.universe : next(random);
  if (c.distribution == 2)
    key = (U(next(random) % 16) << 48) | (key & ((U{1} << 48) - 1));
  return key;
}
void require(bool b, const char *msg) {
  if (!b)
    throw std::runtime_error(msg);
}
struct Summary {
  std::int64_t count = 0, sum = 0;
  bool operator==(const Summary &) const = default;
};
struct Bits {
  std::array<U, 4> w{};
  bool has(unsigned s) const { return (w[s / 64] >> (s % 64)) & 1; }
  void set(unsigned s) { w[s / 64] |= U{1} << (s % 64); }
  void clear(unsigned s) { w[s / 64] &= ~(U{1} << (s % 64)); }
  unsigned rank(unsigned s) const {
    unsigned n = 0;
    for (unsigned i = 0; i < s / 64; ++i)
      n += std::popcount(w[i]);
    return n + std::popcount(w[s / 64] & ((U{1} << (s % 64)) - 1));
  }
  int after(unsigned s) const {
    if (s >= 256)
      return 256;
    unsigned b = s / 64;
    U x = w[b] & (~U{0} << (s % 64));
    while (!x && ++b < 4)
      x = w[b];
    return b == 4 ? 256 : int(b * 64 + std::countr_zero(x));
  }
  int before(int s) const {
    if (s < 0)
      return -1;
    unsigned b = unsigned(s) / 64;
    U x = w[b] & (~U{0} >> (63 - unsigned(s) % 64));
    while (!x && b > 0)
      x = w[--b];
    return x ? int(b * 64 + 63 - std::countl_zero(x)) : -1;
  }
  unsigned free() const {
    for (unsigned b = 0; b < 4; ++b)
      if (~w[b])
        return b * 64 + std::countr_zero(~w[b]);
    return 256;
  }
};
struct Page {
  Summary total;
  std::vector<Entry> rows;
  std::vector<std::vector<U>> cols;
  std::vector<unsigned> order, pool_slots;
  Bits bits;
  unsigned n = 0, label = 0;
  bool direct = false;
};
struct Node {
  U prefix = 0, mask = 0;
  unsigned shift = 0;
  std::unique_ptr<Page> leaf;
  std::vector<std::pair<unsigned, std::unique_ptr<Node>>> children;
  std::unique_ptr<std::array<Node *, 256>> direct;
};
struct Found {
  Page *p = nullptr;
  unsigned s = 0;
  explicit operator bool() const { return p != nullptr; }
};

template <bool Account> class Tree final : public Index {
  enum class Mode {
    natural,
    packed,
    gapped,
    ranked,
    blocked,
    indirect,
    gapped_tail
  };
  Mode method;
  unsigned terminal_limit = 64;
  Profile profile;
  Counters stats;
  std::unique_ptr<Node> root;
  std::vector<std::unique_ptr<Page>> physical;
  std::vector<Page *> ordered;
  std::vector<U> fences;
  std::vector<Entry> pool;
  std::vector<std::vector<U>> pool_cols;
  std::vector<unsigned> free_pool;
  std::vector<std::vector<U>> references;
  std::vector<U> identity_map;
  Summary collection_total;
  bool natural() const { return method == Mode::natural; }
  bool tail_policy() const { return method == Mode::gapped_tail; }
  bool ranked() const { return method == Mode::ranked; }
  bool gapped() const {
    return method == Mode::gapped || ranked() || tail_policy();
  }
  bool blocked() const { return method == Mode::blocked; }
  bool indirect() const { return method == Mode::indirect; }
  void moved(U bytes, U keys) {
    if constexpr (Account) {
      stats.payload_moved += bytes;
      stats.key_moved += keys;
    }
  }
  U payload(const Page &p, unsigned s, unsigned c) const {
    return indirect() ? pool_cols[c][p.pool_slots[s]]
                      : p.cols[c][ranked() ? p.bits.rank(s) : s];
  }
  U coordinate(const Page &p, unsigned s) const {
    return natural()    ? p.rows[s].key
           : indirect() ? p.pool_slots[s]
                        : (U(p.label) << 8) | s;
  }
  unsigned at_rank(const Page &p, unsigned r) const {
    if (blocked())
      return p.order[r];
    if (gapped()) {
      int s = p.bits.after(0);
      while (r--)
        s = p.bits.after(unsigned(s + 1));
      return unsigned(s);
    }
    return r;
  }
  std::array<unsigned, 256> ordered_slots(const Page &p) const {
    std::array<unsigned, 256> result{};
    if (gapped()) {
      unsigned r = 0;
      for (int s = p.bits.after(0); s < 256; s = p.bits.after(unsigned(s + 1)))
        result[r++] = unsigned(s);
    }
    return result;
  }
  U min_key(const Page &p) const {
    return p.rows[gapped() ? unsigned(p.bits.after(0)) : at_rank(p, 0)].key;
  }
  U max_key(const Page &p) const {
    return p.rows[gapped() ? unsigned(p.bits.before(255)) : at_rank(p, p.n - 1)]
        .key;
  }
  void adjust(U, int sign, U v) {
    if (!profile.summaries)
      return;
    collection_total.count += sign;
    collection_total.sum += sign * std::int64_t(v);
    if constexpr (Account)
      ++stats.summary_adjustments;
  }
  void page_adjust(Page &p, int sign, U v) {
    if (!profile.summaries)
      return;
    p.total.count += sign;
    p.total.sum += sign * std::int64_t(v);
    if constexpr (Account)
      ++stats.summary_adjustments;
  }
  void reference_set(unsigned id, U loc, bool repair) {
    if (profile.references) {
      if (profile.stable_ids) {
        identity_map[id] = loc;
        if constexpr (Account)
          stats.references_repaired += repair;
      } else
        for (auto &ref : references) {
          ref[id] = loc;
          if constexpr (Account)
            stats.references_repaired += repair;
        }
    }
  }
  void add_metadata(Entry e, U loc, U v) {
    reference_set(e.id, loc, false);
    if (profile.stable_ids)
      for (auto &ref : references)
        ref[e.id] = e.id;
    adjust(loc, 1, v);
  }
  void remove_metadata(Entry e, U loc, U v) {
    reference_set(e.id, absent, false);
    if (profile.stable_ids)
      for (auto &ref : references)
        ref[e.id] = absent;
    adjust(loc, -1, v);
  }
  void relocate(Entry e, U old, U now, U) {
    if (old == now)
      return;
    if constexpr (Account)
      ++stats.relabelled;
    reference_set(e.id, now, true);
  }
  void gap_before(Page &p, unsigned s) {
    U k = s < 256 ? p.rows[s].key : absent;
    int i = int(s) - 1;
    while (i >= 0 && !p.bits.has(unsigned(i))) {
      p.rows[unsigned(i)].key = k;
      --i;
    }
  }
  void refresh_direct(Page &p) {
    p.bits = {};
    p.direct = p.n && ((p.rows.front().key ^ p.rows[p.n - 1].key) < 256);
    if (p.direct)
      for (unsigned i = 0; i < p.n; ++i)
        p.bits.set(unsigned(p.rows[i].key & 255));
  }
  std::unique_ptr<Page> load_page(const Image &image, unsigned first,
                                  unsigned last, unsigned label = 0) {
    auto p = std::make_unique<Page>();
    p->label = label;
    p->n = last - first;
    bool fixed = !natural() && !indirect();
    unsigned cap = 256;
    if (natural()) {
      bool byte =
          p->n && (image.rows[first].key ^ image.rows[last - 1].key) < 256;
      cap = std::min(byte ? 256u : terminal_limit + 1,
                     p->n + (p->n * profile.natural_slack + 99) / 100);
    }
    p->rows.reserve(cap);
    p->rows.resize(fixed ? 256 : p->n);
    unsigned width = indirect() ? 0 : profile.columns;
    p->cols.resize(width);
    for (auto &col : p->cols) {
      col.reserve(cap);
      col.resize(fixed && !ranked() ? 256 : p->n);
    }
    if (blocked())
      p->order.reserve(256);
    for (unsigned r = 0; r < p->n; ++r) {
      unsigned s = gapped() ? unsigned(U(r + 1) * 256 / (p->n + 1)) : r;
      p->rows[s] = image.rows[first + r];
      page_adjust(*p, 1, profile.columns ? image.columns[0][first + r] : 0);
      if (!natural())
        p->bits.set(s);
      for (unsigned c = 0; c < width; ++c)
        p->cols[c][ranked() ? r : s] = image.columns[c][first + r];
      if (blocked())
        p->order.push_back(s);
    }
    if (gapped()) {
      U k = absent;
      for (int s = 255; s >= 0; --s) {
        if (p->bits.has(unsigned(s)))
          k = p->rows[unsigned(s)].key;
        else
          p->rows[unsigned(s)].key = k;
      }
    }
    if (natural())
      refresh_direct(*p);
    return p;
  }
  static unsigned differing_byte(U a, U b) {
    require(a != b, "different keys");
    return (63 - unsigned(std::countl_zero(a ^ b))) / 8 * 8;
  }
  static U above_mask(unsigned shift) {
    return shift == 56 ? 0 : (~U{0} << (shift + 8));
  }
  void refresh_children(Node &n) {
    if (n.children.size() > 48 && !n.direct)
      n.direct = std::make_unique<std::array<Node *, 256>>();
    if (n.direct) {
      n.direct->fill(nullptr);
      for (auto &[b, ch] : n.children)
        (*n.direct)[b] = ch.get();
    }
  }
  std::unique_ptr<Node> build_natural(const Image &image, unsigned first,
                                      unsigned last) {
    auto n = std::make_unique<Node>();
    if (last - first <= terminal_limit ||
        (image.rows[first].key ^ image.rows[last - 1].key) < 256) {
      n->leaf = load_page(image, first, last);
      return n;
    }
    n->shift = differing_byte(image.rows[first].key, image.rows[last - 1].key);
    n->mask = above_mask(n->shift);
    n->prefix = image.rows[first].key & n->mask;
    for (unsigned i = first; i < last;) {
      unsigned b = unsigned((image.rows[i].key >> n->shift) & 255), j = i + 1;
      while (j < last && ((image.rows[j].key >> n->shift) & 255) == b)
        ++j;
      n->children.emplace_back(b, build_natural(image, i, j));
      i = j;
    }
    refresh_children(*n);
    return n;
  }
  unsigned lower(const Page &p, U k) const {
    if (blocked())
      return unsigned(std::lower_bound(p.order.begin(), p.order.end(), k,
                                       [&](unsigned s, U key) {
                                         return p.rows[s].key < key;
                                       }) -
                      p.order.begin());
    return unsigned(std::lower_bound(
                        p.rows.begin(), p.rows.begin() + (gapped() ? 256 : p.n),
                        k, [](Entry e, U key) { return e.key < key; }) -
                    p.rows.begin());
  }
  Found in_page(Page *p, U k) const {
    if (!p || !p->n)
      return {};
    unsigned s;
    if (natural() && p->direct) {
      if ((k >> 8) != (p->rows[0].key >> 8) || !p->bits.has(unsigned(k & 255)))
        return {};
      s = p->bits.rank(unsigned(k & 255));
    } else {
      s = lower(*p, k);
      if (blocked()) {
        if (s == p->n)
          return {};
        s = p->order[s];
      } else if (gapped())
        s = unsigned(p->bits.after(s));
    }
    if (s >= (natural() || indirect() ? p->n : 256) || p->rows[s].key != k)
      return {};
    return {p, s};
  }
  Node *natural_child(Node *n, unsigned b) const {
    if (n->direct)
      return (*n->direct)[b];
    auto it =
        std::lower_bound(n->children.begin(), n->children.end(), b,
                         [](const auto &x, unsigned v) { return x.first < v; });
    return it != n->children.end() && it->first == b ? it->second.get()
                                                     : nullptr;
  }
  Found find(U k) const {
    if (natural()) {
      Node *n = root.get();
      while (n && !n->leaf) {
        if ((k & n->mask) != n->prefix)
          return {};
        n = natural_child(n, unsigned((k >> n->shift) & 255));
      }
      return in_page(n ? n->leaf.get() : nullptr, k);
    }
    if (ordered.empty())
      return {};
    auto it = std::lower_bound(fences.begin(), fences.end(), k);
    return it == fences.end()
               ? Found{}
               : in_page(ordered[unsigned(it - fences.begin())], k);
  }
  U digest(const Page &p, unsigned s) const {
    U result = mix(p.rows[s].key ^ p.rows[s].id);
    unsigned physical_slot = indirect() ? p.pool_slots[s]
                             : ranked() ? p.bits.rank(s)
                                        : s;
    for (unsigned c = 0; c < profile.columns; ++c)
      result = mix(result ^ (indirect() ? pool_cols[c][physical_slot]
                                        : p.cols[c][physical_slot]));
    return result;
  }
  void walk_natural(const Node *n,
                    const std::function<void(const Page &)> &f) const {
    if (n->leaf)
      f(*n->leaf);
    else
      for (auto &x : n->children)
        walk_natural(x.second.get(), f);
  }
  void pages(const std::function<void(const Page &)> &f) const {
    if (natural())
      walk_natural(root.get(), f);
    else
      for (Page *p : ordered)
        f(*p);
  }
  Image page_image(const Page &p) const {
    Image out;
    out.columns.resize(profile.columns);
    auto slots = ordered_slots(p);
    for (unsigned r = 0; r < p.n; ++r) {
      unsigned s = gapped() ? slots[r] : at_rank(p, r);
      out.rows.push_back(p.rows[s]);
      for (unsigned c = 0; c < profile.columns; ++c)
        out.columns[c].push_back(payload(p, s, c));
    }
    return out;
  }
  void packed_insert(Page &p, Entry e, const U *v, bool is_natural) {
    unsigned pos = lower(p, e.key);
    bool was_direct = p.direct;
    U old_prefix = p.n ? p.rows[0].key >> 8 : 0;
    if (is_natural) {
      p.cols.resize(profile.columns);
      if (p.rows.size() == p.rows.capacity())
        moved(0, U(p.n) * sizeof(Entry));
      for (auto &col : p.cols)
        if (col.size() == col.capacity())
          moved(U(p.n) * 8, 0);
      p.rows.insert(p.rows.begin() + pos, e);
      for (unsigned c = 0; c < profile.columns; ++c)
        p.cols[c].insert(p.cols[c].begin() + pos, v[c]);
    } else {
      if (Account || profile.references || profile.summaries)
        for (unsigned j = pos; j < p.n; ++j)
          relocate(p.rows[j], (U(p.label) << 8) | j,
                   (U(p.label) << 8) | (j + 1),
                   profile.columns ? p.cols[0][j] : 0);
      std::move_backward(p.rows.begin() + pos, p.rows.begin() + p.n,
                         p.rows.begin() + p.n + 1);
      for (auto &col : p.cols)
        std::move_backward(col.begin() + pos, col.begin() + p.n,
                           col.begin() + p.n + 1);
      p.rows[pos] = e;
      for (unsigned c = 0; c < profile.columns; ++c)
        p.cols[c][pos] = v[c];
      p.bits.set(p.n);
    }
    moved(U(p.n - pos) * profile.columns * 8, U(p.n - pos) * sizeof(Entry));
    ++p.n;
    if (is_natural)
      page_adjust(p, 1, profile.columns ? v[0] : 0);
    if (is_natural) {
      if (was_direct && (e.key >> 8) == old_prefix)
        p.bits.set(unsigned(e.key & 255));
      else
        refresh_direct(p);
    }
  }
  void natural_insert(std::unique_ptr<Node> &n, Entry e, const U *v) {
    if (n->leaf) {
      auto &p = *n->leaf;
      packed_insert(p, e, v, true);
      if (p.n > terminal_limit && !p.direct) {
        Image im = page_image(p);
        moved(U(p.n) * profile.columns * 8, U(p.n) * sizeof(Entry));
        n = build_natural(im, 0, p.n);
        if constexpr (Account)
          ++stats.splits;
      }
      return;
    }
    if ((e.key & n->mask) != n->prefix) {
      auto parent = std::make_unique<Node>();
      parent->shift = differing_byte(n->prefix, e.key);
      parent->mask = above_mask(parent->shift);
      parent->prefix = e.key & parent->mask;
      unsigned old = unsigned((n->prefix >> parent->shift) & 255);
      parent->children.emplace_back(old, std::move(n));
      n = std::move(parent);
      if constexpr (Account)
        ++stats.splits;
    }
    unsigned b = unsigned((e.key >> n->shift) & 255);
    auto it =
        std::lower_bound(n->children.begin(), n->children.end(), b,
                         [](const auto &x, unsigned y) { return x.first < y; });
    if (it == n->children.end() || it->first != b) {
      Image im;
      im.rows.push_back(e);
      im.columns.resize(profile.columns);
      for (unsigned c = 0; c < profile.columns; ++c)
        im.columns[c].push_back(v[c]);
      n->children.insert(it, {b, build_natural(im, 0, 1)});
      refresh_children(*n);
    } else {
      natural_insert(it->second, e, v);
      if (n->direct)
        (*n->direct)[b] = it->second.get();
    }
  }
  void natural_erase(Node &n, U k) {
    if (n.leaf) {
      auto &p = *n.leaf;
      unsigned s = in_page(&p, k).s;
      require(p.n && p.rows[s].key == k, "erase natural");
      p.rows.erase(p.rows.begin() + s);
      for (auto &col : p.cols)
        col.erase(col.begin() + s);
      --p.n;
      moved(U(p.n - s) * profile.columns * 8, U(p.n - s) * sizeof(Entry));
      if (p.direct) {
        p.bits.clear(unsigned(k & 255));
        if (!p.n)
          p.direct = false;
      } else
        refresh_direct(p);
      return;
    }
    unsigned b = unsigned((k >> n.shift) & 255);
    auto it =
        std::lower_bound(n.children.begin(), n.children.end(), b,
                         [](const auto &x, unsigned y) { return x.first < y; });
    require(it != n.children.end() && it->first == b, "erase branch");
    natural_erase(*it->second, k);
    if (it->second->leaf && !it->second->leaf->n) {
      n.children.erase(it);
      refresh_children(n);
    }
    if (n.children.empty())
      n.leaf = std::make_unique<Page>();
  }
  void slot_move(Page &p, unsigned from, unsigned to) {
    Entry e = p.rows[from];
    U v = profile.columns ? payload(p, from, 0) : 0;
    p.rows[to] = e;
    if (!ranked())
      for (unsigned c = 0; c < profile.columns; ++c)
        p.cols[c][to] = p.cols[c][from];
    p.bits.set(to);
    p.bits.clear(from);
    moved(ranked() ? 0 : profile.columns * 8, sizeof(Entry));
    relocate(e, (U(p.label) << 8) | from, (U(p.label) << 8) | to, v);
  }
  void page_relabel(unsigned old, unsigned now) {
    auto p = std::move(physical[old]);
    require(p && !physical[now], "page label");
    auto slots = ordered_slots(*p);
    for (unsigned r = 0; r < p->n; ++r) {
      unsigned s = gapped() ? slots[r] : at_rank(*p, r);
      relocate(p->rows[s], (U(old) << 8) | s, (U(now) << 8) | s,
               profile.columns ? payload(*p, s, 0) : 0);
    }
    p->label = now;
    physical[now] = std::move(p);
    if constexpr (Account)
      ++stats.page_relabels;
  }
  unsigned label_after(Page *left) {
    unsigned wanted = left->label + 1;
    if (wanted >= physical.size())
      physical.resize(physical.size() * 2);
    unsigned hole = wanted;
    while (hole < physical.size() && physical[hole])
      ++hole;
    if (hole == physical.size())
      physical.resize(physical.size() * 2);
    while (hole > wanted) {
      page_relabel(hole - 1, hole);
      --hole;
    }
    return wanted;
  }
  void split(unsigned ordinal) {
    Page *old = ordered[ordinal];
    unsigned label = old->label;
    unsigned original_rows = old->n;
    if (indirect()) {
      unsigned half = old->n / 2;
      auto right = std::make_unique<Page>();
      right->label = unsigned(physical.size());
      right->n = old->n - half;
      right->rows.assign(old->rows.begin() + half, old->rows.end());
      right->rows.reserve(256);
      right->pool_slots.assign(old->pool_slots.begin() + half,
                               old->pool_slots.end());
      right->pool_slots.reserve(256);
      if (profile.summaries)
        for (unsigned s = half; s < old->n; ++s)
          page_adjust(*right, 1, profile.columns ? payload(*old, s, 0) : 0);
      old->rows.resize(half);
      old->pool_slots.resize(half);
      old->n = half;
      if (profile.summaries) {
        old->total.count -= right->total.count;
        old->total.sum -= right->total.sum;
      }
      Page *rp = right.get();
      physical.push_back(std::move(right));
      ordered.insert(ordered.begin() + ordinal + 1, rp);
      moved(0, U(original_rows - half) * (sizeof(Entry) + sizeof(unsigned)));
    } else {
      Image im = page_image(*old);
      std::vector<U> previous;
      auto old_slots = ordered_slots(*old);
      for (unsigned r = 0; r < old->n; ++r)
        previous.push_back(
            coordinate(*old, gapped() ? old_slots[r] : at_rank(*old, r)));
      unsigned right_label = label_after(old);
      auto a = load_page(im, 0, old->n / 2, label);
      auto b = load_page(im, old->n / 2, old->n, right_label);
      unsigned rank = 0;
      for (Page *p : {a.get(), b.get()}) {
        auto slots = ordered_slots(*p);
        for (unsigned r = 0; r < p->n; ++r) {
          unsigned s = gapped() ? slots[r] : at_rank(*p, r);
          relocate(p->rows[s], previous[rank++], coordinate(*p, s),
                   profile.columns ? payload(*p, s, 0) : 0);
        }
      }
      moved(U(im.rows.size()) * profile.columns * 8,
            U(im.rows.size()) * sizeof(Entry));
      ordered[ordinal] = a.get();
      ordered.insert(ordered.begin() + ordinal + 1, b.get());
      physical[label] = std::move(a);
      physical[right_label] = std::move(b);
    }
    fences[ordinal] = max_key(*ordered[ordinal]);
    fences.insert(fences.begin() + ordinal + 1, max_key(*ordered[ordinal + 1]));
    if constexpr (Account) {
      ++stats.splits;
      stats.largest_move = std::max(stats.largest_move, U(original_rows));
    }
  }
  unsigned target(U key) const {
    if (ordered.size() == 1 && !ordered[0]->n)
      return 0;
    auto it = std::lower_bound(fences.begin(), fences.end(), key);
    return it == fences.end() ? unsigned(ordered.size() - 1)
                              : unsigned(it - fences.begin());
  }

public:
  Tree(std::string m, Profile p, const Image &im, unsigned ids)
      : method(Mode::natural), profile(std::move(p)) {
    if (m == "packed")
      method = Mode::packed;
    else if (m == "gapped")
      method = Mode::gapped;
    else if (m == "gapped_tail")
      method = Mode::gapped_tail;
    else if (m == "ranked")
      method = Mode::ranked;
    else if (m == "blocked")
      method = Mode::blocked;
    else if (m == "indirect")
      method = Mode::indirect;
    else if (m == "natural256")
      terminal_limit = 256;
    else if (m == "natural1024")
      terminal_limit = 1024;
    else
      require(m == "natural", "unknown method");
    require(profile.columns <= columns_max, "column limit");
    references.resize(profile.references);
    for (auto &r : references)
      r.resize(ids, absent);
    if (profile.stable_ids)
      identity_map.resize(ids, absent);
    if (natural())
      root = im.rows.empty() ? std::make_unique<Node>()
                             : build_natural(im, 0, unsigned(im.rows.size()));
    else {
      unsigned count = unsigned((im.rows.size() + 191) / 192),
               capacity = std::bit_ceil(std::max(4u, count * 2));
      physical.resize(capacity);
      if (indirect()) {
        pool = im.rows;
        pool_cols.resize(profile.columns);
        for (unsigned c = 0; c < profile.columns; ++c)
          pool_cols[c] = im.columns[c];
      }
      for (unsigned i = 0; i < count; ++i) {
        unsigned first = i * 192,
                 last = std::min(first + 192, unsigned(im.rows.size()));
        unsigned label = (i + 1) * capacity / (count + 1);
        auto page = load_page(im, first, last, label);
        if (indirect()) {
          for (unsigned j = first; j < last; ++j)
            page->pool_slots.push_back(j);
          page->pool_slots.reserve(256);
        }
        ordered.push_back(page.get());
        physical[label] = std::move(page);
      }
    }
    if (!natural())
      for (Page *p : ordered)
        fences.push_back(max_key(*p));
    if (natural() && !root->leaf && root->children.empty())
      root->leaf = std::make_unique<Page>();
    pages([&](const Page &page) {
      auto slots = ordered_slots(page);
      for (unsigned r = 0; r < page.n; ++r) {
        unsigned s = gapped() ? slots[r] : at_rank(page, r);
        add_metadata(page.rows[s], coordinate(page, s),
                     profile.columns ? payload(page, s, 0) : 0);
      }
    });
    stats = {};
  }
  U point(U key) const override {
    auto f = find(key);
    return f ? digest(*f.p, f.s) : 0;
  }
  U scan(U lower_key, unsigned count) const override {
    U sum = 0;
    auto dense = [&](const Page &p, unsigned start, unsigned take) {
      if (!take)
        return;
      if (profile.columns) {
        const U *values = p.cols[0].data() + start;
        for (unsigned j = 0; j < take; ++j)
          sum += values[j];
      } else
        for (unsigned j = 0; j < take; ++j)
          sum += p.rows[start + j].key;
      count -= take;
    };
    if (natural()) {
      auto visit = [&](auto &&self, const Node *n) -> void {
        if (!count)
          return;
        if (n->leaf) {
          const auto &p = *n->leaf;
          unsigned start = lower(p, lower_key);
          dense(p, start, std::min(count, p.n - start));
          return;
        }
        if ((n->prefix | ~n->mask) < lower_key)
          return;
        unsigned label = (lower_key & n->mask) == n->prefix
                             ? unsigned((lower_key >> n->shift) & 255)
                             : 0;
        auto it = std::lower_bound(
            n->children.begin(), n->children.end(), label,
            [](const auto &x, unsigned y) { return x.first < y; });
        for (; it != n->children.end() && count; ++it)
          self(self, it->second.get());
      };
      visit(visit, root.get());
      return sum;
    }
    if (ordered.empty())
      return 0;
    for (unsigned i = target(lower_key); i < ordered.size() && count; ++i) {
      const Page &p = *ordered[i];
      unsigned start = lower(p, lower_key);
      if (ranked() && profile.columns) {
        unsigned slot = unsigned(p.bits.after(start));
        if (slot < 256) {
          unsigned rank = p.bits.rank(slot);
          dense(p, rank, std::min(count, p.n - rank));
        }
      } else if (gapped()) {
        auto scan_bits = [&](auto get) {
          if (start >= 256)
            return;
          for (unsigned word = start / 64; word < 4 && count; ++word) {
            U bits = p.bits.w[word];
            if (word == start / 64)
              bits &= ~U{0} << (start % 64);
            while (bits && count) {
              unsigned slot = word * 64 + std::countr_zero(bits);
              sum += get(slot);
              bits &= bits - 1;
              --count;
            }
          }
        };
        if (profile.columns)
          scan_bits([&](unsigned slot) { return p.cols[0][slot]; });
        else
          scan_bits([&](unsigned slot) { return p.rows[slot].key; });
      } else if (blocked()) {
        unsigned take = std::min(count, p.n - start);
        if (profile.columns)
          for (unsigned j = 0; j < take; ++j)
            sum += p.cols[0][p.order[start + j]];
        else
          for (unsigned j = 0; j < take; ++j)
            sum += p.rows[p.order[start + j]].key;
        count -= take;
      } else if (indirect()) {
        unsigned take = std::min(count, p.n - start);
        if (profile.columns)
          for (unsigned j = 0; j < take; ++j)
            sum += pool_cols[0][p.pool_slots[start + j]];
        else
          for (unsigned j = 0; j < take; ++j)
            sum += p.rows[start + j].key;
        count -= take;
      } else
        dense(p, start, std::min(count, p.n - start));
    }
    return sum;
  }
  U secondary(unsigned id, unsigned index) const override {
    U loc = references[index][id];
    if (loc == absent)
      return 0;
    if (profile.stable_ids)
      loc = identity_map[unsigned(loc)];
    if (loc == absent)
      return 0;
    if (natural())
      return point(loc);
    if (indirect()) {
      require(loc < pool.size(), "pool locator");
      U result = mix(pool[loc].key ^ pool[loc].id);
      for (unsigned c = 0; c < profile.columns; ++c)
        result = mix(result ^ pool_cols[c][loc]);
      return result;
    }
    unsigned label = unsigned(loc >> 8), s = unsigned(loc & 255);
    require(label < physical.size() && physical[label] &&
                physical[label]->bits.has(s),
            "physical locator");
    return digest(*physical[label], s);
  }
  void erase(U key) override {
    auto f = find(key);
    require(bool(f), "erase missing");
    Page &p = *f.p;
    unsigned ordinal = natural() ? 0 : target(key);
    unsigned s = f.s;
    remove_metadata(p.rows[s], coordinate(p, s),
                    profile.columns ? payload(p, s, 0) : 0);
    page_adjust(p, -1, profile.columns ? payload(p, s, 0) : 0);
    if (natural()) {
      natural_erase(*root, key);
      return;
    }
    if (indirect()) {
      unsigned slot = p.pool_slots[s];
      pool[slot] = {absent, 0};
      free_pool.push_back(slot);
      p.rows.erase(p.rows.begin() + s);
      p.pool_slots.erase(p.pool_slots.begin() + s);
      moved(0, U(p.n - s - 1) * (sizeof(Entry) + sizeof(unsigned)));
    } else if (gapped()) {
      if (ranked()) {
        unsigned r = p.bits.rank(s);
        for (auto &col : p.cols)
          col.erase(col.begin() + r);
        moved(U(p.n - r - 1) * profile.columns * 8, 0);
      }
      p.bits.clear(s);
      unsigned next_slot = unsigned(p.bits.after(s));
      p.rows[s].key = next_slot < 256 ? p.rows[next_slot].key : absent;
      gap_before(p, s);
    } else if (blocked()) {
      p.bits.clear(s);
      auto it = std::find(p.order.begin(), p.order.end(), s);
      moved(0, U(p.order.end() - it - 1) * sizeof(unsigned));
      p.order.erase(it);
    } else {
      if (Account || profile.references || profile.summaries)
        for (unsigned j = s + 1; j < p.n; ++j)
          relocate(p.rows[j], (U(p.label) << 8) | j,
                   (U(p.label) << 8) | (j - 1),
                   profile.columns ? p.cols[0][j] : 0);
      std::move(p.rows.begin() + s + 1, p.rows.begin() + p.n,
                p.rows.begin() + s);
      for (auto &col : p.cols)
        std::move(col.begin() + s + 1, col.begin() + p.n, col.begin() + s);
      p.bits.clear(p.n - 1);
      moved(U(p.n - s - 1) * profile.columns * 8,
            U(p.n - s - 1) * sizeof(Entry));
    }
    --p.n;
    if (!p.n) {
      unsigned label = p.label;
      ordered.erase(ordered.begin() + ordinal);
      fences.erase(fences.begin() + ordinal);
      physical[label].reset();
    } else
      fences[ordinal] = max_key(p);
  }
  void insert(Entry e, const U *values) override {
    if (natural()) {
      natural_insert(root, e, values);
      add_metadata(e, e.key, profile.columns ? values[0] : 0);
      return;
    }
    if (ordered.empty()) {
      Image im;
      im.columns.resize(profile.columns);
      unsigned label = 0;
      auto p = load_page(im, 0, 0, label);
      if (gapped())
        for (auto &r : p->rows)
          r.key = absent;
      ordered.push_back(p.get());
      fences.push_back(absent);
      physical[label] = std::move(p);
    }
    unsigned i = target(e.key);
    if (ordered[i]->n == 256) {
      split(i);
      i = target(e.key);
    }
    Page &p = *ordered[i];
    unsigned s = 0;
    if (indirect()) {
      s = lower(p, e.key);
      unsigned slot;
      if (free_pool.empty()) {
        slot = unsigned(pool.size());
        pool.push_back(e);
        for (unsigned c = 0; c < profile.columns; ++c)
          pool_cols[c].push_back(values[c]);
      } else {
        slot = free_pool.back();
        free_pool.pop_back();
        pool[slot] = e;
        for (unsigned c = 0; c < profile.columns; ++c)
          pool_cols[c][slot] = values[c];
      }
      moved(0, U(p.n - s) * (sizeof(Entry) + sizeof(unsigned)));
      p.rows.insert(p.rows.begin() + s, e);
      p.pool_slots.insert(p.pool_slots.begin() + s, slot);
      ++p.n;
    } else if (blocked()) {
      unsigned r = lower(p, e.key);
      s = p.bits.free();
      require(s < 256, "block full");
      p.rows[s] = e;
      for (unsigned c = 0; c < profile.columns; ++c)
        p.cols[c][s] = values[c];
      p.bits.set(s);
      moved(0, U(p.n - r) * sizeof(unsigned));
      p.order.insert(p.order.begin() + r, s);
      ++p.n;
    } else if (gapped()) {
      unsigned bound = lower(p, e.key), right = unsigned(p.bits.after(bound));
      int left = p.bits.before(int(right) - 1);
      if (tail_policy() && right == 256 && left == 255) {
        auto slots = ordered_slots(p);
        for (unsigned r = 0; r < p.n; ++r)
          if (slots[r] != r)
            slot_move(p, slots[r], r);
        for (unsigned j = p.n; j < 256; ++j)
          p.rows[j].key = absent;
        left = int(p.n) - 1;
        if constexpr (Account)
          ++stats.redistributions;
      }
      if (int(right) - left > 1)
        s = tail_policy() && right == 256 ? unsigned(left + 1)
                                          : unsigned((left + int(right)) / 2);
      else {
        int hole_left = left;
        while (hole_left >= 0 && p.bits.has(unsigned(hole_left)))
          --hole_left;
        unsigned hole_right = right;
        while (hole_right < 256 && p.bits.has(hole_right))
          ++hole_right;
        if (hole_right < 256 &&
            (hole_left < 0 ||
             hole_right - right <= unsigned(left - hole_left))) {
          s = right;
          for (unsigned j = hole_right; j > s; --j)
            slot_move(p, j - 1, j);
        } else {
          require(hole_left >= 0, "no gap");
          s = unsigned(left);
          for (unsigned j = unsigned(hole_left); j < s; ++j)
            slot_move(p, j + 1, j);
        }
      }
      p.rows[s] = e;
      if (ranked()) {
        unsigned r = p.bits.rank(s);
        for (unsigned c = 0; c < profile.columns; ++c)
          p.cols[c].insert(p.cols[c].begin() + r, values[c]);
        moved(U(p.n - r) * profile.columns * 8, 0);
      } else
        for (unsigned c = 0; c < profile.columns; ++c)
          p.cols[c][s] = values[c];
      p.bits.set(s);
      gap_before(p, s);
      ++p.n;
    } else {
      s = lower(p, e.key);
      packed_insert(p, e, values, false);
    }
    fences[i] = max_key(p);
    page_adjust(p, 1, profile.columns ? values[0] : 0);
    add_metadata(e, coordinate(p, s), profile.columns ? values[0] : 0);
  }
  Image image() const override {
    Image result;
    result.columns.resize(profile.columns);
    pages([&](const Page &p) {
      auto slots = ordered_slots(p);
      for (unsigned r = 0; r < p.n; ++r) {
        unsigned s = gapped() ? slots[r] : at_rank(p, r);
        result.rows.push_back(p.rows[s]);
        for (unsigned c = 0; c < profile.columns; ++c)
          result.columns[c].push_back(payload(p, s, c));
      }
    });
    return result;
  }
  Counters counters() const override { return stats; }
  void reset_counters() override { stats = {}; }
  Footprint footprint() const override {
    Footprint f;
    f.routing = physical.capacity() * sizeof(std::unique_ptr<Page>) +
                ordered.capacity() * sizeof(Page *) +
                fences.capacity() * sizeof(U);
    pages([&](const Page &p) {
      ++f.pages;
      f.keys += p.rows.capacity() * sizeof(Entry);
      f.routing += sizeof(Page) + p.cols.capacity() * sizeof(std::vector<U>) +
                   p.order.capacity() * sizeof(unsigned) +
                   p.pool_slots.capacity() * sizeof(unsigned);
      for (auto &c : p.cols)
        f.payload += c.capacity() * 8;
    });
    if (natural()) {
      std::function<void(const Node *)> walk = [&](const Node *n) {
        ++f.nodes;
        f.routing += sizeof(Node) +
                     n->children.capacity() * sizeof(n->children[0]) +
                     (n->direct ? sizeof(*n->direct) : 0);
        for (auto &ch : n->children)
          walk(ch.second.get());
      };
      walk(root.get());
    }
    f.keys += pool.capacity() * sizeof(Entry);
    for (auto &col : pool_cols)
      f.payload += col.capacity() * 8;
    f.routing += free_pool.capacity() * sizeof(unsigned);
    for (auto &r : references)
      f.references += r.capacity() * 8;
    f.references += identity_map.capacity() * 8;
    f.summary_entries = profile.summaries ? f.pages + 1 : 0;
    return f;
  }
  void validate() const override {
    Image im = image();
    require(std::is_sorted(im.rows.begin(), im.rows.end(),
                           [](Entry a, Entry b) { return a.key < b.key; }),
            "logical order");
    Summary expected;
    U previous = 0;
    bool first = true;
    pages([&](const Page &p) {
      Summary page_expected;
      unsigned rank = 0;
      U last = 0;
      bool start = true;
      auto slots = ordered_slots(p);
      for (unsigned r = 0; r < p.n; ++r) {
        unsigned s = gapped() ? slots[r] : at_rank(p, r);
        Entry e = p.rows[s];
        require(start || last < e.key, "page order");
        last = e.key;
        start = false;
        U loc = coordinate(p, s);
        if (!natural() && !indirect()) {
          require(p.bits.has(s), "occupied slot");
          if (!blocked())
            require(first || previous < loc, "physical order");
          previous = loc;
          first = false;
        }
        require(point(e.key) == digest(p, s), "point agrees");
        for (unsigned ref = 0; ref < profile.references; ++ref)
          require(secondary(e.id, ref) == digest(p, s), "secondary agrees");
        ++page_expected.count;
        page_expected.sum +=
            std::int64_t(profile.columns ? payload(p, s, 0) : 0);
        ++rank;
      }
      require(rank == p.n, "rank count");
      if (profile.summaries)
        require(page_expected == p.total, "page summary oracle");
      expected.count += page_expected.count;
      expected.sum += page_expected.sum;
      if (gapped())
        require(std::is_sorted(p.rows.begin(), p.rows.end(),
                               [](Entry a, Entry b) { return a.key < b.key; }),
                "gap search order");
    });
    if (profile.summaries)
      require(expected == collection_total, "summary oracle");
  }
};
} // namespace
U row_digest(Entry e, const U *v, unsigned width) {
  U result = mix(e.key ^ e.id);
  for (unsigned c = 0; c < width; ++c)
    result = mix(result ^ v[c]);
  return result;
}
std::vector<std::string> methods() {
  return {"natural",     "natural256", "natural1024", "packed",  "gapped",
          "gapped_tail", "ranked",     "blocked",     "indirect"};
}
std::vector<Profile> profiles() {
  return {{"keys", 0, 0, false, false},
          {"narrow", 1, 0, false, false},
          {"wide", 16, 0, false, false},
          {"deps", 16, 4, true, false},
          {"ids", 16, 4, true, true}};
}
std::vector<Case> cases() {
  return {{"d01", 655, 512, 65536},
          {"d05", 3277, 512, 65536},
          {"d20", 13107, 512, 65536},
          {"d50", 32768, 512, 65536},
          {"d90", 58982, 512, 65536},
          {"spread", 4096, 512, 0, 1},
          {"collision", 4096, 512, 0, 2},
          {"hot", 4096, 2048, 65536, 0, 1},
          {"append", 4096, 2048, 65536, 0, 2},
          {"moving", 4096, 2048, 65536, 0, 3},
          {"fixed_dense", 4096, 512, 8192},
          {"fixed_sparse", 4096, 512, 1048576}};
}
Trace trace(Case c) {
  Trace t;
  t.config = c;
  t.initial.columns.resize(columns_max);
  U rng = 17, qrng = 99;
  std::unordered_set<U> used;
  std::vector<U> keys;
  if (c.universe) {
    std::vector<U> all(c.universe);
    std::iota(all.begin(), all.end(), 0);
    std::mt19937_64 random(42);
    std::shuffle(all.begin(), all.end(), random);
    keys.assign(all.begin(), all.begin() + c.population);
  } else
    while (keys.size() < c.population) {
      U k = next(rng);
      if (c.distribution == 2)
        k = (U(next(rng) % 16) << 48) | (k & ((U{1} << 48) - 1));
      if (k != absent && used.insert(k).second)
        keys.push_back(k);
    }
  std::sort(keys.begin(), keys.end());
  used = {keys.begin(), keys.end()};
  std::vector<Entry> live;
  for (unsigned id = 0; id < keys.size(); ++id) {
    Entry e{keys[id], id};
    t.initial.rows.push_back(e);
    t.digest = mix(t.digest ^ e.key ^ e.id);
    live.push_back(e);
    for (unsigned col = 0; col < columns_max; ++col)
      t.initial.columns[col].push_back(value(id, col));
  }
  for (unsigned j = 0; j < c.operations; ++j) {
    unsigned victim = unsigned(next(rng) % live.size());
    U k;
    do {
      if (c.insertion == 2)
        k = c.universe + j;
      else if (c.insertion == 1 || c.insertion == 3)
        k = (c.insertion == 3
                 ? c.universe / 4 + (j >= c.operations / 2 ? c.universe / 2 : 0)
                 : c.universe / 2) +
            next(rng) % std::max<U>(c.operations + 64,
                                    std::max<U>(64, c.universe / 16));
      else if (c.universe)
        k = next(rng) % c.universe;
      else {
        k = next(rng);
        if (c.distribution == 2)
          k = (U(next(rng) % 16) << 48) | (k & ((U{1} << 48) - 1));
      }
    } while (k == absent || used.contains(k));
    Op op;
    op.erase = live[victim].key;
    op.row = {k, c.population + j};
    for (unsigned col = 0; col < columns_max; ++col)
      op.values[col] = value(op.row.id, col);
    used.erase(op.erase);
    used.insert(k);
    live[victim] = op.row;
    op.probe = live[next(qrng) % live.size()].key;
    t.operations.push_back(op);
    t.digest = mix(t.digest ^ op.erase ^ k ^ op.row.id);
  }
  for (unsigned j = 0; j < 2048; ++j) {
    U key;
    if (j % 2)
      key = t.initial.rows[next(qrng) % t.initial.rows.size()].key;
    else
      do {
        key = query_key(c, qrng);
      } while (std::binary_search(keys.begin(), keys.end(), key));
    t.queries.push_back(key);
  }
  t.identities = c.population + c.operations;
  return t;
}
Trace after_trace(const Trace &t) {
  Trace result = t;
  std::map<U, unsigned> rows;
  for (auto e : t.initial.rows)
    rows[e.key] = e.id;
  for (const auto &op : t.operations) {
    rows.erase(op.erase);
    rows[op.row.key] = op.row.id;
  }
  result.initial = {};
  result.initial.columns.resize(columns_max);
  for (auto [key, id] : rows) {
    result.initial.rows.push_back({key, id});
    for (unsigned c = 0; c < columns_max; ++c)
      result.initial.columns[c].push_back(value(id, c));
  }
  U random = 373;
  result.queries.clear();
  for (unsigned q = 0; q < t.queries.size(); ++q) {
    U key;
    if (q % 2)
      key = result.initial.rows[next(random) % rows.size()].key;
    else
      do {
        key = query_key(t.config, random);
      } while (rows.contains(key));
    result.queries.push_back(key);
  }
  return result;
}
std::unique_ptr<Index> make_index(const std::string &m, const Profile &p,
                                  const Image &im, unsigned ids,
                                  bool accounting) {
  if (accounting)
    return std::make_unique<Tree<true>>(m, p, im, ids);
  return std::make_unique<Tree<false>>(m, p, im, ids);
}
U cycle(Index &index, const Trace &t, const Profile &p, bool verify) {
  U sum = 0;
  for (unsigned j = 0; j < t.operations.size(); ++j) {
    const auto &op = t.operations[j];
    index.erase(op.erase);
    index.insert(op.row, op.values.data());
    U result = index.point(op.row.key);
    if (verify)
      require(result == row_digest(op.row, op.values.data(), p.columns),
              "insert oracle");
    sum += result;
    sum += index.point(op.probe);
    if (j % 8 == 0)
      sum += index.scan(op.probe, 16);
    if (p.references && j % 4 == 0)
      sum += index.secondary(op.row.id, j % p.references);
  }
  return sum;
}
void check_all() {
  auto domain = trace({"domain", 128, 128, 0, 2});
  auto final_domain = after_trace(domain);
  for (auto *t : {&domain, &final_domain})
    for (unsigned q = 0; q < t->queries.size(); ++q) {
      U key = t->queries[q];
      require((key >> 48) < 16, "collision query domain");
      bool found = std::binary_search(
          t->initial.rows.begin(), t->initial.rows.end(), Entry{key, 0},
          [](Entry a, Entry b) { return a.key < b.key; });
      require(found == bool(q % 2), "query hit ratio");
    }
  for (auto p : profiles())
    for (auto m : methods())
      for (bool accounting : {false, true})
        for (unsigned slack : {0u, 25u}) {
          p.natural_slack = slack;
          Image empty;
          empty.columns.resize(p.columns);
          auto emptiness = make_index(m, p, empty, 300, accounting);
          std::array<U, columns_max> zero{};
          for (unsigned i = 0; i < 300; ++i)
            emptiness->insert({U(i) * 293, i}, zero.data());
          for (unsigned i = 0; i < 300; ++i)
            emptiness->erase(U(i) * 293);
          emptiness->validate();
          require(emptiness->scan(0, 16) == 0 &&
                      emptiness->scan(absent, 1) == 0,
                  "empty scan");
          emptiness->insert({0, 0}, zero.data());
          emptiness->validate();
          for (Case c : std::vector<Case>{{"tiny", 12, 80, 32},
                                          {"splits", 400, 600, 4096, 0, 1},
                                          {"radix", 600, 300, 0, 1},
                                          {"fullbyte", 230, 200, 256}}) {
            Trace t = trace(c);
            auto index = make_index(m, p, t.initial, t.identities, accounting);
            index->validate();
            std::map<U, std::pair<unsigned, std::array<U, columns_max>>> oracle;
            for (unsigned i = 0; i < t.initial.rows.size(); ++i) {
              std::array<U, columns_max> v{};
              for (unsigned col = 0; col < columns_max; ++col)
                v[col] = t.initial.columns[col][i];
              oracle[t.initial.rows[i].key] = {t.initial.rows[i].id, v};
            }
            unsigned n = 0;
            for (const auto &op : t.operations) {
              index->erase(op.erase);
              oracle.erase(op.erase);
              index->insert(op.row, op.values.data());
              oracle[op.row.key] = {op.row.id, op.values};
              require(index->point(op.row.key) ==
                          row_digest(op.row, op.values.data(), p.columns),
                      "mutation result");
              if (++n % 37 == 0 || n == t.operations.size()) {
                index->validate();
                auto im = index->image();
                require(im.rows.size() == oracle.size(), "image count");
                unsigned row = 0;
                for (auto &[k, r] : oracle) {
                  require(im.rows[row] == Entry{k, r.first}, "image keys");
                  for (unsigned col = 0; col < p.columns; ++col)
                    require(im.columns[col][row] == r.second[col],
                            "image column");
                  ++row;
                }
                for (U lower : {U{0}, op.row.key, op.erase, absent})
                  for (unsigned count : {1u, 16u, 1000u}) {
                    U expected = 0;
                    auto it = oracle.lower_bound(lower);
                    for (unsigned q = 0; q < count && it != oracle.end();
                         ++q, ++it)
                      expected += p.columns ? it->second.second[0] : it->first;
                    require(index->scan(lower, count) == expected,
                            "range oracle");
                  }
              }
            }
            // A retained source remains readable while a remapped edition is
            // built.
            auto im = index->image();
            auto rebuilt =
                make_index("blocked", p, im, t.identities, accounting);
            rebuilt->validate();
            index->validate();
            require(rebuilt->image().rows == im.rows, "conversion identity");
          }
          if (m == "natural1024" && p.name == "deps") {
            Trace t = trace({"threshold", 1600, 2200, 0, 0, 1});
            auto index = make_index(m, p, t.initial, t.identities, accounting);
            cycle(*index, t, p, true);
            index->validate();
            auto expected = after_trace(t);
            auto actual = index->image();
            require(actual.rows == expected.initial.rows &&
                        actual.columns == expected.initial.columns,
                    "large terminal transition");
          }
          std::cerr << "checked " << m << '/' << p.name
                    << " accounting=" << accounting << " slack=" << slack
                    << '\n';
        }
}
} // namespace remap

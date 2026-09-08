#include "model.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <stdexcept>
#ifdef __aarch64__
#include <arm_neon.h>
#endif

namespace signatures {
U mix(U x) {
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
uint32_t tag(unsigned f, uint32_t v, U s) {
  return uint32_t(mix((U(f) << 32 | v) ^ mix(s))) & 15;
}
uint32_t shared(unsigned f, uint32_t v, U s, unsigned w) {
  U h = mix((U(f) << 32 | v) ^ mix(s));
  unsigned a = h % w, b = (h >> 32) % (w - 1);
  if (b >= a)
    ++b;
  return (uint32_t(1) << a) | (uint32_t(1) << b);
}
unsigned atoms(Expr e) {
  return e == Expr::one ? 1 : e == Expr::both ? 2 : e == Expr::either ? 3 : 4;
}
bool evaluate(Expr e, const std::array<bool, 4> &a) {
  switch (e) {
  case Expr::one:
    return a[0];
  case Expr::both:
    return a[0] && a[1];
  case Expr::either:
    return (a[0] && a[1]) || a[2];
  case Expr::cnf:
    return (a[0] || a[2]) && (a[1] || a[3]);
  }
  return false;
}
std::vector<Case> cases() {
  return {
      {"absent", "scalar", "independent", Expr::both, {0, 1, 2, 3}, 0},
      {"single", "scalar", "independent", Expr::one, {0, 1, 2, 3}, 0},
      {"rare", "scalar", "independent", Expr::both, {0, 1, 2, 3}, 100},
      {"near", "scalar", "near", Expr::both, {0, 1, 2, 3}, 0},
      {"far", "scalar", "independent", Expr::both, {0, 7, 2, 3}, 100},
      {"broad", "scalar", "independent", Expr::both, {0, 7, 2, 3}, 3000},
      {"clustered", "scalar", "clustered", Expr::both, {0, 7, 2, 3}, 3000},
      {"hot", "scalar", "hot", Expr::both, {0, 1, 2, 3}, 9000},
      {"or", "scalar", "independent", Expr::either, {0, 1, 2, 3}, 100},
      {"or_broad", "scalar", "independent", Expr::either, {0, 1, 2, 3}, 3000},
      {"cnf", "scalar", "independent", Expr::cnf, {0, 1, 2, 3}, 100},
      {"exclusive", "scalar", "exclusive", Expr::both, {0, 1, 2, 3}, 5000},
      {"paired", "scalar", "paired", Expr::both, {0, 1, 2, 3}, 5000},
      {"range", "range", "uniform"},
      {"ties", "range", "ties"},
      {"range_clustered", "range", "clustered"},
      {"text", "text", "source"},
      {"text_long", "text", "synthetic"}};
}
std::vector<std::string> methods(const Case &c) {
  if (c.family == "range")
    return {"native", "prefix16", "prefix8"};
  if (c.family == "text")
    return {"direct", "gram8", "gram16", "gram32", "gram256", "term_bits"};
  return {"aos",
          "soa",
          "tag32",
          "tag16",
          "tag8",
          "tag8_eager",
          "tag8_cnf",
          "joint64_cnf",
          "joint16",
          "joint64",
          "joint256",
          "marginal64",
          "shared8",
          "shared16",
          "shared32",
          "union64",
          "joint16_planar",
          "joint64_planar",
          "joint256_planar"};
}
Fixture fixture(Case c, size_t n, U seed, const std::string &path) {
  Fixture f;
  f.config = c;
  std::mt19937_64 rng(seed);
  // Four query targets are reserved outside ordinary values. Every query is
  // shared by all blocks.
  for (unsigned q = 0; q < 4; ++q) {
    Query query{c.expr, c.fields, {q, q, q, q}, 0, 0, {}};
    query.lo =
        c.distribution == "ties" ? 0x12345640u : 0x40000000u + q * 0x01000000u;
    query.hi =
        c.distribution == "ties" ? 0x123456bfu : 0xbfffffffu - q * 0x01000000u;
    if (c.distribution == "near")
      query.values[0] = 0;
    f.queries.push_back(query);
  }
  if (c.family == "text") {
    f.queries[0].term = "Accident";
    f.queries[1].term = "lane blocked";
    f.queries[2].term = "construction";
    f.queries[3].term = "unobtainium";
    if (c.distribution == "source") {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        throw std::runtime_error("Cannot open prepared text");
      uint32_t len;
      while (in.read(reinterpret_cast<char *>(&len), 4)) {
        if (len > 1000000)
          throw std::runtime_error("Invalid string length");
        std::string s(len, ' ');
        if (!in.read(s.data(), len))
          throw std::runtime_error("Truncated string");
        f.strings.push_back(std::move(s));
      }
      if (f.strings.empty() || !in.eof())
        throw std::runtime_error("Empty/invalid text input");
      if (n < f.strings.size())
        f.strings.resize(n);
    } else {
      const std::string alphabet = " abcdefghijklmnopqrstuvwxyz";
      for (size_t i = 0; i < n; ++i) {
        std::string s(512, ' ');
        for (char &x : s)
          x = alphabet[rng() % alphabet.size()];
        if (i % 100 == 0)
          s.replace(240, 8, "Accident");
        if (i % 10 == 0)
          s.replace(320, 12, "lane blocked");
        f.strings.push_back(std::move(s));
      }
    }
    return f;
  }
  f.rows.resize(n);
  for (size_t i = 0; i < n; ++i) {
    for (unsigned j = 0; j < 8; ++j) {
      uint32_t v = uint32_t(rng()) | 0x80000000u;
      if (rng() % 10000 < c.rate)
        v = c.distribution == "hot" ? 0 : uint32_t(rng() % 4);
      f.rows[i][j] = v;
    }
    if (c.distribution == "near")
      f.rows[i][0] = 0;
    if (c.distribution == "exclusive" || c.distribution == "paired") {
      unsigned q = unsigned((i / 2) % 4);
      f.rows[i][0] = (i % 2 == 0) ? q : 0x80000000u + q;
      bool second = c.distribution == "paired" ? i % 2 == 0 : i % 2 != 0;
      f.rows[i][1] = second ? q : 0x80000000u + q;
    }
    if (c.family == "range")
      f.rows[i][0] = c.distribution == "ties"
                         ? 0x12345600u | uint32_t(rng() % 256)
                         : uint32_t(rng());
  }
  if (c.distribution == "clustered")
    std::sort(f.rows.begin(), f.rows.end(), [&](auto &a, auto &b) {
      return c.family == "range"
                 ? a[0] < b[0]
                 : std::pair(a[0], a[7]) < std::pair(b[0], b[7]);
    });
  for (auto &col : f.columns)
    col.resize(n);
  for (size_t i = 0; i < n; ++i)
    for (unsigned j = 0; j < 8; ++j)
      f.columns[j][i] = f.rows[i][j];
  return f;
}
namespace {
uint32_t code(const Row &r, U seed) {
  uint32_t v = 0;
  for (unsigned j = 0; j < 8; ++j)
    v |= tag(j, r[j], seed) << (4 * j);
  return v;
}
uint32_t signature(const Row &r, U seed, unsigned w) {
  uint32_t v = 0;
  for (unsigned j = 0; j < 8; ++j)
    v |= shared(j, r[j], seed, w);
  return v;
}
Bits grams(const std::string &s, unsigned w, U seed) {
  Bits b{};
  for (size_t j = 2; j < s.size(); ++j) {
    U h = mix((U(uint8_t(s[j - 2])) | (U(uint8_t(s[j - 1])) << 8) |
               (U(uint8_t(s[j])) << 16)) ^
              mix(seed));
    for (unsigned k = 0; k < 2; ++k) {
      unsigned pos = unsigned(h >> (32 * k)) % w;
      b[pos / 64] |= U(1) << (pos % 64);
    }
  }
  return b;
}
bool truth(const Fixture &f, const Query &q, size_t i) {
  if (f.config.family == "range")
    return f.rows[i][0] >= q.lo && f.rows[i][0] <= q.hi;
  if (f.config.family == "text")
    return f.strings[i].find(q.term) != std::string::npos;
  std::array<bool, 4> a{};
  for (unsigned j = 0; j < atoms(q.expr); ++j)
    a[j] = f.rows[i][q.fields[j]] == q.values[j];
  return evaluate(q.expr, a);
}
void output(Result &r, std::vector<U> &out, size_t i) {
  ++r.count;
  r.sum += i + 1;
  out[i / 64] |= U(1) << (i % 64);
}
constexpr U full = ~U(0);
U formula(Expr e, const std::array<U, 4> &a) {
  switch (e) {
  case Expr::one:
    return a[0];
  case Expr::both:
    return a[0] & a[1];
  case Expr::either:
    return (a[0] & a[1]) | a[2];
  case Expr::cnf:
    return (a[0] | a[2]) & (a[1] | a[3]);
  }
  return 0;
}
U lanes(unsigned n) { return n == 16 ? full : (U(1) << (4 * n)) - 1; }
// Four mask bits per row let NEON enumerate candidates without a scalar
// movemask loop.
#ifdef __aarch64__
U packed(uint8x16_t v) {
  return vget_lane_u64(
      vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(v), 4)), 0);
}
template <class T> U compare(const T *p, T m, T q, unsigned n) {
  if (n == 16) {
    if constexpr (sizeof(T) == 1)
      return packed(
          vceqq_u8(vandq_u8(vld1q_u8(p), vdupq_n_u8(m)), vdupq_n_u8(q)));
    if constexpr (sizeof(T) == 2) {
      auto a =
          vceqq_u16(vandq_u16(vld1q_u16(p), vdupq_n_u16(m)), vdupq_n_u16(q));
      auto b = vceqq_u16(vandq_u16(vld1q_u16(p + 8), vdupq_n_u16(m)),
                         vdupq_n_u16(q));
      return packed(vcombine_u8(vmovn_u16(a), vmovn_u16(b)));
    }
    if constexpr (sizeof(T) == 4) {
      uint16x4_t v[4];
      for (unsigned k = 0; k < 4; ++k)
        v[k] = vmovn_u32(vceqq_u32(
            vandq_u32(vld1q_u32(p + 4 * k), vdupq_n_u32(m)), vdupq_n_u32(q)));
      return packed(vcombine_u8(vmovn_u16(vcombine_u16(v[0], v[1])),
                                vmovn_u16(vcombine_u16(v[2], v[3]))));
    }
  }
  U r = 0;
  for (unsigned i = 0; i < n; ++i)
    if ((p[i] & m) == q)
      r |= U(15) << (4 * i);
  return r;
}
template <class T> U below(const T *p, T q, unsigned n) {
  if (n == 16) {
    if constexpr (sizeof(T) == 1)
      return packed(vcltq_u8(vld1q_u8(p), vdupq_n_u8(q)));
    if constexpr (sizeof(T) == 2)
      return packed(
          vcombine_u8(vmovn_u16(vcltq_u16(vld1q_u16(p), vdupq_n_u16(q))),
                      vmovn_u16(vcltq_u16(vld1q_u16(p + 8), vdupq_n_u16(q)))));
    if constexpr (sizeof(T) == 4) {
      uint16x4_t v[4];
      for (unsigned k = 0; k < 4; ++k)
        v[k] = vmovn_u32(vcltq_u32(vld1q_u32(p + 4 * k), vdupq_n_u32(q)));
      return packed(vcombine_u8(vmovn_u16(vcombine_u16(v[0], v[1])),
                                vmovn_u16(vcombine_u16(v[2], v[3]))));
    }
  }
  U r = 0;
  for (unsigned i = 0; i < n; ++i)
    if (p[i] < q)
      r |= U(15) << (4 * i);
  return r;
}
#else
template <class T> U compare(const T *p, T m, T q, unsigned n) {
  U r = 0;
  for (unsigned i = 0; i < n; ++i)
    if ((p[i] & m) == q)
      r |= U(15) << (4 * i);
  return r;
}
template <class T> U below(const T *p, T q, unsigned n) {
  U r = 0;
  for (unsigned i = 0; i < n; ++i)
    if (p[i] < q)
      r |= U(15) << (4 * i);
  return r;
}
#endif
} // namespace
Store build(const Fixture &f, const std::string &m, U seed) {
  Store s;
  s.method = m;
  const size_t n = f.size();
  if (m == "aos" || m == "soa" || m == "direct" || m == "native")
    return s;
  if (m == "term_bits") {
    for (auto &q : f.queries) {
      std::vector<U> b((n + 63) / 64);
      for (size_t i = 0; i < n; ++i)
        if (truth(f, q, i))
          b[i / 64] |= U(1) << (i % 64);
      s.exact.push_back(std::move(b));
    }
    return s;
  }
  if (m.starts_with("gram")) {
    s.width = unsigned(std::stoul(m.substr(4)));
    if (s.width == 256) {
      s.text.resize(n);
      for (size_t i = 0; i < n; ++i)
        s.text[i] = grams(f.strings[i], 256, seed);
    } else if (s.width == 32) {
      s.words.resize(n);
      for (size_t i = 0; i < n; ++i)
        s.words[i] = uint32_t(grams(f.strings[i], 32, seed)[0]);
    } else if (s.width == 16) {
      s.halves[0].resize(n);
      for (size_t i = 0; i < n; ++i)
        s.halves[0][i] = uint16_t(grams(f.strings[i], 16, seed)[0]);
    } else {
      s.bytes[0].resize(n);
      for (size_t i = 0; i < n; ++i)
        s.bytes[0][i] = uint8_t(grams(f.strings[i], 8, seed)[0]);
    }
    return s;
  }
  if (m.starts_with("joint"))
    s.block = unsigned(std::stoul(m.substr(5)));
  if (m == "marginal64" || m == "union64")
    s.block = 64;
  bool sh = m.starts_with("shared") || m == "union64";
  s.width = m == "tag32" || m == "union64"    ? 32
            : m == "tag16" || m == "prefix16" ? 16
                                              : 8;
  if (m.starts_with("shared"))
    s.width = unsigned(std::stoul(m.substr(6)));
  if (s.width == 32)
    s.words.resize(n);
  if (s.width == 16)
    for (unsigned j = 0; j < (sh ? 1 : 2); ++j)
      s.halves[j].resize(n);
  if (s.width == 8)
    for (unsigned j = 0; j < (sh ? 1 : 4); ++j)
      s.bytes[j].resize(n);
  for (size_t i = 0; i < n; ++i) {
    uint32_t v = sh ? signature(f.rows[i], seed, s.width)
                 : f.config.family == "range" ? f.rows[i][0]
                                              : code(f.rows[i], seed);
    if (m.ends_with("_cnf"))
      v = (v & ~0xff0u) | ((v >> 4 & 15u) << 8) | ((v >> 8 & 15u) << 4);
    if (s.width == 32)
      s.words[i] = v;
    else if (s.width == 16)
      for (unsigned j = 0; j < (sh ? 1 : 2); ++j)
        s.halves[j][i] =
            uint16_t(v >> (16 * (f.config.family == "range" ? 1 - j : j)));
    else
      for (unsigned j = 0; j < (sh ? 1 : 4); ++j)
        s.bytes[j][i] =
            uint8_t(v >> (8 * (f.config.family == "range" ? 3 - j : j)));
  }
  size_t blocks = s.block ? (n + s.block - 1) / s.block : 0;
  if (m.starts_with("joint")) {
    bool planar = m.ends_with("_planar");
    if (planar)
      for (auto &plane : s.joint_planes)
        plane.resize(blocks);
    else
      s.joint.resize(blocks);
    for (size_t i = 0; i < n; ++i)
      for (unsigned p = 0; p < 4; ++p) {
        unsigned v = s.bytes[p][i];
        auto &bits =
            planar ? s.joint_planes[p][i / s.block] : s.joint[i / s.block][p];
        bits[v / 64] |= U(1) << (v % 64);
      }
  }
  if (m == "marginal64") {
    s.marginal.resize(blocks);
    for (size_t i = 0; i < n; ++i)
      for (unsigned j = 0; j < 8; ++j)
        s.marginal[i / s.block][j] |= uint16_t(1 << tag(j, f.rows[i][j], seed));
  }
  if (m == "union64") {
    s.unions.resize(blocks);
    for (size_t i = 0; i < n; ++i)
      s.unions[i / s.block] |= s.words[i];
  }
  return s;
}
U Store::allocated() const {
  U b = words.capacity() * 4 + text.capacity() * sizeof(Bits) +
        joint.capacity() * sizeof(joint[0]) +
        marginal.capacity() * sizeof(marginal[0]) + unions.capacity() * 4;
  for (auto &x : bytes)
    b += x.capacity();
  for (auto &x : halves)
    b += x.capacity() * 2;
  for (auto &x : exact)
    b += x.capacity() * 8;
  for (auto &x : joint_planes)
    b += x.capacity() * sizeof(Bits);
  return b;
}
Result oracle(const Fixture &f, const Query &q, std::vector<U> *mask) {
  std::vector<U> scratch;
  if (!mask)
    mask = &scratch;
  mask->assign((f.size() + 63) / 64, 0);
  Result r;
  for (size_t i = 0; i < f.size(); ++i)
    if (truth(f, q, i))
      output(r, *mask, i);
  return r;
}
namespace {
template <bool Track>
Result execute(const Fixture &f, const Store &s, const Query &q, U seed,
               std::vector<U> &out, Work &w) {
  const size_t n = f.size();
  out.assign((n + 63) / 64, 0);
  Result result;
  if (s.method == "aos" || s.method == "direct") {
    for (size_t i = 0; i < n; ++i)
      if (truth(f, q, i))
        output(result, out, i);
    if constexpr (Track) {
      w.residuals += n;
      if (f.config.family == "text")
        for (auto &x : f.strings)
          w.text_bytes += x.size();
    }
    return result;
  }
  if (s.method == "term_bits") {
    auto it = std::find_if(f.queries.begin(), f.queries.end(),
                           [&](auto &x) { return x.term == q.term; });
    if (it == f.queries.end())
      throw std::runtime_error("Unprepared term");
    out = s.exact[size_t(it - f.queries.begin())];
    for (size_t j = 0; j < out.size(); ++j) {
      U b = out[j];
      while (b) {
        unsigned k = std::countr_zero(b);
        ++result.count;
        result.sum += j * 64 + k + 1;
        b &= b - 1;
      }
    }
    if constexpr (Track) {
      w.plane_bytes += out.size() * 8;
      w.certain += result.count;
    }
    return result;
  }
  bool sh = s.method.starts_with("shared") || s.method == "union64";
  const bool reorder = s.method.ends_with("_cnf");
  const bool eager = s.method == "tag8_eager";
  const bool conjunction = q.expr == Expr::one || q.expr == Expr::both;
  const bool planar = !s.joint_planes[0].empty();
  const bool joint = planar || !s.joint.empty();
  auto slot = [&](unsigned field) {
    return reorder ? (field == 1 ? 2 : field == 2 ? 1 : field) : field;
  };
  std::array<uint32_t, 4> wanted{};
  for (unsigned j = 0; j < atoms(q.expr); ++j)
    wanted[j] = sh ? shared(q.fields[j], q.values[j], seed, s.width)
                   : tag(q.fields[j], q.values[j], seed);
  std::array<Bits, 4> allowed{};
  std::array<bool, 4> used{};
  for (unsigned j = 0; j < atoms(q.expr); ++j)
    used[slot(q.fields[j]) / 2] = true;
  if (joint)
    for (unsigned p = 0; p < 4; ++p)
      if (used[p])
        for (unsigned v = 0; v < 256; ++v) {
          std::array<bool, 4> a{true, true, true, true};
          for (unsigned j = 0; j < atoms(q.expr); ++j)
            if (slot(q.fields[j]) / 2 == p)
              a[j] = ((v >> (4 * (slot(q.fields[j]) % 2))) & 15) == wanted[j];
          if (evaluate(q.expr, a))
            allowed[p][v / 64] |= U(1) << (v % 64);
        }
  Bits tq{};
  if (f.config.family == "text")
    tq = grams(q.term, s.width, seed);
  // Compile masks once per query, including fused conjunctions within each
  // plane.
  std::array<uint32_t, 4> plane_mask{}, plane_want{}, atom_mask{}, atom_want{};
  std::array<unsigned, 4> atom_plane{};
  std::array<bool, 4> relevant{};
  const unsigned planes =
      s.width && f.config.family == "scalar" ? (sh ? 1 : 32 / s.width) : 0;
  if (planes)
    for (unsigned j = 0; j < atoms(q.expr); ++j) {
      unsigned p = sh ? 0 : slot(q.fields[j]) * 4 / s.width;
      unsigned shift = sh ? 0 : slot(q.fields[j]) * 4 % s.width;
      atom_plane[j] = p;
      atom_mask[j] = sh ? wanted[j] : 15u << shift;
      atom_want[j] = wanted[j] << shift;
      relevant[p] = true;
      plane_mask[p] |= atom_mask[j];
      plane_want[p] |= atom_want[j];
    }
  // Flat scans need no outer block handoff. Rollups pay their actual block
  // loop.
  const size_t step = s.block ? s.block : n;
  for (size_t block = 0; block < n; block += step) {
    size_t end = std::min(n, block + step);
    bool possible = true;
    if (s.block) {
      if constexpr (Track)
        ++w.blocks;
      if (joint)
        for (unsigned p = 0; p < 4; ++p)
          if (used[p]) {
            U any = 0;
            const auto &bits = planar ? s.joint_planes[p][block / s.block]
                                      : s.joint[block / s.block][p];
            for (unsigned k = 0; k < 4; ++k)
              any |= bits[k] & allowed[p][k];
            possible &= any != 0;
            if constexpr (Track)
              w.summary_bytes += 32;
          }
      if (!s.marginal.empty()) {
        std::array<bool, 4> a{};
        for (unsigned j = 0; j < atoms(q.expr); ++j)
          a[j] = (s.marginal[block / s.block][q.fields[j]] &
                  (1 << wanted[j])) != 0;
        possible = evaluate(q.expr, a);
        if constexpr (Track)
          w.summary_bytes += 2 * atoms(q.expr);
      }
      if (!s.unions.empty()) {
        std::array<bool, 4> a{};
        for (unsigned j = 0; j < atoms(q.expr); ++j)
          a[j] = (s.unions[block / s.block] & wanted[j]) == wanted[j];
        possible = evaluate(q.expr, a);
        if constexpr (Track)
          w.summary_bytes += 4;
      }
      if (!possible) {
        if constexpr (Track)
          ++w.skipped;
        continue;
      }
    }
    for (size_t i = block; i < end; i += 16) {
      unsigned count = unsigned(std::min<size_t>(16, end - i));
      U valid = lanes(count), active = valid;
      if (s.method == "soa") {
        std::array<U, 4> a{};
        for (unsigned j = 0; j < atoms(q.expr); ++j)
          a[j] = compare(f.columns[q.fields[j]].data() + i, ~uint32_t(0),
                         q.values[j], count);
        active = formula(q.expr, a) & valid;
        if constexpr (Track) {
          w.plane_bytes += 4 * count * atoms(q.expr);
          w.certain += std::popcount(active) / 4;
        }
      } else if (s.method == "native") {
        auto data = f.columns[0].data() + i;
        active = (~below(data, q.lo, count)) &
                 (below(data, q.hi, count) |
                  compare(data, ~uint32_t(0), q.hi, count)) &
                 valid;
        if constexpr (Track) {
          ++w.groups[0];
          w.plane_bytes += count * 4;
          w.certain += std::popcount(active) / 4;
        }
      } else if (f.config.family == "range") {
        U low = valid, high = valid;
        unsigned width = s.method == "native" ? 32 : s.width;
        for (unsigned p = 0; p < 32 / width; ++p) {
          if (!(low | high))
            break;
          unsigned shift = 32 - width * (p + 1);
          uint32_t mask =
              width == 32 ? ~uint32_t(0) : (uint32_t(1) << width) - 1;
          uint32_t lo = (q.lo >> shift) & mask, hi = (q.hi >> shift) & mask;
          U lt, gt, el, eh;
          auto stage = [&]<class T>(const T *data) {
            el = compare(data, T(mask), T(lo), count);
            eh = compare(data, T(mask), T(hi), count);
            lt = below(data, T(lo), count);
            gt = ~(below(data, T(hi), count) | eh);
          };
          if (width == 32)
            stage(f.columns[0].data() + i);
          else if (width == 16)
            stage(s.halves[p].data() + i);
          else
            stage(s.bytes[p].data() + i);
          active &= ~((low & lt) | (high & gt));
          low &= el & active;
          high &= eh & active;
          if constexpr (Track) {
            ++w.groups[p];
            w.plane_bytes += count * width / 8;
          }
        }
        if (q.lo > q.hi)
          active = 0;
        if constexpr (Track)
          w.certain += std::popcount(active) / 4;
      } else if (f.config.family == "text") {
        if (s.width == 8)
          active = compare(s.bytes[0].data() + i, uint8_t(tq[0]),
                           uint8_t(tq[0]), count);
        else if (s.width == 16)
          active = compare(s.halves[0].data() + i, uint16_t(tq[0]),
                           uint16_t(tq[0]), count);
        else if (s.width == 32)
          active = compare(s.words.data() + i, uint32_t(tq[0]), uint32_t(tq[0]),
                           count);
        else {
          active = 0;
          for (unsigned k = 0; k < count; ++k) {
            bool yes = true;
            for (unsigned p = 0; p < 4; ++p)
              yes &= (s.text[i + k][p] & tq[p]) == tq[p];
            if (yes)
              active |= U(15) << (k * 4);
          }
        }
        if constexpr (Track) {
          ++w.groups[0];
          w.plane_bytes += count * s.width / 8;
        }
      } else {
        std::array<U, 4> a{valid, valid, valid, valid};
        for (unsigned p = 0; p < planes; ++p) {
          if (!relevant[p])
            continue;
          if (!active && !eager)
            break;
          if (conjunction) {
            uint32_t mask = plane_mask[p], want = plane_want[p];
            if (s.width == 32)
              active &= compare(s.words.data() + i, mask, want, count);
            else if (s.width == 16)
              active &= compare(s.halves[p].data() + i, uint16_t(mask),
                                uint16_t(want), count);
            else
              active &= compare(s.bytes[p].data() + i, uint8_t(mask),
                                uint8_t(want), count);
          } else {
            for (unsigned j = 0; j < atoms(q.expr); ++j)
              if (atom_plane[j] == p) {
                uint32_t m = atom_mask[j], v = atom_want[j];
                if (s.width == 32)
                  a[j] = compare(s.words.data() + i, m, v, count);
                else if (s.width == 16)
                  a[j] = compare(s.halves[p].data() + i, uint16_t(m),
                                 uint16_t(v), count);
                else
                  a[j] = compare(s.bytes[p].data() + i, uint8_t(m), uint8_t(v),
                                 count);
              }
            active = formula(q.expr, a) & valid;
          }
          if constexpr (Track) {
            ++w.groups[p];
            w.plane_bytes += count * s.width / 8;
          }
        }
      }
      bool exact = s.method == "soa" || f.config.family == "range";
      if constexpr (Track)
        w.candidates += std::popcount(active) / 4;
      while (active) {
        unsigned lane = std::countr_zero(active) / 4;
        active &= ~(U(15) << (lane * 4));
        size_t row = i + lane;
        if constexpr (Track)
          if (!exact) {
            ++w.residuals;
            if (f.config.family == "text")
              w.text_bytes += f.strings[row].size();
          }
        if (exact || truth(f, q, row))
          output(result, out, row);
      }
    }
  }
  return result;
}
} // namespace
Result scan(const Fixture &f, const Store &s, const Query &q, U seed,
            std::vector<U> &out, Work *work) {
  Work ignored;
  return work ? execute<true>(f, s, q, seed, out, *work)
              : execute<false>(f, s, q, seed, out, ignored);
}
void replace(Row &row, Store &s, size_t id, unsigned field, uint32_t value,
             U seed) {
  row[field] = value;
  if (s.method == "aos")
    return;
  if (s.block)
    throw std::runtime_error("Rollup replacement not in timed mutation probe");
  if (s.method.starts_with("shared")) {
    auto v = signature(row, seed, s.width);
    if (s.width == 32)
      s.words[id] = v;
    else if (s.width == 16)
      s.halves[0][id] = uint16_t(v);
    else
      s.bytes[0][id] = uint8_t(v);
    return;
  }
  uint32_t v = tag(field, value, seed), shift = field * 4 % s.width,
           mask = 15u << shift;
  if (s.width == 32)
    s.words[id] = (s.words[id] & ~mask) | (v << shift);
  else if (s.width == 16) {
    auto &x = s.halves[field / 4][id];
    x = uint16_t((x & ~mask) | (v << shift));
  } else {
    auto &x = s.bytes[field / 2][id];
    x = uint8_t((x & ~mask) | (v << shift));
  }
}
} // namespace signatures

using word = unsigned long long;
constexpr word mask = (word(1) << 40) - 1;
constexpr word factor = 0xd6e8feb86659fd93ULL;

namespace impl {
static inline word plain(word x) { return x & mask; }
static inline word intrinsic(word x) { return __builtin_ia32_bzhi_di(x, 40U); }
static inline word extract(word x) { return __builtin_ia32_bextr_u64(x, 40U << 8); }
static inline word narrow(word x) { return word((unsigned _BitInt(40))x); }
static inline word unsigned_count(word x) {
  unsigned count = 40;
  return __builtin_ia32_bzhi_di(x, count);
}
static inline word shift_pair(word x) { return (x << 24) >> 24; }
static inline word count_only(word x) {
  unsigned count = 40;
  asm("" : "+r"(count));
  return __builtin_ia32_bzhi_di(x, count);
}
}

#define CASE(NAME, F) \
extern "C" word NAME(word x) { return impl::F(x); } \
extern "C" word NAME##_producer(unsigned i, word seed) { \
  return impl::F((word(i) * factor & mask) ^ seed); \
} \
extern "C" word NAME##_loop(const word* data, unsigned n, word seed) { \
  word sum = 0; \
  for (unsigned i = 0; i < n; ++i) { \
    word x = impl::F((word(i) * factor & mask) ^ seed); \
    sum += data[x & 255] ^ (x >> 10); \
  } \
  return sum; \
}
CASE(plain, plain)
CASE(intrinsic, intrinsic)
CASE(extract, extract)
CASE(narrow, narrow)
CASE(unsigned_count, unsigned_count)
CASE(shift_pair, shift_pair)
CASE(count_only, count_only)

extern "C" word plain_load(const word* x) { return impl::plain(*x); }
extern "C" word extract_load(const word* x) { return impl::extract(*x); }

template<unsigned Width> static inline word width_extract(word x) {
  if constexpr (Width == 0) return 0;
  else if constexpr (Width >= 64) return x;
  else if constexpr (Width <= 32) return x & ((word(1) << Width) - 1);
  else return __builtin_ia32_bextr_u64(x, Width << 8);
}
#define WIDTH(W) extern "C" word extract_width_##W(word x) { return width_extract<W>(x); }
WIDTH(0) WIDTH(1) WIDTH(8) WIDTH(16) WIDTH(24) WIDTH(32)
WIDTH(33) WIDTH(40) WIDTH(48) WIDTH(56) WIDTH(63) WIDTH(64)
WIDTH(65) WIDTH(256) WIDTH(4294967295u)

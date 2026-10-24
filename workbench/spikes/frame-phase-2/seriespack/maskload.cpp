// Masked versus plain 64-byte loads over a cold buffer (Zen 5 question: are
// SeriesPack's masked Local loads what makes compact flushed scans slow?).
// Each step reads `take` bytes at stride `take`, sums them with SAD; the
// buffer is flushed before every timed pass. Prints ns per 256 bytes read.
#include <immintrin.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
static void flush(const std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i < n; i += 64) _mm_clflushopt(p + i);
    _mm_mfence();
}
template <bool Masked> __attribute__((noinline)) std::uint64_t pass(const std::uint8_t* p, std::size_t n, unsigned take) {
    __m512i acc = _mm512_setzero_si512();
    const std::uint64_t m = take == 64 ? ~0ull : (1ull << take) - 1;
    for (std::size_t off = 0; off + 64 <= n; off += take) {
        const __m512i x = Masked ? _mm512_maskz_loadu_epi8(m, p + off) : _mm512_maskz_mov_epi8(m, _mm512_loadu_si512(p + off));
        acc = _mm512_add_epi64(acc, _mm512_sad_epu8(x, _mm512_setzero_si512()));
    }
    return _mm512_reduce_add_epi64(acc);
}
int main() {
    const std::size_t n = std::size_t{64} << 20;
    auto* p = static_cast<std::uint8_t*>(std::aligned_alloc(64, n));
    for (std::size_t i = 0; i < n; ++i) p[i] = static_cast<std::uint8_t>(i * 2654435761u >> 13);
    for (unsigned take : {24u, 40u, 48u, 56u, 60u, 64u})
        for (int masked = 0; masked < 2; ++masked)
            for (int cold = 0; cold < 2; ++cold) {
                std::vector<double> t;
                std::uint64_t s = 0;
                const std::size_t len = cold ? n : 256 << 10;
                for (int r = 0; r < 9; ++r) {
                    if (cold) flush(p, len);
                    const auto a = std::chrono::steady_clock::now();
                    s += masked ? pass<true>(p, len, take) : pass<false>(p, len, take);
                    const auto b = std::chrono::steady_clock::now();
                    t.push_back(std::chrono::duration<double, std::nano>(b - a).count() / (len / 256.0));
                }
                std::nth_element(t.begin(), t.begin() + 4, t.end());
                std::printf("{\"take\":%u,\"masked\":%d,\"cold\":%d,\"ns_per_256_bytes\":%.3f,\"check\":%llu}\n", take, masked, cold, t[4], (unsigned long long)(s & 0xff));
            }
}

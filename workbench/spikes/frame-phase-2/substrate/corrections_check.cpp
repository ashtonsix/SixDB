// Corrections against an oracle array: random writes (repeated hot rows and
// one row in every block), then SUM, COUNT, masks, masked SUM and points.
#include "corrections.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <string>

namespace fi = frame_image;
using fi::u64;
using fi::u128;

int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    std::ifstream in(std::string(argv[1]) + "/" + argv[2] + ".u64", std::ios::binary | std::ios::ate);
    const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(in.tellg()) / 8, 2999808) / 256 * 256;
    std::vector<u64> v(n);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(8 * n));
    const fi::image img = fi::build(v, fi::placement::by_kind);
    const fi::kind_index index = fi::index_by_kind(img);
    fi::corrected c(img);
    std::mt19937_64 rng(11);
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    for (int pass = 0; pass < 3; ++pass) {
        for (std::size_t k = 0; k < 4096; ++k) {
            const std::size_t row = pass == 0 ? rng() % 64 : pass == 1 ? (k % (n / 256)) * 256 + rng() % 256 : rng() % n;
            const u64 x = *lo + rng() % (*hi - *lo + 1);
            c.write(row, x), v[row] = x;
        }
        u128 sum = 0;
        u64 cnt = 0;
        const u64 t = v[rng() % n];
        for (u64 x : v)
            sum += x, cnt += x >= t;
        std::vector<u64> masks(n / 64);
        const u64 mc = c.mask_ge(t, masks.data());
        u128 msum = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (v[i] >= t)
                msum += v[i];
        bool ok = c.sum(index) == sum && c.count_ge(t) == cnt && mc == cnt && c.masked_sum(masks.data()) == msum;
        for (int q = 0; q < 10000 && ok; ++q) {
            const std::size_t r = rng() % n;
            ok = c.point(r) == v[r];
        }
        std::printf("%s pass %d: %s (%zu corrections)\n", argv[2], pass, ok ? "ok" : "MISMATCH", c.entries);
        if (!ok)
            return 1;
    }
}

// Front A's pinned codec peers beside the front C leaves, at the same widths,
// rows and timing: TurboPFor horizontal bit packing (bitgetx32 direct access,
// bitunpack32), its 256-value vertical SIMD unpack (x86; 128-value on NEON),
// and FastLanes FFOR on 1024-value vectors. For SUM a peer decodes 256 rows
// (1024 for FastLanes) into an L1 scratch vector and sums it with the lane
// leaf: decode-then-consume, no second memory pass. Every result is checked
// against the u64 arm.
//
// usage: frame_bodies_peers ROWS ROUNDS K...
#include "catalogue.h"
#include "packed.h"
#include "timing.h"

#include <fls/ffor.hpp>
#include <fls/unffor.hpp>
#include <ic.h>
// As front A's check: DAC (bitgetx32) through upstream's own vp4.h.
#define TURBOPFOR_DAC
#include <vp4.h>

#include <cstring>

using namespace bodies;

namespace {
template <class T> u64 sum_lanes(const T *x, unsigned n) {
    u64 s = 0;
    for (unsigned i = 0; i < n; i += block_rows)
        s += lanes<T>::sum(reinterpret_cast<const u8 *>(x + i));
    return s;
}

struct images {
    std::size_t blocks;
    unsigned k;
    std::vector<u64> v;
    std::vector<u8> plane, local, native, logical;
    std::vector<u8> tp_h;   // TurboPFor horizontal, 32k bytes per 256 rows
    std::vector<u8> tp_v;   // TurboPFor vertical SIMD
    std::size_t tp_v_stride = 0;
    std::vector<u32> fl;    // FastLanes FFOR u32 vectors of 1024
};

template <class T> void fastlanes_pack(const std::vector<u64> &v, unsigned k, std::vector<T> &out) {
    const std::size_t vectors = v.size() / 1024, words = std::size_t{k} * 1024 / (8 * sizeof(T));
    out.assign(vectors * words + 64, 0);
    std::vector<T> in(1024);
    for (std::size_t q = 0; q < vectors; ++q) {
        for (unsigned i = 0; i < 1024; ++i)
            in[i] = static_cast<T>(v[q * 1024 + i]);
        const T base = 0;
        ffor::ffor(in.data(), out.data() + q * words, static_cast<u8>(k), &base);
    }
}

void cell(std::size_t rows, unsigned rounds, unsigned k, std::mt19937_64 &rng) {
    const std::size_t blocks = rows / 1024 * 4;  // whole FastLanes vectors
    const u64 top = (u64{1} << k) - 1;
    images im{blocks, k, std::vector<u64>(blocks * block_rows), {}, {}, {}, {}, {}, {}, 0, {}};
    for (auto &x : im.v)
        x = rng() & top;
    const auto enc = [&](layout l, std::vector<u8> &out) {
        out.assign(blocks * block_bytes(l, k) + 256, 0);
        for (std::size_t b = 0; b < blocks; ++b)
            encode(l, k, im.v.data() + b * block_rows, out.data() + b * block_bytes(l, k));
    };
    enc(layout::plane256, im.plane);
    enc(layout::local8, im.local);
    enc(layout::lane, im.native);
    im.logical.assign(blocks * 2048 + 256, 0);
    std::memcpy(im.logical.data(), im.v.data(), blocks * 2048);
    std::vector<u32> in(block_rows);
    im.tp_h.assign(blocks * 32 * k + 256, 0);
    for (std::size_t b = 0; b < blocks; ++b) {
        for (unsigned i = 0; i < block_rows; ++i)
            in[i] = static_cast<u32>(im.v[b * block_rows + i]);
        bitpack32(in.data(), block_rows, im.tp_h.data() + b * 32 * k, k);
    }
#if defined(__AVX2__)
    im.tp_v_stride = 32 * k;
    im.tp_v.assign(blocks * im.tp_v_stride + 256, 0);
    for (std::size_t b = 0; b < blocks; ++b) {
        for (unsigned i = 0; i < block_rows; ++i)
            in[i] = static_cast<u32>(im.v[b * block_rows + i]);
        bitpack256v32(in.data(), block_rows, im.tp_v.data() + b * im.tp_v_stride, k);
    }
#else
    im.tp_v_stride = 32 * k;  // two 128-value vertical blocks
    im.tp_v.assign(blocks * im.tp_v_stride + 256, 0);
    for (std::size_t b = 0; b < blocks; ++b) {
        for (unsigned i = 0; i < block_rows; ++i)
            in[i] = static_cast<u32>(im.v[b * block_rows + i]);
        u8 *o = im.tp_v.data() + b * im.tp_v_stride;
        bitpack128v32(in.data(), 128, o, k);
        bitpack128v32(in.data() + 128, 128, o + 16 * k, k);
    }
#endif
    fastlanes_pack<u32>(im.v, k, im.fl);
    std::vector<u8> fl8;
    std::vector<u16> fl16;
    if (k <= 8)
        fastlanes_pack<u8>(im.v, k, fl8);
    else if (k <= 16)
        fastlanes_pack<u16>(im.v, k, fl16);

    std::vector<u32> points(std::min<std::size_t>(rows, 1u << 20));
    for (auto &p : points)
        p = static_cast<u32>((rng() % blocks) << 8 | (rng() & 255));
    alignas(64) static u32 scratch[1024 + 64];
    alignas(64) static u8 scratch8[1024 + 64];
    alignas(64) static u16 scratch16[1024 + 64];
    const std::size_t fl_words = std::size_t{k} * 1024 / 32;

    // SUM
    {
        std::vector<arm> arms;
        const auto packed_arm = [&](const char *name, layout l, const std::vector<u8> &bytes) {
            const packed_sweep f = packed_catalogue(l, l == layout::lane ? k : k).f[0];
            const u8 *p = bytes.data();
            arms.push_back({name, bytes.size() - 256, [=] { return f(p, blocks, {}); }});
        };
        {
            const packed_sweep f = packed_catalogue(layout::lane, 64).f[0];
            const u8 *p = im.logical.data();
            arms.push_back({"u64", im.logical.size() - 256, [=] { return f(p, blocks, {}); }});
        }
        packed_arm("native", layout::lane, im.native);
        packed_arm("plane256", layout::plane256, im.plane);
        {
            const u8 *p = im.tp_v.data();
            const std::size_t stride = im.tp_v_stride;
            arms.push_back({"turbopfor-v", im.tp_v.size() - 256, [=] {
                                u64 s = 0;
                                for (std::size_t b = 0; b < blocks; ++b) {
#if defined(__AVX2__)
                                    bitunpack256v32(const_cast<u8 *>(p + b * stride), block_rows, scratch, k);
#else
                                    bitunpack128v32(const_cast<u8 *>(p + b * stride), 128, scratch, k);
                                    bitunpack128v32(const_cast<u8 *>(p + b * stride + 16 * k), 128, scratch + 128, k);
#endif
                                    s += sum_lanes(scratch, block_rows);
                                }
                                return s;
                            }});
        }
        {
            const u8 *p = im.tp_h.data();
            arms.push_back({"turbopfor-h", im.tp_h.size() - 256, [=] {
                                u64 s = 0;
                                for (std::size_t b = 0; b < blocks; ++b) {
                                    bitunpack32(p + b * 32 * k, block_rows, scratch, k);
                                    s += sum_lanes(scratch, block_rows);
                                }
                                return s;
                            }});
        }
        {
            const u32 *p = im.fl.data();
            arms.push_back({"fastlanes-u32", 4 * (im.fl.size() - 64), [=] {
                                u64 s = 0;
                                const u32 base = 0;
                                for (std::size_t q = 0; q < blocks / 4; ++q) {
                                    unffor::unffor(p + q * fl_words, scratch, static_cast<u8>(k), &base);
                                    s += sum_lanes(scratch, 1024);
                                }
                                return s;
                            }});
        }
        if (k <= 8) {
            const u8 *p = fl8.data();
            const std::size_t words = std::size_t{k} * 1024 / 8;
            arms.push_back({"fastlanes-narrow", fl8.size() - 64, [=] {
                                u64 s = 0;
                                const u8 base = 0;
                                for (std::size_t q = 0; q < blocks / 4; ++q) {
                                    unffor::unffor(p + q * words, scratch8, static_cast<u8>(k), &base);
                                    s += sum_lanes(scratch8, 1024);
                                }
                                return s;
                            }});
        } else if (k <= 16) {
            const u16 *p = fl16.data();
            const std::size_t words = std::size_t{k} * 1024 / 16;
            arms.push_back({"fastlanes-narrow", 2 * (fl16.size() - 64), [=] {
                                u64 s = 0;
                                const u16 base = 0;
                                for (std::size_t q = 0; q < blocks / 4; ++q) {
                                    unffor::unffor(p + q * words, scratch16, static_cast<u8>(k), &base);
                                    s += sum_lanes(scratch16, 1024);
                                }
                                return s;
                            }});
        }
        char head[160];
        std::snprintf(head, sizeof head, "\"body\":\"peers\",\"k\":%u,\"rows\":%zu,\"op\":\"sum\",\"variant\":\"all\"", k,
                      blocks * block_rows);
        measure(head, arms, rounds, double(blocks), nullptr, 0);
    }
    // Unpack to u32 (decode into an L1 scratch, folded).
    {
        std::vector<arm> arms;
        const auto packed_arm = [&](const char *name, layout l, unsigned kk, const std::vector<u8> &bytes) {
            const packed_sweep f = packed_catalogue(l, kk).f[static_cast<unsigned>(op::unpack)];
            const u8 *p = bytes.data();
            sweep_args a;
            a.scratch = scratch;
            arms.push_back({name, bytes.size() - 256, [=] { return f(p, blocks, a); }});
        };
        packed_arm("native", layout::lane, k, im.native);
        packed_arm("plane256", layout::plane256, k, im.plane);
        packed_arm("local8", layout::local8, k, im.local);
        {
            const u8 *p = im.tp_v.data();
            const std::size_t stride = im.tp_v_stride;
            arms.push_back({"turbopfor-v", im.tp_v.size() - 256, [=] {
                                u64 f = 0;
                                for (std::size_t b = 0; b < blocks; ++b) {
#if defined(__AVX2__)
                                    bitunpack256v32(const_cast<u8 *>(p + b * stride), block_rows, scratch, k);
#else
                                    bitunpack128v32(const_cast<u8 *>(p + b * stride), 128, scratch, k);
                                    bitunpack128v32(const_cast<u8 *>(p + b * stride + 16 * k), 128, scratch + 128, k);
#endif
                                    asm volatile("" ::"r"(scratch) : "memory");
                                    f += scratch[b & 255];
                                }
                                return f;
                            }});
        }
        {
            const u32 *p = im.fl.data();
            arms.push_back({"fastlanes-u32", 4 * (im.fl.size() - 64), [=] {
                                u64 f = 0;
                                const u32 base = 0;
                                for (std::size_t q = 0; q < blocks / 4; ++q) {
                                    unffor::unffor(p + q * fl_words, scratch, static_cast<u8>(k), &base);
                                    asm volatile("" ::"r"(scratch) : "memory");
                                    // The same rows as four packed blocks' (b & 255) folds.
                                    for (unsigned j = 0; j < 4; ++j)
                                        f += scratch[256 * j + ((4 * q + j) & 255)];
                                }
                                return f;
                            }});
        }
        char head[160];
        std::snprintf(head, sizeof head, "\"body\":\"peers\",\"k\":%u,\"rows\":%zu,\"op\":\"unpack\",\"variant\":\"u32\"", k,
                      blocks * block_rows);
        measure(head, arms, rounds, double(blocks), nullptr, 0);
    }
    // Independent points.
    {
        std::vector<arm> arms;
        const auto packed_arm = [&](const char *name, layout l, unsigned kk, const std::vector<u8> &bytes) {
            const packed_sweep f = packed_catalogue(l, kk).f[static_cast<unsigned>(op::point)];
            const u8 *p = bytes.data();
            sweep_args a;
            a.points = points.data(), a.npoints = points.size();
            arms.push_back({name, bytes.size() - 256, [=] { return f(p, blocks, a); }});
        };
        packed_arm("native", layout::lane, k, im.native);
        packed_arm("local8", layout::local8, k, im.local);
        packed_arm("plane256", layout::plane256, k, im.plane);
        {
            const u8 *p = im.tp_h.data();
            const u32 *pt = points.data();
            const std::size_t np = points.size();
            arms.push_back({"turbopfor-h", im.tp_h.size() - 256, [=] {
                                u64 s = 0;
                                for (std::size_t i = 0; i < np; ++i)
                                    s += bitgetx32(p + (pt[i] >> 8) * 32 * k, pt[i] & 255, k);
                                return s;
                            }});
        }
        char head[160];
        std::snprintf(head, sizeof head, "\"body\":\"peers\",\"k\":%u,\"rows\":%zu,\"op\":\"point\",\"variant\":\"uniform\"", k,
                      blocks * block_rows);
        measure(head, arms, rounds, double(points.size()), nullptr, 0);
    }
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s ROWS ROUNDS K...\n", argv[0]);
        return 2;
    }
    const std::size_t rows = std::strtoull(argv[1], nullptr, 10);
    const unsigned rounds = static_cast<unsigned>(std::atoi(argv[2]));
    std::mt19937_64 rng(13);
    for (int i = 3; i < argc; ++i)
        cell(rows, rounds, static_cast<unsigned>(std::atoi(argv[i])), rng);
}

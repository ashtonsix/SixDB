#include "plan.h"

#include "kernels.h"
#include "packing.h"

#include <cstring>

namespace frame_substrate {
namespace {
constexpr unsigned rows = fe::block_rows;
constexpr std::uint8_t constant_class = 0, packed_class = 1, runs_class = 2, other_class = 3;

// Run values of one uncoded runs block into the narrowest lane, and run
// lengths from the u8 (end - 1) ends after the values plane. Lengths are
// differences of adjacent ends, so no loop carries a dependency and both
// consumers below vectorise (phase 1 walked runs with a carried end).
template <class T, class F>
void runs_visit(fe::lane_range<T> range, const std::uint8_t *body, unsigned w, unsigned n, F &&consume) {
    alignas(64) T v[rows];
    alignas(64) std::uint16_t len[rows];
    if constexpr (sizeof(T) == 1) {
        if (w == 8)  // Local width 8 stores values as bytes: read in place
            std::memcpy(v, body, n);
        else
            range(body, 0, n, v);
    } else {
        range(body, 0, n, v);
    }
    const std::uint8_t *e = body + fe::plane_bytes(w, n);
    len[0] = static_cast<std::uint16_t>(unsigned{e[0]} + 1);
    for (unsigned r = 1; r < n; ++r)
        len[r] = static_cast<std::uint16_t>(unsigned{e[r]} - e[r - 1]);
    consume(static_cast<const T *>(v), static_cast<const std::uint16_t *>(len), n);
}

template <class F> void runs_group(const plan &p, unsigned w, F &&visit_block) {
    const auto &cat = fe::plane_catalogue;
    const std::uint8_t *payload = p.image->payload.data();
    for (std::uint32_t b : p.runs[w]) {
        const std::uint8_t *body = payload + p.offset[b];
        const unsigned n = p.count[b];
        if (w <= 8)
            visit_block(b, [&](auto &&f) { runs_visit<std::uint8_t>(cat.range8[w], body, w, n, f); });
        else if (w <= 16)
            visit_block(b, [&](auto &&f) { runs_visit<std::uint16_t>(cat.range16[w], body, w, n, f); });
        else if (w <= 32)
            visit_block(b, [&](auto &&f) { runs_visit<std::uint32_t>(cat.range32[w], body, w, n, f); });
        else
            visit_block(b, [&](auto &&f) { runs_visit<u64>(cat.range[w], body, w, n, f); });
    }
}
}  // namespace

std::size_t plan::bytes() const {
    std::size_t n = base.capacity() * 8 + offset.capacity() * 4 + count.capacity() * 2 +
                    constant.capacity() * 4 + other.capacity() * 4;
    for (const auto &g : packed)
        n += g.capacity() * 4;
    for (const auto &g : runs)
        n += g.capacity() * 4;
    return n;
}

plan build(const fe::column &c) {
    plan p;
    p.image = &c;
    const std::size_t n = c.blocks.size();
    p.base.resize(n);
    p.offset.resize(n);
    p.count.resize(n);
    for (std::size_t b = 0; b < n; ++b) {
        const auto &d = c.blocks[b];
        p.base[b] = d.base;
        p.offset[b] = d.offset;
        p.count[b] = d.count;
        const auto id = static_cast<std::uint32_t>(b);
        const unsigned w = fe::plane_width(d);
        std::uint8_t k = other_class;
        if (fe::is_coded(c, b))
            p.other.push_back(id);
        else if (d.type == fe::kind::constant)
            p.constant.push_back(id), k = constant_class;
        else if (d.type == fe::kind::packed && w <= 56)
            p.packed[w].push_back(id), k = packed_class;
        else if (d.type == fe::kind::runs && w >= 1)
            p.runs[w].push_back(id), k = runs_class;
        else
            p.other.push_back(id);
        p.cls.push_back(k);
        p.width.push_back(static_cast<std::uint8_t>(w));
    }
    return p;
}

u128 sum(const plan &p) {
    const auto &cat = fe::plane_catalogue;
    const std::uint8_t *payload = p.image->payload.data();
    // Each block's stored total is below 2^72, so groups accumulate the base
    // terms and the residual terms separately in u128.
    u128 bases = 0;
    for (std::uint32_t b : p.constant)
        bases += p.base[b];
    u128 total = 0;
    for (unsigned w = 1; w <= 56; ++w) {
        const auto &g = p.packed[w];
        if (g.empty())
            continue;
        const auto leaf = cat.sum[w];
        u128 residuals = 0;
        for (std::uint32_t b : g) {
            bases += p.base[b];
            residuals += leaf(payload + p.offset[b], rows);
        }
        total += residuals;
    }
    for (unsigned w = 1; w <= 64; ++w) {
        if (p.runs[w].empty())
            continue;
        runs_group(p, w, [&](std::uint32_t b, auto &&each) {
            bases += p.base[b];
            each([&](const auto *v, const std::uint16_t *len, unsigned n) {
                if (w <= 55) {  // 256 (2^w - 1) < 2^64
                    u64 s = 0;
                    for (unsigned r = 0; r < n; ++r)
                        s += u64{v[r]} * len[r];
                    total += s;
                } else {
                    for (unsigned r = 0; r < n; ++r)
                        total += static_cast<u128>(v[r]) * len[r];
                }
            });
        });
    }
    for (std::uint32_t b : p.other)
        total += fe::block_sum(*p.image, b);
    return total + bases * rows;
}

u64 count_ge(const plan &p, u64 t) {
    const auto &cat = fe::plane_catalogue;
    const std::uint8_t *payload = p.image->payload.data();
    u64 n = 0;
    for (std::uint32_t b : p.constant)
        n += p.base[b] >= t ? rows : 0;
    for (unsigned w = 1; w <= 56; ++w) {
        const auto &g = p.packed[w];
        if (g.empty())
            continue;
        const auto leaf = cat.count_ge[w];
        const u64 max = (u64{1} << w) - 1;
        for (std::uint32_t b : g) {
            const u64 base = p.base[b];
            if (base >= t) {
                n += rows;
                continue;
            }
            const u64 rhs = t - base;
            if (rhs <= max)
                n += leaf(payload + p.offset[b], rows, rhs);
        }
    }
    for (unsigned w = 1; w <= 64; ++w) {
        if (p.runs[w].empty())
            continue;
        runs_group(p, w, [&](std::uint32_t b, auto &&each) {
            const u64 base = p.base[b];
            if (base >= t) {
                n += rows;
                return;
            }
            const u64 rhs = t - base;
            if (w < 64 && rhs >> w)  // above every value the plane can hold
                return;
            each([&](const auto *v, const std::uint16_t *len, unsigned runs) {
                unsigned c = 0;
                for (unsigned r = 0; r < runs; ++r)
                    c += len[r] & -static_cast<unsigned>(u64{v[r]} >= rhs);
                n += c;
            });
        });
    }
    for (std::uint32_t b : p.other)
        n += fe::block_count_ge(*p.image, b, t);
    return n;
}
namespace {
// One block's run leaves, the same code the kind-index arm inlines.
template <class Consume> void runs_block(const plan &p, std::uint32_t b, unsigned w, Consume &&consume) {
    const auto &cat = fe::plane_catalogue;
    const std::uint8_t *body = p.image->payload.data() + p.offset[b];
    const unsigned n = p.count[b];
    if (w <= 8)
        runs_visit<std::uint8_t>(cat.range8[w], body, w, n, consume);
    else if (w <= 16)
        runs_visit<std::uint16_t>(cat.range16[w], body, w, n, consume);
    else if (w <= 32)
        runs_visit<std::uint32_t>(cat.range32[w], body, w, n, consume);
    else
        runs_visit<u64>(cat.range[w], body, w, n, consume);
}

// Residual sum of one runs block (base excluded).
u128 runs_residuals(const plan &p, std::uint32_t b, unsigned w) {
    u128 total = 0;
    runs_block(p, b, w, [&](const auto *v, const std::uint16_t *len, unsigned n) {
        if (w <= 55) {
            u64 s = 0;
            for (unsigned r = 0; r < n; ++r)
                s += u64{v[r]} * len[r];
            total = s;
        } else {
            for (unsigned r = 0; r < n; ++r)
                total += static_cast<u128>(v[r]) * len[r];
        }
    });
    return total;
}

u64 runs_count(const plan &p, std::uint32_t b, unsigned w, u64 t) {
    const u64 base = p.base[b];
    if (base >= t)
        return rows;
    const u64 rhs = t - base;
    if (w < 64 && rhs >> w)
        return 0;
    u64 c = 0;
    runs_block(p, b, w, [&](const auto *v, const std::uint16_t *len, unsigned n) {
        unsigned k = 0;
        for (unsigned r = 0; r < n; ++r)
            k += len[r] & -static_cast<unsigned>(u64{v[r]} >= rhs);
        c = k;
    });
    return c;
}
}  // namespace

u128 sum_per_block(const plan &p) {
    const auto &cat = fe::plane_catalogue;
    const std::uint8_t *payload = p.image->payload.data();
    u128 bases = 0, total = 0;
    const std::size_t n = p.cls.size();
    for (std::uint32_t b = 0; b < n; ++b) {
        const unsigned w = p.width[b];
        switch (p.cls[b]) {
        case constant_class: bases += p.base[b]; break;
        case packed_class: bases += p.base[b]; total += cat.sum[w](payload + p.offset[b], rows); break;
        case runs_class: bases += p.base[b]; total += runs_residuals(p, b, w); break;
        default: total += fe::block_sum(*p.image, b);
        }
    }
    return total + bases * rows;
}

u64 count_ge_per_block(const plan &p, u64 t) {
    const auto &cat = fe::plane_catalogue;
    const std::uint8_t *payload = p.image->payload.data();
    u64 c = 0;
    const std::size_t n = p.cls.size();
    for (std::uint32_t b = 0; b < n; ++b) {
        const unsigned w = p.width[b];
        const u64 base = p.base[b];
        switch (p.cls[b]) {
        case constant_class: c += base >= t ? rows : 0; break;
        case packed_class:
            if (base >= t)
                c += rows;
            else if (const u64 rhs = t - base; !(rhs >> w))
                c += cat.count_ge[w](payload + p.offset[b], rows, rhs);
            break;
        case runs_class: c += runs_count(p, b, w, t); break;
        default: c += fe::block_count_ge(*p.image, b, t);
        }
    }
    return c;
}

// Maximal address-order segments of equal (class, width), each run by one
// loop whose leaf is chosen once.
template <class Seg> void segments(const plan &p, Seg &&seg) {
    const std::size_t n = p.cls.size();
    for (std::uint32_t b = 0; b < n;) {
        std::uint32_t e = b + 1;
        while (e < n && p.cls[e] == p.cls[b] && p.width[e] == p.width[b])
            ++e;
        seg(p.cls[b], p.width[b], b, e);
        b = e;
    }
}

u128 sum_segments(const plan &p) {
    const auto &cat = fe::plane_catalogue;
    const std::uint8_t *payload = p.image->payload.data();
    u128 bases = 0, total = 0;
    segments(p, [&](std::uint8_t k, unsigned w, std::uint32_t first, std::uint32_t limit) {
        switch (k) {
        case constant_class:
            for (std::uint32_t b = first; b < limit; ++b)
                bases += p.base[b];
            break;
        case packed_class: {
            const auto leaf = cat.sum[w];
            u128 r = 0;
            for (std::uint32_t b = first; b < limit; ++b)
                bases += p.base[b], r += leaf(payload + p.offset[b], rows);
            total += r;
            break;
        }
        case runs_class:
            for (std::uint32_t b = first; b < limit; ++b)
                bases += p.base[b], total += runs_residuals(p, b, w);
            break;
        default:
            for (std::uint32_t b = first; b < limit; ++b)
                total += fe::block_sum(*p.image, b);
        }
    });
    return total + bases * rows;
}

u64 count_ge_segments(const plan &p, u64 t) {
    const auto &cat = fe::plane_catalogue;
    const std::uint8_t *payload = p.image->payload.data();
    u64 c = 0;
    segments(p, [&](std::uint8_t k, unsigned w, std::uint32_t first, std::uint32_t limit) {
        switch (k) {
        case constant_class:
            for (std::uint32_t b = first; b < limit; ++b)
                c += p.base[b] >= t ? rows : 0;
            break;
        case packed_class: {
            const auto leaf = cat.count_ge[w];
            for (std::uint32_t b = first; b < limit; ++b) {
                const u64 base = p.base[b];
                if (base >= t)
                    c += rows;
                else if (const u64 rhs = t - base; !(rhs >> w))
                    c += leaf(payload + p.offset[b], rows, rhs);
            }
            break;
        }
        case runs_class:
            for (std::uint32_t b = first; b < limit; ++b)
                c += runs_count(p, b, w, t);
            break;
        default:
            for (std::uint32_t b = first; b < limit; ++b)
                c += fe::block_count_ge(*p.image, b, t);
        }
    });
    return c;
}
}  // namespace frame_substrate

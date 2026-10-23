// Kind-batched execution over phase 1 images (front B, first probe).
//
// Phase 1's owner visits each 256-row block through borrow() and a switch on
// its kind, decoding packed planes into a 2 KiB u64 buffer. This probe keeps
// the same bytes and leaf kernels but changes the substrate: descriptors become
// columns, blocks are grouped by (kind, width) once per image, and each group
// runs one loop with its leaf chosen outside the loop. The question is how much
// of phase 1's per-block cost was dispatch and materialisation rather than the
// representation.
#pragma once

#include "material.h"

#include <array>
#include <cstdint>
#include <vector>

namespace frame_substrate {
namespace fe = frame_encoding;
using u64 = std::uint64_t;
using u128 = unsigned __int128;

// Descriptor planes and per-(kind, width) block lists for one image. Derived
// evidence: rebuilt from the descriptors, never stored with the image.
struct plan {
    const fe::column *image = nullptr;
    std::vector<u64> base;               // per block
    std::vector<std::uint32_t> offset;   // per block
    std::vector<std::uint16_t> count;    // per block
    std::vector<std::uint32_t> constant; // block ids
    std::array<std::vector<std::uint32_t>, 65> packed;  // by plane width
    std::array<std::vector<std::uint32_t>, 65> runs;    // by plane width
    std::vector<std::uint32_t> other;    // delta, patched, coded: phase 1 owner leaf
    std::size_t bytes() const;
};

plan build(const fe::column &c);

// Stored-domain SUM and COUNT(x >= t) over every block, with the same answers
// as phase 1's encoded_sum and encoded_count_ge.
u128 sum(const plan &p);
u64 count_ge(const plan &p, u64 t);
}  // namespace frame_substrate

#pragma once
#include "material.h"
#include <cstring>
#include <stdexcept>

namespace frame_material_witness {
// Temporary construction state shared by the native lifetime check and phase
// driver. Only metadata and the copied body enter a published Specification.
// Encoding policy belongs to the caller. Address-bound decoder preparation is
// disabled because the material is about to move to caller-owned storage.
struct EncodedRegion {
    fe::column staging;
    Region metadata;

    explicit EncodedRegion(std::span<const fe::u64> original,
                           fe::encode_options options = {}, unsigned first = 0,
                           unsigned padding = 32) {
        if (original.empty() || original.size() % fe::block_rows ||
            original.size() > region_blocks * fe::block_rows ||
            first > UINT32_MAX - original.size() ||
            unsigned(options.delta_checkpoints) > unsigned(fe::checkpoints::none) ||
            (options.dictionary != fe::dictionary_order::none &&
             options.dictionary != fe::dictionary_order::sorted &&
             options.dictionary != fe::dictionary_order::frequency))
            throw std::invalid_argument("encoded region does not fit the admitted geometry/laws");
        options.bind_decoders = false;
        staging = fe::encode(original, options);
        auto& r = metadata;
        r.block_count = unsigned(staging.blocks.size());
        r.first_position = first;
        r.readable_padding = padding;
        r.checkpoints = options.delta_checkpoints;
        if (!staging.parts.empty() && staging.parts[0].count) {
            const auto d = fe::dictionary_of(staging, 0);
            r.dictionary = {d.min, d.edition, 0, d.count, unsigned(d.bytes()), d.width, d.order};
            r.body_bytes = unsigned(d.bytes());
        }
        for (unsigned i = 0; i < r.block_count; ++i) {
            const auto k = fe::borrow(staging, i);
            const auto extent = fe::extent_of(k.d, k.delta_checkpoints, k.body, k.grant);
            if (!extent || extent->read_grant_bytes > k.grant || !fe::admits(k))
                throw std::logic_error("encoder produced an inadmissible block");
            auto& b = r.blocks[i];
            b.descriptor = k.d;
            b.descriptor.offset = 0;
            b.offset = r.body_bytes;
            b.body_bytes = unsigned(extent->body_bytes);
            b.grant_bytes = unsigned(extent->read_grant_bytes);
            b.rows = k.rows;
            b.coded = k.coded;
            b.dictionary_edition = k.coded ? k.dictionary.edition : fe::dictionary_identity{};
            r.body_bytes += b.grant_bytes;
        }
        if (!r.valid()) throw std::logic_error("encoder produced inadmissible region metadata");
    }

    // Destination is distinct mutable storage owned by the caller. Copy only
    // the admitted body; initialization of outer readable padding stays with
    // that owner. A short span refuses before any copy. This does not allocate,
    // capture, publish or transfer ownership. Neither member is mutated between
    // construction and copy; staging is discarded after native construction.
    void copy_into(orbital::MutableBytes destination) const {
        const auto& r = metadata;
        if (destination.size() < r.bytes())
            throw std::invalid_argument("short encoded-region destination");
        if (r.dictionary.count) {
            const auto d = fe::dictionary_of(staging, 0);
            std::memcpy(destination.data() + r.dictionary.offset, d.plane, r.dictionary.grant_bytes);
        }
        for (unsigned i = 0; i < r.block_count; ++i) {
            const auto& b = r.blocks[i];
            if (b.grant_bytes)
                std::memcpy(destination.data() + b.offset, fe::borrow(staging, i).body, b.grant_bytes);
        }
        if (!attach(r, destination.first(r.bytes())))
            throw std::logic_error("copied native image refused admission");
    }
};
} // namespace frame_material_witness

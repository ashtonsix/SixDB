#pragma once

#include "../ikea-composition/probes/ikea-heterogeneous/range_kernel.h"
#include <ikea/seriespack/detail/range_regions.h>
#include <ikea/seriespack/operations.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

// A bounded experimental consumer, not a new public cursor or BEC interface.
namespace bec_metadata {
namespace h = ikea::heterogeneous;
namespace sp = ikea::seriespack;
using ikea::integers::Bytes;
enum class Reader { specialized, native, materialized };

// All copies/moves own the resources. The child and bound reader store views by
// value: neither points at another member, a temporary view, or a range wrapper.
class Source {
    std::shared_ptr<const h::MetadataOwner> metadata_;
    std::shared_ptr<const h::BodyOwner> body_;
    std::shared_ptr<const h::AlignedBytes> query_;
    sp::const_view lengths_;
    sp::bound_reader reader_;
    sp::const_view admit() const;
public:
    Source(std::shared_ptr<const h::MetadataOwner> metadata,
           std::shared_ptr<const h::BodyOwner> body,
           std::shared_ptr<const h::AlignedBytes> query,
           sp::execution_target target = sp::execution_target::automatic)
        : metadata_(std::move(metadata)), body_(std::move(body)), query_(std::move(query)),
          lengths_(admit()), reader_(sp::bind_reader(lengths_, target).value()) {}
    const h::MetadataOwner& metadata() const { return *metadata_; }
    const h::BodyOwner& body() const { return *body_; }
    const h::AlignedBytes& query() const { return *query_; }
    const sp::const_view& lengths() const { return lengths_; }
    const sp::bound_reader& reader() const { return reader_; }
    void admit_range(unsigned first, unsigned count) const {
        if (first > body_->count || count > body_->count - first)
            throw std::invalid_argument("BEC metadata range");
    }
};

// group is a 16-record checkpoint below the true logical count N. The owner
// validated the complete scan128 capacity C, including zero length/population
// slack. The physical native read may include that slack; it emits no bodies.
template<Reader R>
[[gnu::always_inline]] inline h::EntryLanes16 refill(const Source& source, unsigned group) {
    const auto* base = source.metadata().bytes().data();
    const auto capacity = source.metadata().capacity();
    const auto logical_count = source.lengths().size();
    __builtin_assume(group % 16 == 0 && group < logical_count);
    if constexpr (R == Reader::specialized)
        return h::read_metadata16<h::MetadataKind::scan128>(base, capacity, group);
    else {
        const auto values = [&]() -> Bytes<16> {
            if constexpr (R == Reader::native) {
                const auto& plane = source.lengths().placement().payload;
                const auto* tile = reinterpret_cast<const std::uint8_t*>(plane.bytes.data())
                    + (group / 128) * plane.stride;
                return {sp::detail::range_regions::striped<6, 16>(tile, group % 128)};
            } else {
                std::uint8_t decoded[16];
                const auto active = std::min<std::size_t>(16, source.lengths().size() - group);
                source.reader().decode({group, group + active}, std::span(decoded, active));
                // Required tail initialization is inside the measured refill.
                // Predecessor lengths before a requested first are never cropped.
                if (active != 16) std::memset(decoded + active, 0, 16 - active);
                return Bytes<16>::load(decoded);
            }
        }();
        const auto* population = base + capacity / 8 + (group / 16) * 18;
        return h::metadata_detail::packed(Bytes<16>::load(population),
            ikea::integers::local_read16<1>(population + 16, 0), values,
            unsigned(ikea::integers::read_bytes<2>(base + group / 8)));
    }
}

template<Reader R> class Cursor {
    const Source& source_;
    unsigned next_, group_ = ~0u;
    h::EntryLanes16 entries_{};
public:
    Cursor(const Source& source, unsigned first) : source_(source), next_(first) {}
    [[gnu::always_inline]] std::uint32_t next() {
        const unsigned group = next_ & ~15u;
        if (group != group_) { entries_ = refill<R>(source_, group); group_ = group; }
        return h::metadata_entry_at(entries_, next_++ % 16);
    }
};

using Count = std::uint64_t (*)(const Source&, unsigned, unsigned);
using Refill = void (*)(const Source&, unsigned, std::uint32_t*);
Count count_kernel(Reader, h::Execution);
Refill refill_kernel(Reader);
} // namespace bec_metadata

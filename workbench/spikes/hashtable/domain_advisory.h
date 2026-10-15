#pragma once
#include <cstdint>
#include <optional>

namespace ht {
// Cold caller policy input, never an automatic representation change. This is
// an optimistic storage bound: bitmap plus packed occupied payload cells. Rank
// support, owner state, alignment and any dense codec overhead must fit inside
// the reported budget before a space saving is established. External multimap
// bag payloads are unchanged and must be excluded on both sides.
struct dense_domain_advisory {
    std::uint64_t bitmap_bytes;
    std::optional<std::uint64_t> minimum_index_bytes;
    std::uint64_t remaining_overhead_budget;
    bool worth_investigating()const {return remaining_overhead_budget!=0;}
};
template<unsigned K> constexpr dense_domain_advisory advise_dense_domain(
        std::uint64_t occupied,std::uint64_t bytes_per_payload_cell,std::uint64_t current_index_bytes) {
    static_assert(K>=8 && K<=64 && K%8==0);
    const auto bitmap=std::uint64_t(1)<<(K-3);
    const auto minimum=__uint128_t(bitmap)+__uint128_t(occupied)*bytes_per_payload_cell;
    const auto remaining=minimum<current_index_bytes?current_index_bytes-std::uint64_t(minimum):0;
    return {bitmap,minimum<=UINT64_MAX?std::optional<std::uint64_t>(minimum):std::nullopt,remaining};
}
} // namespace ht

#pragma once
#include <ikea/bec256/author/write.h>
#include <ikea/bec256/detail/write.h>

namespace bec_study {
namespace bc = ikea::bec256;
// Same checks and output contract as the first casing, retained as a compiled
// control while evaluating a different operation boundary.
std::expected<unsigned, bc::error> staged_encode(std::span<const bc::byte, 32>, unsigned,
                                                 bc::destination &, std::size_t,
                                                 ikea::source_write_journal &);

// Experimental bound operation: establish storage/control disjointness once.
// Capacity and effect availability are still checked on every call, and values
// are checked against the caller's population. Binding grants no extra writes.
// The named destination, journal and their storage associations remain stable;
// callers may consume/reset the journal between operations.
class bound_encoder {
    bc::destination &target_;
    ikea::source_write_journal &effects_;
    bound_encoder(bc::destination &target, ikea::source_write_journal &effects)
        : target_(target), effects_(effects) {}

  public:
    static std::expected<bound_encoder, bc::error> bind(bc::destination &,
                                                        ikea::source_write_journal &);
    std::expected<unsigned, bc::error> encode(std::span<const bc::byte, 32>, unsigned,
                                              std::size_t offset = 0) const;
};
} // namespace bec_study

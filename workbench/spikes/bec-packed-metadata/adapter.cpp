#include "adapter.h"

namespace bec_metadata {
sp::const_view Source::admit() const {
    h::PreparedRange prepared;
    if (!metadata_ || metadata_->kind() != h::MetadataKind::scan128 || !body_ ||
        h::prepare(metadata_, body_, query_, 0, body_->count, h::Execution::inlined, prepared)
            != h::AdmissionError::none)
        throw std::invalid_argument("BEC metadata source admission");
    const auto& bytes = metadata_->bytes();
    const auto offset = h::scan_length_offset(metadata_->capacity());
    const auto* lengths = reinterpret_cast<const std::byte*>(bytes.data() + offset);
    return sp::const_view::attach({6, 0, sp::geometry::striped}, body_->count,
        {{{lengths, bytes.size() - offset}, 96}, {}}).value();
}

template<Reader R, h::Execution E>
[[gnu::noinline]] std::uint64_t count(const Source& source, unsigned first, unsigned n) {
    Cursor<R> metadata(source, first);
    h::CountOperations<E> operations;
    return h::count_range(metadata, operations, source.body().bytes.data(),
                          source.query().data(), first, n);
}

// The standalone refill measurement includes this common u32[16] sink. Whole
// count operations above retain native entries and never materialize this array.
template<Reader R>
[[gnu::noinline]] void refill_stored(const Source& source, unsigned group, std::uint32_t* output) {
    h::store_metadata16(output, refill<R>(source, group));
}
Count count_kernel(Reader reader, h::Execution execution) {
    static constexpr Count kernels[3][2] = {
        {count<Reader::specialized, h::Execution::inlined>, count<Reader::specialized, h::Execution::split>},
        {count<Reader::native, h::Execution::inlined>, count<Reader::native, h::Execution::split>},
        {count<Reader::materialized, h::Execution::inlined>, count<Reader::materialized, h::Execution::split>}};
    return kernels[unsigned(reader)][unsigned(execution)];
}
Refill refill_kernel(Reader reader) {
    static constexpr Refill kernels[] = {refill_stored<Reader::specialized>,
        refill_stored<Reader::native>, refill_stored<Reader::materialized>};
    return kernels[unsigned(reader)];
}
} // namespace bec_metadata

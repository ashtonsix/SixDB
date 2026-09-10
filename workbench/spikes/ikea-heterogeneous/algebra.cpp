#include "algebra.h"
#include "algebra_native.h"
#include "metadata_point.h"
#include "selection.h"
#include <cstring>
#include <utility>

#ifndef SIXDB_IKEA_HETERO_OUTPUT_GRAIN
#define SIXDB_IKEA_HETERO_OUTPUT_GRAIN 2
#endif
static_assert(SIXDB_IKEA_HETERO_OUTPUT_GRAIN == 1 || SIXDB_IKEA_HETERO_OUTPUT_GRAIN == 2);

namespace ikea::heterogeneous {
namespace {
template<bool Compressed, bool InlineLocal> class SourceCursor {
  const BitsetView &source_;
  const ResolverBinding &resolver_;
  Resolution resolution_;
  unsigned group_ = ~0u;
  // A frame resolver defines all sixteen entries before the first access.
  // Point reads and empty selections do not need an initialized native frame.
  EntryLanes16 entries_;
public:
  SourceCursor(const BitsetView &source, const ResolverBinding &resolver)
      : source_(source), resolver_(resolver), resolution_(resolver.resolution) {}
  inline __attribute__((always_inline)) std::uint32_t resolve(unsigned ordinal) {
    if constexpr (!Compressed) return 0;
    else {
      if constexpr (!InlineLocal) {
        // The direct scalar record is a deliberately curated cheap leaf.
        if (source_.kind == MetadataKind::direct32)
          return read_metadata_point<MetadataKind::direct32>(source_.metadata, ordinal);
        if (resolution_ == Resolution::point)
          return resolver_.point(source_.metadata, ordinal);
      }
      const unsigned group = ordinal & ~15u;
      if (group != group_) {
        if constexpr (InlineLocal)
          entries_ = read_metadata16<MetadataKind::local16>(source_.metadata, 256, group);
        else resolver_.frame(source_.metadata, group, &entries_);
        group_ = group;
      }
      return metadata_entry_at(entries_, ordinal % 16);
    }
  }
};

template<bool Compressed>
inline __attribute__((always_inline)) algebra::Native1
read_one(const BitsetView &source, unsigned ordinal, std::uint32_t entry) {
  if constexpr (Compressed)
    return algebra::decode_bec1(source.data + entry_offset(entry), entry_population(entry));
  else return algebra::load_plain1(source.data + 32 * ordinal);
}
template<bool Compressed>
inline __attribute__((always_inline)) algebra::Native2
read_pair(const BitsetView &source, unsigned ordinal, std::uint32_t a, std::uint32_t b) {
  if constexpr (Compressed)
    return algebra::decode_bec2(source.data + entry_offset(a), entry_population(a),
                               source.data + entry_offset(b), entry_population(b));
  else return algebra::load_plain2(source.data + 32 * ordinal, source.data + 32 * (ordinal + 1));
}
template<bool Compressed>
inline __attribute__((always_inline)) bool terminal(std::uint32_t entry) {
  if constexpr (!Compressed) return false;
  else return entry_population(entry) == 0 || entry_population(entry) == 256;
}

template<bool LeftBec, bool RightBec, bool Union>
inline __attribute__((always_inline)) void write_one(const AlgebraBinding &binding,
    unsigned ordinal, std::uint32_t a, std::uint32_t b, std::uint8_t *out) {
  const auto pa = entry_population(a), pb = entry_population(b);
  // Local algebra uses admitted populations. It neither strengthens the caller's
  // selection nor substitutes for an Engine conjunction plan.
  if constexpr (LeftBec) {
    if (pa == (Union ? 256u : 0u)) { std::memset(out, Union ? 255 : 0, 32); return; }
  }
  if constexpr (RightBec) {
    if (pb == (Union ? 256u : 0u)) { std::memset(out, Union ? 255 : 0, 32); return; }
  }
  if constexpr (LeftBec) {
    if (pa == (Union ? 0u : 256u)) {
      if constexpr (RightBec) {
        if (terminal<true>(b)) { std::memset(out, pb == 256 ? 255 : 0, 32); return; }
      }
      algebra::store1(out, read_one<RightBec>(binding.right, ordinal, b)); return;
    }
  }
  if constexpr (RightBec) {
    if (pb == (Union ? 0u : 256u)) {
      algebra::store1(out, read_one<LeftBec>(binding.left, ordinal, a)); return;
    }
  }
  if constexpr (LeftBec && RightBec) {
    // Two encoded operands at ONE ordinal fill the decoder's two-input grain.
    const auto pair = algebra::decode_bec2(
        binding.left.data + entry_offset(a), pa, binding.right.data + entry_offset(b), pb);
    algebra::store1(out, algebra::combine_halves<Union>(pair));
  } else {
    algebra::store1(out, algebra::combine1<Union>(
        read_one<LeftBec>(binding.left, ordinal, a), read_one<RightBec>(binding.right, ordinal, b)));
  }
}

template<bool LeftBec, bool RightBec, bool Union>
inline __attribute__((always_inline)) void write_pair(const AlgebraBinding &binding,
    unsigned ordinal, std::uint32_t a0, std::uint32_t a1,
    std::uint32_t b0, std::uint32_t b1, std::uint8_t *out) {
  if (terminal<LeftBec>(a0) || terminal<LeftBec>(a1) ||
      terminal<RightBec>(b0) || terminal<RightBec>(b1)) {
    write_one<LeftBec, RightBec, Union>(binding, ordinal, a0, b0, out);
    write_one<LeftBec, RightBec, Union>(binding, ordinal + 1, a1, b1, out + 32);
  } else {
    // Two adjacent selected ordinals, with matching coordinates in both sources.
    const auto left = read_pair<LeftBec>(binding.left, ordinal, a0, a1);
    const auto right = read_pair<RightBec>(binding.right, ordinal, b0, b1);
    algebra::store2(out, out + 32, algebra::combine2<Union>(left, right));
  }
}

template<bool LeftBec, bool RightBec, bool Union, bool InlineLocal = false>
__attribute__((noinline)) void selected_kernel(const AlgebraBinding &binding,
    const SliceMask &mask, std::uint8_t *out) noexcept {
  SourceCursor<LeftBec, InlineLocal> left(binding.left, binding.left_resolver);
  SourceCursor<RightBec, InlineLocal> right(binding.right, binding.right_resolver);
  SelectedSlices selected(mask);
#pragma clang loop unroll(disable)
  for (unsigned ordinal = selected.next(); ordinal != 256; ordinal = selected.next()) {
    const auto a = left.resolve(ordinal), b = right.resolve(ordinal);
    if ((!LeftBec || SIXDB_IKEA_HETERO_OUTPUT_GRAIN == 2) &&
        ordinal < 255 && selected.peek() == ordinal + 1) {
      selected.next();
      const auto a1 = left.resolve(ordinal + 1), b1 = right.resolve(ordinal + 1);
      write_pair<LeftBec, RightBec, Union>(binding, ordinal, a, a1, b, b1, out + 32 * ordinal);
    } else write_one<LeftBec, RightBec, Union>(binding, ordinal, a, b, out + 32 * ordinal);
  }
}

bool overlap(const std::uint8_t *a, std::size_t an,
             const std::uint8_t *b, std::size_t bn) {
  const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
  return an && bn && (x <= y ? y - x < an : x - y < bn);
}
} // namespace

AdmissionError admit_plain(std::shared_ptr<const AlignedBytes> plain, AdmittedBitset &out) {
  if (!plain || plain->size() != 8192) return AdmissionError::query_extent;
  AdmittedBitset candidate;
  candidate.view_ = {plain->data(), nullptr, MetadataKind::direct32, false};
  candidate.plain_ = std::move(plain);
  out = std::move(candidate);
  return AdmissionError::none;
}
AdmissionError admit_bec(std::shared_ptr<const MetadataOwner> metadata,
                         std::shared_ptr<const BodyOwner> body, AdmittedBitset &out) {
  if (!metadata || !body || metadata->body().get() != body.get()) return AdmissionError::association;
  if (body->count != 256) return AdmissionError::range;
  const auto error = validate_metadata(metadata->kind(),
      {metadata->bytes().data(), metadata->bytes().size()}, metadata->capacity(), 256,
      {body->bytes.data(), body->bytes.size()}, body->logical_bytes);
  if (error != AdmissionError::none) return error;
  AdmittedBitset candidate;
  candidate.view_ = {body->bytes.data(), metadata->bytes().data(), metadata->kind(), true};
  candidate.metadata_ = std::move(metadata);
  candidate.body_ = std::move(body);
  out = std::move(candidate);
  return AdmissionError::none;
}
AlgebraError prepare_algebra(AdmittedBitset left, AdmittedBitset right,
    std::shared_ptr<AlignedBytes> output, SetOperation operation, Resolution left_resolution,
    Resolution right_resolution,
    AlgebraExecution execution, PreparedAlgebra &out) {
  if (!left.admitted() || !right.admitted()) return AlgebraError::source;
  if (!output || output->size() != 8192) return AlgebraError::output_extent;
  for (const auto *source : {&left, &right}) {
    if (overlap(output->data(), output->size(), source->view().data, source->readable_bytes()) ||
        (source->view().compressed && overlap(output->data(), output->size(), source->view().metadata,
                                             metadata_bytes(source->view().kind, 256))))
      return AlgebraError::overlap;
  }
  if (execution == AlgebraExecution::local_inline &&
      (!left.view().compressed || !right.view().compressed ||
       left.view().kind != MetadataKind::local16 || right.view().kind != MetadataKind::local16 ||
       left_resolution != Resolution::cached16 || right_resolution != Resolution::cached16))
    return AlgebraError::unsupported;
  // Union/intersection are commutative. Canonicalise only physical source order,
  // retaining each view together with the owner that admits its interpretation.
  if (!left.view().compressed && right.view().compressed) {
    std::swap(left, right);
    std::swap(left_resolution, right_resolution);
  }
  PreparedAlgebra candidate;
  candidate.left_ = std::move(left); candidate.right_ = std::move(right);
  candidate.output_ = std::move(output);
  candidate.binding_ = {candidate.left_.view(), candidate.right_.view(),
      bind_resolver(candidate.left_.view().kind, left_resolution),
      bind_resolver(candidate.right_.view().kind, right_resolution)};
  const bool is_union = operation == SetOperation::set_union;
  if (execution == AlgebraExecution::local_inline)
    candidate.kernel_ = is_union ? selected_kernel<true, true, true, true> : selected_kernel<true, true, false, true>;
  else if (candidate.binding_.right.compressed)
    candidate.kernel_ = is_union ? selected_kernel<true, true, true> : selected_kernel<true, true, false>;
  else if (candidate.binding_.left.compressed)
    candidate.kernel_ = is_union ? selected_kernel<true, false, true> : selected_kernel<true, false, false>;
  else candidate.kernel_ = is_union ? selected_kernel<false, false, true> : selected_kernel<false, false, false>;
  out = std::move(candidate);
  return AlgebraError::none;
}
void PreparedAlgebra::apply(const SliceMask &mask) const noexcept {
  if ((mask[0] & mask[1] & mask[2] & mask[3]) != ~std::uint64_t{0})
    std::memset(output_->data(), 0, 8192);
  kernel_(binding_, mask, output_->data());
}
} // namespace ikea::heterogeneous

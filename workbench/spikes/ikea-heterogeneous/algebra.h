#pragma once
#include "source.h"
#include <utility>

namespace ikea::heterogeneous {
using SliceMask = std::array<std::uint64_t, 4>;
enum class SetOperation { intersection, set_union };
enum class Resolution { point, cached16 };
enum class AlgebraExecution { factored, local_inline };

struct BitsetView {
  const std::uint8_t *data = nullptr;
  const std::uint8_t *metadata = nullptr;
  MetadataKind kind = MetadataKind::direct32;
  bool compressed = false;
};

// Cold source admission is independent of a particular selection or consumer.
// The caller establishes the common coordinate domain and immutable snapshots.
class AdmittedBitset {
  std::shared_ptr<const MetadataOwner> metadata_;
  std::shared_ptr<const BodyOwner> body_;
  std::shared_ptr<const AlignedBytes> plain_;
  BitsetView view_;
  friend AdmissionError admit_plain(std::shared_ptr<const AlignedBytes>, AdmittedBitset &);
  friend AdmissionError admit_bec(std::shared_ptr<const MetadataOwner>,
                                 std::shared_ptr<const BodyOwner>, AdmittedBitset &);
public:
  const BitsetView &view() const { return view_; }
  bool admitted() const { return bool(body_) || bool(plain_); }
  std::size_t readable_bytes() const {
    return body_ ? body_->bytes.size() : plain_ ? plain_->size() : 0;
  }
};
AdmissionError admit_plain(std::shared_ptr<const AlignedBytes>, AdmittedBitset &);
AdmissionError admit_bec(std::shared_ptr<const MetadataOwner>,
                        std::shared_ptr<const BodyOwner>, AdmittedBitset &);

struct EntryLanes16;
using PointResolver = std::uint32_t (*)(const std::uint8_t *, unsigned) noexcept;
using FrameResolver = void (*)(const std::uint8_t *, unsigned, EntryLanes16 *) noexcept;
struct ResolverBinding {
  PointResolver point;
  FrameResolver frame;
  Resolution resolution = Resolution::point;
};
ResolverBinding bind_resolver(MetadataKind, Resolution);
struct AlgebraBinding {
  BitsetView left, right;
  ResolverBinding left_resolver{}, right_resolver{};
};
using AlgebraKernel = void (*)(const AlgebraBinding &, const SliceMask &,
                                std::uint8_t *) noexcept;

enum class AlgebraError { none, source, output_extent, overlap, unsupported };
class PreparedAlgebra {
  AdmittedBitset left_, right_;
  std::shared_ptr<AlignedBytes> output_;
  AlgebraBinding binding_{};
  AlgebraKernel kernel_ = nullptr;
  friend AlgebraError prepare_algebra(AdmittedBitset, AdmittedBitset,
      std::shared_ptr<AlignedBytes>, SetOperation, Resolution, Resolution,
      AlgebraExecution, PreparedAlgebra &);
public:
  // 1 selects the original slice ordinal. This establishes the complete plain
  // result, including inactive zeros. Output is disjoint from sources and mask.
  void apply(const SliceMask &) const noexcept;
  // Active-only writer: selected slices are defined, inactive bytes untouched.
  // Useful for separating kernel work from the complete-result obligation.
  void apply_selected(const SliceMask &mask) const noexcept {
    kernel_(binding_, mask, output_->data());
  }
};
AlgebraError prepare_algebra(AdmittedBitset, AdmittedBitset,
    std::shared_ptr<AlignedBytes>, SetOperation, Resolution, Resolution,
    AlgebraExecution, PreparedAlgebra &);

// Convenience for comparisons using the same strategy on both sources.
inline AlgebraError prepare_algebra(AdmittedBitset left, AdmittedBitset right,
    std::shared_ptr<AlignedBytes> output, SetOperation operation, Resolution resolution,
    AlgebraExecution execution, PreparedAlgebra &prepared) {
  return prepare_algebra(std::move(left), std::move(right), std::move(output),
      operation, resolution, resolution, execution, prepared);
}

} // namespace ikea::heterogeneous

// Isolated checks are built into the operation check target.
int check_algebra_native();

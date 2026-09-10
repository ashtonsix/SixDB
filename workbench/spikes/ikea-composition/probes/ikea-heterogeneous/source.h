#pragma once
#include "metadata_format.h"
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace ikea::heterogeneous {
using PlainBlock = std::array<std::uint8_t, 32>;
class AlignedBytes {
  struct Free {
    void operator()(std::uint8_t *) const noexcept;
  };
  std::unique_ptr<std::uint8_t, Free> data_;
  std::size_t size_;

public:
  explicit AlignedBytes(std::size_t size);
  std::uint8_t *data() { return data_.get(); }
  const std::uint8_t *data() const { return data_.get(); }
  std::size_t size() const { return size_; }
};
struct BodyOwner {
  AlignedBytes bytes;
  unsigned count;
  unsigned logical_bytes;
  BodyOwner(unsigned n, unsigned logical)
      : bytes(logical + 64), count(n), logical_bytes(logical) {}
};
// A construction object, not retained by hot prepared reads. Its catalog is
// derived from the original bitsets and is used to build actual metadata bytes.
class EncodedSource {
  std::shared_ptr<const BodyOwner> body_;
  std::vector<Entry> entries_;
  friend EncodedSource encode_source(std::span<const PlainBlock>);
  friend class MetadataOwner;

public:
  const std::shared_ptr<const BodyOwner> &body() const { return body_; }
  std::span<const Entry> entries() const { return entries_; }
};
EncodedSource encode_source(std::span<const PlainBlock>);
class MetadataOwner {
  std::shared_ptr<const BodyOwner> body_;
  MetadataKind kind_;
  unsigned capacity_;
  AlignedBytes bytes_;

public:
  MetadataOwner(const EncodedSource &, MetadataKind);
  const std::shared_ptr<const BodyOwner> &body() const { return body_; }
  MetadataKind kind() const { return kind_; }
  unsigned capacity() const { return capacity_; }
  const AlignedBytes &bytes() const { return bytes_; }
};
Entry read_entry_reference(MetadataKind, std::span<const std::uint8_t>,
                           unsigned capacity, unsigned index);

enum class AdmissionError {
  none,
  association,
  range,
  query_extent,
  metadata_extent,
  metadata_value,
  framing,
  body_extent,
  body_code
};
AdmissionError validate_metadata(MetadataKind,
                                 std::span<const std::uint8_t> metadata,
                                 unsigned capacity, unsigned count,
                                 std::span<const std::uint8_t> body,
                                 unsigned logical_bytes);
enum class Execution { inlined, split };
using RangeKernel = std::uint64_t (*)(const std::uint8_t *, unsigned,
                                      const std::uint8_t *,
                                      const std::uint8_t *, unsigned,
                                      unsigned) noexcept;
class PreparedRange {
  std::shared_ptr<const MetadataOwner> metadata_;
  std::shared_ptr<const BodyOwner> body_;
  std::shared_ptr<const AlignedBytes> query_;
  RangeKernel kernel_ = nullptr;
  unsigned first_ = 0, count_ = 0;
  friend AdmissionError prepare(std::shared_ptr<const MetadataOwner>,
                                std::shared_ptr<const BodyOwner>,
                                std::shared_ptr<const AlignedBytes>, unsigned,
                                unsigned, Execution, PreparedRange &);

public:
  std::uint64_t count() const noexcept {
    return kernel_(metadata_->bytes().data(), metadata_->capacity(),
                   body_->bytes.data(), query_->data(), first_, count_);
  }
};
AdmissionError prepare(std::shared_ptr<const MetadataOwner>,
                       std::shared_ptr<const BodyOwner>,
                       std::shared_ptr<const AlignedBytes>, unsigned first,
                       unsigned count, Execution, PreparedRange &);
#define IH_RANGE_DECL(name)                                                    \
  extern "C" std::uint64_t name(const std::uint8_t *, unsigned,                \
                                const std::uint8_t *, const std::uint8_t *,    \
                                unsigned, unsigned) noexcept
IH_RANGE_DECL(ikea_heterogeneous_direct_inline);
IH_RANGE_DECL(ikea_heterogeneous_direct_split);
IH_RANGE_DECL(ikea_heterogeneous_local_inline);
IH_RANGE_DECL(ikea_heterogeneous_local_split);
IH_RANGE_DECL(ikea_heterogeneous_scan_inline);
IH_RANGE_DECL(ikea_heterogeneous_scan_split);
#undef IH_RANGE_DECL
} // namespace ikea::heterogeneous

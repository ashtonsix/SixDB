#include "source.h"
#include "../ikea-blocks/codec.h"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace ikea::heterogeneous {
void AlignedBytes::Free::operator()(std::uint8_t *p) const noexcept {
  std::free(p);
}
AlignedBytes::AlignedBytes(std::size_t size)
    : data_(static_cast<std::uint8_t *>(std::aligned_alloc(
          64, std::max<std::size_t>(64, (size + 63) & ~std::size_t(63))))),
      size_(size) {
  if (!data_)
    throw std::bad_alloc();
  std::memset(data(), 0,
              std::max<std::size_t>(64, (size + 63) & ~std::size_t(63)));
}
static unsigned load16(const std::uint8_t *p) {
  return p[0] | unsigned(p[1]) << 8;
}
static void store16(std::uint8_t *p, unsigned x) {
  p[0] = x;
  p[1] = x >> 8;
}
static unsigned load32(const std::uint8_t *p) {
  return load16(p) | load16(p + 2) << 16;
}
static void store32(std::uint8_t *p, unsigned x) {
  store16(p, x);
  store16(p + 2, x >> 16);
}
static unsigned local_get(const std::uint8_t *p, unsigned k, unsigned i) {
  unsigned value = 0;
  for (unsigned b = 0; b < k; ++b)
    value |= ((p[i / 8 * k + b] >> (i % 8)) & 1) << b;
  return value;
}
static void local_put(std::uint8_t *p, unsigned k, unsigned i, unsigned value) {
  for (unsigned b = 0; b < k; ++b)
    p[i / 8 * k + b] |= ((value >> b) & 1) << (i % 8);
}
// Independent six-bit wire transcription, used only by construction/admission.
static constexpr unsigned scan6[4][6] = {{0, 1, 2, 3, 4, 5},
                                         {8, 9, 10, 11, 6, 7},
                                         {12, 13, 14, 15, 22, 23},
                                         {16, 17, 18, 19, 20, 21}};
static unsigned scan_get(const std::uint8_t *p, unsigned i) {
  unsigned v = 0;
  for (unsigned b = 0; b < 6; ++b) {
    auto q = scan6[i % 128 / 32][b];
    v |= ((p[i / 128 * 96 + q / 8 * 32 + i % 32] >> (q % 8)) & 1) << b;
  }
  return v;
}
static void scan_put(std::uint8_t *p, unsigned i, unsigned value) {
  for (unsigned b = 0; b < 6; ++b) {
    auto q = scan6[i % 128 / 32][b];
    p[i / 128 * 96 + q / 8 * 32 + i % 32] |= ((value >> b) & 1) << (q % 8);
  }
}
EncodedSource encode_source(std::span<const PlainBlock> plain) {
  if (plain.size() > max_blocks)
    throw std::invalid_argument("too many bitset blocks");
  EncodedSource out;
  std::vector<std::array<std::uint8_t, 64>> encoded(plain.size());
  unsigned offset = 0;
  for (unsigned i = 0; i < plain.size(); ++i) {
    unsigned population = 0;
    for (auto b : plain[i])
      population += std::popcount(b);
    unsigned bits =
        ikea_probe::encode_reference(plain[i].data(), encoded[i].data());
    unsigned length = (bits + 7) / 8;
    if (length > 47)
      throw std::logic_error("BEC oracle length");
    out.entries_.push_back({static_cast<std::uint16_t>(population),
                            static_cast<std::uint16_t>(offset),
                            static_cast<std::uint8_t>(length)});
    offset += length;
  }
  auto body = std::make_shared<BodyOwner>(plain.size(), offset);
  for (unsigned i = 0; i < plain.size(); ++i)
    std::memcpy(body->bytes.data() + out.entries_[i].offset, encoded[i].data(),
                out.entries_[i].length);
  out.body_ = std::move(body);
  return out;
}
MetadataOwner::MetadataOwner(const EncodedSource &source, MetadataKind kind)
    : body_(source.body_), kind_(kind),
      capacity_(metadata_capacity(kind, body_->count)),
      bytes_(metadata_bytes(kind, capacity_)) {
  auto *p = bytes_.data();
  for (unsigned i = 0; i < capacity_; ++i) {
    const Entry e =
        i < body_->count
            ? source.entries_[i]
            : Entry{0, static_cast<std::uint16_t>(body_->logical_bytes), 0};
    const unsigned group = i / 16, lane = i % 16;
    if (kind == MetadataKind::direct32)
      store32(p + 4 * i, e.population | unsigned(e.length) << 9 |
                             unsigned(e.offset) << 15);
    else if (kind == MetadataKind::local16) {
      auto *q = p + group * 32;
      if (lane == 0)
        store16(q, e.offset);
      q[2 + lane] = e.population >> 1;
      local_put(q + 18, 1, lane, e.population & 1);
      local_put(q + 20, 6, lane, e.length);
    } else {
      if (lane == 0)
        store16(p + group * 2, e.offset);
      auto *q = p + scan_pop_offset(capacity_) + group * 18;
      q[lane] = e.population >> 1;
      local_put(q + 16, 1, lane, e.population & 1);
      scan_put(p + scan_length_offset(capacity_), i, e.length);
    }
  }
}
Entry read_entry_reference(MetadataKind kind,
                           std::span<const std::uint8_t> metadata,
                           unsigned capacity, unsigned i) {
  const auto *p = metadata.data();
  if (kind == MetadataKind::direct32) {
    auto w = load32(p + 4 * i);
    return {std::uint16_t(w & 511), std::uint16_t(w >> 15),
            std::uint8_t((w >> 9) & 63)};
  }
  const unsigned group = i / 16, lane = i % 16;
  unsigned offset, population, length;
  if (kind == MetadataKind::local16) {
    auto *q = p + group * 32;
    offset = load16(q);
    for (unsigned j = 0; j < lane; ++j)
      offset += local_get(q + 20, 6, j);
    population = q[2 + lane] * 2 + local_get(q + 18, 1, lane);
    length = local_get(q + 20, 6, lane);
  } else {
    const auto *q = p + scan_pop_offset(capacity) + group * 18;
    offset = load16(p + group * 2);
    for (unsigned j = group * 16; j < i; ++j)
      offset += scan_get(p + scan_length_offset(capacity), j);
    population = q[lane] * 2 + local_get(q + 16, 1, lane);
    length = scan_get(p + scan_length_offset(capacity), i);
  }
  return {std::uint16_t(population), std::uint16_t(offset),
          std::uint8_t(length)};
}
AdmissionError validate_metadata(MetadataKind kind,
                                 std::span<const std::uint8_t> metadata,
                                 unsigned capacity, unsigned count,
                                 std::span<const std::uint8_t> body,
                                 unsigned logical_bytes) {
  if (count > max_blocks || capacity != metadata_capacity(kind, count) ||
      metadata.size() != metadata_bytes(kind, capacity))
    return AdmissionError::metadata_extent;
  if (logical_bytes > body.size() || body.size() - logical_bytes < 64)
    return AdmissionError::body_extent;
  unsigned next = 0;
  for (unsigned i = 0; i < capacity; ++i) {
    if (kind == MetadataKind::direct32 &&
        (load32(metadata.data() + i * 4) >> 29) != 0)
      return AdmissionError::metadata_value;
    const auto e = read_entry_reference(kind, metadata, capacity, i);
    if (e.population > 256 || e.length > 47)
      return AdmissionError::metadata_value;
    if (e.offset != next || e.length > logical_bytes - next)
      return AdmissionError::framing;
    if (i >= count && (e.population != 0 || e.length != 0))
      return AdmissionError::metadata_value;
    if (i < count) {
      if (ikea_probe::validate(body.subspan(e.offset, e.length), e.population)
              .error != ikea_probe::Invalid::none)
        return AdmissionError::body_code;
      next += e.length;
    }
  }
  return next == logical_bytes ? AdmissionError::none : AdmissionError::framing;
}
AdmissionError prepare(std::shared_ptr<const MetadataOwner> metadata,
                       std::shared_ptr<const BodyOwner> body,
                       std::shared_ptr<const AlignedBytes> query,
                       unsigned first, unsigned count, Execution execution,
                       PreparedRange &out) {
  if (!metadata || !body || metadata->body().get() != body.get())
    return AdmissionError::association;
  if (first > body->count || count > body->count - first)
    return AdmissionError::range;
  if (!query || query->size() < std::size_t(body->count) * 32)
    return AdmissionError::query_extent;
  const auto valid = validate_metadata(
      metadata->kind(), {metadata->bytes().data(), metadata->bytes().size()},
      metadata->capacity(), body->count,
      {body->bytes.data(), body->bytes.size()}, body->logical_bytes);
  if (valid != AdmissionError::none)
    return valid;
  RangeKernel kernels[][2] = {
      {ikea_heterogeneous_direct_inline, ikea_heterogeneous_direct_split},
      {ikea_heterogeneous_local_inline, ikea_heterogeneous_local_split},
      {ikea_heterogeneous_scan_inline, ikea_heterogeneous_scan_split}};
  out.metadata_ = std::move(metadata);
  out.body_ = std::move(body);
  out.query_ = std::move(query);
  out.first_ = first;
  out.count_ = count;
  out.kernel_ = kernels[unsigned(out.metadata_->kind())][unsigned(execution)];
  return AdmissionError::none;
}
} // namespace ikea::heterogeneous

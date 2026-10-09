#pragma once
#include "../../../benchmarks/orbital/packed-page.h"
#include <expected>
#include <vector>

namespace critical_paths {
namespace packed = orbital_bench::packed;
using orbital_bench::Key;
using orbital_bench::Record;
using orbital_bench::Outcome;
enum class Status : unsigned { found, absent, closed };
enum class Protection { owner_borrow, retained_scope };
enum class Admission { description, order, backing };
class Scope;
Status visible(const Scope&, Key, Outcome&) noexcept;
Status fused(const Scope&, Key, Outcome&) noexcept;
Status fused_multiply(const Scope&, Key, Outcome&) noexcept;
Status fused_simd(const Scope&, Key, Outcome&) noexcept;
Status fused_fp(const Scope&, Key, Outcome&) noexcept;

// Fixture seams around the actual allocation-stage page, not Object/MVCC types.
class Directory {
  packed::Source source_;
public:
  explicit Directory(packed::Source source) : source_(source) {}
  const packed::Page* locate(Key) const noexcept;
  friend Status visible(const Scope&, Key, Outcome&) noexcept;
  friend Status fused(const Scope&, Key, Outcome&) noexcept;
  friend Status fused_multiply(const Scope&, Key, Outcome&) noexcept;
  friend Status fused_simd(const Scope&, Key, Outcome&) noexcept;
  friend Status fused_fp(const Scope&, Key, Outcome&) noexcept;
};
class View {
  const packed::Page* page_;
public:
  explicit View(const packed::Page& page) : page_(&page) {}
  std::size_t size() const noexcept;
  std::size_t physical(std::size_t) const noexcept;
  Key key(std::size_t) const noexcept;
  Record payload(std::size_t) const noexcept;
};
class Scope {
  Directory directory_;
  std::vector<packed::alloc::Hold> holds_;
  bool open_{true};
  explicit Scope(packed::Source source) : directory_(source) {}
public:
  Scope(Scope&& source) noexcept : directory_(source.directory_),
      holds_(std::move(source.holds_)), open_(std::exchange(source.open_, false)) {}
  Scope& operator=(Scope&& source) noexcept {
    if (this != &source) {
      open_ = false;
      directory_ = source.directory_; holds_ = std::move(source.holds_);
      open_ = std::exchange(source.open_, false);
    }
    return *this;
  }
  Scope(const Scope&) = delete;
  // Cold admission of exact fixed-format interpretation and complete footprint.
  // Metadata/Schema/Page objects and immutable contents outlive the Scope.
  static std::expected<Scope, Admission> qualify(packed::Source, Protection);
  bool open() const noexcept;
  const Directory& directory() const noexcept;
  void revoke() noexcept { open_ = false; } // Retention survives revoked interest.
  friend Status visible(const Scope&, Key, Outcome&) noexcept;
  friend Status fused(const Scope&, Key, Outcome&) noexcept;
  friend Status fused_multiply(const Scope&, Key, Outcome&) noexcept;
  friend Status fused_simd(const Scope&, Key, Outcome&) noexcept;
  friend Status fused_fp(const Scope&, Key, Outcome&) noexcept;
};
Status ordinary(const Scope&, Key, Outcome&) noexcept;
// Complete found-row tail of those callers, used for straight-line MCA regions.
void payload_ordinary(const View&, std::size_t, Key, Outcome&) noexcept;
void payload_visible(const packed::Page&, std::size_t, Key, Outcome&) noexcept;
void payload_fused(const packed::Page&, std::size_t, Key, Outcome&) noexcept;
void payload_multiply(const packed::Page&, std::size_t, Key, Outcome&) noexcept;
void payload_simd(const packed::Page&, std::size_t, Key, Outcome&) noexcept;
void payload_fp(const packed::Page&, std::size_t, Key, Outcome&) noexcept;
using Query = Status (*)(const Scope&, Key, Outcome&) noexcept;
} // namespace critical_paths

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <utility>
#include <vector>

// Embedded read-scope retention policies, not Journal/recovery retention.
// A Plan lives through its cursors. Histories are visited strictly descending;
// the head is retained before calling keep for each older version.
namespace compact_retention {

struct FloorRetention {
  static constexpr bool uses_distinct_cuts = false;
  class Cursor {
    std::uint64_t floor_;
    bool crossed_;
  public:
    Cursor(std::uint64_t floor, std::uint64_t head) noexcept
        : floor_(floor), crossed_(head <= floor) {}

    bool keep(std::uint64_t cut) noexcept {
      if (crossed_) return false;
      crossed_ = cut <= floor_;
      return true;
    }
  };

  class Plan {
    std::uint64_t floor_;
  public:
    template<class Visitor>
    Plan(std::pmr::memory_resource&, std::size_t, std::uint64_t floor,
        Visitor&&) noexcept : floor_(floor) {}

    Cursor cursor(std::uint64_t head) const noexcept { return {floor_, head}; }
    std::size_t requested_bytes() const noexcept { return 0; }
    std::size_t cut_count() const noexcept { return 0; }
  };
};

struct DistinctRetention {
  static constexpr bool uses_distinct_cuts = true;
  class Cursor {
    const std::uint64_t* cuts_;
    std::size_t remaining_;
  public:
    Cursor(const std::uint64_t* cuts, std::size_t remaining) noexcept
        : cuts_(cuts), remaining_(remaining) {}

    bool keep(std::uint64_t cut) noexcept {
      if (!remaining_ || cuts_[remaining_ - 1] < cut) return false;
      do { --remaining_; } while (remaining_ && cuts_[remaining_ - 1] >= cut);
      return true;
    }
  };

  class Plan {
    std::pmr::vector<std::uint64_t> cuts_;
  public:
    // Visitor calls its supplied consumer with active scope cuts in ascending
    // order. Refusal throws before history mutation; the owner leaves debt live.
    template<class Visitor>
    Plan(std::pmr::memory_resource& resource, std::size_t scope_count,
        std::uint64_t, Visitor&& visit_sorted_cuts) : cuts_(&resource) {
      if (scope_count > cuts_.max_size()) throw std::bad_alloc();
      cuts_.reserve(scope_count);
      std::forward<Visitor>(visit_sorted_cuts)([this](std::uint64_t cut) {
        assert(cuts_.empty() || cut >= cuts_.back());
        if (cuts_.empty() || cut != cuts_.back()) cuts_.push_back(cut);
      });
    }

    Plan(const Plan&) = delete;
    Plan& operator=(const Plan&) = delete;
    Plan(Plan&&) = default;
    Plan& operator=(Plan&&) = delete;

    Cursor cursor(std::uint64_t head) const noexcept {
      if (cuts_.empty()) return {nullptr, 0};
      const auto first_covered = std::lower_bound(cuts_.begin(), cuts_.end(), head);
      return {cuts_.data(), static_cast<std::size_t>(first_covered - cuts_.begin())};
    }

    std::size_t requested_bytes() const noexcept {
      return cuts_.capacity() * sizeof(std::uint64_t);
    }
    std::size_t cut_count() const noexcept { return cuts_.size(); }
  };
};

static_assert(sizeof(FloorRetention::Cursor) == 16);
static_assert(sizeof(DistinctRetention::Cursor) == 16);

} // namespace compact_retention

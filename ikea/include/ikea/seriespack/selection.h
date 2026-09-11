#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace ikea::seriespack {
/// Borrowed row evidence at the operation boundary. Each word describes sixteen
/// original coordinates. It is stable through admission/execution, and does not
/// grant permission to read an otherwise unreadable inactive input lane.
class row_selection {
    enum class kind { all, none, regions };
    kind kind_;
    std::size_t first_;
    std::span<const std::uint16_t> words_;
    row_selection(kind type, std::size_t first, std::span<const std::uint16_t> words)
        : kind_(type), first_(first), words_(words) {}

  public:
    static row_selection all() {
        return {kind::all, 0, {}};
    }
    static row_selection none() {
        return {kind::none, 0, {}};
    }
    /// Checked operations reject a misaligned origin or insufficient coverage.
    static row_selection regions(std::size_t first, std::span<const std::uint16_t> words) {
        return {kind::regions, first, words};
    }
    bool is_all() const {
        return kind_ == kind::all;
    }
    bool is_none() const {
        return kind_ == kind::none;
    }
    bool covers(std::size_t first, std::size_t count) const {
        if (kind_ != kind::regions)
            return true;
        if (first_ % 16)
            return false;
        if (!count)
            return true;
        if (count - 1 > ~std::size_t{0} - first || first / 16 < first_ / 16)
            return false;
        return (first + count - 1) / 16 - first_ / 16 < words_.size();
    }
    std::uint16_t operator()(std::size_t origin) const {
        if (kind_ == kind::all)
            return 0xffff;
        if (kind_ == kind::none)
            return 0;
        return words_[origin / 16 - first_ / 16];
    }
};
} // namespace ikea::seriespack

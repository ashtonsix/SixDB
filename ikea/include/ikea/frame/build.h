#pragma once
#include <ikea/frame/author/stage.h>
#include <ikea/frame/description.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ikea::frame {

/// Construction policy: how fitting chooses among byte laws the description
/// admits. It is never an interpretation of stored bytes, so changing it
/// leaves existing images' meaning unchanged.
struct fitting_policy {
    /// Bytes charged per exception on top of its storage when bound prices a
    /// window (Engine's read-time weight for patching).
    unsigned penalty = 0;
    /// The most exceptions one block may hold (64 fit one register of positions).
    unsigned exception_cap = 64;
    /// Spare exception slots reserved per group; zero reserves none.
    unsigned slack = 0;
};

/// One stream's emitted sections: its rows and each group's image, for one
/// binding unit. path is the stream's role path ("" for the root stream).
struct stream_images {
    std::string path;
    std::uint16_t root_node{};
    std::size_t rows{};
    std::vector<std::vector<std::byte>> groups;
};
/// A built unit: every stream's sections, and fit facts for Engine.
struct built_unit {
    std::vector<stream_images> streams;
    author::census census;
};

/// Fit one binding unit (at most unit_rows values) under a description and
/// emit its group sections. Refuses descriptions it cannot execute
/// (placeholders, H8/H16 heads) before reading a value.
[[nodiscard]] std::expected<built_unit, error> build(const description &, std::span<const std::uint64_t> values,
                                                     const fitting_policy & = {});

namespace detail {
/// The stage tree a description executes, unfitted and unattached. Fit and
/// attach establish its units; the checked reader and build share it.
[[nodiscard]] std::expected<std::unique_ptr<author::stage>, error> make_stages(const description &,
                                                                               const fitting_policy & = {});
} // namespace detail
} // namespace ikea::frame

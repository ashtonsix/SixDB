#pragma once
#include <ikea/frame/image.h>
#include <ikea/frame/author/stage.h>

namespace ikea::frame::detail {
struct image_state {
    std::unique_ptr<author::stage> root_;
    description description_;
    std::vector<stream_manifest> streams_;
    std::vector<section_key> available_;
    std::size_t rows_{};
    std::uint16_t ready_groups_{};
};
} // namespace ikea::frame::detail

#pragma once
#include "control.h"
#include <memory>

namespace calico_control {
struct sum_program {
    frame::LayoutId layout;
    uint32_t frame_rows;
};
enum class sum_refusal {
    none, field_width, value_width, program, grain, layout_width, palette, size, upstream
};
const char* refusal_name(sum_refusal refusal);

struct sum_preparation {
    sum_program program;
    uint64_t rows = 0, complete_frames = 0;
    uint32_t tail_rows = 0;
    uint16_t distinct_boxes = 0, symbols = 0;
    uint8_t physical_width = 0, max_residual_bits = 0;
    std::size_t type_wire_bytes = 0;
    // Temporary fitting storage only; upstream staging/peak is not measured.
    std::size_t fit_box_capacity_bytes = 0, wire_staging_bytes = 0;
    // Private RawCodec vector element bytes from the frozen constructor.
    // This is a logical charge, not an observed capacity/allocator footprint.
    std::size_t private_codec_element_bytes = 0;
};

class prepared_sum {
    std::unique_ptr<unsigned_image> image_;
    sum_preparation preparation_;
public:
    prepared_sum(std::unique_ptr<unsigned_image> image, sum_preparation preparation);
    prepared_sum(const prepared_sum&) = delete;
    prepared_sum& operator=(const prepared_sum&) = delete;
    bool sum(u128& out) const;
    std::vector<range> sum_ranges() const;
    std::vector<range> retained_ranges() const;
    const unsigned_image& image() const { return *image_; }
    const sum_preparation& preparation() const { return preparation_; }
};
struct sum_candidate {
    std::unique_ptr<prepared_sum> owner;
    sum_refusal refusal = sum_refusal::none;
    std::string detail;
    explicit operator bool() const { return bool(owner); }
};

// Small preparation control, not a production fitter. Programs are explicit:
// lane256/256, byteplane/256, FastLanes/1024. All source values remain uint64;
// physical_width is their declared unsigned domain, never a guessed residual
// width. Complete-frame exact boxes are deduplicated without merging. Palette
// sizes are the source's 1/16/256; >256 distinct boxes is a refusal. Partial
// rows use the actual Engine literal tail. No input or aggregate is retained.
sum_candidate prepare_sum(std::span<const uint64_t> values, uint8_t physical_width,
                          sum_program program);
} // namespace calico_control

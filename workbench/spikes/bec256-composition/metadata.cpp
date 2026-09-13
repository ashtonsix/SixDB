#include "metadata.h"
#include <algorithm>
#include <stdexcept>

namespace bec_study {
const char *name(layout value) {
    switch (value) {
    case layout::direct:
        return "direct8";
    case layout::direct32:
        return "direct4";
    case layout::tuple_absolute:
        return "tuple_absolute6";
    case layout::tuple_checkpoint:
        return "tuple_checkpoint2";
    case layout::series_local:
        return "series_local";
    case layout::series_scan:
        return "series_scan";
    case layout::tuple_folded:
        return "tuple_folded2";
    case layout::series_folded_local:
        return "series_folded_local";
    case layout::series_folded_scan:
        return "series_folded_scan";
    }
    std::abort();
}
const char *name(resolution value) {
    switch (value) {
    case resolution::point:
        return "point";
    case resolution::buffered16:
        return "buffered16";
    case resolution::native16:
        return "native16";
    }
    std::abort();
}
directory::directory(layout kind, std::span<const entry> entries, unsigned checkpoint)
    : kind_(kind), count_(entries.size()),
      capacity_(absolute(kind) ? count_ : (count_ + 15) & ~15u), checkpoint_(checkpoint),
      direct_(kind == layout::direct ? capacity_ : 0),
      direct32_(kind == layout::direct32 ? capacity_ : 0),
      tuples_((kind == layout::tuple_absolute ? 6
               : bec_study::tuple(kind)       ? 2
                                              : 0) *
              capacity_),
      populations_(series(kind) ? capacity_ * (folded(kind) ? 8 : 9) / 8 : 0),
      lengths_(series(kind) ? (scan(kind) ? ((capacity_ + 127) / 128) * scan_lengths::tile_bytes
                                          : capacity_ * 6 / 8)
                            : 0),
      checkpoints_(!absolute(kind) ? (count_ + checkpoint - 1) / checkpoint : 0) {
    assert(checkpoint == 16 || checkpoint == 64);
    if (count_ == 0)
        throw std::invalid_argument("empty test directory");
    std::vector<ikea::owner_write> storage(capacity_ * 4);
    ikea::source_write_journal effects{storage};
    if (kind == layout::direct || kind == layout::direct32) {
        for (unsigned i = 0; i < count_; ++i) {
            const auto e = entries[i];
            const auto word =
                e.population | (std::uint64_t(e.bytes) << 9) | (std::uint64_t(e.offset) << 15);
            if (kind == layout::direct32) {
                if (e.offset >= (1u << 17))
                    throw std::invalid_argument("direct4 offset does not fit");
                direct32_[i] = word;
            } else
                direct_[i] = word;
        }
    } else if (bec_study::tuple(kind)) {
        std::vector<tp::code> codes{{0, 0, 8}};
        if (folded(kind))
            codes.push_back({1, 0, 6});
        else {
            codes.push_back({1, 0, 1});
            codes.push_back({1, 1, 6});
        }
        if (kind == layout::tuple_absolute)
            for (unsigned b = 0; b < 4; ++b)
                codes.push_back({tp::byte(b + 2), 0, 8});
        tuple_layout_ = *tp::layout::make(kind == layout::tuple_absolute ? 6 : 2, codes);
        tuple_view_ =
            *tp::view::bind(*tuple_layout_, tuples_.span(), capacity_, tuple_layout_->bytes());
        const std::array<tp::byte, 7> map{0, 1, 2, 3, 4, 5, 6};
        auto mapped = std::span(map).first(codes.size());
        tuple_plan_ = *tp::reader<8>::make(*tuple_layout_, mapped);
        tuple_writer_ = *tp::writer<8>::make(*tuple_layout_, mapped);
        tuple_read_ = *tp::bind_reader(*tuple_plan_, *tuple_view_);
        if (kind != layout::tuple_absolute) {
            frame_plan_ = *tp::reader<64, 16>::make(*tuple_layout_, mapped);
            frame_read_ = *tp::bind_reader(*frame_plan_, *tuple_view_);
        }
        tuple_write_ = *tp::bind_writer(*tuple_writer_, *tuple_view_);
        const auto &write = tuple_write_;
        assert(write);
        std::vector<std::uint64_t> input(capacity_);
        for (unsigned i = 0; i < count_; ++i) {
            const auto e = entries[i];
            input[i] = (folded(kind) ? population_code(e.population) : e.population) |
                       (std::uint64_t(e.bytes) << (folded(kind) ? 8 : 16));
            if (kind == layout::tuple_absolute)
                input[i] |= std::uint64_t(e.offset) << 24;
        }
        assert(write->replace(0, input, effects));
    } else {
        std::vector<std::uint16_t> input(capacity_);
        std::vector<std::uint8_t> lengths(capacity_);
        for (unsigned i = 0; i < count_; ++i) {
            input[i] = entries[i].population;
            lengths[i] = entries[i].bytes;
        }
        if (folded(kind)) {
            folded_view_ = *sp::view<folded_populations, std::uint8_t>::attach(
                capacity_, {{{populations_.span(), folded_populations::tile_bytes}, {}, {}}});
            folded_write_.emplace(*sp::bind_mutation(*folded_view_));
            const auto &write = folded_write_;
            for (auto &value : input)
                value = population_code(value);
            assert(write->initialize<std::uint16_t>(input, effects));
            folded_reader_ = sp::bind_decoder<std::uint8_t>(*sp::dense(*folded_view_));
        } else {
            population_view_ = *sp::view<populations, std::uint8_t>::attach(
                capacity_, {{{populations_.span(), populations::tile_bytes}, {}, {}}});
            population_write_.emplace(*sp::bind_mutation(*population_view_));
            const auto &write = population_write_;
            assert(write->initialize<std::uint16_t>(input, effects));
            population_reader_ = sp::bind_decoder<std::uint16_t>(*sp::dense(*population_view_));
        }
        if (!scan(kind)) {
            local_view_ = *sp::view<local_lengths, std::uint8_t>::attach(
                capacity_, {{{lengths_.span(), local_lengths::tile_bytes}, {}, {}}});
            local_write_.emplace(*sp::bind_mutation(*local_view_));
            const auto &length_write = local_write_;
            assert(length_write->initialize<std::uint8_t>(lengths, effects));
            length_reader_ = sp::bind_decoder<std::uint8_t>(*sp::dense(*local_view_));
        } else {
            scan_view_ = *sp::view<scan_lengths, std::uint8_t>::attach(
                capacity_, {{{lengths_.span(), scan_lengths::tile_bytes}, {}, {}}});
            scan_write_.emplace(*sp::bind_mutation(*scan_view_));
            const auto &length_write = scan_write_;
            assert(length_write->initialize<std::uint8_t>(lengths, effects));
            length_reader_ = sp::bind_decoder<std::uint8_t>(*sp::dense(*scan_view_));
        }
    }
    for (unsigned i = 0; i < checkpoints_.size(); ++i) {
        const auto row = i * checkpoint_;
        checkpoints_[i] =
            row < count_ ? entries[row].offset : entries.back().offset + entries.back().bytes;
    }
}
std::size_t directory::storage_bytes() const {
    return direct_.size() * 8 + direct32_.size() * 4 + tuples_.size() + populations_.size() +
           lengths_.size() + checkpoints_.size() * 4;
}
void directory::buffered(unsigned first, unsigned *pops, unsigned *lens) const {
    if (bec_study::tuple(kind_)) {
        auto packet = frame_read_->get_unchecked(first);
        for (unsigned i = 0; i < 16; ++i) {
            pops[i] = packet[i * 4] | (folded(kind_) ? 0 : (packet[i * 4 + 1] << 8));
            lens[i] = packet[i * 4 + (folded(kind_) ? 1 : 2)];
            if (folded(kind_))
                pops[i] = expand_population(pops[i], lens[i]);
        }
    } else {
        std::uint16_t p[16];
        std::uint8_t l[16];
        if (folded(kind_)) {
            std::uint8_t codes[16];
            folded_reader_->read16_unchecked(first, codes);
            for (unsigned i = 0; i < 16; ++i)
                p[i] = codes[i];
        } else
            population_reader_->read16_unchecked(first, p);
        length_reader_->read16_unchecked(first, l);
        for (unsigned i = 0; i < 16; ++i) {
            pops[i] = folded(kind_) ? expand_population(p[i], l[i]) : p[i];
            lens[i] = l[i];
        }
    }
}
std::size_t directory::update(unsigned row, entry replacement) {
    assert(row < count_ && replacement.population <= 256 && replacement.bytes <= 47);
    std::array<ikea::owner_write, 16> storage;
    ikea::source_write_journal effects{storage};
    if (kind_ == layout::direct || kind_ == layout::direct32) {
        const auto word = replacement.population | (std::uint64_t(replacement.bytes) << 9) |
                          (std::uint64_t(replacement.offset) << 15);
        if (kind_ == layout::direct32) {
            assert(replacement.offset < (1u << 17));
            effects.before(*this, {0, row * 4u, 4});
            direct32_[row] = word;
        } else {
            effects.before(*this, {0, row * 8u, 8});
            direct_[row] = word;
        }
    } else if (bec_study::tuple(kind_)) {
        const auto &write = tuple_write_;
        const auto value =
            (folded(kind_) ? population_code(replacement.population) : replacement.population) |
            (std::uint64_t(replacement.bytes) << (folded(kind_) ? 8 : 16)) |
            (kind_ == layout::tuple_absolute ? std::uint64_t(replacement.offset) << 24 : 0);
        assert(write->set(row, value, effects));
    } else {
        sp::no_summary summary;
        if (folded(kind_)) {
            const auto &p = folded_write_;
            assert(p->set(row, population_code(replacement.population), summary, effects));
        } else {
            const auto &p = population_write_;
            assert(p->set(row, replacement.population, summary, effects));
        }
        if (!scan(kind_)) {
            const auto &l = local_write_;
            assert(l->set(row, replacement.bytes, summary, effects));
        } else {
            const auto &l = scan_write_;
            assert(l->set(row, replacement.bytes, summary, effects));
        }
    }
    std::size_t bytes = 0;
    for (const auto &e : effects.entries())
        bytes += e.bytes.size;
    return bytes;
}
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
frame make_frame(const unsigned *pops, const unsigned *lens, unsigned base) {
    alignas(64) std::uint16_t values[32];
    unsigned offset = 0;
    for (unsigned i = 0; i < 16; ++i) {
        values[i] = pops[i];
        values[16 + i] = offset;
        offset += lens[i];
    }
#if defined(IKEA_BEC256_AVX512)
    return {_mm256_load_si256(reinterpret_cast<const __m256i *>(values)),
            _mm256_load_si256(reinterpret_cast<const __m256i *>(values + 16)), base, base + offset};
#else
    return {vld1q_u8_x4(reinterpret_cast<const std::uint8_t *>(values)), base, base + offset};
#endif
}
#endif
} // namespace bec_study

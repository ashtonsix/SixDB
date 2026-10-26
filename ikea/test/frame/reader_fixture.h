// Test-only adapters preserve the old conformance corpus while exercising the
// ordinary selection-based reader. No codecs, validation or query laws live here.
#pragma once
#include <ikea/frame/build.h>
#include <ikea/frame/reader.h>
#include <ikea/frame/detail/group_image.h>
#include <ikea/frame/detail/recipe.h>
#include <memory>

namespace frame_fixture {
using namespace ikea::frame;
using ikea::bitset256;
using binding_error = error;
using detail::binding_recipe;
using detail::validate_group;
using detail::image_preset;
inline auto make_tree(const description& d, fitting_policy policy = {}) { return detail::make_stages(d, policy); }
class stream_binding {
    reader reader_;
    explicit stream_binding(reader value) : reader_(std::move(value)) {}
    static auto coarse(const ikea::bitset256& blocks) { return ikea::intervals::selection::bits(0, blocks); }
  public:
    static std::expected<std::unique_ptr<stream_binding>, error> attach(const description& d,
        std::size_t rows, std::span<const std::size_t> extents, std::span<const stream_manifest> streams,
        std::span<const borrowed_part> parts, std::span<const section_key> available) {
        auto r = bind_reader(d, {rows,extents,streams,parts,available});
        if (!r) return std::unexpected(r.error());
        return std::unique_ptr<stream_binding>(new stream_binding(std::move(*r)));
    }
    auto size() const { return reader_.size(); }
    auto get(unsigned row) const { return reader_.get(row); }
    auto read(unsigned first, std::span<std::uint64_t> out) const { return reader_.read(first,out); }
    auto required(std::span<const row_range> ranges) const { return reader_.required(ranges); }
    auto sum(const ikea::bitset256& blocks) const { return reader_.sum(coarse(blocks)); }
    auto count(ikea::comparison op, const ikea::bitset256& blocks, std::uint64_t threshold) const {
        return reader_.count(op,threshold,coarse(blocks));
    }
    auto filter(ikea::comparison op, const ikea::bitset256& blocks, std::uint64_t threshold, block_rows_set& out) const {
        return reader_.filter(op,threshold,out,coarse(blocks));
    }
    auto sum_selected(const ikea::bitset256& blocks, const block_rows_set& rows) const {
        return reader_.sum_selected(ikea::seriespack::selection::bits(0,std::span<const ikea::bitset256>(rows)),coarse(blocks));
    }
    auto select(const ikea::bitset256& blocks, const block_rows_set& rows, std::span<std::uint64_t> output) const {
        return reader_.select(ikea::seriespack::selection::bits(0,std::span<const ikea::bitset256>(rows)),output,coarse(blocks));
    }
};
class column {
    std::unique_ptr<author::stage> root_;
  public:
    column(std::unique_ptr<author::stage> root, std::span<const std::uint64_t> values) : root_(std::move(root)) {
        const auto r = root_->fit(0,{values});
        assert(r);
    }
    auto streams() const {
        std::vector<std::pair<std::string,const author::stage*>> out{{"",root_.get()}};
        root_->streams("",out);
        return out;
    }
    auto image(const author::stage& stage, unsigned unit, unsigned group) const {
        std::vector<std::byte> out;
        stage.emit(unit,group,out);
        return out;
    }
    author::stage& root() { return *root_; }
    void read(std::uint64_t* out) const { root_->read(0,0,root_->rows(0),out); }
};
}

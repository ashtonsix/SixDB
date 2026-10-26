#include <ikea/frame.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <vector>

namespace fr = ikea::frame;
using u64 = std::uint64_t;
template<class T> void check(const T& value) { if (!value) std::abort(); }

// This example places each emitted group in a separate allocation. An owner
// may instead co-pack sections and record their distinct offsets and grants.
struct placement {
    std::vector<std::size_t> extents;
    std::vector<fr::stream_manifest> streams;
    std::vector<fr::borrowed_part> parts;
    std::vector<fr::section_key> available;
    explicit placement(const fr::built_unit& image) {
        for (const auto& stream : image.streams) {
            fr::stream_manifest manifest{stream.root_node,stream.rows,{}};
            for (std::size_t g=0;g<stream.groups.size();++g) {
                const auto& bytes=stream.groups[g];
                const auto id=static_cast<std::uint32_t>(parts.size());
                manifest.groups.push_back({id,0,bytes.size()});
                extents.push_back(bytes.size());
                parts.push_back({id,bytes});
                available.push_back({stream.root_node,g});
            }
            streams.push_back(std::move(manifest));
        }
    }
    fr::image_view view(std::size_t rows) const { return {rows,extents,streams,parts,available}; }
};
int main() {
    fr::description recipe;
    recipe.nodes={{fr::node_kind::pack,0,0,{}},
                  {fr::node_kind::bound,0,0,{{0,fr::coordinate::identity}}}};
    recipe.root=1;
    std::vector<u64> values(1025);
    for (unsigned r=0;r<values.size();++r) values[r]=1000+r/16;
    values[19]=~u64{0};
    auto built=fr::build(recipe,values);
    check(built);
    const placement placed(*built);
    const auto encoded=fr::encode(recipe);
    check(encoded);
    const auto recovered=fr::decode(*encoded);
    check(recovered);
    const auto image=fr::validate_image(*recovered,placed.view(values.size()));
    check(image);
    const auto reader=fr::bind_reader(*image);
    fr::u128 expected=0;
    for (auto x:values) expected+=x;
    const auto total=reader.sum();
    check(total && *total==expected);
    // Logical spans stay in unit rows; Frame lowers their edges and full interiors.
    const fr::row_range span{17, 509};
    fr::u128 expected_span = 0;
    std::uint64_t expected_count = 0;
    for (std::size_t r = span.first; r < span.first + span.count; ++r) {
        expected_span += values[r];
        expected_count += values[r] >= 1020;
    }
    const auto span_total = reader.sum(span);
    const auto span_count = reader.count(ikea::comparison::greater_equal, 1020, span);
    check(span_total && *span_total == expected_span);
    check(span_count && *span_count == expected_count);
    std::vector<u64> output(values.size());
    check(reader.read(0,output) && output==values);

    // The coarse mask names blocks; fine masks name original unit rows.
    ikea::bitset256 coarse=ikea::bitset256::first_n(2);
    std::array<ikea::bitset256,fr::unit_blocks> matches;
    const auto selected_blocks=ikea::intervals::selection::bits(0,coarse);
    check(reader.filter(ikea::comparison::greater_equal,1020,matches,selected_blocks));
    // Reuse filter output directly, without flattening bitset subobjects.
    const auto summary=reader.summarise_selected(
        ikea::seriespack::selection::bits(0,std::span<const ikea::bitset256>(matches)),selected_blocks);
    check(summary);
    fr::summary want;
    for (unsigned r=0;r<512;++r) if(values[r]>=1020) {
        want.sum+=values[r];++want.count;
        want.min=std::min(want.min,values[r]);want.max=std::max(want.max,values[r]);
    }
    check(summary->sum==want.sum && summary->count==want.count && summary->min==want.min && summary->max==want.max);
    std::printf("Frame: %zu rows, %zu streams, exact SUM and fused selected summary pass\n",reader.size(),built->streams.size());
}

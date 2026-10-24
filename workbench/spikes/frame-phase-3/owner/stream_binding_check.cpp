#include "stream_binding.h"
#include "../proto/placeholders.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace fo = frame3_owner;
using u64 = std::uint64_t;
using u128 = unsigned __int128;
namespace {
unsigned checks{};
void check(bool ok, const char *what) {
    ++checks;
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    std::abort();
}
template <class T, class E> T take(std::expected<T, E> result) {
    check(bool(result), "unexpected refusal");
    return std::move(*result);
}
template <class T> void refused(const std::expected<T, fo::binding_error> &result,
    fo::binding_error error, const char *what) {
    check(!result && result.error() == error, what);
}
using Bytes = std::vector<std::byte>;
struct fixture {
    fo::description program;
    std::vector<u64> values;
    std::vector<Bytes> parts;
    std::vector<std::size_t> extents;
    std::vector<fo::stream_manifest> streams;
    std::vector<fo::section_key> keys;
};
void images(fixture &f, frame3::column &column, std::uint16_t checkpoints) {
    f.parts.clear(); f.extents.clear(); f.streams.clear(); f.keys.clear();
    const auto streams = column.streams();
    check(streams.size() == (fo::is_intervals(f.program.nodes[f.program.root].kind) ? 2 : 1),
        "shared stage enumerated unexpected streams");
    for (const auto &[role, stage] : streams) {
        check(role == "" || role == "checkpoints", "unexpected stream role");
        const auto node = role.empty() ? f.program.root : checkpoints;
        fo::stream_manifest stream{node, stage->rows(0), {}};
        for (unsigned g = 0; g < stage->groups_in(0); ++g) {
            auto image = column.image(*stage, 0, g);
            stream.groups.push_back({static_cast<std::uint32_t>(f.parts.size()), 0, image.size()});
            f.extents.push_back(image.size());
            f.parts.push_back(std::move(image));
            f.keys.push_back({node, g});
        }
        f.streams.push_back(std::move(stream));
    }
}
std::uint16_t basic(fo::description &d, bool bounded) {
    const auto leaf = static_cast<std::uint16_t>(d.nodes.size());
    d.nodes.push_back({fo::node_kind::pack, 0, 0, {}});
    if (!bounded) return leaf;
    const auto root = static_cast<std::uint16_t>(d.nodes.size());
    d.nodes.push_back({fo::node_kind::bound, 0, 0, {{leaf, fo::coordinate::identity}}});
    return root;
}
fixture built(unsigned spacing, bool intervals = true, bool bounded_deltas = true, bool bounded_checkpoints = true,
    unsigned boundary = 0, std::optional<u64> constant = {}, unsigned slack = 8,
    fo::node_kind form = fo::node_kind::intervals_sizes) {
    fixture f;
    const auto delta = basic(f.program, bounded_deltas);
    f.program.root = delta;
    std::uint16_t cp = 0;
    if (intervals) {
        cp = basic(f.program, bounded_checkpoints);
        f.program.root = static_cast<std::uint16_t>(f.program.nodes.size());
        f.program.nodes.push_back({form, static_cast<std::uint16_t>(spacing), 0,
                                  {{delta, fo::coordinate::identity}, {cp, fo::coordinate::checkpoints}}});
    }
    f.values.resize(4367);
    for (std::size_t r = 0; r < f.values.size(); ++r) f.values[r] = 1000 + (r / 256) * 1024 + r % 256;
    std::swap(f.values[259], f.values[260]); // A non-monotone block.
    f.values[512] = 0; f.values[513] = std::numeric_limits<u64>::max(); f.values[514] = 0;
    if (boundary == 1) f.values[0] = u64{1} << 63, f.values[1] = 0; // Exactly -2^63 fits.
    if (boundary == 2) f.values[0] = 0, f.values[1] = u64{1} << 63; // +2^63 does not.
    if (boundary == 3) f.values[0] = 0, f.values[1] = u64{1} << 62, f.values[2] = u64{1} << 63;
    if (boundary == 4)
        for (unsigned r = 0; r < 256; ++r) f.values[r] = r < spacing ? 0 : std::numeric_limits<u64>::max();
    if (constant) std::fill(f.values.begin(), f.values.end(), *constant);
    frame3::column column(take(fo::make_tree(f.program, {0, 64, slack})), f.values);
    images(f, column, cp);
    return f;
}
std::span<std::byte> section(fixture &f, fo::section_key key) {
    for (const auto &s : f.streams) if (s.root_node == key.root_node) {
        const auto &location = s.groups[key.group];
        return std::span(f.parts[location.owner_part]).subspan(location.offset, location.extent);
    }
    std::abort();
}
std::vector<fo::borrowed_part> parts(const fixture &f) {
    std::vector<fo::borrowed_part> result;
    for (std::size_t i = 0; i < f.parts.size(); ++i) result.push_back({static_cast<std::uint32_t>(i), f.parts[i]});
    return result;
}
auto bind(const fixture &f, std::span<const fo::section_key> available) {
    const auto borrowed = parts(f); // Binding must not borrow these wrappers.
    return fo::stream_binding::attach(f.program, f.values.size(), f.extents, f.streams, borrowed, available);
}
auto bind(const fixture &f) { return bind(f, f.keys); }
void copack(fixture &f) {
    Bytes together(17, std::byte{0xA5});
    for (auto &s : f.streams)
        for (auto &g : s.groups) {
            const auto source = std::span(f.parts[g.owner_part]).subspan(g.offset, g.extent);
            const auto offset = together.size();
            together.insert(together.end(), source.begin(), source.end());
            together.insert(together.end(), 13, std::byte{0xA5});
            g.owner_part = 0; g.offset = offset;
        }
    f.extents = {together.size()};
    f.parts.clear();
    f.parts.push_back(std::move(together));
}
bool test(ikea::comparison op, u64 value, u64 threshold) {
    switch (op) {
    case ikea::comparison::equal: return value == threshold;
    case ikea::comparison::less: return value < threshold;
    case ikea::comparison::less_equal: return value <= threshold;
    case ikea::comparison::greater: return value > threshold;
    case ikea::comparison::greater_equal: return value >= threshold;
    }
    std::abort();
}
void answers(const fo::stream_binding &reader, const fixture &f, bool full) {
    const unsigned first = full ? 0 : 4096;
    const auto n = f.values.size() - first;
    std::vector<u64> output(n);
    check(reader.size() == f.values.size() && bool(reader.read(first, output)), "read lost original stream coordinates");
    check(std::equal(output.begin(), output.end(), f.values.begin() + first), "read differs from independent original rows");
    frame3::bitset256 blocks;
    u128 expected = 0;
    for (std::size_t r = first; r < f.values.size(); ++r) {
        expected += f.values[r];
        blocks.set(static_cast<unsigned>(r / 256));
    }
    check(take(reader.sum(blocks)) == expected, "exact sum differs from original rows");
    for (auto op : {ikea::comparison::equal, ikea::comparison::less, ikea::comparison::less_equal,
                    ikea::comparison::greater, ikea::comparison::greater_equal})
        for (u64 threshold : {u64{0}, u64{1000}, u64{1007}, std::numeric_limits<u64>::max()}) {
            u64 count = 0;
            for (std::size_t r = first; r < f.values.size(); ++r) count += test(op, f.values[r], threshold);
            check(take(reader.count(op, blocks, threshold)) == count, "predicate count used unsound evidence");
            frame3::block_rows_set filtered;
            for (auto &slot : filtered) slot.words.fill(0xA5A5A5A5A5A5A5A5);
            auto expected_masks = filtered;
            frame3::each(blocks, [&](unsigned b) { expected_masks[b] = {}; });
            for (std::size_t r = first; r < f.values.size(); ++r)
                if (test(op, f.values[r], threshold)) expected_masks[r / 256].set(static_cast<unsigned>(r % 256));
            check(bool(reader.filter(op, blocks, threshold, filtered)) && filtered == expected_masks,
                "filter changed excluded slots, left high tail bits or disagreed with original rows");
        }
    for (std::size_t r = first; r < f.values.size(); r += 137)
        check(take(reader.get(static_cast<unsigned>(r))) == f.values[r], "point differs from original rows");
    check(take(reader.get(static_cast<unsigned>(f.values.size() - 1))) == f.values.back(), "last partial-row point is wrong");
    frame3::block_rows_set selected;
    std::vector<u64> expected_values;
    u128 selected_sum = 0;
    for (std::size_t r = first; r < f.values.size(); ++r)
        if (r % 3 == 1) {
            selected[r / 256].set(static_cast<unsigned>(r % 256));
            selected_sum += f.values[r];
            expected_values.push_back(f.values[r]);
        }
    check(take(reader.sum_selected(blocks, selected)) == selected_sum, "selected sum differs from original-row mask");
    constexpr u64 sentinel = 0xA1B2C3D4E5F67890;
    std::vector<u64> gathered(expected_values.size() + 4, sentinel);
    const auto count = take(reader.select(blocks, selected, std::span(gathered).subspan(1, expected_values.size() + 2)));
    check(count == expected_values.size() && std::equal(expected_values.begin(), expected_values.end(), gathered.begin() + 1),
        "select changed original row order, count or values");
    check(gathered.front() == sentinel &&
        std::all_of(gathered.begin() + 1 + static_cast<std::ptrdiff_t>(count), gathered.end(),
                    [&](u64 value) { return value == sentinel; }),
        "select overwrote a sentinel or output beyond returned count");
    std::fill(gathered.begin(), gathered.end(), sentinel);
    const auto untouched = gathered;
    refused(reader.select(blocks, selected, std::span(gathered).subspan(1, expected_values.size() - 1)),
        fo::binding_error::resources, "select output shortage did not refuse");
    check(gathered == untouched, "select wrote a prefix before output capacity admission");
    selected[17].set(255);
    refused(reader.sum_selected(blocks, selected), fo::binding_error::range, "out-of-tail fine bit was admitted");
    refused(reader.select(blocks, selected, gathered), fo::binding_error::range, "select admitted an out-of-tail fine bit");
    check(gathered == untouched, "invalid fine mask changed select output");
}
void complete_and_sparse(fo::node_kind form) {
    for (unsigned spacing : {16u, 64u}) {
        auto f = built(spacing, true, true, true, 0, {}, 8, form);
        auto reader = take(bind(f));
        answers(*reader, f, true);
        const auto cp = f.program.nodes[f.program.root].children[1].child;
        const std::array tail{fo::row_range{4096, 271}};
        check(take(reader->required(tail)) == std::vector<fo::section_key>{{cp, 0}, {f.program.root, 1}},
            "root group1 was routed to the wrong checkpoint group");
        const std::array root_only{fo::section_key{f.program.root, 1}};
        auto no_cp = take(bind(f, root_only));
        refused(no_cp->get(4097), fo::binding_error::unavailable, "root-only point read absent checkpoints");
        frame3::bitset256 last;
        last.set(16), last.set(17);
        refused(no_cp->sum(last), fo::binding_error::unavailable, "root-only sum read absent checkpoints");
        refused(no_cp->count(ikea::comparison::greater_equal, last, 1), fo::binding_error::unavailable,
            "payload-dependent count bypassed missing checkpoint admission");
        const std::array cp_only{fo::section_key{cp, 0}};
        auto no_root = take(bind(f, cp_only));
        refused(no_root->get(4097), fo::binding_error::unavailable, "checkpoint-only point read absent deltas");
        const std::array tail_keys{fo::section_key{f.program.root, 1}, fo::section_key{cp, 0}};
        auto tail_reader = take(bind(f, tail_keys));
        answers(*tail_reader, f, false);
        refused(tail_reader->get(0), fo::binding_error::unavailable, "missing root group became a zero constant");
        const std::array head_keys{fo::section_key{f.program.root, 0}, fo::section_key{cp, 0}};
        auto head_reader = take(bind(f, head_keys));
        std::array<u64, 2> output{55, 66};
        refused(head_reader->read(4095, output), fo::binding_error::unavailable,
            "range crossing into an absent group was admitted");
        check(output == std::array<u64, 2>{55, 66}, "range wrote its available prefix before full admission");
        auto mixed = last; mixed.set(0);
        refused(tail_reader->sum(mixed), fo::binding_error::unavailable, "mixed coarse mask silently dropped an absent group");
        copack(f);
        auto shared = take(bind(f, tail_keys));
        answers(*shared, f, false);
        refused(shared->get(3), fo::binding_error::unavailable, "physical co-location silently granted another root section");
        auto shared_root_only = take(bind(f, root_only));
        refused(shared_root_only->get(4097), fo::binding_error::unavailable, "co-location silently granted checkpoint payload");
        // Copied metadata/interpretation survive transient caller containers.
        auto independent = take(bind(f));
        f.program.nodes.clear(); f.streams.clear(); f.extents.clear(); f.keys.clear();
        check(take(independent->get(4097)) == f.values[4097] && take(independent->required(tail)).size() == 2,
            "binding retained transient descriptor or manifest wrappers");
    }
    for (bool bounded_deltas : {false, true})
        for (bool bounded_checkpoints : {false, true}) {
            auto f = built(64, true, bounded_deltas, bounded_checkpoints, 0, {}, 8, form);
            answers(*take(bind(f)), f, true);
        }
    for (bool bounded : {false, true}) {
        auto f = built(16, false, bounded);
        answers(*take(bind(f)), f, true);
    }
}
void evidence(fo::node_kind form) {
    const auto original = built(64, true, true, true, 0, {}, 8, form);
    const fo::section_key root{original.program.root, 0};
    auto bad = original;
    section(bad, root)[8] = std::byte{2};
    refused(bind(bad), fo::binding_error::image, "nonboolean monotone flag reached trusted attach");
    bad = original;
    section(bad, root)[9] = std::byte{2};
    refused(bind(bad), fo::binding_error::image, "nonboolean exact flag reached trusted attach");
    bad = original;
    section(bad, root)[0] ^= std::byte{1};
    refused(bind(bad), fo::binding_error::image, "stored last value was trusted without decode");
    const std::array root_only{root};
    auto unproved = take(bind(bad, root_only));
    refused(unproved->get(3), fo::binding_error::unavailable, "unproved sparse metadata enabled a query");
    bad = original;
    check(section(bad, root)[18] == std::byte{0}, "nonmonotone witness has a true flag");
    section(bad, root)[18] = std::byte{1};
    refused(bind(bad), fo::binding_error::image, "false monotone claim enabled a shortcut");
    bad = original;
    check(section(bad, root)[29] == std::byte{0}, "large unsigned jump witness has a true exact flag");
    section(bad, root)[29] = std::byte{1};
    refused(bind(bad), fo::binding_error::image, "false exact-delta claim was admitted");
    auto conservative = original;
    section(conservative, root)[8] = section(conservative, root)[9] = std::byte{0};
    answers(*take(bind(conservative)), conservative, true);
    auto negative = built(16, true, true, true, 1, {}, 8, form);
    check(section(negative, {negative.program.root, 0})[9] == std::byte{1}, "negative -2^63 boundary witness is not exact");
    answers(*take(bind(negative)), negative, true);
    auto positive = built(16, true, true, true, 2, {}, 8, form);
    check(section(positive, {positive.program.root, 0})[9] == std::byte{0}, "positive +2^63 boundary witness was called exact");
    section(positive, {positive.program.root, 0})[9] = std::byte{1};
    refused(bind(positive), fo::binding_error::image, "positive +2^63 difference was admitted as signed exact");
    if (form == fo::node_kind::intervals_relative_ends) {
        // All adjacent differences fit, but row2 is +2^63 from its checkpoint.
        auto wide = built(16, true, true, true, 3, {}, 8, form);
        check(section(wide, {wide.program.root, 0})[9] == std::byte{0}, "wide relative offset was called exact");
        answers(*take(bind(wide)), wide, true); // False exact uses full logical reconstruction.
        section(wide, {wide.program.root, 0})[9] = std::byte{1};
        refused(bind(wide), fo::binding_error::image, "adjacent differences falsely proved a relative-offset law");
        // A jump between checkpoints does not invalidate exact relative ends.
        for (unsigned spacing : {16u, 64u}) {
            auto jumps = built(spacing, true, true, true, 4, {}, 8, form);
            check(section(jumps, {jumps.program.root, 0})[9] == std::byte{1},
                "checkpoint jump incorrectly disabled exact relative offsets");
            answers(*take(bind(jumps)), jumps, true);
        }
    }
}
void checkpoint_fields(fo::node_kind form) {
    constexpr u64 bias = u64{1} << 63;
    for (bool bounded : {false, true}) {
        auto f = built(64, true, bounded, true, 0, {}, 8, form);
        frame3::column column(take(fo::make_tree(f.program)), f.values);
        std::vector<u64> fields(f.values.size());
        for (std::size_t r = 0; r < fields.size(); ++r) {
            const auto reference = form == fo::node_kind::intervals_relative_ends ? r - r % 64 : r - (r != 0);
            fields[r] = r % 64 == 0 ? bias : (f.values[r] - f.values[reference]) ^ bias;
        }
        fields[64] = bias + 1; // Logical decode ignores this checkpoint field.
        auto &child = *column.root().children()[0];
        child.clear(0);
        child.build(0, {fields});
        std::vector<u64> logical(f.values.size());
        column.read(logical.data());
        check(logical == f.values, "checkpoint poison changed decoded logical values");
        if (form == fo::node_kind::intervals_relative_ends) {
            u128 expected = 0;
            for (unsigned i = 0; i < 256; ++i) expected += f.values[i];
            frame3::bitset256 first; first.set(0);
            check(column.root().sum(0, first) == expected + 1,
                "counterexample did not expose the unchecked SUM-law disagreement");
        }
        const auto cp = f.program.nodes[f.program.root].children[1].child;
        images(f, column, cp);
        const fo::section_key key{f.program.root, 0};
        const auto image = section(f, key);
        check(bool(fo::validate_group(image.subspan(16 * 10), fo::image_preset::compact, bounded,
            f.values.size(), 0)), "checkpoint counterexample is not a structurally valid child image");
        refused(bind(f), fo::binding_error::image, "ignored checkpoint field violated the zero-offset premise");
    }
}
void no_access(fo::node_kind form) {
    auto f = built(64, true, true, true, 0, {}, 8, form);
    const auto cp = f.program.nodes[f.program.root].children[1].child;
    const auto source_size = f.parts[0].size();
    const long page_size = sysconf(_SC_PAGESIZE);
    check(page_size > 0, "page size unavailable");
    const auto page = static_cast<std::size_t>(page_size);
    const auto allocation = ((source_size + page - 1) / page) * page;
    void *guard = mmap(nullptr, allocation, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(guard != MAP_FAILED, "guard allocation failed");
    auto supplied = parts(f);
    supplied[0].bytes = {static_cast<const std::byte *>(guard), source_size};
    const std::array tail_keys{fo::section_key{f.program.root, 1}, fo::section_key{cp, 0}};
    auto tail = take(fo::stream_binding::attach(f.program, f.values.size(), f.extents, f.streams, supplied, tail_keys));
    answers(*tail, f, false); // Excluded root group0 stays inaccessible.
    const std::array bad_keys{fo::section_key{f.program.root, 0}, fo::section_key{65535, 0}};
    refused(fo::stream_binding::attach(f.program, f.values.size(), f.extents, f.streams, supplied, bad_keys),
        fo::binding_error::range, "later malformed key did not precede every payload access");
    auto malformed = f.streams;
    malformed[1].rows = 0;
    refused(fo::stream_binding::attach(f.program, f.values.size(), f.extents, malformed, supplied, f.keys),
        fo::binding_error::image, "malformed complete metadata did not precede payload access");
    check(munmap(guard, allocation) == 0, "guard release failed");
    // The binding borrows no absent section, so its unavailable Part wrapper
    // may disappear without changing the valid tail route.
    answers(*tail, f, false);
    auto missing = parts(f);
    missing.pop_back();
    refused(fo::stream_binding::attach(f.program, f.values.size(), f.extents, f.streams, missing, f.keys),
        fo::binding_error::unavailable, "available key lacking its Part was admitted");
    auto short_part = parts(f);
    short_part[0].bytes = short_part[0].bytes.first(source_size - 1);
    refused(fo::stream_binding::attach(f.program, f.values.size(), f.extents, f.streams, short_part, f.keys),
        fo::binding_error::range, "supplied Part extent mismatch was admitted");
}
void empty(fo::node_kind form) {
    for (bool intervals : {false, true}) {
        fixture f;
        const auto delta = basic(f.program, true);
        f.program.root = delta;
        f.streams.push_back({delta, 0, {}});
        if (intervals) {
            const auto cp = basic(f.program, true);
            f.program.root = static_cast<std::uint16_t>(f.program.nodes.size());
            f.program.nodes.push_back({form, 64, 0,
                                      {{delta, fo::coordinate::identity}, {cp, fo::coordinate::checkpoints}}});
            f.streams[0].root_node = f.program.root;
            f.streams.push_back({cp, 0, {}});
        }
        auto reader = take(bind(f));
        check(reader->size() == 0 && take(reader->sum({})) == 0 &&
            take(reader->count(ikea::comparison::greater_equal, {}, 0)) == 0 && bool(reader->read(0, {})),
            "empty stream queries called an unattached stage");
        refused(reader->get(0), fo::binding_error::range, "point on empty image was admitted");
        frame3::block_rows_set masks;
        check(bool(reader->filter(ikea::comparison::equal, {}, 77, masks)) && take(reader->select({}, masks, {})) == 0,
            "empty filter/select called an unattached stage");
    }
}
void constants(fo::node_kind form) {
    const auto qualify = [](fixture f, u64 value) {
        // Persist/recover the program; the images themselves come from the
        // shared stage's real emit and round-trip through checked attachment.
        f.program = take(fo::decode(take(fo::encode(f.program))));
        const auto reader = take(bind(f));
        answers(*reader, f, true);
        const auto blocks = frame3::first_n(static_cast<unsigned>((f.values.size() + 255) / 256));
        check(take(reader->sum(blocks)) == u128{value} * f.values.size(), "constant SUM differs from hand answer");
        check(take(reader->count(ikea::comparison::equal, blocks, value)) == f.values.size() &&
            take(reader->count(ikea::comparison::greater, blocks, value)) == 0,
            "constant COUNT differs from hand answers");
        check(take(reader->get(0)) == value && take(reader->get(4366)) == value,
            "constant endpoint points differ from hand answers");
        const bool intervals = fo::is_intervals(f.program.nodes[f.program.root].kind);
        for (const auto &stream : f.streams) {
            const bool interval_root = intervals && stream.root_node == f.program.root;
            const auto basic_node = interval_root ? f.program.nodes[f.program.root].children[0].child : stream.root_node;
            const auto recipe = take(fo::binding_recipe(f.program, basic_node));
            for (std::size_t g = 0; g < stream.groups.size(); ++g) {
                const auto n = (std::min(std::size_t{4096}, stream.rows - g * 4096) + 255) / 256;
                const auto image = section(f, {stream.root_node, g});
                const auto manifest = take(fo::validate_group(image.subspan(interval_root ? n * 10 : 0),
                    fo::image_preset::compact, recipe.bounded, stream.rows, static_cast<unsigned>(g)));
                check(std::all_of(manifest.width.begin(), manifest.width.end(), [](auto width) { return width == 0; }) &&
                    std::all_of(manifest.arrays.begin(), manifest.arrays.end(), [](const auto &array) { return array.size == 0; }),
                    "constant fixture failed to exercise K0 with zero leaf body");
                if (recipe.bounded)
                    check(manifest.capacity == 0 && manifest.used == 0,
                        "slack-zero constant fixture did not emit an empty exception area");
            }
        }
    };
    qualify(built(16, false, false, false, 0, u64{0}, 0), 0);
    qualify(built(16, false, true, false, 0, u64{77}, 0), 77);
    for (unsigned spacing : {16u, 64u})
        qualify(built(spacing, true, true, true, 0, u64{77}, 0, form), 77);
}
void domain_without_payload(fo::node_kind form) {
    auto f = built(64, true, true, true, 0, {}, 8, form);
    const long page_size = sysconf(_SC_PAGESIZE);
    check(page_size > 0, "domain guard page size unavailable");
    const auto page = static_cast<std::size_t>(page_size);
    const auto maximum_part = *std::max_element(f.extents.begin(), f.extents.end());
    const auto extent = ((maximum_part + page - 1) / page) * page;
    void *guard = mmap(nullptr, extent, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(guard != MAP_FAILED, "domain guard allocation failed");
    std::vector<fo::borrowed_part> supplied;
    for (std::size_t i = 0; i < f.extents.size(); ++i)
        supplied.push_back({static_cast<std::uint32_t>(i), {static_cast<const std::byte *>(guard), f.extents[i]}});
    const auto reader = take(fo::stream_binding::attach(f.program, f.values.size(), f.extents, f.streams, supplied, {}));
    frame3::bitset256 blocks;
    blocks.set(0), blocks.set(16), blocks.set(17); // 256+256+15 original rows.
    struct known { ikea::comparison op; u64 threshold; bool all; };
    for (auto predicate : {known{ikea::comparison::greater_equal, 0, true},
                           known{ikea::comparison::less_equal, std::numeric_limits<u64>::max(), true},
                           known{ikea::comparison::less, 0, false},
                           known{ikea::comparison::greater, std::numeric_limits<u64>::max(), false}}) {
        check(take(reader->count(predicate.op, blocks, predicate.threshold)) == (predicate.all ? 527 : 0),
            "UInt64 domain COUNT read unavailable payload or returned the wrong extent");
        frame3::block_rows_set out;
        for (auto &slot : out) slot.words.fill(0xDEADBEEFDEADBEEF);
        auto expected = out;
        frame3::each(blocks, [&](unsigned b) { expected[b] = {}; });
        if (predicate.all)
            for (std::size_t r = 0; r < f.values.size(); ++r)
                if (blocks.contains(static_cast<unsigned>(r / 256))) expected[r / 256].set(static_cast<unsigned>(r % 256));
        check(bool(reader->filter(predicate.op, blocks, predicate.threshold, out)) && out == expected,
            "domain FILTER changed excluded slots or partial-tail high bits");
    }
    frame3::block_rows_set out;
    for (auto &slot : out) slot.words.fill(0xDEADBEEFDEADBEEF);
    const auto unchanged = out;
    auto invalid = blocks;
    invalid.set(18);
    refused(reader->count(ikea::comparison::greater_equal, invalid, 0), fo::binding_error::range,
        "domain COUNT bypassed invalid coarse coordinates");
    refused(reader->filter(ikea::comparison::less_equal, invalid, std::numeric_limits<u64>::max(), out),
        fo::binding_error::range, "domain FILTER bypassed invalid coarse coordinates");
    refused(reader->count(static_cast<ikea::comparison>(255), {}, 0), fo::binding_error::range,
        "empty COUNT bypassed invalid comparison");
    refused(reader->filter(static_cast<ikea::comparison>(255), {}, 0, out), fo::binding_error::range,
        "empty FILTER bypassed invalid comparison");
    refused(reader->count(ikea::comparison::equal, blocks, 1000), fo::binding_error::unavailable,
        "data-dependent COUNT silently acquired absent payload");
    refused(reader->filter(ikea::comparison::equal, blocks, 1000, out), fo::binding_error::unavailable,
        "data-dependent FILTER silently acquired absent payload");
    check(out == unchanged, "filter refusal changed output");
    frame3::block_rows_set no_rows;
    check(take(reader->sum_selected(blocks, no_rows)) == 0 && take(reader->select(blocks, no_rows, {})) == 0,
        "empty fine demand accessed unavailable payload");
    no_rows[0].set(0);
    std::array<u64, 2> sentinel{55, 66};
    refused(reader->select(blocks, no_rows, sentinel), fo::binding_error::unavailable,
        "nonempty select skipped complete availability admission");
    check(sentinel == std::array<u64, 2>{55, 66}, "select availability refusal changed output");
    const std::array demand{fo::row_range{0, 1}};
    check(take(reader->required(demand)).size() == 2, "point closure was incorrectly weakened by domain predicate settles");
    check(munmap(guard, extent) == 0, "domain guard release failed");
}

void placeholder_refusal(const fo::description &program) {
    check(bool(fo::validate(program)), "placeholder has invalid description syntax");
    refused(fo::make_tree(program), fo::binding_error::unsupported, "placeholder became executable without an image law");
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    void *guard = mmap(nullptr, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(guard != MAP_FAILED, "placeholder guard allocation failed");
    const std::array extents{std::size_t{1}};
    const std::array streams{fo::stream_manifest{program.root, 257, {{0, 0, 1}}}};
    const std::array supplied{fo::borrowed_part{0, {static_cast<const std::byte *>(guard), 1}}};
    const std::array available{fo::section_key{program.root, 0}};
    refused(fo::stream_binding::attach(program, 257, extents, streams, supplied, available),
        fo::binding_error::unsupported, "placeholder touched bytes before refusing unsupported image binding");
    check(munmap(guard, page) == 0, "placeholder guard release failed");
}

void runs_shape() {
    fo::description d;
    d.nodes = {{fo::node_kind::pack, 0, 0, {}}, {fo::node_kind::pack, 0, 0, {}},
               {fo::node_kind::runs, 0, 0, {{0, fo::coordinate::run_ordinal}, {1, fo::coordinate::run_ordinal}}}};
    d.root = 2;
    d = take(fo::decode(take(fo::encode(d))));
    check(d.nodes[2].children[0].child == 0 && d.nodes[2].children[1].child == 1,
        "run child roles lost their identities in the description");
    for (bool alternating : {false, true}) {
        std::vector<u64> values(257, 77);
        if (alternating)
            for (unsigned r = 0; r < values.size(); ++r) values[r] += r % 2;
        // The existing in-memory prototype witnesses this graph's row law.
        // It has no imported-image law; no substitute encoder is introduced.
        frame3::column column(std::make_unique<frame3::runs>(
            std::make_unique<frame3::pack<ikea::seriespack::preset::compact>>(),
            std::make_unique<frame3::pack<ikea::seriespack::preset::compact>>()), values);
        const auto children = column.root().children();
        const std::size_t run_count = alternating ? 257 : 2;
        check(children.size() == 2 && children[0]->rows(0) == run_count && children[1]->rows(0) == run_count,
            "run cardinality was confused with original rows or a fixed checkpoint ratio");
        std::vector<u64> sizes(run_count), run_values(run_count);
        children[0]->read(0, 0, static_cast<unsigned>(run_count), sizes.data());
        children[1]->read(0, 0, static_cast<unsigned>(run_count), run_values.data());
        if (alternating) {
            check(std::all_of(sizes.begin(), sizes.end(), [](u64 n) { return n == 0; }) && run_values == values,
                "singleton run sizes-minus-one or run ordinal changed");
        } else {
            check(sizes == std::vector<u64>{255, 0} && run_values == std::vector<u64>{77, 77},
                "block-local RLE incorrectly merged equal values across a block boundary");
        }
        std::vector<u64> decoded(values.size());
        column.read(decoded.data());
        check(decoded == values && column.get(255) == values[255] && column.get(256) == values[256],
            "run child ranges lost the parent block-to-run mapping");
        u128 sum = 0;
        for (auto x : values) sum += x;
        check(column.sum() == sum, "run-size multiplication does not reconstruct the original SUM");
    }
    placeholder_refusal(d);
}

template <unsigned K, unsigned H> void headed_leaf_shape() {
    namespace sp = ikea::seriespack;
    using F = sp::preset_layout<K, sp::preset::bulk_x86, H>;
    constexpr unsigned payload = K - H;
    constexpr u64 head_mask = (u64{1} << H) - 1, suffix_mask = (u64{1} << payload) - 1;
    constexpr std::size_t rows = 257, tiles = (rows + F::tile_rows - 1) / F::tile_rows;
    Bytes body(tiles * F::tile_bytes), high(tiles * F::tile_rows), next(H == 16 ? tiles * F::tile_rows : 0);
    auto view = take(sp::view<F>::attach(rows,
        {{{body, F::tile_bytes}, {high, F::tile_rows}, {next, H == 16 ? F::tile_rows : 0}}}));
    std::vector<u64> values(rows);
    for (unsigned r = 0; r < rows; ++r) {
        const u64 head = ((H == 8 ? 0xABu : 0xABCDu) + r * 3) & head_mask;
        values[r] = (head << payload) | ((5 + r * 13) & suffix_mask);
    }
    auto writer = take(sp::bind_writer(view));
    std::vector<ikea::journal::entry> records(take(writer.effect_capacity_initialize()));
    ikea::journal effects{records};
    check(bool(writer.initialize(std::span<const u64>(values), effects)), "headed SeriesPack initialization refused");
    auto immutable = view.as_const();
    auto reader = sp::bind_reader(immutable);
    std::vector<u64> decoded(rows);
    reader.read_unchecked(0, rows, decoded.data());
    check(decoded == values, "existing headed leaf failed exact reconstruction");
    for (unsigned r = 0; r < rows; ++r) {
        check(std::to_integer<unsigned>(high[r]) == (values[r] >> (K - 8)),
            "head byte used a fixed logical shift rather than this block's fitted K");
        if constexpr (H == 16)
            check(std::to_integer<unsigned>(next[r]) == ((values[r] >> (K - 16)) & 255),
                "second head byte lost its fitted-width basis");
    }
    fo::description d;
    d.nodes = {{fo::node_kind::pack, static_cast<std::uint16_t>(fo::pack_preset::bulk_x86), H, {}}};
    const auto recovered = take(fo::decode(take(fo::encode(d))));
    check(recovered.nodes[0].heads == H &&
        (fo::packing_geometry(fo::pack_preset::bulk_x86, payload) == fo::pack_geometry::striped) ==
            (F::storage == sp::geometry::striped), "head option and remaining-payload geometry were conflated");
    placeholder_refusal(recovered);
}
} // namespace

int main() {
    for (auto form : {fo::node_kind::intervals_sizes, fo::node_kind::intervals_relative_ends}) {
        complete_and_sparse(form);
        evidence(form);
        checkpoint_fields(form);
        no_access(form);
        empty(form);
        constants(form);
        domain_without_payload(form);
    }
    runs_shape();
    headed_leaf_shape<12, 8>();
    headed_leaf_shape<20, 8>();
    headed_leaf_shape<20, 16>();
    headed_leaf_shape<32, 16>();
    std::printf("stream_binding_check: %u checks\n", checks);
}

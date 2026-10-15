#include "layout_solver.h"

#include <cstdio>
#include <cstdlib>
#include <charconv>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace ls = ht::layout_solver;

namespace {
void require(bool yes, const char* why) {
    if (!yes) { std::fprintf(stderr, "layout solver: %s\n", why); std::abort(); }
}
template<class T> T take(std::expected<T, ls::error> result) {
    require(bool(result), "unexpected recipe refusal");
    return *result;
}
using id = ls::field_id;

constexpr ls::request list_request{
    .fingerprint_bits = 8, .suffix_bits = 32, .value_bytes = 8,
    .kind = ls::container::multimap, .where = ls::tier::list,
    .count_bytes = 0, .descendant_bytes = 0, .head_bytes = 0,
    .chain_reference_bytes = 4};
constexpr auto list_recipe = ls::make(list_request,
    {.slots = 16, .unit_bytes = 224, .tag = ls::tags::row_byte});
static_assert(list_recipe.has_value());
static_assert(list_recipe->get(id::fingerprints).offset_bits == 0);
static_assert(list_recipe->get(id::suffixes).offset_bits == 16 * 8);
static_assert(list_recipe->get(id::values).offset_bits == 80 * 8);
static_assert(list_recipe->get(id::values).stride_bits == 9 * 8);
static_assert(list_recipe->get(id::tag).offset_bits == 88 * 8);
static_assert(list_recipe->used_bytes == 224);
static_assert(list_recipe->get(id::values).width_bits == 64);

// Validate ownership by expanding every field entry into an independent interval
// list. This catches row/plane overlap and omitted final entries, not merely
// whether make() reports the same total twice.
void ownership(const ls::candidate& c) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> occupied;
    std::uint64_t owned_bits = 0;
    for (unsigned f = 0; f < static_cast<unsigned>(id::end); ++f) {
        auto which = static_cast<id>(f);
        const auto& field = c.get(which);
        require(field.logical_bits <= field.width_bits, "logical bits exceed field storage");
        for (unsigned i = 0; i < field.entries; ++i) {
            const auto at = field.offset_bits + i * field.stride_bits;
            const auto end = at + field.width_bits;
            require(end <= c.used_bytes * 8, "entry outside used unit");
            const auto w = take(ls::field_window(c, which, i));
            require(w.offset + w.bytes <= c.shape.unit_bytes, "entry window overread");
            if (!field.width_bits) { require(w.bytes == 0, "zero field read"); continue; }
            require(w.offset * 8 <= at && (w.offset + w.bytes) * 8 >= end,
                    "physical window does not contain entry");
            require((at / 8 == w.offset) && (end - 1) / 8 == w.offset + w.bytes - 1,
                    "field window is not minimal");
            occupied.emplace_back(at, end);
            owned_bits += field.width_bits;
        }
    }
    std::sort(occupied.begin(), occupied.end());
    for (unsigned i = 1; i < occupied.size(); ++i)
        require(occupied[i - 1].second <= occupied[i].first, "two entries own the same bit");
    require(owned_bits <= c.used_bytes * 8, "owned data exceeds unit");
    require(c.used_bytes + c.padding_bytes == c.shape.unit_bytes, "unaccounted tail bytes");
    if (c.spec.kind == ls::container::set)
        require(!c.get(id::values).present() && !c.get(id::tag).present(), "set retained map payload");
    if (c.spec.kind == ls::container::multimap) {
        require(c.payload_bytes >= c.spec.value_bytes &&
                c.payload_bytes >= c.spec.chain_reference_bytes, "multimap stole inline/reference bits");
        require(c.get(id::tag).present(), "multimap omitted explicit tag");
    }
    for (unsigned group = 0; group < c.selector_groups; ++group) {
        auto w = take(ls::selector_window(c, group));
        require(w.offset >= c.selector_storage.offset &&
                w.offset + w.bytes <= c.selector_storage.offset + c.selector_storage.bytes,
                "selector window escapes initialized selector storage");
        unsigned first = group * c.shape.selector_lanes;
        unsigned last = std::min(c.shape.slots, first + c.shape.selector_lanes);
        for (unsigned slot = first; slot < last; ++slot) {
            auto entry = take(ls::field_window(c, id::fingerprints, slot));
            require(entry.offset >= w.offset && entry.offset + entry.bytes <= w.offset + w.bytes,
                    "selector window omitted an active lane");
        }
    }
}

void lines(const ls::candidate& c, std::span<const ls::window> windows) {
    const auto got = take(ls::line_touches(c, windows));
    std::set<unsigned> phases;
    // Enumerate actual aligned bases and a sufficient number of successive
    // buckets, independently of the implementation's gcd shortcut.
    for (unsigned base = 0; base < c.spec.cacheline_bytes; ++base) {
        if (base % c.spec.allocation_alignment) continue;
        const unsigned buckets = c.spec.where == ls::tier::list ? 1 : c.spec.cacheline_bytes;
        for (unsigned bucket = 0; bucket < buckets; ++bucket)
            phases.insert((base + std::uint64_t(bucket) * c.shape.unit_bytes) % c.spec.cacheline_bytes);
    }
    unsigned low = ~0u, high = 0;
    std::uint64_t total = 0;
    for (unsigned phase : phases) {
        std::set<std::uint64_t> touched;
        for (auto w : windows) for (std::uint64_t i = 0; i < w.bytes; ++i)
            touched.insert((phase + w.offset + i) / c.spec.cacheline_bytes);
        low = std::min(low, static_cast<unsigned>(touched.size()));
        high = std::max(high, static_cast<unsigned>(touched.size()));
        total += touched.size();
    }
    require(got.minimum == low && got.maximum == high && got.sum == total &&
            got.phases == phases.size(), "cacheline union/phase accounting differs from byte oracle");
}

// Raw bit writes deliberately do not use either packing primitive. Each entry
// gets an independently reproducible bit pattern. Adjacent packed entries and
// tags must survive; all unowned bits must retain their initial sentinel.
void storage_roundtrip(const ls::candidate& c) {
    std::vector<unsigned char> storage(c.shape.unit_bytes, 0xa5);
    std::vector<bool> touched(std::size_t(c.shape.unit_bytes) * 8, false);
    auto expected = [](unsigned field, unsigned entry, std::uint64_t bit) {
        const std::uint64_t v = (std::uint64_t(field + 1) * 0x9e3779b97f4a7c15ull) ^
            (std::uint64_t(entry + 3) * 0xd6e8feb86659fd93ull) ^
            (bit * 0x94d049bb133111ebull);
        return bool(((v ^ (v >> 31)) >> (bit % 17)) & 1);
    };
    for (unsigned f = 0; f < static_cast<unsigned>(id::end); ++f) {
        const auto& field = c.get(static_cast<id>(f));
        for (unsigned entry = 0; entry < field.entries; ++entry)
        for (std::uint64_t b = 0; b < field.width_bits; ++b) {
            const auto at = field.offset_bits + entry * field.stride_bits + b;
            require(!touched[at], "bit overwritten by a different field/entry");
            touched[at] = true;
            const unsigned mask = 1u << (at % 8);
            storage[at / 8] = static_cast<unsigned char>((storage[at / 8] & ~mask) |
                                                       (expected(f, entry, b) ? mask : 0));
        }
    }
    for (unsigned f = 0; f < static_cast<unsigned>(id::end); ++f) {
        const auto& field = c.get(static_cast<id>(f));
        for (unsigned entry = 0; entry < field.entries; ++entry)
        for (std::uint64_t b = 0; b < field.width_bits; ++b) {
            const auto at = field.offset_bits + entry * field.stride_bits + b;
            require(bool((storage[at / 8] >> (at % 8)) & 1) == expected(f, entry, b),
                    "another field changed packed entry contents");
        }
    }
    for (std::size_t at = 0; at < touched.size(); ++at) if (!touched[at])
        require(((storage[at / 8] >> (at % 8)) & 1) == ((0xa5 >> (at % 8)) & 1),
                "padding was consumed as data");
}

void boundary_checks() {
    auto r = list_request;
    auto s = list_recipe->shape;
    require(list_recipe->missing(ls::initial_bucket_features) & ls::bit(ls::feature::row_tags),
            "unlowered multimap silently reported executable");
    require(list_recipe->missing(ls::initial_bucket_features) & ls::bit(ls::feature::list),
            "unbound list silently reported executable");
    require(list_recipe->payload_bytes == 8, "inline64 narrowed to reference32");
    ownership(*list_recipe); storage_roundtrip(*list_recipe);
    auto aligned_list = take(ls::make(r, {.slots = 14, .unit_bytes = 186,
        .tag = ls::tags::bit_plane, .value_alignment = 8}));
    require(aligned_list.get(id::values).offset_bits % 64 == 0 && aligned_list.used_bytes == 186,
            "single allocation inherited an unnecessary unit-stride alignment constraint");
    ownership(aligned_list); storage_roundtrip(aligned_list);
    r.value_bytes = 1; r.chain_reference_bytes = 8;
    auto reference_wider = take(ls::make(r, s));
    require(reference_wider.payload_bytes == 8 && reference_wider.get(id::values).logical_bits == 64,
            "reference narrowed to short inline value");

    r = list_request; r.kind = ls::container::set; r.value_bytes = 0;
    s.tag = ls::tags::none; s.slots = 256; s.unit_bytes = 1280;
    auto full = take(ls::make(r, s));
    require(full.used_bytes == 1280, "caller-count list charged hidden header/value");
    ownership(full); storage_roundtrip(full);
    r.count_bytes = 1;
    require(!ls::make(r, s) && ls::make(r, s).error() == ls::error::invalid_header,
            "8-bit count cannot represent occupied capacity256");
    r.count_bytes = 2; s.unit_bytes = 1282;
    require(bool(ls::make(r, s)), "16-bit count refused capacity256");
    r.occupancy = 257;
    require(ls::make(r, s).error() == ls::error::occupancy_exceeds_capacity, "occupancy ignored");
    r.occupancy = 0;
    require(bool(ls::make(r, s)), "empty occupancy rejected");

    r = {}; s = {};
    require(take(ls::make(r, s)).missing(ls::initial_bucket_features) == 0,
            "original physical grammar changed capabilities");
    r.fingerprint_bits = 9; require(ls::make(r, s).error() == ls::error::invalid_width, "fp9 accepted");
    r = {}; r.suffix_bits = 65;
    require(ls::make(r, s).error() == ls::error::invalid_width, "suffix65 accepted");
    r = {}; r.value_bytes = ~0u;
    require(ls::make(r, s).error() == ls::error::insufficient_unit, "large payload arithmetic wrapped");
    r = {}; r.kind = ls::container::set;
    require(ls::make(r, s).error() == ls::error::invalid_kind, "set values silently discarded");
    r = {}; r.where = ls::tier::list;
    require(ls::make(r, s).error() == ls::error::invalid_header, "list retained descendants");
    r = {}; r.where = ls::tier::secondary;
    require(ls::make(r, s).error() == ls::error::invalid_header, "secondary omitted head");
    r = {}; s.slots = 0;
    require(ls::make(r, s).error() == ls::error::invalid_capacity, "zero capacity accepted");
    s = {}; s.slots = 257;
    require(ls::make(r, s).error() == ls::error::invalid_capacity, "capacity257 accepted");
    s = {}; s.selector_lanes = 32;
    require(ls::make(r, s).error() == ls::error::invalid_selector, "NEON native32 implied");
    r.fingerprint_bits = 1; s.fp = ls::fingerprint::packed_bits; s.selector_lanes = 128;
    s.slots = 128; s.unit_bytes = 2048;
    auto dense_selector = take(ls::make(r, s));
    require(take(ls::selector_window(dense_selector, 0)).bytes == 16 &&
            dense_selector.missing(ls::initial_bucket_features) != 0,
            "packed selector density or unsupported-lowering status lost");
    ownership(dense_selector);
    s.fp = ls::fingerprint::exact_bytes;
    require(ls::make(r, s).error() == ls::error::invalid_selector, "byte selector inherited bit density");
    r = {};
    s = {}; s.unit_bytes = 197; s.value_alignment = 8;
    require(ls::make(r, s).error() == ls::error::invalid_alignment, "stride broke claimed alignment");
    s = {}; r.fingerprint_bits = 0; r.suffix_bits = 0; r.value_bytes = 0;
    auto empty_fields = take(ls::make(r, s));
    require(empty_fields.used_bytes == 5 && empty_fields.selector_groups == 0,
            "zero-width fields retained storage");
    require(take(ls::field_window(empty_fields, id::suffixes, 15)).bytes == 0,
            "zero suffix requested load");
    require(!ls::selector_window(empty_fields, 0), "absent selector produced window");
    require(!ls::field_window(empty_fields, id::suffixes, 16), "inactive slot accepted");
    const std::array bad{ls::window{255, 2}};
    require(!ls::line_touches(empty_fields, bad), "out-of-unit window accepted");
    const std::array<ls::window, 17> too_many{};
    require(!ls::line_touches(empty_fields, too_many), "window limit silently truncated");
}

void phase_checks() {
    for (unsigned unit : {48u, 64u, 96u, 128u, 192u, 256u})
    for (unsigned alignment : {1u, 8u, 16u, 32u, 64u, 128u})
    for (unsigned line : {32u, 64u, 128u}) {
        auto r = list_request;
        r.kind = ls::container::set; r.value_bytes = 0;
        r.where = ls::tier::primary; r.count_bytes = 1; r.descendant_bytes = 4;
        r.allocation_alignment = alignment; r.cacheline_bytes = line;
        auto c = take(ls::make(r, {.slots = 1, .unit_bytes = unit}));
        const std::array whole{ls::window{0, unit}};
        const std::array disjoint{ls::window{0, 1}, ls::window{unit - 1, 1}, ls::window{0, 1}};
        const std::array overlap{ls::window{0, unit / 2}, ls::window{unit / 3, unit / 2}, ls::window{unit, 0}};
        lines(c, whole); lines(c, disjoint); lines(c, overlap);
        if (unit == 96 && alignment == 64 && line == 64)
            require(c.unit_lines.minimum == 2 && c.unit_lines.maximum == 2,
                    "96-byte tiling lost its actual two-line bound");
        if (unit == 96 && alignment == 1 && line == 64)
            require(c.unit_lines.maximum == 3, "unjustified 96-byte two-line promise");
    }
    auto r = list_request; r.kind = ls::container::set; r.value_bytes = 0;
    auto list = take(ls::make(r, {.slots = 1, .unit_bytes = 48}));
    require(list.unit_lines.minimum == 1 && list.unit_lines.maximum == 1 && list.unit_lines.phases == 1,
            "standalone aligned list inherited repeated-bucket phases");
    const std::array window{ls::window{0, 48}};
    lines(list, window);
}

unsigned dimensional_checks() {
    unsigned checks = 0;
    // Every fp width, suffix width, kind and platform, with all requested value
    // families. Tiers and encoding/body choices cross these axes too. Capacities
    // rotate through boundary cases; the focused checks above cover count256.
    for (unsigned fp = 0; fp <= 8; ++fp) for (unsigned suffix = 0; suffix <= 64; ++suffix)
    for (auto kind : {ls::container::set, ls::container::map, ls::container::multimap})
    for (unsigned value : {0u, 1u, 8u, 16u, 24u, 192u}) {
        if (kind == ls::container::set && value) continue;
        for (auto target : {ls::platform::neon, ls::platform::avx2_amd, ls::platform::avx2_intel,
                            ls::platform::avx512_amd, ls::platform::avx512_intel})
        for (auto tier : {ls::tier::list, ls::tier::primary, ls::tier::secondary})
        for (unsigned grammar = 0; grammar < 3; ++grammar) {
            constexpr std::array sizes{1u, 8u, 14u, 16u, 32u, 256u};
            const unsigned n = sizes[(fp + suffix + value + grammar) % sizes.size()];
            ls::request r{.fingerprint_bits = fp, .suffix_bits = suffix, .value_bytes = value,
                          .kind = kind, .where = tier, .target = target,
                          .count_bytes = tier == ls::tier::list ? 0u : 2u,
                          .descendant_bytes = tier == ls::tier::list ? 0u : 4u,
                          .head_bytes = tier == ls::tier::secondary ? 4u : 0u,
                          .chain_reference_bytes = 8, .occupancy = n};
            ls::choice s{.slots = n, .unit_bytes = 65536,
                         .fp = static_cast<ls::fingerprint>(grammar),
                         .suffix_encoding = grammar == 2 ? ls::suffix::packed_bits : ls::suffix::bytes,
                         .fields = grammar == 1 ? ls::body::rows : ls::body::planes,
                         .tag = kind != ls::container::multimap ? ls::tags::none :
                             grammar == 0 ? ls::tags::row_byte :
                             grammar == 1 ? ls::tags::byte_plane : ls::tags::bit_plane,
                         .selector_lanes = target == ls::platform::neon ? 16u :
                             target == ls::platform::avx2_amd || target == ls::platform::avx2_intel ? 32u : 64u,
                         .value_alignment = grammar == 1 ? 8u : 1u};
            auto c = take(ls::make(r, s));
            ownership(c);
            // Exact-fit acceptance and one-byte-short refusal validate physical
            // admission independently of capacity/shape enumeration order.
            s.unit_bytes = static_cast<unsigned>(c.used_bytes);
            if (!s.unit_bytes) s.unit_bytes = 1;
            if (s.unit_bytes % s.value_alignment == 0) {
                require(bool(ls::make(r, s)), "exact physical fit rejected");
                if (c.used_bytes > 1 && s.value_alignment == 1) {
                    --s.unit_bytes;
                    require(ls::make(r, s).error() == ls::error::insufficient_unit,
                            "one-byte-short unit admitted");
                }
            }
            ++checks;
        }
    }
    return checks;
}

unsigned enumeration_checks() {
    auto r = list_request;
    r.target = ls::platform::avx512_intel;
    ls::limits l{.capacities = {1, 14, 16, 256}, .units = {48, 64, 96, 128, 192, 256, 4096},
                 .alignments = {1, 8}, .fingerprint_alignments = {1, 16},
                 .selector_groups = {8, 16, 32, 64}};
    auto candidates = take(ls::enumerate(r, l));
    require(!candidates.empty(), "empty frontier for supported geometry");
    bool packed_fp = false, packed_suffix = false, rows = false, bits = false, row_tag = false,
         wide_selector = false, capacity256 = false;
    for (const auto& c : candidates) {
        ownership(c);
        packed_fp |= c.shape.fp == ls::fingerprint::packed_bits;
        packed_suffix |= c.shape.suffix_encoding == ls::suffix::packed_bits;
        rows |= c.shape.fields == ls::body::rows;
        bits |= c.shape.tag == ls::tags::bit_plane;
        row_tag |= c.shape.tag == ls::tags::row_byte;
        wide_selector |= c.shape.selector_lanes == 64;
        capacity256 |= c.shape.slots == 256;
        require(c.missing(ls::initial_bucket_features) != 0, "list/MM missing-lowering admission disappeared");
    }
    require(packed_fp && packed_suffix && rows && bits && row_tag && wide_selector && capacity256,
            "frontier silently dropped an unlowered dimension");
    l.max_candidates = 1;
    require(ls::enumerate(r, l).error() == ls::error::candidate_limit, "frontier silently truncated");
    r.fingerprint_bits = 9;
    require(ls::enumerate(r, l).error() == ls::error::invalid_width, "invalid request swallowed by enumeration");
    return static_cast<unsigned>(candidates.size());
}

int dump(int argc, char** argv) {
    if (argc != 8 && argc != 9) {
        std::fputs("usage: solver_check --dump set|map|multimap list|primary|secondary FP_BITS SUFFIX_BITS VALUE_BYTES neon|avx2-amd|avx2-intel|avx512-amd|avx512-intel [OCCUPANCY]\n"
                   "Unranked physical frontier. Dump defaults: caller-owned list count; 2-byte bucket count (admits256), 4-byte descendants/secondary head, 8-byte multimap reference, 64-byte allocation alignment. Use the C++ request API for other header/reference domains.\n", stderr);
        return 2;
    }
    auto number = [](std::string_view text) -> std::optional<unsigned> {
        unsigned value = 0;
        auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
        return value;
    };
    ls::request r;
    const std::string_view kind = argv[2], tier = argv[3], target = argv[7];
    if (kind == "set") r.kind = ls::container::set;
    else if (kind == "map") r.kind = ls::container::map;
    else if (kind == "multimap") r.kind = ls::container::multimap;
    else { std::fputs("invalid container kind\n", stderr); return 2; }
    if (tier == "list") r.where = ls::tier::list;
    else if (tier == "primary") r.where = ls::tier::primary;
    else if (tier == "secondary") r.where = ls::tier::secondary;
    else { std::fputs("invalid tier\n", stderr); return 2; }
    if (target == "neon") r.target = ls::platform::neon;
    else if (target == "avx2-amd") r.target = ls::platform::avx2_amd;
    else if (target == "avx2-intel") r.target = ls::platform::avx2_intel;
    else if (target == "avx512-amd") r.target = ls::platform::avx512_amd;
    else if (target == "avx512-intel") r.target = ls::platform::avx512_intel;
    else { std::fputs("invalid platform\n", stderr); return 2; }
    auto fp = number(argv[4]), suffix = number(argv[5]), value = number(argv[6]);
    if (!fp || !suffix || !value) { std::fputs("invalid numeric width\n", stderr); return 2; }
    r.fingerprint_bits = *fp; r.suffix_bits = *suffix; r.value_bytes = *value;
    r.count_bytes = r.where == ls::tier::list ? 0 : 2;
    r.descendant_bytes = r.where == ls::tier::list ? 0 : 4;
    r.head_bytes = r.where == ls::tier::secondary ? 4 : 0;
    if (argc == 9) {
        r.occupancy = number(argv[8]);
        if (!r.occupancy) { std::fputs("invalid occupancy\n", stderr); return 2; }
    }
    auto frontier = ls::enumerate(r);
    if (!frontier) {
        std::fprintf(stderr, "frontier refused; error enum %u\n", static_cast<unsigned>(frontier.error()));
        return 2;
    }
    std::cout << "# Unranked structural recipes; missing_initial_features is a lowering requirement, not measured speed.\n"
              << "# fp: 0=byte_tiles 1=exact_bytes 2=packed_bits; suffix: 0=bytes 1=packed_bits; body: 0=planes 1=rows; tag: 0=none 1=byte_plane 2=bit_plane 3=row_byte.\n"
              << "# kind=" << kind << " tier=" << tier << " platform=" << target
              << " fp_bits=" << *fp << " suffix_bits=" << *suffix << " value_bytes=" << *value
              << " count_bytes=" << r.count_bytes << " descendants=" << r.descendant_bytes
              << " head=" << r.head_bytes << " reference_bytes=" << r.chain_reference_bytes
              << " occupancy=" << (r.occupancy ? std::to_string(*r.occupancy) : "unknown") << '\n'
              << "candidate,slots,unit_bytes,fp,suffix,body,tag,selector_lanes,fp_alignment,value_alignment,used_bytes,tail_padding_bytes,logical_bits,selector_groups,unit_lines_min,unit_lines_max,phase_sum,phases,missing_initial_features";
    for (auto name : {"count", "descendants", "head", "fingerprints", "suffixes", "values", "tag"})
        std::cout << ',' << name << "_offset_bits," << name << "_stride_bits," << name << "_width_bits";
    std::cout << '\n';
    for (std::size_t i = 0; i < frontier->size(); ++i) {
        const auto& c = (*frontier)[i]; const auto& s = c.shape;
        std::cout << i << ',' << s.slots << ',' << s.unit_bytes << ',' << static_cast<unsigned>(s.fp)
                  << ',' << static_cast<unsigned>(s.suffix_encoding) << ',' << static_cast<unsigned>(s.fields)
                  << ',' << static_cast<unsigned>(s.tag) << ',' << s.selector_lanes << ',' << s.fingerprint_alignment
                  << ',' << s.value_alignment << ',' << c.used_bytes << ',' << c.padding_bytes << ',' << c.logical_bits
                  << ',' << c.selector_groups << ',' << c.unit_lines.minimum << ',' << c.unit_lines.maximum
                  << ',' << c.unit_lines.sum << ',' << c.unit_lines.phases << ',' << c.missing(ls::initial_bucket_features);
        for (const auto& f : c.fields)
            std::cout << ',' << f.offset_bits << ',' << f.stride_bits << ',' << f.width_bits;
        std::cout << '\n';
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        if (std::string_view(argv[1]) == "--dump") return dump(argc, argv);
        std::fputs("unknown argument (run without arguments for checks, or use --dump)\n", stderr); return 2;
    }
    boundary_checks(); phase_checks();
    const auto dimensions = dimensional_checks();
    const auto frontier = enumeration_checks();
    // Packed, row, full-width and tag alternatives receive actual overlapping
    // byte writes in addition to the full interval-ownership grid.
    for (unsigned suffix : {0u, 1u, 7u, 8u, 9u, 24u, 63u, 64u})
    for (auto tag : {ls::tags::byte_plane, ls::tags::bit_plane, ls::tags::row_byte})
    for (auto body : {ls::body::planes, ls::body::rows}) {
        auto r = list_request; r.fingerprint_bits = 3; r.suffix_bits = suffix;
        r.value_bytes = 16; r.chain_reference_bytes = 4;
        auto c = take(ls::make(r, {.slots = 14, .unit_bytes = 512,
            .fp = ls::fingerprint::packed_bits, .suffix_encoding = ls::suffix::packed_bits,
            .fields = body, .tag = tag}));
        storage_roundtrip(c);
        const std::array windows{take(ls::field_window(c, id::values, 13)),
                                take(ls::field_window(c, id::tag, 13)),
                                take(ls::field_window(c, id::suffixes, 13)),
                                take(ls::selector_window(c, 1))};
        lines(c, windows);
    }
    std::printf("layout solver: %u dimensional recipes, %u frontier candidates; ownership, windows, phase unions and refusal checks passed\n",
                dimensions, frontier);
}

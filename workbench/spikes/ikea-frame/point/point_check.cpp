// Frame point semantics; independent of the isolated Intervals investigation.
#include "point_read.h"
#include "compact_read.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>
#include <string_view>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {
namespace fe = frame_encoding;
using frame_points::point_reader;
void need(bool ok, const char *why) {
    if (!ok) throw std::runtime_error(why);
}

std::vector<fe::u64> authored() {
    std::vector<fe::u64> data(6 * fe::block_rows);
    std::mt19937 random(713);
    fe::u64 increasing = 1000, decreasing = UINT64_MAX - 1000;
    for (unsigned i = 0; i < fe::block_rows; ++i) {
        data[i] = 999;
        data[256 + i] = (i / 16 % 3) * 100000;
        data[512 + i] = random() % 256;
        data[768 + i] = increasing += 5 + random() % 3;
        data[1024 + i] = decreasing -= 5 + random() % 3;
        data[1280 + i] = random() % 256;
    }
    data[1280 + 17] = 1ull << 60;
    data[1280 + 199] = 1ull << 61;
    return data;
}

unsigned verify(const std::vector<fe::u64> &values, bool require_kinds,
                bool delta_exceptions = false,
                fe::dictionary_order dictionary = fe::dictionary_order::none,
                bool every_part = false) {
    unsigned checks = 0;
    std::vector<std::size_t> rows(values.size());
    std::iota(rows.begin(), rows.end(), 0);
    std::vector<fe::column> retained;
    retained.reserve(4);
    for (auto mode : {fe::checkpoints::native, fe::checkpoints::packed,
                      fe::checkpoints::every64, fe::checkpoints::none})
        retained.push_back(fe::encode(values, {.delta_checkpoints = mode,
            .delta_exceptions = delta_exceptions, .dictionary = dictionary,
            .dictionary_every_part = every_part}));
    std::vector<std::unique_ptr<frame_points::compact_image>> lean;
    for (const auto &column : retained)
        lean.push_back(std::make_unique<frame_points::compact_image>(
            fe::encode(values, column.options)));
    // Construct every image before reading any: options and bytes must
    // remain attached to each retained image, not a process-global switch.
    for (unsigned image = 0; image < retained.size(); ++image) {
        const auto &column = retained[image];
        point_reader stored(column, frame_points::point_domain::stored_codes);
        frame_points::compact_reader lean_stored(*lean[image], false, frame_points::point_domain::stored_codes);
        frame_points::compact_reader prefix_stored(*lean[image], true, frame_points::point_domain::stored_codes);
        frame_points::dictionary_points reader(column, stored);
        frame_points::dictionary_points lean_reader(lean[image]->body, lean_stored);
        frame_points::dictionary_points prefix_reader(lean[image]->body, prefix_stored);
        std::set<fe::kind> kinds;
        for (const auto &d : column.blocks) kinds.insert(d.type);
        if (require_kinds)
            for (auto required : {fe::kind::constant, fe::kind::packed, fe::kind::runs,
                                  fe::kind::delta, fe::kind::patched})
                need(kinds.contains(required), "authored fixture omitted a required kind");
        for (std::size_t row = 0; row < values.size(); ++row) {
            need(reader.get(row) == values[row], "point differs from original input");
            need(lean_reader.get(row) == values[row], "shared-kernel point differs from input");
            need(prefix_reader.get(row) == values[row], "native prefix point differs from input");
            checks += 3;
        }
        for (unsigned pattern = 0; pattern < 4; ++pattern) {
            fe::block_mask mask(column.blocks.size());
            for (std::size_t b = 0; b < mask.blocks; ++b)
                if (pattern == 0 || (pattern == 2 && b % 2) ||
                    (pattern == 3 && b / 16 % 2)) mask.set(b);
            fe::u64 expected = 0;
            std::size_t expected_count = 0;
            for (auto row : rows)
                if ((mask.words[row / 256 / 64] >> (row / 256 % 64)) & 1) {
                    expected += values[row];
                    ++expected_count;
                }
            const auto selected = [&](const auto &candidate) {
                fe::u64 actual = 0;
                std::size_t count = 0;
                frame_points::selected_points(candidate, mask, rows, [&](auto row, auto value) {
                    need(value == values[row], "selected point changed original coordinates");
                    actual += value; ++count;
                });
                need(actual == expected && count == expected_count, "coarse point selection mismatch");
            };
            selected(reader); selected(lean_reader); selected(prefix_reader);
        }
    }
    return checks;
}

unsigned mixed_editions() {
    std::vector<fe::u64> values(258 * 256);
    const std::array<fe::u64, 4> first{0, 17, (fe::u64{1} << 63) + 13, UINT64_MAX};
    const std::array<fe::u64, 4> next{9, 500, (fe::u64{1} << 63) - 7, UINT64_MAX - 100};
    for (std::size_t r = 0; r < values.size(); ++r)
        values[r] = r / 256 == 0 ? 7 : r < 65536 ? first[r % 4] : next[r % 4];
    unsigned checks = 0;
    for (auto order : {fe::dictionary_order::sorted, fe::dictionary_order::frequency}) {
        const auto column = fe::encode(values, {.dictionary = order});
        need(column.coded_blocks() && column.coded_blocks() < column.blocks.size(),
             "mixed dictionary fixture lost its raw/coded distinction");
        need(column.parts.size() == 2 && column.parts[0].count && column.parts[1].count,
             "mixed dictionary fixture lost its distinct part editions");
        checks += verify(values, false, true, order);
    }
    return checks;
}

unsigned shared_kernel_widths() {
    std::mt19937_64 rng(713);
    unsigned checks = 0;
    for (unsigned k = 1; k <= 64; ++k) {
        std::array<fe::u64, 256> expected;
        const auto max = UINT64_MAX >> (64 - k);
        for (auto &v : expected) v = rng() & max;
        expected[0] = 0; expected[255] = max;
        std::vector<std::uint8_t> packed(fe::plane_bytes(k, expected.size()));
        fe::pack(k, expected, packed.data());
        const auto &ops = frame_points::local_kernels(k);
        for (auto first : {0u, 16u, 128u, 240u})
            for (auto take : {0u, 1u, 2u, 7u, 8u, 15u, 16u, 17u, 31u, 32u, 63u, 64u, 128u, 256u}) {
                if (first + take > 256) continue;
                fe::u64 sum = 0;
                for (unsigned r = 0; r < take; ++r) sum += expected[first + r];
                need(ops.prefixes[0](packed.data(), first, take) == sum &&
                         ops.prefixes[1](packed.data(), first, take) == sum,
                     "native prefix differs from original modular sum");
                ++checks;
            }
        const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        need(packed.size() <= page, "native prefix fixture exceeds page");
        auto *mapping = static_cast<std::uint8_t *>(mmap(nullptr, 2 * page,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        need(mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0,
             "native prefix guard mapping failed");
        auto *edge = mapping + page - packed.size();
        std::memcpy(edge, packed.data(), packed.size());
        for (unsigned take = 1; take <= 16; ++take) {
            fe::u64 sum = 0;
            for (unsigned r = 0; r < take; ++r) sum += expected[240 + r];
            need(ops.prefixes[0](edge, 240, take) == sum && ops.prefixes[1](edge, 240, take) == sum,
                 "prefix exceeded final body grant");
            ++checks;
        }
        need(mprotect(mapping, page, PROT_NONE) == 0, "prefix none protection failed");
        need(ops.prefixes[0](edge, 240, 0) == 0 && ops.prefixes[1](edge, 240, 0) == 0,
             "empty prefix read source");
        need(munmap(mapping, 2 * page) == 0, "prefix unmap failed");
        for (unsigned r = 0; r < 256; ++r) {
            need(ops.point(packed.data(), r) == expected[r], "shared point width differs");
            ++checks;
        }
        for (auto first : {0u, 1u, 7u, 8u, 15u, 16u, 127u, 239u, 255u})
            for (auto take : {0u, 1u, 7u, 8u, 15u, 16u, 17u, 31u, 32u, 128u, 256u}) {
                if (first + take > 256) continue;
                std::array<fe::u64, 258> result;
                result.fill(0x6a31da87cb9e120fULL);
                ops.range(packed.data(), first, take, result.data() + 1);
                need(result[0] == 0x6a31da87cb9e120fULL &&
                         result[take + 1] == 0x6a31da87cb9e120fULL,
                     "shared range wrote outside its output grant");
                for (unsigned r = 0; r < take; ++r) {
                    need(result[r + 1] == expected[first + r], "shared range width differs");
                    ++checks;
                }
            }
    }
    return checks;
}

// Valid deliberately nonminimal K63 prediction: stored residual checkpoint sums
// wrap u64, while every reconstructed value and signed step remains legal.
fe::column wide_prediction(fe::checkpoints mode) {
    fe::column c;
    c.options.delta_checkpoints = mode;
    c.rows = 256;
    c.blocks.push_back({fe::u64{1} << 62, 0, 256, fe::kind::delta, 63});
    c.payload.resize(fe::plane_bytes(63, 256) + 8 + fe::checkpoint_cost(mode, 63));
    std::array<fe::u64, 256> residuals{};
    for (unsigned r = 1; r < 256; r += 2) residuals[r] = (fe::u64{1} << 63) - 1;
    fe::pack(63, residuals, c.payload.data());
    const fe::i64 rbase = -(fe::i64{1} << 62);
    auto *tail = c.payload.data() + fe::plane_bytes(63, 256);
    std::memcpy(tail, &rbase, 8);
    const unsigned spacing = mode == fe::checkpoints::every64 ? 64 : 16;
    fe::u64 sum = 0;
    for (unsigned r = 0; r < 256; ++r) {
        sum += residuals[r];
        if (mode != fe::checkpoints::none && (r + 1) % spacing == 0 && r < 255)
            std::memcpy(tail + 8 + ((r + 1) / spacing - 1) * 8, &sum, 8);
    }
    auto decoder = fe::sp::bind_decoder<fe::u64>(fe::sp::dense_input{
        63, fe::sp::geometry::local, std::span(c.payload.data(), fe::plane_bytes(63, 256)), 256});
    need(decoder.has_value(), "wide predictor binding failed");
    c.decoders.push_back(*decoder);
    return c;
}
unsigned wide_checkpoints() {
    unsigned checks = 0;
    for (auto mode : {fe::checkpoints::native, fe::checkpoints::packed,
                      fe::checkpoints::every64, fe::checkpoints::none}) {
        const auto c = wide_prediction(mode);
        point_reader reader(c);
        const frame_points::compact_image lean(wide_prediction(mode));
        frame_points::compact_reader compact(lean);
        frame_points::compact_reader native(lean, true);
        for (unsigned r = 0; r < 256; ++r) {
            const fe::u64 expected = r % 2 ? (fe::u64{1} << 63) - 1 - r / 2
                                                : (fe::u64{1} << 62) - r / 2;
            need(reader.get(r) == expected, "wide cumulative eager reconstruction differs");
            need(compact.get(r) == expected, "wide cumulative shared reconstruction differs");
            need(native.get(r) == expected, "wide cumulative native reconstruction differs");
            checks += 3;
        }
    }
    return checks;
}

template <class Reader> void protected_reader(Reader reader, std::size_t blocks) {
    const auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    void *inaccessible = mmap(nullptr, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    need(inaccessible != MAP_FAILED, "guard mapping failed");
    reader.source = static_cast<decltype(reader.source)>(inaccessible);
    fe::block_mask mask(blocks);
    const std::array<std::size_t, 6> rows{0, 255, 256, 768, 1024, 1535};
    unsigned calls = 0;
    frame_points::selected_points(reader, mask, rows, [&](auto, auto) { ++calls; });
    need(calls == 0, "none mask emitted a point");
    const pid_t child = fork();
    need(child >= 0, "fork failed");
    if (!child) {
        rlimit no_core{0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        mask.set(0); // Deliberately select an inaccessible descriptor.
        frame_points::selected_points(reader, mask, rows,
                                           [](auto, auto value) { fe::sink = value; });
        _exit(0);
    }
    int status;
    need(waitpid(child, &status, 0) == child, "wait failed");
    need(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS),
         "injected descriptor read did not fault");
    munmap(inaccessible, page_size);
}
void protected_none() {
    const auto values = authored();
    const auto column = fe::encode(values);
    protected_reader(point_reader(column), column.blocks.size());
    const frame_points::compact_image compact(fe::encode(values));
    protected_reader(frame_points::compact_reader(compact), compact.body.blocks.size());
    protected_reader(frame_points::compact_reader(compact, true), compact.body.blocks.size());
}
} // namespace

int main(int argc, char **argv) {
    try {
        bool delta_exceptions = false;
        auto dictionary = fe::dictionary_order::none;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--delta-exceptions") delta_exceptions = true;
            if (arg.starts_with("--dictionary=")) {
                need(arg == "--dictionary=sorted" || arg == "--dictionary=frequency",
                     "dictionary must be sorted or frequency");
                dictionary = arg == "--dictionary=sorted" ? fe::dictionary_order::sorted
                                                         : fe::dictionary_order::frequency;
            }
        }
        unsigned checks = verify(authored(), true) + shared_kernel_widths() + wide_checkpoints() + mixed_editions();
        if (delta_exceptions) checks += verify(authored(), false, true);
        if (dictionary != fe::dictionary_order::none)
            checks += verify(authored(), false, delta_exceptions, dictionary, true);
        protected_none();
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--delta-exceptions") continue;
            if (std::string_view(argv[i]).starts_with("--dictionary=")) continue;
            std::ifstream input(argv[i], std::ios::binary | std::ios::ate);
            need(bool(input), "cannot open input");
            const auto bytes = input.tellg();
            need(bytes > 0 && bytes % (256 * sizeof(fe::u64)) == 0,
                 "input must contain complete nonempty u64 blocks");
            std::vector<fe::u64> values(static_cast<std::size_t>(bytes) / sizeof(fe::u64));
            input.seekg(0);
            need(bool(input.read(reinterpret_cast<char *>(values.data()), bytes)), "short input read");
            checks += verify(values, false, delta_exceptions, dictionary);
        }
        std::printf("%u exact point reads passed for eager/shared/native-prefix preparation; "
                    "authored five kinds and %s, four per-image checkpoint modes, "
                    "immutable run ends, mixed dictionary editions (%s corpus), coarse masks; "
                    "protected none and fault injection passed.\n", checks,
                    delta_exceptions ? "opt-in delta exceptions" : "default encoding",
                    fe::dictionary_name(dictionary));
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

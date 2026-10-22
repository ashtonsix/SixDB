#include "delta_repair.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <random>

namespace fl = frame_lifecycle;
namespace fe = frame_encoding;
namespace {
void need(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::abort(); }
}
struct fixture { fe::column body; std::array<fe::u64, 256> original; };
template <unsigned K> fixture make(fe::checkpoints mode, bool monotone = false) {
    fixture f;
    f.body.rows = 256;
    f.body.options.delta_checkpoints = mode;
    constexpr fe::u64 maximum = K == 64 ? UINT64_MAX : (fe::u64{1} << K) - 1;
    const fe::i64 rbase = monotone ? 0 : -fe::i64(maximum / 2);
    const auto anchor = maximum / 2 + 128;
    // Poisoned siblings and an unaligned body exercise owner-relative effects.
    constexpr unsigned body_offset = 13;
    f.body.blocks.push_back({anchor, body_offset, 256, fe::kind::delta, K});
    f.body.payload.resize(body_offset + fe::plane_bytes(K, 256) + 8 +
                          fe::checkpoint_cost(mode, K) + 17, 0xa5);
    f.body.d8.resize(1); f.body.d16.resize(1); f.body.d32.resize(1);
    std::array<fe::u64, 256> residuals{};
    std::vector<fe::u64> marks;
    const unsigned spacing = mode == fe::checkpoints::every64 ? 64 : 16;
    fe::u64 sum = 0;
    f.original[0] = anchor;
    for (unsigned i = 1; i < 256; ++i) {
        residuals[i] = i % 2 ? 0 : monotone ? 1 : maximum;
        const fl::i128 x = fl::i128(f.original[i - 1]) + rbase + fl::i128(residuals[i]);
        need(x >= 0 && x <= fl::i128(UINT64_MAX), "fixture overflowed logical domain");
        f.original[i] = fe::u64(x);
        sum += residuals[i];
        if (mode != fe::checkpoints::none && (i + 1) % spacing == 0 && i != 255)
            marks.push_back(sum);
    }
    auto *data = f.body.payload.data() + body_offset;
    fe::pack(K, residuals, data);
    const auto offset = fe::plane_bytes(K, 256);
    std::memcpy(data + offset, &rbase, 8);
    if (mode == fe::checkpoints::packed && K + 8 <= 64) fe::pack(K + 8, marks, data + offset + 8);
    else {
        const unsigned bytes = K + 8 <= 32 ? 4 : 8;
        for (unsigned i = 0; i < marks.size(); ++i)
            std::memcpy(data + offset + 8 + i * bytes, &marks[i], bytes);
    }
    const auto decoder = fe::sp::bind_decoder<fe::u64>(fe::sp::dense_input{
        K, fe::sp::geometry::local, std::span(data, offset), 256});
    need(bool(decoder), "fixture binding failed");
    f.body.decoders.push_back(*decoder);
    return f;
}
void check(const fixture &f, const frame_points::point_reader &reader) {
    fe::u128 expected = 0;
    for (unsigned r = 0; r < 256; ++r) {
        need(reader.get(r) == f.original[r], "point or a neighbour changed");
        expected += f.original[r];
    }
    fe::block_mask all(1); all.set(0);
    need(fe::encoded_sum(f.body, all) == expected, "exact SUM differs after repair");
    for (auto threshold : {fe::u64{0}, f.original[15], f.original[16], f.original[17],
                           f.original[63], f.original[64], f.original[65],
                           f.original[239], f.original[240], f.original[254], UINT64_MAX}) {
        fe::u64 expected_count = 0;
        for (auto x : f.original) expected_count += x >= threshold;
        need(fe::encoded_count_ge(f.body, all, threshold) == expected_count, "COUNT differs after repair");
    }
}
template <unsigned K> unsigned exercise() {
    unsigned applied = 0;
    for (bool monotone : {false, true})
    for (auto mode : {fe::checkpoints::native, fe::checkpoints::packed,
                      fe::checkpoints::every64, fe::checkpoints::none}) {
        auto f = make<K>(mode, monotone);
        frame_points::point_reader reader(f.body);
        check(f, reader);
        for (unsigned row = 1; row < 256; ++row) {
            for (bool restore : {false, true}) {
                const auto before = f.body.payload;
                const auto old = f.original[row];
                const bool increase = bool(row % 2) != restore;
                const auto value = increase ? old + 1 : old - 1;
                const auto result = fl::repair_delta<K>(f.body, reader, row, value, true);
                need(result.state == fl::repair_result::applied, "fitting repair refused");
                need(result.logical_change == (increase ? 1 : -1), "logical delta differs");
                const unsigned spacing = mode == fe::checkpoints::every64 ? 64 : 16;
                const auto expected_marks = mode != fe::checkpoints::none && row != 255 &&
                                             (row + 1) % spacing == 0;
                need(result.checkpoints == unsigned(expected_marks), "checkpoint repair widened");
                need(result.residuals == (row == 255 ? 1u : 2u), "wrong residual repair count");
                std::vector<bool> covered(before.size());
                for (unsigned i = 0; i < result.count; ++i) {
                    const auto d = result.writes[i];
                    need(d.offset >= 13 && d.offset <= before.size() - 17 &&
                         d.size <= before.size() - 17 - d.offset,
                         "effect escaped the actual encoded body");
                    for (auto j = d.offset; j < d.offset + d.size; ++j) covered[j] = true;
                }
                for (unsigned i = 0; i < before.size(); ++i)
                    need(before[i] == f.body.payload[i] || covered[i], "changed byte missing from effects");
                f.original[row] = value;
                check(f, reader);
                ++applied;
            }
            const auto before = f.body.payload;
            // Moving an odd row down would make its zero residual negative;
            // moving an even row up would exceed K. Refusal preserves bytes.
            const auto impossible = row % 2 ? f.original[row] - 1 : f.original[row] + 1;
            const auto result = fl::repair_delta<K>(f.body, reader, row, impossible, true);
            need(result.state == fl::repair_result::refused && !result.count &&
                 f.body.payload == before, "refusal mutated source/effects");
        }
        const auto before = f.body.payload;
        need(fl::repair_delta<K>(f.body, reader, 15, f.original[15] + 1, true, 0).state ==
             fl::repair_result::refused && f.body.payload == before, "effect-capacity failure mutated");
        need(fl::repair_delta<K>(f.body, reader, 0, 7, true).state == fl::repair_result::refused,
             "anchor change silently treated as an interior edit");
    }
    return applied;
}
void protected_skip() {
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    void *memory = mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    need(memory != MAP_FAILED && sizeof(fe::column) <= page, "protected fixture allocation failed");
    auto f = make<13>(fe::checkpoints::native);
    auto *column = std::construct_at(static_cast<fe::column *>(memory), std::move(f.body));
    frame_points::point_reader reader(*column);
    need(!mprotect(memory, page, PROT_NONE), "protect failed");
    need(fl::repair_delta<13>(*column, reader, 15, 500, false).state == fl::repair_result::skipped,
         "coarse skip read its input");
    const auto child = fork(); need(child >= 0, "fork failed");
    if (!child) { (void)fl::repair_delta<13>(*column, reader, 15, 500, true); _exit(0); }
    int status; need(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
                     (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS),
                     "positive access control failed to fault");
    need(!mprotect(memory, page, PROT_READ | PROT_WRITE), "restore failed");
    std::destroy_at(column); munmap(memory, page);
}
}
int main() {
    unsigned count = 0;
    count += exercise<1>(); count += exercise<8>(); count += exercise<13>(); count += exercise<24>();
    count += exercise<32>(); count += exercise<56>(); count += exercise<63>(); count += exercise<64>();
    protected_skip();
    std::printf("%u fitting delta replacements pass: 8 widths,4 CP modes, monotone/zigzag, every interior/last row,"
                " inverse updates, exact points/SUM/COUNT, checkpoint locality, physical effects,"
                " refusal/zero-capacity atomicity and protected coarse skip.\n", count);
}

// Frame point semantics; independent of the isolated Intervals investigation.
#include "point_read.h"
#include <cstdio>
#include <fstream>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>
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

unsigned verify(const std::vector<fe::u64> &values, bool require_kinds) {
    unsigned checks = 0;
    std::vector<std::size_t> rows(values.size());
    std::iota(rows.begin(), rows.end(), 0);
    for (bool compact : {false, true}) {
        std::vector<fe::column> retained;
        retained.reserve(4);
        for (auto mode : {fe::checkpoints::native, fe::checkpoints::packed,
                          fe::checkpoints::every64, fe::checkpoints::none})
            retained.push_back(fe::encode(values, {compact, mode}));
        // Construct every image before reading any: options and bytes must
        // remain attached to each retained image, not a process-global switch.
        for (const auto &column : retained) {
            point_reader reader(column);
            std::set<fe::kind> kinds;
            for (const auto &d : column.blocks) kinds.insert(d.type);
            if (require_kinds)
                for (auto required : {fe::kind::constant, fe::kind::packed, fe::kind::runs,
                                      fe::kind::delta, fe::kind::patched})
                    need(kinds.contains(required), "authored fixture omitted a required kind");
            for (std::size_t row = 0; row < values.size(); ++row) {
                need(reader.get(row) == values[row], "point differs from original input");
                ++checks;
            }
            for (unsigned pattern = 0; pattern < 4; ++pattern) {
                fe::block_mask mask(column.blocks.size());
                for (std::size_t b = 0; b < mask.blocks; ++b)
                    if (pattern == 0 || (pattern == 2 && b % 2) ||
                        (pattern == 3 && b / 16 % 2)) mask.set(b);
                fe::u64 expected = 0, actual = 0;
                std::size_t expected_count = 0, count = 0;
                for (auto row : rows)
                    if ((mask.words[row / 256 / 64] >> (row / 256 % 64)) & 1) {
                        expected += values[row];
                        ++expected_count;
                    }
                frame_points::selected_points(reader, mask, rows, [&](auto row, auto value) {
                    need(value == values[row], "selected point changed original coordinates");
                    actual += value;
                    ++count;
                });
                need(actual == expected && count == expected_count, "coarse point selection mismatch");
            }
        }
    }
    return checks;
}

void protected_none() {
    const auto values = authored();
    const auto column = fe::encode(values);
    point_reader reader(column);
    const auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    void *inaccessible = mmap(nullptr, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    need(inaccessible != MAP_FAILED, "guard mapping failed");
    reader.source = static_cast<const fe::column *>(inaccessible);
    fe::block_mask mask(column.blocks.size());
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
} // namespace

int main(int argc, char **argv) {
    try {
        unsigned checks = verify(authored(), true);
        protected_none();
        for (int i = 1; i < argc; ++i) {
            std::ifstream input(argv[i], std::ios::binary | std::ios::ate);
            need(bool(input), "cannot open input");
            const auto bytes = input.tellg();
            need(bytes > 0 && bytes % (256 * sizeof(fe::u64)) == 0,
                 "input must contain complete nonempty u64 blocks");
            std::vector<fe::u64> values(static_cast<std::size_t>(bytes) / sizeof(fe::u64));
            input.seekg(0);
            need(bool(input.read(reinterpret_cast<char *>(values.data()), bytes)), "short input read");
            checks += verify(values, false);
        }
        std::printf("%u exact point reads passed; all five kinds, four per-image checkpoint modes, "
                    "two run geometries, coarse masks; protected none and fault injection passed.\n", checks);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

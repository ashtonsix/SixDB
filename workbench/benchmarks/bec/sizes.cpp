// Logical storage per 4096-position group: Bec4096 against sixteen Bec256
// bodies with their per-block directory. Counts encoded bytes; no timing.
// Usage: bec4096_sizes [WINDOWS_DIR]  (8 KiB RealRoaring windows, 16 groups each)
#include "../../../ikea/test/bec/bec4096_reference.h"
#include <ikea/bec/author/bec4096.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace ikea::bec4096;
using ikea::bitset4096;
namespace inputs = bec4096_inputs;
struct totals {
    double groups = 0, b4096 = 0, head = 0, table = 0, streams = 0, b256 = 0, terminal = 0;
};
void add(totals &t, const bitset4096 &x) {
    detail::plan plan;
    detail::prepare(reinterpret_cast<const std::uint8_t *>(x.words.data()), plan);
    t.groups += 1;
    t.b4096 += (plan.bits + 7) / 8;
    unsigned table = 0, streams = 0;
    for (unsigned k = 0; k < 16; ++k) {
        table += k < 15 ? detail::length_width[plan.population[k]] : 0;
        streams += plan.length[k];
        const auto block = x.block(k);
        auto body = ikea::bec256::encoding::make(block, block.count());
        t.b256 += body->bytes();
        t.terminal += plan.population[k] == 0 || plan.population[k] == 256;
    }
    t.table += table / 8.0;
    t.streams += streams / 8.0;
    t.head += (plan.bits - table - streams) / 8.0;
}
void print(const std::string &label, const totals &t) {
    const double g = t.groups;
    // Directory models per group: Bec256 direct4 is 16 x 4 B; packed is 16 x
    // (population-code 8 + length 6 bits) + one 4 B checkpoint. Bec4096 needs
    // population 13 + length 10 bits per group: 3 B packed, or an 8 B word.
    const double b256 = t.b256 / g, b4096 = t.b4096 / g;
    std::printf("%-28s %6.1f%% %8.1f %8.1f %8.1f %8.1f | %8.1f %8.1f %8.1f | %6.1f %6.1f %6.1f\n",
                label.c_str(), 100.0 * t.terminal / (16 * g), b256, b256 + 64, b256 + 32, b4096,
                b4096 + 8, b4096 + 3, b4096 / b256, t.head / g, t.table / g, t.streams / g);
}
} // namespace

int main(int argc, char **argv) {
    std::printf("%-28s %7s %8s %8s %8s %8s | %8s %8s %8s | %6s %6s %6s\n", "input (bytes per 4096)",
                "term", "256body", "+direct", "+packed", "4096body", "+word", "+packed", "ratio",
                "head", "table", "blocks");
    for (auto s : inputs::all) {
        totals t;
        for (unsigned seed = 0; seed < 64; ++seed)
            add(t, inputs::make(s, seed));
        print(inputs::name(s), t);
    }
    if (argc < 2)
        return 0;
    std::vector<std::filesystem::path> files;
    for (auto &entry : std::filesystem::directory_iterator(argv[1]))
        if (entry.path().extension() == ".windows")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    totals all;
    for (auto &path : files) {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
        totals t;
        for (std::size_t at = 0; at + 512 <= data.size(); at += 512) {
            bitset4096 x;
            std::memcpy(x.words.data(), data.data() + at, 512);
            add(t, x);
            add(all, x);
        }
        print(path.stem().string(), t);
    }
    print("all windows", all);
}

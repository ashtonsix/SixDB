#include "common.h"
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace screen {
options parse(int argc, char** argv) {
    options out;
    std::set<std::string_view> seen;
    for (int i = 1; i < argc; i += 2) {
        const std::string_view flag = argv[i];
        if (i + 1 == argc || !seen.insert(flag).second)
            throw std::invalid_argument("Every option needs one value and may occur once");
        const std::string value = argv[i + 1];
        if (flag == "--operation")
            out.operation = value;
        else if (flag == "--owner")
            out.owner = value;
        else if (flag == "--cpu")
            out.cpu = value;
        else if (flag == "--isa")
            out.isa = value;
        else {
            std::size_t used = 0;
            if (value.empty() || value[0] == '-')
                throw std::invalid_argument("Unsigned option value");
            const auto number = std::stoull(value, &used);
            if (used != value.size())
                throw std::invalid_argument("Incomplete numeric option");
            auto narrow = [&] {
                if (number > std::numeric_limits<unsigned>::max())
                    throw std::out_of_range("Option exceeds unsigned range");
                return unsigned(number);
            };
            if (flag == "--source-k")
                out.source_k = narrow();
            else if (flag == "--source-b")
                out.source_b = narrow();
            else if (flag == "--inputs")
                out.inputs = narrow();
            else if (flag == "--seed")
                out.seed = number;
            else if (flag == "--rep")
                out.rep = narrow();
            else if (flag == "--repetitions")
                out.repetitions = narrow();
            else if (flag == "--requests")
                out.requests = number;
            else if (flag == "--conditioning-bytes")
                out.conditioning_bytes = number;
            else if (flag == "--miss-percent")
                out.miss_percent = narrow();
            else if (flag == "--occupancy-percent")
                out.occupancy_percent = narrow();
            else if (flag == "--new-percent")
                out.new_percent = narrow();
            else if (flag == "--overlap-percent")
                out.overlap_percent = narrow();
            else
                throw std::invalid_argument("Unknown hashtable benchmark option");
        }
    }
    if (!out.repetitions || !out.requests || out.miss_percent > 100 || !out.occupancy_percent ||
        out.occupancy_percent > 100 || out.new_percent > 100 || out.overlap_percent > 100)
        throw std::invalid_argument("Nonempty workload and bounded percentages required");
    return out;
}
[[gnu::noinline]] void condition(std::span<const byte> memory) {
    word sum = 0;
    const volatile byte* data = memory.data();
    for (std::size_t i = 0; i < memory.size(); i += 64)
        sum += std::to_integer<unsigned>(data[i]);
    asm volatile("" : "+r"(sum) : : "memory");
}
double elapsed(clock::time_point start) {
    return std::chrono::duration<double, std::nano>(clock::now() - start).count();
}
} // namespace screen

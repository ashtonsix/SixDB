#include "measure_order.h"
#include <array>
#include <cstdio>
#include <cstdlib>

int main() {
    std::mt19937 random(1729);
    unsigned checks = 0;
    for (unsigned n = 1; n <= 32; ++n) {
        const auto rows = frame_measure::balanced_round(n, random);
        std::array<std::array<unsigned, 32>, 32> positions{}, follows{};
        for (const auto& row : rows) {
            std::array<bool, 32> seen{};
            for (unsigned i = 0; i < n; ++i) {
                if (row[i] >= n || seen[row[i]]) std::abort();
                seen[row[i]] = true;
                ++positions[row[i]][i];
                if (i) ++follows[row[i - 1]][row[i]];
            }
        }
        const unsigned expected = n % 2 ? 2 : 1;
        for (unsigned a = 0; a < n; ++a) for (unsigned b = 0; b < n; ++b) {
            if (positions[a][b] != expected || follows[a][b] != (a == b ? 0 : expected))
                std::abort();
            ++checks;
        }
    }
    std::printf("%u arm-position/predecessor checks pass for 1..32 arms.\n", checks);
}

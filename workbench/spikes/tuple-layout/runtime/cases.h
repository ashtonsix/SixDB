#pragma once
#include "model.h"
#include <algorithm>
#include <string>
#include <vector>

namespace tuple_runtime {
struct case_spec {
    std::string name;
    unsigned bytes, stride;
    std::vector<code> codes;
    mapping map;
    schema physical() const { return {bytes, codes}; }
};
inline mapping empty_map() { mapping m; m.fill(255); return m; }
inline std::vector<case_spec> cases() {
    std::vector<case_spec> result;
    for (unsigned order = 0; order < 3; ++order) {
        case_spec c{"bytes64_" + std::to_string(order), 64, 64, {}, empty_map()};
        for (unsigned i = 0; i < 64; ++i) {
            c.codes.push_back({byte(i), 0, 8});
            c.map[i] = order == 0 ? i : order == 1 ? ((i / 16) * 16 + 15 - i % 16) : 63 - i;
        }
        result.push_back(c);
    }
    for (unsigned order = 0; order < 3; ++order) {
        case_spec c{"motif16_" + std::to_string(order), 16, 16, {}, empty_map()};
        for (unsigned i = 0; i < 16; ++i) {
            c.codes.push_back({byte(i), 0, 1});
            c.codes.push_back({byte(i), 1, 4});
            c.codes.push_back({byte(i), 5, 3});
        }
        for (unsigned i = 0; i < (order == 2 ? 16u : 48u); ++i)
            c.map[i] = order == 0 ? i : order == 1 ? (i % 16) * 3 + i / 16 : i * 3;
        result.push_back(c);
    }
    constexpr std::array<unsigned, 8> w{1, 7, 2, 6, 3, 5, 4, 4};
    for (unsigned layout = 0; layout < 4; ++layout) {
        for (unsigned order = 0; order < 2; ++order) {
            case_spec c{"stress_" + std::to_string(layout) + "_" + std::to_string(order),
                        layout == 3 ? 60u : 48u, layout == 3 ? 60u : 48u, {}, empty_map()};
            for (unsigned j = 0; j < 12; ++j) {
                for (unsigned k = 0; k < 8; ++k) {
                    unsigned offset, shift;
                    if (layout < 3) {
                        offset = layout == 2 ? 12 * (k / 2) + j : 4 * j + k / 2;
                        shift = layout == 1 ? (k % 2 ? 0 : w[k + 1]) : (k % 2 ? w[k - 1] : 0);
                    } else {
                        constexpr std::array<unsigned, 8> o{0, 2, 0, 3, 0, 4, 1, 1};
                        constexpr std::array<unsigned, 8> s{0, 0, 1, 0, 3, 0, 0, 4};
                        offset = 5 * j + o[k]; shift = s[k];
                    }
                    c.codes.push_back({byte(offset), byte(shift), byte(w[k])});
                }
                for (unsigned k = 0; k < 4; ++k) c.map[j * 4 + k] = j * 8 + 2 * k;
            }
            if (order) std::reverse(c.map.begin(), c.map.begin() + 48);
            result.push_back(c);
        }
    }
    result.push_back({"tiny_dense", 2, 16, {{0,0,1},{0,1,7},{1,0,3},{1,3,5}}, empty_map()});
    result.push_back({"tiny_cluster", 3, 16, {{0,0,1},{1,0,7},{0,1,3},{2,0,5}}, empty_map()});
    for (unsigned i = result.size() - 2; i < result.size(); ++i) {
        result[i].map[0] = 0; result[i].map[1] = 2;
        result[i].map[2] = 1; result[i].map[3] = 3;
    }
    return result;
}
} // namespace tuple_runtime

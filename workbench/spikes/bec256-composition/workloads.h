#pragma once
#include "consumer.h"
#include <string>
struct workload {
    std::string label;
    std::vector<std::unique_ptr<bec_study::bitset>> inputs;
    std::vector<std::vector<bec_study::bc::byte>> query;
};
extern std::vector<workload> workloads;
void register_whole_bitset_cases();

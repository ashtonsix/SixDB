#include "reference.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>

static int check(const char* path, bool structured) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return 2;
    std::array<std::uint8_t, 43> record{};
    const auto length = structured ? 43 : 41;
    unsigned cases = 0;
    while (input.read(reinterpret_cast<char*>(record.data()), length)) {
        std::uint64_t expected_features = 0;
        for (unsigned i = 0; i != 8; ++i) expected_features |= std::uint64_t{record[32 + i]} << (8 * i);
        const auto actual_features = structured ? ikea::bec_predictor::reference_features_all(record.data()) :
                                                 ikea::bec_predictor::reference_features(record.data());
        const auto actual_size = ikea::bec_predictor::predict_size(actual_features);
        if (actual_features != expected_features || actual_size != record[40] ||
            (structured && (ikea::bec_predictor::predict_size_transitions(actual_features) != record[41] ||
                            ikea::bec_predictor::predict_size_quadrants(actual_features) != record[42]))) {
            std::cerr << "Cross-language predictor mismatch at case " << cases << '\n';
            return 1;
        }
        ++cases;
    }
    if (input.gcount() || !input.eof() || cases == 0) return 2;
    std::cout << (structured ? "structured_cases," : "basic_cases,") << cases << '\n';
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (const auto error = check(argv[1], false)) return error;
    if (const auto error = check(argv[2], true)) return error;
    std::cout << "status,pass\n";
}

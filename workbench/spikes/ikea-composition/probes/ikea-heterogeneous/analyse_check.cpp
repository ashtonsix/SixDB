#include "analyse.h"
#include "../ikea-blocks/codec.h"
#include "../ikea-blocks/composition/native_features.h"
#include "../ikea-blocks/predictor/reference.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>

namespace ikea::heterogeneous {
namespace {
using Tile = std::array<std::uint8_t, 32>;
using Window = std::array<std::uint8_t, analyse_plain_bytes>;
constexpr std::array<unsigned, 32> sample_indices{
    3, 8, 21, 26, 39, 44, 49, 62, 67, 72, 85, 90, 103, 108, 113, 126,
    131, 136, 149, 154, 167, 172, 177, 190, 195, 200, 213, 218, 231, 236, 241, 254};

std::uint64_t mix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

Tile population_tile(unsigned population, unsigned multiplier, unsigned start) {
    Tile tile{};
    for(unsigned i = 0; i != population; ++i) {
        const unsigned position = (i * multiplier + start) & 255;
        tile[position / 8] |= 1u << (position % 8);
    }
    return tile;
}

bool check_features(const Tile& tile) {
    const auto cheap = bec_predictor::reference_features(tile.data());
    const auto quadrants = bec_predictor::reference_features_all(tile.data());
    if(composition::features_from_bytes(tile.data()) != cheap ||
       composition::features_with_quadrants(tile.data()) != quadrants)
        return false;
    std::array<std::uint8_t, 64> pair{};
    std::copy(tile.begin(), tile.end(), pair.begin());
    for(unsigned i = 32; i != 64; ++i) pair[i] = std::uint8_t(mix(i));
    const auto features = composition::features_from_64_bytes(pair.data());
    return features.first == cheap &&
           features.second == bec_predictor::reference_features(pair.data() + 32);
}

unsigned oracle_predict(const Window& plain, AnalyseModel model, AnalyseScan scan) {
    const auto one = [&](unsigned tile) {
        const auto* p = plain.data() + tile * 32;
        return model == AnalyseModel::cheap
            ? bec_predictor::predict_size(bec_predictor::reference_features(p))
            : bec_predictor::predict_size_quadrants(bec_predictor::reference_features_all(p));
    };
    unsigned bytes = 0;
    if(scan == AnalyseScan::full) {
        for(unsigned tile = 0; tile != analyse_tiles; ++tile) bytes += one(tile);
        return bytes;
    }
    for(auto tile : sample_indices) bytes += one(tile);
    return bytes * 8;
}

bool check_window(const Window& plain) {
    for(auto model : {AnalyseModel::cheap, AnalyseModel::quadrants})
        for(auto scan : {AnalyseScan::full, AnalyseScan::sample32})
            if(predict_body_bytes(plain.data(), model, scan) != oracle_predict(plain, model, scan))
                return false;
    // Exactly the public writable extent, followed by a sentinel. Per-tile
    // native overstores may overlap the next body, which must repair those bytes.
    auto native = std::make_unique<std::uint8_t[]>(analyse_encode_writable_bytes + 32);
    std::fill_n(native.get(), analyse_encode_writable_bytes + 32, 0xa5);
    const unsigned size = encode_body_bytes(plain.data(), native.get());
    std::array<std::uint8_t, 47> encoded{};
    unsigned reference_size = 0;
    for(unsigned tile = 0; tile != analyse_tiles; ++tile) {
        const unsigned n = (ikea_probe::encode_reference(plain.data() + tile * 32, encoded.data()) + 7) / 8;
        if(reference_size + n > size ||
           std::memcmp(native.get() + reference_size, encoded.data(), n) != 0)
            return false;
        reference_size += n;
    }
    return size == reference_size && size <= analyse_max_body_bytes &&
           std::all_of(native.get() + analyse_encode_writable_bytes,
                       native.get() + analyse_encode_writable_bytes + 32,
                       [](auto byte) { return byte == 0xa5; });
}

bool check_guards(const Window& plain) {
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    const auto readable = (analyse_encode_writable_bytes + page - 1) / page * page;
    const auto span = readable + 2 * page;
    auto* mapping = static_cast<std::uint8_t*>(mmap(nullptr, span, PROT_NONE,
                                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if(mapping == MAP_FAILED) return false;
    if(mprotect(mapping + page, readable, PROT_READ | PROT_WRITE) != 0) {
        munmap(mapping, span);
        return false;
    }
    auto* output = mapping + page + readable - analyse_encode_writable_bytes;
    const unsigned size = encode_body_bytes(plain.data(), output);
    // Reuse the allocation for an exact input ending at the protected page.
    auto* input = mapping + page + readable - analyse_plain_bytes;
    std::memcpy(input, plain.data(), analyse_plain_bytes);
    bool ok = size <= analyse_max_body_bytes;
    for(auto model : {AnalyseModel::cheap, AnalyseModel::quadrants})
        for(auto scan : {AnalyseScan::full, AnalyseScan::sample32})
            ok &= predict_body_bytes(input, model, scan) == oracle_predict(plain, model, scan);
    munmap(mapping, span);
    return ok;
}
} // namespace

int check_analyser() {
    unsigned feature_cases = 0, windows = 0;
    for(unsigned s = 0; s != sample_indices.size(); ++s)
        if(analyse_sample_tile(s) != sample_indices[s] || sample_indices[s] / 8 != s)
            return 1;
    for(unsigned population = 0; population != 257; ++population)
        for(unsigned pattern = 0; pattern != 3; ++pattern) {
            const auto tile = population_tile(population, pattern == 2 ? 157 : 1, pattern ? 37 : 0);
            if(!check_features(tile)) return 2;
            ++feature_cases;
        }
    for(unsigned byte = 0; byte != 256; ++byte) {
        Tile tile;
        tile.fill(byte);
        if(!check_features(tile)) return 3;
        ++feature_cases;
    }
    Window plain{};
    for(unsigned pattern = 0; pattern != 14; ++pattern) {
        for(unsigned tile = 0; tile != analyse_tiles; ++tile) {
            Tile value{};
            if(pattern == 1) value.fill(255);
            else if(pattern >= 2 && pattern <= 4)
                value = population_tile((tile + pattern) % 257, pattern == 4 ? 157 : 1, pattern == 3 ? 37 : 0);
            else if(pattern == 5) value.fill(std::uint8_t(tile));
            else if(pattern == 6 || pattern == 7) {
                const bool sampled = std::binary_search(sample_indices.begin(), sample_indices.end(), tile);
                if(sampled == (pattern == 6)) value.fill(0x55);
            } else if(pattern == 8) {
                if(tile < 128) value.fill(0x55);
            } else if(pattern == 9) {
                for(unsigned byte = 0; byte != 32; ++byte) value[byte] = byte < 16 ? 255 : 0;
            } else if(pattern == 10) {
                for(unsigned byte = 0; byte != 32; ++byte) value[byte] = byte % 2 ? 255 : 0;
            } else if(pattern >= 11) {
                for(unsigned byte = 0; byte != 32; ++byte)
                    value[byte] = std::uint8_t(mix(tile * 32 + byte + std::uint64_t(pattern) * 8192));
            }
            std::copy(value.begin(), value.end(), plain.begin() + tile * 32);
        }
        if(!check_window(plain)) return 4;
        if(pattern == 6 || pattern == 7)
            for(auto model : {AnalyseModel::cheap, AnalyseModel::quadrants}) {
                const auto full = predict_body_bytes(plain.data(), model, AnalyseScan::full);
                const auto sample = predict_body_bytes(plain.data(), model, AnalyseScan::sample32);
                if(pattern == 6 ? sample != 8 * full || full == 0 : sample != 0 || full == 0)
                    return 5;
            }
        ++windows;
    }
    if(!check_guards(plain)) return 6;
    std::printf("analyser_features=%u windows=%u; frozen rounded models, dense native bytes, "
                "sampling over/underestimation and exact extents passed\n", feature_cases, windows);
    return 0;
}
} // namespace ikea::heterogeneous
